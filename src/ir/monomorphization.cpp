#include "ir.h"
#include "ir/common.h"

#include <cctype>
#include <functional>
#include <iostream>

namespace
{

using mlang::ir_detail::isBitFieldTypeNode;

static std::string generateMangledName(const std::string& baseName,
                                       const std::vector<TypeNode*>& typeArgs)
{
    auto normalize_type_name = [](const std::string& n) -> std::string
    {
        std::string out;
        out.reserve(n.size());
        char prev = '\0';
        for(size_t i = 0; i < n.size(); ++i)
        {
            char c = n[i];
            bool keep = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            char w = keep ? c : '_';
            if(w == '_' && prev == '_')
                continue;
            out.push_back(w);
            prev = w;
        }
        while(!out.empty() && out.back() == '_')
            out.pop_back();
        return out.empty() ? "unknown" : out;
    };

    std::function<std::string(TypeNode*)> mangle_type =
        [&](TypeNode* t) -> std::string
    {
        if(!t)
            return "unknown";
        if(auto* sr = dynamic_cast<StructTypeRefNode*>(t))
            return normalize_type_name(sr->structName);
        if(auto* gs = dynamic_cast<GenericStructTypeRefNode*>(t))
            return gs->getMangledName();
        if(auto* gl = dynamic_cast<GenericListTypeNode*>(t))
            return "list_" + mangle_type(gl->elementType);
        if(auto* mp = dynamic_cast<MapTypeNode*>(t))
            return "map_" + mangle_type(mp->keyType) + "_" +
                   mangle_type(mp->valueType);
        if(auto* tp = dynamic_cast<TupleTypeNode*>(t))
        {
            std::string out = "tuple";
            if(tp->elementTypes)
            {
                for(auto* e : tp->elementTypes->types)
                    out += "_" + mangle_type(e);
            }
            return out;
        }
        switch(t->kind)
        {
        case TypeNode::TYPE_BOOL:
            return "bool";
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
            return "i32";
        case TypeNode::TYPE_I8:
            return "i8";
        case TypeNode::TYPE_I16:
            return "i16";
        case TypeNode::TYPE_I64:
            return "i64";
        case TypeNode::TYPE_U8:
            return "u8";
        case TypeNode::TYPE_U16:
            return "u16";
        case TypeNode::TYPE_U32:
            return "u32";
        case TypeNode::TYPE_U64:
            return "u64";
        case TypeNode::TYPE_FLOAT:
            return "f32";
        case TypeNode::TYPE_DOUBLE:
            return "f64";
        case TypeNode::TYPE_STRING:
            return "str8";
        case TypeNode::TYPE_STR8:
            return "str8";
        case TypeNode::TYPE_STR16:
            return "str16";
        case TypeNode::TYPE_LIST:
            return "list";
        case TypeNode::TYPE_MAP:
            return "map";
        case TypeNode::TYPE_TUPLE:
            return "tuple";
        default:
            return "unknown";
        }
    };

    std::string mangled = baseName;
    for(auto* typeArg : typeArgs)
    {
        mangled += "_" + mangle_type(typeArg);
    }
    return mangled;
}

} // namespace

// Monomorphize a generic struct with concrete type arguments
void CodeGenerator::monomorphizeStruct(const std::string& genericName,
                                       const std::vector<TypeNode*>& typeArgs,
                                       const std::string& mangledName)
{
    // Find the generic template
    auto templateIt = genericStructTemplates.find(genericName);
    if(templateIt == genericStructTemplates.end())
    {
        std::cerr << "Error: Generic struct template '" << genericName
                  << "' not found" << std::endl;
        hasError = true;
        return;
    }

    StructDefNode* templateStruct = templateIt->second;
    const std::vector<std::string>& typeParams = templateStruct->typeParams;

    if(typeParams.size() != typeArgs.size())
    {
        std::cerr << "Error: Type argument count mismatch for '" << genericName
                  << "': expected " << typeParams.size() << ", got "
                  << typeArgs.size() << std::endl;
        hasError = true;
        return;
    }

    if(!validateTypeArgumentTraitBounds(typeParams,
                                        templateStruct->typeParamTraitBounds,
                                        typeArgs, {}, 0, "struct", genericName))
    {
        hasError = true;
        return;
    }

    // Generate the monomorphized struct type
    std::vector<llvm::Type*> memberTypes;
    std::vector<std::pair<std::string, TypeNode*>> members;
    std::vector<StructFieldLayout> layouts;
    std::vector<StructFieldAccessInfo> accessInfo;

    // Process each member, substituting type parameters
    bool packingBitRun = false;
    unsigned packedStorageIndex = 0;
    unsigned packedBitOffset = 0;
    if(templateStruct->members)
    {
        for(auto* member : templateStruct->members->members)
        {
            TypeNode* substitutedType =
                substituteTypeParams(member->type, typeParams, typeArgs);

            members.push_back({member->name, substitutedType});
            StructFieldAccessInfo fieldAccess;
            fieldAccess.ownerStructName = mangledName;
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
            if(isBitFieldTypeNode(substitutedType))
            {
                if(!packingBitRun || packedBitOffset >= 8)
                {
                    packedStorageIndex =
                        static_cast<unsigned>(memberTypes.size());
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
                llvm::Type* llvmType = getLLVMTypeFromNode(substitutedType);
                if(!llvmType)
                {
                    std::cerr << "Error: Failed to get LLVM type for member '"
                              << member->name << "' in " << mangledName
                              << std::endl;
                    hasError = true;
                    return;
                }
                packingBitRun = false;
                packedBitOffset = 0;
                layout.storageIndex = static_cast<unsigned>(memberTypes.size());
                memberTypes.push_back(llvmType);
            }
            layouts.push_back(layout);
        }
    }

    // Create the LLVM struct type
    llvm::StructType* structType =
        llvm::StructType::create(context, memberTypes, mangledName);

    // Register the monomorphized type
    structTypes[mangledName] = structType;
    structMembers[mangledName] = members;
    structFieldLayouts[mangledName] = layouts;
    structFieldAccessInfo[mangledName] = accessInfo;
    monomorphizedTypes.insert(mangledName);
    mangledToGenericName[mangledName] = genericName;
    std::vector<TypeNode*> storedTypeArgs;
    storedTypeArgs.reserve(typeArgs.size());
    for(auto* arg : typeArgs)
        storedTypeArgs.push_back(cloneTypeNode(arg));
    monomorphizedTypeArgs[mangledName] = std::move(storedTypeArgs);
    if(templateStruct->deriveDebug)
        debugStructs.insert(mangledName);
    if(templateStruct->deriveJson)
        jsonStructs.insert(mangledName);

    // Copy visibility from template
    structVisibility[mangledName] =
        std::make_pair(templateStruct->isPublic, templateStruct->sourceModule);

    // Store the type params and args for later method generation
    // We'll generate methods lazily when they're called

    // Process impl blocks - just register methods, don't generate bodies yet
    auto implIt = genericImplBlocks.find(genericName);
    if(implIt != genericImplBlocks.end())
    {
        for(auto* impl : implIt->second)
        {
            if(!validateTypeArgumentTraitBounds(
                   impl->typeParams, impl->typeParamTraitBounds, typeArgs, {},
                   0, "impl", genericName, false))
            {
                continue;
            }
            if(!impl->traitName.empty())
            {
                structImplementedTraits[mangledName].insert(impl->traitName);
            }
            for(auto* method : impl->methods)
            {
                // Substitute types in return type
                TypeNode* newReturnType = substituteTypeParams(
                    method->returnType, typeParams, typeArgs);

                // Substitute types in parameters
                auto* newParams = new ParameterListNode();
                for(auto* param : method->parameters->parameters)
                {
                    TypeNode* newParamType =
                        substituteTypeParams(param->type, typeParams, typeArgs);
                    newParams->parameters.push_back(
                        new ParameterNode(newParamType, param->name));
                }

                // Create monomorphized method node
                auto* newMethod = new StructMethodNode(
                    newReturnType, method->name, newParams, method->body,
                    method->isPublic, method->isStatic);
                newMethod->sourceModule = method->sourceModule;
                newMethod->isSynthesizedPropertyAccessor =
                    method->isSynthesizedPropertyAccessor;
                newMethod->isAtomicPropertyAccessor =
                    method->isAtomicPropertyAccessor;
                newMethod->isMutexPropertyAccessor =
                    method->isMutexPropertyAccessor;
                newMethod->isRecursiveMutexPropertyAccessor =
                    method->isRecursiveMutexPropertyAccessor;
                newMethod->isSynthesizedJsonSerializer =
                    method->isSynthesizedJsonSerializer;
                newMethod->isSynthesizedJsonTextDeserializer =
                    method->isSynthesizedJsonTextDeserializer;
                newMethod->isSynthesizedJsonValueDeserializer =
                    method->isSynthesizedJsonValueDeserializer;
                newMethod->isPropertySetter = method->isPropertySetter;
                newMethod->propertyFieldName = method->propertyFieldName;
                newMethod->propertyLockFieldName =
                    method->propertyLockFieldName;

                // Register the method (but don't generate code yet)
                structMethods[mangledName][method->name] =
                    std::make_pair(method->isPublic, newMethod);

                // Generate only the declaration (forward declaration)
                generateMethodDeclaration(mangledName, newMethod);
            }
        }
    }

    // Process methods defined inside the struct template
    if(templateStruct->members)
    {
        for(auto* method : templateStruct->members->methods)
        {
            // Substitute types in return type and parameters
            TypeNode* newReturnType =
                substituteTypeParams(method->returnType, typeParams, typeArgs);

            auto* newParams = new ParameterListNode();
            for(auto* param : method->parameters->parameters)
            {
                TypeNode* newParamType =
                    substituteTypeParams(param->type, typeParams, typeArgs);
                newParams->parameters.push_back(
                    new ParameterNode(newParamType, param->name));
            }

            // Create a new method node with substituted types
            auto* newMethod = new StructMethodNode(
                newReturnType, method->name, newParams, method->body,
                method->isPublic, method->isStatic);
            newMethod->sourceModule = method->sourceModule;
            newMethod->isSynthesizedPropertyAccessor =
                method->isSynthesizedPropertyAccessor;
            newMethod->isAtomicPropertyAccessor =
                method->isAtomicPropertyAccessor;
            newMethod->isMutexPropertyAccessor =
                method->isMutexPropertyAccessor;
            newMethod->isRecursiveMutexPropertyAccessor =
                method->isRecursiveMutexPropertyAccessor;
            newMethod->isSynthesizedJsonSerializer =
                method->isSynthesizedJsonSerializer;
            newMethod->isSynthesizedJsonTextDeserializer =
                method->isSynthesizedJsonTextDeserializer;
            newMethod->isSynthesizedJsonValueDeserializer =
                method->isSynthesizedJsonValueDeserializer;
            newMethod->isPropertySetter = method->isPropertySetter;
            newMethod->propertyFieldName = method->propertyFieldName;
            newMethod->propertyLockFieldName = method->propertyLockFieldName;

            // Register the method
            structMethods[mangledName][method->name] =
                std::make_pair(method->isPublic, newMethod);

            // Generate only the declaration
            generateMethodDeclaration(mangledName, newMethod);
        }
    }
}

// Monomorphize an impl block (Note: this is no longer called from
// monomorphizeStruct but kept for potential future use)
void CodeGenerator::monomorphizeImplBlock(
    ImplBlockNode* impl, const std::vector<std::string>& typeParams,
    const std::vector<TypeNode*>& typeArgs,
    const std::string& mangledStructName)
{
    if(impl && !impl->traitName.empty())
    {
        structImplementedTraits[mangledStructName].insert(impl->traitName);
    }
    for(auto* method : impl->methods)
    {
        // Substitute types in return type
        TypeNode* newReturnType =
            substituteTypeParams(method->returnType, typeParams, typeArgs);

        // Substitute types in parameters
        auto* newParams = new ParameterListNode();
        for(auto* param : method->parameters->parameters)
        {
            TypeNode* newParamType =
                substituteTypeParams(param->type, typeParams, typeArgs);
            newParams->parameters.push_back(
                new ParameterNode(newParamType, param->name));
        }

        // Create monomorphized method
        auto* newMethod = new StructMethodNode(
            newReturnType, method->name, newParams, method->body,
            method->isPublic, method->isStatic);
        newMethod->sourceModule = method->sourceModule;
        newMethod->isSynthesizedPropertyAccessor =
            method->isSynthesizedPropertyAccessor;
        newMethod->isAtomicPropertyAccessor = method->isAtomicPropertyAccessor;
        newMethod->isMutexPropertyAccessor = method->isMutexPropertyAccessor;
        newMethod->isRecursiveMutexPropertyAccessor =
            method->isRecursiveMutexPropertyAccessor;
        newMethod->isSynthesizedJsonSerializer =
            method->isSynthesizedJsonSerializer;
        newMethod->isSynthesizedJsonTextDeserializer =
            method->isSynthesizedJsonTextDeserializer;
        newMethod->isSynthesizedJsonValueDeserializer =
            method->isSynthesizedJsonValueDeserializer;
        newMethod->isPropertySetter = method->isPropertySetter;
        newMethod->propertyFieldName = method->propertyFieldName;
        newMethod->propertyLockFieldName = method->propertyLockFieldName;

        // Register the method
        structMethods[mangledStructName][method->name] =
            std::make_pair(method->isPublic, newMethod);

        // Generate only declaration (body is generated lazily on first call)
        generateMethodDeclaration(mangledStructName, newMethod);
    }
}

// Get or create a monomorphized struct type
std::string CodeGenerator::getOrCreateMonomorphizedStruct(
    const std::string& genericName, const std::vector<TypeNode*>& typeArgs)
{
    // Generate the mangled name
    std::string mangledName = generateMangledName(genericName, typeArgs);

    // Check if already monomorphized
    if(monomorphizedTypes.count(mangledName))
    {
        return mangledName;
    }

    // Check if this is actually a generic struct
    if(genericStructTemplates.find(genericName) == genericStructTemplates.end())
    {
        // Not a generic struct - might be a non-generic struct being used
        // Just return the original name
        return genericName;
    }

    // Monomorphize the struct
    monomorphizeStruct(genericName, typeArgs, mangledName);

    return mangledName;
}
