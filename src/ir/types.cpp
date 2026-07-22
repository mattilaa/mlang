#include "ir.h"

#include <llvm/Config/llvm-config.h>

llvm::Type* CodeGenerator::getLLVMType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_VOID:
        return llvm::Type::getVoidTy(context);
    case TypeNode::TYPE_BOOL:
    case TypeNode::TYPE_BIT:
        return llvm::Type::getInt1Ty(context);
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I32:
        return llvm::Type::getInt32Ty(context);
    case TypeNode::TYPE_FLOAT:
        return llvm::Type::getFloatTy(context);
    case TypeNode::TYPE_DOUBLE:
        return llvm::Type::getDoubleTy(context);
    case TypeNode::TYPE_STRING:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    case TypeNode::TYPE_STR8:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    case TypeNode::TYPE_STR16:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt16Ty(context), 0);
#endif
    case TypeNode::TYPE_I8:
        return llvm::Type::getInt8Ty(context);
    case TypeNode::TYPE_I16:
        return llvm::Type::getInt16Ty(context);
    case TypeNode::TYPE_I64:
        return llvm::Type::getInt64Ty(context);
    case TypeNode::TYPE_U8:
        return llvm::Type::getInt8Ty(context);
    case TypeNode::TYPE_U16:
        return llvm::Type::getInt16Ty(context);
    case TypeNode::TYPE_U32:
        return llvm::Type::getInt32Ty(context);
    case TypeNode::TYPE_U64:
        return llvm::Type::getInt64Ty(context);
    case TypeNode::TYPE_PTR:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    default:
        return nullptr;
    }
}

bool CodeGenerator::isUnsignedType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
        return true;
    default:
        return false;
    }
}
