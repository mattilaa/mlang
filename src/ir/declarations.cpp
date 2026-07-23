#include "ir.h"
#include "ir/common.h"
#include "ir/expression_type_kind.h"

#include <llvm/Config/llvm-config.h>

using mlang::ir_detail::common::Helpers;

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
