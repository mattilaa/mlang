#include "ir.h"
#include "ir/common.h"

#include <limits>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constants.h>

using mlang::ir_detail::common::Helpers;


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
    if(Helpers::isEnumStringType(baseKind))
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
            enumValNorm, enumTy, !Helpers::enumIsUnsigned(baseKind), "enum.str.cast");
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
                enumTy, it->second, !Helpers::enumIsUnsigned(baseKind));
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
            enumTy, kv.second, !Helpers::enumIsUnsigned(baseKind));
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

void CodeGenerator::generateEnumDefinition(EnumDefNode* node)
{
    if(!node)
        return;

    if(enumValues.find(node->name) != enumValues.end())
    {
        reportError(node->line, "duplicate enum: '" + node->name + "'");
        return;
    }

    TypeNode::TypeKind baseKind = node->backingType;
    if(!Helpers::isEnumIntegralType(baseKind) && !Helpers::isEnumStringType(baseKind))
    {
        reportError(node->line,
                    "enum '" + node->name + "' has unsupported backing type");
        return;
    }

    if(Helpers::isEnumStringType(baseKind))
    {
        std::map<std::string, std::string> variants;
        std::vector<std::pair<std::string, std::string>> orderedVariants;
        if(node->variants)
        {
            for(auto* variant : node->variants->variants)
            {
                if(!variant)
                    continue;
                if(variants.find(variant->name) != variants.end())
                {
                    reportError(node->line, "duplicate enum variant: '" +
                                                variant->name + "'");
                    return;
                }

                std::string value = variant->name;
                if(variant->hasExplicitStringValue)
                {
                    value = variant->explicitStringValue;
                }
                else if(variant->hasReferenceValue)
                {
                    bool resolved = false;
                    if(variant->refEnumName == node->name)
                    {
                        auto selfIt = variants.find(variant->refVariantName);
                        if(selfIt != variants.end())
                        {
                            value = selfIt->second;
                            resolved = true;
                        }
                    }
                    else
                    {
                        auto refEnumIt =
                            enumStringValues.find(variant->refEnumName);
                        if(refEnumIt != enumStringValues.end())
                        {
                            auto refVarIt =
                                refEnumIt->second.find(variant->refVariantName);
                            if(refVarIt != refEnumIt->second.end())
                            {
                                value = refVarIt->second;
                                resolved = true;
                            }
                        }
                    }

                    if(!resolved)
                    {
                        reportError(
                            variant->line > 0 ? variant->line : node->line,
                            "enum variant '" + variant->name +
                                "' references unknown string enum value '" +
                                variant->refEnumName +
                                "::" + variant->refVariantName + "' in enum '" +
                                node->name + "'");
                        return;
                    }
                }
                else if(variant->hasExplicitValue)
                {
                    reportError(variant->line > 0 ? variant->line : node->line,
                                "string-backed enum '" + node->name +
                                    "' requires string variant values");
                    return;
                }

                variants[variant->name] = value;
                orderedVariants.push_back({variant->name, value});
            }
        }

        enumStringValues[node->name] = variants;
        enumStringVariantOrder[node->name] = orderedVariants;
        enumBaseTypes[node->name] = TypeNode::TYPE_STR8;
        return;
    }

    std::map<std::string, int64_t> variants;
    std::vector<std::pair<std::string, int64_t>> orderedVariants;
    int64_t nextValue = 0;
    bool nextImplicitValid = true;
    if(node->variants)
    {
        for(auto* variant : node->variants->variants)
        {
            if(!variant)
                continue;
            if(variants.find(variant->name) != variants.end())
            {
                reportError(node->line,
                            "duplicate enum variant: '" + variant->name + "'");
                return;
            }

            int64_t value = nextValue;
            if(variant->hasExplicitValue)
            {
                value = variant->explicitValue;
                nextImplicitValid = true;
            }
            else if(variant->hasReferenceValue)
            {
                const std::string& refEnumName = variant->refEnumName;
                const std::string& refVariantName = variant->refVariantName;
                bool resolved = false;

                // Self-reference is allowed only to already defined variants.
                if(refEnumName == node->name)
                {
                    auto selfIt = variants.find(refVariantName);
                    if(selfIt != variants.end())
                    {
                        value = selfIt->second;
                        resolved = true;
                    }
                }
                else
                {
                    auto refEnumIt = enumValues.find(refEnumName);
                    if(refEnumIt != enumValues.end())
                    {
                        auto refVarIt = refEnumIt->second.find(refVariantName);
                        if(refVarIt != refEnumIt->second.end())
                        {
                            value = refVarIt->second;
                            resolved = true;
                        }
                    }
                }

                if(!resolved)
                {
                    reportError(variant->line > 0 ? variant->line : node->line,
                                "enum variant '" + variant->name +
                                    "' references unknown enum value '" +
                                    refEnumName + "::" + refVariantName +
                                    "' in enum '" + node->name + "'");
                    return;
                }

                nextImplicitValid = true;
            }
            else if(!nextImplicitValid)
            {
                reportError(variant->line > 0 ? variant->line : node->line,
                            "enum implicit value overflows backing type '" +
                                Helpers::enumBaseTypeName(baseKind) + "' in enum '" +
                                node->name + "'");
                return;
            }

            if(!Helpers::fitsInEnumBaseType(baseKind, value))
            {
                if(variant->hasReferenceValue)
                {
                    TypeNode::TypeKind refBaseKind = TypeNode::TYPE_I32;
                    auto refBkIt = enumBaseTypes.find(variant->refEnumName);
                    if(refBkIt != enumBaseTypes.end())
                        refBaseKind = refBkIt->second;

                    reportError(variant->line > 0 ? variant->line : node->line,
                                "enum types/values are not compatible: '" +
                                    variant->refEnumName +
                                    "::" + variant->refVariantName + "' (" +
                                    Helpers::enumBaseTypeName(refBaseKind) + ", value " +
                                    std::to_string(value) +
                                    ") cannot fit in enum '" + node->name +
                                    "' backing type '" +
                                    Helpers::enumBaseTypeName(baseKind) + "'");
                }
                else
                {
                    reportError(variant->line > 0 ? variant->line : node->line,
                                "enum variant value '" + std::to_string(value) +
                                    "' does not fit backing type '" +
                                    Helpers::enumBaseTypeName(baseKind) + "' in enum '" +
                                    node->name + "'");
                }
                return;
            }

            variants[variant->name] = value;
            orderedVariants.push_back({variant->name, value});
            if(value == std::numeric_limits<int64_t>::max())
            {
                nextImplicitValid = false;
            }
            else
            {
                nextValue = value + 1;
                nextImplicitValid = Helpers::fitsInEnumBaseType(baseKind, nextValue);
            }
        }
    }

    enumValues[node->name] = variants;
    enumVariantOrder[node->name] = orderedVariants;
    enumBaseTypes[node->name] = baseKind;
}
