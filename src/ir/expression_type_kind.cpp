#include "ir/expression_type_kind.h"

TypeNode::TypeKind getExpressionTypeKind(
    ExpressionNode* expr,
    const std::map<std::string, TypeNode::TypeKind>& variableTypes)
{
    if(dynamic_cast<BoolLiteralNode*>(expr))
        return TypeNode::TYPE_BOOL;
    if(dynamic_cast<IntLiteralNode*>(expr))
        return TypeNode::TYPE_INT;
    if(dynamic_cast<FloatLiteralNode*>(expr))
        return TypeNode::TYPE_FLOAT;
    if(dynamic_cast<DoubleLiteralNode*>(expr))
        return TypeNode::TYPE_DOUBLE;
    if(dynamic_cast<StringLiteralNode*>(expr) ||
       dynamic_cast<FormatNode*>(expr))
        return TypeNode::TYPE_STRING;
    if(auto* castExpr = dynamic_cast<CastExpressionNode*>(expr))
        return castExpr->targetType;
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = variableTypes.find(id->name);
        if(it != variableTypes.end())
            return it->second;
    }
    if(auto* fn = dynamic_cast<FunctionCallNode*>(expr))
    {
        if(fn->name == "String::new" || fn->name == "String::with_capacity" ||
           fn->name == "String::from" || fn->name == "String::to_utf8")
            return TypeNode::TYPE_STRING;
    }
    if(auto* mc = dynamic_cast<MethodCallNode*>(expr))
    {
        if(mc->methodName == "clone")
        {
            TypeNode::TypeKind recvKind =
                getExpressionTypeKind(mc->object, variableTypes);
            if(recvKind == TypeNode::TYPE_STRING ||
               recvKind == TypeNode::TYPE_STR8 ||
               recvKind == TypeNode::TYPE_STR16)
                return recvKind;
        }
    }
    if(auto* bin = dynamic_cast<BinaryOpNode*>(expr))
    {
        if(bin->op == BinaryOpNode::OP_LT || bin->op == BinaryOpNode::OP_GT ||
           bin->op == BinaryOpNode::OP_LE || bin->op == BinaryOpNode::OP_GE ||
           bin->op == BinaryOpNode::OP_EQ || bin->op == BinaryOpNode::OP_NE ||
           bin->op == BinaryOpNode::OP_AND || bin->op == BinaryOpNode::OP_OR)
            return TypeNode::TYPE_BOOL;
        if(bin->op == BinaryOpNode::OP_SPACESHIP)
            return TypeNode::TYPE_INT;

        TypeNode::TypeKind lhsKind =
            getExpressionTypeKind(bin->left, variableTypes);
        TypeNode::TypeKind rhsKind =
            getExpressionTypeKind(bin->right, variableTypes);
        bool lhsIsString = lhsKind == TypeNode::TYPE_STRING ||
                           lhsKind == TypeNode::TYPE_STR8 ||
                           lhsKind == TypeNode::TYPE_STR16;
        bool rhsIsString = rhsKind == TypeNode::TYPE_STRING ||
                           rhsKind == TypeNode::TYPE_STR8 ||
                           rhsKind == TypeNode::TYPE_STR16;
        if(bin->op == BinaryOpNode::OP_PLUS && lhsIsString && rhsIsString &&
           lhsKind == rhsKind)
            return lhsKind;
        if(lhsKind == TypeNode::TYPE_DOUBLE || rhsKind == TypeNode::TYPE_DOUBLE)
            return TypeNode::TYPE_DOUBLE;
        if(lhsKind == TypeNode::TYPE_FLOAT || rhsKind == TypeNode::TYPE_FLOAT)
            return TypeNode::TYPE_FLOAT;
        return TypeNode::TYPE_INT;
    }
    return TypeNode::TYPE_INT;
}
