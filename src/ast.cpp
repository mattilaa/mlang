#include "AST.h"
#include "parser.hpp"
#include <stdexcept>

ASTNode* create_program(ASTNode* struct_list, ASTNode* function_list) {
    return new ProgramNode(
        static_cast<StructListNode*>(struct_list),
        static_cast<FunctionListNode*>(function_list)
    );
}

ASTNode* create_function_list(ASTNode* function) {
    auto list = new FunctionListNode();
    list->functions.push_back(static_cast<FunctionDefNode*>(function));
    return list;
}

ASTNode* add_function_to_list(ASTNode* list, ASTNode* function) {
    auto func_list = static_cast<FunctionListNode*>(list);
    func_list->functions.push_back(static_cast<FunctionDefNode*>(function));
    return func_list;
}

ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body) {
    return new FunctionDefNode(
        static_cast<TypeNode*>(type),
        std::string(name),
        static_cast<ParameterListNode*>(params),
        static_cast<StatementListNode*>(body)
    );
}

ASTNode* create_type_node(TypeNode::TypeKind type) {
    return new TypeNode(type);
}

ASTNode* create_parameter_list() {
    return new ParameterListNode();
}

ASTNode* create_parameter(ASTNode* type, char* name) {
    return new ParameterNode(static_cast<TypeNode*>(type), std::string(name));
}

ASTNode* add_parameter(ASTNode* list, ASTNode* param) {
    auto param_list = static_cast<ParameterListNode*>(list);
    param_list->parameters.push_back(static_cast<ParameterNode*>(param));
    return param_list;
}

ASTNode* create_statement_list(ASTNode* stmt) {
    auto list = new StatementListNode();
    list->statements.push_back(static_cast<StatementNode*>(stmt));
    return list;
}

ASTNode* add_statement(ASTNode* list, ASTNode* stmt) {
    auto stmt_list = static_cast<StatementListNode*>(list);
    stmt_list->statements.push_back(static_cast<StatementNode*>(stmt));
    return stmt_list;
}

/*
ASTNode* create_var_decl(ASTNode* type, char* name) {
    return new VarDeclNode(static_cast<TypeNode*>(type), std::string(name));
}
*/

ASTNode* create_assignment(char* name, ASTNode* expr) {
    return new AssignmentNode(std::string(name), static_cast<ExpressionNode*>(expr));
}

ASTNode* create_return_stmt(ASTNode* expr) {
    return new ReturnNode(static_cast<ExpressionNode*>(expr));
}

ASTNode* create_int_literal(int value) {
    return new IntLiteralNode(value);
}

ASTNode* create_float_literal(float value) {
    return new FloatLiteralNode(value);
}

ASTNode* create_identifier(char* name) {
    return new IdentifierNode(std::string(name));
}

ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right) {
    if (!left || !right) {
        fprintf(stderr, "Error: Null operand in binary operation\n");
        return nullptr;
    }

    BinaryOpNode::OpType opType;
    switch (op) {
        case PLUS:
            opType = BinaryOpNode::OpType::OP_PLUS;
            break;
        case MINUS:
            opType = BinaryOpNode::OpType::OP_MINUS;
            break;
        case MULTIPLY:
            opType = BinaryOpNode::OpType::OP_MULTIPLY;
            break;
        case DIVIDE:
            opType = BinaryOpNode::OpType::OP_DIVIDE;
            break;
        case LT:
            opType = BinaryOpNode::OpType::OP_LT;
            break;
        case GT:
            opType = BinaryOpNode::OpType::OP_GT;
            break;
        case LE:
            opType = BinaryOpNode::OpType::OP_LE;
            break;
        case GE:
            opType = BinaryOpNode::OpType::OP_GE;
            break;
        case EQ:
            opType = BinaryOpNode::OpType::OP_EQ;
            break;
        case NE:
            opType = BinaryOpNode::OpType::OP_NE;
            break;
        default:
            fprintf(stderr, "Error: Unknown binary operator: %d\n", op);
            return nullptr;
    }

    ExpressionNode* leftExpr = dynamic_cast<ExpressionNode*>(left);
    ExpressionNode* rightExpr = dynamic_cast<ExpressionNode*>(right);

    if (!leftExpr || !rightExpr) {
        fprintf(stderr, "Error: Invalid operand types for binary operation\n");
        return nullptr;
    }

    return new BinaryOpNode(opType, leftExpr, rightExpr);
}

ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2) {
    auto call = new FunctionCallNode(std::string(name));
    if (arg1) call->arguments.push_back(static_cast<ExpressionNode*>(arg1));
    if (arg2) call->arguments.push_back(static_cast<ExpressionNode*>(arg2));
    return call;
}

ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch) {
    return new IfNode(
        static_cast<ExpressionNode*>(condition),
        static_cast<StatementListNode*>(then_branch),
        static_cast<IfNode*>(else_if_branch),
        static_cast<StatementListNode*>(else_branch)
    );
}

ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr) {
    return new LetDeclNode(
        static_cast<TypeNode*>(type),
        std::string(name),
        static_cast<ExpressionNode*>(expr)
    );
}

ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr) {
    return new VarDeclNode(
        static_cast<TypeNode*>(type),
        std::string(name),
        static_cast<ExpressionNode*>(expr)
    );
}

ASTNode* create_cast_expression(int type, ASTNode* expr) {
    TypeNode::TypeKind targetType;
    switch (type) {
        case TypeNode::TYPE_INT:
            targetType = TypeNode::TYPE_INT;
            break;
        case TypeNode::TYPE_FLOAT:
            targetType = TypeNode::TYPE_FLOAT;
            break;
        default:
            throw std::runtime_error("Unsupported cast type");
    }
    return new CastExpressionNode(targetType, static_cast<ExpressionNode*>(expr));
}

ASTNode* create_struct_list(ASTNode* struct_def) {
    auto list = new StructListNode();
    list->addStruct(static_cast<StructDefNode*>(struct_def));
    return list;
}

ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def) {
    auto structList = static_cast<StructListNode*>(list);
    structList->addStruct(static_cast<StructDefNode*>(struct_def));
    return structList;
}

ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members) {
    return new StructDefNode(
        std::string(name),
        base_name ? std::string(base_name) : "",
        static_cast<StructMemberListNode*>(members)
    );
}

ASTNode* create_struct_member_list(ASTNode* member) {
    auto list = new StructMemberListNode();
    list->addMember(static_cast<StructMemberNode*>(member));
    return list;
}

ASTNode* add_struct_member(ASTNode* list, ASTNode* member) {
    auto memberList = static_cast<StructMemberListNode*>(list);
    memberList->addMember(static_cast<StructMemberNode*>(member));
    return memberList;
}

ASTNode* create_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr) {
    return new StructMemberNode(
        is_var != 0,
        static_cast<TypeNode*>(type),
        std::string(name),
        static_cast<ExpressionNode*>(init_expr)
    );
}

// Implement toString() methods for each node type
std::string TypeNode::toString() const {
    switch(kind) {
        case TYPE_VOID: return "void";
        case TYPE_BOOL: return "bool";
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        default: return "unknown";
    }
}

std::string FunctionListNode::toString() const {
    std::string result = "Functions:\n";
    for (const auto& func : functions) {
        result += func->toString() + "\n";
    }
    return result;
}

std::string FunctionDefNode::toString() const {
    return "fn " + name + "(" + parameters->toString() + ") -> " + returnType->toString() + " {\n" + body->toString() + "\n}";
}

std::string ParameterNode::toString() const {
    return name + ": " + type->toString();
}

std::string ParameterListNode::toString() const {
    std::string result;
    for (size_t i = 0; i < parameters.size(); ++i) {
        if (i > 0) result += ", ";
        result += parameters[i]->toString();
    }
    return result;
}

std::string StatementListNode::toString() const {
    std::string result;
    for (const auto& stmt : statements) {
        result += stmt->toString() + "\n";
    }
    return result;
}

/*
std::string VarDeclNode::toString() const {
    return type->toString() + " " + name + ";";
}
*/

std::string AssignmentNode::toString() const {
    return name + " = " + expression->toString() + ";";
}

std::string ReturnNode::toString() const {
    return "return " + (expression ? expression->toString() : "") + ";";
}

std::string IntLiteralNode::toString() const {
    return std::to_string(value);
}

std::string FloatLiteralNode::toString() const {
    return std::to_string(value);
}

std::string IdentifierNode::toString() const {
    return name;
}

std::string BinaryOpNode::toString() const {
    std::string op_str;
    switch(op) {
        case OP_PLUS: op_str = "+"; break;
        case OP_MINUS: op_str = "-"; break;
        case OP_MULTIPLY: op_str = "*"; break;
        case OP_DIVIDE: op_str = "/"; break;
        case OP_LT: op_str = "<"; break;
        case OP_GT: op_str = ">"; break;
        case OP_LE: op_str = "<="; break;
        case OP_GE: op_str = ">="; break;
        case OP_EQ: op_str = "=="; break;
        case OP_NE: op_str = "!="; break;
    }
    return "(" + left->toString() + " " + op_str + " " + right->toString() + ")";
}

std::string FunctionCallNode::toString() const {
    std::string result = name + "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) result += ", ";
        result += arguments[i]->toString();
    }
    result += ")";
    return result;
}

std::string IfNode::toString() const {
    std::string result = "if " + condition->toString() + ":\n";
    result += thenBranch->toString();
    if (elseIfBranch) {
        result += "else " + elseIfBranch->toString();
    }
    if (elseBranch) {
        result += "else:\n" + elseBranch->toString();
    }
    return result;
}

std::string LetDeclNode::toString() const {
    return "let " + name + ": " + type->toString() + " = " + expression->toString() + ";";
}

std::string VarDeclNode::toString() const {
    return "var " + name + ": " + type->toString() + " = " + expression->toString() + ";";
}

std::string CastExpressionNode::toString() const {
    std::string typeName;
    switch (targetType) {
        case TypeNode::TYPE_INT:
            typeName = "int";
            break;
        case TypeNode::TYPE_FLOAT:
            typeName = "float";
            break;
        default:
            typeName = "unknown";
    }
    return typeName + "(" + expression->toString() + ")";
}

std::string StructMemberNode::toString() const {
    std::string result = (isVar ? "var " : "let ") + name + ": " + type->toString();
    if (initExpr) {
        result += " = " + initExpr->toString();
    }
    return result + ";";
}

std::string StructMemberListNode::toString() const {
    std::string result;
    for (const auto& member : members) {
        result += "    " + member->toString() + "\n";
    }
    return result;
}

std::string StructDefNode::toString() const {
    std::string result = "struct " + name;
    if (!baseName.empty()) {
        result += " : " + baseName;
    }
    result += " {\n" + members->toString() + "};\n";
    return result;
}

std::string StructListNode::toString() const {
    std::string result;
    for (const auto& structDef : structs) {
        result += structDef->toString() + "\n";
    }
    return result;
}

std::string ProgramNode::toString() const {
    std::string result;
    if (structList) {
        result += structList->toString();
    }
    if (functionList) {
        result += functionList->toString();
    }
    return result;
}
