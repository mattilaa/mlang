#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"
#include "llvm_compat.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Verifier.h>

using mlang::ir_detail::ast_analysis::collect_used_idents;
using mlang::ir_detail::ast_analysis::contains_exception_control_flow;
using mlang::ir_detail::common::Helpers;

llvm::Function*
CodeGenerator::generateFunctionDeclaration(FunctionDefNode* node)
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
            reportError(param->line,
                        "unknown type: " + Helpers::type_name_for_error(param->type));
            paramType = llvm::Type::getInt32Ty(context); // fallback
        }
        paramTypes.push_back(paramType);
    }

    llvm::Type* returnType = getLLVMTypeFromNode(node->returnType);
    if(!returnType)
    {
        reportError(node->line,
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
            reportError(node->line, "conflicting declaration for function '" +
                                        node->name + "'");
        }
        return existing;
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(
        returnType, paramTypes, node->parameters->isVarArg);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, symbolName, module.get());

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
        if(ref->isMutable)
            return llvm::PointerType::get(context, 0);
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
            llvm::Value* value = builder.CreateLoad(local->getAllocatedType(), local, inout.second + ".out");
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
    for(const auto& it : globalStructVariableTypes)
        structVariableTypes[it.first] = it.second;
    for(const auto& n : globalConstantVariables)
        constantVariables.insert(n);
}

void CodeGenerator::generateGlobalVarDeclaration(VarDeclNode* node)
{
    if(!node)
        return;
    if(globalNamedValues.find(node->name) != globalNamedValues.end())
    {
        reportError(node->line,
                    "duplicate global variable: '" + node->name + "'");
        return;
    }

    TypeNode::TypeKind kind = TypeNode::TYPE_INT;
    if(node->type)
        kind = node->type->kind;
    else if(node->initExpr)
    {
        if(dynamic_cast<BoolLiteralNode*>(node->initExpr))
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
        reportError(node->line,
                    "invalid global variable type for '" + node->name + "'");
        return;
    }

    llvm::Constant* init = llvm::Constant::getNullValue(llvmTy);
    if(node->initExpr)
    {
        if(auto* i = dynamic_cast<IntLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isIntegerTy())
            {
                reportError(node->line,
                            "global integer initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantInt::get(llvmTy, i->value, true);
        }
        else if(auto* b = dynamic_cast<BoolLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isIntegerTy(1))
            {
                reportError(node->line,
                            "global bool initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantInt::get(llvmTy, b->value ? 1 : 0, false);
        }
        else if(auto* f = dynamic_cast<FloatLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isFloatTy())
            {
                reportError(node->line,
                            "global float initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantFP::get(llvmTy, f->value);
        }
        else if(auto* d = dynamic_cast<DoubleLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isDoubleTy())
            {
                reportError(node->line,
                            "global double initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantFP::get(llvmTy, d->value);
        }
        else if(auto* s = dynamic_cast<StringLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isPointerTy())
            {
                reportError(node->line,
                            "global string initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
#if LLVM_VERSION_MAJOR >= 21
            auto* gstr =
                builder.CreateGlobalString(s->value, node->name + ".gstr");
            init = llvm::dyn_cast<llvm::Constant>(gstr);
#else
            auto* gstr =
                builder.CreateGlobalStringPtr(s->value, node->name + ".gstr");
            init = llvm::dyn_cast<llvm::Constant>(gstr);
#endif
            if(!init)
                init = llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(llvmTy));
        }
        else
        {
            reportError(node->line,
                        "global initializer must be a literal constant");
            return;
        }
    }

    auto* gv = new llvm::GlobalVariable(*module, llvmTy, false,
                                        llvm::GlobalValue::InternalLinkage,
                                        init, "__mlang_global_" + node->name);
    globalNamedValues[node->name] = gv;
    globalVariableTypes[node->name] = kind;
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

    if(node->isExtern || !node->body)
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
    auto savedConstexprValues = constexprValues;
    auto savedPackExpansions = activePackExpansions;
    auto savedFunctionTypeBindings = activeFunctionTypeBindings;
    auto savedMovedVariables = movedVariables;
    auto savedPointerBorrowTarget = pointerBorrowTarget;
    auto savedActiveBorrowers = activeBorrowers;
    auto savedActiveMutBorrower = activeMutBorrower;
    auto savedVariableScopeDepth = variableScopeDepth;
    auto savedVariableTypes = variableTypes;
    auto savedStructVariableTypes = structVariableTypes;
    auto savedTraitObjectVariableTypes = traitObjectVariableTypes;
    auto savedEnumVariableTypes = enumVariableTypes;
    auto savedListElementTypes = listElementTypes;
    auto savedMapKeyValueTypes = mapKeyValueTypes;
    auto savedTupleElementTypes = tupleElementTypes;
    auto savedPointerElementTypes = pointerElementTypes;
    auto savedPointerKnownNull = pointerKnownNull;
    auto savedCleanupScopes = cleanupScopes;
    auto savedPointerBorrowScopes = pointerBorrowScopes;
    auto savedVariableScopeDepthScopes = variableScopeDepthScopes;
    auto savedClosureVariables = closureVariables;
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
    movedVariables.clear();
    closureVariables.clear();
    closureVariables = node->boundClosureParameters;
    activeInlineClosures.clear();
    inlineClosureReturnStates.clear();
    pointerBorrowTarget.clear();
    pointerKnownNull.clear();
    activeBorrowers.clear();
    activeMutBorrower.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    enumVariableTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
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
        exceptionsFrameEnvFunc, {currentFunctionExceptionFrame}, "fn.exc.env");
    auto* functionSetjmpCall = builder.CreateCall(
        exceptionsSetjmpFunc, {functionExceptionEnv}, "fn.exc.state");
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
            if(auto* ref = dynamic_cast<ReferenceTypeNode*>(node->parameters->parameters[paramIdx]->type))
                if(ref->isMutable)
                    inoutType = ref;
        llvm::Type* localType = inoutType ? getLLVMTypeFromNode(inoutType->elementType) : arg.getType();
        // Allocate space for parameters so they can be modified
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            localType, nullptr, std::string(arg.getName()) + ".addr");
        llvm::Value* paramValue = &arg;
        if(inoutType)
        {
            paramValue = builder.CreateLoad(localType, &arg, std::string(arg.getName()) + ".in");
            inoutParams.push_back({&arg, std::string(arg.getName())});
        }
        else if(arg.getType()->isStructTy())
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
                        genStructInner->structName, genStructInner->typeArgs);
                    structVariableTypes[std::string(arg.getName())] = mangled;
                }
                // Immutable reference: param may not be mutated inside body.
                if(!refType->isMutable)
                    constantVariables.insert(std::string(arg.getName()));
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
                    genStructType->structName, genStructType->typeArgs);
                structVariableTypes[std::string(arg.getName())] = mangled;
            }
            if(auto* genListType =
                   dynamic_cast<GenericListTypeNode*>(paramNode->type))
            {
                listElementTypes[std::string(arg.getName())] =
                    genListType->elementType;
                if(auto* arrayType =
                       dynamic_cast<ArrayTypeNode*>(genListType))
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
            }
            if(auto* traitObjType =
                   dynamic_cast<TraitObjectTypeNode*>(paramNode->type))
            {
                variableTypes[std::string(arg.getName())] =
                    TypeNode::TYPE_TRAIT_OBJECT;
                traitObjectVariableTypes[std::string(arg.getName())] =
                    traitObjType->traitName;
            }
        }
        // mlang -g: the parameter, with its MLang type.
        if(paramIdx < node->parameters->parameters.size())
            debugDeclareVariable(std::string(arg.getName()), alloca,
                                 node->parameters->parameters[paramIdx]->type,
                                 node->parameters->parameters[paramIdx], paramIdx + 1);
        paramIdx++;
    }

    // Generate the function body
    if(node->body)
    {
        const auto& bodyStmts = node->body->statements;
        for(size_t si = 0; si < bodyStmts.size(); si++)
        {
            generateStatement(bodyStmts[si]);
            // NLL: expire borrow variables not referenced in remaining stmts
            if(!pointerBorrowTarget.empty())
            {
                std::set<std::string> futureIdents;
                for(size_t sj = si + 1; sj < bodyStmts.size(); sj++)
                    collect_used_idents(bodyStmts[sj], futureIdents);
                std::vector<std::string> toClear;
                for(const auto& kv : pointerBorrowTarget)
                {
                    if(futureIdents.count(kv.first))
                        continue;
                    // Don't NLL-expire exclusive struct borrows (ptr<T> where T
                    // is a struct). They remain active until scope exit or
                    // explicit reassignment so a second borrow of the same
                    // owner is rejected.
                    auto peit = pointerElementTypes.find(kv.first);
                    if(peit != pointerElementTypes.end() && peit->second &&
                       peit->second->kind == TypeNode::TYPE_STRUCT)
                        continue;
                    toClear.push_back(kv.first);
                }
                for(const auto& ptr : toClear)
                    clearPointerBorrow(ptr);
            }
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
            builder.CreateCall(exceptionsPopFrameFunc,
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
        builder.CreateCall(exceptionsPopFrameFunc,
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
    movedVariables = std::move(savedMovedVariables);
    pointerBorrowTarget = std::move(savedPointerBorrowTarget);
    activeBorrowers = std::move(savedActiveBorrowers);
    activeMutBorrower = std::move(savedActiveMutBorrower);
    variableScopeDepth = std::move(savedVariableScopeDepth);
    variableTypes = std::move(savedVariableTypes);
    structVariableTypes = std::move(savedStructVariableTypes);
    traitObjectVariableTypes = std::move(savedTraitObjectVariableTypes);
    enumVariableTypes = std::move(savedEnumVariableTypes);
    listElementTypes = std::move(savedListElementTypes);
    mapKeyValueTypes = std::move(savedMapKeyValueTypes);
    tupleElementTypes = std::move(savedTupleElementTypes);
    pointerElementTypes = std::move(savedPointerElementTypes);
    pointerKnownNull = std::move(savedPointerKnownNull);
    cleanupScopes = std::move(savedCleanupScopes);
    pointerBorrowScopes = std::move(savedPointerBorrowScopes);
    variableScopeDepthScopes = std::move(savedVariableScopeDepthScopes);
    closureVariables = std::move(savedClosureVariables);
    activeInlineClosures = std::move(savedActiveInlineClosures);
    inlineClosureReturnStates = std::move(savedInlineClosureReturnStates);
    currentFunctionExceptionFrame = savedCurrentFunctionExceptionFrame;
    currentSemanticReturnType = savedSemanticReturnType;
    unsafeDepth = savedUnsafeDepth;
    currentModule = savedModule;
    constexprValues = std::move(savedConstexprValues);
    activePackExpansions = std::move(savedPackExpansions);
    activeFunctionTypeBindings = std::move(savedFunctionTypeBindings);
    builder.restoreIP(savedIP);

    // Verify the function
    llvm::verifyFunction(*function);
    return function;
}
