#include "ir.h"
#include "ir/common.h"

#include <llvm/Config/llvm-config.h>

using mlang::ir_detail::common::Helpers;

llvm::Value* CodeGenerator::getLValuePointer(ExpressionNode* expr, int line)
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        if(!validateVariableAccessible(id->name, line, id->col))
            return nullptr;
        auto it = namedValues.find(id->name);
        return it->second;
    }

    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(expr))
    {
        llvm::Value* structPtr = nullptr;
        std::string structTypeName;

        if(fieldAccess->object)
        {
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, line);
            if(!ptr)
                return nullptr;
            structPtr = ptr;
            structTypeName = typeName;
        }
        else
        {
            if(!validateVariableAccessible(fieldAccess->structName, line,
                                           fieldAccess->col))
                return nullptr;
            structPtr = namedValues[fieldAccess->structName];
            if(!structPtr)
            {
                reportError(line,
                            "unknown variable: " + fieldAccess->structName);
                return nullptr;
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + fieldAccess->structName +
                                      "' is not a struct");
                return nullptr;
            }
            structTypeName = typeIt->second;

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

        auto memberIt = structMembers.find(structTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(line, "unknown struct type: " + structTypeName);
            return nullptr;
        }

        int fieldIndex = -1;
        const auto& members = memberIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldAccess->fieldName)
            {
                fieldIndex = static_cast<int>(i);
                break;
            }
        }

        if(fieldIndex < 0)
        {
            reportError(line, "struct '" + structTypeName +
                                  "' has no field named '" +
                                  fieldAccess->fieldName + "'");
            return nullptr;
        }
        if(!canAccessStructField(structTypeName, fieldIndex, line,
                                 fieldAccess->fieldName))
            return nullptr;

        llvm::StructType* structType = getStructType(structTypeName);
        if(!structType)
            return nullptr;

        return builder.CreateStructGEP(structType, structPtr,
                                       static_cast<unsigned>(fieldIndex),
                                       fieldAccess->fieldName + "_ptr");
    }

    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_DEREF)
        {
            if(!validatePointerDereference(unary->operand, line))
                return nullptr;

            llvm::Value* ptrVal = generateExpression(unary->operand);
            if(!ptrVal)
                return nullptr;
            if(!ptrVal->getType()->isPointerTy())
            {
                reportError(line, "dereference requires a pointer value");
                return nullptr;
            }
            if(!emitRuntimeNullPointerCheck(ptrVal, line))
                return nullptr;
            return ptrVal;
        }
    }

    reportError(line, "address-of operator requires an assignable expression");
    return nullptr;
}

TypeNode* CodeGenerator::getPointerElementType(ExpressionNode* expr, int line)
{
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_ADDR)
            return getLValueType(unary->operand, line);
    }

    TypeNode* type = getLValueType(expr, line);
    if(auto* ptrNode = dynamic_cast<PointerTypeNode*>(type))
        return ptrNode->elementType;

    reportError(line, "dereference requires a pointer value");
    return nullptr;
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
