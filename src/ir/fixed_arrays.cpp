#include "ir.h"
#include "ir/ast_analysis.h"

llvm::Value* CodeGenerator::generateFixedArrayValue(
    ExpressionNode* expression,
    FixedArrayTypeNode* type,
    int line)
{
    auto* arrayType =
        llvm::dyn_cast_or_null<llvm::ArrayType>(getLLVMTypeFromNode(type));
    if(!arrayType)
        return nullptr;
    auto* literal = dynamic_cast<ListLiteralNode*>(expression);
    auto* fill = dynamic_cast<ArrayFillNode*>(expression);
    if(expression && !literal && !fill)
    {
        llvm::Value* value = generateExpression(expression, type);
        if(value && value->getType() != arrayType)
        {
            reportError(
                line,
                "fixedarray initializer must have the same element type and "
                "extent");
            return nullptr;
        }
        return value;
    }
    int64_t count =
        literal && literal->elements ? literal->elements->elements.size() : 0;
    if(fill && !evaluateCompileTimeInt(fill->count, count))
    {
        reportError(
            line,
            "fixedarray fill count must be a compile-time integer");
        return nullptr;
    }
    if(count < 0 || count > type->capacity)
    {
        reportError(line, "fixedarray initializer exceeds its fixed extent");
        return nullptr;
    }
    auto convert = [&](ExpressionNode* item) -> llvm::Value*
    {
        llvm::Value* value = nullptr;
        if(auto* nested = dynamic_cast<FixedArrayTypeNode*>(type->elementType))
            value = generateFixedArrayValue(item, nested, line);
        else
            value = generateExpression(item, type->elementType);
        if(!value)
            return nullptr;
        llvm::Type* element = arrayType->getElementType();
        if(value->getType() == element)
            return value;
        if(value->getType()->isIntegerTy() && element->isIntegerTy())
        {
            TypeNode* source = inferExpressionTypeNode(item, line);
            return builder.CreateIntCast(
                value,
                element,
                !source || !isUnsignedType(source->kind));
        }
        if(value->getType()->isFloatingPointTy() &&
           element->isFloatingPointTy())
            return builder.CreateFPCast(value, element);
        reportError(line, "fixedarray element type mismatch");
        return nullptr;
    };
    llvm::Value* value = llvm::Constant::getNullValue(arrayType);
    llvm::Value* repeated = fill && count ? convert(fill->value) : nullptr;
    if(fill && count && !repeated)
        return nullptr;
    for(int64_t i = 0; i < count; ++i)
    {
        llvm::Value* item =
            fill ? repeated : convert(literal->elements->elements[i]);
        if(!item)
            return nullptr;
        value = builder.CreateInsertValue(
            value,
            item,
            {static_cast<unsigned>(i)},
            "fixedarray.init");
    }
    return value;
}

void CodeGenerator::generateFixedArrayDeclaration(
    const std::string& name,
    FixedArrayTypeNode* type,
    ExpressionNode* expression,
    bool immutable,
    int line)
{
    llvm::Value* value = generateFixedArrayValue(expression, type, line);
    if(!value)
        return;
    value = applyStructCopySemantics(value, type);
    auto* storage = createEntryBlockAlloca(
        builder.GetInsertBlock()->getParent(),
        value->getType(),
        name);
    builder.CreateStore(value, storage);
    recordVariableScopeDepth(name);
    clearMovedVariable(name);
    clearPointerBorrow(name);
    namedValues[name] = storage;
    variableTypes[name] = TypeNode::TYPE_LIST;
    listElementTypes[name] = type->elementType;
    arrayCapacities[name] = type->capacity;
    arrayKnownLengths[name] = type->capacity;
    multiarrayMutability.erase(name);
    if(immutable)
        constantVariables.insert(name);
    else
        constantVariables.erase(name);
    if(expression)
    {
        consumeMoveFromExpression(
            expression,
            line,
            "initializing fixedarray '" + name + "'");
        trackAggregateBorrowOrigins(name, expression);
    }
}

llvm::Value* CodeGenerator::generateFixedArrayIndexPointer(
    IndexExpressionNode* node,
    FixedArrayTypeNode* type)
{
    if(mlang::ir_detail::ast_analysis::contains_update_expression(node->index))
    {
        reportError(
            node->line,
            "index expression does not allow pre/post ++/--");
        return nullptr;
    }
    int64_t knownIndex = 0;
    if(evaluateCompileTimeInt(node->index, knownIndex) &&
       (knownIndex < 0 || knownIndex >= type->capacity))
    {
        reportError(node->line, "fixedarray index out of bounds");
        return nullptr;
    }
    llvm::Value* storage = nullptr;
    if(dynamic_cast<FunctionCallNode*>(node->base))
    {
        llvm::Value* value = generateExpression(node->base);
        if(!value)
            return nullptr;
        storage = builder.CreateAlloca(value->getType());
        builder.CreateStore(value, storage);
    }
    else
        storage = getLValuePointer(node->base, node->line);
    llvm::Value* index = generateExpression(node->index);
    if(!storage || !index)
        return nullptr;
    if(!index->getType()->isIntegerTy())
    {
        reportError(node->line, "fixedarray index must be an integer");
        return nullptr;
    }
    auto* integer = llvm::Type::getInt64Ty(context);
    TypeNode* indexType = inferExpressionTypeNode(node->index, node->line);
    index = builder.CreateIntCast(
        index,
        integer,
        !indexType || !isUnsignedType(indexType->kind));
    auto* function = builder.GetInsertBlock()->getParent();
    auto* valid =
        llvm::BasicBlock::Create(context, "fixedarray.index.ok", function);
    auto* invalid =
        llvm::BasicBlock::Create(context, "fixedarray.index.fail", function);
    builder.CreateCondBr(
        builder.CreateICmpULT(
            index,
            llvm::ConstantInt::get(integer, type->capacity)),
        valid,
        invalid);
    builder.SetInsertPoint(invalid);
    initializeFormatFunctions();
    builder.CreateCall(abortFunc, {});
    builder.CreateUnreachable();
    builder.SetInsertPoint(valid);
    return builder.CreateGEP(
        getLLVMTypeFromNode(type),
        storage,
        {llvm::ConstantInt::get(integer, 0), index},
        "fixedarray.index");
}
