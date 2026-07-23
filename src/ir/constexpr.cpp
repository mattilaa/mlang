#include "ir.h"
#include "ir/common.h"

#include <optional>

using mlang::ir_detail::isFloatInferKind;
using mlang::ir_detail::isIntegerInferKind;
using mlang::ir_detail::normalizeInferredKind;

llvm::Constant* CodeGenerator::buildLLVMConstantFromConstexprValue(
    const ConstexprValue& value, TypeNode* targetType, int line)
{
    if(value.kind == ConstexprValue::Kind::OpaqueStruct)
    {
        reportError(line,
                    "struct values in cexpr are only supported for "
                    "generic type dispatch with type_id(T); struct fields "
                    "and struct constants are not compile-time evaluable yet");
        return nullptr;
    }
    if(value.kind == ConstexprValue::Kind::Type)
    {
        reportError(line,
                    "type_id values in cexpr can only be used in "
                    "compile-time comparisons");
        return nullptr;
    }

    TypeNode::TypeKind kind =
        targetType ? normalizeInferredKind(targetType->kind) : value.typeKind;
    if(isFloatInferKind(kind))
    {
        const double floatValue =
            value.kind == ConstexprValue::Kind::Float
                ? value.floatValue
                : (value.kind == ConstexprValue::Kind::Bool
                       ? (value.boolValue ? 1.0 : 0.0)
                       : static_cast<double>(value.intValue));
        if(kind == TypeNode::TYPE_FLOAT)
            return llvm::ConstantFP::get(llvm::Type::getFloatTy(context),
                                         floatValue);
        return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context),
                                     floatValue);
    }
    if(kind == TypeNode::TYPE_BOOL)
    {
        const bool boolValue = value.kind == ConstexprValue::Kind::Bool
                                   ? value.boolValue
                                   : value.kind == ConstexprValue::Kind::Float
                                         ? value.floatValue != 0.0
                                   : (value.intValue != 0);
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context),
                                      boolValue ? 1 : 0, false);
    }

    llvm::Type* llvmType = getLLVMType(kind);
    if(!llvmType || !llvmType->isIntegerTy())
    {
        reportError(line,
                    "cexpr currently supports only integer, float, and bool "
                    "values");
        return nullptr;
    }
    const int64_t intValue =
        value.kind == ConstexprValue::Kind::Bool
            ? (value.boolValue ? 1 : 0)
            : value.kind == ConstexprValue::Kind::Float
                  ? static_cast<int64_t>(value.floatValue)
                  : value.intValue;
    return llvm::ConstantInt::get(llvmType, intValue, !isUnsignedType(kind));
}

bool CodeGenerator::coerceConstexprValueToKind(ConstexprValue& value,
                                               TypeNode::TypeKind targetKind,
                                               std::string* errorMessage,
                                               const char* context)
{
    targetKind = normalizeInferredKind(targetKind);
    if(value.kind == ConstexprValue::Kind::OpaqueStruct)
    {
        if(errorMessage)
        {
            *errorMessage =
                "struct values in " + std::string(context) +
                " are only supported for generic type dispatch with "
                "type_id(T); struct fields and struct constants are not "
                "compile-time evaluable yet";
        }
        return false;
    }
    if(value.kind == ConstexprValue::Kind::Type)
    {
        if(errorMessage)
        {
            *errorMessage =
                "type_id values in " + std::string(context) +
                " can only be used in compile-time comparisons";
        }
        return false;
    }
    if(targetKind == TypeNode::TYPE_BOOL)
    {
        const bool boolValue =
            value.kind == ConstexprValue::Kind::Bool
                ? value.boolValue
                : value.kind == ConstexprValue::Kind::Float
                      ? value.floatValue != 0.0
                      : value.intValue != 0;
        value.kind = ConstexprValue::Kind::Bool;
        value.boolValue = boolValue;
        value.intValue = boolValue ? 1 : 0;
        value.floatValue = boolValue ? 1.0 : 0.0;
        value.typeKind = TypeNode::TYPE_BOOL;
        return true;
    }
    if(isFloatInferKind(targetKind))
    {
        const double floatValue =
            value.kind == ConstexprValue::Kind::Float
                ? value.floatValue
                : value.kind == ConstexprValue::Kind::Bool
                      ? (value.boolValue ? 1.0 : 0.0)
                      : static_cast<double>(value.intValue);
        value.kind = ConstexprValue::Kind::Float;
        value.floatValue = floatValue;
        value.boolValue = floatValue != 0.0;
        value.intValue = static_cast<int64_t>(floatValue);
        value.typeKind = targetKind;
        return true;
    }
    if(isIntegerInferKind(targetKind))
    {
        const int64_t intValue =
            value.kind == ConstexprValue::Kind::Bool
                ? (value.boolValue ? 1 : 0)
                : value.kind == ConstexprValue::Kind::Float
                      ? static_cast<int64_t>(value.floatValue)
                      : value.intValue;
        value.kind = ConstexprValue::Kind::Int;
        value.intValue = intValue;
        value.boolValue = intValue != 0;
        value.floatValue = static_cast<double>(intValue);
        value.typeKind = targetKind;
        return true;
    }

    if(errorMessage)
    {
        *errorMessage = std::string(context) +
                        " currently supports only integer, float, and bool "
                        "types";
    }
    return false;
}

double CodeGenerator::constexprValueAsDouble(const ConstexprValue& value) const
{
    if(value.kind == ConstexprValue::Kind::Float)
        return value.floatValue;
    if(value.kind == ConstexprValue::Kind::Bool)
        return value.boolValue ? 1.0 : 0.0;
    return static_cast<double>(value.intValue);
}

int64_t CodeGenerator::constexprValueAsInt(const ConstexprValue& value) const
{
    if(value.kind == ConstexprValue::Kind::Bool)
        return value.boolValue ? 1 : 0;
    if(value.kind == ConstexprValue::Kind::Float)
        return static_cast<int64_t>(value.floatValue);
    if(value.kind == ConstexprValue::Kind::Type)
        return value.typeName.empty() ? 0 : 1;
    return value.intValue;
}

std::string CodeGenerator::canonicalConstexprTypeName(TypeNode* type) const
{
    if(!type)
        return "";
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
    {
        auto aliasIt = typeAliases.find(structRef->structName);
        if(aliasIt != typeAliases.end() && aliasIt->second.aliasedType)
            return canonicalConstexprTypeName(aliasIt->second.aliasedType);
        return structRef->structName;
    }
    if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(type))
        return genRef->toString();
    if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
        return listType->toString();
    if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
        return mapType->toString();
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
        return tupleType->toString();
    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(type))
        return ptrType->toString();
    if(auto* refType = dynamic_cast<ReferenceTypeNode*>(type))
        return refType->toString();
    return type->toString();
}

bool CodeGenerator::constexprTypeNameFromIdentifier(const std::string& name,
                                                    std::string& out) const
{
    auto aliasIt = typeAliases.find(name);
    if(aliasIt != typeAliases.end() && aliasIt->second.aliasedType)
    {
        out = canonicalConstexprTypeName(aliasIt->second.aliasedType);
        return true;
    }
    if(structTypes.find(name) != structTypes.end() ||
       structMethods.find(name) != structMethods.end() ||
       structVisibility.find(name) != structVisibility.end() ||
       genericStructTemplates.find(name) != genericStructTemplates.end())
    {
        out = name;
        return true;
    }
    return false;
}

bool CodeGenerator::bindConstexprGenericTypeParams(
    TypeNode* pattern, TypeNode* concrete, const std::set<std::string>& typeParams,
    std::map<std::string, std::string>& bindings) const
{
    if(!pattern || !concrete)
        return true;

    if(auto* paramRef = dynamic_cast<StructTypeRefNode*>(pattern))
    {
        if(typeParams.count(paramRef->structName))
        {
            std::string concreteName = canonicalConstexprTypeName(concrete);
            auto it = bindings.find(paramRef->structName);
            if(it == bindings.end())
            {
                bindings[paramRef->structName] = concreteName;
                return true;
            }
            return it->second == concreteName;
        }
    }

    if(auto* patternList = dynamic_cast<GenericListTypeNode*>(pattern))
    {
        auto* concreteList = dynamic_cast<GenericListTypeNode*>(concrete);
        if(!concreteList)
            return true;
        return bindConstexprGenericTypeParams(patternList->elementType,
                                             concreteList->elementType,
                                             typeParams, bindings);
    }
    if(auto* patternMap = dynamic_cast<MapTypeNode*>(pattern))
    {
        auto* concreteMap = dynamic_cast<MapTypeNode*>(concrete);
        if(!concreteMap)
            return true;
        return bindConstexprGenericTypeParams(patternMap->keyType,
                                             concreteMap->keyType, typeParams,
                                             bindings) &&
               bindConstexprGenericTypeParams(patternMap->valueType,
                                             concreteMap->valueType,
                                             typeParams, bindings);
    }
    if(auto* patternPtr = dynamic_cast<PointerTypeNode*>(pattern))
    {
        auto* concretePtr = dynamic_cast<PointerTypeNode*>(concrete);
        if(!concretePtr)
            return true;
        return bindConstexprGenericTypeParams(patternPtr->elementType,
                                             concretePtr->elementType,
                                             typeParams, bindings);
    }
    if(auto* patternRef = dynamic_cast<ReferenceTypeNode*>(pattern))
    {
        auto* concreteRef = dynamic_cast<ReferenceTypeNode*>(concrete);
        if(!concreteRef)
            return true;
        return bindConstexprGenericTypeParams(patternRef->elementType,
                                             concreteRef->elementType,
                                             typeParams, bindings);
    }
    if(auto* patternGen = dynamic_cast<GenericStructTypeRefNode*>(pattern))
    {
        auto* concreteGen = dynamic_cast<GenericStructTypeRefNode*>(concrete);
        if(!concreteGen ||
           patternGen->typeArgs.size() != concreteGen->typeArgs.size())
            return true;
        for(size_t i = 0; i < patternGen->typeArgs.size(); ++i)
        {
            if(!bindConstexprGenericTypeParams(patternGen->typeArgs[i],
                                              concreteGen->typeArgs[i],
                                              typeParams, bindings))
                return false;
        }
    }
    return true;
}

bool CodeGenerator::evalConstexprCall(FunctionCallNode* call,
                                      ConstexprValue& out,
                                      std::string* errorMessage,
                                      ConstexprEnv* env, int depth)
{
    if(!call)
    {
        if(errorMessage)
            *errorMessage = "missing function call in cexpr";
        return false;
    }
    if(depth > 64)
    {
        if(errorMessage)
            *errorMessage = "cexpr recursion depth exceeded";
        return false;
    }

    if(call->name == "type_id")
    {
        if(call->arguments.size() != 1)
        {
            if(errorMessage)
                *errorMessage = "type_id expects one type argument";
            return false;
        }
        auto* id = dynamic_cast<IdentifierNode*>(call->arguments[0]);
        if(!id)
        {
            if(errorMessage)
                *errorMessage = "type_id expects a type name";
            return false;
        }
        std::string typeName;
        if(env)
        {
            auto boundIt = env->find("__type:" + id->name);
            if(boundIt != env->end() &&
               boundIt->second.kind == ConstexprValue::Kind::Type)
                typeName = boundIt->second.typeName;
        }
        if(typeName.empty() &&
           !constexprTypeNameFromIdentifier(id->name, typeName))
        {
            if(errorMessage)
                *errorMessage = "unknown type in type_id: '" + id->name + "'";
            return false;
        }
        out.kind = ConstexprValue::Kind::Type;
        out.typeName = typeName;
        out.boolValue = !typeName.empty();
        out.intValue = out.boolValue ? 1 : 0;
        out.floatValue = out.boolValue ? 1.0 : 0.0;
        out.typeKind = TypeNode::TYPE_I64;
        return true;
    }

    auto overloadIt = functionOverloads.find(call->name);
    if(overloadIt == functionOverloads.end())
    {
        if(errorMessage)
            *errorMessage =
                "unknown function in cexpr call: '" + call->name + "'";
        return false;
    }

    for(const auto& overload : overloadIt->second)
    {
        FunctionDefNode* fn = overload.node;
        if(!fn || !fn->isCexpr || !fn->body || !fn->parameters)
            continue;
        if(fn->parameters->isVarArg)
            continue;
        if(fn->parameters->parameters.size() != call->arguments.size())
            continue;

        ConstexprEnv localEnv;
        bool argsOk = true;
        std::set<std::string> typeParamSet(fn->typeParams.begin(),
                                           fn->typeParams.end());
        std::map<std::string, std::string> typeBindings;
        std::vector<TypeNode*> concreteArgTypes;
        concreteArgTypes.reserve(call->arguments.size());
        for(size_t i = 0; i < call->arguments.size(); ++i)
        {
            TypeNode* concreteType = nullptr;
            if(env)
            {
                if(auto* id =
                       dynamic_cast<IdentifierNode*>(call->arguments[i]))
                {
                    auto envIt = env->find(id->name);
                    if(envIt != env->end() &&
                       envIt->second.kind != ConstexprValue::Kind::Type)
                    {
                        if(envIt->second.kind ==
                               ConstexprValue::Kind::OpaqueStruct &&
                           !envIt->second.typeName.empty())
                            concreteType =
                                new StructTypeRefNode(envIt->second.typeName);
                        else
                            concreteType = new TypeNode(envIt->second.typeKind);
                    }
                }
            }
            if(!concreteType)
                concreteType =
                    inferExpressionTypeNode(call->arguments[i], call->line);
            concreteArgTypes.push_back(concreteType);
            auto* param = fn->parameters->parameters[i];
            if(!fn->typeParams.empty() && param &&
               !bindConstexprGenericTypeParams(param->type, concreteType,
                                               typeParamSet, typeBindings))
            {
                argsOk = false;
                break;
            }
        }
        if(!argsOk)
            continue;
        for(const auto& binding : typeBindings)
        {
            ConstexprValue typeValue;
            typeValue.kind = ConstexprValue::Kind::Type;
            typeValue.typeName = binding.second;
            typeValue.boolValue = !binding.second.empty();
            typeValue.intValue = typeValue.boolValue ? 1 : 0;
            typeValue.floatValue = typeValue.boolValue ? 1.0 : 0.0;
            localEnv["__type:" + binding.first] = typeValue;
        }
        for(size_t i = 0; i < call->arguments.size(); ++i)
        {
            auto* param = fn->parameters->parameters[i];
            ConstexprValue argValue;
            if(!evalConstexprExpression(call->arguments[i], argValue,
                                        errorMessage, env, depth + 1))
            {
                if(errorMessage && errorMessage->empty())
                {
                    *errorMessage = "argument " + std::to_string(i + 1) +
                                    " in cexpr call to '" + call->name +
                                    "' is not compile-time evaluable";
                }
                argsOk = false;
                break;
            }

            TypeNode::TypeKind paramKind =
                concreteArgTypes[i]
                    ? normalizeInferredKind(concreteArgTypes[i]->kind)
                    : param ? normalizeInferredKind(param->type->kind)
                            : TypeNode::TYPE_I64;
            if(argValue.kind == ConstexprValue::Kind::OpaqueStruct &&
               paramKind == TypeNode::TYPE_STRUCT)
            {
                localEnv[param->name] = argValue;
                continue;
            }
            if(!coerceConstexprValueToKind(argValue, paramKind, errorMessage,
                                           "cexpr fn parameters"))
                return false;

            localEnv[param->name] = argValue;
        }
        if(!argsOk)
            return false;

        ConstexprValue returnValue;
        bool didReturn = false;
        if(!evalConstexprStatementList(fn->body, localEnv, returnValue,
                                       didReturn, errorMessage, depth + 1))
            return false;
        if(!didReturn)
        {
            if(errorMessage)
                *errorMessage =
                    "cexpr fn '" + call->name + "' must return a value";
            return false;
        }
        out = returnValue;
        if(fn->returnType)
            out.typeKind = normalizeInferredKind(fn->returnType->kind);
        return true;
    }

    if(errorMessage)
        *errorMessage =
            "cexpr call requires a matching 'cexpr fn' overload: '" +
            call->name + "'";
    return false;
}

bool CodeGenerator::evalConstexprStatementList(
    StatementListNode* body, ConstexprEnv& env, ConstexprValue& returnValue,
    bool& didReturn, std::string* errorMessage, int depth)
{
    if(!body)
        return true;

    for(auto* stmt : body->statements)
    {
        if(!stmt)
            continue;
        if(auto* ret = dynamic_cast<ReturnNode*>(stmt))
        {
            if(!ret->expression)
            {
                if(errorMessage)
                    *errorMessage = "cexpr fn does not support bare return";
                return false;
            }
            if(!evalConstexprExpression(ret->expression, returnValue,
                                        errorMessage, &env, depth + 1))
                return false;
            didReturn = true;
            return true;
        }
        if(auto* letDecl = dynamic_cast<LetDeclNode*>(stmt))
        {
            if(!letDecl->expression)
            {
                if(errorMessage)
                    *errorMessage =
                        "cexpr fn let declarations require an initializer";
                return false;
            }
            ConstexprValue value;
            if(!evalConstexprExpression(letDecl->expression, value,
                                        errorMessage, &env, depth + 1))
                return false;
            if(letDecl->type &&
               !coerceConstexprValueToKind(
                   value, letDecl->type->kind, errorMessage,
                   "cexpr fn let declarations"))
                return false;
            env[letDecl->name] = value;
            continue;
        }
        if(auto* varDecl = dynamic_cast<VarDeclNode*>(stmt))
        {
            ConstexprValue value;
            if(varDecl->initExpr)
            {
                if(!evalConstexprExpression(varDecl->initExpr, value,
                                            errorMessage, &env, depth + 1))
                    return false;
                if(varDecl->type &&
                   !coerceConstexprValueToKind(
                       value, varDecl->type->kind, errorMessage,
                       "cexpr fn var declarations"))
                    return false;
            }
            else
            {
                TypeNode::TypeKind kind =
                    varDecl->type ? normalizeInferredKind(varDecl->type->kind)
                                  : TypeNode::TYPE_I64;
                if(kind == TypeNode::TYPE_BOOL)
                {
                    value.kind = ConstexprValue::Kind::Bool;
                    value.boolValue = false;
                    value.intValue = 0;
                    value.floatValue = 0.0;
                    value.typeKind = TypeNode::TYPE_BOOL;
                }
                else if(isFloatInferKind(kind))
                {
                    value.kind = ConstexprValue::Kind::Float;
                    value.floatValue = 0.0;
                    value.intValue = 0;
                    value.boolValue = false;
                    value.typeKind = kind;
                }
                else if(isIntegerInferKind(kind))
                {
                    value.kind = ConstexprValue::Kind::Int;
                    value.intValue = 0;
                    value.boolValue = false;
                    value.floatValue = 0.0;
                    value.typeKind = kind;
                }
                else
                {
                    if(errorMessage)
                    {
                        *errorMessage =
                            "cexpr fn zero-initialization currently supports "
                            "only integer, float, and bool vars";
                    }
                    return false;
                }
            }
            env[varDecl->name] = value;
            continue;
        }
        if(auto* assign = dynamic_cast<AssignmentNode*>(stmt))
        {
            auto it = env.find(assign->name);
            if(it == env.end())
            {
                if(errorMessage)
                    *errorMessage =
                        "unknown variable in cexpr fn assignment: '" +
                        assign->name + "'";
                return false;
            }
            ConstexprValue value;
            if(!evalConstexprExpression(assign->expression, value, errorMessage,
                                        &env, depth + 1))
                return false;
            it->second = value;
            continue;
        }
        if(auto* exprStmt = dynamic_cast<ExpressionStatementNode*>(stmt))
        {
            ConstexprValue ignored;
            if(!evalConstexprExpression(exprStmt->expression, ignored,
                                        errorMessage, &env, depth + 1))
                return false;
            continue;
        }
        if(auto* block = dynamic_cast<BlockStatementNode*>(stmt))
        {
            ConstexprEnv nestedEnv = env;
            if(!evalConstexprStatementList(block->statements, nestedEnv,
                                           returnValue, didReturn, errorMessage,
                                           depth + 1))
                return false;
            if(didReturn)
                return true;
            continue;
        }
        if(auto* cexprIf = dynamic_cast<CexprIfNode*>(stmt))
        {
            ConstexprValue condValue;
            if(!evalConstexprExpression(cexprIf->condition, condValue,
                                        errorMessage, &env, depth + 1))
                return false;
            if(condValue.kind == ConstexprValue::Kind::OpaqueStruct ||
               condValue.kind == ConstexprValue::Kind::Type)
            {
                if(errorMessage)
                    *errorMessage =
                        "cexpr if condition requires a bool or numeric value, "
                        "not a struct or type_id placeholder";
                return false;
            }
            const bool cond = condValue.kind == ConstexprValue::Kind::Bool
                                  ? condValue.boolValue
                                  : constexprValueAsDouble(condValue) != 0.0;
            if(cond)
            {
                ConstexprEnv nestedEnv = env;
                if(!evalConstexprStatementList(cexprIf->thenBranch, nestedEnv,
                                               returnValue, didReturn,
                                               errorMessage, depth + 1))
                    return false;
                if(didReturn)
                    return true;
                continue;
            }
            if(cexprIf->elseIfBranch)
            {
                StatementListNode nestedList;
                CexprIfNode* last = cexprIf->elseIfBranch;
                while(last->elseIfBranch)
                    last = last->elseIfBranch;
                StatementListNode* savedFinalElse = last->elseBranch;
                if(!last->elseBranch)
                    last->elseBranch = cexprIf->elseBranch;
                nestedList.statements.push_back(cexprIf->elseIfBranch);
                ConstexprEnv nestedEnv = env;
                bool ok = evalConstexprStatementList(
                    &nestedList, nestedEnv, returnValue, didReturn,
                    errorMessage, depth + 1);
                last->elseBranch = savedFinalElse;
                if(!ok)
                    return false;
                if(didReturn)
                    return true;
                continue;
            }
            if(cexprIf->elseBranch)
            {
                ConstexprEnv nestedEnv = env;
                if(!evalConstexprStatementList(cexprIf->elseBranch, nestedEnv,
                                               returnValue, didReturn,
                                               errorMessage, depth + 1))
                    return false;
                if(didReturn)
                    return true;
            }
            continue;
        }
        if(auto* ifNode = dynamic_cast<IfNode*>(stmt))
        {
            if(ifNode->conditionInit)
            {
                if(errorMessage)
                    *errorMessage =
                        "cexpr fn does not support if let/if var initializers";
                return false;
            }
            bool cond = false;
            if(!evalConstexprExpression(ifNode->condition, returnValue,
                                        errorMessage, &env, depth + 1))
                return false;
            cond = returnValue.kind == ConstexprValue::Kind::Bool
                       ? returnValue.boolValue
                       : constexprValueAsDouble(returnValue) != 0.0;
            if(cond)
            {
                ConstexprEnv nestedEnv = env;
                if(!evalConstexprStatementList(ifNode->thenBranch, nestedEnv,
                                               returnValue, didReturn,
                                               errorMessage, depth + 1))
                    return false;
                if(didReturn)
                    return true;
                continue;
            }
            if(ifNode->elseIfBranch)
            {
                StatementListNode nestedList;
                nestedList.statements.push_back(ifNode->elseIfBranch);
                ConstexprEnv nestedEnv = env;
                if(!evalConstexprStatementList(&nestedList, nestedEnv,
                                               returnValue, didReturn,
                                               errorMessage, depth + 1))
                    return false;
                if(didReturn)
                    return true;
                continue;
            }
            if(ifNode->elseBranch)
            {
                ConstexprEnv nestedEnv = env;
                if(!evalConstexprStatementList(ifNode->elseBranch, nestedEnv,
                                               returnValue, didReturn,
                                               errorMessage, depth + 1))
                    return false;
                if(didReturn)
                    return true;
            }
            continue;
        }

        if(errorMessage)
            *errorMessage = "statement is not supported in cexpr fn";
        return false;
    }
    return true;
}

bool CodeGenerator::evalConstexprExpression(ExpressionNode* expr,
                                            ConstexprValue& out,
                                            std::string* errorMessage,
                                            ConstexprEnv* env, int depth)
{
    if(!expr)
    {
        if(errorMessage)
            *errorMessage = "missing expression in cexpr";
        return false;
    }
    if(depth > 64)
    {
        if(errorMessage)
            *errorMessage = "cexpr recursion depth exceeded";
        return false;
    }
    if(auto* cexprExpr = dynamic_cast<CexprExpressionNode*>(expr))
        return evalConstexprExpression(cexprExpr->expression, out, errorMessage,
                                       env, depth + 1);
    if(auto* sizeofExpr = dynamic_cast<SizeofExpressionNode*>(expr))
    {
        TypeNode* targetType = nullptr;
        if(sizeofExpr->typeTarget)
            targetType = cloneTypeNode(sizeofExpr->typeTarget);
        else
        {
            if(auto* id =
                   dynamic_cast<IdentifierNode*>(sizeofExpr->expressionTarget))
            {
                auto it = variableTypes.find(id->name);
                if(it != variableTypes.end())
                {
                    switch(it->second)
                    {
                    case TypeNode::TYPE_STRUCT:
                    {
                        auto sit = structVariableTypes.find(id->name);
                        if(sit != structVariableTypes.end())
                            targetType = new StructTypeRefNode(sit->second);
                        break;
                    }
                    case TypeNode::TYPE_LIST:
                    {
                        auto lit = listElementTypes.find(id->name);
                        if(lit != listElementTypes.end())
                        {
                            auto capIt = arrayCapacities.find(id->name);
                            if(capIt != arrayCapacities.end())
                                targetType = new ArrayTypeNode(
                                    cloneTypeNode(lit->second), capIt->second);
                            else
                                targetType = new GenericListTypeNode(
                                    cloneTypeNode(lit->second));
                        }
                        break;
                    }
                    case TypeNode::TYPE_MAP:
                    {
                        auto mit = mapKeyValueTypes.find(id->name);
                        if(mit != mapKeyValueTypes.end())
                            targetType = new MapTypeNode(
                                cloneTypeNode(mit->second.first),
                                cloneTypeNode(mit->second.second));
                        break;
                    }
                    case TypeNode::TYPE_TUPLE:
                    {
                        auto tit = tupleElementTypes.find(id->name);
                        if(tit != tupleElementTypes.end())
                        {
                            auto* types = new TypeListNode();
                            for(auto* elem : tit->second)
                                types->addType(cloneTypeNode(elem));
                            targetType = new TupleTypeNode(types);
                        }
                        break;
                    }
                    case TypeNode::TYPE_PTR:
                    {
                        auto pit = pointerElementTypes.find(id->name);
                        if(pit != pointerElementTypes.end())
                            targetType =
                                new PointerTypeNode(cloneTypeNode(pit->second));
                        break;
                    }
                    default:
                        targetType = new TypeNode(it->second);
                        break;
                    }
                }
            }
            if(!targetType)
                targetType = inferExpressionTypeNode(
                    sizeofExpr->expressionTarget, sizeofExpr->line);
        }
        if(!targetType)
        {
            if(errorMessage)
                *errorMessage = "cannot infer size_of target in cexpr";
            return false;
        }
        llvm::Type* llvmType = getLLVMTypeFromNode(targetType);
        if(!llvmType)
        {
            if(errorMessage)
                *errorMessage = "cannot lower size_of target type in cexpr";
            return false;
        }
        out.kind = ConstexprValue::Kind::Int;
        if(std::optional<uint64_t> arrayBytes = fixedArrayByteSize(targetType))
            out.intValue = static_cast<int64_t>(*arrayBytes);
        else
        {
            const llvm::DataLayout& dl = module->getDataLayout();
            out.intValue = static_cast<int64_t>(
                dl.getTypeAllocSize(llvmType).getFixedValue());
        }
        out.boolValue = out.intValue != 0;
        out.typeKind = TypeNode::TYPE_I64;
        return true;
    }
    if(auto* i = dynamic_cast<IntLiteralNode*>(expr))
    {
        out.kind = ConstexprValue::Kind::Int;
        out.intValue = i->value;
        out.boolValue = i->value != 0;
        out.floatValue = static_cast<double>(i->value);
        out.typeKind = TypeNode::TYPE_I64;
        return true;
    }
    if(auto* b = dynamic_cast<BoolLiteralNode*>(expr))
    {
        out.kind = ConstexprValue::Kind::Bool;
        out.boolValue = b->value;
        out.intValue = b->value ? 1 : 0;
        out.floatValue = b->value ? 1.0 : 0.0;
        out.typeKind = TypeNode::TYPE_BOOL;
        return true;
    }
    if(auto* f = dynamic_cast<FloatLiteralNode*>(expr))
    {
        out.kind = ConstexprValue::Kind::Float;
        out.floatValue = static_cast<double>(f->value);
        out.intValue = static_cast<int64_t>(f->value);
        out.boolValue = f->value != 0.0f;
        out.typeKind = TypeNode::TYPE_FLOAT;
        return true;
    }
    if(auto* d = dynamic_cast<DoubleLiteralNode*>(expr))
    {
        out.kind = ConstexprValue::Kind::Float;
        out.floatValue = d->value;
        out.intValue = static_cast<int64_t>(d->value);
        out.boolValue = d->value != 0.0;
        out.typeKind = TypeNode::TYPE_DOUBLE;
        return true;
    }
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        if(env)
        {
            auto it = env->find(id->name);
            if(it != env->end())
            {
                out = it->second;
                return true;
            }
            auto typeIt = env->find("__type:" + id->name);
            if(typeIt != env->end() &&
               typeIt->second.kind == ConstexprValue::Kind::Type)
            {
                out = typeIt->second;
                return true;
            }
        }
        auto constexprIt = constexprValues.find(id->name);
        if(constexprIt != constexprValues.end())
        {
            out = constexprIt->second;
            return true;
        }
        std::string typeName;
        if(constexprTypeNameFromIdentifier(id->name, typeName))
        {
            out.kind = ConstexprValue::Kind::Type;
            out.typeName = typeName;
            out.boolValue = !typeName.empty();
            out.intValue = out.boolValue ? 1 : 0;
            out.floatValue = out.boolValue ? 1.0 : 0.0;
            out.typeKind = TypeNode::TYPE_I64;
            return true;
        }
        auto varTypeIt = variableTypes.find(id->name);
        if(varTypeIt != variableTypes.end() &&
           varTypeIt->second == TypeNode::TYPE_STRUCT)
        {
            auto structIt = structVariableTypes.find(id->name);
            if(structIt != structVariableTypes.end())
            {
                out.kind = ConstexprValue::Kind::OpaqueStruct;
                out.typeName = structIt->second;
                out.typeKind = TypeNode::TYPE_STRUCT;
                out.boolValue = true;
                out.intValue = 1;
                out.floatValue = 1.0;
                return true;
            }
        }
        if(errorMessage)
        {
            *errorMessage =
                env ? "unknown compile-time variable in cexpr: '" + id->name +
                          "'"
                    : "runtime variable is not available in cexpr: '" +
                          id->name + "'";
        }
        return false;
    }
    if(auto* structLit = dynamic_cast<StructLiteralNode*>(expr))
    {
        TypeNode* structType = inferExpressionTypeNode(structLit, structLit->line);
        std::string typeName = canonicalConstexprTypeName(structType);
        if(typeName.empty())
            typeName = structLit->structName;
        out.kind = ConstexprValue::Kind::OpaqueStruct;
        out.typeName = typeName;
        out.typeKind = TypeNode::TYPE_STRUCT;
        out.boolValue = true;
        out.intValue = 1;
        out.floatValue = 1.0;
        return true;
    }
    if(auto* castExpr = dynamic_cast<CastExpressionNode*>(expr))
    {
        if(!evalConstexprExpression(castExpr->expression, out, errorMessage,
                                    env, depth + 1))
            return false;
        TypeNode::TypeKind targetKind =
            normalizeInferredKind(castExpr->targetType);
        if(targetKind == TypeNode::TYPE_BOOL)
        {
            return coerceConstexprValueToKind(out, targetKind, errorMessage,
                                              "cexpr casts");
        }
        return coerceConstexprValueToKind(out, targetKind, errorMessage,
                                          "cexpr casts");
    }
    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
        return evalConstexprCall(call, out, errorMessage, env, depth + 1);
    if(auto* un = dynamic_cast<UnaryOpNode*>(expr))
    {
        ConstexprValue operand;
        if(!evalConstexprExpression(un->operand, operand, errorMessage, env,
                                    depth + 1))
        {
            if(errorMessage && errorMessage->empty())
                *errorMessage = "unary operand is not compile-time evaluable";
            return false;
        }
        if(operand.kind == ConstexprValue::Kind::OpaqueStruct ||
           operand.kind == ConstexprValue::Kind::Type)
        {
            if(errorMessage)
                *errorMessage =
                    "cexpr unary operators require a value; struct and "
                    "type_id placeholders are only available for type dispatch";
            return false;
        }
        switch(un->op)
        {
        case UnaryOpNode::OP_NEG:
            if(operand.kind == ConstexprValue::Kind::Float)
            {
                out.kind = ConstexprValue::Kind::Float;
                out.floatValue = -operand.floatValue;
                out.intValue = static_cast<int64_t>(out.floatValue);
                out.boolValue = out.floatValue != 0.0;
                out.typeKind = operand.typeKind;
                return true;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = -constexprValueAsInt(operand);
            out.floatValue = static_cast<double>(out.intValue);
            out.boolValue = out.intValue != 0;
            out.typeKind = operand.typeKind;
            return true;
        case UnaryOpNode::OP_NOT:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = constexprValueAsDouble(operand) == 0.0;
            out.intValue = out.boolValue ? 1 : 0;
            out.floatValue = out.boolValue ? 1.0 : 0.0;
            out.typeKind = TypeNode::TYPE_BOOL;
            return true;
        case UnaryOpNode::OP_BITNOT:
            if(operand.kind == ConstexprValue::Kind::Float)
            {
                if(errorMessage)
                    *errorMessage = "bitwise not in cexpr requires an integer";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = ~constexprValueAsInt(operand);
            out.floatValue = static_cast<double>(out.intValue);
            out.boolValue = out.intValue != 0;
            out.typeKind = operand.typeKind;
            return true;
        default:
            if(errorMessage)
                *errorMessage = "unsupported unary operator in cexpr";
            return false;
        }
    }
    if(auto* bin = dynamic_cast<BinaryOpNode*>(expr))
    {
        ConstexprValue lhsValue;
        ConstexprValue rhsValue;
        if(!evalConstexprExpression(bin->left, lhsValue, errorMessage, env,
                                    depth + 1))
        {
            if(errorMessage && errorMessage->empty())
                *errorMessage =
                    "left operand is not compile-time evaluable in cexpr";
            return false;
        }
        if(!evalConstexprExpression(bin->right, rhsValue, errorMessage, env,
                                    depth + 1))
        {
            if(errorMessage && errorMessage->empty())
                *errorMessage =
                    "right operand is not compile-time evaluable in cexpr";
            return false;
        }
        if(lhsValue.kind == ConstexprValue::Kind::Type ||
           rhsValue.kind == ConstexprValue::Kind::Type)
        {
            if(lhsValue.kind != ConstexprValue::Kind::Type ||
               rhsValue.kind != ConstexprValue::Kind::Type ||
               (bin->op != BinaryOpNode::OP_EQ &&
                bin->op != BinaryOpNode::OP_NE))
            {
                if(errorMessage)
                    *errorMessage =
                        "type_id values in cexpr support only == and !=";
                return false;
            }
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = (lhsValue.typeName == rhsValue.typeName);
            if(bin->op == BinaryOpNode::OP_NE)
                out.boolValue = !out.boolValue;
            out.intValue = out.boolValue ? 1 : 0;
            out.floatValue = out.boolValue ? 1.0 : 0.0;
            out.typeKind = TypeNode::TYPE_BOOL;
            return true;
        }
        if(lhsValue.kind == ConstexprValue::Kind::OpaqueStruct ||
           rhsValue.kind == ConstexprValue::Kind::OpaqueStruct)
        {
            if(errorMessage)
                *errorMessage =
                    "struct values in cexpr are only supported for generic "
                    "type dispatch with type_id(T); struct value operations "
                    "are not compile-time evaluable yet";
            return false;
        }
        const bool anyFloat = lhsValue.kind == ConstexprValue::Kind::Float ||
                              rhsValue.kind == ConstexprValue::Kind::Float;
        const TypeNode::TypeKind floatResultKind =
            lhsValue.typeKind == TypeNode::TYPE_DOUBLE ||
                    rhsValue.typeKind == TypeNode::TYPE_DOUBLE
                ? TypeNode::TYPE_DOUBLE
                : TypeNode::TYPE_FLOAT;
        const int64_t lhs = constexprValueAsInt(lhsValue);
        const int64_t rhs = constexprValueAsInt(rhsValue);
        const double lhsFloat = constexprValueAsDouble(lhsValue);
        const double rhsFloat = constexprValueAsDouble(rhsValue);
        switch(bin->op)
        {
        case BinaryOpNode::OP_PLUS:
            if(anyFloat)
            {
                out.kind = ConstexprValue::Kind::Float;
                out.floatValue = lhsFloat + rhsFloat;
                out.intValue = static_cast<int64_t>(out.floatValue);
                out.typeKind = floatResultKind;
            }
            else
            {
                out.kind = ConstexprValue::Kind::Int;
                out.intValue = lhs + rhs;
                out.floatValue = static_cast<double>(out.intValue);
                out.typeKind = lhsValue.typeKind;
            }
            break;
        case BinaryOpNode::OP_MINUS:
            if(anyFloat)
            {
                out.kind = ConstexprValue::Kind::Float;
                out.floatValue = lhsFloat - rhsFloat;
                out.intValue = static_cast<int64_t>(out.floatValue);
                out.typeKind = floatResultKind;
            }
            else
            {
                out.kind = ConstexprValue::Kind::Int;
                out.intValue = lhs - rhs;
                out.floatValue = static_cast<double>(out.intValue);
                out.typeKind = lhsValue.typeKind;
            }
            break;
        case BinaryOpNode::OP_MULTIPLY:
            if(anyFloat)
            {
                out.kind = ConstexprValue::Kind::Float;
                out.floatValue = lhsFloat * rhsFloat;
                out.intValue = static_cast<int64_t>(out.floatValue);
                out.typeKind = floatResultKind;
            }
            else
            {
                out.kind = ConstexprValue::Kind::Int;
                out.intValue = lhs * rhs;
                out.floatValue = static_cast<double>(out.intValue);
                out.typeKind = lhsValue.typeKind;
            }
            break;
        case BinaryOpNode::OP_DIVIDE:
            if(anyFloat)
            {
                if(rhsFloat == 0.0)
                {
                    if(errorMessage)
                        *errorMessage = "division by zero in cexpr";
                    return false;
                }
                out.kind = ConstexprValue::Kind::Float;
                out.floatValue = lhsFloat / rhsFloat;
                out.intValue = static_cast<int64_t>(out.floatValue);
                out.typeKind = floatResultKind;
                break;
            }
            if(rhs == 0)
            {
                if(errorMessage)
                    *errorMessage = "division by zero in cexpr";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = lhs / rhs;
            out.floatValue = static_cast<double>(out.intValue);
            out.typeKind = lhsValue.typeKind;
            break;
        case BinaryOpNode::OP_MODULO:
            if(anyFloat)
            {
                if(errorMessage)
                    *errorMessage = "modulo in cexpr requires integers";
                return false;
            }
            if(rhs == 0)
            {
                if(errorMessage)
                    *errorMessage = "modulo by zero in cexpr";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = lhs % rhs;
            out.floatValue = static_cast<double>(out.intValue);
            out.typeKind = lhsValue.typeKind;
            break;
        case BinaryOpNode::OP_BITAND:
            if(anyFloat)
            {
                if(errorMessage)
                    *errorMessage = "bitwise and in cexpr requires integers";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = lhs & rhs;
            out.floatValue = static_cast<double>(out.intValue);
            out.typeKind = lhsValue.typeKind;
            break;
        case BinaryOpNode::OP_BITOR:
            if(anyFloat)
            {
                if(errorMessage)
                    *errorMessage = "bitwise or in cexpr requires integers";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = lhs | rhs;
            out.floatValue = static_cast<double>(out.intValue);
            out.typeKind = lhsValue.typeKind;
            break;
        case BinaryOpNode::OP_BITXOR:
            if(anyFloat)
            {
                if(errorMessage)
                    *errorMessage = "bitwise xor in cexpr requires integers";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = lhs ^ rhs;
            out.floatValue = static_cast<double>(out.intValue);
            out.typeKind = lhsValue.typeKind;
            break;
        case BinaryOpNode::OP_SHL:
            if(anyFloat)
            {
                if(errorMessage)
                    *errorMessage = "shift in cexpr requires integers";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = lhs << rhs;
            out.floatValue = static_cast<double>(out.intValue);
            out.typeKind = lhsValue.typeKind;
            break;
        case BinaryOpNode::OP_SHR:
            if(anyFloat)
            {
                if(errorMessage)
                    *errorMessage = "shift in cexpr requires integers";
                return false;
            }
            out.kind = ConstexprValue::Kind::Int;
            out.intValue = lhs >> rhs;
            out.floatValue = static_cast<double>(out.intValue);
            out.typeKind = lhsValue.typeKind;
            break;
        case BinaryOpNode::OP_LT:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = anyFloat ? lhsFloat < rhsFloat : lhs < rhs;
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        case BinaryOpNode::OP_GT:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = anyFloat ? lhsFloat > rhsFloat : lhs > rhs;
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        case BinaryOpNode::OP_LE:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = anyFloat ? lhsFloat <= rhsFloat : lhs <= rhs;
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        case BinaryOpNode::OP_GE:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = anyFloat ? lhsFloat >= rhsFloat : lhs >= rhs;
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        case BinaryOpNode::OP_EQ:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = anyFloat ? lhsFloat == rhsFloat : lhs == rhs;
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        case BinaryOpNode::OP_NE:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = anyFloat ? lhsFloat != rhsFloat : lhs != rhs;
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        case BinaryOpNode::OP_AND:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = (lhsFloat != 0.0 && rhsFloat != 0.0);
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        case BinaryOpNode::OP_OR:
            out.kind = ConstexprValue::Kind::Bool;
            out.boolValue = (lhsFloat != 0.0 || rhsFloat != 0.0);
            out.typeKind = TypeNode::TYPE_BOOL;
            break;
        default:
            if(errorMessage)
                *errorMessage = "unsupported binary operator in cexpr";
            return false;
        }
        if(out.kind == ConstexprValue::Kind::Bool)
        {
            out.intValue = out.boolValue ? 1 : 0;
            out.floatValue = out.boolValue ? 1.0 : 0.0;
        }
        else
        {
            out.boolValue = out.intValue != 0;
            if(out.kind == ConstexprValue::Kind::Float)
                out.boolValue = out.floatValue != 0.0;
        }
        return true;
    }
    if(auto* tern = dynamic_cast<TernaryNode*>(expr))
    {
        ConstexprValue condValue;
        if(!evalConstexprExpression(tern->condition, condValue, errorMessage,
                                    env, depth + 1))
        {
            if(errorMessage && errorMessage->empty())
                *errorMessage =
                    "ternary condition is not compile-time evaluable in cexpr";
            return false;
        }
        if(condValue.kind == ConstexprValue::Kind::OpaqueStruct ||
           condValue.kind == ConstexprValue::Kind::Type)
        {
            if(errorMessage)
                *errorMessage =
                    "cexpr ternary condition requires a bool or numeric "
                    "value, not a struct or type_id placeholder";
            return false;
        }
        const bool cond = condValue.kind == ConstexprValue::Kind::Bool
                              ? condValue.boolValue
                              : constexprValueAsDouble(condValue) != 0.0;
        return evalConstexprExpression(cond ? tern->trueExpr : tern->falseExpr,
                                       out, errorMessage, env, depth + 1);
    }
    if(errorMessage)
        *errorMessage = "unsupported expression in cexpr: " + expr->toString();
    return false;
}

llvm::Value* CodeGenerator::generateCexprExpression(CexprExpressionNode* node)
{
    if(!node || !node->expression)
        return nullptr;
    ConstexprValue value;
    std::string errorMessage;
    if(!evalConstexprExpression(node->expression, value, &errorMessage, nullptr,
                                0))
    {
        reportError(node->line,
                    errorMessage.empty()
                        ? "cexpr(...) requires a compile-time expression"
                        : errorMessage);
        return nullptr;
    }
    TypeNode* targetType =
        inferExpressionTypeNode(node->expression, node->line);
    return buildLLVMConstantFromConstexprValue(value, targetType, node->line);
}
