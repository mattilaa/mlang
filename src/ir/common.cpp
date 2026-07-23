#include "ir/common.h"

#include <llvm/Config/llvm-config.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace mlang::ir_detail::common
{

namespace
{

enum class PrimitiveTypeAlias
{
    Bool,
    Bit,
    I32,
    I8,
    I16,
    I64,
    U8,
    U16,
    U32,
    U64,
    F32,
    F64,
    Str8,
    Str16,
};

template <typename Enum, size_t N>
std::optional<Enum>
find_enum_key(std::string_view key,
              const std::array<std::pair<Enum, std::string_view>, N>& mappings)
{
    const auto it =
        std::find_if(mappings.begin(), mappings.end(),
                     [&](const auto& entry) { return entry.second == key; });
    if(it == mappings.end())
        return std::nullopt;
    return it->first;
}

constexpr std::array<std::pair<PrimitiveTypeAlias, std::string_view>, 14>
    kPrimitiveTypeAliases{{{PrimitiveTypeAlias::Bool, "bool"},
                           {PrimitiveTypeAlias::Bit, "bit"},
                           {PrimitiveTypeAlias::I32, "i32"},
                           {PrimitiveTypeAlias::I8, "i8"},
                           {PrimitiveTypeAlias::I16, "i16"},
                           {PrimitiveTypeAlias::I64, "i64"},
                           {PrimitiveTypeAlias::U8, "u8"},
                           {PrimitiveTypeAlias::U16, "u16"},
                           {PrimitiveTypeAlias::U32, "u32"},
                           {PrimitiveTypeAlias::U64, "u64"},
                           {PrimitiveTypeAlias::F32, "f32"},
                           {PrimitiveTypeAlias::F64, "f64"},
                           {PrimitiveTypeAlias::Str8, "str8"},
                           {PrimitiveTypeAlias::Str16, "str16"}}};

std::string trim_copy(std::string s)
{
    size_t b = 0;
    while(b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    size_t e = s.size();
    while(e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

bool is_wrapped_generic(const std::string& s, const char* head)
{
    size_t n = std::strlen(head);
    return s.size() > n + 2 && s.compare(0, n, head) == 0 && s[n] == '<' &&
           s.back() == '>';
}

std::vector<std::string> split_top_level_commas(const std::string& s)
{
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for(size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if(c == '<')
            ++depth;
        else if(c == '>')
            --depth;
        else if(c == ',' && depth == 0)
        {
            out.push_back(trim_copy(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim_copy(s.substr(start)));
    return out;
}

} // namespace

llvm::Value* Helpers::create_global_cstring(llvm::IRBuilder<>& builder,
                                            llvm::StringRef text,
                                            const llvm::Twine& name)
{
#if LLVM_VERSION_MAJOR >= 21
    return builder.CreateGlobalString(text, name);
#else
    return builder.CreateGlobalStringPtr(text, name);
#endif
}

bool Helpers::is_same_module_family(const std::string& a,
                                    const std::string& b)
{
    if(a.empty() || b.empty())
        return a == b;
    if(a == b)
        return true;
    auto is_nested = [](const std::string& parent, const std::string& child)
    {
        if(child.size() <= parent.size())
            return false;
        if(child.compare(0, parent.size(), parent) != 0)
            return false;
        return child.compare(parent.size(), 2, "::") == 0;
    };
    return is_nested(a, b) || is_nested(b, a);
}

bool Helpers::isSynthesizedPropertyLockFieldName(const std::string& fieldName)
{
    constexpr std::string_view prefix = "__mlang_prop_lock_";
    return fieldName.compare(0, prefix.size(), prefix) == 0;
}

bool Helpers::trait_names_equivalent(const std::string& lhs,
                                     const std::string& rhs)
{
    if(lhs == rhs)
        return true;

    auto suffixMatches =
        [](const std::string& qualified, const std::string& shortName)
    {
        if(shortName.find("::") != std::string::npos)
            return false;
        if(qualified.size() <= shortName.size() + 2)
            return false;
        return qualified.compare(qualified.size() - shortName.size(),
                                 shortName.size(), shortName) == 0 &&
               qualified.compare(qualified.size() - shortName.size() - 2, 2,
                                 "::") == 0;
    };

    return suffixMatches(lhs, rhs) || suffixMatches(rhs, lhs);
}

std::string Helpers::type_name_for_error(TypeNode* typeNode)
{
    return typeNode ? typeNode->toString() : "<unknown>";
}

TypeNode* Helpers::type_from_text(std::string text)
{
    text = trim_copy(text);
    if(const auto typeKey = find_enum_key(text, kPrimitiveTypeAliases);
       typeKey.has_value())
    {
        switch(*typeKey)
        {
        case PrimitiveTypeAlias::Bool:
            return new TypeNode(TypeNode::TYPE_BOOL);
        case PrimitiveTypeAlias::Bit:
            return new TypeNode(TypeNode::TYPE_BIT);
        case PrimitiveTypeAlias::I32:
            return new TypeNode(TypeNode::TYPE_I32);
        case PrimitiveTypeAlias::I8:
            return new TypeNode(TypeNode::TYPE_I8);
        case PrimitiveTypeAlias::I16:
            return new TypeNode(TypeNode::TYPE_I16);
        case PrimitiveTypeAlias::I64:
            return new TypeNode(TypeNode::TYPE_I64);
        case PrimitiveTypeAlias::U8:
            return new TypeNode(TypeNode::TYPE_U8);
        case PrimitiveTypeAlias::U16:
            return new TypeNode(TypeNode::TYPE_U16);
        case PrimitiveTypeAlias::U32:
            return new TypeNode(TypeNode::TYPE_U32);
        case PrimitiveTypeAlias::U64:
            return new TypeNode(TypeNode::TYPE_U64);
        case PrimitiveTypeAlias::F32:
            return new TypeNode(TypeNode::TYPE_FLOAT);
        case PrimitiveTypeAlias::F64:
            return new TypeNode(TypeNode::TYPE_DOUBLE);
        case PrimitiveTypeAlias::Str8:
            return new TypeNode(TypeNode::TYPE_STR8);
        case PrimitiveTypeAlias::Str16:
            return new TypeNode(TypeNode::TYPE_STR16);
        }
    }

    if(is_wrapped_generic(text, "list"))
    {
        std::string inner = text.substr(5, text.size() - 6);
        return new GenericListTypeNode(Helpers::type_from_text(inner));
    }
    if(is_wrapped_generic(text, "map"))
    {
        std::string inner = text.substr(4, text.size() - 5);
        auto parts = split_top_level_commas(inner);
        if(parts.size() == 2)
        {
            return new MapTypeNode(Helpers::type_from_text(parts[0]),
                                   Helpers::type_from_text(parts[1]));
        }
    }
    if(is_wrapped_generic(text, "tuple"))
    {
        std::string inner = text.substr(6, text.size() - 7);
        auto parts = split_top_level_commas(inner);
        auto* elems = new TypeListNode();
        for(const auto& p : parts)
            elems->addType(Helpers::type_from_text(p));
        return new TupleTypeNode(elems);
    }

    size_t genericPos = text.find('<');
    if(genericPos != std::string::npos && !text.empty() && text.back() == '>')
    {
        std::string baseName = trim_copy(text.substr(0, genericPos));
        std::string inner =
            text.substr(genericPos + 1, text.size() - genericPos - 2);
        auto parts = split_top_level_commas(inner);
        auto* generic = new GenericStructTypeRefNode(baseName);
        for(const auto& p : parts)
            generic->typeArgs.push_back(Helpers::type_from_text(p));
        return generic;
    }

    return new StructTypeRefNode(text);
}

std::string Helpers::resolve_visible_struct_base_name(
    const std::string& structName,
    const std::map<std::string, StructDefNode*>& genericStructTemplates,
    const std::map<std::string,
                   std::map<std::string, std::pair<bool, StructMethodNode*>>>&
        structMethods,
    const std::map<std::string, std::pair<bool, std::string>>& structVisibility)
{
    if(genericStructTemplates.count(structName) ||
       structMethods.count(structName) || structVisibility.count(structName))
    {
        return structName;
    }

    std::string tailName = structName;
    size_t scopePos = structName.rfind("::");
    if(scopePos != std::string::npos)
        tailName = structName.substr(scopePos + 2);

    if(genericStructTemplates.count(tailName) ||
       structMethods.count(tailName) || structVisibility.count(tailName))
    {
        return tailName;
    }

    auto matchesVisibleTail = [&](const std::string& candidate)
    {
        if(candidate == structName || candidate == tailName)
            return true;
        if(candidate.size() > tailName.size() &&
           candidate.compare(candidate.size() - tailName.size(),
                             tailName.size(), tailName) == 0)
        {
            char sep = candidate[candidate.size() - tailName.size() - 1];
            return sep == ':' || sep == '.' || sep == '_';
        }
        return false;
    };

    for(const auto& entry : genericStructTemplates)
    {
        if(matchesVisibleTail(entry.first))
            return entry.first;
    }
    for(const auto& entry : structMethods)
    {
        if(matchesVisibleTail(entry.first))
            return entry.first;
    }
    for(const auto& entry : structVisibility)
    {
        if(matchesVisibleTail(entry.first))
            return entry.first;
    }

    return structName;
}

bool Helpers::isBitFieldTypeNode(TypeNode* type)
{
    if(!type)
        return false;
    if(type->kind == TypeNode::TYPE_BIT)
        return true;
    if(auto* ref = dynamic_cast<StructTypeRefNode*>(type))
        return ref->structName == "bit";
    return false;
}

bool Helpers::isEnumIntegralType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I8:
    case TypeNode::TYPE_I16:
    case TypeNode::TYPE_I32:
    case TypeNode::TYPE_I64:
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
        return true;
    default:
        return false;
    }
}

bool Helpers::isEnumStringType(TypeNode::TypeKind kind)
{
    return kind == TypeNode::TYPE_STR8 || kind == TypeNode::TYPE_STRING;
}

unsigned Helpers::enumBitWidth(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_I8:
    case TypeNode::TYPE_U8:
        return 8;
    case TypeNode::TYPE_I16:
    case TypeNode::TYPE_U16:
        return 16;
    case TypeNode::TYPE_I64:
    case TypeNode::TYPE_U64:
        return 64;
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I32:
    case TypeNode::TYPE_U32:
    default:
        return 32;
    }
}

bool Helpers::enumIsUnsigned(TypeNode::TypeKind kind)
{
    return kind == TypeNode::TYPE_U8 || kind == TypeNode::TYPE_U16 ||
           kind == TypeNode::TYPE_U32 || kind == TypeNode::TYPE_U64;
}

std::string Helpers::enumBaseTypeName(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_INT:
        return "i32";
    case TypeNode::TYPE_I8:
        return "i8";
    case TypeNode::TYPE_I16:
        return "i16";
    case TypeNode::TYPE_I32:
        return "i32";
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
    default:
        return "i32";
    }
}

bool Helpers::fitsInEnumBaseType(TypeNode::TypeKind kind, int64_t value)
{
    const unsigned bits = Helpers::enumBitWidth(kind);
    if(Helpers::enumIsUnsigned(kind))
    {
        if(value < 0)
            return false;
        if(bits >= 64)
            return true;
        const uint64_t maxVal = (uint64_t{1} << bits) - 1u;
        return static_cast<uint64_t>(value) <= maxVal;
    }

    if(bits >= 64)
        return true;

    const int64_t minVal = -(int64_t{1} << (bits - 1));
    const int64_t maxVal = (int64_t{1} << (bits - 1)) - 1;
    return value >= minVal && value <= maxVal;
}

TypeNode::TypeKind Helpers::normalizeInferredKind(TypeNode::TypeKind kind)
{
    if(kind == TypeNode::TYPE_INT)
        return TypeNode::TYPE_I32;
    if(kind == TypeNode::TYPE_STR8 || kind == TypeNode::TYPE_STR16)
        return TypeNode::TYPE_STRING;
    return kind;
}

bool Helpers::isIntegerInferKind(TypeNode::TypeKind kind)
{
    switch(Helpers::normalizeInferredKind(kind))
    {
    case TypeNode::TYPE_I8:
    case TypeNode::TYPE_I16:
    case TypeNode::TYPE_I32:
    case TypeNode::TYPE_I64:
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
        return true;
    default:
        return false;
    }
}

bool Helpers::isFloatInferKind(TypeNode::TypeKind kind)
{
    kind = Helpers::normalizeInferredKind(kind);
    return kind == TypeNode::TYPE_FLOAT || kind == TypeNode::TYPE_DOUBLE;
}

} // namespace mlang::ir_detail::common
