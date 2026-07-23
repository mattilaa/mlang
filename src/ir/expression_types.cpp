#include "ir.h"

TypeNode* CodeGenerator::getLValueType(ExpressionNode* expr, int line)
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        if(!validateVariableAccessible(id->name, line, id->col))
            return nullptr;
        auto typeIt = variableTypes.find(id->name);
        if(typeIt == variableTypes.end())
        {
            reportError(line, id->col, "unknown variable: '" + id->name + "'");
            return nullptr;
        }

        TypeNode::TypeKind kind = typeIt->second;
        if(kind == TypeNode::TYPE_STRUCT)
        {
            auto structIt = structVariableTypes.find(id->name);
            if(structIt == structVariableTypes.end())
            {
                reportError(line,
                            "variable '" + id->name + "' is not a struct");
                return nullptr;
            }
            return new StructTypeRefNode(structIt->second);
        }
        if(kind == TypeNode::TYPE_INT)
        {
            auto enumIt = enumVariableTypes.find(id->name);
            if(enumIt != enumVariableTypes.end())
                return new StructTypeRefNode(enumIt->second);
        }
        if(kind == TypeNode::TYPE_STR8 || kind == TypeNode::TYPE_STRING)
        {
            auto enumIt = enumVariableTypes.find(id->name);
            if(enumIt != enumVariableTypes.end())
                return new StructTypeRefNode(enumIt->second);
        }
        if(kind == TypeNode::TYPE_LIST)
        {
            auto listIt = listElementTypes.find(id->name);
            if(listIt == listElementTypes.end())
            {
                reportError(line, "list element type not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            auto capIt = arrayCapacities.find(id->name);
            if(capIt != arrayCapacities.end())
                return new ArrayTypeNode(cloneTypeNode(listIt->second),
                                         capIt->second);
            return new GenericListTypeNode(cloneTypeNode(listIt->second));
        }
        if(kind == TypeNode::TYPE_MAP)
        {
            auto mapIt = mapKeyValueTypes.find(id->name);
            if(mapIt == mapKeyValueTypes.end())
            {
                reportError(line, "map key/value types not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            return new MapTypeNode(mapIt->second.first, mapIt->second.second);
        }
        if(kind == TypeNode::TYPE_TUPLE)
        {
            auto tupleIt = tupleElementTypes.find(id->name);
            if(tupleIt == tupleElementTypes.end())
            {
                reportError(line, "tuple element types not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            auto* list = new TypeListNode();
            for(auto* t : tupleIt->second)
                list->addType(t);
            return new TupleTypeNode(list);
        }
        if(kind == TypeNode::TYPE_PTR)
        {
            auto ptrIt = pointerElementTypes.find(id->name);
            if(ptrIt == pointerElementTypes.end())
            {
                reportError(line, "pointer element type not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            return new PointerTypeNode(ptrIt->second);
        }
        if(kind == TypeNode::TYPE_TRAIT_OBJECT)
        {
            auto traitIt = traitObjectVariableTypes.find(id->name);
            if(traitIt == traitObjectVariableTypes.end())
            {
                reportError(line, "trait object type not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            return new TraitObjectTypeNode(traitIt->second);
        }

        return new TypeNode(kind);
    }

    if(auto* index = dynamic_cast<IndexExpressionNode*>(expr))
    {
        TypeNode* baseType = getLValueType(index->base, line);
        if(auto* listType = dynamic_cast<GenericListTypeNode*>(baseType))
            return cloneTypeNode(listType->elementType);
        if(auto* mapType = dynamic_cast<MapTypeNode*>(baseType))
            return cloneTypeNode(mapType->valueType);

        if(auto* baseId = dynamic_cast<IdentifierNode*>(index->base))
        {
            auto listIt = listElementTypes.find(baseId->name);
            if(listIt != listElementTypes.end())
                return cloneTypeNode(listIt->second);

            auto mapIt = mapKeyValueTypes.find(baseId->name);
            if(mapIt != mapKeyValueTypes.end())
                return cloneTypeNode(mapIt->second.second);
        }
    }

    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
    {
        auto overloadIt = functionOverloads.find(call->name);
        if(overloadIt != functionOverloads.end())
        {
            for(const auto& info : overloadIt->second)
            {
                if(!info.node || !info.node->returnType)
                    continue;
                if(!isOverloadVisible(info))
                    continue;
                return info.node->returnType;
            }
        }
    }

    if(auto* methodCall = dynamic_cast<MethodCallNode*>(expr))
    {
        if(methodCall->methodName == "first" ||
           methodCall->methodName == "last" ||
           methodCall->methodName == "pop")
        {
            TypeNode* objectType = getLValueType(methodCall->object, line);
            if(auto* listType = dynamic_cast<GenericListTypeNode*>(objectType))
                return cloneTypeNode(listType->elementType);
        }
    }

    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(expr))
    {
        if(fieldAccess->fieldName == "name")
        {
            bool hasRealNameField = false;
            if(fieldAccess->object)
            {
                TypeNode* objType = getLValueType(fieldAccess->object, line);
                if(objType && objType->kind == TypeNode::TYPE_STRUCT)
                {
                    std::string objStructTypeName;
                    if(auto* structRef =
                           dynamic_cast<StructTypeRefNode*>(objType))
                    {
                        objStructTypeName = structRef->structName;
                    }
                    else if(auto* genRef =
                                dynamic_cast<GenericStructTypeRefNode*>(
                                    objType))
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
                auto kindIt = variableTypes.find(fieldAccess->structName);
                if(kindIt == variableTypes.end())
                {
                    reportError(line, "unknown variable: '" +
                                          fieldAccess->structName + "'");
                    return nullptr;
                }

                if(kindIt->second == TypeNode::TYPE_STRUCT)
                {
                    auto typeIt =
                        structVariableTypes.find(fieldAccess->structName);
                    if(typeIt != structVariableTypes.end())
                    {
                        hasRealNameField =
                            structHasFieldNamed(typeIt->second, "name");
                    }
                }
            }

            if(!hasRealNameField)
                return new TypeNode(TypeNode::TYPE_STRING);
        }

        std::string structTypeName;
        if(fieldAccess->object)
        {
            TypeNode* objType = getLValueType(fieldAccess->object, line);
            if(!objType)
                return nullptr;
            if(objType->kind != TypeNode::TYPE_STRUCT)
            {
                reportError(line, "field access requires a struct value");
                return nullptr;
            }

            if(auto* structRef = dynamic_cast<StructTypeRefNode*>(objType))
            {
                structTypeName = structRef->structName;
            }
            else if(auto* genRef =
                        dynamic_cast<GenericStructTypeRefNode*>(objType))
            {
                structTypeName = getOrCreateMonomorphizedStruct(
                    genRef->structName, genRef->typeArgs);
            }
        }
        else
        {
            if(!validateVariableAccessible(fieldAccess->structName, line,
                                           fieldAccess->col))
                return nullptr;
            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + fieldAccess->structName +
                                      "' is not a struct");
                return nullptr;
            }
            structTypeName = typeIt->second;
        }

        auto memberIt = structMembers.find(structTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(line, "unknown struct type: " + structTypeName);
            return nullptr;
        }

        for(const auto& member : memberIt->second)
        {
            if(member.first == fieldAccess->fieldName)
                return member.second;
        }

        reportError(line, "struct '" + structTypeName +
                              "' has no field named '" +
                              fieldAccess->fieldName + "'");
        return nullptr;
    }

    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_DEREF)
        {
            TypeNode* ptrType = getLValueType(unary->operand, line);
            if(auto* ptrNode = dynamic_cast<PointerTypeNode*>(ptrType))
                return ptrNode->elementType;

            reportError(line, "dereference requires a pointer value");
            return nullptr;
        }
    }

    return nullptr;
}

TypeNode* CodeGenerator::inferExpressionTypeNode(ExpressionNode* expr, int line)
{
    if(!expr)
        return nullptr;

    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto constexprIt = constexprValues.find(id->name);
        if(constexprIt != constexprValues.end())
        {
            if(constexprIt->second.kind == ConstexprValue::Kind::OpaqueStruct &&
               !constexprIt->second.typeName.empty())
                return new StructTypeRefNode(constexprIt->second.typeName);
            return new TypeNode(constexprIt->second.typeKind);
        }
    }

    if(TypeNode* lvalueType = getLValueType(expr, line))
        return cloneTypeNode(lvalueType);

    if(dynamic_cast<BoolLiteralNode*>(expr))
        return new TypeNode(TypeNode::TYPE_BOOL);
    if(dynamic_cast<IntLiteralNode*>(expr))
        return new TypeNode(TypeNode::TYPE_I64);
    if(dynamic_cast<FloatLiteralNode*>(expr))
        return new TypeNode(TypeNode::TYPE_FLOAT);
    if(dynamic_cast<DoubleLiteralNode*>(expr))
        return new TypeNode(TypeNode::TYPE_DOUBLE);
    if(dynamic_cast<StringLiteralNode*>(expr) ||
       dynamic_cast<FormatNode*>(expr))
        return new TypeNode(TypeNode::TYPE_STR8);

    if(auto* structLit = dynamic_cast<StructLiteralNode*>(expr))
    {
        if(structLit->typeArgs.empty())
            return new StructTypeRefNode(structLit->structName);

        auto* generic = new GenericStructTypeRefNode(structLit->structName);
        for(const auto& arg : structLit->typeArgs)
            generic->typeArgs.push_back(new StructTypeRefNode(arg));
        return generic;
    }

    if(auto* castExpr = dynamic_cast<CastExpressionNode*>(expr))
        return new TypeNode(castExpr->targetType);

    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_ADDR ||
           unary->op == UnaryOpNode::OP_ADDR_MUT)
        {
            TypeNode* pointee = inferExpressionTypeNode(unary->operand, line);
            if(!pointee)
                return nullptr;
            return new PointerTypeNode(pointee);
        }
        if(unary->op == UnaryOpNode::OP_DEREF)
            return cloneTypeNode(getPointerElementType(unary->operand, line));
    }

    if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
    {
        if(TypeNode* thenType =
               inferExpressionTypeNode(ternary->trueExpr, line))
            return thenType;
        return inferExpressionTypeNode(ternary->falseExpr, line);
    }

    if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(expr))
    {
        auto* typeList = new TypeListNode();
        if(tupleLit->elements)
        {
            for(auto* elem : tupleLit->elements->elements)
            {
                TypeNode* elemType = inferExpressionTypeNode(elem, line);
                if(!elemType)
                    return nullptr;
                typeList->addType(elemType);
            }
        }
        return new TupleTypeNode(typeList);
    }

    if(auto* listLit = dynamic_cast<ListLiteralNode*>(expr))
    {
        if(!listLit->elements || listLit->elements->elements.empty())
            return new ListTypeNode();

        TypeNode* elemType =
            inferExpressionTypeNode(listLit->elements->elements.front(), line);
        if(!elemType)
            return nullptr;
        return new GenericListTypeNode(elemType);
    }

    if(auto* arrFill = dynamic_cast<ArrayFillNode*>(expr))
    {
        TypeNode* elemType = inferExpressionTypeNode(arrFill->value, line);
        if(!elemType)
            return nullptr;
        return new GenericListTypeNode(elemType);
    }

    if(auto* mapLit = dynamic_cast<MapLiteralNode*>(expr))
    {
        if(!mapLit->entries || mapLit->entries->entries.empty())
            return new TypeNode(TypeNode::TYPE_MAP);

        auto* entry = mapLit->entries->entries.front();
        TypeNode* keyType = inferExpressionTypeNode(entry->key, line);
        TypeNode* valueType = inferExpressionTypeNode(entry->value, line);
        if(!keyType || !valueType)
            return nullptr;
        return new MapTypeNode(keyType, valueType);
    }

    if(auto* bin = dynamic_cast<BinaryOpNode*>(expr))
    {
        if(bin->op == BinaryOpNode::OP_LT || bin->op == BinaryOpNode::OP_GT ||
           bin->op == BinaryOpNode::OP_LE || bin->op == BinaryOpNode::OP_GE ||
           bin->op == BinaryOpNode::OP_EQ || bin->op == BinaryOpNode::OP_NE ||
           bin->op == BinaryOpNode::OP_AND || bin->op == BinaryOpNode::OP_OR)
            return new TypeNode(TypeNode::TYPE_BOOL);

        if(bin->op == BinaryOpNode::OP_SPACESHIP)
            return new TypeNode(TypeNode::TYPE_I32);

        TypeNode* leftType = inferExpressionTypeNode(bin->left, line);
        TypeNode* rightType = inferExpressionTypeNode(bin->right, line);
        if(leftType && (leftType->kind == TypeNode::TYPE_STR8 ||
                        leftType->kind == TypeNode::TYPE_STR16 ||
                        leftType->kind == TypeNode::TYPE_STRING))
            return leftType;
        if(rightType && (rightType->kind == TypeNode::TYPE_DOUBLE ||
                         rightType->kind == TypeNode::TYPE_FLOAT))
            return rightType;
        if(leftType)
            return leftType;
        return rightType;
    }

    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
    {
        auto overloadIt = functionOverloads.find(call->name);
        if(overloadIt != functionOverloads.end())
        {
            for(const auto& info : overloadIt->second)
            {
                if(info.node && info.node->returnType &&
                   isOverloadVisible(info))
                    return cloneTypeNode(info.node->returnType);
            }
        }
    }

    if(auto* methodCall = dynamic_cast<MethodCallNode*>(expr))
    {
        if(methodCall->methodName == "clone")
            return inferExpressionTypeNode(methodCall->object, line);
        if(methodCall->methodName == "len")
            return new TypeNode(TypeNode::TYPE_I64);
        if(methodCall->methodName == "is_empty" ||
           methodCall->methodName == "is_ok" ||
           methodCall->methodName == "is_err" ||
           methodCall->methodName == "is_some")
            return new TypeNode(TypeNode::TYPE_BOOL);
    }

    if(auto* sizeofExpr = dynamic_cast<SizeofExpressionNode*>(expr))
        return new TypeNode(TypeNode::TYPE_I64);
    if(auto* cexprExpr = dynamic_cast<CexprExpressionNode*>(expr))
    {
        if(TypeNode* inferred =
               inferExpressionTypeNode(cexprExpr->expression, line))
            return inferred;
        ConstexprValue value;
        std::string errorMessage;
        if(evalConstexprExpression(cexprExpr->expression, value,
                                   &errorMessage, nullptr, 0))
            return new TypeNode(value.typeKind);
        return nullptr;
    }

    return nullptr;
}
