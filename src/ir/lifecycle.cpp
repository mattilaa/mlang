#include "ir.h"
#include "diagnostics.h"
#include "llvm_compat.h"

#include <iostream>

void CodeGenerator::reportError(int line, const std::string& message)
{
    reportError(line, 0, message);
}

std::string CodeGenerator::diagnosticFileName() const
{
    if(diagnosticFile && *diagnosticFile)
        return diagnosticFile;
    return sourceFileName.empty() ? std::string("<input>") : sourceFileName;
}

void CodeGenerator::reportError(int line, int col, const std::string& message)
{
    const std::string file = diagnosticFileName();
    mlang::diag::print_diagnostic_location(std::cerr, file, line, col, "error");
    std::cerr << mlang::diag::format_error_message(message) << std::endl;
    hasError = true;
}

void CodeGenerator::reportWarning(int line, int col, const std::string& message)
{
    const std::string file = diagnosticFileName();
    mlang::diag::print_diagnostic_location(std::cerr, file, line, col,
                                           "warning");
    std::cerr << mlang::diag::format_warning_message(message) << std::endl;
}

void CodeGenerator::enterCleanupScope()
{
    cleanupScopes.emplace_back();
    pointerBorrowScopes.emplace_back();
    variableScopeDepthScopes.emplace_back();
}

void CodeGenerator::releaseRuntimeClosureBorrows(
    const std::string& closureName)
{
    auto captures = runtimeClosureBorrowOwners.find(closureName);
    if(captures == runtimeClosureBorrowOwners.end())
        return;
    for(const auto& owner : captures->second)
    {
        auto borrowers = activeBorrowers.find(owner);
        if(borrowers == activeBorrowers.end())
            continue;
        borrowers->second.erase(closureName);
        if(borrowers->second.empty())
            activeBorrowers.erase(borrowers);
    }
}

void CodeGenerator::exitCleanupScope()
{
    if(cleanupScopes.empty() || pointerBorrowScopes.empty() ||
       variableScopeDepthScopes.empty())
        return;

    const int exitingDepth = static_cast<int>(cleanupScopes.size());
    for(const auto& closure : runtimeClosureBorrowOwners)
    {
        auto depth = variableScopeDepth.find(closure.first);
        if(depth != variableScopeDepth.end() && depth->second == exitingDepth)
            releaseRuntimeClosureBorrows(closure.first);
    }

    auto actions = std::move(cleanupScopes.back());
    cleanupScopes.pop_back();
    auto pointerEntries = std::move(pointerBorrowScopes.back());
    pointerBorrowScopes.pop_back();
    auto depthEntries = std::move(variableScopeDepthScopes.back());
    variableScopeDepthScopes.pop_back();

    for(auto it = pointerEntries.rbegin(); it != pointerEntries.rend(); ++it)
        clearPointerBorrow(it->pointerVar);

    for(auto it = depthEntries.rbegin(); it != depthEntries.rend(); ++it)
    {
        // Restore the binding's shadowed metadata before restoring its borrow.
        // clearAggregateBorrowOrigins below can clear a same-named pointer, so
        // restoring the outer borrow earlier would accidentally erase it.
        for(auto moved = movedVariables.begin(); moved != movedVariables.end();)
        {
            const bool belongsToVariable =
                *moved == it->varName ||
                (moved->size() > it->varName.size() &&
                 moved->compare(0, it->varName.size(), it->varName) == 0 &&
                 ((*moved)[it->varName.size()] == '.' ||
                  (*moved)[it->varName.size()] == '['));
            if(belongsToVariable)
                moved = movedVariables.erase(moved);
            else
                ++moved;
        }
        movedVariables.insert(it->previousMovedValues.begin(),
                              it->previousMovedValues.end());
        if(it->hadPreviousNamedValue)
            namedValues[it->varName] = it->previousNamedValue;
        else
            namedValues.erase(it->varName);
        if(it->hadPreviousVariableType)
            variableTypes[it->varName] = it->previousVariableType;
        else
            variableTypes.erase(it->varName);
        if(it->hadPreviousStructVariableType)
            structVariableTypes[it->varName] = it->previousStructVariableType;
        else
            structVariableTypes.erase(it->varName);
        if(it->previousWasConstant)
            constantVariables.insert(it->varName);
        else
            constantVariables.erase(it->varName);
        if(it->previousWasImmutableReference)
            immutableReferenceVariables.insert(it->varName);
        else
            immutableReferenceVariables.erase(it->varName);
        auto restore = [&](auto& mapping, const auto& previous) {
            if(previous.existed)
                mapping[it->varName] = previous.value;
            else
                mapping.erase(it->varName);
        };
        restore(listElementTypes, it->previousListElementType);
        restore(arrayCapacities, it->previousArrayCapacity);
        restore(arrayKnownLengths, it->previousArrayKnownLength);
        restore(multiarrayMutability, it->previousMultiarrayMutability);
        restore(mapKeyValueTypes, it->previousMapKeyValueTypes);
        restore(pointerElementTypes, it->previousPointerElementType);
        restore(pointerKnownNull, it->previousPointerKnownNull);
        restore(tupleElementTypes, it->previousTupleElementTypes);
        restore(enumVariableTypes, it->previousEnumVariableType);
        restore(traitObjectVariableTypes,
                it->previousTraitObjectVariableType);
        restore(functionTypeVariables, it->previousFunctionTypeVariable);
        restore(closureVariables, it->previousClosureVariable);
        restore(runtimeClosureVariables,
                it->previousRuntimeClosureVariable);
        restore(runtimeClosureReturnTypes,
                it->previousRuntimeClosureReturnType);
        restore(runtimeClosureBorrowOwners,
                it->previousRuntimeClosureBorrowOwners);
        if(it->previousRuntimeClosureBorrowOwners.existed)
            for(const auto& owner :
                it->previousRuntimeClosureBorrowOwners.value)
                activeBorrowers[owner].insert(it->varName);
        auto belongsToVariable = [&](const std::string& place) {
            return place == it->varName ||
                   (place.size() > it->varName.size() &&
                    place.compare(0, it->varName.size(), it->varName) == 0 &&
                    (place[it->varName.size()] == '.' ||
                     place[it->varName.size()] == '['));
        };
        for(auto borrowers = activeBorrowers.begin();
            borrowers != activeBorrowers.end();)
        {
            if(belongsToVariable(borrowers->first))
                borrowers = activeBorrowers.erase(borrowers);
            else
                ++borrowers;
        }
        for(auto borrower = activeMutBorrower.begin();
            borrower != activeMutBorrower.end();)
        {
            if(belongsToVariable(borrower->first))
                borrower = activeMutBorrower.erase(borrower);
            else
                ++borrower;
        }
        activeBorrowers.insert(it->previousActiveBorrowers.begin(),
                               it->previousActiveBorrowers.end());
        activeMutBorrower.insert(it->previousActiveMutBorrowers.begin(),
                                 it->previousActiveMutBorrowers.end());
        clearAggregateBorrowOrigins(it->varName);
        if(it->hadPreviousAggregateBorrowOrigins)
        {
            aggregateBorrowOrigins[it->varName] =
                it->previousAggregateBorrowOrigins;
            for(const auto& owner : it->previousAggregateBorrowOrigins)
                activeBorrowers[owner].insert(it->varName);
        }
        if(it->hadPreviousAggregateBorrowPlaces)
            aggregateBorrowPlaces[it->varName] =
                it->previousAggregateBorrowPlaces;
        if(it->hadPreviousAggregateBorrowFieldPlaces)
            aggregateBorrowFieldPlaces[it->varName] =
                it->previousAggregateBorrowFieldPlaces;
        if(it->hadPreviousAggregateBorrowPlace)
            pointerBorrowPlace[it->varName] =
                it->previousAggregateBorrowPlace;
        if(it->hadPreviousFunctionItemTarget)
            functionItemVariables[it->varName] =
                it->previousFunctionItemTargets;
        else
            functionItemVariables.erase(it->varName);
        if(it->hadPreviousDepth)
            variableScopeDepth[it->varName] = it->previousDepth;
        else
            variableScopeDepth.erase(it->varName);
    }

    for(auto it = pointerEntries.rbegin(); it != pointerEntries.rend(); ++it)
    {
        if(it->hadPreviousBorrow && !it->previousOwner.empty())
        {
            if(!it->shadowAlias.empty())
            {
                for(auto& borrowers : activeBorrowers)
                    if(borrowers.second.erase(it->shadowAlias))
                        borrowers.second.insert(it->pointerVar);
                for(auto& borrower : activeMutBorrower)
                    if(borrower.second == it->shadowAlias)
                        borrower.second = it->pointerVar;
                for(auto& parent : pointerBorrowParent)
                    if(parent.first != it->pointerVar &&
                       parent.second == it->shadowAlias)
                        parent.second = it->pointerVar;
                pointerBorrowTarget.erase(it->shadowAlias);
                pointerBorrowParent.erase(it->shadowAlias);
                pointerBorrowPlace.erase(it->shadowAlias);
            }
            pointerBorrowTarget[it->pointerVar] = it->previousOwner;
            if(!it->previousParent.empty())
                pointerBorrowParent[it->pointerVar] = it->previousParent;
            if(!it->previousPlace.empty())
                pointerBorrowPlace[it->pointerVar] = it->previousPlace;
            if(it->previousWasMutable)
            {
                std::string borrowKey = it->previousBorrowKey.empty()
                                            ? it->previousOwner
                                            : it->previousBorrowKey;
                auto rootBorrow = activeMutBorrower.find(it->previousOwner);
                if(borrowKey == it->previousOwner &&
                   rootBorrow != activeMutBorrower.end() &&
                   rootBorrow->second != it->pointerVar)
                    borrowKey = it->previousPlace.empty()
                                    ? it->previousOwner
                                    : it->previousPlace;
                activeMutBorrower[borrowKey] = it->pointerVar;
            }
            else
            {
                activeBorrowers[it->previousOwner].insert(it->pointerVar);
            }
        }
    }

    if(!builder.GetInsertBlock() || mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
        return;

    for(auto it = actions.rbegin(); it != actions.rend(); ++it)
    {
        if(movedVariables.count(it->varName) && !it->dropFlag)
            continue;
        emitScopeCleanup(*it, "scope");
    }
}

void CodeGenerator::emitAllActiveCleanups()
{
    if(!builder.GetInsertBlock() || mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
        return;

    for(auto scopeIt = cleanupScopes.rbegin(); scopeIt != cleanupScopes.rend();
        ++scopeIt)
    {
        auto& actions = *scopeIt;
        for(auto it = actions.rbegin(); it != actions.rend(); ++it)
        {
            if(movedVariables.count(it->varName) && !it->dropFlag)
                continue;
            emitScopeCleanup(*it, "return");
        }
    }
}

void CodeGenerator::emitActiveCleanupsDeeperThan(int scopeDepth)
{
    if(!builder.GetInsertBlock() || mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
        return;

    for(int depth = static_cast<int>(cleanupScopes.size()); depth > scopeDepth;
        --depth)
    {
        auto& actions = cleanupScopes[static_cast<size_t>(depth - 1)];
        for(auto it = actions.rbegin(); it != actions.rend(); ++it)
        {
            if(movedVariables.count(it->varName) && !it->dropFlag)
                continue;
            emitScopeCleanup(*it, "unwind");
        }
    }
}

void CodeGenerator::emitScopeCleanup(const ScopeCleanup& cleanup,
                                     const std::string& suffix)
{
    if(!cleanup.function || !builder.GetInsertBlock() ||
       mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
        return;

    auto* alloca = cleanup.storage;
    if(!alloca)
        return;
    llvm::Type* allocaType = alloca->getAllocatedType();
    if(!allocaType || !allocaType->isStructTy())
        return;

    auto emitCall = [&]() {
        if(cleanup.callKind == ScopeCleanup::CallKind::ByPointerMethod)
        {
            builder.CreateCall(cleanup.function, {alloca});
            return;
        }

        llvm::Value* aggregate = builder.CreateLoad(
            allocaType, alloca, cleanup.varName + ".dropval." + suffix);
        builder.CreateCall(cleanup.function, {aggregate});
    };

    if(!cleanup.dropFlag)
    {
        emitCall();
        return;
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* runBlock = llvm::BasicBlock::Create(
        context, cleanup.varName + ".drop." + suffix, function);
    llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(
        context, cleanup.varName + ".drop.cont." + suffix, function);
    llvm::Value* initialized = builder.CreateLoad(
        llvm::Type::getInt1Ty(context), cleanup.dropFlag,
        cleanup.varName + ".initialized");
    builder.CreateCondBr(initialized, runBlock, continueBlock);

    builder.SetInsertPoint(runBlock);
    builder.CreateStore(llvm::ConstantInt::getFalse(context),
                        cleanup.dropFlag);
    emitCall();
    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
        builder.CreateBr(continueBlock);
    builder.SetInsertPoint(continueBlock);
}

llvm::AllocaInst* CodeGenerator::findActiveDropFlag(
    const std::string& varName) const
{
    for(auto scope = cleanupScopes.rbegin(); scope != cleanupScopes.rend();
        ++scope)
        for(auto cleanup = scope->rbegin(); cleanup != scope->rend(); ++cleanup)
            if(cleanup->varName == varName && cleanup->dropFlag)
                return cleanup->dropFlag;
    return nullptr;
}

void CodeGenerator::setActiveDropFlag(const std::string& varName,
                                     bool initialized)
{
    if(auto* flag = findActiveDropFlag(varName))
        builder.CreateStore(llvm::ConstantInt::get(
                                llvm::Type::getInt1Ty(context), initialized),
                            flag);
}

CodeGenerator::ScopeCleanup
CodeGenerator::resolveDropFunctionForStruct(const std::string& structTypeName)
{
    ScopeCleanup cleanup;
    cleanup.structTypeName = structTypeName;

    auto* structType = getStructType(structTypeName);
    if(!structType)
        return cleanup;

    auto find_drop = [&](const std::string& fnName) -> llvm::Function*
    {
        auto it = functionOverloads.find(fnName);
        if(it == functionOverloads.end())
            return nullptr;
        for(auto& info : it->second)
        {
            llvm::Function* fn = info.function;
            if(!fn || fn->arg_size() != 1 || !fn->getReturnType()->isVoidTy())
                continue;
            llvm::Type* p0 = fn->getArg(0)->getType();
            if(p0 == structType)
                return fn;
        }
        return nullptr;
    };

    for(const std::string& fnName : {"__drop", "drop", "free", "destroy"})
    {
        if(auto* f = find_drop(fnName))
        {
            cleanup.function = f;
            cleanup.callKind = ScopeCleanup::CallKind::ByValueFunction;
            return cleanup;
        }
    }

    auto methodsIt = structMethods.find(structTypeName);
    if(methodsIt == structMethods.end())
        return cleanup;

    for(const std::string& methodName : {"__drop", "drop", "free", "destroy"})
    {
        auto methodIt = methodsIt->second.find(methodName);
        if(methodIt == methodsIt->second.end() || !methodIt->second.second)
            continue;

        StructMethodNode* method = methodIt->second.second;
        if(method->isStatic)
            continue;

        size_t nonSelfParams = 0;
        if(method->parameters)
        {
            for(auto* param : method->parameters->parameters)
            {
                if(param && param->name != "self")
                    nonSelfParams++;
            }
        }
        if(nonSelfParams != 0)
            continue;

        llvm::Function* fn =
            module->getFunction(structTypeName + "_" + methodName);
        if(!fn)
            fn = generateMethodDeclaration(structTypeName, method);
        if(!fn)
            continue;

        cleanup.function = fn;
        cleanup.callKind = ScopeCleanup::CallKind::ByPointerMethod;
        return cleanup;
    }

    return cleanup;
}

void CodeGenerator::registerStructCleanupIfNeeded(
    const std::string& varName, const std::string& structTypeName)
{
    if(varName.empty() || structTypeName.empty() || cleanupScopes.empty())
        return;
    ScopeCleanup cleanup = resolveDropFunctionForStruct(structTypeName);
    if(cleanup.function)
    {
        cleanup.varName = varName;
        auto value = namedValues.find(varName);
        if(value != namedValues.end())
            cleanup.storage = llvm::dyn_cast<llvm::AllocaInst>(value->second);
        llvm::Function* function = builder.GetInsertBlock()
                                       ? builder.GetInsertBlock()->getParent()
                                       : nullptr;
        if(function)
        {
            cleanup.dropFlag = createEntryBlockAlloca(
                function, llvm::Type::getInt1Ty(context),
                varName + ".initialized");
            builder.CreateStore(llvm::ConstantInt::getTrue(context),
                                cleanup.dropFlag);
        }
        cleanupScopes.back().push_back(std::move(cleanup));
    }
}
