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
