#ifndef MLANG_CONCEPTS_H
#define MLANG_CONCEPTS_H
#include "ast.h"
#include <algorithm>
#include <cstdint>

namespace mlang::concepts
{
inline std::string constraintKey(const FunctionDefNode* function)
{
    if(function->genericOrigin)
        function = function->genericOrigin;
    std::string key;
    if(function->requiresClause)
        key = "requires:" + function->requiresClause->toString();
    for(const auto& bound : function->typeParamTraitBounds)
        key += ";" + bound.first + ":" + bound.second;
    return key;
}

inline std::string constraintSymbol(const FunctionDefNode* function)
{
    const std::string key = constraintKey(function);
    if(key.empty())
        return "";
    uint64_t hash = 14695981039346656037ULL;
    for(unsigned char byte : key)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return "_constraint_" + std::to_string(hash);
}

using Terms = std::vector<std::set<std::string>>;

inline bool implies(const Terms& left, const Terms& right)
{
    for(const auto& term : left)
    {
        bool found = false;
        for(const auto& weaker : right)
            if(std::includes(
                   term.begin(),
                   term.end(),
                   weaker.begin(),
                   weaker.end()))
            {
                found = true;
                break;
            }
        if(!found)
            return false;
    }
    return true;
}

inline bool moreConstrained(
    const FunctionDefNode* left,
    const FunctionDefNode* right)
{
    if(!left || !right)
        return false;
    if(bool(left->genericOrigin) != bool(right->genericOrigin))
        return !left->genericOrigin;
    const Terms unconstrained{std::set<std::string>{}};
    const Terms& l =
        left->constraintTerms.empty() ? unconstrained : left->constraintTerms;
    const Terms& r =
        right->constraintTerms.empty() ? unconstrained : right->constraintTerms;
    return implies(l, r) && !implies(r, l);
}
} // namespace mlang::concepts
#endif
