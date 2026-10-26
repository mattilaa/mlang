#ifndef MLANG_LITERAL_OPERATORS_H
#define MLANG_LITERAL_OPERATORS_H

#include "ast.h"
#include <string>

namespace mlang::literals
{

// Include a character unavailable in source identifiers so ordinary functions
// cannot accidentally become literal operators. Categories never convert into
// one another during overload resolution.
inline const char* inputName(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_U64:
        return "u64";
    case TypeNode::TYPE_DOUBLE:
        return "f64";
    case TypeNode::TYPE_STR8:
        return "str8";
    default:
        return nullptr;
    }
}

inline std::string operatorName(
    const std::string& suffix,
    TypeNode::TypeKind kind)
{
    const char* input = inputName(kind);
    return "__mlang_literal$" + std::string(input ? input : "invalid") + "$" +
           suffix;
}

inline bool decodeOperatorName(
    const std::string& name,
    std::string& suffix,
    std::string& input)
{
    const std::string prefix = "__mlang_literal$";
    const size_t start = name.rfind(prefix);
    if(start == std::string::npos ||
       (start != 0 && (start < 2 || name.substr(start - 2, 2) != "::")))
        return false;
    const size_t separator = name.find('$', start + prefix.size());
    if(separator == std::string::npos)
        return false;
    input =
        name.substr(start + prefix.size(), separator - start - prefix.size());
    suffix = name.substr(separator + 1);
    return true;
}

template <class Contains>
inline std::string scopedOperatorName(
    const std::string& name,
    std::string scope,
    Contains contains)
{
    while(!scope.empty())
    {
        const std::string qualified = scope + "::" + name;
        if(contains(qualified))
            return qualified;
        const size_t separator = scope.rfind("::");
        if(separator == std::string::npos)
            break;
        scope.resize(separator);
    }
    return name;
}

inline bool isBuiltinResult(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_BOOL:
    case TypeNode::TYPE_BIT:
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I8:
    case TypeNode::TYPE_I16:
    case TypeNode::TYPE_I32:
    case TypeNode::TYPE_I64:
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
    case TypeNode::TYPE_FLOAT:
    case TypeNode::TYPE_DOUBLE:
    case TypeNode::TYPE_STR8:
    case TypeNode::TYPE_STR16:
        return true;
    default:
        return false;
    }
}

} // namespace mlang::literals
#endif
