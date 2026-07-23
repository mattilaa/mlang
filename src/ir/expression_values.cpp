#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"

#include <llvm/Config/llvm-config.h>
#include <functional>

using mlang::ir_detail::ast_analysis::contains_update_expression;
using mlang::ir_detail::common::Helpers;

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

