#include "ir.h"

#include <functional>

void CodeGenerator::consumeMoveFromExpression(ExpressionNode* expr, int line,
                                              const std::string& context)
{
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
                reportError(line, id->col,
                            "use of moved value: '" + id->name + "'");
                return;
            }

            if(!borrowed && isMoveOnlyVariable(id->name))
            {
                auto borIt = activeBorrowers.find(id->name);
                if(borIt != activeBorrowers.end() && !borIt->second.empty())
                {
                    std::string by = *borIt->second.begin();
                    reportError(line, "cannot move '" + id->name +
                                          "' while borrowed by '" + by + "'");
                    return;
                }
                movedVariables.insert(id->name);
            }
            return;
        }

        if(auto* unary = dynamic_cast<UnaryOpNode*>(e))
        {
            if(unary->op == UnaryOpNode::OP_ADDR)
            {
                consume(unary->operand, true);
            }
            else
            {
                consume(unary->operand, borrowed);
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
                    reportError(line, "use of moved value: '" +
                                          field->structName + "'");
                }
            }
            return;
        }

        if(auto* index = dynamic_cast<IndexExpressionNode*>(e))
        {
            consume(index->base, true);
            consume(index->index, false);
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
