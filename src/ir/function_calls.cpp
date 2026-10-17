#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"
#include "llvm_compat.h"
#include "module.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <llvm/Config/llvm-config.h>
#include <pthread.h>

using mlang::ir_detail::ast_analysis::collect_used_idents;
using mlang::ir_detail::common::Helpers;

bool CodeGenerator::functionMayWritePointerParameter(
    FunctionDefNode* function, size_t parameterIndex) const
{
    if(!function || !function->parameters ||
       parameterIndex >= function->parameters->parameters.size())
        return true;
    ParameterNode* parameter =
        function->parameters->parameters[parameterIndex];
    if(!parameter || !dynamic_cast<PointerTypeNode*>(parameter->type) ||
       !function->body)
        return true;

    std::set<std::string> aliases{parameter->name};
    auto usesAlias = [&](ASTNode* expression) {
        std::set<std::string> identifiers;
        collect_used_idents(expression, identifiers);
        return std::any_of(identifiers.begin(), identifiers.end(),
                           [&](const std::string& identifier) {
                               return aliases.count(identifier) != 0;
                           });
    };
    std::function<void(StatementListNode*)> collectAliases;
    std::function<void(StatementNode*)> collectAliasesFromStatement;
    collectAliases = [&](StatementListNode* statements) {
        if(!statements)
            return;
        for(auto* statement : statements->statements)
            collectAliasesFromStatement(statement);
    };
    collectAliasesFromStatement = [&](StatementNode* statement) {
        if(!statement)
            return;
        if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
        {
            if(usesAlias(declaration->expression))
                aliases.insert(declaration->name);
        }
        else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
        {
            if(usesAlias(declaration->initExpr))
                aliases.insert(declaration->name);
        }
        else if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
        {
            if(usesAlias(assignment->expression))
                aliases.insert(assignment->name);
        }
        else if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
            collectAliases(block->statements);
        else if(auto* branch = dynamic_cast<IfNode*>(statement))
        {
            collectAliases(branch->thenBranch);
            collectAliasesFromStatement(branch->elseIfBranch);
            collectAliases(branch->elseBranch);
        }
        else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
        {
            collectAliases(branch->thenBranch);
            collectAliasesFromStatement(branch->elseIfBranch);
            collectAliases(branch->elseBranch);
        }
        else if(auto* loop = dynamic_cast<ForNode*>(statement))
            collectAliases(loop->body);
        else if(auto* loop = dynamic_cast<WhileNode*>(statement))
            collectAliases(loop->body);
        else if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
        {
            collectAliasesFromStatement(attempt->tryBlock);
            collectAliasesFromStatement(attempt->catchBlock);
        }
    };
    size_t previousAliasCount = 0;
    do
    {
        previousAliasCount = aliases.size();
        collectAliases(function->body);
    } while(aliases.size() != previousAliasCount);

    std::function<bool(StatementListNode*)> scansStatements;
    std::function<bool(StatementNode*)> scansStatement;
    auto expressionMayWrite = [&](ASTNode* expression) {
        if(auto* call = dynamic_cast<FunctionCallNode*>(expression))
            for(auto* argument : call->arguments)
                if(usesAlias(argument))
                    return true;
        if(auto* call = dynamic_cast<CallableInvokeNode*>(expression))
        {
            if(usesAlias(call->callable))
                return true;
            for(auto* argument : call->arguments)
                if(usesAlias(argument))
                    return true;
        }
        if(auto* call = dynamic_cast<MethodCallNode*>(expression))
        {
            if(usesAlias(call->object))
                return true;
            for(auto* argument : call->arguments)
                if(usesAlias(argument))
                    return true;
        }
        if(auto* assembly = dynamic_cast<InlineAsmNode*>(expression))
            for(auto* argument : assembly->arguments)
                if(usesAlias(argument))
                    return true;
        if(auto* closure = dynamic_cast<ClosureNode*>(expression))
            return scansStatements(closure->body);
        return false;
    };
    scansStatements = [&](StatementListNode* statements) {
        if(!statements)
            return false;
        for(auto* statement : statements->statements)
            if(scansStatement(statement))
                return true;
        return false;
    };
    scansStatement = [&](StatementNode* statement) {
        if(!statement)
            return false;
        if(auto* assignment = dynamic_cast<DerefAssignmentNode*>(statement))
            return usesAlias(assignment->pointerExpr);
        if(auto* assignment = dynamic_cast<FieldAssignmentNode*>(statement))
            return assignment->target ? usesAlias(assignment->target)
                                      : aliases.count(assignment->structName) !=
                                            0;
        if(auto* expression =
               dynamic_cast<ExpressionStatementNode*>(statement))
            return expressionMayWrite(expression->expression);
        if(auto* returned = dynamic_cast<ReturnNode*>(statement))
            return expressionMayWrite(returned->expression);
        if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            return expressionMayWrite(declaration->expression);
        if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            return expressionMayWrite(declaration->initExpr);
        if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
            return expressionMayWrite(assignment->expression);
        if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
            return scansStatements(block->statements);
        if(auto* branch = dynamic_cast<IfNode*>(statement))
            return scansStatements(branch->thenBranch) ||
                   scansStatement(branch->elseIfBranch) ||
                   scansStatements(branch->elseBranch);
        if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            return scansStatements(branch->thenBranch) ||
                   scansStatement(branch->elseIfBranch) ||
                   scansStatements(branch->elseBranch);
        if(auto* loop = dynamic_cast<ForNode*>(statement))
            return scansStatements(loop->body);
        if(auto* loop = dynamic_cast<WhileNode*>(statement))
            return scansStatements(loop->body);
        if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
            return scansStatement(attempt->tryBlock) ||
                   scansStatement(attempt->catchBlock);
        if(auto* print = dynamic_cast<PrintNode*>(statement))
        {
            for(auto* argument : print->arguments)
                if(expressionMayWrite(argument))
                    return true;
            for(const auto& argument : print->namedArguments)
                if(expressionMayWrite(argument.second))
                    return true;
        }
        if(auto* assertion = dynamic_cast<AssertNode*>(statement))
            return expressionMayWrite(assertion->condition);
        if(auto* assertion = dynamic_cast<AssertEqNode*>(statement))
            return expressionMayWrite(assertion->left) ||
                   expressionMayWrite(assertion->right);
        return false;
    };
    return scansStatements(function->body);
}

bool CodeGenerator::functionCanReadBorrowingAggregateParameter(
    FunctionDefNode* function, size_t parameterIndex)
{
    if(!function || !function->parameters || !function->body ||
       parameterIndex >= function->parameters->parameters.size() ||
       !function->typeParams.empty())
        return false;
    auto* parameter = function->parameters->parameters[parameterIndex];
    if(!parameter ||
       (!dynamic_cast<StructTypeRefNode*>(parameter->type) &&
        !dynamic_cast<GenericStructTypeRefNode*>(parameter->type) &&
        !dynamic_cast<GenericListTypeNode*>(parameter->type) &&
        !dynamic_cast<MapTypeNode*>(parameter->type)))
        return false;

    const std::string parameterName = parameter->name;
    std::set<std::string> borrowedPointerAliases;
    std::map<std::string, PointerTypeNode*> borrowedPointerAliasTypes;
    auto usesParameter = [&](ASTNode* expression) {
        std::set<std::string> identifiers;
        collect_used_idents(expression, identifiers);
        if(identifiers.count(parameterName) != 0)
            return true;
        return std::any_of(identifiers.begin(), identifiers.end(),
                           [&](const std::string& identifier) {
                               return borrowedPointerAliases.count(
                                          identifier) != 0;
                           });
    };
    std::function<bool(ExpressionNode*)> isParameterFieldPath =
        [&](ExpressionNode* expression) {
            if(auto* identifier = dynamic_cast<IdentifierNode*>(expression))
                return identifier->name == parameterName;
            if(auto* field = dynamic_cast<FieldAccessNode*>(expression))
                return field->object
                           ? isParameterFieldPath(field->object)
                           : field->structName == parameterName;
            if(auto* index = dynamic_cast<IndexExpressionNode*>(expression))
                return isParameterFieldPath(index->base);
            return false;
        };
    std::function<TypeNode*(ExpressionNode*)> parameterFieldType =
        [&](ExpressionNode* expression) -> TypeNode* {
            if(auto* identifier = dynamic_cast<IdentifierNode*>(expression))
                return identifier->name == parameterName ? parameter->type
                                                          : nullptr;
            if(auto* index = dynamic_cast<IndexExpressionNode*>(expression))
            {
                TypeNode* baseType = parameterFieldType(index->base);
                if(auto* listType =
                       dynamic_cast<GenericListTypeNode*>(baseType))
                    return listType->elementType;
                if(auto* mapType = dynamic_cast<MapTypeNode*>(baseType))
                    return mapType->valueType;
                return nullptr;
            }
            auto* field = dynamic_cast<FieldAccessNode*>(expression);
            if(!field)
                return nullptr;
            TypeNode* baseType = field->object
                                     ? parameterFieldType(field->object)
                                     : (field->structName == parameterName
                                            ? parameter->type
                                            : nullptr);
            std::string structName;
            if(auto* structure = dynamic_cast<StructTypeRefNode*>(baseType))
                structName = structure->structName;
            else if(auto* generic =
                        dynamic_cast<GenericStructTypeRefNode*>(baseType))
                structName = generic->getMangledName();
            if(structName.empty())
                return nullptr;
            auto members = structMembers.find(structName);
            if(members == structMembers.end())
                return nullptr;
            auto member = std::find_if(
                members->second.begin(), members->second.end(),
                [&](const auto& candidate) {
                    return candidate.first == field->fieldName;
                });
            return member == members->second.end() ? nullptr : member->second;
        };
    auto isBorrowedPointerSource = [&](ExpressionNode* expression) {
        if(auto* fieldType = parameterFieldType(expression))
            return dynamic_cast<PointerTypeNode*>(fieldType) != nullptr;
        if(auto* identifier = dynamic_cast<IdentifierNode*>(expression))
            return borrowedPointerAliasTypes.count(identifier->name) != 0;
        return false;
    };
    auto isScalarType = [](TypeNode* type) {
        if(!type)
            return false;
        switch(type->kind)
        {
        case TypeNode::TYPE_BOOL:
        case TypeNode::TYPE_BIT:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_I32:
        case TypeNode::TYPE_I64:
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
        case TypeNode::TYPE_U64:
        case TypeNode::TYPE_FLOAT:
        case TypeNode::TYPE_DOUBLE:
            return true;
        default:
            return false;
        }
    };
    std::function<bool(ExpressionNode*)> expressionIsSafe;
    expressionIsSafe = [&](ExpressionNode* expression) {
        if(!expression || !usesParameter(expression))
            return true;
        if(auto* unary = dynamic_cast<UnaryOpNode*>(expression))
        {
            if(unary->op != UnaryOpNode::OP_DEREF ||
               !isBorrowedPointerSource(unary->operand))
                return false;
            TypeNode* resultType = nullptr;
            if(auto* fieldType = parameterFieldType(unary->operand))
                if(auto* pointer = dynamic_cast<PointerTypeNode*>(fieldType))
                    resultType = pointer->elementType;
            if(auto* identifier =
                   dynamic_cast<IdentifierNode*>(unary->operand))
                if(auto alias = borrowedPointerAliasTypes.find(
                       identifier->name);
                   alias != borrowedPointerAliasTypes.end())
                    resultType = alias->second->elementType;
            return resultType && isScalarType(resultType);
        }
        if(auto* field = dynamic_cast<FieldAccessNode*>(expression))
            return isParameterFieldPath(field) &&
                   isScalarType(parameterFieldType(field));
        if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
            return expressionIsSafe(binary->left) &&
                   expressionIsSafe(binary->right);
        if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
            return expressionIsSafe(ternary->condition) &&
                   expressionIsSafe(ternary->trueExpr) &&
                   expressionIsSafe(ternary->falseExpr);
        if(auto* match = dynamic_cast<MatchExpressionNode*>(expression))
        {
            if(!expressionIsSafe(match->target))
                return false;
            for(auto* arm : match->arms)
                if(!arm || !expressionIsSafe(arm->expression))
                    return false;
            return true;
        }
        return false;
    };

    std::function<bool(StatementNode*)> statementIsSafe;
    std::function<bool(StatementListNode*)> statementsAreSafe;
    statementsAreSafe = [&](StatementListNode* statements) {
        if(!statements)
            return true;
        const auto outerAliases = borrowedPointerAliases;
        const auto outerAliasTypes = borrowedPointerAliasTypes;
        for(auto* statement : statements->statements)
            if(!statementIsSafe(statement))
            {
                borrowedPointerAliases = outerAliases;
                borrowedPointerAliasTypes = outerAliasTypes;
                return false;
            }
        borrowedPointerAliases = outerAliases;
        borrowedPointerAliasTypes = outerAliasTypes;
        return true;
    };
    statementIsSafe = [&](StatementNode* statement) {
        if(!statement)
            return true;
        if(auto* returned = dynamic_cast<ReturnNode*>(statement))
            return expressionIsSafe(returned->expression);
        if(auto* expression =
               dynamic_cast<ExpressionStatementNode*>(statement))
            return expressionIsSafe(expression->expression);
        if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
        {
            auto* source = dynamic_cast<IdentifierNode*>(
                declaration->expression);
            TypeNode* sourceType = parameterFieldType(
                declaration->expression);
            if(source)
                if(auto alias = borrowedPointerAliasTypes.find(source->name);
                   alias != borrowedPointerAliasTypes.end())
                    sourceType = alias->second;
            const bool aliasesBorrowedPointer =
                (source && borrowedPointerAliases.count(source->name) != 0) ||
                isParameterFieldPath(declaration->expression);
            if(aliasesBorrowedPointer)
            {
                auto* pointerType =
                    dynamic_cast<PointerTypeNode*>(sourceType);
                if(pointerType)
                {
                    if(declaration->name == parameterName ||
                       borrowedPointerAliases.count(declaration->name) != 0)
                        return false;
                    borrowedPointerAliases.insert(declaration->name);
                    borrowedPointerAliasTypes[declaration->name] = pointerType;
                    return true;
                }
                if(!source)
                    return expressionIsSafe(declaration->expression);
                return false;
            }
            return expressionIsSafe(declaration->expression);
        }
        if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            return expressionIsSafe(declaration->initExpr);
        if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
            return assignment->name != parameterName &&
                   expressionIsSafe(assignment->expression);
        if(auto* assignment = dynamic_cast<DerefAssignmentNode*>(statement))
            return !usesParameter(assignment->pointerExpr) &&
                   expressionIsSafe(assignment->pointerExpr) &&
                   expressionIsSafe(assignment->value);
        if(auto* assignment = dynamic_cast<FieldAssignmentNode*>(statement))
            return (assignment->target
                        ? !usesParameter(assignment->target)
                        : assignment->structName != parameterName) &&
                   expressionIsSafe(assignment->expression);
        if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
            return statementsAreSafe(block->statements);
        if(auto* branch = dynamic_cast<IfNode*>(statement))
            return !branch->conditionInit &&
                   expressionIsSafe(branch->condition) &&
                   statementsAreSafe(branch->thenBranch) &&
                   statementIsSafe(branch->elseIfBranch) &&
                   statementsAreSafe(branch->elseBranch);
        if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            return expressionIsSafe(branch->condition) &&
                   statementsAreSafe(branch->thenBranch) &&
                   statementIsSafe(branch->elseIfBranch) &&
                   statementsAreSafe(branch->elseBranch);
        if(auto* loop = dynamic_cast<WhileNode*>(statement))
            return expressionIsSafe(loop->condition) &&
                   statementsAreSafe(loop->body);
        if(auto* loop = dynamic_cast<ForNode*>(statement))
            return loop->varName != parameterName &&
                   loop->indexVarName != parameterName &&
                   expressionIsSafe(loop->iterable) &&
                   statementsAreSafe(loop->body);
        if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
            return attempt->catchName != parameterName &&
                   statementIsSafe(attempt->tryBlock) &&
                   statementIsSafe(attempt->catchBlock);
        if(auto* print = dynamic_cast<PrintNode*>(statement))
        {
            for(auto* argument : print->arguments)
                if(!expressionIsSafe(argument))
                    return false;
            for(const auto& argument : print->namedArguments)
                if(!expressionIsSafe(argument.second))
                    return false;
            return true;
        }
        if(auto* assertion = dynamic_cast<AssertNode*>(statement))
            return expressionIsSafe(assertion->condition);
        if(auto* assertion = dynamic_cast<AssertEqNode*>(statement))
            return expressionIsSafe(assertion->left) &&
                   expressionIsSafe(assertion->right);
        if(auto* thrown = dynamic_cast<ThrowNode*>(statement))
            return expressionIsSafe(thrown->expression);
        return !usesParameter(statement);
    };
    return statementsAreSafe(function->body);
}

llvm::Value*
CodeGenerator::generateVariadicGenericCall(FunctionCallNode* node,
                                           FunctionDefNode* functionTemplate)
{
    if(!functionTemplate || !functionTemplate->parameters)
        return nullptr;

    ParameterNode* packParameter = nullptr;
    std::vector<ParameterNode*> fixedParameters;
    for(auto* parameter : functionTemplate->parameters->parameters)
    {
        if(parameter && parameter->isPack)
        {
            if(packParameter ||
               parameter != functionTemplate->parameters->parameters.back())
            {
                reportError(node->line,
                            "a variadic generic function must have exactly one "
                            "trailing parameter pack");
                return nullptr;
            }
            packParameter = parameter;
        }
        else
        {
            fixedParameters.push_back(parameter);
        }
    }
    if(!packParameter || node->arguments.size() < fixedParameters.size())
    {
        reportError(node->line,
                    "invalid argument count for variadic generic function '" +
                        node->name + "'");
        return nullptr;
    }
    if(functionTemplate->typeParams.empty() ||
       functionTemplate->typeParams.back() != functionTemplate->typePackParam)
    {
        reportError(
            node->line,
            "the type parameter pack must be the final generic parameter");
        return nullptr;
    }
    auto* packType = dynamic_cast<StructTypeRefNode*>(packParameter->type);
    if(!packType || packType->structName != functionTemplate->typePackParam)
    {
        reportError(node->line, "parameter pack '" + packParameter->name +
                                    "' must use type pack '" +
                                    functionTemplate->typePackParam + "'");
        return nullptr;
    }

    std::set<std::string> fixedTypeParams;
    for(const auto& param : functionTemplate->typeParams)
        if(param != functionTemplate->typePackParam)
            fixedTypeParams.insert(param);
    std::map<std::string, TypeNode*> bindings;
    std::function<bool(TypeNode*, TypeNode*)> bindType =
        [&](TypeNode* pattern, TypeNode* concrete) -> bool
    {
        if(!pattern || !concrete)
            return false;
        if(auto* ref = dynamic_cast<ReferenceTypeNode*>(pattern))
            return bindType(ref->elementType, concrete);
        if(auto* named = dynamic_cast<StructTypeRefNode*>(pattern))
        {
            if(fixedTypeParams.count(named->structName))
            {
                auto found = bindings.find(named->structName);
                if(found == bindings.end())
                    bindings[named->structName] = cloneTypeNode(concrete);
                else if(typeMangle(found->second) != typeMangle(concrete))
                    return false;
                return true;
            }
        }
        if(auto* patternList = dynamic_cast<GenericListTypeNode*>(pattern))
        {
            auto* concreteList = dynamic_cast<GenericListTypeNode*>(concrete);
            return concreteList && bindType(patternList->elementType,
                                            concreteList->elementType);
        }
        if(auto* patternPtr = dynamic_cast<PointerTypeNode*>(pattern))
        {
            auto* concretePtr = dynamic_cast<PointerTypeNode*>(concrete);
            return concretePtr &&
                   bindType(patternPtr->elementType, concretePtr->elementType);
        }
        if(auto* patternGeneric =
               dynamic_cast<GenericStructTypeRefNode*>(pattern))
        {
            const std::vector<TypeNode*>* concreteArgs = nullptr;
            if(auto* concreteGeneric =
                   dynamic_cast<GenericStructTypeRefNode*>(concrete))
            {
                if(patternGeneric->structName == concreteGeneric->structName)
                    concreteArgs = &concreteGeneric->typeArgs;
            }
            else if(auto* concreteName =
                        dynamic_cast<StructTypeRefNode*>(concrete))
            {
                auto genericName =
                    mangledToGenericName.find(concreteName->structName);
                auto typeArgs =
                    monomorphizedTypeArgs.find(concreteName->structName);
                if(genericName != mangledToGenericName.end() &&
                   genericName->second == patternGeneric->structName &&
                   typeArgs != monomorphizedTypeArgs.end())
                    concreteArgs = &typeArgs->second;
            }
            if(!concreteArgs ||
               patternGeneric->typeArgs.size() != concreteArgs->size())
                return false;
            for(size_t i = 0; i < patternGeneric->typeArgs.size(); ++i)
                if(!bindType(patternGeneric->typeArgs[i], (*concreteArgs)[i]))
                    return false;
            return true;
        }
        return typeMangle(pattern) == typeMangle(concrete);
    };

    for(size_t i = 0; i < fixedParameters.size(); ++i)
    {
        TypeNode* pattern = fixedParameters[i]->type;
        ExpressionNode* argument = node->arguments[i];
        TypeNode* concrete = nullptr;
        if(auto* reference = dynamic_cast<ReferenceTypeNode*>(pattern))
        {
            auto* unary = dynamic_cast<UnaryOpNode*>(argument);
            const bool correctBorrow =
                unary &&
                (reference->isMutable ? unary->op == UnaryOpNode::OP_ADDR_MUT
                                      : unary->op == UnaryOpNode::OP_ADDR);
            if(!correctBorrow)
            {
                reportError(node->line, "parameter '" +
                                            fixedParameters[i]->name +
                                            "' requires an explicit reference");
                return nullptr;
            }
            concrete = inferExpressionTypeNode(unary->operand, node->line);
        }
        else
        {
            concrete = inferExpressionTypeNode(argument, node->line);
        }
        if(!bindType(pattern, concrete))
        {
            reportError(node->line, "cannot infer generic arguments for '" +
                                        node->name + "'");
            return nullptr;
        }
    }

    std::vector<std::string> fixedTypeNames;
    std::vector<TypeNode*> fixedTypeArgs;
    for(const auto& typeParam : functionTemplate->typeParams)
    {
        if(typeParam == functionTemplate->typePackParam)
            continue;
        auto found = bindings.find(typeParam);
        if(found == bindings.end())
        {
            reportError(node->line, "could not infer type parameter '" +
                                        typeParam + "' for '" + node->name +
                                        "'");
            return nullptr;
        }
        fixedTypeNames.push_back(typeParam);
        fixedTypeArgs.push_back(found->second);
    }

    std::vector<TypeNode*> packTypes;
    for(size_t i = fixedParameters.size(); i < node->arguments.size(); ++i)
    {
        TypeNode* type =
            inferExpressionTypeNode(node->arguments[i], node->line);
        if(!type)
        {
            reportError(node->line, "cannot infer parameter-pack argument " +
                                        std::to_string(i + 1));
            return nullptr;
        }
        packTypes.push_back(type);
    }

    std::string specializedName = functionTemplate->name + "__pack";
    for(auto* type : fixedTypeArgs)
        specializedName += "__" + typeMangle(type);
    for(auto* type : packTypes)
        specializedName += "__" + typeMangle(type);

    if(functionOverloads.find(specializedName) == functionOverloads.end())
    {
        auto* parameters = new ParameterListNode();
        for(auto* parameter : fixedParameters)
        {
            parameters->parameters.push_back(new ParameterNode(
                substituteTypeParams(parameter->type, fixedTypeNames,
                                     fixedTypeArgs),
                parameter->name));
        }

        std::vector<std::string> expansionNames;
        for(size_t i = 0; i < packTypes.size(); ++i)
        {
            std::string name = packParameter->name + "$" + std::to_string(i);
            parameters->parameters.push_back(
                new ParameterNode(cloneTypeNode(packTypes[i]), name));
            expansionNames.push_back(name);
        }

        auto* specialized = new FunctionDefNode(
            substituteTypeParams(functionTemplate->returnType, fixedTypeNames,
                                 fixedTypeArgs),
            specializedName, parameters, functionTemplate->body,
            functionTemplate->isPublic, false);
        specialized->line = functionTemplate->line;
        specialized->sourceModule = functionTemplate->sourceModule;
        specialized->isInline = true;
        specialized->packExpansionNames[packParameter->name] = expansionNames;
        specialized->concreteTypeBindings = bindings;
        llvm::Function* declaration = generateFunctionDeclaration(specialized);
        registerFunctionOverload(specialized, declaration);
        generateFunctionDefinition(specialized);
    }

    FunctionCallNode specializedCall(specializedName);
    specializedCall.arguments = node->arguments;
    specializedCall.line = node->line;
    specializedCall.col = node->col;
    return generateFunctionCall(&specializedCall);
}

bool CodeGenerator::inferGenericFunctionTypeBindings(
    FunctionDefNode* functionTemplate,
    const std::vector<ExpressionNode*>& arguments,
    std::map<std::string, TypeNode*>& bindings)
{
    if(!functionTemplate || !functionTemplate->parameters ||
       functionTemplate->parameters->parameters.size() != arguments.size())
        return false;

    std::set<std::string> typeParamNames(functionTemplate->typeParams.begin(),
                                         functionTemplate->typeParams.end());
    std::function<bool(TypeNode*, TypeNode*)> bindType =
        [&](TypeNode* pattern, TypeNode* concrete) -> bool
    {
        if(!pattern || !concrete)
            return false;
        if(auto* patternRef = dynamic_cast<ReferenceTypeNode*>(pattern))
        {
            if(auto* concreteRef = dynamic_cast<ReferenceTypeNode*>(concrete))
                concrete = concreteRef->elementType;
            return bindType(patternRef->elementType, concrete);
        }
        if(auto* patternFunction =
               dynamic_cast<FunctionTypeNode*>(pattern))
        {
            auto* concreteFunction =
                dynamic_cast<FunctionTypeNode*>(concrete);
            const size_t patternCount =
                patternFunction->parameterTypes
                    ? patternFunction->parameterTypes->types.size()
                    : 0;
            const size_t concreteCount =
                concreteFunction && concreteFunction->parameterTypes
                    ? concreteFunction->parameterTypes->types.size()
                    : 0;
            if(!concreteFunction || patternCount != concreteCount)
                return false;
            for(size_t i = 0; i < patternCount; ++i)
                if(!bindType(patternFunction->parameterTypes->types[i],
                             concreteFunction->parameterTypes->types[i]))
                    return false;
            return bindType(patternFunction->returnType,
                            concreteFunction->returnType);
        }
        if(auto* patternName = dynamic_cast<StructTypeRefNode*>(pattern))
        {
            if(typeParamNames.count(patternName->structName))
            {
                auto found = bindings.find(patternName->structName);
                if(found == bindings.end())
                    bindings[patternName->structName] = cloneTypeNode(concrete);
                else if(typeMangle(found->second) != typeMangle(concrete))
                    return false;
                return true;
            }
        }
        if(auto* patternMulti = dynamic_cast<MultiArrayTypeNode*>(pattern))
        {
            auto* concreteMulti = dynamic_cast<MultiArrayTypeNode*>(concrete);
            return concreteMulti &&
                   patternMulti->capacity == concreteMulti->capacity &&
                   patternMulti->elementsMutable ==
                       concreteMulti->elementsMutable &&
                   bindType(patternMulti->elementType,
                            concreteMulti->elementType);
        }
        if(auto* patternArray = dynamic_cast<ArrayTypeNode*>(pattern))
        {
            auto* concreteArray = dynamic_cast<ArrayTypeNode*>(concrete);
            return concreteArray &&
                   patternArray->capacity == concreteArray->capacity &&
                   bindType(patternArray->elementType,
                            concreteArray->elementType);
        }
        if(auto* patternList = dynamic_cast<GenericListTypeNode*>(pattern))
        {
            auto* concreteList = dynamic_cast<GenericListTypeNode*>(concrete);
            return concreteList && bindType(patternList->elementType,
                                            concreteList->elementType);
        }
        if(auto* patternMap = dynamic_cast<MapTypeNode*>(pattern))
        {
            auto* concreteMap = dynamic_cast<MapTypeNode*>(concrete);
            return concreteMap &&
                   bindType(patternMap->keyType, concreteMap->keyType) &&
                   bindType(patternMap->valueType, concreteMap->valueType);
        }
        if(auto* patternTuple = dynamic_cast<TupleTypeNode*>(pattern))
        {
            auto* concreteTuple = dynamic_cast<TupleTypeNode*>(concrete);
            if(!concreteTuple || !patternTuple->elementTypes ||
               !concreteTuple->elementTypes ||
               patternTuple->elementTypes->types.size() !=
                   concreteTuple->elementTypes->types.size())
                return false;
            for(size_t i = 0; i < patternTuple->elementTypes->types.size(); ++i)
                if(!bindType(patternTuple->elementTypes->types[i],
                             concreteTuple->elementTypes->types[i]))
                    return false;
            return true;
        }
        if(auto* patternPtr = dynamic_cast<PointerTypeNode*>(pattern))
        {
            auto* concretePtr = dynamic_cast<PointerTypeNode*>(concrete);
            return concretePtr &&
                   bindType(patternPtr->elementType, concretePtr->elementType);
        }
        if(auto* patternGeneric =
               dynamic_cast<GenericStructTypeRefNode*>(pattern))
        {
            const std::vector<TypeNode*>* concreteArgs = nullptr;
            if(auto* concreteGeneric =
                   dynamic_cast<GenericStructTypeRefNode*>(concrete))
            {
                if(patternGeneric->structName == concreteGeneric->structName)
                    concreteArgs = &concreteGeneric->typeArgs;
            }
            else if(auto* concreteName =
                        dynamic_cast<StructTypeRefNode*>(concrete))
            {
                auto genericName =
                    mangledToGenericName.find(concreteName->structName);
                auto typeArgs =
                    monomorphizedTypeArgs.find(concreteName->structName);
                if(genericName != mangledToGenericName.end() &&
                   genericName->second == patternGeneric->structName &&
                   typeArgs != monomorphizedTypeArgs.end())
                    concreteArgs = &typeArgs->second;
            }
            if(!concreteArgs ||
               patternGeneric->typeArgs.size() != concreteArgs->size())
                return false;
            for(size_t i = 0; i < patternGeneric->typeArgs.size(); ++i)
                if(!bindType(patternGeneric->typeArgs[i], (*concreteArgs)[i]))
                    return false;
            return true;
        }
        return typeMangle(pattern) == typeMangle(concrete);
    };

    // Closure parameters are intentionally represented as opaque pointers in
    // the generic argument list. When the function's return type contains an
    // otherwise-unbound type parameter (for example list<Output> in
    // ranges::transform), infer that parameter from the closure's return
    // expression. Keep this local to overload probing: callback parameters
    // must not leak into the enclosing function's variable scopes.
    auto inferClosureResult = [&](ClosureNode* closure) -> TypeNode*
    {
        if(!closure || !closure->body)
            return nullptr;
        std::map<std::string, TypeNode*> parametersByName;
        std::vector<std::map<std::string, TypeNode*>> localScopes(1);
        if(closure->parameters)
            for(auto* parameter : closure->parameters->parameters)
                if(parameter && parameter->type)
                    parametersByName[parameter->name] = parameter->type;

        std::function<TypeNode*(ExpressionNode*)> inferClosureExpr =
            [&](ExpressionNode* expr) -> TypeNode*
        {
            if(!expr)
                return nullptr;
            if(auto* identifier = dynamic_cast<IdentifierNode*>(expr))
            {
                for(auto scope = localScopes.rbegin();
                    scope != localScopes.rend(); ++scope)
                {
                    auto local = scope->find(identifier->name);
                    if(local != scope->end())
                        return cloneTypeNode(local->second);
                }
                auto found = parametersByName.find(identifier->name);
                if(found != parametersByName.end())
                    return cloneTypeNode(found->second);
                return inferExpressionTypeNode(expr, functionTemplate->line);
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
            {
                TypeNode* objectType = nullptr;
                if(field->object)
                    objectType = inferClosureExpr(field->object);
                else
                {
                    auto parameter =
                        parametersByName.find(field->structName);
                    if(parameter != parametersByName.end())
                        objectType = cloneTypeNode(parameter->second);
                    else
                        objectType = inferExpressionTypeNode(
                            new IdentifierNode(field->structName),
                            functionTemplate->line);
                }
                if(auto* reference =
                       dynamic_cast<ReferenceTypeNode*>(objectType))
                    objectType = reference->elementType;
                std::string structName;
                if(auto* structRef =
                       dynamic_cast<StructTypeRefNode*>(objectType))
                {
                    structName =
                        resolveVisibleStructName(structRef->structName);
                    if(structName.empty())
                        structName = structRef->structName;
                }
                else if(auto* genericStruct =
                            dynamic_cast<GenericStructTypeRefNode*>(objectType))
                    structName = getOrCreateMonomorphizedStruct(
                        genericStruct->structName, genericStruct->typeArgs);
                auto members = structMembers.find(structName);
                if(members != structMembers.end())
                    for(const auto& member : members->second)
                        if(member.first == field->fieldName)
                            return cloneTypeNode(member.second);
                return inferExpressionTypeNode(expr, functionTemplate->line);
            }
            if(auto* tuple = dynamic_cast<TupleLiteralNode*>(expr))
            {
                auto* elementTypes = new TypeListNode();
                if(tuple->elements)
                    for(auto* element : tuple->elements->elements)
                    {
                        TypeNode* elementType = inferClosureExpr(element);
                        if(!elementType)
                            return nullptr;
                        elementTypes->addType(elementType);
                    }
                return new TupleTypeNode(elementTypes);
            }
            if(auto* access = dynamic_cast<TupleAccessNode*>(expr))
            {
                TypeNode* tupleType = inferClosureExpr(access->tuple);
                if(auto* tuple = dynamic_cast<TupleTypeNode*>(tupleType))
                {
                    if(tuple->elementTypes && access->index >= 0 &&
                       static_cast<size_t>(access->index) <
                           tuple->elementTypes->types.size())
                        return cloneTypeNode(
                            tuple->elementTypes->types[access->index]);
                }
                return nullptr;
            }
            if(auto* list = dynamic_cast<ListLiteralNode*>(expr))
            {
                if(!list->elements || list->elements->elements.empty())
                    return nullptr;
                TypeNode* elementType = nullptr;
                for(auto* element : list->elements->elements)
                {
                    TypeNode* current = inferClosureExpr(element);
                    if(!current ||
                       (elementType &&
                        typeMangle(elementType) != typeMangle(current)))
                        return nullptr;
                    if(!elementType)
                        elementType = current;
                }
                return new GenericListTypeNode(elementType);
            }
            if(auto* array = dynamic_cast<ArrayFillNode*>(expr))
            {
                TypeNode* elementType = inferClosureExpr(array->value);
                if(!elementType)
                    return nullptr;
                return new GenericListTypeNode(elementType);
            }
            if(auto* index = dynamic_cast<IndexExpressionNode*>(expr))
            {
                TypeNode* baseType = inferClosureExpr(index->base);
                if(auto* list =
                       dynamic_cast<GenericListTypeNode*>(baseType))
                    return cloneTypeNode(list->elementType);
                if(auto* map = dynamic_cast<MapTypeNode*>(baseType))
                    return cloneTypeNode(map->valueType);
                return nullptr;
            }
            if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
            {
                auto overloads = functionOverloads.find(call->name);
                if(overloads == functionOverloads.end())
                    return nullptr;

                TypeNode* selectedResult = nullptr;
                size_t matches = 0;
                for(const auto& overload : overloads->second)
                {
                    FunctionDefNode* candidate = overload.node;
                    if(!isOverloadVisible(overload) || !candidate ||
                       !candidate->parameters ||
                       candidate->parameters->isVarArg ||
                       candidate->parameters->parameters.size() !=
                           call->arguments.size() ||
                       !candidate->returnType || candidate->isCexpr)
                        continue;

                    TypeNode* resultType = nullptr;
                    if(candidate->typeParams.empty())
                    {
                        bool argumentsMatch = true;
                        for(size_t i = 0; i < call->arguments.size(); ++i)
                        {
                            TypeNode* actual = inferClosureExpr(
                                call->arguments[i]);
                            TypeNode* expected =
                                candidate->parameters->parameters[i]->type;
                            if(!actual || !expected ||
                               typeMangle(actual) != typeMangle(expected))
                            {
                                argumentsMatch = false;
                                break;
                            }
                        }
                        if(argumentsMatch)
                            resultType = cloneTypeNode(candidate->returnType);
                    }
                    else
                    {
                        std::map<std::string, TypeNode*> callBindings;
                        std::set<std::string> callTypeParams(
                            candidate->typeParams.begin(),
                            candidate->typeParams.end());
                        std::function<bool(TypeNode*, TypeNode*)> bindCallType =
                            [&](TypeNode* pattern, TypeNode* actual)
                        {
                            if(!pattern || !actual)
                                return false;
                            if(auto* reference =
                                   dynamic_cast<ReferenceTypeNode*>(pattern))
                            {
                                if(auto* actualReference =
                                       dynamic_cast<ReferenceTypeNode*>(actual))
                                {
                                    if(reference->isMutable !=
                                       actualReference->isMutable)
                                        return false;
                                    actual = actualReference->elementType;
                                }
                                return bindCallType(reference->elementType,
                                                    actual);
                            }
                            if(auto* name =
                                   dynamic_cast<StructTypeRefNode*>(pattern))
                            {
                                if(callTypeParams.count(name->structName))
                                {
                                    auto inserted = callBindings.emplace(
                                        name->structName,
                                        cloneTypeNode(actual));
                                    return inserted.second ||
                                        typeMangle(inserted.first->second) ==
                                            typeMangle(actual);
                                }
                            }
                            if(auto* patternMulti =
                                   dynamic_cast<MultiArrayTypeNode*>(pattern))
                            {
                                auto* actualMulti =
                                    dynamic_cast<MultiArrayTypeNode*>(actual);
                                return actualMulti &&
                                    patternMulti->capacity ==
                                        actualMulti->capacity &&
                                    patternMulti->elementsMutable ==
                                        actualMulti->elementsMutable &&
                                    bindCallType(patternMulti->elementType,
                                                 actualMulti->elementType);
                            }
                            if(auto* patternArray =
                                   dynamic_cast<ArrayTypeNode*>(pattern))
                            {
                                auto* actualArray =
                                    dynamic_cast<ArrayTypeNode*>(actual);
                                return actualArray &&
                                    patternArray->capacity ==
                                        actualArray->capacity &&
                                    bindCallType(patternArray->elementType,
                                                 actualArray->elementType);
                            }
                            if(auto* patternList =
                                   dynamic_cast<GenericListTypeNode*>(pattern))
                            {
                                auto* actualList =
                                    dynamic_cast<GenericListTypeNode*>(actual);
                                return actualList && bindCallType(
                                    patternList->elementType,
                                    actualList->elementType);
                            }
                            if(auto* patternMap =
                                   dynamic_cast<MapTypeNode*>(pattern))
                            {
                                auto* actualMap =
                                    dynamic_cast<MapTypeNode*>(actual);
                                return actualMap && bindCallType(
                                    patternMap->keyType, actualMap->keyType) &&
                                    bindCallType(patternMap->valueType,
                                                 actualMap->valueType);
                            }
                            if(auto* patternTuple =
                                   dynamic_cast<TupleTypeNode*>(pattern))
                            {
                                auto* actualTuple =
                                    dynamic_cast<TupleTypeNode*>(actual);
                                if(!actualTuple || !patternTuple->elementTypes ||
                                   !actualTuple->elementTypes ||
                                   patternTuple->elementTypes->types.size() !=
                                       actualTuple->elementTypes->types.size())
                                    return false;
                                for(size_t n = 0;
                                    n < patternTuple->elementTypes->types.size();
                                    ++n)
                                    if(!bindCallType(
                                           patternTuple->elementTypes->types[n],
                                           actualTuple->elementTypes->types[n]))
                                        return false;
                                return true;
                            }
                            if(auto* patternPointer =
                                   dynamic_cast<PointerTypeNode*>(pattern))
                            {
                                auto* actualPointer =
                                    dynamic_cast<PointerTypeNode*>(actual);
                                return actualPointer && bindCallType(
                                    patternPointer->elementType,
                                    actualPointer->elementType);
                            }
                            if(auto* patternFunction =
                                   dynamic_cast<FunctionTypeNode*>(pattern))
                            {
                                auto* actualFunction =
                                    dynamic_cast<FunctionTypeNode*>(actual);
                                const size_t patternCount =
                                    patternFunction->parameterTypes
                                        ? patternFunction->parameterTypes
                                              ->types.size()
                                        : 0;
                                const size_t actualCount =
                                    actualFunction &&
                                            actualFunction->parameterTypes
                                        ? actualFunction->parameterTypes
                                              ->types.size()
                                        : 0;
                                if(!actualFunction ||
                                   patternCount != actualCount)
                                    return false;
                                for(size_t n = 0; n < patternCount; ++n)
                                    if(!bindCallType(
                                           patternFunction->parameterTypes
                                               ->types[n],
                                           actualFunction->parameterTypes
                                               ->types[n]))
                                        return false;
                                return bindCallType(
                                    patternFunction->returnType,
                                    actualFunction->returnType);
                            }
                            if(auto* patternGeneric =
                                   dynamic_cast<GenericStructTypeRefNode*>(
                                       pattern))
                            {
                                const std::vector<TypeNode*>* actualArgs =
                                    nullptr;
                                if(auto* actualGeneric =
                                       dynamic_cast<GenericStructTypeRefNode*>(
                                           actual))
                                {
                                    if(patternGeneric->structName ==
                                       actualGeneric->structName)
                                        actualArgs = &actualGeneric->typeArgs;
                                }
                                else if(auto* actualStruct =
                                            dynamic_cast<StructTypeRefNode*>(
                                                actual))
                                {
                                    auto genericName =
                                        mangledToGenericName.find(
                                            actualStruct->structName);
                                    auto typeArgs = monomorphizedTypeArgs.find(
                                        actualStruct->structName);
                                    if(genericName !=
                                           mangledToGenericName.end() &&
                                       genericName->second ==
                                           patternGeneric->structName &&
                                       typeArgs != monomorphizedTypeArgs.end())
                                        actualArgs = &typeArgs->second;
                                }
                                if(!actualArgs ||
                                   patternGeneric->typeArgs.size() !=
                                       actualArgs->size())
                                    return false;
                                for(size_t n = 0;
                                    n < patternGeneric->typeArgs.size(); ++n)
                                    if(!bindCallType(
                                           patternGeneric->typeArgs[n],
                                           (*actualArgs)[n]))
                                        return false;
                                return true;
                            }
                            return typeMangle(pattern) == typeMangle(actual);
                        };
                        bool matched = true;
                        for(size_t i = 0; i < call->arguments.size(); ++i)
                        {
                            TypeNode* actual =
                                inferClosureExpr(call->arguments[i]);
                            if(!actual || !bindCallType(
                                              candidate->parameters
                                                  ->parameters[i]->type,
                                              actual))
                            {
                                matched = false;
                                break;
                            }
                        }
                        std::vector<TypeNode*> typeArgs;
                        if(matched)
                            for(const auto& name : candidate->typeParams)
                            {
                                auto binding = callBindings.find(name);
                                if(binding == callBindings.end())
                                {
                                    matched = false;
                                    break;
                                }
                                typeArgs.push_back(binding->second);
                            }
                        if(matched)
                            resultType = substituteTypeParams(
                                candidate->returnType,
                                candidate->typeParams, typeArgs);
                    }

                    if(resultType)
                    {
                        ++matches;
                        selectedResult = resultType;
                    }
                }
                return matches == 1 ? selectedResult : nullptr;
            }
            if(auto* invoke = dynamic_cast<CallableInvokeNode*>(expr))
            {
                auto* callableType = dynamic_cast<FunctionTypeNode*>(
                    inferClosureExpr(invoke->callable));
                if(!callableType)
                    return nullptr;
                const size_t parameterCount = callableType->parameterTypes
                    ? callableType->parameterTypes->types.size()
                    : 0;
                if(parameterCount != invoke->arguments.size())
                    return nullptr;
                return cloneTypeNode(callableType->returnType);
            }
            if(auto* method = dynamic_cast<MethodCallNode*>(expr))
            {
                TypeNode* receiverType = inferClosureExpr(method->object);
                if(auto* reference =
                       dynamic_cast<ReferenceTypeNode*>(receiverType))
                    receiverType = reference->elementType;
                std::string receiverStructName;
                if(auto* structRef =
                       dynamic_cast<StructTypeRefNode*>(receiverType))
                {
                    receiverStructName =
                        resolveVisibleStructName(structRef->structName);
                    if(receiverStructName.empty())
                        receiverStructName = structRef->structName;
                }
                else if(auto* genericStruct =
                            dynamic_cast<GenericStructTypeRefNode*>(
                                receiverType))
                    receiverStructName = getOrCreateMonomorphizedStruct(
                        genericStruct->structName, genericStruct->typeArgs);

                auto methods = structMethods.find(receiverStructName);
                if(methods != structMethods.end())
                {
                    auto selected = methods->second.find(method->methodName);
                    if(selected != methods->second.end() &&
                       selected->second.second &&
                       selected->second.second->returnType)
                        return cloneTypeNode(
                            selected->second.second->returnType);
                }
                if(method->methodName == "len")
                    return new TypeNode(TypeNode::TYPE_I64);
                if(method->methodName == "is_empty" ||
                   method->methodName == "is_ok" ||
                   method->methodName == "is_err" ||
                   method->methodName == "is_some" ||
                   method->methodName == "is_none")
                    return new TypeNode(TypeNode::TYPE_BOOL);
                if(method->methodName == "clone")
                    return inferClosureExpr(method->object);
            }
            if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
            {
                if(unary->op == UnaryOpNode::OP_NOT)
                    return new TypeNode(TypeNode::TYPE_BOOL);
                if(unary->op == UnaryOpNode::OP_NEG ||
                   unary->op == UnaryOpNode::OP_BITNOT)
                    return inferClosureExpr(unary->operand);
            }
            if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
            {
                TypeNode* trueType = inferClosureExpr(ternary->trueExpr);
                TypeNode* falseType = inferClosureExpr(ternary->falseExpr);
                if(trueType && falseType &&
                   typeMangle(trueType) != typeMangle(falseType))
                    return nullptr;
                return trueType ? trueType : falseType;
            }
            if(auto* match = dynamic_cast<MatchExpressionNode*>(expr))
            {
                TypeNode* resultType = nullptr;
                if(match->arms.empty())
                    return nullptr;
                for(auto* arm : match->arms)
                {
                    if(!arm || !arm->expression)
                        return nullptr;
                    TypeNode* armType = inferClosureExpr(arm->expression);
                    if(!armType ||
                       (resultType && typeMangle(resultType) !=
                                          typeMangle(armType)))
                        return nullptr;
                    if(!resultType)
                        resultType = armType;
                }
                return resultType;
            }
            if(auto* binary = dynamic_cast<BinaryOpNode*>(expr))
            {
                switch(binary->op)
                {
                case BinaryOpNode::OP_LT:
                case BinaryOpNode::OP_GT:
                case BinaryOpNode::OP_LE:
                case BinaryOpNode::OP_GE:
                case BinaryOpNode::OP_EQ:
                case BinaryOpNode::OP_NE:
                case BinaryOpNode::OP_AND:
                case BinaryOpNode::OP_OR:
                    return new TypeNode(TypeNode::TYPE_BOOL);
                default:
                    break;
                }
                TypeNode* left = inferClosureExpr(binary->left);
                TypeNode* right = inferClosureExpr(binary->right);
                if(left && (left->kind == TypeNode::TYPE_STR8 ||
                            left->kind == TypeNode::TYPE_STR16 ||
                            left->kind == TypeNode::TYPE_STRING))
                    return left;
                if(right && (right->kind == TypeNode::TYPE_DOUBLE ||
                             right->kind == TypeNode::TYPE_FLOAT))
                    return right;
                return left ? left : right;
            }
            if(auto* cast = dynamic_cast<CastExpressionNode*>(expr))
                return new TypeNode(cast->targetType);
            if(dynamic_cast<BoolLiteralNode*>(expr))
                return new TypeNode(TypeNode::TYPE_BOOL);
            if(dynamic_cast<IntLiteralNode*>(expr))
                return new TypeNode(TypeNode::TYPE_I64);
            if(dynamic_cast<FloatLiteralNode*>(expr))
                return new TypeNode(TypeNode::TYPE_FLOAT);
            if(dynamic_cast<DoubleLiteralNode*>(expr))
                return new TypeNode(TypeNode::TYPE_DOUBLE);
            if(dynamic_cast<StringLiteralNode*>(expr) ||
               dynamic_cast<FormatNode*>(expr))
                return new TypeNode(TypeNode::TYPE_STR8);
            return inferExpressionTypeNode(expr, functionTemplate->line);
        };

        TypeNode* result = nullptr;
        bool resultIsConsistent = true;
        std::function<void(StatementNode*)> inferReturns =
            [&](StatementNode* statement)
        {
            if(auto* let = dynamic_cast<LetDeclNode*>(statement))
            {
                TypeNode* localType = let->type
                                          ? cloneTypeNode(let->type)
                                          : inferClosureExpr(let->expression);
                if(localType)
                    localScopes.back()[let->name] = localType;
                return;
            }
            if(auto* variable = dynamic_cast<VarDeclNode*>(statement))
            {
                TypeNode* localType = variable->type
                                          ? cloneTypeNode(variable->type)
                                          : inferClosureExpr(variable->initExpr);
                if(localType)
                    localScopes.back()[variable->name] = localType;
                return;
            }
            if(auto* ret = dynamic_cast<ReturnNode*>(statement))
            {
                if(!ret->expression)
                    return;
                TypeNode* returned = inferClosureExpr(ret->expression);
                if(!returned ||
                   (result && typeMangle(result) != typeMangle(returned)))
                {
                    resultIsConsistent = false;
                    return;
                }
                result = returned;
                return;
            }
            auto inferList = [&](StatementListNode* statements)
            {
                if(statements)
                {
                    localScopes.emplace_back();
                    for(auto* nested : statements->statements)
                        inferReturns(nested);
                    localScopes.pop_back();
                }
            };
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                inferList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                inferList(branch->thenBranch);
                inferList(branch->elseBranch);
                inferReturns(branch->elseIfBranch);
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                inferList(branch->thenBranch);
                inferList(branch->elseBranch);
                inferReturns(branch->elseIfBranch);
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
                inferList(loop->body);
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
                inferList(loop->body);
            else if(auto* tryCatch = dynamic_cast<TryCatchNode*>(statement))
            {
                inferReturns(tryCatch->tryBlock);
                inferReturns(tryCatch->catchBlock);
            }
        };
        for(auto* statement : closure->body->statements)
            inferReturns(statement);
        if(!resultIsConsistent)
            return nullptr;
        return result;
    };

    auto inferClosureCallableType = [&](ClosureNode* closure) -> TypeNode*
    {
        if(!closure)
            return nullptr;
        auto* parameters = new TypeListNode();
        if(closure->parameters)
        {
            for(auto* parameter : closure->parameters->parameters)
            {
                if(!parameter || !parameter->type)
                    return nullptr;
                parameters->addType(cloneTypeNode(parameter->type));
            }
        }
        TypeNode* result = inferClosureResult(closure);
        if(!result)
            return nullptr;
        return new FunctionTypeNode(parameters, result);
    };

    for(size_t i = 0; i < arguments.size(); ++i)
    {
        TypeNode* concrete = nullptr;
        TypeNode* parameterType =
            functionTemplate->parameters->parameters[i]->type;
        if(auto* reference = dynamic_cast<ReferenceTypeNode*>(parameterType))
        {
            // Address-of expressions infer as pointer types, but generic
            // reference parameters bind against the referenced value type.
            // Validate the explicit borrow here just as we do for variadic
            // generic functions, then infer from its operand. Immutable
            // references also accept ordinary values, preserving the usual
            // implicit shared-borrow behavior.
            auto* borrow = dynamic_cast<UnaryOpNode*>(arguments[i]);
            const bool correctBorrow =
                borrow &&
                (reference->isMutable ? borrow->op == UnaryOpNode::OP_ADDR_MUT
                                      : borrow->op == UnaryOpNode::OP_ADDR);
            if(correctBorrow)
            {
                concrete = inferExpressionTypeNode(borrow->operand,
                                                   functionTemplate->line);
            }
            else if(reference->isMutable)
            {
                return false;
            }
            else
            {
                concrete = inferExpressionTypeNode(arguments[i],
                                                   functionTemplate->line);
            }
        }
        // A named inline closure is not a normal local value: it lives in
        // closureVariables and is expanded into the generic specialization.
        // Resolve it here before ordinary identifier inference, otherwise
        // overload probing can diagnose the closure name as an unknown local.
        else
        {
            if(dynamic_cast<FunctionTypeNode*>(parameterType))
            {
                if(auto* identifier =
                       dynamic_cast<IdentifierNode*>(arguments[i]))
                {
                    auto overloads = functionOverloads.find(identifier->name);
                    if(overloads != functionOverloads.end() &&
                       !functionTypeVariables.count(identifier->name) &&
                       !namedValues.count(identifier->name) &&
                       !globalNamedValues.count(identifier->name))
                        continue; // Resolve after other arguments bind T.
                    auto callable =
                        functionTypeVariables.find(identifier->name);
                    if(callable != functionTypeVariables.end())
                        concrete = callable->second;
                    else
                    {
                        auto closure = closureVariables.find(identifier->name);
                        if(closure != closureVariables.end())
                            concrete = inferClosureCallableType(
                                closure->second);
                    }
                }
                else if(auto* closure =
                            dynamic_cast<ClosureNode*>(arguments[i]))
                    concrete = inferClosureCallableType(closure);
            }
            if(!concrete)
            {
                bool isBoundClosure =
                    dynamic_cast<ClosureNode*>(arguments[i]) != nullptr;
                if(auto* identifier =
                       dynamic_cast<IdentifierNode*>(arguments[i]))
                    isBoundClosure =
                        closureVariables.count(identifier->name) != 0;
                if(isBoundClosure &&
                   !dynamic_cast<FunctionTypeNode*>(parameterType))
                    concrete = new PointerTypeNode(
                        new TypeNode(TypeNode::TYPE_VOID));
                else if(!isBoundClosure)
                    concrete = inferExpressionTypeNode(
                        arguments[i], functionTemplate->line);
            }
        }
        if(!concrete)
        {
            bool isClosureArgument =
                dynamic_cast<ClosureNode*>(arguments[i]) != nullptr;
            if(auto* identifier =
                   dynamic_cast<IdentifierNode*>(arguments[i]))
                isClosureArgument =
                    closureVariables.count(identifier->name) != 0;
            if(isClosureArgument &&
               dynamic_cast<FunctionTypeNode*>(parameterType))
                continue; // Infer after ordinary arguments bind type params.
            return false;
        }
        if(!bindType(parameterType, concrete))
            return false;
    }

    // A lambda may omit parameter annotations when the remaining call
    // arguments determine its generic callback signature, as in apply(|x| x,
    // value). Temporarily supply any now-concrete parameter types, infer the
    // lambda result, and restore the parser AST before overload probing ends.
    std::function<bool(TypeNode*)> hasUnboundTypeParameter =
        [&](TypeNode* type) -> bool
    {
        if(!type)
            return true;
        if(auto* named = dynamic_cast<StructTypeRefNode*>(type))
            return typeParamNames.count(named->structName) &&
                   bindings.count(named->structName) == 0;
        if(auto* generic = dynamic_cast<GenericStructTypeRefNode*>(type))
        {
            if(std::find(functionTemplate->typeParams.begin(),
                         functionTemplate->typeParams.end(),
                         generic->structName) !=
               functionTemplate->typeParams.end() &&
               bindings.count(generic->structName) == 0)
                return true;
            return std::any_of(generic->typeArgs.begin(), generic->typeArgs.end(),
                               hasUnboundTypeParameter);
        }
        if(auto* list = dynamic_cast<GenericListTypeNode*>(type))
            return hasUnboundTypeParameter(list->elementType);
        if(auto* array = dynamic_cast<ArrayTypeNode*>(type))
            return hasUnboundTypeParameter(array->elementType);
        if(auto* multi = dynamic_cast<MultiArrayTypeNode*>(type))
            return hasUnboundTypeParameter(multi->elementType);
        if(auto* map = dynamic_cast<MapTypeNode*>(type))
            return hasUnboundTypeParameter(map->keyType) ||
                   hasUnboundTypeParameter(map->valueType);
        if(auto* tuple = dynamic_cast<TupleTypeNode*>(type))
            return !tuple->elementTypes ||
                   std::any_of(tuple->elementTypes->types.begin(),
                               tuple->elementTypes->types.end(),
                               hasUnboundTypeParameter);
        if(auto* pointer = dynamic_cast<PointerTypeNode*>(type))
            return hasUnboundTypeParameter(pointer->elementType);
        if(auto* reference = dynamic_cast<ReferenceTypeNode*>(type))
            return hasUnboundTypeParameter(reference->elementType);
        if(auto* function = dynamic_cast<FunctionTypeNode*>(type))
        {
            if(function->parameterTypes &&
               std::any_of(function->parameterTypes->types.begin(),
                           function->parameterTypes->types.end(),
                           hasUnboundTypeParameter))
                return true;
            return hasUnboundTypeParameter(function->returnType);
        }
        return false;
    };

    for(size_t i = 0; i < arguments.size(); ++i)
    {
        auto* pattern = dynamic_cast<FunctionTypeNode*>(
            functionTemplate->parameters->parameters[i]->type);
        ClosureNode* closure = dynamic_cast<ClosureNode*>(arguments[i]);
        if(auto* identifier = dynamic_cast<IdentifierNode*>(arguments[i]))
        {
            auto found = closureVariables.find(identifier->name);
            if(found != closureVariables.end())
                closure = found->second;
        }
        if(!pattern || !closure || !closure->parameters ||
           !pattern->parameterTypes ||
           pattern->parameterTypes->types.size() !=
               closure->parameters->parameters.size())
            continue;

        std::vector<TypeNode*> typeArgs;
        typeArgs.reserve(functionTemplate->typeParams.size());
        for(const auto& name : functionTemplate->typeParams)
        {
            auto found = bindings.find(name);
            typeArgs.push_back(found == bindings.end()
                                   ? static_cast<TypeNode*>(
                                         new StructTypeRefNode(name))
                                   : cloneTypeNode(found->second));
        }
        auto* context = dynamic_cast<FunctionTypeNode*>(substituteTypeParams(
            pattern, functionTemplate->typeParams, typeArgs));
        if(!context || !context->parameterTypes)
            return false;

        std::vector<std::pair<ParameterNode*, TypeNode*>> savedParameterTypes;
        bool canInfer = true;
        for(size_t parameterIndex = 0;
            parameterIndex < closure->parameters->parameters.size();
            ++parameterIndex)
        {
            ParameterNode* parameter =
                closure->parameters->parameters[parameterIndex];
            TypeNode* expected =
                context->parameterTypes->types[parameterIndex];
            if(!parameter->type)
            {
                if(hasUnboundTypeParameter(expected))
                {
                    canInfer = false;
                    break;
                }
                savedParameterTypes.emplace_back(parameter, nullptr);
                parameter->type = cloneTypeNode(expected);
            }
        }
        TypeNode* concrete = canInfer ? inferClosureCallableType(closure)
                                      : nullptr;
        for(const auto& saved : savedParameterTypes)
            saved.first->type = saved.second;
        if(!concrete || !bindType(pattern, concrete))
            return false;
    }

    // Named overloads used as generic callbacks must be resolved against the
    // function type after ordinary arguments have supplied any type bindings.
    for(size_t i = 0; i < arguments.size(); ++i)
    {
        auto* expectedFunction = dynamic_cast<FunctionTypeNode*>(
            functionTemplate->parameters->parameters[i]->type);
        auto* identifier = dynamic_cast<IdentifierNode*>(arguments[i]);
        if(!expectedFunction || !identifier ||
           functionTypeVariables.count(identifier->name) ||
           namedValues.count(identifier->name) ||
           globalNamedValues.count(identifier->name))
            continue;
        auto overloads = functionOverloads.find(identifier->name);
        if(overloads == functionOverloads.end())
            continue;

        std::map<std::string, TypeNode*> selectedBindings;
        size_t matches = 0;
        for(const auto& overload : overloads->second)
        {
            if(!isOverloadVisible(overload) || !overload.node ||
               !overload.function || !overload.node->parameters ||
               overload.node->parameters->isVarArg ||
               !overload.node->typeParams.empty())
                continue;
            auto* parameters = new TypeListNode();
            for(auto* parameter : overload.node->parameters->parameters)
                parameters->addType(cloneTypeNode(parameter->type));
            auto* actualFunction = new FunctionTypeNode(
                parameters, cloneTypeNode(overload.node->returnType));
            auto savedBindings = bindings;
            if(bindType(expectedFunction, actualFunction))
            {
                ++matches;
                selectedBindings = bindings;
            }
            bindings = std::move(savedBindings);
        }
        if(matches != 1)
            return false;
        bindings = std::move(selectedBindings);
    }

    auto bindUnboundReturnType = [&](TypeNode* pattern,
                                     TypeNode* concrete) -> bool
    {
        // Prefer structural matching when the callback result has the same
        // shape as the generic function's return type (for example
        // result<Output, E> in expected::and_then). Only fall back to binding
        // the first unbound parameter for APIs such as ranges::transform,
        // where the callback returns an element but the function returns a
        // container of that element.
        auto savedBindings = bindings;
        if(bindType(pattern, concrete))
            return true;
        bindings = std::move(savedBindings);

        std::function<bool(TypeNode*)> bindFirst = [&](TypeNode* current)
        {
            if(!current)
                return false;
            if(auto* name = dynamic_cast<StructTypeRefNode*>(current))
            {
                if(typeParamNames.count(name->structName) &&
                   bindings.count(name->structName) == 0)
                {
                    bindings[name->structName] = cloneTypeNode(concrete);
                    return true;
                }
                return false;
            }
            if(auto* list = dynamic_cast<GenericListTypeNode*>(current))
                return bindFirst(list->elementType);
            if(auto* array = dynamic_cast<ArrayTypeNode*>(current))
                return bindFirst(array->elementType);
            if(auto* multi = dynamic_cast<MultiArrayTypeNode*>(current))
                return bindFirst(multi->elementType);
            if(auto* pointer = dynamic_cast<PointerTypeNode*>(current))
                return bindFirst(pointer->elementType);
            if(auto* reference = dynamic_cast<ReferenceTypeNode*>(current))
                return bindFirst(reference->elementType);
            if(auto* tuple = dynamic_cast<TupleTypeNode*>(current))
                if(tuple->elementTypes)
                    for(auto* element : tuple->elementTypes->types)
                        if(bindFirst(element))
                            return true;
            if(auto* map = dynamic_cast<MapTypeNode*>(current))
                return bindFirst(map->valueType);
            if(auto* generic = dynamic_cast<GenericStructTypeRefNode*>(current))
                for(auto* argument : generic->typeArgs)
                    if(bindFirst(argument))
                        return true;
            return false;
        };
        return bindFirst(pattern);
    };
    for(size_t i = 0; i < arguments.size(); ++i)
    {
        ClosureNode* closure = dynamic_cast<ClosureNode*>(arguments[i]);
        if(auto* identifier = dynamic_cast<IdentifierNode*>(arguments[i]))
        {
            auto found = closureVariables.find(identifier->name);
            if(found != closureVariables.end())
                closure = found->second;
        }
        if(!closure)
            continue;
        if(closure->parameters &&
           std::any_of(closure->parameters->parameters.begin(),
                       closure->parameters->parameters.end(),
                       [](ParameterNode* parameter)
                       { return !parameter || !parameter->type; }))
            continue; // Its result was already inferred from callback context.
        TypeNode* closureResult = inferClosureResult(closure);
        if(closureResult)
        {
            auto* mapReturn =
                dynamic_cast<MapTypeNode*>(functionTemplate->returnType);
            auto* tupleResult = dynamic_cast<TupleTypeNode*>(closureResult);
            if(mapReturn && tupleResult && tupleResult->elementTypes &&
               tupleResult->elementTypes->types.size() == 2)
            {
                bindType(mapReturn->keyType,
                         tupleResult->elementTypes->types[0]);
                bindType(mapReturn->valueType,
                         tupleResult->elementTypes->types[1]);
            }
            else
                bindUnboundReturnType(functionTemplate->returnType,
                                      closureResult);
        }
    }
    return true;
}

void CodeGenerator::instantiateGenericFunctionOverloads(
    FunctionCallNode* call, const std::string& lookupName,
    std::vector<FunctionOverloadInfo>& overloads)
{
    if(!call)
        return;

    // Copy the templates before registering specializations: registration
    // appends to the overload list and must not invalidate this iteration.
    std::vector<FunctionDefNode*> templates;
    for(const auto& overload : overloads)
    {
        FunctionDefNode* candidate = overload.node;
        if(candidate && !overload.function && !candidate->typeParams.empty() &&
           candidate->typePackParam.empty() && !candidate->isCexpr &&
           candidate->parameters &&
           candidate->parameters->parameters.size() == call->arguments.size() &&
           isOverloadVisible(overload))
        {
            templates.push_back(candidate);
        }
    }

    for(FunctionDefNode* functionTemplate : templates)
    {
        std::map<std::string, TypeNode*> bindings;
        if(!inferGenericFunctionTypeBindings(functionTemplate, call->arguments,
                                             bindings))
            continue;

        std::vector<TypeNode*> typeArgs;
        typeArgs.reserve(functionTemplate->typeParams.size());
        bool complete = true;
        for(const auto& typeParam : functionTemplate->typeParams)
        {
            auto found = bindings.find(typeParam);
            if(found == bindings.end())
            {
                complete = false;
                break;
            }
            typeArgs.push_back(found->second);
        }
        if(!complete ||
           !validateTypeArgumentTraitBounds(
               functionTemplate->typeParams,
               functionTemplate->typeParamTraitBounds, typeArgs, {},
               functionTemplate->line, "function", lookupName, false))
            continue;

        auto* specializedParameters = new ParameterListNode();
        for(auto* parameter : functionTemplate->parameters->parameters)
        {
            specializedParameters->parameters.push_back(new ParameterNode(
                substituteTypeParams(parameter->type,
                                     functionTemplate->typeParams, typeArgs),
                parameter->name));
        }

        auto* specialized = new FunctionDefNode(
            substituteTypeParams(functionTemplate->returnType,
                                 functionTemplate->typeParams, typeArgs),
            functionTemplate->name, specializedParameters,
            functionTemplate->body, functionTemplate->isPublic, false);
        specialized->line = functionTemplate->line;
        specialized->col = functionTemplate->col;
        specialized->sourceModule = functionTemplate->sourceModule;
        specialized->isInline = functionTemplate->isInline;
        specialized->isInlineAlways = functionTemplate->isInlineAlways;
        specialized->isInlineNever = functionTemplate->isInlineNever;
        specialized->boundClosureParameters =
            functionTemplate->boundClosureParameters;
        specialized->boundClosureCaptureAliases =
            functionTemplate->boundClosureCaptureAliases;
        specialized->boundClosureCaptureTypes =
            functionTemplate->boundClosureCaptureTypes;
        specialized->boundClosureConstCaptures =
            functionTemplate->boundClosureConstCaptures;
        specialized->closureSpecializationKey =
            functionTemplate->closureSpecializationKey;
        static std::map<ClosureNode*, size_t> closureSpecializationIds;
        static size_t nextClosureSpecializationId = 0;
        for(size_t i = 0; i < call->arguments.size(); ++i)
        {
            ClosureNode* closure =
                dynamic_cast<ClosureNode*>(call->arguments[i]);
            if(auto* identifier =
                   dynamic_cast<IdentifierNode*>(call->arguments[i]))
            {
                auto closureIt = closureVariables.find(identifier->name);
                if(closureIt != closureVariables.end())
                    closure = closureIt->second;
            }
            if(closure &&
               !dynamic_cast<FunctionTypeNode*>(
                   functionTemplate->parameters->parameters[i]->type))
            {
                const std::string& parameterName =
                    functionTemplate->parameters->parameters[i]->name;
                specialized->boundClosureParameters[parameterName] = closure;
                std::set<std::string> usedNames;
                if(closure->body)
                    for(auto* statement : closure->body->statements)
                        collect_used_idents(statement, usedNames);
                if(closure->parameters)
                    for(auto* closureParameter :
                        closure->parameters->parameters)
                        usedNames.erase(closureParameter->name);
                size_t captureIndex = 0;
                for(const auto& usedName : usedNames)
                {
                    if(globalNamedValues.count(usedName) ||
                       namedValues.count(usedName) == 0 ||
                       variableTypes.count(usedName) == 0)
                        continue;
                    TypeNode* capturedType =
                        getLValueType(new IdentifierNode(usedName), call->line);
                    if(!capturedType)
                        continue;
                    const std::string hiddenName =
                        "__closure_capture_" + std::to_string(i) + "_" +
                        std::to_string(captureIndex++);
                    specialized
                        ->boundClosureCaptureAliases[parameterName][usedName] =
                        hiddenName;
                    specialized
                        ->boundClosureCaptureTypes[parameterName][usedName] =
                        cloneTypeNode(capturedType);
                    if(constantVariables.count(usedName))
                        specialized->boundClosureConstCaptures[parameterName]
                            .insert(usedName);
                    specializedParameters->parameters.push_back(
                        new ParameterNode(
                            new PointerTypeNode(cloneTypeNode(capturedType)),
                            hiddenName));
                }
                if(!specialized->closureSpecializationKey.empty())
                    specialized->closureSpecializationKey += "_";
                auto closureId = closureSpecializationIds.find(closure);
                if(closureId == closureSpecializationIds.end())
                {
                    closureId =
                        closureSpecializationIds
                            .emplace(closure, ++nextClosureSpecializationId)
                            .first;
                }
                specialized->closureSpecializationKey +=
                    std::to_string(closureId->second);
            }
        }
        for(const auto& binding : bindings)
            specialized->concreteTypeBindings[binding.first] =
                cloneTypeNode(binding.second);

        const std::string signatureKey = functionSignatureKey(specialized);
        const bool alreadySpecialized =
            std::any_of(overloads.begin(), overloads.end(),
                        [&](const FunctionOverloadInfo& overload)
                        {
                            return overload.function &&
                                   overload.signatureKey == signatureKey;
                        });
        if(alreadySpecialized)
            continue;

        llvm::Function* declaration = generateFunctionDeclaration(specialized);
        if(!declaration)
            continue;
        registerFunctionOverload(specialized, declaration);
        generateFunctionDefinition(specialized);
    }
}

llvm::Value* CodeGenerator::generateCallableInvocation(
    CallableInvokeNode* node)
{
    TypeNode* callableSemantic = getLValueType(node->callable, node->line);
    auto* callableType = dynamic_cast<FunctionTypeNode*>(callableSemantic);
    if(!callableType)
    {
        reportError(node->line,
                    "expression is not a callable function value");
        return nullptr;
    }
    const size_t expectedCount = callableType->parameterTypes
                                     ? callableType->parameterTypes->types.size()
                                     : 0;
    if(node->arguments.size() != expectedCount)
    {
        reportError(node->line, "callable expects " +
                                    std::to_string(expectedCount) +
                                    " argument(s), got " +
                                    std::to_string(node->arguments.size()));
        return nullptr;
    }
    if(callableType->parameterTypes)
        for(size_t i = 0; i < expectedCount; ++i)
            if(dynamic_cast<PointerTypeNode*>(
                   callableType->parameterTypes->types[i]))
            {
                if(expressionIsKnownSharedBorrow(node->arguments[i]))
                {
                    reportError(
                        node->line,
                        "cannot pass a shared borrow to opaque callable "
                        "pointer parameter " + std::to_string(i + 1));
                    return nullptr;
                }
                const auto* callableIdentifier =
                    dynamic_cast<IdentifierNode*>(node->callable);
                const bool isCheckedCallableParameter =
                    callableIdentifier &&
                    functionTypeParameters.count(callableIdentifier->name);
                if(unsafeDepth == 0 &&
                   expressionCarriesLocalBorrow(node->arguments[i]) &&
                   !(isCheckedCallableParameter &&
                     expressionIsPointerParameterBorrow(
                         node->arguments[i])))
                {
                    reportError(
                        node->line,
                        "cannot pass a borrow of local storage to opaque "
                        "callable pointer parameter " +
                            std::to_string(i + 1) +
                            " outside unsafe because the callable may retain "
                            "it");
                    return nullptr;
                }
            }

    llvm::Value* callableValue = generateExpression(node->callable,
                                                    callableType);
    if(!callableValue)
        return nullptr;
    auto* closureStruct =
        llvm::dyn_cast<llvm::StructType>(callableValue->getType());
    if(!closureStruct || closureStruct->getNumElements() != 2)
    {
        reportError(node->line,
                    "callable value has an unsupported runtime representation");
        return nullptr;
    }

    llvm::Value* code = builder.CreateExtractValue(callableValue, 0,
                                                   "callable.code");
    llvm::Value* environment = builder.CreateExtractValue(
        callableValue, 1, "callable.environment");
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* erasedPointerType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* erasedPointerType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    std::vector<llvm::Type*> signatureParameters = {erasedPointerType};
    std::vector<llvm::Value*> callArguments = {environment};
    if(callableType->parameterTypes)
    {
        for(size_t i = 0; i < expectedCount; ++i)
        {
            TypeNode* parameter = callableType->parameterTypes->types[i];
            llvm::Type* parameterType = getLLVMTypeFromNode(parameter);
            llvm::Value* argument = generateArgumentValue(
                node->arguments[i], parameter);
            if(!parameterType || !argument)
            {
                reportError(node->line,
                            "callable parameter types must be known and "
                            "arguments must be valid");
                return nullptr;
            }
            if(argument->getType() != parameterType)
            {
                if(argument->getType()->isIntegerTy() &&
                   parameterType->isIntegerTy())
                    argument = builder.CreateIntCast(argument, parameterType,
                                                      true, "callable.arg.cast");
                else if(argument->getType()->isFloatingPointTy() &&
                        parameterType->isFloatingPointTy())
                    argument = builder.CreateFPCast(argument, parameterType,
                                                     "callable.arg.cast");
                else
                {
                    reportError(node->line,
                                "callable argument type does not match its "
                                "parameter");
                    return nullptr;
                }
            }
            signatureParameters.push_back(parameterType);
            callArguments.push_back(argument);
        }
    }
    llvm::Type* returnType = getLLVMTypeFromNode(callableType->returnType);
    if(!returnType)
    {
        reportError(node->line, "callable return type is not known");
        return nullptr;
    }
    llvm::FunctionType* signature = llvm::FunctionType::get(
        returnType, signatureParameters, false);
#if LLVM_VERSION_MAJOR < 15
    code = builder.CreateBitCast(code, signature->getPointerTo(),
                                 "callable.indirect");
#endif
    return builder.CreateCall(signature, code, callArguments,
                              returnType->isVoidTy() ? "" : "callable.call");
}

llvm::Value* CodeGenerator::generateFunctionCall(FunctionCallNode* node)
{
    // std::copy has a dedicated diagnostic below and must reach that check.
    if(node->name != "std::copy")
    {
        std::function<bool(ExpressionNode*)> containsLocalBorrow =
            [&](ExpressionNode* expression) {
                if(!expression)
                    return false;
                if(auto* identifier =
                       dynamic_cast<IdentifierNode*>(expression))
                {
                    if(isMutBorrower(identifier->name))
                        return true;
                    auto pointer = pointerBorrowTarget.find(identifier->name);
                    if(pointer != pointerBorrowTarget.end() &&
                       variableScopeDepth.count(pointer->second))
                        return true;
                    auto origins =
                        aggregateBorrowOrigins.find(identifier->name);
                    if(origins != aggregateBorrowOrigins.end())
                        for(const auto& owner : origins->second)
                            if(variableScopeDepth.count(owner))
                                return true;
                    return false;
                }
                if(auto* unary = dynamic_cast<UnaryOpNode*>(expression))
                {
                    if(unary->op == UnaryOpNode::OP_ADDR ||
                       unary->op == UnaryOpNode::OP_ADDR_MUT)
                    {
                        const auto owner =
                            resolveBorrowOwnerFromLValue(unary->operand);
                        if(variableScopeDepth.count(owner))
                            return true;
                    }
                    return containsLocalBorrow(unary->operand);
                }
                if(auto* structure =
                       dynamic_cast<StructLiteralNode*>(expression))
                {
                    for(const auto& field : structure->fields)
                        if(containsLocalBorrow(field.second))
                            return true;
                }
                else if(auto* tuple =
                            dynamic_cast<TupleLiteralNode*>(expression))
                {
                    if(tuple->elements)
                        for(auto* element : tuple->elements->elements)
                            if(containsLocalBorrow(element))
                                return true;
                }
                else if(auto* list =
                            dynamic_cast<ListLiteralNode*>(expression))
                {
                    if(list->elements)
                        for(auto* element : list->elements->elements)
                            if(containsLocalBorrow(element))
                                return true;
                }
                else if(auto* map =
                            dynamic_cast<MapLiteralNode*>(expression))
                {
                    if(map->entries)
                        for(auto* entry : map->entries->entries)
                            if(containsLocalBorrow(entry->key) ||
                               containsLocalBorrow(entry->value))
                                return true;
                }
                else if(auto* ternary =
                            dynamic_cast<TernaryNode*>(expression))
                    return containsLocalBorrow(ternary->trueExpr) ||
                           containsLocalBorrow(ternary->falseExpr);
                else if(auto* match =
                            dynamic_cast<MatchExpressionNode*>(expression))
                {
                    for(auto* arm : match->arms)
                        if(arm && containsLocalBorrow(arm->expression))
                            return true;
                }
                else if(auto* binary =
                            dynamic_cast<BinaryOpNode*>(expression))
                    return containsLocalBorrow(binary->left) ||
                           containsLocalBorrow(binary->right);
                else if(auto* array =
                            dynamic_cast<ArrayFillNode*>(expression))
                    return containsLocalBorrow(array->value);
                else if(auto* field =
                            dynamic_cast<FieldAccessNode*>(expression))
                    return containsLocalBorrow(field->object);
                else if(auto* index =
                            dynamic_cast<IndexExpressionNode*>(expression))
                    return containsLocalBorrow(index->base);
                else if(auto* tuple =
                            dynamic_cast<TupleAccessNode*>(expression))
                    return containsLocalBorrow(tuple->tuple);
                else if(auto* attempt =
                            dynamic_cast<TryExpressionNode*>(expression))
                    return containsLocalBorrow(attempt->expression);
                else if(auto* cast =
                            dynamic_cast<CastExpressionNode*>(expression))
                    return containsLocalBorrow(cast->expression);
                else if(auto* call =
                            dynamic_cast<FunctionCallNode*>(expression))
                {
                    for(auto* argument : call->arguments)
                        if(containsLocalBorrow(argument))
                            return true;
                }
                else if(auto* invoke =
                            dynamic_cast<CallableInvokeNode*>(expression))
                {
                    if(containsLocalBorrow(invoke->callable))
                        return true;
                    for(auto* argument : invoke->arguments)
                        if(containsLocalBorrow(argument))
                            return true;
                }
                else if(auto* method =
                            dynamic_cast<MethodCallNode*>(expression))
                {
                    if(containsLocalBorrow(method->object))
                        return true;
                    for(auto* argument : method->arguments)
                        if(containsLocalBorrow(argument))
                            return true;
                }
                return false;
            };
        auto isAggregateType = [](TypeNode* type) {
            if(!type)
                return false;
            switch(type->kind)
            {
            case TypeNode::TYPE_STRUCT:
            case TypeNode::TYPE_LIST:
            case TypeNode::TYPE_MAP:
            case TypeNode::TYPE_TUPLE:
                return true;
            default:
                return false;
            }
        };
        std::function<bool(ExpressionNode*)> containsExclusiveBorrow;
        containsExclusiveBorrow = [&](ExpressionNode* expression) {
            if(!expression)
                return false;
            if(auto* identifier = dynamic_cast<IdentifierNode*>(expression))
                return isMutBorrower(identifier->name);
            if(auto* unary = dynamic_cast<UnaryOpNode*>(expression))
                return unary->op == UnaryOpNode::OP_ADDR_MUT ||
                       containsExclusiveBorrow(unary->operand);
            if(auto* structure = dynamic_cast<StructLiteralNode*>(expression))
                for(const auto& field : structure->fields)
                    if(containsExclusiveBorrow(field.second))
                        return true;
            if(auto* tuple = dynamic_cast<TupleLiteralNode*>(expression))
                if(tuple->elements)
                    for(auto* element : tuple->elements->elements)
                        if(containsExclusiveBorrow(element))
                            return true;
            if(auto* list = dynamic_cast<ListLiteralNode*>(expression))
                if(list->elements)
                    for(auto* element : list->elements->elements)
                        if(containsExclusiveBorrow(element))
                            return true;
            if(auto* map = dynamic_cast<MapLiteralNode*>(expression))
                if(map->entries)
                    for(auto* entry : map->entries->entries)
                        if(entry &&
                           (containsExclusiveBorrow(entry->key) ||
                            containsExclusiveBorrow(entry->value)))
                            return true;
            if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
                return containsExclusiveBorrow(ternary->trueExpr) ||
                       containsExclusiveBorrow(ternary->falseExpr);
            if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
                return containsExclusiveBorrow(binary->left) ||
                       containsExclusiveBorrow(binary->right);
            if(auto* array = dynamic_cast<ArrayFillNode*>(expression))
                return containsExclusiveBorrow(array->value);
            if(auto* field = dynamic_cast<FieldAccessNode*>(expression))
                return containsExclusiveBorrow(field->object);
            if(auto* index = dynamic_cast<IndexExpressionNode*>(expression))
                return containsExclusiveBorrow(index->base);
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(expression))
                return containsExclusiveBorrow(tuple->tuple);
            if(auto* attempt = dynamic_cast<TryExpressionNode*>(expression))
                return containsExclusiveBorrow(attempt->expression);
            if(auto* cast = dynamic_cast<CastExpressionNode*>(expression))
                return containsExclusiveBorrow(cast->expression);
            if(auto* call = dynamic_cast<FunctionCallNode*>(expression))
                for(auto* argument : call->arguments)
                    if(containsExclusiveBorrow(argument))
                        return true;
            return false;
        };
        for(size_t argumentIndex = 0;
            argumentIndex < node->arguments.size(); ++argumentIndex)
        {
            auto* argument = node->arguments[argumentIndex];
            if(containsLocalBorrow(argument) &&
               isAggregateType(
                   inferExpressionTypeNode(argument, node->line)))
            {
                const std::string aggregate =
                    resolveBorrowOwnerFromLValue(argument);
                TypeNode* argumentType =
                    inferExpressionTypeNode(argument, node->line);
                bool hasMatchingVisibleOverload = false;
                bool everyMatchingOverloadIsSafe = true;
                auto overloads = functionOverloads.find(node->name);
                if(argumentType && !containsExclusiveBorrow(argument) &&
                   overloads != functionOverloads.end())
                {
                    for(const auto& overload : overloads->second)
                    {
                        FunctionDefNode* candidate = overload.node;
                        if(!isOverloadVisible(overload) || !candidate ||
                           !candidate->parameters ||
                           candidate->parameters->isVarArg ||
                           candidate->parameters->parameters.size() !=
                               node->arguments.size() ||
                           argumentIndex >=
                               candidate->parameters->parameters.size())
                            continue;
                        auto* parameter = candidate->parameters->parameters[
                            argumentIndex];
                        if(!parameter || !parameter->type ||
                           typeMangle(parameter->type) !=
                               typeMangle(argumentType))
                            continue;
                        hasMatchingVisibleOverload = true;
                        if(!functionCanReadBorrowingAggregateParameter(
                               candidate, argumentIndex))
                            everyMatchingOverloadIsSafe = false;
                    }
                }
                if(hasMatchingVisibleOverload &&
                   everyMatchingOverloadIsSafe)
                    continue;
                reportError(node->line,
                            "cannot pass aggregate" +
                                (aggregate.empty()
                                     ? std::string()
                                     : " '" + aggregate + "'") +
                                " containing a borrow of a local value "
                                "because the callee may retain it");
                return nullptr;
            }
            const std::string aggregate =
                resolveBorrowOwnerFromLValue(argument);
            if(aggregate.empty() || !isMutBorrower(aggregate))
                continue;
            const bool extractsPointer =
                !dynamic_cast<IdentifierNode*>(argument) &&
                dynamic_cast<PointerTypeNode*>(
                    inferExpressionTypeNode(argument, node->line));
            if(extractsPointer)
            {
                reportError(
                    node->line,
                    "cannot pass a mutable borrow out of aggregate '" +
                        aggregate + "' because the callee may retain it");
                return nullptr;
            }
        }
    }

    if(auto overloads = functionOverloads.find(node->name);
       overloads != functionOverloads.end())
    {
        for(size_t argumentIndex = 0;
            argumentIndex < node->arguments.size(); ++argumentIndex)
        {
            auto* identifier =
                dynamic_cast<IdentifierNode*>(node->arguments[argumentIndex]);
            auto closure = identifier
                               ? closureVariables.find(identifier->name)
                               : closureVariables.end();
            if(closure == closureVariables.end() || !closure->second)
                continue;

            bool callableCandidate = false;
            for(const auto& candidate : overloads->second)
            {
                if(!candidate.node || !candidate.node->parameters ||
                   candidate.node->parameters->isVarArg ||
                   candidate.node->parameters->parameters.size() !=
                       node->arguments.size() ||
                   argumentIndex >=
                       candidate.node->parameters->parameters.size())
                    continue;
                if(dynamic_cast<FunctionTypeNode*>(
                       candidate.node->parameters->parameters[argumentIndex]
                           ->type))
                    callableCandidate = true;
                else if(auto* named = dynamic_cast<StructTypeRefNode*>(
                            candidate.node->parameters
                                ->parameters[argumentIndex]->type))
                    callableCandidate =
                        std::find(candidate.node->typeParams.begin(),
                                  candidate.node->typeParams.end(),
                                  named->structName) !=
                        candidate.node->typeParams.end();
            }
            if(callableCandidate)
                continue;

            std::vector<std::pair<std::string, TypeNode*>> captures;
            TypeNode* closureReturnType = nullptr;
            if(getRuntimeClosureCaptures(closure->second, captures,
                                         closureReturnType))
                for(const auto& capture : captures)
                    if(variableScopeDepth.count(capture.first))
                    {
                        reportError(
                            node->line,
                            "cannot pass closure '" + identifier->name +
                                "' to function '" + node->name +
                                "' because it borrows local variable '" +
                                capture.first + "'");
                        return nullptr;
                    }
            reportError(node->line,
                        "closure '" + identifier->name +
                            "' can only be passed to a callable parameter");
            return nullptr;
        }
    }

    // Explicit value copy. This is deliberately opt-in: ordinary by-value
    // ownership transfers continue to move move-only values.
    if(node->name == "std::copy")
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line, "std::copy expects exactly one argument");
            return nullptr;
        }

        ExpressionNode* sourceExpr = node->arguments.front();
        const std::string sourceOwner =
            resolveBorrowOwnerFromLValue(sourceExpr);
        if((!sourceOwner.empty() && isMutBorrower(sourceOwner)) ||
           (dynamic_cast<IdentifierNode*>(sourceExpr) &&
            isMutBorrower(
                static_cast<IdentifierNode*>(sourceExpr)->name)))
        {
            reportError(node->line,
                        "std::copy cannot duplicate a mutable borrow or an "
                        "aggregate that contains one");
            return nullptr;
        }
        TypeNode* sourceType = inferExpressionTypeNode(sourceExpr, node->line);
        if(!sourceType)
        {
            reportError(node->line,
                        "cannot determine the type passed to std::copy");
            return nullptr;
        }

        if(auto* listType = dynamic_cast<GenericListTypeNode*>(sourceType))
        {
            if(!listType->elementType || !isCopyType(listType->elementType))
            {
                reportError(node->line,
                            "std::copy cannot copy a list with move-only or "
                            "unresolved elements");
                return nullptr;
            }

            llvm::Type* listLlvmType = getLLVMTypeFromNode(sourceType);
            llvm::Type* elementLlvmType =
                getLLVMTypeFromNode(listType->elementType);
            llvm::Value* source = generateExpression(sourceExpr, sourceType);
            if(!listLlvmType || !elementLlvmType || !source)
                return nullptr;

            llvm::Value* length =
                builder.CreateExtractValue(source, {0}, "copy.list.length");
            llvm::Value* sourceData =
                builder.CreateExtractValue(source, {1}, "copy.list.data");
            const uint64_t elementSize = module->getDataLayout()
                                             .getTypeAllocSize(elementLlvmType)
                                             .getFixedValue();
            llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
            llvm::Value* byteCount = builder.CreateMul(
                length, llvm::ConstantInt::get(i64Type, elementSize),
                "copy.list.bytes");
            llvm::Function* function = builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* emptyBlock =
                llvm::BasicBlock::Create(context, "copy.list.empty", function);
            llvm::BasicBlock* allocateBlock = llvm::BasicBlock::Create(
                context, "copy.list.allocate", function);
            llvm::BasicBlock* mergeBlock =
                llvm::BasicBlock::Create(context, "copy.list.merge", function);
            llvm::Value* isEmpty =
                builder.CreateICmpEQ(length, llvm::ConstantInt::get(i64Type, 0),
                                     "copy.list.is.empty");
            builder.CreateCondBr(isEmpty, emptyBlock, allocateBlock);

            builder.SetInsertPoint(emptyBlock);
            llvm::Value* emptyList = llvm::Constant::getNullValue(listLlvmType);
            builder.CreateBr(mergeBlock);

            builder.SetInsertPoint(allocateBlock);
            initializeStdlibFunctions();
            llvm::Value* destination =
                builder.CreateCall(mallocFunc, {byteCount}, "copy.list.data");
            llvm::Type* ptrType = destination->getType();
            if(sourceData->getType() != ptrType &&
               sourceData->getType()->isPointerTy())
                sourceData = builder.CreateBitCast(sourceData, ptrType,
                                                   "copy.list.source.cast");
            llvm::FunctionType* memcpyType = llvm::FunctionType::get(
                ptrType, {ptrType, ptrType, i64Type}, false);
            llvm::FunctionCallee memcpyFn =
                module->getOrInsertFunction("memcpy", memcpyType);
            builder.CreateCall(memcpyFn, {destination, sourceData, byteCount});
            llvm::Value* copiedList = llvm::UndefValue::get(listLlvmType);
            copiedList = builder.CreateInsertValue(copiedList, length, {0},
                                                   "copy.list.result.length");
            copiedList = builder.CreateInsertValue(copiedList, destination, {1},
                                                   "copy.list.result.data");
            builder.CreateBr(mergeBlock);

            builder.SetInsertPoint(mergeBlock);
            llvm::PHINode* result =
                builder.CreatePHI(listLlvmType, 2, "copy.list.result");
            result->addIncoming(emptyList, emptyBlock);
            result->addIncoming(copiedList, allocateBlock);
            return result;
        }

        if(!isCopyType(sourceType))
        {
            reportError(node->line,
                        "std::copy requires a Copy type; this value is "
                        "move-only");
            return nullptr;
        }

        llvm::Value* value = generateExpression(sourceExpr, sourceType);
        return applyStructCopySemantics(value, sourceType);
    }

    const std::string numericLimitsMarker = "numeric_limits<";
    const size_t numericLimitsPos = node->name.rfind(numericLimitsMarker);
    if(numericLimitsPos != std::string::npos)
    {
        const size_t typeBegin = numericLimitsPos + numericLimitsMarker.size();
        const size_t typeEnd = node->name.find(">::", typeBegin);
        if(typeEnd != std::string::npos)
        {
            const std::string operation = node->name.substr(typeEnd + 3);
            if(operation == "max" || operation == "min")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "numeric_limits<T>::" + operation +
                                                "() does not take arguments");
                    return nullptr;
                }

                const std::string typeName =
                    node->name.substr(typeBegin, typeEnd - typeBegin);
                TypeNode* numericType = nullptr;
                auto typeBinding = activeTypeParamBindings.find(typeName);
                if(typeBinding != activeTypeParamBindings.end())
                    numericType = typeBinding->second;
                if(!numericType)
                {
                    auto functionBinding =
                        activeFunctionTypeBindings.find(typeName);
                    if(functionBinding != activeFunctionTypeBindings.end())
                        numericType = functionBinding->second;
                }
                if(!numericType)
                    numericType = Helpers::type_from_text(typeName);
                if(!numericType)
                {
                    reportError(node->line,
                                "numeric_limits<T>::" + operation +
                                    "() requires a primitive numeric type");
                    return nullptr;
                }

                if(numericType->kind == TypeNode::TYPE_BOOL)
                {
                    return llvm::ConstantInt::get(
                        llvm::Type::getInt1Ty(context), operation == "max");
                }

                llvm::Type* llvmType = getLLVMTypeFromNode(numericType);
                if(!llvmType)
                {
                    reportError(node->line,
                                "numeric_limits<T>::" + operation +
                                    "() supports only primitive numeric types");
                    return nullptr;
                }

                if(llvmType->isIntegerTy())
                {
                    const unsigned width = llvmType->getIntegerBitWidth();
                    const bool isSigned =
                        numericType->kind == TypeNode::TYPE_I8 ||
                        numericType->kind == TypeNode::TYPE_I16 ||
                        numericType->kind == TypeNode::TYPE_I32 ||
                        numericType->kind == TypeNode::TYPE_I64 ||
                        numericType->kind == TypeNode::TYPE_INT;
                    llvm::APInt value =
                        operation == "max"
                            ? (isSigned ? llvm::APInt::getSignedMaxValue(width)
                                        : llvm::APInt::getMaxValue(width))
                            : (isSigned ? llvm::APInt::getSignedMinValue(width)
                                        : llvm::APInt(width, 0));
                    return llvm::ConstantInt::get(context, value);
                }

                if(llvmType->isFloatTy())
                {
                    const double value =
                        operation == "max" ? std::numeric_limits<float>::max()
                                           : std::numeric_limits<float>::min();
                    return llvm::ConstantFP::get(llvmType, value);
                }
                if(llvmType->isDoubleTy())
                {
                    const double value =
                        operation == "max" ? std::numeric_limits<double>::max()
                                           : std::numeric_limits<double>::min();
                    return llvm::ConstantFP::get(llvmType, value);
                }

                reportError(node->line,
                            "numeric_limits<T>::" + operation +
                                "() supports only primitive numeric types");
                return nullptr;
            }
        }
    }

    auto variadicIt = variadicGenericFunctionTemplates.find(node->name);
    if(variadicIt != variadicGenericFunctionTemplates.end() &&
       !variadicIt->second.empty())
        return generateVariadicGenericCall(node, variadicIt->second.front());

    const std::string constructiblePrefix = "is_constructible<";
    if(node->name.rfind(constructiblePrefix, 0) == 0 && !node->name.empty() &&
       node->name.back() == '>')
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line,
                        "is_constructible<T> expects exactly one value");
            return nullptr;
        }
        std::string targetName = node->name.substr(
            constructiblePrefix.size(),
            node->name.size() - constructiblePrefix.size() - 1);
        TypeNode* targetType = nullptr;
        auto bound = activeFunctionTypeBindings.find(targetName);
        if(bound != activeFunctionTypeBindings.end())
            targetType = bound->second;
        else
            targetType = Helpers::type_from_text(targetName);
        TypeNode* sourceType =
            inferExpressionTypeNode(node->arguments.front(), node->line);
        bool constructible = false;
        if(targetType && sourceType)
        {
            if(typeMangle(targetType) == typeMangle(sourceType))
                constructible = true;
            else if(llvm::Type* targetLLVM = getLLVMTypeFromNode(targetType))
            {
                if(llvm::Type* sourceLLVM = getLLVMTypeFromNode(sourceType))
                {
                    int conversionCost = 0;
                    constructible =
                        canConvertType(sourceLLVM, targetLLVM, conversionCost);
                }
            }
        }
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context),
                                      constructible ? 1 : 0);
    }

    auto validateTemporaryBorrowArguments =
        [&](const std::vector<ExpressionNode*>& args,
            const std::string& calleeName,
            const std::string& implicitWholeOwner,
            const std::vector<bool>* sharedReferenceParameters) -> bool
    {
        std::set<std::string> wholeOwnersInCall;
        std::map<std::string, std::set<std::string>> subpathsInCall;
        std::set<std::string> exclusiveWholeOwnersInCall;
        std::map<std::string, std::set<std::string>> exclusiveSubpathsInCall;
        std::set<std::string> movedOwnersInCall;
        if(!implicitWholeOwner.empty())
        {
            wholeOwnersInCall.insert(implicitWholeOwner);
            exclusiveWholeOwnersInCall.insert(implicitWholeOwner);
        }
        auto describeBorrowPath =
            [&](ExpressionNode* expr, std::string& ownerOut,
                std::string& pathOut, bool& isWholeOwnerOut) -> bool
        {
            ownerOut = resolveBorrowOwnerFromLValue(expr);
            if(ownerOut.empty())
                return false;

            isWholeOwnerOut = true;
            pathOut.clear();

            pathOut = resolveBorrowPlaceFromLValue(expr);
            if(pathOut.compare(0, ownerOut.size(), ownerOut) == 0)
            {
                pathOut.erase(0, ownerOut.size());
                if(!pathOut.empty() && pathOut.front() == '.')
                    pathOut.erase(0, 1);
            }
            isWholeOwnerOut = pathOut.empty();
            return true;
        };
        std::function<void(ExpressionNode*, bool)> collectMovedOwners =
            [&](ExpressionNode* expr, bool borrowed) -> void
        {
            if(!expr)
                return;

            if(auto* id = dynamic_cast<IdentifierNode*>(expr))
            {
                if(globalNamedValues.find(id->name) != globalNamedValues.end())
                    return;
                if(!borrowed && isMoveOnlyVariable(id->name))
                    movedOwnersInCall.insert(id->name);
                return;
            }

            if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
            {
                if(unary->op == UnaryOpNode::OP_ADDR ||
                   unary->op == UnaryOpNode::OP_ADDR_MUT)
                    collectMovedOwners(unary->operand, true);
                else
                    collectMovedOwners(unary->operand, borrowed);
                return;
            }

            if(auto* binary = dynamic_cast<BinaryOpNode*>(expr))
            {
                collectMovedOwners(binary->left, false);
                collectMovedOwners(binary->right, false);
                return;
            }

            if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
            {
                if(field->object)
                    collectMovedOwners(field->object, true);
                return;
            }

            if(auto* index = dynamic_cast<IndexExpressionNode*>(expr))
            {
                collectMovedOwners(index->base, true);
                collectMovedOwners(index->index, false);
                return;
            }

            if(auto* castExpr = dynamic_cast<CastExpressionNode*>(expr))
            {
                collectMovedOwners(castExpr->expression, false);
                return;
            }

            if(auto* tryExpr = dynamic_cast<TryExpressionNode*>(expr))
            {
                collectMovedOwners(tryExpr->expression, false);
                return;
            }

            if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
            {
                collectMovedOwners(ternary->condition, false);
                collectMovedOwners(ternary->trueExpr, false);
                collectMovedOwners(ternary->falseExpr, false);
                return;
            }

            if(auto* listLit = dynamic_cast<ListLiteralNode*>(expr))
            {
                if(listLit->elements)
                {
                    for(auto* elem : listLit->elements->elements)
                        collectMovedOwners(elem, false);
                }
                return;
            }

            if(auto* mapLit = dynamic_cast<MapLiteralNode*>(expr))
            {
                if(mapLit->entries)
                {
                    for(auto* entry : mapLit->entries->entries)
                    {
                        collectMovedOwners(entry->key, false);
                        collectMovedOwners(entry->value, false);
                    }
                }
                return;
            }

            if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(expr))
            {
                if(tupleLit->elements)
                {
                    for(auto* elem : tupleLit->elements->elements)
                        collectMovedOwners(elem, false);
                }
                return;
            }

            if(auto* structLit = dynamic_cast<StructLiteralNode*>(expr))
            {
                for(const auto& init : structLit->fields)
                    collectMovedOwners(init.second, false);
                return;
            }
        };

        for(size_t argIndex = 0; argIndex < args.size(); ++argIndex)
        {
            auto* arg = args[argIndex];
            const bool sharedReference = sharedReferenceParameters &&
                argIndex < sharedReferenceParameters->size() &&
                (*sharedReferenceParameters)[argIndex];
            collectMovedOwners(arg, false);

            auto registerBorrowInCall = [&](const std::string& owner,
                                            const std::string& path,
                                            bool isWholeOwner,
                                            bool isExclusive) -> bool
            {
                if(isWholeOwner)
                {
                    const bool overlaps = wholeOwnersInCall.count(owner) ||
                        (subpathsInCall.count(owner) &&
                         !subpathsInCall[owner].empty());
                    const bool overlapsExclusive =
                        exclusiveWholeOwnersInCall.count(owner) ||
                        (exclusiveSubpathsInCall.count(owner) &&
                         !exclusiveSubpathsInCall[owner].empty());
                    if(overlaps && (isExclusive || overlapsExclusive))
                    {
                        reportError(node->line,
                                    "cannot borrow overlapping parts of '" +
                                        owner + "' in call to '" + calleeName +
                                        "'");
                        return false;
                    }
                    wholeOwnersInCall.insert(owner);
                    if(isExclusive)
                        exclusiveWholeOwnersInCall.insert(owner);
                    return true;
                }

                if(wholeOwnersInCall.count(owner) &&
                   (isExclusive ||
                    exclusiveWholeOwnersInCall.count(owner)))
                {
                    reportError(node->line,
                                "cannot borrow overlapping parts of '" + owner +
                                    "' in call to '" + calleeName + "'");
                    return false;
                }
                auto& paths = subpathsInCall[owner];
                bool overlaps = false;
                for(const auto& existing : paths)
                {
                    if(borrowPlacesOverlap(existing, path))
                    {
                        overlaps = true;
                        break;
                    }
                }
                bool overlapsExclusive = false;
                auto exclusivePaths = exclusiveSubpathsInCall.find(owner);
                if(exclusivePaths != exclusiveSubpathsInCall.end())
                    for(const auto& existing : exclusivePaths->second)
                        if(borrowPlacesOverlap(existing, path))
                        {
                            overlapsExclusive = true;
                            break;
                        }
                if(overlaps && (isExclusive || overlapsExclusive))
                {
                    reportError(node->line,
                                "cannot borrow '" + owner + "." + path +
                                    "' multiple times in call to '" +
                                    calleeName + "'");
                    return false;
                }
                paths.insert(path);
                if(isExclusive)
                    exclusiveSubpathsInCall[owner].insert(path);
                return true;
            };

            if(auto* idArg = dynamic_cast<IdentifierNode*>(arg))
            {
                auto pit = pointerBorrowTarget.find(idArg->name);
                if(pit != pointerBorrowTarget.end())
                {
                    auto vit = variableTypes.find(idArg->name);
                    if(vit == variableTypes.end() ||
                       vit->second != TypeNode::TYPE_PTR)
                    {
                        continue;
                    }
                    if(!isMoveOnlyVariable(pit->second))
                    {
                        continue;
                    }
                    std::string path;
                    auto place = pointerBorrowPlace.find(idArg->name);
                    if(place != pointerBorrowPlace.end())
                    {
                        path = place->second;
                        if(path.compare(0, pit->second.size(), pit->second) ==
                           0)
                        {
                            path.erase(0, pit->second.size());
                            if(!path.empty() && path.front() == '.')
                                path.erase(0, 1);
                        }
                    }
                    if(!registerBorrowInCall(pit->second, path, path.empty(),
                                             true))
                        return false;
                    continue;
                }
            }

            auto* unary = dynamic_cast<UnaryOpNode*>(arg);
            if(!unary || (unary->op != UnaryOpNode::OP_ADDR &&
                          unary->op != UnaryOpNode::OP_ADDR_MUT))
                continue;

            std::string owner;
            std::string path;
            bool isWholeOwner = true;
            if(!describeBorrowPath(unary->operand, owner, path, isWholeOwner))
                continue;

            if(globalNamedValues.find(owner) == globalNamedValues.end())
            {
                std::string borrowedPlace =
                    resolveBorrowPlaceFromLValue(unary->operand);
                if(borrowedPlace.empty())
                    borrowedPlace = owner;
                for(const auto& movedPlace : movedVariables)
                    if(borrowPlacesOverlap(borrowedPlace, movedPlace))
                    {
                        reportError(
                            node->line,
                            "cannot borrow moved value: '" + movedPlace + "'");
                        return false;
                    }
            }

            const bool wantsMutable = (unary->op == UnaryOpNode::OP_ADDR_MUT);
            std::string exclusiveReborrowParent;
            if(wantsMutable)
            {
                if(auto* dereference =
                       dynamic_cast<UnaryOpNode*>(unary->operand))
                {
                    if(dereference->op == UnaryOpNode::OP_DEREF)
                    {
                        if(auto* parent = dynamic_cast<IdentifierNode*>(
                               dereference->operand))
                            if(isMutBorrower(parent->name))
                                exclusiveReborrowParent = parent->name;
                    }
                }
            }
            auto activeIt = activeBorrowers.find(owner);
            auto overlapsActiveBorrower = [&](const std::string& borrower) {
                auto ownerIt = pointerBorrowTarget.find(borrower);
                if(ownerIt != pointerBorrowTarget.end() &&
                   ownerIt->second != owner)
                    return false;
                std::string requestedPlace = owner;
                if(!isWholeOwner && !path.empty())
                    requestedPlace += path.front() == '[' ? path
                                                          : "." + path;
                return borrowerPlaceOverlaps(borrower, owner,
                                             requestedPlace);
            };
            auto hasOverlappingSharedBorrow = [&]() {
                if(activeIt == activeBorrowers.end())
                    return false;
                return std::any_of(activeIt->second.begin(),
                                   activeIt->second.end(),
                                   overlapsActiveBorrower);
            };
            std::string overlappingMutableBorrower;
            for(const auto& activeMutable : activeMutBorrower)
            {
                const bool isReborrowAncestor =
                    activeMutable.second == exclusiveReborrowParent ||
                    isPointerBorrowAncestor(activeMutable.second,
                                            exclusiveReborrowParent);
                if(isReborrowAncestor)
                    continue;
                if(overlapsActiveBorrower(activeMutable.second))
                {
                    overlappingMutableBorrower = activeMutable.second;
                    break;
                }
            }
            if(wantsMutable)
            {
                if(hasOverlappingSharedBorrow())
                {
                    reportError(
                        node->line,
                        "cannot borrow '" + owner +
                            "' as mutable because it is already borrowed");
                    return false;
                }
                if(!overlappingMutableBorrower.empty())
                {
                    reportError(node->line,
                                "cannot borrow '" + owner +
                                    "' as mutable more than once at a time");
                    return false;
                }
            }
            else
            {
                if(!overlappingMutableBorrower.empty())
                {
                    reportError(
                        node->line,
                        "cannot borrow '" + owner +
                            "' as immutable because it is also borrowed "
                            "as mutable by '" +
                            overlappingMutableBorrower + "'");
                    return false;
                }
                if(!sharedReference && hasOverlappingSharedBorrow())
                {
                    std::string by;
                    for(const auto& borrower : activeIt->second)
                        if(overlapsActiveBorrower(borrower))
                        {
                            by = borrower;
                            break;
                        }
                    reportError(node->line,
                                "cannot borrow '" + owner +
                                    "' while already borrowed by '" + by + "'");
                    return false;
                }
            }

            if(!registerBorrowInCall(owner, path, isWholeOwner,
                                     !sharedReference))
                return false;
        }

        for(const auto& movedOwner : movedOwnersInCall)
        {
            if(wholeOwnersInCall.count(movedOwner) ||
               subpathsInCall.find(movedOwner) != subpathsInCall.end())
            {
                reportError(node->line, "cannot move '" + movedOwner +
                                            "' while borrowed in call to '" +
                                            calleeName + "'");
                return false;
            }
        }

        return true;
    };
    auto isFreeLikeFunctionName = [&](const std::string& fnName) -> bool
    {
        if(fnName == "String::free" || fnName == "free")
            return true;
        return fnName.size() > 5 &&
               fnName.compare(fnName.size() - 5, 5, "_free") == 0;
    };
    auto precheckFreeLikeArgument = [&](ExpressionNode* argExpr,
                                        const std::string& fnName) -> bool
    {
        if(!argExpr)
            return true;
        if((fnName == "String::free" || fnName == "free") &&
           dynamic_cast<StringLiteralNode*>(argExpr))
        {
            reportError(
                node->line,
                "cannot free string literal; free only heap-allocated strings");
            return false;
        }
        if(auto* idArg = dynamic_cast<IdentifierNode*>(argExpr))
        {
            if(globalNamedValues.find(idArg->name) == globalNamedValues.end() &&
               isVariableMoved(idArg->name))
            {
                reportError(node->line, idArg->col,
                            "double free or use-after-free of value: '" +
                                idArg->name + "'");
                return false;
            }
        }
        return true;
    };
    auto markFreeLikeConsumed = [&](ExpressionNode* argExpr) -> void
    {
        if(auto* idArg = dynamic_cast<IdentifierNode*>(argExpr))
        {
            if(globalNamedValues.find(idArg->name) == globalNamedValues.end())
                movedVariables.insert(idArg->name);
        }
    };

    // `free_objs(a, b, ...)` is the heterogeneous object equivalent of a
    // C++ fold over `release()`.  Keep this as a compiler intrinsic: ordinary
    // MLang generic parameters are homogeneous and therefore cannot express a
    // parameter pack containing unrelated struct types.  Each synthesized
    // method call goes through the normal method resolver, which gives us the
    // same static checks (struct receiver, visible instance method, and zero
    // explicit arguments) as spelling every `object.release()` separately.
    if(node->name == "free_objs")
    {
        if(node->arguments.empty())
        {
            reportError(node->line,
                        "free_objs expects at least one releasable object");
            return nullptr;
        }

        llvm::Value* lastCall = nullptr;
        for(size_t i = 0; i < node->arguments.size(); ++i)
        {
            MethodCallNode releaseCall(node->arguments[i], "release");
            releaseCall.line = node->line;
            releaseCall.col = node->col;
            lastCall = generateMethodCall(&releaseCall);
            if(!lastCall)
            {
                reportError(node->line,
                            "argument " + std::to_string(i + 1) +
                                " to free_objs must be an object with a "
                                "zero-argument release() method");
                return nullptr;
            }
        }
        return lastCall;
    }

    // Vec::new() — returns an empty list struct {0, null}
    if(node->name == "Vec::new")
    {
        if(!node->arguments.empty())
        {
            reportError(node->line, "Vec::new expects no arguments");
            return nullptr;
        }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);
        return llvm::ConstantAggregateZero::get(listStructType);
    }
    if(node->name == "String::new")
    {
        if(!node->arguments.empty())
        {
            reportError(node->line, "String::new expects no arguments");
            return nullptr;
        }
        initializeStdlibFunctions();
        llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
        llvm::Type* int8Type = llvm::Type::getInt8Ty(context);
        llvm::Value* size = llvm::ConstantInt::get(int64Type, 1);
        llvm::Value* ptr = builder.CreateCall(mallocFunc, {size}, "string_new");
        builder.CreateStore(llvm::ConstantInt::get(int8Type, 0), ptr);
        return ptr;
    }
    if(node->name == "String::with_capacity")
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line,
                        "String::with_capacity expects one argument");
            return nullptr;
        }
        initializeStdlibFunctions();
        llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
        llvm::Type* int8Type = llvm::Type::getInt8Ty(context);
        llvm::Value* cap = generateExpression(node->arguments[0]);
        if(!cap)
            return nullptr;
        if(!cap->getType()->isIntegerTy())
        {
            reportError(node->line,
                        "String::with_capacity expects an integer capacity");
            return nullptr;
        }
        if(cap->getType()->getIntegerBitWidth() < 64)
            cap = builder.CreateSExt(cap, int64Type, "cap_sext");
        else if(cap->getType()->getIntegerBitWidth() > 64)
            cap = builder.CreateTrunc(cap, int64Type, "cap_trunc");
        llvm::Value* size = builder.CreateAdd(
            cap, llvm::ConstantInt::get(int64Type, 1), "string_cap_plus_one");
        llvm::Value* ptr =
            builder.CreateCall(mallocFunc, {size}, "string_with_capacity");
        builder.CreateStore(llvm::ConstantInt::get(int8Type, 0), ptr);
        return ptr;
    }
    if(node->name == "String::free")
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line, "String::free expects one argument");
            return nullptr;
        }
        ExpressionNode* argExpr = node->arguments[0];
        if(!precheckFreeLikeArgument(argExpr, node->name))
            return nullptr;
        initializeStdlibFunctions();
        llvm::Value* ptr = generateExpression(argExpr);
        if(!ptr)
            return nullptr;
        consumeMoveFromExpression(argExpr, node->line,
                                  "passing argument to String::free");
        markFreeLikeConsumed(argExpr);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        if(ptr->getType() != ptrType && ptr->getType()->isPointerTy())
            ptr = builder.CreateBitCast(ptr, ptrType, "string_free_cast");
        if(!ptr->getType()->isPointerTy())
        {
            reportError(node->line, "String::free expects a string/pointer");
            return nullptr;
        }
        return builder.CreateCall(freeFunc, {ptr});
    }
    if(node->name == "String::from" || node->name == "String::to_utf8")
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line, node->name + " expects one argument");
            return nullptr;
        }
        llvm::Value* arg = generateExpression(node->arguments[0]);
        if(!arg)
            return nullptr;
        initializeStdlibFunctions();
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::FunctionType* cloneFnType =
            llvm::FunctionType::get(ptrType, {ptrType}, false);
        llvm::FunctionCallee cloneFn = module->getOrInsertFunction(
            "__mlang_std_strbuf_clone", cloneFnType);
        return builder.CreateCall(cloneFn, {arg}, "string_from");
    }

    if(node->name == "thread_spawn" || node->name == "thread::spawn")
        return generateThreadSpawn(node);
    if(node->name == "thread_join")
        return generateThreadJoin(node);
    if(node->name == "mutex_create")
        return generateMutexCreate(node);
    if(node->name == "mutex_lock")
        return generateMutexLock(node);
    if(node->name == "mutex_unlock")
        return generateMutexUnlock(node);
    if(node->name == "mutex_destroy")
        return generateMutexDestroy(node);
    if(node->name == "atomic_i64_new")
        return generateAtomicI64New(node);
    if(node->name == "atomic_i64_load")
        return generateAtomicI64Load(node);
    if(node->name == "atomic_i64_store")
        return generateAtomicI64Store(node);
    if(node->name == "atomic_i64_add")
        return generateAtomicI64Add(node);
    if(node->name == "atomic_i64_free")
        return generateAtomicI64Free(node);

    auto functionItem = functionItemVariables.find(node->name);
    if(functionItem != functionItemVariables.end() &&
       functionItem->second.size() == 1)
    {
        const std::string variableName = node->name;
        const std::string targetName = *functionItem->second.begin();
        FunctionCallNode directCall(targetName);
        directCall.line = node->line;
        directCall.col = node->col;
        directCall.arguments = node->arguments;
        functionItemVariables.erase(functionItem);
        llvm::Value* result = generateFunctionCall(&directCall);
        functionItemVariables[variableName] = {targetName};
        return result;
    }

    // Runtime closures are represented by a code pointer and an environment
    // pointer. Their declared parameter types determine the indirect call ABI.
    auto closureSignature = functionTypeVariables.find(node->name);
    if(closureSignature != functionTypeVariables.end())
    {
        FunctionTypeNode* closureType = closureSignature->second;
        const size_t expectedCount = closureType->parameterTypes
                                         ? closureType->parameterTypes->types.size()
                                         : 0;
        if(node->arguments.size() != expectedCount)
        {
            reportError(node->line, "runtime closure '" + node->name +
                                        "' expects " +
                                        std::to_string(expectedCount) +
                                        " argument(s), got " +
                                        std::to_string(node->arguments.size()));
            return nullptr;
        }
        std::vector<bool> sharedReferenceParameters(expectedCount, false);
        if(closureType->parameterTypes)
            for(size_t i = 0; i < expectedCount; ++i)
                if(auto* reference = dynamic_cast<ReferenceTypeNode*>(
                       closureType->parameterTypes->types[i]))
                    sharedReferenceParameters[i] = !reference->isMutable;
        if(!validateTemporaryBorrowArguments(
               node->arguments, node->name, "",
               &sharedReferenceParameters))
            return nullptr;
        std::vector<FunctionDefNode*> knownTargets;
        bool hasKnownCallableTargetSet = false;
        bool allCallableTargetsResolved = false;
        auto knownTargetSet = functionItemVariables.find(node->name);
        if(knownTargetSet != functionItemVariables.end() &&
           !knownTargetSet->second.empty())
        {
            hasKnownCallableTargetSet = true;
            allCallableTargetsResolved = true;
            const std::string expectedSignature = typeMangle(closureType);
            for(const auto& targetName : knownTargetSet->second)
            {
                auto overloads = functionOverloads.find(targetName);
                if(overloads == functionOverloads.end())
                {
                    allCallableTargetsResolved = false;
                    break;
                }
                FunctionDefNode* target = nullptr;
                for(const auto& overload : overloads->second)
                {
                    if(!isOverloadVisible(overload) || !overload.node ||
                       !overload.node->parameters ||
                       overload.node->parameters->isVarArg ||
                       !overload.node->typeParams.empty())
                        continue;
                    auto* parameters = new TypeListNode();
                    for(auto* parameter :
                        overload.node->parameters->parameters)
                        parameters->addType(cloneTypeNode(parameter->type));
                    auto* candidateType = new FunctionTypeNode(
                        parameters, cloneTypeNode(overload.node->returnType));
                    if(typeMangle(candidateType) != expectedSignature)
                        continue;
                    if(target)
                    {
                        target = nullptr;
                        break;
                    }
                    target = overload.node;
                }
                if(!target)
                {
                    allCallableTargetsResolved = false;
                    break;
                }
                knownTargets.push_back(target);
            }
        }
        if(closureType->parameterTypes)
            for(size_t i = 0; i < expectedCount; ++i)
                if(dynamic_cast<PointerTypeNode*>(
                       closureType->parameterTypes->types[i]))
                {
                    if(hasKnownCallableTargetSet &&
                       allCallableTargetsResolved)
                    {
                        bool mutatesPointer = false;
                        bool allTargetsHaveBodies = true;
                        for(auto* target : knownTargets)
                        {
                            if(!target || !target->body)
                            {
                                allTargetsHaveBodies = false;
                                mutatesPointer = true;
                                continue;
                            }
                            if(functionMayWritePointerParameter(target, i))
                                mutatesPointer = true;
                        }
                        if(expressionIsKnownSharedBorrow(node->arguments[i]) &&
                           mutatesPointer)
                        {
                            reportError(
                                node->line,
                                "cannot pass a shared borrow to mutating "
                                "callable pointer parameter " +
                                    std::to_string(i + 1));
                            return nullptr;
                        }
                        if(unsafeDepth == 0 &&
                           expressionCarriesLocalBorrow(node->arguments[i]))
                        {
                            bool resultCanCarryBorrow = false;
                            if(closureType->returnType &&
                               closureType->returnType->kind ==
                                   TypeNode::TYPE_PTR)
                            {
                                resultCanCarryBorrow = std::any_of(
                                    knownTargets.begin(), knownTargets.end(),
                                    [&](FunctionDefNode* target) {
                                        auto record = std::find_if(
                                            functionOverloads.begin(),
                                            functionOverloads.end(),
                                            [&](const auto& namedOverloads) {
                                                return std::any_of(
                                                    namedOverloads.second.begin(),
                                                    namedOverloads.second.end(),
                                                    [&](const auto& overload) {
                                                        return overload.node ==
                                                               target;
                                                    });
                                            });
                                        if(record == functionOverloads.end())
                                            return true;
                                        for(const auto& overload :
                                            record->second)
                                            if(overload.node == target)
                                                return !functionPointerReturnSummaryComplete
                                                            .count(overload.symbolName);
                                        return true;
                                    });
                            }
                            const auto returnKind =
                                closureType->returnType
                                    ? closureType->returnType->kind
                                    : TypeNode::TYPE_VOID;
                            const bool returnsSimpleValue =
                                returnKind == TypeNode::TYPE_VOID ||
                                returnKind == TypeNode::TYPE_BOOL ||
                                returnKind == TypeNode::TYPE_INT ||
                                returnKind == TypeNode::TYPE_I8 ||
                                returnKind == TypeNode::TYPE_I16 ||
                                returnKind == TypeNode::TYPE_I32 ||
                                returnKind == TypeNode::TYPE_I64 ||
                                returnKind == TypeNode::TYPE_U8 ||
                                returnKind == TypeNode::TYPE_U16 ||
                                returnKind == TypeNode::TYPE_U32 ||
                                returnKind == TypeNode::TYPE_U64 ||
                                returnKind == TypeNode::TYPE_FLOAT ||
                                returnKind == TypeNode::TYPE_DOUBLE;
                            const bool canSafelyTrackResult =
                                returnsSimpleValue ||
                                (returnKind == TypeNode::TYPE_PTR &&
                                 !resultCanCarryBorrow);
                            if(!allTargetsHaveBodies || !canSafelyTrackResult)
                            {
                                reportError(
                                    node->line,
                                    "cannot pass a borrow of local storage to "
                                    "opaque callable pointer parameter " +
                                        std::to_string(i + 1) +
                                        " outside unsafe because the callable "
                                        "may retain it");
                                return nullptr;
                            }
                        }
                        continue;
                    }
                    if(expressionIsKnownSharedBorrow(node->arguments[i]))
                    {
                        reportError(
                            node->line,
                            "cannot pass a shared borrow to opaque callable "
                            "pointer parameter " + std::to_string(i + 1));
                        return nullptr;
                    }
                    if(unsafeDepth == 0 &&
                       expressionCarriesLocalBorrow(node->arguments[i]) &&
                       !(functionTypeParameters.count(node->name) &&
                         expressionIsPointerParameterBorrow(
                             node->arguments[i])))
                    {
                        reportError(
                            node->line,
                            "cannot pass a borrow of local storage to opaque "
                            "callable pointer parameter " +
                                std::to_string(i + 1) +
                                " outside unsafe because the callable may "
                                "retain it");
                        return nullptr;
                    }
                }
        auto valueIt = namedValues.find(node->name);
        auto* alloca = valueIt == namedValues.end()
                           ? nullptr
                           : llvm::dyn_cast<llvm::AllocaInst>(valueIt->second);
        if(!alloca)
        {
            reportError(node->line,
                        "runtime closure value '" + node->name +
                            "' is not available in this scope");
            return nullptr;
        }
        llvm::Value* closureValue = builder.CreateLoad(
            alloca->getAllocatedType(), alloca, node->name + ".closure.value");
        llvm::Value* code = builder.CreateExtractValue(
            closureValue, 0, node->name + ".code");
        llvm::Value* environment = builder.CreateExtractValue(
            closureValue, 1, node->name + ".environment");
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* erasedPointerType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* erasedPointerType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> signatureParameters = {erasedPointerType};
        std::vector<llvm::Value*> callArguments = {environment};
        if(closureType->parameterTypes)
        {
            for(size_t i = 0; i < closureType->parameterTypes->types.size(); ++i)
            {
                TypeNode* parameter = closureType->parameterTypes->types[i];
                llvm::Type* parameterType = parameterLLVMType(parameter);
                auto* reference =
                    dynamic_cast<ReferenceTypeNode*>(parameter);
                auto* borrow =
                    dynamic_cast<UnaryOpNode*>(node->arguments[i]);
                if(reference && reference->isMutable &&
                   (!borrow || borrow->op != UnaryOpNode::OP_ADDR_MUT))
                {
                    reportError(node->line,
                                "runtime closure parameter " +
                                    std::to_string(i + 1) +
                                    " expects &mut argument");
                    return nullptr;
                }
                ExpressionNode* argumentExpression = node->arguments[i];
                if(reference && !reference->isMutable && borrow &&
                   borrow->op == UnaryOpNode::OP_ADDR)
                    argumentExpression = borrow->operand;
                llvm::Value* argument =
                    generateExpression(argumentExpression);
                if(!parameterType || !argument)
                {
                    reportError(node->line,
                                "runtime closure parameter types must be "
                                "known and arguments must be valid");
                    return nullptr;
                }
                if(argument->getType() != parameterType)
                {
                    if(argument->getType()->isIntegerTy() &&
                       parameterType->isIntegerTy())
                        argument = builder.CreateIntCast(
                            argument, parameterType, true,
                            "closure.arg.cast");
                    else if(argument->getType()->isFloatingPointTy() &&
                            parameterType->isFloatingPointTy())
                        argument = builder.CreateFPCast(
                            argument, parameterType, "closure.arg.cast");
                    else
                    {
                        reportError(node->line,
                                    "runtime closure argument type does not "
                                    "match its parameter");
                        return nullptr;
                    }
                }
                signatureParameters.push_back(parameterType);
                callArguments.push_back(argument);
            }
        }
        llvm::FunctionType* signature = llvm::FunctionType::get(
            getLLVMTypeFromNode(closureType->returnType),
            signatureParameters, false);
#if LLVM_VERSION_MAJOR < 15
        code = builder.CreateBitCast(code, signature->getPointerTo(),
                                     node->name + ".callable");
#endif
        llvm::Value* callResult = builder.CreateCall(
            signature, code, callArguments,
            signature->getReturnType()->isVoidTy() ? ""
                                                  : node->name + ".call");

        auto runtimeClosure = runtimeClosureVariables.find(node->name);
        if(runtimeClosure != runtimeClosureVariables.end() &&
           runtimeClosure->second)
            applyRuntimeClosurePointerBorrowEffects(
                node->name, runtimeClosure->second, node->line);
        return callResult;
    }

    // Inline closure/lambda call: inc(...)
    {
        auto closIt = closureVariables.find(node->name);
        if(closIt != closureVariables.end())
        {
            // Re-entrancy guard — recursive inline closures are unsupported.
            if(!activeInlineClosures.insert(node->name).second)
            {
                reportError(node->line, "recursive call to inline closure '" +
                                            node->name + "' is not supported");
                return nullptr;
            }
            ClosureNode* closure = closIt->second;
            size_t expectedArgs =
                (closure->parameters ? closure->parameters->parameters.size()
                                     : 0);
            if(node->arguments.size() != expectedArgs)
            {
                reportError(node->line,
                            "inline lambda '" + node->name + "' expects " +
                                std::to_string(expectedArgs) +
                                " argument(s), got " +
                                std::to_string(node->arguments.size()));
                activeInlineClosures.erase(node->name);
                return nullptr;
            }

            auto savedNamedValues = namedValues;
            auto savedConstantVariables = constantVariables;
            auto savedVariableTypes = variableTypes;
            auto savedStructVariableTypes = structVariableTypes;
            auto savedTraitObjectVariableTypes = traitObjectVariableTypes;
            auto savedEnumVariableTypes = enumVariableTypes;
            auto savedListElementTypes = listElementTypes;
            auto savedMapKeyValueTypes = mapKeyValueTypes;
            auto savedTupleElementTypes = tupleElementTypes;
            auto savedPointerElementTypes = pointerElementTypes;
            auto savedArrayCapacities = arrayCapacities;
            auto savedMultiarrayMutability = multiarrayMutability;
            auto savedClosureCaptureReferenceAliases =
                closureCaptureReferenceAliases;

            auto restoreInlineState = [&]()
            {
                namedValues = savedNamedValues;
                constantVariables = savedConstantVariables;
                variableTypes = savedVariableTypes;
                structVariableTypes = savedStructVariableTypes;
                traitObjectVariableTypes = savedTraitObjectVariableTypes;
                enumVariableTypes = savedEnumVariableTypes;
                listElementTypes = savedListElementTypes;
                mapKeyValueTypes = savedMapKeyValueTypes;
                tupleElementTypes = savedTupleElementTypes;
                pointerElementTypes = savedPointerElementTypes;
                arrayCapacities = savedArrayCapacities;
                multiarrayMutability = savedMultiarrayMutability;
                closureCaptureReferenceAliases =
                    savedClosureCaptureReferenceAliases;
            };

            auto captureAliases = closureCaptureAliases.find(node->name);
            if(captureAliases != closureCaptureAliases.end())
            {
                for(const auto& capture : captureAliases->second)
                {
                    const std::string& capturedName = capture.first;
                    const std::string& hiddenName = capture.second;
                    auto hiddenValue = namedValues.find(hiddenName);
                    if(hiddenValue == namedValues.end())
                        continue;
                    llvm::Type* capturePointerType =
                        llvm::cast<llvm::AllocaInst>(hiddenValue->second)
                            ->getAllocatedType();
                    llvm::Value* captureAddress = builder.CreateLoad(
                        capturePointerType, hiddenValue->second,
                        capturedName + ".capture");
                    TypeNode* capturedType =
                        closureCaptureTypes[node->name][capturedName];
                    if(!capturedType)
                        continue;
                    namedValues[capturedName] = captureAddress;
                    closureCaptureReferenceAliases.insert(capturedName);
                    auto captureDepth = variableScopeDepth.find(hiddenName);
                    if(captureDepth != variableScopeDepth.end())
                        variableScopeDepth[capturedName] = captureDepth->second;
                    if(capturedType)
                    {
                        variableTypes[capturedName] = capturedType->kind;
                        if(auto* structType =
                               dynamic_cast<StructTypeRefNode*>(capturedType))
                            structVariableTypes[capturedName] =
                                structType->structName;
                        if(auto* listType =
                               dynamic_cast<GenericListTypeNode*>(capturedType))
                        {
                            variableTypes[capturedName] = TypeNode::TYPE_LIST;
                            listElementTypes[capturedName] =
                                listType->elementType;
                        }
                        if(auto* mapType =
                               dynamic_cast<MapTypeNode*>(capturedType))
                        {
                            variableTypes[capturedName] = TypeNode::TYPE_MAP;
                            mapKeyValueTypes[capturedName] = {
                                mapType->keyType, mapType->valueType};
                        }
                        if(auto* tupleType =
                               dynamic_cast<TupleTypeNode*>(capturedType))
                        {
                            variableTypes[capturedName] = TypeNode::TYPE_TUPLE;
                            tupleElementTypes[capturedName] =
                                tupleType->elementTypes->types;
                        }
                        if(auto* pointerType =
                               dynamic_cast<PointerTypeNode*>(capturedType))
                        {
                            variableTypes[capturedName] = TypeNode::TYPE_PTR;
                            pointerElementTypes[capturedName] =
                                pointerType->elementType;
                        }
                    }
                    if(closureConstCaptures[node->name].count(capturedName))
                        constantVariables.insert(capturedName);
                    else
                        constantVariables.erase(capturedName);
                }
            }

            // Bind lambda arguments to local parameter variables.
            for(size_t i = 0; i < expectedArgs; ++i)
            {
                auto* param = closure->parameters->parameters[i];
                llvm::Value* argVal = generateExpression(node->arguments[i]);
                if(!argVal)
                {
                    restoreInlineState();
                    activeInlineClosures.erase(node->name);
                    return nullptr;
                }
                consumeMoveFromExpression(
                    node->arguments[i], node->line,
                    "passing argument to inline lambda '" + node->name + "'");
                llvm::Type* expectedType = getLLVMTypeFromNode(param->type);
                if(!expectedType)
                {
                    restoreInlineState();
                    activeInlineClosures.erase(node->name);
                    return nullptr;
                }
                if(argVal->getType() != expectedType)
                {
                    int cost = 0;
                    if(!canConvertType(argVal->getType(), expectedType, cost))
                    {
                        reportError(node->line,
                                    "argument " + std::to_string(i + 1) +
                                        " has wrong type for inline lambda '" +
                                        node->name + "'");
                        restoreInlineState();
                        activeInlineClosures.erase(node->name);
                        return nullptr;
                    }
                    if(argVal->getType()->isIntegerTy() &&
                       expectedType->isIntegerTy())
                    {
                        unsigned srcBits =
                            argVal->getType()->getIntegerBitWidth();
                        unsigned dstBits = expectedType->getIntegerBitWidth();
                        if(srcBits > dstBits)
                            argVal = builder.CreateTrunc(argVal, expectedType,
                                                         "lam.arg.trunc");
                        else if(srcBits < dstBits)
                            argVal = builder.CreateSExt(argVal, expectedType,
                                                        "lam.arg.sext");
                    }
                    else if(argVal->getType()->isIntegerTy() &&
                            expectedType->isFloatingPointTy())
                    {
                        argVal = builder.CreateSIToFP(argVal, expectedType,
                                                      "lam.arg.sitofp");
                    }
                    else if(argVal->getType()->isFloatingPointTy() &&
                            expectedType->isIntegerTy())
                    {
                        argVal = builder.CreateFPToSI(argVal, expectedType,
                                                      "lam.arg.fptosi");
                    }
                    else if(argVal->getType()->isFloatingPointTy() &&
                            expectedType->isFloatingPointTy())
                    {
                        argVal = builder.CreateFPCast(argVal, expectedType,
                                                      "lam.arg.fpcast");
                    }
                    else if(argVal->getType()->isPointerTy() &&
                            expectedType->isPointerTy())
                    {
                        argVal = builder.CreateBitCast(argVal, expectedType,
                                                       "lam.arg.ptrcast");
                    }
                }

                llvm::AllocaInst* alloca = builder.CreateAlloca(
                    expectedType, nullptr, param->name + ".lam.addr");
                argVal = applyStructCopySemantics(argVal, param->type);
                builder.CreateStore(argVal, alloca);
                namedValues[param->name] = alloca;
                recordVariableScopeDepth(param->name);
                variableTypes[param->name] = param->type->kind;

                if(auto* structType =
                       dynamic_cast<StructTypeRefNode*>(param->type))
                {
                    std::string resolvedEnumName =
                        resolveVisibleEnumName(structType->structName);
                    if(!resolvedEnumName.empty())
                    {
                        variableTypes[param->name] = TypeNode::TYPE_INT;
                        enumVariableTypes[param->name] = resolvedEnumName;
                    }
                    else
                        structVariableTypes[param->name] =
                            structType->structName;
                }
                if(auto* genListType =
                       dynamic_cast<GenericListTypeNode*>(param->type))
                {
                    listElementTypes[param->name] = genListType->elementType;
                    if(auto* arrayType =
                           dynamic_cast<ArrayTypeNode*>(genListType))
                        arrayCapacities[param->name] = arrayType->capacity;
                    if(auto* multiarrayType =
                           dynamic_cast<MultiArrayTypeNode*>(genListType))
                        multiarrayMutability[param->name] =
                            multiarrayType->elementsMutable;
                }
                if(auto* mapType = dynamic_cast<MapTypeNode*>(param->type))
                {
                    mapKeyValueTypes[param->name] =
                        std::make_pair(mapType->keyType, mapType->valueType);
                }
                if(auto* ptrType = dynamic_cast<PointerTypeNode*>(param->type))
                {
                    pointerElementTypes[param->name] = ptrType->elementType;
                }
            }

            // Clear loop context so break/continue don't escape the closure.
            auto savedBreak = loopBreakBlocks;
            auto savedContinue = loopContinueBlocks;
            loopBreakBlocks.clear();
            loopContinueBlocks.clear();

            InlineClosureReturnState closureReturn;
            closureReturn.exitBlock = llvm::BasicBlock::Create(
                context, "lambda.exit", builder.GetInsertBlock()->getParent());
            inlineClosureReturnStates.push_back(&closureReturn);
            enterCleanupScope();
            if(closure->body)
            {
                for(auto* stmt : closure->body->statements)
                {
                    generateStatement(stmt);
                    if(builder.GetInsertBlock() &&
                       mlang::llvm_compat::terminatorOrNull(
                           builder.GetInsertBlock()))
                        break;
                }
            }
            exitCleanupScope();

            if(builder.GetInsertBlock() &&
               !mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                builder.CreateBr(closureReturn.exitBlock);
            inlineClosureReturnStates.pop_back();
            builder.SetInsertPoint(closureReturn.exitBlock);

            loopBreakBlocks = std::move(savedBreak);
            loopContinueBlocks = std::move(savedContinue);
            restoreInlineState();
            activeInlineClosures.erase(node->name);
            if(closureReturn.resultStorage)
                return builder.CreateLoad(
                    closureReturn.resultStorage->getAllocatedType(),
                    closureReturn.resultStorage, "lambda.result");
            return nullptr; // inline closures without a value return are void
        }
    }

    auto hasRegisteredOverload = [&](FunctionDefNode* fn) -> bool
    {
        if(!fn)
            return false;
        auto it = functionOverloads.find(fn->name);
        if(it == functionOverloads.end())
            return false;
        std::string sig = functionSignatureKey(fn);
        for(const auto& info : it->second)
        {
            if(info.signatureKey == sig &&
               info.sourceModule == fn->sourceModule)
                return true;
        }
        return false;
    };

    auto isCompositeSemanticType = [&](TypeNode* type) -> bool
    {
        while(auto* refType = dynamic_cast<ReferenceTypeNode*>(type))
            type = refType->elementType;
        if(!type)
            return false;
        return dynamic_cast<GenericListTypeNode*>(type) != nullptr ||
               dynamic_cast<MapTypeNode*>(type) != nullptr ||
               dynamic_cast<TupleTypeNode*>(type) != nullptr ||
               dynamic_cast<FunctionTypeNode*>(type) != nullptr;
    };

    auto isListToFixedArrayCompatible = [&](TypeNode* expected,
                                            TypeNode* actual) -> bool
    {
        while(auto* refType = dynamic_cast<ReferenceTypeNode*>(expected))
            expected = refType->elementType;
        while(auto* refType = dynamic_cast<ReferenceTypeNode*>(actual))
            actual = refType->elementType;
        auto* expectedList = dynamic_cast<GenericListTypeNode*>(expected);
        auto* actualArray = dynamic_cast<ArrayTypeNode*>(actual);
        if(!expectedList || !actualArray ||
           dynamic_cast<MultiArrayTypeNode*>(actualArray))
            return false;
        return typeMangle(expectedList->elementType) ==
               typeMangle(actualArray->elementType);
    };

    auto concreteTypeImplementsTrait = [&](TypeNode* type,
                                           const std::string& traitName) -> bool
    {
        if(!type)
            return false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
        {
            std::string resolved =
                resolveVisibleStructName(structRef->structName);
            if(resolved.empty())
                resolved = structRef->structName;
            auto it = structImplementedTraits.find(resolved);
            return it != structImplementedTraits.end() &&
                   it->second.find(traitName) != it->second.end();
        }
        if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(type))
        {
            std::string resolved = getOrCreateMonomorphizedStruct(
                genRef->structName, genRef->typeArgs);
            auto it = structImplementedTraits.find(resolved);
            return it != structImplementedTraits.end() &&
                   it->second.find(traitName) != it->second.end();
        }
        return false;
    };

    auto semanticArgumentType = [&](ExpressionNode* expr) -> TypeNode*
    {
        if(!expr)
            return nullptr;
        if(auto* identifier = dynamic_cast<IdentifierNode*>(expr))
        {
            auto functionItem = functionOverloads.find(identifier->name);
            if(functionItem != functionOverloads.end() &&
               !namedValues.count(identifier->name) &&
               !globalNamedValues.count(identifier->name))
                return nullptr;
        }
        if(TypeNode* ty = getLValueType(expr, node->line))
            return ty;

        if(dynamic_cast<StringLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_STR8);
        if(dynamic_cast<BoolLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_BOOL);
        if(dynamic_cast<IntLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_I64);
        if(dynamic_cast<FloatLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_FLOAT);
        if(dynamic_cast<DoubleLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_DOUBLE);
        return nullptr;
    };

    auto registerModuleFunctionsOnDemand = [&](const std::string& moduleName)
    {
        if(!moduleLoader || moduleName.empty())
            return;
        auto moduleFns = moduleLoader->getModuleFunctions(moduleName);
        for(auto* fn : moduleFns)
        {
            if(!fn || fn->name.empty() || hasRegisteredOverload(fn))
                continue;
            if(!fn->typePackParam.empty())
            {
                variadicGenericFunctionTemplates[fn->name].push_back(fn);
                continue;
            }
            if(!fn->typeParams.empty())
            {
                registerFunctionOverload(fn, nullptr);
                continue;
            }
            llvm::Function* decl = generateFunctionDeclaration(fn);
            registerFunctionOverload(fn, decl);
            bool alreadyQueued = false;
            for(auto* queued : deferredModuleFunctionDefs)
            {
                if(queued == fn)
                {
                    alreadyQueued = true;
                    break;
                }
            }
            if(!alreadyQueued)
                deferredModuleFunctionDefs.push_back(fn);
        }
    };

    auto overloadIt = functionOverloads.find(node->name);
    std::string qualifiedModuleFilter;
    bool usingTailQualifiedLookup = false;
    size_t qualifiedPos = node->name.rfind("::");
    if(qualifiedPos != std::string::npos)
    {
        qualifiedModuleFilter = node->name.substr(0, qualifiedPos);
        bool treatAsModulePath = false;
        if(moduleLoader && !qualifiedModuleFilter.empty())
        {
            if(moduleLoader->getModule(qualifiedModuleFilter) != nullptr)
                treatAsModulePath = true;
        }
        if(treatAsModulePath)
        {
            registerModuleFunctionsOnDemand(qualifiedModuleFilter);
            if(overloadIt == functionOverloads.end())
            {
                std::string tailName = node->name.substr(qualifiedPos + 2);
                auto tailIt = functionOverloads.find(tailName);
                if(tailIt != functionOverloads.end())
                {
                    overloadIt = tailIt;
                    usingTailQualifiedLookup = true;
                }
            }
        }
    }

    // C++20-style source_location::current(): unlike an ordinary function,
    // this value belongs to the call expression, so lower it before emitting
    // a regular call into the stdlib module.
    const std::string sourceLocationName =
        qualifiedPos == std::string::npos ? node->name
                                          : node->name.substr(qualifiedPos + 2);
    auto sourceLocationOverloads = functionOverloads.find(sourceLocationName);
    bool isSourceLocationCurrent = false;
    FunctionOverloadInfo* sourceLocationOverload = nullptr;
    if(sourceLocationName == "current" &&
       sourceLocationOverloads != functionOverloads.end())
    {
        for(auto& candidate : sourceLocationOverloads->second)
        {
            if(candidate.sourceModule.size() >=
                   std::string("std::source_location").size() &&
               candidate.sourceModule.compare(
                   candidate.sourceModule.size() -
                       std::string("std::source_location").size(),
                   std::string("std::source_location").size(),
                   "std::source_location") == 0)
            {
                isSourceLocationCurrent = true;
                sourceLocationOverload = &candidate;
                break;
            }
        }
    }
    if(isSourceLocationCurrent)
    {
        if(!node->arguments.empty())
        {
            reportError(node->line,
                        "std::source_location::current() takes no arguments");
            return nullptr;
        }
        if(!sourceLocationOverload || !sourceLocationOverload->function ||
           !sourceLocationOverload->function->getReturnType()->isStructTy())
        {
            reportError(node->line,
                        "std::source_location::current() has an invalid "
                        "return type");
            return nullptr;
        }

        auto* resultType = llvm::cast<llvm::StructType>(
            sourceLocationOverload->function->getReturnType());
        const std::string resultName = resultType->getName().str();
        auto members = structMembers.find(resultName);
        auto layouts = structFieldLayouts.find(resultName);
        if(members == structMembers.end() ||
           layouts == structFieldLayouts.end())
        {
            reportError(node->line,
                        "std::source_location::current() requires the "
                        "std::source_location::SourceLocation struct");
            return nullptr;
        }

        std::string callFile = node->file ? node->file : "";
        if(callFile.empty())
            callFile = diagnosticFileName();
#if LLVM_VERSION_MAJOR >= 21
        llvm::Value* fileValue =
            builder.CreateGlobalString(callFile, "source.location.file");
#else
        llvm::Value* fileValue =
            builder.CreateGlobalStringPtr(callFile, "source.location.file");
#endif
        llvm::Function* caller = builder.GetInsertBlock()
                                     ? builder.GetInsertBlock()->getParent()
                                     : nullptr;
#if LLVM_VERSION_MAJOR >= 21
        llvm::Value* functionValue = builder.CreateGlobalString(
            caller ? caller->getName().str() : std::string("<global>"),
            "source.location.function");
#else
        llvm::Value* functionValue = builder.CreateGlobalStringPtr(
            caller ? caller->getName().str() : std::string("<global>"),
            "source.location.function");
#endif
        llvm::Value* lineValue = llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(context),
            static_cast<uint64_t>(std::max(node->line, 0)));
        llvm::Value* columnValue = llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(context),
            static_cast<uint64_t>(std::max(node->col, 1)));

        llvm::Value* result = llvm::UndefValue::get(resultType);
        for(size_t i = 0; i < members->second.size(); ++i)
        {
            const std::string& fieldName = members->second[i].first;
            const auto& layout = layouts->second[i];
            llvm::Value* fieldValue = nullptr;
            if(fieldName == "file")
                fieldValue = fileValue;
            else if(fieldName == "function")
                fieldValue = functionValue;
            else if(fieldName == "line")
                fieldValue = lineValue;
            else if(fieldName == "column")
                fieldValue = columnValue;
            else
            {
                reportError(node->line,
                            "unknown field '" + fieldName +
                                "' in std::source_location::SourceLocation");
                return nullptr;
            }
            result = builder.CreateInsertValue(result, fieldValue,
                                               {layout.storageIndex},
                                               "source.location." + fieldName);
        }
        return result;
    }
    std::vector<TypeNode*> preInstantiationArrayArgumentTypes(
        node->arguments.size(), nullptr);
    for(size_t i = 0; i < node->arguments.size(); ++i)
        if(auto* identifier =
               dynamic_cast<IdentifierNode*>(node->arguments[i]);
           identifier && arrayCapacities.count(identifier->name))
            preInstantiationArrayArgumentTypes[i] =
                semanticArgumentType(node->arguments[i]);

    if(overloadIt != functionOverloads.end())
        instantiateGenericFunctionOverloads(node, node->name,
                                            overloadIt->second);
    if(overloadIt == functionOverloads.end())
    {
        // Static struct method call syntax: Type::method(...)
        size_t scopePos = node->name.rfind("::");
        if(scopePos != std::string::npos)
        {
            std::string structName = node->name.substr(0, scopePos);
            std::string methodName = node->name.substr(scopePos + 2);
            std::string displayStructName = structName;

            if(TypeNode* parsedType = Helpers::type_from_text(structName))
            {
                std::set<std::string> emptyTypeParams;
                std::vector<std::string> aliasStack;
                parsedType = resolveTypeAliasNode(parsedType, emptyTypeParams,
                                                  aliasStack);
                // Static generic method calls nested inside a generic
                // function can spell their receiver using the caller's type
                // parameters (for example FlatMap<K, V>::new()). Resolve
                // those names before asking for a monomorphization; otherwise
                // the generic struct is instantiated with self-referential
                // K/V type refs and type lowering recurses indefinitely.
                std::map<std::string, TypeNode*> callTypeBindings =
                    activeFunctionTypeBindings;
                for(const auto& binding : activeTypeParamBindings)
                    callTypeBindings[binding.first] = binding.second;
                if(!callTypeBindings.empty())
                {
                    std::vector<std::string> typeParamNames;
                    std::vector<TypeNode*> typeArgs;
                    typeParamNames.reserve(callTypeBindings.size());
                    typeArgs.reserve(callTypeBindings.size());
                    for(const auto& binding : callTypeBindings)
                    {
                        typeParamNames.push_back(binding.first);
                        typeArgs.push_back(binding.second);
                    }
                    parsedType = substituteTypeParams(parsedType,
                                                      typeParamNames, typeArgs);
                }
                if(auto* genericStructType =
                       dynamic_cast<GenericStructTypeRefNode*>(parsedType))
                {
                    std::string visibleStructName =
                        Helpers::resolve_visible_struct_base_name(
                            genericStructType->structName,
                            genericStructTemplates, structMethods,
                            structVisibility);
                    structName = getOrCreateMonomorphizedStruct(
                        visibleStructName, genericStructType->typeArgs);
                }
                else if(auto* structType =
                            dynamic_cast<StructTypeRefNode*>(parsedType))
                {
                    structName = Helpers::resolve_visible_struct_base_name(
                        structType->structName, genericStructTemplates,
                        structMethods, structVisibility);
                }
            }

            auto sit = structMethods.find(structName);
            std::string resolvedStructName = structName;
            if(sit == structMethods.end())
            {
                // Imported structs can be namespaced internally. Allow calling
                // with the visible tail name (e.g. File::open).
                for(auto it = structMethods.begin(); it != structMethods.end();
                    ++it)
                {
                    const std::string& candidate = it->first;
                    if(candidate == structName)
                    {
                        sit = it;
                        resolvedStructName = candidate;
                        break;
                    }
                    if(candidate.size() > structName.size() &&
                       candidate.compare(candidate.size() - structName.size(),
                                         structName.size(), structName) == 0)
                    {
                        char sep =
                            candidate[candidate.size() - structName.size() - 1];
                        if(sep == ':' || sep == '.' || sep == '_')
                        {
                            sit = it;
                            resolvedStructName = candidate;
                            break;
                        }
                    }
                }
            }

            if(sit != structMethods.end())
            {
                auto mit = sit->second.find(methodName);
                if(mit != sit->second.end() && mit->second.second)
                {
                    if(!mit->second.second->isStatic)
                    {
                        reportError(node->line,
                                    "instance method '" + displayStructName +
                                        "::" + methodName +
                                        "' must be called on a value");
                        return nullptr;
                    }

                    std::string mangledName =
                        resolvedStructName + "_" + methodName;
                    llvm::Function* callee = module->getFunction(mangledName);
                    if(!callee)
                    {
                        callee = generateMethodDeclaration(resolvedStructName,
                                                           mit->second.second);
                    }
                    if(callee && callee->empty())
                    {
                        llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
                        auto savedNamedValues = namedValues;
                        auto savedConstantVariables = constantVariables;
                        auto savedVariableTypes = variableTypes;
                        auto savedStructVariableTypes = structVariableTypes;
                        auto savedTraitObjectVariableTypes =
                            traitObjectVariableTypes;
                        auto savedEnumVariableTypes = enumVariableTypes;
                        auto savedListElementTypes = listElementTypes;
                        auto savedMapKeyValueTypes = mapKeyValueTypes;
                        auto savedTupleElementTypes = tupleElementTypes;
                        auto savedPointerElementTypes = pointerElementTypes;
                        auto savedMovedVariables = movedVariables;
                        auto savedPointerBorrowTarget = pointerBorrowTarget;
                        auto savedPointerBorrowParent = pointerBorrowParent;
                        auto savedActiveBorrowers = activeBorrowers;
                        auto savedActiveMutBorrower = activeMutBorrower;
                        auto savedVariableScopeDepth = variableScopeDepth;
                        auto savedCleanupScopes = cleanupScopes;
                        auto savedPointerBorrowScopes = pointerBorrowScopes;
                        auto savedVariableScopeDepthScopes =
                            variableScopeDepthScopes;

                        callee = generateMethodDefinition(resolvedStructName,
                                                          mit->second.second);

                        namedValues = savedNamedValues;
                        constantVariables = savedConstantVariables;
                        variableTypes = savedVariableTypes;
                        structVariableTypes = savedStructVariableTypes;
                        traitObjectVariableTypes =
                            savedTraitObjectVariableTypes;
                        enumVariableTypes = savedEnumVariableTypes;
                        listElementTypes = savedListElementTypes;
                        mapKeyValueTypes = savedMapKeyValueTypes;
                        tupleElementTypes = savedTupleElementTypes;
                        pointerElementTypes = savedPointerElementTypes;
                        movedVariables = savedMovedVariables;
                        pointerBorrowTarget = savedPointerBorrowTarget;
                        pointerBorrowParent = savedPointerBorrowParent;
                        activeBorrowers = savedActiveBorrowers;
                        activeMutBorrower = savedActiveMutBorrower;
                        variableScopeDepth = savedVariableScopeDepth;
                        cleanupScopes = savedCleanupScopes;
                        pointerBorrowScopes = savedPointerBorrowScopes;
                        variableScopeDepthScopes =
                            savedVariableScopeDepthScopes;

                        if(savedBlock)
                            builder.SetInsertPoint(savedBlock);
                    }
                    if(!callee)
                    {
                        reportError(node->line, "unknown static method: '" +
                                                    node->name + "'");
                        return nullptr;
                    }

                    if(callee->arg_size() != node->arguments.size())
                    {
                        reportError(
                            node->line,
                            "wrong number of arguments for static method '" +
                                node->name + "': expected " +
                                std::to_string(callee->arg_size()) + ", got " +
                                std::to_string(node->arguments.size()));
                        return nullptr;
                    }

                    if(!validateTemporaryBorrowArguments(node->arguments,
                                                         node->name, "",
                                                         nullptr))
                    {
                        return nullptr;
                    }

                    std::vector<llvm::Value*> callArgs;
                    callArgs.reserve(node->arguments.size());
                    size_t idx = 0;
                    for(auto* argNode : node->arguments)
                    {
                        llvm::Value* argVal = generateExpression(argNode);
                        if(!argVal)
                            return nullptr;
                        consumeMoveFromExpression(
                            argNode, node->line,
                            "passing argument to static method '" + node->name +
                                "'");

                        llvm::Type* expectedType =
                            callee->getArg(idx)->getType();
                        if(argVal->getType() != expectedType)
                        {
                            int convCost = 0;
                            if(!canConvertType(argVal->getType(), expectedType,
                                               convCost))
                            {
                                reportError(node->line,
                                            "argument type mismatch for static "
                                            "method '" +
                                                node->name + "'");
                                return nullptr;
                            }

                            if(argVal->getType()->isIntegerTy() &&
                               expectedType->isIntegerTy())
                            {
                                argVal = builder.CreateIntCast(
                                    argVal, expectedType, true, "arg.cast");
                            }
                            else if(argVal->getType()->isIntegerTy() &&
                                    expectedType->isFloatingPointTy())
                            {
                                argVal = builder.CreateSIToFP(
                                    argVal, expectedType, "arg.sitofp");
                            }
                            else if(argVal->getType()->isFloatingPointTy() &&
                                    expectedType->isFloatingPointTy())
                            {
                                argVal = builder.CreateFPCast(
                                    argVal, expectedType, "arg.fpcast");
                            }
                            else if(argVal->getType()->isPointerTy() &&
                                    expectedType->isPointerTy())
                            {
                                argVal = builder.CreateBitCast(
                                    argVal, expectedType, "arg.ptrcast");
                            }
                        }

                        callArgs.push_back(argVal);
                        ++idx;
                    }

                    if(callee->getReturnType()->isVoidTy())
                        return builder.CreateCall(callee, callArgs);
                    return builder.CreateCall(callee, callArgs, "staticcall");
                }
            }
        }

        reportError(node->line, "unknown function: '" + node->name + "'");
        return nullptr;
    }

    std::vector<bool> sharedReferenceParameters(node->arguments.size(), false);
    for(size_t i = 0; i < sharedReferenceParameters.size(); ++i)
    {
        TypeNode* commonType = overloadParameterType(
            overloadIt->second, node->arguments.size(), i);
        if(auto* reference = dynamic_cast<ReferenceTypeNode*>(commonType))
        {
            sharedReferenceParameters[i] = !reference->isMutable;
            continue;
        }

        // When overloads disagree at this position, determine whether the
        // explicit `&value` can only match immutable-reference candidates.
        // Do not treat a matching raw-pointer/value overload as a shared
        // borrow, since that would weaken its call-site alias checks.
        auto* unary = dynamic_cast<UnaryOpNode*>(node->arguments[i]);
        if(!unary || unary->op != UnaryOpNode::OP_ADDR)
            continue;
        TypeNode* operandType = semanticArgumentType(unary->operand);
        bool hasSharedReferenceCandidate = false;
        bool hasMatchingNonSharedCandidate = false;
        for(const auto& candidate : overloadIt->second)
        {
            if(!candidate.node || !candidate.node->parameters ||
               candidate.node->parameters->isVarArg ||
               candidate.node->parameters->parameters.size() !=
                   node->arguments.size() ||
               i >= candidate.node->parameters->parameters.size())
                continue;
            TypeNode* parameterType =
                candidate.node->parameters->parameters[i]->type;
            if(auto* reference =
                   dynamic_cast<ReferenceTypeNode*>(parameterType))
            {
                if(!reference->isMutable && operandType &&
                   typeMangle(reference->elementType) ==
                       typeMangle(operandType))
                    hasSharedReferenceCandidate = true;
            }
            else
            {
                TypeNode* argumentType = semanticArgumentType(
                    node->arguments[i]);
                if(parameterType && argumentType &&
                   typeMangle(parameterType) == typeMangle(argumentType))
                    hasMatchingNonSharedCandidate = true;
            }
        }
        sharedReferenceParameters[i] = hasSharedReferenceCandidate &&
                                       !hasMatchingNonSharedCandidate;
    }
    if(!validateTemporaryBorrowArguments(node->arguments, node->name, "",
                                         &sharedReferenceParameters))
        return nullptr;

    // Validate temporary closure borrows before generating their bodies. This
    // keeps an aliasing diagnostic from being obscured by later LLVM lowering
    // (for example, attempting an unsafe dereference in another argument).
    for(size_t argumentIndex = 0;
        argumentIndex < node->arguments.size(); ++argumentIndex)
    {
        auto* closure =
            dynamic_cast<ClosureNode*>(node->arguments[argumentIndex]);
        if(!closure)
            continue;
        bool isCallableArgument = false;
        for(const auto& candidate : overloadIt->second)
        {
            if(!candidate.node || !candidate.node->parameters ||
               argumentIndex >=
                   candidate.node->parameters->parameters.size())
                continue;
            if(dynamic_cast<FunctionTypeNode*>(
                   candidate.node->parameters->parameters[argumentIndex]
                       ->type))
            {
                isCallableArgument = true;
                break;
            }
        }
        if(!isCallableArgument)
            continue;

        std::vector<std::string> capturedNames;
        const bool hasUnannotatedParameter =
            closure->parameters &&
            std::any_of(closure->parameters->parameters.begin(),
                        closure->parameters->parameters.end(),
                        [](ParameterNode* parameter)
                        { return !parameter || !parameter->type; });
        if(hasUnannotatedParameter)
        {
            std::set<std::string> used;
            if(closure->body)
                for(auto* statement : closure->body->statements)
                    collect_used_idents(statement, used);
            for(auto* parameter : closure->parameters->parameters)
                if(parameter)
                    used.erase(parameter->name);
            for(const auto& name : used)
            {
                auto value = namedValues.find(name);
                if(value == namedValues.end() ||
                   llvm::isa<llvm::GlobalVariable>(value->second) ||
                   closureVariables.count(name) ||
                   !llvm::isa<llvm::AllocaInst>(value->second))
                    continue;
                capturedNames.push_back(name);
            }
        }
        else
        {
            std::vector<std::pair<std::string, TypeNode*>> captures;
            TypeNode* closureReturnType = nullptr;
            if(!getRuntimeClosureCaptures(closure, captures,
                                          closureReturnType))
                return nullptr;
            for(const auto& capture : captures)
                capturedNames.push_back(capture.first);
        }
        for(const auto& captureName : capturedNames)
        {
            auto exclusive = activeMutBorrower.find(captureName);
            auto shared = activeBorrowers.find(captureName);
            if(exclusive != activeMutBorrower.end() ||
               (shared != activeBorrowers.end() &&
                !shared->second.empty()))
            {
                reportError(node->line,
                            "cannot pass inline closure because captured "
                            "variable '" +
                                captureName + "' is already borrowed");
                return nullptr;
            }
            for(size_t otherIndex = 0;
                otherIndex < node->arguments.size(); ++otherIndex)
            {
                if(otherIndex == argumentIndex)
                    continue;
                auto* borrow = dynamic_cast<UnaryOpNode*>(
                    node->arguments[otherIndex]);
                auto* borrowedName =
                    borrow ? dynamic_cast<IdentifierNode*>(borrow->operand)
                           : nullptr;
                if(borrowedName && borrowedName->name == captureName &&
                   (borrow->op == UnaryOpNode::OP_ADDR ||
                    borrow->op == UnaryOpNode::OP_ADDR_MUT))
                {
                    reportError(
                        node->line,
                        "cannot pass inline closure and a reference to "
                        "captured variable '" +
                            captureName + "' in the same call");
                    return nullptr;
                }
            }
        }
    }

    const bool isFreeLikeCall =
        isFreeLikeFunctionName(node->name) && node->arguments.size() == 1;
    if(isFreeLikeCall &&
       !precheckFreeLikeArgument(node->arguments[0], node->name))
    {
        return nullptr;
    }

    std::vector<llvm::Value*> argVals;
    argVals.reserve(node->arguments.size());
    std::vector<TypeNode*> semanticArgumentTypes;
    semanticArgumentTypes.reserve(node->arguments.size());
    for(size_t i = 0; i < node->arguments.size(); ++i)
        semanticArgumentTypes.push_back(
            preInstantiationArrayArgumentTypes[i]
                ? preInstantiationArrayArgumentTypes[i]
                : semanticArgumentType(node->arguments[i]));
    for(size_t argIndex = 0; argIndex < node->arguments.size(); ++argIndex)
    {
        auto arg = node->arguments[argIndex];
        llvm::Value* argVal = nullptr;
        if(auto* identifier = dynamic_cast<IdentifierNode*>(arg))
        {
            auto functionType = functionTypeVariables.find(identifier->name);
            auto value = namedValues.find(identifier->name);
            auto* slot = value == namedValues.end()
                             ? nullptr
                             : llvm::dyn_cast<llvm::AllocaInst>(value->second);
            if(functionType != functionTypeVariables.end() && slot)
                argVal = builder.CreateLoad(slot->getAllocatedType(), slot,
                                            identifier->name + ".closure.arg");
        }
        if(!argVal)
        {
            TypeNode* expectedArgumentType = overloadParameterType(
                overloadIt->second, node->arguments.size(), argIndex);
            if(!expectedArgumentType)
            {
                if(auto* closure = dynamic_cast<ClosureNode*>(arg))
                {
                    TypeNode* selectedType = nullptr;
                    bool conflictingTypes = false;
                    for(const auto& candidate : overloadIt->second)
                    {
                        if(!candidate.function || !candidate.node ||
                           !candidate.node->typeParams.empty() ||
                           !candidate.node->parameters ||
                           candidate.node->parameters->parameters.size() !=
                               node->arguments.size() ||
                           argIndex >=
                               candidate.node->parameters->parameters.size())
                            continue;
                        auto* callbackType = dynamic_cast<FunctionTypeNode*>(
                            candidate.node->parameters
                                ->parameters[argIndex]->type);
                        const size_t expectedCount =
                            callbackType && callbackType->parameterTypes
                                ? callbackType->parameterTypes->types.size()
                                : 0;
                        const size_t actualCount =
                            closure->parameters
                                ? closure->parameters->parameters.size()
                                : 0;
                        if(!callbackType || expectedCount != actualCount)
                            continue;

                        bool signatureMatches = true;
                        for(size_t parameterIndex = 0;
                            closure->parameters &&
                            parameterIndex <
                                closure->parameters->parameters.size();
                            ++parameterIndex)
                        {
                            auto* actualParameter =
                                closure->parameters
                                    ->parameters[parameterIndex];
                            TypeNode* declared = actualParameter
                                                     ? actualParameter->type
                                                     : nullptr;
                            TypeNode* expected = callbackType->parameterTypes
                                                     ->types[parameterIndex];
                            if(!expected ||
                               (declared && typeMangle(declared) !=
                                                typeMangle(expected)))
                            {
                                signatureMatches = false;
                                break;
                            }
                        }
                        if(!signatureMatches)
                            continue;

                        bool otherArgumentsMatch = true;
                        for(size_t i = 0; i < node->arguments.size(); ++i)
                        {
                            if(i == argIndex)
                                continue;
                            TypeNode* actual = inferExpressionTypeNode(
                                node->arguments[i], node->line);
                            TypeNode* expected = candidate.node->parameters
                                                     ->parameters[i]->type;
                            if(!actual || !expected ||
                               typeMangle(actual) != typeMangle(expected))
                            {
                                otherArgumentsMatch = false;
                                break;
                            }
                        }
                        if(!otherArgumentsMatch)
                            continue;
                        if(selectedType &&
                           typeMangle(selectedType) !=
                               typeMangle(callbackType))
                        {
                            conflictingTypes = true;
                            break;
                        }
                        selectedType = callbackType;
                    }
                    if(!conflictingTypes)
                        expectedArgumentType = selectedType;
                }
            }
            // Generic specializations coexist with their unspecialized
            // templates in the overload set. The template's fn(T)->T
            // parameter makes the common-type helper return null; recover a
            // concrete callback signature from the unique specialization
            // whose other arguments match and which accepts this function
            // item.
            if(!expectedArgumentType &&
               dynamic_cast<IdentifierNode*>(arg))
            {
                auto* functionItem = static_cast<IdentifierNode*>(arg);
                auto items = functionOverloads.find(functionItem->name);
                TypeNode* selectedType = nullptr;
                bool conflictingTypes = false;
                if(items != functionOverloads.end())
                {
                    for(const auto& candidate : overloadIt->second)
                    {
                        if(!candidate.function || !candidate.node ||
                           !candidate.node->parameters ||
                           candidate.node->parameters->parameters.size() !=
                               node->arguments.size() ||
                           argIndex >=
                               candidate.node->parameters->parameters.size())
                            continue;
                        auto* callbackType = dynamic_cast<FunctionTypeNode*>(
                            candidate.node->parameters
                                ->parameters[argIndex]->type);
                        if(!callbackType)
                            continue;

                        bool otherArgumentsMatch = true;
                        for(size_t i = 0; i < node->arguments.size(); ++i)
                        {
                            if(i == argIndex)
                                continue;
                            TypeNode* actual = inferExpressionTypeNode(
                                node->arguments[i], node->line);
                            TypeNode* expected = candidate.node->parameters
                                                     ->parameters[i]->type;
                            if(!actual || !expected ||
                               typeMangle(actual) != typeMangle(expected))
                            {
                                otherArgumentsMatch = false;
                                break;
                            }
                        }
                        if(!otherArgumentsMatch)
                            continue;

                        size_t matchingItems = 0;
                        for(const auto& item : items->second)
                        {
                            if(!isOverloadVisible(item) || !item.node ||
                               !item.function || !item.node->parameters ||
                               item.node->parameters->isVarArg ||
                               !item.node->typeParams.empty() ||
                               item.node->parameters->parameters.size() !=
                                   (callbackType->parameterTypes
                                        ? callbackType->parameterTypes->types.size()
                                        : 0))
                                continue;
                            auto* itemParameters = new TypeListNode();
                            for(auto* parameter :
                                item.node->parameters->parameters)
                                itemParameters->addType(
                                    cloneTypeNode(parameter->type));
                            auto* itemType = new FunctionTypeNode(
                                itemParameters,
                                cloneTypeNode(item.node->returnType));
                            if(typeMangle(itemType) ==
                               typeMangle(callbackType))
                                ++matchingItems;
                        }
                        if(matchingItems != 1)
                            continue;
                        if(selectedType &&
                           typeMangle(selectedType) !=
                               typeMangle(callbackType))
                        {
                            conflictingTypes = true;
                            break;
                        }
                        selectedType = callbackType;
                    }
                }
                if(!conflictingTypes)
                    expectedArgumentType = selectedType;
            }
            if(auto* closure = dynamic_cast<ClosureNode*>(arg))
            {
                const bool hasUnannotatedParameter =
                    closure->parameters &&
                    std::any_of(closure->parameters->parameters.begin(),
                                closure->parameters->parameters.end(),
                                [](const auto* parameter) {
                                    return !parameter || !parameter->type;
                                });
                bool expectedCallbackIsUnresolved = false;
                if(hasUnannotatedParameter && expectedArgumentType)
                {
                    auto* callbackType =
                        dynamic_cast<FunctionTypeNode*>(expectedArgumentType);
                    for(const auto& candidate : overloadIt->second)
                    {
                        if(!candidate.node ||
                           candidate.node->typeParams.empty() ||
                           !candidate.node->parameters ||
                           argIndex >= candidate.node->parameters
                                           ->parameters.size())
                            continue;
                        auto* templateCallback = dynamic_cast<FunctionTypeNode*>(
                            candidate.node->parameters
                                ->parameters[argIndex]->type);
                        if(!templateCallback || !callbackType ||
                           typeMangle(templateCallback) !=
                               typeMangle(callbackType))
                            continue;
                        const auto& typeParams = candidate.node->typeParams;
                        auto isTemplateType = [&](TypeNode* type) {
                            auto* named = dynamic_cast<StructTypeRefNode*>(type);
                            return named &&
                                   std::find(typeParams.begin(),
                                             typeParams.end(),
                                             named->structName) !=
                                       typeParams.end();
                        };
                        if((templateCallback->parameterTypes &&
                            std::any_of(
                                templateCallback->parameterTypes->types.begin(),
                                templateCallback->parameterTypes->types.end(),
                                isTemplateType)) ||
                           isTemplateType(templateCallback->returnType))
                        {
                            expectedCallbackIsUnresolved = true;
                            break;
                        }
                    }
                }
                if(hasUnannotatedParameter &&
                   (!expectedArgumentType || expectedCallbackIsUnresolved))
                {
                    reportError(
                        node->line,
                        "cannot infer inline closure parameter types; add "
                        "annotations or provide arguments that determine "
                        "the callback input type");
                    return nullptr;
                }
            }
            // Immutable reference parameters use the value ABI, like runtime
            // closures. Accept an explicit `&value` at the call site, but
            // pass the referenced value rather than the address expression.
            if(auto* reference =
                   dynamic_cast<ReferenceTypeNode*>(expectedArgumentType))
            {
                auto* unary = dynamic_cast<UnaryOpNode*>(arg);
                if(!reference->isMutable && unary &&
                   unary->op == UnaryOpNode::OP_ADDR)
                    argVal = generateArgumentValue(unary->operand,
                                                   reference->elementType);
                else
                    argVal = generateArgumentValue(arg, expectedArgumentType);
            }
            else
                argVal = generateArgumentValue(arg, expectedArgumentType);
        }
        if(!argVal)
            return nullptr;
        // &s where s is a string: generateExpression(OP_ADDR) returns the
        // alloca (char**). Load through to get the actual char* so the callee
        // receives the string pointer, not the stack address. `&mut s` keeps
        // the address: a `&mut` parameter writes back through it.
        if(auto* unary = dynamic_cast<UnaryOpNode*>(arg))
        {
            if(unary->op == UnaryOpNode::OP_ADDR)
            {
                if(auto* id = dynamic_cast<IdentifierNode*>(unary->operand))
                {
                    auto tit = variableTypes.find(id->name);
                    if(tit != variableTypes.end() &&
                       (tit->second == TypeNode::TYPE_STRING ||
                        tit->second == TypeNode::TYPE_STR8 ||
                        tit->second == TypeNode::TYPE_STR16))
                    {
                        argVal = builder.CreateLoad(argVal->getType(), argVal,
                                                    id->name + ".str_deref");
                    }
                }
            }
        }
        argVals.push_back(argVal);
    }

    FunctionOverloadInfo* best = nullptr;
    int bestCost = std::numeric_limits<int>::max();
    bool ambiguous = false;
    std::string privateModule;

    for(auto& info : overloadIt->second)
    {
        if(usingTailQualifiedLookup && !qualifiedModuleFilter.empty() &&
           !Helpers::is_same_module_family(qualifiedModuleFilter,
                                           info.sourceModule))
        {
            continue;
        }
        if(!isOverloadVisible(info))
        {
            if(privateModule.empty() && !info.sourceModule.empty())
                privateModule = info.sourceModule;
            continue;
        }

        llvm::Function* callee = info.function;
        if(!callee)
            continue;
        bool boundClosuresMatch = true;
        if(info.node)
        {
            for(const auto& boundClosure : info.node->boundClosureParameters)
            {
                size_t parameterIndex = 0;
                while(parameterIndex <
                          info.node->parameters->parameters.size() &&
                      info.node->parameters->parameters[parameterIndex]->name !=
                          boundClosure.first)
                    ++parameterIndex;
                if(parameterIndex >= node->arguments.size())
                {
                    boundClosuresMatch = false;
                    break;
                }
                ClosureNode* actualClosure =
                    dynamic_cast<ClosureNode*>(node->arguments[parameterIndex]);
                if(auto* identifier = dynamic_cast<IdentifierNode*>(
                       node->arguments[parameterIndex]))
                {
                    auto closureIt = closureVariables.find(identifier->name);
                    if(closureIt != closureVariables.end())
                        actualClosure = closureIt->second;
                }
                if(actualClosure != boundClosure.second)
                {
                    boundClosuresMatch = false;
                    break;
                }
            }
        }
        if(!boundClosuresMatch)
            continue;
        size_t expectedArgs = callee->arg_size();
        size_t actualArgs = argVals.size();
        size_t hiddenCaptureCount = 0;
        if(info.node)
            for(const auto& closureCaptures :
                info.node->boundClosureCaptureAliases)
                hiddenCaptureCount += closureCaptures.second.size();
        if(hiddenCaptureCount > expectedArgs)
            continue;
        const size_t sourceParameterCount = expectedArgs - hiddenCaptureCount;
        bool isVarArg = callee->isVarArg();

        if(!isVarArg && sourceParameterCount != actualArgs)
            continue;
        if(isVarArg && actualArgs < sourceParameterCount)
            continue;

        int totalCost = 0;
        bool ok = true;
        for(size_t i = 0; i < sourceParameterCount; ++i)
        {
            bool isBoundClosureArgument = false;
            if(info.node && info.node->parameters)
            {
                for(const auto& bound : info.node->boundClosureParameters)
                {
                    if(i >= info.node->parameters->parameters.size() ||
                       info.node->parameters->parameters[i]->name !=
                           bound.first)
                        continue;
                    ClosureNode* actual =
                        dynamic_cast<ClosureNode*>(node->arguments[i]);
                    if(auto* identifier =
                           dynamic_cast<IdentifierNode*>(node->arguments[i]))
                    {
                        auto closure = closureVariables.find(identifier->name);
                        if(closure != closureVariables.end())
                            actual = closure->second;
                    }
                    isBoundClosureArgument = actual == bound.second;
                    break;
                }
            }
            // This generic specialization has the closure body and captures
            // bound into its generated function. The source closure is not a
            // runtime argument, so it must not participate in ABI matching.
            if(isBoundClosureArgument)
                continue;

            bool explicitSharedReferenceArgument = false;
            if(info.node && info.node->parameters &&
               i < info.node->parameters->parameters.size())
            {
                TypeNode* expectedSemantic =
                    info.node->parameters->parameters[i]->type;
                if(auto* refType =
                       dynamic_cast<ReferenceTypeNode*>(expectedSemantic))
                {
                    expectedSemantic = refType->elementType;
                    auto* unary =
                        dynamic_cast<UnaryOpNode*>(node->arguments[i]);
                    explicitSharedReferenceArgument =
                        !refType->isMutable && unary &&
                        unary->op == UnaryOpNode::OP_ADDR;
                    // `&mut x` must name a value of the parameter's type;
                    // without `&mut` the call-site check reports it.
                    if(refType->isMutable)
                    {
                        auto* unary =
                            dynamic_cast<UnaryOpNode*>(node->arguments[i]);
                        if(!(unary && unary->op == UnaryOpNode::OP_ADDR_MUT))
                            continue;
                        {
                            TypeNode* operandType =
                                semanticArgumentType(unary->operand);
                            if(operandType &&
                               typeMangle(operandType) !=
                                   typeMangle(expectedSemantic) &&
                               !isListToFixedArrayCompatible(expectedSemantic,
                                                             operandType))
                            {
                                ok = false;
                                break;
                            }
                            int cost = 0;
                            if(!canConvertType(argVals[i]->getType(),
                                               callee->getArg(i)->getType(),
                                               cost))
                            {
                                ok = false;
                                break;
                            }
                            continue;
                        }
                    }
                }

                TypeNode* actualSemantic = semanticArgumentTypes[i];
                if(auto* reference =
                       dynamic_cast<ReferenceTypeNode*>(
                           info.node->parameters->parameters[i]->type))
                {
                    auto* unary =
                        dynamic_cast<UnaryOpNode*>(node->arguments[i]);
                    if(!reference->isMutable && unary &&
                       unary->op == UnaryOpNode::OP_ADDR)
                        actualSemantic = semanticArgumentType(unary->operand);
                }
                if(auto* expectedFunction =
                       dynamic_cast<FunctionTypeNode*>(expectedSemantic))
                {
                    FunctionTypeNode* actualFunction =
                        dynamic_cast<FunctionTypeNode*>(actualSemantic);
                    if(auto* closure = dynamic_cast<ClosureNode*>(
                           node->arguments[i]))
                    {
                        const size_t expectedCount =
                            expectedFunction->parameterTypes
                                ? expectedFunction->parameterTypes->types.size()
                                : 0;
                        const size_t actualCount =
                            closure->parameters
                                ? closure->parameters->parameters.size()
                                : 0;
                        bool parametersMatch = expectedCount == actualCount;
                        for(size_t parameterIndex = 0;
                            parametersMatch && parameterIndex < expectedCount;
                            ++parameterIndex)
                        {
                            auto* declaredParameter =
                                closure->parameters
                                    ->parameters[parameterIndex];
                            TypeNode* actualParameter =
                                declaredParameter ? declaredParameter->type
                                                  : nullptr;
                            TypeNode* expectedParameter =
                                expectedFunction->parameterTypes
                                    ->types[parameterIndex];
                            parametersMatch = expectedParameter &&
                                              (!actualParameter ||
                                               typeMangle(actualParameter) ==
                                                   typeMangle(
                                                       expectedParameter));
                        }
                        if(parametersMatch)
                            actualFunction = expectedFunction;
                    }
                    if(auto* id = dynamic_cast<IdentifierNode*>(
                           node->arguments[i]))
                    {
                        auto actual = functionTypeVariables.find(id->name);
                        if(actual != functionTypeVariables.end())
                            actualFunction = actual->second;
                        else
                        {
                            auto items = functionOverloads.find(id->name);
                            if(items != functionOverloads.end())
                            {
                                size_t matchingItems = 0;
                                for(const auto& item : items->second)
                                {
                                    if(!isOverloadVisible(item) || !item.node ||
                                       !item.function ||
                                       !item.node->parameters ||
                                       item.node->parameters->isVarArg ||
                                       !item.node->typeParams.empty() ||
                                       item.node->parameters->parameters.size() !=
                                           (expectedFunction->parameterTypes
                                                ? expectedFunction->parameterTypes
                                                      ->types.size()
                                                : 0))
                                        continue;
                                    auto* itemParams = new TypeListNode();
                                    for(auto* parameter :
                                        item.node->parameters->parameters)
                                        itemParams->addType(
                                            cloneTypeNode(parameter->type));
                                    auto* itemType = new FunctionTypeNode(
                                        itemParams,
                                        cloneTypeNode(item.node->returnType));
                                    if(typeMangle(itemType) ==
                                       typeMangle(expectedFunction))
                                        ++matchingItems;
                                }
                                if(matchingItems == 1)
                                    actualFunction = expectedFunction;
                            }
                        }
                    }
                    if(!actualFunction ||
                       typeMangle(expectedFunction) !=
                           typeMangle(actualFunction))
                    {
                        ok = false;
                        break;
                    }
                    continue;
                }
                if(auto* traitObj =
                       dynamic_cast<TraitObjectTypeNode*>(expectedSemantic))
                {
                    if(auto* actualTraitObj =
                           dynamic_cast<TraitObjectTypeNode*>(actualSemantic))
                    {
                        if(Helpers::trait_names_equivalent(
                               actualTraitObj->traitName, traitObj->traitName))
                            continue;
                    }
                    if(!concreteTypeImplementsTrait(actualSemantic,
                                                    traitObj->traitName))
                    {
                        ok = false;
                        break;
                    }
                    continue;
                }
                const bool fixedArrayAsRange = isListToFixedArrayCompatible(
                    expectedSemantic, actualSemantic);
                if((isCompositeSemanticType(expectedSemantic) ||
                    isCompositeSemanticType(actualSemantic)) &&
                   actualSemantic &&
                   typeMangle(actualSemantic) != typeMangle(expectedSemantic) &&
                   !fixedArrayAsRange)
                {
                    ok = false;
                    break;
                }
            }

            int cost = 0;
            auto* declaredReference =
                info.node && info.node->parameters &&
                        i < info.node->parameters->parameters.size()
                    ? dynamic_cast<ReferenceTypeNode*>(
                          info.node->parameters->parameters[i]->type)
                    : nullptr;
            if(explicitSharedReferenceArgument && declaredReference &&
               argVals[i]->getType()->isPointerTy() &&
               getLLVMTypeFromNode(declaredReference->elementType) ==
                   callee->getArg(i)->getType())
            {
                // The explicit address is used for candidate matching and
                // borrow validation; immutable references use the value ABI.
                cost = 0;
            }
            else if(!canConvertType(argVals[i]->getType(),
                                    callee->getArg(i)->getType(), cost))
            {
                ok = false;
                break;
            }
            totalCost += cost;
        }
        if(!ok)
            continue;

        if(totalCost < bestCost)
        {
            bestCost = totalCost;
            best = &info;
            ambiguous = false;
        }
        else if(totalCost == bestCost)
        {
            ambiguous = true;
        }
    }

    if(!best)
    {
        if(!privateModule.empty())
        {
            reportError(node->line, "function '" + node->name +
                                        "' is private in module '" +
                                        privateModule + "'");
        }
        else
        {
            reportError(node->line, "no matching overload for function '" +
                                        node->name + "'");
        }
        return nullptr;
    }
    if(ambiguous)
    {
        reportError(node->line,
                    "ambiguous call to function '" + node->name + "'");
        return nullptr;
    }

    // Pointer parameters use the raw `ptr<T>` ABI, so their caller-side
    // mutability is otherwise erased. Inspect the selected body for writes
    // through a pointer parameter and reject a known shared borrow at this
    // boundary. Unknown raw pointers remain governed by unsafe code.
    std::function<bool(FunctionDefNode*, size_t, const void*,
                       std::set<std::pair<const void*, size_t>>&)>
        functionMutatesPointerParameter;
    functionMutatesPointerParameter = [&](FunctionDefNode* function,
                                          size_t parameterIndex,
                                          const void* callableIdentity,
                                          std::set<std::pair<const void*,
                                                             size_t>>& visiting) {
        if(!function || !function->parameters ||
           parameterIndex >= function->parameters->parameters.size())
            return false;
        ParameterNode* parameter =
            function->parameters->parameters[parameterIndex];
        if(!parameter ||
           !dynamic_cast<PointerTypeNode*>(parameter->type))
            return false;
        if(!function->body)
            return true;
        const auto functionAndParameter = std::make_pair(
            callableIdentity ? callableIdentity : static_cast<const void*>(function),
                                                         parameterIndex);
        if(!visiting.insert(functionAndParameter).second)
            return false;
        const std::string parameterName = parameter->name;
        std::set<std::string> pointerAliases{parameterName};
        auto usesPointerAlias = [&](ASTNode* expression) {
            std::set<std::string> identifiers;
            collect_used_idents(expression, identifiers);
            return std::any_of(identifiers.begin(), identifiers.end(),
                               [&](const std::string& identifier) {
                                   return pointerAliases.count(identifier) != 0;
                               });
        };
        std::function<void(StatementListNode*)> collectAliasesFromList;
        std::function<void(StatementNode*)> collectAliasesFromStatement;
        collectAliasesFromList = [&](StatementListNode* statements) {
            if(!statements)
                return;
            for(auto* statement : statements->statements)
                collectAliasesFromStatement(statement);
        };
        collectAliasesFromStatement = [&](StatementNode* statement) {
            if(!statement)
                return;
            if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                if(usesPointerAlias(declaration->expression))
                    pointerAliases.insert(declaration->name);
                return;
            }
            if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            {
                if(usesPointerAlias(declaration->initExpr))
                    pointerAliases.insert(declaration->name);
                return;
            }
            if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
            {
                if(usesPointerAlias(assignment->expression))
                    pointerAliases.insert(assignment->name);
                return;
            }
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                collectAliasesFromList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                collectAliasesFromList(branch->thenBranch);
                collectAliasesFromStatement(branch->elseIfBranch);
                collectAliasesFromList(branch->elseBranch);
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                collectAliasesFromList(branch->thenBranch);
                collectAliasesFromStatement(branch->elseIfBranch);
                collectAliasesFromList(branch->elseBranch);
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
                collectAliasesFromList(loop->body);
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
                collectAliasesFromList(loop->body);
            else if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
            {
                collectAliasesFromStatement(attempt->tryBlock);
                collectAliasesFromStatement(attempt->catchBlock);
            }
        };
        bool addedPointerAlias;
        do
        {
            const size_t previousCount = pointerAliases.size();
            collectAliasesFromList(function->body);
            addedPointerAlias = pointerAliases.size() != previousCount;
        } while(addedPointerAlias);

        auto structNameFromType = [&](TypeNode* type) {
            if(auto* reference = dynamic_cast<ReferenceTypeNode*>(type))
                type = reference->elementType;
            if(auto* structure = dynamic_cast<StructTypeRefNode*>(type))
                return structure->structName;
            if(auto* generic =
                   dynamic_cast<GenericStructTypeRefNode*>(type))
                return generic->getMangledName();
            return std::string();
        };
        auto structNameFromExpression = [&](ExpressionNode* expression) {
            if(auto* structure = dynamic_cast<StructLiteralNode*>(expression))
                return structure->structName;
            return std::string();
        };
        auto receiverStructName = [&](ExpressionNode* receiver) {
            if(auto* id = dynamic_cast<IdentifierNode*>(receiver))
            {
                if(function->parameters)
                    for(auto* parameter : function->parameters->parameters)
                        if(parameter && parameter->name == id->name)
                        {
                            std::string name = structNameFromType(parameter->type);
                            if(!name.empty())
                                return name;
                        }
                std::string resolvedName;
                std::function<void(StatementListNode*)> findLocal;
                std::function<void(StatementNode*)> findInStatement;
                findLocal = [&](StatementListNode* statements) {
                    if(!statements)
                        return;
                    for(auto* statement : statements->statements)
                        findInStatement(statement);
                };
                findInStatement = [&](StatementNode* statement) {
                    if(!statement)
                        return;
                    if(auto* declaration =
                           dynamic_cast<LetDeclNode*>(statement))
                    {
                        if(declaration->name == id->name)
                        {
                            resolvedName = structNameFromType(declaration->type);
                            if(resolvedName.empty())
                                resolvedName = structNameFromExpression(
                                    declaration->expression);
                        }
                    }
                    else if(auto* declaration =
                                dynamic_cast<VarDeclNode*>(statement))
                    {
                        if(declaration->name == id->name)
                        {
                            resolvedName = structNameFromType(declaration->type);
                            if(resolvedName.empty())
                                resolvedName = structNameFromExpression(
                                    declaration->initExpr);
                        }
                    }
                    else if(auto* block =
                                dynamic_cast<BlockStatementNode*>(statement))
                        findLocal(block->statements);
                    else if(auto* branch = dynamic_cast<IfNode*>(statement))
                    {
                        findLocal(branch->thenBranch);
                        findInStatement(branch->elseIfBranch);
                        findLocal(branch->elseBranch);
                    }
                    else if(auto* branch =
                                dynamic_cast<CexprIfNode*>(statement))
                    {
                        findLocal(branch->thenBranch);
                        findInStatement(branch->elseIfBranch);
                        findLocal(branch->elseBranch);
                    }
                    else if(auto* loop = dynamic_cast<ForNode*>(statement))
                        findLocal(loop->body);
                    else if(auto* loop = dynamic_cast<WhileNode*>(statement))
                        findLocal(loop->body);
                };
                findLocal(function->body);
                return resolvedName;
            }
            return structNameFromExpression(receiver);
        };

        auto suppliedCallableEffect = [&](const std::string& callableName,
                                           const std::vector<ExpressionNode*>&
                                               arguments) {
            if(function != best->node || !function->parameters)
                return std::pair<bool, bool>{false, false};
            for(size_t callableIndex = 0;
                callableIndex < function->parameters->parameters.size();
                ++callableIndex)
            {
                auto* parameter =
                    function->parameters->parameters[callableIndex];
                if(!parameter || parameter->name != callableName ||
                   callableIndex >= node->arguments.size())
                    continue;
                ClosureNode* closure =
                    dynamic_cast<ClosureNode*>(node->arguments[callableIndex]);
                if(!closure)
                    if(auto* identifier = dynamic_cast<IdentifierNode*>(
                           node->arguments[callableIndex]))
                    {
                        auto knownTarget =
                            functionItemVariables.find(identifier->name);
                        if(knownTarget != functionItemVariables.end() &&
                           !knownTarget->second.empty())
                        {
                            bool foundTarget = false;
                            bool mayMutate = false;
                            for(const auto& targetName : knownTarget->second)
                            {
                                auto overloads =
                                    functionOverloads.find(targetName);
                                if(overloads == functionOverloads.end())
                                    return std::pair<bool, bool>{false,
                                                                 false};
                                for(const auto& overload : overloads->second)
                                {
                                    if(!isOverloadVisible(overload) ||
                                       !overload.node ||
                                       !overload.node->parameters)
                                        continue;
                                    foundTarget = true;
                                    for(size_t argumentIndex = 0;
                                        argumentIndex < arguments.size() &&
                                        argumentIndex <
                                            overload.node->parameters
                                                ->parameters.size();
                                        ++argumentIndex)
                                    {
                                        if(!usesPointerAlias(
                                               arguments[argumentIndex]) ||
                                           !dynamic_cast<PointerTypeNode*>(
                                               overload.node->parameters
                                                   ->parameters[argumentIndex]
                                                   ->type))
                                            continue;
                                        if(functionMutatesPointerParameter(
                                               overload.node, argumentIndex,
                                               overload.node, visiting))
                                            mayMutate = true;
                                    }
                                }
                            }
                            if(foundTarget)
                                return std::pair<bool, bool>{true, mayMutate};
                        }
                        if(knownTarget != functionItemVariables.end())
                            return std::pair<bool, bool>{false, false};
                    }
                if(!closure)
                    if(auto* identifier = dynamic_cast<IdentifierNode*>(
                           node->arguments[callableIndex]))
                    {
                        auto found = closureVariables.find(identifier->name);
                        if(found != closureVariables.end())
                            closure = found->second;
                    }
                if(!closure || !closure->parameters)
                    return std::pair<bool, bool>{false, false};

                ParameterListNode closureParameters;
                closureParameters.isVarArg =
                    closure->parameters->isVarArg;
                std::vector<size_t> pointerArgumentIndexes;
                for(size_t argumentIndex = 0;
                    argumentIndex < arguments.size() &&
                    argumentIndex < closure->parameters->parameters.size();
                    ++argumentIndex)
                {
                    auto* closureParameter =
                        closure->parameters->parameters[argumentIndex];
                    if(!closureParameter)
                        continue;
                    TypeNode* closureParameterType = closureParameter->type;
                    if(usesPointerAlias(arguments[argumentIndex]))
                    {
                        closureParameterType = new PointerTypeNode(
                            new TypeNode(TypeNode::TYPE_VOID));
                        pointerArgumentIndexes.push_back(argumentIndex);
                    }
                    closureParameters.parameters.push_back(
                        new ParameterNode(closureParameterType,
                                          closureParameter->name));
                }
                if(pointerArgumentIndexes.empty())
                    return std::pair<bool, bool>{true, false};

                FunctionDefNode closureAdapter(
                    new TypeNode(TypeNode::TYPE_VOID),
                    "<borrow-check closure>", &closureParameters,
                    closure->body);
                for(size_t argumentIndex : pointerArgumentIndexes)
                {
                    if(functionMutatesPointerParameter(
                           &closureAdapter, argumentIndex, closure, visiting))
                        return std::pair<bool, bool>{true, true};
                }
                return std::pair<bool, bool>{true, false};
            }
            return std::pair<bool, bool>{false, false};
        };

        std::function<bool(StatementListNode*)> scansStatements;
        std::function<bool(StatementNode*)> scansStatement;
        std::function<bool(ExpressionNode*)> expressionCallsMutatingPointer;
        expressionCallsMutatingPointer = [&](ExpressionNode* expression) {
            if(!expression)
                return false;
            auto scansChildren = [&](const std::vector<ExpressionNode*>& children) {
                return std::any_of(children.begin(), children.end(),
                                   expressionCallsMutatingPointer);
            };
            if(auto* call = dynamic_cast<FunctionCallNode*>(expression))
            {
                const auto suppliedEffect =
                    suppliedCallableEffect(call->name, call->arguments);
                if(suppliedEffect.first)
                    return suppliedEffect.second ||
                           scansChildren(call->arguments);
                auto candidates = functionOverloads.find(call->name);
                if(candidates == functionOverloads.end() &&
                   function->parameters)
                {
                    for(auto* callableParameter :
                        function->parameters->parameters)
                    {
                        if(!callableParameter ||
                           callableParameter->name != call->name)
                            continue;
                        auto* callableType = dynamic_cast<FunctionTypeNode*>(
                            callableParameter->type);
                        if(callableType && callableType->parameterTypes)
                            for(size_t argumentIndex = 0;
                                argumentIndex < call->arguments.size() &&
                                argumentIndex < callableType->parameterTypes
                                                   ->types.size();
                                ++argumentIndex)
                                if(dynamic_cast<PointerTypeNode*>(
                                       callableType->parameterTypes
                                           ->types[argumentIndex]) &&
                                   usesPointerAlias(
                                       call->arguments[argumentIndex]))
                                    return true;
                        break;
                    }
                }
                if(candidates != functionOverloads.end())
                    for(const auto& candidate : candidates->second)
                    {
                        if(!candidate.node || !candidate.node->parameters)
                            continue;
                        const size_t argumentCount = std::min(
                            call->arguments.size(),
                            candidate.node->parameters->parameters.size());
                        for(size_t argumentIndex = 0;
                            argumentIndex < argumentCount; ++argumentIndex)
                            if(usesPointerAlias(
                                   call->arguments[argumentIndex]) &&
                               functionMutatesPointerParameter(
                                   candidate.node, argumentIndex,
                                   candidate.node, visiting))
                                return true;
                    }
                return scansChildren(call->arguments);
            }
            if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
                return expressionCallsMutatingPointer(binary->left) ||
                       expressionCallsMutatingPointer(binary->right);
            if(auto* unary = dynamic_cast<UnaryOpNode*>(expression))
                return expressionCallsMutatingPointer(unary->operand);
            if(auto* update =
                   dynamic_cast<UpdateExpressionNode*>(expression))
                return expressionCallsMutatingPointer(update->operand);
            if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
                return expressionCallsMutatingPointer(ternary->condition) ||
                       expressionCallsMutatingPointer(ternary->trueExpr) ||
                       expressionCallsMutatingPointer(ternary->falseExpr);
            if(auto* match = dynamic_cast<MatchExpressionNode*>(expression))
            {
                if(expressionCallsMutatingPointer(match->target))
                    return true;
                for(auto* arm : match->arms)
                    if(arm && expressionCallsMutatingPointer(arm->expression))
                        return true;
                return false;
            }
            if(auto* invoke = dynamic_cast<CallableInvokeNode*>(expression))
            {
                if(auto* callableName =
                       dynamic_cast<IdentifierNode*>(invoke->callable))
                {
                    const auto suppliedEffect = suppliedCallableEffect(
                        callableName->name, invoke->arguments);
                    if(suppliedEffect.first)
                        return suppliedEffect.second ||
                               scansChildren(invoke->arguments);
                }
                FunctionTypeNode* callableType = nullptr;
                if(auto* callableName =
                       dynamic_cast<IdentifierNode*>(invoke->callable))
                {
                    if(function->parameters)
                        for(auto* parameter :
                            function->parameters->parameters)
                            if(parameter &&
                               parameter->name == callableName->name)
                            {
                                callableType = dynamic_cast<FunctionTypeNode*>(
                                    parameter->type);
                                break;
                            }
                }
                if(callableType && callableType->parameterTypes)
                    for(size_t argumentIndex = 0;
                        argumentIndex < invoke->arguments.size() &&
                        argumentIndex <
                            callableType->parameterTypes->types.size();
                        ++argumentIndex)
                        if(dynamic_cast<PointerTypeNode*>(
                               callableType->parameterTypes
                                   ->types[argumentIndex]) &&
                           usesPointerAlias(invoke->arguments[argumentIndex]))
                            return true;
                return expressionCallsMutatingPointer(invoke->callable) ||
                       scansChildren(invoke->arguments);
            }
            if(auto* method = dynamic_cast<MethodCallNode*>(expression))
            {
                const std::string receiverType =
                    receiverStructName(method->object);
                for(const auto& byStruct : structMethods)
                {
                    if(!receiverType.empty() &&
                       byStruct.first != receiverType)
                        continue;
                    auto methodIt = byStruct.second.find(method->methodName);
                    if(methodIt == byStruct.second.end() ||
                       !methodIt->second.second ||
                       !methodIt->second.second->parameters)
                        continue;
                    StructMethodNode* methodNode = methodIt->second.second;
                    std::vector<std::pair<ParameterNode*, size_t>>
                        explicitParameters;
                    for(size_t formalIndex = 0;
                        formalIndex <
                        methodNode->parameters->parameters.size();
                        ++formalIndex)
                    {
                        auto* parameter =
                            methodNode->parameters->parameters[formalIndex];
                        if(parameter && parameter->name != "self")
                            explicitParameters.emplace_back(parameter,
                                                            formalIndex);
                    }
                    if(explicitParameters.size() != method->arguments.size())
                        continue;
                    for(size_t argumentIndex = 0;
                        argumentIndex < method->arguments.size();
                        ++argumentIndex)
                    {
                        ParameterNode* parameter =
                            explicitParameters[argumentIndex].first;
                        if(!parameter ||
                           !dynamic_cast<PointerTypeNode*>(parameter->type) ||
                           !usesPointerAlias(method->arguments[argumentIndex]))
                            continue;
                        FunctionDefNode methodAdapter(
                            methodNode->returnType, methodNode->name,
                            methodNode->parameters, methodNode->body);
                        const size_t methodFormalIndex =
                            explicitParameters[argumentIndex].second;
                        if(functionMutatesPointerParameter(
                               &methodAdapter, methodFormalIndex, methodNode,
                               visiting))
                            return true;
                    }
                }
                return expressionCallsMutatingPointer(method->object) ||
                       scansChildren(method->arguments);
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(expression))
                return expressionCallsMutatingPointer(field->object);
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(expression))
                return expressionCallsMutatingPointer(tuple->tuple);
            if(auto* index = dynamic_cast<IndexExpressionNode*>(expression))
                return expressionCallsMutatingPointer(index->base) ||
                       expressionCallsMutatingPointer(index->index);
            if(auto* cast = dynamic_cast<CastExpressionNode*>(expression))
                return expressionCallsMutatingPointer(cast->expression);
            if(auto* attempt = dynamic_cast<TryExpressionNode*>(expression))
                return expressionCallsMutatingPointer(attempt->expression);
            if(auto* cexpr = dynamic_cast<CexprExpressionNode*>(expression))
                return expressionCallsMutatingPointer(cexpr->expression);
            if(auto* list = dynamic_cast<ListLiteralNode*>(expression))
                return list->elements && scansChildren(list->elements->elements);
            if(auto* tuple = dynamic_cast<TupleLiteralNode*>(expression))
                return tuple->elements && scansChildren(tuple->elements->elements);
            if(auto* map = dynamic_cast<MapLiteralNode*>(expression))
            {
                if(!map->entries)
                    return false;
                for(auto* entry : map->entries->entries)
                    if(entry &&
                       (expressionCallsMutatingPointer(entry->key) ||
                        expressionCallsMutatingPointer(entry->value)))
                        return true;
                return false;
            }
            if(auto* range = dynamic_cast<RangeExpressionNode*>(expression))
                return expressionCallsMutatingPointer(range->start) ||
                       expressionCallsMutatingPointer(range->end);
            if(auto* closure = dynamic_cast<ClosureNode*>(expression))
                return scansStatements(closure->body);
            if(auto* array = dynamic_cast<ArrayFillNode*>(expression))
                return expressionCallsMutatingPointer(array->value) ||
                       expressionCallsMutatingPointer(array->count);
            if(auto* structure = dynamic_cast<StructLiteralNode*>(expression))
                for(const auto& field : structure->fields)
                    if(expressionCallsMutatingPointer(field.second))
                        return true;
            if(auto* format = dynamic_cast<FormatNode*>(expression))
            {
                if(scansChildren(format->arguments))
                    return true;
                for(const auto& argument : format->namedArguments)
                    if(expressionCallsMutatingPointer(argument.second))
                        return true;
            }
            return false;
        };

        scansStatements = [&](StatementListNode* statements) {
            if(!statements)
                return false;
            for(auto* statement : statements->statements)
                if(scansStatement(statement))
                    return true;
            return false;
        };
        scansStatement = [&](StatementNode* statement) {
            if(!statement)
                return false;
            if(auto* assignment =
                   dynamic_cast<DerefAssignmentNode*>(statement))
                return usesPointerAlias(assignment->pointerExpr);
            if(auto* assignment =
                   dynamic_cast<FieldAssignmentNode*>(statement))
                return assignment->target
                           ? usesPointerAlias(assignment->target)
                           : pointerAliases.count(assignment->structName) != 0;
            if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
                return expressionCallsMutatingPointer(
                    declaration->expression);
            if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
                return expressionCallsMutatingPointer(declaration->initExpr);
            if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
                return expressionCallsMutatingPointer(assignment->expression);
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
                return expressionCallsMutatingPointer(returned->expression);
            if(auto* expression =
                   dynamic_cast<ExpressionStatementNode*>(statement))
                return expressionCallsMutatingPointer(expression->expression);
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                return scansStatements(block->statements);
            if(auto* branch = dynamic_cast<IfNode*>(statement))
                return scansStatements(branch->thenBranch) ||
                       scansStatement(branch->elseIfBranch) ||
                       scansStatements(branch->elseBranch);
            if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
                return scansStatements(branch->thenBranch) ||
                       scansStatement(branch->elseIfBranch) ||
                       scansStatements(branch->elseBranch);
            if(auto* loop = dynamic_cast<ForNode*>(statement))
                return scansStatements(loop->body);
            if(auto* loop = dynamic_cast<WhileNode*>(statement))
                return scansStatements(loop->body);
            if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
                return scansStatement(attempt->tryBlock) ||
                       scansStatement(attempt->catchBlock);
            if(auto* print = dynamic_cast<PrintNode*>(statement))
            {
                for(auto* argument : print->arguments)
                    if(expressionCallsMutatingPointer(argument))
                        return true;
                for(const auto& argument : print->namedArguments)
                    if(expressionCallsMutatingPointer(argument.second))
                        return true;
                return false;
            }
            if(auto* assertion = dynamic_cast<AssertNode*>(statement))
                return expressionCallsMutatingPointer(assertion->condition);
            if(auto* assertion = dynamic_cast<AssertEqNode*>(statement))
                return expressionCallsMutatingPointer(assertion->left) ||
                       expressionCallsMutatingPointer(assertion->right);
            return false;
        };
        const bool mutates = scansStatements(function->body);
        visiting.erase(functionAndParameter);
        return mutates;
    };
    auto argumentIsKnownSharedBorrow = [&](ExpressionNode* argument) {
        return expressionIsKnownSharedBorrow(argument);
    };
    auto argumentCarriesLocalBorrow = [&](ExpressionNode* argument) {
        if(expressionCarriesLocalBorrow(argument))
            return true;
        auto ownerIsLocal = [&](const std::string& owner) {
            return !owner.empty() && variableScopeDepth.count(owner) != 0;
        };
        if(auto* address = dynamic_cast<UnaryOpNode*>(argument))
            if(address->op == UnaryOpNode::OP_ADDR ||
               address->op == UnaryOpNode::OP_ADDR_MUT)
                return ownerIsLocal(
                    resolveBorrowOwnerFromLValue(address->operand));
        if(auto* identifier = dynamic_cast<IdentifierNode*>(argument))
        {
            auto pointer = pointerBorrowTarget.find(identifier->name);
            if(pointer != pointerBorrowTarget.end() &&
               ownerIsLocal(pointer->second))
                return true;
            auto origins = aggregateBorrowOrigins.find(identifier->name);
            if(origins != aggregateBorrowOrigins.end())
                for(const auto& owner : origins->second)
                    if(ownerIsLocal(owner))
                        return true;
        }
        const std::string container =
            resolveBorrowOwnerFromLValue(argument);
        auto origins = aggregateBorrowOrigins.find(container);
        if(origins != aggregateBorrowOrigins.end())
            for(const auto& owner : origins->second)
                if(ownerIsLocal(owner))
                    return true;
        return false;
    };
    // Generic specializations may already have inlined/bound their callback
    // while generating the body. Re-analyzing the call-site closure there can
    // corrupt generic closure return-type inference; this transfer is only
    // needed for ordinary concrete wrapper functions.
    if(best->node && best->node->parameters &&
       best->node->typeParams.empty() &&
       best->node->boundClosureParameters.empty() &&
       best->node->closureSpecializationKey.empty())
        for(size_t parameterIndex = 0;
            parameterIndex < node->arguments.size() &&
            parameterIndex < best->node->parameters->parameters.size();
            ++parameterIndex)
        {
            ParameterNode* parameter =
                best->node->parameters->parameters[parameterIndex];
            if(parameter &&
               dynamic_cast<PointerTypeNode*>(parameter->type) &&
               !best->node->body && unsafeDepth == 0 &&
               argumentCarriesLocalBorrow(
                   node->arguments[parameterIndex]))
            {
                reportError(
                    node->line,
                    "cannot pass a borrow of local storage to opaque pointer "
                    "parameter '" + parameter->name +
                        "' outside unsafe because the callee may retain it");
                return nullptr;
            }
            std::set<std::pair<const void*, size_t>> visiting;
            if(functionMutatesPointerParameter(best->node, parameterIndex,
                                               best->node, visiting) &&
               argumentIsKnownSharedBorrow(node->arguments[parameterIndex]))
            {
                std::string argumentName = "argument";
                if(auto* identifier = dynamic_cast<IdentifierNode*>(
                       node->arguments[parameterIndex]))
                    argumentName = identifier->name;
                reportError(
                    node->line,
                    "cannot pass shared borrow '" + argumentName +
                        "' to mutating pointer parameter '" +
                        best->node->parameters->parameters[parameterIndex]
                            ->name + "'");
                return nullptr;
            }
        }

    auto callableParameterIsNoEscape = [&](size_t parameterIndex)
    {
        if(!best->node || !best->node->body || !best->node->parameters ||
           parameterIndex >= best->node->parameters->parameters.size())
            return false;
        ParameterNode* parameter =
            best->node->parameters->parameters[parameterIndex];
        if(!parameter ||
           !dynamic_cast<FunctionTypeNode*>(parameter->type))
            return false;

        auto conditionUsesParameter = [&](ExpressionNode* expression) {
            std::set<std::string> usedIdentifiers;
            collect_used_idents(expression, usedIdentifiers);
            return usedIdentifiers.count(parameter->name) != 0;
        };
        std::function<bool(ExpressionNode*)> expressionIsNoEscape;
        expressionIsNoEscape = [&](ExpressionNode* expression) {
            if(!expression || !conditionUsesParameter(expression))
                return true;
            if(auto* call = dynamic_cast<FunctionCallNode*>(expression))
            {
                for(auto* argument : call->arguments)
                    if(!expressionIsNoEscape(argument))
                        return false;
                // Any callback occurrence in the callee or arguments must
                // itself be an invocation; passing the callback value is not
                // a proof.
                return true;
            }
            if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
                return expressionIsNoEscape(binary->left) &&
                       expressionIsNoEscape(binary->right);
            if(auto* unary = dynamic_cast<UnaryOpNode*>(expression))
                return expressionIsNoEscape(unary->operand);
            if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
                return expressionIsNoEscape(ternary->condition) &&
                       expressionIsNoEscape(ternary->trueExpr) &&
                       expressionIsNoEscape(ternary->falseExpr);
            if(auto* cast = dynamic_cast<CastExpressionNode*>(expression))
                return expressionIsNoEscape(cast->expression);
            if(auto* attempt = dynamic_cast<TryExpressionNode*>(expression))
                return expressionIsNoEscape(attempt->expression);
            return false;
        };
        auto isDirectInvocation = [&](ExpressionNode* expression) {
            auto* call = dynamic_cast<FunctionCallNode*>(expression);
            if(!call || call->name != parameter->name)
                return false;
            for(auto* argument : call->arguments)
                if(!expressionIsNoEscape(argument))
                    return false;
            return true;
        };
        std::function<bool(StatementNode*)> statementIsNoEscape;
        std::function<bool(StatementListNode*)> statementsAreNoEscape;
        statementsAreNoEscape = [&](StatementListNode* statements) {
            if(!statements)
                return true;
            for(auto* statement : statements->statements)
                if(!statementIsNoEscape(statement))
                    return false;
            return true;
        };
        statementIsNoEscape = [&](StatementNode* statement) {
            if(!statement)
                return true;
            if(auto* expressionStatement =
                   dynamic_cast<ExpressionStatementNode*>(statement))
                return isDirectInvocation(expressionStatement->expression);
            if(auto* returnStatement = dynamic_cast<ReturnNode*>(statement))
                return expressionIsNoEscape(returnStatement->expression);
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                return statementsAreNoEscape(block->statements);
            if(auto* branch = dynamic_cast<IfNode*>(statement))
                return !branch->conditionInit &&
                       expressionIsNoEscape(branch->condition) &&
                       statementsAreNoEscape(branch->thenBranch) &&
                       statementIsNoEscape(branch->elseIfBranch) &&
                       statementsAreNoEscape(branch->elseBranch);
            if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
                return expressionIsNoEscape(branch->condition) &&
                       statementsAreNoEscape(branch->thenBranch) &&
                       statementIsNoEscape(branch->elseIfBranch) &&
                       statementsAreNoEscape(branch->elseBranch);
            if(auto* loop = dynamic_cast<WhileNode*>(statement))
                return expressionIsNoEscape(loop->condition) &&
                       statementsAreNoEscape(loop->body);
            if(auto* loop = dynamic_cast<ForNode*>(statement))
                return loop->varName != parameter->name &&
                       loop->indexVarName != parameter->name &&
                       expressionIsNoEscape(loop->iterable) &&
                       statementsAreNoEscape(loop->body);
            if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
                return attempt->catchName != parameter->name &&
                       statementIsNoEscape(attempt->tryBlock) &&
                       statementIsNoEscape(attempt->catchBlock);
            if(auto* print = dynamic_cast<PrintNode*>(statement))
            {
                auto argumentIsSafe = [&](ExpressionNode* expression) {
                    return expressionIsNoEscape(expression);
                };
                for(auto* argument : print->arguments)
                    if(!argumentIsSafe(argument))
                        return false;
                for(const auto& argument : print->namedArguments)
                    if(!argumentIsSafe(argument.second))
                        return false;
                return true;
            }
            if(auto* assertion = dynamic_cast<AssertNode*>(statement))
                return expressionIsNoEscape(assertion->condition);
            if(auto* assertion = dynamic_cast<AssertEqNode*>(statement))
            {
                auto expressionIsSafe = [&](ExpressionNode* expression) {
                    return expressionIsNoEscape(expression);
                };
                return expressionIsSafe(assertion->left) &&
                       expressionIsSafe(assertion->right);
            }
            return false;
        };
        return statementsAreNoEscape(best->node->body);
    };

    auto callableParameterIsDirectlyInvoked = [&](size_t parameterIndex) {
        if(!best->node || !best->node->body || !best->node->parameters ||
           parameterIndex >= best->node->parameters->parameters.size())
            return false;
        const auto* parameter =
            best->node->parameters->parameters[parameterIndex];
        if(!parameter || !dynamic_cast<FunctionTypeNode*>(parameter->type))
            return false;
        auto isDirectInvocation = [&](ExpressionNode* expression) {
            auto* call = dynamic_cast<FunctionCallNode*>(expression);
            return call && call->name == parameter->name;
        };
        std::function<bool(StatementNode*)> statementInvokes;
        std::function<bool(StatementListNode*)> statementsInvoke;
        statementsInvoke = [&](StatementListNode* statements) {
            if(!statements)
                return false;
            for(auto* statement : statements->statements)
                if(statementInvokes(statement))
                    return true;
            return false;
        };
        statementInvokes = [&](StatementNode* statement) {
            if(!statement)
                return false;
            if(auto* expressionStatement =
                   dynamic_cast<ExpressionStatementNode*>(statement))
                return isDirectInvocation(expressionStatement->expression);
            if(auto* returnStatement = dynamic_cast<ReturnNode*>(statement))
                return isDirectInvocation(returnStatement->expression);
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                return statementsInvoke(block->statements);
            if(auto* branch = dynamic_cast<IfNode*>(statement))
                return statementsInvoke(branch->thenBranch) ||
                       statementInvokes(branch->elseIfBranch) ||
                       statementsInvoke(branch->elseBranch);
            if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
                return statementsInvoke(branch->thenBranch) ||
                       statementInvokes(branch->elseIfBranch) ||
                       statementsInvoke(branch->elseBranch);
            if(auto* loop = dynamic_cast<WhileNode*>(statement))
                return statementsInvoke(loop->body);
            if(auto* loop = dynamic_cast<ForNode*>(statement))
                return statementsInvoke(loop->body);
            if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
                return statementInvokes(attempt->tryBlock) ||
                       statementInvokes(attempt->catchBlock);
            if(auto* print = dynamic_cast<PrintNode*>(statement))
            {
                for(auto* argument : print->arguments)
                    if(isDirectInvocation(argument))
                        return true;
                for(const auto& argument : print->namedArguments)
                    if(isDirectInvocation(argument.second))
                        return true;
                return false;
            }
            if(auto* assertion = dynamic_cast<AssertNode*>(statement))
                return isDirectInvocation(assertion->condition);
            if(auto* assertion = dynamic_cast<AssertEqNode*>(statement))
                return isDirectInvocation(assertion->left) ||
                       isDirectInvocation(assertion->right);
            return false;
        };
        return statementsInvoke(best->node->body);
    };

    for(size_t argumentIndex = 0;
        argumentIndex < node->arguments.size(); ++argumentIndex)
    {
        if(dynamic_cast<ClosureNode*>(node->arguments[argumentIndex]))
        {
            if(!callableParameterIsNoEscape(argumentIndex))
            {
                reportError(node->line,
                            "cannot pass an inline closure to function '" +
                                node->name +
                                "' because that callback parameter may escape");
                return nullptr;
            }

            continue;
        }
        auto* identifier =
            dynamic_cast<IdentifierNode*>(node->arguments[argumentIndex]);
        if(!identifier)
            continue;
        auto callableType = functionTypeVariables.find(identifier->name);
        auto runtime = runtimeClosureVariables.find(identifier->name);
        auto declared = closureVariables.find(identifier->name);
        const bool hasRuntimeAst =
            runtime != runtimeClosureVariables.end() &&
            declared != closureVariables.end() &&
            runtime->second == declared->second && runtime->second;
        if(callableType != functionTypeVariables.end() && !hasRuntimeAst)
        {
            auto* expectedType =
                best->node && best->node->parameters &&
                        argumentIndex < best->node->parameters->parameters.size()
                    ? dynamic_cast<FunctionTypeNode*>(
                          best->node->parameters->parameters[argumentIndex]
                              ->type)
                    : nullptr;
            if(!expectedType ||
               typeMangle(callableType->second) != typeMangle(expectedType))
            {
                reportError(node->line,
                            "callable argument '" + identifier->name +
                                "' requires a matching fn(...) -> ... "
                                "parameter on function '" + node->name + "'");
                return nullptr;
            }
            if(!callableParameterIsNoEscape(argumentIndex))
            {
                reportError(node->line,
                            "cannot pass callable '" + identifier->name +
                                "' to function '" + node->name +
                                "' because that parameter may escape");
                return nullptr;
            }
            continue;
        }
        if(runtime == runtimeClosureVariables.end() ||
           declared == closureVariables.end() ||
           runtime->second != declared->second || !runtime->second)
            continue;

        bool specializedInlineCallback = false;
        if(best->node && best->node->parameters)
        {
            for(const auto& bound : best->node->boundClosureParameters)
            {
                if(bound.second != runtime->second)
                    continue;
                for(size_t parameterIndex = 0;
                    parameterIndex <
                    best->node->parameters->parameters.size();
                    ++parameterIndex)
                {
                    if(best->node->parameters->parameters[parameterIndex]
                               ->name == bound.first &&
                       parameterIndex == argumentIndex)
                        specializedInlineCallback = true;
                }
            }
        }
        if(specializedInlineCallback)
            continue;

        auto owners = runtimeClosureBorrowOwners.find(identifier->name);
        if(owners != runtimeClosureBorrowOwners.end() &&
           !owners->second.empty() &&
           !callableParameterIsNoEscape(argumentIndex))
        {
            reportError(node->line, "cannot pass closure '" +
                                        identifier->name +
                                        "' to function '" + node->name +
                                        "' because it borrows local variable '" +
                                        *owners->second.begin() + "'");
            return nullptr;
        }
        auto* expectedType =
            best->node && best->node->parameters &&
                    argumentIndex < best->node->parameters->parameters.size()
                ? dynamic_cast<FunctionTypeNode*>(
                      best->node->parameters->parameters[argumentIndex]->type)
                : nullptr;
        auto actualType = functionTypeVariables.find(identifier->name);
        if(!expectedType || actualType == functionTypeVariables.end() ||
           typeMangle(actualType->second) != typeMangle(expectedType))
        {
            reportError(node->line,
                        "runtime closure values can only be passed to a "
                        "matching fn(...) -> ... parameter or used as a "
                        "generic callback");
            return nullptr;
        }
        if(!callableParameterIsNoEscape(argumentIndex))
        {
            reportError(node->line,
                        "cannot pass closure '" + identifier->name +
                            "' to function '" + node->name +
                            "' because that parameter may escape");
            return nullptr;
        }
        continue;
    }

    // A directly invoked callback parameter can still retain a pointer passed
    // to it. At this boundary the actual callback is available, so reject a
    // local borrow when that callback is opaque (for example an extern fn).
    if(best->node && best->node->body && best->node->parameters &&
       unsafeDepth == 0)
    {
        auto callbackIsOpaque = [&](ExpressionNode* supplied)
        {
            if(dynamic_cast<ClosureNode*>(supplied))
                return false;
            auto* identifier = dynamic_cast<IdentifierNode*>(supplied);
            if(!identifier)
                return true;
            auto closure = closureVariables.find(identifier->name);
            if(closure != closureVariables.end() && closure->second)
                return false;
            auto knownTarget = functionItemVariables.find(identifier->name);
            if(functionTypeVariables.count(identifier->name) &&
               (knownTarget == functionItemVariables.end() ||
                knownTarget->second.empty()))
                return true;
            bool foundVisible = false;
            const std::set<std::string> targetNames =
                knownTarget == functionItemVariables.end()
                    ? std::set<std::string>{identifier->name}
                    : knownTarget->second;
            for(const auto& targetName : targetNames)
            {
                auto overloads = functionOverloads.find(targetName);
                if(overloads == functionOverloads.end())
                    return true;
                for(const auto& overload : overloads->second)
                {
                    if(!isOverloadVisible(overload) || !overload.node)
                        continue;
                    foundVisible = true;
                    if(!overload.node->body)
                        return true;
                }
            }
            return !foundVisible;
        };
        for(size_t callbackIndex = 0;
            callbackIndex < best->node->parameters->parameters.size() &&
            callbackIndex < node->arguments.size();
            ++callbackIndex)
        {
            auto* callbackParameter =
                best->node->parameters->parameters[callbackIndex];
            if(!callbackParameter ||
               !dynamic_cast<FunctionTypeNode*>(callbackParameter->type) ||
               !callbackIsOpaque(node->arguments[callbackIndex]))
                continue;

            for(size_t pointerIndex = 0;
                pointerIndex < best->node->parameters->parameters.size() &&
                pointerIndex < node->arguments.size();
                ++pointerIndex)
            {
                auto* pointerParameter =
                    best->node->parameters->parameters[pointerIndex];
                if(!pointerParameter ||
                   !dynamic_cast<PointerTypeNode*>(pointerParameter->type) ||
                   !argumentCarriesLocalBorrow(node->arguments[pointerIndex]))
                    continue;

                bool forwarded = false;
                for(auto* statement : best->node->body->statements)
                {
                    ExpressionNode* expression = nullptr;
                    if(auto* expressionStatement =
                           dynamic_cast<ExpressionStatementNode*>(statement))
                        expression = expressionStatement->expression;
                    else if(auto* returnStatement =
                                dynamic_cast<ReturnNode*>(statement))
                        expression = returnStatement->expression;
                    auto* invocation =
                        dynamic_cast<FunctionCallNode*>(expression);
                    if(!invocation ||
                       invocation->name != callbackParameter->name)
                        continue;
                    for(auto* argument : invocation->arguments)
                    {
                        std::set<std::string> identifiers;
                        collect_used_idents(argument, identifiers);
                        if(identifiers.count(pointerParameter->name))
                        {
                            forwarded = true;
                            break;
                        }
                    }
                    if(forwarded)
                        break;
                }
                if(forwarded)
                {
                    reportError(
                        node->line,
                        "cannot pass a borrow of local storage through opaque "
                        "callable pointer parameter outside unsafe because "
                        "the callable may retain it");
                    return nullptr;
                }
            }
        }
    }

    llvm::Function* callee = best->function;
    size_t expectedArgs = callee->arg_size();
    bool isVarArg = callee->isVarArg();
    size_t hiddenCaptureCount = 0;
    if(best->node)
        for(const auto& closureCaptures :
            best->node->boundClosureCaptureAliases)
            hiddenCaptureCount += closureCaptures.second.size();
    const size_t sourceParameterCount = expectedArgs - hiddenCaptureCount;

    if(isVarArg && argVals.size() < sourceParameterCount)
    {
        reportError(node->line,
                    "function '" + node->name + "' requires at least " +
                        std::to_string(sourceParameterCount) +
                        " argument(s), but " + std::to_string(argVals.size()) +
                        " provided");
        return nullptr;
    }

    std::vector<llvm::Value*> callArgVals = argVals;
    if(best->node && best->node->parameters)
    {
        for(size_t i = 0; i < node->arguments.size() &&
                           i < best->node->parameters->parameters.size();
            ++i)
        {
            auto* reference = dynamic_cast<ReferenceTypeNode*>(
                best->node->parameters->parameters[i]->type);
            auto* unary = dynamic_cast<UnaryOpNode*>(node->arguments[i]);
            if(reference && !reference->isMutable && unary &&
               unary->op == UnaryOpNode::OP_ADDR &&
               callArgVals[i]->getType()->isPointerTy())
                callArgVals[i] = builder.CreateLoad(
                    callee->getArg(i)->getType(), callArgVals[i],
                    "shared.ref.arg");
        }
        for(const auto& bound : best->node->boundClosureParameters)
        {
            for(size_t i = 0; i < node->arguments.size() &&
                               i < best->node->parameters->parameters.size();
                ++i)
            {
                if(best->node->parameters->parameters[i]->name != bound.first)
                    continue;
                ClosureNode* actual =
                    dynamic_cast<ClosureNode*>(node->arguments[i]);
                if(auto* identifier =
                       dynamic_cast<IdentifierNode*>(node->arguments[i]))
                {
                    auto closure = closureVariables.find(identifier->name);
                    if(closure != closureVariables.end())
                        actual = closure->second;
                }
                if(actual == bound.second && i < expectedArgs &&
                   callee->getArg(i)->getType()->isPointerTy())
                    callArgVals[i] = llvm::ConstantPointerNull::get(
                        llvm::cast<llvm::PointerType>(
                            callee->getArg(i)->getType()));
                break;
            }
        }
    }
    if(best->node)
    {
        for(const auto& closureCaptures :
            best->node->boundClosureCaptureAliases)
        {
            for(const auto& capture : closureCaptures.second)
            {
                auto captured = namedValues.find(capture.first);
                if(captured == namedValues.end())
                {
                    reportError(node->line,
                                "cannot pass captured variable '" +
                                    capture.first +
                                    "' to generic closure specialization");
                    return nullptr;
                }
                llvm::Value* captureValue = captured->second;
                if(!captureValue)
                    return nullptr;
                callArgVals.push_back(captureValue);
            }
        }
    }

    std::vector<llvm::Value*> args;
    unsigned paramIdx = 0;
    for(auto* argValIn : callArgVals)
    {
        llvm::Value* argVal = argValIn;
        if(paramIdx < expectedArgs)
        {
            llvm::Type* expectedType = callee->getArg(paramIdx)->getType();
            llvm::Type* actualType = argVal->getType();

            if(actualType != expectedType)
            {
                if(actualType->isIntegerTy() && expectedType->isIntegerTy())
                {
                    unsigned actualBits = actualType->getIntegerBitWidth();
                    unsigned expectedBits = expectedType->getIntegerBitWidth();
                    if(actualBits > expectedBits)
                    {
                        argVal =
                            builder.CreateTrunc(argVal, expectedType, "trunc");
                    }
                    else if(actualBits < expectedBits)
                    {
                        argVal =
                            builder.CreateSExt(argVal, expectedType, "sext");
                    }
                }
                else if(actualType->isIntegerTy() &&
                        expectedType->isFloatingPointTy())
                {
                    argVal =
                        builder.CreateSIToFP(argVal, expectedType, "sitofp");
                }
                else if(actualType->isFloatingPointTy() &&
                        expectedType->isIntegerTy())
                {
                    argVal =
                        builder.CreateFPToSI(argVal, expectedType, "fptosi");
                }
                else if(actualType->isFloatingPointTy() &&
                        expectedType->isFloatingPointTy())
                {
                    argVal =
                        builder.CreateFPCast(argVal, expectedType, "fpcast");
                }
                else if(best->node &&
                        paramIdx <
                            (size_t)best->node->parameters->parameters.size())
                {
                    auto* declParam =
                        best->node->parameters->parameters[paramIdx];
                    if(auto* traitObj =
                           dynamic_cast<TraitObjectTypeNode*>(declParam->type))
                    {
                        llvm::Value* traitObjVal = nullptr;
                        TypeNode* actualSemantic =
                            semanticArgumentType(node->arguments[paramIdx]);
                        if(dynamic_cast<TraitObjectTypeNode*>(actualSemantic))
                        {
                            traitObjVal = coerceTraitObjectValue(
                                argVal, expectedType, node->line);
                        }
                        else
                        {
                            traitObjVal = buildTraitObjectValue(
                                node->arguments[paramIdx], traitObj->traitName,
                                node->line);
                        }
                        if(!traitObjVal)
                            return nullptr;
                        args.push_back(traitObjVal);
                        paramIdx++;
                        continue;
                    }
                }
                else if(!(actualType->isPointerTy() &&
                          expectedType->isPointerTy()))
                {
                    std::string actualStr, expectedStr;

                    if(actualType->isStructTy())
                        actualStr = actualType->getStructName().str().empty()
                                        ? "struct"
                                        : actualType->getStructName().str();
                    else if(actualType->isIntegerTy())
                        actualStr = "i" + std::to_string(
                                              actualType->getIntegerBitWidth());
                    else if(actualType->isFloatTy())
                        actualStr = "f32";
                    else if(actualType->isDoubleTy())
                        actualStr = "f64";
                    else
                        actualStr = "unknown";

                    if(expectedType->isStructTy())
                        expectedStr =
                            expectedType->getStructName().str().empty()
                                ? "struct"
                                : expectedType->getStructName().str();
                    else if(expectedType->isIntegerTy())
                        expectedStr =
                            "i" +
                            std::to_string(expectedType->getIntegerBitWidth());
                    else if(expectedType->isFloatTy())
                        expectedStr = "f32";
                    else if(expectedType->isDoubleTy())
                        expectedStr = "f64";
                    else
                        expectedStr = "unknown";

                    reportError(node->line,
                                "argument " + std::to_string(paramIdx + 1) +
                                    " of function '" + node->name +
                                    "' has wrong type: expected '" +
                                    expectedStr + "', got '" + actualStr + "'");
                    return nullptr;
                }
            }
        }

        // &mut T parameter requires caller to pass &mut value.
        // &T accepts both plain value and &val (string is Copy).
        if(best->node &&
           paramIdx < (size_t)best->node->parameters->parameters.size() &&
           paramIdx < node->arguments.size())
        {
            auto* declParam = best->node->parameters->parameters[paramIdx];
            if(auto* refType =
                   dynamic_cast<ReferenceTypeNode*>(declParam->type))
            {
                if(refType->isMutable)
                {
                    auto* argExpr = node->arguments[paramIdx];
                    auto* unary = dynamic_cast<UnaryOpNode*>(argExpr);
                    bool isRefMut =
                        unary && unary->op == UnaryOpNode::OP_ADDR_MUT;
                    if(!isRefMut)
                    {
                        reportError(node->line, "parameter '" +
                                                    declParam->name +
                                                    "' expects &mut argument");
                        return nullptr;
                    }
                }
            }
        }

        // Consume move-only values only for non-reference parameters.
        // Reference parameters (&T / &mut T) borrow and must not move.
        bool shouldConsume = true;
        if(best->node &&
           paramIdx < (size_t)best->node->parameters->parameters.size() &&
           paramIdx < node->arguments.size())
        {
            auto* declParam = best->node->parameters->parameters[paramIdx];
            if(dynamic_cast<ReferenceTypeNode*>(declParam->type))
                shouldConsume = false;
        }
        if(shouldConsume && paramIdx < node->arguments.size())
        {
            consumeMoveFromExpression(node->arguments[paramIdx], node->line,
                                      "passing argument to function '" +
                                          node->name + "'");
        }

        args.push_back(argVal);
        paramIdx++;
    }

    llvm::Value* callResult = nullptr;
    if(callee->getReturnType()->isVoidTy())
        callResult = builder.CreateCall(callee, args);
    else
        callResult = builder.CreateCall(callee, args, "calltmp");

    // A no-escape wrapper can synchronously invoke a concrete runtime closure.
    // Reflect its pointer reassignments at the call boundary so the caller's
    // borrow state matches the callback's effects.
    if(best->node && best->node->parameters)
        for(size_t parameterIndex = 0;
            parameterIndex < best->node->parameters->parameters.size() &&
            parameterIndex < node->arguments.size();
            ++parameterIndex)
        {
            if(!callableParameterIsNoEscape(parameterIndex) ||
               !callableParameterIsDirectlyInvoked(parameterIndex))
                continue;
            auto* identifier =
                dynamic_cast<IdentifierNode*>(node->arguments[parameterIndex]);
            if(identifier)
            {
                auto runtime = runtimeClosureVariables.find(identifier->name);
                auto declared = closureVariables.find(identifier->name);
                if(runtime != runtimeClosureVariables.end() &&
                   declared != closureVariables.end() && runtime->second &&
                   runtime->second == declared->second)
                    applyRuntimeClosurePointerBorrowEffects(
                        identifier->name, runtime->second, node->line);
                continue;
            }
            auto* inlineClosure = dynamic_cast<ClosureNode*>(
                node->arguments[parameterIndex]);
            if(!inlineClosure)
                continue;
            std::function<bool(StatementListNode*)> assignsCapturedPointer;
            std::function<bool(StatementNode*)> statementAssignsCapturedPointer;
            assignsCapturedPointer = [&](StatementListNode* statements) {
                if(!statements)
                    return false;
                return std::any_of(
                    statements->statements.begin(),
                    statements->statements.end(),
                    [&](StatementNode* statement) {
                        return statementAssignsCapturedPointer(statement);
                    });
            };
            statementAssignsCapturedPointer = [&](StatementNode* statement) {
                if(auto* assignment =
                       dynamic_cast<AssignmentNode*>(statement))
                    return pointerElementTypes.count(assignment->name) != 0;
                if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                    return assignsCapturedPointer(block->statements);
                if(auto* branch = dynamic_cast<IfNode*>(statement))
                    return assignsCapturedPointer(branch->thenBranch) ||
                           assignsCapturedPointer(branch->elseBranch) ||
                           (branch->elseIfBranch &&
                            statementAssignsCapturedPointer(
                                branch->elseIfBranch));
                if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
                    return assignsCapturedPointer(branch->thenBranch) ||
                           assignsCapturedPointer(branch->elseBranch) ||
                           (branch->elseIfBranch &&
                            statementAssignsCapturedPointer(
                                branch->elseIfBranch));
                if(auto* loop = dynamic_cast<WhileNode*>(statement))
                    return assignsCapturedPointer(loop->body);
                if(auto* loop = dynamic_cast<ForNode*>(statement))
                    return assignsCapturedPointer(loop->body);
                return false;
            };
            if(!assignsCapturedPointer(inlineClosure->body))
                continue;
            std::vector<std::pair<std::string, TypeNode*>> captures;
            TypeNode* closureReturnType = nullptr;
            if(getRuntimeClosureCaptures(inlineClosure, captures,
                                         closureReturnType))
            {
                analyzeRuntimeClosurePointerBorrowEffects(inlineClosure,
                                                          captures);
                applyRuntimeClosurePointerBorrowEffects(
                    "", inlineClosure, node->line);
            }
        }

    if(isFreeLikeCall)
        markFreeLikeConsumed(node->arguments[0]);

    return callResult;
}

llvm::Function* CodeGenerator::generateClosureFn(
    ClosureNode* node,
    const std::vector<std::pair<std::string, TypeNode*>>* captures,
    const std::set<std::string>* constCaptures,
    TypeNode* returnType)
{
    static int closureSeq = 0;
    std::string closureName = "__mlang_closure_" + std::to_string(closureSeq++);

    std::vector<llvm::Type*> closureParameters;
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* erasedPointerType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* erasedPointerType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    if(captures)
        closureParameters.push_back(erasedPointerType);
    if(node->parameters)
    {
        for(auto* parameter : node->parameters->parameters)
        {
            llvm::Type* parameterType =
                parameter ? parameterLLVMType(parameter->type) : nullptr;
            if(!parameterType)
            {
                reportError(node->line,
                            "runtime closure parameter has an unsupported "
                            "type");
                return nullptr;
            }
            closureParameters.push_back(parameterType);
        }
    }
    llvm::Type* llvmReturnType = returnType
                                     ? getLLVMTypeFromNode(returnType)
                                     : llvm::Type::getVoidTy(context);
    if(!llvmReturnType)
    {
        reportError(node->line,
                    "runtime closure has an unsupported return type");
        return nullptr;
    }
    llvm::FunctionType* closureFnType = llvm::FunctionType::get(
        llvmReturnType, closureParameters, false);
    llvm::Function* closureFn =
        llvm::Function::Create(closureFnType, llvm::Function::PrivateLinkage,
                               closureName, module.get());

    // Save current codegen state so we can restore it after the closure body
    auto savedIP = builder.saveIP();
    llvm::Value* savedExceptionFrame = currentFunctionExceptionFrame;
    currentFunctionExceptionFrame = nullptr;
    auto savedNamedValues = namedValues;
    auto savedConstantVars = constantVariables;
    auto savedImmutableReferenceVars = immutableReferenceVariables;
    auto savedMovedVars = movedVariables;
    auto savedPtrBorrowTarget = pointerBorrowTarget;
    auto savedPtrBorrowParent = pointerBorrowParent;
    auto savedPtrBorrowPlace = pointerBorrowPlace;
    auto savedActiveBorrowers = activeBorrowers;
    auto savedActiveMutBorrow = activeMutBorrower;
    auto savedVarScopeDepth = variableScopeDepth;
    auto savedVarTypes = variableTypes;
    auto savedStructVarTypes = structVariableTypes;
    auto savedEnumVarTypes = enumVariableTypes;
    auto savedListElementTypes = listElementTypes;
    auto savedMapKeyValueTypes = mapKeyValueTypes;
    auto savedTupleElementTypes = tupleElementTypes;
    auto savedPointerElementTypes = pointerElementTypes;
    auto savedArrayCapacities = arrayCapacities;
    auto savedMultiarrayMutability = multiarrayMutability;
    auto savedCleanupScopes = cleanupScopes;
    auto savedPtrBorrowScopes = pointerBorrowScopes;
    auto savedVarDepthScopes = variableScopeDepthScopes;
    auto savedRuntimeClosureVariables = runtimeClosureVariables;
    auto savedRuntimeClosureReturnTypes = runtimeClosureReturnTypes;
    auto savedFunctionTypeVariables = functionTypeVariables;
    auto savedFunctionItemVariables = functionItemVariables;
    auto savedFunctionTypeParameters = functionTypeParameters;
    auto savedPointerParameterBorrowOrigins = pointerParameterBorrowOrigins;
    auto savedRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;
    auto savedLoopBreak = loopBreakBlocks;
    auto savedLoopContinue = loopContinueBlocks;
    auto savedModule = currentModule;
    auto savedClosureVars = closureVariables;
    auto savedClosureCaptureAliases = closureCaptureAliases;
    auto savedClosureCaptureTypes = closureCaptureTypes;
    auto savedClosureConstCaptures = closureConstCaptures;
    auto savedClosureCaptureReferenceAliases = closureCaptureReferenceAliases;
    auto savedActiveInline = activeInlineClosures;
    auto savedSemanticReturnType = currentSemanticReturnType;
    auto savedInoutParams = inoutParams;
    llvm::Function* savedInoutFunction = inoutFunction;
    inoutParams.clear();
    inoutFunction = closureFn;

    // Initialise a fresh scope for the closure body
    namedValues.clear();
    constantVariables.clear();
    immutableReferenceVariables.clear();
    movedVariables.clear();
    closureVariables.clear();
    runtimeClosureVariables.clear();
    runtimeClosureReturnTypes.clear();
    functionTypeVariables.clear();
    functionItemVariables.clear();
    functionTypeParameters.clear();
    pointerParameterBorrowOrigins.clear();
    runtimeClosureBorrowOwners.clear();
    closureCaptureAliases.clear();
    closureCaptureTypes.clear();
    closureConstCaptures.clear();
    closureCaptureReferenceAliases.clear();
    activeInlineClosures.clear();
    pointerBorrowTarget.clear();
    pointerBorrowParent.clear();
    pointerBorrowPlace.clear();
    pointerKnownNull.clear();
    activeBorrowers.clear();
    activeMutBorrower.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    enumVariableTypes.clear();
    listElementTypes.clear();
    mapKeyValueTypes.clear();
    tupleElementTypes.clear();
    pointerElementTypes.clear();
    arrayCapacities.clear();
    multiarrayMutability.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    loopBreakBlocks.clear();
    loopContinueBlocks.clear();
    currentModule = "";
    currentSemanticReturnType = returnType;

    llvm::BasicBlock* entry =
        llvm::BasicBlock::Create(context, "entry", closureFn);
    builder.SetInsertPoint(entry);
    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    if(captures && !captures->empty())
    {
        std::vector<llvm::Type*> environmentFields(captures->size(),
                                                   erasedPointerType);
        llvm::StructType* environmentType =
            llvm::StructType::get(context, environmentFields);
        llvm::Value* environment = &*closureFn->arg_begin();
#if LLVM_VERSION_MAJOR < 15
        environment = builder.CreateBitCast(
            environment,
            llvm::PointerType::get(environmentType, 0), "closure.env.cast");
#endif
        for(size_t i = 0; i < captures->size(); ++i)
        {
            const auto& capture = (*captures)[i];
            llvm::Type* capturedType = getLLVMTypeFromNode(capture.second);
            if(!capturedType)
            {
                reportError(node->line,
                            "unsupported runtime closure capture type for '" +
                                capture.first + "'");
                continue;
            }
            llvm::Value* field = builder.CreateStructGEP(
                environmentType, environment, static_cast<unsigned>(i),
                capture.first + ".capture.field");
            llvm::Value* address = builder.CreateLoad(
                erasedPointerType, field, capture.first + ".capture.address");
#if LLVM_VERSION_MAJOR < 15
            address = builder.CreateBitCast(
                address, llvm::PointerType::get(capturedType, 0),
                capture.first + ".capture.typed");
#endif
            namedValues[capture.first] = address;
            closureCaptureReferenceAliases.insert(capture.first);
            recordVariableScopeDepth(capture.first);
            variableTypes[capture.first] = capture.second->kind;
            if(auto* structType =
                   dynamic_cast<StructTypeRefNode*>(capture.second))
            {
                std::string enumName =
                    resolveVisibleEnumName(structType->structName);
                if(!enumName.empty())
                {
                    variableTypes[capture.first] = TypeNode::TYPE_INT;
                    enumVariableTypes[capture.first] = enumName;
                }
                else
                    structVariableTypes[capture.first] = structType->structName;
            }
            if(auto* listType =
                   dynamic_cast<GenericListTypeNode*>(capture.second))
            {
                listElementTypes[capture.first] = listType->elementType;
                if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(listType))
                    arrayCapacities[capture.first] = arrayType->capacity;
                if(auto* multiarrayType =
                       dynamic_cast<MultiArrayTypeNode*>(listType))
                    multiarrayMutability[capture.first] =
                        multiarrayType->elementsMutable;
            }
            if(auto* mapType = dynamic_cast<MapTypeNode*>(capture.second))
                mapKeyValueTypes[capture.first] = {
                    mapType->keyType, mapType->valueType};
            if(auto* tupleType = dynamic_cast<TupleTypeNode*>(capture.second))
                tupleElementTypes[capture.first] =
                    tupleType->elementTypes->types;
            if(auto* ptrType = dynamic_cast<PointerTypeNode*>(capture.second))
                pointerElementTypes[capture.first] = ptrType->elementType;
            if(constCaptures && constCaptures->count(capture.first))
                constantVariables.insert(capture.first);
        }
    }

    if(node->parameters)
    {
        size_t parameterIndex = captures ? 1 : 0;
        for(auto* parameter : node->parameters->parameters)
        {
            llvm::Argument* argument = closureFn->getArg(parameterIndex++);
            argument->setName(parameter->name);
            auto* inoutType = dynamic_cast<ReferenceTypeNode*>(parameter->type);
            if(inoutType && !inoutType->isMutable)
                inoutType = nullptr;
            llvm::Type* localType =
                inoutType ? getLLVMTypeFromNode(inoutType->elementType)
                          : argument->getType();
            llvm::AllocaInst* local = builder.CreateAlloca(
                localType, nullptr, parameter->name + ".addr");
            llvm::Value* incoming = &*argument;
            if(inoutType)
            {
                incoming = builder.CreateLoad(
                    localType, argument, parameter->name + ".in");
                inoutParams.push_back({argument, parameter->name});
            }
            else if(localType->isStructTy())
                incoming = applyStructCopySemantics(incoming);
            builder.CreateStore(incoming, local);
            namedValues[parameter->name] = local;
            recordVariableScopeDepth(parameter->name);
            if(parameter->type)
            {
                TypeNode* semanticType = parameter->type;
                if(auto* reference =
                       dynamic_cast<ReferenceTypeNode*>(semanticType))
                {
                    semanticType = reference->elementType;
                    if(!reference->isMutable)
                    {
                        constantVariables.insert(parameter->name);
                        immutableReferenceVariables.insert(parameter->name);
                    }
                }
                variableTypes[parameter->name] = semanticType->kind;
                if(auto* pointer =
                       dynamic_cast<PointerTypeNode*>(semanticType))
                {
                    pointerElementTypes[parameter->name] =
                        pointer->elementType;
                    pointerBorrowTarget[parameter->name] = parameter->name;
                    pointerBorrowPlace[parameter->name] = parameter->name;
                    pointerParameterBorrowOrigins.insert(parameter->name);
                }
                if(auto* functionType =
                       dynamic_cast<FunctionTypeNode*>(semanticType))
                {
                    functionTypeVariables[parameter->name] = functionType;
                    functionTypeParameters.insert(parameter->name);
                }
                if(auto* structType =
                       dynamic_cast<StructTypeRefNode*>(semanticType))
                {
                    std::string enumName =
                        resolveVisibleEnumName(structType->structName);
                    if(!enumName.empty())
                    {
                        variableTypes[parameter->name] = TypeNode::TYPE_INT;
                        enumVariableTypes[parameter->name] = enumName;
                    }
                    else
                        structVariableTypes[parameter->name] =
                            structType->structName;
                }
                else if(auto* genericStruct =
                            dynamic_cast<GenericStructTypeRefNode*>(
                                semanticType))
                    structVariableTypes[parameter->name] =
                        genericStruct->getMangledName();
                else if(auto* listType =
                            dynamic_cast<GenericListTypeNode*>(semanticType))
                {
                    listElementTypes[parameter->name] = listType->elementType;
                    if(auto* arrayType =
                           dynamic_cast<ArrayTypeNode*>(listType))
                        arrayCapacities[parameter->name] =
                            arrayType->capacity;
                    if(auto* multiarrayType =
                           dynamic_cast<MultiArrayTypeNode*>(listType))
                        multiarrayMutability[parameter->name] =
                            multiarrayType->elementsMutable;
                }
                else if(auto* mapType =
                            dynamic_cast<MapTypeNode*>(semanticType))
                    mapKeyValueTypes[parameter->name] = {
                        mapType->keyType, mapType->valueType};
                else if(auto* tupleType =
                            dynamic_cast<TupleTypeNode*>(semanticType))
                    tupleElementTypes[parameter->name] =
                        tupleType->elementTypes->types;
                else if(auto* pointerType =
                            dynamic_cast<PointerTypeNode*>(semanticType))
                    pointerElementTypes[parameter->name] =
                        pointerType->elementType;
            }
        }
    }

    if(node->body)
    {
        for(auto* stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock() &&
               mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                break;
        }
    }

    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
        emitInoutWriteBack();
    exitCleanupScope();

    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
    {
        if(llvmReturnType->isVoidTy())
            builder.CreateRetVoid();
        else
        {
            reportError(node->line,
                        "runtime closure with a value return must return on "
                        "every reachable path");
            builder.CreateRet(llvm::UndefValue::get(llvmReturnType));
        }
    }

    // Restore caller state
    namedValues = std::move(savedNamedValues);
    constantVariables = std::move(savedConstantVars);
    immutableReferenceVariables =
        std::move(savedImmutableReferenceVars);
    movedVariables = std::move(savedMovedVars);
    pointerBorrowTarget = std::move(savedPtrBorrowTarget);
    pointerBorrowParent = std::move(savedPtrBorrowParent);
    pointerBorrowPlace = std::move(savedPtrBorrowPlace);
    activeBorrowers = std::move(savedActiveBorrowers);
    activeMutBorrower = std::move(savedActiveMutBorrow);
    variableScopeDepth = std::move(savedVarScopeDepth);
    variableTypes = std::move(savedVarTypes);
    structVariableTypes = std::move(savedStructVarTypes);
    enumVariableTypes = std::move(savedEnumVarTypes);
    listElementTypes = std::move(savedListElementTypes);
    mapKeyValueTypes = std::move(savedMapKeyValueTypes);
    tupleElementTypes = std::move(savedTupleElementTypes);
    pointerElementTypes = std::move(savedPointerElementTypes);
    arrayCapacities = std::move(savedArrayCapacities);
    multiarrayMutability = std::move(savedMultiarrayMutability);
    cleanupScopes = std::move(savedCleanupScopes);
    pointerBorrowScopes = std::move(savedPtrBorrowScopes);
    variableScopeDepthScopes = std::move(savedVarDepthScopes);
    loopBreakBlocks = std::move(savedLoopBreak);
    loopContinueBlocks = std::move(savedLoopContinue);
    currentModule = std::move(savedModule);
    currentFunctionExceptionFrame = savedExceptionFrame;
    closureVariables = std::move(savedClosureVars);
    runtimeClosureVariables = std::move(savedRuntimeClosureVariables);
    runtimeClosureReturnTypes = std::move(savedRuntimeClosureReturnTypes);
    functionTypeVariables = std::move(savedFunctionTypeVariables);
    functionItemVariables = std::move(savedFunctionItemVariables);
    functionTypeParameters = std::move(savedFunctionTypeParameters);
    pointerParameterBorrowOrigins =
        std::move(savedPointerParameterBorrowOrigins);
    runtimeClosureBorrowOwners =
        std::move(savedRuntimeClosureBorrowOwners);
    closureCaptureAliases = std::move(savedClosureCaptureAliases);
    closureCaptureTypes = std::move(savedClosureCaptureTypes);
    closureConstCaptures = std::move(savedClosureConstCaptures);
    closureCaptureReferenceAliases =
        std::move(savedClosureCaptureReferenceAliases);
    activeInlineClosures = std::move(savedActiveInline);
    currentSemanticReturnType = savedSemanticReturnType;
    inoutParams = std::move(savedInoutParams);
    inoutFunction = savedInoutFunction;
    builder.restoreIP(savedIP);

    return closureFn;
}

llvm::Value* CodeGenerator::generateThreadSpawn(FunctionCallNode* node)
{
    // A thread spawn can occur in a transitively imported module whose structs
    // were not merged into the root program. Load the stdlib-owned return type
    // before lowering the intrinsic; the compiler does not synthesize it.
    if(!getStructType("thread") && moduleLoader)
    {
        for(auto* structDef : moduleLoader->getModuleStructs("std::thread"))
        {
            if(structDef && structDef->name == "thread")
            {
                structVisibility[structDef->name] = std::make_pair(
                    structDef->isPublic, structDef->sourceModule);
                generateStructDefinition(structDef);
                break;
            }
        }
    }

    if(node->arguments.size() < 1 || node->arguments.size() > 5)
    {
        reportError(node->line,
                    "thread_spawn expects a function name and up to 4 integer "
                    "arguments");
        return nullptr;
    }

    size_t argCount = 0;
    llvm::Function* targetFunc = nullptr;

    if(auto* closureArg = dynamic_cast<ClosureNode*>(node->arguments[0]))
    {
        if(closureArg->parameters &&
           !closureArg->parameters->parameters.empty())
        {
            reportError(node->line,
                        "thread_spawn closure does not support parameters");
            return nullptr;
        }
        if(node->arguments.size() != 1)
        {
            reportError(node->line,
                        "closure-based thread_spawn takes no extra arguments");
            return nullptr;
        }
        targetFunc = generateClosureFn(closureArg);
        if(!targetFunc)
            return nullptr;
        // argCount stays 0 — closure captures nothing via the arg buffer
    }
    else
    {
        auto* targetId = dynamic_cast<IdentifierNode*>(node->arguments[0]);
        if(!targetId)
        {
            reportError(
                node->line,
                "thread_spawn expects a function name identifier or closure");
            return nullptr;
        }

        argCount = node->arguments.size() - 1;

        auto overloadIt = functionOverloads.find(targetId->name);
        if(overloadIt != functionOverloads.end())
        {
            for(auto& info : overloadIt->second)
            {
                if(!isOverloadVisible(info))
                    continue;
                llvm::Function* candidate = info.function;
                if(!candidate || candidate->isVarArg())
                    continue;
                if(candidate->arg_size() != argCount)
                    continue;
                if(targetFunc && targetFunc != candidate)
                {
                    reportError(node->line, "ambiguous function: '" +
                                                targetId->name +
                                                "' for thread_spawn");
                    return nullptr;
                }
                targetFunc = candidate;
            }
        }

        if(!targetFunc)
            targetFunc = module->getFunction(targetId->name);
        if(!targetFunc)
        {
            reportError(node->line,
                        "unknown function: '" + targetId->name + "'");
            return nullptr;
        }

        if(targetFunc->arg_size() != argCount)
        {
            reportError(node->line,
                        "thread_spawn target argument count mismatch");
            return nullptr;
        }

        for(size_t i = 0; i < argCount; ++i)
        {
            llvm::Type* paramType =
                targetFunc->getFunctionType()->getParamType(i);
            if(paramType->isIntegerTy())
                continue;
            if(paramType->isStructTy())
            {
                auto* structType = llvm::cast<llvm::StructType>(paramType);
                std::string structName = structType->getName().str();
                auto memIt = structMembers.find(structName);
                int rawIndex = -1;
                if(memIt != structMembers.end())
                {
                    for(size_t m = 0; m < memIt->second.size(); ++m)
                    {
                        if(memIt->second[m].first == "raw")
                        {
                            rawIndex = static_cast<int>(m);
                            break;
                        }
                    }
                }
                if(rawIndex >= 0)
                    continue;
            }
            reportError(
                node->line,
                "thread_spawn arguments must be integer or handle types");
            return nullptr;
        }
    }

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::string wrapperName =
        "__mlang_thread_wrapper_" + targetFunc->getName().str();
    if(argCount > 0)
        wrapperName += "_args" + std::to_string(argCount);
    llvm::Function* wrapperFunc = module->getFunction(wrapperName);
    if(!wrapperFunc)
    {
        llvm::FunctionType* wrapperType =
            llvm::FunctionType::get(ptrType, {ptrType}, false);
        wrapperFunc =
            llvm::Function::Create(wrapperType, llvm::Function::PrivateLinkage,
                                   wrapperName, module.get());

        auto savedIP = builder.saveIP();
        llvm::BasicBlock* entry =
            llvm::BasicBlock::Create(context, "entry", wrapperFunc);
        builder.SetInsertPoint(entry);

        std::vector<llvm::Value*> callArgs;
        if(argCount > 0)
        {
            llvm::Value* basePtr = wrapperFunc->getArg(0);
            for(size_t i = 0; i < argCount; ++i)
            {
                llvm::Value* offset = llvm::ConstantInt::get(
                    int64Type, static_cast<uint64_t>(i * 8), false);
                llvm::Value* bytePtr =
                    builder.CreateGEP(llvm::Type::getInt8Ty(context), basePtr,
                                      offset, "thread.argbyte");
#if LLVM_VERSION_MAJOR < 15
                llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
                llvm::Value* argPtr = builder.CreateBitCast(
                    bytePtr, int64PtrType, "thread.argptr");
                llvm::Value* argVal =
                    builder.CreateLoad(int64Type, argPtr, "thread.arg");
#else
                llvm::Value* argVal =
                    builder.CreateLoad(int64Type, bytePtr, "thread.arg");
#endif
                llvm::Type* expectedType =
                    targetFunc->getFunctionType()->getParamType(i);
                llvm::Value* callArg = argVal;
                if(expectedType->isIntegerTy())
                {
                    if(expectedType != int64Type)
                    {
                        unsigned srcBits = int64Type->getIntegerBitWidth();
                        unsigned dstBits = expectedType->getIntegerBitWidth();
                        if(srcBits > dstBits)
                            callArg = builder.CreateTrunc(callArg, expectedType,
                                                          "thread.trunc");
                        else if(srcBits < dstBits)
                            callArg = builder.CreateSExt(callArg, expectedType,
                                                         "thread.sext");
                    }
                    callArgs.push_back(callArg);
                }
                else if(expectedType->isStructTy())
                {
                    auto* structType =
                        llvm::cast<llvm::StructType>(expectedType);
                    std::string structName = structType->getName().str();
                    llvm::Value* handleVal =
                        buildHandleValue(structName, callArg, node->line);
                    if(!handleVal)
                        return nullptr;
                    callArgs.push_back(handleVal);
                }
                else
                {
                    reportError(node->line,
                                "thread_spawn arg type unsupported");
                    return nullptr;
                }
            }
            builder.CreateCall(targetFunc, callArgs);
            builder.CreateCall(freeFunc, {wrapperFunc->getArg(0)});
        }
        else
        {
            builder.CreateCall(targetFunc, {});
        }

        llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
            llvm::cast<llvm::PointerType>(ptrType)
#else
            llvm::cast<llvm::PointerType>(ptrType)
#endif
        );
        builder.CreateRet(nullPtr);
        builder.restoreIP(savedIP);
    }

    llvm::AllocaInst* threadHandle =
        builder.CreateAlloca(ptrType, nullptr, "thread.handle");
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
        llvm::cast<llvm::PointerType>(ptrType)
#else
        llvm::cast<llvm::PointerType>(ptrType)
#endif
    );
    llvm::Value* wrapperPtr =
        builder.CreateBitCast(wrapperFunc, ptrType, "thread.wrapper");
    llvm::Value* argPtr = nullPtr;
    if(argCount > 0)
    {
        llvm::Value* sizeVal = llvm::ConstantInt::get(
            int64Type, static_cast<uint64_t>(argCount * 8), false);
        argPtr = builder.CreateCall(mallocFunc, {sizeVal}, "thread.argptr");
        for(size_t i = 0; i < argCount; ++i)
        {
            llvm::Value* rawArg = generateExpression(node->arguments[i + 1]);
            if(!rawArg)
                return nullptr;
            if(rawArg->getType()->isIntegerTy())
            {
                if(rawArg->getType() != int64Type)
                {
                    rawArg =
                        builder.CreateSExt(rawArg, int64Type, "thread.argsext");
                }
            }
            else if(rawArg->getType()->isStructTy())
            {
                auto* structType =
                    llvm::cast<llvm::StructType>(rawArg->getType());
                std::string structName = structType->getName().str();
                auto memIt = structMembers.find(structName);
                int rawIndex = -1;
                if(memIt != structMembers.end())
                {
                    for(size_t m = 0; m < memIt->second.size(); ++m)
                    {
                        if(memIt->second[m].first == "raw")
                        {
                            rawIndex = static_cast<int>(m);
                            break;
                        }
                    }
                }
                if(rawIndex < 0)
                {
                    reportError(node->line,
                                "thread_spawn handle arg missing raw field");
                    return nullptr;
                }
                rawArg = builder.CreateExtractValue(rawArg, rawIndex,
                                                    "thread.handle.raw");
                if(rawArg->getType() != int64Type)
                {
                    rawArg =
                        builder.CreateSExt(rawArg, int64Type, "thread.argsext");
                }
            }
            else
            {
                reportError(node->line,
                            "thread_spawn arguments must be integer or handle");
                return nullptr;
            }
            llvm::Value* offset = llvm::ConstantInt::get(
                int64Type, static_cast<uint64_t>(i * 8), false);
            llvm::Value* bytePtr =
                builder.CreateGEP(llvm::Type::getInt8Ty(context), argPtr,
                                  offset, "thread.argbyte");
#if LLVM_VERSION_MAJOR < 15
            llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
            llvm::Value* typedPtr = builder.CreateBitCast(bytePtr, int64PtrType,
                                                          "thread.argptr_i64");
            builder.CreateStore(rawArg, typedPtr);
#else
            builder.CreateStore(rawArg, bytePtr);
#endif
        }
    }

    builder.CreateCall(pthreadCreateFunc,
                       {threadHandle, nullPtr, wrapperPtr, argPtr});

    llvm::Value* threadVal =
        builder.CreateLoad(ptrType, threadHandle, "thread.value");
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(threadVal, int64Type, "thread.handle_i64");

    return buildHandleValue("thread", rawHandle, node->line);
}

llvm::Value* CodeGenerator::generateThreadJoin(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "thread_join expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* threadPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "thread.ptr");
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
        llvm::cast<llvm::PointerType>(ptrType)
#else
        llvm::cast<llvm::PointerType>(ptrType)
#endif
    );

    return builder.CreateCall(pthreadJoinFunc, {threadPtr, nullPtr},
                              "thread.join");
}

llvm::Value* CodeGenerator::buildHandleValue(const std::string& handleTypeName,
                                             llvm::Value* rawHandle, int line)
{
    if(!rawHandle)
        return nullptr;

    llvm::StructType* handleType = getStructType(handleTypeName);
    if(!handleType)
    {
        reportError(line, "unknown handle type: " + handleTypeName);
        return nullptr;
    }

    auto memIt = structMembers.find(handleTypeName);
    if(memIt == structMembers.end())
    {
        reportError(line, "unknown handle struct members: " + handleTypeName);
        return nullptr;
    }

    int rawIndex = -1;
    for(size_t i = 0; i < memIt->second.size(); ++i)
    {
        if(memIt->second[i].first == "raw")
        {
            rawIndex = static_cast<int>(i);
            break;
        }
    }

    if(rawIndex < 0)
    {
        reportError(line, "handle type missing raw field: " + handleTypeName);
        return nullptr;
    }

    llvm::Type* expectedType = handleType->getElementType(rawIndex);
    llvm::Value* rawVal = rawHandle;
    if(rawVal->getType() != expectedType)
    {
        if(rawVal->getType()->isIntegerTy() && expectedType->isIntegerTy())
        {
            unsigned srcBits = rawVal->getType()->getIntegerBitWidth();
            unsigned dstBits = expectedType->getIntegerBitWidth();
            if(srcBits > dstBits)
                rawVal =
                    builder.CreateTrunc(rawVal, expectedType, "handle.trunc");
            else if(srcBits < dstBits)
                rawVal =
                    builder.CreateSExt(rawVal, expectedType, "handle.sext");
        }
        else
        {
            reportError(line, "handle raw type mismatch");
            return nullptr;
        }
    }

    llvm::Value* handleVal = llvm::Constant::getNullValue(handleType);
    return builder.CreateInsertValue(
        handleVal, rawVal, static_cast<unsigned>(rawIndex), "handle.raw");
}

llvm::Value* CodeGenerator::extractHandleValue(
    ExpressionNode* expr, const std::string& expectedHandleType, int line)
{
    llvm::Value* val = generateExpression(expr);
    if(!val)
        return nullptr;

    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    llvm::Type* valType = val->getType();

    if(valType->isIntegerTy())
    {
        if(valType != int64Type)
            val = builder.CreateSExt(val, int64Type, "handle.sext");
        return val;
    }

    if(valType->isStructTy())
    {
        auto* structType = llvm::cast<llvm::StructType>(valType);
        std::string structName = structType->getName().str();
        if(!expectedHandleType.empty() && structName != expectedHandleType)
        {
            reportError(line, "handle type mismatch: expected " +
                                  expectedHandleType + ", got " + structName);
            return nullptr;
        }

        auto memIt = structMembers.find(structName);
        if(memIt == structMembers.end())
        {
            reportError(line, "unknown handle struct members: " + structName);
            return nullptr;
        }
        int rawIndex = -1;
        for(size_t i = 0; i < memIt->second.size(); ++i)
        {
            if(memIt->second[i].first == "raw")
            {
                rawIndex = static_cast<int>(i);
                break;
            }
        }
        if(rawIndex < 0)
        {
            reportError(line, "handle type missing raw field: " + structName);
            return nullptr;
        }
        llvm::Value* rawVal =
            builder.CreateExtractValue(val, rawIndex, "handle.raw");
        if(rawVal->getType() != int64Type)
            rawVal = builder.CreateSExt(rawVal, int64Type, "handle.sext");
        return rawVal;
    }

    reportError(line, "expected handle or integer value");
    return nullptr;
}

llvm::Value* CodeGenerator::generateMutexCreate(FunctionCallNode* node)
{
    if(!node->arguments.empty())
    {
        reportError(node->line, "mutex_create expects no arguments");
        return nullptr;
    }

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* sizeVal = llvm::ConstantInt::get(int64Type, 64, false);
    llvm::Value* mem = builder.CreateCall(mallocFunc, {sizeVal}, "mutex.mem");
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
        llvm::cast<llvm::PointerType>(ptrType)
#else
        llvm::cast<llvm::PointerType>(ptrType)
#endif
    );
    builder.CreateCall(pthreadMutexInitFunc, {mem, nullPtr});
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(mem, int64Type, "mutex.handle_i64");
    return rawHandle;
}

llvm::Value*
CodeGenerator::createInternalMutexHandle(bool recursive,
                                         const std::string& namePrefix)
{
    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int32Type = llvm::Type::getInt32Ty(context);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* mutexSize = llvm::ConstantInt::get(int64Type, 64, false);
    llvm::Value* mutexMem =
        builder.CreateCall(mallocFunc, {mutexSize}, namePrefix + ".mutex.mem");
    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType));

    if(recursive)
    {
        llvm::Value* attrSize = llvm::ConstantInt::get(
            int64Type, static_cast<uint64_t>(sizeof(pthread_mutexattr_t)),
            false);
        llvm::Value* attrMem = builder.CreateCall(mallocFunc, {attrSize},
                                                  namePrefix + ".attr.mem");
        builder.CreateCall(pthreadMutexAttrInitFunc, {attrMem});
        builder.CreateCall(
            pthreadMutexAttrSetTypeFunc,
            {attrMem,
             llvm::ConstantInt::get(
                 int32Type, static_cast<uint64_t>(PTHREAD_MUTEX_RECURSIVE),
                 false)});
        builder.CreateCall(pthreadMutexInitFunc, {mutexMem, attrMem});
        builder.CreateCall(pthreadMutexAttrDestroyFunc, {attrMem});
        builder.CreateCall(freeFunc, {attrMem});
    }
    else
    {
        builder.CreateCall(pthreadMutexInitFunc, {mutexMem, nullPtr});
    }

    return builder.CreatePtrToInt(mutexMem, int64Type, namePrefix + ".handle");
}

void CodeGenerator::destroyInternalMutexHandle(llvm::Value* rawHandle,
                                               const std::string& namePrefix)
{
    if(!rawHandle)
        return;

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(rawHandle, ptrType, namePrefix + ".ptr");
    builder.CreateCall(pthreadMutexDestroyFunc, {mutexPtr});
    builder.CreateCall(freeFunc, {mutexPtr});
}

llvm::Value* CodeGenerator::ensurePropertyMutexHandle(
    llvm::Value* handleSlotPtr, bool recursive, const std::string& namePrefix)
{
    if(!handleSlotPtr)
        return nullptr;

    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    auto* currentHandle =
        builder.CreateLoad(int64Type, handleSlotPtr, namePrefix + ".cur");
    currentHandle->setAlignment(llvm::Align(8));
    currentHandle->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);

    llvm::Function* curFn = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* entryBB = builder.GetInsertBlock();
    llvm::BasicBlock* initBB =
        llvm::BasicBlock::Create(context, namePrefix + ".init", curFn);
    llvm::BasicBlock* doneBB =
        llvm::BasicBlock::Create(context, namePrefix + ".done", curFn);
    llvm::Value* hasHandle = builder.CreateICmpNE(
        currentHandle, llvm::ConstantInt::get(int64Type, 0),
        namePrefix + ".has_handle");
    builder.CreateCondBr(hasHandle, doneBB, initBB);

    builder.SetInsertPoint(initBB);
    llvm::Value* candidate =
        createInternalMutexHandle(recursive, namePrefix + ".new");
    auto* cmpxchg = builder.CreateAtomicCmpXchg(
        handleSlotPtr, llvm::ConstantInt::get(int64Type, 0), candidate,
        llvm::Align(8), llvm::AtomicOrdering::SequentiallyConsistent,
        llvm::AtomicOrdering::SequentiallyConsistent);
    cmpxchg->setWeak(false);
    llvm::Value* installed =
        builder.CreateExtractValue(cmpxchg, 0, namePrefix + ".installed");
    llvm::Value* success =
        builder.CreateExtractValue(cmpxchg, 1, namePrefix + ".success");
    llvm::BasicBlock* winnerBB =
        llvm::BasicBlock::Create(context, namePrefix + ".winner", curFn);
    llvm::BasicBlock* loserBB =
        llvm::BasicBlock::Create(context, namePrefix + ".loser", curFn);
    builder.CreateCondBr(success, winnerBB, loserBB);

    builder.SetInsertPoint(winnerBB);
    builder.CreateBr(doneBB);

    builder.SetInsertPoint(loserBB);
    destroyInternalMutexHandle(candidate, namePrefix + ".discard");
    builder.CreateBr(doneBB);

    builder.SetInsertPoint(doneBB);
    auto* phi = builder.CreatePHI(int64Type, 3, namePrefix + ".handle");
    phi->addIncoming(currentHandle, entryBB);
    phi->addIncoming(candidate, winnerBB);
    phi->addIncoming(installed, loserBB);
    return phi;
}

llvm::Value* CodeGenerator::generateMutexLock(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "mutex_lock expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    return builder.CreateCall(pthreadMutexLockFunc, {mutexPtr}, "mutex.lock");
}

llvm::Value* CodeGenerator::generateMutexUnlock(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "mutex_unlock expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    return builder.CreateCall(pthreadMutexUnlockFunc, {mutexPtr},
                              "mutex.unlock");
}

llvm::Value* CodeGenerator::generateMutexDestroy(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "mutex_destroy expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    llvm::Value* result = builder.CreateCall(pthreadMutexDestroyFunc,
                                             {mutexPtr}, "mutex.destroy");
    builder.CreateCall(freeFunc, {mutexPtr});
    return result;
}

llvm::Value* CodeGenerator::generateAtomicI64New(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "atomic_i64_new expects one argument");
        return nullptr;
    }

    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* initVal = generateExpression(node->arguments[0]);
    if(!initVal)
        return nullptr;
    if(!initVal->getType()->isIntegerTy())
    {
        reportError(node->line, "atomic_i64_new expects integer value");
        return nullptr;
    }
    if(initVal->getType() != int64Type)
        initVal = builder.CreateSExt(initVal, int64Type, "atomic.sext");

    llvm::Value* sizeVal = llvm::ConstantInt::get(int64Type, 8, false);
    llvm::Value* mem = builder.CreateCall(mallocFunc, {sizeVal}, "atomic.mem");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
    llvm::Value* typedPtr =
        builder.CreateBitCast(mem, int64PtrType, "atomic.ptr");
    builder.CreateStore(initVal, typedPtr);
#else
    builder.CreateStore(initVal, mem);
#endif
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(mem, int64Type, "atomic.handle_i64");
    return rawHandle;
}

llvm::Value* CodeGenerator::generateAtomicI64Load(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "atomic_i64_load expects one argument");
        return nullptr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
    ptr = builder.CreateBitCast(ptr, int64PtrType, "atomic.ptr_i64");
#endif
    auto* loadInst = builder.CreateLoad(int64Type, ptr, "atomic.load");
    // Natural alignment keeps this a lock-free instruction. Without it LLVM
    // expands the access to __atomic_load, which needs libatomic on Linux.
    loadInst->setAlignment(llvm::Align(8));
    loadInst->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
    return loadInst;
}

llvm::Value* CodeGenerator::generateAtomicI64Store(FunctionCallNode* node)
{
    if(node->arguments.size() != 2)
    {
        reportError(node->line, "atomic_i64_store expects two arguments");
        return nullptr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    llvm::Value* valueVal = generateExpression(node->arguments[1]);
    if(!handleVal || !valueVal)
        return nullptr;
    if(valueVal->getType() != int64Type)
        valueVal = builder.CreateSExt(valueVal, int64Type, "atomic.sextval");

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
    ptr = builder.CreateBitCast(ptr, int64PtrType, "atomic.ptr_i64");
#endif
    auto* storeInst = builder.CreateStore(valueVal, ptr);
    storeInst->setAlignment(llvm::Align(8));
    storeInst->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
    return valueVal;
}

llvm::Value* CodeGenerator::generateAtomicI64Add(FunctionCallNode* node)
{
    if(node->arguments.size() != 2)
    {
        reportError(node->line, "atomic_i64_add expects two arguments");
        return nullptr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    llvm::Value* addVal = generateExpression(node->arguments[1]);
    if(!handleVal || !addVal)
        return nullptr;
    if(addVal->getType() != int64Type)
        addVal = builder.CreateSExt(addVal, int64Type, "atomic.sextval");

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
    ptr = builder.CreateBitCast(ptr, int64PtrType, "atomic.ptr_i64");
#endif
    return builder.CreateAtomicRMW(
        llvm::AtomicRMWInst::Add, ptr, addVal, llvm::Align(8),
        llvm::AtomicOrdering::SequentiallyConsistent);
}

llvm::Value* CodeGenerator::generateAtomicI64Free(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "atomic_i64_free expects one argument");
        return nullptr;
    }

    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::string freeOwner = resolveBorrowOwnerFromLValue(node->arguments[0]);
    if(!freeOwner.empty() &&
       globalNamedValues.find(freeOwner) == globalNamedValues.end() &&
       isVariableMoved(freeOwner))
    {
        reportError(node->line, "double free or use-after-free of value: '" +
                                    freeOwner + "'");
        return nullptr;
    }
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
    if(!freeOwner.empty() &&
       globalNamedValues.find(freeOwner) == globalNamedValues.end())
    {
        movedVariables.insert(freeOwner);
    }
    return builder.CreateCall(freeFunc, {ptr});
}
