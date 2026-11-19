#include "ir.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constants.h>

bool CodeGenerator::emitRuntimeNullPointerCheck(llvm::Value* ptrValue, int line)
{
    if(!ptrValue || !ptrValue->getType()->isPointerTy())
        return false;
    if(structuralRequirementProbe)
        return true;

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
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrValue->getType()));
    llvm::Value* isNull =
        builder.CreateICmpEQ(ptrValue, nullPtr, "ptr.is_null");
    builder.CreateCondBr(isNull, failBB, okBB);

    builder.SetInsertPoint(failBB);
#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr =
        builder
            .CreateGlobalString("null pointer dereference\n", "ptr.null.msg");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(
        "null pointer dereference\n",
        "ptr.null.msg");
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

bool CodeGenerator::validatePointerDereference(
    ExpressionNode* pointerExpr,
    int line)
{
    // Unevaluated requirements check pointer types without dereferencing a
    // runtime address or needing a surrounding unsafe block.
    if(structuralRequirementProbe)
        return getPointerElementType(pointerExpr, line) != nullptr;

    if(auto knownNull = pointerExpressionKnownNull(pointerExpr))
    {
        if(*knownNull)
        {
            reportError(line, "cannot dereference null pointer");
            return false;
        }
    }

    // A typed reference is created from a checked borrow; unlike a raw pointer
    // it does not require an unsafe block even when nested in another
    // reference.
    if(dynamic_cast<ReferenceTypeNode*>(getLValueType(pointerExpr, line)))
        return true;

    std::string ownerName = getBorrowedOwnerForPointerExpression(pointerExpr);
    if(ownerName.empty())
    {
        if(unsafeDepth <= 0)
        {
            reportError(
                line,
                "dereferencing raw pointer requires an unsafe block");
            return false;
        }
        return true;
    }

    if(globalNamedValues.find(ownerName) == globalNamedValues.end() &&
       isVariableMoved(ownerName))
    {
        reportError(
            line,
            "cannot dereference pointer to moved value: '" + ownerName + "'");
        return false;
    }

    return true;
}

llvm::Value* CodeGenerator::generatePointerIndexPointer(
    IndexExpressionNode* node,
    TypeNode*& elementType)
{
    auto* pointer =
        dynamic_cast<PointerTypeNode*>(getLValueType(node->base, node->line));
    if(!pointer)
        return nullptr;
    if(unsafeDepth <= 0 && !structuralRequirementProbe)
    {
        reportError(
            node->line,
            "raw pointer indexing requires an unsafe block");
        return nullptr;
    }
    if(!validatePointerDereference(node->base, node->line))
        return nullptr;
    llvm::Value* address = generateExpression(node->base);
    llvm::Value* index = generateExpression(node->index);
    elementType = pointer->elementType;
    llvm::Type* element = getLLVMTypeFromNode(elementType);
    if(!address || !index)
        return nullptr;
    if(!element || !element->isSized() || element->isVoidTy() ||
       module->getDataLayout().getTypeAllocSize(element) == 0)
    {
        reportError(
            node->line,
            "pointer indexing requires a sized nonempty element type");
        return nullptr;
    }
    if(!index->getType()->isIntegerTy())
    {
        reportError(node->line, "pointer index must be an integer");
        return nullptr;
    }
    if(!emitRuntimeNullPointerCheck(address, node->line))
        return nullptr;
    TypeNode* indexType = inferExpressionTypeNode(node->index, node->line);
    index = builder.CreateIntCast(
        index,
        llvm::Type::getInt64Ty(context),
        !indexType || !isUnsignedType(indexType->kind));
    return builder.CreateGEP(element, address, index, "ptr.index");
}
