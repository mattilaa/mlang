#include "ir.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constants.h>

namespace
{

bool isEnumStringType(TypeNode::TypeKind kind)
{
    return kind == TypeNode::TYPE_STR8 || kind == TypeNode::TYPE_STRING;
}

bool enumIsUnsigned(TypeNode::TypeKind kind)
{
    return kind == TypeNode::TYPE_U8 || kind == TypeNode::TYPE_U16 ||
           kind == TypeNode::TYPE_U32 || kind == TypeNode::TYPE_U64;
}

} // namespace

std::string
CodeGenerator::resolveVisibleEnumName(const std::string& enumName) const
{
    auto hasEnumNamed = [&](const std::string& name) -> bool
    {
        return enumValues.find(name) != enumValues.end() ||
               enumStringValues.find(name) != enumStringValues.end();
    };

    if(hasEnumNamed(enumName))
        return enumName;

    std::string shortName = enumName;
    size_t scopePos = enumName.rfind("::");
    if(scopePos != std::string::npos && (scopePos + 2) < enumName.size())
    {
        shortName = enumName.substr(scopePos + 2);
        if(hasEnumNamed(shortName))
            return shortName;
    }

    for(const auto& kv : enumValues)
    {
        const std::string& candidate = kv.first;
        if(candidate.size() > shortName.size() &&
           candidate.compare(candidate.size() - shortName.size(),
                             shortName.size(), shortName) == 0)
        {
            char sep = candidate[candidate.size() - shortName.size() - 1];
            if(sep == ':' || sep == '.' || sep == '_')
                return candidate;
        }
    }
    for(const auto& kv : enumStringValues)
    {
        const std::string& candidate = kv.first;
        if(candidate.size() > shortName.size() &&
           candidate.compare(candidate.size() - shortName.size(),
                             shortName.size(), shortName) == 0)
        {
            char sep = candidate[candidate.size() - shortName.size() - 1];
            if(sep == ':' || sep == '.' || sep == '_')
                return candidate;
        }
    }
    return {};
}

std::string
CodeGenerator::resolveVisibleStructName(const std::string& structName) const
{
    auto hasStructNamed = [&](const std::string& name) -> bool
    {
        return structTypes.find(name) != structTypes.end() ||
               genericStructTemplates.find(name) !=
                   genericStructTemplates.end() ||
               structMethods.find(name) != structMethods.end();
    };

    if(hasStructNamed(structName))
        return structName;

    std::string shortName = structName;
    size_t scopePos = structName.rfind("::");
    if(scopePos != std::string::npos && (scopePos + 2) < structName.size())
    {
        shortName = structName.substr(scopePos + 2);
        if(hasStructNamed(shortName))
            return shortName;
    }

    for(const auto& kv : structTypes)
    {
        const std::string& candidate = kv.first;
        if(candidate.size() > shortName.size() &&
           candidate.compare(candidate.size() - shortName.size(),
                             shortName.size(), shortName) == 0)
        {
            char sep = candidate[candidate.size() - shortName.size() - 1];
            if(sep == ':' || sep == '.' || sep == '_')
                return candidate;
        }
    }
    for(const auto& kv : genericStructTemplates)
    {
        const std::string& candidate = kv.first;
        if(candidate.size() > shortName.size() &&
           candidate.compare(candidate.size() - shortName.size(),
                             shortName.size(), shortName) == 0)
        {
            char sep = candidate[candidate.size() - shortName.size() - 1];
            if(sep == ':' || sep == '.' || sep == '_')
                return candidate;
        }
    }
    for(const auto& kv : structMethods)
    {
        const std::string& candidate = kv.first;
        if(candidate.size() > shortName.size() &&
           candidate.compare(candidate.size() - shortName.size(),
                             shortName.size(), shortName) == 0)
        {
            char sep = candidate[candidate.size() - shortName.size() - 1];
            if(sep == ':' || sep == '.' || sep == '_')
                return candidate;
        }
    }
    return {};
}

llvm::Value* CodeGenerator::buildEnumString(llvm::Value* enumVal,
                                            const std::string& enumName,
                                            int line)
{
    std::string resolvedEnumName = resolveVisibleEnumName(enumName);
    if(resolvedEnumName.empty())
    {
        reportError(line, "unknown enum: '" + enumName + "'");
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString("<enum>");
#else
        return builder.CreateGlobalStringPtr("<enum>");
#endif
    }
    auto orderedIt = enumVariantOrder.find(resolvedEnumName);
    auto orderedStrIt = enumStringVariantOrder.find(resolvedEnumName);

    TypeNode::TypeKind baseKind = TypeNode::TYPE_I32;
    auto bkIt = enumBaseTypes.find(resolvedEnumName);
    if(bkIt != enumBaseTypes.end())
        baseKind = bkIt->second;
    if(isEnumStringType(baseKind))
    {
        if(enumVal->getType()->isPointerTy())
            return enumVal;
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString("<enum>");
#else
        return builder.CreateGlobalStringPtr("<enum>");
#endif
    }
    llvm::Type* enumTy = getLLVMType(baseKind);

    llvm::Value* enumValNorm = enumVal;
    if(enumValNorm->getType() != enumTy &&
       enumValNorm->getType()->isIntegerTy())
    {
        enumValNorm = builder.CreateIntCast(
            enumValNorm, enumTy, !enumIsUnsigned(baseKind), "enum.str.cast");
    }

    std::string unknownText = "<" + resolvedEnumName + ":unknown>";
#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* out = builder.CreateGlobalString(unknownText);
#else
    llvm::Value* out = builder.CreateGlobalStringPtr(unknownText);
#endif

    if(orderedIt != enumVariantOrder.end())
    {
        for(auto it = orderedIt->second.rbegin();
            it != orderedIt->second.rend(); ++it)
        {
            llvm::Value* variantConst = llvm::ConstantInt::get(
                enumTy, it->second, !enumIsUnsigned(baseKind));
            llvm::Value* isMatch =
                builder.CreateICmpEQ(enumValNorm, variantConst, "enum.str.eq");
            std::string text = resolvedEnumName + "::" + it->first;
#if LLVM_VERSION_MAJOR >= 21
            llvm::Value* variantStr = builder.CreateGlobalString(text);
#else
            llvm::Value* variantStr = builder.CreateGlobalStringPtr(text);
#endif
            out =
                builder.CreateSelect(isMatch, variantStr, out, "enum.str.sel");
        }
        return out;
    }

    if(orderedStrIt != enumStringVariantOrder.end() &&
       !orderedStrIt->second.empty())
        return out;

    auto enumIt = enumValues.find(resolvedEnumName);
    if(enumIt == enumValues.end())
        return out;

    for(const auto& kv : enumIt->second)
    {
        llvm::Value* variantConst = llvm::ConstantInt::get(
            enumTy, kv.second, !enumIsUnsigned(baseKind));
        llvm::Value* isMatch =
            builder.CreateICmpEQ(enumValNorm, variantConst, "enum.str.eq");
        std::string text = resolvedEnumName + "::" + kv.first;
#if LLVM_VERSION_MAJOR >= 21
        llvm::Value* variantStr = builder.CreateGlobalString(text);
#else
        llvm::Value* variantStr = builder.CreateGlobalStringPtr(text);
#endif
        out = builder.CreateSelect(isMatch, variantStr, out, "enum.str.sel");
    }

    return out;
}
