#include "ir.h"
#include "ir/common.h"
#include <llvm/IR/InlineAsm.h>

bool CodeGenerator::isHardwareIntrinsic(const std::string& name) const
{
    return name.rfind("ptr_from_address<", 0) == 0 || name == "ptr_address" ||
           name == "volatile_load" || name == "volatile_store" ||
           name == "compiler_barrier";
}

TypeNode* CodeGenerator::hardwareIntrinsicType(FunctionCallNode* node)
{
    if(node->name.rfind("ptr_from_address<", 0) == 0 &&
       node->name.back() == '>')
    {
        const std::string prefix = "ptr_from_address<";
        auto* element = mlang::ir_detail::common::Helpers::type_from_text(
            node->name
                .substr(prefix.size(), node->name.size() - prefix.size() - 1));
        return new PointerTypeNode(element);
    }
    if(node->name == "ptr_address")
        return new TypeNode(
            module->getDataLayout().getPointerSizeInBits() == 32
                ? TypeNode::TYPE_U32
                : TypeNode::TYPE_U64);
    if(node->name == "volatile_load" && node->arguments.size() == 1)
    {
        if(auto* pointer = dynamic_cast<PointerTypeNode*>(
               inferExpressionTypeNode(node->arguments[0], node->line)))
            return cloneTypeNode(pointer->elementType);
        return nullptr;
    }
    return new TypeNode(TypeNode::TYPE_VOID);
}

llvm::Value* CodeGenerator::generateHardwareIntrinsic(FunctionCallNode* node)
{
    if(unsafeDepth <= 0 && !structuralRequirementProbe)
    {
        reportError(node->line, node->name + " requires an unsafe block");
        return nullptr;
    }
    const size_t count = node->name == "compiler_barrier" ? 0
                         : node->name == "volatile_store" ? 2
                                                          : 1;
    if(node->arguments.size() != count)
    {
        reportError(
            node->line,
            "invalid argument count for hardware intrinsic '" + node->name +
                "'");
        return nullptr;
    }
    if(node->name == "compiler_barrier")
    {
        auto* type =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context), false);
        auto* assembly = llvm::InlineAsm::get(type, "", "~{memory}", true);
        return builder.CreateCall(assembly, {});
    }
    if(node->name.rfind("ptr_from_address<", 0) == 0)
    {
        auto* resultType = hardwareIntrinsicType(node);
        llvm::Type* type = getLLVMTypeFromNode(resultType);
        llvm::Value* address = generateExpression(node->arguments[0]);
        if(!type || !address)
            return nullptr;
        auto* integer = module->getDataLayout().getIntPtrType(context);
        TypeNode* sourceType =
            inferExpressionTypeNode(node->arguments[0], node->line);
        if(!sourceType || getLLVMTypeFromNode(sourceType) != integer)
        {
            reportError(
                node->line,
                "ptr_from_address requires an integer with the target pointer "
                "width");
            return nullptr;
        }
        address = builder.CreateIntCast(address, integer, false);
        return builder.CreateIntToPtr(address, type, "hardware.pointer");
    }
    auto* pointer = dynamic_cast<PointerTypeNode*>(
        inferExpressionTypeNode(node->arguments[0], node->line));
    if(!pointer)
    {
        reportError(node->line, node->name + " requires a typed raw pointer");
        return nullptr;
    }
    if(node->name == "volatile_store" &&
       !validateBorrowWriteThroughPointer(node->arguments[0], node->line))
        return nullptr;
    if(node->name == "volatile_load" &&
       !validateBorrowRead(node->arguments[0], node->line))
        return nullptr;
    llvm::Value* address = generateExpression(node->arguments[0]);
    if(!address)
        return nullptr;
    if(node->name == "ptr_address")
        return builder.CreatePtrToInt(
            address,
            module->getDataLayout().getIntPtrType(context),
            "hardware.address");
    llvm::Type* element = getLLVMTypeFromNode(pointer->elementType);
    if(!element || !(element->isIntegerTy(8) || element->isIntegerTy(16) ||
                     element->isIntegerTy(32) || element->isIntegerTy(64)))
    {
        reportError(
            node->line,
            "volatile access requires an 8, 16, 32, or 64-bit integer element "
            "type");
        return nullptr;
    }
    if(!emitRuntimeNullPointerCheck(address, node->line))
        return nullptr;
    if(node->name == "volatile_load")
    {
        auto* load = builder.CreateLoad(element, address, "hardware.load");
        load->setVolatile(true);
        return load;
    }
    llvm::Value* value =
        generateExpression(node->arguments[1], pointer->elementType);
    if(!value)
        return nullptr;
    auto* literal = dynamic_cast<IntLiteralNode*>(node->arguments[1]);
    TypeNode* valueType =
        inferExpressionTypeNode(node->arguments[1], node->line);
    const bool contextualLiteral =
        literal && literal->explicitType == TypeNode::TYPE_VOID;
    if(!value->getType()->isIntegerTy() || !valueType ||
       (!contextualLiteral &&
        typeMangle(valueType) != typeMangle(pointer->elementType)))
    {
        reportError(
            node->line,
            "volatile_store value must match the pointer element type");
        return nullptr;
    }
    value = builder.CreateIntCast(
        value,
        element,
        !isUnsignedType(pointer->elementType->kind));
    auto* store = builder.CreateStore(value, address);
    store->setVolatile(true);
    return store;
}
