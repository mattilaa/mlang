#include "ir/return_inference.h"

#include "ir/common.h"

#include <vector>

namespace mlang::ir_detail::return_inference
{

using mlang::ir_detail::common::Helpers;

namespace
{

std::string inferredTypeName(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_VOID:
        return "void";
    case TypeNode::TYPE_BOOL:
        return "bool";
    case TypeNode::TYPE_BIT:
        return "bit";
    case TypeNode::TYPE_INT:
        return "i32";
    case TypeNode::TYPE_I32:
        return "i32";
    case TypeNode::TYPE_I64:
        return "i64";
    case TypeNode::TYPE_FLOAT:
        return "f32";
    case TypeNode::TYPE_DOUBLE:
        return "f64";
    case TypeNode::TYPE_STRING:
        return "str8";
    case TypeNode::TYPE_STR8:
        return "str8";
    case TypeNode::TYPE_STR16:
        return "str16";
    case TypeNode::TYPE_I8:
        return "i8";
    case TypeNode::TYPE_I16:
        return "i16";
    case TypeNode::TYPE_U8:
        return "u8";
    case TypeNode::TYPE_U16:
        return "u16";
    case TypeNode::TYPE_U32:
        return "u32";
    case TypeNode::TYPE_U64:
        return "u64";
    default:
        return "unknown";
    }
}

bool mergeInferredKinds(TypeNode::TypeKind& acc, TypeNode::TypeKind next)
{
    acc = Helpers::normalizeInferredKind(acc);
    next = Helpers::normalizeInferredKind(next);
    if(acc == next)
        return true;

    if(Helpers::isIntegerInferKind(acc) && Helpers::isIntegerInferKind(next))
    {
        if(acc == TypeNode::TYPE_I64 || acc == TypeNode::TYPE_U64 ||
           next == TypeNode::TYPE_I64 || next == TypeNode::TYPE_U64)
            acc = TypeNode::TYPE_I64;
        else
            acc = TypeNode::TYPE_I32;
        return true;
    }
    if((Helpers::isIntegerInferKind(acc) || Helpers::isFloatInferKind(acc)) &&
       (Helpers::isIntegerInferKind(next) || Helpers::isFloatInferKind(next)))
    {
        if(acc == TypeNode::TYPE_DOUBLE || next == TypeNode::TYPE_DOUBLE)
            acc = TypeNode::TYPE_DOUBLE;
        else
            acc = TypeNode::TYPE_FLOAT;
        return true;
    }
    if((acc == TypeNode::TYPE_STRING && next == TypeNode::TYPE_STR8) ||
       (acc == TypeNode::TYPE_STR8 && next == TypeNode::TYPE_STRING) ||
       (acc == TypeNode::TYPE_STRING && next == TypeNode::TYPE_STR16) ||
       (acc == TypeNode::TYPE_STR16 && next == TypeNode::TYPE_STRING) ||
       (acc == TypeNode::TYPE_STR8 && next == TypeNode::TYPE_STR16) ||
       (acc == TypeNode::TYPE_STR16 && next == TypeNode::TYPE_STR8))
    {
        acc = TypeNode::TYPE_STRING;
        return true;
    }
    return false;
}

struct ReturnInferenceData
{
    std::vector<TypeNode::TypeKind> valueReturns;
    bool hasBareReturn = false;
    bool hasUnknownValueReturn = false;
};

bool inferExprKindForReturn(
    ExpressionNode* expr,
    const std::unordered_map<std::string, TypeNode::TypeKind>& localKinds,
    const std::unordered_map<std::string, TypeNode::TypeKind>& fnReturnKinds,
    TypeNode::TypeKind& outKind)
{
    if(!expr)
        return false;
    if(dynamic_cast<BoolLiteralNode*>(expr))
    {
        outKind = TypeNode::TYPE_BOOL;
        return true;
    }
    if(dynamic_cast<IntLiteralNode*>(expr))
    {
        outKind = TypeNode::TYPE_I32;
        return true;
    }
    if(dynamic_cast<FloatLiteralNode*>(expr))
    {
        outKind = TypeNode::TYPE_FLOAT;
        return true;
    }
    if(dynamic_cast<DoubleLiteralNode*>(expr))
    {
        outKind = TypeNode::TYPE_DOUBLE;
        return true;
    }
    if(dynamic_cast<StringLiteralNode*>(expr))
    {
        outKind = TypeNode::TYPE_STRING;
        return true;
    }
    if(auto* castExpr = dynamic_cast<CastExpressionNode*>(expr))
    {
        outKind = Helpers::normalizeInferredKind(castExpr->targetType);
        return true;
    }
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = localKinds.find(id->name);
        if(it == localKinds.end())
            return false;
        outKind = Helpers::normalizeInferredKind(it->second);
        return true;
    }
    if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
    {
        if(call->name == "String::new" ||
           call->name == "String::with_capacity" ||
           call->name == "String::from" || call->name == "String::to_utf8")
        {
            outKind = TypeNode::TYPE_STRING;
            return true;
        }
        auto fit = fnReturnKinds.find(call->name);
        if(fit == fnReturnKinds.end())
            return false;
        outKind = Helpers::normalizeInferredKind(fit->second);
        return true;
    }
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        TypeNode::TypeKind operandKind = TypeNode::TYPE_VOID;
        if(!inferExprKindForReturn(unary->operand, localKinds, fnReturnKinds,
                                   operandKind))
            return false;
        if(unary->op == UnaryOpNode::OP_NOT)
        {
            outKind = TypeNode::TYPE_BOOL;
            return true;
        }
        outKind = Helpers::normalizeInferredKind(operandKind);
        return true;
    }
    if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
    {
        TypeNode::TypeKind t = TypeNode::TYPE_VOID;
        TypeNode::TypeKind f = TypeNode::TYPE_VOID;
        if(!inferExprKindForReturn(ternary->trueExpr, localKinds, fnReturnKinds,
                                   t))
            return false;
        if(!inferExprKindForReturn(ternary->falseExpr, localKinds,
                                   fnReturnKinds, f))
            return false;
        t = Helpers::normalizeInferredKind(t);
        if(!mergeInferredKinds(t, f))
            return false;
        outKind = t;
        return true;
    }
    if(auto* bin = dynamic_cast<BinaryOpNode*>(expr))
    {
        if(bin->op == BinaryOpNode::OP_LT || bin->op == BinaryOpNode::OP_GT ||
           bin->op == BinaryOpNode::OP_LE || bin->op == BinaryOpNode::OP_GE ||
           bin->op == BinaryOpNode::OP_EQ || bin->op == BinaryOpNode::OP_NE ||
           bin->op == BinaryOpNode::OP_AND || bin->op == BinaryOpNode::OP_OR)
        {
            outKind = TypeNode::TYPE_BOOL;
            return true;
        }
        if(bin->op == BinaryOpNode::OP_SPACESHIP)
        {
            outKind = TypeNode::TYPE_INT;
            return true;
        }

        TypeNode::TypeKind l = TypeNode::TYPE_VOID;
        TypeNode::TypeKind r = TypeNode::TYPE_VOID;
        if(!inferExprKindForReturn(bin->left, localKinds, fnReturnKinds, l))
            return false;
        if(!inferExprKindForReturn(bin->right, localKinds, fnReturnKinds, r))
            return false;
        l = Helpers::normalizeInferredKind(l);
        if(!mergeInferredKinds(l, r))
            return false;
        outKind = l;
        return true;
    }
    return false;
}

void collectReturnKindsFromStmt(
    StatementNode* stmt,
    std::unordered_map<std::string, TypeNode::TypeKind>& locals,
    const std::unordered_map<std::string, TypeNode::TypeKind>& fnReturnKinds,
    ReturnInferenceData& out);

bool tryEvalReturnInferenceBool(ExpressionNode* expr, bool& out)
{
    if(!expr)
        return false;
    if(auto* b = dynamic_cast<BoolLiteralNode*>(expr))
    {
        out = b->value;
        return true;
    }
    if(auto* i = dynamic_cast<IntLiteralNode*>(expr))
    {
        out = i->value != 0;
        return true;
    }
    if(auto* f = dynamic_cast<FloatLiteralNode*>(expr))
    {
        out = f->value != 0.0f;
        return true;
    }
    if(auto* d = dynamic_cast<DoubleLiteralNode*>(expr))
    {
        out = d->value != 0.0;
        return true;
    }
    if(auto* un = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(un->op != UnaryOpNode::OP_NOT)
            return false;
        bool value = false;
        if(!tryEvalReturnInferenceBool(un->operand, value))
            return false;
        out = !value;
        return true;
    }
    if(auto* cexprExpr = dynamic_cast<CexprExpressionNode*>(expr))
        return tryEvalReturnInferenceBool(cexprExpr->expression, out);
    return false;
}

void collectReturnKindsFromList(
    StatementListNode* body,
    std::unordered_map<std::string, TypeNode::TypeKind>& locals,
    const std::unordered_map<std::string, TypeNode::TypeKind>& fnReturnKinds,
    ReturnInferenceData& out)
{
    if(!body)
        return;
    for(auto* stmt : body->statements)
        collectReturnKindsFromStmt(stmt, locals, fnReturnKinds, out);
}

void collectReturnKindsFromStmt(
    StatementNode* stmt,
    std::unordered_map<std::string, TypeNode::TypeKind>& locals,
    const std::unordered_map<std::string, TypeNode::TypeKind>& fnReturnKinds,
    ReturnInferenceData& out)
{
    if(!stmt)
        return;

    if(auto* ret = dynamic_cast<ReturnNode*>(stmt))
    {
        if(!ret->expression)
        {
            out.hasBareReturn = true;
            return;
        }
        TypeNode::TypeKind k = TypeNode::TYPE_VOID;
        if(inferExprKindForReturn(ret->expression, locals, fnReturnKinds, k))
            out.valueReturns.push_back(Helpers::normalizeInferredKind(k));
        else
            out.hasUnknownValueReturn = true;
        return;
    }
    if(auto* letDecl = dynamic_cast<LetDeclNode*>(stmt))
    {
        if(letDecl->type)
        {
            locals[letDecl->name] =
                Helpers::normalizeInferredKind(letDecl->type->kind);
        }
        else if(letDecl->expression)
        {
            TypeNode::TypeKind k = TypeNode::TYPE_VOID;
            if(inferExprKindForReturn(letDecl->expression, locals,
                                      fnReturnKinds, k))
                locals[letDecl->name] = Helpers::normalizeInferredKind(k);
        }
        return;
    }
    if(auto* varDecl = dynamic_cast<VarDeclNode*>(stmt))
    {
        if(varDecl->type)
        {
            locals[varDecl->name] =
                Helpers::normalizeInferredKind(varDecl->type->kind);
        }
        else if(varDecl->initExpr)
        {
            TypeNode::TypeKind k = TypeNode::TYPE_VOID;
            if(inferExprKindForReturn(varDecl->initExpr, locals, fnReturnKinds,
                                      k))
                locals[varDecl->name] = Helpers::normalizeInferredKind(k);
        }
        return;
    }
    if(auto* block = dynamic_cast<BlockStatementNode*>(stmt))
    {
        auto blockLocals = locals;
        collectReturnKindsFromList(block->statements, blockLocals,
                                   fnReturnKinds, out);
        return;
    }
    if(auto* ifNode = dynamic_cast<IfNode*>(stmt))
    {
        auto ifScopeLocals = locals;
        if(ifNode->conditionInit)
            collectReturnKindsFromStmt(ifNode->conditionInit, ifScopeLocals,
                                       fnReturnKinds, out);

        auto thenLocals = ifScopeLocals;
        collectReturnKindsFromList(ifNode->thenBranch, thenLocals,
                                   fnReturnKinds, out);

        if(ifNode->elseIfBranch)
        {
            auto elseIfLocals = ifScopeLocals;
            collectReturnKindsFromStmt(ifNode->elseIfBranch, elseIfLocals,
                                       fnReturnKinds, out);
        }
        if(ifNode->elseBranch)
        {
            auto elseLocals = ifScopeLocals;
            collectReturnKindsFromList(ifNode->elseBranch, elseLocals,
                                       fnReturnKinds, out);
        }
        return;
    }
    if(auto* cexprIf = dynamic_cast<CexprIfNode*>(stmt))
    {
        bool takeThen = false;
        if(tryEvalReturnInferenceBool(cexprIf->condition, takeThen))
        {
            if(takeThen)
            {
                auto thenLocals = locals;
                collectReturnKindsFromList(cexprIf->thenBranch, thenLocals,
                                           fnReturnKinds, out);
            }
            else if(cexprIf->elseIfBranch)
            {
                auto elseIfLocals = locals;
                collectReturnKindsFromStmt(cexprIf->elseIfBranch,
                                           elseIfLocals, fnReturnKinds, out);
            }
            else if(cexprIf->elseBranch)
            {
                auto elseLocals = locals;
                collectReturnKindsFromList(cexprIf->elseBranch, elseLocals,
                                           fnReturnKinds, out);
            }
            return;
        }

        auto thenLocals = locals;
        collectReturnKindsFromList(cexprIf->thenBranch, thenLocals,
                                   fnReturnKinds, out);
        if(cexprIf->elseIfBranch)
        {
            auto elseIfLocals = locals;
            collectReturnKindsFromStmt(cexprIf->elseIfBranch, elseIfLocals,
                                       fnReturnKinds, out);
        }
        if(cexprIf->elseBranch)
        {
            auto elseLocals = locals;
            collectReturnKindsFromList(cexprIf->elseBranch, elseLocals,
                                       fnReturnKinds, out);
        }
        return;
    }
    if(auto* whileNode = dynamic_cast<WhileNode*>(stmt))
    {
        auto loopLocals = locals;
        collectReturnKindsFromList(whileNode->body, loopLocals, fnReturnKinds,
                                   out);
        return;
    }
    if(auto* forNode = dynamic_cast<ForNode*>(stmt))
    {
        auto loopLocals = locals;
        collectReturnKindsFromList(forNode->body, loopLocals, fnReturnKinds,
                                   out);
        return;
    }
}

} // namespace

bool infer_function_return_type(
    FunctionDefNode* fn,
    const std::unordered_map<std::string, TypeNode::TypeKind>& fnReturnKinds,
    TypeNode::TypeKind& inferred,
    std::string& reason)
{
    if(!fn || fn->isExtern || !fn->body)
    {
        reason = "function body is required for inferred return type";
        return false;
    }

    std::unordered_map<std::string, TypeNode::TypeKind> localKinds;
    if(fn->parameters)
    {
        for(auto* p : fn->parameters->parameters)
        {
            if(!p || !p->type)
                continue;
            localKinds[p->name] = Helpers::normalizeInferredKind(p->type->kind);
        }
    }

    ReturnInferenceData data;
    collectReturnKindsFromList(fn->body, localKinds, fnReturnKinds, data);

    if(data.hasBareReturn && !data.valueReturns.empty())
    {
        reason = "function mixes 'return;' and 'return value;'; add explicit "
                 "return type";
        return false;
    }

    if(data.valueReturns.empty())
    {
        if(data.hasUnknownValueReturn)
        {
            reason = "cannot infer return type from return expressions; add "
                     "explicit return type";
            return false;
        }
        inferred = TypeNode::TYPE_VOID;
        return true;
    }

    TypeNode::TypeKind merged = data.valueReturns.front();
    for(size_t i = 1; i < data.valueReturns.size(); ++i)
    {
        TypeNode::TypeKind candidate = data.valueReturns[i];
        TypeNode::TypeKind before = merged;
        if(!mergeInferredKinds(merged, candidate))
        {
            reason = "incompatible return types '" + inferredTypeName(before) +
                     "' and '" + inferredTypeName(candidate) +
                     "'; add explicit return type";
            return false;
        }
    }
    if(data.hasUnknownValueReturn)
    {
        reason = "some return expressions could not be inferred; add explicit "
                 "return type";
        return false;
    }

    inferred = Helpers::normalizeInferredKind(merged);
    return true;
}

} // namespace mlang::ir_detail::return_inference
