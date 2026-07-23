#include "ir.h"

#include <llvm/IR/Constants.h>

bool CodeGenerator::isStringExpression(ExpressionNode* expr) const
{
    if(dynamic_cast<StringLiteralNode*>(expr))
        return true;
    if(dynamic_cast<FormatNode*>(expr))
        return true;
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = variableTypes.find(id->name);
        return it != variableTypes.end() && it->second == TypeNode::TYPE_STRING;
    }
    return false;
}

std::string CodeGenerator::getStructTypeName(ExpressionNode* expr) const
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = structVariableTypes.find(id->name);
        if(it != structVariableTypes.end())
            return it->second;
    }
    if(auto* lit = dynamic_cast<StructLiteralNode*>(expr))
        return lit->structName;
    return {};
}

std::string CodeGenerator::getEnumTypeName(ExpressionNode* expr, int line)
{
    if(auto* lit = dynamic_cast<EnumLiteralNode*>(expr))
        return lit->enumName;

    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = enumVariableTypes.find(id->name);
        if(it != enumVariableTypes.end())
            return it->second;
        return {};
    }

    return {};
}

bool CodeGenerator::structHasFieldNamed(const std::string& structTypeName,
                                        const std::string& fieldName) const
{
    auto it = structMembers.find(structTypeName);
    if(it == structMembers.end())
        return false;
    for(const auto& member : it->second)
    {
        if(member.first == fieldName)
            return true;
    }
    return false;
}

const CodeGenerator::StructFieldLayout*
CodeGenerator::getStructFieldLayout(const std::string& structName,
                                    int fieldIndex) const
{
    auto it = structFieldLayouts.find(structName);
    if(it == structFieldLayouts.end())
        return nullptr;
    if(fieldIndex < 0 || static_cast<size_t>(fieldIndex) >= it->second.size())
        return nullptr;
    return &it->second[static_cast<size_t>(fieldIndex)];
}

const CodeGenerator::StructFieldAccessInfo*
CodeGenerator::getStructFieldAccessInfo(const std::string& structName,
                                        int fieldIndex) const
{
    auto it = structFieldAccessInfo.find(structName);
    if(it == structFieldAccessInfo.end())
        return nullptr;
    if(fieldIndex < 0 || static_cast<size_t>(fieldIndex) >= it->second.size())
        return nullptr;
    return &it->second[static_cast<size_t>(fieldIndex)];
}

bool CodeGenerator::isStructSameOrDerivedFrom(
    const std::string& candidateStruct, const std::string& baseStruct) const
{
    if(candidateStruct.empty() || baseStruct.empty())
        return false;
    if(candidateStruct == baseStruct)
        return true;

    std::string current = candidateStruct;
    std::set<std::string> seen;
    while(!current.empty() && seen.insert(current).second)
    {
        auto it = structBases.find(current);
        if(it == structBases.end())
            return false;
        current = it->second;
        if(current == baseStruct)
            return true;
    }
    return false;
}

bool CodeGenerator::canAccessStructField(
    const std::string& accessedThroughStruct, int fieldIndex, int line,
    const std::string& fieldName) const
{
    const StructFieldAccessInfo* accessInfo =
        getStructFieldAccessInfo(accessedThroughStruct, fieldIndex);
    if(!accessInfo)
        return true;

    switch(accessInfo->encapsulation)
    {
    case FieldEncapsulation::Public:
        return true;
    case FieldEncapsulation::Hidden:
        if(currentStructContext == accessInfo->ownerStructName)
            return true;
        break;
    case FieldEncapsulation::Protected:
        if(isStructSameOrDerivedFrom(currentStructContext,
                                     accessInfo->ownerStructName))
            return true;
        break;
    }

    std::string message = "field '" + fieldName + "' of struct '" +
                          accessInfo->ownerStructName + "'";
    if(accessInfo->encapsulation == FieldEncapsulation::Hidden)
    {
        message += " is hidden";
    }
    else
    {
        message += " is protected";
    }

    if(currentStructContext.empty())
    {
        message += " and cannot be accessed here";
    }
    else
    {
        message += " and cannot be accessed from struct '" +
                   currentStructContext + "'";
    }
    const_cast<CodeGenerator*>(this)->reportError(line, message);
    return false;
}

llvm::Value* CodeGenerator::loadStructFieldValue(
    const std::string& structTypeName, llvm::Value* structPtr, int fieldIndex,
    TypeNode* fieldType, const std::string& fieldName)
{
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
        return nullptr;

    const StructFieldLayout* layout =
        getStructFieldLayout(structTypeName, fieldIndex);
    if(!layout)
        return nullptr;

    llvm::Type* storageType = structType->getElementType(layout->storageIndex);
    llvm::Value* fieldPtr = builder.CreateStructGEP(
        structType, structPtr, layout->storageIndex, fieldName + "_ptr");
    llvm::Value* storage = builder.CreateLoad(storageType, fieldPtr, fieldName);

    if(!layout->packedBit)
        return storage;

    llvm::Value* shifted = builder.CreateLShr(
        storage, llvm::ConstantInt::get(storageType, layout->bitOffset),
        fieldName + ".bit_shift");
    llvm::Value* masked = builder.CreateAnd(
        shifted, llvm::ConstantInt::get(storageType, 1), fieldName + ".bit");
    return builder.CreateTrunc(masked, llvm::Type::getInt1Ty(context),
                               fieldName);
}

void CodeGenerator::storeStructFieldValue(const std::string& structTypeName,
                                          llvm::Value* structPtr,
                                          int fieldIndex, TypeNode* fieldType,
                                          llvm::Value* value,
                                          const std::string& fieldName,
                                          int line)
{
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
        return;

    const StructFieldLayout* layout =
        getStructFieldLayout(structTypeName, fieldIndex);
    if(!layout)
        return;

    llvm::Value* fieldPtr = builder.CreateStructGEP(
        structType, structPtr, layout->storageIndex, fieldName + "_ptr");
    llvm::Type* storageType = structType->getElementType(layout->storageIndex);

    if(!layout->packedBit)
    {
        value = applyStructCopySemantics(value, fieldType);
        builder.CreateStore(value, fieldPtr);
        return;
    }

    llvm::Value* storage =
        builder.CreateLoad(storageType, fieldPtr, fieldName + ".storage");
    llvm::Value* zextValue = value;
    if(!value->getType()->isIntegerTy(1))
    {
        reportError(line, "bit field '" + fieldName + "' expects a bit value");
        return;
    }
    zextValue = builder.CreateZExt(value, storageType, fieldName + ".zext");
    llvm::Value* shifted = builder.CreateShl(
        zextValue, llvm::ConstantInt::get(storageType, layout->bitOffset),
        fieldName + ".shifted");
    llvm::Value* mask = llvm::ConstantInt::get(
        storageType, static_cast<uint64_t>(1u) << layout->bitOffset);
    llvm::Value* cleared = builder.CreateAnd(storage, builder.CreateNot(mask),
                                             fieldName + ".clear");
    llvm::Value* combined =
        builder.CreateOr(cleared, shifted, fieldName + ".combined");
    builder.CreateStore(combined, fieldPtr);
}
