#include "ir.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constants.h>

bool CodeGenerator::emitRuntimeNullPointerCheck(llvm::Value* ptrValue, int line)
{
    if(!ptrValue || !ptrValue->getType()->isPointerTy())
        return false;

    if(llvm::isa<llvm::ConstantPointerNull>(ptrValue))
    {
        reportError(line, "cannot dereference null pointer");
        return false;
    }

    initializeFormatFunctions();

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* okBB =
        llvm::BasicBlock::Create(context, "ptr.not_null", function);
    llvm::BasicBlock* failBB =
        llvm::BasicBlock::Create(context, "ptr.null", function);
    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptrValue->getType()));
    llvm::Value* isNull =
        builder.CreateICmpEQ(ptrValue, nullPtr, "ptr.is_null");
    builder.CreateCondBr(isNull, failBB, okBB);

    builder.SetInsertPoint(failBB);
#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(
        "null pointer dereference\n", "ptr.null.msg");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(
        "null pointer dereference\n", "ptr.null.msg");
#endif
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* opaquePtrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* opaquePtrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Value* stderrVal =
        builder.CreateLoad(opaquePtrType, stderrPtr, "stderr");
    builder.CreateCall(fprintfFunc, {stderrVal, formatStr});
    builder.CreateCall(abortFunc, {});
    builder.CreateUnreachable();

    builder.SetInsertPoint(okBB);
    return true;
}

bool CodeGenerator::validatePointerDereference(ExpressionNode* pointerExpr,
                                               int line)
{
    if(auto knownNull = pointerExpressionKnownNull(pointerExpr))
    {
        if(*knownNull)
        {
            reportError(line, "cannot dereference null pointer");
            return false;
        }
    }

    std::string ownerName = getBorrowedOwnerForPointerExpression(pointerExpr);
    if(ownerName.empty())
    {
        if(unsafeDepth <= 0)
        {
            reportError(line,
                        "dereferencing raw pointer requires an unsafe block");
            return false;
        }
        return true;
    }

    if(globalNamedValues.find(ownerName) == globalNamedValues.end() &&
       isVariableMoved(ownerName))
    {
        reportError(line, "cannot dereference pointer to moved value: '" +
                              ownerName + "'");
        return false;
    }

    return true;
}
