#include "ir.h"
#include "ir/common.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Verifier.h>
#include <algorithm>
#include <functional>

using mlang::ir_detail::common::Helpers;

void CodeGenerator::generateStructMethods(StructDefNode* node)
{
    if(!node->members)
        return;

    // First, inherit methods from base struct if any
    if(!node->baseName.empty())
    {
        auto baseMethodsIt = structMethods.find(node->baseName);
        if(baseMethodsIt != structMethods.end())
        {
            // Copy all base methods to this struct
            for(const auto& methodPair : baseMethodsIt->second)
            {
                // Only inherit if not overridden by this struct
                bool overridden = false;
                for(auto method : node->members->methods)
                {
                    if(method->name == methodPair.first)
                    {
                        overridden = true;
                        break;
                    }
                }
                if(!overridden)
                {
                    structMethods[node->name][methodPair.first] =
                        methodPair.second;
                }
            }
        }
    }

    // Register all methods for this struct
    for(auto method : node->members->methods)
    {
        if(method && method->sourceModule.empty())
            method->sourceModule = node->sourceModule;
        structMethods[node->name][method->name] =
            std::make_pair(method->isPublic, method);

        // Generate forward declaration
        generateMethodDeclaration(node->name, method);
    }
}

llvm::Function*
CodeGenerator::generateMethodDeclaration(const std::string& structName,
                                         StructMethodNode* method)
{
    // Method name is mangled: StructName_methodName
    std::string mangledName = structName + "_" + method->name;

    // Check if already declared
    if(module->getFunction(mangledName))
    {
        return module->getFunction(mangledName);
    }

    std::vector<llvm::Type*> paramTypes;

    // First parameter is pointer to struct (self)
    if(!method->isStatic)
    {
        llvm::Type* structType = getStructType(structName);
        if(structType)
        {
#if LLVM_VERSION_MAJOR >= 15
            paramTypes.push_back(llvm::PointerType::get(context, 0));
#else
            paramTypes.push_back(llvm::PointerType::get(structType, 0));
#endif
        }
    }

    // Add other parameters (skip 'self' if it's explicitly declared)
    for(auto param : method->parameters->parameters)
    {
        // Skip 'self' parameter - it's handled separately above
        if(param->name == "self")
            continue;

        llvm::Type* paramType = getLLVMTypeFromNode(param->type);
        if(!paramType)
        {
            reportError(param->line,
                        "unknown type: " + Helpers::type_name_for_error(param->type));
            paramType = llvm::Type::getInt32Ty(context);
        }
        paramTypes.push_back(paramType);
    }

    llvm::Type* returnType = getLLVMTypeFromNode(method->returnType);
    if(!returnType)
    {
        reportError(method->line,
                    "unknown type: " + Helpers::type_name_for_error(method->returnType));
        returnType = llvm::Type::getVoidTy(context);
    }

    llvm::FunctionType* funcType =
        llvm::FunctionType::get(returnType, paramTypes, false);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, mangledName, module.get());

    // Set parameter names
    unsigned idx = 0;
    unsigned paramIdx = 0;
    for(auto& arg : function->args())
    {
        if(idx == 0 && !method->isStatic)
        {
            arg.setName("self");
        }
        else
        {
            // Find the next non-self parameter
            while(paramIdx < method->parameters->parameters.size() &&
                  method->parameters->parameters[paramIdx]->name == "self")
            {
                paramIdx++;
            }
            if(paramIdx < method->parameters->parameters.size())
            {
                arg.setName(method->parameters->parameters[paramIdx]->name);
                paramIdx++;
            }
        }
        idx++;
    }

    // Register static methods as callable qualified functions:
    // StructName::method(...)
    if(method->isStatic)
    {
        std::string qname = structName + "::" + method->name;
        auto& overloads = functionOverloads[qname];
        bool exists = false;
        for(const auto& ov : overloads)
        {
            if(ov.function == function)
            {
                exists = true;
                break;
            }
        }
        if(!exists)
        {
            std::string srcModule = method->sourceModule;
            if(srcModule.empty())
            {
                auto sit = structVisibility.find(structName);
                if(sit != structVisibility.end())
                    srcModule = sit->second.second;
            }

            std::string signatureKey = qname + "#" + function->getName().str();

            FunctionOverloadInfo info{nullptr,      function,
                                      signatureKey, function->getName().str(),
                                      false,        method->isPublic,
                                      srcModule};
            overloads.push_back(info);
        }
    }

    return function;
}

llvm::Function*
CodeGenerator::generateMethodDefinition(const std::string& structName,
                                        StructMethodNode* method)
{
    std::string mangledName = structName + "_" + method->name;

    llvm::Function* function = module->getFunction(mangledName);
    if(!function)
    {
        function = generateMethodDeclaration(structName, method);
    }

    // Check if already has a body
    if(!function->empty())
    {
        return function;
    }

    if(!method->body)
    {
        reportError(method->line, "method '" + method->name + "' has no body");
        return function;
    }

    std::string savedModule = currentModule;
    std::string savedStructContext = currentStructContext;
    if(!method->sourceModule.empty())
        currentModule = method->sourceModule;
    currentStructContext = structName;
    auto savedIP = builder.saveIP();
    llvm::Value* savedExceptionFrame = currentFunctionExceptionFrame;
    currentFunctionExceptionFrame = nullptr;
    int savedUnsafeDepth = unsafeDepth;
    unsafeDepth = 0;
    auto savedNamedValues = namedValues;
    auto savedConstantVariables = constantVariables;
    auto savedMovedVariables = movedVariables;
    auto savedPointerBorrowTarget = pointerBorrowTarget;
    auto savedActiveBorrowers = activeBorrowers;
    auto savedActiveMutBorrower = activeMutBorrower;
    auto savedVariableScopeDepth = variableScopeDepth;
    auto savedVariableTypes = variableTypes;
    auto savedStructVariableTypes = structVariableTypes;
    auto savedClosureVariables = closureVariables;
    auto savedActiveInlineClosures = activeInlineClosures;
    auto savedTraitObjectVariableTypes = traitObjectVariableTypes;
    auto savedEnumVariableTypes = enumVariableTypes;
    auto savedListElementTypes = listElementTypes;
    auto savedMapKeyValueTypes = mapKeyValueTypes;
    auto savedTupleElementTypes = tupleElementTypes;
    auto savedPointerElementTypes = pointerElementTypes;
    auto savedCleanupScopes = cleanupScopes;
    auto savedPointerBorrowScopes = pointerBorrowScopes;
    auto savedVariableScopeDepthScopes = variableScopeDepthScopes;
    auto savedTypeParamBindings = activeTypeParamBindings;
    auto savedSemanticReturnType = currentSemanticReturnType;
    currentSemanticReturnType = method->returnType;
    auto restoreMethodCodegenState = [&]()
    {
        activeTypeParamBindings = savedTypeParamBindings;
        currentSemanticReturnType = savedSemanticReturnType;
        currentModule = savedModule;
        currentStructContext = savedStructContext;
        builder.restoreIP(savedIP);
        currentFunctionExceptionFrame = savedExceptionFrame;
        unsafeDepth = savedUnsafeDepth;
        namedValues = savedNamedValues;
        constantVariables = savedConstantVariables;
        movedVariables = savedMovedVariables;
        pointerBorrowTarget = savedPointerBorrowTarget;
        activeBorrowers = savedActiveBorrowers;
        activeMutBorrower = savedActiveMutBorrower;
        variableScopeDepth = savedVariableScopeDepth;
        variableTypes = savedVariableTypes;
        structVariableTypes = savedStructVariableTypes;
        traitObjectVariableTypes = savedTraitObjectVariableTypes;
        enumVariableTypes = savedEnumVariableTypes;
        listElementTypes = savedListElementTypes;
        mapKeyValueTypes = savedMapKeyValueTypes;
        tupleElementTypes = savedTupleElementTypes;
        pointerElementTypes = savedPointerElementTypes;
        cleanupScopes = savedCleanupScopes;
        pointerBorrowScopes = savedPointerBorrowScopes;
        variableScopeDepthScopes = savedVariableScopeDepthScopes;
        closureVariables = savedClosureVariables;
        activeInlineClosures = savedActiveInlineClosures;
    };

    // Create entry block
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear scope
    namedValues.clear();
    constantVariables.clear();
    movedVariables.clear();
    pointerBorrowTarget.clear();
    activeBorrowers.clear();
    activeMutBorrower.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    traitObjectVariableTypes.clear();
    enumVariableTypes.clear();
    listElementTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    mapKeyValueTypes.clear();
    tupleElementTypes.clear();
    pointerElementTypes.clear();
    pointerKnownNull.clear();
    closureVariables.clear();
    activeInlineClosures.clear();
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    activeTypeParamBindings.clear();
    auto genericNameIt = mangledToGenericName.find(structName);
    if(genericNameIt != mangledToGenericName.end())
    {
        auto templIt = genericStructTemplates.find(genericNameIt->second);
        auto argsIt = monomorphizedTypeArgs.find(structName);
        if(templIt != genericStructTemplates.end() &&
           argsIt != monomorphizedTypeArgs.end())
        {
            const auto& params = templIt->second->typeParams;
            const auto& args = argsIt->second;
            for(size_t i = 0; i < params.size() && i < args.size(); ++i)
                activeTypeParamBindings[params[i]] = args[i];
        }
    }
    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    // Set up self parameter and other parameters
    unsigned argIdx = 0;
    unsigned methodParamIdx = 0;
    for(auto& arg : function->args())
    {
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        llvm::Value* paramValue = &arg;
        if(arg.getType()->isStructTy())
            paramValue = applyStructCopySemantics(paramValue);
        builder.CreateStore(paramValue, alloca);
        namedValues[std::string(arg.getName())] = alloca;
        recordVariableScopeDepth(std::string(arg.getName()));

        if(argIdx == 0 && !method->isStatic)
        {
            // 'self' is a pointer to the struct
            structVariableTypes["self"] = structName;
            variableTypes["self"] = TypeNode::TYPE_STRUCT;
        }
        else
        {
            // Find next non-self parameter
            while(methodParamIdx < method->parameters->parameters.size() &&
                  method->parameters->parameters[methodParamIdx]->name ==
                      "self")
            {
                methodParamIdx++;
            }
            if(methodParamIdx < method->parameters->parameters.size())
            {
                auto* paramNode =
                    method->parameters->parameters[methodParamIdx];
                if(auto* refType =
                       dynamic_cast<ReferenceTypeNode*>(paramNode->type))
                {
                    variableTypes[std::string(arg.getName())] =
                        refType->elementType->kind;
                    if(auto* genListInner = dynamic_cast<GenericListTypeNode*>(
                           refType->elementType))
                    {
                        listElementTypes[std::string(arg.getName())] =
                            genListInner->elementType;
                    }
                    if(auto* mapInner =
                           dynamic_cast<MapTypeNode*>(refType->elementType))
                    {
                        mapKeyValueTypes[std::string(arg.getName())] =
                            std::make_pair(mapInner->keyType,
                                           mapInner->valueType);
                    }
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
                        structVariableTypes[std::string(arg.getName())] =
                            structType->structName;
                }
                if(auto* genStructType =
                       dynamic_cast<GenericStructTypeRefNode*>(paramNode->type))
                {
                    std::string mangled = getOrCreateMonomorphizedStruct(
                        genStructType->structName, genStructType->typeArgs);
                    structVariableTypes[std::string(arg.getName())] = mangled;
                }
                if(auto* genListType =
                       dynamic_cast<GenericListTypeNode*>(paramNode->type))
                {
                    listElementTypes[std::string(arg.getName())] =
                        genListType->elementType;
                }
                if(auto* mapType = dynamic_cast<MapTypeNode*>(paramNode->type))
                {
                    mapKeyValueTypes[std::string(arg.getName())] =
                        std::make_pair(mapType->keyType, mapType->valueType);
                }
                if(auto* tupleType =
                       dynamic_cast<TupleTypeNode*>(paramNode->type))
                {
                    std::vector<TypeNode*> elemTypes;
                    for(auto* t : tupleType->elementTypes->types)
                        elemTypes.push_back(t);
                    tupleElementTypes[std::string(arg.getName())] = elemTypes;
                }
                if(auto* ptrType =
                       dynamic_cast<PointerTypeNode*>(paramNode->type))
                {
                    pointerElementTypes[std::string(arg.getName())] =
                        ptrType->elementType;
                }
                if(auto* traitObjType =
                       dynamic_cast<TraitObjectTypeNode*>(paramNode->type))
                {
                    variableTypes[std::string(arg.getName())] =
                        TypeNode::TYPE_TRAIT_OBJECT;
                    traitObjectVariableTypes[std::string(arg.getName())] =
                        traitObjType->traitName;
                }
                methodParamIdx++;
            }
        }
        argIdx++;
    }

    if(method->isMutexPropertyAccessor)
    {
        bool ok = generateMutexPropertyMethodBody(structName, method, function);
        restoreMethodCodegenState();
        return ok ? function : nullptr;
    }

    if(method->isAtomicPropertyAccessor)
    {
        bool ok =
            generateAtomicPropertyMethodBody(structName, method, function);
        restoreMethodCodegenState();
        return ok ? function : nullptr;
    }

    if(method->isSynthesizedJsonSerializer)
    {
        bool ok = generateJsonSerializerMethodBody(structName, method, function);
        restoreMethodCodegenState();
        return ok ? function : nullptr;
    }

    if(method->isSynthesizedJsonTextDeserializer)
    {
        bool ok = generateJsonTextDeserializerMethodBody(structName, method,
                                                         function);
        restoreMethodCodegenState();
        return ok ? function : nullptr;
    }

    if(method->isSynthesizedJsonValueDeserializer)
    {
        bool ok = generateJsonValueDeserializerMethodBody(structName, method,
                                                          function);
        restoreMethodCodegenState();
        return ok ? function : nullptr;
    }

    // Generate body
    for(auto stmt : method->body->statements)
    {
        generateStatement(stmt);
    }

    // Run scope-exit destructors for locals at normal method fallthrough.
    exitCleanupScope();

    // Add terminator if needed
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!currentBlock->getTerminator())
    {
        if(returnType->isVoidTy())
        {
            builder.CreateRetVoid();
        }
        else
        {
            builder.CreateUnreachable();
        }
    }

    llvm::verifyFunction(*function);
    restoreMethodCodegenState();
    return function;
}

bool CodeGenerator::generateMutexPropertyMethodBody(
    const std::string& structName, StructMethodNode* method,
    llvm::Function* function)
{
    if(!method || !function || method->propertyFieldName.empty() ||
       method->propertyLockFieldName.empty())
    {
        reportError(method ? method->line : 0,
                    "invalid synthesized mutex property accessor");
        return false;
    }

    auto memberIt = structMembers.find(structName);
    if(memberIt == structMembers.end())
    {
        reportError(method->line, "unknown struct type: " + structName);
        return false;
    }

    int fieldIndex = -1;
    int lockFieldIndex = -1;
    TypeNode* fieldType = nullptr;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        if(members[i].first == method->propertyFieldName)
        {
            fieldIndex = static_cast<int>(i);
            fieldType = members[i].second;
        }
        if(members[i].first == method->propertyLockFieldName)
            lockFieldIndex = static_cast<int>(i);
    }

    if(fieldIndex < 0 || !fieldType)
    {
        reportError(method->line, "struct '" + structName +
                                      "' has no field named '" +
                                      method->propertyFieldName + "'");
        return false;
    }
    if(lockFieldIndex < 0)
    {
        reportError(method->line, "struct '" + structName +
                                      "' has no lock field named '" +
                                      method->propertyLockFieldName + "'");
        return false;
    }

    llvm::StructType* structType = getStructType(structName);
    if(!structType)
        return false;

    const StructFieldLayout* fieldLayout =
        getStructFieldLayout(structName, fieldIndex);
    const StructFieldLayout* lockLayout =
        getStructFieldLayout(structName, lockFieldIndex);
    if(!fieldLayout || !lockLayout)
        return false;
    if(fieldLayout->packedBit)
    {
        reportError(method->line,
                    "@property(mutex) is not supported on packed bit fields");
        return false;
    }

    llvm::Value* selfStorage = namedValues["self"];
    if(!selfStorage)
    {
        reportError(method->line,
                    "internal error: missing self for mutex property method");
        return false;
    }

    llvm::Value* selfPtr = selfStorage;
    if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(selfStorage))
    {
        llvm::Type* allocaType = alloca->getAllocatedType();
        if(allocaType->isPointerTy())
            selfPtr = builder.CreateLoad(allocaType, alloca, "self.ptr");
    }

    llvm::Type* llvmFieldType = getLLVMTypeFromNode(fieldType);
    if(!llvmFieldType)
    {
        reportError(method->line,
                    "unknown type: " + Helpers::type_name_for_error(fieldType));
        return false;
    }

    llvm::Value* fieldPtr =
        builder.CreateStructGEP(structType, selfPtr, fieldLayout->storageIndex,
                                method->propertyFieldName + "_ptr");
    llvm::Value* lockPtr =
        builder.CreateStructGEP(structType, selfPtr, lockLayout->storageIndex,
                                method->propertyLockFieldName + "_ptr");
    llvm::Value* rawHandle = ensurePropertyMutexHandle(
        lockPtr, method->isRecursiveMutexPropertyAccessor,
        method->propertyFieldName + ".mutex");
    if(!rawHandle)
        return false;

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Value* mutexPtr = builder.CreateIntToPtr(
        rawHandle, ptrType, method->propertyFieldName + ".mutex.ptr");
    builder.CreateCall(pthreadMutexLockFunc, {mutexPtr});

    if(!method->isPropertySetter)
    {
        llvm::Value* loaded =
            builder.CreateLoad(llvmFieldType, fieldPtr, "mutex.prop.load");
        builder.CreateCall(pthreadMutexUnlockFunc, {mutexPtr});
        exitCleanupScope();
        if(currentFunctionExceptionFrame)
            builder.CreateCall(exceptionsPopFrameFunc,
                               {currentFunctionExceptionFrame});
        builder.CreateRet(loaded);
        llvm::verifyFunction(*function);
        return true;
    }

    llvm::Value* valueStorage = namedValues["value"];
    if(!valueStorage)
    {
        reportError(method->line,
                    "internal error: missing setter value for mutex property");
        return false;
    }

    llvm::Value* desired =
        builder.CreateLoad(llvmFieldType, valueStorage, "mutex.prop.value");
    builder.CreateStore(desired, fieldPtr);
    builder.CreateCall(pthreadMutexUnlockFunc, {mutexPtr});
    exitCleanupScope();
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    builder.CreateRetVoid();
    llvm::verifyFunction(*function);
    return true;
}

bool CodeGenerator::generateAtomicPropertyMethodBody(
    const std::string& structName, StructMethodNode* method,
    llvm::Function* function)
{
    if(!method || !function || method->propertyFieldName.empty())
    {
        reportError(method ? method->line : 0,
                    "invalid synthesized atomic property accessor");
        return false;
    }

    auto memberIt = structMembers.find(structName);
    if(memberIt == structMembers.end())
    {
        reportError(method->line, "unknown struct type: " + structName);
        return false;
    }

    int fieldIndex = -1;
    TypeNode* fieldType = nullptr;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        if(members[i].first == method->propertyFieldName)
        {
            fieldIndex = static_cast<int>(i);
            fieldType = members[i].second;
            break;
        }
    }

    if(fieldIndex < 0 || !fieldType)
    {
        reportError(method->line, "struct '" + structName +
                                      "' has no field named '" +
                                      method->propertyFieldName + "'");
        return false;
    }

    switch(fieldType->kind)
    {
    case TypeNode::TYPE_BOOL:
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I8:
    case TypeNode::TYPE_I16:
    case TypeNode::TYPE_I32:
    case TypeNode::TYPE_I64:
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
        break;
    default:
        reportError(method->line,
                    "@property(atomic) only supports bool and integer "
                    "primitive fields");
        return false;
    }

    llvm::StructType* structType = getStructType(structName);
    if(!structType)
        return false;

    const StructFieldLayout* layout =
        getStructFieldLayout(structName, fieldIndex);
    if(!layout)
        return false;
    if(layout->packedBit)
    {
        reportError(method->line,
                    "@property(atomic) is not supported on packed bit "
                    "fields");
        return false;
    }

    llvm::Value* selfStorage = namedValues["self"];
    if(!selfStorage)
    {
        reportError(method->line,
                    "internal error: missing self for atomic property method");
        return false;
    }

    llvm::Value* selfPtr = selfStorage;
    if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(selfStorage))
    {
        llvm::Type* allocaType = alloca->getAllocatedType();
        if(allocaType->isPointerTy())
            selfPtr = builder.CreateLoad(allocaType, alloca, "self.ptr");
    }

    llvm::Type* llvmFieldType = getLLVMTypeFromNode(fieldType);
    if(!llvmFieldType)
    {
        reportError(method->line,
                    "unknown type: " + Helpers::type_name_for_error(fieldType));
        return false;
    }

    llvm::Value* fieldPtr =
        builder.CreateStructGEP(structType, selfPtr, layout->storageIndex,
                                method->propertyFieldName + "_ptr");

    if(!method->isPropertySetter)
    {
        auto* loadInst =
            builder.CreateLoad(llvmFieldType, fieldPtr, "atomic.prop.load");
        loadInst->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        exitCleanupScope();
        if(currentFunctionExceptionFrame)
            builder.CreateCall(exceptionsPopFrameFunc,
                               {currentFunctionExceptionFrame});
        builder.CreateRet(loadInst);
        llvm::verifyFunction(*function);
        return true;
    }

    llvm::Value* valueStorage = namedValues["value"];
    if(!valueStorage)
    {
        reportError(method->line,
                    "internal error: missing setter value for atomic property");
        return false;
    }

    llvm::Value* desired =
        builder.CreateLoad(llvmFieldType, valueStorage, "atomic.prop.desired");
    llvm::Function* curFn = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* loopBB =
        llvm::BasicBlock::Create(context, "atomic.prop.cas", curFn);
    llvm::BasicBlock* doneBB =
        llvm::BasicBlock::Create(context, "atomic.prop.done", curFn);

    builder.CreateBr(loopBB);
    builder.SetInsertPoint(loopBB);

    auto* expected =
        builder.CreateLoad(llvmFieldType, fieldPtr, "atomic.prop.expected");
    expected->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
    auto* cmpxchg = builder.CreateAtomicCmpXchg(
        fieldPtr, expected, desired, llvm::MaybeAlign(),
        llvm::AtomicOrdering::SequentiallyConsistent,
        llvm::AtomicOrdering::SequentiallyConsistent);
    cmpxchg->setWeak(false);
    llvm::Value* success =
        builder.CreateExtractValue(cmpxchg, 1, "atomic.prop.success");
    builder.CreateCondBr(success, doneBB, loopBB);

    builder.SetInsertPoint(doneBB);
    exitCleanupScope();
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    builder.CreateRetVoid();
    llvm::verifyFunction(*function);
    return true;
}

llvm::Value* CodeGenerator::getJsonLastErrorString()
{
    initializeStdlibFunctions();
    return builder.CreateCall(jsonLastErrorFunc, {}, "json.last_error");
}

llvm::Value* CodeGenerator::buildJsonResultValue(llvm::Function* function,
                                                 bool isOk,
                                                 llvm::Value* payload,
                                                 int payloadIndexOverride)
{
    if(!function || !function->getReturnType()->isStructTy())
        return nullptr;

    auto* retStruct = llvm::cast<llvm::StructType>(function->getReturnType());
    std::string retName = retStruct->getName().str();
    auto membersIt = structMembers.find(retName);
    if(membersIt == structMembers.end())
        return nullptr;

    int isOkIndex = -1;
    int okIndex = -1;
    int errIndex = -1;
    for(size_t i = 0; i < membersIt->second.size(); ++i)
    {
        const auto& mem = membersIt->second[i];
        if(mem.first == "is_ok")
            isOkIndex = static_cast<int>(i);
        else if(mem.first == "ok")
            okIndex = static_cast<int>(i);
        else if(mem.first == "err")
            errIndex = static_cast<int>(i);
    }

    int payloadIndex = payloadIndexOverride >= 0
                           ? payloadIndexOverride
                           : (isOk ? okIndex : errIndex);
    if(isOkIndex < 0 || payloadIndex < 0)
        return nullptr;

    llvm::Value* result = llvm::Constant::getNullValue(retStruct);
    result = builder.CreateInsertValue(
        result, llvm::ConstantInt::get(llvm::Type::getInt1Ty(context),
                                       isOk ? 1 : 0),
        static_cast<unsigned>(isOkIndex), "json.result.flag");

    llvm::Type* expectedType =
        retStruct->getStructElementType(static_cast<unsigned>(payloadIndex));
    if(payload && payload->getType() != expectedType)
    {
        if(payload->getType()->isIntegerTy() && expectedType->isIntegerTy())
        {
            payload = builder.CreateIntCast(payload, expectedType, true,
                                            "json.result.intcast");
        }
        else if(payload->getType()->isIntegerTy() &&
                expectedType->isFloatingPointTy())
        {
            payload = builder.CreateSIToFP(payload, expectedType,
                                           "json.result.sitofp");
        }
        else if(payload->getType()->isFloatingPointTy() &&
                expectedType->isIntegerTy())
        {
            payload = builder.CreateFPToSI(payload, expectedType,
                                           "json.result.fptosi");
        }
        else if(payload->getType()->isFloatingPointTy() &&
                expectedType->isFloatingPointTy())
        {
            payload = builder.CreateFPCast(payload, expectedType,
                                           "json.result.fpcast");
        }
        else if(payload->getType()->isPointerTy() &&
                expectedType->isPointerTy())
        {
            payload = builder.CreateBitCast(payload, expectedType,
                                            "json.result.ptrcast");
        }
    }

    if(payload)
    {
        result = builder.CreateInsertValue(
            result, payload, static_cast<unsigned>(payloadIndex),
            "json.result.payload");
    }
    return result;
}

bool CodeGenerator::populateStructFromJsonValue(
    const std::string& structName, llvm::Value* jsonValueHandle,
    llvm::Value* outStructAlloca, llvm::Function* function,
    llvm::BasicBlock* failBB, llvm::AllocaInst* errorSlot)
{
    initializeStdlibFunctions();
    if(!jsonValueHandle || !outStructAlloca || !function || !failBB ||
       !errorSlot)
    {
        return false;
    }

    auto membersIt = structMembers.find(structName);
    if(membersIt == structMembers.end())
    {
        reportError(0, "unknown struct for json serde: " + structName);
        return false;
    }

    auto* structType = getStructType(structName);
    if(!structType)
        return false;

    auto branchToFailWithError = [&](llvm::Value* errVal) {
        builder.CreateStore(errVal, errorSlot);
        builder.CreateBr(failBB);
    };

    llvm::BasicBlock* objectOkBB =
        llvm::BasicBlock::Create(context, "json.object.ok", function);
    llvm::BasicBlock* objectFailBB =
        llvm::BasicBlock::Create(context, "json.object.fail", function);
    llvm::Value* rootKind =
        builder.CreateCall(jsonValueKindFunc, {jsonValueHandle}, "json.kind");
    llvm::Value* isObject = builder.CreateICmpEQ(
        rootKind, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 6),
        "json.is_object");
    builder.CreateCondBr(isObject, objectOkBB, objectFailBB);

    builder.SetInsertPoint(objectFailBB);
    branchToFailWithError(
        Helpers::create_global_cstring(builder, "std::json from_json: expected object",
                              "json.expected_object"));

    builder.SetInsertPoint(objectOkBB);

    for(size_t idx = 0; idx < membersIt->second.size(); ++idx)
    {
        const auto* accessInfo =
            getStructFieldAccessInfo(structName, static_cast<int>(idx));
        if(accessInfo && accessInfo->isSynthesizedPropertyStorage)
            continue;

        const auto& member = membersIt->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;
        const StructFieldLayout* layout =
            getStructFieldLayout(structName, static_cast<int>(idx));
        if(!layout || layout->packedBit)
        {
            reportError(0, "field '" + memberName + "' of struct '" +
                               structName +
                               "' is not supported for Json derive");
            return false;
        }

        llvm::Value* keyVal =
            Helpers::create_global_cstring(builder, memberName, "json.field.key");
        llvm::Value* childHandle = builder.CreateCall(
            jsonObjectGetFunc, {jsonValueHandle, keyVal}, "json.field.handle");

        llvm::BasicBlock* childOkBB =
            llvm::BasicBlock::Create(context, "json.field.ok", function);
        llvm::BasicBlock* childMissingBB =
            llvm::BasicBlock::Create(context, "json.field.missing", function);
        llvm::Value* childExists = builder.CreateICmpNE(
            childHandle, llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0),
            "json.field.exists");
        builder.CreateCondBr(childExists, childOkBB, childMissingBB);

        builder.SetInsertPoint(childMissingBB);
        branchToFailWithError(getJsonLastErrorString());

        builder.SetInsertPoint(childOkBB);
        llvm::Value* fieldPtr = builder.CreateStructGEP(
            structType, outStructAlloca, layout->storageIndex,
            memberName + ".json.ptr");

        auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType);
        if(structRef)
        {
            std::string enumName =
                resolveVisibleEnumName(structRef->structName);
            if(!enumName.empty())
            {
                reportError(0, "field '" + memberName + "' of struct '" +
                                   structName +
                                   "' has unsupported Json derive type");
                return false;
            }

            std::string childStruct = structRef->structName;
            if(!jsonStructs.count(childStruct))
            {
                reportError(0, "struct '" + childStruct +
                                   "' does not derive Json");
                return false;
            }

            llvm::Type* childStructType = getLLVMTypeFromNode(memberType);
            if(!childStructType)
                return false;
            auto* nestedAlloca = builder.CreateAlloca(
                childStructType, nullptr, memberName + ".json.tmp");
            builder.CreateStore(llvm::Constant::getNullValue(childStructType),
                                nestedAlloca);

            llvm::BasicBlock* nestedFailBB =
                llvm::BasicBlock::Create(context, "json.nested.fail",
                                         function);
            if(!populateStructFromJsonValue(childStruct, childHandle,
                                            nestedAlloca, function,
                                            nestedFailBB, errorSlot))
            {
                return false;
            }

            llvm::Value* nestedValue = builder.CreateLoad(
                childStructType, nestedAlloca, memberName + ".json.value");
            builder.CreateCall(jsonValueFreeFunc, {childHandle});
            builder.CreateStore(nestedValue, fieldPtr);

            llvm::BasicBlock* nestedContBB =
                llvm::BasicBlock::Create(context, "json.nested.cont", function);
            builder.CreateBr(nestedContBB);

            builder.SetInsertPoint(nestedFailBB);
            builder.CreateCall(jsonValueFreeFunc, {childHandle});
            builder.CreateBr(failBB);

            builder.SetInsertPoint(nestedContBB);
            continue;
        }

        llvm::Type* llvmFieldType = getLLVMTypeFromNode(memberType);
        if(!llvmFieldType)
            return false;

        int expectedKind = -1;
        switch(memberType ? memberType->kind : TypeNode::TYPE_VOID)
        {
        case TypeNode::TYPE_BOOL:
            expectedKind = 2;
            break;
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
        case TypeNode::TYPE_I64:
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
        case TypeNode::TYPE_U64:
        case TypeNode::TYPE_FLOAT:
        case TypeNode::TYPE_DOUBLE:
            expectedKind = 3;
            break;
        case TypeNode::TYPE_STR8:
            expectedKind = 4;
            break;
        default:
            reportError(0, "field '" + memberName + "' of struct '" +
                               structName +
                               "' has unsupported Json derive type");
            return false;
        }

        llvm::Value* childKind = builder.CreateCall(jsonValueKindFunc,
                                                    {childHandle},
                                                    "json.field.kind");
        llvm::BasicBlock* kindOkBB =
            llvm::BasicBlock::Create(context, "json.kind.ok", function);
        llvm::BasicBlock* kindFailBB =
            llvm::BasicBlock::Create(context, "json.kind.fail", function);
        llvm::Value* kindMatches = builder.CreateICmpEQ(
            childKind,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context),
                                   expectedKind),
            "json.kind.match");
        builder.CreateCondBr(kindMatches, kindOkBB, kindFailBB);

        builder.SetInsertPoint(kindFailBB);
        builder.CreateCall(jsonValueFreeFunc, {childHandle});
        branchToFailWithError(Helpers::create_global_cstring(
            builder, "std::json from_json: field type mismatch",
            "json.type_mismatch"));

        builder.SetInsertPoint(kindOkBB);

        switch(memberType ? memberType->kind : TypeNode::TYPE_VOID)
        {
        case TypeNode::TYPE_BOOL:
        {
            llvm::Value* rawBool = builder.CreateCall(jsonAsBoolFunc,
                                                      {childHandle},
                                                      "json.bool");
            llvm::Value* boolVal = builder.CreateICmpNE(
                rawBool, llvm::ConstantInt::get(rawBool->getType(), 0),
                "json.bool.i1");
            builder.CreateStore(boolVal, fieldPtr);
            break;
        }
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
        case TypeNode::TYPE_I64:
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
        case TypeNode::TYPE_U64:
        {
            llvm::Value* rawInt = builder.CreateCall(jsonAsI64Func,
                                                     {childHandle},
                                                     "json.i64");
            bool isUnsigned =
                memberType->kind == TypeNode::TYPE_U8 ||
                memberType->kind == TypeNode::TYPE_U16 ||
                memberType->kind == TypeNode::TYPE_U32 ||
                memberType->kind == TypeNode::TYPE_U64;
            llvm::Value* castInt =
                builder.CreateIntCast(rawInt, llvmFieldType, !isUnsigned,
                                      "json.int.cast");
            builder.CreateStore(castInt, fieldPtr);
            break;
        }
        case TypeNode::TYPE_FLOAT:
        {
            llvm::Value* rawFloat = builder.CreateCall(jsonAsF64Func,
                                                       {childHandle},
                                                       "json.f64");
            builder.CreateStore(
                builder.CreateFPTrunc(rawFloat, llvmFieldType, "json.f32"),
                fieldPtr);
            break;
        }
        case TypeNode::TYPE_DOUBLE:
        {
            llvm::Value* rawFloat = builder.CreateCall(jsonAsF64Func,
                                                       {childHandle},
                                                       "json.f64");
            builder.CreateStore(rawFloat, fieldPtr);
            break;
        }
        case TypeNode::TYPE_STR8:
        {
            llvm::Value* rawString = builder.CreateCall(jsonAsStringFunc,
                                                        {childHandle},
                                                        "json.str");
            builder.CreateStore(rawString, fieldPtr);
            break;
        }
        default:
            break;
        }

        builder.CreateCall(jsonValueFreeFunc, {childHandle});
    }

    return true;
}

bool CodeGenerator::generateJsonSerializerMethodBody(
    const std::string& structName, StructMethodNode* method,
    llvm::Function* function)
{
    auto memberIt = structMembers.find(structName);
    if(memberIt == structMembers.end())
        return false;

    llvm::StructType* structType = getStructType(structName);
    if(!structType)
        return false;

    llvm::Value* selfStorage = namedValues["self"];
    if(!selfStorage)
        return false;

    llvm::Value* selfPtr = selfStorage;
    if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(selfStorage))
    {
        llvm::Type* allocaType = alloca->getAllocatedType();
        if(allocaType->isPointerTy())
            selfPtr = builder.CreateLoad(allocaType, alloca,
                                         "self.json.ptr");
    }

    llvm::Value* selfValue =
        builder.CreateLoad(structType, selfPtr, "self.json.value");
    llvm::Value* jsonValue =
        buildStructSerdeJsonString(selfValue, structName, method->line);
    exitCleanupScope();
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    builder.CreateRet(jsonValue);
    llvm::verifyFunction(*function);
    return true;
}

bool CodeGenerator::generateJsonTextDeserializerMethodBody(
    const std::string& structName, StructMethodNode* method,
    llvm::Function* function)
{
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    llvm::StructType* structType = getStructType(structName);
    if(!structType)
        return false;

    llvm::Value* jsonTextStorage = namedValues["json_text"];
    if(!jsonTextStorage)
        return false;
    llvm::Value* jsonText =
        builder.CreateLoad(ptrType, jsonTextStorage, "json.text");

    auto* docSlot = builder.CreateAlloca(int64Type, nullptr, "json.doc.slot");
    auto* rootSlot = builder.CreateAlloca(int64Type, nullptr, "json.root.slot");
    auto* errorSlot = builder.CreateAlloca(ptrType, nullptr, "json.err.slot");
    builder.CreateStore(llvm::ConstantInt::get(int64Type, 0), docSlot);
    builder.CreateStore(llvm::ConstantInt::get(int64Type, 0), rootSlot);
    builder.CreateStore(Helpers::create_global_cstring(builder, "std::json from_json failed",
                                              "json.default.err"),
                        errorSlot);

    llvm::BasicBlock* failBB =
        llvm::BasicBlock::Create(context, "json.text.fail", function);
    llvm::BasicBlock* parseOkBB =
        llvm::BasicBlock::Create(context, "json.parse.ok", function);
    llvm::BasicBlock* parseFailBB =
        llvm::BasicBlock::Create(context, "json.parse.fail", function);
    llvm::Value* docHandle =
        builder.CreateCall(jsonParseFunc, {jsonText}, "json.doc");
    builder.CreateStore(docHandle, docSlot);
    llvm::Value* parseOk = builder.CreateICmpNE(
        docHandle, llvm::ConstantInt::get(int64Type, 0), "json.parse.ok");
    builder.CreateCondBr(parseOk, parseOkBB, parseFailBB);

    builder.SetInsertPoint(parseFailBB);
    builder.CreateStore(getJsonLastErrorString(), errorSlot);
    builder.CreateBr(failBB);

    builder.SetInsertPoint(parseOkBB);
    llvm::Value* rootHandle =
        builder.CreateCall(jsonDocRootFunc, {docHandle}, "json.root");
    builder.CreateStore(rootHandle, rootSlot);
    llvm::BasicBlock* rootOkBB =
        llvm::BasicBlock::Create(context, "json.root.ok", function);
    llvm::BasicBlock* rootFailBB =
        llvm::BasicBlock::Create(context, "json.root.fail", function);
    llvm::Value* rootOk = builder.CreateICmpNE(
        rootHandle, llvm::ConstantInt::get(int64Type, 0), "json.root.exists");
    builder.CreateCondBr(rootOk, rootOkBB, rootFailBB);

    builder.SetInsertPoint(rootFailBB);
    builder.CreateStore(getJsonLastErrorString(), errorSlot);
    builder.CreateBr(failBB);

    builder.SetInsertPoint(rootOkBB);
    auto* outAlloca =
        builder.CreateAlloca(structType, nullptr, "json.struct.tmp");
    builder.CreateStore(llvm::Constant::getNullValue(structType), outAlloca);
    if(!populateStructFromJsonValue(structName, rootHandle, outAlloca, function,
                                    failBB, errorSlot))
    {
        return false;
    }

    llvm::Value* outValue =
        builder.CreateLoad(structType, outAlloca, "json.struct.value");
    builder.CreateCall(jsonValueFreeFunc, {rootHandle});
    builder.CreateCall(jsonDocFreeFunc, {docHandle});
    exitCleanupScope();
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    builder.CreateRet(buildJsonResultValue(function, true, outValue));

    builder.SetInsertPoint(failBB);
    llvm::Value* failDoc =
        builder.CreateLoad(int64Type, docSlot, "json.fail.doc");
    llvm::Value* failRoot =
        builder.CreateLoad(int64Type, rootSlot, "json.fail.root");
    llvm::Value* storedErr =
        builder.CreateLoad(ptrType, errorSlot, "json.fail.err");
    builder.CreateCall(jsonValueFreeFunc, {failRoot});
    builder.CreateCall(jsonDocFreeFunc, {failDoc});
    exitCleanupScope();
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    builder.CreateRet(buildJsonResultValue(function, false, storedErr));
    llvm::verifyFunction(*function);
    return true;
}

bool CodeGenerator::generateJsonValueDeserializerMethodBody(
    const std::string& structName, StructMethodNode* method,
    llvm::Function* function)
{
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    llvm::StructType* structType = getStructType(structName);
    if(!structType)
        return false;

    llvm::Value* jsonValueStorage = namedValues["json_value"];
    if(!jsonValueStorage)
        return false;
    llvm::Value* jsonValue =
        builder.CreateLoad(int64Type, jsonValueStorage, "json.value.handle");

    auto* errorSlot = builder.CreateAlloca(ptrType, nullptr, "json.err.slot");
    builder.CreateStore(Helpers::create_global_cstring(builder, "std::json from_json failed",
                                              "json.default.err"),
                        errorSlot);
    auto* outAlloca =
        builder.CreateAlloca(structType, nullptr, "json.struct.tmp");
    builder.CreateStore(llvm::Constant::getNullValue(structType), outAlloca);

    llvm::BasicBlock* failBB =
        llvm::BasicBlock::Create(context, "json.value.fail", function);
    if(!populateStructFromJsonValue(structName, jsonValue, outAlloca, function,
                                    failBB, errorSlot))
    {
        return false;
    }

    llvm::Value* outValue =
        builder.CreateLoad(structType, outAlloca, "json.struct.value");
    exitCleanupScope();
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    builder.CreateRet(buildJsonResultValue(function, true, outValue));

    builder.SetInsertPoint(failBB);
    llvm::Value* storedErr =
        builder.CreateLoad(ptrType, errorSlot, "json.fail.err");
    exitCleanupScope();
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    builder.CreateRet(buildJsonResultValue(function, false, storedErr));
    llvm::verifyFunction(*function);
    return true;
}

llvm::Value* CodeGenerator::generateMethodCall(MethodCallNode* node)
{
    auto resolveStructAliasName = [&](const std::string& typeName)
    {
        std::string current = typeName;
        std::set<std::string> seen;
        while(!current.empty() && seen.insert(current).second)
        {
            auto aliasIt = typeAliases.find(current);
            if(aliasIt == typeAliases.end() || !aliasIt->second.aliasedType)
                break;
            if(auto* structRef = dynamic_cast<StructTypeRefNode*>(
                   aliasIt->second.aliasedType))
            {
                current = structRef->structName;
                continue;
            }
            if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(
                   aliasIt->second.aliasedType))
            {
                current = getOrCreateMonomorphizedStruct(genRef->structName,
                                                         genRef->typeArgs);
                break;
            }
            break;
        }
        return current;
    };

    auto validateTemporaryBorrowArguments =
        [&](const std::vector<ExpressionNode*>& args,
            const std::string& calleeName,
            const std::string& implicitWholeOwner) -> bool
    {
        std::set<std::string> wholeOwnersInCall;
        std::map<std::string, std::set<std::string>> subpathsInCall;
        std::set<std::string> movedOwnersInCall;
        if(!implicitWholeOwner.empty())
            wholeOwnersInCall.insert(implicitWholeOwner);
        auto describeBorrowPath =
            [&](ExpressionNode* expr, std::string& ownerOut,
                std::string& pathOut, bool& isWholeOwnerOut) -> bool
        {
            ownerOut = resolveBorrowOwnerFromLValue(expr);
            if(ownerOut.empty())
                return false;

            isWholeOwnerOut = true;
            pathOut.clear();

            std::vector<std::string> fields;
            ExpressionNode* cur = expr;
            while(auto* field = dynamic_cast<FieldAccessNode*>(cur))
            {
                fields.push_back(field->fieldName);
                if(field->object)
                    cur = field->object;
                else
                    break;
            }

            if(!fields.empty())
            {
                isWholeOwnerOut = false;
                std::reverse(fields.begin(), fields.end());
                for(size_t i = 0; i < fields.size(); ++i)
                {
                    if(i)
                        pathOut += ".";
                    pathOut += fields[i];
                }
            }
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

        for(auto* arg : args)
        {
            collectMovedOwners(arg, false);

            auto registerBorrowInCall = [&](const std::string& owner,
                                            const std::string& path,
                                            bool isWholeOwner) -> bool
            {
                if(isWholeOwner)
                {
                    if(wholeOwnersInCall.count(owner) ||
                       (subpathsInCall.count(owner) &&
                        !subpathsInCall[owner].empty()))
                    {
                        reportError(node->line,
                                    "cannot borrow overlapping parts of '" +
                                        owner + "' in call to '" + calleeName +
                                        "'");
                        return false;
                    }
                    wholeOwnersInCall.insert(owner);
                    return true;
                }

                if(wholeOwnersInCall.count(owner))
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
                    if(existing == path)
                    {
                        overlaps = true;
                        break;
                    }
                    if(path.size() > existing.size() &&
                       path.compare(0, existing.size(), existing) == 0 &&
                       path[existing.size()] == '.')
                    {
                        overlaps = true;
                        break;
                    }
                    if(existing.size() > path.size() &&
                       existing.compare(0, path.size(), path) == 0 &&
                       existing[path.size()] == '.')
                    {
                        overlaps = true;
                        break;
                    }
                }
                if(overlaps)
                {
                    reportError(node->line,
                                "cannot borrow '" + owner + "." + path +
                                    "' multiple times in call to '" +
                                    calleeName + "'");
                    return false;
                }
                paths.insert(path);
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
                    if(!registerBorrowInCall(pit->second, "", true))
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

            if(globalNamedValues.find(owner) == globalNamedValues.end() &&
               isVariableMoved(owner))
            {
                reportError(node->line,
                            "cannot borrow moved value: '" + owner + "'");
                return false;
            }

            const bool wantsMutable = (unary->op == UnaryOpNode::OP_ADDR_MUT);
            auto activeIt = activeBorrowers.find(owner);
            auto mutIt = activeMutBorrower.find(owner);
            if(wantsMutable)
            {
                if(activeIt != activeBorrowers.end() &&
                   !activeIt->second.empty())
                {
                    reportError(
                        node->line,
                        "cannot borrow '" + owner +
                            "' as mutable because it is already borrowed");
                    return false;
                }
                if(mutIt != activeMutBorrower.end())
                {
                    reportError(node->line,
                                "cannot borrow '" + owner +
                                    "' as mutable more than once at a time");
                    return false;
                }
            }
            else
            {
                if(mutIt != activeMutBorrower.end())
                {
                    reportError(
                        node->line,
                        "cannot borrow '" + owner +
                            "' as immutable because it is also borrowed "
                            "as mutable by '" +
                            mutIt->second + "'");
                    return false;
                }
                if(activeIt != activeBorrowers.end() &&
                   !activeIt->second.empty())
                {
                    std::string by = *activeIt->second.begin();
                    reportError(node->line,
                                "cannot borrow '" + owner +
                                    "' while already borrowed by '" + by + "'");
                    return false;
                }
            }

            if(!registerBorrowInCall(owner, path, isWholeOwner))
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

    // Get the object struct pointer and type
    // This supports both simple (p.method()) and chained (a.b.method()) access
    llvm::Value* objPtr;
    std::string structTypeName;

    auto* objId = dynamic_cast<IdentifierNode*>(node->object);
    if(!objId)
    {
        TypeNode* receiverSemanticType =
            getLValueType(node->object, node->line);
        std::string traitName;
        if(auto* traitObjType =
               dynamic_cast<TraitObjectTypeNode*>(receiverSemanticType))
        {
            traitName = traitObjType->traitName;
        }

        if(!traitName.empty())
        {
            llvm::Value* receiverValue = generateExpression(node->object);
            if(!receiverValue)
                return nullptr;

            llvm::Type* traitObjectType = getTraitObjectType(traitName);
            if(!traitObjectType)
                return nullptr;
            if(receiverValue->getType()->isPointerTy() &&
               traitObjectType->isStructTy())
            {
                receiverValue = builder.CreateLoad(
                    traitObjectType, receiverValue, "traitobj.load");
            }

            auto traitIt = traitDefinitions.find(traitName);
            if(traitIt == traitDefinitions.end())
            {
                for(auto it = traitDefinitions.begin();
                    it != traitDefinitions.end(); ++it)
                {
                    if(Helpers::trait_names_equivalent(it->first, traitName))
                    {
                        traitIt = it;
                        break;
                    }
                }
            }
            if(traitIt == traitDefinitions.end() || !traitIt->second)
            {
                reportError(node->line,
                            "unknown trait object type '" + traitName + "'");
                return nullptr;
            }

            TraitDefNode* traitDef = traitIt->second;
            StructMethodNode* traitMethod = nullptr;
            size_t methodIndex = 0;
            for(size_t i = 0; i < traitDef->methods.size(); ++i)
            {
                if(traitDef->methods[i] &&
                   traitDef->methods[i]->name == node->methodName)
                {
                    traitMethod = traitDef->methods[i];
                    methodIndex = i;
                    break;
                }
            }
            if(!traitMethod)
            {
                reportError(node->line, "trait '" + traitName +
                                            "' has no method named '" +
                                            node->methodName + "'");
                return nullptr;
            }
            if(traitMethod->isStatic)
            {
                reportError(node->line,
                            "static trait method '" + traitName +
                                "::" + node->methodName +
                                "' cannot be called on a trait object");
                return nullptr;
            }

            llvm::Value* dataPtr =
                builder.CreateExtractValue(receiverValue, 0, "traitobj.data");
            llvm::Value* vtablePtr =
                builder.CreateExtractValue(receiverValue, 1, "traitobj.vtable");
            llvm::Type* vtableType = getTraitVTableType(traitName);
            if(!vtableType)
                return nullptr;
            auto* vtableStructType = llvm::cast<llvm::StructType>(vtableType);
            llvm::Value* typedVtablePtr = builder.CreateBitCast(
                vtablePtr, llvm::PointerType::get(context, 0),
                "traitobj.vtable.cast");
            llvm::Value* slotPtr = builder.CreateStructGEP(
                vtableStructType, typedVtablePtr,
                static_cast<unsigned>(methodIndex), "traitobj.slot");
#if LLVM_VERSION_MAJOR >= 15
            llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
            llvm::Type* opaquePtr =
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
            llvm::Value* fnPtr =
                builder.CreateLoad(opaquePtr, slotPtr, "traitobj.fn");

            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(opaquePtr);
            if(traitMethod->parameters)
            {
                for(auto* param : traitMethod->parameters->parameters)
                {
                    if(!param || param->name == "self")
                        continue;
                    llvm::Type* paramType = getLLVMTypeFromNode(param->type);
                    if(!paramType)
                    {
                        reportError(node->line,
                                    "unknown type: " +
                                        Helpers::type_name_for_error(param->type));
                        return nullptr;
                    }
                    paramTypes.push_back(paramType);
                }
            }
            llvm::Type* returnType =
                getLLVMTypeFromNode(traitMethod->returnType);
            if(!returnType)
            {
                reportError(node->line,
                            "unknown type: " +
                                Helpers::type_name_for_error(traitMethod->returnType));
                return nullptr;
            }
            llvm::FunctionType* fnType =
                llvm::FunctionType::get(returnType, paramTypes, false);
            llvm::Value* fn = builder.CreateBitCast(
                fnPtr, llvm::PointerType::get(context, 0), "traitobj.fn.cast");

            std::vector<llvm::Value*> callArgs;
            callArgs.push_back(dataPtr);
            size_t argIndex = 0;
            for(auto* argExpr : node->arguments)
            {
                llvm::Value* argVal = generateExpression(argExpr);
                if(!argVal)
                    return nullptr;
                if(argIndex + 1 < paramTypes.size())
                {
                    llvm::Type* expectedType = paramTypes[argIndex + 1];
                    if(argVal->getType() != expectedType)
                    {
                        if(argVal->getType()->isIntegerTy() &&
                           expectedType->isIntegerTy())
                        {
                            argVal = builder.CreateIntCast(
                                argVal, expectedType, true, "trait.arg.cast");
                        }
                        else if(argVal->getType()->isIntegerTy() &&
                                expectedType->isFloatingPointTy())
                        {
                            argVal = builder.CreateSIToFP(argVal, expectedType,
                                                          "trait.arg.sitofp");
                        }
                        else if(argVal->getType()->isFloatingPointTy() &&
                                expectedType->isFloatingPointTy())
                        {
                            argVal = builder.CreateFPCast(argVal, expectedType,
                                                          "trait.arg.fpcast");
                        }
                        else if(argVal->getType()->isPointerTy() &&
                                expectedType->isPointerTy())
                        {
                            argVal = builder.CreateBitCast(argVal, expectedType,
                                                           "trait.arg.ptrcast");
                        }
                        else
                        {
                            reportError(
                                node->line,
                                "argument type mismatch for trait call '" +
                                    node->methodName + "'");
                            return nullptr;
                        }
                    }
                }
                callArgs.push_back(argVal);
                ++argIndex;
            }

            return builder.CreateCall(fnType, fn, callArgs, "traitcall");
        }
    }
    if(objId)
    {
        if(!validateVariableAccessible(objId->name, node->line, objId->col))
            return nullptr;

        auto traitObjVarIt = traitObjectVariableTypes.find(objId->name);
        if(traitObjVarIt != traitObjectVariableTypes.end())
        {
            const std::string& traitName = traitObjVarIt->second;
            auto traitIt = traitDefinitions.find(traitName);
            if(traitIt == traitDefinitions.end())
            {
                for(auto it = traitDefinitions.begin();
                    it != traitDefinitions.end(); ++it)
                {
                    if(Helpers::trait_names_equivalent(it->first, traitName))
                    {
                        traitIt = it;
                        break;
                    }
                }
            }
            if(traitIt == traitDefinitions.end() || !traitIt->second)
            {
                reportError(node->line,
                            "unknown trait object type '" + traitName + "'");
                return nullptr;
            }
            TraitDefNode* traitDef = traitIt->second;
            StructMethodNode* traitMethod = nullptr;
            size_t methodIndex = 0;
            for(size_t i = 0; i < traitDef->methods.size(); ++i)
            {
                if(traitDef->methods[i] &&
                   traitDef->methods[i]->name == node->methodName)
                {
                    traitMethod = traitDef->methods[i];
                    methodIndex = i;
                    break;
                }
            }
            if(!traitMethod)
            {
                reportError(node->line, "trait '" + traitName +
                                            "' has no method named '" +
                                            node->methodName + "'");
                return nullptr;
            }
            if(traitMethod->isStatic)
            {
                reportError(node->line,
                            "static trait method '" + traitName +
                                "::" + node->methodName +
                                "' cannot be called on a trait object");
                return nullptr;
            }
            llvm::Value* objAlloc = namedValues[objId->name];
            if(!objAlloc)
            {
                reportError(node->line, "unknown variable: " + objId->name);
                return nullptr;
            }
            llvm::Type* objType = getTraitObjectType(traitName);
            llvm::Value* obj = builder.CreateLoad(objType, objAlloc,
                                                  objId->name + ".traitobj");
            llvm::Value* dataPtr = builder.CreateExtractValue(
                obj, 0, objId->name + ".traitobj.data");
            llvm::Value* vtablePtr = builder.CreateExtractValue(
                obj, 1, objId->name + ".traitobj.vtable");
            llvm::Type* vtableType = getTraitVTableType(traitName);
            if(!vtableType)
                return nullptr;
            auto* vtableStructType = llvm::cast<llvm::StructType>(vtableType);
            llvm::Value* typedVtablePtr = builder.CreateBitCast(
                vtablePtr, llvm::PointerType::get(context, 0),
                objId->name + ".traitobj.vtable.cast");
            llvm::Value* slotPtr =
                builder.CreateStructGEP(vtableStructType, typedVtablePtr,
                                        static_cast<unsigned>(methodIndex),
                                        objId->name + ".traitobj.slot");
#if LLVM_VERSION_MAJOR >= 15
            llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
            llvm::Type* opaquePtr =
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
            llvm::Value* fnPtr = builder.CreateLoad(
                opaquePtr, slotPtr, objId->name + ".traitobj.fn");

            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(opaquePtr);
            if(traitMethod->parameters)
            {
                for(auto* param : traitMethod->parameters->parameters)
                {
                    if(!param || param->name == "self")
                        continue;
                    llvm::Type* paramType = getLLVMTypeFromNode(param->type);
                    if(!paramType)
                    {
                        reportError(node->line,
                                    "unknown type: " +
                                        Helpers::type_name_for_error(param->type));
                        return nullptr;
                    }
                    paramTypes.push_back(paramType);
                }
            }
            llvm::Type* returnType =
                getLLVMTypeFromNode(traitMethod->returnType);
            if(!returnType)
            {
                reportError(node->line,
                            "unknown type: " +
                                Helpers::type_name_for_error(traitMethod->returnType));
                return nullptr;
            }
            llvm::FunctionType* fnType =
                llvm::FunctionType::get(returnType, paramTypes, false);
            llvm::Value* fn =
                builder.CreateBitCast(fnPtr, llvm::PointerType::get(context, 0),
                                      objId->name + ".traitobj.fn.cast");

            if(!validateTemporaryBorrowArguments(node->arguments,
                                                 node->methodName, objId->name))
                return nullptr;

            std::vector<llvm::Value*> callArgs;
            callArgs.push_back(dataPtr);
            size_t argIndex = 0;
            for(auto* argExpr : node->arguments)
            {
                llvm::Value* argVal = generateExpression(argExpr);
                if(!argVal)
                    return nullptr;
                if(argIndex + 1 < paramTypes.size())
                {
                    llvm::Type* expectedType = paramTypes[argIndex + 1];
                    if(argVal->getType() != expectedType)
                    {
                        if(argVal->getType()->isIntegerTy() &&
                           expectedType->isIntegerTy())
                        {
                            argVal = builder.CreateIntCast(
                                argVal, expectedType, true, "trait.arg.cast");
                        }
                        else if(argVal->getType()->isIntegerTy() &&
                                expectedType->isFloatingPointTy())
                        {
                            argVal = builder.CreateSIToFP(argVal, expectedType,
                                                          "trait.arg.sitofp");
                        }
                        else if(argVal->getType()->isFloatingPointTy() &&
                                expectedType->isFloatingPointTy())
                        {
                            argVal = builder.CreateFPCast(argVal, expectedType,
                                                          "trait.arg.fpcast");
                        }
                        else if(argVal->getType()->isPointerTy() &&
                                expectedType->isPointerTy())
                        {
                            argVal = builder.CreateBitCast(argVal, expectedType,
                                                           "trait.arg.ptrcast");
                        }
                        else
                        {
                            reportError(
                                node->line,
                                "argument type mismatch for trait call '" +
                                    node->methodName + "'");
                            return nullptr;
                        }
                    }
                }
                consumeMoveFromExpression(argExpr, node->line,
                                          "passing argument to trait method '" +
                                              node->methodName + "'");
                callArgs.push_back(argVal);
                ++argIndex;
            }

            if(returnType->isVoidTy())
                return builder.CreateCall(fnType, fn, callArgs, "traitcall");
            return builder.CreateCall(fnType, fn, callArgs, "traitcall");
        }

        TypeNode* receiverSemanticType =
            getLValueType(node->object, node->line);
        std::string traitName;
        if(auto* traitObjType =
               dynamic_cast<TraitObjectTypeNode*>(receiverSemanticType))
        {
            traitName = traitObjType->traitName;
        }

        llvm::Value* receiverValue = generateExpression(node->object);
        if(!traitName.empty() && receiverValue)
        {
            llvm::Type* traitObjectType = getTraitObjectType(traitName);
            if(!traitObjectType)
                return nullptr;
            if(receiverValue->getType()->isPointerTy() &&
               traitObjectType->isStructTy())
            {
                receiverValue = builder.CreateLoad(
                    traitObjectType, receiverValue, "traitobj.load");
            }
        }

        if(receiverValue && receiverValue->getType()->isStructTy())
        {
            auto* receiverStructType =
                llvm::cast<llvm::StructType>(receiverValue->getType());
            if(traitName.empty())
            {
                std::string structTypeName =
                    receiverStructType->getName().str();
                const std::string prefix = "trait.obj.";
                if(structTypeName.rfind(prefix, 0) == 0 &&
                   structTypeName.size() > prefix.size())
                {
                    traitName = structTypeName.substr(prefix.size());
                }
            }
            if(traitName.empty())
                goto trait_object_receiver_fallback;

            auto traitIt = traitDefinitions.find(traitName);
            if(traitIt == traitDefinitions.end())
            {
                for(auto it = traitDefinitions.begin();
                    it != traitDefinitions.end(); ++it)
                {
                    if(Helpers::trait_names_equivalent(it->first, traitName))
                    {
                        traitIt = it;
                        break;
                    }
                }
            }
            if(traitIt == traitDefinitions.end() || !traitIt->second)
            {
                reportError(node->line,
                            "unknown trait object type '" + traitName + "'");
                return nullptr;
            }
            TraitDefNode* traitDef = traitIt->second;
            StructMethodNode* traitMethod = nullptr;
            size_t methodIndex = 0;
            for(size_t i = 0; i < traitDef->methods.size(); ++i)
            {
                if(traitDef->methods[i] &&
                   traitDef->methods[i]->name == node->methodName)
                {
                    traitMethod = traitDef->methods[i];
                    methodIndex = i;
                    break;
                }
            }
            if(!traitMethod)
            {
                reportError(node->line, "trait '" + traitName +
                                            "' has no method named '" +
                                            node->methodName + "'");
                return nullptr;
            }
            if(traitMethod->isStatic)
            {
                reportError(node->line,
                            "static trait method '" + traitName +
                                "::" + node->methodName +
                                "' cannot be called on a trait object");
                return nullptr;
            }

            llvm::Value* dataPtr =
                builder.CreateExtractValue(receiverValue, 0, "traitobj.data");
            llvm::Value* vtablePtr =
                builder.CreateExtractValue(receiverValue, 1, "traitobj.vtable");
            llvm::Type* vtableType = getTraitVTableType(traitName);
            if(!vtableType)
                return nullptr;
            auto* vtableStructType = llvm::cast<llvm::StructType>(vtableType);
            llvm::Value* typedVtablePtr = builder.CreateBitCast(
                vtablePtr, llvm::PointerType::get(context, 0),
                "traitobj.vtable.cast");
            llvm::Value* slotPtr = builder.CreateStructGEP(
                vtableStructType, typedVtablePtr,
                static_cast<unsigned>(methodIndex), "traitobj.slot");
#if LLVM_VERSION_MAJOR >= 15
            llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
            llvm::Type* opaquePtr =
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
            llvm::Value* fnPtr =
                builder.CreateLoad(opaquePtr, slotPtr, "traitobj.fn");

            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(opaquePtr);
            if(traitMethod->parameters)
            {
                for(auto* param : traitMethod->parameters->parameters)
                {
                    if(!param || param->name == "self")
                        continue;
                    llvm::Type* paramType = getLLVMTypeFromNode(param->type);
                    if(!paramType)
                    {
                        reportError(node->line,
                                    "unknown type: " +
                                        Helpers::type_name_for_error(param->type));
                        return nullptr;
                    }
                    paramTypes.push_back(paramType);
                }
            }
            llvm::Type* returnType =
                getLLVMTypeFromNode(traitMethod->returnType);
            if(!returnType)
            {
                reportError(node->line,
                            "unknown type: " +
                                Helpers::type_name_for_error(traitMethod->returnType));
                return nullptr;
            }
            llvm::FunctionType* fnType =
                llvm::FunctionType::get(returnType, paramTypes, false);
            llvm::Value* fn = builder.CreateBitCast(
                fnPtr, llvm::PointerType::get(context, 0), "traitobj.fn.cast");

            std::vector<llvm::Value*> callArgs;
            callArgs.push_back(dataPtr);
            size_t argIndex = 0;
            for(auto* argExpr : node->arguments)
            {
                llvm::Value* argVal = generateExpression(argExpr);
                if(!argVal)
                    return nullptr;
                if(argIndex + 1 < paramTypes.size())
                {
                    llvm::Type* expectedType = paramTypes[argIndex + 1];
                    if(argVal->getType() != expectedType)
                    {
                        if(argVal->getType()->isIntegerTy() &&
                           expectedType->isIntegerTy())
                        {
                            argVal = builder.CreateIntCast(
                                argVal, expectedType, true, "trait.arg.cast");
                        }
                        else if(argVal->getType()->isIntegerTy() &&
                                expectedType->isFloatingPointTy())
                        {
                            argVal = builder.CreateSIToFP(argVal, expectedType,
                                                          "trait.arg.sitofp");
                        }
                        else if(argVal->getType()->isFloatingPointTy() &&
                                expectedType->isFloatingPointTy())
                        {
                            argVal = builder.CreateFPCast(argVal, expectedType,
                                                          "trait.arg.fpcast");
                        }
                        else if(argVal->getType()->isPointerTy() &&
                                expectedType->isPointerTy())
                        {
                            argVal = builder.CreateBitCast(argVal, expectedType,
                                                           "trait.arg.ptrcast");
                        }
                        else
                        {
                            reportError(
                                node->line,
                                "argument type mismatch for trait call '" +
                                    node->methodName + "'");
                            return nullptr;
                        }
                    }
                }
                callArgs.push_back(argVal);
                ++argIndex;
            }

            if(returnType->isVoidTy())
                return builder.CreateCall(fnType, fn, callArgs, "traitcall");
            return builder.CreateCall(fnType, fn, callArgs, "traitcall");
        }

    trait_object_receiver_fallback:
        // Handle built-in string methods (push_str, etc.)
        {
            auto strTypeIt = variableTypes.find(objId->name);
            if(strTypeIt != variableTypes.end() &&
               strTypeIt->second == TypeNode::TYPE_STRING)
            {
                if(node->methodName == "push_str")
                {
                    if(node->arguments.size() != 1)
                    {
                        reportError(node->line,
                                    "push_str expects one argument");
                        return nullptr;
                    }
                    if(constantVariables.count(objId->name))
                    {
                        reportError(node->line,
                                    "cannot call push_str on immutable "
                                    "string '" +
                                        objId->name + "'");
                        return nullptr;
                    }
                    auto borrowIt = activeBorrowers.find(objId->name);
                    if(borrowIt != activeBorrowers.end() &&
                       !borrowIt->second.empty())
                    {
                        reportError(node->line, "cannot mutate '" +
                                                    objId->name +
                                                    "' while it is borrowed");
                        return nullptr;
                    }
                    auto mutBorrowIt = activeMutBorrower.find(objId->name);
                    if(mutBorrowIt != activeMutBorrower.end())
                    {
                        reportError(
                            node->line,
                            "cannot mutate '" + objId->name +
                                "' directly while mutably borrowed by '" +
                                mutBorrowIt->second + "'");
                        return nullptr;
                    }
                    llvm::Value* allocaPtr = namedValues[objId->name];
                    if(!allocaPtr)
                    {
                        reportError(node->line,
                                    "unknown variable: " + objId->name);
                        return nullptr;
                    }
#if LLVM_VERSION_MAJOR >= 15
                    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                    llvm::Type* ptrType = llvm::PointerType::get(
                        llvm::Type::getInt8Ty(context), 0);
#endif
                    llvm::Value* currentStr = builder.CreateLoad(
                        ptrType, allocaPtr, objId->name + ".load");
                    llvm::Value* suffix =
                        generateExpression(node->arguments[0]);
                    if(!suffix)
                        return nullptr;
                    llvm::FunctionType* concatFnType = llvm::FunctionType::get(
                        ptrType, {ptrType, ptrType}, false);
                    llvm::FunctionCallee concatFn = module->getOrInsertFunction(
                        "__mlang_std_strbuf_concat", concatFnType);
                    llvm::Value* newStr = builder.CreateCall(
                        concatFn, {currentStr, suffix}, "push_str.result");
                    builder.CreateStore(newStr, allocaPtr);
                    return llvm::Constant::getNullValue(ptrType);
                }
                // --- clone() ---
                if(node->methodName == "clone")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "clone() takes no arguments");
                        return nullptr;
                    }
                    llvm::Value* allocaPtr = namedValues[objId->name];
                    if(!allocaPtr)
                    {
                        reportError(node->line,
                                    "unknown variable: " + objId->name);
                        return nullptr;
                    }
#if LLVM_VERSION_MAJOR >= 15
                    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                    llvm::Type* ptrType = llvm::PointerType::get(
                        llvm::Type::getInt8Ty(context), 0);
#endif
                    llvm::Value* currentStr = builder.CreateLoad(
                        ptrType, allocaPtr, objId->name + ".load");
                    llvm::FunctionType* cloneFnType =
                        llvm::FunctionType::get(ptrType, {ptrType}, false);
                    llvm::FunctionCallee cloneFn = module->getOrInsertFunction(
                        "__mlang_std_strbuf_clone", cloneFnType);
                    return builder.CreateCall(cloneFn, {currentStr},
                                              objId->name + ".clone");
                }
                // --- len() ---
                if(node->methodName == "len")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "len() takes no arguments");
                        return nullptr;
                    }
                    llvm::Value* allocaPtr = namedValues[objId->name];
                    if(!allocaPtr)
                    {
                        reportError(node->line,
                                    "unknown variable: " + objId->name);
                        return nullptr;
                    }
#if LLVM_VERSION_MAJOR >= 15
                    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                    llvm::Type* ptrType = llvm::PointerType::get(
                        llvm::Type::getInt8Ty(context), 0);
#endif
                    llvm::Value* currentStr = builder.CreateLoad(
                        ptrType, allocaPtr, objId->name + ".load");
                    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
                    llvm::FunctionType* lenFnType =
                        llvm::FunctionType::get(i64Type, {ptrType}, false);
                    llvm::FunctionCallee lenFn = module->getOrInsertFunction(
                        "__mlang_std_strbuf_len", lenFnType);
                    return builder.CreateCall(lenFn, {currentStr},
                                              objId->name + ".len");
                }
                // --- is_empty() ---
                if(node->methodName == "is_empty")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line,
                                    "is_empty() takes no arguments");
                        return nullptr;
                    }
                    llvm::Value* allocaPtr = namedValues[objId->name];
                    if(!allocaPtr)
                    {
                        reportError(node->line,
                                    "unknown variable: " + objId->name);
                        return nullptr;
                    }
#if LLVM_VERSION_MAJOR >= 15
                    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                    llvm::Type* ptrType = llvm::PointerType::get(
                        llvm::Type::getInt8Ty(context), 0);
#endif
                    llvm::Value* currentStr = builder.CreateLoad(
                        ptrType, allocaPtr, objId->name + ".load");
                    llvm::Type* intType = llvm::Type::getInt32Ty(context);
                    llvm::FunctionType* emptyFnType =
                        llvm::FunctionType::get(intType, {ptrType}, false);
                    llvm::FunctionCallee emptyFn = module->getOrInsertFunction(
                        "__mlang_std_strbuf_is_empty", emptyFnType);
                    return builder.CreateCall(emptyFn, {currentStr},
                                              objId->name + ".is_empty");
                }
                reportError(node->line, "string has no method named '" +
                                            node->methodName + "'");
                return nullptr;
            }
        }

        // Handle built-in list methods (len)
        {
            auto listTypeIt = variableTypes.find(objId->name);
            if(listTypeIt != variableTypes.end() &&
               listTypeIt->second == TypeNode::TYPE_LIST)
            {
                if(node->methodName == "len")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "len() takes no arguments");
                        return nullptr;
                    }
                    llvm::Value* allocaPtr = namedValues[objId->name];
                    if(!allocaPtr)
                    {
                        reportError(node->line,
                                    "unknown variable: " + objId->name);
                        return nullptr;
                    }
                    // List struct layout: {i64 count, ptr data}
                    // Extract field 0 (count) via extractvalue after loading
                    // the struct
                    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
                    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                    llvm::Type* ptrType = llvm::PointerType::get(
                        llvm::Type::getInt8Ty(context), 0);
#endif
                    std::vector<llvm::Type*> listStructTypes = {i64Type,
                                                                ptrType};
                    llvm::StructType* listStructType =
                        llvm::StructType::get(context, listStructTypes);
                    llvm::Value* listStruct = builder.CreateLoad(
                        listStructType, allocaPtr, objId->name + ".load");
                    return builder.CreateExtractValue(listStruct, 0,
                                                      objId->name + ".len");
                }
                // ---- helper lambdas for Vec methods ----
                llvm::Value* allocaPtr2 = namedValues[objId->name];
                if(!allocaPtr2)
                {
                    reportError(node->line, "unknown variable: " + objId->name);
                    return nullptr;
                }
                llvm::Type* voidType2 = llvm::Type::getVoidTy(context);
#if LLVM_VERSION_MAJOR >= 15
                llvm::Type* opaquePtrType = llvm::PointerType::get(context, 0);
#else
                llvm::Type* opaquePtrType =
                    llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
                llvm::Type* i32Type2 = llvm::Type::getInt32Ty(context);
                llvm::Type* i64Type2 = llvm::Type::getInt64Ty(context);
                llvm::Type* i1Type2 = llvm::Type::getInt1Ty(context);

                // Determine element type for overload selection
                TypeNode::TypeKind elemKind2 = TypeNode::TYPE_I32;
                TypeNode* elemTypeNode2 = nullptr;
                {
                    auto elit = listElementTypes.find(objId->name);
                    if(elit != listElementTypes.end() && elit->second)
                    {
                        elemKind2 = elit->second->kind;
                        elemTypeNode2 = elit->second;
                    }
                }
                bool elemIsI64 = (elemKind2 == TypeNode::TYPE_I64 ||
                                  elemKind2 == TypeNode::TYPE_U64);
                bool elemIsStr = (elemKind2 == TypeNode::TYPE_STRING ||
                                  elemKind2 == TypeNode::TYPE_STR8);
                auto arrayCapIt2 = arrayCapacities.find(objId->name);
                bool receiverIsArray2 = arrayCapIt2 != arrayCapacities.end();
                llvm::Value* arrayCapacity2 =
                    receiverIsArray2
                        ? llvm::ConstantInt::get(i64Type2, arrayCapIt2->second)
                        : nullptr;
                auto checkKnownArrayGrowth2 =
                    [&](int64_t add, const std::string& methodName) -> bool
                {
                    if(!receiverIsArray2)
                        return true;
                    if(add < 0)
                    {
                        reportError(node->line, methodName +
                                                    "() source length must be "
                                                    "non-negative");
                        return false;
                    }
                    if(add > arrayCapIt2->second)
                    {
                        reportError(
                            node->line,
                            methodName + "() would exceed " +
                                std::string("array<T, N> capacity: add=") +
                                std::to_string(add) + " capacity=" +
                                std::to_string(arrayCapIt2->second));
                        return false;
                    }
                    auto lenIt = arrayKnownLengths.find(objId->name);
                    if(lenIt == arrayKnownLengths.end())
                        return true;
                    if(lenIt->second > arrayCapIt2->second - add)
                    {
                        reportError(
                            node->line,
                            methodName + "() would exceed " +
                                std::string("array<T, N> capacity: len=") +
                                std::to_string(lenIt->second) +
                                " add=" + std::to_string(add) +
                                " capacity=" +
                                std::to_string(arrayCapIt2->second));
                        return false;
                    }
                    return true;
                };
                auto updateKnownArrayLength2 =
                    [&](std::optional<int64_t> nextLength)
                {
                    if(!receiverIsArray2)
                        return;
                    if(nextLength)
                        arrayKnownLengths[objId->name] = *nextLength;
                    else
                        arrayKnownLengths.erase(objId->name);
                };

                auto callVecVoidFn = [&](const std::string& fnName)
                {
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        voidType2, {opaquePtrType}, false);
                    llvm::FunctionCallee fn =
                        module->getOrInsertFunction(fnName, ft);
                    builder.CreateCall(fn, {allocaPtr2});
                    return llvm::Constant::getNullValue(voidType2);
                };
                auto emitNonEmptyCheck2 =
                    [&](llvm::Value* count,
                        const std::string& methodName) -> bool
                {
                    if(receiverIsArray2)
                    {
                        auto lenIt = arrayKnownLengths.find(objId->name);
                        if(lenIt != arrayKnownLengths.end() &&
                           lenIt->second <= 0)
                        {
                            reportError(node->line,
                                        methodName +
                                            "() requires a non-empty array");
                            return false;
                        }
                    }

                    initializeFormatFunctions();
                    llvm::Function* function =
                        builder.GetInsertBlock()->getParent();
                    llvm::BasicBlock* okBB = llvm::BasicBlock::Create(
                        context, methodName + ".non_empty", function);
                    llvm::BasicBlock* failBB = llvm::BasicBlock::Create(
                        context, methodName + ".empty", function);
                    llvm::Value* nonEmpty = builder.CreateICmpSGT(
                        count, llvm::ConstantInt::get(i64Type2, 0),
                        methodName + ".has_items");
                    builder.CreateCondBr(nonEmpty, okBB, failBB);

                    builder.SetInsertPoint(failBB);
#if LLVM_VERSION_MAJOR >= 21
                    llvm::Value* formatStr = builder.CreateGlobalString(
                        (methodName + "() requires a non-empty list/array\n")
                            .c_str(),
                        methodName + ".empty.msg");
#else
                    llvm::Value* formatStr = builder.CreateGlobalStringPtr(
                        (methodName + "() requires a non-empty list/array\n")
                            .c_str(),
                        methodName + ".empty.msg");
#endif
                    llvm::Value* stderrVal =
                        builder.CreateLoad(opaquePtrType, stderrPtr, "stderr");
                    builder.CreateCall(fprintfFunc, {stderrVal, formatStr});
                    builder.CreateCall(abortFunc, {});
                    builder.CreateUnreachable();

                    builder.SetInsertPoint(okBB);
                    return true;
                };

                // --- is_empty() ---
                if(node->methodName == "is_empty")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line,
                                    "is_empty() takes no arguments");
                        return nullptr;
                    }
                    // reuse the len path: compare count == 0
                    std::vector<llvm::Type*> lsTypes = {i64Type2,
                                                        opaquePtrType};
                    llvm::StructType* lsType =
                        llvm::StructType::get(context, lsTypes);
                    llvm::Value* ls = builder.CreateLoad(lsType, allocaPtr2,
                                                         objId->name + ".load");
                    llvm::Value* cnt = builder.CreateExtractValue(ls, 0);
                    return builder.CreateICmpEQ(
                        cnt, llvm::ConstantInt::get(i64Type2, 0),
                        objId->name + ".is_empty");
                }
                // --- fill(val) ---
                if(node->methodName == "fill")
                {
                    if(!receiverIsArray2)
                    {
                        reportError(node->line,
                                    "fill() is only available for array<T, N>");
                        return nullptr;
                    }
                    if(node->arguments.size() != 1)
                    {
                        reportError(node->line, "fill() takes one argument");
                        return nullptr;
                    }
                    llvm::Value* val2 = generateExpression(node->arguments[0]);
                    if(!val2)
                        return nullptr;

                    if(elemIsStr)
                    {
                        llvm::FunctionType* ft = llvm::FunctionType::get(
                            voidType2,
                            {opaquePtrType, opaquePtrType, i64Type2}, false);
                        llvm::FunctionCallee fn =
                            module->getOrInsertFunction(
                                "__mlang_std_array_fill_str", ft);
                        builder.CreateCall(
                            fn, {allocaPtr2, val2, arrayCapacity2});
                        updateKnownArrayLength2(arrayCapIt2->second);
                        return llvm::Constant::getNullValue(voidType2);
                    }
                    if(elemIsI64)
                    {
                        if(val2->getType() != i64Type2)
                            val2 = builder.CreateSExt(val2, i64Type2);
                        llvm::FunctionType* ft = llvm::FunctionType::get(
                            voidType2,
                            {opaquePtrType, i64Type2, i64Type2}, false);
                        llvm::FunctionCallee fn =
                            module->getOrInsertFunction(
                                "__mlang_std_array_fill_i64", ft);
                        builder.CreateCall(
                            fn, {allocaPtr2, val2, arrayCapacity2});
                        updateKnownArrayLength2(arrayCapIt2->second);
                        return llvm::Constant::getNullValue(voidType2);
                    }
                    if(elemKind2 == TypeNode::TYPE_INT ||
                       elemKind2 == TypeNode::TYPE_I32 ||
                       elemKind2 == TypeNode::TYPE_U32 ||
                       elemKind2 == TypeNode::TYPE_I16 ||
                       elemKind2 == TypeNode::TYPE_U16 ||
                       elemKind2 == TypeNode::TYPE_I8 ||
                       elemKind2 == TypeNode::TYPE_U8 ||
                       elemKind2 == TypeNode::TYPE_BOOL)
                    {
                        if(val2->getType() != i32Type2)
                            val2 = builder.CreateTrunc(val2, i32Type2);
                        llvm::FunctionType* ft = llvm::FunctionType::get(
                            voidType2,
                            {opaquePtrType, i32Type2, i64Type2}, false);
                        llvm::FunctionCallee fn =
                            module->getOrInsertFunction(
                                "__mlang_std_array_fill_i32", ft);
                        builder.CreateCall(
                            fn, {allocaPtr2, val2, arrayCapacity2});
                        updateKnownArrayLength2(arrayCapIt2->second);
                        return llvm::Constant::getNullValue(voidType2);
                    }

                    llvm::Type* elemLlvmType =
                        elemTypeNode2 ? getLLVMTypeFromNode(elemTypeNode2)
                                      : val2->getType();
                    if(!elemLlvmType || val2->getType() != elemLlvmType)
                    {
                        reportError(node->line,
                                    "fill() argument type mismatch for array "
                                    "element");
                        return nullptr;
                    }
                    llvm::AllocaInst* tmpElem = builder.CreateAlloca(
                        elemLlvmType, nullptr, "array.fill.tmp");
                    builder.CreateStore(val2, tmpElem);
                    llvm::Value* elemPtrAsOpaque = builder.CreateBitCast(
                        tmpElem, opaquePtrType, "array.fill.ptr");
                    uint64_t elemSizeU =
                        module->getDataLayout().getTypeAllocSize(elemLlvmType);
                    llvm::Value* elemSize =
                        llvm::ConstantInt::get(i64Type2, elemSizeU);
                    llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                        voidType2,
                        {opaquePtrType, opaquePtrType, i64Type2, i64Type2},
                        false);
                    llvm::FunctionCallee fnRaw =
                        module->getOrInsertFunction(
                            "__mlang_std_array_fill_raw", ftRaw);
                    builder.CreateCall(fnRaw,
                                       {allocaPtr2, elemPtrAsOpaque, elemSize,
                                        arrayCapacity2});
                    updateKnownArrayLength2(arrayCapIt2->second);
                    return llvm::Constant::getNullValue(voidType2);
                }
                // --- push(val) ---
                if(node->methodName == "push")
                {
                    if(node->arguments.size() != 1)
                    {
                        reportError(node->line, "push() takes one argument");
                        return nullptr;
                    }
                    if(!checkKnownArrayGrowth2(1, "push"))
                        return nullptr;
                    llvm::Value* val2 = generateExpression(node->arguments[0]);
                    if(!val2)
                        return nullptr;

                    std::string fnName2;
                    llvm::Type* valType2;
                    if(elemIsStr)
                    {
                        fnName2 = "__mlang_std_vec_push_str";
                        valType2 = opaquePtrType;
                    }
                    else if(elemIsI64)
                    {
                        fnName2 = "__mlang_std_vec_push_i64";
                        valType2 = i64Type2;
                        if(val2->getType() != i64Type2)
                            val2 = builder.CreateSExt(val2, i64Type2);
                    }
                    else if(elemKind2 == TypeNode::TYPE_INT ||
                            elemKind2 == TypeNode::TYPE_I32 ||
                            elemKind2 == TypeNode::TYPE_U32 ||
                            elemKind2 == TypeNode::TYPE_I16 ||
                            elemKind2 == TypeNode::TYPE_U16 ||
                            elemKind2 == TypeNode::TYPE_I8 ||
                            elemKind2 == TypeNode::TYPE_U8 ||
                            elemKind2 == TypeNode::TYPE_BOOL)
                    {
                        fnName2 = "__mlang_std_vec_push_i32";
                        valType2 = i32Type2;
                        if(val2->getType() != i32Type2)
                            val2 = builder.CreateTrunc(val2, i32Type2);
                    }
                    else
                    {
                        llvm::Type* elemLlvmType =
                            elemTypeNode2 ? getLLVMTypeFromNode(elemTypeNode2)
                                          : val2->getType();
                        if(!elemLlvmType)
                        {
                            reportError(
                                node->line,
                                "unsupported list element type for push()");
                            return nullptr;
                        }
                        llvm::AllocaInst* tmpElem = builder.CreateAlloca(
                            elemLlvmType, nullptr, "vec.push.tmp");
                        if(val2->getType() != elemLlvmType)
                        {
                            reportError(node->line,
                                        "push() argument type mismatch for "
                                        "list element");
                            return nullptr;
                        }
                        builder.CreateStore(val2, tmpElem);
                        llvm::Value* elemPtrAsOpaque = builder.CreateBitCast(
                            tmpElem, opaquePtrType, "vec.push.ptr");
                        uint64_t elemSizeU =
                            module->getDataLayout().getTypeAllocSize(
                                elemLlvmType);
                        llvm::Value* elemSize = llvm::ConstantInt::get(
                            i64Type2, (uint64_t)elemSizeU);
                        llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                            voidType2,
                            receiverIsArray2
                                ? std::vector<llvm::Type*>{
                                      opaquePtrType, opaquePtrType, i64Type2,
                                      i64Type2}
                                : std::vector<llvm::Type*>{
                                      opaquePtrType, opaquePtrType, i64Type2},
                            false);
                        llvm::FunctionCallee fnRaw =
                            module->getOrInsertFunction(
                                receiverIsArray2
                                    ? "__mlang_std_array_push_raw"
                                    : "__mlang_std_vec_push_raw",
                                ftRaw);
                        if(receiverIsArray2)
                            builder.CreateCall(fnRaw,
                                               {allocaPtr2, elemPtrAsOpaque,
                                                elemSize, arrayCapacity2});
                        else
                            builder.CreateCall(
                                fnRaw, {allocaPtr2, elemPtrAsOpaque, elemSize});
                        if(receiverIsArray2)
                        {
                            auto lenIt = arrayKnownLengths.find(objId->name);
                            if(lenIt != arrayKnownLengths.end())
                                updateKnownArrayLength2(lenIt->second + 1);
                        }
                        return llvm::Constant::getNullValue(voidType2);
                    }
                    llvm::FunctionType* ft2 = llvm::FunctionType::get(
                        voidType2,
                        receiverIsArray2
                            ? std::vector<llvm::Type*>{
                                  opaquePtrType, valType2, i64Type2}
                            : std::vector<llvm::Type*>{opaquePtrType,
                                                       valType2},
                        false);
                    if(receiverIsArray2)
                    {
                        if(fnName2 == "__mlang_std_vec_push_str")
                            fnName2 = "__mlang_std_array_push_str";
                        else if(fnName2 == "__mlang_std_vec_push_i64")
                            fnName2 = "__mlang_std_array_push_i64";
                        else
                            fnName2 = "__mlang_std_array_push_i32";
                    }
                    llvm::FunctionCallee fn2 =
                        module->getOrInsertFunction(fnName2, ft2);
                    if(receiverIsArray2)
                        builder.CreateCall(
                            fn2, {allocaPtr2, val2, arrayCapacity2});
                    else
                        builder.CreateCall(fn2, {allocaPtr2, val2});
                    if(receiverIsArray2)
                    {
                        auto lenIt = arrayKnownLengths.find(objId->name);
                        if(lenIt != arrayKnownLengths.end())
                            updateKnownArrayLength2(lenIt->second + 1);
                    }
                    return llvm::Constant::getNullValue(voidType2);
                }
                // --- extend(list_or_array) ---
                if(node->methodName == "extend")
                {
                    if(node->arguments.size() != 1)
                    {
                        reportError(node->line, "extend() takes one argument");
                        return nullptr;
                    }
                    std::optional<int64_t> sourceKnownLength =
                        fixedArrayExpressionKnownLength(node->arguments[0]);
                    if(receiverIsArray2 && sourceKnownLength &&
                       !checkKnownArrayGrowth2(*sourceKnownLength, "extend"))
                        return nullptr;

                    llvm::Type* elemLlvmType =
                        elemTypeNode2 ? getLLVMTypeFromNode(elemTypeNode2)
                                      : getLLVMType(elemKind2);
                    if(!elemLlvmType)
                    {
                        reportError(node->line,
                                    "unsupported container element type for "
                                    "extend()");
                        return nullptr;
                    }

                    TypeNode* srcType =
                        inferExpressionTypeNode(node->arguments[0],
                                                node->line);
                    TypeNode* srcElemType = nullptr;
                    if(auto* srcArray = dynamic_cast<ArrayTypeNode*>(srcType))
                        srcElemType = srcArray->elementType;
                    else if(auto* srcList =
                                dynamic_cast<GenericListTypeNode*>(srcType))
                        srcElemType = srcList->elementType;
                    else if(srcType && srcType->kind == TypeNode::TYPE_LIST)
                        srcElemType = nullptr;
                    else
                    {
                        reportError(node->line,
                                    "extend() expects a list<T>, vec![...], or "
                                    "array<T, N> argument");
                        return nullptr;
                    }
                    if(srcElemType)
                    {
                        llvm::Type* srcElemLlvm =
                            getLLVMTypeFromNode(srcElemType);
                        bool sourceIsLiteral =
                            dynamic_cast<ListLiteralNode*>(
                                node->arguments[0]) ||
                            dynamic_cast<ArrayFillNode*>(node->arguments[0]);
                        bool literalIntegerCoercion =
                            sourceIsLiteral && srcElemLlvm &&
                            srcElemLlvm->isIntegerTy() &&
                            elemLlvmType->isIntegerTy();
                        if(srcElemLlvm && srcElemLlvm != elemLlvmType &&
                           !literalIntegerCoercion)
                        {
                            reportError(node->line,
                                        "extend() argument element type does "
                                        "not match array element type");
                            return nullptr;
                        }
                    }

                    llvm::Value* srcPtr = nullptr;
                    if(dynamic_cast<IdentifierNode*>(node->arguments[0]) ||
                       dynamic_cast<FieldAccessNode*>(node->arguments[0]))
                        srcPtr = getLValuePointer(node->arguments[0],
                                                  node->line);

                    if(!srcPtr)
                    {
                        llvm::Value* srcValue = nullptr;
                        if(auto* listLit = dynamic_cast<ListLiteralNode*>(
                               node->arguments[0]))
                            srcValue = generateListLiteral(listLit,
                                                           elemLlvmType);
                        else if(auto* arrFill = dynamic_cast<ArrayFillNode*>(
                                    node->arguments[0]))
                            srcValue = generateArrayFill(arrFill,
                                                         elemLlvmType);
                        else
                            srcValue = generateExpression(node->arguments[0]);
                        if(!srcValue)
                            return nullptr;
                        llvm::AllocaInst* tmpList = builder.CreateAlloca(
                            srcValue->getType(), nullptr, "array.extend.tmp");
                        builder.CreateStore(srcValue, tmpList);
                        srcPtr = tmpList;
                    }

                    std::string fnName =
                        elemIsStr
                            ? (receiverIsArray2 ? "__mlang_std_array_extend_str"
                                                : "__mlang_std_vec_extend_str")
                        : elemIsI64
                            ? (receiverIsArray2 ? "__mlang_std_array_extend_i64"
                                                : "__mlang_std_vec_extend_i64")
                            : (receiverIsArray2 ? "__mlang_std_array_extend_i32"
                                                : "__mlang_std_vec_extend_i32");
                    if(!(elemIsStr || elemIsI64 ||
                         elemKind2 == TypeNode::TYPE_INT ||
                         elemKind2 == TypeNode::TYPE_I32 ||
                         elemKind2 == TypeNode::TYPE_U32 ||
                         elemKind2 == TypeNode::TYPE_I16 ||
                         elemKind2 == TypeNode::TYPE_U16 ||
                         elemKind2 == TypeNode::TYPE_I8 ||
                         elemKind2 == TypeNode::TYPE_U8 ||
                         elemKind2 == TypeNode::TYPE_BOOL))
                    {
                        uint64_t elemSizeU =
                            module->getDataLayout().getTypeAllocSize(
                                elemLlvmType);
                        llvm::Value* elemSize = llvm::ConstantInt::get(
                            i64Type2, (uint64_t)elemSizeU);
                        llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                            voidType2,
                            receiverIsArray2
                                ? std::vector<llvm::Type*>{
                                      opaquePtrType, opaquePtrType, i64Type2,
                                      i64Type2}
                                : std::vector<llvm::Type*>{
                                      opaquePtrType, opaquePtrType, i64Type2},
                            false);
                        llvm::FunctionCallee fnRaw =
                            module->getOrInsertFunction(
                                receiverIsArray2
                                    ? "__mlang_std_array_extend_raw"
                                    : "__mlang_std_vec_extend_raw",
                                ftRaw);
                        if(receiverIsArray2)
                            builder.CreateCall(fnRaw,
                                               {allocaPtr2, srcPtr, elemSize,
                                                arrayCapacity2});
                        else
                            builder.CreateCall(fnRaw,
                                               {allocaPtr2, srcPtr, elemSize});
                        if(receiverIsArray2)
                        {
                            auto lenIt = arrayKnownLengths.find(objId->name);
                            if(lenIt != arrayKnownLengths.end() &&
                               sourceKnownLength)
                                updateKnownArrayLength2(lenIt->second +
                                                        *sourceKnownLength);
                            else
                                updateKnownArrayLength2(std::nullopt);
                        }
                        return llvm::Constant::getNullValue(voidType2);
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        voidType2,
                        receiverIsArray2
                            ? std::vector<llvm::Type*>{
                                  opaquePtrType, opaquePtrType, i64Type2}
                            : std::vector<llvm::Type*>{
                                  opaquePtrType, opaquePtrType},
                        false);
                    llvm::FunctionCallee fn =
                        module->getOrInsertFunction(fnName, ft);
                    if(receiverIsArray2)
                        builder.CreateCall(fn,
                                           {allocaPtr2, srcPtr, arrayCapacity2});
                    else
                        builder.CreateCall(fn, {allocaPtr2, srcPtr});
                    if(receiverIsArray2)
                    {
                        auto lenIt = arrayKnownLengths.find(objId->name);
                        if(lenIt != arrayKnownLengths.end() &&
                           sourceKnownLength)
                            updateKnownArrayLength2(lenIt->second +
                                                    *sourceKnownLength);
                        else
                            updateKnownArrayLength2(std::nullopt);
                    }
                    return llvm::Constant::getNullValue(voidType2);
                }
                // --- pop() ---
                if(node->methodName == "pop")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "pop() takes no arguments");
                        return nullptr;
                    }
                    std::vector<llvm::Type*> lsTypes = {i64Type2,
                                                        opaquePtrType};
                    llvm::StructType* lsType =
                        llvm::StructType::get(context, lsTypes);
                    llvm::Value* ls = builder.CreateLoad(lsType, allocaPtr2,
                                                         objId->name + ".load");
                    llvm::Value* cnt = builder.CreateExtractValue(ls, 0);
                    if(!emitNonEmptyCheck2(cnt, "pop"))
                        return nullptr;
                    if(elemIsStr || elemIsI64 ||
                       elemKind2 == TypeNode::TYPE_INT ||
                       elemKind2 == TypeNode::TYPE_I32 ||
                       elemKind2 == TypeNode::TYPE_U32 ||
                       elemKind2 == TypeNode::TYPE_I16 ||
                       elemKind2 == TypeNode::TYPE_U16 ||
                       elemKind2 == TypeNode::TYPE_I8 ||
                       elemKind2 == TypeNode::TYPE_U8 ||
                       elemKind2 == TypeNode::TYPE_BOOL)
                    {
                        std::string fnName3;
                        llvm::Type* retType3;
                        if(elemIsStr)
                        {
                            fnName3 = "__mlang_std_vec_pop_str";
                            retType3 = opaquePtrType;
                        }
                        else if(elemIsI64)
                        {
                            fnName3 = "__mlang_std_vec_pop_i64";
                            retType3 = i64Type2;
                        }
                        else
                        {
                            fnName3 = "__mlang_std_vec_pop_i32";
                            retType3 = i32Type2;
                        }
                        llvm::FunctionType* ft3 = llvm::FunctionType::get(
                            retType3, {opaquePtrType}, false);
                        llvm::FunctionCallee fn3 =
                            module->getOrInsertFunction(fnName3, ft3);
                        llvm::Value* popped = builder.CreateCall(
                            fn3, {allocaPtr2}, objId->name + ".pop");
                        if(receiverIsArray2)
                        {
                            auto lenIt = arrayKnownLengths.find(objId->name);
                            if(lenIt != arrayKnownLengths.end() &&
                               lenIt->second > 0)
                                updateKnownArrayLength2(lenIt->second - 1);
                        }
                        return popped;
                    }
                    llvm::Type* elemLlvmType =
                        elemTypeNode2 ? getLLVMTypeFromNode(elemTypeNode2)
                                      : nullptr;
                    if(!elemLlvmType)
                    {
                        reportError(node->line,
                                    "unsupported list element type for pop()");
                        return nullptr;
                    }
                    llvm::AllocaInst* tmpElem = builder.CreateAlloca(
                        elemLlvmType, nullptr, "vec.pop.tmp");
                    llvm::Value* tmpElemPtr = builder.CreateBitCast(
                        tmpElem, opaquePtrType, "vec.pop.ptr");
                    uint64_t elemSizeU =
                        module->getDataLayout().getTypeAllocSize(elemLlvmType);
                    llvm::Value* elemSize =
                        llvm::ConstantInt::get(i64Type2, (uint64_t)elemSizeU);
                    llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                        i32Type2, {opaquePtrType, opaquePtrType, i64Type2},
                        false);
                    llvm::FunctionCallee fnRaw = module->getOrInsertFunction(
                        "__mlang_std_vec_pop_raw", ftRaw);
                    builder.CreateCall(fnRaw,
                                       {allocaPtr2, tmpElemPtr, elemSize});
                    if(receiverIsArray2)
                    {
                        auto lenIt = arrayKnownLengths.find(objId->name);
                        if(lenIt != arrayKnownLengths.end() &&
                           lenIt->second > 0)
                            updateKnownArrayLength2(lenIt->second - 1);
                    }
                    return builder.CreateLoad(elemLlvmType, tmpElem,
                                              objId->name + ".pop");
                }
                // --- clear() ---
                if(node->methodName == "clear")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "clear() takes no arguments");
                        return nullptr;
                    }
                    llvm::Value* result = callVecVoidFn("__mlang_std_vec_clear");
                    updateKnownArrayLength2(0);
                    return result;
                }
                // --- contains(val) ---
                if(node->methodName == "contains")
                {
                    if(node->arguments.size() != 1)
                    {
                        reportError(node->line,
                                    "contains() takes one argument");
                        return nullptr;
                    }
                    llvm::Value* val4 = generateExpression(node->arguments[0]);
                    if(!val4)
                        return nullptr;

                    std::string fnName4;
                    llvm::Type* valType4;
                    if(elemIsI64)
                    {
                        fnName4 = "__mlang_std_vec_contains_i64";
                        valType4 = i64Type2;
                        if(val4->getType() != i64Type2)
                            val4 = builder.CreateSExt(val4, i64Type2);
                    }
                    else
                    {
                        fnName4 = "__mlang_std_vec_contains_i32";
                        valType4 = i32Type2;
                        if(val4->getType() != i32Type2)
                            val4 = builder.CreateTrunc(val4, i32Type2);
                    }
                    llvm::FunctionType* ft4 = llvm::FunctionType::get(
                        i32Type2, {opaquePtrType, valType4}, false);
                    llvm::FunctionCallee fn4 =
                        module->getOrInsertFunction(fnName4, ft4);
                    return builder.CreateCall(fn4, {allocaPtr2, val4},
                                              objId->name + ".contains");
                }
                // --- index_of(val) ---
                if(node->methodName == "index_of")
                {
                    if(node->arguments.size() != 1)
                    {
                        reportError(node->line,
                                    "index_of() takes one argument");
                        return nullptr;
                    }
                    llvm::Value* val5 = generateExpression(node->arguments[0]);
                    if(!val5)
                        return nullptr;

                    std::string fnName5;
                    llvm::Type* valType5;
                    if(elemIsI64)
                    {
                        fnName5 = "__mlang_std_vec_index_of_i64";
                        valType5 = i64Type2;
                        if(val5->getType() != i64Type2)
                            val5 = builder.CreateSExt(val5, i64Type2);
                    }
                    else
                    {
                        fnName5 = "__mlang_std_vec_index_of_i32";
                        valType5 = i32Type2;
                        if(val5->getType() != i32Type2)
                            val5 = builder.CreateTrunc(val5, i32Type2);
                    }
                    llvm::FunctionType* ft5 = llvm::FunctionType::get(
                        i64Type2, {opaquePtrType, valType5}, false);
                    llvm::FunctionCallee fn5 =
                        module->getOrInsertFunction(fnName5, ft5);
                    return builder.CreateCall(fn5, {allocaPtr2, val5},
                                              objId->name + ".index_of");
                }
                // --- sort() ---
                if(node->methodName == "sort")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "sort() takes no arguments");
                        return nullptr;
                    }
                    return callVecVoidFn(elemIsI64
                                             ? "__mlang_std_vec_sort_i64"
                                             : "__mlang_std_vec_sort_i32");
                }
                // --- sort_desc() ---
                if(node->methodName == "sort_desc")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line,
                                    "sort_desc() takes no arguments");
                        return nullptr;
                    }
                    return callVecVoidFn(elemIsI64
                                             ? "__mlang_std_vec_sort_desc_i64"
                                             : "__mlang_std_vec_sort_desc_i32");
                }
                // --- reverse() ---
                if(node->methodName == "reverse")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "reverse() takes no arguments");
                        return nullptr;
                    }
                    return callVecVoidFn(
                        elemIsStr   ? "__mlang_std_vec_reverse_str"
                        : elemIsI64 ? "__mlang_std_vec_reverse_i64"
                                    : "__mlang_std_vec_reverse_i32");
                }
                // --- dedup() ---
                if(node->methodName == "dedup")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "dedup() takes no arguments");
                        return nullptr;
                    }
                    return callVecVoidFn("__mlang_std_vec_dedup_i32");
                }
                // --- first() ---
                if(node->methodName == "first")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "first() takes no arguments");
                        return nullptr;
                    }
                    // v[0] — direct GEP (same pattern as last())
                    std::vector<llvm::Type*> lsTypes5 = {i64Type2,
                                                         opaquePtrType};
                    llvm::StructType* lsType5 =
                        llvm::StructType::get(context, lsTypes5);
                    llvm::Value* ls5 = builder.CreateLoad(
                        lsType5, allocaPtr2, objId->name + ".load");
                    llvm::Value* cnt5 =
                        builder.CreateExtractValue(ls5, 0, "count");
                    if(!emitNonEmptyCheck2(cnt5, "first"))
                        return nullptr;
                    llvm::Value* dataPtr5 =
                        builder.CreateExtractValue(ls5, 1, "dataptr");
                    llvm::Type* elemType5 =
                        elemTypeNode2 ? getLLVMTypeFromNode(elemTypeNode2)
                                      : getLLVMType(elemKind2);
                    if(!elemType5)
                    {
                        reportError(node->line,
                                    "unsupported list element type for "
                                    "first()");
                        return nullptr;
                    }
                    llvm::Value* zero5 = llvm::ConstantInt::get(i64Type2, 0);
                    llvm::Value* gep5 = builder.CreateGEP(elemType5, dataPtr5,
                                                          zero5, "first.ptr");
                    return builder.CreateLoad(elemType5, gep5,
                                              objId->name + ".first");
                }
                // --- last() ---
                if(node->methodName == "last")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "last() takes no arguments");
                        return nullptr;
                    }
                    // v[v.len() - 1]
                    std::vector<llvm::Type*> lsTypes6 = {i64Type2,
                                                         opaquePtrType};
                    llvm::StructType* lsType6 =
                        llvm::StructType::get(context, lsTypes6);
                    llvm::Value* ls6 = builder.CreateLoad(
                        lsType6, allocaPtr2, objId->name + ".load");
                    llvm::Value* cnt6 =
                        builder.CreateExtractValue(ls6, 0, "count");
                    if(!emitNonEmptyCheck2(cnt6, "last"))
                        return nullptr;
                    llvm::Value* lastIdx = builder.CreateSub(
                        cnt6, llvm::ConstantInt::get(i64Type2, 1), "lastIdx");
                    // Generate via list indexing using lastIdx directly
                    // We replicate the GEP logic to avoid parsing -1 as a
                    // literal
                    llvm::Value* dataPtr6 =
                        builder.CreateExtractValue(ls6, 1, "dataptr");
                    llvm::Type* elemType6 =
                        elemTypeNode2 ? getLLVMTypeFromNode(elemTypeNode2)
                                      : getLLVMType(elemKind2);
                    if(!elemType6)
                    {
                        reportError(node->line,
                                    "unsupported list element type for "
                                    "last()");
                        return nullptr;
                    }
                    llvm::Value* gep6 = builder.CreateGEP(elemType6, dataPtr6,
                                                          lastIdx, "last.ptr");
                    return builder.CreateLoad(elemType6, gep6,
                                              objId->name + ".last");
                }
                reportError(node->line, "Vec has no method named '" +
                                            node->methodName + "'");
                return nullptr;
            }
        }

        // Handle built-in map methods (len)
        {
            auto mapTypeIt = variableTypes.find(objId->name);
            if(mapTypeIt != variableTypes.end() &&
               mapTypeIt->second == TypeNode::TYPE_MAP)
            {
                if(node->methodName == "extend")
                {
                    if(node->arguments.size() != 1)
                    {
                        reportError(node->line, "extend() takes one argument");
                        return nullptr;
                    }
                    llvm::Value* allocaPtr = namedValues[objId->name];
                    if(!allocaPtr)
                    {
                        reportError(node->line,
                                    "unknown variable: " + objId->name);
                        return nullptr;
                    }
                    auto dstMapIt = mapKeyValueTypes.find(objId->name);
                    if(dstMapIt == mapKeyValueTypes.end() ||
                       !dstMapIt->second.first || !dstMapIt->second.second)
                    {
                        reportError(node->line,
                                    "map.extend() requires known map key and "
                                    "value types");
                        return nullptr;
                    }
                    llvm::Type* dstKeyType =
                        getLLVMTypeFromNode(dstMapIt->second.first);
                    llvm::Type* dstValueType =
                        getLLVMTypeFromNode(dstMapIt->second.second);
                    if(!dstKeyType || !dstValueType)
                    {
                        reportError(node->line,
                                    "unsupported map key/value type for "
                                    "extend()");
                        return nullptr;
                    }

                    TypeNode* srcType =
                        inferExpressionTypeNode(node->arguments[0],
                                                node->line);
                    MapTypeNode* srcMapType =
                        dynamic_cast<MapTypeNode*>(srcType);
                    if(!srcMapType && (!srcType ||
                                       srcType->kind != TypeNode::TYPE_MAP))
                    {
                        reportError(node->line,
                                    "map.extend() expects a map<K, V> "
                                    "argument");
                        return nullptr;
                    }
                    if(srcMapType &&
                       !dynamic_cast<MapLiteralNode*>(node->arguments[0]))
                    {
                        llvm::Type* srcKeyType =
                            getLLVMTypeFromNode(srcMapType->keyType);
                        llvm::Type* srcValueType =
                            getLLVMTypeFromNode(srcMapType->valueType);
                        if(srcKeyType != dstKeyType ||
                           srcValueType != dstValueType)
                        {
                            reportError(node->line,
                                        "map.extend() argument key/value "
                                        "types do not match destination map");
                            return nullptr;
                        }
                    }

                    llvm::Value* srcPtr = nullptr;
                    if(dynamic_cast<IdentifierNode*>(node->arguments[0]) ||
                       dynamic_cast<FieldAccessNode*>(node->arguments[0]))
                        srcPtr = getLValuePointer(node->arguments[0],
                                                  node->line);
                    if(!srcPtr)
                    {
                        llvm::Value* srcValue = nullptr;
                        if(auto* mapLit = dynamic_cast<MapLiteralNode*>(
                               node->arguments[0]))
                            srcValue = generateMapLiteral(mapLit, dstKeyType,
                                                          dstValueType);
                        else
                            srcValue = generateExpression(node->arguments[0]);
                        if(!srcValue)
                            return nullptr;
                        llvm::AllocaInst* tmpMap = builder.CreateAlloca(
                            srcValue->getType(), nullptr, "map.extend.tmp");
                        builder.CreateStore(srcValue, tmpMap);
                        srcPtr = tmpMap;
                    }

                    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
                    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                    llvm::Type* ptrType = llvm::PointerType::get(
                        llvm::Type::getInt8Ty(context), 0);
#endif
                    uint64_t keySizeU =
                        module->getDataLayout().getTypeAllocSize(dstKeyType);
                    uint64_t valueSizeU =
                        module->getDataLayout().getTypeAllocSize(dstValueType);
                    llvm::Value* keySize =
                        llvm::ConstantInt::get(i64Type, keySizeU);
                    llvm::Value* valueSize =
                        llvm::ConstantInt::get(i64Type, valueSizeU);
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(context),
                        {ptrType, ptrType, i64Type, i64Type}, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(
                        "__mlang_std_map_extend_raw", ft);
                    builder.CreateCall(
                        fn, {allocaPtr, srcPtr, keySize, valueSize});
                    return llvm::Constant::getNullValue(
                        llvm::Type::getVoidTy(context));
                }
                if(node->methodName == "len")
                {
                    if(!node->arguments.empty())
                    {
                        reportError(node->line, "len() takes no arguments");
                        return nullptr;
                    }
                    llvm::Value* allocaPtr = namedValues[objId->name];
                    if(!allocaPtr)
                    {
                        reportError(node->line,
                                    "unknown variable: " + objId->name);
                        return nullptr;
                    }
                    // Map struct layout: {i64 count, ptr keys, ptr vals}
                    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
                    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                    llvm::Type* ptrType = llvm::PointerType::get(
                        llvm::Type::getInt8Ty(context), 0);
#endif
                    std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType,
                                                               ptrType};
                    llvm::StructType* mapStructType =
                        llvm::StructType::get(context, mapStructTypes);
                    llvm::Value* mapStruct = builder.CreateLoad(
                        mapStructType, allocaPtr, objId->name + ".load");
                    return builder.CreateExtractValue(mapStruct, 0,
                                                      objId->name + ".len");
                }
                reportError(node->line, "map has no method named '" +
                                            node->methodName + "'");
                return nullptr;
            }
        }

        // Simple case: identifier.method()
        auto typeIt = structVariableTypes.find(objId->name);
        if(typeIt == structVariableTypes.end())
        {
            reportError(node->line,
                        "'" + objId->name + "' is not a struct variable");
            return nullptr;
        }
        structTypeName = resolveStructAliasName(typeIt->second);

        objPtr = namedValues[objId->name];
        if(!objPtr)
        {
            reportError(node->line, "unknown variable: " + objId->name);
            return nullptr;
        }

        // Handle self pointer (alloca containing pointer)
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(objPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                objPtr = builder.CreateLoad(allocaType, alloca,
                                            objId->name + ".ptr");
            }
        }
    }
    else
    {
        // Non-identifier receiver: support built-in container/string methods on
        // struct fields (e.g. state.items.push(...), state.buf.push_str(...)).
        TypeNode* recvType = getLValueType(node->object, node->line);
        if(recvType && (recvType->kind == TypeNode::TYPE_STRING ||
                        recvType->kind == TypeNode::TYPE_STR8))
        {
            llvm::Value* recvPtr = getLValuePointer(node->object, node->line);
            if(!recvPtr)
                return nullptr;
#if LLVM_VERSION_MAJOR >= 15
            llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
            llvm::Type* ptrType =
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
            if(node->methodName == "push_str")
            {
                if(node->arguments.size() != 1)
                {
                    reportError(node->line, "push_str expects one argument");
                    return nullptr;
                }
                llvm::Value* currentStr =
                    builder.CreateLoad(ptrType, recvPtr, "fieldstr.load");
                llvm::Value* suffix = generateExpression(node->arguments[0]);
                if(!suffix)
                    return nullptr;
                llvm::FunctionType* concatFnType =
                    llvm::FunctionType::get(ptrType, {ptrType, ptrType}, false);
                llvm::FunctionCallee concatFn = module->getOrInsertFunction(
                    "__mlang_std_strbuf_concat", concatFnType);
                llvm::Value* newStr = builder.CreateCall(
                    concatFn, {currentStr, suffix}, "field.push_str.result");
                builder.CreateStore(newStr, recvPtr);
                return llvm::Constant::getNullValue(ptrType);
            }
            if(node->methodName == "clone")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "clone() takes no arguments");
                    return nullptr;
                }
                llvm::Value* currentStr =
                    builder.CreateLoad(ptrType, recvPtr, "fieldstr.load");
                llvm::FunctionType* cloneFnType =
                    llvm::FunctionType::get(ptrType, {ptrType}, false);
                llvm::FunctionCallee cloneFn = module->getOrInsertFunction(
                    "__mlang_std_strbuf_clone", cloneFnType);
                return builder.CreateCall(cloneFn, {currentStr}, "field.clone");
            }
            if(node->methodName == "len")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "len() takes no arguments");
                    return nullptr;
                }
                llvm::Value* currentStr =
                    builder.CreateLoad(ptrType, recvPtr, "fieldstr.load");
                llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
                llvm::FunctionType* lenFnType =
                    llvm::FunctionType::get(i64Type, {ptrType}, false);
                llvm::FunctionCallee lenFn = module->getOrInsertFunction(
                    "__mlang_std_strbuf_len", lenFnType);
                return builder.CreateCall(lenFn, {currentStr}, "field.len");
            }
            if(node->methodName == "is_empty")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "is_empty() takes no arguments");
                    return nullptr;
                }
                llvm::Value* currentStr =
                    builder.CreateLoad(ptrType, recvPtr, "fieldstr.load");
                llvm::Type* intType = llvm::Type::getInt32Ty(context);
                llvm::FunctionType* emptyFnType =
                    llvm::FunctionType::get(intType, {ptrType}, false);
                llvm::FunctionCallee emptyFn = module->getOrInsertFunction(
                    "__mlang_std_strbuf_is_empty", emptyFnType);
                return builder.CreateCall(emptyFn, {currentStr},
                                          "field.is_empty");
            }
            reportError(node->line, "string has no method named '" +
                                        node->methodName + "'");
            return nullptr;
        }
        if(recvType && recvType->kind == TypeNode::TYPE_LIST)
        {
            llvm::Value* recvPtr = getLValuePointer(node->object, node->line);
            if(!recvPtr)
                return nullptr;
            TypeNode::TypeKind elemKind = TypeNode::TYPE_I32;
            TypeNode* recvElemTypeForList = nullptr;
            int64_t arrayCapacityValue = 0;
            bool receiverIsArray = false;
            if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(recvType))
            {
                receiverIsArray = true;
                arrayCapacityValue = arrayType->capacity;
                recvElemTypeForList = arrayType->elementType;
                if(arrayType->elementType)
                    elemKind = arrayType->elementType->kind;
            }
            else if(auto* gl = dynamic_cast<GenericListTypeNode*>(recvType))
            {
                if(gl->elementType)
                {
                    recvElemTypeForList = gl->elementType;
                    elemKind = gl->elementType->kind;
                }
            }
            llvm::Type* i32Type = llvm::Type::getInt32Ty(context);
            llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
            llvm::Type* voidType = llvm::Type::getVoidTy(context);
#if LLVM_VERSION_MAJOR >= 15
            llvm::Type* opaquePtrType = llvm::PointerType::get(context, 0);
#else
            llvm::Type* opaquePtrType =
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
            bool elemIsI64 = (elemKind == TypeNode::TYPE_I64 ||
                              elemKind == TypeNode::TYPE_U64);
            bool elemIsStr = (elemKind == TypeNode::TYPE_STRING ||
                              elemKind == TypeNode::TYPE_STR8);
            bool elemIsI32Like = (elemKind == TypeNode::TYPE_INT ||
                                  elemKind == TypeNode::TYPE_I32 ||
                                  elemKind == TypeNode::TYPE_U32 ||
                                  elemKind == TypeNode::TYPE_I16 ||
                                  elemKind == TypeNode::TYPE_U16 ||
                                  elemKind == TypeNode::TYPE_I8 ||
                                  elemKind == TypeNode::TYPE_U8 ||
                                  elemKind == TypeNode::TYPE_BOOL);
            llvm::Value* arrayCapacity =
                receiverIsArray
                    ? llvm::ConstantInt::get(i64Type, arrayCapacityValue)
                    : nullptr;
            auto emitFieldListNonEmptyCheck =
                [&](llvm::Value* count,
                    const std::string& methodName) -> bool
            {
                initializeFormatFunctions();
                llvm::Function* function =
                    builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* okBB = llvm::BasicBlock::Create(
                    context, "fieldlist." + methodName + ".non_empty",
                    function);
                llvm::BasicBlock* failBB = llvm::BasicBlock::Create(
                    context, "fieldlist." + methodName + ".empty", function);
                llvm::Value* nonEmpty = builder.CreateICmpSGT(
                    count, llvm::ConstantInt::get(i64Type, 0),
                    "fieldlist." + methodName + ".has_items");
                builder.CreateCondBr(nonEmpty, okBB, failBB);

                builder.SetInsertPoint(failBB);
#if LLVM_VERSION_MAJOR >= 21
                llvm::Value* formatStr = builder.CreateGlobalString(
                    (methodName + "() requires a non-empty list/array\n")
                        .c_str(),
                    "fieldlist." + methodName + ".empty.msg");
#else
                llvm::Value* formatStr = builder.CreateGlobalStringPtr(
                    (methodName + "() requires a non-empty list/array\n")
                        .c_str(),
                    "fieldlist." + methodName + ".empty.msg");
#endif
                llvm::Value* stderrVal =
                    builder.CreateLoad(opaquePtrType, stderrPtr, "stderr");
                builder.CreateCall(fprintfFunc, {stderrVal, formatStr});
                builder.CreateCall(abortFunc, {});
                builder.CreateUnreachable();

                builder.SetInsertPoint(okBB);
                return true;
            };

            if(node->methodName == "len")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "len() takes no arguments");
                    return nullptr;
                }
                std::vector<llvm::Type*> listStructTypes = {i64Type,
                                                            opaquePtrType};
                llvm::StructType* listStructType =
                    llvm::StructType::get(context, listStructTypes);
                llvm::Value* listStruct = builder.CreateLoad(
                    listStructType, recvPtr, "fieldlist.load");
                return builder.CreateExtractValue(listStruct, 0,
                                                  "fieldlist.len");
            }
            if(node->methodName == "is_empty")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "is_empty() takes no arguments");
                    return nullptr;
                }
                std::vector<llvm::Type*> lsTypes = {i64Type, opaquePtrType};
                llvm::StructType* lsType =
                    llvm::StructType::get(context, lsTypes);
                llvm::Value* ls =
                    builder.CreateLoad(lsType, recvPtr, "fieldlist.load");
                llvm::Value* cnt = builder.CreateExtractValue(ls, 0);
                return builder.CreateICmpEQ(cnt,
                                            llvm::ConstantInt::get(i64Type, 0),
                                            "fieldlist.is_empty");
            }
            if(node->methodName == "fill")
            {
                if(!receiverIsArray)
                {
                    reportError(node->line,
                                "fill() is only available for array<T, N>");
                    return nullptr;
                }
                if(node->arguments.size() != 1)
                {
                    reportError(node->line, "fill() takes one argument");
                    return nullptr;
                }
                llvm::Value* val = generateExpression(node->arguments[0]);
                if(!val)
                    return nullptr;

                if(elemIsStr)
                {
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        voidType, {opaquePtrType, opaquePtrType, i64Type},
                        false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(
                        "__mlang_std_array_fill_str", ft);
                    builder.CreateCall(fn, {recvPtr, val, arrayCapacity});
                    return llvm::Constant::getNullValue(voidType);
                }
                if(elemIsI64)
                {
                    if(val->getType() != i64Type)
                        val = builder.CreateSExt(val, i64Type);
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        voidType, {opaquePtrType, i64Type, i64Type}, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(
                        "__mlang_std_array_fill_i64", ft);
                    builder.CreateCall(fn, {recvPtr, val, arrayCapacity});
                    return llvm::Constant::getNullValue(voidType);
                }
                if(elemIsI32Like)
                {
                    if(val->getType() != i32Type)
                        val = builder.CreateTrunc(val, i32Type);
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        voidType, {opaquePtrType, i32Type, i64Type}, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction(
                        "__mlang_std_array_fill_i32", ft);
                    builder.CreateCall(fn, {recvPtr, val, arrayCapacity});
                    return llvm::Constant::getNullValue(voidType);
                }

                llvm::Type* elemLlvmType =
                    recvElemTypeForList
                        ? getLLVMTypeFromNode(recvElemTypeForList)
                        : val->getType();
                if(!elemLlvmType || val->getType() != elemLlvmType)
                {
                    reportError(node->line,
                                "fill() argument type mismatch for array "
                                "element");
                    return nullptr;
                }
                llvm::AllocaInst* tmpElem = builder.CreateAlloca(
                    elemLlvmType, nullptr, "array.fill.tmp");
                builder.CreateStore(val, tmpElem);
                llvm::Value* elemPtrAsOpaque = builder.CreateBitCast(
                    tmpElem, opaquePtrType, "array.fill.ptr");
                uint64_t elemSizeU =
                    module->getDataLayout().getTypeAllocSize(elemLlvmType);
                llvm::Value* elemSize =
                    llvm::ConstantInt::get(i64Type, elemSizeU);
                llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                    voidType,
                    {opaquePtrType, opaquePtrType, i64Type, i64Type}, false);
                llvm::FunctionCallee fnRaw = module->getOrInsertFunction(
                    "__mlang_std_array_fill_raw", ftRaw);
                builder.CreateCall(fnRaw,
                                   {recvPtr, elemPtrAsOpaque, elemSize,
                                    arrayCapacity});
                return llvm::Constant::getNullValue(voidType);
            }
            if(node->methodName == "push")
            {
                if(node->arguments.size() != 1)
                {
                    reportError(node->line, "push() takes one argument");
                    return nullptr;
                }
                llvm::Value* val = generateExpression(node->arguments[0]);
                if(!val)
                    return nullptr;
                std::string fnName;
                llvm::Type* valType = nullptr;
                if(elemIsStr)
                {
                    fnName = "__mlang_std_vec_push_str";
                    valType = opaquePtrType;
                }
                else if(elemIsI64)
                {
                    fnName = "__mlang_std_vec_push_i64";
                    valType = i64Type;
                    if(val->getType() != i64Type)
                        val = builder.CreateSExt(val, i64Type);
                }
                else
                {
                    if(!elemIsI32Like)
                    {
                        llvm::Type* elemLlvmType =
                            recvElemTypeForList
                                ? getLLVMTypeFromNode(recvElemTypeForList)
                                : nullptr;
                        if(!elemLlvmType)
                        {
                            reportError(
                                node->line,
                                "unsupported list element type for push()");
                            return nullptr;
                        }
                        if(val->getType() == elemLlvmType &&
                           !elemLlvmType->isIntegerTy(32))
                        {
                            llvm::AllocaInst* tmpElem = builder.CreateAlloca(
                                elemLlvmType, nullptr, "fieldlist.push.tmp");
                            builder.CreateStore(val, tmpElem);
                            llvm::Value* elemPtrAsOpaque =
                                builder.CreateBitCast(tmpElem, opaquePtrType,
                                                      "fieldlist.push.ptr");
                            uint64_t elemSizeU =
                                module->getDataLayout().getTypeAllocSize(
                                    elemLlvmType);
                            llvm::Value* elemSize = llvm::ConstantInt::get(
                                i64Type, (uint64_t)elemSizeU);
                            llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                                voidType,
                                receiverIsArray
                                    ? std::vector<llvm::Type*>{
                                          opaquePtrType, opaquePtrType, i64Type,
                                          i64Type}
                                    : std::vector<llvm::Type*>{
                                          opaquePtrType, opaquePtrType,
                                          i64Type},
                                false);
                            llvm::FunctionCallee fnRaw =
                                module->getOrInsertFunction(
                                    receiverIsArray
                                        ? "__mlang_std_array_push_raw"
                                        : "__mlang_std_vec_push_raw",
                                    ftRaw);
                            if(receiverIsArray)
                                builder.CreateCall(fnRaw,
                                                   {recvPtr, elemPtrAsOpaque,
                                                    elemSize, arrayCapacity});
                            else
                                builder.CreateCall(fnRaw,
                                                   {recvPtr, elemPtrAsOpaque,
                                                    elemSize});
                            return llvm::Constant::getNullValue(voidType);
                        }
                    }
                    fnName = "__mlang_std_vec_push_i32";
                    valType = i32Type;
                    if(val->getType() != i32Type)
                        val = builder.CreateTrunc(val, i32Type);
                }
                if(receiverIsArray)
                {
                    if(fnName == "__mlang_std_vec_push_str")
                        fnName = "__mlang_std_array_push_str";
                    else if(fnName == "__mlang_std_vec_push_i64")
                        fnName = "__mlang_std_array_push_i64";
                    else
                        fnName = "__mlang_std_array_push_i32";
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    voidType,
                    receiverIsArray
                        ? std::vector<llvm::Type*>{opaquePtrType, valType,
                                                   i64Type}
                        : std::vector<llvm::Type*>{opaquePtrType, valType},
                    false);
                llvm::FunctionCallee fn =
                    module->getOrInsertFunction(fnName, ft);
                if(receiverIsArray)
                    builder.CreateCall(fn, {recvPtr, val, arrayCapacity});
                else
                    builder.CreateCall(fn, {recvPtr, val});
                return llvm::Constant::getNullValue(voidType);
            }
            if(node->methodName == "extend")
            {
                if(node->arguments.size() != 1)
                {
                    reportError(node->line, "extend() takes one argument");
                    return nullptr;
                }
                std::optional<int64_t> sourceKnownLength =
                    fixedArrayExpressionKnownLength(node->arguments[0]);
                if(receiverIsArray && sourceKnownLength)
                {
                    if(*sourceKnownLength < 0)
                    {
                        reportError(node->line,
                                    "extend() source length must be "
                                    "non-negative");
                        return nullptr;
                    }
                    if(*sourceKnownLength > arrayCapacityValue)
                    {
                        reportError(node->line,
                                    "extend() would exceed array<T, N> "
                                    "capacity: add=" +
                                        std::to_string(*sourceKnownLength) +
                                        " capacity=" +
                                        std::to_string(arrayCapacityValue));
                        return nullptr;
                    }
                }

                llvm::Type* elemLlvmType =
                    recvElemTypeForList
                        ? getLLVMTypeFromNode(recvElemTypeForList)
                        : getLLVMType(elemKind);
                if(!elemLlvmType)
                {
                    reportError(node->line,
                                "unsupported container element type for "
                                "extend()");
                    return nullptr;
                }

                TypeNode* srcType =
                    inferExpressionTypeNode(node->arguments[0], node->line);
                TypeNode* srcElemType = nullptr;
                if(auto* srcArray = dynamic_cast<ArrayTypeNode*>(srcType))
                    srcElemType = srcArray->elementType;
                else if(auto* srcList =
                            dynamic_cast<GenericListTypeNode*>(srcType))
                    srcElemType = srcList->elementType;
                else if(srcType && srcType->kind == TypeNode::TYPE_LIST)
                    srcElemType = nullptr;
                else
                {
                    reportError(node->line,
                                "extend() expects a list<T>, vec![...], or "
                                "array<T, N> argument");
                    return nullptr;
                }
                if(srcElemType)
                {
                    llvm::Type* srcElemLlvm =
                        getLLVMTypeFromNode(srcElemType);
                    bool sourceIsLiteral =
                        dynamic_cast<ListLiteralNode*>(node->arguments[0]) ||
                        dynamic_cast<ArrayFillNode*>(node->arguments[0]);
                    bool literalIntegerCoercion =
                        sourceIsLiteral && srcElemLlvm &&
                        srcElemLlvm->isIntegerTy() &&
                        elemLlvmType->isIntegerTy();
                    if(srcElemLlvm && srcElemLlvm != elemLlvmType &&
                       !literalIntegerCoercion)
                    {
                        reportError(node->line,
                                    "extend() argument element type does not "
                                    "match array element type");
                        return nullptr;
                    }
                }

                llvm::Value* srcPtr = nullptr;
                if(dynamic_cast<IdentifierNode*>(node->arguments[0]) ||
                   dynamic_cast<FieldAccessNode*>(node->arguments[0]))
                    srcPtr = getLValuePointer(node->arguments[0], node->line);

                if(!srcPtr)
                {
                    llvm::Value* srcValue = nullptr;
                    if(auto* listLit =
                           dynamic_cast<ListLiteralNode*>(node->arguments[0]))
                        srcValue = generateListLiteral(listLit, elemLlvmType);
                    else if(auto* arrFill =
                                dynamic_cast<ArrayFillNode*>(
                                    node->arguments[0]))
                        srcValue = generateArrayFill(arrFill, elemLlvmType);
                    else
                        srcValue = generateExpression(node->arguments[0]);
                    if(!srcValue)
                        return nullptr;
                    llvm::AllocaInst* tmpList = builder.CreateAlloca(
                        srcValue->getType(), nullptr, "array.extend.tmp");
                    builder.CreateStore(srcValue, tmpList);
                    srcPtr = tmpList;
                }

                std::string fnName =
                    elemIsStr
                        ? (receiverIsArray ? "__mlang_std_array_extend_str"
                                           : "__mlang_std_vec_extend_str")
                    : elemIsI64
                        ? (receiverIsArray ? "__mlang_std_array_extend_i64"
                                           : "__mlang_std_vec_extend_i64")
                        : (receiverIsArray ? "__mlang_std_array_extend_i32"
                                           : "__mlang_std_vec_extend_i32");
                if(!(elemIsStr || elemIsI64 || elemIsI32Like))
                {
                    uint64_t elemSizeU =
                        module->getDataLayout().getTypeAllocSize(
                            elemLlvmType);
                    llvm::Value* elemSize = llvm::ConstantInt::get(
                        i64Type, (uint64_t)elemSizeU);
                    llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                        voidType,
                        receiverIsArray
                            ? std::vector<llvm::Type*>{
                                  opaquePtrType, opaquePtrType, i64Type,
                                  i64Type}
                            : std::vector<llvm::Type*>{
                                  opaquePtrType, opaquePtrType, i64Type},
                        false);
                    llvm::FunctionCallee fnRaw =
                        module->getOrInsertFunction(
                            receiverIsArray ? "__mlang_std_array_extend_raw"
                                            : "__mlang_std_vec_extend_raw",
                            ftRaw);
                    if(receiverIsArray)
                        builder.CreateCall(fnRaw,
                                           {recvPtr, srcPtr, elemSize,
                                            arrayCapacity});
                    else
                        builder.CreateCall(fnRaw, {recvPtr, srcPtr, elemSize});
                    return llvm::Constant::getNullValue(voidType);
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    voidType,
                    receiverIsArray
                        ? std::vector<llvm::Type*>{
                              opaquePtrType, opaquePtrType, i64Type}
                        : std::vector<llvm::Type*>{
                              opaquePtrType, opaquePtrType},
                    false);
                llvm::FunctionCallee fn =
                    module->getOrInsertFunction(fnName, ft);
                if(receiverIsArray)
                    builder.CreateCall(fn, {recvPtr, srcPtr, arrayCapacity});
                else
                    builder.CreateCall(fn, {recvPtr, srcPtr});
                return llvm::Constant::getNullValue(voidType);
            }
            if(node->methodName == "first")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "first() takes no arguments");
                    return nullptr;
                }
                std::vector<llvm::Type*> lsTypes = {i64Type, opaquePtrType};
                llvm::StructType* lsType =
                    llvm::StructType::get(context, lsTypes);
                llvm::Value* ls =
                    builder.CreateLoad(lsType, recvPtr, "fieldlist.load");
                llvm::Value* cnt = builder.CreateExtractValue(ls, 0, "count");
                if(!emitFieldListNonEmptyCheck(cnt, "first"))
                    return nullptr;
                llvm::Value* dataPtr =
                    builder.CreateExtractValue(ls, 1, "dataptr");
                llvm::Type* elemLlvmType =
                    recvElemTypeForList
                        ? getLLVMTypeFromNode(recvElemTypeForList)
                        : getLLVMType(elemKind);
                if(!elemLlvmType)
                {
                    reportError(node->line,
                                "unsupported list element type for first()");
                    return nullptr;
                }
                llvm::Value* zero = llvm::ConstantInt::get(i64Type, 0);
                llvm::Value* elemPtr = builder.CreateGEP(
                    elemLlvmType, dataPtr, zero, "fieldlist.first.ptr");
                return builder.CreateLoad(elemLlvmType, elemPtr,
                                          "fieldlist.first");
            }
            if(node->methodName == "last")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "last() takes no arguments");
                    return nullptr;
                }
                std::vector<llvm::Type*> lsTypes = {i64Type, opaquePtrType};
                llvm::StructType* lsType =
                    llvm::StructType::get(context, lsTypes);
                llvm::Value* ls =
                    builder.CreateLoad(lsType, recvPtr, "fieldlist.load");
                llvm::Value* cnt = builder.CreateExtractValue(ls, 0, "count");
                if(!emitFieldListNonEmptyCheck(cnt, "last"))
                    return nullptr;
                llvm::Value* lastIdx = builder.CreateSub(
                    cnt, llvm::ConstantInt::get(i64Type, 1), "lastIdx");
                llvm::Value* dataPtr =
                    builder.CreateExtractValue(ls, 1, "dataptr");
                llvm::Type* elemLlvmType =
                    recvElemTypeForList
                        ? getLLVMTypeFromNode(recvElemTypeForList)
                        : getLLVMType(elemKind);
                if(!elemLlvmType)
                {
                    reportError(node->line,
                                "unsupported list element type for last()");
                    return nullptr;
                }
                llvm::Value* elemPtr = builder.CreateGEP(
                    elemLlvmType, dataPtr, lastIdx, "fieldlist.last.ptr");
                return builder.CreateLoad(elemLlvmType, elemPtr,
                                          "fieldlist.last");
            }
            if(node->methodName == "pop")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "pop() takes no arguments");
                    return nullptr;
                }
                std::vector<llvm::Type*> lsTypes = {i64Type, opaquePtrType};
                llvm::StructType* lsType =
                    llvm::StructType::get(context, lsTypes);
                llvm::Value* ls =
                    builder.CreateLoad(lsType, recvPtr, "fieldlist.load");
                llvm::Value* cnt = builder.CreateExtractValue(ls, 0);
                initializeFormatFunctions();
                llvm::Function* function =
                    builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* okBB = llvm::BasicBlock::Create(
                    context, "fieldlist.pop.non_empty", function);
                llvm::BasicBlock* failBB = llvm::BasicBlock::Create(
                    context, "fieldlist.pop.empty", function);
                llvm::Value* nonEmpty = builder.CreateICmpSGT(
                    cnt, llvm::ConstantInt::get(i64Type, 0),
                    "fieldlist.pop.has_items");
                builder.CreateCondBr(nonEmpty, okBB, failBB);

                builder.SetInsertPoint(failBB);
#if LLVM_VERSION_MAJOR >= 21
                llvm::Value* formatStr = builder.CreateGlobalString(
                    "pop() requires a non-empty list/array\n",
                    "fieldlist.pop.empty.msg");
#else
                llvm::Value* formatStr = builder.CreateGlobalStringPtr(
                    "pop() requires a non-empty list/array\n",
                    "fieldlist.pop.empty.msg");
#endif
                llvm::Value* stderrVal =
                    builder.CreateLoad(opaquePtrType, stderrPtr, "stderr");
                builder.CreateCall(fprintfFunc, {stderrVal, formatStr});
                builder.CreateCall(abortFunc, {});
                builder.CreateUnreachable();
                builder.SetInsertPoint(okBB);

                if(elemIsStr || elemIsI64 || elemIsI32Like)
                {
                    std::string fnName;
                    llvm::Type* retType = nullptr;
                    if(elemIsStr)
                    {
                        fnName = "__mlang_std_vec_pop_str";
                        retType = opaquePtrType;
                    }
                    else if(elemIsI64)
                    {
                        fnName = "__mlang_std_vec_pop_i64";
                        retType = i64Type;
                    }
                    else
                    {
                        fnName = "__mlang_std_vec_pop_i32";
                        retType = i32Type;
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        retType, {opaquePtrType}, false);
                    llvm::FunctionCallee fn =
                        module->getOrInsertFunction(fnName, ft);
                    return builder.CreateCall(fn, {recvPtr}, "fieldlist.pop");
                }
                TypeNode* recvElemType = nullptr;
                if(auto* gl = dynamic_cast<GenericListTypeNode*>(recvType))
                    recvElemType = gl->elementType;
                llvm::Type* elemLlvmType =
                    recvElemType ? getLLVMTypeFromNode(recvElemType) : nullptr;
                if(!elemLlvmType)
                {
                    reportError(node->line,
                                "unsupported list element type for pop()");
                    return nullptr;
                }
                llvm::AllocaInst* tmpElem = builder.CreateAlloca(
                    elemLlvmType, nullptr, "fieldlist.pop.tmp");
                llvm::Value* tmpElemPtr = builder.CreateBitCast(
                    tmpElem, opaquePtrType, "fieldlist.pop.ptr");
                uint64_t elemSizeU =
                    module->getDataLayout().getTypeAllocSize(elemLlvmType);
                llvm::Value* elemSize =
                    llvm::ConstantInt::get(i64Type, (uint64_t)elemSizeU);
                llvm::FunctionType* ftRaw = llvm::FunctionType::get(
                    i32Type, {opaquePtrType, opaquePtrType, i64Type}, false);
                llvm::FunctionCallee fnRaw = module->getOrInsertFunction(
                    "__mlang_std_vec_pop_raw", ftRaw);
                builder.CreateCall(fnRaw, {recvPtr, tmpElemPtr, elemSize});
                return builder.CreateLoad(elemLlvmType, tmpElem,
                                          "fieldlist.pop");
            }
            if(node->methodName == "clear")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "clear() takes no arguments");
                    return nullptr;
                }
                llvm::FunctionType* ft =
                    llvm::FunctionType::get(voidType, {opaquePtrType}, false);
                llvm::FunctionCallee fn =
                    module->getOrInsertFunction("__mlang_std_vec_clear", ft);
                builder.CreateCall(fn, {recvPtr});
                return llvm::Constant::getNullValue(voidType);
            }
            if(node->methodName == "contains")
            {
                if(node->arguments.size() != 1)
                {
                    reportError(node->line, "contains() takes one argument");
                    return nullptr;
                }
                llvm::Value* val = generateExpression(node->arguments[0]);
                if(!val)
                    return nullptr;

                std::string fnName;
                llvm::Type* valType = nullptr;
                if(elemIsI64)
                {
                    fnName = "__mlang_std_vec_contains_i64";
                    valType = i64Type;
                    if(val->getType() != i64Type)
                        val = builder.CreateSExt(val, i64Type);
                }
                else
                {
                    fnName = "__mlang_std_vec_contains_i32";
                    valType = i32Type;
                    if(val->getType() != i32Type)
                        val = builder.CreateTrunc(val, i32Type);
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    i32Type, {opaquePtrType, valType}, false);
                llvm::FunctionCallee fn =
                    module->getOrInsertFunction(fnName, ft);
                return builder.CreateCall(fn, {recvPtr, val},
                                          "fieldlist.contains");
            }
            if(node->methodName == "index_of")
            {
                if(node->arguments.size() != 1)
                {
                    reportError(node->line, "index_of() takes one argument");
                    return nullptr;
                }
                llvm::Value* val = generateExpression(node->arguments[0]);
                if(!val)
                    return nullptr;

                std::string fnName;
                llvm::Type* valType = nullptr;
                if(elemIsI64)
                {
                    fnName = "__mlang_std_vec_index_of_i64";
                    valType = i64Type;
                    if(val->getType() != i64Type)
                        val = builder.CreateSExt(val, i64Type);
                }
                else
                {
                    fnName = "__mlang_std_vec_index_of_i32";
                    valType = i32Type;
                    if(val->getType() != i32Type)
                        val = builder.CreateTrunc(val, i32Type);
                }
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    i64Type, {opaquePtrType, valType}, false);
                llvm::FunctionCallee fn =
                    module->getOrInsertFunction(fnName, ft);
                return builder.CreateCall(fn, {recvPtr, val},
                                          "fieldlist.index_of");
            }
            if(node->methodName == "sort")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "sort() takes no arguments");
                    return nullptr;
                }
                llvm::FunctionType* ft =
                    llvm::FunctionType::get(voidType, {opaquePtrType}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(
                    elemIsI64 ? "__mlang_std_vec_sort_i64"
                              : "__mlang_std_vec_sort_i32",
                    ft);
                builder.CreateCall(fn, {recvPtr});
                return llvm::Constant::getNullValue(voidType);
            }
            if(node->methodName == "sort_desc")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line,
                                "sort_desc() takes no arguments");
                    return nullptr;
                }
                llvm::FunctionType* ft =
                    llvm::FunctionType::get(voidType, {opaquePtrType}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(
                    elemIsI64 ? "__mlang_std_vec_sort_desc_i64"
                              : "__mlang_std_vec_sort_desc_i32",
                    ft);
                builder.CreateCall(fn, {recvPtr});
                return llvm::Constant::getNullValue(voidType);
            }
            if(node->methodName == "reverse")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "reverse() takes no arguments");
                    return nullptr;
                }
                llvm::FunctionType* ft =
                    llvm::FunctionType::get(voidType, {opaquePtrType}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(
                    elemIsStr   ? "__mlang_std_vec_reverse_str"
                    : elemIsI64 ? "__mlang_std_vec_reverse_i64"
                                : "__mlang_std_vec_reverse_i32",
                    ft);
                builder.CreateCall(fn, {recvPtr});
                return llvm::Constant::getNullValue(voidType);
            }
            if(node->methodName == "dedup")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "dedup() takes no arguments");
                    return nullptr;
                }
                llvm::FunctionType* ft =
                    llvm::FunctionType::get(voidType, {opaquePtrType}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(
                    "__mlang_std_vec_dedup_i32", ft);
                builder.CreateCall(fn, {recvPtr});
                return llvm::Constant::getNullValue(voidType);
            }
            reportError(node->line,
                        "Vec has no method named '" + node->methodName + "'");
            return nullptr;
        }
        if(recvType && recvType->kind == TypeNode::TYPE_MAP)
        {
            if(node->methodName == "extend")
            {
                if(node->arguments.size() != 1)
                {
                    reportError(node->line, "extend() takes one argument");
                    return nullptr;
                }
                auto* dstMapType = dynamic_cast<MapTypeNode*>(recvType);
                if(!dstMapType || !dstMapType->keyType ||
                   !dstMapType->valueType)
                {
                    reportError(node->line,
                                "map.extend() requires known map key and "
                                "value types");
                    return nullptr;
                }
                llvm::Value* recvPtr =
                    getLValuePointer(node->object, node->line);
                if(!recvPtr)
                    return nullptr;
                llvm::Type* dstKeyType =
                    getLLVMTypeFromNode(dstMapType->keyType);
                llvm::Type* dstValueType =
                    getLLVMTypeFromNode(dstMapType->valueType);
                if(!dstKeyType || !dstValueType)
                {
                    reportError(node->line,
                                "unsupported map key/value type for "
                                "extend()");
                    return nullptr;
                }

                TypeNode* srcType =
                    inferExpressionTypeNode(node->arguments[0], node->line);
                MapTypeNode* srcMapType = dynamic_cast<MapTypeNode*>(srcType);
                if(!srcMapType && (!srcType ||
                                   srcType->kind != TypeNode::TYPE_MAP))
                {
                    reportError(node->line,
                                "map.extend() expects a map<K, V> argument");
                    return nullptr;
                }
                if(srcMapType &&
                   !dynamic_cast<MapLiteralNode*>(node->arguments[0]))
                {
                    llvm::Type* srcKeyType =
                        getLLVMTypeFromNode(srcMapType->keyType);
                    llvm::Type* srcValueType =
                        getLLVMTypeFromNode(srcMapType->valueType);
                    if(srcKeyType != dstKeyType ||
                       srcValueType != dstValueType)
                    {
                        reportError(node->line,
                                    "map.extend() argument key/value types do "
                                    "not match destination map");
                        return nullptr;
                    }
                }

                llvm::Value* srcPtr = nullptr;
                if(dynamic_cast<IdentifierNode*>(node->arguments[0]) ||
                   dynamic_cast<FieldAccessNode*>(node->arguments[0]))
                    srcPtr =
                        getLValuePointer(node->arguments[0], node->line);
                if(!srcPtr)
                {
                    llvm::Value* srcValue = nullptr;
                    if(auto* mapLit =
                           dynamic_cast<MapLiteralNode*>(node->arguments[0]))
                        srcValue =
                            generateMapLiteral(mapLit, dstKeyType,
                                               dstValueType);
                    else
                        srcValue = generateExpression(node->arguments[0]);
                    if(!srcValue)
                        return nullptr;
                    llvm::AllocaInst* tmpMap = builder.CreateAlloca(
                        srcValue->getType(), nullptr, "map.extend.tmp");
                    builder.CreateStore(srcValue, tmpMap);
                    srcPtr = tmpMap;
                }

                llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
                llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                llvm::Type* ptrType =
                    llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
                uint64_t keySizeU =
                    module->getDataLayout().getTypeAllocSize(dstKeyType);
                uint64_t valueSizeU =
                    module->getDataLayout().getTypeAllocSize(dstValueType);
                llvm::Value* keySize =
                    llvm::ConstantInt::get(i64Type, keySizeU);
                llvm::Value* valueSize =
                    llvm::ConstantInt::get(i64Type, valueSizeU);
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(context),
                    {ptrType, ptrType, i64Type, i64Type}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(
                    "__mlang_std_map_extend_raw", ft);
                builder.CreateCall(fn,
                                   {recvPtr, srcPtr, keySize, valueSize});
                return llvm::Constant::getNullValue(
                    llvm::Type::getVoidTy(context));
            }
            if(node->methodName == "len")
            {
                if(!node->arguments.empty())
                {
                    reportError(node->line, "len() takes no arguments");
                    return nullptr;
                }
                llvm::Value* recvPtr =
                    getLValuePointer(node->object, node->line);
                if(!recvPtr)
                    return nullptr;
                llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
                llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
                llvm::Type* ptrType =
                    llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
                std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType,
                                                           ptrType};
                llvm::StructType* mapStructType =
                    llvm::StructType::get(context, mapStructTypes);
                llvm::Value* mapStruct =
                    builder.CreateLoad(mapStructType, recvPtr, "fieldmap.load");
                return builder.CreateExtractValue(mapStruct, 0, "fieldmap.len");
            }
            reportError(node->line,
                        "map has no method named '" + node->methodName + "'");
            return nullptr;
        }

        // Chained case: a.b.method() - use helper to get struct pointer
        auto [ptr, typeName] = getStructPtrAndType(node->object, node->line);
        if(!ptr)
        {
            return nullptr;
        }
        objPtr = ptr;
        structTypeName = typeName;
    }

    auto isResultType = [&](const std::string& typeName) -> bool
    {
        if(typeName == "result")
            return true;
        auto it = mangledToGenericName.find(typeName);
        return it != mangledToGenericName.end() && it->second == "result";
    };

    auto loadStructField = [&](llvm::Value* basePtr,
                               const std::string& ownerTypeName,
                               const std::string& fieldName) -> llvm::Value*
    {
        auto memberIt = structMembers.find(ownerTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(node->line, "unknown struct type: " + ownerTypeName);
            return nullptr;
        }

        int fieldIndex = -1;
        TypeNode* fieldType = nullptr;
        const auto& members = memberIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldName)
            {
                fieldIndex = static_cast<int>(i);
                fieldType = members[i].second;
                break;
            }
        }

        if(fieldIndex < 0 || !fieldType)
        {
            reportError(node->line, "struct '" + ownerTypeName +
                                        "' has no field named '" + fieldName +
                                        "'");
            return nullptr;
        }

        llvm::StructType* ownerType = getStructType(ownerTypeName);
        if(!ownerType)
            return nullptr;
        llvm::Type* llvmFieldType = getLLVMTypeFromNode(fieldType);
        llvm::Value* fieldPtr = builder.CreateStructGEP(
            ownerType, basePtr, static_cast<unsigned>(fieldIndex),
            fieldName + "_ptr");
        return builder.CreateLoad(llvmFieldType, fieldPtr, fieldName);
    };

    if(isResultType(structTypeName))
    {
        if(node->methodName == "is_ok")
        {
            if(!node->arguments.empty())
            {
                reportError(node->line, "is_ok() takes no arguments");
                return nullptr;
            }
            return loadStructField(objPtr, structTypeName, "is_ok");
        }
        if(node->methodName == "is_err")
        {
            if(!node->arguments.empty())
            {
                reportError(node->line, "is_err() takes no arguments");
                return nullptr;
            }
            llvm::Value* isOk =
                loadStructField(objPtr, structTypeName, "is_ok");
            if(!isOk)
                return nullptr;
            return builder.CreateNot(isOk, "is_err");
        }
        if(node->methodName == "unwrap")
        {
            if(!node->arguments.empty())
            {
                reportError(node->line, "unwrap() takes no arguments");
                return nullptr;
            }
            if(warnResultUnwrap)
            {
                reportWarning(node->line, node->col,
                              "result.unwrap() may panic on Err; consider "
                              "match/is_ok/is_err");
            }
            return loadStructField(objPtr, structTypeName, "ok");
        }
        if(node->methodName == "unwrap_err")
        {
            if(!node->arguments.empty())
            {
                reportError(node->line, "unwrap_err() takes no arguments");
                return nullptr;
            }
            return loadStructField(objPtr, structTypeName, "err");
        }
    }

    // Check if method exists on this struct (including inherited methods)
    auto structIt = structMethods.find(structTypeName);
    if(structIt == structMethods.end())
    {
        reportError(node->line,
                    "struct '" + structTypeName + "' has no methods");
        return nullptr;
    }

    auto methodIt = structIt->second.find(node->methodName);
    if(methodIt == structIt->second.end())
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no method named '" +
                                    node->methodName + "'");
        return nullptr;
    }

    if(node->methodName == "unwrap" && isResultType(structTypeName) &&
       warnResultUnwrap)
    {
        reportWarning(node->line, node->col,
                      "result.unwrap() may panic on Err; consider "
                      "match/is_ok/is_err");
    }

    bool isPublic = methodIt->second.first;
    StructMethodNode* methodNode = methodIt->second.second;
    (void)isPublic;
    if(methodNode && methodNode->isStatic)
    {
        reportError(node->line, "static method '" + structTypeName +
                                    "::" + node->methodName +
                                    "' must be called as " + structTypeName +
                                    "::" + node->methodName + "(...)");
        return nullptr;
    }

    std::vector<ParameterNode*> declaredParams;
    if(methodNode && methodNode->parameters)
    {
        for(auto* param : methodNode->parameters->parameters)
        {
            if(param && param->name != "self")
                declaredParams.push_back(param);
        }
    }

    if(node->arguments.size() != declaredParams.size())
    {
        reportError(node->line, "method '" + node->methodName + "' expects " +
                                    std::to_string(declaredParams.size()) +
                                    " argument(s), but " +
                                    std::to_string(node->arguments.size()) +
                                    " provided");
        return nullptr;
    }

    // Find the actual struct that defines this method (may be a base struct)
    std::string definingStruct = structTypeName;
    std::string searchStruct = structTypeName;
    while(!searchStruct.empty())
    {
        // Check if this struct directly defines the method (has it in its
        // members)
        std::string mangledName = searchStruct + "_" + node->methodName;
        if(module->getFunction(mangledName))
        {
            definingStruct = searchStruct;
            break;
        }
        // Move to base struct
        auto baseIt = structBases.find(searchStruct);
        if(baseIt != structBases.end())
        {
            searchStruct = baseIt->second;
        }
        else
        {
            break;
        }
    }

    StructMethodNode* definingMethodNode = methodNode;
    auto definingStructIt = structMethods.find(definingStruct);
    if(definingStructIt != structMethods.end())
    {
        auto definingMethodIt = definingStructIt->second.find(node->methodName);
        if(definingMethodIt != definingStructIt->second.end())
        {
            isPublic = definingMethodIt->second.first;
            if(definingMethodIt->second.second)
                definingMethodNode = definingMethodIt->second.second;
        }
    }

    std::string methodModule =
        definingMethodNode ? definingMethodNode->sourceModule : "";
    if(methodModule.empty())
    {
        auto structVisIt = structVisibility.find(definingStruct);
        if(structVisIt != structVisibility.end())
            methodModule = structVisIt->second.second;
    }
    if(!methodModule.empty() && !isPublic &&
       !Helpers::is_same_module_family(methodModule, currentModule))
    {
        reportError(node->line, "method '" + node->methodName +
                                    "' is private in module '" + methodModule +
                                    "'");
        return nullptr;
    }

    std::string mangledName = definingStruct + "_" + node->methodName;
    llvm::Function* callee = module->getFunction(mangledName);
    if(!callee)
    {
        reportError(node->line, "unknown method: " + node->methodName);
        return nullptr;
    }

    // Check if this is a monomorphized struct method that needs body generation
    // If the function is declared but has no body (empty), generate it now
    if(callee->empty() && monomorphizedTypes.count(definingStruct))
    {
        // Find the method node
        auto structIt = structMethods.find(definingStruct);
        if(structIt != structMethods.end())
        {
            auto methodIt = structIt->second.find(node->methodName);
            if(methodIt != structIt->second.end())
            {
                StructMethodNode* methodDef = methodIt->second.second;
                if(methodDef && methodDef->body)
                {
                    // Save current state - generateMethodDefinition will clear
                    // these
                    llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
                    auto savedNamedValues = namedValues;
                    auto savedConstantVariables = constantVariables;
                    auto savedVariableTypes = variableTypes;
                    auto savedStructVariableTypes = structVariableTypes;
                    auto savedEnumVariableTypes = enumVariableTypes;
                    auto savedListElementTypes = listElementTypes;
                    auto savedMapKeyValueTypes = mapKeyValueTypes;
                    auto savedTupleElementTypes = tupleElementTypes;
                    auto savedPointerElementTypes = pointerElementTypes;
                    auto savedMovedVariables = movedVariables;
                    auto savedPointerBorrowTarget = pointerBorrowTarget;
                    auto savedActiveBorrowers = activeBorrowers;
                    auto savedActiveMutBorrower = activeMutBorrower;
                    auto savedVariableScopeDepth = variableScopeDepth;
                    auto savedCleanupScopes = cleanupScopes;
                    auto savedPointerBorrowScopes = pointerBorrowScopes;
                    auto savedVariableScopeDepthScopes =
                        variableScopeDepthScopes;

                    // Generate the method body
                    generateMethodDefinition(definingStruct, methodDef);

                    // Restore all state
                    namedValues = savedNamedValues;
                    constantVariables = savedConstantVariables;
                    variableTypes = savedVariableTypes;
                    structVariableTypes = savedStructVariableTypes;
                    enumVariableTypes = savedEnumVariableTypes;
                    listElementTypes = savedListElementTypes;
                    mapKeyValueTypes = savedMapKeyValueTypes;
                    tupleElementTypes = savedTupleElementTypes;
                    pointerElementTypes = savedPointerElementTypes;
                    movedVariables = savedMovedVariables;
                    pointerBorrowTarget = savedPointerBorrowTarget;
                    activeBorrowers = savedActiveBorrowers;
                    activeMutBorrower = savedActiveMutBorrower;
                    variableScopeDepth = savedVariableScopeDepth;
                    cleanupScopes = savedCleanupScopes;
                    pointerBorrowScopes = savedPointerBorrowScopes;
                    variableScopeDepthScopes = savedVariableScopeDepthScopes;

                    if(savedBlock)
                    {
                        builder.SetInsertPoint(savedBlock);
                    }
                }
            }
        }
    }

    std::string receiverOwner = resolveBorrowOwnerFromLValue(node->object);
    if(!receiverOwner.empty())
    {
        auto activeIt = activeBorrowers.find(receiverOwner);
        if(activeIt != activeBorrowers.end() && !activeIt->second.empty())
        {
            std::string by = *activeIt->second.begin();
            reportError(node->line, "cannot call method on '" + receiverOwner +
                                        "' while borrowed by '" + by + "'");
            return nullptr;
        }
    }

    // Build arguments - first is pointer to struct
    std::vector<llvm::Value*> args;
    args.push_back(objPtr);

    if(!validateTemporaryBorrowArguments(node->arguments, node->methodName,
                                         receiverOwner))
        return nullptr;

    // Add other arguments with the same coercion rules as normal function
    // calls so synthesized setters and typed methods receive well-formed IR.
    for(size_t argIndex = 0; argIndex < node->arguments.size(); ++argIndex)
    {
        auto* arg = node->arguments[argIndex];
        llvm::Value* argVal = generateExpression(arg);
        if(!argVal)
            return nullptr;

        if(argIndex < declaredParams.size())
        {
            auto* declParam = declaredParams[argIndex];
            llvm::Type* expectedType =
                callee->getArg(static_cast<unsigned>(argIndex + 1))->getType();
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
                                "argument " + std::to_string(argIndex + 1) +
                                    " of method '" + node->methodName +
                                    "' has wrong type: expected '" +
                                    expectedStr + "', got '" + actualStr + "'");
                    return nullptr;
                }
            }

            if(auto* refType =
                   dynamic_cast<ReferenceTypeNode*>(declParam->type))
            {
                if(refType->isMutable)
                {
                    auto* unary = dynamic_cast<UnaryOpNode*>(arg);
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

            if(!dynamic_cast<ReferenceTypeNode*>(declParam->type))
            {
                consumeMoveFromExpression(arg, node->line,
                                          "passing argument to method '" +
                                              node->methodName + "'");
            }
        }

        args.push_back(argVal);
    }

    const bool consumesReceiver =
        receiverOwner.size() > 0 &&
        (node->methodName == "__drop" || node->methodName == "drop" ||
         node->methodName == "free" || node->methodName == "destroy") &&
        globalNamedValues.find(receiverOwner) == globalNamedValues.end();
    if(consumesReceiver)
        movedVariables.insert(receiverOwner);

    if(callee->getReturnType()->isVoidTy())
    {
        return builder.CreateCall(callee, args);
    }
    return builder.CreateCall(callee, args, "methodcall");
}
