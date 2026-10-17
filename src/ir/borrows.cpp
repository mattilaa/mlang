#include "ir.h"

#include <functional>

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
        if(auto* literal = dynamic_cast<IntLiteralNode*>(index->index))
            return base + "[" + std::to_string(literal->value) + "]";
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

    std::string ownerName;
    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = pointerBorrowTarget.find(idExpr->name);
        if(it != pointerBorrowTarget.end())
            ownerName = it->second;
    }
    else if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_ADDR)
        {
            ownerName = resolveBorrowOwnerFromLValue(unary->operand);
        }
    }

    if(ownerName.empty())
        return true;

    if(globalNamedValues.find(ownerName) != globalNamedValues.end())
        return true;

    if(namedValues.find(ownerName) != namedValues.end())
    {
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

void CodeGenerator::registerPointerBorrow(const std::string& pointerVar,
                                          ExpressionNode* expr, int line,
                                          bool isMutable)
{
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
                    auto place = pointerBorrowPlace.find(borrower);
                    const std::string existingPlace =
                        place == pointerBorrowPlace.end() ? ownerName
                                                          : place->second;
                    if(borrowPlacesOverlap(existingPlace, borrowPlace))
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
                auto existingOwner = pointerBorrowTarget.find(mutBorrow.second);
                if(existingOwner == pointerBorrowTarget.end() ||
                   existingOwner->second != ownerName)
                    continue;
                hasRootMutableBorrow |= mutBorrow.first == ownerName;
                auto place = pointerBorrowPlace.find(mutBorrow.second);
                const std::string existingPlace =
                    place == pointerBorrowPlace.end() ? ownerName
                                                      : place->second;
                if(mutBorrow.second != pointerVar &&
                   borrowPlacesOverlap(existingPlace, borrowPlace))
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
            auto existingOwner = pointerBorrowTarget.find(mutBorrow.second);
            if(existingOwner == pointerBorrowTarget.end() ||
               existingOwner->second != ownerName)
                continue;
            auto place = pointerBorrowPlace.find(mutBorrow.second);
            const std::string existingPlace =
                place == pointerBorrowPlace.end() ? ownerName : place->second;
            if(borrowPlacesOverlap(existingPlace, borrowPlace))
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
                auto place = pointerBorrowPlace.find(borrower);
                const std::string existingPlace =
                    place == pointerBorrowPlace.end() ? ownerName
                                                      : place->second;
                if(borrower != pointerVar &&
                   borrowPlacesOverlap(existingPlace, borrowPlace))
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
                reportError(line, "cannot alias exclusive borrow from '" +
                                      idExpr->name + "' into '" + pointerVar +
                                      "'");
                return;
            }
            (void)registerOwnerBorrow(borrowIt->second, true);
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

