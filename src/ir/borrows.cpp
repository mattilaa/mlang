#include "ir.h"

#include <functional>
#include <limits>
#include <optional>

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

bool CodeGenerator::validateBorrowRead(ExpressionNode* expr, int line)
{
    if(suppressBorrowReadValidation)
        return true;

    const std::string owner = resolveBorrowOwnerFromLValue(expr);
    const std::string place = resolveBorrowPlaceFromLValue(expr);
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
    auto* pointer = dynamic_cast<IdentifierNode*>(expr);
    if(!pointer)
        return true;

    auto ownerIt = pointerBorrowTarget.find(pointer->name);
    if(ownerIt == pointerBorrowTarget.end())
        return true;

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
        TypeNode* expressionType = getLValueType(expr, line);
        if(expressionType && expressionType->kind == TypeNode::TYPE_PTR)
        {
            const std::string owner = resolveBorrowOwnerFromLValue(expr);
            if(!owner.empty())
                ownerNames.insert(owner);
        }
    }

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

void CodeGenerator::trackAggregateBorrowOrigins(
    const std::string& variable, ExpressionNode* expression)
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
    collectFields(expression, "");
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
                aggregateBorrowOrigins[variable].insert(
                    registeredOwner->second);
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
            aggregateBorrowOrigins[variable].insert(borrow.owner);
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

    auto registerOwnerBorrow = [&](const std::string& ownerName,
                                   bool enforceExclusive) -> bool
    {
        if(globalNamedValues.find(ownerName) == globalNamedValues.end() &&
           isVariableMoved(ownerName))
        {
            reportError(line, "cannot borrow moved value: '" + ownerName + "'");
            return false;
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
                if(borrowerPlaceOverlaps(mutBorrow.second, ownerName,
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
                !hasRootMutableBorrow ? ownerName : borrowPlace;
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

    (void)registerOwnerBorrow(ownerName, enforceExclusive);
}

