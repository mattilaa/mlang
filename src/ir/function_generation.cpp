#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"
#include "llvm_compat.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Verifier.h>

using mlang::ir_detail::ast_analysis::contains_exception_control_flow;
using mlang::ir_detail::common::Helpers;

llvm::Function* CodeGenerator::generateFunctionDeclaration(
    FunctionDefNode* node)
{
    std::string symbolName = functionSymbolName(node);
    std::vector<llvm::Type*> paramTypes;
    for(auto param : node->parameters->parameters)
    {
        // `&mut T` passes the caller's storage (copy-in/copy-out, see
        // generateFunction); `&T` passes the value.
        llvm::Type* paramType = parameterLLVMType(param->type);
        if(!paramType)
        {
            reportError(
                param->line,
                "unknown type: " + Helpers::type_name_for_error(param->type));
            paramType = llvm::Type::getInt32Ty(context); // fallback
        }
        paramTypes.push_back(paramType);
    }

    // Shared reference return annotations retain the existing value ABI.
    llvm::Type* returnType = parameterLLVMType(node->returnType);
    if(!returnType)
    {
        reportError(
            node->line,
            "unknown type: " + Helpers::type_name_for_error(node->returnType));
        returnType = llvm::Type::getVoidTy(context); // fallback
    }

    // Check if function already declared
    if(llvm::Function* existing = module->getFunction(symbolName))
    {
        if(existing->arg_size() != paramTypes.size() ||
           existing->getReturnType() != returnType ||
           existing->isVarArg() != node->parameters->isVarArg)
        {
            reportError(
                node->line,
                "conflicting declaration for function '" + node->name + "'");
        }
        return existing;
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(
        returnType,
        paramTypes,
        node->parameters->isVarArg);
    llvm::Function* function = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        symbolName,
        module.get());

    // Set parameter names
    unsigned idx = 0;
    for(auto& arg : function->args())
    {
        arg.setName(node->parameters->parameters[idx++]->name);
    }

    return function;
}

llvm::Type* CodeGenerator::parameterLLVMType(TypeNode* type)
{
    if(auto* ref = dynamic_cast<ReferenceTypeNode*>(type))
        return ref->isMutable ? llvm::PointerType::get(context, 0)
                              : getLLVMTypeFromNode(ref->elementType);
    return getLLVMTypeFromNode(type);
}

// Before a return: store each `&mut` parameter's local copy back through
// the caller's pointer.
void CodeGenerator::emitInoutWriteBack()
{
    llvm::BasicBlock* block = builder.GetInsertBlock();
    if(!block || block->getParent() != inoutFunction)
        return;
    for(const auto& inout : inoutParams)
    {
        auto it = namedValues.find(inout.second);
        if(it == namedValues.end())
            continue;
        if(auto* local = llvm::dyn_cast<llvm::AllocaInst>(it->second))
        {
            llvm::Value* value = builder.CreateLoad(
                local->getAllocatedType(),
                local,
                inout.second + ".out");
            builder.CreateStore(value, inout.first);
        }
    }
}

void CodeGenerator::seedFunctionScopeWithGlobals()
{
    for(const auto& it : globalNamedValues)
        namedValues[it.first] = it.second;
    for(const auto& it : globalVariableTypes)
        variableTypes[it.first] = it.second;
    for(const auto& item : globalSemanticTypes)
    {
        if(auto* pointer = dynamic_cast<PointerTypeNode*>(item.second))
            pointerElementTypes[item.first] = pointer->elementType;
        if(auto* array = dynamic_cast<ArrayTypeNode*>(item.second))
        {
            listElementTypes[item.first] = array->elementType;
            arrayCapacities[item.first] = array->capacity;
        }
    }
    for(const auto& it : globalStructVariableTypes)
        structVariableTypes[it.first] = it.second;
    for(const auto& n : globalConstantVariables)
        constantVariables.insert(n);
}

llvm::Constant* CodeGenerator::buildGlobalInitializer(
    ExpressionNode* expression,
    TypeNode* type,
    int line)
{
    llvm::Type* target = getLLVMTypeFromNode(type);
    if(!target)
        return nullptr;
    auto fail = [&]() -> llvm::Constant*
    {
        reportError(
            line,
            "global initializer must be a compatible constant expression");
        return nullptr;
    };
    if(auto* fixed = dynamic_cast<FixedArrayTypeNode*>(type))
    {
        auto* array = llvm::cast<llvm::ArrayType>(target);
        std::vector<llvm::Constant*> elements;
        auto* literal = dynamic_cast<ListLiteralNode*>(expression);
        auto* fill = dynamic_cast<ArrayFillNode*>(expression);
        int64_t count = literal && literal->elements
                            ? literal->elements->elements.size()
                            : 0;
        if(fill && !evaluateCompileTimeInt(fill->count, count))
            return fail();
        if((expression && !literal && !fill) || count < 0 ||
           count > fixed->capacity)
            return fail();
        for(int64_t i = 0; i < fixed->capacity; ++i)
        {
            ExpressionNode* value =
                i < count
                    ? (fill ? fill->value : literal->elements->elements[i])
                    : nullptr;
            auto* constant =
                buildGlobalInitializer(value, fixed->elementType, line);
            if(!constant)
                return nullptr;
            elements.push_back(constant);
        }
        return llvm::ConstantArray::get(array, elements);
    }
    if(auto* structure = llvm::dyn_cast<llvm::StructType>(target))
    {
        if(!structure->hasName())
            return expression ? fail() : llvm::Constant::getNullValue(target);
        const std::string name = structure->getName().str();
        auto members = structMembers.find(name);
        if(members == structMembers.end())
            return fail();
        auto* literal = dynamic_cast<StructLiteralNode*>(expression);
        if(expression && !literal)
            return fail();
        if(literal && !literal->structName.empty())
        {
            TypeNode* literalType = new StructTypeRefNode(literal->structName);
            if(!literal->typeArgs.empty())
            {
                auto* generic =
                    new GenericStructTypeRefNode(literal->structName);
                for(const auto& argument : literal->typeArgs)
                    generic->typeArgs.push_back(
                        Helpers::type_from_text(argument));
                literalType = generic;
            }
            if(getLLVMTypeFromNode(literalType) != target)
                return fail();
        }
        std::map<std::string, ExpressionNode*> explicitFields;
        if(literal)
        {
            for(const auto& field : literal->fields)
            {
                if(!explicitFields.emplace(field.first, field.second).second)
                {
                    reportError(
                        line,
                        "duplicate global initializer field '" + field.first +
                            "'");
                    return nullptr;
                }
            }
        }
        std::vector<llvm::Constant*> fields;
        for(llvm::Type* fieldType : structure->elements())
            fields.push_back(llvm::Constant::getNullValue(fieldType));
        for(size_t i = 0; i < members->second.size(); ++i)
        {
            const auto& member = members->second[i];
            ExpressionNode* value = nullptr;
            auto supplied = explicitFields.find(member.first);
            if(supplied != explicitFields.end())
            {
                if(!canAccessStructField(
                       name,
                       static_cast<int>(i),
                       line,
                       member.first))
                    return nullptr;
                value = supplied->second;
                explicitFields.erase(supplied);
            }
            else
            {
                auto defaults = structMemberDefaults.find(name);
                if(defaults != structMemberDefaults.end())
                {
                    auto defaultValue = defaults->second.find(member.first);
                    if(defaultValue != defaults->second.end())
                        value = defaultValue->second;
                }
            }
            llvm::Constant* field =
                buildGlobalInitializer(value, member.second, line);
            const auto* layout =
                getStructFieldLayout(name, static_cast<int>(i));
            if(!field || !layout)
                return nullptr;
            if(layout->packedBit)
            {
                auto* integer = llvm::dyn_cast<llvm::ConstantInt>(field);
                auto* previous = llvm::dyn_cast<llvm::ConstantInt>(
                    fields[layout->storageIndex]);
                if(!integer || !previous)
                    return fail();
                fields[layout->storageIndex] = llvm::ConstantInt::get(
                    previous->getType(),
                    previous->getZExtValue() |
                        ((integer->isZero() ? 0ULL : 1ULL)
                         << layout->bitOffset));
            }
            else
                fields[layout->storageIndex] = field;
        }
        if(!explicitFields.empty())
        {
            reportError(
                line,
                "unknown global initializer field '" +
                    explicitFields.begin()->first + "'");
            return nullptr;
        }
        return llvm::ConstantStruct::get(structure, fields);
    }
    if(!expression)
        return llvm::Constant::getNullValue(target);
    if(auto* string = dynamic_cast<StringLiteralNode*>(expression))
    {
        if(!target->isPointerTy() || (type->kind != TypeNode::TYPE_STRING &&
                                      type->kind != TypeNode::TYPE_STR8))
            return fail();
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString(
            string->value,
            "global.string",
            0,
            module.get());
#else
        return builder.CreateGlobalStringPtr(
            string->value,
            "global.string",
            0,
            module.get());
#endif
    }
    if(auto* address = dynamic_cast<UnaryOpNode*>(expression))
    {
        if(target->isPointerTy() && (address->op == UnaryOpNode::OP_ADDR ||
                                     address->op == UnaryOpNode::OP_ADDR_MUT))
        {
            if(auto* id = dynamic_cast<IdentifierNode*>(address->operand))
            {
                auto global = globalNamedValues.find(id->name);
                if(global != globalNamedValues.end())
                {
                    auto* pointer = dynamic_cast<PointerTypeNode*>(type);
                    auto* storage =
                        llvm::dyn_cast<llvm::GlobalVariable>(global->second);
                    if(pointer && storage &&
                       storage->getValueType() ==
                           getLLVMTypeFromNode(pointer->elementType))
                        return llvm::ConstantExpr::getPointerCast(
                            storage,
                            llvm::cast<llvm::PointerType>(target));
                }
            }
            return fail();
        }
    }
    if(target->isFloatingPointTy())
    {
        if(auto* number = dynamic_cast<FloatLiteralNode*>(expression))
            return llvm::ConstantFP::get(target, number->value);
        if(auto* number = dynamic_cast<DoubleLiteralNode*>(expression))
            return llvm::ConstantFP::get(target, number->value);
        if(auto* negative = dynamic_cast<UnaryOpNode*>(expression))
        {
            if(negative->op == UnaryOpNode::OP_NEG)
            {
                auto* operand =
                    buildGlobalInitializer(negative->operand, type, line);
                if(auto* number =
                       llvm::dyn_cast_or_null<llvm::ConstantFP>(operand))
                {
                    llvm::APFloat value = number->getValueAPF();
                    value.changeSign();
                    return llvm::ConstantFP::get(context, value);
                }
                return nullptr;
            }
        }
    }
    if(target->isIntegerTy())
    {
        int64_t value = 0;
        if(evaluateCompileTimeInt(expression, value))
            return llvm::ConstantInt::get(
                target,
                value,
                !isUnsignedType(type->kind));
    }
    return fail();
}

void CodeGenerator::generateGlobalVarDeclaration(
    VarDeclNode* node,
    bool declarationOnly)
{
    if(!node)
        return;
    if(globalNamedValues.find(node->name) != globalNamedValues.end())
    {
        reportError(
            node->line,
            "duplicate global variable: '" + node->name + "'");
        return;
    }

    TypeNode::TypeKind kind = TypeNode::TYPE_INT;
    if(node->type)
        kind = node->type->kind;
    else if(node->initExpr)
    {
        if(auto* literal = dynamic_cast<StructLiteralNode*>(node->initExpr))
        {
            if(literal->typeArgs.empty())
                node->type = new StructTypeRefNode(literal->structName);
            else
            {
                auto* generic =
                    new GenericStructTypeRefNode(literal->structName);
                for(const auto& argument : literal->typeArgs)
                    generic->typeArgs.push_back(
                        Helpers::type_from_text(argument));
                node->type = generic;
            }
            kind = TypeNode::TYPE_STRUCT;
        }
        else if(dynamic_cast<BoolLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_BOOL;
        else if(dynamic_cast<FloatLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_FLOAT;
        else if(dynamic_cast<DoubleLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_DOUBLE;
        else if(dynamic_cast<StringLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_STRING;
    }

    llvm::Type* llvmTy =
        node->type ? getLLVMTypeFromNode(node->type) : getLLVMType(kind);
    if(!llvmTy)
    {
        reportError(
            node->line,
            "invalid global variable type for '" + node->name + "'");
        return;
    }

    llvm::Constant* init =
        declarationOnly ? llvm::Constant::getNullValue(llvmTy)
                        : buildGlobalInitializer(
                              node->initExpr,
                              node->type ? node->type : new TypeNode(kind),
                              node->line);
    if(!init)
        return;

    auto* gv = new llvm::GlobalVariable(
        *module,
        llvmTy,
        false,
        llvm::GlobalValue::InternalLinkage,
        init,
        "__mlang_global_" + node->name);
    if(node->storageAlignment)
    {
        if(node->storageAlignment & (node->storageAlignment - 1))
        {
            reportError(node->line, "storage alignment must be a power of two");
            return;
        }
        gv->setAlignment(
            llvm::Align(
                std::max<uint64_t>(
                    module->getDataLayout().getABITypeAlign(llvmTy).value(),
                    node->storageAlignment)));
    }
    globalNamedValues[node->name] = gv;
    globalVariableTypes[node->name] = kind;
    globalSemanticTypes[node->name] =
        node->type ? cloneTypeNode(node->type) : new TypeNode(kind);
    if(auto* structure = llvm::dyn_cast<llvm::StructType>(llvmTy))
    {
        if(structure->hasName())
        {
            globalVariableTypes[node->name] = TypeNode::TYPE_STRUCT;
            globalStructVariableTypes[node->name] = structure->getName().str();
        }
    }
}

llvm::Function* CodeGenerator::generateFunctionDefinition(FunctionDefNode* node)
{
    std::string symbolName = functionSymbolName(node);
    // Get the function (should already be declared)
    llvm::Function* function = module->getFunction(symbolName);
    if(!function)
    {
        // If not declared yet, declare it now
        function = generateFunctionDeclaration(node);
    }

    if((structuralRequirementProbe && node != structuralProbeFunction) ||
       node->isExtern || !node->body)
    {
        return function;
    }

    // Check if function already has a body (was already defined)
    if(!function->empty())
    {
        // Function already defined, skip
        return function;
    }

    // Apply inline attributes
    if(node->isInlineAlways)
        function->addFnAttr(llvm::Attribute::AlwaysInline);
    else if(node->isInlineNever)
        function->addFnAttr(llvm::Attribute::NoInline);
    else if(node->isInline)
        function->addFnAttr(llvm::Attribute::InlineHint);

    if(contains_exception_control_flow(node->body))
    {
        function->addFnAttr(llvm::Attribute::OptimizeNone);
        function->addFnAttr(llvm::Attribute::NoInline);
    }

    // Track which module this function is from (for visibility checks)
    std::string savedModule = currentModule;
    currentModule = node->sourceModule;
    auto savedIP = builder.saveIP();
    auto savedNamedValues = namedValues;
    auto savedConstantVariables = constantVariables;
    auto savedImmutableReferenceVariables = immutableReferenceVariables;
    auto savedConstexprValues = constexprValues;
    auto savedPackExpansions = activePackExpansions;
    auto savedFunctionTypeBindings = activeFunctionTypeBindings;
    auto savedInoutParams = inoutParams;
    auto* savedInoutFunction = inoutFunction;
    auto savedMovedVariables = movedVariables;
    auto savedPointerBorrowTarget = pointerBorrowTarget;
    auto savedPointerBorrowParent = pointerBorrowParent;
    auto savedAggregateBorrowOrigins = aggregateBorrowOrigins;
    auto savedAggregateBorrowPlaces = aggregateBorrowPlaces;
    auto savedAggregateBorrowFieldPlaces = aggregateBorrowFieldPlaces;
    auto savedPointerBorrowPlace = pointerBorrowPlace;
    auto savedActiveBorrowers = activeBorrowers;
    auto savedActiveMutBorrower = activeMutBorrower;
    auto savedVariableScopeDepth = variableScopeDepth;
    auto savedVariableTypes = variableTypes;
    auto savedStructVariableTypes = structVariableTypes;
    auto savedTraitObjectVariableTypes = traitObjectVariableTypes;
    auto savedEnumVariableTypes = enumVariableTypes;
    auto savedListElementTypes = listElementTypes;
    auto savedArrayCapacities = arrayCapacities;
    auto savedArrayKnownLengths = arrayKnownLengths;
    auto savedMultiarrayMutability = multiarrayMutability;
    auto savedMapKeyValueTypes = mapKeyValueTypes;
    auto savedTupleElementTypes = tupleElementTypes;
    auto savedPointerElementTypes = pointerElementTypes;
    auto savedPointerKnownNull = pointerKnownNull;
    auto savedCleanupScopes = cleanupScopes;
    auto savedPointerBorrowScopes = pointerBorrowScopes;
    auto savedVariableScopeDepthScopes = variableScopeDepthScopes;
    auto savedClosureVariables = closureVariables;
    auto savedRuntimeClosureVariables = runtimeClosureVariables;
    auto savedRuntimeClosureReturnTypes = runtimeClosureReturnTypes;
    auto savedFunctionTypeVariables = functionTypeVariables;
    auto savedFunctionItemVariables = functionItemVariables;
    auto savedFunctionTypeParameters = functionTypeParameters;
    auto savedPointerParameterBorrowOrigins = pointerParameterBorrowOrigins;
    auto savedRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;
    auto savedClosureCaptureAliases = closureCaptureAliases;
    auto savedClosureCaptureTypes = closureCaptureTypes;
    auto savedClosureConstCaptures = closureConstCaptures;
    auto savedClosureCaptureReferenceAliases = closureCaptureReferenceAliases;
    auto savedActiveInlineClosures = activeInlineClosures;
    auto savedInlineClosureReturnStates = inlineClosureReturnStates;
    auto savedCurrentFunctionExceptionFrame = currentFunctionExceptionFrame;
    auto savedSemanticReturnType = currentSemanticReturnType;
    int savedUnsafeDepth = unsafeDepth;
    currentSemanticReturnType = node->returnType;
    activePackExpansions = node->packExpansionNames;
    activeFunctionTypeBindings = node->concreteTypeBindings;

    // Create a new basic block for the function
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);
    // mlang -g: the function's DISubprogram while its body is generated.
    DebugFunctionScope debugScope(*this, function, node, node->name);

    // Clear the named values map and constant tracking for new function scope
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
    closureVariables = node->boundClosureParameters;
    closureCaptureAliases = node->boundClosureCaptureAliases;
    closureCaptureTypes = node->boundClosureCaptureTypes;
    closureConstCaptures = node->boundClosureConstCaptures;
    closureCaptureReferenceAliases.clear();
    activeInlineClosures.clear();
    inlineClosureReturnStates.clear();
    pointerBorrowTarget.clear();
    pointerBorrowParent.clear();
    aggregateBorrowOrigins.clear();
    aggregateBorrowPlaces.clear();
    aggregateBorrowFieldPlaces.clear();
    pointerBorrowPlace.clear();
    pointerKnownNull.clear();
    activeBorrowers.clear();
    activeMutBorrower.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    enumVariableTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    multiarrayMutability.clear();
    unsafeDepth = 0;
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    currentFunctionExceptionFrame = nullptr;
    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    initializeStdlibFunctions();
    llvm::BasicBlock* functionBodyBB =
        llvm::BasicBlock::Create(context, "fn.body", function);
    llvm::BasicBlock* functionExceptionBB =
        llvm::BasicBlock::Create(context, "fn.exc", function);
    currentFunctionExceptionFrame =
        builder.CreateCall(exceptionsPushFrameFunc, {}, "fn.exc.frame");
    llvm::Value* functionExceptionEnv = builder.CreateCall(
        exceptionsFrameEnvFunc,
        {currentFunctionExceptionFrame},
        "fn.exc.env");
    auto* functionSetjmpCall = builder.CreateCall(
        exceptionsSetjmpFunc,
        {functionExceptionEnv},
        "fn.exc.state");
    functionSetjmpCall->setCanReturnTwice();
    llvm::Value* functionExceptionState = functionSetjmpCall;
    llvm::Value* enteredNormally = builder.CreateICmpEQ(
        functionExceptionState,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
        "fn.exc.ok");
    builder.CreateCondBr(enteredNormally, functionBodyBB, functionExceptionBB);
    builder.SetInsertPoint(functionBodyBB);

    // Set up parameters
    unsigned paramIdx = 0;
    inoutParams.clear();
    inoutFunction = function;
    for(auto& arg : function->args())
    {
        // A `&mut T` parameter arrives as the caller's pointer: copy the
        // value in (shallow, it stays the caller's) and write it back before
        // every return, so the body works on a local like any parameter.
        ReferenceTypeNode* inoutType = nullptr;
        if(paramIdx < node->parameters->parameters.size())
            if(auto* ref = dynamic_cast<ReferenceTypeNode*>(
                   node->parameters->parameters[paramIdx]->type))
                if(ref->isMutable)
                    inoutType = ref;
        llvm::Type* localType =
            inoutType ? getLLVMTypeFromNode(inoutType->elementType)
                      : arg.getType();
        // Allocate space for parameters so they can be modified
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            localType,
            nullptr,
            std::string(arg.getName()) + ".addr");
        llvm::Value* paramValue = &arg;
        if(inoutType)
        {
            paramValue = builder.CreateLoad(
                localType,
                &arg,
                std::string(arg.getName()) + ".in");
            inoutParams.push_back({&arg, std::string(arg.getName())});
        }
        else if(arg.getType()->isStructTy() || arg.getType()->isArrayTy())
            paramValue = applyStructCopySemantics(paramValue);
        builder.CreateStore(paramValue, alloca);
        namedValues[std::string(arg.getName())] = alloca;
        recordVariableScopeDepth(std::string(arg.getName()));

        // Track parameter types
        if(paramIdx < node->parameters->parameters.size())
        {
            auto* paramNode = node->parameters->parameters[paramIdx];
            if(auto* refType =
                   dynamic_cast<ReferenceTypeNode*>(paramNode->type))
            {
                // Store inner element kind so the borrow checker treats the
                // param as the inner type (e.g. &str8 -> TYPE_STRING).
                variableTypes[std::string(arg.getName())] =
                    refType->elementType->kind;
                // Preserve container element typing for borrowed params so
                // indexing and methods work on &list<T> / &map<K,V>.
                if(auto* genListInner =
                       dynamic_cast<GenericListTypeNode*>(refType->elementType))
                {
                    listElementTypes[std::string(arg.getName())] =
                        genListInner->elementType;
                    if(auto* array = dynamic_cast<ArrayTypeNode*>(genListInner))
                        arrayCapacities[std::string(arg.getName())] =
                            array->capacity;
                }
                if(auto* mapInner =
                       dynamic_cast<MapTypeNode*>(refType->elementType))
                {
                    mapKeyValueTypes[std::string(arg.getName())] =
                        std::make_pair(mapInner->keyType, mapInner->valueType);
                }
                if(auto* structInner =
                       dynamic_cast<StructTypeRefNode*>(refType->elementType))
                {
                    std::string resolvedEnumName =
                        resolveVisibleEnumName(structInner->structName);
                    if(!resolvedEnumName.empty())
                    {
                        variableTypes[std::string(arg.getName())] =
                            TypeNode::TYPE_INT;
                        enumVariableTypes[std::string(arg.getName())] =
                            resolvedEnumName;
                    }
                    else
                    {
                        structVariableTypes[std::string(arg.getName())] =
                            structInner->structName;
                    }
                }
                if(auto* genStructInner =
                       dynamic_cast<GenericStructTypeRefNode*>(
                           refType->elementType))
                {
                    std::string mangled = getOrCreateMonomorphizedStruct(
                        genStructInner->structName,
                        genStructInner->typeArgs);
                    structVariableTypes[std::string(arg.getName())] = mangled;
                }
                if(auto* inner =
                       dynamic_cast<ReferenceTypeNode*>(refType->elementType))
                {
                    const std::string name(arg.getName());
                    pointerElementTypes[name] = inner->elementType;
                    pointerBorrowTarget[name] = name;
                    pointerBorrowPlace[name] = name;
                    pointerParameterBorrowOrigins.insert(name);
                    if(!inner->isMutable)
                        activeBorrowers[name].insert(name);
                }
                // Immutable reference: param may not be mutated inside body.
                if(!refType->isMutable)
                {
                    constantVariables.insert(std::string(arg.getName()));
                    immutableReferenceVariables.insert(
                        std::string(arg.getName()));
                }
                // Mutable reference: leave out of constantVariables.
            }
            else
            {
                variableTypes[std::string(arg.getName())] =
                    paramNode->type->kind;
            }
            if(auto* structType =
                   dynamic_cast<StructTypeRefNode*>(paramNode->type))
            {
                std::string resolvedEnumName =
                    resolveVisibleEnumName(structType->structName);
                if(!resolvedEnumName.empty())
                {
                    variableTypes[std::string(arg.getName())] =
                        TypeNode::TYPE_INT;
                    enumVariableTypes[std::string(arg.getName())] =
                        resolvedEnumName;
                }
                else
                {
                    structVariableTypes[std::string(arg.getName())] =
                        structType->structName;
                }
            }
            if(auto* genStructType =
                   dynamic_cast<GenericStructTypeRefNode*>(paramNode->type))
            {
                std::string mangled = getOrCreateMonomorphizedStruct(
                    genStructType->structName,
                    genStructType->typeArgs);
                structVariableTypes[std::string(arg.getName())] = mangled;
            }
            if(auto* genListType =
                   dynamic_cast<GenericListTypeNode*>(paramNode->type))
            {
                listElementTypes[std::string(arg.getName())] =
                    genListType->elementType;
                if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(genListType))
                    arrayCapacities[std::string(arg.getName())] =
                        arrayType->capacity;
                if(auto* multiarrayType =
                       dynamic_cast<MultiArrayTypeNode*>(genListType))
                    multiarrayMutability[std::string(arg.getName())] =
                        multiarrayType->elementsMutable;
            }
            if(auto* mapType = dynamic_cast<MapTypeNode*>(paramNode->type))
            {
                mapKeyValueTypes[std::string(arg.getName())] =
                    std::make_pair(mapType->keyType, mapType->valueType);
            }
            if(auto* ptrType = dynamic_cast<PointerTypeNode*>(paramNode->type))
            {
                pointerElementTypes[std::string(arg.getName())] =
                    ptrType->elementType;
                // Pointer parameters may originate from caller-owned stack
                // storage. Track the parameter as a local borrow origin so
                // global escapes remain rejected; direct returns are tied to
                // their argument at visible call sites.
                const std::string parameterName(arg.getName());
                pointerBorrowTarget[parameterName] = parameterName;
                pointerBorrowPlace[parameterName] = parameterName;
                pointerParameterBorrowOrigins.insert(parameterName);
            }
            if(auto* traitObjType =
                   dynamic_cast<TraitObjectTypeNode*>(paramNode->type))
            {
                variableTypes[std::string(arg.getName())] =
                    TypeNode::TYPE_TRAIT_OBJECT;
                traitObjectVariableTypes[std::string(arg.getName())] =
                    traitObjType->traitName;
            }
            if(auto* functionType =
                   dynamic_cast<FunctionTypeNode*>(paramNode->type))
            {
                functionTypeVariables[std::string(arg.getName())] =
                    functionType;
                functionTypeParameters.insert(std::string(arg.getName()));
            }
        }
        // mlang -g: the parameter, with its MLang type.
        if(paramIdx < node->parameters->parameters.size())
            debugDeclareVariable(
                std::string(arg.getName()),
                alloca,
                node->parameters->parameters[paramIdx]->type,
                node->parameters->parameters[paramIdx],
                paramIdx + 1);
        paramIdx++;
    }

    // Generate the function body
    if(node->body)
    {
        const auto& bodyStmts = node->body->statements;
        for(size_t si = 0; si < bodyStmts.size(); si++)
        {
            generateStatement(bodyStmts[si]);
            expireUnusedLocalBorrows(bodyStmts, si, currentScopeDepth());
        }
    }

    auto exceptionNamedValues = namedValues;
    auto exceptionStructVariableTypes = structVariableTypes;
    auto exceptionCleanupScopes = cleanupScopes;
    auto exceptionMovedVariables = movedVariables;

    // `&mut` parameters go back to the caller, then scope-exit destructors
    // run for locals at normal function fallthrough.
    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
        emitInoutWriteBack();
    exitCleanupScope();

    // If the function is void and doesn't have a return, add one
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!mlang::llvm_compat::terminatorOrNull(currentBlock))
    {
        if(currentFunctionExceptionFrame)
            builder.CreateCall(
                exceptionsPopFrameFunc,
                {currentFunctionExceptionFrame});
        if(returnType->isVoidTy())
        {
            builder.CreateRetVoid();
        }
        else
        {
            if(node->name == "main" || node->name == "__mlang_user_main")
            {
                // Default main return to 0 when no explicit return is present.
                builder.CreateRet(llvm::ConstantInt::get(returnType, 0, true));
            }
            else
            {
                // For non-void functions without a return, add unreachable
                // This indicates a bug in the source code but prevents LLVM
                // crashes
                builder.CreateUnreachable();
            }
        }
    }

    builder.SetInsertPoint(functionExceptionBB);
    if(currentFunctionExceptionFrame)
        builder.CreateCall(
            exceptionsPopFrameFunc,
            {currentFunctionExceptionFrame});
    namedValues = exceptionNamedValues;
    structVariableTypes = exceptionStructVariableTypes;
    cleanupScopes = exceptionCleanupScopes;
    movedVariables = exceptionMovedVariables;
    emitAllActiveCleanups();
    builder.CreateCall(exceptionsRethrowFunc, {});
    builder.CreateUnreachable();

    namedValues = std::move(savedNamedValues);
    constantVariables = std::move(savedConstantVariables);
    immutableReferenceVariables = std::move(savedImmutableReferenceVariables);
    movedVariables = std::move(savedMovedVariables);
    pointerBorrowTarget = std::move(savedPointerBorrowTarget);
    pointerBorrowParent = std::move(savedPointerBorrowParent);
    aggregateBorrowOrigins = std::move(savedAggregateBorrowOrigins);
    aggregateBorrowPlaces = std::move(savedAggregateBorrowPlaces);
    aggregateBorrowFieldPlaces = std::move(savedAggregateBorrowFieldPlaces);
    pointerBorrowPlace = std::move(savedPointerBorrowPlace);
    activeBorrowers = std::move(savedActiveBorrowers);
    activeMutBorrower = std::move(savedActiveMutBorrower);
    variableScopeDepth = std::move(savedVariableScopeDepth);
    variableTypes = std::move(savedVariableTypes);
    structVariableTypes = std::move(savedStructVariableTypes);
    traitObjectVariableTypes = std::move(savedTraitObjectVariableTypes);
    enumVariableTypes = std::move(savedEnumVariableTypes);
    listElementTypes = std::move(savedListElementTypes);
    arrayCapacities = std::move(savedArrayCapacities);
    arrayKnownLengths = std::move(savedArrayKnownLengths);
    multiarrayMutability = std::move(savedMultiarrayMutability);
    mapKeyValueTypes = std::move(savedMapKeyValueTypes);
    tupleElementTypes = std::move(savedTupleElementTypes);
    pointerElementTypes = std::move(savedPointerElementTypes);
    pointerKnownNull = std::move(savedPointerKnownNull);
    cleanupScopes = std::move(savedCleanupScopes);
    pointerBorrowScopes = std::move(savedPointerBorrowScopes);
    variableScopeDepthScopes = std::move(savedVariableScopeDepthScopes);
    closureVariables = std::move(savedClosureVariables);
    runtimeClosureVariables = std::move(savedRuntimeClosureVariables);
    runtimeClosureReturnTypes = std::move(savedRuntimeClosureReturnTypes);
    functionTypeVariables = std::move(savedFunctionTypeVariables);
    functionItemVariables = std::move(savedFunctionItemVariables);
    functionTypeParameters = std::move(savedFunctionTypeParameters);
    pointerParameterBorrowOrigins =
        std::move(savedPointerParameterBorrowOrigins);
    runtimeClosureBorrowOwners = std::move(savedRuntimeClosureBorrowOwners);
    closureCaptureAliases = std::move(savedClosureCaptureAliases);
    closureCaptureTypes = std::move(savedClosureCaptureTypes);
    closureConstCaptures = std::move(savedClosureConstCaptures);
    closureCaptureReferenceAliases =
        std::move(savedClosureCaptureReferenceAliases);
    activeInlineClosures = std::move(savedActiveInlineClosures);
    inlineClosureReturnStates = std::move(savedInlineClosureReturnStates);
    currentFunctionExceptionFrame = savedCurrentFunctionExceptionFrame;
    currentSemanticReturnType = savedSemanticReturnType;
    unsafeDepth = savedUnsafeDepth;
    currentModule = savedModule;
    constexprValues = std::move(savedConstexprValues);
    activePackExpansions = std::move(savedPackExpansions);
    inoutParams = std::move(savedInoutParams);
    inoutFunction = savedInoutFunction;
    activeFunctionTypeBindings = std::move(savedFunctionTypeBindings);
    builder.restoreIP(savedIP);

    // Verify the function
    if(!structuralRequirementProbe)
        llvm::verifyFunction(*function);
    return function;
}
