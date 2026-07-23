#include "ir.h"

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
