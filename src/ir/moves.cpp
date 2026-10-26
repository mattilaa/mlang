#include "ir.h"

#include <functional>

void CodeGenerator::consumeMoveFromExpression(
    ExpressionNode* expr,
    int line,
    const std::string& context)
{
    auto validateMoveFromBorrowedPlace = [&](ExpressionNode* placeExpr)
    {
        TypeNode* placeType = getLValueType(placeExpr, line);
        if(!placeType)
            placeType = inferExpressionTypeNode(placeExpr, line);
        if(!placeType || isCopyType(placeType))
            return true;

        const std::string owner = resolveBorrowOwnerFromLValue(placeExpr);
        if(owner.empty())
            return true;
        std::string targetPlace = resolveBorrowPlaceFromLValue(placeExpr);
        if(targetPlace.empty())
            targetPlace = owner;

        auto conflictsWith = [&](const std::string& borrower)
        {
            auto borrowerOwner = pointerBorrowTarget.find(borrower);
            if(borrowerOwner != pointerBorrowTarget.end() &&
               borrowerOwner->second != owner)
                return false;
            if(!borrowerPlaceOverlaps(borrower, owner, targetPlace))
                return false;
            reportError(
                line,
                "cannot move '" + targetPlace + "' while borrowed by '" +
                    borrower + "'");
            return true;
        };

        auto sharedBorrowers = activeBorrowers.find(owner);
        if(sharedBorrowers != activeBorrowers.end())
            for(const auto& borrower : sharedBorrowers->second)
                if(conflictsWith(borrower))
                    return false;
        for(const auto& mutableBorrow : activeMutBorrower)
            if(conflictsWith(mutableBorrow.second))
                return false;
        return true;
    };

    auto validatePartialMoveFromOwner =
        [&](const std::string& owner, const std::string& place)
    {
        auto ownerType = structVariableTypes.find(owner);
        if(ownerType == structVariableTypes.end())
            return true;

        ScopeCleanup cleanup = resolveDropFunctionForStruct(ownerType->second);
        if(!cleanup.function)
            return true;

        reportError(
            line,
            "cannot partially move '" + place + "' from type '" +
                ownerType->second +
                "' because it has an automatic cleanup method");
        return false;
    };

    std::function<void(ExpressionNode*, bool)> consume =
        [&](ExpressionNode* e, bool borrowed) -> void
    {
        if(!e)
            return;

        if(auto* id = dynamic_cast<IdentifierNode*>(e))
        {
            if(globalNamedValues.find(id->name) != globalNamedValues.end())
                return;

            if(isVariableMoved(id->name))
            {
                reportError(
                    line,
                    id->col,
                    "use of moved value: '" + id->name + "'");
                return;
            }

            if(!borrowed && isMoveOnlyVariable(id->name))
            {
                if(!validateMoveFromBorrowedPlace(e))
                    return;
                setActiveDropFlag(id->name, false);
                movedVariables.insert(id->name);
            }
            return;
        }

        if(auto* unary = dynamic_cast<UnaryOpNode*>(e))
        {
            if(unary->op == UnaryOpNode::OP_ADDR ||
               unary->op == UnaryOpNode::OP_ADDR_MUT)
            {
                consume(unary->operand, true);
            }
            else
            {
                if(unary->op == UnaryOpNode::OP_DEREF && !borrowed &&
                   !validateMoveFromBorrowedPlace(e))
                    return;
                // Reading or moving the pointee does not move the reference
                // used to reach it. Keep exclusive reference bindings usable.
                consume(
                    unary->operand,
                    borrowed || unary->op == UnaryOpNode::OP_DEREF);
            }
            return;
        }

        if(auto* binary = dynamic_cast<BinaryOpNode*>(e))
        {
            consume(binary->left, false);
            consume(binary->right, false);
            return;
        }

        if(auto* call = dynamic_cast<FunctionCallNode*>(e))
        {
            (void)call;
            return;
        }

        if(auto* methodCall = dynamic_cast<MethodCallNode*>(e))
        {
            (void)methodCall;
            return;
        }

        if(auto* field = dynamic_cast<FieldAccessNode*>(e))
        {
            if(!borrowed && !validateMoveFromBorrowedPlace(e))
                return;
            if(!borrowed)
            {
                TypeNode* fieldType = getLValueType(e, line);
                const std::string owner = resolveBorrowOwnerFromLValue(e);
                const std::string place = resolveBorrowPlaceFromLValue(e);
                if(fieldType && !isCopyType(fieldType) && !place.empty() &&
                   globalNamedValues.find(owner) == globalNamedValues.end())
                {
                    if(!validatePartialMoveFromOwner(owner, place))
                        return;
                    movedVariables.insert(place);
                }
            }
            if(field->object)
            {
                consume(field->object, true);
            }
            else if(!field->structName.empty())
            {
                if(globalNamedValues.find(field->structName) ==
                       globalNamedValues.end() &&
                   isVariableMoved(field->structName))
                {
                    reportError(
                        line,
                        "use of moved value: '" + field->structName + "'");
                }
            }
            return;
        }

        if(auto* index = dynamic_cast<IndexExpressionNode*>(e))
        {
            if(!borrowed && !validateMoveFromBorrowedPlace(e))
                return;
            if(!borrowed)
            {
                TypeNode* elementType = getLValueType(e, line);
                const std::string owner = resolveBorrowOwnerFromLValue(e);
                const std::string place = resolveBorrowPlaceFromLValue(e);
                if(elementType && !isCopyType(elementType) && !place.empty() &&
                   globalNamedValues.find(owner) == globalNamedValues.end())
                {
                    if(!validatePartialMoveFromOwner(owner, place))
                        return;
                    movedVariables.insert(place);
                }
            }
            consume(index->base, true);
            consume(index->index, false);
            return;
        }

        if(auto* tuple = dynamic_cast<TupleAccessNode*>(e))
        {
            if(!borrowed && !validateMoveFromBorrowedPlace(e))
                return;
            if(!borrowed)
            {
                TypeNode* elementType = getLValueType(e, line);
                const std::string owner = resolveBorrowOwnerFromLValue(e);
                const std::string place = resolveBorrowPlaceFromLValue(e);
                if(elementType && !isCopyType(elementType) && !place.empty() &&
                   globalNamedValues.find(owner) == globalNamedValues.end())
                {
                    if(!validatePartialMoveFromOwner(owner, place))
                        return;
                    movedVariables.insert(place);
                }
            }
            consume(tuple->tuple, true);
            return;
        }

        if(auto* castExpr = dynamic_cast<CastExpressionNode*>(e))
        {
            consume(castExpr->expression, false);
            return;
        }

        if(auto* tryExpr = dynamic_cast<TryExpressionNode*>(e))
        {
            consume(tryExpr->expression, false);
            return;
        }

        if(auto* ternary = dynamic_cast<TernaryNode*>(e))
        {
            consume(ternary->condition, false);
            auto incomingMoved = movedVariables;

            movedVariables = incomingMoved;
            consume(ternary->trueExpr, false);
            auto thenMoved = movedVariables;

            movedVariables = incomingMoved;
            consume(ternary->falseExpr, false);
            auto elseMoved = movedVariables;

            thenMoved.insert(elseMoved.begin(), elseMoved.end());
            movedVariables = std::move(thenMoved);
            return;
        }

        if(auto* listLit = dynamic_cast<ListLiteralNode*>(e))
        {
            if(listLit->elements)
            {
                for(auto* elem : listLit->elements->elements)
                    consume(elem, false);
            }
            return;
        }

        if(auto* mapLit = dynamic_cast<MapLiteralNode*>(e))
        {
            if(mapLit->entries)
            {
                for(auto* entry : mapLit->entries->entries)
                {
                    consume(entry->key, false);
                    consume(entry->value, false);
                }
            }
            return;
        }

        if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(e))
        {
            if(tupleLit->elements)
            {
                for(auto* elem : tupleLit->elements->elements)
                    consume(elem, false);
            }
            return;
        }

        if(auto* structLit = dynamic_cast<StructLiteralNode*>(e))
        {
            for(const auto& init : structLit->fields)
                consume(init.second, false);
            return;
        }
    };

    consume(expr, false);
}
