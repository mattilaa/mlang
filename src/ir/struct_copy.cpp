#include "ir.h"
#include "ir/common.h"

using mlang::ir_detail::common::Helpers;

llvm::Value*
CodeGenerator::resetCopiedStructState(llvm::Value* value,
                                      const std::string& structName)
{
    if(!value)
        return nullptr;

    auto* structType = llvm::dyn_cast<llvm::StructType>(value->getType());
    if(!structType || structName.empty())
        return value;

    auto memberIt = structMembers.find(structName);
    if(memberIt == structMembers.end())
        return value;

    llvm::Value* adjusted = value;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        const StructFieldLayout* layout =
            getStructFieldLayout(structName, static_cast<int>(i));
        if(!layout || layout->packedBit)
            continue;

        const std::string& fieldName = members[i].first;
        TypeNode* fieldType = members[i].second;
        llvm::Type* storageType =
            structType->getElementType(layout->storageIndex);

        if(Helpers::isSynthesizedPropertyLockFieldName(fieldName))
        {
            adjusted = builder.CreateInsertValue(
                adjusted, llvm::Constant::getNullValue(storageType),
                {layout->storageIndex}, structName + "." + fieldName + ".copy");
            continue;
        }

        std::string nestedStructName;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(fieldType))
            nestedStructName = structRef->structName;
        else if(auto* genStructRef =
                    dynamic_cast<GenericStructTypeRefNode*>(fieldType))
            nestedStructName = getOrCreateMonomorphizedStruct(
                genStructRef->structName, genStructRef->typeArgs);

        if(nestedStructName.empty() || !storageType->isStructTy())
            continue;

        llvm::Value* nestedValue = builder.CreateExtractValue(
            adjusted, {layout->storageIndex},
            structName + "." + fieldName + ".copy.extract");
        llvm::Value* nestedAdjusted =
            resetCopiedStructState(nestedValue, nestedStructName);
        adjusted = builder.CreateInsertValue(
            adjusted, nestedAdjusted, {layout->storageIndex},
            structName + "." + fieldName + ".copy.insert");
    }

    return adjusted;
}

llvm::Value* CodeGenerator::applyStructCopySemantics(llvm::Value* value,
                                                     TypeNode* semanticType)
{
    if(!value)
        return nullptr;

    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(semanticType))
        return resetCopiedStructState(value, structRef->structName);

    if(auto* genStructRef =
           dynamic_cast<GenericStructTypeRefNode*>(semanticType))
    {
        return resetCopiedStructState(
            value, getOrCreateMonomorphizedStruct(genStructRef->structName,
                                                  genStructRef->typeArgs));
    }

    return applyStructCopySemantics(value);
}

llvm::Value* CodeGenerator::applyStructCopySemantics(llvm::Value* value)
{
    if(!value)
        return nullptr;

    auto* structType = llvm::dyn_cast<llvm::StructType>(value->getType());
    if(!structType || !structType->hasName())
        return value;

    return resetCopiedStructState(value, structType->getName().str());
}

