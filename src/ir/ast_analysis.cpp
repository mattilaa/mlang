#include "ir/ast_analysis.h"

namespace mlang::ir_detail::ast_analysis
{

void collect_used_idents(ASTNode* node, std::set<std::string>& out)
{
    if(!node)
        return;
    if(auto* id = dynamic_cast<IdentifierNode*>(node))
    {
        out.insert(id->name);
        return;
    }
    if(auto* bin = dynamic_cast<BinaryOpNode*>(node))
    {
        collect_used_idents(bin->left, out);
        collect_used_idents(bin->right, out);
        return;
    }
    if(auto* un = dynamic_cast<UnaryOpNode*>(node))
    {
        collect_used_idents(un->operand, out);
        return;
    }
    if(auto* tern = dynamic_cast<TernaryNode*>(node))
    {
        collect_used_idents(tern->condition, out);
        collect_used_idents(tern->trueExpr, out);
        collect_used_idents(tern->falseExpr, out);
        return;
    }
    if(auto* call = dynamic_cast<FunctionCallNode*>(node))
    {
        for(auto* arg : call->arguments)
            collect_used_idents(arg, out);
        return;
    }
    if(auto* mc = dynamic_cast<MethodCallNode*>(node))
    {
        collect_used_idents(mc->object, out);
        for(auto* arg : mc->arguments)
            collect_used_idents(arg, out);
        return;
    }
    if(auto* fa = dynamic_cast<FieldAccessNode*>(node))
    {
        if(!fa->structName.empty())
            out.insert(fa->structName);
        collect_used_idents(fa->object, out);
        return;
    }
    if(auto* idx = dynamic_cast<IndexExpressionNode*>(node))
    {
        collect_used_idents(idx->base, out);
        collect_used_idents(idx->index, out);
        return;
    }
    if(auto* cast = dynamic_cast<CastExpressionNode*>(node))
    {
        collect_used_idents(cast->expression, out);
        return;
    }
    if(auto* tryE = dynamic_cast<TryExpressionNode*>(node))
    {
        collect_used_idents(tryE->expression, out);
        return;
    }
    if(auto* sizeofExpr = dynamic_cast<SizeofExpressionNode*>(node))
    {
        collect_used_idents(sizeofExpr->expressionTarget, out);
        return;
    }
    if(auto* cexprExpr = dynamic_cast<CexprExpressionNode*>(node))
    {
        collect_used_idents(cexprExpr->expression, out);
        return;
    }
    if(auto* es = dynamic_cast<ExpressionStatementNode*>(node))
    {
        collect_used_idents(es->expression, out);
        return;
    }
    if(auto* ret = dynamic_cast<ReturnNode*>(node))
    {
        collect_used_idents(ret->expression, out);
        return;
    }
    if(auto* throwNode = dynamic_cast<ThrowNode*>(node))
    {
        collect_used_idents(throwNode->expression, out);
        return;
    }
    if(auto* letD = dynamic_cast<LetDeclNode*>(node))
    {
        collect_used_idents(letD->expression, out);
        return;
    }
    if(auto* varD = dynamic_cast<VarDeclNode*>(node))
    {
        collect_used_idents(varD->initExpr, out);
        return;
    }
    if(auto* asgn = dynamic_cast<AssignmentNode*>(node))
    {
        collect_used_idents(asgn->expression, out);
        return;
    }
    if(auto* fa2 = dynamic_cast<FieldAssignmentNode*>(node))
    {
        if(!fa2->structName.empty())
            out.insert(fa2->structName);
        collect_used_idents(fa2->target, out);
        collect_used_idents(fa2->expression, out);
        return;
    }
    if(auto* da = dynamic_cast<DerefAssignmentNode*>(node))
    {
        collect_used_idents(da->pointerExpr, out);
        collect_used_idents(da->value, out);
        return;
    }
    if(auto* print = dynamic_cast<PrintNode*>(node))
    {
        for(auto* arg : print->arguments)
            collect_used_idents(arg, out);
        for(const auto& namedArg : print->namedArguments)
            collect_used_idents(namedArg.second, out);
        return;
    }
    if(auto* aeq = dynamic_cast<AssertEqNode*>(node))
    {
        collect_used_idents(aeq->left, out);
        collect_used_idents(aeq->right, out);
        return;
    }
    if(auto* ifN = dynamic_cast<IfNode*>(node))
    {
        collect_used_idents(ifN->conditionInit, out);
        collect_used_idents(ifN->condition, out);
        if(ifN->thenBranch)
            for(auto* s : ifN->thenBranch->statements)
                collect_used_idents(s, out);
        collect_used_idents(ifN->elseIfBranch, out);
        if(ifN->elseBranch)
            for(auto* s : ifN->elseBranch->statements)
                collect_used_idents(s, out);
        return;
    }
    if(auto* forN = dynamic_cast<ForNode*>(node))
    {
        collect_used_idents(forN->iterable, out);
        if(forN->body)
            for(auto* s : forN->body->statements)
                collect_used_idents(s, out);
        return;
    }
    if(auto* whileN = dynamic_cast<WhileNode*>(node))
    {
        collect_used_idents(whileN->condition, out);
        if(whileN->body)
            for(auto* s : whileN->body->statements)
                collect_used_idents(s, out);
        return;
    }
    if(auto* blk = dynamic_cast<BlockStatementNode*>(node))
    {
        if(blk->statements)
            for(auto* s : blk->statements->statements)
                collect_used_idents(s, out);
        return;
    }
    if(auto* tc = dynamic_cast<TryCatchNode*>(node))
    {
        collect_used_idents(tc->tryBlock, out);
        collect_used_idents(tc->catchBlock, out);
        return;
    }
}

bool contains_update_expression(ASTNode* node)
{
    if(!node)
        return false;
    if(dynamic_cast<UpdateExpressionNode*>(node))
        return true;
    if(auto* bin = dynamic_cast<BinaryOpNode*>(node))
        return contains_update_expression(bin->left) ||
               contains_update_expression(bin->right);
    if(auto* un = dynamic_cast<UnaryOpNode*>(node))
        return contains_update_expression(un->operand);
    if(auto* tern = dynamic_cast<TernaryNode*>(node))
        return contains_update_expression(tern->condition) ||
               contains_update_expression(tern->trueExpr) ||
               contains_update_expression(tern->falseExpr);
    if(auto* idx = dynamic_cast<IndexExpressionNode*>(node))
        return contains_update_expression(idx->base) ||
               contains_update_expression(idx->index);
    if(auto* cast = dynamic_cast<CastExpressionNode*>(node))
        return contains_update_expression(cast->expression);
    if(auto* tryE = dynamic_cast<TryExpressionNode*>(node))
        return contains_update_expression(tryE->expression);
    if(auto* call = dynamic_cast<FunctionCallNode*>(node))
    {
        for(auto* arg : call->arguments)
        {
            if(contains_update_expression(arg))
                return true;
        }
        return false;
    }
    if(auto* mc = dynamic_cast<MethodCallNode*>(node))
    {
        if(contains_update_expression(mc->object))
            return true;
        for(auto* arg : mc->arguments)
        {
            if(contains_update_expression(arg))
                return true;
        }
        return false;
    }
    if(auto* fa = dynamic_cast<FieldAccessNode*>(node))
        return contains_update_expression(fa->object);
    if(auto* fmt = dynamic_cast<FormatNode*>(node))
    {
        for(auto* arg : fmt->arguments)
        {
            if(contains_update_expression(arg))
                return true;
        }
        for(const auto& namedArg : fmt->namedArguments)
        {
            if(contains_update_expression(namedArg.second))
                return true;
        }
        return false;
    }
    return false;
}

bool contains_exception_control_flow(ASTNode* node)
{
    if(!node)
        return false;
    if(dynamic_cast<ThrowNode*>(node) || dynamic_cast<TryCatchNode*>(node))
        return true;
    if(auto* stmtList = dynamic_cast<StatementListNode*>(node))
    {
        for(auto* stmt : stmtList->statements)
        {
            if(contains_exception_control_flow(stmt))
                return true;
        }
        return false;
    }
    if(auto* block = dynamic_cast<BlockStatementNode*>(node))
        return contains_exception_control_flow(block->statements);
    if(auto* ifNode = dynamic_cast<IfNode*>(node))
    {
        return contains_exception_control_flow(ifNode->conditionInit) ||
               contains_exception_control_flow(ifNode->thenBranch) ||
               contains_exception_control_flow(ifNode->elseIfBranch) ||
               contains_exception_control_flow(ifNode->elseBranch);
    }
    if(auto* forNode = dynamic_cast<ForNode*>(node))
        return contains_exception_control_flow(forNode->body);
    if(auto* whileNode = dynamic_cast<WhileNode*>(node))
        return contains_exception_control_flow(whileNode->body);
    if(auto* closure = dynamic_cast<ClosureNode*>(node))
        return contains_exception_control_flow(closure->body);
    return false;
}

ExpressionNode* strip_iter_methods(ExpressionNode* expr)
{
    while(auto* mc = dynamic_cast<MethodCallNode*>(expr))
    {
        const std::string& mn = mc->methodName;
        if(mn == "iter" || mn == "into_iter" || mn == "enumerate")
            expr = mc->object;
        else
            break;
    }
    return expr;
}

} // namespace mlang::ir_detail::ast_analysis
