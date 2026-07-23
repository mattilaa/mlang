#include "ir.h"

#include <llvm/IR/Constants.h>

llvm::Value* CodeGenerator::generateSizeofExpression(SizeofExpressionNode* node)
{
    TypeNode* targetType = nullptr;
    if(node->typeTarget)
    {
        targetType = cloneTypeNode(node->typeTarget);
        if(auto* namedTarget =
               dynamic_cast<StructTypeRefNode*>(node->typeTarget))
        {
            auto varIt = variableTypes.find(namedTarget->structName);
            if(varIt != variableTypes.end())
            {
                IdentifierNode idExpr(namedTarget->structName);
                idExpr.line = node->line;
                targetType = inferExpressionTypeNode(&idExpr, node->line);
                if(auto* listType =
                       dynamic_cast<GenericListTypeNode*>(targetType))
                {
                    auto capIt = arrayCapacities.find(namedTarget->structName);
                    if(capIt != arrayCapacities.end())
                    {
                        targetType = new ArrayTypeNode(
                            cloneTypeNode(listType->elementType),
                            capIt->second);
                    }
                }
            }
        }
    }
    else
    {
        targetType =
            inferExpressionTypeNode(node->expressionTarget, node->line);
        if(auto* id = dynamic_cast<IdentifierNode*>(node->expressionTarget))
        {
            if(auto* listType = dynamic_cast<GenericListTypeNode*>(targetType))
            {
                auto capIt = arrayCapacities.find(id->name);
                if(capIt != arrayCapacities.end())
                {
                    targetType =
                        new ArrayTypeNode(cloneTypeNode(listType->elementType),
                                          capIt->second);
                }
            }
        }
    }

    if(!targetType)
    {
        reportError(node->line, "cannot infer type for size_of expression");
        return nullptr;
    }

    llvm::Type* llvmType = getLLVMTypeFromNode(targetType);
    if(!llvmType)
    {
        reportError(node->line, "cannot lower size_of target type");
        return nullptr;
    }

    uint64_t sizeBytes = 0;
    if(std::optional<uint64_t> arrayBytes = fixedArrayByteSize(targetType))
        sizeBytes = *arrayBytes;
    else
    {
        const llvm::DataLayout& dl = module->getDataLayout();
        sizeBytes = dl.getTypeAllocSize(llvmType).getFixedValue();
    }
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), sizeBytes);
}
