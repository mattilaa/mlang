#include "ir.h"
#include "ir/common.h"

#include <iostream>
#include <llvm/Config/llvm-config.h>

using mlang::ir_detail::common::Helpers;

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

llvm::Type* CodeGenerator::getLLVMTypeFromNode(TypeNode* typeNode)
{
    if(!typeNode)
        return nullptr;

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    // Must check this BEFORE StructTypeRefNode since GenericStructTypeRefNode
    // is a more specific case
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(typeNode))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        auto it = structTypes.find(mangledName);
        if(it != structTypes.end())
        {
            return it->second;
        }
        std::cerr << "Failed to monomorphize struct: "
                  << genStructRef->structName << std::endl;
        return nullptr;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(typeNode))
    {
        std::string resolvedEnumName =
            resolveVisibleEnumName(structRef->structName);
        if(!resolvedEnumName.empty())
        {
            auto bkIt = enumBaseTypes.find(resolvedEnumName);
            TypeNode::TypeKind baseKind = TypeNode::TYPE_I32;
            if(bkIt != enumBaseTypes.end())
                baseKind = bkIt->second;
            return getLLVMType(baseKind);
        }

        std::string resolvedStructName =
            resolveVisibleStructName(structRef->structName);
        auto it = structTypes.find(resolvedStructName);
        if(it != structTypes.end())
        {
            return it->second;
        }

        // When generating monomorphized generic method bodies, unresolved
        // identifiers like `T` may appear in type positions inside the body
        // AST. Resolve them through the active type-param bindings.
        auto bindIt = activeTypeParamBindings.find(structRef->structName);
        if(bindIt != activeTypeParamBindings.end() && bindIt->second)
        {
            return getLLVMTypeFromNode(bindIt->second);
        }

        // Generic container type args in generic structs can surface as
        // textual struct refs (e.g. "list<i64>"). Reparse and resolve.
        if(structRef->structName.find('<') != std::string::npos)
        {
            TypeNode* reparsed = Helpers::type_from_text(structRef->structName);
            if(reparsed && !dynamic_cast<StructTypeRefNode*>(reparsed))
                return getLLVMTypeFromNode(reparsed);
        }

        // Check if this is a type parameter (like T, U) - should not reach here
        // in properly monomorphized code.
        return nullptr;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(typeNode))
    {
        std::vector<llvm::Type*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
                return nullptr;
            elemTypes.push_back(elemType);
        }
        return llvm::StructType::get(context, elemTypes);
    }

    // Handle trait object type: { i8* data, i8* vtable }
    if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(typeNode))
    {
        return getTraitObjectType(traitObj->traitName);
    }

    // Handle generic list type
    if(auto* listType = dynamic_cast<GenericListTypeNode*>(typeNode))
    {
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* elemType = getLLVMTypeFromNode(listType->elementType);
        llvm::Type* ptrType = llvm::PointerType::get(elemType, 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        return llvm::StructType::get(context, listStructTypes);
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(typeNode))
    {
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        return llvm::StructType::get(context, mapStructTypes);
    }

    // Handle pointer type
    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(typeNode))
    {
        llvm::Type* elemType = getLLVMTypeFromNode(ptrType->elementType);
        if(!elemType)
            return nullptr;
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(elemType, 0);
#endif
    }

    // Handle reference type (&T and &mut T)
    // At LLVM level &string == string (both char*); resolve to element type.
    if(auto* refType = dynamic_cast<ReferenceTypeNode*>(typeNode))
    {
        return getLLVMTypeFromNode(refType->elementType);
    }

    // Fall back to basic type kind
    return getLLVMType(typeNode->kind);
}
