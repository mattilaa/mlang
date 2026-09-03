#include "ir.h"
#include "ir/common.h"

#include <llvm/Config/llvm-config.h>

using mlang::ir_detail::common::Helpers;

void CodeGenerator::generateStructDefinition(StructDefNode* node)
{
    std::vector<llvm::Type*> memberTypes;
    std::vector<std::pair<std::string, TypeNode*>> members;
    std::vector<StructFieldLayout> layouts;
    std::vector<StructFieldAccessInfo> accessInfo;

    // If this struct has a base, include base struct's fields first
    if(!node->baseName.empty())
    {
        auto baseMemIt = structMembers.find(node->baseName);
        if(baseMemIt != structMembers.end())
        {
            llvm::StructType* baseStructType = getStructType(node->baseName);
            auto baseLayoutIt = structFieldLayouts.find(node->baseName);
            for(const auto& baseMember : baseMemIt->second)
                members.push_back(baseMember);
            auto baseAccessIt = structFieldAccessInfo.find(node->baseName);
            if(baseAccessIt != structFieldAccessInfo.end())
                accessInfo.insert(accessInfo.end(),
                                  baseAccessIt->second.begin(),
                                  baseAccessIt->second.end());
            if(baseLayoutIt != structFieldLayouts.end())
                layouts.insert(layouts.end(), baseLayoutIt->second.begin(),
                               baseLayoutIt->second.end());
            if(baseStructType)
            {
                for(unsigned i = 0; i < baseStructType->getNumElements(); ++i)
                    memberTypes.push_back(baseStructType->getElementType(i));
            }
        }
        else
        {
            reportError(node->line,
                        "base struct '" + node->baseName + "' not found");
        }
    }

    // Add this struct's own members
    bool packingBitRun = false;
    unsigned packedStorageIndex = 0;
    unsigned packedBitOffset = 0;
    for(auto member : node->members->members)
    {
        members.push_back({member->name, member->type});
        StructFieldAccessInfo fieldAccess;
        fieldAccess.ownerStructName = node->name;
        fieldAccess.isProperty = member->isProperty;
        fieldAccess.isAtomicProperty = member->isAtomicProperty;
        fieldAccess.isMutexProperty = member->isMutexProperty;
        fieldAccess.isRecursiveProperty = member->isRecursiveProperty;
        fieldAccess.isSynthesizedPropertyStorage =
            member->isSynthesizedPropertyStorage;
        if(member->isHiddenProperty || member->isSynthesizedPropertyStorage)
            fieldAccess.encapsulation = FieldEncapsulation::Hidden;
        else if(member->isProtectedProperty)
            fieldAccess.encapsulation = FieldEncapsulation::Protected;
        accessInfo.push_back(fieldAccess);
        StructFieldLayout layout;
        if(Helpers::isBitFieldTypeNode(member->type))
        {
            if(!packingBitRun || packedBitOffset >= 8)
            {
                packedStorageIndex = static_cast<unsigned>(memberTypes.size());
                memberTypes.push_back(llvm::Type::getInt8Ty(context));
                packedBitOffset = 0;
                packingBitRun = true;
            }
            layout.storageIndex = packedStorageIndex;
            layout.packedBit = true;
            layout.bitOffset = packedBitOffset++;
        }
        else
        {
            packingBitRun = false;
            packedBitOffset = 0;
            layout.storageIndex = static_cast<unsigned>(memberTypes.size());
            memberTypes.push_back(getLLVMTypeFromNode(member->type));
        }
        layouts.push_back(layout);
    }

    llvm::StructType* structType = getStructType(node->name);
    if(structType)
    {
        if(structType->isOpaque())
            structType->setBody(memberTypes, false);
    }
    else
    {
        structType = llvm::StructType::create(context, memberTypes, node->name);
    }
    structTypes[node->name] = structType;
    structMembers[node->name] = members;
    structFieldLayouts[node->name] = layouts;
    structFieldAccessInfo[node->name] = accessInfo;
    if(!node->debugDisplayName.empty())
        structDebugDisplayNames[node->name] = node->debugDisplayName;

    // Track per-field default initializers so that struct-literal
    // construction can fill in unspecified fields from declaration-site
    // defaults (e.g. `let x: i32{7};` or `let p: Point{x: 3};`).
    {
        auto& defaults = structMemberDefaults[node->name];
        for(auto* member : node->members->members)
        {
            if(member && member->initExpr)
                defaults[member->name] = member->initExpr;
        }
    }
    if(node->deriveDebug)
        debugStructs.insert(node->name);
    if(node->deriveJson)
        jsonStructs.insert(node->name);

    // Track base name for inheritance lookups
    if(!node->baseName.empty())
    {
        structBases[node->name] = node->baseName;
    }
}
llvm::StructType* CodeGenerator::getStructType(const std::string& name)
{
    auto it = structTypes.find(name);
    if(it != structTypes.end())
    {
        return llvm::cast<llvm::StructType>(it->second);
    }
    return nullptr;
}

llvm::Type* CodeGenerator::getTraitObjectType(const std::string& traitName)
{
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
    llvm::Type* opaquePtr =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    auto it = traitObjectTypes.find(traitName);
    if(it != traitObjectTypes.end())
        return it->second;
    auto* ty = llvm::StructType::create(context, {opaquePtr, opaquePtr},
                                        "trait.obj." + traitName);
    traitObjectTypes[traitName] = ty;
    return ty;
}

llvm::Type* CodeGenerator::getTraitVTableType(const std::string& traitName)
{
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
    llvm::Type* opaquePtr =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    auto it = traitVTableTypes.find(traitName);
    if(it != traitVTableTypes.end())
        return it->second;

    auto traitIt = traitDefinitions.find(traitName);
    if(traitIt == traitDefinitions.end())
    {
        for(auto it = traitDefinitions.begin(); it != traitDefinitions.end();
            ++it)
        {
            if(Helpers::trait_names_equivalent(it->first, traitName))
            {
                traitIt = it;
                break;
            }
        }
    }
    if(traitIt == traitDefinitions.end() || !traitIt->second)
        return nullptr;

    std::vector<llvm::Type*> fields;
    fields.reserve(traitIt->second->methods.size());
    for(size_t i = 0; i < traitIt->second->methods.size(); ++i)
        fields.push_back(opaquePtr);

    auto* ty =
        llvm::StructType::create(context, fields, "trait.vtable." + traitName);
    traitVTableTypes[traitName] = ty;
    return ty;
}

llvm::GlobalVariable*
CodeGenerator::ensureTraitVTable(const std::string& concreteTypeName,
                                 const std::string& traitName)
{
    std::string key = concreteTypeName + "::" + traitName;
    auto it = traitVTableGlobals.find(key);
    if(it != traitVTableGlobals.end())
        return it->second;

    auto traitIt = traitDefinitions.find(traitName);
    if(traitIt == traitDefinitions.end())
    {
        for(auto it = traitDefinitions.begin(); it != traitDefinitions.end();
            ++it)
        {
            if(Helpers::trait_names_equivalent(it->first, traitName))
            {
                traitIt = it;
                break;
            }
        }
    }
    if(traitIt == traitDefinitions.end() || !traitIt->second)
    {
        reportError(0, "unknown trait object type: '" + traitName + "'");
        return nullptr;
    }
    TraitDefNode* traitDef = traitIt->second;
    llvm::Type* vtableType = getTraitVTableType(traitName);
    if(!vtableType)
        return nullptr;

    std::vector<llvm::Constant*> entries;
    entries.reserve(traitDef->methods.size());
    for(auto* traitMethod : traitDef->methods)
    {
        if(!traitMethod)
            continue;
        auto methodsIt = structMethods.find(concreteTypeName);
        if(methodsIt == structMethods.end())
        {
            reportError(traitMethod->line, "unknown struct '" +
                                               concreteTypeName +
                                               "' for trait object dispatch");
            return nullptr;
        }
        auto mit = methodsIt->second.find(traitMethod->name);
        if(mit == methodsIt->second.end() || !mit->second.second)
        {
            reportError(traitMethod->line,
                        "missing method '" + traitMethod->name +
                            "' for trait object dispatch on '" +
                            concreteTypeName + "'");
            return nullptr;
        }
        StructMethodNode* concreteMethod = mit->second.second;
        llvm::Function* function =
            generateMethodDeclaration(concreteTypeName, concreteMethod);
        if(function && function->empty())
            function =
                generateMethodDefinition(concreteTypeName, concreteMethod);
        if(!function)
            return nullptr;

#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
        llvm::Type* opaquePtr =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        entries.push_back(llvm::ConstantExpr::getBitCast(function, opaquePtr));
    }

    auto* init = llvm::ConstantStruct::get(
        llvm::cast<llvm::StructType>(vtableType), entries);
    auto* gv = new llvm::GlobalVariable(
        *module, llvm::cast<llvm::StructType>(vtableType), true,
        llvm::GlobalValue::PrivateLinkage, init, "__mlang_trait_vtable." + key);
    traitVTableGlobals[key] = gv;
    return gv;
}

llvm::Value* CodeGenerator::buildTraitObjectValue(ExpressionNode* expr,
                                                  const std::string& traitName,
                                                  int line, bool heapCopy)
{
    llvm::Value* dataPtr = getLValuePointer(expr, line);
    if(!dataPtr)
        return nullptr;

    TypeNode* lvalueType = getLValueType(expr, line);
    if(!lvalueType)
        return nullptr;

    std::string concreteTypeName;
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(lvalueType))
    {
        concreteTypeName = resolveVisibleStructName(structRef->structName);
    }
    else if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(lvalueType))
    {
        concreteTypeName = getOrCreateMonomorphizedStruct(genRef->structName,
                                                          genRef->typeArgs);
    }
    else
    {
        reportError(line, "trait object arguments require a struct value");
        return nullptr;
    }

    if(concreteTypeName.empty())
    {
        reportError(line, "unknown concrete type for trait object dispatch");
        return nullptr;
    }
    auto implIt = structImplementedTraits.find(concreteTypeName);
    std::string implementedTraitName;
    if(implIt != structImplementedTraits.end())
    {
        for(const auto& candidate : implIt->second)
        {
            if(Helpers::trait_names_equivalent(candidate, traitName))
            {
                implementedTraitName = candidate;
                break;
            }
        }
    }
    if(implementedTraitName.empty())
    {
        reportError(line, "type '" + concreteTypeName +
                              "' does not implement trait '" + traitName + "'");
        return nullptr;
    }

    llvm::GlobalVariable* vtable =
        ensureTraitVTable(concreteTypeName, implementedTraitName);
    if(!vtable)
        return nullptr;

    if(heapCopy)
    {
        llvm::Type* concreteType = getLLVMTypeFromNode(lvalueType);
        if(!concreteType)
            return nullptr;

        const llvm::DataLayout& dl = module->getDataLayout();
        auto sizeBytes = dl.getTypeAllocSize(concreteType).getFixedValue();
        llvm::Value* sizeVal =
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), sizeBytes);
        llvm::Value* heapPtr =
            builder.CreateCall(mallocFunc, {sizeVal}, "trait.obj.heap");
        llvm::Value* concreteValue =
            builder.CreateLoad(concreteType, dataPtr, "trait.obj.copy");
        builder.CreateStore(concreteValue, heapPtr);
        dataPtr = heapPtr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* opaquePtr = llvm::PointerType::get(context, 0);
#else
    llvm::Type* opaquePtr =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Value* dataPtrI8 =
        builder.CreateBitCast(dataPtr, opaquePtr, "trait.obj.data");
    llvm::Value* vtablePtr =
        builder.CreateBitCast(vtable, opaquePtr, "trait.obj.vtable");

    llvm::StructType* objType =
        llvm::cast<llvm::StructType>(getTraitObjectType(traitName));
    llvm::Value* obj = llvm::Constant::getNullValue(objType);
    obj = builder.CreateInsertValue(obj, dataPtrI8, 0, "trait.obj.data");
    obj = builder.CreateInsertValue(obj, vtablePtr, 1, "trait.obj.vtable");
    return obj;
}

llvm::Value* CodeGenerator::coerceTraitObjectValue(llvm::Value* value,
                                                   llvm::Type* expectedType,
                                                   int line)
{
    if(!value || !expectedType)
        return nullptr;
    if(value->getType() == expectedType)
        return value;

    auto* actualStruct = llvm::dyn_cast<llvm::StructType>(value->getType());
    auto* expectedStruct = llvm::dyn_cast<llvm::StructType>(expectedType);
    if(!actualStruct || !expectedStruct ||
       actualStruct->getNumElements() != 2 ||
       expectedStruct->getNumElements() != 2)
    {
        reportError(line, "trait object type mismatch");
        return nullptr;
    }

    llvm::Value* dataPtr =
        builder.CreateExtractValue(value, 0, "trait.obj.data");
    llvm::Value* vtablePtr =
        builder.CreateExtractValue(value, 1, "trait.obj.vtable");
    llvm::Value* coerced = llvm::Constant::getNullValue(expectedType);
    coerced = builder.CreateInsertValue(coerced, dataPtr, 0,
                                        "trait.obj.coerced.data");
    coerced = builder.CreateInsertValue(coerced, vtablePtr, 1,
                                        "trait.obj.coerced.vtable");
    return coerced;
}
