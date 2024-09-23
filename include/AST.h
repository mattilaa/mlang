#ifndef AST_H
#define AST_H

#include <string>
#include <vector>
#include <memory>

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual std::string toString() const = 0;
};

class TypeNode : public ASTNode {
public:
    enum TypeKind { TYPE_VOID, TYPE_BOOL, TYPE_INT, TYPE_FLOAT };
    TypeKind kind;

    TypeNode(TypeKind k) : kind(k) {}
    std::string toString() const override;
};

class ExpressionNode : public ASTNode {};

class StatementNode : public ASTNode {};

class ParameterNode : public ASTNode {
public:
    TypeNode* type;
    std::string name;

    ParameterNode(TypeNode* t, const std::string& n) : type(t), name(n) {}
    std::string toString() const override;
};

class ParameterListNode : public ASTNode {
public:
    std::vector<ParameterNode*> parameters;
    std::string toString() const override;
};

class FunctionDefNode : public ASTNode {
public:
    TypeNode* returnType;
    std::string name;
    ParameterListNode* parameters;
    class StatementListNode* body;

    FunctionDefNode(TypeNode* type, const std::string& n, ParameterListNode* params, StatementListNode* b)
        : returnType(type), name(n), parameters(params), body(b) {}
    std::string toString() const override;
};

class FunctionListNode : public ASTNode {
public:
    std::vector<FunctionDefNode*> functions;
    std::string toString() const override;
};

class ProgramNode : public ASTNode {
public:
    FunctionListNode* functionList;

    ProgramNode(FunctionListNode* list) : functionList(list) {}
    std::string toString() const override;
};

class StatementListNode : public ASTNode {
public:
    std::vector<StatementNode*> statements;
    std::string toString() const override;
};

/*
class VarDeclNode : public StatementNode {
public:
    TypeNode* type;
    std::string name;

    VarDeclNode(TypeNode* t, const std::string& n) : type(t), name(n) {}
    std::string toString() const override;
};
*/

class AssignmentNode : public StatementNode {
public:
    std::string name;
    ExpressionNode* expression;

    AssignmentNode(const std::string& n, ExpressionNode* expr) : name(n), expression(expr) {}
    std::string toString() const override;
};

class ReturnNode : public StatementNode {
public:
    ExpressionNode* expression;

    ReturnNode(ExpressionNode* expr) : expression(expr) {}
    std::string toString() const override;
};

class IntLiteralNode : public ExpressionNode {
public:
    int value;

    IntLiteralNode(int v) : value(v) {}
    std::string toString() const override;
};

class FloatLiteralNode : public ExpressionNode {
public:
    float value;

    FloatLiteralNode(float v) : value(v) {}
    std::string toString() const override;
};

class IdentifierNode : public ExpressionNode {
public:
    std::string name;

    IdentifierNode(const std::string& n) : name(n) {}
    std::string toString() const override;
};

class BinaryOpNode : public ExpressionNode {
public:
    enum OpType { OP_PLUS, OP_MINUS, OP_MULTIPLY, OP_DIVIDE, OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE };
    OpType op;
    ExpressionNode* left;
    ExpressionNode* right;

    BinaryOpNode(OpType o, ExpressionNode* l, ExpressionNode* r) : op(o), left(l), right(r) {}
    std::string toString() const override;
};

class FunctionCallNode : public ExpressionNode {
public:
    std::string name;
    std::vector<ExpressionNode*> arguments;

    FunctionCallNode(const std::string& n) : name(n) {}
    std::string toString() const override;
};

class IfNode : public StatementNode {
public:
    ExpressionNode* condition;
    StatementListNode* thenBranch;
    IfNode* elseIfBranch;  // For else if
    StatementListNode* elseBranch;  // For final else

    IfNode(ExpressionNode* cond, StatementListNode* then, IfNode* elseIf, StatementListNode* else_)
        : condition(cond), thenBranch(then), elseIfBranch(elseIf), elseBranch(else_) {}

    std::string toString() const override;
};

class LetDeclNode : public StatementNode {
public:
    TypeNode* type;
    std::string name;
    ExpressionNode* expression;

    LetDeclNode(TypeNode* t, const std::string& n, ExpressionNode* expr)
        : type(t), name(n), expression(expr) {}
    std::string toString() const override;
};

class VarDeclNode : public StatementNode {
public:
    TypeNode* type;
    std::string name;
    ExpressionNode* expression;

    VarDeclNode(TypeNode* t, const std::string& n, ExpressionNode* expr)
        : type(t), name(n), expression(expr) {}
    std::string toString() const override;
};

// Function declarations for AST node creation
ASTNode* create_program(ASTNode* function_list);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body);
ASTNode* create_type_node(TypeNode::TypeKind type);
ASTNode* create_parameter_list();
ASTNode* create_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* param);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
//ASTNode* create_var_decl(ASTNode* type, char* name);
ASTNode* create_assignment(char* name, ASTNode* expr);
ASTNode* create_return_stmt(ASTNode* expr);
ASTNode* create_int_literal(int value);
ASTNode* create_float_literal(float value);
ASTNode* create_identifier(char* name);
ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2);
ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr);


#endif // AST_H
