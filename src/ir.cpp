#include "ir.h"
#include "diagnostics.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"
#include "ir/expression_type_kind.h"
#include "ir/return_inference.h"
#include "module.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <llvm/IR/Verifier.h>
#include <pthread.h>
#include <unordered_map>
#include <unordered_set>

namespace
{

using mlang::ir_detail::ast_analysis::collect_used_idents;
using mlang::ir_detail::ast_analysis::contains_exception_control_flow;
using mlang::ir_detail::ast_analysis::contains_update_expression;
using mlang::ir_detail::common::Helpers;
using mlang::ir_detail::return_inference::infer_function_return_type;

} // namespace

#include <llvm/Config/llvm-config.h>
#include <llvm/Bitcode/BitcodeWriter.h>

void CodeGenerator::registerPointerBorrow(const std::string& pointerVar,
                                          ExpressionNode* expr, int line,
                                          bool isMutable)
{
    clearPointerBorrow(pointerVar);

    auto registerOwnerBorrow = [&](const std::string& ownerName,
                                   bool enforceExclusive) -> bool
    {
        if(globalNamedValues.find(ownerName) == globalNamedValues.end() &&
           isVariableMoved(ownerName))
        {
            reportError(line, "cannot borrow moved value: '" + ownerName + "'");
            return false;
        }

        auto pointerDepthIt = variableScopeDepth.find(pointerVar);
        auto ownerDepthIt = variableScopeDepth.find(ownerName);
        if(pointerDepthIt != variableScopeDepth.end() &&
           ownerDepthIt != variableScopeDepth.end() &&
           ownerDepthIt->second > pointerDepthIt->second)
        {
            reportError(line, "cannot borrow '" + ownerName +
                                  "' into longer-lived pointer '" + pointerVar +
                                  "'");
            return false;
        }

        if(isMutable)
        {
            // &mut: reject if any shared borrowers exist
            auto sharedIt = activeBorrowers.find(ownerName);
            if(sharedIt != activeBorrowers.end() && !sharedIt->second.empty())
            {
                reportError(line,
                            "cannot borrow '" + ownerName +
                                "' as mutable because it is already borrowed");
                return false;
            }
            // &mut: reject if another &mut borrow exists
            auto mutIt = activeMutBorrower.find(ownerName);
            if(mutIt != activeMutBorrower.end() && mutIt->second != pointerVar)
            {
                reportError(line, "cannot borrow '" + ownerName +
                                      "' as mutable more than once at a time");
                return false;
            }
            // Owner must be var (mutable)
            if(constantVariables.count(ownerName))
            {
                reportError(line, "cannot borrow immutable variable '" +
                                      ownerName + "' as mutable");
                return false;
            }
            pointerBorrowTarget[pointerVar] = ownerName;
            pointerKnownNull[pointerVar] = false;
            activeMutBorrower[ownerName] = pointerVar;
            return true;
        }

        // Shared borrow: reject if a &mut borrow is active
        auto mutIt = activeMutBorrower.find(ownerName);
        if(mutIt != activeMutBorrower.end())
        {
            reportError(line,
                        "cannot borrow '" + ownerName +
                            "' as immutable because it is also borrowed as "
                            "mutable by '" +
                            mutIt->second + "'");
            return false;
        }

        auto activeIt = activeBorrowers.find(ownerName);
        if(enforceExclusive && activeIt != activeBorrowers.end())
        {
            for(const auto& borrower : activeIt->second)
            {
                if(borrower != pointerVar)
                {
                    reportError(line, "cannot borrow '" + ownerName +
                                          "' while already borrowed by '" +
                                          borrower + "'");
                    return false;
                }
            }
        }

        pointerBorrowTarget[pointerVar] = ownerName;
        pointerKnownNull[pointerVar] = false;
        activeBorrowers[ownerName].insert(pointerVar);
        return true;
    };

    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto borrowIt = pointerBorrowTarget.find(idExpr->name);
        if(borrowIt != pointerBorrowTarget.end())
        {
            if(idExpr->name != pointerVar)
            {
                // For Copy types (strings), assigning a borrow variable is
                // just copying the char* value — no alias tracking needed.
                auto typeIt = variableTypes.find(idExpr->name);
                if(typeIt != variableTypes.end() &&
                   (typeIt->second == TypeNode::TYPE_STRING ||
                    typeIt->second == TypeNode::TYPE_STR8 ||
                    typeIt->second == TypeNode::TYPE_STR16))
                {
                    return; // plain char* copy, no borrow alias
                }
                reportError(line, "cannot alias exclusive borrow from '" +
                                      idExpr->name + "' into '" + pointerVar +
                                      "'");
                return;
            }
            (void)registerOwnerBorrow(borrowIt->second, true);
            return;
        }
    }

    auto* unary = dynamic_cast<UnaryOpNode*>(expr);
    if(!unary || (unary->op != UnaryOpNode::OP_ADDR &&
                  unary->op != UnaryOpNode::OP_ADDR_MUT))
        return;

    std::string ownerName = resolveBorrowOwnerFromLValue(unary->operand);
    if(ownerName.empty())
        return;

    // Shared borrows allow multiple concurrent borrowers; only typed exclusive
    // pointers (e.g. *T owning pointers) enforce a single active borrow.
    bool enforceExclusive = false;
    auto pit = pointerElementTypes.find(pointerVar);
    TypeNode* ptrElemType =
        pit != pointerElementTypes.end() ? pit->second : nullptr;
    if(ptrElemType)
    {
        TypeNode::TypeKind ownerKind = TypeNode::TYPE_VOID;
        bool hasOwnerKind = false;
        auto vit = variableTypes.find(ownerName);
        if(vit != variableTypes.end())
        {
            ownerKind = vit->second;
            hasOwnerKind = true;
        }
        else
        {
            auto gvit = globalVariableTypes.find(ownerName);
            if(gvit != globalVariableTypes.end())
            {
                ownerKind = gvit->second;
                hasOwnerKind = true;
            }
        }

        if(hasOwnerKind)
        {
            bool sameOwnerType = false;
            if(ownerKind == TypeNode::TYPE_STRUCT &&
               ptrElemType->kind == TypeNode::TYPE_STRUCT)
            {
                std::string ownerStructName;
                auto sit = structVariableTypes.find(ownerName);
                if(sit != structVariableTypes.end())
                    ownerStructName = sit->second;
                else
                {
                    auto gsit = globalStructVariableTypes.find(ownerName);
                    if(gsit != globalStructVariableTypes.end())
                        ownerStructName = gsit->second;
                }

                if(auto* sr = dynamic_cast<StructTypeRefNode*>(ptrElemType))
                    sameOwnerType = (sr->structName == ownerStructName);
                else if(auto* gsr = dynamic_cast<GenericStructTypeRefNode*>(
                            ptrElemType))
                    sameOwnerType = (getOrCreateMonomorphizedStruct(
                                         gsr->structName, gsr->typeArgs) ==
                                     ownerStructName);
                // Be conservative when owner struct metadata is unavailable.
                if(ownerStructName.empty())
                    sameOwnerType = true;
            }
            else
            {
                sameOwnerType = (ownerKind == ptrElemType->kind);
            }

            enforceExclusive = sameOwnerType;
        }
    }

    (void)registerOwnerBorrow(ownerName, enforceExclusive);
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

        auto it = structTypes.find(structRef->structName);
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

llvm::Value*
CodeGenerator::resetCopiedStructState(llvm::Value* value,
                                      const std::string& structName)
{
    if(!value)
        return nullptr;

    auto* structType = llvm::dyn_cast<llvm::StructType>(value->getType());
    if(!structType || structName.empty())
        return value;

    auto memberIt = structMembers.find(structName);
    if(memberIt == structMembers.end())
        return value;

    llvm::Value* adjusted = value;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        const StructFieldLayout* layout =
            getStructFieldLayout(structName, static_cast<int>(i));
        if(!layout || layout->packedBit)
            continue;

        const std::string& fieldName = members[i].first;
        TypeNode* fieldType = members[i].second;
        llvm::Type* storageType =
            structType->getElementType(layout->storageIndex);

        if(Helpers::isSynthesizedPropertyLockFieldName(fieldName))
        {
            adjusted = builder.CreateInsertValue(
                adjusted, llvm::Constant::getNullValue(storageType),
                {layout->storageIndex}, structName + "." + fieldName + ".copy");
            continue;
        }

        std::string nestedStructName;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(fieldType))
            nestedStructName = structRef->structName;
        else if(auto* genStructRef =
                    dynamic_cast<GenericStructTypeRefNode*>(fieldType))
            nestedStructName = getOrCreateMonomorphizedStruct(
                genStructRef->structName, genStructRef->typeArgs);

        if(nestedStructName.empty() || !storageType->isStructTy())
            continue;

        llvm::Value* nestedValue = builder.CreateExtractValue(
            adjusted, {layout->storageIndex},
            structName + "." + fieldName + ".copy.extract");
        llvm::Value* nestedAdjusted =
            resetCopiedStructState(nestedValue, nestedStructName);
        adjusted = builder.CreateInsertValue(
            adjusted, nestedAdjusted, {layout->storageIndex},
            structName + "." + fieldName + ".copy.insert");
    }

    return adjusted;
}

llvm::Value* CodeGenerator::applyStructCopySemantics(llvm::Value* value,
                                                     TypeNode* semanticType)
{
    if(!value)
        return nullptr;

    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(semanticType))
        return resetCopiedStructState(value, structRef->structName);

    if(auto* genStructRef =
           dynamic_cast<GenericStructTypeRefNode*>(semanticType))
    {
        return resetCopiedStructState(
            value, getOrCreateMonomorphizedStruct(genStructRef->structName,
                                                  genStructRef->typeArgs));
    }

    return applyStructCopySemantics(value);
}

llvm::Value* CodeGenerator::applyStructCopySemantics(llvm::Value* value)
{
    if(!value)
        return nullptr;

    auto* structType = llvm::dyn_cast<llvm::StructType>(value->getType());
    if(!structType || !structType->hasName())
        return value;

    return resetCopiedStructState(value, structType->getName().str());
}

llvm::Value* CodeGenerator::buildDebugString(ExpressionNode* expr, bool pretty,
                                             int line)
{
    llvm::Value* val = generateExpression(expr);
    if(!val)
        return Helpers::create_global_cstring(builder, "<null>");
    if(val->getType()->isStructTy())
    {
        std::string structName = val->getType()->getStructName().str();
        if(structName.empty())
            structName = getStructTypeName(expr);
        return buildStructDebugString(val, structName, pretty, line);
    }

    std::vector<llvm::Value*> argValues;
    std::string cFormat;
    appendFormatValue(expr, val, false, false, false, cFormat, argValues, line);

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "dbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(cFormat, "dbgfmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 = builder.CreateCall(snprintfFunc, sizeArgs, "dbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "dbglen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "dbgsz");
    llvm::Value* buffer = builder.CreateCall(mallocFunc, {size}, "dbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

llvm::Value*
CodeGenerator::buildStructDebugString(llvm::Value* structVal,
                                      const std::string& structName,
                                      bool pretty, int line)
{
    initializeFormatFunctions();

    auto it = structMembers.find(structName);
    if(it == structMembers.end())
    {
        reportError(line, "unknown struct for debug: " + structName);
        return Helpers::create_global_cstring(builder, "<struct>");
    }

    std::string displayName = structName;
    if(auto dit = structDebugDisplayNames.find(structName);
       dit != structDebugDisplayNames.end())
    {
        displayName = dit->second;
    }
    if(auto mit = mangledToGenericName.find(structName);
       mit != mangledToGenericName.end())
    {
        displayName = mit->second;
    }

    std::string fmt = displayName + (pretty ? " {\n" : " { ");
    std::vector<llvm::Value*> argValues;

    for(size_t idx = 0; idx < it->second.size(); ++idx)
    {
        const auto& member = it->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;

        if(pretty)
            fmt += "    " + memberName + ": ";
        else
            fmt += memberName + ": ";

        llvm::Value* fieldVal = builder.CreateExtractValue(
            structVal, static_cast<unsigned>(idx), "dbgfield");

        bool handled = false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType))
        {
            std::string fieldStruct = structRef->structName;
            if(!debugStructs.count(fieldStruct))
            {
                reportError(line, "struct '" + fieldStruct +
                                      "' does not derive Debug");
            }
            llvm::Value* fieldStr =
                buildStructDebugString(fieldVal, fieldStruct, pretty, line);
            fmt += "%s";
            argValues.push_back(fieldStr);
            handled = true;
        }

        if(!handled)
        {
            switch(memberType->kind)
            {
            case TypeNode::TYPE_BOOL:
            {
                fmt += "%d";
                llvm::Value* intVal = builder.CreateZExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgbool");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_I8:
            case TypeNode::TYPE_I16:
            case TypeNode::TYPE_INT:
            {
                fmt += "%d";
                llvm::Value* intVal = builder.CreateSExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgint");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_U8:
            case TypeNode::TYPE_U16:
            case TypeNode::TYPE_U32:
            {
                fmt += "%u";
                llvm::Value* intVal = builder.CreateZExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgu");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_I32:
            {
                fmt += "%d";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_I64:
            {
                fmt += "%lld";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_U64:
            {
                fmt += "%llu";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_FLOAT:
            {
                fmt += "%f";
                llvm::Value* doubleVal = builder.CreateFPExt(
                    fieldVal, llvm::Type::getDoubleTy(context), "dbgfloat");
                argValues.push_back(doubleVal);
                break;
            }
            case TypeNode::TYPE_DOUBLE:
            {
                fmt += "%f";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_STRING:
            case TypeNode::TYPE_STR8:
            case TypeNode::TYPE_STR16:
            {
                fmt += "%s";
                argValues.push_back(fieldVal);
                break;
            }
            default:
                fmt += "<unsupported>";
                break;
            }
        }

        if(pretty)
            fmt += ",\n";
        else if(idx + 1 < it->second.size())
            fmt += ", ";
        else
            fmt += " ";
    }

    if(pretty)
        fmt += "}";
    else
        fmt += "}";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(fmt, "dbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(fmt, "dbgfmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 = builder.CreateCall(snprintfFunc, sizeArgs, "dbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "dbglen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "dbgsz");
    llvm::Value* buffer = builder.CreateCall(mallocFunc, {size}, "dbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

llvm::Value* CodeGenerator::buildStructJsonString(llvm::Value* structVal,
                                                  const std::string& structName,
                                                  bool pretty, int line,
                                                  int indentLevel)
{
    initializeFormatFunctions();

    auto escapeJsonStringValue = [&](llvm::Value* strVal) -> llvm::Value*
    { return builder.CreateCall(jsonEscapeFunc, {strVal}, "json.escape"); };

    auto it = structMembers.find(structName);
    if(it == structMembers.end())
    {
        reportError(line, "unknown struct for json debug: " + structName);
        return Helpers::create_global_cstring(builder, "{}");
    }

    std::string displayName = structName;
    if(auto dit = structDebugDisplayNames.find(structName);
       dit != structDebugDisplayNames.end())
    {
        displayName = dit->second;
    }
    if(auto mit = mangledToGenericName.find(structName);
       mit != mangledToGenericName.end())
    {
        displayName = mit->second;
    }

    std::string currentIndent(indentLevel * 2, ' ');
    std::string childIndent((indentLevel + 1) * 2, ' ');
    std::string innerSep = pretty ? ",\n" + childIndent : ",";
    std::string fmt = "{";
    if(pretty)
        fmt += "\n" + childIndent;
    fmt += "\"type\": \"" + displayName + "\"";
    std::vector<llvm::Value*> argValues;

    for(size_t idx = 0; idx < it->second.size(); ++idx)
    {
        const auto& member = it->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;
        llvm::Value* fieldVal = builder.CreateExtractValue(
            structVal, static_cast<unsigned>(idx), "jsonfield");

        fmt += innerSep + "\"" + memberName + "\": ";

        bool handled = false;

        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType))
        {
            std::string resolvedEnumName =
                resolveVisibleEnumName(structRef->structName);
            if(!resolvedEnumName.empty())
            {
                fmt += "\"%s\"";
                llvm::Value* enumStr =
                    buildEnumString(fieldVal, resolvedEnumName, line);
                argValues.push_back(escapeJsonStringValue(enumStr));
                handled = true;
            }
            else
            {
                std::string fieldStruct = structRef->structName;
                if(!debugStructs.count(fieldStruct))
                {
                    reportError(line, "struct '" + fieldStruct +
                                          "' does not derive Debug");
                }
                llvm::Value* fieldStr = buildStructJsonString(
                    fieldVal, fieldStruct, pretty, line, indentLevel + 1);
                fmt += "%s";
                argValues.push_back(fieldStr);
                handled = true;
            }
        }

        if(handled)
            continue;

        switch(memberType ? memberType->kind : TypeNode::TYPE_VOID)
        {
        case TypeNode::TYPE_BOOL:
        {
#if LLVM_VERSION_MAJOR >= 21
            llvm::Value* trueStr =
                builder.CreateGlobalString("true", "json.true");
            llvm::Value* falseStr =
                builder.CreateGlobalString("false", "json.false");
#else
            llvm::Value* trueStr =
                builder.CreateGlobalStringPtr("true", "json.true");
            llvm::Value* falseStr =
                builder.CreateGlobalStringPtr("false", "json.false");
#endif
            fmt += "%s";
            argValues.push_back(builder.CreateSelect(
                fieldVal, trueStr, falseStr, "json.field.bool"));
            break;
        }
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
            fmt += "%d";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_I64:
            fmt += "%lld";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
            fmt += "%u";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U64:
            fmt += "%llu";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_FLOAT:
        {
            fmt += "%f";
            llvm::Value* doubleVal = builder.CreateFPExt(
                fieldVal, llvm::Type::getDoubleTy(context), "json.float");
            argValues.push_back(doubleVal);
            break;
        }
        case TypeNode::TYPE_DOUBLE:
            fmt += "%f";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_STR8:
        case TypeNode::TYPE_STR16:
        case TypeNode::TYPE_PTR:
            fmt += "\"%s\"";
            argValues.push_back(escapeJsonStringValue(fieldVal));
            break;
        default:
            fmt += "\"<unsupported>\"";
            break;
        }
    }

    if(pretty)
        fmt += "\n" + currentIndent;
    fmt += "}";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(fmt, "jsondbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(fmt, "jsondbgfmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 =
        builder.CreateCall(snprintfFunc, sizeArgs, "jsondbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "jsondbglen64");
    llvm::Value* size = builder.CreateAdd(
        len64, llvm::ConstantInt::get(int64Type, 1), "jsondbgsz");
    llvm::Value* buffer = builder.CreateCall(mallocFunc, {size}, "jsondbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

llvm::Value* CodeGenerator::buildStructSerdeJsonString(
    llvm::Value* structVal, const std::string& structName, int line,
    int indentLevel)
{
    initializeFormatFunctions();

    auto escapeJsonStringValue = [&](llvm::Value* strVal) -> llvm::Value*
    { return builder.CreateCall(jsonEscapeFunc, {strVal}, "json.escape"); };

    auto it = structMembers.find(structName);
    if(it == structMembers.end())
    {
        reportError(line, "unknown struct for json serde: " + structName);
        return Helpers::create_global_cstring(builder, "{}");
    }

    std::string displayName = structName;
    if(auto dit = structDebugDisplayNames.find(structName);
       dit != structDebugDisplayNames.end())
    {
        displayName = dit->second;
    }
    if(auto mit = mangledToGenericName.find(structName);
       mit != mangledToGenericName.end())
    {
        displayName = mit->second;
    }

    std::string currentIndent(indentLevel * 2, ' ');
    std::string childIndent((indentLevel + 1) * 2, ' ');
    std::string innerSep = ",\n" + childIndent;
    std::string fmt = "{\n" + childIndent + "\"type\": \"" + displayName + "\"";
    std::vector<llvm::Value*> argValues;

    std::string propFmt;
    std::vector<llvm::Value*> propArgs;
    bool hasPropertyMetadata = false;

    for(size_t idx = 0; idx < it->second.size(); ++idx)
    {
        const auto* access = getStructFieldAccessInfo(structName,
                                                      static_cast<int>(idx));
        if(access && access->isSynthesizedPropertyStorage)
            continue;

        const auto& member = it->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;
        llvm::Value* fieldVal = builder.CreateExtractValue(
            structVal, static_cast<unsigned>(idx), "jsonserde.field");

        fmt += innerSep + "\"" + memberName + "\": ";

        bool handled = false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType))
        {
            std::string resolvedEnumName =
                resolveVisibleEnumName(structRef->structName);
            if(!resolvedEnumName.empty())
            {
                fmt += "\"%s\"";
                llvm::Value* enumStr =
                    buildEnumString(fieldVal, resolvedEnumName, line);
                argValues.push_back(escapeJsonStringValue(enumStr));
                handled = true;
            }
            else
            {
                std::string fieldStruct = structRef->structName;
                if(!jsonStructs.count(fieldStruct))
                {
                    reportError(line, "struct '" + fieldStruct +
                                          "' does not derive Json");
                    return Helpers::create_global_cstring(builder, "{}");
                }
                llvm::Value* fieldStr = buildStructSerdeJsonString(
                    fieldVal, fieldStruct, line, indentLevel + 1);
                fmt += "%s";
                argValues.push_back(fieldStr);
                handled = true;
            }
        }

        if(handled)
            goto append_property_metadata;

        switch(memberType ? memberType->kind : TypeNode::TYPE_VOID)
        {
        case TypeNode::TYPE_BOOL:
        {
#if LLVM_VERSION_MAJOR >= 21
            llvm::Value* trueStr =
                builder.CreateGlobalString("true", "json.true");
            llvm::Value* falseStr =
                builder.CreateGlobalString("false", "json.false");
#else
            llvm::Value* trueStr =
                builder.CreateGlobalStringPtr("true", "json.true");
            llvm::Value* falseStr =
                builder.CreateGlobalStringPtr("false", "json.false");
#endif
            fmt += "%s";
            argValues.push_back(builder.CreateSelect(
                fieldVal, trueStr, falseStr, "jsonserde.bool"));
            break;
        }
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
            fmt += "%d";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_I64:
            fmt += "%lld";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
            fmt += "%u";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U64:
            fmt += "%llu";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_FLOAT:
        {
            fmt += "%f";
            llvm::Value* doubleVal = builder.CreateFPExt(
                fieldVal, llvm::Type::getDoubleTy(context),
                "jsonserde.float");
            argValues.push_back(doubleVal);
            break;
        }
        case TypeNode::TYPE_DOUBLE:
            fmt += "%f";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_STR8:
            fmt += "\"%s\"";
            argValues.push_back(escapeJsonStringValue(fieldVal));
            break;
        default:
            reportError(line, "field '" + memberName + "' of struct '" +
                                  structName +
                                  "' has unsupported Json derive type");
            return Helpers::create_global_cstring(builder, "{}");
        }

append_property_metadata:
        if(access && access->isProperty)
        {
            if(!hasPropertyMetadata)
            {
                propFmt = "\"@property\": {";
                hasPropertyMetadata = true;
            }
            else
            {
                propFmt += ",";
            }

            propFmt += "\"" + memberName + "\": {"
                       "\"hidden\": %s,"
                       "\"protected\": %s,"
                       "\"atomic\": %s,"
                       "\"mutex\": %s,"
                       "\"recursive\": %s}";

#if LLVM_VERSION_MAJOR >= 21
            llvm::Value* trueStr =
                builder.CreateGlobalString("true", "json.meta.true");
            llvm::Value* falseStr =
                builder.CreateGlobalString("false", "json.meta.false");
#else
            llvm::Value* trueStr =
                builder.CreateGlobalStringPtr("true", "json.meta.true");
            llvm::Value* falseStr =
                builder.CreateGlobalStringPtr("false", "json.meta.false");
#endif

            auto boolStr = [&](bool flag, const char* name) -> llvm::Value* {
                return flag ? trueStr : falseStr;
            };

            propArgs.push_back(boolStr(
                access->encapsulation == FieldEncapsulation::Hidden,
                "hidden"));
            propArgs.push_back(boolStr(
                access->encapsulation == FieldEncapsulation::Protected,
                "protected"));
            propArgs.push_back(
                boolStr(access->isAtomicProperty, "atomic"));
            propArgs.push_back(boolStr(access->isMutexProperty, "mutex"));
            propArgs.push_back(
                boolStr(access->isRecursiveProperty, "recursive"));
        }
    }

    if(hasPropertyMetadata)
    {
        propFmt += "}";
        fmt += innerSep + propFmt;
        argValues.insert(argValues.end(), propArgs.begin(), propArgs.end());
    }

    fmt += "\n" + currentIndent + "}";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(fmt, "jsonserdefmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(fmt, "jsonserdefmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 =
        builder.CreateCall(snprintfFunc, sizeArgs, "jsonserde.len");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "jsonserde.len64");
    llvm::Value* size = builder.CreateAdd(
        len64, llvm::ConstantInt::get(int64Type, 1), "jsonserde.size");
    llvm::Value* buffer =
        builder.CreateCall(mallocFunc, {size}, "jsonserde.buf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

void CodeGenerator::generateCode(ProgramNode* program)
{
    globalNamedValues.clear();
    globalConstantVariables.clear();
    globalVariableTypes.clear();
    globalStructVariableTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    constexprValues.clear();
    deferredModuleFunctionDefs.clear();

    ensureHandleBuiltin(program);
    ensureThreadBuiltin(program);
    ensureMutexBuiltin(program);
    ensureAtomic64Builtin(program);
    ensureOptionBuiltin(program);
    ensureResultBuiltin(program);

    traitDefinitions.clear();
    structImplementedTraits.clear();
    for(auto* traitDef : program->traitDefs)
    {
        if(!traitDef || traitDef->name.empty())
            continue;
        traitDefinitions[traitDef->name] = traitDef;
    }
    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl || impl->traitName.empty())
                continue;
            if(!impl->typeParams.empty())
                continue;
            structImplementedTraits[impl->structName].insert(impl->traitName);
        }
    }

    resolveTypeAliasesInProgram(program);

    for(auto* cexprDecl : program->cexprDecls)
    {
        if(cexprDecl)
            generateCexprDeclaration(cexprDecl, false);
    }

    enum class MainArgMode
    {
        None,
        ArgsList,
        ArgsListWithCount
    };

    FunctionDefNode* mainDef = nullptr;
    FunctionDefNode* firstUserFunction = nullptr;
    MainArgMode mainArgMode = MainArgMode::None;
    GenericListTypeNode* mainArgsListType = nullptr;
    TypeNode::TypeKind mainArgcKind = TypeNode::TYPE_VOID;
    std::vector<FunctionDefNode*> testFunctions;

    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(fn && !fn->isExtern && !firstUserFunction)
                firstUserFunction = fn;
            if(fn && fn->name == "main" && !fn->isExtern)
            {
                mainDef = fn;
            }
            if(fn && fn->isTest && !fn->isExtern)
                testFunctions.push_back(fn);
        }
    }

    if(program->functionList)
    {
        std::unordered_map<std::string, TypeNode::TypeKind> fnReturnKinds;
        for(auto* fn : program->functionList->functions)
        {
            if(!fn || fn->isExtern || !fn->returnType)
                continue;
            auto it = fnReturnKinds.find(fn->name);
            if(it == fnReturnKinds.end())
            {
                fnReturnKinds[fn->name] =
                    Helpers::normalizeInferredKind(fn->returnType->kind);
            }
            else if(it->second != Helpers::normalizeInferredKind(fn->returnType->kind))
            {
                // Ambiguous overload returns for name-only inference; drop
                // entry.
                fnReturnKinds.erase(it);
            }
        }

        bool progress = true;
        while(progress)
        {
            progress = false;
            for(auto* fn : program->functionList->functions)
            {
                if(!fn || fn->isExtern || fn->returnType)
                    continue;
                if(!fn->typeParams.empty() && fn->isCexpr)
                    continue;
                if(fn->name == "main")
                {
                    fn->returnType = new TypeNode(TypeNode::TYPE_I32);
                    fnReturnKinds[fn->name] = TypeNode::TYPE_I32;
                    progress = true;
                    continue;
                }

                TypeNode::TypeKind inferredKind = TypeNode::TYPE_VOID;
                std::string reason;
                if(!infer_function_return_type(fn, fnReturnKinds, inferredKind,
                                               reason))
                    continue;

                fn->returnType = new TypeNode(inferredKind);
                fnReturnKinds[fn->name] = inferredKind;
                progress = true;
            }
        }

        for(auto* fn : program->functionList->functions)
        {
            if(!fn || fn->isExtern || fn->returnType)
                continue;
            if(!fn->typeParams.empty() && fn->isCexpr)
                continue;
            TypeNode::TypeKind inferredKind = TypeNode::TYPE_VOID;
            std::string reason;
            if(!infer_function_return_type(fn, fnReturnKinds, inferredKind,
                                           reason))
            {
                reportError(fn->line,
                            "cannot infer return type for function '" +
                                fn->name + "': " + reason);
                fn->returnType = new TypeNode(TypeNode::TYPE_VOID);
            }
            else
            {
                fn->returnType = new TypeNode(inferredKind);
            }
        }
    }

    if(testMode && mainDef)
    {
        reportError(mainDef->line,
                    "main is not allowed in test mode; use #[test] functions");
    }
    else if(requireMain && !mainDef)
    {
        std::string msg =
            "missing entry point: executable builds require 'fn main() -> i32'";
        if(firstUserFunction)
        {
            msg += "; found function '" + firstUserFunction->name +
                   "' instead";
            if(firstUserFunction->name != "main")
                msg += " (did you mean 'main'?)";
        }
        reportError(firstUserFunction ? firstUserFunction->line : 1, msg);
    }

    if(mainDef)
    {
        size_t paramCount = mainDef->parameters->parameters.size();
        bool returnOk = mainDef->returnType &&
                        (mainDef->returnType->kind == TypeNode::TYPE_INT ||
                         mainDef->returnType->kind == TypeNode::TYPE_I32);
        if(!returnOk)
        {
            reportError(
                mainDef->line,
                "invalid signature for 'main': return type must be i32");
        }

        if(paramCount == 0)
        {
            mainArgMode = MainArgMode::None;
        }
        else if(paramCount == 1 || paramCount == 2)
        {
            size_t listIndex = paramCount - 1;
            if(paramCount == 2)
            {
                mainArgcKind = mainDef->parameters->parameters[0]->type->kind;
                if(!(mainArgcKind == TypeNode::TYPE_INT ||
                     mainArgcKind == TypeNode::TYPE_I32 ||
                     mainArgcKind == TypeNode::TYPE_I64))
                {
                    reportError(
                        mainDef->line,
                        "invalid signature for 'main': argc must be i32/i64");
                }
            }

            auto* listType = dynamic_cast<GenericListTypeNode*>(
                mainDef->parameters->parameters[listIndex]->type);
            if(!listType)
            {
                reportError(
                    mainDef->line,
                    "invalid signature for 'main': argv must be list<str8>");
            }
            else
            {
                auto elemKind = listType->elementType->kind;
                if(!(elemKind == TypeNode::TYPE_STR8 ||
                     elemKind == TypeNode::TYPE_STRING))
                {
                    reportError(mainDef->line, "invalid signature for 'main': "
                                               "argv must be list<str8>");
                }
                else
                {
                    mainArgsListType = listType;
                }
            }

            mainArgMode = (paramCount == 1) ? MainArgMode::ArgsList
                                            : MainArgMode::ArgsListWithCount;
            mainDef->name = "__mlang_user_main";
        }
        else
        {
            reportError(mainDef->line,
                        "invalid signature for 'main': expected no args, "
                        "list<str8>, or (i32, list<str8>)");
        }
    }

    // Reserved type keywords and type/name conflicts.
    const std::unordered_set<std::string> reservedTypeNames = {
        "void", "bool", "f32", "f64", "str8", "str16", "list", "map", "tuple",
        "i8",   "i16",  "i32", "i64", "u8",   "u16",   "u32",  "u64"};

    std::map<std::string, std::pair<std::string, int>> typeDefs;
    if(!program->typeAliases.empty())
    {
        std::set<std::string> seenAliasNames;
        for(auto* aliasDef : program->typeAliases)
        {
            if(!aliasDef)
                continue;
            if(seenAliasNames.count(aliasDef->name))
                continue;
            seenAliasNames.insert(aliasDef->name);
            if(reservedTypeNames.count(aliasDef->name))
            {
                reportError(aliasDef->line, aliasDef->col,
                            "type name '" + aliasDef->name +
                                "' is a reserved keyword");
            }
            auto it = typeDefs.find(aliasDef->name);
            if(it != typeDefs.end())
            {
                reportError(aliasDef->line, aliasDef->col,
                            "type name '" + aliasDef->name +
                                "' conflicts with earlier " + it->second.first +
                                " defined at line " +
                                std::to_string(it->second.second));
            }
            else
            {
                typeDefs[aliasDef->name] = {"type alias", aliasDef->line};
            }
        }
    }
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(!st)
                continue;
            if(reservedTypeNames.count(st->name))
            {
                reportError(st->line, "type name '" + st->name +
                                          "' is a reserved keyword");
            }
            auto it = typeDefs.find(st->name);
            if(it != typeDefs.end())
            {
                reportError(st->line, "type name '" + st->name +
                                          "' conflicts with earlier " +
                                          it->second.first +
                                          " defined at line " +
                                          std::to_string(it->second.second));
            }
            else
            {
                typeDefs[st->name] = {"struct", st->line};
            }

            if(st->members)
            {
                for(auto* nestedEnum : st->members->enums)
                {
                    if(!nestedEnum)
                        continue;
                    if(reservedTypeNames.count(nestedEnum->name))
                    {
                        reportError(nestedEnum->line,
                                    "type name '" + nestedEnum->name +
                                        "' is a reserved keyword");
                    }
                    auto eit = typeDefs.find(nestedEnum->name);
                    if(eit != typeDefs.end())
                    {
                        reportError(nestedEnum->line,
                                    "type name '" + nestedEnum->name +
                                        "' conflicts with earlier " +
                                        eit->second.first +
                                        " defined at line " +
                                        std::to_string(eit->second.second));
                    }
                    else
                    {
                        typeDefs[nestedEnum->name] = {"enum", nestedEnum->line};
                    }
                }
            }
        }
    }

    if(!program->globalVars.empty())
    {
        for(auto* gv : program->globalVars)
        {
            if(!gv)
                continue;
            generateGlobalVarDeclaration(gv);
        }
    }

    if(program->enumList)
    {
        for(auto* en : program->enumList->enums)
        {
            if(!en)
                continue;
            if(reservedTypeNames.count(en->name))
            {
                reportError(en->line, "type name '" + en->name +
                                          "' is a reserved keyword");
            }
            auto it = typeDefs.find(en->name);
            if(it != typeDefs.end())
            {
                reportError(en->line, "type name '" + en->name +
                                          "' conflicts with earlier " +
                                          it->second.first +
                                          " defined at line " +
                                          std::to_string(it->second.second));
            }
            else
            {
                typeDefs[en->name] = {"enum", en->line};
            }
        }
    }

    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(!fn)
                continue;
            auto it = typeDefs.find(fn->name);
            if(it != typeDefs.end())
            {
                reportError(fn->line, "function name '" + fn->name +
                                          "' conflicts with type '" +
                                          it->first + "'");
            }
        }
    }

    if(program->enumList)
    {
        for(auto enumDef : program->enumList->enums)
        {
            generateEnumDefinition(enumDef);
        }
    }
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(!st || !st->members)
                continue;
            for(auto* nestedEnum : st->members->enums)
            {
                generateEnumDefinition(nestedEnum);
            }
        }
    }

    // First, collect generic struct templates and impl blocks
    // These are NOT generated immediately - they're instantiated on demand
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(structDef->isGeneric())
            {
                // Store as template for later instantiation
                genericStructTemplates[structDef->name] = structDef;
            }
        }
    }

    traitDefinitions.clear();
    structImplementedTraits.clear();
    for(auto* traitDef : program->traitDefs)
    {
        if(!traitDef || traitDef->name.empty())
            continue;
        traitDefinitions[traitDef->name] = traitDef;
    }

    auto typeNodesEquivalent = [&](TypeNode* lhs, TypeNode* rhs,
                                   const std::vector<std::string>& typeParams,
                                   const std::string& selfTypeName) -> bool
    {
        std::vector<std::string> substParams = typeParams;
        substParams.push_back("Self");
        std::vector<TypeNode*> substArgs;
        substArgs.reserve(typeParams.size() + 1);
        for(const auto& typeParam : typeParams)
            substArgs.push_back(new StructTypeRefNode(typeParam));
        substArgs.push_back(new StructTypeRefNode(selfTypeName));

        TypeNode* lhsResolved =
            substituteTypeParams(lhs, substParams, substArgs);
        TypeNode* rhsResolved =
            substituteTypeParams(rhs, substParams, substArgs);
        return Helpers::type_name_for_error(lhsResolved) ==
               Helpers::type_name_for_error(rhsResolved);
    };

    auto validateTraitImplBlock = [&](ImplBlockNode* impl)
    {
        if(!impl || impl->traitName.empty())
            return;

        auto traitIt = traitDefinitions.find(impl->traitName);
        if(traitIt == traitDefinitions.end() || !traitIt->second)
        {
            reportError(impl->line, "unknown trait '" + impl->traitName +
                                        "' in impl for '" + impl->structName +
                                        "'");
            return;
        }

        TraitDefNode* traitDef = traitIt->second;
        for(auto* traitMethod : traitDef->methods)
        {
            if(!traitMethod)
                continue;

            StructMethodNode* implMethod = nullptr;
            for(auto* candidate : impl->methods)
            {
                if(candidate && candidate->name == traitMethod->name)
                {
                    implMethod = candidate;
                    break;
                }
            }

            if(!implMethod)
            {
                // Trait method has a default body — synthesize a method on
                // the impl that delegates to it. We deep-copy the parameter
                // list so we can rebind `self: Self` to the concrete type
                // without mutating the trait definition. The body itself is
                // shared (read-only AST during codegen).
                if(traitMethod->body)
                {
                    auto* newParams = traitMethod->parameters
                                          ? new ParameterListNode()
                                          : nullptr;
                    if(newParams && traitMethod->parameters)
                    {
                        for(auto* p : traitMethod->parameters->parameters)
                        {
                            if(!p)
                            {
                                newParams->parameters.push_back(nullptr);
                                continue;
                            }
                            TypeNode* paramType = p->type;
                            if(p->name == "self")
                            {
                                if(auto* selfRef =
                                       dynamic_cast<StructTypeRefNode*>(
                                           p->type))
                                {
                                    if(selfRef->structName == "Self")
                                        paramType = new StructTypeRefNode(
                                            impl->structName);
                                }
                            }
                            auto* cloned =
                                new ParameterNode(paramType, p->name);
                            cloned->line = p->line;
                            newParams->parameters.push_back(cloned);
                        }
                    }
                    auto* defaulted = new StructMethodNode(
                        traitMethod->returnType, traitMethod->name, newParams,
                        traitMethod->body, traitMethod->isPublic,
                        traitMethod->isStatic);
                    // The defaulted method should resolve under the trait's
                    // module (same module-context rules as a normal trait
                    // method) so visibility behaves predictably.
                    defaulted->sourceModule = traitDef->sourceModule;
                    defaulted->line = traitMethod->line;
                    impl->methods.push_back(defaulted);
                    continue;
                }

                reportError(impl->line,
                            "trait '" + impl->traitName + "' for struct '" +
                                impl->structName + "' requires method '" +
                                traitMethod->name + "'");
                continue;
            }

            if(implMethod->isStatic != traitMethod->isStatic)
            {
                reportError(
                    implMethod->line,
                    "method '" + impl->structName + "::" + implMethod->name +
                        "' does not match trait '" + impl->traitName +
                        "': expected " +
                        std::string(traitMethod->isStatic ? "static"
                                                          : "instance") +
                        " method");
                continue;
            }

            size_t traitParamCount =
                traitMethod->parameters
                    ? traitMethod->parameters->parameters.size()
                    : 0;
            size_t implParamCount =
                implMethod->parameters
                    ? implMethod->parameters->parameters.size()
                    : 0;
            if(traitParamCount != implParamCount)
            {
                reportError(
                    implMethod->line,
                    "method '" + impl->structName + "::" + implMethod->name +
                        "' does not match trait '" + impl->traitName +
                        "': expected " + std::to_string(traitParamCount) +
                        " parameter(s), got " + std::to_string(implParamCount));
                continue;
            }

            bool mismatch = false;
            for(size_t i = 0; i < traitParamCount; ++i)
            {
                auto* expectedParam = traitMethod->parameters->parameters[i];
                auto* actualParam = implMethod->parameters->parameters[i];
                if(!expectedParam || !actualParam)
                    continue;
                if(expectedParam->name != actualParam->name)
                {
                    reportError(implMethod->line,
                                "method '" + impl->structName +
                                    "::" + implMethod->name +
                                    "' does not match trait '" +
                                    impl->traitName + "': parameter " +
                                    std::to_string(i + 1) + " must be named '" +
                                    expectedParam->name + "'");
                    mismatch = true;
                    break;
                }
                if(!typeNodesEquivalent(expectedParam->type, actualParam->type,
                                        impl->typeParams, impl->structName))
                {
                    reportError(implMethod->line,
                                "method '" + impl->structName +
                                    "::" + implMethod->name +
                                    "' does not match trait '" +
                                    impl->traitName + "': parameter '" +
                                    actualParam->name + "' has type '" +
                                    Helpers::type_name_for_error(actualParam->type) +
                                    "', expected '" +
                                    Helpers::type_name_for_error(expectedParam->type) +
                                    "'");
                    mismatch = true;
                    break;
                }
            }
            if(mismatch)
                continue;

            if(!typeNodesEquivalent(traitMethod->returnType,
                                    implMethod->returnType, impl->typeParams,
                                    impl->structName))
            {
                reportError(
                    implMethod->line,
                    "method '" + impl->structName + "::" + implMethod->name +
                        "' does not match trait '" + impl->traitName +
                        "': return type '" +
                        Helpers::type_name_for_error(implMethod->returnType) +
                        "' does not match expected '" +
                        Helpers::type_name_for_error(traitMethod->returnType) + "'");
            }
        }

        // Super-trait check: `trait Foo: Bar` requires every implementer of
        // Foo to also implement Bar. Generic impls (`impl<T> Foo for X`) are
        // skipped here because their concrete type isn't fixed yet.
        if(!traitDef->superTraits.empty() && impl->typeParams.empty())
        {
            auto& concreteImpls = structImplementedTraits[impl->structName];
            for(const auto& superTrait : traitDef->superTraits)
            {
                if(superTrait.empty())
                    continue;
                if(concreteImpls.find(superTrait) == concreteImpls.end())
                {
                    reportError(impl->line, "trait '" + impl->traitName +
                                                "' for struct '" +
                                                impl->structName +
                                                "' requires struct to also "
                                                "implement super-trait '" +
                                                superTrait + "'");
                }
            }
        }
    };

    // Repopulate structImplementedTraits after the cleanup above, so the
    // super-trait validation can see all concrete impls in the program.
    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl || impl->traitName.empty())
                continue;
            if(!impl->typeParams.empty())
                continue;
            structImplementedTraits[impl->structName].insert(impl->traitName);
        }
    }

    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
            validateTraitImplBlock(impl);
    }

    // Collect generic impl blocks
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(!impl->typeParams.empty())
            {
                // This is a generic impl block
                genericImplBlocks[impl->structName].push_back(impl);
            }
        }
    }

    // Generate all NON-GENERIC struct definitions
    // We need to process base structs before derived structs
    if(program->structList)
    {
        // Build a map of struct names to their definitions
        std::map<std::string, StructDefNode*> structMap;
        for(auto structDef : program->structList->structs)
        {
            structMap[structDef->name] = structDef;
        }

        // Process structs in dependency order (bases before derived)
        std::set<std::string> processed;
        std::function<void(StructDefNode*)> processStruct =
            [&](StructDefNode* structDef)
        {
            if(processed.count(structDef->name))
                return;

            // Skip generic structs - they're instantiated on demand
            if(structDef->isGeneric())
            {
                processed.insert(structDef->name);
                return;
            }

            // Process base first if it exists
            if(!structDef->baseName.empty())
            {
                auto baseIt = structMap.find(structDef->baseName);
                if(baseIt != structMap.end())
                {
                    processStruct(baseIt->second);
                }
            }

            std::function<void(TypeNode*)> processMemberType =
                [&](TypeNode* type)
            {
                if(!type)
                    return;
                if(auto* refType = dynamic_cast<ReferenceTypeNode*>(type))
                {
                    processMemberType(refType->elementType);
                    return;
                }
                if(auto* ptrType = dynamic_cast<PointerTypeNode*>(type))
                {
                    processMemberType(ptrType->elementType);
                    return;
                }
                if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
                {
                    if(tupleType->elementTypes)
                    {
                        for(auto* elem : tupleType->elementTypes->types)
                            processMemberType(elem);
                    }
                    return;
                }
                if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
                {
                    processMemberType(listType->elementType);
                    return;
                }
                if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
                {
                    processMemberType(mapType->keyType);
                    processMemberType(mapType->valueType);
                    return;
                }
                if(auto* genStructRef =
                       dynamic_cast<GenericStructTypeRefNode*>(type))
                {
                    for(auto* arg : genStructRef->typeArgs)
                        processMemberType(arg);
                    return;
                }
                if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
                {
                    auto depIt = structMap.find(structRef->structName);
                    if(depIt != structMap.end() && depIt->second != structDef)
                        processStruct(depIt->second);
                }
            };

            if(structDef->members)
            {
                for(auto* member : structDef->members->members)
                {
                    if(member)
                        processMemberType(member->type);
                }
            }

            // Track struct visibility
            structVisibility[structDef->name] =
                std::make_pair(structDef->isPublic, structDef->sourceModule);
            generateStructDefinition(structDef);
            processed.insert(structDef->name);
        };

        for(auto structDef : program->structList->structs)
        {
            processStruct(structDef);
        }
    }

    // Generate forward declarations for all functions first
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(funcDef->isTest && !includeTests)
                continue;
            if(!funcDef->typeParams.empty() && funcDef->isCexpr)
            {
                registerFunctionOverload(funcDef, nullptr);
                continue;
            }
            llvm::Function* decl = generateFunctionDeclaration(funcDef);
            registerFunctionOverload(funcDef, decl);
        }
    }

    // Generate NON-GENERIC struct method declarations and track visibility
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric())
            {
                generateStructMethods(structDef);
            }
        }
    }

    // Process non-generic impl blocks (add methods to existing structs)
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                if(!impl->traitName.empty())
                {
                    structImplementedTraits[impl->structName].insert(
                        impl->traitName);
                }
                // Non-generic impl block - process immediately
                for(auto method : impl->methods)
                {
                    if(method && method->sourceModule.empty())
                        method->sourceModule = currentModule;
                    // Register the method with the struct
                    structMethods[impl->structName][method->name] =
                        std::make_pair(method->isPublic, method);

                    // Generate method declaration
                    generateMethodDeclaration(impl->structName, method);
                }
            }
        }
    }

    // Then generate all function bodies
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(funcDef->isTest && !includeTests)
                continue;
            if(!funcDef->typeParams.empty() && funcDef->isCexpr)
                continue;
            generateFunctionDefinition(funcDef);
        }
    }

    // Emit definitions for module functions that were loaded via `mod` and
    // referenced through fully-qualified calls (e.g. std::x::foo()) even when
    // they were not pulled in by `use`.
    if(!deferredModuleFunctionDefs.empty())
    {
        for(auto* fn : deferredModuleFunctionDefs)
        {
            if(!fn)
                continue;
            if(fn->isTest && !includeTests)
                continue;
            generateFunctionDefinition(fn);
        }
    }

    // Collect fixture-test methods from #[fixture] impl blocks. Each #[test]
    // method inside a #[fixture] impl gets a fresh stack-allocated, zero-
    // initialized instance via its &mut Self parameter.
    std::vector<FixtureTestEntry> fixtureTests;
    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl || !impl->isFixture)
                continue;
            if(!impl->traitName.empty())
            {
                reportError(impl->line,
                            "#[fixture] is only valid on inherent impl blocks");
                continue;
            }
            for(auto* method : impl->methods)
            {
                if(!method || !method->isTest)
                    continue;
                if(method->isStatic)
                {
                    reportError(method->line,
                                "fixture #[test] methods must take 'self: "
                                "&mut Self' (got static method)");
                    continue;
                }
                bool selfOnly = true;
                for(auto* p : method->parameters->parameters)
                {
                    if(p && p->name != "self")
                    {
                        selfOnly = false;
                        break;
                    }
                }
                if(!selfOnly)
                {
                    reportError(method->line,
                                "fixture #[test] methods must take only "
                                "'self: &mut Self' as parameter");
                    continue;
                }
                if(method->returnType &&
                   !(method->returnType->kind == TypeNode::TYPE_VOID ||
                     method->returnType->kind == TypeNode::TYPE_INT ||
                     method->returnType->kind == TypeNode::TYPE_I32))
                {
                    reportError(method->line,
                                "fixture #[test] methods must return void or "
                                "i32");
                    continue;
                }
                fixtureTests.push_back({impl, method});
            }
        }
    }

    if(testMode && (!testFunctions.empty() || !fixtureTests.empty()))
    {
        for(auto* testFn : testFunctions)
        {
            if(!testFn || !testFn->parameters)
                continue;
            if(!testFn->parameters->parameters.empty())
            {
                reportError(testFn->line,
                            "test functions must have no parameters");
                continue;
            }
            if(testFn->returnType &&
               !(testFn->returnType->kind == TypeNode::TYPE_VOID ||
                 testFn->returnType->kind == TypeNode::TYPE_INT ||
                 testFn->returnType->kind == TypeNode::TYPE_I32))
            {
                reportError(testFn->line,
                            "test functions must return void or i32");
                continue;
            }
        }
        if(benchmarkMode)
            generateBenchmarkMain(testFunctions);
        else
            generateTestMain(testFunctions, fixtureTests);
    }

    // Generate a C-compatible main wrapper if needed.
    if(!testMode && mainDef && mainArgMode != MainArgMode::None &&
       mainArgsListType)
    {
        llvm::Type* i32Type = llvm::Type::getInt32Ty(context);
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
        llvm::Type* argvPtrType = ptrType;
#else
        llvm::Type* i8Type = llvm::Type::getInt8Ty(context);
        llvm::Type* i8PtrType = llvm::PointerType::get(i8Type, 0);
        llvm::Type* argvPtrType = llvm::PointerType::get(i8PtrType, 0);
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMTypeFromNode(mainArgsListType->elementType), 0);
#endif

        std::vector<llvm::Type*> wrapperParams = {i32Type, argvPtrType};
        llvm::FunctionType* wrapperType =
            llvm::FunctionType::get(i32Type, wrapperParams, false);
        llvm::Function* wrapper = llvm::Function::Create(
            wrapperType, llvm::Function::ExternalLinkage, "main", module.get());

        auto argIt = wrapper->arg_begin();
        llvm::Value* argcArg = &*argIt++;
        llvm::Value* argvArg = &*argIt++;
        argcArg->setName("argc");
        argvArg->setName("argv");

        llvm::BasicBlock* entry =
            llvm::BasicBlock::Create(context, "entry", wrapper);
        builder.SetInsertPoint(entry);

        llvm::Value* argc64 = builder.CreateSExt(argcArg, i64Type, "argc64");

        // Build list struct { size: i64, data: ptr }
        llvm::Type* listStructType = getLLVMTypeFromNode(mainArgsListType);
        llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
        listStruct =
            builder.CreateInsertValue(listStruct, argc64, 0, "args.size");

#if LLVM_VERSION_MAJOR >= 15
        llvm::Value* dataPtr = argvArg;
#else
        llvm::Value* dataPtr = argvArg;
        if(argvArg->getType() != ptrType)
            dataPtr = builder.CreateBitCast(argvArg, ptrType, "args.data");
#endif

        listStruct =
            builder.CreateInsertValue(listStruct, dataPtr, 1, "args.data");

        llvm::Function* userMain =
            module->getFunction(functionSymbolName(mainDef));
        if(!userMain)
            userMain = module->getFunction("__mlang_user_main");
        if(!userMain)
        {
            reportError(mainDef->line,
                        "failed to generate main wrapper: missing user main");
            builder.CreateRet(llvm::ConstantInt::get(i32Type, 1));
        }
        else
        {
            std::vector<llvm::Value*> callArgs;
            if(mainArgMode == MainArgMode::ArgsListWithCount)
            {
                llvm::Value* argcValue = argcArg;
                if(mainArgcKind == TypeNode::TYPE_I64)
                    argcValue = argc64;
                callArgs.push_back(argcValue);
            }
            callArgs.push_back(listStruct);
            llvm::Value* rc = builder.CreateCall(userMain, callArgs, "mainrc");
            builder.CreateRet(rc);
        }
    }

    // Generate NON-GENERIC struct method bodies
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric() && structDef->members)
            {
                for(auto method : structDef->members->methods)
                {
                    generateMethodDefinition(structDef->name, method);
                }
            }
        }
    }

    // Generate non-generic impl block method bodies
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                for(auto method : impl->methods)
                {
                    generateMethodDefinition(impl->structName, method);
                }
            }
        }
    }
}
llvm::Function*
CodeGenerator::generateFunctionDeclaration(FunctionDefNode* node)
{
    std::string symbolName = functionSymbolName(node);
    std::vector<llvm::Type*> paramTypes;
    for(auto param : node->parameters->parameters)
    {
        llvm::Type* paramType = getLLVMTypeFromNode(param->type);
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
    auto savedCurrentFunctionExceptionFrame = currentFunctionExceptionFrame;
    auto savedSemanticReturnType = currentSemanticReturnType;
    int savedUnsafeDepth = unsafeDepth;
    currentSemanticReturnType = node->returnType;

    // Create a new basic block for the function
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear the named values map and constant tracking for new function scope
    namedValues.clear();
    constantVariables.clear();
    movedVariables.clear();
    closureVariables.clear();
    activeInlineClosures.clear();
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
    for(auto& arg : function->args())
    {
        // Allocate space for parameters so they can be modified
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        llvm::Value* paramValue = &arg;
        if(arg.getType()->isStructTy())
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

    // Run scope-exit destructors for locals at normal function fallthrough.
    exitCleanupScope();

    // If the function is void and doesn't have a return, add one
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!currentBlock->getTerminator())
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
    currentFunctionExceptionFrame = savedCurrentFunctionExceptionFrame;
    currentSemanticReturnType = savedSemanticReturnType;
    unsafeDepth = savedUnsafeDepth;
    currentModule = savedModule;
    constexprValues = std::move(savedConstexprValues);
    builder.restoreIP(savedIP);

    // Verify the function
    llvm::verifyFunction(*function);
    return function;
}

void CodeGenerator::generateStatement(StatementNode* node)
{
    if(auto returnNode = dynamic_cast<ReturnNode*>(node))
    {
        generateReturnStatement(returnNode);
    }
    else if(auto cexprNode = dynamic_cast<CexprDeclNode*>(node))
    {
        generateCexprDeclaration(cexprNode);
    }
    else if(auto letNode = dynamic_cast<LetDeclNode*>(node))
    {
        generateLetDeclaration(letNode);
    }
    else if(auto varNode = dynamic_cast<VarDeclNode*>(node))
    {
        generateVarDeclaration(varNode);
    }
    else if(auto assignNode = dynamic_cast<AssignmentNode*>(node))
    {
        generateAssignment(assignNode);
    }
    else if(auto fieldAssignNode = dynamic_cast<FieldAssignmentNode*>(node))
    {
        generateFieldAssignment(fieldAssignNode);
    }
    else if(auto derefAssignNode = dynamic_cast<DerefAssignmentNode*>(node))
    {
        generateDerefAssignment(derefAssignNode);
    }
    else if(auto ifNode = dynamic_cast<IfNode*>(node))
    {
        generateIfStatement(ifNode);
    }
    else if(auto cexprIfNode = dynamic_cast<CexprIfNode*>(node))
    {
        generateCexprIfStatement(cexprIfNode);
    }
    else if(auto forNode = dynamic_cast<ForNode*>(node))
    {
        generateForStatement(forNode);
    }
    else if(auto whileNode = dynamic_cast<WhileNode*>(node))
    {
        generateWhileStatement(whileNode);
    }
    else if(auto tryCatchNode = dynamic_cast<TryCatchNode*>(node))
    {
        generateTryCatchStatement(tryCatchNode);
    }
    else if(auto blockNode = dynamic_cast<BlockStatementNode*>(node))
    {
        auto savedConstexprValues = constexprValues;
        enterCleanupScope();
        if(blockNode->isUnsafe)
            unsafeDepth++;
        const auto& blkStmts = blockNode->statements->statements;
        if(blkStmts.empty())
        {
            reportWarning(blockNode->line, blockNode->col, "empty block");
        }
        for(size_t si = 0; si < blkStmts.size(); si++)
        {
            generateStatement(blkStmts[si]);
            // NLL: expire borrow variables not referenced in remaining stmts
            if(!pointerBorrowTarget.empty())
            {
                std::set<std::string> futureIdents;
                for(size_t sj = si + 1; sj < blkStmts.size(); sj++)
                    collect_used_idents(blkStmts[sj], futureIdents);
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
        if(blockNode->isUnsafe)
            unsafeDepth--;
        exitCleanupScope();
        constexprValues = std::move(savedConstexprValues);
    }
    else if(auto printNode = dynamic_cast<PrintNode*>(node))
    {
        generatePrintStatement(printNode);
    }
    else if(auto assertNode = dynamic_cast<AssertNode*>(node))
    {
        generateAssert(assertNode);
    }
    else if(auto assertEqNode = dynamic_cast<AssertEqNode*>(node))
    {
        generateAssertEq(assertEqNode);
    }
    else if(auto staticAssertNode = dynamic_cast<StaticAssertNode*>(node))
    {
        generateStaticAssert(staticAssertNode);
    }
    else if(auto breakNode = dynamic_cast<BreakNode*>(node))
    {
        generateBreakStatement(breakNode);
    }
    else if(auto throwNode = dynamic_cast<ThrowNode*>(node))
    {
        generateThrowStatement(throwNode);
    }
    else if(auto continueNode = dynamic_cast<ContinueNode*>(node))
    {
        generateContinueStatement(continueNode);
    }
    else if(auto exprStmt = dynamic_cast<ExpressionStatementNode*>(node))
    {
        generateExpression(exprStmt->expression);
    }
}

llvm::Value* CodeGenerator::generateExpression(ExpressionNode* node)
{
    if(auto intLit = dynamic_cast<IntLiteralNode*>(node))
    {
        return generateIntLiteral(intLit);
    }
    else if(auto boolLit = dynamic_cast<BoolLiteralNode*>(node))
    {
        return generateBoolLiteral(boolLit);
    }
    else if(auto floatLit = dynamic_cast<FloatLiteralNode*>(node))
    {
        return generateFloatLiteral(floatLit);
    }
    else if(auto doubleLit = dynamic_cast<DoubleLiteralNode*>(node))
    {
        return generateDoubleLiteral(doubleLit);
    }
    else if(auto stringLit = dynamic_cast<StringLiteralNode*>(node))
    {
        return generateStringLiteral(stringLit);
    }
    else if(auto formatExpr = dynamic_cast<FormatNode*>(node))
    {
        return generateFormatExpression(formatExpr);
    }
    else if(auto enumLit = dynamic_cast<EnumLiteralNode*>(node))
    {
        return generateEnumLiteral(enumLit);
    }
    else if(auto binOp = dynamic_cast<BinaryOpNode*>(node))
    {
        return generateBinaryOp(binOp);
    }
    else if(auto* foldOp = dynamic_cast<FoldExpressionNode*>(node))
    {
        return generateFoldExpression(foldOp);
    }
    else if(auto unaryOp = dynamic_cast<UnaryOpNode*>(node))
    {
        return generateUnaryOp(unaryOp);
    }
    else if(auto* updateOp = dynamic_cast<UpdateExpressionNode*>(node))
    {
        return generateUpdateExpression(updateOp);
    }
    else if(auto ternary = dynamic_cast<TernaryNode*>(node))
    {
        return generateTernaryExpression(ternary);
    }
    else if(auto tryExpr = dynamic_cast<TryExpressionNode*>(node))
    {
        return generateTryExpression(tryExpr);
    }
    else if(auto* sizeofExpr = dynamic_cast<SizeofExpressionNode*>(node))
    {
        return generateSizeofExpression(sizeofExpr);
    }
    else if(auto* cexprExpr = dynamic_cast<CexprExpressionNode*>(node))
    {
        return generateCexprExpression(cexprExpr);
    }
    else if(auto* inlineAsm = dynamic_cast<InlineAsmNode*>(node))
    {
        return generateInlineAsm(inlineAsm);
    }
    else if(auto id = dynamic_cast<IdentifierNode*>(node))
    {
        return generateIdentifier(id);
    }
    else if(auto fieldAcc = dynamic_cast<FieldAccessNode*>(node))
    {
        return generateFieldAccess(fieldAcc);
    }
    else if(auto call = dynamic_cast<FunctionCallNode*>(node))
    {
        return generateFunctionCall(call);
    }
    else if(auto methodCall = dynamic_cast<MethodCallNode*>(node))
    {
        return generateMethodCall(methodCall);
    }
    else if(auto cast = dynamic_cast<CastExpressionNode*>(node))
    {
        return generateCastExpression(cast);
    }
    else if(auto listLit = dynamic_cast<ListLiteralNode*>(node))
    {
        return generateListLiteral(listLit);
    }
    else if(auto mapLit = dynamic_cast<MapLiteralNode*>(node))
    {
        return generateMapLiteral(mapLit);
    }
    else if(auto indexExpr = dynamic_cast<IndexExpressionNode*>(node))
    {
        return generateIndexExpression(indexExpr);
    }
    else if(auto tupleLit = dynamic_cast<TupleLiteralNode*>(node))
    {
        return generateTupleLiteral(tupleLit);
    }
    else if(auto tupleAcc = dynamic_cast<TupleAccessNode*>(node))
    {
        return generateTupleAccess(tupleAcc);
    }
    else if(auto structLit = dynamic_cast<StructLiteralNode*>(node))
    {
        return generateStructLiteral(structLit);
    }
    else if(auto matchExpr = dynamic_cast<MatchExpressionNode*>(node))
    {
        return generateMatchExpression(matchExpr);
    }
    else if(auto* closure = dynamic_cast<ClosureNode*>(node))
    {
        llvm::Function* fn = generateClosureFn(closure);
        if(!fn)
            return nullptr;
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        return builder.CreateBitCast(fn, ptrType, "closure.ptr");
    }
    else if(auto* arrFill = dynamic_cast<ArrayFillNode*>(node))
    {
        return generateArrayFill(arrFill);
    }
    return nullptr;
}

void CodeGenerator::generateStructDefinition(StructDefNode* node)
{
    std::vector<llvm::Type*> memberTypes;
    std::vector<std::pair<std::string, TypeNode*>> members;
    std::vector<StructFieldLayout> layouts;
    std::vector<StructFieldAccessInfo> accessInfo;

    // If this struct has a base, include base struct's fields first
    if(!node->baseName.empty())
    {
        auto baseMemIt = structMembers.find(node->baseName);
        if(baseMemIt != structMembers.end())
        {
            llvm::StructType* baseStructType = getStructType(node->baseName);
            auto baseLayoutIt = structFieldLayouts.find(node->baseName);
            for(const auto& baseMember : baseMemIt->second)
                members.push_back(baseMember);
            auto baseAccessIt = structFieldAccessInfo.find(node->baseName);
            if(baseAccessIt != structFieldAccessInfo.end())
                accessInfo.insert(accessInfo.end(),
                                  baseAccessIt->second.begin(),
                                  baseAccessIt->second.end());
            if(baseLayoutIt != structFieldLayouts.end())
                layouts.insert(layouts.end(), baseLayoutIt->second.begin(),
                               baseLayoutIt->second.end());
            if(baseStructType)
            {
                for(unsigned i = 0; i < baseStructType->getNumElements(); ++i)
                    memberTypes.push_back(baseStructType->getElementType(i));
            }
        }
        else
        {
            reportError(node->line,
                        "base struct '" + node->baseName + "' not found");
        }
    }

    // Add this struct's own members
    bool packingBitRun = false;
    unsigned packedStorageIndex = 0;
    unsigned packedBitOffset = 0;
    for(auto member : node->members->members)
    {
        members.push_back({member->name, member->type});
        StructFieldAccessInfo fieldAccess;
        fieldAccess.ownerStructName = node->name;
        fieldAccess.isProperty = member->isProperty;
        fieldAccess.isAtomicProperty = member->isAtomicProperty;
        fieldAccess.isMutexProperty = member->isMutexProperty;
        fieldAccess.isRecursiveProperty = member->isRecursiveProperty;
        fieldAccess.isSynthesizedPropertyStorage =
            member->isSynthesizedPropertyStorage;
        if(member->isHiddenProperty || member->isSynthesizedPropertyStorage)
            fieldAccess.encapsulation = FieldEncapsulation::Hidden;
        else if(member->isProtectedProperty)
            fieldAccess.encapsulation = FieldEncapsulation::Protected;
        accessInfo.push_back(fieldAccess);
        StructFieldLayout layout;
        if(Helpers::isBitFieldTypeNode(member->type))
        {
            if(!packingBitRun || packedBitOffset >= 8)
            {
                packedStorageIndex = static_cast<unsigned>(memberTypes.size());
                memberTypes.push_back(llvm::Type::getInt8Ty(context));
                packedBitOffset = 0;
                packingBitRun = true;
            }
            layout.storageIndex = packedStorageIndex;
            layout.packedBit = true;
            layout.bitOffset = packedBitOffset++;
        }
        else
        {
            packingBitRun = false;
            packedBitOffset = 0;
            layout.storageIndex = static_cast<unsigned>(memberTypes.size());
            memberTypes.push_back(getLLVMTypeFromNode(member->type));
        }
        layouts.push_back(layout);
    }

    llvm::StructType* structType =
        llvm::StructType::create(context, memberTypes, node->name);
    structTypes[node->name] = structType;
    structMembers[node->name] = members;
    structFieldLayouts[node->name] = layouts;
    structFieldAccessInfo[node->name] = accessInfo;
    if(!node->debugDisplayName.empty())
        structDebugDisplayNames[node->name] = node->debugDisplayName;

    // Track per-field default initializers so that struct-literal
    // construction can fill in unspecified fields from declaration-site
    // defaults (e.g. `let x: i32{7};` or `let p: Point{x: 3};`).
    {
        auto& defaults = structMemberDefaults[node->name];
        for(auto* member : node->members->members)
        {
            if(member && member->initExpr)
                defaults[member->name] = member->initExpr;
        }
    }
    if(node->deriveDebug)
        debugStructs.insert(node->name);
    if(node->deriveJson)
        jsonStructs.insert(node->name);

    // Track base name for inheritance lookups
    if(!node->baseName.empty())
    {
        structBases[node->name] = node->baseName;
    }
}
llvm::StructType* CodeGenerator::getStructType(const std::string& name)
{
    auto it = structTypes.find(name);
    if(it != structTypes.end())
    {
        return llvm::cast<llvm::StructType>(it->second);
    }
    return nullptr;
}

llvm::Type* CodeGenerator::getTraitObjectType(const std::string& traitName)
{
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
    llvm::Type* opaquePtr =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    auto it = traitObjectTypes.find(traitName);
    if(it != traitObjectTypes.end())
        return it->second;
    auto* ty = llvm::StructType::create(context, {opaquePtr, opaquePtr},
                                        "trait.obj." + traitName);
    traitObjectTypes[traitName] = ty;
    return ty;
}

llvm::Type* CodeGenerator::getTraitVTableType(const std::string& traitName)
{
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
    llvm::Type* opaquePtr =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    auto it = traitVTableTypes.find(traitName);
    if(it != traitVTableTypes.end())
        return it->second;

    auto traitIt = traitDefinitions.find(traitName);
    if(traitIt == traitDefinitions.end())
    {
        for(auto it = traitDefinitions.begin(); it != traitDefinitions.end();
            ++it)
        {
            if(Helpers::trait_names_equivalent(it->first, traitName))
            {
                traitIt = it;
                break;
            }
        }
    }
    if(traitIt == traitDefinitions.end() || !traitIt->second)
        return nullptr;

    std::vector<llvm::Type*> fields;
    fields.reserve(traitIt->second->methods.size());
    for(size_t i = 0; i < traitIt->second->methods.size(); ++i)
        fields.push_back(opaquePtr);

    auto* ty =
        llvm::StructType::create(context, fields, "trait.vtable." + traitName);
    traitVTableTypes[traitName] = ty;
    return ty;
}

llvm::GlobalVariable*
CodeGenerator::ensureTraitVTable(const std::string& concreteTypeName,
                                 const std::string& traitName)
{
    std::string key = concreteTypeName + "::" + traitName;
    auto it = traitVTableGlobals.find(key);
    if(it != traitVTableGlobals.end())
        return it->second;

    auto traitIt = traitDefinitions.find(traitName);
    if(traitIt == traitDefinitions.end())
    {
        for(auto it = traitDefinitions.begin(); it != traitDefinitions.end();
            ++it)
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
        reportError(0, "unknown trait object type: '" + traitName + "'");
        return nullptr;
    }
    TraitDefNode* traitDef = traitIt->second;
    llvm::Type* vtableType = getTraitVTableType(traitName);
    if(!vtableType)
        return nullptr;

    std::vector<llvm::Constant*> entries;
    entries.reserve(traitDef->methods.size());
    for(auto* traitMethod : traitDef->methods)
    {
        if(!traitMethod)
            continue;
        auto methodsIt = structMethods.find(concreteTypeName);
        if(methodsIt == structMethods.end())
        {
            reportError(traitMethod->line, "unknown struct '" +
                                               concreteTypeName +
                                               "' for trait object dispatch");
            return nullptr;
        }
        auto mit = methodsIt->second.find(traitMethod->name);
        if(mit == methodsIt->second.end() || !mit->second.second)
        {
            reportError(traitMethod->line,
                        "missing method '" + traitMethod->name +
                            "' for trait object dispatch on '" +
                            concreteTypeName + "'");
            return nullptr;
        }
        StructMethodNode* concreteMethod = mit->second.second;
        llvm::Function* function =
            generateMethodDeclaration(concreteTypeName, concreteMethod);
        if(function && function->empty())
            function =
                generateMethodDefinition(concreteTypeName, concreteMethod);
        if(!function)
            return nullptr;

#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
        llvm::Type* opaquePtr =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        entries.push_back(llvm::ConstantExpr::getBitCast(function, opaquePtr));
    }

    auto* init = llvm::ConstantStruct::get(
        llvm::cast<llvm::StructType>(vtableType), entries);
    auto* gv = new llvm::GlobalVariable(
        *module, llvm::cast<llvm::StructType>(vtableType), true,
        llvm::GlobalValue::PrivateLinkage, init, "__mlang_trait_vtable." + key);
    traitVTableGlobals[key] = gv;
    return gv;
}

llvm::Value* CodeGenerator::buildTraitObjectValue(ExpressionNode* expr,
                                                  const std::string& traitName,
                                                  int line, bool heapCopy)
{
    llvm::Value* dataPtr = getLValuePointer(expr, line);
    if(!dataPtr)
        return nullptr;

    TypeNode* lvalueType = getLValueType(expr, line);
    if(!lvalueType)
        return nullptr;

    std::string concreteTypeName;
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(lvalueType))
    {
        concreteTypeName = resolveVisibleStructName(structRef->structName);
    }
    else if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(lvalueType))
    {
        concreteTypeName = getOrCreateMonomorphizedStruct(genRef->structName,
                                                          genRef->typeArgs);
    }
    else
    {
        reportError(line, "trait object arguments require a struct value");
        return nullptr;
    }

    if(concreteTypeName.empty())
    {
        reportError(line, "unknown concrete type for trait object dispatch");
        return nullptr;
    }
    auto implIt = structImplementedTraits.find(concreteTypeName);
    std::string implementedTraitName;
    if(implIt != structImplementedTraits.end())
    {
        for(const auto& candidate : implIt->second)
        {
            if(Helpers::trait_names_equivalent(candidate, traitName))
            {
                implementedTraitName = candidate;
                break;
            }
        }
    }
    if(implementedTraitName.empty())
    {
        reportError(line, "type '" + concreteTypeName +
                              "' does not implement trait '" + traitName + "'");
        return nullptr;
    }

    llvm::GlobalVariable* vtable =
        ensureTraitVTable(concreteTypeName, implementedTraitName);
    if(!vtable)
        return nullptr;

    if(heapCopy)
    {
        llvm::Type* concreteType = getLLVMTypeFromNode(lvalueType);
        if(!concreteType)
            return nullptr;

        const llvm::DataLayout& dl = module->getDataLayout();
        auto sizeBytes = dl.getTypeAllocSize(concreteType).getFixedValue();
        llvm::Value* sizeVal =
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), sizeBytes);
        llvm::Value* heapPtr =
            builder.CreateCall(mallocFunc, {sizeVal}, "trait.obj.heap");
        llvm::Value* concreteValue =
            builder.CreateLoad(concreteType, dataPtr, "trait.obj.copy");
        builder.CreateStore(concreteValue, heapPtr);
        dataPtr = heapPtr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
    llvm::Type* opaquePtr =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Value* dataPtrI8 =
        builder.CreateBitCast(dataPtr, opaquePtr, "trait.obj.data");
    llvm::Value* vtablePtr =
        builder.CreateBitCast(vtable, opaquePtr, "trait.obj.vtable");

    llvm::StructType* objType =
        llvm::cast<llvm::StructType>(getTraitObjectType(traitName));
    llvm::Value* obj = llvm::Constant::getNullValue(objType);
    obj = builder.CreateInsertValue(obj, dataPtrI8, 0, "trait.obj.data");
    obj = builder.CreateInsertValue(obj, vtablePtr, 1, "trait.obj.vtable");
    return obj;
}

llvm::Value* CodeGenerator::coerceTraitObjectValue(llvm::Value* value,
                                                   llvm::Type* expectedType,
                                                   int line)
{
    if(!value || !expectedType)
        return nullptr;
    if(value->getType() == expectedType)
        return value;

    auto* actualStruct = llvm::dyn_cast<llvm::StructType>(value->getType());
    auto* expectedStruct = llvm::dyn_cast<llvm::StructType>(expectedType);
    if(!actualStruct || !expectedStruct ||
       actualStruct->getNumElements() != 2 ||
       expectedStruct->getNumElements() != 2)
    {
        reportError(line, "trait object type mismatch");
        return nullptr;
    }

    llvm::Value* dataPtr =
        builder.CreateExtractValue(value, 0, "trait.obj.data");
    llvm::Value* vtablePtr =
        builder.CreateExtractValue(value, 1, "trait.obj.vtable");
    llvm::Value* coerced = llvm::Constant::getNullValue(expectedType);
    coerced = builder.CreateInsertValue(coerced, dataPtr, 0,
                                        "trait.obj.coerced.data");
    coerced = builder.CreateInsertValue(coerced, vtablePtr, 1,
                                        "trait.obj.coerced.vtable");
    return coerced;
}

void CodeGenerator::generateLetDeclaration(LetDeclNode* node)
{
    recordScopedPointerVariable(node->name);
    enumVariableTypes.erase(node->name);

    if(!validateFixedArrayInitializer(node->type, node->expression,
                                      node->line))
        return;

    // Inline closure: let inc = || { ... }
    if(auto* closureInit = dynamic_cast<ClosureNode*>(node->expression))
    {
        closureVariables[node->name] = closureInit;
        recordVariableScopeDepth(node->name);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::AllocaInst* alloca =
            builder.CreateAlloca(ptrType, nullptr, node->name + ".closure");
        builder.CreateStore(llvm::ConstantPointerNull::get(
                                llvm::cast<llvm::PointerType>(ptrType)),
                            alloca);
        namedValues[node->name] = alloca;
        return;
    }

    if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(node->type))
    {
        llvm::Type* traitObjType = getLLVMTypeFromNode(traitObj);
        if(!traitObjType)
        {
            reportError(node->line,
                        "unknown trait object type: " + traitObj->traitName);
            return;
        }

        llvm::AllocaInst* alloca = createEntryBlockAlloca(
            builder.GetInsertBlock()->getParent(), traitObjType, node->name);

        llvm::Value* storedValue = nullptr;
        if(node->expression)
        {
            TypeNode* exprType = getLValueType(node->expression, node->line);
            if(dynamic_cast<TraitObjectTypeNode*>(exprType))
            {
                storedValue = generateExpression(node->expression);
                storedValue = coerceTraitObjectValue(storedValue, traitObjType,
                                                     node->line);
            }
            else if(exprType)
                storedValue = buildTraitObjectValue(
                    node->expression, traitObj->traitName, node->line);
            else
            {
                storedValue = generateExpression(node->expression);
                storedValue = coerceTraitObjectValue(storedValue, traitObjType,
                                                     node->line);
            }
            if(!storedValue)
                return;
        }
        else
        {
            storedValue = llvm::Constant::getNullValue(traitObjType);
        }

        storedValue = applyStructCopySemantics(storedValue);
        builder.CreateStore(storedValue, alloca);
        if(node->expression)
            consumeMoveFromExpression(node->expression, node->line,
                                      "initializing '" + node->name + "'");
        clearMovedVariable(node->name);
        clearPointerBorrow(node->name);
        namedValues[node->name] = alloca;
        recordVariableScopeDepth(node->name);
        variableTypes[node->name] = TypeNode::TYPE_TRAIT_OBJECT;
        traitObjectVariableTypes[node->name] = traitObj->traitName;
        constantVariables.insert(node->name);
        return;
    }

    // For list/array-fill initializers with a declared element type, pass that
    // type to the generator so integer literals (always i64) are coerced to the
    // declared size (e.g., i32), preventing stride mismatches during iteration.
    llvm::Value* initValue = nullptr;
    if(auto* genListType = dynamic_cast<GenericListTypeNode*>(node->type))
    {
        llvm::Type* declElem = getLLVMType(genListType->elementType->kind);
        if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->expression))
            initValue = generateListLiteral(listLit, declElem);
        else if(auto* arrFill = dynamic_cast<ArrayFillNode*>(node->expression))
            initValue = generateArrayFill(arrFill, declElem);
    }
    if(!initValue)
        initValue = generateExpression(node->expression);
    if(!initValue)
        return;
    // For `let r = &s` where s is a string: getLValuePointer returns the alloca
    // (char**). Load the actual char* so the borrow variable behaves like a
    // normal string value when passed to println! / strcmp / etc.
    if(auto* unary = dynamic_cast<UnaryOpNode*>(node->expression))
    {
        if(unary->op == UnaryOpNode::OP_ADDR ||
           unary->op == UnaryOpNode::OP_ADDR_MUT)
        {
            if(auto* id = dynamic_cast<IdentifierNode*>(unary->operand))
            {
                auto typeIt = variableTypes.find(id->name);
                if(typeIt != variableTypes.end() &&
                   typeIt->second == TypeNode::TYPE_STRING)
                {
                    initValue =
                        builder.CreateLoad(initValue->getType(), initValue,
                                           id->name + ".str_borrow");
                }
            }
        }
    }
    consumeMoveFromExpression(node->expression, node->line,
                              "initializing '" + node->name + "'");
    clearMovedVariable(node->name);
    clearPointerBorrow(node->name);
    recordVariableScopeDepth(node->name);

    if(!node->type)
    {
        llvm::Function* currentFunction = builder.GetInsertBlock()->getParent();
        llvm::AllocaInst* alloca =
            initValue->getType()->isStructTy()
                ? createEntryBlockAlloca(currentFunction, initValue->getType(),
                                         node->name)
                : builder.CreateAlloca(initValue->getType(), nullptr,
                                       node->name);
        initValue = applyStructCopySemantics(initValue);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;

        auto infer_kind_from_expr =
            [&](ExpressionNode* expr) -> TypeNode::TypeKind
        {
            if(dynamic_cast<IntLiteralNode*>(expr))
                return TypeNode::TYPE_I64; // generateIntLiteral always emits
                                           // i64
            if(dynamic_cast<BoolLiteralNode*>(expr))
                return TypeNode::TYPE_BOOL;
            if(dynamic_cast<FloatLiteralNode*>(expr))
                return TypeNode::TYPE_FLOAT;
            if(dynamic_cast<DoubleLiteralNode*>(expr))
                return TypeNode::TYPE_DOUBLE;
            if(dynamic_cast<StringLiteralNode*>(expr))
                return TypeNode::TYPE_STRING;
            if(dynamic_cast<ListLiteralNode*>(expr))
                return TypeNode::TYPE_LIST;
            if(dynamic_cast<MapLiteralNode*>(expr))
                return TypeNode::TYPE_MAP;
            if(dynamic_cast<TupleLiteralNode*>(expr))
                return TypeNode::TYPE_TUPLE;
            if(dynamic_cast<StructLiteralNode*>(expr))
                return TypeNode::TYPE_STRUCT;
            if(auto* id = dynamic_cast<IdentifierNode*>(expr))
            {
                auto it = variableTypes.find(id->name);
                if(it != variableTypes.end())
                    return it->second;
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
            {
                TypeNode* fieldType = getLValueType(field, node->line);
                if(fieldType)
                    return fieldType->kind;
            }
            if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
            {
                if(call->name == "String::new" ||
                   call->name == "String::with_capacity" ||
                   call->name == "String::from" ||
                   call->name == "String::to_utf8")
                    return TypeNode::TYPE_STRING;
                if(call->name == "Vec::new")
                    return TypeNode::TYPE_LIST;
            }
            if(auto* bin = dynamic_cast<BinaryOpNode*>(expr))
            {
                if(bin->op == BinaryOpNode::OP_PLUS)
                {
                    TypeNode::TypeKind lhsKind =
                        getExpressionTypeKind(bin->left, variableTypes);
                    TypeNode::TypeKind rhsKind =
                        getExpressionTypeKind(bin->right, variableTypes);
                    bool lhsIsString = lhsKind == TypeNode::TYPE_STRING ||
                                       lhsKind == TypeNode::TYPE_STR8 ||
                                       lhsKind == TypeNode::TYPE_STR16;
                    bool rhsIsString = rhsKind == TypeNode::TYPE_STRING ||
                                       rhsKind == TypeNode::TYPE_STR8 ||
                                       rhsKind == TypeNode::TYPE_STR16;
                    if(lhsIsString && rhsIsString && lhsKind == rhsKind)
                        return lhsKind;
                }
            }
            if(auto* mc = dynamic_cast<MethodCallNode*>(expr))
            {
                if(mc->methodName == "clone")
                    return TypeNode::TYPE_STRING;
            }
            if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
            {
                if(unary->op == UnaryOpNode::OP_ADDR ||
                   unary->op == UnaryOpNode::OP_ADDR_MUT)
                {
                    if(auto* id = dynamic_cast<IdentifierNode*>(unary->operand))
                    {
                        auto tit = variableTypes.find(id->name);
                        if(tit != variableTypes.end() &&
                           (tit->second == TypeNode::TYPE_STRING ||
                            tit->second == TypeNode::TYPE_STR8 ||
                            tit->second == TypeNode::TYPE_STR16))
                            return tit->second;
                    }
                }
            }

            llvm::Type* t = initValue->getType();
            if(t->isIntegerTy(1))
                return TypeNode::TYPE_BOOL;
            if(t->isFloatingPointTy())
            {
                if(t->isFloatTy())
                    return TypeNode::TYPE_FLOAT;
                if(t->isDoubleTy())
                    return TypeNode::TYPE_DOUBLE;
            }
            if(t->isPointerTy())
                return TypeNode::TYPE_PTR;
            if(t->isStructTy())
                return TypeNode::TYPE_STRUCT;
            return TypeNode::TYPE_INT;
        };

        TypeNode::TypeKind inferredKind =
            infer_kind_from_expr(node->expression);
        variableTypes[node->name] = inferredKind;

        if(auto* id = dynamic_cast<IdentifierNode*>(node->expression))
        {
            auto sit = structVariableTypes.find(id->name);
            if(sit != structVariableTypes.end())
            {
                structVariableTypes[node->name] = sit->second;
                registerStructCleanupIfNeeded(node->name, sit->second);
            }
            auto eit = enumVariableTypes.find(id->name);
            if(eit != enumVariableTypes.end())
            {
                enumVariableTypes[node->name] = eit->second;
            }
            auto lit = listElementTypes.find(id->name);
            if(lit != listElementTypes.end())
                listElementTypes[node->name] = lit->second;
            auto capIt = arrayCapacities.find(id->name);
            if(capIt != arrayCapacities.end())
            {
                arrayCapacities[node->name] = capIt->second;
                auto lenIt = arrayKnownLengths.find(id->name);
                if(lenIt != arrayKnownLengths.end())
                    arrayKnownLengths[node->name] = lenIt->second;
                else
                    arrayKnownLengths.erase(node->name);
            }
            auto mit = mapKeyValueTypes.find(id->name);
            if(mit != mapKeyValueTypes.end())
                mapKeyValueTypes[node->name] = mit->second;
            auto tit = tupleElementTypes.find(id->name);
            if(tit != tupleElementTypes.end())
                tupleElementTypes[node->name] = tit->second;
            auto pit = pointerElementTypes.find(id->name);
            if(pit != pointerElementTypes.end())
                pointerElementTypes[node->name] = pit->second;
            auto pnit = pointerKnownNull.find(id->name);
            if(pnit != pointerKnownNull.end())
                pointerKnownNull[node->name] = pnit->second;
            else
                pointerKnownNull.erase(node->name);
        }

        if(TypeNode* inferredExprType =
               getLValueType(node->expression, node->line))
        {
            if(auto* structRef =
                   dynamic_cast<StructTypeRefNode*>(inferredExprType))
            {
                std::string resolvedEnumName =
                    resolveVisibleEnumName(structRef->structName);
                if(!resolvedEnumName.empty())
                {
                    TypeNode::TypeKind baseKind = TypeNode::TYPE_I32;
                    auto baseIt = enumBaseTypes.find(resolvedEnumName);
                    if(baseIt != enumBaseTypes.end())
                        baseKind = baseIt->second;
                    variableTypes[node->name] = baseKind;
                    enumVariableTypes[node->name] = resolvedEnumName;
                }
                else
                {
                    variableTypes[node->name] = TypeNode::TYPE_STRUCT;
                    structVariableTypes[node->name] = structRef->structName;
                    registerStructCleanupIfNeeded(node->name,
                                                  structRef->structName);
                }
            }
            else if(auto* genStructRef =
                        dynamic_cast<GenericStructTypeRefNode*>(
                            inferredExprType))
            {
                std::string mangledName = getOrCreateMonomorphizedStruct(
                    genStructRef->structName, genStructRef->typeArgs);
                variableTypes[node->name] = TypeNode::TYPE_STRUCT;
                structVariableTypes[node->name] = mangledName;
                registerStructCleanupIfNeeded(node->name, mangledName);
            }
            else if(auto* genListType =
                        dynamic_cast<GenericListTypeNode*>(inferredExprType))
            {
                variableTypes[node->name] = TypeNode::TYPE_LIST;
                listElementTypes[node->name] = genListType->elementType;
                if(auto* arrayType =
                       dynamic_cast<ArrayTypeNode*>(genListType))
                {
                    arrayCapacities[node->name] = arrayType->capacity;
                    if(auto size =
                           fixedArrayExpressionKnownLength(node->expression))
                        arrayKnownLengths[node->name] = *size;
                    else
                        arrayKnownLengths.erase(node->name);
                }
            }
            else if(auto* mapType =
                        dynamic_cast<MapTypeNode*>(inferredExprType))
            {
                variableTypes[node->name] = TypeNode::TYPE_MAP;
                mapKeyValueTypes[node->name] =
                    std::make_pair(mapType->keyType, mapType->valueType);
            }
            else if(auto* tupleType =
                        dynamic_cast<TupleTypeNode*>(inferredExprType))
            {
                variableTypes[node->name] = TypeNode::TYPE_TUPLE;
                std::vector<TypeNode*> elemTypes;
                if(tupleType->elementTypes)
                {
                    for(auto* t : tupleType->elementTypes->types)
                        elemTypes.push_back(t);
                }
                tupleElementTypes[node->name] = elemTypes;
            }
            else if(auto* ptrType =
                        dynamic_cast<PointerTypeNode*>(inferredExprType))
            {
                variableTypes[node->name] = TypeNode::TYPE_PTR;
                pointerElementTypes[node->name] = ptrType->elementType;
                if(auto knownNull =
                       pointerExpressionKnownNull(node->expression))
                    pointerKnownNull[node->name] = *knownNull;
                else
                    pointerKnownNull.erase(node->name);
            }
        }

        if(auto* call = dynamic_cast<FunctionCallNode*>(node->expression))
        {
            if(call->name == "String::new" ||
               call->name == "String::with_capacity" ||
               call->name == "String::from" || call->name == "String::to_utf8")
            {
                variableTypes[node->name] = TypeNode::TYPE_STRING;
            }
            if(call->name == "Vec::new")
            {
                variableTypes[node->name] = TypeNode::TYPE_LIST;
            }
        }
        if(auto* mc2 = dynamic_cast<MethodCallNode*>(node->expression))
        {
            if(mc2->methodName == "clone")
                variableTypes[node->name] = TypeNode::TYPE_STRING;
        }
        if(auto* enumLit = dynamic_cast<EnumLiteralNode*>(node->expression))
        {
            enumVariableTypes[node->name] = enumLit->enumName;
            std::string resolvedEnumName =
                resolveVisibleEnumName(enumLit->enumName);
            auto baseIt = enumBaseTypes.find(resolvedEnumName);
            if(baseIt != enumBaseTypes.end())
                variableTypes[node->name] = baseIt->second;
        }

        if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_LIST;
            TypeNode::TypeKind elemKind = TypeNode::TYPE_I64;
            if(listLit->elements && !listLit->elements->elements.empty())
                elemKind = infer_kind_from_expr(listLit->elements->elements[0]);
            listElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(elemKind));
        }
        else if(auto* arrFill2 = dynamic_cast<ArrayFillNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_LIST;
            TypeNode::TypeKind elemKind = TypeNode::TYPE_I64;
            if(arrFill2->value)
                elemKind = infer_kind_from_expr(arrFill2->value);
            listElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(elemKind));
        }
        else if(auto* mapLit = dynamic_cast<MapLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_MAP;
            TypeNode::TypeKind keyKind = TypeNode::TYPE_INT;
            TypeNode::TypeKind valKind = TypeNode::TYPE_INT;
            if(mapLit->entries && !mapLit->entries->entries.empty())
            {
                auto* first = mapLit->entries->entries[0];
                keyKind = infer_kind_from_expr(first->key);
                valKind = infer_kind_from_expr(first->value);
            }
            mapKeyValueTypes[node->name] = std::make_pair(
                static_cast<TypeNode*>(create_type_node(keyKind)),
                static_cast<TypeNode*>(create_type_node(valKind)));
        }
        else if(auto* tupleLit =
                    dynamic_cast<TupleLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_TUPLE;
            std::vector<TypeNode*> elems;
            if(tupleLit->elements)
            {
                for(auto* e : tupleLit->elements->elements)
                {
                    elems.push_back(static_cast<TypeNode*>(
                        create_type_node(infer_kind_from_expr(e))));
                }
            }
            tupleElementTypes[node->name] = elems;
        }
        else if(auto* structLit =
                    dynamic_cast<StructLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_STRUCT;
            structVariableTypes[node->name] = structLit->structName;
            registerStructCleanupIfNeeded(node->name, structLit->structName);
        }
        else if(variableTypes[node->name] == TypeNode::TYPE_PTR)
        {
            pointerElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(TypeNode::TYPE_I8));
            if(auto knownNull = pointerExpressionKnownNull(node->expression))
                pointerKnownNull[node->name] = *knownNull;
            else
                pointerKnownNull.erase(node->name);
        }
        {
            auto* _borrow_unary = dynamic_cast<UnaryOpNode*>(node->expression);
            bool _is_mut_borrow =
                _borrow_unary && _borrow_unary->op == UnaryOpNode::OP_ADDR_MUT;
            registerPointerBorrow(node->name, node->expression, node->line,
                                  _is_mut_borrow);
        }

        constantVariables.insert(node->name);
        return;
    }

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(node->type))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        llvm::Type* structType = getStructType(mangledName);
        if(!structType)
        {
            reportError(node->line, "failed to monomorphize struct: " +
                                        genStructRef->structName);
            return;
        }

        llvm::AllocaInst* alloca = createEntryBlockAlloca(
            builder.GetInsertBlock()->getParent(), structType, node->name);
        initValue = applyStructCopySemantics(initValue, genStructRef);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = mangledName;
        registerStructCleanupIfNeeded(node->name, mangledName);
        constantVariables.insert(node->name);
        return;
    }

    // Handle generic list type
    if(auto* genListType = dynamic_cast<GenericListTypeNode*>(node->type))
    {
        // Store element type for iteration
        listElementTypes[node->name] = genListType->elementType;
        if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(genListType))
        {
            arrayCapacities[node->name] = arrayType->capacity;
            if(auto size = fixedArrayExpressionKnownLength(node->expression))
                arrayKnownLengths[node->name] = *size;
            else
                arrayKnownLengths.erase(node->name);
        }

        // List struct type: { i64, ptr }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMType(genListType->elementType->kind), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(listStructType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_LIST;
        constantVariables.insert(node->name);
        return;
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(node->type))
    {
        // Store key/value types
        mapKeyValueTypes[node->name] =
            std::make_pair(mapType->keyType, mapType->valueType);

        // Map struct type: { i64, ptr, ptr }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(mapStructType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_MAP;
        constantVariables.insert(node->name);
        return;
    }

    // Handle pointer type
    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(node->type))
    {
        pointerElementTypes[node->name] = ptrType->elementType;

        llvm::Type* llvmPtrType = getLLVMTypeFromNode(ptrType);
        if(!llvmPtrType)
            return;

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(llvmPtrType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_PTR;
        if(auto knownNull = pointerExpressionKnownNull(node->expression))
            pointerKnownNull[node->name] = *knownNull;
        else
            pointerKnownNull.erase(node->name);
        {
            auto* _borrow_unary = dynamic_cast<UnaryOpNode*>(node->expression);
            bool _is_mut_borrow =
                _borrow_unary && _borrow_unary->op == UnaryOpNode::OP_ADDR_MUT;
            registerPointerBorrow(node->name, node->expression, node->line,
                                  _is_mut_borrow);
        }
        constantVariables.insert(node->name);
        return;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(node->type))
    {
        // Store element types for tuple access
        std::vector<TypeNode*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            elemTypes.push_back(t);
        }
        tupleElementTypes[node->name] = elemTypes;

        // Create LLVM struct type for tuple based on declared types
        std::vector<llvm::Type*> tupleTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
            {
                reportError(node->line, "invalid type in tuple");
                return;
            }
            tupleTypes.push_back(elemType);
        }
        llvm::StructType* tupleStructType =
            llvm::StructType::get(context, tupleTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(tupleStructType, nullptr, node->name);

        // Convert tuple literal elements to match declared types
        if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->expression))
        {
            // Build tuple with proper type conversions.
            // Use CreateExtractValue from the already-generated initValue so
            // each element is generated exactly once (consumeMoveFromExpression
            // has already run and marked MoveOnly sources as moved).
            llvm::Value* tupleVal = llvm::UndefValue::get(tupleStructType);

            for(size_t i = 0; i < tupleLit->elements->elements.size() &&
                              i < tupleType->elementTypes->types.size();
                ++i)
            {
                llvm::Value* elemVal = builder.CreateExtractValue(
                    initValue, {static_cast<unsigned>(i)}, "tuple.extract");
                if(!elemVal)
                    return;

                llvm::Type* targetElemType = tupleTypes[i];
                llvm::Type* sourceElemType = elemVal->getType();

                // Convert if needed (only for primitive types)
                if(sourceElemType != targetElemType)
                {
                    if(sourceElemType->isIntegerTy() &&
                       targetElemType->isIntegerTy())
                    {
                        unsigned srcBits = sourceElemType->getIntegerBitWidth();
                        unsigned dstBits = targetElemType->getIntegerBitWidth();
                        if(srcBits > dstBits)
                        {
                            elemVal = builder.CreateTrunc(
                                elemVal, targetElemType, "trunc");
                        }
                        else if(srcBits < dstBits)
                        {
                            elemVal = builder.CreateSExt(
                                elemVal, targetElemType, "sext");
                        }
                    }
                    else if(sourceElemType->isIntegerTy() &&
                            targetElemType->isFloatingPointTy())
                    {
                        elemVal = builder.CreateSIToFP(elemVal, targetElemType,
                                                       "sitofp");
                    }
                    else if(sourceElemType->isFloatingPointTy() &&
                            targetElemType->isIntegerTy())
                    {
                        elemVal = builder.CreateFPToSI(elemVal, targetElemType,
                                                       "fptosi");
                    }
                    else if(sourceElemType->isFloatingPointTy() &&
                            targetElemType->isFloatingPointTy())
                    {
                        elemVal = builder.CreateFPCast(elemVal, targetElemType,
                                                       "fpcast");
                    }
                    // For struct types and other complex types, no conversion
                    // needed if types match
                }

                tupleVal = builder.CreateInsertValue(
                    tupleVal, elemVal, static_cast<unsigned>(i), "tuple.elem");
            }

            builder.CreateStore(tupleVal, alloca);
        }
        else
        {
            // Not a literal, just store (may fail if types mismatch)
            builder.CreateStore(initValue, alloca);
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_TUPLE;
        constantVariables.insert(node->name);
        return;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(node->type))
    {
        std::string resolvedEnumName =
            resolveVisibleEnumName(structRef->structName);
        if(!resolvedEnumName.empty())
        {
            TypeNode::TypeKind baseKind = TypeNode::TYPE_I32;
            auto baseIt = enumBaseTypes.find(resolvedEnumName);
            if(baseIt != enumBaseTypes.end())
                baseKind = baseIt->second;

            llvm::Type* targetType = getLLVMType(baseKind);
            llvm::AllocaInst* alloca =
                builder.CreateAlloca(targetType, nullptr, node->name);

            llvm::Value* initValue = nullptr;
            if(node->expression)
            {
                initValue = generateExpression(node->expression);
            }
            if(!initValue)
            {
                if(Helpers::isEnumStringType(baseKind))
                {
                    initValue = llvm::ConstantPointerNull::get(
                        llvm::cast<llvm::PointerType>(targetType));
                }
                else
                {
                    initValue = llvm::ConstantInt::get(targetType, 0, true);
                }
            }

            if(initValue->getType() != targetType)
            {
                if(Helpers::isEnumStringType(baseKind) &&
                   initValue->getType()->isPointerTy())
                {
                    initValue = builder.CreateBitCast(initValue, targetType,
                                                      "enum.cast.ptr");
                }
                else if(initValue->getType()->isIntegerTy())
                {
                    initValue = builder.CreateIntCast(
                        initValue, targetType, !Helpers::enumIsUnsigned(baseKind),
                        Helpers::enumIsUnsigned(baseKind) ? "enum.cast.u"
                                                 : "enum.cast.s");
                }
                else
                {
                    reportError(node->line,
                                Helpers::isEnumStringType(baseKind)
                                    ? "enum initializer must be str8"
                                    : "enum initializer must be integer");
                    return;
                }
            }

            initValue = applyStructCopySemantics(initValue, node->type);
            builder.CreateStore(initValue, alloca);
            namedValues[node->name] = alloca;
            variableTypes[node->name] = baseKind;
            enumVariableTypes[node->name] = resolvedEnumName;
            constantVariables.insert(node->name);
            return;
        }

        llvm::Type* structType = getStructType(structRef->structName);
        if(!structType)
        {
            reportError(node->line,
                        "unknown struct type: " + structRef->structName);
            return;
        }

        llvm::AllocaInst* alloca = createEntryBlockAlloca(
            builder.GetInsertBlock()->getParent(), structType, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = structRef->structName;
        registerStructCleanupIfNeeded(node->name, structRef->structName);
        constantVariables.insert(node->name);
        return;
    }

    llvm::Type* targetType = getLLVMType(node->type->kind);
    llvm::AllocaInst* alloca =
        builder.CreateAlloca(targetType, nullptr, node->name);

    // Convert init value to target type if necessary
    llvm::Type* initType = initValue->getType();
    if(initType != targetType)
    {
        if(initType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned initBits = initType->getIntegerBitWidth();
            unsigned targetBits = targetType->getIntegerBitWidth();
            if(initBits > targetBits)
            {
                // Truncate (e.g., i64 -> i8)
                initValue = builder.CreateTrunc(initValue, targetType, "trunc");
            }
            else if(initBits < targetBits)
            {
                // Extend - use ZExt for unsigned target, SExt for signed
                if(isUnsignedType(node->type->kind))
                {
                    initValue =
                        builder.CreateZExt(initValue, targetType, "zext");
                }
                else
                {
                    initValue =
                        builder.CreateSExt(initValue, targetType, "sext");
                }
            }
        }
        else if(initType->isIntegerTy() && targetType->isFloatingPointTy())
        {
            initValue = builder.CreateSIToFP(initValue, targetType, "sitofp");
        }
        else if(initType->isFloatingPointTy() && targetType->isIntegerTy())
        {
            initValue = builder.CreateFPToSI(initValue, targetType, "fptosi");
        }
        else if(initType->isFloatingPointTy() &&
                targetType->isFloatingPointTy())
        {
            initValue = builder.CreateFPCast(initValue, targetType, "fpcast");
        }
    }

    builder.CreateStore(initValue, alloca);
    namedValues[node->name] = alloca;
    variableTypes[node->name] = node->type->kind;

    // Mark this variable as constant (declared with 'let')
    constantVariables.insert(node->name);
}

void CodeGenerator::generateCexprDeclaration(CexprDeclNode* node,
                                             bool emitRuntimeBinding)
{
    if(!node)
        return;
    if(!node->type)
    {
        reportError(node->line,
                    "cexpr declaration requires an explicit type");
        return;
    }
    if(!node->expression)
    {
        reportError(node->line,
                    "cexpr declaration requires an initializer");
        return;
    }

    ConstexprValue value;
    std::string errorMessage;
    if(!evalConstexprExpression(node->expression, value, &errorMessage,
                                nullptr, 0))
    {
        reportError(node->line,
                    errorMessage.empty()
                        ? "cexpr declaration requires a compile-time "
                          "expression"
                        : errorMessage);
        return;
    }
    if(!coerceConstexprValueToKind(value, node->type->kind, &errorMessage,
                                   "cexpr declarations"))
    {
        reportError(node->line, errorMessage);
        return;
    }

    constexprValues[node->name] = value;
    if(!emitRuntimeBinding)
        return;

    llvm::Constant* initValue =
        buildLLVMConstantFromConstexprValue(value, node->type, node->line);
    if(!initValue)
        return;
    llvm::Function* currentFunction = builder.GetInsertBlock()->getParent();
    llvm::AllocaInst* alloca =
        createEntryBlockAlloca(currentFunction, initValue->getType(),
                               node->name);
    builder.CreateStore(initValue, alloca);
    namedValues[node->name] = alloca;
    variableTypes[node->name] = Helpers::normalizeInferredKind(node->type->kind);
    constantVariables.insert(node->name);
    recordVariableScopeDepth(node->name);
}

void CodeGenerator::generateVarDeclaration(VarDeclNode* node)
{
    recordScopedPointerVariable(node->name);
    enumVariableTypes.erase(node->name);
    clearPointerBorrow(node->name);
    // `var` is always mutable, including when shadowing a previous `let`.
    constantVariables.erase(node->name);

    if(!validateFixedArrayInitializer(node->type, node->initExpr, node->line))
        return;

    if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(node->type))
    {
        llvm::Type* traitObjType = getLLVMTypeFromNode(traitObj);
        if(!traitObjType)
        {
            reportError(node->line,
                        "unknown trait object type: " + traitObj->traitName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(traitObjType, nullptr, node->name);
        llvm::Value* storedValue = nullptr;
        if(node->initExpr)
        {
            TypeNode* exprType = getLValueType(node->initExpr, node->line);
            if(dynamic_cast<TraitObjectTypeNode*>(exprType))
            {
                storedValue = generateExpression(node->initExpr);
                storedValue = coerceTraitObjectValue(storedValue, traitObjType,
                                                     node->line);
            }
            else if(exprType)
                storedValue = buildTraitObjectValue(
                    node->initExpr, traitObj->traitName, node->line);
            else
            {
                storedValue = generateExpression(node->initExpr);
                storedValue = coerceTraitObjectValue(storedValue, traitObjType,
                                                     node->line);
            }
            if(!storedValue)
                return;
        }
        else
        {
            storedValue = llvm::Constant::getNullValue(traitObjType);
        }
        storedValue = applyStructCopySemantics(storedValue);
        builder.CreateStore(storedValue, alloca);
        if(node->initExpr)
            consumeMoveFromExpression(node->initExpr, node->line,
                                      "initializing '" + node->name + "'");
        clearMovedVariable(node->name);
        namedValues[node->name] = alloca;
        recordVariableScopeDepth(node->name);
        variableTypes[node->name] = TypeNode::TYPE_TRAIT_OBJECT;
        traitObjectVariableTypes[node->name] = traitObj->traitName;
        return;
    }

    if(node->initExpr)
    {
        consumeMoveFromExpression(node->initExpr, node->line,
                                  "initializing '" + node->name + "'");
    }
    clearMovedVariable(node->name);
    if(node->isStaticStorage || node->isGlobalStorage)
    {
        if(node->type && !node->initExpr && !node->isExplicitZeroInit &&
           warnImplicitZeroInit)
        {
            reportWarning(node->line, node->col,
                          "implicit zero-initialization for typed var '" +
                              node->name +
                              "'; use '{}' to make zero-init explicit");
        }
        std::string storageName = node->name;
        if(node->isStaticStorage)
        {
            llvm::Function* fn = builder.GetInsertBlock()
                                     ? builder.GetInsertBlock()->getParent()
                                     : nullptr;
            const std::string owner = fn ? fn->getName().str() : "global";
            storageName = "__mlang_static_" + owner + "_" + node->name;
        }
        else
        {
            storageName = "__mlang_global_" + node->name;
        }

        TypeNode::TypeKind kind = TypeNode::TYPE_INT;
        llvm::Type* targetType = nullptr;
        std::string structTypeName;
        if(node->type)
        {
            kind = node->type->kind;
            targetType = getLLVMTypeFromNode(node->type);
            if(auto* sr = dynamic_cast<StructTypeRefNode*>(node->type))
            {
                kind = TypeNode::TYPE_STRUCT;
                structTypeName = sr->structName;
            }
            else if(auto* gsr =
                        dynamic_cast<GenericStructTypeRefNode*>(node->type))
            {
                kind = TypeNode::TYPE_STRUCT;
                structTypeName = getOrCreateMonomorphizedStruct(gsr->structName,
                                                                gsr->typeArgs);
            }
        }

        if(!targetType)
        {
            if(!node->initExpr)
            {
                reportError(node->line, "static/global var declaration without "
                                        "type requires initializer");
                return;
            }
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(!initValue)
                return;
            targetType = initValue->getType();
            if(TypeNode* inferredNode =
                   inferExpressionTypeNode(node->initExpr, node->line))
                kind = inferredNode->kind;
            else if(targetType->isIntegerTy(1))
                kind = TypeNode::TYPE_BOOL;
            else if(targetType->isFloatTy())
                kind = TypeNode::TYPE_FLOAT;
            else if(targetType->isDoubleTy())
                kind = TypeNode::TYPE_DOUBLE;
            else if(targetType->isPointerTy())
                kind = TypeNode::TYPE_PTR;
            else
                kind = TypeNode::TYPE_INT;
        }

        auto* gv = module->getGlobalVariable(storageName);
        if(!gv)
        {
            gv = new llvm::GlobalVariable(
                *module, targetType, false, llvm::GlobalValue::InternalLinkage,
                llvm::Constant::getNullValue(targetType), storageName);
        }

        if(node->initExpr)
        {
            if((kind == TypeNode::TYPE_PTR || targetType->isPointerTy()) &&
               !validateNoEscapingBorrow(node->initExpr, node->line,
                                         "store in global/static '" +
                                             node->name + "'"))
            {
                return;
            }

            if(node->isStaticStorage)
            {
                std::string guardName = storageName + "__inited";
                auto* guard = module->getGlobalVariable(guardName);
                if(!guard)
                {
                    guard = new llvm::GlobalVariable(
                        *module, llvm::Type::getInt1Ty(context), false,
                        llvm::GlobalValue::InternalLinkage,
                        llvm::ConstantInt::getFalse(context), guardName);
                }

                llvm::Function* fn = builder.GetInsertBlock()->getParent();
                auto* initBB =
                    llvm::BasicBlock::Create(context, "static.init", fn);
                auto* contBB =
                    llvm::BasicBlock::Create(context, "static.cont", fn);
                llvm::Value* isInit = builder.CreateLoad(
                    guard->getValueType(), guard, "static.inited");
                builder.CreateCondBr(isInit, contBB, initBB);

                builder.SetInsertPoint(initBB);
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(!initValue)
                    return;
                if(initValue->getType() != targetType)
                {
                    if(initValue->getType()->isIntegerTy() &&
                       targetType->isIntegerTy())
                        initValue = builder.CreateSExtOrTrunc(
                            initValue, targetType, "static.cast");
                    else if(initValue->getType()->isIntegerTy() &&
                            targetType->isFloatingPointTy())
                        initValue = builder.CreateSIToFP(initValue, targetType,
                                                         "static.sitofp");
                    else if(initValue->getType()->isFloatingPointTy() &&
                            targetType->isIntegerTy())
                        initValue = builder.CreateFPToSI(initValue, targetType,
                                                         "static.fptosi");
                    else if(initValue->getType()->isFloatingPointTy() &&
                            targetType->isFloatingPointTy())
                        initValue = builder.CreateFPCast(initValue, targetType,
                                                         "static.fpcast");
                    else
                    {
                        reportError(
                            node->line,
                            "type mismatch in static initializer for '" +
                                node->name + "'");
                        return;
                    }
                }
                builder.CreateStore(initValue, gv);
                builder.CreateStore(llvm::ConstantInt::getTrue(context), guard);
                builder.CreateBr(contBB);
                builder.SetInsertPoint(contBB);
            }
            else if(auto* cinit = llvm::dyn_cast<llvm::Constant>(
                        generateExpression(node->initExpr)))
            {
                gv->setInitializer(cinit);
            }
            else
            {
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(!initValue)
                    return;
                if(initValue->getType() != targetType)
                {
                    if(initValue->getType()->isIntegerTy() &&
                       targetType->isIntegerTy())
                        initValue = builder.CreateSExtOrTrunc(
                            initValue, targetType, "global.cast");
                    else if(initValue->getType()->isIntegerTy() &&
                            targetType->isFloatingPointTy())
                        initValue = builder.CreateSIToFP(initValue, targetType,
                                                         "global.sitofp");
                    else if(initValue->getType()->isFloatingPointTy() &&
                            targetType->isIntegerTy())
                        initValue = builder.CreateFPToSI(initValue, targetType,
                                                         "global.fptosi");
                    else if(initValue->getType()->isFloatingPointTy() &&
                            targetType->isFloatingPointTy())
                        initValue = builder.CreateFPCast(initValue, targetType,
                                                         "global.fpcast");
                    else
                    {
                        reportError(
                            node->line,
                            "type mismatch in global initializer for '" +
                                node->name + "'");
                        return;
                    }
                }
                builder.CreateStore(initValue, gv);
            }
        }

        namedValues[node->name] = gv;
        variableTypes[node->name] = kind;
        recordVariableScopeDepth(node->name);
        if(!structTypeName.empty())
            structVariableTypes[node->name] = structTypeName;
        if(node->isGlobalStorage)
        {
            globalNamedValues[node->name] = gv;
            globalVariableTypes[node->name] = kind;
            if(!structTypeName.empty())
                globalStructVariableTypes[node->name] = structTypeName;
        }
        return;
    }

    recordVariableScopeDepth(node->name);

    // Inline closure: var inc = || { ... }
    // Store the AST and leave a null-ptr placeholder; no LLVM value generated.
    if(node->initExpr)
    {
        if(auto* closureInit = dynamic_cast<ClosureNode*>(node->initExpr))
        {
            closureVariables[node->name] = closureInit;
#if LLVM_VERSION_MAJOR >= 15
            llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
            llvm::Type* ptrType =
                llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
            llvm::AllocaInst* alloca =
                builder.CreateAlloca(ptrType, nullptr, node->name + ".closure");
            builder.CreateStore(llvm::ConstantPointerNull::get(
                                    llvm::cast<llvm::PointerType>(ptrType)),
                                alloca);
            namedValues[node->name] = alloca;
            return;
        }
    }

    auto storeZeroInitializedValue = [&](llvm::AllocaInst* alloca,
                                         llvm::Type* storageType) -> void
    {
        if(!alloca || !storageType || node->initExpr)
            return;
        builder.CreateStore(llvm::Constant::getNullValue(storageType), alloca);
    };
    auto emitImplicitZeroInitWarning = [&]() -> void
    {
        if(node->initExpr || node->isExplicitZeroInit ||
           !this->warnImplicitZeroInit)
            return;
        reportWarning(node->line, node->col,
                      "implicit zero-initialization for typed var '" +
                          node->name +
                          "'; use '{}' to make zero-init explicit");
    };

    if(!node->type)
    {
        if(!node->initExpr)
        {
            reportError(node->line,
                        "var declaration without type requires initializer");
            return;
        }

        llvm::Value* initValue = generateExpression(node->initExpr);
        if(!initValue)
            return;

        llvm::Function* currentFunction = builder.GetInsertBlock()->getParent();
        llvm::AllocaInst* alloca =
            initValue->getType()->isStructTy()
                ? createEntryBlockAlloca(currentFunction, initValue->getType(),
                                         node->name)
                : builder.CreateAlloca(initValue->getType(), nullptr,
                                       node->name);
        initValue = applyStructCopySemantics(initValue);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;

        auto infer_kind_from_expr =
            [&](ExpressionNode* expr) -> TypeNode::TypeKind
        {
            if(auto* cast = dynamic_cast<CastExpressionNode*>(expr))
                return cast->targetType;
            if(dynamic_cast<IntLiteralNode*>(expr))
                return TypeNode::TYPE_I64; // generateIntLiteral always emits
                                           // i64
            if(dynamic_cast<BoolLiteralNode*>(expr))
                return TypeNode::TYPE_BOOL;
            if(dynamic_cast<FloatLiteralNode*>(expr))
                return TypeNode::TYPE_FLOAT;
            if(dynamic_cast<DoubleLiteralNode*>(expr))
                return TypeNode::TYPE_DOUBLE;
            if(dynamic_cast<StringLiteralNode*>(expr))
                return TypeNode::TYPE_STRING;
            if(dynamic_cast<ListLiteralNode*>(expr))
                return TypeNode::TYPE_LIST;
            if(dynamic_cast<MapLiteralNode*>(expr))
                return TypeNode::TYPE_MAP;
            if(dynamic_cast<TupleLiteralNode*>(expr))
                return TypeNode::TYPE_TUPLE;
            if(dynamic_cast<StructLiteralNode*>(expr))
                return TypeNode::TYPE_STRUCT;
            if(auto* id = dynamic_cast<IdentifierNode*>(expr))
            {
                auto it = variableTypes.find(id->name);
                if(it != variableTypes.end())
                    return it->second;
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
            {
                TypeNode* fieldType = getLValueType(field, node->line);
                if(fieldType)
                    return fieldType->kind;
            }
            if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
            {
                if(call->name == "String::new" ||
                   call->name == "String::with_capacity" ||
                   call->name == "String::from" ||
                   call->name == "String::to_utf8")
                    return TypeNode::TYPE_STRING;
                if(call->name == "Vec::new")
                    return TypeNode::TYPE_LIST;
            }
            if(auto* bin = dynamic_cast<BinaryOpNode*>(expr))
            {
                if(bin->op == BinaryOpNode::OP_PLUS)
                {
                    TypeNode::TypeKind lhsKind =
                        getExpressionTypeKind(bin->left, variableTypes);
                    TypeNode::TypeKind rhsKind =
                        getExpressionTypeKind(bin->right, variableTypes);
                    bool lhsIsString = lhsKind == TypeNode::TYPE_STRING ||
                                       lhsKind == TypeNode::TYPE_STR8 ||
                                       lhsKind == TypeNode::TYPE_STR16;
                    bool rhsIsString = rhsKind == TypeNode::TYPE_STRING ||
                                       rhsKind == TypeNode::TYPE_STR8 ||
                                       rhsKind == TypeNode::TYPE_STR16;
                    if(lhsIsString && rhsIsString && lhsKind == rhsKind)
                        return lhsKind;
                }
            }
            if(auto* mc = dynamic_cast<MethodCallNode*>(expr))
            {
                if(mc->methodName == "clone")
                    return TypeNode::TYPE_STRING;
            }
            if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
            {
                if(unary->op == UnaryOpNode::OP_ADDR ||
                   unary->op == UnaryOpNode::OP_ADDR_MUT)
                {
                    if(auto* id = dynamic_cast<IdentifierNode*>(unary->operand))
                    {
                        auto tit = variableTypes.find(id->name);
                        if(tit != variableTypes.end() &&
                           (tit->second == TypeNode::TYPE_STRING ||
                            tit->second == TypeNode::TYPE_STR8 ||
                            tit->second == TypeNode::TYPE_STR16))
                            return tit->second;
                    }
                }
            }

            llvm::Type* t = initValue->getType();
            if(t->isIntegerTy(1))
                return TypeNode::TYPE_BOOL;
            if(t->isFloatingPointTy())
            {
                if(t->isFloatTy())
                    return TypeNode::TYPE_FLOAT;
                if(t->isDoubleTy())
                    return TypeNode::TYPE_DOUBLE;
            }
            if(t->isPointerTy())
                return TypeNode::TYPE_PTR;
            if(t->isStructTy())
                return TypeNode::TYPE_STRUCT;
            return TypeNode::TYPE_INT;
        };

        TypeNode::TypeKind inferredKind = infer_kind_from_expr(node->initExpr);
        variableTypes[node->name] = inferredKind;

        if(auto* id = dynamic_cast<IdentifierNode*>(node->initExpr))
        {
            auto sit = structVariableTypes.find(id->name);
            if(sit != structVariableTypes.end())
            {
                structVariableTypes[node->name] = sit->second;
                registerStructCleanupIfNeeded(node->name, sit->second);
            }
            auto eit = enumVariableTypes.find(id->name);
            if(eit != enumVariableTypes.end())
            {
                enumVariableTypes[node->name] = eit->second;
            }
            auto lit = listElementTypes.find(id->name);
            if(lit != listElementTypes.end())
                listElementTypes[node->name] = lit->second;
            auto capIt = arrayCapacities.find(id->name);
            if(capIt != arrayCapacities.end())
            {
                arrayCapacities[node->name] = capIt->second;
                auto lenIt = arrayKnownLengths.find(id->name);
                if(lenIt != arrayKnownLengths.end())
                    arrayKnownLengths[node->name] = lenIt->second;
                else
                    arrayKnownLengths.erase(node->name);
            }
            auto mit = mapKeyValueTypes.find(id->name);
            if(mit != mapKeyValueTypes.end())
                mapKeyValueTypes[node->name] = mit->second;
            auto tit = tupleElementTypes.find(id->name);
            if(tit != tupleElementTypes.end())
                tupleElementTypes[node->name] = tit->second;
            auto pit = pointerElementTypes.find(id->name);
            if(pit != pointerElementTypes.end())
                pointerElementTypes[node->name] = pit->second;
        }

        if(auto* call = dynamic_cast<FunctionCallNode*>(node->initExpr))
        {
            if(call->name == "String::new" ||
               call->name == "String::with_capacity" ||
               call->name == "String::from" || call->name == "String::to_utf8")
            {
                variableTypes[node->name] = TypeNode::TYPE_STRING;
            }
            if(call->name == "Vec::new")
            {
                variableTypes[node->name] = TypeNode::TYPE_LIST;
            }
        }
        if(auto* mc2 = dynamic_cast<MethodCallNode*>(node->initExpr))
        {
            if(mc2->methodName == "clone")
                variableTypes[node->name] = TypeNode::TYPE_STRING;
        }
        if(auto* enumLit = dynamic_cast<EnumLiteralNode*>(node->initExpr))
        {
            enumVariableTypes[node->name] = enumLit->enumName;
            std::string resolvedEnumName =
                resolveVisibleEnumName(enumLit->enumName);
            auto baseIt = enumBaseTypes.find(resolvedEnumName);
            if(baseIt != enumBaseTypes.end())
                variableTypes[node->name] = baseIt->second;
        }

        if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_LIST;
            TypeNode::TypeKind elemKind = TypeNode::TYPE_I64;
            if(listLit->elements && !listLit->elements->elements.empty())
                elemKind = infer_kind_from_expr(listLit->elements->elements[0]);
            listElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(elemKind));
        }
        else if(auto* arrFill = dynamic_cast<ArrayFillNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_LIST;
            TypeNode::TypeKind elemKind = TypeNode::TYPE_I64;
            if(arrFill->value)
                elemKind = infer_kind_from_expr(arrFill->value);
            listElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(elemKind));
        }
        else if(auto* mapLit = dynamic_cast<MapLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_MAP;
            TypeNode::TypeKind keyKind = TypeNode::TYPE_INT;
            TypeNode::TypeKind valKind = TypeNode::TYPE_INT;
            if(mapLit->entries && !mapLit->entries->entries.empty())
            {
                auto* first = mapLit->entries->entries[0];
                keyKind = infer_kind_from_expr(first->key);
                valKind = infer_kind_from_expr(first->value);
            }
            mapKeyValueTypes[node->name] = std::make_pair(
                static_cast<TypeNode*>(create_type_node(keyKind)),
                static_cast<TypeNode*>(create_type_node(valKind)));
        }
        else if(auto* tupleLit =
                    dynamic_cast<TupleLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_TUPLE;
            std::vector<TypeNode*> elems;
            if(tupleLit->elements)
            {
                for(auto* e : tupleLit->elements->elements)
                {
                    elems.push_back(static_cast<TypeNode*>(
                        create_type_node(infer_kind_from_expr(e))));
                }
            }
            tupleElementTypes[node->name] = elems;
        }
        else if(auto* structLit =
                    dynamic_cast<StructLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_STRUCT;
            structVariableTypes[node->name] = structLit->structName;
            registerStructCleanupIfNeeded(node->name, structLit->structName);
        }
        else if(variableTypes[node->name] == TypeNode::TYPE_PTR)
        {
            pointerElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(TypeNode::TYPE_I8));
            if(auto knownNull = pointerExpressionKnownNull(node->initExpr))
                pointerKnownNull[node->name] = *knownNull;
            else
                pointerKnownNull.erase(node->name);
        }
        {
            auto* _borrow_unary = dynamic_cast<UnaryOpNode*>(node->initExpr);
            bool _is_mut_borrow =
                _borrow_unary && _borrow_unary->op == UnaryOpNode::OP_ADDR_MUT;
            registerPointerBorrow(node->name, node->initExpr, node->line,
                                  _is_mut_borrow);
        }

        return;
    }

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(node->type))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        llvm::Type* structType = getStructType(mangledName);
        if(!structType)
        {
            reportError(node->line, "failed to monomorphize struct: " +
                                        genStructRef->structName);
            return;
        }

        llvm::AllocaInst* alloca = createEntryBlockAlloca(
            builder.GetInsertBlock()->getParent(), structType, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                initValue = applyStructCopySemantics(initValue, genStructRef);
                builder.CreateStore(initValue, alloca);
            }
        }
        else
        {
            storeZeroInitializedValue(alloca, structType);
            emitImplicitZeroInitWarning();
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = mangledName;
        registerStructCleanupIfNeeded(node->name, mangledName);
        return;
    }

    // Handle generic list type
    if(auto* genListType = dynamic_cast<GenericListTypeNode*>(node->type))
    {
        listElementTypes[node->name] = genListType->elementType;
        if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(genListType))
        {
            arrayCapacities[node->name] = arrayType->capacity;
            if(auto size = fixedArrayExpressionKnownLength(node->initExpr))
                arrayKnownLengths[node->name] = *size;
            else
                arrayKnownLengths.erase(node->name);
        }

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMType(genListType->elementType->kind), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(listStructType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Type* declElem = getLLVMType(genListType->elementType->kind);
            llvm::Value* initValue = nullptr;
            if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->initExpr))
                initValue = generateListLiteral(listLit, declElem);
            else if(auto* arrFill =
                        dynamic_cast<ArrayFillNode*>(node->initExpr))
                initValue = generateArrayFill(arrFill, declElem);
            else
                initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }
        else
        {
            storeZeroInitializedValue(alloca, listStructType);
            emitImplicitZeroInitWarning();
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_LIST;
        return;
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(node->type))
    {
        mapKeyValueTypes[node->name] =
            std::make_pair(mapType->keyType, mapType->valueType);

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(mapStructType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }
        else
        {
            storeZeroInitializedValue(alloca, mapStructType);
            emitImplicitZeroInitWarning();
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_MAP;
        return;
    }

    // Handle pointer type
    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(node->type))
    {
        pointerElementTypes[node->name] = ptrType->elementType;

        llvm::Type* llvmPtrType = getLLVMTypeFromNode(ptrType);
        if(!llvmPtrType)
            return;

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(llvmPtrType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
            if(auto knownNull = pointerExpressionKnownNull(node->initExpr))
                pointerKnownNull[node->name] = *knownNull;
            else
                pointerKnownNull.erase(node->name);
        }
        else
        {
            storeZeroInitializedValue(alloca, llvmPtrType);
            emitImplicitZeroInitWarning();
            pointerKnownNull[node->name] = true;
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_PTR;
        {
            auto* _borrow_unary = dynamic_cast<UnaryOpNode*>(node->initExpr);
            bool _is_mut_borrow =
                _borrow_unary && _borrow_unary->op == UnaryOpNode::OP_ADDR_MUT;
            registerPointerBorrow(node->name, node->initExpr, node->line,
                                  _is_mut_borrow);
        }
        return;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(node->type))
    {
        // Store element types for tuple access
        std::vector<TypeNode*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            elemTypes.push_back(t);
        }
        tupleElementTypes[node->name] = elemTypes;

        // Create LLVM struct type for tuple based on declared types
        std::vector<llvm::Type*> tupleTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
            {
                reportError(node->line, "invalid type in tuple");
                return;
            }
            tupleTypes.push_back(elemType);
        }
        llvm::StructType* tupleStructType =
            llvm::StructType::get(context, tupleTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(tupleStructType, nullptr, node->name);

        if(node->initExpr)
        {
            // Convert tuple literal elements to match declared types
            if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->initExpr))
            {
                // Build tuple with proper type conversions
                llvm::Value* tupleVal = llvm::UndefValue::get(tupleStructType);

                for(size_t i = 0; i < tupleLit->elements->elements.size() &&
                                  i < tupleType->elementTypes->types.size();
                    ++i)
                {
                    llvm::Value* elemVal =
                        generateExpression(tupleLit->elements->elements[i]);
                    if(!elemVal)
                        return;

                    llvm::Type* targetElemType = tupleTypes[i];
                    llvm::Type* sourceElemType = elemVal->getType();

                    // Convert if needed (only for primitive types)
                    if(sourceElemType != targetElemType)
                    {
                        if(sourceElemType->isIntegerTy() &&
                           targetElemType->isIntegerTy())
                        {
                            unsigned srcBits =
                                sourceElemType->getIntegerBitWidth();
                            unsigned dstBits =
                                targetElemType->getIntegerBitWidth();
                            if(srcBits > dstBits)
                            {
                                elemVal = builder.CreateTrunc(
                                    elemVal, targetElemType, "trunc");
                            }
                            else if(srcBits < dstBits)
                            {
                                elemVal = builder.CreateSExt(
                                    elemVal, targetElemType, "sext");
                            }
                        }
                        else if(sourceElemType->isIntegerTy() &&
                                targetElemType->isFloatingPointTy())
                        {
                            elemVal = builder.CreateSIToFP(
                                elemVal, targetElemType, "sitofp");
                        }
                        else if(sourceElemType->isFloatingPointTy() &&
                                targetElemType->isIntegerTy())
                        {
                            elemVal = builder.CreateFPToSI(
                                elemVal, targetElemType, "fptosi");
                        }
                        else if(sourceElemType->isFloatingPointTy() &&
                                targetElemType->isFloatingPointTy())
                        {
                            elemVal = builder.CreateFPCast(
                                elemVal, targetElemType, "fpcast");
                        }
                        // For struct types and other complex types, no
                        // conversion needed
                    }

                    tupleVal = builder.CreateInsertValue(
                        tupleVal, elemVal, static_cast<unsigned>(i),
                        "tuple.elem");
                }

                builder.CreateStore(tupleVal, alloca);
            }
            else
            {
                // Not a literal, just store
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(initValue)
                {
                    builder.CreateStore(initValue, alloca);
                }
            }
        }
        else
        {
            storeZeroInitializedValue(alloca, tupleStructType);
            emitImplicitZeroInitWarning();
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_TUPLE;
        return;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(node->type))
    {
        std::string resolvedEnumName =
            resolveVisibleEnumName(structRef->structName);
        if(!resolvedEnumName.empty())
        {
            TypeNode::TypeKind baseKind = TypeNode::TYPE_I32;
            auto baseIt = enumBaseTypes.find(resolvedEnumName);
            if(baseIt != enumBaseTypes.end())
                baseKind = baseIt->second;

            llvm::Type* targetType = getLLVMType(baseKind);
            llvm::AllocaInst* alloca =
                builder.CreateAlloca(targetType, nullptr, node->name);

            if(node->initExpr)
            {
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(initValue)
                {
                    if(initValue->getType() != targetType)
                    {
                        if(Helpers::isEnumStringType(baseKind) &&
                           initValue->getType()->isPointerTy())
                        {
                            initValue = builder.CreateBitCast(
                                initValue, targetType, "enum.cast.ptr");
                        }
                        else if(initValue->getType()->isIntegerTy())
                        {
                            initValue = builder.CreateIntCast(
                                initValue, targetType,
                                !Helpers::enumIsUnsigned(baseKind),
                                Helpers::enumIsUnsigned(baseKind) ? "enum.cast.u"
                                                         : "enum.cast.s");
                        }
                        else
                        {
                            reportError(
                                node->line,
                                Helpers::isEnumStringType(baseKind)
                                    ? "enum initializer must be str8"
                                    : "enum initializer must be integer");
                            return;
                        }
                    }
                    builder.CreateStore(initValue, alloca);
                }
            }
            else
            {
                storeZeroInitializedValue(alloca, targetType);
                emitImplicitZeroInitWarning();
            }

            namedValues[node->name] = alloca;
            variableTypes[node->name] = baseKind;
            enumVariableTypes[node->name] = resolvedEnumName;
            return;
        }

        llvm::Type* structType = getStructType(structRef->structName);
        if(!structType)
        {
            reportError(node->line,
                        "unknown struct type: " + structRef->structName);
            return;
        }

        llvm::AllocaInst* alloca = createEntryBlockAlloca(
            builder.GetInsertBlock()->getParent(), structType, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }
        else
        {
            storeZeroInitializedValue(alloca, structType);
            emitImplicitZeroInitWarning();
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = structRef->structName;
        registerStructCleanupIfNeeded(node->name, structRef->structName);
        return;
    }

    llvm::Type* targetType = getLLVMType(node->type->kind);
    llvm::AllocaInst* alloca =
        builder.CreateAlloca(targetType, nullptr, node->name);

    if(node->initExpr)
    {
        llvm::Value* initValue = generateExpression(node->initExpr);
        if(initValue)
        {
            // Convert init value to target type if necessary
            llvm::Type* initType = initValue->getType();
            if(initType != targetType)
            {
                if(initType->isIntegerTy() && targetType->isIntegerTy())
                {
                    unsigned initBits = initType->getIntegerBitWidth();
                    unsigned targetBits = targetType->getIntegerBitWidth();
                    if(initBits > targetBits)
                    {
                        // Truncate (e.g., i64 -> i8)
                        initValue =
                            builder.CreateTrunc(initValue, targetType, "trunc");
                    }
                    else if(initBits < targetBits)
                    {
                        // Extend - use ZExt for unsigned target, SExt for
                        // signed
                        if(isUnsignedType(node->type->kind))
                        {
                            initValue = builder.CreateZExt(initValue,
                                                           targetType, "zext");
                        }
                        else
                        {
                            initValue = builder.CreateSExt(initValue,
                                                           targetType, "sext");
                        }
                    }
                }
                else if(initType->isIntegerTy() &&
                        targetType->isFloatingPointTy())
                {
                    initValue =
                        builder.CreateSIToFP(initValue, targetType, "sitofp");
                }
                else if(initType->isFloatingPointTy() &&
                        targetType->isIntegerTy())
                {
                    initValue =
                        builder.CreateFPToSI(initValue, targetType, "fptosi");
                }
                else if(initType->isFloatingPointTy() &&
                        targetType->isFloatingPointTy())
                {
                    initValue =
                        builder.CreateFPCast(initValue, targetType, "fpcast");
                }
            }
            builder.CreateStore(initValue, alloca);
        }
    }
    else
    {
        storeZeroInitializedValue(alloca, targetType);
        emitImplicitZeroInitWarning();
    }

    namedValues[node->name] = alloca;
    variableTypes[node->name] = node->type->kind;
}

void CodeGenerator::generateAssignment(AssignmentNode* node)
{
    // Check if trying to assign to a constant (let) variable
    if(constantVariables.find(node->name) != constantVariables.end())
    {
        reportError(node->line, "cannot assign to constant variable '" +
                                    node->name + "' (declared with 'let')");
        return;
    }

    llvm::Value* variable = namedValues[node->name];
    if(!variable)
    {
        reportError(node->line, "unknown variable: '" + node->name + "'");
        return;
    }

    llvm::Value* value = generateExpression(node->expression);
    if(!value)
        return;

    auto borIt = activeBorrowers.find(node->name);
    if(borIt != activeBorrowers.end() && !borIt->second.empty())
    {
        std::string by = *borIt->second.begin();
        reportError(node->line, "cannot assign to '" + node->name +
                                    "' while borrowed by '" + by + "'");
        return;
    }
    auto mutBorIt = activeMutBorrower.find(node->name);
    if(mutBorIt != activeMutBorrower.end())
    {
        reportError(node->line, "cannot assign to '" + node->name +
                                    "' directly while mutably borrowed by '" +
                                    mutBorIt->second + "'");
        return;
    }
    consumeMoveFromExpression(node->expression, node->line,
                              "assigning to '" + node->name + "'");

    llvm::Type* targetType = nullptr;
    llvm::AllocaInst* targetAlloca = llvm::dyn_cast<llvm::AllocaInst>(variable);
    llvm::GlobalVariable* targetGlobal =
        llvm::dyn_cast<llvm::GlobalVariable>(variable);
    if(targetAlloca)
        targetType = targetAlloca->getAllocatedType();
    else if(targetGlobal)
        targetType = targetGlobal->getValueType();
    else
    {
        reportError(node->line, "assignment target is not addressable: '" +
                                    node->name + "'");
        return;
    }

    llvm::Type* valueType = value->getType();

    // Convert value to target type if necessary
    if(valueType != targetType)
    {
        bool isEnumAssignment =
            enumVariableTypes.find(node->name) != enumVariableTypes.end();
        TypeNode::TypeKind enumBaseKind = TypeNode::TYPE_I32;
        if(isEnumAssignment)
        {
            std::string enumName =
                resolveVisibleEnumName(enumVariableTypes[node->name]);
            auto baseIt = enumBaseTypes.find(enumName);
            if(baseIt != enumBaseTypes.end())
                enumBaseKind = baseIt->second;
        }

        if(isEnumAssignment && Helpers::isEnumStringType(enumBaseKind) &&
           valueType->isPointerTy() && targetType->isPointerTy())
        {
            value = builder.CreateBitCast(value, targetType, "enum.assign.ptr");
        }
        else if(valueType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned valueBits = valueType->getIntegerBitWidth();
            unsigned targetBits = targetType->getIntegerBitWidth();
            if(valueBits > targetBits)
            {
                value = builder.CreateTrunc(value, targetType, "trunc");
            }
            else if(valueBits < targetBits)
            {
                value = builder.CreateSExt(value, targetType, "sext");
            }
        }
        else if(valueType->isIntegerTy() && targetType->isFloatingPointTy())
        {
            value = builder.CreateSIToFP(value, targetType, "sitofp");
        }
        else if(valueType->isFloatingPointTy() && targetType->isIntegerTy())
        {
            value = builder.CreateFPToSI(value, targetType, "fptosi");
        }
        else if(valueType->isFloatingPointTy() &&
                targetType->isFloatingPointTy())
        {
            value = builder.CreateFPCast(value, targetType, "fpcast");
        }
        else
        {
            reportError(
                node->line,
                (isEnumAssignment && Helpers::isEnumStringType(enumBaseKind))
                    ? "string-backed enum assignment requires str8 value"
                    : "type mismatch in assignment to variable '" + node->name +
                          "'");
            return;
        }
    }

    value = applyStructCopySemantics(value);
    builder.CreateStore(value, variable);
    clearMovedVariable(node->name);
    if(targetType->isPointerTy())
    {
        if(targetGlobal && !validateNoEscapingBorrow(
                               node->expression, node->line,
                               "store in global/static '" + node->name + "'"))
        {
            return;
        }
        {
            auto* _borrow_unary = dynamic_cast<UnaryOpNode*>(node->expression);
            bool _is_mut_borrow =
                _borrow_unary && _borrow_unary->op == UnaryOpNode::OP_ADDR_MUT;
            registerPointerBorrow(node->name, node->expression, node->line,
                                  _is_mut_borrow);
        }
        if(auto knownNull = pointerExpressionKnownNull(node->expression))
            pointerKnownNull[node->name] = *knownNull;
        else
            pointerKnownNull.erase(node->name);
    }
    else
    {
        clearPointerBorrow(node->name);
        pointerKnownNull.erase(node->name);
    }
}

void CodeGenerator::generateFieldAssignment(FieldAssignmentNode* node)
{
    llvm::Value* structPtr;
    std::string structTypeName;
    std::string fieldName;
    std::string ownerName;
    bool targetGlobalStorage = false;

    std::function<bool(ExpressionNode*)> isGlobalBackedStructExpr =
        [&](ExpressionNode* expr) -> bool
    {
        if(!expr)
            return false;
        if(auto* id = dynamic_cast<IdentifierNode*>(expr))
        {
            auto it = namedValues.find(id->name);
            return it != namedValues.end() &&
                   llvm::isa<llvm::GlobalVariable>(it->second);
        }
        if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
        {
            if(field->object)
                return isGlobalBackedStructExpr(field->object);
            auto it = namedValues.find(field->structName);
            return it != namedValues.end() &&
                   llvm::isa<llvm::GlobalVariable>(it->second);
        }
        return false;
    };

    // Handle chained assignment (a.b.c = x) vs simple assignment (a.b = x)
    if(node->target)
    {
        // Chained assignment: target is a FieldAccessNode representing the full
        // path
        auto* fieldAccess = dynamic_cast<FieldAccessNode*>(node->target);
        if(!fieldAccess)
        {
            reportError(node->line, "invalid assignment target");
            return;
        }

        fieldName = fieldAccess->fieldName;
        ownerName = resolveBorrowOwnerFromLValue(fieldAccess);

        // Get the struct pointer for the object part (everything except the
        // last field)
        if(fieldAccess->object)
        {
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, node->line);
            if(!ptr)
                return;
            structPtr = ptr;
            structTypeName = typeName;
            targetGlobalStorage = isGlobalBackedStructExpr(fieldAccess->object);
        }
        else
        {
            // Simple case within chained: the target is like "a.b", so get "a"
            structPtr = namedValues[fieldAccess->structName];
            if(!structPtr)
            {
                reportError(node->line,
                            "unknown variable: " + fieldAccess->structName);
                return;
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(node->line, "variable '" + fieldAccess->structName +
                                            "' is not a struct");
                return;
            }
            structTypeName = typeIt->second;
            targetGlobalStorage = llvm::isa<llvm::GlobalVariable>(structPtr);

            // Handle self pointer
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
            {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if(allocaType->isPointerTy())
                {
                    structPtr = builder.CreateLoad(
                        allocaType, alloca, fieldAccess->structName + ".ptr");
                }
            }
        }
    }
    else
    {
        // Simple assignment: a.b = x
        fieldName = node->fieldName;
        ownerName = node->structName;

        structPtr = namedValues[node->structName];
        if(!structPtr)
        {
            reportError(node->line, "unknown variable: " + node->structName);
            return;
        }

        auto typeIt = structVariableTypes.find(node->structName);
        if(typeIt == structVariableTypes.end())
        {
            reportError(node->line,
                        "variable '" + node->structName + "' is not a struct");
            return;
        }
        structTypeName = typeIt->second;
        targetGlobalStorage = llvm::isa<llvm::GlobalVariable>(structPtr);

        // Handle self pointer
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                structPtr = builder.CreateLoad(allocaType, alloca,
                                               node->structName + ".ptr");
            }
        }
    }

    if(!ownerName.empty())
    {
        auto borIt = activeBorrowers.find(ownerName);
        if(borIt != activeBorrowers.end() && !borIt->second.empty())
        {
            std::set<std::string> allowedBorrowers;
            if(node->target)
            {
                std::function<void(ExpressionNode*)> collectAllowedBorrowers =
                    [&](ExpressionNode* expr) -> void
                {
                    if(!expr)
                        return;

                    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
                    {
                        if(unary->op == UnaryOpNode::OP_DEREF)
                        {
                            if(auto* pid = dynamic_cast<IdentifierNode*>(
                                   unary->operand))
                            {
                                auto pit = pointerBorrowTarget.find(pid->name);
                                if(pit != pointerBorrowTarget.end() &&
                                   pit->second == ownerName)
                                {
                                    allowedBorrowers.insert(pid->name);
                                }
                            }
                        }
                        collectAllowedBorrowers(unary->operand);
                        return;
                    }

                    if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
                    {
                        collectAllowedBorrowers(field->object);
                        return;
                    }

                    if(auto* index = dynamic_cast<IndexExpressionNode*>(expr))
                    {
                        collectAllowedBorrowers(index->base);
                        collectAllowedBorrowers(index->index);
                        return;
                    }

                    if(auto* tuple = dynamic_cast<TupleAccessNode*>(expr))
                    {
                        collectAllowedBorrowers(tuple->tuple);
                        return;
                    }
                };
                collectAllowedBorrowers(node->target);
            }

            for(const auto& by : borIt->second)
            {
                if(allowedBorrowers.find(by) == allowedBorrowers.end())
                {
                    reportError(node->line,
                                "cannot assign to field of '" + ownerName +
                                    "' while borrowed by '" + by + "'");
                    return;
                }
            }
        }
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return;
    }

    // Find field index
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

    if(fieldIndex < 0)
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no field named '" + fieldName + "'");
        return;
    }
    if(!canAccessStructField(structTypeName, fieldIndex, node->line, fieldName))
        return;

    // Get struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
        return;

    // Convert value if needed
    llvm::Type* targetType = getLLVMTypeFromNode(fieldType);
    llvm::Value* value = nullptr;
    if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(fieldType))
    {
        TypeNode* exprType = getLValueType(node->expression, node->line);
        if(dynamic_cast<TraitObjectTypeNode*>(exprType))
        {
            value = generateExpression(node->expression);
            value = coerceTraitObjectValue(value, targetType, node->line);
        }
        else if(exprType)
        {
            value = buildTraitObjectValue(node->expression, traitObj->traitName,
                                          node->line,
                                          /*heapCopy=*/true);
        }
        else
        {
            value = generateExpression(node->expression);
            value = coerceTraitObjectValue(value, targetType, node->line);
        }
        if(!value)
            return;
    }
    else
    {
        value = generateExpression(node->expression);
        if(!value)
            return;
    }
    consumeMoveFromExpression(node->expression, node->line,
                              "assigning to field '" + fieldName + "'");

    if(targetType->isPointerTy() && targetGlobalStorage &&
       !validateNoEscapingBorrow(node->expression, node->line,
                                 "store in global/static field '" + fieldName +
                                     "'"))
    {
        return;
    }
    llvm::Type* valueType = value->getType();

    if(valueType != targetType)
    {
        if(valueType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned srcBits = valueType->getIntegerBitWidth();
            unsigned dstBits = targetType->getIntegerBitWidth();
            if(srcBits > dstBits)
            {
                value = builder.CreateTrunc(value, targetType, "trunc");
            }
            else if(srcBits < dstBits)
            {
                value = builder.CreateSExt(value, targetType, "sext");
            }
        }
        else if(valueType->isFloatingPointTy() &&
                targetType->isFloatingPointTy())
        {
            value = builder.CreateFPCast(value, targetType, "fpcast");
        }
        else if(valueType->isIntegerTy() && targetType->isFloatingPointTy())
        {
            value = builder.CreateSIToFP(value, targetType, "sitofp");
        }
        else if(valueType->isFloatingPointTy() && targetType->isIntegerTy())
        {
            value = builder.CreateFPToSI(value, targetType, "fptosi");
        }
        else
        {
            // Incompatible types
            std::string valueTypeStr, targetTypeStr;

            if(valueType->isIntegerTy())
                valueTypeStr =
                    "i" + std::to_string(valueType->getIntegerBitWidth());
            else if(valueType->isFloatTy())
                valueTypeStr = "f32";
            else if(valueType->isDoubleTy())
                valueTypeStr = "f64";
            else if(valueType->isPointerTy())
                valueTypeStr = "pointer";
            else if(valueType->isStructTy())
                valueTypeStr = valueType->getStructName().str().empty()
                                   ? "struct"
                                   : valueType->getStructName().str();
            else
                valueTypeStr = "unknown";

            if(targetType->isIntegerTy())
                targetTypeStr =
                    "i" + std::to_string(targetType->getIntegerBitWidth());
            else if(targetType->isFloatTy())
                targetTypeStr = "f32";
            else if(targetType->isDoubleTy())
                targetTypeStr = "f64";
            else if(targetType->isPointerTy())
                targetTypeStr = "pointer";
            else if(targetType->isStructTy())
                targetTypeStr = targetType->getStructName().str().empty()
                                    ? "struct"
                                    : targetType->getStructName().str();
            else
                targetTypeStr = "unknown";

            reportError(node->line, "type mismatch in assignment to field '" +
                                        fieldName + "': expected '" +
                                        targetTypeStr + "', got '" +
                                        valueTypeStr + "'");
            return;
        }
    }

    storeStructFieldValue(structTypeName, structPtr, fieldIndex, fieldType,
                          value, fieldName, node->line);
}

void CodeGenerator::generateDerefAssignment(DerefAssignmentNode* node)
{
    if(!validatePointerDereference(node->pointerExpr, node->line))
        return;

    llvm::Value* ptrVal = generateExpression(node->pointerExpr);
    if(!ptrVal)
        return;

    if(!ptrVal->getType()->isPointerTy())
    {
        reportError(node->line, "dereference requires a pointer value");
        return;
    }
    if(!emitRuntimeNullPointerCheck(ptrVal, node->line))
        return;

    TypeNode* elemTypeNode =
        getPointerElementType(node->pointerExpr, node->line);
    if(!elemTypeNode)
        return;

    llvm::Type* elemType = getLLVMTypeFromNode(elemTypeNode);
    if(!elemType)
        return;

    llvm::Value* value = generateExpression(node->value);
    if(!value)
        return;
    consumeMoveFromExpression(node->value, node->line,
                              "assigning through dereference");

    llvm::Type* valueType = value->getType();
    if(valueType != elemType)
    {
        if(valueType->isIntegerTy() && elemType->isIntegerTy())
        {
            unsigned valueBits = valueType->getIntegerBitWidth();
            unsigned targetBits = elemType->getIntegerBitWidth();
            if(valueBits > targetBits)
            {
                value = builder.CreateTrunc(value, elemType, "trunc");
            }
            else if(valueBits < targetBits)
            {
                value = builder.CreateSExt(value, elemType, "sext");
            }
        }
        else if(valueType->isIntegerTy() && elemType->isFloatingPointTy())
        {
            value = builder.CreateSIToFP(value, elemType, "sitofp");
        }
        else if(valueType->isFloatingPointTy() && elemType->isIntegerTy())
        {
            value = builder.CreateFPToSI(value, elemType, "fptosi");
        }
        else if(valueType->isFloatingPointTy() && elemType->isFloatingPointTy())
        {
            value = builder.CreateFPCast(value, elemType, "fpcast");
        }
        else
        {
            reportError(node->line, "type mismatch in deref assignment");
            return;
        }
    }

    builder.CreateStore(value, ptrVal);
}

// Helper to get struct pointer and type name from an expression
// Returns {pointer, typeName} or {nullptr, ""} on error
std::pair<llvm::Value*, std::string>
CodeGenerator::getStructPtrAndType(ExpressionNode* expr, int line)
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

    // Case 1: Simple identifier (e.g., "myStruct")
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        if(!validateVariableAccessible(id->name, line, id->col))
            return {nullptr, ""};

        llvm::Value* ptr = namedValues[id->name];
        if(!ptr)
        {
            reportError(line, "unknown variable: " + id->name);
            return {nullptr, ""};
        }

        auto typeIt = structVariableTypes.find(id->name);
        std::string inferredStructName;
        if(typeIt == structVariableTypes.end())
        {
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr))
            {
                llvm::Type* at = alloca->getAllocatedType();
                if(at && at->isStructTy())
                {
                    auto* st = llvm::cast<llvm::StructType>(at);
                    inferredStructName = st->getName().str();
                }
            }
            if(inferredStructName.empty())
            {
                reportError(line,
                            "variable '" + id->name + "' is not a struct");
                return {nullptr, ""};
            }
        }

        // Handle self pointer (alloca containing pointer)
        llvm::Value* actualPtr = ptr;
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                actualPtr =
                    builder.CreateLoad(allocaType, alloca, id->name + ".ptr");
            }
        }

        std::string structName = typeIt != structVariableTypes.end()
                                     ? typeIt->second
                                     : inferredStructName;
        return {actualPtr, resolveStructAliasName(structName)};
    }

    // Case 2: Field access (e.g., "a.b" in "a.b.c")
    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(expr))
    {
        // Recursively get the struct pointer for the object
        llvm::Value* objPtr;
        std::string objTypeName;

        if(fieldAccess->object)
        {
            // Chained: get pointer from the object expression
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, line);
            if(!ptr)
                return {nullptr, ""};
            objPtr = ptr;
            objTypeName = typeName;
        }
        else
        {
            // Simple: get pointer from structName
            if(!validateVariableAccessible(fieldAccess->structName, line,
                                           fieldAccess->col))
                return {nullptr, ""};
            objPtr = namedValues[fieldAccess->structName];
            if(!objPtr)
            {
                reportError(line,
                            "unknown variable: " + fieldAccess->structName);
                return {nullptr, ""};
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + fieldAccess->structName +
                                      "' is not a struct");
                return {nullptr, ""};
            }
            objTypeName = resolveStructAliasName(typeIt->second);

            // Handle self pointer
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(objPtr))
            {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if(allocaType->isPointerTy())
                {
                    objPtr = builder.CreateLoad(
                        allocaType, alloca, fieldAccess->structName + ".ptr");
                }
            }
        }

        // Now access the field
        auto memberIt = structMembers.find(objTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(line, "unknown struct type: " + objTypeName);
            return {nullptr, ""};
        }

        // Find field index and type
        int fieldIndex = -1;
        TypeNode* fieldType = nullptr;
        const auto& members = memberIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldAccess->fieldName)
            {
                fieldIndex = static_cast<int>(i);
                fieldType = members[i].second;
                break;
            }
        }

        if(fieldIndex < 0)
        {
            reportError(line, "struct '" + objTypeName +
                                  "' has no field named '" +
                                  fieldAccess->fieldName + "'");
            return {nullptr, ""};
        }
        if(!canAccessStructField(objTypeName, fieldIndex, line,
                                 fieldAccess->fieldName))
            return {nullptr, ""};

        // Check if the field is a struct type
        if(fieldType->kind == TypeNode::TYPE_STRUCT)
        {
            auto* structTypeRef = dynamic_cast<StructTypeRefNode*>(fieldType);
            if(!structTypeRef)
            {
                reportError(line, "internal error: expected StructTypeRefNode");
                return {nullptr, ""};
            }

            llvm::StructType* structType = getStructType(objTypeName);
            if(!structType)
                return {nullptr, ""};
            const StructFieldLayout* layout =
                getStructFieldLayout(objTypeName, fieldIndex);
            if(!layout)
                return {nullptr, ""};

            // Get pointer to the nested struct field
            llvm::Value* fieldPtr = builder.CreateStructGEP(
                structType, objPtr, layout->storageIndex,
                fieldAccess->fieldName + "_ptr");

            return {fieldPtr, structTypeRef->structName};
        }
        else
        {
            reportError(line, "field '" + fieldAccess->fieldName +
                                  "' is not a struct type");
            return {nullptr, ""};
        }
    }

    // Case 3: Dereference of a pointer to struct (e.g., "*p")
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_DEREF)
        {
            if(!validatePointerDereference(unary->operand, line))
                return {nullptr, ""};

            llvm::Value* ptrVal = generateExpression(unary->operand);
            if(!ptrVal)
                return {nullptr, ""};
            if(!ptrVal->getType()->isPointerTy())
            {
                reportError(line, "dereference requires a pointer value");
                return {nullptr, ""};
            }

            TypeNode* elemType = getPointerElementType(unary->operand, line);
            if(!elemType)
                return {nullptr, ""};
            if(elemType->kind != TypeNode::TYPE_STRUCT)
            {
                reportError(line, "dereference does not yield a struct value");
                return {nullptr, ""};
            }

            if(auto* structRef = dynamic_cast<StructTypeRefNode*>(elemType))
            {
                return {ptrVal, structRef->structName};
            }
            if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(elemType))
            {
                std::string mangled = getOrCreateMonomorphizedStruct(
                    genRef->structName, genRef->typeArgs);
                return {ptrVal, mangled};
            }
        }
    }

    // Case 4: Temporary expression producing a struct value
    if(TypeNode* exprType = getLValueType(expr, line))
    {
        if(exprType->kind == TypeNode::TYPE_STRUCT)
        {
            llvm::Value* value = generateExpression(expr);
            if(!value)
                return {nullptr, ""};

            std::string structTypeName;
            if(auto* structRef = dynamic_cast<StructTypeRefNode*>(exprType))
            {
                structTypeName = structRef->structName;
            }
            else if(auto* genRef =
                        dynamic_cast<GenericStructTypeRefNode*>(exprType))
            {
                structTypeName = getOrCreateMonomorphizedStruct(
                    genRef->structName, genRef->typeArgs);
            }
            if(structTypeName.empty())
            {
                reportError(line,
                            "internal error: expected struct type reference");
                return {nullptr, ""};
            }

            llvm::AllocaInst* tmp =
                builder.CreateAlloca(value->getType(), nullptr, "field.tmp");
            value = applyStructCopySemantics(value);
            builder.CreateStore(value, tmp);
            return {tmp, structTypeName};
        }
    }

    reportError(line, "invalid expression for field access");
    return {nullptr, ""};
}

llvm::Value* CodeGenerator::generateFieldAccess(FieldAccessNode* node)
{
    if(node->fieldName == "name")
    {
        bool hasRealNameField = false;
        if(node->object)
        {
            TypeNode* objType = getLValueType(node->object, node->line);
            if(objType && objType->kind == TypeNode::TYPE_STRUCT)
            {
                std::string objStructTypeName;
                if(auto* structRef = dynamic_cast<StructTypeRefNode*>(objType))
                {
                    objStructTypeName = structRef->structName;
                }
                else if(auto* genRef =
                            dynamic_cast<GenericStructTypeRefNode*>(objType))
                {
                    objStructTypeName = getOrCreateMonomorphizedStruct(
                        genRef->structName, genRef->typeArgs);
                }
                hasRealNameField =
                    structHasFieldNamed(objStructTypeName, "name");
            }
        }
        else
        {
            auto structIt = structVariableTypes.find(node->structName);
            if(structIt != structVariableTypes.end())
            {
                hasRealNameField =
                    structHasFieldNamed(structIt->second, "name");
            }
        }

        if(!hasRealNameField)
        {
            std::string typeName;
            if(node->object)
            {
                typeName = expressionTypeNameForLog(node->object, node->line);
            }
            else
            {
                IdentifierNode tmp(node->structName);
                tmp.line = node->line;
                typeName = expressionTypeNameForLog(&tmp, node->line);
            }
            return Helpers::create_global_cstring(builder, typeName, "type.name");
        }
    }

    llvm::Value* structPtr;
    std::string structTypeName;

    // Handle chained access (a.b.c) vs simple access (a.b)
    if(node->object)
    {
        // Chained access: evaluate the object expression first
        auto [ptr, typeName] = getStructPtrAndType(node->object, node->line);
        if(!ptr)
            return nullptr;
        structPtr = ptr;
        structTypeName = typeName;
    }
    else
    {
        // Simple access: get from structName
        if(!validateVariableAccessible(node->structName, node->line, node->col))
            return nullptr;
        structPtr = namedValues[node->structName];
        if(!structPtr)
        {
            reportError(node->line, "unknown variable: " + node->structName);
            return nullptr;
        }

        auto typeIt = structVariableTypes.find(node->structName);
        if(typeIt == structVariableTypes.end())
        {
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
            {
                llvm::Type* at = alloca->getAllocatedType();
                if(at && at->isStructTy())
                {
                    auto* st = llvm::cast<llvm::StructType>(at);
                    structTypeName = st->getName().str();
                }
            }
            if(structTypeName.empty())
            {
                reportError(node->line, "variable '" + node->structName +
                                            "' is not a struct");
                return nullptr;
            }
        }
        else
        {
            structTypeName = typeIt->second;
        }

        // Handle self pointer (alloca containing pointer)
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                structPtr = builder.CreateLoad(allocaType, alloca,
                                               node->structName + ".ptr");
            }
        }
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return nullptr;
    }

    // Find field index
    int fieldIndex = -1;
    TypeNode* fieldType = nullptr;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        if(members[i].first == node->fieldName)
        {
            fieldIndex = static_cast<int>(i);
            fieldType = members[i].second;
            break;
        }
    }

    if(fieldIndex < 0)
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no field named '" + node->fieldName +
                                    "'");
        return nullptr;
    }
    if(!canAccessStructField(structTypeName, fieldIndex, node->line,
                             node->fieldName))
        return nullptr;

    return loadStructFieldValue(structTypeName, structPtr, fieldIndex,
                                fieldType, node->fieldName);
}

llvm::Value* CodeGenerator::generateFunctionCall(FunctionCallNode* node)
{
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
            auto savedPointerElementTypes = pointerElementTypes;

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
                pointerElementTypes = savedPointerElementTypes;
            };

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

            enterCleanupScope();
            if(closure->body)
            {
                for(auto* stmt : closure->body->statements)
                {
                    generateStatement(stmt);
                    if(builder.GetInsertBlock() &&
                       builder.GetInsertBlock()->getTerminator())
                        break;
                }
            }
            exitCleanupScope();

            loopBreakBlocks = std::move(savedBreak);
            loopContinueBlocks = std::move(savedContinue);
            restoreInlineState();
            activeInlineClosures.erase(node->name);
            return nullptr; // inline closures return void
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
               dynamic_cast<TupleTypeNode*>(type) != nullptr;
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
                                                         node->name, ""))
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

    if(!validateTemporaryBorrowArguments(node->arguments, node->name, ""))
        return nullptr;

    const bool isFreeLikeCall =
        isFreeLikeFunctionName(node->name) && node->arguments.size() == 1;
    if(isFreeLikeCall &&
       !precheckFreeLikeArgument(node->arguments[0], node->name))
    {
        return nullptr;
    }

    std::vector<llvm::Value*> argVals;
    argVals.reserve(node->arguments.size());
    for(auto arg : node->arguments)
    {
        llvm::Value* argVal = generateExpression(arg);
        if(!argVal)
            return nullptr;
        // &s / &mut s where s is a string: generateExpression(OP_ADDR) returns
        // the alloca (char**). Load through to get the actual char* so the
        // callee receives the string pointer, not the stack address.
        if(auto* unary = dynamic_cast<UnaryOpNode*>(arg))
        {
            if(unary->op == UnaryOpNode::OP_ADDR ||
               unary->op == UnaryOpNode::OP_ADDR_MUT)
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
           !Helpers::is_same_module_family(qualifiedModuleFilter, info.sourceModule))
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
        size_t expectedArgs = callee->arg_size();
        size_t actualArgs = argVals.size();
        bool isVarArg = callee->isVarArg();

        if(!isVarArg && expectedArgs != actualArgs)
            continue;
        if(isVarArg && actualArgs < expectedArgs)
            continue;

        int totalCost = 0;
        bool ok = true;
        for(size_t i = 0; i < expectedArgs; ++i)
        {
            if(info.node && info.node->parameters &&
               i < info.node->parameters->parameters.size())
            {
                TypeNode* expectedSemantic =
                    info.node->parameters->parameters[i]->type;
                if(auto* refType =
                       dynamic_cast<ReferenceTypeNode*>(expectedSemantic))
                {
                    expectedSemantic = refType->elementType;
                }

                TypeNode* actualSemantic =
                    semanticArgumentType(node->arguments[i]);
                if(auto* traitObj =
                       dynamic_cast<TraitObjectTypeNode*>(expectedSemantic))
                {
                    if(auto* actualTraitObj =
                           dynamic_cast<TraitObjectTypeNode*>(actualSemantic))
                    {
                        if(Helpers::trait_names_equivalent(actualTraitObj->traitName,
                                                  traitObj->traitName))
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
                if((isCompositeSemanticType(expectedSemantic) ||
                    isCompositeSemanticType(actualSemantic)) &&
                   actualSemantic &&
                   typeMangle(actualSemantic) != typeMangle(expectedSemantic))
                {
                    ok = false;
                    break;
                }
            }

            int cost = 0;
            if(!canConvertType(argVals[i]->getType(),
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

    llvm::Function* callee = best->function;
    size_t expectedArgs = callee->arg_size();
    bool isVarArg = callee->isVarArg();

    if(isVarArg && argVals.size() < expectedArgs)
    {
        reportError(node->line,
                    "function '" + node->name + "' requires at least " +
                        std::to_string(expectedArgs) + " argument(s), but " +
                        std::to_string(argVals.size()) + " provided");
        return nullptr;
    }

    std::vector<llvm::Value*> args;
    unsigned paramIdx = 0;
    for(auto* argValIn : argVals)
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

    if(isFreeLikeCall)
        markFreeLikeConsumed(node->arguments[0]);

    return callResult;
}

llvm::Function* CodeGenerator::generateClosureFn(ClosureNode* node)
{
    if(node->parameters && !node->parameters->parameters.empty())
    {
        reportError(node->line,
                    "thread_spawn closure does not support parameters");
        return nullptr;
    }

    static int closureSeq = 0;
    std::string closureName = "__mlang_closure_" + std::to_string(closureSeq++);

    llvm::FunctionType* closureFnType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {}, false);
    llvm::Function* closureFn =
        llvm::Function::Create(closureFnType, llvm::Function::PrivateLinkage,
                               closureName, module.get());

    // Save current codegen state so we can restore it after the closure body
    auto savedIP = builder.saveIP();
    auto savedNamedValues = namedValues;
    auto savedConstantVars = constantVariables;
    auto savedMovedVars = movedVariables;
    auto savedPtrBorrowTarget = pointerBorrowTarget;
    auto savedActiveBorrowers = activeBorrowers;
    auto savedActiveMutBorrow = activeMutBorrower;
    auto savedVarScopeDepth = variableScopeDepth;
    auto savedVarTypes = variableTypes;
    auto savedStructVarTypes = structVariableTypes;
    auto savedEnumVarTypes = enumVariableTypes;
    auto savedCleanupScopes = cleanupScopes;
    auto savedPtrBorrowScopes = pointerBorrowScopes;
    auto savedVarDepthScopes = variableScopeDepthScopes;
    auto savedLoopBreak = loopBreakBlocks;
    auto savedLoopContinue = loopContinueBlocks;
    auto savedModule = currentModule;
    auto savedClosureVars = closureVariables;
    auto savedActiveInline = activeInlineClosures;

    // Initialise a fresh scope for the closure body
    namedValues.clear();
    constantVariables.clear();
    movedVariables.clear();
    closureVariables.clear();
    activeInlineClosures.clear();
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
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    loopBreakBlocks.clear();
    loopContinueBlocks.clear();
    currentModule = "";

    llvm::BasicBlock* entry =
        llvm::BasicBlock::Create(context, "entry", closureFn);
    builder.SetInsertPoint(entry);

    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    if(node->body)
    {
        for(auto* stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock() &&
               builder.GetInsertBlock()->getTerminator())
                break;
        }
    }

    exitCleanupScope();

    if(!builder.GetInsertBlock()->getTerminator())
        builder.CreateRetVoid();

    // Restore caller state
    namedValues = std::move(savedNamedValues);
    constantVariables = std::move(savedConstantVars);
    movedVariables = std::move(savedMovedVars);
    pointerBorrowTarget = std::move(savedPtrBorrowTarget);
    activeBorrowers = std::move(savedActiveBorrowers);
    activeMutBorrower = std::move(savedActiveMutBorrow);
    variableScopeDepth = std::move(savedVarScopeDepth);
    variableTypes = std::move(savedVarTypes);
    structVariableTypes = std::move(savedStructVarTypes);
    enumVariableTypes = std::move(savedEnumVarTypes);
    cleanupScopes = std::move(savedCleanupScopes);
    pointerBorrowScopes = std::move(savedPtrBorrowScopes);
    variableScopeDepthScopes = std::move(savedVarDepthScopes);
    loopBreakBlocks = std::move(savedLoopBreak);
    loopContinueBlocks = std::move(savedLoopContinue);
    currentModule = std::move(savedModule);
    closureVariables = std::move(savedClosureVars);
    activeInlineClosures = std::move(savedActiveInline);
    builder.restoreIP(savedIP);

    return closureFn;
}

llvm::Value* CodeGenerator::generateThreadSpawn(FunctionCallNode* node)
{
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Thread"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    return buildHandleValue(handleTypeName, rawHandle, node->line);
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Thread"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
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
    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    return buildHandleValue(handleTypeName, rawHandle, node->line);
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
        llvm::MaybeAlign(), llvm::AtomicOrdering::SequentiallyConsistent,
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
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
    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    return buildHandleValue(handleTypeName, rawHandle, node->line);
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
    ptr = builder.CreateBitCast(ptr, int64PtrType, "atomic.ptr_i64");
#endif
    auto* loadInst = builder.CreateLoad(int64Type, ptr, "atomic.load");
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
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
        llvm::AtomicRMWInst::Add, ptr, addVal, llvm::MaybeAlign(),
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

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
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
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
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
        if(typeName == "Result")
            return true;
        auto it = mangledToGenericName.find(typeName);
        return it != mangledToGenericName.end() && it->second == "Result";
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
                              "Result.unwrap() may panic on Err; consider "
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
                      "Result.unwrap() may panic on Err; consider "
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

llvm::Value* CodeGenerator::generateCastExpression(CastExpressionNode* node)
{
    llvm::Value* value = generateExpression(node->expression);
    if(!value)
        return nullptr;

    TypeNode* sourceTypeNode =
        inferExpressionTypeNode(node->expression, node->line);

    if(node->targetType == TypeNode::TYPE_BIT)
    {
        llvm::Type* bitType = llvm::Type::getInt1Ty(context);
        if(value->getType()->isIntegerTy(1))
            return value;

        if(!value->getType()->isIntegerTy())
        {
            reportError(node->line,
                        "bit cast expects an integer or bool value");
            return nullptr;
        }

        if(auto* ci = llvm::dyn_cast<llvm::ConstantInt>(value))
        {
            const uint64_t raw = ci->getZExtValue();
            if(raw > 1u)
            {
                reportError(node->line,
                            "bit cast expects integer value 0 or 1");
                return nullptr;
            }
        }

        return builder.CreateICmpNE(
            value, llvm::ConstantInt::get(value->getType(), 0), "bitcast");
    }

    llvm::Type* targetType = getLLVMType(node->targetType);
    llvm::Type* sourceType = value->getType();

    if(sourceType == targetType)
        return value;

    if(sourceType->isIntegerTy() && targetType->isIntegerTy())
    {
        bool treatAsUnsigned =
            sourceType->isIntegerTy(1) ||
            (sourceTypeNode && isUnsignedType(sourceTypeNode->kind));
        return builder.CreateIntCast(value, targetType, !treatAsUnsigned,
                                     treatAsUnsigned ? "zextcast" : "sextcast");
    }

    // Integer to float/double
    if(sourceType->isIntegerTy())
    {
        if(targetType->isFloatTy() || targetType->isDoubleTy())
        {
            return builder.CreateSIToFP(value, targetType, "casttmp");
        }
    }

    // Float/double to integer
    if(sourceType->isFloatingPointTy())
    {
        if(targetType->isIntegerTy())
        {
            return builder.CreateFPToSI(value, targetType, "casttmp");
        }
        // Float to double or double to float
        if(targetType->isFloatingPointTy())
        {
            return builder.CreateFPCast(value, targetType, "casttmp");
        }
    }

    return value;
}

llvm::Value* CodeGenerator::generateListLiteral(ListLiteralNode* node,
                                                llvm::Type* declaredElemType)
{
    initializeStdlibFunctions();

    // List structure: { i64 size, ptr data }
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    if(!node->elements || node->elements->elements.empty())
    {
        // Empty list: return {0, null}
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
        listStruct = builder.CreateInsertValue(
            listStruct, llvm::ConstantInt::get(i64Type, 0), 0);
        listStruct = builder.CreateInsertValue(
            listStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            1);
        return listStruct;
    }

    // Generate all elements
    std::vector<llvm::Value*> elementValues;
    // Preferred element type: use declared type if provided, else infer from
    // first
    llvm::Type* elementType = declaredElemType;

    for(auto* elem : node->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
            return nullptr;
        if(!elementType)
        {
            elementType = val->getType();
        }
        // Coerce element to match the target element type (e.g., truncate i64 →
        // i32)
        if(val->getType() != elementType)
        {
            if(val->getType()->isIntegerTy() && elementType->isIntegerTy())
            {
                unsigned valBits = val->getType()->getIntegerBitWidth();
                unsigned tgtBits = elementType->getIntegerBitWidth();
                if(valBits > tgtBits)
                    val = builder.CreateTrunc(val, elementType, "elem.trunc");
                else
                    val = builder.CreateSExt(val, elementType, "elem.ext");
            }
        }
        elementValues.push_back(val);
    }

    int64_t listSize = static_cast<int64_t>(elementValues.size());

    // Allocate heap storage for elements. List/array mutation grows this
    // buffer with realloc, so stack-backed alloca storage would be invalid.
    llvm::Value* arraySizeVal = llvm::ConstantInt::get(i64Type, listSize);
    uint64_t elemSizeU =
        module->getDataLayout().getTypeAllocSize(elementType);
    llvm::Value* elemSize = llvm::ConstantInt::get(i64Type, elemSizeU);
    llvm::Value* byteSize =
        builder.CreateMul(arraySizeVal, elemSize, "list.bytes");
    llvm::Value* dataAlloc =
        builder.CreateCall(mallocFunc, {byteSize}, "listdata");

    // Store each element
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        llvm::Value* idx = llvm::ConstantInt::get(i64Type, i);
        llvm::Value* elemPtr =
            builder.CreateGEP(elementType, dataAlloc, idx, "elemptr");
        builder.CreateStore(elementValues[i], elemPtr);
    }

    // Create list struct
    std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
    llvm::StructType* listStructType =
        llvm::StructType::get(context, listStructTypes);

    llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
    listStruct = builder.CreateInsertValue(
        listStruct, llvm::ConstantInt::get(i64Type, listSize), 0);
    listStruct = builder.CreateInsertValue(listStruct, dataAlloc, 1);

    return listStruct;
}

llvm::Value* CodeGenerator::generateArrayFill(ArrayFillNode* node,
                                              llvm::Type* declaredElemType)
{
    initializeStdlibFunctions();

    // [val; N] — a list of N copies of val
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    llvm::Value* fillVal = generateExpression(node->value);
    llvm::Value* countVal = generateExpression(node->count);
    if(!fillVal || !countVal)
        return nullptr;

    // Coerce fill value to the declared element type (e.g., i64 literal → i32)
    llvm::Type* elemType =
        declaredElemType ? declaredElemType : fillVal->getType();
    if(fillVal->getType() != elemType)
    {
        if(fillVal->getType()->isIntegerTy() && elemType->isIntegerTy())
        {
            unsigned srcBits = fillVal->getType()->getIntegerBitWidth();
            unsigned dstBits = elemType->getIntegerBitWidth();
            if(srcBits > dstBits)
                fillVal = builder.CreateTrunc(fillVal, elemType, "fill.trunc");
            else
                fillVal = builder.CreateSExt(fillVal, elemType, "fill.ext");
        }
    }

    // Extend count to i64
    if(countVal->getType() != i64Type)
        countVal = builder.CreateSExt(countVal, i64Type, "fill.count");

    // Allocate heap storage for count elements. Filled lists/arrays can escape
    // or later grow with realloc, so stack-backed storage would be invalid.
    uint64_t elemSizeU = module->getDataLayout().getTypeAllocSize(elemType);
    llvm::Value* elemSize = llvm::ConstantInt::get(i64Type, elemSizeU);
    llvm::Value* byteSize =
        builder.CreateMul(countVal, elemSize, "fill.bytes");
    llvm::Value* dataAlloc =
        builder.CreateCall(mallocFunc, {byteSize}, "filldata");

    // Loop to store the fill value at each index
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::AllocaInst* idxAlloca = builder.CreateAlloca(i64Type, nullptr, "fi");
    builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), idxAlloca);

    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "fill.cond", function);
    llvm::BasicBlock* bodyBB =
        llvm::BasicBlock::Create(context, "fill.body", function);
    llvm::BasicBlock* endBB =
        llvm::BasicBlock::Create(context, "fill.end", function);

    builder.CreateBr(condBB);

    builder.SetInsertPoint(condBB);
    llvm::Value* idx = builder.CreateLoad(i64Type, idxAlloca, "fi");
    llvm::Value* cmp = builder.CreateICmpSLT(idx, countVal, "fill.cond");
    builder.CreateCondBr(cmp, bodyBB, endBB);

    builder.SetInsertPoint(bodyBB);
    llvm::Value* elemPtr =
        builder.CreateGEP(elemType, dataAlloc, idx, "fill.ptr");
    builder.CreateStore(fillVal, elemPtr);
    llvm::Value* nextIdx =
        builder.CreateAdd(idx, llvm::ConstantInt::get(i64Type, 1), "fi.next");
    builder.CreateStore(nextIdx, idxAlloca);
    builder.CreateBr(condBB);

    builder.SetInsertPoint(endBB);

    // Build list struct { size, data }
    std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
    llvm::StructType* listStructType =
        llvm::StructType::get(context, listStructTypes);

    llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
    listStruct = builder.CreateInsertValue(listStruct, countVal, 0);
    listStruct = builder.CreateInsertValue(listStruct, dataAlloc, 1);
    return listStruct;
}

llvm::Value* CodeGenerator::generateMapLiteral(MapLiteralNode* node,
                                               llvm::Type* declaredKeyType,
                                               llvm::Type* declaredValueType)
{
    initializeStdlibFunctions();

    // Map structure: { i64 size, ptr keys, ptr values }
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
    llvm::StructType* mapStructType =
        llvm::StructType::get(context, mapStructTypes);

    if(!node->entries || node->entries->entries.empty())
    {
        // Empty map
        llvm::Value* mapStruct = llvm::UndefValue::get(mapStructType);
        mapStruct = builder.CreateInsertValue(
            mapStruct, llvm::ConstantInt::get(i64Type, 0), 0);
        mapStruct = builder.CreateInsertValue(
            mapStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            1);
        mapStruct = builder.CreateInsertValue(
            mapStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            2);
        return mapStruct;
    }

    // Generate all key-value pairs
    std::vector<llvm::Value*> keyValues;
    std::vector<llvm::Value*> valueValues;
    llvm::Type* keyType = declaredKeyType;
    llvm::Type* valueType = declaredValueType;

    auto coerceMapLiteralValue =
        [&](llvm::Value* value, llvm::Type* targetType,
            const char* label) -> llvm::Value*
    {
        if(!value || !targetType || value->getType() == targetType)
            return value;
        llvm::Type* actualType = value->getType();
        if(actualType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned actualBits = actualType->getIntegerBitWidth();
            unsigned targetBits = targetType->getIntegerBitWidth();
            if(actualBits > targetBits)
                return builder.CreateTrunc(value, targetType,
                                           std::string(label) + ".trunc");
            if(actualBits < targetBits)
                return builder.CreateSExt(value, targetType,
                                          std::string(label) + ".ext");
            return value;
        }
        if(actualType->isFloatingPointTy() && targetType->isFloatingPointTy())
            return builder.CreateFPCast(value, targetType,
                                        std::string(label) + ".fpcast");
        if(actualType->isIntegerTy() && targetType->isFloatingPointTy())
            return builder.CreateSIToFP(value, targetType,
                                        std::string(label) + ".sitofp");
        if(actualType->isFloatingPointTy() && targetType->isIntegerTy())
            return builder.CreateFPToSI(value, targetType,
                                        std::string(label) + ".fptosi");

        reportError(node->line,
                    std::string("map literal ") + label +
                        " type does not match declared map type");
        return nullptr;
    };

    for(auto* entry : node->entries->entries)
    {
        llvm::Value* keyVal = generateExpression(entry->key);
        llvm::Value* valVal = generateExpression(entry->value);
        if(!keyVal || !valVal)
            return nullptr;

        if(!keyType)
        {
            keyType = keyVal->getType();
            valueType = valVal->getType();
        }
        keyVal = coerceMapLiteralValue(keyVal, keyType, "key");
        valVal = coerceMapLiteralValue(valVal, valueType, "value");
        if(!keyVal || !valVal)
            return nullptr;

        keyValues.push_back(keyVal);
        valueValues.push_back(valVal);
    }

    int64_t mapSize = static_cast<int64_t>(keyValues.size());

    // Allocate heap storage for keys and values. Map values can escape the
    // current stack frame, so stack-backed alloca arrays would dangle.
    llvm::Value* sizeVal = llvm::ConstantInt::get(i64Type, mapSize);
    uint64_t keySizeU = module->getDataLayout().getTypeAllocSize(keyType);
    uint64_t valueSizeU =
        module->getDataLayout().getTypeAllocSize(valueType);
    llvm::Value* keyBytes = builder.CreateMul(
        sizeVal, llvm::ConstantInt::get(i64Type, keySizeU), "map.key.bytes");
    llvm::Value* valueBytes =
        builder.CreateMul(sizeVal,
                          llvm::ConstantInt::get(i64Type, valueSizeU),
                          "map.value.bytes");
    llvm::Value* keysAlloc =
        builder.CreateCall(mallocFunc, {keyBytes}, "mapkeys");
    llvm::Value* valsAlloc =
        builder.CreateCall(mallocFunc, {valueBytes}, "mapvals");

    // Store each key-value pair
    for(size_t i = 0; i < keyValues.size(); ++i)
    {
        llvm::Value* idx = llvm::ConstantInt::get(i64Type, i);
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysAlloc, idx, "keyptr");
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsAlloc, idx, "valptr");
        builder.CreateStore(keyValues[i], keyPtr);
        builder.CreateStore(valueValues[i], valPtr);
    }

    // Create map struct
    llvm::Value* mapStruct = llvm::UndefValue::get(mapStructType);
    mapStruct = builder.CreateInsertValue(
        mapStruct, llvm::ConstantInt::get(i64Type, mapSize), 0);
    mapStruct = builder.CreateInsertValue(mapStruct, keysAlloc, 1);
    mapStruct = builder.CreateInsertValue(mapStruct, valsAlloc, 2);

    return mapStruct;
}

llvm::Value* CodeGenerator::generateIndexExpression(IndexExpressionNode* node)
{
    if(contains_update_expression(node->index))
    {
        reportError(node->line,
                    "index expression does not allow pre/post ++/--; update "
                    "the index in a separate statement to avoid off-by-one "
                    "and bounds bugs");
        return nullptr;
    }

    auto* baseId = dynamic_cast<IdentifierNode*>(node->base);
    TypeNode* baseType = getLValueType(node->base, node->line);
    llvm::Value* basePtr = getLValuePointer(node->base, node->line);
    if(!basePtr)
        return nullptr;

    llvm::Value* indexVal = generateExpression(node->index);
    if(!indexVal)
        return nullptr;

    auto* listType = dynamic_cast<GenericListTypeNode*>(baseType);
    if(listType)
    {
        auto* arrayType = dynamic_cast<ArrayTypeNode*>(baseType);
        std::optional<int64_t> arrayCapacity;
        std::optional<int64_t> knownArrayLength;
        if(arrayType)
            arrayCapacity = arrayType->capacity;
        if(baseId)
        {
            auto arrayCapIt = arrayCapacities.find(baseId->name);
            if(arrayCapIt != arrayCapacities.end())
                arrayCapacity = arrayCapIt->second;
            auto knownLenIt = arrayKnownLengths.find(baseId->name);
            if(knownLenIt != arrayKnownLengths.end())
                knownArrayLength = knownLenIt->second;
        }

        if(arrayCapacity)
        {
            int64_t knownIndex = 0;
            if(evaluateCompileTimeInt(node->index, knownIndex))
            {
                if(knownIndex < 0)
                {
                    reportError(node->line,
                                "array index out of bounds: index=" +
                                    std::to_string(knownIndex) +
                                    " capacity=" +
                                    std::to_string(*arrayCapacity));
                    return nullptr;
                }
                if(knownArrayLength && knownIndex >= *knownArrayLength)
                {
                    reportError(node->line,
                                "array index out of bounds: index=" +
                                    std::to_string(knownIndex) +
                                    " len=" +
                                    std::to_string(*knownArrayLength) +
                                    " capacity=" +
                                    std::to_string(*arrayCapacity));
                    return nullptr;
                }
                if(knownIndex >= *arrayCapacity)
                {
                    reportError(node->line,
                                "array index out of bounds: index=" +
                                    std::to_string(knownIndex) +
                                    " capacity=" +
                                    std::to_string(*arrayCapacity));
                    return nullptr;
                }
            }
        }

        initializeFormatFunctions();

        // List indexing
        TypeNode* elemTypeNode = listType->elementType;
        llvm::Type* elementType = getLLVMTypeFromNode(elemTypeNode);
        if(!elementType)
        {
            reportError(node->line,
                        "cannot index list with unresolved element type '" +
                            Helpers::type_name_for_error(elemTypeNode) + "'");
            return nullptr;
        }

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(elementType, 0);
#endif

        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        // Load list struct
        llvm::Value* listStruct =
            builder.CreateLoad(listStructType, basePtr, "list");
        llvm::Value* listSize =
            builder.CreateExtractValue(listStruct, 0, "size");
        llvm::Value* dataPtr =
            builder.CreateExtractValue(listStruct, 1, "data");

        // Ensure index is i64
        if(indexVal->getType() != i64Type)
        {
            indexVal = builder.CreateSExtOrTrunc(indexVal, i64Type, "idx64");
        }

        llvm::Function* function = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* okBB =
            llvm::BasicBlock::Create(context, "index.ok", function);
        llvm::BasicBlock* failBB =
            llvm::BasicBlock::Create(context, "index.fail", function);

        llvm::Value* nonNegative = builder.CreateICmpSGE(
            indexVal, llvm::ConstantInt::get(i64Type, 0), "index.nonneg");
        llvm::Value* withinUpper =
            builder.CreateICmpSLT(indexVal, listSize, "index.upper");
        llvm::Value* inBounds =
            builder.CreateAnd(nonNegative, withinUpper, "index.inbounds");
        builder.CreateCondBr(inBounds, okBB, failBB);

        builder.SetInsertPoint(failBB);
#if LLVM_VERSION_MAJOR >= 21
        llvm::Value* formatStr = builder.CreateGlobalString(
            "list/span index out of bounds\n", "index.bounds.msg");
#else
        llvm::Value* formatStr = builder.CreateGlobalStringPtr(
            "list/span index out of bounds\n", "index.bounds.msg");
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

        // Get element pointer and load
        llvm::Value* elemPtr =
            builder.CreateGEP(elementType, dataPtr, indexVal, "elemptr");
        return builder.CreateLoad(elementType, elemPtr, "elem");
    }

    // Check if it's a map
    TypeNode* mapKeyTypeNode = nullptr;
    TypeNode* mapValueTypeNode = nullptr;
    if(auto* mapType = dynamic_cast<MapTypeNode*>(baseType))
    {
        mapKeyTypeNode = mapType->keyType;
        mapValueTypeNode = mapType->valueType;
    }
    else if(baseId)
    {
        auto mapIt = mapKeyValueTypes.find(baseId->name);
        if(mapIt != mapKeyValueTypes.end())
        {
            mapKeyTypeNode = mapIt->second.first;
            mapValueTypeNode = mapIt->second.second;
        }
    }
    if(mapKeyTypeNode && mapValueTypeNode)
    {
        // Map lookup - linear search for key
        TypeNode* keyTypeNode = mapKeyTypeNode;
        TypeNode* valTypeNode = mapValueTypeNode;
        llvm::Type* keyType = getLLVMTypeFromNode(keyTypeNode);
        llvm::Type* valueType = getLLVMTypeFromNode(valTypeNode);
        if(!keyType || !valueType)
        {
            reportError(node->line,
                        "cannot index map with unresolved key/value type '" +
                            Helpers::type_name_for_error(keyTypeNode) + "'/'" +
                            Helpers::type_name_for_error(valTypeNode) + "'");
            return nullptr;
        }
        if(indexVal->getType() != keyType)
        {
            llvm::Type* indexType = indexVal->getType();
            if(indexType->isIntegerTy() && keyType->isIntegerTy())
                indexVal = builder.CreateSExtOrTrunc(indexVal, keyType,
                                                     "map.key.cast");
            else if(indexType->isFloatingPointTy() &&
                    keyType->isFloatingPointTy())
                indexVal =
                    builder.CreateFPCast(indexVal, keyType, "map.key.cast");
            else if(indexType->isIntegerTy() && keyType->isFloatingPointTy())
                indexVal =
                    builder.CreateSIToFP(indexVal, keyType, "map.key.cast");
            else if(indexType->isFloatingPointTy() && keyType->isIntegerTy())
                indexVal =
                    builder.CreateFPToSI(indexVal, keyType, "map.key.cast");
        }

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(keyType, 0);
#endif

        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        // Load map struct
        llvm::Value* mapStruct =
            builder.CreateLoad(mapStructType, basePtr, "map");
        llvm::Value* mapSize = builder.CreateExtractValue(mapStruct, 0, "size");
        llvm::Value* keysPtr = builder.CreateExtractValue(mapStruct, 1, "keys");
        llvm::Value* valsPtr = builder.CreateExtractValue(mapStruct, 2, "vals");

        // Linear search loop for key
        llvm::Function* function = builder.GetInsertBlock()->getParent();

        llvm::AllocaInst* idxVar =
            builder.CreateAlloca(i64Type, nullptr, "mapidx");
        builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), idxVar);

        llvm::AllocaInst* resultVar =
            builder.CreateAlloca(valueType, nullptr, "mapresult");
        // Initialize with default value
        builder.CreateStore(llvm::Constant::getNullValue(valueType), resultVar);
        llvm::Type* i1Type = llvm::Type::getInt1Ty(context);
        llvm::AllocaInst* foundVar =
            builder.CreateAlloca(i1Type, nullptr, "mapfound");
        builder.CreateStore(llvm::ConstantInt::getFalse(context), foundVar);

        llvm::BasicBlock* condBB =
            llvm::BasicBlock::Create(context, "map.cond", function);
        llvm::BasicBlock* bodyBB =
            llvm::BasicBlock::Create(context, "map.body");
        llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "map.inc");
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "map.end");

        builder.CreateBr(condBB);

        builder.SetInsertPoint(condBB);
        llvm::Value* currentIdx = builder.CreateLoad(i64Type, idxVar, "idx");
        llvm::Value* cond =
            builder.CreateICmpSLT(currentIdx, mapSize, "mapcond");
        builder.CreateCondBr(cond, bodyBB, endBB);

        bodyBB->insertInto(function);
        builder.SetInsertPoint(bodyBB);

        // Compare keys
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysPtr, currentIdx, "keyptr");
        llvm::Value* currentKey = builder.CreateLoad(keyType, keyPtr, "curkey");

        llvm::Value* keyMatch;
        if(keyType->isIntegerTy())
        {
            keyMatch = builder.CreateICmpEQ(currentKey, indexVal, "keymatch");
        }
        else if(keyType->isFloatingPointTy())
        {
            keyMatch = builder.CreateFCmpOEQ(currentKey, indexVal, "keymatch");
        }
        else
        {
            // For strings/pointers, need strcmp or pointer comparison
            keyMatch = builder.CreateICmpEQ(currentKey, indexVal, "keymatch");
        }

        llvm::BasicBlock* foundBB =
            llvm::BasicBlock::Create(context, "map.found", function);
        builder.CreateCondBr(keyMatch, foundBB, incBB);

        builder.SetInsertPoint(foundBB);
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsPtr, currentIdx, "valptr");
        llvm::Value* foundVal =
            builder.CreateLoad(valueType, valPtr, "foundval");
        builder.CreateStore(foundVal, resultVar);
        builder.CreateStore(llvm::ConstantInt::getTrue(context), foundVar);
        builder.CreateBr(endBB);

        incBB->insertInto(function);
        builder.SetInsertPoint(incBB);
        llvm::Value* nextIdx =
            builder.CreateAdd(builder.CreateLoad(i64Type, idxVar, ""),
                              llvm::ConstantInt::get(i64Type, 1), "nextidx");
        builder.CreateStore(nextIdx, idxVar);
        builder.CreateBr(condBB);

        endBB->insertInto(function);
        builder.SetInsertPoint(endBB);

        initializeFormatFunctions();
        llvm::BasicBlock* okBB =
            llvm::BasicBlock::Create(context, "map.lookup.ok", function);
        llvm::BasicBlock* missingBB =
            llvm::BasicBlock::Create(context, "map.lookup.missing", function);
        llvm::Value* found =
            builder.CreateLoad(i1Type, foundVar, "map.found");
        builder.CreateCondBr(found, okBB, missingBB);

        builder.SetInsertPoint(missingBB);
#if LLVM_VERSION_MAJOR >= 21
        llvm::Value* formatStr = builder.CreateGlobalString(
            "map key not found\n", "map.lookup.missing.msg");
#else
        llvm::Value* formatStr = builder.CreateGlobalStringPtr(
            "map key not found\n", "map.lookup.missing.msg");
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
        return builder.CreateLoad(valueType, resultVar, "mapval");
    }

    std::string baseName = baseId ? baseId->name : "expression";
    reportError(node->line,
                "cannot index non-list/non-map variable: " + baseName);
    return nullptr;
}

llvm::Value* CodeGenerator::generateTupleLiteral(TupleLiteralNode* node)
{
    if(!node->elements || node->elements->elements.empty())
    {
        reportError(node->line, "tuple must have at least one element");
        return nullptr;
    }

    // Generate all elements and determine their types
    std::vector<llvm::Value*> elementValues;
    std::vector<llvm::Type*> elementTypes;

    for(auto* elem : node->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
            return nullptr;
        elementValues.push_back(val);
        elementTypes.push_back(val->getType());
    }

    // Create the tuple struct type
    llvm::StructType* tupleType = llvm::StructType::get(context, elementTypes);

    // Create the tuple value
    llvm::Value* tupleVal = llvm::UndefValue::get(tupleType);
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        tupleVal = builder.CreateInsertValue(
            tupleVal, elementValues[i], static_cast<unsigned>(i), "tuple.elem");
    }

    return tupleVal;
}

llvm::Value* CodeGenerator::generateTupleAccess(TupleAccessNode* node)
{
    // Get the tuple variable
    auto* baseId = dynamic_cast<IdentifierNode*>(node->tuple);
    if(!baseId)
    {
        reportError(node->line, "tuple access requires an identifier");
        return nullptr;
    }

    llvm::Value* tuplePtr = namedValues[baseId->name];
    if(!tuplePtr)
    {
        reportError(node->line, "unknown variable: " + baseId->name);
        return nullptr;
    }

    // Check if it's a tuple
    auto it = tupleElementTypes.find(baseId->name);
    if(it == tupleElementTypes.end())
    {
        reportError(node->line, "cannot access tuple element: '" +
                                    baseId->name + "' is not a tuple");
        return nullptr;
    }

    const std::vector<TypeNode*>& elemTypes = it->second;

    // Bounds check
    if(node->index < 0 || static_cast<size_t>(node->index) >= elemTypes.size())
    {
        reportError(node->line, "tuple index " + std::to_string(node->index) +
                                    " out of bounds (tuple has " +
                                    std::to_string(elemTypes.size()) +
                                    " elements)");
        return nullptr;
    }

    // Build the tuple struct type using getLLVMTypeFromNode for proper struct
    // support
    std::vector<llvm::Type*> tupleTypes;
    for(auto* t : elemTypes)
    {
        llvm::Type* elemType = getLLVMTypeFromNode(t);
        if(!elemType)
        {
            reportError(node->line, "invalid type in tuple");
            return nullptr;
        }
        tupleTypes.push_back(elemType);
    }
    llvm::StructType* tupleStructType =
        llvm::StructType::get(context, tupleTypes);

    // Load the tuple struct
    llvm::Value* tupleVal =
        builder.CreateLoad(tupleStructType, tuplePtr, "tuple");

    // Extract the element
    return builder.CreateExtractValue(
        tupleVal, static_cast<unsigned>(node->index), "tuple.elem");
}

llvm::Value* CodeGenerator::generateStructLiteral(StructLiteralNode* node)
{
    std::string structTypeName = node->structName;

    // Check if this is a generic struct instantiation (has type arguments)
    if(node->structName == "Result" && node->typeArgs.empty())
    {
        reportError(node->line, "Result literals require type arguments (e.g. "
                                "Ok<i32, str8>(...))");
        return nullptr;
    }
    if(node->structName == "Option" && node->typeArgs.empty())
    {
        reportError(node->line, "Option literals require type arguments (e.g. "
                                "Some<i32>(...))");
        return nullptr;
    }
    if(!node->typeArgs.empty())
        if(!node->typeArgs.empty())
        {
            // Convert typeArgs from strings to TypeNodes
            // The typeArgs in StructLiteralNode are stored as strings from the
            // parser We need to look them up and create proper TypeNode
            // references
            std::vector<TypeNode*> typeArgNodes;
            for(const auto& typeArgStr : node->typeArgs)
            {
                // Try to create a TypeNode from the type argument string
                TypeNode* typeArg = nullptr;

                auto bindIt = activeTypeParamBindings.find(typeArgStr);
                if(bindIt != activeTypeParamBindings.end() && bindIt->second)
                {
                    typeArgNodes.push_back(cloneTypeNode(bindIt->second));
                    continue;
                }

                // Check if it's a basic type
                if(typeArgStr == "i8")
                    typeArg = new TypeNode(TypeNode::TYPE_I8);
                else if(typeArgStr == "i16")
                    typeArg = new TypeNode(TypeNode::TYPE_I16);
                else if(typeArgStr == "i32")
                    typeArg = new TypeNode(TypeNode::TYPE_I32);
                else if(typeArgStr == "i64")
                    typeArg = new TypeNode(TypeNode::TYPE_I64);
                else if(typeArgStr == "u8")
                    typeArg = new TypeNode(TypeNode::TYPE_U8);
                else if(typeArgStr == "u16")
                    typeArg = new TypeNode(TypeNode::TYPE_U16);
                else if(typeArgStr == "u32")
                    typeArg = new TypeNode(TypeNode::TYPE_U32);
                else if(typeArgStr == "u64")
                    typeArg = new TypeNode(TypeNode::TYPE_U64);
                else if(typeArgStr == "f32")
                    typeArg = new TypeNode(TypeNode::TYPE_FLOAT);
                else if(typeArgStr == "f64")
                    typeArg = new TypeNode(TypeNode::TYPE_DOUBLE);
                else if(typeArgStr == "bool")
                    typeArg = new TypeNode(TypeNode::TYPE_BOOL);
                else if(typeArgStr == "str8")
                    typeArg = new TypeNode(TypeNode::TYPE_STR8);
                else if(typeArgStr == "str16")
                    typeArg = new TypeNode(TypeNode::TYPE_STR16);
                else
                {
                    // Assume it's a struct type reference
                    typeArg = new StructTypeRefNode(typeArgStr);
                }

                typeArgNodes.push_back(typeArg);
            }

            // Get or create the monomorphized struct type
            structTypeName =
                getOrCreateMonomorphizedStruct(node->structName, typeArgNodes);
        }

    // Get the struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return nullptr;
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "no member info for struct: " + structTypeName);
        return nullptr;
    }
    const auto& members = memberIt->second;

    auto convertStructLiteralFieldValue =
        [&](llvm::Value* fieldValue, llvm::Type* expectedType,
            TypeNode* expectedSemanticType, ExpressionNode* valueExpr,
            const std::string& fullFieldName) -> llvm::Value*
    {
        if(auto* traitObj =
               dynamic_cast<TraitObjectTypeNode*>(expectedSemanticType))
        {
            llvm::Value* traitValue = nullptr;
            TypeNode* exprType =
                valueExpr ? getLValueType(valueExpr, node->line) : nullptr;
            if(dynamic_cast<TraitObjectTypeNode*>(exprType))
            {
                traitValue =
                    fieldValue ? fieldValue : generateExpression(valueExpr);
                return coerceTraitObjectValue(traitValue, expectedType,
                                              node->line);
            }
            if(exprType)
                return buildTraitObjectValue(valueExpr, traitObj->traitName,
                                             node->line,
                                             /*heapCopy=*/true);

            traitValue =
                fieldValue ? fieldValue : generateExpression(valueExpr);
            return coerceTraitObjectValue(traitValue, expectedType, node->line);
        }

        if(!fieldValue)
            return nullptr;

        llvm::Type* actualType = fieldValue->getType();
        if(actualType == expectedType)
            return fieldValue;

        if(actualType->isIntegerTy() && expectedType->isIntegerTy())
        {
            unsigned actualBits = actualType->getIntegerBitWidth();
            unsigned expectedBits = expectedType->getIntegerBitWidth();
            if(actualBits > expectedBits)
                return builder.CreateTrunc(fieldValue, expectedType, "trunc");
            if(actualBits < expectedBits)
                return builder.CreateSExt(fieldValue, expectedType, "sext");
            return fieldValue;
        }

        if(actualType->isFloatingPointTy() && expectedType->isFloatingPointTy())
            return builder.CreateFPCast(fieldValue, expectedType, "fpcast");

        if(actualType->isIntegerTy() && expectedType->isFloatingPointTy())
            return builder.CreateSIToFP(fieldValue, expectedType, "sitofp");

        if(actualType->isFloatingPointTy() && expectedType->isIntegerTy())
            return builder.CreateFPToSI(fieldValue, expectedType, "fptosi");

        std::string actualTypeStr;
        std::string expectedTypeStr;

        if(actualType->isIntegerTy())
            actualTypeStr =
                "i" + std::to_string(actualType->getIntegerBitWidth());
        else if(actualType->isFloatTy())
            actualTypeStr = "f32";
        else if(actualType->isDoubleTy())
            actualTypeStr = "f64";
        else if(actualType->isPointerTy())
            actualTypeStr = "pointer";
        else if(actualType->isStructTy())
            actualTypeStr = actualType->getStructName().str().empty()
                                ? "struct"
                                : actualType->getStructName().str();
        else
            actualTypeStr = "unknown";

        if(expectedType->isIntegerTy())
            expectedTypeStr =
                "i" + std::to_string(expectedType->getIntegerBitWidth());
        else if(expectedType->isFloatTy())
            expectedTypeStr = "f32";
        else if(expectedType->isDoubleTy())
            expectedTypeStr = "f64";
        else if(expectedType->isPointerTy())
            expectedTypeStr = "pointer";
        else if(expectedType->isStructTy())
            expectedTypeStr = expectedType->getStructName().str().empty()
                                  ? "struct"
                                  : expectedType->getStructName().str();
        else
            expectedTypeStr = "unknown";

        reportError(node->line, "type mismatch for field '" + fullFieldName +
                                    "' in struct '" + structTypeName +
                                    "': expected '" + expectedTypeStr +
                                    "', got '" + actualTypeStr + "'");
        return nullptr;
    };

    std::function<std::string(TypeNode*)> getNestedStructTypeName =
        [&](TypeNode* type) -> std::string
    {
        if(!type)
            return "";

        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
            return structRef->structName;

        if(auto* genericRef = dynamic_cast<GenericStructTypeRefNode*>(type))
            return getOrCreateMonomorphizedStruct(genericRef->structName,
                                                  genericRef->typeArgs);

        return "";
    };

    std::function<llvm::Value*(
        const std::string&, llvm::StructType*,
        const std::vector<std::pair<std::string, TypeNode*>>&, llvm::Value*,
        const std::vector<std::string>&, size_t, ExpressionNode*,
        const std::string&, bool)>
        applyNestedFieldInit =
            [&](const std::string& currentStructName,
                llvm::StructType* currentStructType,
                const std::vector<std::pair<std::string, TypeNode*>>&
                    currentMembers,
                llvm::Value* currentStructVal,
                const std::vector<std::string>& fieldParts, size_t partIndex,
                ExpressionNode* valueExpr, const std::string& fullFieldName,
                bool enforceAccess) -> llvm::Value*
    {
        if(partIndex >= fieldParts.size())
            return currentStructVal;

        int memberIndex = -1;
        for(size_t i = 0; i < currentMembers.size(); ++i)
        {
            if(currentMembers[i].first == fieldParts[partIndex])
            {
                memberIndex = static_cast<int>(i);
                break;
            }
        }

        if(memberIndex < 0)
        {
            reportError(node->line, "unknown field '" + fieldParts[partIndex] +
                                        "' in struct '" + currentStructName +
                                        "'");
            return nullptr;
        }
        if(enforceAccess &&
           !canAccessStructField(currentStructName, memberIndex, node->line,
                                 fieldParts[partIndex]))
        {
            return nullptr;
        }

        const StructFieldLayout* layout =
            getStructFieldLayout(currentStructName, memberIndex);
        if(!layout)
        {
            reportError(node->line,
                        "missing field layout for '" + fieldParts[partIndex] +
                            "' in struct '" + currentStructName + "'");
            return nullptr;
        }

        if(partIndex + 1 == fieldParts.size())
        {
            llvm::Value* fieldValue = nullptr;
            if(auto* nestedLit = dynamic_cast<StructLiteralNode*>(valueExpr))
            {
                if(nestedLit->structName.empty())
                {
                    std::string expectedStructName = getNestedStructTypeName(
                        currentMembers[memberIndex].second);
                    if(expectedStructName.empty())
                    {
                        reportError(node->line,
                                    "field '" + fullFieldName +
                                        "' in struct '" + currentStructName +
                                        "' does not accept an anonymous object "
                                        "literal");
                        return nullptr;
                    }

                    StructLiteralNode contextual(expectedStructName);
                    contextual.line = nestedLit->line;
                    contextual.fields = nestedLit->fields;
                    contextual.typeArgs = nestedLit->typeArgs;
                    fieldValue = generateStructLiteral(&contextual);
                }
            }

            if(!fieldValue)
            {
                if(auto* expectedList =
                       dynamic_cast<GenericListTypeNode*>(
                           currentMembers[memberIndex].second))
                {
                    if(auto* expectedArray =
                           dynamic_cast<ArrayTypeNode*>(
                               currentMembers[memberIndex].second))
                    {
                        if(auto size = fixedArrayInitializerSize(valueExpr))
                        {
                            if(*size < 0)
                            {
                                reportError(
                                    node->line,
                                    "array initializer size must be "
                                    "non-negative");
                                return nullptr;
                            }
                            if(*size > expectedArray->capacity)
                            {
                                reportError(
                                    node->line,
                                    "array initializer for field '" +
                                        fullFieldName + "' has " +
                                        std::to_string(*size) +
                                        " elements but " +
                                        expectedArray->toString() +
                                        " capacity is " +
                                        std::to_string(
                                            expectedArray->capacity));
                                return nullptr;
                            }
                        }
                    }
                    llvm::Type* expectedElemType =
                        getLLVMTypeFromNode(expectedList->elementType);
                    if(auto* listLit =
                           dynamic_cast<ListLiteralNode*>(valueExpr))
                        fieldValue =
                            generateListLiteral(listLit, expectedElemType);
                    else if(auto* arrFill =
                                dynamic_cast<ArrayFillNode*>(valueExpr))
                        fieldValue =
                            generateArrayFill(arrFill, expectedElemType);
                }
                else if(auto* expectedMap =
                            dynamic_cast<MapTypeNode*>(
                                currentMembers[memberIndex].second))
                {
                    llvm::Type* expectedKeyType =
                        getLLVMTypeFromNode(expectedMap->keyType);
                    llvm::Type* expectedValueType =
                        getLLVMTypeFromNode(expectedMap->valueType);
                    if(auto* mapLit =
                           dynamic_cast<MapLiteralNode*>(valueExpr))
                    {
                        fieldValue = generateMapLiteral(
                            mapLit, expectedKeyType, expectedValueType);
                    }
                }
            }

            if(!fieldValue)
                fieldValue = generateExpression(valueExpr);
            if(!fieldValue)
            {
                reportError(node->line, "failed to generate value for field '" +
                                            fullFieldName + "'");
                return nullptr;
            }

            llvm::Type* expectedType =
                layout->packedBit
                    ? llvm::Type::getInt1Ty(context)
                    : currentStructType->getElementType(layout->storageIndex);
            fieldValue = convertStructLiteralFieldValue(
                fieldValue, expectedType, currentMembers[memberIndex].second,
                valueExpr, fullFieldName);
            if(!fieldValue)
                return nullptr;

            if(layout->packedBit)
            {
                llvm::Type* storageType =
                    currentStructType->getElementType(layout->storageIndex);
                llvm::Value* currentStorage = builder.CreateExtractValue(
                    currentStructVal, {layout->storageIndex},
                    currentStructName + "." + fullFieldName + ".storage");
                llvm::Value* zextValue = builder.CreateZExt(
                    fieldValue, storageType,
                    currentStructName + "." + fullFieldName + ".zext");
                llvm::Value* shifted = builder.CreateShl(
                    zextValue,
                    llvm::ConstantInt::get(storageType, layout->bitOffset),
                    currentStructName + "." + fullFieldName + ".shift");
                llvm::Value* mask = llvm::ConstantInt::get(
                    storageType, static_cast<uint64_t>(1u)
                                     << layout->bitOffset);
                llvm::Value* cleared = builder.CreateAnd(
                    currentStorage, builder.CreateNot(mask),
                    currentStructName + "." + fullFieldName + ".clear");
                llvm::Value* combined = builder.CreateOr(
                    cleared, shifted,
                    currentStructName + "." + fullFieldName + ".combine");
                return builder.CreateInsertValue(
                    currentStructVal, combined, {layout->storageIndex},
                    currentStructName + "." + fullFieldName);
            }

            fieldValue = applyStructCopySemantics(
                fieldValue, currentMembers[memberIndex].second);
            return builder.CreateInsertValue(
                currentStructVal, fieldValue, {layout->storageIndex},
                currentStructName + "." + fullFieldName);
        }

        if(layout->packedBit)
        {
            reportError(node->line, "field '" + fieldParts[partIndex] +
                                        "' in struct '" + currentStructName +
                                        "' is not a nested struct");
            return nullptr;
        }

        std::string nestedStructName =
            getNestedStructTypeName(currentMembers[memberIndex].second);
        if(nestedStructName.empty())
        {
            reportError(node->line, "field '" + fieldParts[partIndex] +
                                        "' in struct '" + currentStructName +
                                        "' is not a nested struct");
            return nullptr;
        }

        auto nestedMembersIt = structMembers.find(nestedStructName);
        if(nestedMembersIt == structMembers.end())
        {
            reportError(node->line,
                        "no member info for struct: " + nestedStructName);
            return nullptr;
        }

        llvm::Type* storageType =
            currentStructType->getElementType(layout->storageIndex);
        auto* nestedStructType = llvm::dyn_cast<llvm::StructType>(storageType);
        if(!nestedStructType)
        {
            reportError(node->line, "field '" + fieldParts[partIndex] +
                                        "' in struct '" + currentStructName +
                                        "' is not stored as a struct");
            return nullptr;
        }

        llvm::Value* nestedValue = builder.CreateExtractValue(
            currentStructVal, {layout->storageIndex},
            currentStructName + "." + fieldParts[partIndex] + ".extract");
        nestedValue = applyNestedFieldInit(nestedStructName, nestedStructType,
                                           nestedMembersIt->second, nestedValue,
                                           fieldParts, partIndex + 1, valueExpr,
                                           fullFieldName, enforceAccess);
        if(!nestedValue)
            return nullptr;

        nestedValue = applyStructCopySemantics(
            nestedValue, currentMembers[memberIndex].second);
        return builder.CreateInsertValue(
            currentStructVal, nestedValue, {layout->storageIndex},
            currentStructName + "." + fieldParts[partIndex]);
    };

    // Build the struct value
    llvm::Value* structVal = llvm::Constant::getNullValue(structType);

    // Apply per-member defaults (from `var x: T{expr};` or
    // `let x: T = expr;` field declarations). Explicit field initializers in
    // the literal below will overwrite anything set here.
    {
        auto defaultsIt = structMemberDefaults.find(structTypeName);
        if(defaultsIt != structMemberDefaults.end())
        {
            for(const auto& kv : defaultsIt->second)
            {
                structVal = applyNestedFieldInit(structTypeName, structType,
                                                 members, structVal, {kv.first},
                                                 0, kv.second, kv.first, false);
                if(!structVal)
                    return nullptr;
            }
        }
    }

    // Process each field initialization
    for(const auto& fieldInit : node->fields)
    {
        const std::string& fieldName = fieldInit.first;
        ExpressionNode* valueExpr = fieldInit.second;
        std::vector<std::string> fieldParts;
        size_t start = 0;
        while(start <= fieldName.size())
        {
            size_t dot = fieldName.find('.', start);
            if(dot == std::string::npos)
            {
                fieldParts.push_back(fieldName.substr(start));
                break;
            }
            fieldParts.push_back(fieldName.substr(start, dot - start));
            start = dot + 1;
        }

        if(fieldParts.empty())
        {
            reportError(node->line, "invalid field path in struct literal");
            return nullptr;
        }

        structVal =
            applyNestedFieldInit(structTypeName, structType, members, structVal,
                                 fieldParts, 0, valueExpr, fieldName, true);
        if(!structVal)
            return nullptr;
    }

    return structVal;
}

llvm::Value* CodeGenerator::generateMatchExpression(MatchExpressionNode* node)
{
    if(!node || !node->target)
        return nullptr;

    llvm::Value* matchVal = generateExpression(node->target);
    if(!matchVal)
        return nullptr;
    auto incomingMoved = movedVariables;
    auto incomingPointerBorrowTarget = pointerBorrowTarget;
    auto incomingActiveBorrowers = activeBorrowers;
    auto incomingActiveMutBorrower = activeMutBorrower;

    bool hasOk = false;
    bool hasErr = false;
    bool hasLiteral = false;
    bool hasSome = false;
    bool hasNone = false;
    MatchArmNode* okArm = nullptr;
    MatchArmNode* errArm = nullptr;
    MatchArmNode* someArm = nullptr;
    MatchArmNode* noneArm = nullptr;
    MatchArmNode* wildcardArm = nullptr;
    std::vector<MatchArmNode*> literalArms;
    for(auto* arm : node->arms)
    {
        if(!arm || !arm->pattern)
            continue;
        switch(arm->pattern->kind)
        {
        case MatchPatternNode::PATTERN_OK:
            hasOk = true;
            if(okArm)
            {
                reportError(node->line, "duplicate Ok match arm");
                return nullptr;
            }
            okArm = arm;
            break;
        case MatchPatternNode::PATTERN_ERR:
            hasErr = true;
            if(errArm)
            {
                reportError(node->line, "duplicate Err match arm");
                return nullptr;
            }
            errArm = arm;
            break;
        case MatchPatternNode::PATTERN_SOME:
            hasSome = true;
            if(someArm)
            {
                reportError(node->line, "duplicate Some match arm");
                return nullptr;
            }
            someArm = arm;
            break;
        case MatchPatternNode::PATTERN_NONE:
            hasNone = true;
            if(noneArm)
            {
                reportError(node->line, "duplicate None match arm");
                return nullptr;
            }
            noneArm = arm;
            break;
        case MatchPatternNode::PATTERN_LITERAL:
            hasLiteral = true;
            literalArms.push_back(arm);
            break;
        case MatchPatternNode::PATTERN_WILDCARD:
            if(wildcardArm)
            {
                reportError(node->line, "duplicate wildcard match arm");
                return nullptr;
            }
            wildcardArm = arm;
            break;
        }
    }

    bool hasResult = hasOk || hasErr;
    bool hasOption = hasSome || hasNone;
    if(hasResult && (hasLiteral || hasOption))
    {
        reportError(node->line, "match cannot mix Result and other patterns");
        return nullptr;
    }
    if(hasOption && hasLiteral)
    {
        reportError(node->line, "match cannot mix Option and literal patterns");
        return nullptr;
    }

    auto bindValue =
        [&](const std::string& name, TypeNode* type, llvm::Value* value)
    {
        if(name.empty() || !type || !value)
            return;

        llvm::Type* llvmType = getLLVMTypeFromNode(type);
        if(!llvmType)
            return;

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(llvmType, nullptr, name);
        builder.CreateStore(value, alloca);
        namedValues[name] = alloca;
        recordVariableScopeDepth(name);

        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
        {
            std::string resolvedEnumName =
                resolveVisibleEnumName(structRef->structName);
            if(!resolvedEnumName.empty())
            {
                variableTypes[name] = TypeNode::TYPE_INT;
                enumVariableTypes[name] = resolvedEnumName;
            }
            else
            {
                variableTypes[name] = TypeNode::TYPE_STRUCT;
                structVariableTypes[name] = structRef->structName;
            }
        }
        else if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_STRUCT;
            structVariableTypes[name] = getOrCreateMonomorphizedStruct(
                genRef->structName, genRef->typeArgs);
        }
        else if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_LIST;
            listElementTypes[name] = listType->elementType;
        }
        else if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_MAP;
            mapKeyValueTypes[name] =
                std::make_pair(mapType->keyType, mapType->valueType);
        }
        else if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_TUPLE;
            std::vector<TypeNode*> elemTypes;
            for(auto* t : tupleType->elementTypes->types)
                elemTypes.push_back(t);
            tupleElementTypes[name] = elemTypes;
        }
        else if(auto* ptrType = dynamic_cast<PointerTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_PTR;
            pointerElementTypes[name] = ptrType->elementType;
        }
        else
        {
            variableTypes[name] = type->kind;
        }
    };

    auto generateArmValue =
        [&](MatchArmNode* arm, int valueIndex, TypeNode* valueType,
            std::set<std::string>* outMovedState,
            std::map<std::string, std::string>* outPointerBorrowTarget,
            std::map<std::string, std::set<std::string>>* outActiveBorrowers)
        -> llvm::Value*
    {
        auto savedNamedValues = namedValues;
        auto savedVariableTypes = variableTypes;
        auto savedStructVariableTypes = structVariableTypes;
        auto savedTraitObjectVariableTypes = traitObjectVariableTypes;
        auto savedEnumVariableTypes = enumVariableTypes;
        auto savedListElementTypes = listElementTypes;
        auto savedMapKeyValueTypes = mapKeyValueTypes;
        auto savedTupleElementTypes = tupleElementTypes;
        auto savedPointerElementTypes = pointerElementTypes;
        auto savedMovedVariables = movedVariables;
        auto savedPointerBorrowTarget = pointerBorrowTarget;
        auto savedActiveBorrowers = activeBorrowers;
        auto savedActiveMutBorrower = activeMutBorrower;
        auto savedCleanupScopes = cleanupScopes;
        auto savedVariableScopeDepth = variableScopeDepth;
        auto savedVariableScopeDepthScopes = variableScopeDepthScopes;

        std::string binding = arm && arm->pattern ? arm->pattern->binding : "";
        if(!binding.empty())
        {
            llvm::Value* payload =
                builder.CreateExtractValue(matchVal, valueIndex, "match.val");
            bindValue(binding, valueType, payload);
        }

        llvm::Value* armValue =
            arm ? generateExpression(arm->expression) : nullptr;
        if(outMovedState)
            *outMovedState = movedVariables;
        if(outPointerBorrowTarget)
            *outPointerBorrowTarget = pointerBorrowTarget;
        if(outActiveBorrowers)
            *outActiveBorrowers = activeBorrowers;

        namedValues = savedNamedValues;
        variableTypes = savedVariableTypes;
        structVariableTypes = savedStructVariableTypes;
        traitObjectVariableTypes = savedTraitObjectVariableTypes;
        enumVariableTypes = savedEnumVariableTypes;
        listElementTypes = savedListElementTypes;
        mapKeyValueTypes = savedMapKeyValueTypes;
        tupleElementTypes = savedTupleElementTypes;
        pointerElementTypes = savedPointerElementTypes;
        movedVariables = savedMovedVariables;
        pointerBorrowTarget = savedPointerBorrowTarget;
        activeBorrowers = savedActiveBorrowers;
        activeMutBorrower = savedActiveMutBorrower;
        cleanupScopes = savedCleanupScopes;
        variableScopeDepth = savedVariableScopeDepth;
        variableScopeDepthScopes = savedVariableScopeDepthScopes;

        return armValue;
    };

    if(hasOk || hasErr)
    {
        llvm::Type* matchType = matchVal->getType();
        if(!matchType->isStructTy())
        {
            reportError(node->line, "match expects a Result value");
            return nullptr;
        }

        auto* structType = llvm::cast<llvm::StructType>(matchType);
        std::string structName = structType->getName().str();
        if(structName.empty())
        {
            reportError(node->line, "match expects a named struct type");
            return nullptr;
        }

        auto memIt = structMembers.find(structName);
        if(memIt == structMembers.end())
        {
            reportError(node->line, "unknown struct type: " + structName);
            return nullptr;
        }

        int isOkIndex = -1;
        int okIndex = -1;
        int errIndex = -1;
        TypeNode* okType = nullptr;
        TypeNode* errType = nullptr;

        const auto& members = memIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == "is_ok")
                isOkIndex = static_cast<int>(i);
            else if(members[i].first == "ok")
            {
                okIndex = static_cast<int>(i);
                okType = members[i].second;
            }
            else if(members[i].first == "err")
            {
                errIndex = static_cast<int>(i);
                errType = members[i].second;
            }
        }

        if(isOkIndex < 0 || okIndex < 0 || errIndex < 0)
        {
            reportError(node->line,
                        "match expects Result with is_ok/ok/err fields");
            return nullptr;
        }

        if(!okArm)
            okArm = wildcardArm;
        if(!errArm)
            errArm = wildcardArm;
        if(!okArm || !errArm)
        {
            reportError(node->line,
                        "match requires Ok and Err arms or wildcard");
            return nullptr;
        }

        llvm::Function* func = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* okBB =
            llvm::BasicBlock::Create(context, "match.ok", func);
        llvm::BasicBlock* errBB =
            llvm::BasicBlock::Create(context, "match.err", func);
        llvm::BasicBlock* mergeBB =
            llvm::BasicBlock::Create(context, "match.merge", func);

        llvm::Value* isOkVal =
            builder.CreateExtractValue(matchVal, isOkIndex, "match.is_ok");
        builder.CreateCondBr(isOkVal, okBB, errBB);

        builder.SetInsertPoint(okBB);
        std::set<std::string> okMovedState;
        std::map<std::string, std::string> okPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> okActiveBorrowers;
        llvm::Value* okValue =
            generateArmValue(okArm, okIndex, okType, &okMovedState,
                             &okPointerBorrowTarget, &okActiveBorrowers);
        if(!okValue)
            return nullptr;
        llvm::BasicBlock* okEnd = builder.GetInsertBlock();
        bool okFallsThrough = (okEnd->getTerminator() == nullptr);
        if(okFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(errBB);
        std::set<std::string> errMovedState;
        std::map<std::string, std::string> errPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> errActiveBorrowers;
        llvm::Value* errValue =
            generateArmValue(errArm, errIndex, errType, &errMovedState,
                             &errPointerBorrowTarget, &errActiveBorrowers);
        if(!errValue)
            return nullptr;
        llvm::BasicBlock* errEnd = builder.GetInsertBlock();
        bool errFallsThrough = (errEnd->getTerminator() == nullptr);
        if(errFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(mergeBB);
        std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> armValues;
        std::vector<std::set<std::string>> armMovedStates;
        std::vector<std::map<std::string, std::string>> armPointerBorrowStates;
        std::vector<std::map<std::string, std::set<std::string>>>
            armActiveBorrowerStates;
        if(okFallsThrough)
        {
            armValues.push_back({okValue, okEnd});
            armMovedStates.push_back(okMovedState);
            armPointerBorrowStates.push_back(okPointerBorrowTarget);
            armActiveBorrowerStates.push_back(okActiveBorrowers);
        }
        if(errFallsThrough)
        {
            armValues.push_back({errValue, errEnd});
            armMovedStates.push_back(errMovedState);
            armPointerBorrowStates.push_back(errPointerBorrowTarget);
            armActiveBorrowerStates.push_back(errActiveBorrowers);
        }
        if(armValues.empty())
        {
            reportError(node->line, "match expression has no continuing arm");
            return nullptr;
        }
        llvm::Type* okValueType = armValues[0].first->getType();

        auto castInBlock = [&](llvm::Value* val, llvm::Type* target,
                               llvm::BasicBlock* block) -> llvm::Value*
        {
            if(!val || !target || !block)
                return val;
            llvm::Type* src = val->getType();
            if(src == target)
                return val;
            llvm::IRBuilder<> castBuilder(block->getTerminator());
            if(src->isIntegerTy() && target->isIntegerTy())
            {
                unsigned srcBits = src->getIntegerBitWidth();
                unsigned dstBits = target->getIntegerBitWidth();
                if(srcBits > dstBits)
                    return castBuilder.CreateTrunc(val, target, "match.trunc");
                if(srcBits < dstBits)
                    return castBuilder.CreateSExt(val, target, "match.sext");
                return val;
            }
            if(src->isIntegerTy() && target->isFloatingPointTy())
                return castBuilder.CreateSIToFP(val, target, "match.sitofp");
            if(src->isFloatingPointTy() && target->isFloatingPointTy())
                return castBuilder.CreateFPCast(val, target, "match.fpcast");
            return val;
        };

        if(armValues.size() == 2 &&
           armValues[0].first->getType() != armValues[1].first->getType())
        {
            llvm::Type* errValueType = armValues[1].first->getType();
            llvm::Type* commonType = nullptr;
            if(okValueType->isIntegerTy() && errValueType->isIntegerTy())
            {
                unsigned okBits = okValueType->getIntegerBitWidth();
                unsigned errBits = errValueType->getIntegerBitWidth();
                commonType = okBits >= errBits ? okValueType : errValueType;
            }
            else if(okValueType->isFloatingPointTy() &&
                    errValueType->isFloatingPointTy())
            {
                commonType =
                    okValueType->isDoubleTy() || errValueType->isDoubleTy()
                        ? llvm::Type::getDoubleTy(context)
                        : llvm::Type::getFloatTy(context);
            }
            else if(okValueType->isFloatingPointTy() &&
                    errValueType->isIntegerTy())
            {
                commonType = okValueType;
            }
            else if(okValueType->isIntegerTy() &&
                    errValueType->isFloatingPointTy())
            {
                commonType = errValueType;
            }

            if(!commonType)
            {
                reportError(node->line, "match arm types do not match");
                return nullptr;
            }

            armValues[0].first = castInBlock(armValues[0].first, commonType,
                                             armValues[0].second);
            armValues[1].first = castInBlock(armValues[1].first, commonType,
                                             armValues[1].second);
            okValueType = commonType;
        }

        if(okValueType->isVoidTy())
        {
            reportError(node->line, "match arms must return a value");
            return nullptr;
        }

        llvm::PHINode* phi = builder.CreatePHI(
            okValueType, (unsigned)armValues.size(), "match.result");
        for(const auto& pair : armValues)
            phi->addIncoming(pair.first, pair.second);
        std::set<std::string> mergedMoved;
        std::map<std::string, std::string> mergedPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> mergedActiveBorrowers;
        for(const auto& st : armMovedStates)
            mergedMoved.insert(st.begin(), st.end());
        for(const auto& ptrState : armPointerBorrowStates)
        {
            for(const auto& kv : ptrState)
            {
                if(mergedPointerBorrowTarget.find(kv.first) ==
                   mergedPointerBorrowTarget.end())
                {
                    mergedPointerBorrowTarget[kv.first] = kv.second;
                }
            }
        }
        for(const auto& borrowersState : armActiveBorrowerStates)
        {
            for(const auto& kv : borrowersState)
            {
                auto& dst = mergedActiveBorrowers[kv.first];
                dst.insert(kv.second.begin(), kv.second.end());
            }
        }
        movedVariables = std::move(mergedMoved);
        pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
        activeBorrowers = std::move(mergedActiveBorrowers);
        return phi;
    }

    if(hasOption)
    {
        llvm::Type* matchType = matchVal->getType();
        if(!matchType->isStructTy())
        {
            reportError(node->line, "match expects an Option value");
            return nullptr;
        }

        auto* structType = llvm::cast<llvm::StructType>(matchType);
        std::string structName = structType->getName().str();
        if(structName.empty())
        {
            reportError(node->line, "match expects a named struct type");
            return nullptr;
        }

        auto memIt = structMembers.find(structName);
        if(memIt == structMembers.end())
        {
            reportError(node->line, "unknown struct type: " + structName);
            return nullptr;
        }

        int isSomeIndex = -1;
        int valueIndex = -1;
        TypeNode* valueType = nullptr;

        const auto& members = memIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == "is_some")
                isSomeIndex = static_cast<int>(i);
            else if(members[i].first == "value")
            {
                valueIndex = static_cast<int>(i);
                valueType = members[i].second;
            }
        }

        if(isSomeIndex < 0 || valueIndex < 0)
        {
            reportError(node->line,
                        "match expects Option with is_some/value fields");
            return nullptr;
        }

        if(!someArm)
            someArm = wildcardArm;
        if(!noneArm)
            noneArm = wildcardArm;
        if(!someArm || !noneArm)
        {
            reportError(node->line,
                        "match requires Some and None arms or wildcard");
            return nullptr;
        }

        llvm::Function* func = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* someBB =
            llvm::BasicBlock::Create(context, "match.some", func);
        llvm::BasicBlock* noneBB =
            llvm::BasicBlock::Create(context, "match.none", func);
        llvm::BasicBlock* mergeBB =
            llvm::BasicBlock::Create(context, "match.merge", func);

        llvm::Value* isSomeVal =
            builder.CreateExtractValue(matchVal, isSomeIndex, "match.is_some");
        builder.CreateCondBr(isSomeVal, someBB, noneBB);

        builder.SetInsertPoint(someBB);
        std::set<std::string> someMovedState;
        std::map<std::string, std::string> somePointerBorrowTarget;
        std::map<std::string, std::set<std::string>> someActiveBorrowers;
        llvm::Value* someValue =
            generateArmValue(someArm, valueIndex, valueType, &someMovedState,
                             &somePointerBorrowTarget, &someActiveBorrowers);
        if(!someValue)
            return nullptr;
        llvm::BasicBlock* someEnd = builder.GetInsertBlock();
        bool someFallsThrough = (someEnd->getTerminator() == nullptr);
        if(someFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(noneBB);
        std::set<std::string> noneMovedState;
        std::map<std::string, std::string> nonePointerBorrowTarget;
        std::map<std::string, std::set<std::string>> noneActiveBorrowers;
        llvm::Value* noneValue =
            generateArmValue(noneArm, valueIndex, valueType, &noneMovedState,
                             &nonePointerBorrowTarget, &noneActiveBorrowers);
        if(!noneValue)
            return nullptr;
        llvm::BasicBlock* noneEnd = builder.GetInsertBlock();
        bool noneFallsThrough = (noneEnd->getTerminator() == nullptr);
        if(noneFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(mergeBB);
        std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> armValues;
        std::vector<std::set<std::string>> armMovedStates;
        std::vector<std::map<std::string, std::string>> armPointerBorrowStates;
        std::vector<std::map<std::string, std::set<std::string>>>
            armActiveBorrowerStates;
        if(someFallsThrough)
        {
            armValues.push_back({someValue, someEnd});
            armMovedStates.push_back(someMovedState);
            armPointerBorrowStates.push_back(somePointerBorrowTarget);
            armActiveBorrowerStates.push_back(someActiveBorrowers);
        }
        if(noneFallsThrough)
        {
            armValues.push_back({noneValue, noneEnd});
            armMovedStates.push_back(noneMovedState);
            armPointerBorrowStates.push_back(nonePointerBorrowTarget);
            armActiveBorrowerStates.push_back(noneActiveBorrowers);
        }
        if(armValues.empty())
        {
            reportError(node->line, "match expression has no continuing arm");
            return nullptr;
        }
        llvm::Type* someValueType = armValues[0].first->getType();

        auto castInBlock = [&](llvm::Value* val, llvm::Type* target,
                               llvm::BasicBlock* block) -> llvm::Value*
        {
            if(!val || !target || !block)
                return val;
            llvm::Type* src = val->getType();
            if(src == target)
                return val;
            llvm::IRBuilder<> castBuilder(block->getTerminator());
            if(src->isIntegerTy() && target->isIntegerTy())
            {
                unsigned srcBits = src->getIntegerBitWidth();
                unsigned dstBits = target->getIntegerBitWidth();
                if(srcBits > dstBits)
                    return castBuilder.CreateTrunc(val, target, "match.trunc");
                if(srcBits < dstBits)
                    return castBuilder.CreateSExt(val, target, "match.sext");
                return val;
            }
            if(src->isIntegerTy() && target->isFloatingPointTy())
                return castBuilder.CreateSIToFP(val, target, "match.sitofp");
            if(src->isFloatingPointTy() && target->isFloatingPointTy())
                return castBuilder.CreateFPCast(val, target, "match.fpcast");
            return val;
        };

        if(armValues.size() == 2 &&
           armValues[0].first->getType() != armValues[1].first->getType())
        {
            llvm::Type* noneValueType = armValues[1].first->getType();
            llvm::Type* commonType = nullptr;
            if(someValueType->isIntegerTy() && noneValueType->isIntegerTy())
            {
                unsigned someBits = someValueType->getIntegerBitWidth();
                unsigned noneBits = noneValueType->getIntegerBitWidth();
                commonType =
                    someBits >= noneBits ? someValueType : noneValueType;
            }
            else if(someValueType->isFloatingPointTy() &&
                    noneValueType->isFloatingPointTy())
            {
                commonType =
                    someValueType->isDoubleTy() || noneValueType->isDoubleTy()
                        ? llvm::Type::getDoubleTy(context)
                        : llvm::Type::getFloatTy(context);
            }
            else if(someValueType->isFloatingPointTy() &&
                    noneValueType->isIntegerTy())
            {
                commonType = someValueType;
            }
            else if(someValueType->isIntegerTy() &&
                    noneValueType->isFloatingPointTy())
            {
                commonType = noneValueType;
            }

            if(!commonType)
            {
                reportError(node->line, "match arm types do not match");
                return nullptr;
            }

            armValues[0].first = castInBlock(armValues[0].first, commonType,
                                             armValues[0].second);
            armValues[1].first = castInBlock(armValues[1].first, commonType,
                                             armValues[1].second);
            someValueType = commonType;
        }

        if(someValueType->isVoidTy())
        {
            reportError(node->line, "match arms must return a value");
            return nullptr;
        }

        llvm::PHINode* phi = builder.CreatePHI(
            someValueType, (unsigned)armValues.size(), "match.result");
        for(const auto& pair : armValues)
            phi->addIncoming(pair.first, pair.second);
        std::set<std::string> mergedMoved;
        std::map<std::string, std::string> mergedPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> mergedActiveBorrowers;
        for(const auto& st : armMovedStates)
            mergedMoved.insert(st.begin(), st.end());
        for(const auto& ptrState : armPointerBorrowStates)
        {
            for(const auto& kv : ptrState)
            {
                if(mergedPointerBorrowTarget.find(kv.first) ==
                   mergedPointerBorrowTarget.end())
                {
                    mergedPointerBorrowTarget[kv.first] = kv.second;
                }
            }
        }
        for(const auto& borrowersState : armActiveBorrowerStates)
        {
            for(const auto& kv : borrowersState)
            {
                auto& dst = mergedActiveBorrowers[kv.first];
                dst.insert(kv.second.begin(), kv.second.end());
            }
        }
        movedVariables = std::move(mergedMoved);
        pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
        activeBorrowers = std::move(mergedActiveBorrowers);
        return phi;
    }

    if(!literalArms.empty() && !wildcardArm)
    {
        reportError(node->line, "literal match requires a wildcard arm");
        return nullptr;
    }

    if(literalArms.empty() && wildcardArm)
    {
        return generateExpression(wildcardArm->expression);
    }

    llvm::Type* matchType = matchVal->getType();
    if(matchType->isStructTy())
    {
        reportError(node->line, "literal match expects a scalar value");
        return nullptr;
    }

    auto buildLiteralCompare = [&](llvm::Value* litVal) -> llvm::Value*
    {
        if(!litVal)
            return nullptr;
        llvm::Type* targetType = matchType;
        llvm::Type* litType = litVal->getType();
        if(targetType->isIntegerTy())
        {
            if(!litType->isIntegerTy())
            {
                reportError(node->line,
                            "literal pattern type does not match target");
                return nullptr;
            }
            unsigned dstBits = targetType->getIntegerBitWidth();
            unsigned srcBits = litType->getIntegerBitWidth();
            if(srcBits > dstBits)
                litVal = builder.CreateTrunc(litVal, targetType, "match.lit");
            else if(srcBits < dstBits)
                litVal = builder.CreateSExt(litVal, targetType, "match.lit");
            return builder.CreateICmpEQ(matchVal, litVal, "match.cmp");
        }
        if(targetType->isFloatingPointTy())
        {
            if(litType->isIntegerTy())
                litVal = builder.CreateSIToFP(litVal, targetType, "match.lit");
            else if(litType->isFloatingPointTy() && litType != targetType)
                litVal = builder.CreateFPCast(litVal, targetType, "match.lit");
            else if(!litType->isFloatingPointTy())
            {
                reportError(node->line,
                            "literal pattern type does not match target");
                return nullptr;
            }
            return builder.CreateFCmpOEQ(matchVal, litVal, "match.fcmp");
        }
        if(targetType->isPointerTy())
        {
            if(!litType->isPointerTy())
            {
                reportError(node->line,
                            "literal pattern type does not match target");
                return nullptr;
            }
            return builder.CreateICmpEQ(matchVal, litVal, "match.pcmp");
        }
        reportError(node->line,
                    "literal match expects numeric or pointer type");
        return nullptr;
    };

    llvm::Function* func = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* mergeBB =
        llvm::BasicBlock::Create(context, "match.merge", func);
    llvm::BasicBlock* wildcardBB = nullptr;
    if(wildcardArm)
        wildcardBB = llvm::BasicBlock::Create(context, "match.wildcard", func);

    std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> armValues;
    std::vector<std::set<std::string>> armMovedStates;
    std::vector<std::map<std::string, std::string>> armPointerBorrowStates;
    std::vector<std::map<std::string, std::set<std::string>>>
        armActiveBorrowerStates;
    std::vector<llvm::BasicBlock*> armBlocks;

    llvm::BasicBlock* nextBB = nullptr;
    for(size_t i = 0; i < literalArms.size(); ++i)
    {
        MatchArmNode* arm = literalArms[i];
        llvm::BasicBlock* armBB =
            llvm::BasicBlock::Create(context, "match.case", func);
        bool isLast = (i + 1 == literalArms.size());
        llvm::BasicBlock* fallBB =
            isLast ? (wildcardBB ? wildcardBB : mergeBB)
                   : llvm::BasicBlock::Create(context, "match.next", func);

        llvm::Value* litVal = generateExpression(arm->pattern->literal);
        llvm::Value* cmp = buildLiteralCompare(litVal);
        if(!cmp)
            return nullptr;
        builder.CreateCondBr(cmp, armBB, fallBB);

        builder.SetInsertPoint(armBB);
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;
        llvm::Value* armVal = generateExpression(arm->expression);
        if(!armVal)
            return nullptr;
        llvm::BasicBlock* armEnd = builder.GetInsertBlock();
        if(!armEnd->getTerminator())
        {
            armMovedStates.push_back(movedVariables);
            armPointerBorrowStates.push_back(pointerBorrowTarget);
            armActiveBorrowerStates.push_back(activeBorrowers);
            builder.CreateBr(mergeBB);
            armValues.push_back({armVal, builder.GetInsertBlock()});
        }
        armBlocks.push_back(armBB);
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;

        builder.SetInsertPoint(fallBB);
        nextBB = fallBB;
    }

    if(wildcardArm)
    {
        builder.SetInsertPoint(wildcardBB);
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;
        llvm::Value* armVal = generateExpression(wildcardArm->expression);
        if(!armVal)
            return nullptr;
        llvm::BasicBlock* wildcardEnd = builder.GetInsertBlock();
        if(!wildcardEnd->getTerminator())
        {
            armMovedStates.push_back(movedVariables);
            armPointerBorrowStates.push_back(pointerBorrowTarget);
            armActiveBorrowerStates.push_back(activeBorrowers);
            builder.CreateBr(mergeBB);
            armValues.push_back({armVal, builder.GetInsertBlock()});
        }
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;
    }
    else
    {
        if(nextBB && nextBB != mergeBB)
            builder.SetInsertPoint(nextBB);
    }

    std::vector<llvm::Type*> armTypes;
    if(armValues.empty())
    {
        reportError(node->line, "match expression has no continuing arm");
        return nullptr;
    }
    armTypes.reserve(armValues.size());
    for(const auto& pair : armValues)
        armTypes.push_back(pair.first->getType());

    auto commonTypeFrom =
        [&](const std::vector<llvm::Type*>& types) -> llvm::Type*
    {
        if(types.empty())
            return nullptr;
        llvm::Type* common = types[0];
        bool anyFloat = common->isFloatingPointTy();
        bool anyInt = common->isIntegerTy();
        bool anyPtr = common->isPointerTy();
        bool anyStruct = common->isStructTy();

        unsigned maxIntBits = anyInt ? common->getIntegerBitWidth() : 0;
        bool anyDouble = common->isDoubleTy();

        for(size_t i = 1; i < types.size(); ++i)
        {
            llvm::Type* t = types[i];
            if(t == common)
                continue;
            if(t->isFloatingPointTy())
            {
                anyFloat = true;
                if(t->isDoubleTy())
                    anyDouble = true;
            }
            if(t->isIntegerTy())
            {
                anyInt = true;
                unsigned bits = t->getIntegerBitWidth();
                if(bits > maxIntBits)
                    maxIntBits = bits;
            }
            if(t->isPointerTy())
                anyPtr = true;
            if(t->isStructTy())
                anyStruct = true;
        }

        if(anyStruct)
        {
            for(auto* t : types)
            {
                if(t != types[0])
                    return nullptr;
            }
            return types[0];
        }

        if(anyPtr && !(anyFloat || anyInt))
        {
            for(auto* t : types)
            {
                if(!t->isPointerTy() || t != types[0])
                    return nullptr;
            }
            return types[0];
        }

        if(anyFloat)
            return anyDouble ? llvm::Type::getDoubleTy(context)
                             : llvm::Type::getFloatTy(context);
        if(anyInt)
            return llvm::Type::getIntNTy(context, maxIntBits);

        for(auto* t : types)
        {
            if(t != types[0])
                return nullptr;
        }
        return types[0];
    };

    llvm::Type* commonType = commonTypeFrom(armTypes);
    if(!commonType)
    {
        reportError(node->line, "match arm types do not match");
        return nullptr;
    }
    if(commonType->isVoidTy())
    {
        reportError(node->line, "match arms must return a value");
        return nullptr;
    }

    for(auto& pair : armValues)
    {
        llvm::Value* val = pair.first;
        llvm::BasicBlock* blk = pair.second;
        if(val->getType() == commonType)
            continue;
        llvm::IRBuilder<> castBuilder(blk->getTerminator());
        llvm::Value* casted = val;
        llvm::Type* src = val->getType();
        if(src->isIntegerTy() && commonType->isIntegerTy())
        {
            unsigned srcBits = src->getIntegerBitWidth();
            unsigned dstBits = commonType->getIntegerBitWidth();
            if(srcBits > dstBits)
                casted =
                    castBuilder.CreateTrunc(val, commonType, "match.trunc");
            else if(srcBits < dstBits)
                casted = castBuilder.CreateSExt(val, commonType, "match.sext");
        }
        else if(src->isIntegerTy() && commonType->isFloatingPointTy())
        {
            casted = castBuilder.CreateSIToFP(val, commonType, "match.sitofp");
        }
        else if(src->isFloatingPointTy() && commonType->isFloatingPointTy())
        {
            casted = castBuilder.CreateFPCast(val, commonType, "match.fpcast");
        }
        pair.first = casted;
    }
    builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder.CreatePHI(
        commonType, (unsigned)armValues.size(), "match.result");
    for(const auto& pair : armValues)
        phi->addIncoming(pair.first, pair.second);
    std::set<std::string> mergedMoved;
    std::map<std::string, std::string> mergedPointerBorrowTarget;
    std::map<std::string, std::set<std::string>> mergedActiveBorrowers;
    for(const auto& st : armMovedStates)
        mergedMoved.insert(st.begin(), st.end());
    for(const auto& ptrState : armPointerBorrowStates)
    {
        for(const auto& kv : ptrState)
        {
            if(mergedPointerBorrowTarget.find(kv.first) ==
               mergedPointerBorrowTarget.end())
            {
                mergedPointerBorrowTarget[kv.first] = kv.second;
            }
        }
    }
    for(const auto& borrowersState : armActiveBorrowerStates)
    {
        for(const auto& kv : borrowersState)
        {
            auto& dst = mergedActiveBorrowers[kv.first];
            dst.insert(kv.second.begin(), kv.second.end());
        }
    }
    movedVariables = std::move(mergedMoved);
    pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
    activeBorrowers = std::move(mergedActiveBorrowers);
    return phi;
}

// ============================================================================
// GENERICS MONOMORPHIZATION IMPLEMENTATION
// ============================================================================

// Substitute type parameters with concrete types
// e.g., if typeParams = ["T", "U"] and typeArgs = [i32, i64],
// then a StructTypeRefNode("T") becomes a TypeNode(TYPE_I32)
TypeNode*
CodeGenerator::substituteTypeParams(TypeNode* type,
                                    const std::vector<std::string>& typeParams,
                                    const std::vector<TypeNode*>& typeArgs)
{
    if(!type)
        return nullptr;

    // Check if this is a struct type reference that matches a type parameter
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
    {
        // Look for matching type parameter
        for(size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i)
        {
            if(structRef->structName == typeParams[i])
            {
                // Return a copy of the concrete type
                return typeArgs[i];
            }
        }
        // Not a type parameter - return as-is (it's a concrete struct type)
        return type;
    }

    // Check if this is a generic struct type reference
    if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(type))
    {
        // Recursively substitute type arguments
        auto* newRef = new GenericStructTypeRefNode(genRef->structName);
        for(auto* arg : genRef->typeArgs)
        {
            newRef->typeArgs.push_back(
                substituteTypeParams(arg, typeParams, typeArgs));
        }
        return newRef;
    }

    // Handle generic list type
    if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
    {
        TypeNode* newElemType =
            substituteTypeParams(listType->elementType, typeParams, typeArgs);
        if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(type))
            return new ArrayTypeNode(newElemType, arrayType->capacity);
        return new GenericListTypeNode(newElemType);
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
    {
        TypeNode* newKeyType =
            substituteTypeParams(mapType->keyType, typeParams, typeArgs);
        TypeNode* newValType =
            substituteTypeParams(mapType->valueType, typeParams, typeArgs);
        return new MapTypeNode(newKeyType, newValType);
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
    {
        auto* newTypeList = new TypeListNode();
        for(auto* elemType : tupleType->elementTypes->types)
        {
            newTypeList->addType(
                substituteTypeParams(elemType, typeParams, typeArgs));
        }
        return new TupleTypeNode(newTypeList);
    }

    // Basic types don't need substitution
    return type;
}
