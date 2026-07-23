#include "ir.h"

#include <llvm/IR/Constants.h>

bool CodeGenerator::evaluateCompileTimeInt(ExpressionNode* expr, int64_t& out)
{
    ConstexprValue value;
    if(!evalConstexprExpression(expr, value, nullptr, nullptr, 0))
        return false;
    out = value.kind == ConstexprValue::Kind::Bool
              ? (value.boolValue ? 1 : 0)
              : value.kind == ConstexprValue::Kind::Float
                    ? static_cast<int64_t>(value.floatValue)
                    : value.intValue;
    return true;
}

bool CodeGenerator::evaluateCompileTimeBool(ExpressionNode* expr, bool& out)
{
    ConstexprValue value;
    if(!evalConstexprExpression(expr, value, nullptr, nullptr, 0))
        return false;
    out = value.kind == ConstexprValue::Kind::Bool
              ? value.boolValue
              : value.kind == ConstexprValue::Kind::Float
                    ? value.floatValue != 0.0
                    : value.intValue != 0;
    return true;
}

std::optional<uint64_t> CodeGenerator::fixedArrayByteSize(TypeNode* typeNode)
{
    auto* arrayType = dynamic_cast<ArrayTypeNode*>(typeNode);
    if(!arrayType)
        return std::nullopt;

    llvm::Type* elemType = getLLVMTypeFromNode(arrayType->elementType);
    if(!elemType)
        return std::nullopt;

    const llvm::DataLayout& dl = module->getDataLayout();
    uint64_t elemBytes = dl.getTypeAllocSize(elemType).getFixedValue();
    return elemBytes * static_cast<uint64_t>(arrayType->capacity);
}

std::optional<int64_t>
CodeGenerator::fixedArrayInitializerSize(ExpressionNode* expr)
{
    if(auto* listLit = dynamic_cast<ListLiteralNode*>(expr))
    {
        if(!listLit->elements)
            return 0;
        return static_cast<int64_t>(listLit->elements->elements.size());
    }

    if(auto* fill = dynamic_cast<ArrayFillNode*>(expr))
    {
        int64_t count = 0;
        if(evaluateCompileTimeInt(fill->count, count))
            return count;
    }

    return std::nullopt;
}

std::optional<int64_t>
CodeGenerator::fixedArrayExpressionKnownLength(ExpressionNode* expr)
{
    if(!expr)
        return 0;

    if(auto size = fixedArrayInitializerSize(expr))
        return size;

    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto lenIt = arrayKnownLengths.find(id->name);
        if(lenIt != arrayKnownLengths.end())
            return lenIt->second;
    }

    return std::nullopt;
}

bool CodeGenerator::validateFixedArrayInitializer(TypeNode* declaredType,
                                                  ExpressionNode* expr,
                                                  int line)
{
    auto* arrayType = dynamic_cast<ArrayTypeNode*>(declaredType);
    if(!arrayType || !expr)
        return true;

    std::optional<int64_t> size = fixedArrayInitializerSize(expr);
    if(!size)
        return true;

    if(*size < 0)
    {
        reportError(line, "array initializer size must be non-negative");
        return false;
    }

    if(*size > arrayType->capacity)
    {
        reportError(line, "array initializer has " + std::to_string(*size) +
                              " elements but " + arrayType->toString() +
                              " capacity is " +
                              std::to_string(arrayType->capacity));
        return false;
    }

    return true;
}

bool CodeGenerator::convertValueToRuntimeBool(llvm::Value* value, int line,
                                              const std::string& context,
                                              llvm::Value*& outBool)
{
    if(!value)
        return false;
    llvm::Type* ty = value->getType();
    if(ty->isIntegerTy(1))
    {
        outBool = value;
        return true;
    }
    if(ty->isIntegerTy())
    {
        outBool = builder.CreateICmpNE(value, llvm::ConstantInt::get(ty, 0),
                                       context + ".bool");
        return true;
    }
    if(ty->isFloatingPointTy())
    {
        outBool = builder.CreateFCmpONE(value, llvm::ConstantFP::get(ty, 0.0),
                                        context + ".bool");
        return true;
    }
    std::string typeStr;
    if(ty->isStructTy())
        typeStr = ty->getStructName().str().empty() ? "struct"
                                                    : ty->getStructName().str();
    else if(ty->isPointerTy())
        typeStr = "pointer";
    else
        typeStr = "non-boolean";
    reportError(
        line, context + " condition must be a boolean or numeric type, got '" +
                  typeStr + "'");
    return false;
}
