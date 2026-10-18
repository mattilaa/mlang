#include "ir.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>

namespace
{
std::string borrowMapKeyPath(ExpressionNode* key)
{
    if(auto* integer = dynamic_cast<IntLiteralNode*>(key))
        return "[" + std::to_string(integer->value) + "]";
    if(auto* string = dynamic_cast<StringLiteralNode*>(key))
    {
        static constexpr char hex[] = "0123456789abcdef";
        std::string encoded = "[s:";
        for(unsigned char byte : string->value)
        {
            encoded.push_back(hex[byte >> 4]);
            encoded.push_back(hex[byte & 0x0f]);
        }
        encoded.push_back(']');
        return encoded;
    }
    return "[*]";
}
} // namespace

std::string
CodeGenerator::resolveBorrowOwnerFromLValue(ExpressionNode* expr) const
{
    std::function<std::string(ExpressionNode*)> resolve =
        [&](ExpressionNode* e) -> std::string
    {
        if(!e)
            return "";

        if(auto* id = dynamic_cast<IdentifierNode*>(e))
            return id->name;

        if(auto* field = dynamic_cast<FieldAccessNode*>(e))
        {
            if(field->object)
                return resolve(field->object);
            return field->structName;
        }

        if(auto* index = dynamic_cast<IndexExpressionNode*>(e))
            return resolve(index->base);

        if(auto* tupleAccess = dynamic_cast<TupleAccessNode*>(e))
            return resolve(tupleAccess->tuple);

        if(auto* unary = dynamic_cast<UnaryOpNode*>(e))
        {
            if(unary->op == UnaryOpNode::OP_DEREF)
            {
                if(auto* pid = dynamic_cast<IdentifierNode*>(unary->operand))
                {
                    auto it = pointerBorrowTarget.find(pid->name);
                    if(it != pointerBorrowTarget.end())
                        return it->second;
                }
            }
            return resolve(unary->operand);
        }

        return "";
    };

    return resolve(expr);
}

std::string
CodeGenerator::resolveBorrowPlaceFromLValue(ExpressionNode* expr) const
{
    std::function<std::optional<int64_t>(ExpressionNode*)> constantInteger =
        [&](ExpressionNode* value) -> std::optional<int64_t> {
        if(auto* literal = dynamic_cast<IntLiteralNode*>(value))
            return literal->value;
        if(auto* cexpr = dynamic_cast<CexprExpressionNode*>(value))
            return constantInteger(cexpr->expression);
        if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
        {
            auto constant = constexprValues.find(identifier->name);
            if(constant == constexprValues.end())
                return std::nullopt;
            if(constant->second.kind == ConstexprValue::Kind::Int)
                return constant->second.intValue;
            if(constant->second.kind == ConstexprValue::Kind::Bool)
                return constant->second.boolValue ? 1 : 0;
            return std::nullopt;
        }
        if(auto* unary = dynamic_cast<UnaryOpNode*>(value))
        {
            auto operand = constantInteger(unary->operand);
            if(!operand)
                return std::nullopt;
            if(unary->op == UnaryOpNode::OP_NEG)
            {
                if(*operand == std::numeric_limits<int64_t>::min())
                    return std::nullopt;
                return -*operand;
            }
            if(unary->op == UnaryOpNode::OP_BITNOT)
                return ~*operand;
            return std::nullopt;
        }
        auto* binary = dynamic_cast<BinaryOpNode*>(value);
        if(!binary)
            return std::nullopt;
        auto left = constantInteger(binary->left);
        auto right = constantInteger(binary->right);
        if(!left || !right)
            return std::nullopt;
        __int128 result = 0;
        switch(binary->op)
        {
        case BinaryOpNode::OP_PLUS:
            result = static_cast<__int128>(*left) + *right;
            break;
        case BinaryOpNode::OP_MINUS:
            result = static_cast<__int128>(*left) - *right;
            break;
        case BinaryOpNode::OP_MULTIPLY:
            result = static_cast<__int128>(*left) * *right;
            break;
        case BinaryOpNode::OP_DIVIDE:
            if(*right == 0 ||
               (*left == std::numeric_limits<int64_t>::min() && *right == -1))
                return std::nullopt;
            return *left / *right;
        case BinaryOpNode::OP_MODULO:
            if(*right == 0 ||
               (*left == std::numeric_limits<int64_t>::min() && *right == -1))
                return std::nullopt;
            return *left % *right;
        default:
            return std::nullopt;
        }
        if(result < std::numeric_limits<int64_t>::min() ||
           result > std::numeric_limits<int64_t>::max())
            return std::nullopt;
        return static_cast<int64_t>(result);
    };

    if(!expr)
        return "";
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
        return id->name;
    if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
    {
        const std::string base = field->object
                                     ? resolveBorrowPlaceFromLValue(field->object)
                                     : field->structName;
        return base.empty() ? "" : base + "." + field->fieldName;
    }
    if(auto* index = dynamic_cast<IndexExpressionNode*>(expr))
    {
        const std::string base = resolveBorrowPlaceFromLValue(index->base);
        if(base.empty())
            return "";
        if(dynamic_cast<StringLiteralNode*>(index->index))
            return base + borrowMapKeyPath(index->index);
        if(auto indexValue = constantInteger(index->index))
            return base + "[" + std::to_string(*indexValue) + "]";
        // Unknown indices may alias any element. Keep the wildcard in the
        // path so fields beneath the element remain conservatively tracked.
        return base + "[*]";
    }
    if(auto* tuple = dynamic_cast<TupleAccessNode*>(expr))
    {
        const std::string base = resolveBorrowPlaceFromLValue(tuple->tuple);
        return base.empty() ? ""
                            : base + "." + std::to_string(tuple->index);
    }
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_DEREF)
        {
            if(auto* id = dynamic_cast<IdentifierNode*>(unary->operand))
            {
                auto place = pointerBorrowPlace.find(id->name);
                if(place != pointerBorrowPlace.end())
                    return place->second;
            }
        }
        return resolveBorrowPlaceFromLValue(unary->operand);
    }
    return "";
}

bool CodeGenerator::borrowPlacesOverlap(const std::string& left,
                                       const std::string& right) const
{
    if(left.empty() || right.empty())
        return true;
    if(left == right)
        return true;
    struct Segment
    {
        std::string value;
        bool index = false;
    };
    const auto split = [](const std::string& path) {
        std::vector<Segment> segments;
        std::size_t start = 0;
        while(start < path.size())
        {
            if(path[start] == '.')
            {
                const std::size_t end = path.find_first_of(".[", start + 1);
                segments.push_back({path.substr(start + 1,
                                                end == std::string::npos
                                                    ? end
                                                    : end - start - 1),
                                    false});
                start = end == std::string::npos ? path.size() : end;
            }
            else if(path[start] == '[')
            {
                const std::size_t end = path.find(']', start + 1);
                if(end == std::string::npos)
                    break;
                segments.push_back({path.substr(start + 1, end - start - 1),
                                    true});
                start = end + 1;
            }
            else
            {
                const std::size_t end = path.find_first_of(".[", start);
                segments.push_back({path.substr(start,
                                                end == std::string::npos
                                                    ? end
                                                    : end - start),
                                    false});
                start = end == std::string::npos ? path.size() : end;
            }
        }
        return segments;
    };
    const auto a = split(left);
    const auto b = split(right);
    for(std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
    {
        if(a[i].index != b[i].index)
            return false;
        if(a[i].value == b[i].value)
            continue;
        if(a[i].index && (a[i].value == "*" || b[i].value == "*"))
            continue;
        return false;
    }
    // A whole place overlaps every nested place beneath it.
    return true;
}

bool CodeGenerator::borrowerPlaceOverlaps(const std::string& borrower,
                                         const std::string& owner,
                                         const std::string& place) const
{
    auto aggregate = aggregateBorrowPlaces.find(borrower);
    if(aggregate != aggregateBorrowPlaces.end())
    {
        auto ownerPlaces = aggregate->second.find(owner);
        if(ownerPlaces != aggregate->second.end() &&
           !ownerPlaces->second.empty())
        {
            for(const auto& borrowedPlace : ownerPlaces->second)
            {
                if(borrowPlacesOverlap(borrowedPlace, place))
                    return true;
            }
            return false;
        }
        return false;
    }

    // Aggregate places include all retained shared and exclusive borrows.
    auto borrowedOwner = pointerBorrowTarget.find(borrower);
    if(borrowedOwner != pointerBorrowTarget.end() &&
       borrowedOwner->second != owner)
        return false;
    auto borrowedPlace = pointerBorrowPlace.find(borrower);
    return borrowPlacesOverlap(
        borrowedPlace == pointerBorrowPlace.end() ? owner
                                                  : borrowedPlace->second,
        place);
}

bool CodeGenerator::isPointerBorrowAncestor(const std::string& ancestor,
                                           const std::string& pointer) const
{
    std::string current = pointer;
    std::set<std::string> visited;
    while(true)
    {
        // Parent links should form a tree, but branch merges and shadowed
        // pointer metadata are maintained independently. Fail closed if a
        // malformed/cyclic chain ever reaches this query instead of hanging
        // the compiler during borrow validation.
        if(!visited.insert(current).second)
            return false;
        auto parent = pointerBorrowParent.find(current);
        if(parent == pointerBorrowParent.end())
            return false;
        if(parent->second == ancestor)
            return true;
        current = parent->second;
    }
}

bool CodeGenerator::validateBorrowRead(ExpressionNode* expr, int line)
{
    const std::string place = resolveBorrowPlaceFromLValue(expr);
    if(!place.empty())
        for(const auto& movedPlace : movedVariables)
            if(borrowPlacesOverlap(place, movedPlace))
            {
                reportError(line, "use of moved value: '" + movedPlace + "'");
                return false;
            }

    if(suppressBorrowReadValidation)
        return true;

    const std::string owner = resolveBorrowOwnerFromLValue(expr);
    if(owner.empty() || place.empty())
        return true;

    std::function<bool(ExpressionNode*, const std::string&)> accessedThrough =
        [&](ExpressionNode* current, const std::string& pointer) -> bool {
        if(auto* unary = dynamic_cast<UnaryOpNode*>(current))
        {
            if(unary->op == UnaryOpNode::OP_DEREF)
                if(auto* id = dynamic_cast<IdentifierNode*>(unary->operand))
                    return id->name == pointer;
            return accessedThrough(unary->operand, pointer);
        }
        if(auto* field = dynamic_cast<FieldAccessNode*>(current))
            return accessedThrough(field->object, pointer);
        if(auto* index = dynamic_cast<IndexExpressionNode*>(current))
            return accessedThrough(index->base, pointer);
        if(auto* tuple = dynamic_cast<TupleAccessNode*>(current))
            return accessedThrough(tuple->tuple, pointer);
        return false;
    };

    for(const auto& mutableBorrow : activeMutBorrower)
    {
        if(accessedThrough(expr, mutableBorrow.second))
            continue;
        bool accessedThroughDescendant = false;
        for(const auto& pointer : pointerBorrowParent)
        {
            if(isPointerBorrowAncestor(mutableBorrow.second, pointer.first) &&
               accessedThrough(expr, pointer.first))
            {
                accessedThroughDescendant = true;
                break;
            }
        }
        if(accessedThroughDescendant)
            continue;
        if(borrowerPlaceOverlaps(mutableBorrow.second, owner, place))
        {
            reportError(line, "cannot read overlapping part of '" + owner +
                                  "' while mutably borrowed by '" +
                                  mutableBorrow.second + "'");
            return false;
        }
    }
    return true;
}

bool CodeGenerator::validateBorrowWriteThroughPointer(ExpressionNode* expr,
                                                       int line)
{
    if(auto* address = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(address->op == UnaryOpNode::OP_ADDR)
        {
            const std::string owner =
                resolveBorrowOwnerFromLValue(address->operand);
            if(owner.empty())
                return true;
            std::string place = resolveBorrowPlaceFromLValue(address->operand);
            if(place.empty())
                place = owner;

            reportError(line, "cannot write through shared borrow '" + place +
                                  "'");
            return false;
        }
    }
    if((dynamic_cast<FunctionCallNode*>(expr) ||
        dynamic_cast<CallableInvokeNode*>(expr)) &&
       expressionIsKnownSharedBorrow(expr))
    {
        reportError(line,
                    "cannot write through shared borrow returned by call");
        return false;
    }

    size_t dereferenceDepth = 0;
    auto* current = expr;
    while(auto* dereference = dynamic_cast<UnaryOpNode*>(current))
    {
        if(dereference->op != UnaryOpNode::OP_DEREF)
            break;
        ++dereferenceDepth;
        current = dereference->operand;
    }
    auto* pointer = dynamic_cast<IdentifierNode*>(current);
    // A write through `*outer` mutates the pointer value stored in outer's
    // referent. Validate each pointer in that chain too: an exclusive borrow
    // of the slot does not upgrade a shared pointer stored inside the slot.
    std::set<std::string> checkedPointers;
    auto nestedOwner = pointer
                           ? pointerBorrowTarget.find(pointer->name)
                           : pointerBorrowTarget.end();
    for(size_t depth = 0;
        pointer && depth < dereferenceDepth &&
        nestedOwner != pointerBorrowTarget.end();
        ++depth)
    {
        const std::string owner = nestedOwner->second;
        if(owner.empty() || !checkedPointers.insert(owner).second)
            break;
        IdentifierNode nestedPointer(owner);
        if(!validateBorrowWriteThroughPointer(&nestedPointer, line))
            return false;
        nestedOwner = pointerBorrowTarget.find(owner);
    }
    if(!pointer)
    {
        const std::string container = resolveBorrowOwnerFromLValue(expr);
        const std::string sourcePlace = resolveBorrowPlaceFromLValue(expr);
        if(container.empty() || sourcePlace.empty() ||
           sourcePlace.compare(0, container.size(), container) != 0)
            return true;

        std::string relativePlace = sourcePlace.substr(container.size());
        if(!relativePlace.empty() && relativePlace.front() == '.')
            relativePlace.erase(0, 1);
        auto aggregate = aggregateBorrowFieldPlaces.find(container);
        if(aggregate == aggregateBorrowFieldPlaces.end())
            return true;
        auto field = aggregate->second.find(relativePlace);
        if(field == aggregate->second.end() || field->second.empty())
            return true;

        reportError(line, "cannot write through shared borrow '" +
                              sourcePlace + "'");
        return false;
    }

    auto ownerIt = pointerBorrowTarget.find(pointer->name);
    if(ownerIt == pointerBorrowTarget.end())
        return true;

    auto sharedBorrowers = activeBorrowers.find(ownerIt->second);
    if(!isMutBorrower(pointer->name) &&
       sharedBorrowers != activeBorrowers.end() &&
       sharedBorrowers->second.count(pointer->name) != 0)
    {
        reportError(line, "cannot write through shared borrow '" +
                              pointer->name + "'");
        return false;
    }

    std::set<std::string> owners{ownerIt->second};
    for(const auto& active : activeBorrowers)
        if(active.second.count(pointer->name) != 0)
            owners.insert(active.first);

    for(const auto& owner : owners)
    {
        auto placeIt = pointerBorrowPlace.find(pointer->name);
        const std::string targetPlace =
            placeIt == pointerBorrowPlace.end() || owner != ownerIt->second
                ? owner
                : placeIt->second;

        auto conflictsWith = [&](const std::string& borrower) {
            if(borrower == pointer->name)
                return false;
            if(isPointerBorrowAncestor(borrower, pointer->name))
                return false;
            if(!borrowerPlaceOverlaps(borrower, owner, targetPlace))
                return false;
            reportError(line, "cannot write overlapping part of '" + owner +
                                  "' through '" + pointer->name +
                                  "' while borrowed by '" + borrower + "'");
            return true;
        };

        auto sharedIt = activeBorrowers.find(owner);
        if(sharedIt != activeBorrowers.end())
            for(const auto& borrower : sharedIt->second)
                if(conflictsWith(borrower))
                    return false;

        for(const auto& mutableBorrow : activeMutBorrower)
        {
            if(conflictsWith(mutableBorrow.second))
                return false;
        }
    }
    return true;
}

std::string
CodeGenerator::getBorrowedOwnerForPointerExpression(ExpressionNode* expr) const
{
    auto* idExpr = dynamic_cast<IdentifierNode*>(expr);
    if(!idExpr)
        return "";

    auto borrowIt = pointerBorrowTarget.find(idExpr->name);
    if(borrowIt == pointerBorrowTarget.end())
        return "";

    return borrowIt->second;
}

std::optional<bool>
CodeGenerator::pointerExpressionKnownNull(ExpressionNode* expr) const
{
    if(!expr)
        return true;

    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = pointerKnownNull.find(idExpr->name);
        if(it != pointerKnownNull.end())
            return it->second;
        return std::nullopt;
    }

    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_ADDR ||
           unary->op == UnaryOpNode::OP_ADDR_MUT)
            return false;
    }

    return std::nullopt;
}

bool CodeGenerator::expressionCarriesLocalBorrow(ExpressionNode* expr) const
{
    if(!expr)
        return false;
    auto isLocalOwner = [&](const std::string& owner) {
        return !owner.empty() && variableScopeDepth.count(owner) != 0;
    };
    if(auto* identifier = dynamic_cast<IdentifierNode*>(expr))
    {
        auto pointer = pointerBorrowTarget.find(identifier->name);
        if(pointer != pointerBorrowTarget.end() &&
           isLocalOwner(pointer->second))
            return true;
        auto aggregate = aggregateBorrowOrigins.find(identifier->name);
        if(aggregate != aggregateBorrowOrigins.end())
            for(const auto& owner : aggregate->second)
                if(isLocalOwner(owner))
                    return true;
        return false;
    }
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if((unary->op == UnaryOpNode::OP_ADDR ||
            unary->op == UnaryOpNode::OP_ADDR_MUT) &&
           isLocalOwner(resolveBorrowOwnerFromLValue(unary->operand)))
            return true;
        return expressionCarriesLocalBorrow(unary->operand);
    }
    if(auto* fold = dynamic_cast<FoldExpressionNode*>(expr))
        return expressionCarriesLocalBorrow(fold->packExpr);
    if(auto* update = dynamic_cast<UpdateExpressionNode*>(expr))
        return expressionCarriesLocalBorrow(update->operand);
    if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
        return expressionCarriesLocalBorrow(ternary->condition) ||
               expressionCarriesLocalBorrow(ternary->trueExpr) ||
               expressionCarriesLocalBorrow(ternary->falseExpr);
    if(auto* binary = dynamic_cast<BinaryOpNode*>(expr))
        return expressionCarriesLocalBorrow(binary->left) ||
               expressionCarriesLocalBorrow(binary->right);
    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
        for(auto* argument : call->arguments)
            if(expressionCarriesLocalBorrow(argument))
                return true;
    if(auto* invoke = dynamic_cast<CallableInvokeNode*>(expr))
    {
        if(expressionCarriesLocalBorrow(invoke->callable))
            return true;
        for(auto* argument : invoke->arguments)
            if(expressionCarriesLocalBorrow(argument))
                return true;
    }
    if(auto* method = dynamic_cast<MethodCallNode*>(expr))
    {
        if(expressionCarriesLocalBorrow(method->object))
            return true;
        for(auto* argument : method->arguments)
            if(expressionCarriesLocalBorrow(argument))
                return true;
    }
    if(auto* match = dynamic_cast<MatchExpressionNode*>(expr))
    {
        if(expressionCarriesLocalBorrow(match->target))
            return true;
        for(auto* arm : match->arms)
            if(arm && (expressionCarriesLocalBorrow(arm->pattern
                                                        ? arm->pattern->literal
                                                        : nullptr) ||
                       expressionCarriesLocalBorrow(arm->expression)))
                return true;
    }
    if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
        return expressionCarriesLocalBorrow(field->object);
    if(auto* index = dynamic_cast<IndexExpressionNode*>(expr))
        return expressionCarriesLocalBorrow(index->base) ||
               expressionCarriesLocalBorrow(index->index);
    if(auto* tuple = dynamic_cast<TupleAccessNode*>(expr))
        return expressionCarriesLocalBorrow(tuple->tuple);
    if(auto* structure = dynamic_cast<StructLiteralNode*>(expr))
        for(const auto& member : structure->fields)
            if(expressionCarriesLocalBorrow(member.second))
                return true;
    if(auto* tuple = dynamic_cast<TupleLiteralNode*>(expr))
        if(tuple->elements)
            for(auto* element : tuple->elements->elements)
                if(expressionCarriesLocalBorrow(element))
                    return true;
    if(auto* list = dynamic_cast<ListLiteralNode*>(expr))
        if(list->elements)
            for(auto* element : list->elements->elements)
                if(expressionCarriesLocalBorrow(element))
                    return true;
    if(auto* map = dynamic_cast<MapLiteralNode*>(expr))
        if(map->entries)
            for(auto* entry : map->entries->entries)
                if(entry &&
                   (expressionCarriesLocalBorrow(entry->key) ||
                    expressionCarriesLocalBorrow(entry->value)))
                    return true;
    if(auto* array = dynamic_cast<ArrayFillNode*>(expr))
        return expressionCarriesLocalBorrow(array->value) ||
               expressionCarriesLocalBorrow(array->count);
    if(auto* cast = dynamic_cast<CastExpressionNode*>(expr))
        return expressionCarriesLocalBorrow(cast->expression);
    if(auto* attempt = dynamic_cast<TryExpressionNode*>(expr))
        return expressionCarriesLocalBorrow(attempt->expression);
    if(auto* range = dynamic_cast<RangeExpressionNode*>(expr))
        return expressionCarriesLocalBorrow(range->start) ||
               expressionCarriesLocalBorrow(range->end);
    if(auto* iterator = dynamic_cast<MapIteratorNode*>(expr))
        return expressionCarriesLocalBorrow(iterator->mapExpr);
    if(auto* format = dynamic_cast<FormatNode*>(expr))
    {
        for(auto* argument : format->arguments)
            if(expressionCarriesLocalBorrow(argument))
                return true;
        for(const auto& argument : format->namedArguments)
            if(expressionCarriesLocalBorrow(argument.second))
                return true;
    }
    if(auto* assembly = dynamic_cast<InlineAsmNode*>(expr))
        for(auto* argument : assembly->arguments)
            if(expressionCarriesLocalBorrow(argument))
                return true;
    if(auto* compileTime = dynamic_cast<CexprExpressionNode*>(expr))
        return expressionCarriesLocalBorrow(compileTime->expression);
    return false;
}

bool CodeGenerator::expressionIsPointerParameterBorrow(ExpressionNode* expr) const
{
    if(!expr)
        return false;
    if(auto* identifier = dynamic_cast<IdentifierNode*>(expr))
    {
        auto origin = pointerBorrowTarget.find(identifier->name);
        return origin != pointerBorrowTarget.end() &&
               pointerParameterBorrowOrigins.count(origin->second) != 0;
    }
    if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
    {
        const IdentifierNode* root = nullptr;
        ExpressionNode* base = field->object;
        while(auto* nestedField = dynamic_cast<FieldAccessNode*>(base))
            base = nestedField->object;
        root = dynamic_cast<IdentifierNode*>(base);
        const std::string rootName =
            root ? root->name : (field->object ? std::string()
                                               : field->structName);
        auto origins = aggregateBorrowOrigins.find(rootName);
        return !rootName.empty() && origins != aggregateBorrowOrigins.end() &&
               !origins->second.empty() &&
               std::all_of(origins->second.begin(), origins->second.end(),
                           [&](const std::string& owner) {
                               return pointerParameterBorrowOrigins.count(
                                          owner) != 0;
                           });
    }
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        // Loading a pointer value through another pointer is not a direct
        // forwarding relationship: the loaded value may point elsewhere.
        if(unary->op == UnaryOpNode::OP_DEREF)
            return false;
        if(unary->op == UnaryOpNode::OP_ADDR ||
           unary->op == UnaryOpNode::OP_ADDR_MUT)
        {
            if(!dynamic_cast<UnaryOpNode*>(unary->operand) ||
               dynamic_cast<UnaryOpNode*>(unary->operand)->op !=
                   UnaryOpNode::OP_DEREF)
                return false;
        }
        return expressionIsPointerParameterBorrow(unary->operand);
    }
    if(auto* cast = dynamic_cast<CastExpressionNode*>(expr))
        return expressionIsPointerParameterBorrow(cast->expression);
    if(auto* attempt = dynamic_cast<TryExpressionNode*>(expr))
        return expressionIsPointerParameterBorrow(attempt->expression);
    if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
        return expressionIsPointerParameterBorrow(ternary->trueExpr) &&
               expressionIsPointerParameterBorrow(ternary->falseExpr);
    if(auto* match = dynamic_cast<MatchExpressionNode*>(expr))
    {
        if(match->arms.empty())
            return false;
        return std::all_of(match->arms.begin(), match->arms.end(),
                           [&](MatchArmNode* arm) {
                               return arm && expressionIsPointerParameterBorrow(
                                                 arm->expression);
                           });
    }
    if(auto* binary = dynamic_cast<BinaryOpNode*>(expr))
        return binary->op == BinaryOpNode::OP_COMMA
                   ? expressionIsPointerParameterBorrow(binary->right)
                   : false;
    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
    {
        bool hasUnknownSummary = false;
        for(const auto& calledName :
            getFunctionItemTargetsForName(call->name))
        {
            auto overloads = functionOverloads.find(calledName);
            if(overloads == functionOverloads.end())
            {
                hasUnknownSummary = true;
                continue;
            }
            for(const auto& overload : overloads->second)
            {
                if(!overload.node || !overload.node->returnType ||
                   overload.node->returnType->kind != TypeNode::TYPE_PTR)
                    continue;
                if(!functionPointerReturnSummaryComplete.count(
                       overload.symbolName))
                {
                    hasUnknownSummary = true;
                    continue;
                }
                auto summary = functionPointerReturnParameters.find(
                    overload.symbolName);
                if(summary == functionPointerReturnParameters.end())
                    continue;
                for(size_t index : summary->second)
                    if(index < call->arguments.size() &&
                       expressionIsPointerParameterBorrow(
                           call->arguments[index]))
                        return true;
            }
        }
        if(hasUnknownSummary)
            for(auto* argument : call->arguments)
                if(expressionIsPointerParameterBorrow(argument))
                    return true;
    }
    return false;
}

bool CodeGenerator::expressionIsKnownSharedBorrow(ExpressionNode* expr) const
{
    if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
        return expressionIsKnownSharedBorrow(ternary->trueExpr) ||
               expressionIsKnownSharedBorrow(ternary->falseExpr);
    if(auto* match = dynamic_cast<MatchExpressionNode*>(expr))
        for(auto* arm : match->arms)
            if(arm && expressionIsKnownSharedBorrow(arm->expression))
                return true;
    if(auto* cast = dynamic_cast<CastExpressionNode*>(expr))
        return expressionIsKnownSharedBorrow(cast->expression);
    if(auto* attempt = dynamic_cast<TryExpressionNode*>(expr))
        return expressionIsKnownSharedBorrow(attempt->expression);
    if(auto* address = dynamic_cast<UnaryOpNode*>(expr))
        return (address->op == UnaryOpNode::OP_ADDR &&
                !resolveBorrowOwnerFromLValue(address->operand).empty()) ||
               expressionIsKnownSharedBorrow(address->operand);
    if(auto* identifier = dynamic_cast<IdentifierNode*>(expr))
    {
        auto owner = pointerBorrowTarget.find(identifier->name);
        if(owner == pointerBorrowTarget.end() || isMutBorrower(identifier->name))
            return false;
        auto borrowers = activeBorrowers.find(owner->second);
        return borrowers != activeBorrowers.end() &&
               borrowers->second.count(identifier->name) != 0;
    }
    auto bodyReturnsSharedBorrow = [&](StatementListNode* body,
                                       bool allowTrackedIdentifiers) {
        bool found = false;
        std::map<std::string, std::vector<ExpressionNode*>> localBorrowAliases;
        bool inConditionalRegion = false;
        auto eraseLocalBorrowAliasSubtree = [&](const std::string& path) {
            for(auto alias = localBorrowAliases.begin();
                alias != localBorrowAliases.end();)
            {
                const bool descendant =
                    alias->first.size() > path.size() &&
                    alias->first.compare(0, path.size(), path) == 0 &&
                    (alias->first[path.size()] == '.' ||
                     alias->first[path.size()] == '[');
                if(alias->first == path || descendant)
                    alias = localBorrowAliases.erase(alias);
                else
                    ++alias;
            }
        };
        auto recordLocalBorrowAlias = [&](const std::string& path,
                                          std::vector<ExpressionNode*> origins) {
            if(path.empty())
                return;
            if(inConditionalRegion)
            {
                if(!origins.empty())
                {
                    auto& existing = localBorrowAliases[path];
                    existing.insert(existing.end(), origins.begin(),
                                    origins.end());
                }
            }
            else if(origins.empty())
                localBorrowAliases.erase(path);
            else
                localBorrowAliases[path] = std::move(origins);
        };
        std::function<std::string(ExpressionNode*)> localBorrowPlacePath;
        localBorrowPlacePath = [&](ExpressionNode* value) -> std::string {
            if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
                return identifier->name;
            if(auto* field = dynamic_cast<FieldAccessNode*>(value))
            {
                const std::string base = field->object
                                             ? localBorrowPlacePath(field->object)
                                             : field->structName;
                return base.empty() ? std::string{}
                                    : base + "." + field->fieldName;
            }
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(value))
            {
                const std::string base = localBorrowPlacePath(tuple->tuple);
                return base.empty() ? std::string{}
                                    : base + "." + std::to_string(tuple->index);
            }
            if(auto* index = dynamic_cast<IndexExpressionNode*>(value))
            {
                const std::string base = localBorrowPlacePath(index->base);
                return base.empty() ? std::string{}
                                    : base + borrowMapKeyPath(index->index);
            }
            return {};
        };
        auto localBorrowDereferencePaths = [&](ExpressionNode* pointer) {
            std::set<std::string> targets;
            if(auto* address = dynamic_cast<UnaryOpNode*>(pointer))
                if(address->op == UnaryOpNode::OP_ADDR ||
                   address->op == UnaryOpNode::OP_ADDR_MUT)
                {
                    const std::string target =
                        localBorrowPlacePath(address->operand);
                    if(!target.empty())
                        targets.insert(target);
                    return targets;
                }
            auto* identifier = dynamic_cast<IdentifierNode*>(pointer);
            if(!identifier)
                return targets;
            auto aliases = localBorrowAliases.find(identifier->name);
            if(aliases == localBorrowAliases.end())
                return targets;
            for(auto* origin : aliases->second)
            {
                auto* originAddress = dynamic_cast<UnaryOpNode*>(origin);
                if(!originAddress ||
                   (originAddress->op != UnaryOpNode::OP_ADDR &&
                    originAddress->op != UnaryOpNode::OP_ADDR_MUT))
                    continue;
                const std::string candidate =
                    localBorrowPlacePath(originAddress->operand);
                if(candidate.empty())
                    continue;
                targets.insert(candidate);
            }
            return targets;
        };
        std::function<bool(ExpressionNode*)> isSharedBorrowExpression;
        std::function<void(ExpressionNode*,
                           std::vector<ExpressionNode*>&)>
            collectBorrowOrigins;
        std::function<void(ExpressionNode*, const std::string&)>
            collectAggregateBorrowFields;
        isSharedBorrowExpression = [&](ExpressionNode* value) {
            if(auto* address = dynamic_cast<UnaryOpNode*>(value))
                return address->op == UnaryOpNode::OP_ADDR &&
                       !resolveBorrowOwnerFromLValue(address->operand).empty();
            if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
            {
                auto aliases = localBorrowAliases.find(identifier->name);
                if(aliases != localBorrowAliases.end())
                    for(auto* origin : aliases->second)
                        if(isSharedBorrowExpression(origin))
                            return true;
                return allowTrackedIdentifiers &&
                       expressionIsKnownSharedBorrow(identifier);
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(value))
            {
                const std::string path = localBorrowPlacePath(field);
                auto aliases = localBorrowAliases.find(path);
                if(aliases != localBorrowAliases.end())
                    for(auto* origin : aliases->second)
                        if(isSharedBorrowExpression(origin))
                            return true;
                return false;
            }
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(value))
            {
                const std::string path = localBorrowPlacePath(tuple);
                auto aliases = localBorrowAliases.find(path);
                if(aliases != localBorrowAliases.end())
                    for(auto* origin : aliases->second)
                        if(isSharedBorrowExpression(origin))
                            return true;
                return false;
            }
            if(auto* index = dynamic_cast<IndexExpressionNode*>(value))
            {
                const std::string path = localBorrowPlacePath(index);
                auto aliases = localBorrowAliases.find(path);
                if(aliases != localBorrowAliases.end())
                    for(auto* origin : aliases->second)
                        if(isSharedBorrowExpression(origin))
                            return true;
                return false;
            }
            if(auto* ternary = dynamic_cast<TernaryNode*>(value))
                return isSharedBorrowExpression(ternary->trueExpr) ||
                       isSharedBorrowExpression(ternary->falseExpr);
            if(auto* match = dynamic_cast<MatchExpressionNode*>(value))
                for(auto* arm : match->arms)
                    if(arm && isSharedBorrowExpression(arm->expression))
                        return true;
            if(auto* cast = dynamic_cast<CastExpressionNode*>(value))
                return isSharedBorrowExpression(cast->expression);
            if(auto* attempt = dynamic_cast<TryExpressionNode*>(value))
                return isSharedBorrowExpression(attempt->expression);
            if(auto* comma = dynamic_cast<BinaryOpNode*>(value))
                if(comma->op == BinaryOpNode::OP_COMMA)
                    return isSharedBorrowExpression(comma->right);
            return false;
        };
        collectBorrowOrigins = [&](ExpressionNode* value,
                                   std::vector<ExpressionNode*>& origins) {
            if(auto* address = dynamic_cast<UnaryOpNode*>(value))
            {
                if(address->op == UnaryOpNode::OP_ADDR ||
                   address->op == UnaryOpNode::OP_ADDR_MUT)
                    origins.push_back(value);
                return;
            }
            if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
            {
                auto aliases = localBorrowAliases.find(identifier->name);
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                else if(allowTrackedIdentifiers &&
                        expressionIsKnownSharedBorrow(identifier))
                    origins.push_back(identifier);
                return;
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(value))
            {
                auto aliases = localBorrowAliases.find(
                    localBorrowPlacePath(field));
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                return;
            }
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(value))
            {
                auto aliases = localBorrowAliases.find(
                    localBorrowPlacePath(tuple));
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                return;
            }
            if(auto* index = dynamic_cast<IndexExpressionNode*>(value))
            {
                auto aliases = localBorrowAliases.find(
                    localBorrowPlacePath(index));
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                return;
            }
            if(auto* ternary = dynamic_cast<TernaryNode*>(value))
            {
                collectBorrowOrigins(ternary->trueExpr, origins);
                collectBorrowOrigins(ternary->falseExpr, origins);
            }
            else if(auto* match = dynamic_cast<MatchExpressionNode*>(value))
            {
                for(auto* arm : match->arms)
                    if(arm)
                        collectBorrowOrigins(arm->expression, origins);
            }
            else if(auto* cast = dynamic_cast<CastExpressionNode*>(value))
                collectBorrowOrigins(cast->expression, origins);
            else if(auto* attempt = dynamic_cast<TryExpressionNode*>(value))
                collectBorrowOrigins(attempt->expression, origins);
            else if(auto* comma = dynamic_cast<BinaryOpNode*>(value))
                if(comma->op == BinaryOpNode::OP_COMMA)
                    collectBorrowOrigins(comma->right, origins);
        };
        collectAggregateBorrowFields = [&](ExpressionNode* value,
                                           const std::string& path) {
            if(auto* structure = dynamic_cast<StructLiteralNode*>(value))
            {
                for(const auto& member : structure->fields)
                {
                    const std::string fieldPath =
                        path + "." + member.first;
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(member.second, origins);
                    recordLocalBorrowAlias(fieldPath, std::move(origins));
                    collectAggregateBorrowFields(member.second, fieldPath);
                }
            }
            else if(auto* tuple = dynamic_cast<TupleLiteralNode*>(value))
            {
                if(!tuple->elements)
                    return;
                for(size_t index = 0;
                    index < tuple->elements->elements.size(); ++index)
                {
                    const std::string elementPath =
                        path + "." + std::to_string(index);
                    auto* element = tuple->elements->elements[index];
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(element, origins);
                    recordLocalBorrowAlias(elementPath, std::move(origins));
                    collectAggregateBorrowFields(element, elementPath);
                }
            }
            else if(auto* list = dynamic_cast<ListLiteralNode*>(value))
            {
                if(!list->elements)
                    return;
                for(size_t index = 0;
                    index < list->elements->elements.size(); ++index)
                {
                    const std::string elementPath =
                        path + "[" + std::to_string(index) + "]";
                    auto* element = list->elements->elements[index];
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(element, origins);
                    recordLocalBorrowAlias(elementPath, std::move(origins));
                    collectAggregateBorrowFields(element, elementPath);
                }
            }
            else if(auto* map = dynamic_cast<MapLiteralNode*>(value))
            {
                if(!map->entries)
                    return;
                for(auto* entry : map->entries->entries)
                {
                    if(!entry)
                        continue;
                    const std::string valuePath =
                        path + borrowMapKeyPath(entry->key);
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(entry->value, origins);
                    recordLocalBorrowAlias(valuePath, std::move(origins));
                    collectAggregateBorrowFields(entry->value, valuePath);
                }
            }
        };
        std::function<void(StatementNode*)> collectStatement;
        std::function<void(StatementListNode*)> collectList;
        collectList = [&](StatementListNode* statements) {
            if(!statements || found)
                return;
            for(auto* statement : statements->statements)
            {
                collectStatement(statement);
                if(found)
                    return;
            }
        };
        collectStatement = [&](StatementNode* statement) {
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
                found = isSharedBorrowExpression(returned->expression);
            else if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(declaration->expression, origins);
                if(!origins.empty())
                    localBorrowAliases[declaration->name] =
                        std::move(origins);
                collectAggregateBorrowFields(declaration->expression,
                                             declaration->name);
            }
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            {
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(declaration->initExpr, origins);
                if(!origins.empty())
                    localBorrowAliases[declaration->name] =
                        std::move(origins);
                collectAggregateBorrowFields(declaration->initExpr,
                                             declaration->name);
            }
            else if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
            {
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(assignment->expression, origins);
                if(!inConditionalRegion)
                    eraseLocalBorrowAliasSubtree(assignment->name);
                recordLocalBorrowAlias(assignment->name, std::move(origins));
                collectAggregateBorrowFields(assignment->expression,
                                             assignment->name);
            }
            else if(auto* assignment =
                        dynamic_cast<FieldAssignmentNode*>(statement))
            {
                const std::string path =
                    assignment->target
                        ? localBorrowPlacePath(assignment->target)
                        : assignment->structName + "." + assignment->fieldName;
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(assignment->expression, origins);
                if(!path.empty())
                {
                    if(!inConditionalRegion)
                        eraseLocalBorrowAliasSubtree(path);
                    recordLocalBorrowAlias(path, std::move(origins));
                    collectAggregateBorrowFields(assignment->expression,
                                                 path);
                }
            }
            else if(auto* assignment =
                        dynamic_cast<DerefAssignmentNode*>(statement))
            {
                const auto paths =
                    localBorrowDereferencePaths(assignment->pointerExpr);
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(assignment->value, origins);
                for(const auto& path : paths)
                {
                    if(!inConditionalRegion)
                        eraseLocalBorrowAliasSubtree(path);
                    recordLocalBorrowAlias(path, origins);
                    collectAggregateBorrowFields(assignment->value, path);
                }
            }
            else if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                collectList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectList(branch->thenBranch);
                collectStatement(branch->elseIfBranch);
                collectList(branch->elseBranch);
                inConditionalRegion = wasConditional;
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectList(branch->thenBranch);
                collectStatement(branch->elseIfBranch);
                collectList(branch->elseBranch);
                inConditionalRegion = wasConditional;
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectList(loop->body);
                inConditionalRegion = wasConditional;
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectList(loop->body);
                inConditionalRegion = wasConditional;
            }
        };
        collectList(body);
        return found;
    };
    auto closureReturnsSharedBorrow = [&](const std::string& name) {
        auto runtimeClosure = runtimeClosureVariables.find(name);
        auto functionItems = functionItemVariables.find(name);
        const bool hasFunctionItems = functionItems != functionItemVariables.end() &&
                                      !functionItems->second.empty();
        if(runtimeClosure == runtimeClosureVariables.end() ||
           !runtimeClosure->second || hasFunctionItems)
            return false;
        return bodyReturnsSharedBorrow(runtimeClosure->second->body, true);
    };
    if(auto* invoke = dynamic_cast<CallableInvokeNode*>(expr))
    {
        auto* callable = dynamic_cast<IdentifierNode*>(invoke->callable);
        if(!callable)
            return false;
        if(closureReturnsSharedBorrow(callable->name))
            return true;

        bool hasUnknownSummary = false;
        for(const auto& calledName :
            getFunctionItemTargetsForName(callable->name))
        {
            auto overloads = functionOverloads.find(calledName);
            if(overloads == functionOverloads.end())
            {
                hasUnknownSummary = true;
                continue;
            }
            for(const auto& overload : overloads->second)
            {
                if(!overload.node || !overload.node->returnType ||
                   overload.node->returnType->kind != TypeNode::TYPE_PTR)
                    continue;
                if(bodyReturnsSharedBorrow(overload.node->body, false))
                    return true;
                if(!functionPointerReturnSummaryComplete.count(
                       overload.symbolName))
                {
                    hasUnknownSummary = true;
                    continue;
                }
                auto summary = functionPointerReturnParameters.find(
                    overload.symbolName);
                if(summary == functionPointerReturnParameters.end())
                    continue;
                for(size_t index : summary->second)
                    if(index < invoke->arguments.size() &&
                       expressionIsKnownSharedBorrow(invoke->arguments[index]))
                        return true;
            }
        }
        if(hasUnknownSummary)
            for(auto* argument : invoke->arguments)
                if(expressionIsKnownSharedBorrow(argument))
                    return true;
    }
    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
    {
        if(closureReturnsSharedBorrow(call->name))
            return true;
        bool hasUnknownSummary = false;
        for(const auto& calledName :
            getFunctionItemTargetsForName(call->name))
        {
            auto overloads = functionOverloads.find(calledName);
            if(overloads == functionOverloads.end())
            {
                hasUnknownSummary = true;
                continue;
            }
            for(const auto& overload : overloads->second)
            {
                if(!overload.node || !overload.node->returnType ||
                   overload.node->returnType->kind != TypeNode::TYPE_PTR)
                    continue;
                if(bodyReturnsSharedBorrow(overload.node->body, false))
                    return true;
                if(!functionPointerReturnSummaryComplete.count(
                       overload.symbolName))
                {
                    hasUnknownSummary = true;
                    continue;
                }
                auto summary = functionPointerReturnParameters.find(
                    overload.symbolName);
                if(summary == functionPointerReturnParameters.end())
                    continue;
                for(size_t index : summary->second)
                    if(index < call->arguments.size() &&
                       expressionIsKnownSharedBorrow(call->arguments[index]))
                        return true;
            }
        }
        if(hasUnknownSummary)
            for(auto* argument : call->arguments)
                if(expressionIsKnownSharedBorrow(argument))
                    return true;
    }
    const std::string container = resolveBorrowOwnerFromLValue(expr);
    const std::string sourcePath = resolveBorrowPlaceFromLValue(expr);
    if(container.empty() || sourcePath.empty() ||
       sourcePath.compare(0, container.size(), container) != 0)
        return false;
    std::string relativePath = sourcePath.substr(container.size());
    if(!relativePath.empty() && relativePath.front() == '.')
        relativePath.erase(0, 1);
    auto fields = aggregateBorrowFieldPlaces.find(container);
    return fields != aggregateBorrowFieldPlaces.end() &&
           fields->second.count(relativePath) != 0;
}

bool CodeGenerator::validateNoEscapingBorrow(ExpressionNode* expr, int line,
                                             const std::string& action)
{
    if(dynamic_cast<ClosureNode*>(expr))
    {
        reportError(line, "cannot " + action +
                              " a closure literal before its capture lifetime "
                              "is proven safe");
        return false;
    }

    // Named function items have no closure environment and therefore cannot
    // carry a borrow. Do not ask getLValueType() to resolve an overloaded
    // function here: the surrounding callable type (for example a function
    // return type) supplies the context needed to select the overload later.
    if(auto* identifier = dynamic_cast<IdentifierNode*>(expr))
        if(functionOverloads.count(identifier->name) &&
           !namedValues.count(identifier->name) &&
           !globalNamedValues.count(identifier->name))
            return true;

    TypeNode* escapingValueType = getLValueType(expr, line);
    if(!escapingValueType)
        escapingValueType = inferExpressionTypeNode(expr, line);
    const bool isAggregateValue = escapingValueType &&
        (escapingValueType->kind == TypeNode::TYPE_STRUCT ||
         escapingValueType->kind == TypeNode::TYPE_LIST ||
         escapingValueType->kind == TypeNode::TYPE_MAP ||
         escapingValueType->kind == TypeNode::TYPE_TUPLE);
    if(isAggregateValue)
    {
        std::set<std::string> borrowOrigins;
        std::function<void(ExpressionNode*)> findBorrowOrigins =
            [&](ExpressionNode* current) {
                if(!current)
                    return;
                if(auto* identifier = dynamic_cast<IdentifierNode*>(current))
                {
                    auto borrow = pointerBorrowTarget.find(identifier->name);
                    if(borrow != pointerBorrowTarget.end())
                        borrowOrigins.insert(borrow->second);
                    auto aggregate = aggregateBorrowOrigins.find(identifier->name);
                    if(aggregate != aggregateBorrowOrigins.end())
                        borrowOrigins.insert(aggregate->second.begin(),
                                             aggregate->second.end());
                    return;
                }
                if(auto* unary = dynamic_cast<UnaryOpNode*>(current))
                {
                    if(unary->op == UnaryOpNode::OP_ADDR ||
                       unary->op == UnaryOpNode::OP_ADDR_MUT)
                    {
                        const std::string owner =
                            resolveBorrowOwnerFromLValue(unary->operand);
                        if(!owner.empty())
                            borrowOrigins.insert(owner);
                        return;
                    }
                    findBorrowOrigins(unary->operand);
                    return;
                }
                if(auto* ternary = dynamic_cast<TernaryNode*>(current))
                {
                    findBorrowOrigins(ternary->trueExpr);
                    findBorrowOrigins(ternary->falseExpr);
                    return;
                }
                if(auto* match = dynamic_cast<MatchExpressionNode*>(current))
                {
                    for(auto* arm : match->arms)
                        if(arm)
                            findBorrowOrigins(arm->expression);
                    return;
                }
                if(auto* attempt = dynamic_cast<TryExpressionNode*>(current))
                {
                    findBorrowOrigins(attempt->expression);
                    return;
                }
                if(auto* structure = dynamic_cast<StructLiteralNode*>(current))
                {
                    for(const auto& field : structure->fields)
                        findBorrowOrigins(field.second);
                    return;
                }
                if(auto* tuple = dynamic_cast<TupleLiteralNode*>(current))
                {
                    if(tuple->elements)
                        for(auto* element : tuple->elements->elements)
                            findBorrowOrigins(element);
                    return;
                }
                if(auto* list = dynamic_cast<ListLiteralNode*>(current))
                {
                    if(list->elements)
                        for(auto* element : list->elements->elements)
                            findBorrowOrigins(element);
                    return;
                }
                if(auto* map = dynamic_cast<MapLiteralNode*>(current))
                {
                    if(map->entries)
                        for(auto* entry : map->entries->entries)
                        {
                            findBorrowOrigins(entry->key);
                            findBorrowOrigins(entry->value);
                        }
                    return;
                }
                if(auto* array = dynamic_cast<ArrayFillNode*>(current))
                {
                    findBorrowOrigins(array->value);
                    return;
                }
                if(auto* field = dynamic_cast<FieldAccessNode*>(current))
                {
                    findBorrowOrigins(field->object);
                    return;
                }
                if(auto* index = dynamic_cast<IndexExpressionNode*>(current))
                {
                    findBorrowOrigins(index->base);
                    findBorrowOrigins(index->index);
                    return;
                }
                if(auto* tuple = dynamic_cast<TupleAccessNode*>(current))
                {
                    findBorrowOrigins(tuple->tuple);
                    return;
                }
                if(auto* binary = dynamic_cast<BinaryOpNode*>(current))
                {
                    findBorrowOrigins(binary->left);
                    findBorrowOrigins(binary->right);
                    return;
                }
                if(auto* cast = dynamic_cast<CastExpressionNode*>(current))
                {
                    findBorrowOrigins(cast->expression);
                    return;
                }
                if(auto* call = dynamic_cast<FunctionCallNode*>(current))
                {
                    for(auto* argument : call->arguments)
                        findBorrowOrigins(argument);
                    return;
                }
                if(auto* invoke = dynamic_cast<CallableInvokeNode*>(current))
                {
                    findBorrowOrigins(invoke->callable);
                    for(auto* argument : invoke->arguments)
                        findBorrowOrigins(argument);
                    return;
                }
                if(auto* method = dynamic_cast<MethodCallNode*>(current))
                {
                    findBorrowOrigins(method->object);
                    for(auto* argument : method->arguments)
                        findBorrowOrigins(argument);
                }
            };
        findBorrowOrigins(expr);
        for(const auto& owner : borrowOrigins)
            if(variableScopeDepth.find(owner) != variableScopeDepth.end())
            {
                reportError(line, "cannot " + action +
                                      " aggregate containing a pointer that "
                                      "borrows local value: '" + owner + "'");
                return false;
            }
    }

    if(escapingValueType &&
       dynamic_cast<PointerTypeNode*>(escapingValueType))
    {
        std::string callableParameter;
        if(auto* invoke = dynamic_cast<CallableInvokeNode*>(expr))
            if(auto* callable =
                   dynamic_cast<IdentifierNode*>(invoke->callable))
                callableParameter = callable->name;
        if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
            callableParameter = call->name;
        if(!callableParameter.empty() &&
           functionTypeParameters.count(callableParameter) != 0 &&
           runtimeClosureVariables.count(callableParameter) == 0)
        {
            reportError(
                line, "cannot " + action +
                          " pointer returned by callable parameter '" +
                          callableParameter +
                          "' because its capture lifetime is unknown");
            return false;
        }
    }

    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto runtimeClosure = runtimeClosureVariables.find(idExpr->name);
        auto declaredClosure = closureVariables.find(idExpr->name);
        if(runtimeClosure != runtimeClosureVariables.end() &&
           declaredClosure != closureVariables.end() &&
           runtimeClosure->second == declaredClosure->second)
        {
            auto owners = runtimeClosureBorrowOwners.find(idExpr->name);
            if(owners != runtimeClosureBorrowOwners.end() &&
               !owners->second.empty())
            {
                reportError(line, "cannot " + action + " closure '" +
                                      idExpr->name +
                                      "' because it borrows local variable '" +
                                      *owners->second.begin() + "'");
            }
            else
            {
                // An empty owner set means the closure environment contains
                // no stack borrows (global captures are not put in the
                // environment), so the value may cross a function boundary.
                return true;
            }
            return false;
        }
        auto closureIt = runtimeClosureBorrowOwners.find(idExpr->name);
        if(closureIt != runtimeClosureBorrowOwners.end())
        {
            for(const auto& owner : closureIt->second)
            {
                if(globalNamedValues.find(owner) != globalNamedValues.end())
                    continue;
                reportError(line, "cannot " + action + " closure '" +
                                      idExpr->name +
                                      "' because it borrows local variable '" +
                                      owner + "'");
                return false;
            }
        }
        if(functionTypeVariables.find(idExpr->name) !=
               functionTypeVariables.end() &&
           runtimeClosureVariables.find(idExpr->name) ==
               runtimeClosureVariables.end())
        {
            reportError(line, "cannot " + action + " callable parameter '" +
                                  idExpr->name +
                                  "' because its capture lifetime is unknown");
            return false;
        }
    }

    std::set<std::string> ownerNames;
    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = pointerBorrowTarget.find(idExpr->name);
        if(it != pointerBorrowTarget.end())
            ownerNames.insert(it->second);
        for(const auto& active : activeBorrowers)
            if(active.second.count(idExpr->name) != 0)
                ownerNames.insert(active.first);
    }
    else if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_ADDR)
        {
            const std::string owner =
                resolveBorrowOwnerFromLValue(unary->operand);
            if(!owner.empty())
                ownerNames.insert(owner);
        }
    }

    if(ownerNames.empty())
    {
        std::function<void(ExpressionNode*)> collectPointerOwners;
        std::function<void(const std::string&)> collectClosureReturns;
        collectClosureReturns = [&](const std::string& name) {
            auto runtime = runtimeClosureVariables.find(name);
            if(runtime == runtimeClosureVariables.end() || !runtime->second)
                return;
            std::function<void(StatementNode*)> collectStatementReturns;
            std::function<void(StatementListNode*)> collectListReturns;
            collectListReturns = [&](StatementListNode* statements) {
                if(!statements)
                    return;
                for(auto* statement : statements->statements)
                    collectStatementReturns(statement);
            };
            collectStatementReturns = [&](StatementNode* statement) {
                if(auto* returned = dynamic_cast<ReturnNode*>(statement))
                {
                    collectPointerOwners(returned->expression);
                    return;
                }
                if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                {
                    collectListReturns(block->statements);
                    return;
                }
                if(auto* branch = dynamic_cast<IfNode*>(statement))
                {
                    collectListReturns(branch->thenBranch);
                    collectStatementReturns(branch->elseIfBranch);
                    collectListReturns(branch->elseBranch);
                    return;
                }
                if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
                {
                    collectListReturns(branch->thenBranch);
                    collectStatementReturns(branch->elseIfBranch);
                    collectListReturns(branch->elseBranch);
                    return;
                }
                if(auto* loop = dynamic_cast<WhileNode*>(statement))
                    collectListReturns(loop->body);
                else if(auto* loop = dynamic_cast<ForNode*>(statement))
                    collectListReturns(loop->body);
            };
            collectListReturns(runtime->second->body);
        };
        collectPointerOwners = [&](ExpressionNode* current) {
                if(!current)
                    return;
                if(auto* identifier = dynamic_cast<IdentifierNode*>(current))
                {
                    auto owner = pointerBorrowTarget.find(identifier->name);
                    if(owner != pointerBorrowTarget.end())
                        ownerNames.insert(owner->second);
                    auto aggregate =
                        aggregateBorrowOrigins.find(identifier->name);
                    if(aggregate != aggregateBorrowOrigins.end())
                        ownerNames.insert(aggregate->second.begin(),
                                          aggregate->second.end());
                    return;
                }
                if(auto* field = dynamic_cast<FieldAccessNode*>(current))
                {
                    const std::string owner =
                        resolveBorrowOwnerFromLValue(field);
                    const std::string place =
                        resolveBorrowPlaceFromLValue(field);
                    if(!owner.empty() && place.compare(0, owner.size(), owner) ==
                                             0)
                    {
                        std::string relative = place.substr(owner.size());
                        if(!relative.empty() && relative.front() == '.')
                            relative.erase(0, 1);
                        auto aggregate = aggregateBorrowFieldPlaces.find(owner);
                        if(aggregate != aggregateBorrowFieldPlaces.end())
                        {
                            auto fieldOrigins = aggregate->second.find(relative);
                            if(fieldOrigins != aggregate->second.end())
                                for(const auto& origin : fieldOrigins->second)
                                    ownerNames.insert(origin.first);
                        }
                    }
                    return;
                }
                if(auto* address = dynamic_cast<UnaryOpNode*>(current))
                {
                    if(address->op == UnaryOpNode::OP_ADDR ||
                       address->op == UnaryOpNode::OP_ADDR_MUT)
                    {
                        const std::string owner =
                            resolveBorrowOwnerFromLValue(address->operand);
                        if(!owner.empty())
                            ownerNames.insert(owner);
                        return;
                    }
                    if(address->op == UnaryOpNode::OP_DEREF)
                    {
                        TypeNode* resultType = getLValueType(current, line);
                        if(resultType && resultType->kind == TypeNode::TYPE_PTR)
                        {
                            const std::string owner =
                                resolveBorrowOwnerFromLValue(current);
                            if(!owner.empty())
                                ownerNames.insert(owner);
                        }
                    }
                    return;
                }
                if(auto* ternary = dynamic_cast<TernaryNode*>(current))
                {
                    collectPointerOwners(ternary->trueExpr);
                    collectPointerOwners(ternary->falseExpr);
                    return;
                }
                if(auto* match = dynamic_cast<MatchExpressionNode*>(current))
                {
                    for(auto* arm : match->arms)
                        if(arm)
                            collectPointerOwners(arm->expression);
                    return;
                }
                if(auto* cast = dynamic_cast<CastExpressionNode*>(current))
                {
                    if(cast->targetType == TypeNode::TYPE_PTR)
                        collectPointerOwners(cast->expression);
                    return;
                }
                if(auto* attempt =
                       dynamic_cast<TryExpressionNode*>(current))
                {
                    collectPointerOwners(attempt->expression);
                    return;
                }
                if(auto* method = dynamic_cast<MethodCallNode*>(current))
                {
                    TypeNode* resultType = getLValueType(current, line);
                    if(!resultType)
                        resultType = inferExpressionTypeNode(current, line);
                    if(!resultType || resultType->kind != TypeNode::TYPE_PTR)
                        return;

                    TypeNode* receiverType =
                        getLValueType(method->object, line);
                    std::string receiverStruct;
                    if(auto* structure =
                           dynamic_cast<StructTypeRefNode*>(receiverType))
                        receiverStruct = structure->structName;
                    else if(auto* generic =
                                dynamic_cast<GenericStructTypeRefNode*>(
                                    receiverType))
                        receiverStruct = generic->getMangledName();

                    StructMethodNode* methodDefinition = nullptr;
                    auto methods = structMethods.find(receiverStruct);
                    if(methods != structMethods.end())
                    {
                        auto candidate =
                            methods->second.find(method->methodName);
                        if(candidate != methods->second.end())
                            methodDefinition = candidate->second.second;
                    }

                    std::vector<std::string> returnedFields;
                    bool hasOnlyDirectSelfFieldReturns =
                        methodDefinition && methodDefinition->body;
                    auto selfFieldPath = [&](ExpressionNode* value) {
                        std::function<std::optional<std::string>(
                            ExpressionNode*)>
                            resolve = [&](ExpressionNode* expression)
                            -> std::optional<std::string> {
                            if(auto* identifier =
                                   dynamic_cast<IdentifierNode*>(expression))
                                if(identifier->name == "self")
                                    return std::string();
                            auto* access =
                                dynamic_cast<FieldAccessNode*>(expression);
                            if(!access)
                                return std::optional<std::string>();
                            if(!access->object)
                                return access->structName == "self"
                                           ? std::optional<std::string>(
                                                 access->fieldName)
                                           : std::nullopt;
                            auto parent = resolve(access->object);
                            if(!parent)
                                return std::nullopt;
                            return parent->empty()
                                       ? access->fieldName
                                       : *parent + "." + access->fieldName;
                        };
                        return resolve(value);
                    };
                    std::function<void(ExpressionNode*)> collectReturns =
                        [&](ExpressionNode* value) {
                            if(!value)
                            {
                                hasOnlyDirectSelfFieldReturns = false;
                                return;
                            }
                            if(auto* ternary =
                                   dynamic_cast<TernaryNode*>(value))
                            {
                                collectReturns(ternary->trueExpr);
                                collectReturns(ternary->falseExpr);
                                return;
                            }
                            if(auto* match =
                                   dynamic_cast<MatchExpressionNode*>(value))
                            {
                                for(auto* arm : match->arms)
                                    if(arm)
                                        collectReturns(arm->expression);
                                return;
                            }
                            if(auto* cast =
                                   dynamic_cast<CastExpressionNode*>(value))
                            {
                                collectReturns(cast->expression);
                                return;
                            }
                            if(auto* attempt =
                                   dynamic_cast<TryExpressionNode*>(value))
                            {
                                collectReturns(attempt->expression);
                                return;
                            }
                            auto field = selfFieldPath(value);
                            if(field && !field->empty())
                                returnedFields.push_back(*field);
                            else
                                hasOnlyDirectSelfFieldReturns = false;
                        };
                    if(methodDefinition && methodDefinition->body)
                        for(auto* statement :
                            methodDefinition->body->statements)
                        {
                            if(auto* returned =
                                   dynamic_cast<ReturnNode*>(statement))
                                collectReturns(returned->expression);
                            else
                                hasOnlyDirectSelfFieldReturns = false;
                        }

                    const std::string receiverOwner =
                        resolveBorrowOwnerFromLValue(method->object);
                    const std::string receiverPlace =
                        resolveBorrowPlaceFromLValue(method->object);
                    if(hasOnlyDirectSelfFieldReturns &&
                       !returnedFields.empty() && !receiverOwner.empty() &&
                       receiverPlace.compare(0, receiverOwner.size(),
                                             receiverOwner) == 0)
                    {
                        std::string receiverPath = receiverPlace.substr(
                            receiverOwner.size());
                        if(!receiverPath.empty() && receiverPath.front() == '.')
                            receiverPath.erase(0, 1);
                        auto aggregate =
                            aggregateBorrowFieldPlaces.find(receiverOwner);
                        if(aggregate != aggregateBorrowFieldPlaces.end())
                            for(const auto& returnedField : returnedFields)
                            {
                                const std::string fieldPath =
                                    receiverPath.empty()
                                        ? returnedField
                                        : receiverPath + "." + returnedField;
                                auto origins =
                                    aggregate->second.find(fieldPath);
                                if(origins == aggregate->second.end())
                                    continue;
                                for(const auto& origin : origins->second)
                                    ownerNames.insert(origin.first);
                            }
                        // A complete direct-field summary with no local borrow
                        // in those fields is precise; do not taint the result
                        // with unrelated borrowed sibling fields.
                        if(aggregate != aggregateBorrowFieldPlaces.end())
                            return;
                    }

                    // Unknown method behavior remains conservative: any
                    // tracked borrow in the receiver or pointer arguments may
                    // be returned.
                    collectPointerOwners(method->object);
                    for(auto* argument : method->arguments)
                        collectPointerOwners(argument);
                    return;
                }
                if(auto* call = dynamic_cast<FunctionCallNode*>(current))
                {
                    TypeNode* resultType = getLValueType(current, line);
                    if(!resultType)
                        resultType = inferExpressionTypeNode(current, line);
                    if(!resultType || resultType->kind != TypeNode::TYPE_PTR)
                        return;
                    auto knownFunctionItem =
                        functionItemVariables.find(call->name);
                    const bool hasKnownFunctionItemTargets =
                        knownFunctionItem != functionItemVariables.end() &&
                        !knownFunctionItem->second.empty();
                    if(runtimeClosureVariables.count(call->name) != 0 &&
                       !hasKnownFunctionItemTargets)
                    {
                        collectClosureReturns(call->name);
                        for(auto* argument : call->arguments)
                            collectPointerOwners(argument);
                    }
                    else
                    {
                        bool foundCompleteSummary = false;
                        bool hasUnknownSummary = false;
                        for(const auto& calledName :
                            getFunctionItemTargetsForName(call->name))
                        {
                            auto overloads =
                                functionOverloads.find(calledName);
                            if(overloads == functionOverloads.end())
                            {
                                hasUnknownSummary = true;
                                continue;
                            }
                            for(const auto& overload : overloads->second)
                            {
                                if(!overload.node ||
                                   !overload.node->returnType ||
                                   overload.node->returnType->kind !=
                                       TypeNode::TYPE_PTR)
                                    continue;
                                if(!functionPointerReturnSummaryComplete.count(
                                       overload.symbolName))
                                {
                                    hasUnknownSummary = true;
                                    continue;
                                }
                                foundCompleteSummary = true;
                                auto summary =
                                    functionPointerReturnParameters.find(
                                        overload.symbolName);
                                if(summary ==
                                   functionPointerReturnParameters.end())
                                    continue;
                                for(size_t index : summary->second)
                                    if(index < call->arguments.size())
                                    {
                                        collectPointerOwners(
                                            call->arguments[index]);
                                    }
                            }
                        }
                        // An opaque or unresolved pointer-returning call may
                        // forward any pointer argument, so retain all of them.
                        if(hasUnknownSummary || !foundCompleteSummary)
                            for(auto* argument : call->arguments)
                                collectPointerOwners(argument);
                    }
                    return;
                }
                if(auto* invoke = dynamic_cast<CallableInvokeNode*>(current))
                {
                    TypeNode* resultType = getLValueType(current, line);
                    if(!resultType)
                        resultType = inferExpressionTypeNode(current, line);
                    if(!resultType || resultType->kind != TypeNode::TYPE_PTR)
                        return;
                    auto* callable =
                        dynamic_cast<IdentifierNode*>(invoke->callable);
                    if(callable)
                        collectClosureReturns(callable->name);
                    for(auto* argument : invoke->arguments)
                        collectPointerOwners(argument);
                    return;
                }
                if(auto* binary = dynamic_cast<BinaryOpNode*>(current))
                    if(binary->op == BinaryOpNode::OP_COMMA)
                        collectPointerOwners(binary->right);
            };
        collectPointerOwners(expr);
    }

    // Returning a caller-provided pointer is safe when the result is directly
    // forwarded (possibly through a local pointer alias). Call-site analysis
    // attaches the actual argument's borrow to the result local.
    if(action == "return" && expressionIsPointerParameterBorrow(expr))
        return true;

    if(ownerNames.empty())
        return true;

    for(const auto& ownerName : ownerNames)
    {
        if(variableScopeDepth.find(ownerName) == variableScopeDepth.end() &&
           globalNamedValues.find(ownerName) != globalNamedValues.end())
            continue;

        if(namedValues.find(ownerName) == namedValues.end())
            continue;

        // For Copy types (strings), a borrow variable is just a char* value.
        // Returning it does not create a dangling reference.
        auto typeIt = variableTypes.find(ownerName);
        if(typeIt != variableTypes.end() &&
           (typeIt->second == TypeNode::TYPE_STRING ||
            typeIt->second == TypeNode::TYPE_STR8 ||
            typeIt->second == TypeNode::TYPE_STR16))
        {
            return true;
        }
        reportError(line, "cannot " + action +
                              " pointer that borrows local value: '" +
                              ownerName + "'");
        return false;
    }

    return true;
}

void CodeGenerator::clearAggregateBorrowOrigins(const std::string& variable)
{
    aggregateBorrowPlaces.erase(variable);
    aggregateBorrowFieldPlaces.erase(variable);
    // An aggregate that owns a moved `&mut` pointer uses the same exclusive
    // borrow bookkeeping as a local pointer. Release that borrow whenever the
    // aggregate is replaced, goes out of scope, or reaches its last use.
    if(isMutBorrower(variable))
        clearPointerBorrow(variable);
    auto origins = aggregateBorrowOrigins.find(variable);
    if(origins == aggregateBorrowOrigins.end())
    {
        pointerBorrowPlace.erase(variable);
        return;
    }
    for(const auto& owner : origins->second)
    {
        auto borrowers = activeBorrowers.find(owner);
        if(borrowers == activeBorrowers.end())
            continue;
        borrowers->second.erase(variable);
        if(borrowers->second.empty())
            activeBorrowers.erase(borrowers);
    }
    aggregateBorrowOrigins.erase(origins);
    pointerBorrowPlace.erase(variable);
}

bool CodeGenerator::mergeAggregateBorrowStateAfterUpdate(
    const std::string& variable,
    const std::set<std::string>& previousOrigins,
    const std::map<std::string, std::set<std::string>>& previousPlaces,
    const std::vector<std::pair<std::string, std::string>>&
        previousExclusivePlaces,
    const std::vector<std::string>& previousExclusiveKeys,
    const std::string& previousExclusiveOwner,
    const std::string& previousExclusivePlace, int line)
{
    auto currentPlaces = aggregateBorrowPlaces.find(variable);
    for(const auto& previous : previousExclusivePlaces)
    {
        if(currentPlaces == aggregateBorrowPlaces.end())
            break;
        auto currentOwnerPlaces = currentPlaces->second.find(previous.first);
        if(currentOwnerPlaces == currentPlaces->second.end())
            continue;
        for(const auto& currentPlace : currentOwnerPlaces->second)
            if(borrowPlacesOverlap(previous.second, currentPlace))
            {
                reportError(
                    line,
                    "cannot retain overlapping mutable borrows of '" +
                        previous.first + "' in aggregate '" + variable + "'");
                return false;
            }
    }

    if(!previousOrigins.empty())
    {
        auto& origins = aggregateBorrowOrigins[variable];
        origins.insert(previousOrigins.begin(), previousOrigins.end());
        for(const auto& origin : previousOrigins)
            activeBorrowers[origin].insert(variable);
    }
    for(const auto& ownerPlaces : previousPlaces)
    {
        auto& current = aggregateBorrowPlaces[variable][ownerPlaces.first];
        current.insert(ownerPlaces.second.begin(), ownerPlaces.second.end());
    }

    for(const auto& previousKey : previousExclusiveKeys)
    {
        auto existing = activeMutBorrower.find(previousKey);
        if(existing != activeMutBorrower.end() &&
           existing->second == variable)
            continue;
        std::string restoredKey = previousKey;
        size_t suffix = 0;
        while(existing != activeMutBorrower.end())
        {
            restoredKey = variable + std::string(1, '\x1f') +
                          "retained_mut_" + std::to_string(suffix++);
            existing = activeMutBorrower.find(restoredKey);
        }
        activeMutBorrower[restoredKey] = variable;
    }
    if(!previousExclusiveOwner.empty() &&
       pointerBorrowTarget.find(variable) == pointerBorrowTarget.end())
    {
        pointerBorrowTarget[variable] = previousExclusiveOwner;
        if(!previousExclusivePlace.empty())
            pointerBorrowPlace[variable] = previousExclusivePlace;
    }
    return true;
}

void CodeGenerator::trackAggregateBorrowOrigins(
    const std::string& variable, ExpressionNode* expression,
    const std::string& destinationPath)
{
    auto type = variableTypes.find(variable);
    if(type == variableTypes.end() ||
       (type->second != TypeNode::TYPE_STRUCT &&
        type->second != TypeNode::TYPE_LIST &&
        type->second != TypeNode::TYPE_MAP &&
        type->second != TypeNode::TYPE_TUPLE))
        return;
    std::set<std::string> origins;
    std::map<std::string, std::set<std::string>> originPlaces;
    std::map<std::string,
             std::map<std::string, std::set<std::string>>>
        fieldOriginPlaces;
    struct ExclusiveBorrow
    {
        std::string source;
        std::string owner;
        std::string place;
        ExpressionNode* expression = nullptr;
    };
    std::vector<ExclusiveBorrow> exclusiveBorrows;
    auto addOrigin = [&](const std::string& owner, const std::string& place) {
        if(owner.empty())
            return;
        origins.insert(owner);
        originPlaces[owner].insert(place.empty() ? owner : place);
    };
    std::function<void(ExpressionNode*)> collect = [&](ExpressionNode* current) {
        if(!current)
            return;
        if(auto* identifier = dynamic_cast<IdentifierNode*>(current))
        {
            if(isMutBorrower(identifier->name))
            {
                auto places = aggregateBorrowPlaces.find(identifier->name);
                if(places != aggregateBorrowPlaces.end())
                {
                    for(const auto& owner : places->second)
                        for(const auto& place : owner.second)
                            exclusiveBorrows.push_back(
                                {identifier->name, owner.first, place,
                                 nullptr});
                }
                else
                {
                    auto owner = pointerBorrowTarget.find(identifier->name);
                    if(owner != pointerBorrowTarget.end())
                    {
                        auto place = pointerBorrowPlace.find(identifier->name);
                        exclusiveBorrows.push_back(
                            {identifier->name, owner->second,
                             place == pointerBorrowPlace.end()
                                 ? owner->second
                                 : place->second,
                             nullptr});
                    }
                }
            }
            else
            {
                auto pointer = pointerBorrowTarget.find(identifier->name);
                if(pointer != pointerBorrowTarget.end())
                {
                    auto place = pointerBorrowPlace.find(identifier->name);
                    addOrigin(pointer->second,
                              place == pointerBorrowPlace.end()
                                  ? pointer->second
                                  : place->second);
                }
                for(const auto& active : activeBorrowers)
                    if(active.second.count(identifier->name) != 0 &&
                       aggregateBorrowOrigins.find(identifier->name) ==
                           aggregateBorrowOrigins.end())
                    {
                        auto place = pointerBorrowPlace.find(identifier->name);
                        addOrigin(active.first,
                                  pointer != pointerBorrowTarget.end() &&
                                          pointer->second == active.first &&
                                          place != pointerBorrowPlace.end()
                                      ? place->second
                                      : active.first);
                    }
            }
            auto aggregate = aggregateBorrowOrigins.find(identifier->name);
            if(aggregate != aggregateBorrowOrigins.end())
            {
                for(const auto& owner : aggregate->second)
                {
                    auto places = aggregateBorrowPlaces.find(identifier->name);
                    bool hasPrecisePlaces = false;
                    if(places != aggregateBorrowPlaces.end())
                    {
                        auto ownerPlaces = places->second.find(owner);
                        if(ownerPlaces != places->second.end() &&
                           !ownerPlaces->second.empty())
                        {
                            hasPrecisePlaces = true;
                            for(const auto& place : ownerPlaces->second)
                                addOrigin(owner, place);
                        }
                    }
                    if(!hasPrecisePlaces)
                    {
                        auto place = pointerBorrowPlace.find(identifier->name);
                        addOrigin(owner,
                                  aggregate->second.size() == 1 &&
                                          place != pointerBorrowPlace.end()
                                      ? place->second
                                      : owner);
                    }
                }
            }
            return;
        }
        if(auto* unary = dynamic_cast<UnaryOpNode*>(current))
        {
            if(unary->op == UnaryOpNode::OP_ADDR ||
               unary->op == UnaryOpNode::OP_ADDR_MUT)
            {
                const std::string owner =
                    resolveBorrowOwnerFromLValue(unary->operand);
                if(!owner.empty())
                {
                    const std::string place =
                        resolveBorrowPlaceFromLValue(unary->operand);
                    const bool isMutable =
                        unary->op == UnaryOpNode::OP_ADDR_MUT;
                    if(isMutable)
                        return;
                    auto conflicts = [&](const std::string& borrower) {
                        return borrowerPlaceOverlaps(
                            borrower, owner, place.empty() ? owner : place);
                    };
                    for(const auto& mutableBorrow : activeMutBorrower)
                    {
                        if(conflicts(mutableBorrow.second))
                        {
                            reportError(
                                current->line,
                                "cannot borrow '" + owner +
                                    "' as immutable while mutably borrowed by '" +
                                    mutableBorrow.second + "'");
                            break;
                        }
                    }
                    addOrigin(owner, place);
                }
            }
            else
                collect(unary->operand);
            return;
        }
        if(auto* ternary = dynamic_cast<TernaryNode*>(current))
        {
            collect(ternary->trueExpr);
            collect(ternary->falseExpr);
            return;
        }
        if(auto* match = dynamic_cast<MatchExpressionNode*>(current))
        {
            for(auto* arm : match->arms)
                if(arm)
                    collect(arm->expression);
            return;
        }
        if(auto* attempt = dynamic_cast<TryExpressionNode*>(current))
        {
            collect(attempt->expression);
            return;
        }
        if(auto* structure = dynamic_cast<StructLiteralNode*>(current))
            for(const auto& field : structure->fields)
                collect(field.second);
        else if(auto* tuple = dynamic_cast<TupleLiteralNode*>(current))
        {
            if(tuple->elements)
                for(auto* element : tuple->elements->elements)
                    collect(element);
        }
        else if(auto* list = dynamic_cast<ListLiteralNode*>(current))
        {
            if(list->elements)
                for(auto* element : list->elements->elements)
                    collect(element);
        }
        else if(auto* map = dynamic_cast<MapLiteralNode*>(current))
        {
            if(map->entries)
                for(auto* entry : map->entries->entries)
                {
                    collect(entry->key);
                    collect(entry->value);
                }
        }
        else if(auto* array = dynamic_cast<ArrayFillNode*>(current))
            collect(array->value);
        else if(auto* field = dynamic_cast<FieldAccessNode*>(current))
            collect(field->object);
        else if(auto* index = dynamic_cast<IndexExpressionNode*>(current))
        {
            collect(index->base);
            collect(index->index);
        }
        else if(auto* tuple = dynamic_cast<TupleAccessNode*>(current))
            collect(tuple->tuple);
        else if(auto* binary = dynamic_cast<BinaryOpNode*>(current))
        {
            collect(binary->left);
            collect(binary->right);
        }
        else if(auto* cast = dynamic_cast<CastExpressionNode*>(current))
            collect(cast->expression);
        else if(auto* call = dynamic_cast<FunctionCallNode*>(current))
        {
            for(auto* argument : call->arguments)
                collect(argument);
        }
        else if(auto* invoke = dynamic_cast<CallableInvokeNode*>(current))
        {
            collect(invoke->callable);
            for(auto* argument : invoke->arguments)
                collect(argument);
        }
        else if(auto* method = dynamic_cast<MethodCallNode*>(current))
        {
            collect(method->object);
            for(auto* argument : method->arguments)
                collect(argument);
        }
    };
    collect(expression);
    auto joinFieldPath = [](const std::string& parent,
                            const std::string& child) {
        if(parent.empty())
            return child;
        if(child.empty())
            return parent;
        return child.front() == '[' ? parent + child
                                    : parent + "." + child;
    };
    auto mapLiteralKeyPath = [&](ExpressionNode* key) {
        ConstexprValue constant;
        if(evalConstexprExpression(key, constant, nullptr, nullptr, 0))
        {
            if(constant.kind == ConstexprValue::Kind::Int)
                return "[" + std::to_string(constant.intValue) + "]";
            if(constant.kind == ConstexprValue::Kind::Bool)
                return std::string(constant.boolValue ? "[1]" : "[0]");
        }
        return borrowMapKeyPath(key);
    };
    std::function<void(ExpressionNode*, const std::string&)> collectFields =
        [&](ExpressionNode* current, const std::string& destinationPath) {
        if(!current)
            return;
        auto addFieldOrigin = [&](const std::string& owner,
                                  const std::string& place) {
            if(!owner.empty())
                fieldOriginPlaces[destinationPath][owner].insert(
                    place.empty() ? owner : place);
        };
        if(auto* identifier = dynamic_cast<IdentifierNode*>(current))
        {
            if(!isMutBorrower(identifier->name))
            {
                auto pointer = pointerBorrowTarget.find(identifier->name);
                if(pointer != pointerBorrowTarget.end())
                {
                    auto place = pointerBorrowPlace.find(identifier->name);
                    addFieldOrigin(pointer->second,
                                   place == pointerBorrowPlace.end()
                                       ? pointer->second
                                       : place->second);
                }
            }
            auto aggregate = aggregateBorrowFieldPlaces.find(identifier->name);
            if(aggregate != aggregateBorrowFieldPlaces.end())
                for(const auto& field : aggregate->second)
                    for(const auto& owner : field.second)
                        for(const auto& place : owner.second)
                            fieldOriginPlaces[joinFieldPath(
                                destinationPath, field.first)][owner.first]
                                .insert(place);
            return;
        }
        if(auto* unary = dynamic_cast<UnaryOpNode*>(current))
        {
            if(unary->op == UnaryOpNode::OP_ADDR_MUT)
            {
                const auto owner =
                    resolveBorrowOwnerFromLValue(unary->operand);
                const auto place =
                    resolveBorrowPlaceFromLValue(unary->operand);
                if(!owner.empty())
                    exclusiveBorrows.push_back(
                        {"", owner, place.empty() ? owner : place, unary});
            }
            else if(unary->op == UnaryOpNode::OP_ADDR)
            {
                const auto owner =
                    resolveBorrowOwnerFromLValue(unary->operand);
                addFieldOrigin(owner,
                               resolveBorrowPlaceFromLValue(unary->operand));
            }
            else
                collectFields(unary->operand, destinationPath);
            return;
        }
        if(auto* ternary = dynamic_cast<TernaryNode*>(current))
        {
            collectFields(ternary->trueExpr, destinationPath);
            collectFields(ternary->falseExpr, destinationPath);
            return;
        }
        if(auto* match = dynamic_cast<MatchExpressionNode*>(current))
        {
            for(auto* arm : match->arms)
                if(arm)
                    collectFields(arm->expression, destinationPath);
            return;
        }
        if(auto* attempt = dynamic_cast<TryExpressionNode*>(current))
        {
            collectFields(attempt->expression, destinationPath);
            return;
        }
        if(auto* structure = dynamic_cast<StructLiteralNode*>(current))
        {
            for(const auto& field : structure->fields)
                collectFields(field.second,
                              joinFieldPath(destinationPath, field.first));
            return;
        }
        if(auto* tuple = dynamic_cast<TupleLiteralNode*>(current))
        {
            if(tuple->elements)
                for(size_t i = 0; i < tuple->elements->elements.size(); ++i)
                    collectFields(tuple->elements->elements[i],
                                  joinFieldPath(destinationPath,
                                                std::to_string(i)));
            return;
        }
        if(auto* list = dynamic_cast<ListLiteralNode*>(current))
        {
            if(list->elements)
                for(size_t i = 0; i < list->elements->elements.size(); ++i)
                    collectFields(
                        list->elements->elements[i],
                        joinFieldPath(destinationPath,
                                      "[" + std::to_string(i) + "]"));
            return;
        }
        if(auto* map = dynamic_cast<MapLiteralNode*>(current))
        {
            if(map->entries)
                for(auto* entry : map->entries->entries)
                {
                    if(!entry)
                        continue;
                    // Map indexing uses its key as the place path, unlike
                    // sequence indexing which uses element position. Literal
                    // keys retain precise provenance; unknown keys overlap
                    // every map entry conservatively.
                    const std::string entryPath = joinFieldPath(
                        destinationPath, mapLiteralKeyPath(entry->key));
                    collectFields(entry->key,
                                  joinFieldPath(entryPath, "key"));
                    collectFields(entry->value, entryPath);
                }
            return;
        }
        if(auto* field = dynamic_cast<FieldAccessNode*>(current))
        {
            const auto owner = resolveBorrowOwnerFromLValue(field);
            const auto sourcePath = resolveBorrowPlaceFromLValue(field);
            std::string relativePath;
            if(!owner.empty() && sourcePath.compare(0, owner.size(), owner) == 0)
            {
                relativePath = sourcePath.substr(owner.size());
                if(!relativePath.empty() && relativePath.front() == '.')
                    relativePath.erase(0, 1);
            }
            auto aggregate = aggregateBorrowFieldPlaces.find(owner);
            if(aggregate != aggregateBorrowFieldPlaces.end())
            {
                auto source = aggregate->second.find(relativePath);
                if(source != aggregate->second.end())
                {
                    for(const auto& sourceOwner : source->second)
                        for(const auto& place : sourceOwner.second)
                            addFieldOrigin(sourceOwner.first, place);
                    return;
                }
            }
            collectFields(field->object, destinationPath);
            return;
        }
        if(auto* tuple = dynamic_cast<TupleAccessNode*>(current))
        {
            const auto owner = resolveBorrowOwnerFromLValue(tuple);
            const auto sourcePath = resolveBorrowPlaceFromLValue(tuple);
            std::string relativePath;
            if(!owner.empty() && sourcePath.compare(0, owner.size(), owner) == 0)
            {
                relativePath = sourcePath.substr(owner.size());
                if(!relativePath.empty() && relativePath.front() == '.')
                    relativePath.erase(0, 1);
            }
            auto aggregate = aggregateBorrowFieldPlaces.find(owner);
            if(aggregate != aggregateBorrowFieldPlaces.end())
            {
                auto source = aggregate->second.find(relativePath);
                if(source != aggregate->second.end())
                {
                    for(const auto& sourceOwner : source->second)
                        for(const auto& place : sourceOwner.second)
                            addFieldOrigin(sourceOwner.first, place);
                    return;
                }
            }
            collectFields(tuple->tuple, destinationPath);
            return;
        }
        if(auto* index = dynamic_cast<IndexExpressionNode*>(current))
        {
            collectFields(index->base, destinationPath);
            return;
        }
        if(auto* array = dynamic_cast<ArrayFillNode*>(current))
        {
            collectFields(array->value,
                          joinFieldPath(destinationPath, "[*]"));
            return;
        }
        if(auto* binary = dynamic_cast<BinaryOpNode*>(current))
        {
            collectFields(binary->left, destinationPath);
            collectFields(binary->right, destinationPath);
            return;
        }
        if(auto* cast = dynamic_cast<CastExpressionNode*>(current))
        {
            collectFields(cast->expression, destinationPath);
            return;
        }
        if(auto* call = dynamic_cast<FunctionCallNode*>(current))
            for(auto* argument : call->arguments)
                collectFields(argument, destinationPath);
        else if(auto* invoke = dynamic_cast<CallableInvokeNode*>(current))
        {
            collectFields(invoke->callable, destinationPath);
            for(auto* argument : invoke->arguments)
                collectFields(argument, destinationPath);
        }
        else if(auto* method = dynamic_cast<MethodCallNode*>(current))
        {
            collectFields(method->object, destinationPath);
            for(auto* argument : method->arguments)
                collectFields(argument, destinationPath);
        }
    };
    collectFields(expression, destinationPath);
    for(const auto& borrow : exclusiveBorrows)
    {
        auto aggregateDepth = variableScopeDepth.find(variable);
        auto ownerDepth = variableScopeDepth.find(borrow.owner);
        const bool aggregateOutlivesOwner =
            ownerDepth != variableScopeDepth.end() &&
            (globalNamedValues.count(variable) != 0 ||
             (aggregateDepth != variableScopeDepth.end() &&
              aggregateDepth->second < ownerDepth->second));
        if(aggregateOutlivesOwner)
        {
            reportError(expression ? expression->line : 0,
                        "cannot store mutable borrow of '" + borrow.owner +
                            "' in longer-lived aggregate '" + variable +
                            "'");
            return;
        }
    }
    clearAggregateBorrowOrigins(variable);
    for(auto it = origins.begin(); it != origins.end();)
        if(variableScopeDepth.find(*it) == variableScopeDepth.end())
        {
            originPlaces.erase(*it);
            it = origins.erase(it);
        }
        else
            ++it;
    if(origins.empty() && exclusiveBorrows.empty())
        return;
    if(!origins.empty())
    {
        aggregateBorrowOrigins[variable] = origins;
        auto& trackedPlaces = aggregateBorrowPlaces[variable];
        for(const auto& owner : origins)
        {
            auto places = originPlaces.find(owner);
            if(places != originPlaces.end())
                trackedPlaces[owner] = places->second;
        }
        for(auto field = fieldOriginPlaces.begin();
            field != fieldOriginPlaces.end();)
        {
            for(auto owner = field->second.begin();
                owner != field->second.end();)
                if(origins.count(owner->first) == 0)
                    owner = field->second.erase(owner);
                else
                    ++owner;
            if(field->second.empty())
                field = fieldOriginPlaces.erase(field);
            else
                ++field;
        }
        aggregateBorrowFieldPlaces[variable] =
            std::move(fieldOriginPlaces);
        for(const auto& owner : origins)
            activeBorrowers[owner].insert(variable);
        if(origins.size() == 1 && originPlaces.begin()->second.size() == 1 &&
           !isMutBorrower(variable))
            pointerBorrowPlace[variable] =
                *originPlaces.begin()->second.begin();
    }

    if(!exclusiveBorrows.empty())
    {
        size_t borrowIndex = 0;
        for(const auto& borrow : exclusiveBorrows)
        {
            if(borrow.source.empty())
            {
                // Register each inline &mut with its own temporary identity
                // so overlap checks can distinguish disjoint field places.
                const std::string temporaryBorrower =
                    variable + std::string(1, '\x1f') + "aggregate_mut_" +
                    std::to_string(borrowIndex++);
                registerPointerBorrow(
                    temporaryBorrower, borrow.expression,
                    borrow.expression ? borrow.expression->line : 0, true);
                auto registeredOwner =
                    pointerBorrowTarget.find(temporaryBorrower);
                if(registeredOwner == pointerBorrowTarget.end())
                    continue;
                auto registeredPlace =
                    pointerBorrowPlace.find(temporaryBorrower);
                const std::string place =
                    registeredPlace == pointerBorrowPlace.end()
                        ? registeredOwner->second
                        : registeredPlace->second;
                for(auto& active : activeMutBorrower)
                    if(active.second == temporaryBorrower)
                        active.second = variable;
                aggregateBorrowPlaces[variable][registeredOwner->second]
                    .insert(place);
                pointerBorrowTarget.erase(temporaryBorrower);
                pointerBorrowPlace.erase(temporaryBorrower);
                continue;
            }

            // Transfer existing active entries rather than releasing and
            // reacquiring them; this preserves all disjoint places when an
            // exclusive pointer is moved into an aggregate.
            for(auto& active : activeMutBorrower)
                if(active.second == borrow.source)
                    active.second = variable;
            movedVariables.insert(borrow.source);
            pointerBorrowTarget.erase(borrow.source);
            pointerBorrowPlace.erase(borrow.source);
            aggregateBorrowPlaces[variable][borrow.owner].insert(borrow.place);
            if(pointerBorrowTarget.find(variable) == pointerBorrowTarget.end())
            {
                pointerBorrowTarget[variable] = borrow.owner;
                pointerBorrowPlace[variable] = borrow.place;
            }
        }
    }
}

void CodeGenerator::registerPointerBorrow(const std::string& pointerVar,
                                          ExpressionNode* expr, int line,
                                          bool isMutable)
{
    std::vector<ExpressionNode*> alternatives;
    bool hasAlternativeWrapper = false;
    std::function<void(ExpressionNode*)> collectAlternatives =
        [&](ExpressionNode* expression) {
            if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
            {
                hasAlternativeWrapper = true;
                collectAlternatives(ternary->trueExpr);
                collectAlternatives(ternary->falseExpr);
                return;
            }
            if(auto* match = dynamic_cast<MatchExpressionNode*>(expression))
            {
                hasAlternativeWrapper = true;
                for(auto* arm : match->arms)
                    if(arm)
                        collectAlternatives(arm->expression);
                return;
            }
            if(auto* cast = dynamic_cast<CastExpressionNode*>(expression))
            {
                hasAlternativeWrapper = true;
                collectAlternatives(cast->expression);
                return;
            }
            if(auto* attempt = dynamic_cast<TryExpressionNode*>(expression))
            {
                hasAlternativeWrapper = true;
                collectAlternatives(attempt->expression);
                return;
            }
            if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
                if(binary->op == BinaryOpNode::OP_COMMA)
                {
                    hasAlternativeWrapper = true;
                    collectAlternatives(binary->right);
                    return;
                }
            alternatives.push_back(expression);
        };
    collectAlternatives(expr);
    if(hasAlternativeWrapper && !alternatives.empty())
    {
        const auto baseTargets = pointerBorrowTarget;
        const auto basePlaces = pointerBorrowPlace;
        const auto baseParents = pointerBorrowParent;
        const auto baseShared = activeBorrowers;
        const auto baseMutable = activeMutBorrower;
        std::vector<std::map<std::string, std::string>> branchTargets;
        std::vector<std::map<std::string, std::string>> branchPlaces;
        std::vector<std::map<std::string, std::string>> branchParents;
        std::vector<std::map<std::string, std::set<std::string>>>
            branchShared;
        std::vector<std::map<std::string, std::string>> branchMutable;

        for(auto* alternative : alternatives)
        {
            pointerBorrowTarget = baseTargets;
            pointerBorrowPlace = basePlaces;
            pointerBorrowParent = baseParents;
            activeBorrowers = baseShared;
            activeMutBorrower = baseMutable;

            bool alternativeIsMutable = isMutable;
            if(auto* address = dynamic_cast<UnaryOpNode*>(alternative))
                alternativeIsMutable =
                    address->op == UnaryOpNode::OP_ADDR_MUT;
            else if(auto* identifier =
                        dynamic_cast<IdentifierNode*>(alternative))
                alternativeIsMutable = isMutBorrower(identifier->name);
            registerPointerBorrow(pointerVar, alternative, line,
                                  alternativeIsMutable);
            branchTargets.push_back(pointerBorrowTarget);
            branchPlaces.push_back(pointerBorrowPlace);
            branchParents.push_back(pointerBorrowParent);
            branchShared.push_back(activeBorrowers);
            branchMutable.push_back(activeMutBorrower);
        }

        pointerBorrowTarget = baseTargets;
        pointerBorrowTarget.erase(pointerVar);
        pointerBorrowPlace = basePlaces;
        pointerBorrowPlace.erase(pointerVar);
        pointerBorrowParent = baseParents;
        pointerBorrowParent.erase(pointerVar);
        activeBorrowers = baseShared;
        activeMutBorrower = baseMutable;
        for(auto it = activeBorrowers.begin(); it != activeBorrowers.end();)
        {
            it->second.erase(pointerVar);
            if(it->second.empty())
                it = activeBorrowers.erase(it);
            else
                ++it;
        }
        for(auto it = activeMutBorrower.begin(); it != activeMutBorrower.end();)
            if(it->second == pointerVar)
                it = activeMutBorrower.erase(it);
            else
                ++it;

        bool commonPlace = true;
        bool commonParent = true;
        std::string placeValue;
        std::string parentValue;
        bool targetAssigned = false;
        for(size_t branch = 0; branch < alternatives.size(); ++branch)
        {
            auto target = branchTargets[branch].find(pointerVar);
            if(target != branchTargets[branch].end() && !targetAssigned)
            {
                pointerBorrowTarget.emplace(pointerVar, target->second);
                targetAssigned = true;
            }

            auto place = branchPlaces[branch].find(pointerVar);
            if(place == branchPlaces[branch].end())
                commonPlace = false;
            else if(placeValue.empty())
                placeValue = place->second;
            else if(placeValue != place->second)
                commonPlace = false;

            auto parent = branchParents[branch].find(pointerVar);
            if(parent == branchParents[branch].end())
                commonParent = false;
            else if(parentValue.empty())
                parentValue = parent->second;
            else if(parentValue != parent->second)
                commonParent = false;

            for(const auto& owner : branchShared[branch])
                if(owner.second.count(pointerVar))
                    activeBorrowers[owner.first].insert(pointerVar);
            for(const auto& borrow : branchMutable[branch])
                if(borrow.second == pointerVar)
                    activeMutBorrower.emplace(borrow.first, pointerVar);
        }
        if(commonPlace && !placeValue.empty())
            pointerBorrowPlace[pointerVar] = placeValue;
        if(commonParent && !parentValue.empty())
            pointerBorrowParent[pointerVar] = parentValue;
        return;
    }

    if(!dynamic_cast<IdentifierNode*>(expr))
    {
        const std::string container = resolveBorrowOwnerFromLValue(expr);
        if(!container.empty() && isMutBorrower(container))
        {
            reportError(line,
                        "cannot extract a mutable borrow from aggregate '" +
                            container + "' because that would duplicate its "
                            "exclusive reference");
            return;
        }
    }
    clearPointerBorrow(pointerVar);
    std::string borrowPlace;
    std::string exclusiveReborrowParent;
    if(isMutable)
    {
        if(auto* address = dynamic_cast<UnaryOpNode*>(expr))
        {
            auto* dereference = dynamic_cast<UnaryOpNode*>(address->operand);
            if(dereference && dereference->op == UnaryOpNode::OP_DEREF)
            {
                if(auto* parent =
                       dynamic_cast<IdentifierNode*>(dereference->operand))
                    if(isMutBorrower(parent->name))
                        exclusiveReborrowParent = parent->name;
            }
        }
    }

    auto registerOwnerBorrow = [&](const std::string& ownerName,
                                   bool enforceExclusive) -> bool
    {
        if(globalNamedValues.find(ownerName) == globalNamedValues.end())
        {
            const std::string requestedPlace =
                borrowPlace.empty() ? ownerName : borrowPlace;
            for(const auto& movedPlace : movedVariables)
                if(borrowPlacesOverlap(requestedPlace, movedPlace))
                {
                    reportError(line,
                                "cannot borrow moved value: '" + movedPlace +
                                    "'");
                    return false;
                }
        }

        auto pointerDepthIt = variableScopeDepth.find(pointerVar);
        auto ownerDepthIt = variableScopeDepth.find(ownerName);
        if(pointerDepthIt != variableScopeDepth.end() &&
           ownerDepthIt != variableScopeDepth.end() &&
           ownerDepthIt->second > pointerDepthIt->second)
        {
            reportError(line, "cannot borrow '" + ownerName +
                                  "' into longer-lived pointer '" + pointerVar +
                                  "'");
            return false;
        }

        if(isMutable)
        {
            // &mut: reject overlapping shared borrowers. The root owner is
            // still used for lifetime bookkeeping, but sibling fields may be
            // borrowed independently.
            auto sharedIt = activeBorrowers.find(ownerName);
            if(sharedIt != activeBorrowers.end())
            {
                for(const auto& borrower : sharedIt->second)
                {
                    if(borrowerPlaceOverlaps(borrower, ownerName, borrowPlace))
                    {
                        reportError(
                            line, "cannot borrow '" + ownerName +
                                      "' as mutable because it is already "
                                      "borrowed");
                        return false;
                    }
                }
            }
            bool hasRootMutableBorrow = false;
            for(const auto& mutBorrow : activeMutBorrower)
            {
                if(mutBorrow.second == pointerVar ||
                   !borrowerPlaceOverlaps(mutBorrow.second, ownerName,
                                          ownerName))
                    continue;
                hasRootMutableBorrow = true;
                const bool isReborrowAncestor =
                    mutBorrow.second == exclusiveReborrowParent ||
                    isPointerBorrowAncestor(mutBorrow.second,
                                            exclusiveReborrowParent);
                if(!isReborrowAncestor &&
                   borrowerPlaceOverlaps(mutBorrow.second, ownerName,
                                         borrowPlace))
                {
                    reportError(line, "cannot borrow '" + ownerName +
                                          "' as mutable more than once at "
                                          "a time");
                    return false;
                }
            }
            // Owner must be var (mutable)
            if(constantVariables.count(ownerName))
            {
                reportError(line, "cannot borrow immutable variable '" +
                                      ownerName + "' as mutable");
                return false;
            }
            pointerBorrowTarget[pointerVar] = ownerName;
            pointerBorrowPlace[pointerVar] = borrowPlace;
            pointerKnownNull[pointerVar] = false;
            const std::string borrowKey =
                !hasRootMutableBorrow
                    ? ownerName
                    : (!exclusiveReborrowParent.empty()
                           ? "@reborrow:" + pointerVar
                           : borrowPlace);
            activeMutBorrower[borrowKey] = pointerVar;
            return true;
        }

        // Shared borrow: reject only an overlapping mutable borrow.
        for(const auto& mutBorrow : activeMutBorrower)
        {
            if(borrowerPlaceOverlaps(mutBorrow.second, ownerName, borrowPlace))
            {
                reportError(line,
                            "cannot borrow '" + ownerName +
                                "' as immutable because it is also borrowed "
                                "as mutable by '" + mutBorrow.second + "'");
                return false;
            }
        }

        auto activeIt = activeBorrowers.find(ownerName);
        if(enforceExclusive && activeIt != activeBorrowers.end())
        {
            for(const auto& borrower : activeIt->second)
            {
                if(borrower != pointerVar &&
                   borrowerPlaceOverlaps(borrower, ownerName, borrowPlace))
                {
                    reportError(line, "cannot borrow '" + ownerName +
                                          "' while already borrowed by '" +
                                          borrower + "'");
                    return false;
                }
            }
        }

        pointerBorrowTarget[pointerVar] = ownerName;
        pointerBorrowPlace[pointerVar] = borrowPlace;
        pointerKnownNull[pointerVar] = false;
        activeBorrowers[ownerName].insert(pointerVar);
        return true;
    };

    // Preserve pointer provenance when a local closure returns a borrowed
    // value and its caller stores that result in another local pointer.
    std::string returnedClosureName;
    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
        returnedClosureName = call->name;
    else if(auto* invoke = dynamic_cast<CallableInvokeNode*>(expr))
        if(auto* callable = dynamic_cast<IdentifierNode*>(invoke->callable))
            returnedClosureName = callable->name;
    auto runtimeClosure = runtimeClosureVariables.find(returnedClosureName);
    auto knownFunctionItem =
        functionItemVariables.find(returnedClosureName);
    const bool hasKnownFunctionItemTargets =
        knownFunctionItem != functionItemVariables.end() &&
        !knownFunctionItem->second.empty();
    if(runtimeClosure != runtimeClosureVariables.end() &&
       runtimeClosure->second && !hasKnownFunctionItemTargets)
    {
        std::vector<ExpressionNode*> returnedExpressions;
        std::function<void(StatementNode*)> collectStatementReturns;
        std::function<void(StatementListNode*)> collectListReturns;
        collectListReturns = [&](StatementListNode* statements) {
            if(!statements)
                return;
            for(auto* statement : statements->statements)
                collectStatementReturns(statement);
        };
        collectStatementReturns = [&](StatementNode* statement) {
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
                returnedExpressions.push_back(returned->expression);
            else if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                collectListReturns(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                collectListReturns(branch->thenBranch);
                collectStatementReturns(branch->elseIfBranch);
                collectListReturns(branch->elseBranch);
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                collectListReturns(branch->thenBranch);
                collectStatementReturns(branch->elseIfBranch);
                collectListReturns(branch->elseBranch);
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
                collectListReturns(loop->body);
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
                collectListReturns(loop->body);
        };
        collectListReturns(runtimeClosure->second->body);
        if(!returnedExpressions.empty())
        {
            registerPointerBorrowAlternatives(pointerVar,
                                              returnedExpressions, line,
                                              isMutable, false);
            return;
        }
    }

    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
    {
        std::set<size_t> returnedParameterIndices;
        bool hasUnknownPointerReturn = false;
        std::vector<ExpressionNode*> directBorrowReturns;
        std::map<std::string, std::vector<ExpressionNode*>> localBorrowAliases;
        std::map<std::string, TypeNode*> localBorrowTypes;
        bool inConditionalRegion = false;
        auto eraseLocalBorrowAliasSubtree = [&](const std::string& path) {
            for(auto alias = localBorrowAliases.begin();
                alias != localBorrowAliases.end();)
            {
                const bool descendant =
                    alias->first.size() > path.size() &&
                    alias->first.compare(0, path.size(), path) == 0 &&
                    (alias->first[path.size()] == '.' ||
                     alias->first[path.size()] == '[');
                if(alias->first == path || descendant)
                    alias = localBorrowAliases.erase(alias);
                else
                    ++alias;
            }
        };
        auto recordLocalBorrowAlias = [&](const std::string& path,
                                          std::vector<ExpressionNode*> origins) {
            if(path.empty())
                return;
            if(inConditionalRegion)
            {
                if(!origins.empty())
                {
                    auto& existing = localBorrowAliases[path];
                    existing.insert(existing.end(), origins.begin(),
                                    origins.end());
                }
            }
            else if(origins.empty())
                localBorrowAliases.erase(path);
            else
                localBorrowAliases[path] = std::move(origins);
        };
        std::function<std::string(ExpressionNode*)> localBorrowPlacePath;
        localBorrowPlacePath = [&](ExpressionNode* value) -> std::string {
            if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
                return identifier->name;
            if(auto* field = dynamic_cast<FieldAccessNode*>(value))
            {
                const std::string base = field->object
                                             ? localBorrowPlacePath(field->object)
                                             : field->structName;
                return base.empty() ? std::string{}
                                    : base + "." + field->fieldName;
            }
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(value))
            {
                const std::string base = localBorrowPlacePath(tuple->tuple);
                return base.empty() ? std::string{}
                                    : base + "." + std::to_string(tuple->index);
            }
            if(auto* index = dynamic_cast<IndexExpressionNode*>(value))
            {
                const std::string base = localBorrowPlacePath(index->base);
                return base.empty() ? std::string{}
                                    : base + borrowMapKeyPath(index->index);
            }
            return {};
        };
        auto localBorrowDereferencePaths = [&](ExpressionNode* pointer) {
            std::set<std::string> targets;
            if(auto* address = dynamic_cast<UnaryOpNode*>(pointer))
                if(address->op == UnaryOpNode::OP_ADDR ||
                   address->op == UnaryOpNode::OP_ADDR_MUT)
                {
                    const std::string target =
                        localBorrowPlacePath(address->operand);
                    if(!target.empty())
                        targets.insert(target);
                    return targets;
                }
            auto* identifier = dynamic_cast<IdentifierNode*>(pointer);
            if(!identifier)
                return targets;
            auto aliases = localBorrowAliases.find(identifier->name);
            if(aliases == localBorrowAliases.end())
                return targets;
            for(auto* origin : aliases->second)
            {
                auto* originAddress = dynamic_cast<UnaryOpNode*>(origin);
                if(!originAddress ||
                   (originAddress->op != UnaryOpNode::OP_ADDR &&
                    originAddress->op != UnaryOpNode::OP_ADDR_MUT))
                    continue;
                const std::string candidate =
                    localBorrowPlacePath(originAddress->operand);
                if(candidate.empty())
                    continue;
                targets.insert(candidate);
            }
            return targets;
        };
        std::function<void(ExpressionNode*, std::vector<ExpressionNode*>&)>
            collectBorrowOrigins;
        std::function<void(ExpressionNode*, const std::string&)>
            collectAggregateBorrowFields;
        collectBorrowOrigins = [&](ExpressionNode* value,
                                   std::vector<ExpressionNode*>& origins) {
            if(auto* address = dynamic_cast<UnaryOpNode*>(value))
            {
                if(address->op == UnaryOpNode::OP_ADDR ||
                   address->op == UnaryOpNode::OP_ADDR_MUT)
                    origins.push_back(value);
                return;
            }
            if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
            {
                auto aliases = localBorrowAliases.find(identifier->name);
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                return;
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(value))
            {
                auto aliases = localBorrowAliases.find(
                    localBorrowPlacePath(field));
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                return;
            }
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(value))
            {
                auto aliases = localBorrowAliases.find(
                    localBorrowPlacePath(tuple));
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                return;
            }
            if(auto* index = dynamic_cast<IndexExpressionNode*>(value))
            {
                auto aliases = localBorrowAliases.find(
                    localBorrowPlacePath(index));
                if(aliases != localBorrowAliases.end())
                    origins.insert(origins.end(), aliases->second.begin(),
                                   aliases->second.end());
                return;
            }
            if(auto* ternary = dynamic_cast<TernaryNode*>(value))
            {
                collectBorrowOrigins(ternary->trueExpr, origins);
                collectBorrowOrigins(ternary->falseExpr, origins);
            }
            else if(auto* match = dynamic_cast<MatchExpressionNode*>(value))
            {
                for(auto* arm : match->arms)
                    if(arm)
                        collectBorrowOrigins(arm->expression, origins);
            }
            else if(auto* cast = dynamic_cast<CastExpressionNode*>(value))
                collectBorrowOrigins(cast->expression, origins);
            else if(auto* attempt = dynamic_cast<TryExpressionNode*>(value))
                collectBorrowOrigins(attempt->expression, origins);
            else if(auto* comma = dynamic_cast<BinaryOpNode*>(value))
                if(comma->op == BinaryOpNode::OP_COMMA)
                    collectBorrowOrigins(comma->right, origins);
        };
        collectAggregateBorrowFields = [&](ExpressionNode* value,
                                           const std::string& path) {
            if(auto* structure = dynamic_cast<StructLiteralNode*>(value))
            {
                for(const auto& member : structure->fields)
                {
                    const std::string fieldPath =
                        path + "." + member.first;
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(member.second, origins);
                    recordLocalBorrowAlias(fieldPath, std::move(origins));
                    collectAggregateBorrowFields(member.second, fieldPath);
                }
            }
            else if(auto* tuple = dynamic_cast<TupleLiteralNode*>(value))
            {
                if(!tuple->elements)
                    return;
                for(size_t index = 0;
                    index < tuple->elements->elements.size(); ++index)
                {
                    const std::string elementPath =
                        path + "." + std::to_string(index);
                    auto* element = tuple->elements->elements[index];
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(element, origins);
                    recordLocalBorrowAlias(elementPath, std::move(origins));
                    collectAggregateBorrowFields(element, elementPath);
                }
            }
            else if(auto* list = dynamic_cast<ListLiteralNode*>(value))
            {
                if(!list->elements)
                    return;
                for(size_t index = 0;
                    index < list->elements->elements.size(); ++index)
                {
                    const std::string elementPath =
                        path + "[" + std::to_string(index) + "]";
                    auto* element = list->elements->elements[index];
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(element, origins);
                    recordLocalBorrowAlias(elementPath, std::move(origins));
                    collectAggregateBorrowFields(element, elementPath);
                }
            }
            else if(auto* map = dynamic_cast<MapLiteralNode*>(value))
            {
                if(!map->entries)
                    return;
                for(auto* entry : map->entries->entries)
                {
                    if(!entry)
                        continue;
                    const std::string valuePath =
                        path + borrowMapKeyPath(entry->key);
                    std::vector<ExpressionNode*> origins;
                    collectBorrowOrigins(entry->value, origins);
                    recordLocalBorrowAlias(valuePath, std::move(origins));
                    collectAggregateBorrowFields(entry->value, valuePath);
                }
            }
        };
        auto functionDefinitelyWritesPointerSlot =
            [&](FunctionDefNode* function, size_t parameterIndex) {
                if(!function)
                    return false;
                auto overloads = functionOverloads.find(function->name);
                if(overloads == functionOverloads.end())
                    return false;
                for(const auto& overload : overloads->second)
                    if(overload.node == function)
                    {
                        auto summary =
                            functionPointerSlotDefiniteMutationParameters.find(
                                overload.symbolName);
                        return summary !=
                                   functionPointerSlotDefiniteMutationParameters.end() &&
                               summary->second.count(parameterIndex) != 0;
                    }
                return false;
            };
        std::function<void(StatementNode*)> collectReturns;
        std::function<void(StatementListNode*)> collectReturnList;
        collectReturnList = [&](StatementListNode* statements) {
            if(!statements)
                return;
            for(auto* statement : statements->statements)
                collectReturns(statement);
        };
        collectReturns = [&](StatementNode* statement) {
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
            {
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(returned->expression, origins);
                directBorrowReturns.insert(directBorrowReturns.end(),
                                          origins.begin(), origins.end());
            }
            else if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                if(declaration->type)
                    localBorrowTypes[declaration->name] = declaration->type;
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(declaration->expression, origins);
                if(!origins.empty())
                    localBorrowAliases[declaration->name] =
                        std::move(origins);
                collectAggregateBorrowFields(declaration->expression,
                                             declaration->name);
            }
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            {
                if(declaration->type)
                    localBorrowTypes[declaration->name] = declaration->type;
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(declaration->initExpr, origins);
                if(!origins.empty())
                    localBorrowAliases[declaration->name] =
                        std::move(origins);
                collectAggregateBorrowFields(declaration->initExpr,
                                             declaration->name);
            }
            else if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
            {
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(assignment->expression, origins);
                if(!inConditionalRegion)
                    eraseLocalBorrowAliasSubtree(assignment->name);
                recordLocalBorrowAlias(assignment->name, std::move(origins));
                collectAggregateBorrowFields(assignment->expression,
                                             assignment->name);
            }
            else if(auto* assignment =
                        dynamic_cast<FieldAssignmentNode*>(statement))
            {
                const std::string path =
                    assignment->target
                        ? localBorrowPlacePath(assignment->target)
                        : assignment->structName + "." + assignment->fieldName;
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(assignment->expression, origins);
                if(!path.empty())
                {
                    if(!inConditionalRegion)
                        eraseLocalBorrowAliasSubtree(path);
                    recordLocalBorrowAlias(path, std::move(origins));
                    collectAggregateBorrowFields(assignment->expression,
                                                 path);
                }
            }
            else if(auto* assignment =
                        dynamic_cast<DerefAssignmentNode*>(statement))
            {
                const auto paths =
                    localBorrowDereferencePaths(assignment->pointerExpr);
                std::vector<ExpressionNode*> origins;
                collectBorrowOrigins(assignment->value, origins);
                for(const auto& path : paths)
                {
                    if(!inConditionalRegion)
                        eraseLocalBorrowAliasSubtree(path);
                    recordLocalBorrowAlias(path, origins);
                    collectAggregateBorrowFields(assignment->value, path);
                }
            }
            else if(auto* expressionStatement =
                        dynamic_cast<ExpressionStatementNode*>(statement))
            {
                auto* call = dynamic_cast<FunctionCallNode*>(
                    expressionStatement->expression);
                auto* method = dynamic_cast<MethodCallNode*>(
                    expressionStatement->expression);
                if(!call && !method)
                    return;
                const std::vector<ExpressionNode*>& arguments =
                    call ? call->arguments : method->arguments;
                std::set<size_t> mutatedSlots;
                std::set<size_t> definiteSlots;
                if(call)
                {
                    bool sawOverload = false;
                    for(const auto& calledName :
                        getFunctionItemTargetsForName(call->name))
                    {
                        auto overloads = functionOverloads.find(calledName);
                        if(overloads == functionOverloads.end())
                            continue;
                        for(const auto& overload : overloads->second)
                        {
                            if(!overload.node)
                                continue;
                            sawOverload = true;
                            std::set<size_t> overloadMutated;
                            auto summary =
                                functionPointerSlotMutationParameters.find(
                                    overload.symbolName);
                            if(summary !=
                               functionPointerSlotMutationParameters.end())
                                overloadMutated = summary->second;
                            mutatedSlots.insert(overloadMutated.begin(),
                                                overloadMutated.end());
                            for(size_t parameterIndex : overloadMutated)
                                if(functionDefinitelyWritesPointerSlot(
                                       overload.node, parameterIndex))
                                    definiteSlots.insert(parameterIndex);
                        }
                    }
                    // An index is definite only if every possible overload
                    // writes it unconditionally.
                    if(sawOverload)
                    {
                        for(size_t slot : mutatedSlots)
                        {
                            bool writtenByEveryCandidate = true;
                            for(const auto& calledName :
                                getFunctionItemTargetsForName(call->name))
                            {
                                auto overloads =
                                    functionOverloads.find(calledName);
                                if(overloads == functionOverloads.end())
                                    continue;
                                for(const auto& overload : overloads->second)
                                {
                                    if(!overload.node)
                                        continue;
                                    auto summary =
                                        functionPointerSlotMutationParameters.find(
                                            overload.symbolName);
                                    if(summary ==
                                           functionPointerSlotMutationParameters.end() ||
                                       summary->second.count(slot) == 0 ||
                                       !functionDefinitelyWritesPointerSlot(
                                           overload.node, slot))
                                        writtenByEveryCandidate = false;
                                }
                            }
                            if(!writtenByEveryCandidate)
                                definiteSlots.erase(slot);
                        }
                    }
                }
                else
                {
                    TypeNode* receiverType = nullptr;
                    if(auto* receiver =
                           dynamic_cast<IdentifierNode*>(method->object))
                    {
                        auto localType = localBorrowTypes.find(receiver->name);
                        if(localType != localBorrowTypes.end())
                            receiverType = localType->second;
                    }
                    std::string receiverStruct;
                    if(auto* structure =
                           dynamic_cast<StructTypeRefNode*>(receiverType))
                        receiverStruct = structure->structName;
                    else if(auto* generic =
                                dynamic_cast<GenericStructTypeRefNode*>(
                                    receiverType))
                        receiverStruct = generic->getMangledName();
                    auto methods = structMethods.find(receiverStruct);
                    StructMethodNode* definition = nullptr;
                    if(methods != structMethods.end())
                    {
                        auto found =
                            methods->second.find(method->methodName);
                        if(found != methods->second.end())
                            definition = found->second.second;
                    }
                    if(!definition)
                    {
                        // When the receiver's local type is unavailable in
                        // this out-of-context body scan, conservatively treat
                        // pointer-to-pointer arguments as possibly mutated.
                        for(size_t index = 0; index < arguments.size(); ++index)
                        {
                            TypeNode* argumentType =
                                inferExpressionTypeNode(arguments[index], line);
                            auto* pointer =
                                dynamic_cast<PointerTypeNode*>(argumentType);
                            if(pointer && dynamic_cast<PointerTypeNode*>(
                                              pointer->elementType))
                                mutatedSlots.insert(index);
                        }
                    }
                    if(definition)
                    {
                        std::map<std::string, size_t> slotParameterArguments;
                        size_t argumentIndex = 0;
                        if(definition->parameters)
                            for(auto* parameter :
                                definition->parameters->parameters)
                            {
                                if(!parameter || parameter->name == "self")
                                    continue;
                                auto* pointer = dynamic_cast<PointerTypeNode*>(
                                    parameter->type);
                                if(pointer &&
                                   dynamic_cast<PointerTypeNode*>(
                                       pointer->elementType) &&
                                   argumentIndex < arguments.size())
                                    slotParameterArguments[parameter->name] =
                                        argumentIndex;
                                ++argumentIndex;
                            }
                        std::map<std::string, size_t> slotAliases =
                            slotParameterArguments;
                        bool conditionalWrite = false;
                        std::function<void(StatementListNode*, bool)> scanList;
                        std::function<void(StatementNode*, bool)> scanStatement;
                        scanList = [&](StatementListNode* statements,
                                       bool conditional) {
                            if(statements)
                                for(auto* child : statements->statements)
                                    scanStatement(child, conditional);
                        };
                        scanStatement = [&](StatementNode* child,
                                            bool conditional) {
                            if(!child)
                                return;
                            if(auto* assignment =
                                   dynamic_cast<DerefAssignmentNode*>(child))
                            {
                                auto* identifier = dynamic_cast<IdentifierNode*>(
                                    assignment->pointerExpr);
                                if(identifier)
                                {
                                    auto alias =
                                        slotAliases.find(identifier->name);
                                    if(alias != slotAliases.end())
                                    {
                                        mutatedSlots.insert(alias->second);
                                        if(!conditional)
                                            definiteSlots.insert(alias->second);
                                        else
                                            conditionalWrite = true;
                                    }
                                }
                                return;
                            }
                            auto copySlotAlias = [&](const std::string& name,
                                                     ExpressionNode* value) {
                                auto* identifier =
                                    dynamic_cast<IdentifierNode*>(value);
                                if(!identifier)
                                    return;
                                auto alias =
                                    slotAliases.find(identifier->name);
                                if(alias != slotAliases.end())
                                    slotAliases[name] = alias->second;
                            };
                            if(auto* declaration =
                                   dynamic_cast<LetDeclNode*>(child))
                                copySlotAlias(declaration->name,
                                              declaration->expression);
                            else if(auto* declaration =
                                        dynamic_cast<VarDeclNode*>(child))
                                copySlotAlias(declaration->name,
                                              declaration->initExpr);
                            else if(auto* assignment =
                                        dynamic_cast<AssignmentNode*>(child))
                                copySlotAlias(assignment->name,
                                              assignment->expression);
                            if(auto* block =
                                   dynamic_cast<BlockStatementNode*>(child))
                                scanList(block->statements, conditional);
                            else if(auto* branch =
                                        dynamic_cast<IfNode*>(child))
                            {
                                conditionalWrite = true;
                                scanList(branch->thenBranch, true);
                                scanStatement(branch->elseIfBranch, true);
                                scanList(branch->elseBranch, true);
                            }
                            else if(auto* branch =
                                        dynamic_cast<CexprIfNode*>(child))
                            {
                                conditionalWrite = true;
                                scanList(branch->thenBranch, true);
                                scanStatement(branch->elseIfBranch, true);
                                scanList(branch->elseBranch, true);
                            }
                            else if(auto* loop =
                                        dynamic_cast<WhileNode*>(child))
                            {
                                conditionalWrite = true;
                                scanList(loop->body, true);
                            }
                            else if(auto* loop =
                                        dynamic_cast<ForNode*>(child))
                            {
                                conditionalWrite = true;
                                scanList(loop->body, true);
                            }
                        };
                        scanList(definition->body, false);
                        if(conditionalWrite)
                            definiteSlots.clear();
                    }
                }
                for(size_t slot : mutatedSlots)
                {
                    if(slot >= arguments.size())
                        continue;
                    const auto targetPaths =
                        localBorrowDereferencePaths(arguments[slot]);
                    std::vector<ExpressionNode*> replacementOrigins;
                    for(size_t argument = 0;
                        argument < arguments.size(); ++argument)
                    {
                        if(mutatedSlots.count(argument) != 0)
                            continue;
                        TypeNode* argumentType = inferExpressionTypeNode(
                            arguments[argument], line);
                        if(!argumentType ||
                           argumentType->kind != TypeNode::TYPE_PTR)
                            continue;
                        collectBorrowOrigins(arguments[argument],
                                             replacementOrigins);
                    }
                    for(const auto& path : targetPaths)
                    {
                        const bool definitelyOverwritten =
                            definiteSlots.count(slot) != 0;
                        if(definitelyOverwritten && !inConditionalRegion)
                            eraseLocalBorrowAliasSubtree(path);
                        const bool wasConditional = inConditionalRegion;
                        if(!definitelyOverwritten)
                            inConditionalRegion = true;
                        recordLocalBorrowAlias(path, replacementOrigins);
                        inConditionalRegion = wasConditional;
                    }
                }
            }
            else if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                collectReturnList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectReturnList(branch->thenBranch);
                collectReturns(branch->elseIfBranch);
                collectReturnList(branch->elseBranch);
                inConditionalRegion = wasConditional;
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectReturnList(branch->thenBranch);
                collectReturns(branch->elseIfBranch);
                collectReturnList(branch->elseBranch);
                inConditionalRegion = wasConditional;
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectReturnList(loop->body);
                inConditionalRegion = wasConditional;
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
            {
                const bool wasConditional = inConditionalRegion;
                inConditionalRegion = true;
                collectReturnList(loop->body);
                inConditionalRegion = wasConditional;
            }
        };
        for(const auto& calledName :
            getFunctionItemTargetsForName(call->name))
        {
            auto overloads = functionOverloads.find(calledName);
            if(overloads == functionOverloads.end())
            {
                hasUnknownPointerReturn = true;
                continue;
            }
            for(const auto& overload : overloads->second)
            {
                if(!overload.node || !overload.node->returnType ||
                   overload.node->returnType->kind != TypeNode::TYPE_PTR)
                    continue;
                collectReturnList(overload.node->body);
                auto summary =
                    functionPointerReturnParameters.find(overload.symbolName);
                if(functionPointerReturnSummaryComplete.count(
                       overload.symbolName))
                {
                    if(summary == functionPointerReturnParameters.end())
                        continue;
                    returnedParameterIndices.insert(summary->second.begin(),
                                                     summary->second.end());
                }
                else
                    hasUnknownPointerReturn = true;
            }
        }
        std::vector<ExpressionNode*> returnedArguments;
        returnedArguments = std::move(directBorrowReturns);
        for(size_t index : returnedParameterIndices)
            if(index < call->arguments.size())
                returnedArguments.push_back(call->arguments[index]);
        if(hasUnknownPointerReturn)
            for(auto* argument : call->arguments)
                if(auto* argumentType = inferExpressionTypeNode(argument, line))
                    if(argumentType->kind == TypeNode::TYPE_PTR)
                        returnedArguments.push_back(argument);
        if(!returnedArguments.empty())
        {
            registerPointerBorrowAlternatives(pointerVar, returnedArguments,
                                              line, isMutable, false);
            return;
        }
    }

    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto borrowIt = pointerBorrowTarget.find(idExpr->name);
        if(borrowIt != pointerBorrowTarget.end())
        {
            auto sourcePlace = pointerBorrowPlace.find(idExpr->name);
            borrowPlace = sourcePlace == pointerBorrowPlace.end()
                              ? borrowIt->second
                              : sourcePlace->second;
            if(idExpr->name != pointerVar)
            {
                // For Copy types (strings), assigning a borrow variable is
                // just copying the char* value — no alias tracking needed.
                auto typeIt = variableTypes.find(idExpr->name);
                if(typeIt != variableTypes.end() &&
                   (typeIt->second == TypeNode::TYPE_STRING ||
                    typeIt->second == TypeNode::TYPE_STR8 ||
                    typeIt->second == TypeNode::TYPE_STR16))
                {
                    return; // plain char* copy, no borrow alias
                }
                if(isMutBorrower(idExpr->name))
                {
                    reportError(line, "cannot alias mutable borrow from '" +
                                          idExpr->name + "' into '" +
                                          pointerVar + "'");
                    return;
                }
                (void)registerOwnerBorrow(borrowIt->second, false);
                std::string primaryOwner = borrowIt->second;
                for(const auto& ownerBorrowers : activeBorrowers)
                {
                    if(ownerBorrowers.first == primaryOwner ||
                       ownerBorrowers.second.count(idExpr->name) == 0)
                        continue;
                    borrowPlace = ownerBorrowers.first;
                    (void)registerOwnerBorrow(ownerBorrowers.first, false);
                }
                borrowPlace = sourcePlace == pointerBorrowPlace.end()
                                  ? primaryOwner
                                  : sourcePlace->second;
                pointerBorrowTarget[pointerVar] = primaryOwner;
                pointerBorrowPlace[pointerVar] = borrowPlace;
                return;
            }
            (void)registerOwnerBorrow(borrowIt->second, true);
            return;
        }
    }

    if(auto* method = dynamic_cast<MethodCallNode*>(expr))
    {
        TypeNode* resultType = getLValueType(method, line);
        if(!resultType)
            resultType = inferExpressionTypeNode(method, line);
        if(resultType && resultType->kind == TypeNode::TYPE_PTR)
        {
            TypeNode* receiverType = getLValueType(method->object, line);
            std::string receiverStruct;
            if(auto* structure = dynamic_cast<StructTypeRefNode*>(receiverType))
                receiverStruct = structure->structName;
            else if(auto* generic =
                        dynamic_cast<GenericStructTypeRefNode*>(receiverType))
                receiverStruct = generic->getMangledName();

            StructMethodNode* definition = nullptr;
            auto methods = structMethods.find(receiverStruct);
            if(methods != structMethods.end())
            {
                auto candidate = methods->second.find(method->methodName);
                if(candidate != methods->second.end())
                    definition = candidate->second.second;
            }

            std::vector<std::string> returnedFields;
            bool completeFieldSummary = definition && definition->body;
            std::map<std::string, std::set<size_t>> pointerAliases;
            if(definition && definition->parameters)
            {
                size_t argumentIndex = 0;
                for(auto* parameter : definition->parameters->parameters)
                {
                    if(!parameter || parameter->name == "self")
                        continue;
                    if(dynamic_cast<PointerTypeNode*>(parameter->type))
                        pointerAliases[parameter->name].insert(argumentIndex);
                    ++argumentIndex;
                }
            }
            std::set<size_t> returnedArgumentIndices;
            bool completeArgumentSummary = definition && definition->body;
            bool sawReturn = false;
            bool unknownArgumentOrigin = false;
            std::function<std::set<size_t>(ExpressionNode*)> argumentOrigins =
                [&](ExpressionNode* value) -> std::set<size_t> {
                if(!value)
                    return {};
                if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
                {
                    auto alias = pointerAliases.find(identifier->name);
                    return alias == pointerAliases.end()
                               ? std::set<size_t>{}
                               : alias->second;
                }
                if(auto* cast = dynamic_cast<CastExpressionNode*>(value))
                    return argumentOrigins(cast->expression);
                if(auto* attempt = dynamic_cast<TryExpressionNode*>(value))
                    return argumentOrigins(attempt->expression);
                if(auto* ternary = dynamic_cast<TernaryNode*>(value))
                {
                    auto result = argumentOrigins(ternary->trueExpr);
                    auto other = argumentOrigins(ternary->falseExpr);
                    result.insert(other.begin(), other.end());
                    return result;
                }
                if(auto* match = dynamic_cast<MatchExpressionNode*>(value))
                {
                    std::set<size_t> result;
                    for(auto* arm : match->arms)
                        if(arm)
                        {
                            auto other = argumentOrigins(arm->expression);
                            result.insert(other.begin(), other.end());
                        }
                    return result;
                }
                if(auto* binary = dynamic_cast<BinaryOpNode*>(value))
                    if(binary->op == BinaryOpNode::OP_COMMA)
                        return argumentOrigins(binary->right);
                if(auto* call = dynamic_cast<FunctionCallNode*>(value))
                {
                    std::set<size_t> result;
                    for(const auto& calledName :
                        getFunctionItemTargetsForName(call->name))
                    {
                        auto overloads = functionOverloads.find(calledName);
                        if(overloads == functionOverloads.end())
                        {
                            unknownArgumentOrigin = true;
                            continue;
                        }
                        for(const auto& overload : overloads->second)
                        {
                            if(!overload.node || !overload.node->returnType ||
                               overload.node->returnType->kind !=
                                   TypeNode::TYPE_PTR)
                                continue;
                            if(!functionPointerReturnSummaryComplete.count(
                                   overload.symbolName))
                            {
                                unknownArgumentOrigin = true;
                                continue;
                            }
                            auto summary = functionPointerReturnParameters.find(
                                overload.symbolName);
                            if(summary == functionPointerReturnParameters.end())
                                continue;
                            for(size_t index : summary->second)
                                if(index < call->arguments.size())
                                {
                                    auto other = argumentOrigins(
                                        call->arguments[index]);
                                    result.insert(other.begin(), other.end());
                                }
                        }
                    }
                    return result;
                }
                if(dynamic_cast<CallableInvokeNode*>(value) ||
                   dynamic_cast<MethodCallNode*>(value))
                    unknownArgumentOrigin = true;
                if(auto* address = dynamic_cast<UnaryOpNode*>(value))
                {
                    if(address->op == UnaryOpNode::OP_ADDR ||
                       address->op == UnaryOpNode::OP_ADDR_MUT)
                        if(auto* dereference =
                               dynamic_cast<UnaryOpNode*>(address->operand))
                            if(dereference->op == UnaryOpNode::OP_DEREF)
                                return argumentOrigins(dereference->operand);
                    if(address->op == UnaryOpNode::OP_DEREF)
                        unknownArgumentOrigin = true;
                }
                return {};
            };
            std::function<void(StatementNode*)> scanArgumentReturns;
            std::function<void(StatementListNode*)> scanArgumentList;
            scanArgumentList = [&](StatementListNode* statements) {
                if(!statements)
                    return;
                for(auto* statement : statements->statements)
                    scanArgumentReturns(statement);
            };
            scanArgumentReturns = [&](StatementNode* statement) {
                if(auto* returned = dynamic_cast<ReturnNode*>(statement))
                {
                    sawReturn = true;
                    auto indices = argumentOrigins(returned->expression);
                    if(indices.empty())
                        completeArgumentSummary = false;
                    returnedArgumentIndices.insert(indices.begin(),
                                                   indices.end());
                    return;
                }
                if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
                {
                    auto indices = argumentOrigins(declaration->expression);
                    if(!indices.empty())
                        pointerAliases[declaration->name] = std::move(indices);
                }
                else if(auto* declaration =
                            dynamic_cast<VarDeclNode*>(statement))
                {
                    auto indices = argumentOrigins(declaration->initExpr);
                    if(!indices.empty())
                        pointerAliases[declaration->name] = std::move(indices);
                }
                else if(auto* assignment =
                            dynamic_cast<AssignmentNode*>(statement))
                {
                    auto indices = argumentOrigins(assignment->expression);
                    if(!indices.empty())
                    {
                        auto& alias = pointerAliases[assignment->name];
                        alias.insert(indices.begin(), indices.end());
                    }
                }
                if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                    scanArgumentList(block->statements);
                else if(auto* branch = dynamic_cast<IfNode*>(statement))
                {
                    scanArgumentList(branch->thenBranch);
                    scanArgumentReturns(branch->elseIfBranch);
                    scanArgumentList(branch->elseBranch);
                }
                else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
                {
                    scanArgumentList(branch->thenBranch);
                    scanArgumentReturns(branch->elseIfBranch);
                    scanArgumentList(branch->elseBranch);
                }
                else if(auto* loop = dynamic_cast<WhileNode*>(statement))
                    scanArgumentList(loop->body);
                else if(auto* loop = dynamic_cast<ForNode*>(statement))
                    scanArgumentList(loop->body);
            };
            if(definition && definition->body)
                scanArgumentList(definition->body);

            std::function<std::optional<std::string>(ExpressionNode*)>
                selfFieldPath = [&](ExpressionNode* value)
                -> std::optional<std::string> {
                if(auto* identifier = dynamic_cast<IdentifierNode*>(value))
                    if(identifier->name == "self")
                        return std::string();
                auto* field = dynamic_cast<FieldAccessNode*>(value);
                if(!field)
                    return std::nullopt;
                if(!field->object)
                    return field->structName == "self"
                               ? std::optional<std::string>(field->fieldName)
                               : std::nullopt;
                auto parent = selfFieldPath(field->object);
                if(!parent)
                    return std::nullopt;
                return parent->empty() ? field->fieldName
                                       : *parent + "." + field->fieldName;
            };
            if(definition && definition->body)
                for(auto* statement : definition->body->statements)
                {
                    auto* returned = dynamic_cast<ReturnNode*>(statement);
                    if(!returned)
                    {
                        completeFieldSummary = false;
                        continue;
                    }
                    auto field = selfFieldPath(returned->expression);
                    if(!field || field->empty())
                        completeFieldSummary = false;
                    else
                        returnedFields.push_back(*field);
                }

            const std::string receiverOwner =
                resolveBorrowOwnerFromLValue(method->object);
            const std::string receiverPlace =
                resolveBorrowPlaceFromLValue(method->object);
            if(completeFieldSummary && !returnedFields.empty() &&
               !receiverOwner.empty() &&
               receiverPlace.compare(0, receiverOwner.size(), receiverOwner) ==
                   0)
            {
                std::string receiverPath =
                    receiverPlace.substr(receiverOwner.size());
                if(!receiverPath.empty() && receiverPath.front() == '.')
                    receiverPath.erase(0, 1);
                auto aggregate =
                    aggregateBorrowFieldPlaces.find(receiverOwner);
                if(aggregate != aggregateBorrowFieldPlaces.end())
                    for(const auto& returnedField : returnedFields)
                    {
                        const std::string path =
                            receiverPath.empty()
                                ? returnedField
                                : receiverPath + "." + returnedField;
                        auto field = aggregate->second.find(path);
                        if(field == aggregate->second.end())
                            continue;
                        for(const auto& origin : field->second)
                            for(const auto& place : origin.second)
                            {
                                borrowPlace = place;
                                if(!registerOwnerBorrow(origin.first, false))
                                    return;
                            }
                    }
                // A complete direct-field summary is precise even when that
                // field contains only a global or otherwise untracked pointer.
                return;
            }

            if(unknownArgumentOrigin)
            {
                std::vector<ExpressionNode*> returnedArguments;
                for(size_t index : returnedArgumentIndices)
                    if(index < method->arguments.size())
                        returnedArguments.push_back(method->arguments[index]);
                for(auto* argument : method->arguments)
                    if(auto* type = inferExpressionTypeNode(argument, line))
                        if(type->kind == TypeNode::TYPE_PTR)
                            returnedArguments.push_back(argument);
                if(!returnedArguments.empty())
                    registerPointerBorrowAlternatives(
                        pointerVar, returnedArguments, line, isMutable, false);
            }
            else if(completeArgumentSummary && sawReturn &&
               !returnedArgumentIndices.empty())
            {
                std::vector<ExpressionNode*> returnedArguments;
                for(size_t index : returnedArgumentIndices)
                    if(index < method->arguments.size())
                        returnedArguments.push_back(method->arguments[index]);
                if(!returnedArguments.empty())
                {
                    registerPointerBorrowAlternatives(
                        pointerVar, returnedArguments, line, isMutable, false);
                    return;
                }
            }

            // Mixed or unknown results may come from either a pointer
            // argument or a borrow retained in the receiver.
            if(!unknownArgumentOrigin &&
               !returnedArgumentIndices.empty())
            {
                std::vector<ExpressionNode*> returnedArguments;
                for(size_t index : returnedArgumentIndices)
                    if(index < method->arguments.size())
                        returnedArguments.push_back(method->arguments[index]);
                registerPointerBorrowAlternatives(
                    pointerVar, returnedArguments, line, isMutable, false);
            }

            // Unknown methods may return any borrow stored in their receiver.
            auto origins = aggregateBorrowOrigins.find(receiverOwner);
            if(origins != aggregateBorrowOrigins.end())
                for(const auto& owner : origins->second)
                {
                    borrowPlace = owner;
                    if(!registerOwnerBorrow(owner, false))
                        return;
                }
            return;
        }
    }

    if(!dynamic_cast<IdentifierNode*>(expr) &&
       !dynamic_cast<UnaryOpNode*>(expr))
    {
        const std::string container = resolveBorrowOwnerFromLValue(expr);
        const std::string sourcePath = resolveBorrowPlaceFromLValue(expr);
        if(!container.empty() &&
           sourcePath.compare(0, container.size(), container) == 0)
        {
            std::string relativePath = sourcePath.substr(container.size());
            if(!relativePath.empty() && relativePath.front() == '.')
                relativePath.erase(0, 1);
            auto aggregateFields =
                aggregateBorrowFieldPlaces.find(container);
            if(aggregateFields != aggregateBorrowFieldPlaces.end())
            {
                auto field = aggregateFields->second.find(relativePath);
                if(field != aggregateFields->second.end() &&
                   field->second.size() == 1 &&
                   field->second.begin()->second.size() == 1)
                {
                    const auto& owner = field->second.begin()->first;
                    borrowPlace =
                        *field->second.begin()->second.begin();
                    (void)registerOwnerBorrow(owner, false);
                    return;
                }
            }
        }
        auto origins = aggregateBorrowOrigins.find(container);
        if(origins != aggregateBorrowOrigins.end())
        {
            if(origins->second.empty())
                return;
            const std::string primaryOwner = *origins->second.begin();
            auto containerPlace = pointerBorrowPlace.find(container);
            const bool hasPrecisePlace = origins->second.size() == 1 &&
                                         containerPlace != pointerBorrowPlace.end();
            for(const auto& owner : origins->second)
            {
                borrowPlace = hasPrecisePlace ? containerPlace->second : owner;
                if(!registerOwnerBorrow(owner, false))
                    return;
            }
            pointerBorrowTarget[pointerVar] = primaryOwner;
            pointerBorrowPlace[pointerVar] =
                hasPrecisePlace ? containerPlace->second : primaryOwner;
            return;
        }
    }

    auto* unary = dynamic_cast<UnaryOpNode*>(expr);
    if(!unary || (unary->op != UnaryOpNode::OP_ADDR &&
                  unary->op != UnaryOpNode::OP_ADDR_MUT))
        return;

    std::string ownerName = resolveBorrowOwnerFromLValue(unary->operand);
    if(ownerName.empty())
        return;
    borrowPlace = resolveBorrowPlaceFromLValue(unary->operand);
    if(borrowPlace.empty())
        borrowPlace = ownerName;

    // Shared borrows allow multiple concurrent borrowers; only typed exclusive
    // pointers (e.g. *T owning pointers) enforce a single active borrow.
    bool enforceExclusive = false;
    auto pit = pointerElementTypes.find(pointerVar);
    TypeNode* ptrElemType =
        pit != pointerElementTypes.end() ? pit->second : nullptr;
    if(ptrElemType)
    {
        TypeNode::TypeKind ownerKind = TypeNode::TYPE_VOID;
        bool hasOwnerKind = false;
        auto vit = variableTypes.find(ownerName);
        if(vit != variableTypes.end())
        {
            ownerKind = vit->second;
            hasOwnerKind = true;
        }
        else
        {
            auto gvit = globalVariableTypes.find(ownerName);
            if(gvit != globalVariableTypes.end())
            {
                ownerKind = gvit->second;
                hasOwnerKind = true;
            }
        }

        if(hasOwnerKind)
        {
            bool sameOwnerType = false;
            if(ownerKind == TypeNode::TYPE_STRUCT &&
               ptrElemType->kind == TypeNode::TYPE_STRUCT)
            {
                std::string ownerStructName;
                auto sit = structVariableTypes.find(ownerName);
                if(sit != structVariableTypes.end())
                    ownerStructName = sit->second;
                else
                {
                    auto gsit = globalStructVariableTypes.find(ownerName);
                    if(gsit != globalStructVariableTypes.end())
                        ownerStructName = gsit->second;
                }

                if(auto* sr = dynamic_cast<StructTypeRefNode*>(ptrElemType))
                    sameOwnerType = (sr->structName == ownerStructName);
                else if(auto* gsr = dynamic_cast<GenericStructTypeRefNode*>(
                            ptrElemType))
                    sameOwnerType = (getOrCreateMonomorphizedStruct(
                                         gsr->structName, gsr->typeArgs) ==
                                     ownerStructName);
                // Be conservative when owner struct metadata is unavailable.
                if(ownerStructName.empty())
                    sameOwnerType = true;
            }
            else
            {
                sameOwnerType = (ownerKind == ptrElemType->kind);
            }

            enforceExclusive = sameOwnerType;
        }
    }

    if(registerOwnerBorrow(ownerName, enforceExclusive) &&
       !exclusiveReborrowParent.empty())
        pointerBorrowParent[pointerVar] = exclusiveReborrowParent;
}

void CodeGenerator::registerPointerBorrowAlternatives(
    const std::string& pointerVar,
    const std::vector<ExpressionNode*>& alternatives, int line,
    bool isMutable, bool preserveIncoming)
{
    if(alternatives.empty() && !preserveIncoming)
        return;

    const auto baseTargets = pointerBorrowTarget;
    const auto basePlaces = pointerBorrowPlace;
    const auto baseParents = pointerBorrowParent;
    const auto baseShared = activeBorrowers;
    const auto baseMutable = activeMutBorrower;
    std::vector<std::map<std::string, std::string>> branchTargets;
    std::vector<std::map<std::string, std::string>> branchPlaces;
    std::vector<std::map<std::string, std::string>> branchParents;
    std::vector<std::map<std::string, std::set<std::string>>> branchShared;
    std::vector<std::map<std::string, std::string>> branchMutable;

    if(preserveIncoming)
    {
        branchTargets.push_back(baseTargets);
        branchPlaces.push_back(basePlaces);
        branchParents.push_back(baseParents);
        branchShared.push_back(baseShared);
        branchMutable.push_back(baseMutable);
    }

    for(auto* alternative : alternatives)
    {
        pointerBorrowTarget = baseTargets;
        pointerBorrowPlace = basePlaces;
        pointerBorrowParent = baseParents;
        activeBorrowers = baseShared;
        activeMutBorrower = baseMutable;

        bool alternativeIsMutable = isMutable;
        if(auto* address = dynamic_cast<UnaryOpNode*>(alternative))
            alternativeIsMutable =
                address->op == UnaryOpNode::OP_ADDR_MUT;
        else if(auto* identifier =
                    dynamic_cast<IdentifierNode*>(alternative))
            alternativeIsMutable = isMutBorrower(identifier->name);
        registerPointerBorrow(pointerVar, alternative, line,
                              alternativeIsMutable);
        branchTargets.push_back(pointerBorrowTarget);
        branchPlaces.push_back(pointerBorrowPlace);
        branchParents.push_back(pointerBorrowParent);
        branchShared.push_back(activeBorrowers);
        branchMutable.push_back(activeMutBorrower);
    }

    pointerBorrowTarget = baseTargets;
    pointerBorrowTarget.erase(pointerVar);
    pointerBorrowPlace = basePlaces;
    pointerBorrowPlace.erase(pointerVar);
    pointerBorrowParent = baseParents;
    pointerBorrowParent.erase(pointerVar);
    activeBorrowers = baseShared;
    activeMutBorrower = baseMutable;
    for(auto it = activeBorrowers.begin(); it != activeBorrowers.end();)
    {
        it->second.erase(pointerVar);
        if(it->second.empty())
            it = activeBorrowers.erase(it);
        else
            ++it;
    }
    for(auto it = activeMutBorrower.begin(); it != activeMutBorrower.end();)
        if(it->second == pointerVar)
            it = activeMutBorrower.erase(it);
        else
            ++it;

    bool commonPlace = true;
    bool commonParent = true;
    std::string placeValue;
    std::string parentValue;
    bool targetAssigned = false;
    for(size_t branch = 0; branch < branchTargets.size(); ++branch)
    {
        auto target = branchTargets[branch].find(pointerVar);
        if(target != branchTargets[branch].end() && !targetAssigned)
        {
            pointerBorrowTarget.emplace(pointerVar, target->second);
            targetAssigned = true;
        }

        auto place = branchPlaces[branch].find(pointerVar);
        if(place == branchPlaces[branch].end())
            commonPlace = false;
        else if(placeValue.empty())
            placeValue = place->second;
        else if(placeValue != place->second)
            commonPlace = false;

        auto parent = branchParents[branch].find(pointerVar);
        if(parent == branchParents[branch].end())
            commonParent = false;
        else if(parentValue.empty())
            parentValue = parent->second;
        else if(parentValue != parent->second)
            commonParent = false;

        for(const auto& owner : branchShared[branch])
            if(owner.second.count(pointerVar))
                activeBorrowers[owner.first].insert(pointerVar);
        for(const auto& borrow : branchMutable[branch])
            if(borrow.second == pointerVar)
                activeMutBorrower.emplace(borrow.first, pointerVar);
    }
    if(commonPlace && !placeValue.empty())
        pointerBorrowPlace[pointerVar] = placeValue;
    if(commonParent && !parentValue.empty())
        pointerBorrowParent[pointerVar] = parentValue;
}

