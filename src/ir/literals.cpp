#include "ir.h"
#include "ir/common.h"

using mlang::ir_detail::common::Helpers;

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constants.h>


llvm::Value* CodeGenerator::generateIntLiteral(IntLiteralNode* node)
{
    return llvm::ConstantInt::get(context, llvm::APInt(64, node->value, true));
}

llvm::Value* CodeGenerator::generateBoolLiteral(BoolLiteralNode* node)
{
    return llvm::ConstantInt::get(context, llvm::APInt(1, node->value ? 1 : 0));
}

llvm::Value* CodeGenerator::generateFloatLiteral(FloatLiteralNode* node)
{
    return llvm::ConstantFP::get(context, llvm::APFloat(node->value));
}

llvm::Value* CodeGenerator::generateDoubleLiteral(DoubleLiteralNode* node)
{
    return llvm::ConstantFP::get(context, llvm::APFloat(node->value));
}

llvm::Value* CodeGenerator::generateStringLiteral(StringLiteralNode* node)
{
#if LLVM_VERSION_MAJOR >= 21
    return builder.CreateGlobalString(node->value);
#else
    return builder.CreateGlobalStringPtr(node->value);
#endif
}

llvm::Value* CodeGenerator::generateEnumLiteral(EnumLiteralNode* node)
{
    std::string resolvedEnumName = resolveVisibleEnumName(node->enumName);
    if(resolvedEnumName.empty())
    {
        reportError(node->line, "unknown enum: '" + node->enumName + "'");
        return nullptr;
    }
    TypeNode::TypeKind baseKind = TypeNode::TYPE_I32;
    auto bkIt = enumBaseTypes.find(resolvedEnumName);
    if(bkIt != enumBaseTypes.end())
        baseKind = bkIt->second;
    if(Helpers::isEnumStringType(baseKind))
    {
        auto enumIt = enumStringValues.find(resolvedEnumName);
        if(enumIt == enumStringValues.end())
        {
            reportError(node->line, "unknown enum: '" + node->enumName + "'");
            return nullptr;
        }
        auto variantIt = enumIt->second.find(node->variantName);
        if(variantIt == enumIt->second.end())
        {
            reportError(node->line,
                        "unknown enum variant: '" + node->variantName + "'");
            return nullptr;
        }
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString(variantIt->second);
#else
        return builder.CreateGlobalStringPtr(variantIt->second);
#endif
    }
    auto enumIt = enumValues.find(resolvedEnumName);
    auto variantIt = enumIt->second.find(node->variantName);
    if(variantIt == enumIt->second.end())
    {
        reportError(node->line,
                    "unknown enum variant: '" + node->variantName + "'");
        return nullptr;
    }
    llvm::Type* enumTy = getLLVMType(baseKind);
    return llvm::ConstantInt::get(enumTy, variantIt->second,
                                  !Helpers::enumIsUnsigned(baseKind));
}

llvm::Value* CodeGenerator::generateIdentifier(IdentifierNode* node)
{
    auto constexprIt = constexprValues.find(node->name);
    if(constexprIt != constexprValues.end())
        return buildLLVMConstantFromConstexprValue(constexprIt->second, nullptr,
                                                   node->line);

    if(!validateVariableAccessible(node->name, node->line, node->col))
        return nullptr;

    llvm::Value* value = namedValues[node->name];

    if(llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
    {
        return builder.CreateLoad(alloca->getAllocatedType(), alloca,
                                  node->name);
    }
    if(llvm::GlobalVariable* global =
           llvm::dyn_cast<llvm::GlobalVariable>(value))
    {
        return builder.CreateLoad(global->getValueType(), global, node->name);
    }

    return value;
}
