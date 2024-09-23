#include "AST.h"
#include "parser.hpp"
#include <stdexcept>

ASTNode* create_program(ASTNode* function_list) {
    return new ProgramNode(static_cast<FunctionListNode*>(function_list));
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

ASTNode* add_parameter(ASTNode* list, ASTNode* type, char* name) {
    auto param_list = static_cast<ParameterListNode*>(list);
    param_list->parameters.emplace_back(static_cast<TypeNode*>(type), std::string(name));
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

ASTNode* create_var_decl(ASTNode* type, char* name) {
    return new VarDeclNode(static_cast<TypeNode*>(type), std::string(name));
}

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
    BinaryOpNode::OpType opType;
    switch (op) {
        case PLUS: opType = BinaryOpNode::OP_PLUS; break;
        case MINUS: opType = BinaryOpNode::OP_MINUS; break;
        case MULTIPLY: opType = BinaryOpNode::OP_MULTIPLY; break;
        case DIVIDE: opType = BinaryOpNode::OP_DIVIDE; break;
        default: throw std::runtime_error("Unknown binary operator");
    }
    return new BinaryOpNode(opType, static_cast<ExpressionNode*>(left), static_cast<ExpressionNode*>(right));
}

ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2) {
    auto call = new FunctionCallNode(std::string(name));
    if (arg1) call->arguments.push_back(static_cast<ExpressionNode*>(arg1));
    if (arg2) call->arguments.push_back(static_cast<ExpressionNode*>(arg2));
    return call;
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

std::string ProgramNode::toString() const {
    return "Program:\n" + functionList->toString();
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

std::string ParameterListNode::toString() const {
    std::string result;
    for (const auto& param : parameters) {
        if (!result.empty()) result += ", ";
        result += param.first->toString() + " " + param.second;
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

std::string VarDeclNode::toString() const {
    return type->toString() + " " + name + ";";
}

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
