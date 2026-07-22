#include "ir.h"
#include "diagnostics.h"

#include <iostream>

void CodeGenerator::reportError(int line, const std::string& message)
{
    reportError(line, 0, message);
}

void CodeGenerator::reportError(int line, int col, const std::string& message)
{
    const std::string& file =
        sourceFileName.empty() ? std::string("<input>") : sourceFileName;
    mlang::diag::print_diagnostic_location(std::cerr, file, line, col, "error");
    std::cerr << mlang::diag::format_error_message(message) << std::endl;
    hasError = true;
}

void CodeGenerator::reportWarning(int line, int col, const std::string& message)
{
    const std::string& file =
        sourceFileName.empty() ? std::string("<input>") : sourceFileName;
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

void CodeGenerator::exitCleanupScope()
{
    if(cleanupScopes.empty() || pointerBorrowScopes.empty() ||
       variableScopeDepthScopes.empty())
        return;

    auto actions = std::move(cleanupScopes.back());
    cleanupScopes.pop_back();
    auto pointerEntries = std::move(pointerBorrowScopes.back());
    pointerBorrowScopes.pop_back();
    auto depthEntries = std::move(variableScopeDepthScopes.back());
    variableScopeDepthScopes.pop_back();

    for(auto it = pointerEntries.rbegin(); it != pointerEntries.rend(); ++it)
    {
        clearPointerBorrow(it->pointerVar);
        if(it->hadPreviousBorrow && !it->previousOwner.empty())
        {
            pointerBorrowTarget[it->pointerVar] = it->previousOwner;
            activeBorrowers[it->previousOwner].insert(it->pointerVar);
        }
    }
    for(auto it = depthEntries.rbegin(); it != depthEntries.rend(); ++it)
    {
        if(it->hadPreviousDepth)
            variableScopeDepth[it->varName] = it->previousDepth;
        else
            variableScopeDepth.erase(it->varName);
    }

    if(!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator())
        return;

    for(auto it = actions.rbegin(); it != actions.rend(); ++it)
    {
        if(movedVariables.count(it->varName))
            continue;
        if(!it->function)
            continue;
        auto nv = namedValues.find(it->varName);
        if(nv == namedValues.end() || !nv->second)
            continue;
        auto sv = structVariableTypes.find(it->varName);
        if(sv == structVariableTypes.end() || sv->second != it->structTypeName)
            continue;

        auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(nv->second);
        if(!alloca)
            continue;
        llvm::Type* allocaType = alloca->getAllocatedType();
        if(!allocaType || !allocaType->isStructTy())
            continue;

        if(it->callKind == ScopeCleanup::CallKind::ByPointerMethod)
        {
            builder.CreateCall(it->function, {alloca});
            continue;
        }

        llvm::Value* value =
            builder.CreateLoad(allocaType, alloca, it->varName + ".dropval");
        builder.CreateCall(it->function, {value});
    }
}

void CodeGenerator::emitAllActiveCleanups()
{
    if(!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator())
        return;

    for(auto scopeIt = cleanupScopes.rbegin(); scopeIt != cleanupScopes.rend();
        ++scopeIt)
    {
        auto& actions = *scopeIt;
        for(auto it = actions.rbegin(); it != actions.rend(); ++it)
        {
            if(movedVariables.count(it->varName))
                continue;
            if(!it->function)
                continue;
            auto nv = namedValues.find(it->varName);
            if(nv == namedValues.end() || !nv->second)
                continue;
            auto sv = structVariableTypes.find(it->varName);
            if(sv == structVariableTypes.end() ||
               sv->second != it->structTypeName)
                continue;

            auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(nv->second);
            if(!alloca)
                continue;
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(!allocaType || !allocaType->isStructTy())
                continue;

            if(it->callKind == ScopeCleanup::CallKind::ByPointerMethod)
            {
                builder.CreateCall(it->function, {alloca});
                continue;
            }

            llvm::Value* value = builder.CreateLoad(
                allocaType, alloca, it->varName + ".dropval.ret");
            builder.CreateCall(it->function, {value});
        }
    }
}

void CodeGenerator::emitActiveCleanupsDeeperThan(int scopeDepth)
{
    if(!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator())
        return;

    for(int depth = static_cast<int>(cleanupScopes.size()); depth > scopeDepth;
        --depth)
    {
        auto& actions = cleanupScopes[static_cast<size_t>(depth - 1)];
        for(auto it = actions.rbegin(); it != actions.rend(); ++it)
        {
            if(movedVariables.count(it->varName))
                continue;
            if(!it->function)
                continue;
            auto nv = namedValues.find(it->varName);
            if(nv == namedValues.end() || !nv->second)
                continue;
            auto sv = structVariableTypes.find(it->varName);
            if(sv == structVariableTypes.end() ||
               sv->second != it->structTypeName)
                continue;

            auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(nv->second);
            if(!alloca)
                continue;
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(!allocaType || !allocaType->isStructTy())
                continue;

            if(it->callKind == ScopeCleanup::CallKind::ByPointerMethod)
            {
                builder.CreateCall(it->function, {alloca});
                continue;
            }

            llvm::Value* value = builder.CreateLoad(
                allocaType, alloca, it->varName + ".dropval.exc");
            builder.CreateCall(it->function, {value});
        }
    }
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
        cleanupScopes.back().push_back(std::move(cleanup));
    }
}
