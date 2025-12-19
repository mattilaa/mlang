#ifndef AST_H
#define AST_H

#include <string>
#include <vector>

// Forward declarations
class ModDeclNode;
class UseDeclNode;

class ASTNode
{
public:
    int line = 0; // Line number where this node appears in source

    virtual ~ASTNode() = default;
    virtual std::string toString() const = 0;
};

class TypeNode : public ASTNode
{
public:
    enum TypeKind
    {
        TYPE_VOID,
        TYPE_BOOL,
        TYPE_INT,
        TYPE_FLOAT,
        TYPE_DOUBLE,
        TYPE_STRING,
        TYPE_LIST
    };
    TypeKind kind;

    TypeNode(TypeKind k) : kind(k) {}
    std::string toString() const override;
};

class ExpressionNode : public ASTNode
{
};

class StatementNode : public ASTNode
{
};

class ParameterNode : public ASTNode
{
public:
    TypeNode* type;
    std::string name;

    ParameterNode(TypeNode* t, const std::string& n) : type(t), name(n) {}
    std::string toString() const override;
};

class ParameterListNode : public ASTNode
{
public:
    std::vector<ParameterNode*> parameters;
    std::string toString() const override;
};

class FunctionDefNode : public ASTNode
{
public:
    TypeNode* returnType;
    std::string name;
    ParameterListNode* parameters;
    class StatementListNode* body;

    FunctionDefNode(TypeNode* type, const std::string& n,
                    ParameterListNode* params, StatementListNode* b)
        : returnType(type), name(n), parameters(params), body(b)
    {
    }
    std::string toString() const override;
};

class FunctionListNode : public ASTNode
{
public:
    std::vector<FunctionDefNode*> functions;
    std::string toString() const override;
};

class StatementListNode : public ASTNode
{
public:
    std::vector<StatementNode*> statements;
    std::string toString() const override;
};

class StructMemberNode : public ASTNode
{
public:
    bool isVar;
    TypeNode* type;
    std::string name;
    ExpressionNode* initExpr;

    StructMemberNode(bool isVar, TypeNode* type, const std::string& name,
                     ExpressionNode* initExpr)
        : isVar(isVar), type(type), name(name), initExpr(initExpr)
    {
    }
    std::string toString() const override;
};

class StructMemberListNode : public ASTNode
{
public:
    std::vector<StructMemberNode*> members;

    void addMember(StructMemberNode* member)
    {
        members.push_back(member);
    }

    std::string toString() const override;
};

class StructDefNode : public ASTNode
{
public:
    std::string name;
    std::string baseName;
    StructMemberListNode* members;

    StructDefNode(const std::string& name, const std::string& baseName,
                  StructMemberListNode* members)
        : name(name), baseName(baseName), members(members)
    {
    }
    std::string toString() const override;
};

class StructListNode : public ASTNode
{
public:
    std::vector<StructDefNode*> structs;

    void addStruct(StructDefNode* structDef)
    {
        structs.push_back(structDef);
    }

    std::string toString() const override;
};

class TopLevelListNode : public ASTNode
{
public:
    std::vector<ASTNode*> items;
    std::string toString() const override;
};

class ProgramNode : public ASTNode
{
public:
    StructListNode* structList;
    FunctionListNode* functionList;
    std::vector<ModDeclNode*> modules;
    std::vector<UseDeclNode*> imports;

    ProgramNode() : structList(nullptr), functionList(nullptr) {}
    std::string toString() const override;
};

class AssignmentNode : public StatementNode
{
public:
    std::string name;
    ExpressionNode* expression;

    AssignmentNode(const std::string& n, ExpressionNode* expr)
        : name(n), expression(expr)
    {
    }
    std::string toString() const override;
};

class ReturnNode : public StatementNode
{
public:
    ExpressionNode* expression;

    ReturnNode(ExpressionNode* expr) : expression(expr) {}
    std::string toString() const override;
};

class StructInitNode : public StatementNode
{
public:
    std::string typeName;
    std::string varName;

    StructInitNode(const std::string& type, const std::string& var)
        : typeName(type), varName(var)
    {
    }
    std::string toString() const override;
};

class IntLiteralNode : public ExpressionNode
{
public:
    int value;

    IntLiteralNode(int v) : value(v) {}
    std::string toString() const override;
};

class FloatLiteralNode : public ExpressionNode
{
public:
    float value;

    FloatLiteralNode(float v) : value(v) {}
    std::string toString() const override;
};

class DoubleLiteralNode : public ExpressionNode
{
public:
    double value;

    DoubleLiteralNode(double v) : value(v) {}
    std::string toString() const override;
};

class StringLiteralNode : public ExpressionNode
{
public:
    std::string value;

    StringLiteralNode(const std::string& v) : value(v) {}
    std::string toString() const override;
};

class IdentifierNode : public ExpressionNode
{
public:
    std::string name;

    IdentifierNode(const std::string& n) : name(n) {}
    std::string toString() const override;
};

class BinaryOpNode : public ExpressionNode
{
public:
    enum OpType
    {
        OP_PLUS,
        OP_MINUS,
        OP_MULTIPLY,
        OP_DIVIDE,
        OP_LT,
        OP_GT,
        OP_LE,
        OP_GE,
        OP_EQ,
        OP_NE
    };
    OpType op;
    ExpressionNode* left;
    ExpressionNode* right;

    BinaryOpNode(OpType o, ExpressionNode* l, ExpressionNode* r)
        : op(o), left(l), right(r)
    {
    }
    std::string toString() const override;
};

class FunctionCallNode : public ExpressionNode
{
public:
    std::string name;
    std::vector<ExpressionNode*> arguments;

    FunctionCallNode(const std::string& n) : name(n) {}
    std::string toString() const override;
};

class CastExpressionNode : public ExpressionNode
{
public:
    TypeNode::TypeKind targetType;
    ExpressionNode* expression;

    CastExpressionNode(TypeNode::TypeKind type, ExpressionNode* expr)
        : targetType(type), expression(expr)
    {
    }
    std::string toString() const override;
};

class ListTypeNode : public TypeNode
{
public:
    ListTypeNode() : TypeNode(TypeNode::TYPE_LIST) {}
    std::string toString() const override
    {
        return "list";
    }
};

class ListElementsNode : public ASTNode
{
public:
    std::vector<ExpressionNode*> elements;

    void addElement(ExpressionNode* element)
    {
        elements.push_back(element);
    }

    std::string toString() const override;
};

class ListLiteralNode : public ExpressionNode
{
public:
    ListElementsNode* elements;

    explicit ListLiteralNode(ListElementsNode* elems) : elements(elems) {}
    std::string toString() const override;
};

class IfNode : public StatementNode
{
public:
    ExpressionNode* condition;
    StatementListNode* thenBranch;
    IfNode* elseIfBranch;          // For else if
    StatementListNode* elseBranch; // For final else

    IfNode(ExpressionNode* cond, StatementListNode* then,
           IfNode* elseIf = nullptr, StatementListNode* else_ = nullptr)
        : condition(cond), thenBranch(then), elseIfBranch(elseIf),
          elseBranch(else_)
    {
    }

    std::string toString() const override;
};

class LetDeclNode : public StatementNode
{
public:
    TypeNode* type;
    std::string name;
    ExpressionNode* expression;

    LetDeclNode(TypeNode* t, const std::string& n, ExpressionNode* expr)
        : type(t), name(n), expression(expr)
    {
    }
    std::string toString() const override;
};

class VarDeclNode : public StatementNode
{
public:
    TypeNode* type;
    std::string name;
    ExpressionNode* initExpr;

    VarDeclNode(TypeNode* t, const std::string& n,
                ExpressionNode* expr = nullptr)
        : type(t), name(n), initExpr(expr)
    {
    }
    std::string toString() const override;
};

class ExpressionStatementNode : public StatementNode
{
public:
    ExpressionNode* expression;

    ExpressionStatementNode(ExpressionNode* expr) : expression(expr) {}
    virtual ~ExpressionStatementNode() = default;
    std::string toString() const override;
};

class BlockStatementNode : public StatementNode
{
public:
    StatementListNode* statements;

    BlockStatementNode(StatementListNode* stmts) : statements(stmts) {}
    virtual ~BlockStatementNode() = default;
    std::string toString() const override;
};

// Range expression: start..end
class RangeExpressionNode : public ExpressionNode
{
public:
    ExpressionNode* start;
    ExpressionNode* end;
    bool inclusive; // false for .., true for ..= (future)

    RangeExpressionNode(ExpressionNode* s, ExpressionNode* e, bool incl = false)
        : start(s), end(e), inclusive(incl)
    {
    }
    virtual ~RangeExpressionNode() = default;
    std::string toString() const override;
};

// For loop: for var in range/iterable { body }
class ForNode : public StatementNode
{
public:
    std::string varName;
    ExpressionNode* iterable; // Can be RangeExpressionNode or other iterable
    StatementListNode* body;

    ForNode(const std::string& var, ExpressionNode* iter, StatementListNode* b)
        : varName(var), iterable(iter), body(b)
    {
    }
    virtual ~ForNode() = default;
    std::string toString() const override;
};

// Module declaration: mod module_name;
class ModDeclNode : public ASTNode
{
public:
    std::string moduleName;
    std::string filePath; // Resolved file path

    ModDeclNode(const std::string& name) : moduleName(name) {}
    virtual ~ModDeclNode() = default;
    std::string toString() const override;
};

// Use declaration: use module_name::item; or use module_name::*;
class UseDeclNode : public ASTNode
{
public:
    std::string moduleName;
    std::string itemName; // Empty or "*" means import all
    bool importAll;

    UseDeclNode(const std::string& mod, const std::string& item,
                bool all = false)
        : moduleName(mod), itemName(item), importAll(all)
    {
    }
    virtual ~UseDeclNode() = default;
    std::string toString() const override;
};

// Print statement: println!("format", args...) or eprintln!("format", args...)
// Rust-like print macros for stdout and stderr
class PrintNode : public StatementNode
{
public:
    enum PrintKind
    {
        PRINT_STDOUT,   // print! - no newline
        PRINTLN_STDOUT, // println! - with newline
        PRINT_STDERR,   // eprint! - no newline
        EPRINTLN_STDERR // eprintln! - with newline
    };

    PrintKind kind;
    std::string formatString;
    std::vector<ExpressionNode*> arguments;

    PrintNode(PrintKind k, const std::string& fmt) : kind(k), formatString(fmt)
    {
    }

    void addArgument(ExpressionNode* arg)
    {
        arguments.push_back(arg);
    }

    virtual ~PrintNode() = default;
    std::string toString() const override;
};

// Function declarations for AST node creation
ASTNode* create_program(ASTNode* top_level_list);
ASTNode* create_top_level_list(ASTNode* item);
ASTNode* add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* create_struct_list(ASTNode* struct_def);
ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params,
                             ASTNode* body);
ASTNode* create_type_node(TypeNode::TypeKind type);
ASTNode* create_parameter_list(ASTNode* param = nullptr);
ASTNode* create_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* param);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
ASTNode* create_assignment(char* name, ASTNode* expr, int line = 0);
ASTNode* create_return_stmt(ASTNode* expr);
ASTNode* create_struct_init(char* type_name, char* var_name);
ASTNode* create_int_literal(int value);
ASTNode* create_float_literal(float value);
ASTNode* create_double_literal(double value);
ASTNode* create_string_literal(char* value);
ASTNode* create_identifier(char* name);
ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2);
ASTNode* create_function_call_multi(char* name, ASTNode* args);
ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch,
                             ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_cast_expression(int type, ASTNode* expr);
ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members);
ASTNode* create_struct_member_list(ASTNode* member);
ASTNode* add_struct_member(ASTNode* list, ASTNode* member);
ASTNode* create_struct_member(int is_var, ASTNode* type, char* name,
                              ASTNode* init_expr);
ASTNode* create_list_type();
ASTNode* create_list_literal(ASTNode* elements);
ASTNode* create_list_element_list(ASTNode* element);
ASTNode* add_list_element(ASTNode* list, ASTNode* element);
ASTNode* create_expression_statement(ASTNode* expr);
ASTNode* create_block_statement(ASTNode* stmt_list);
ASTNode* create_else_if(ASTNode* condition, ASTNode* body);
ASTNode* add_else_if(ASTNode* else_if_list, ASTNode* else_if);
ASTNode* create_for_range(char* var_name, ASTNode* range, ASTNode* body,
                          int line);
ASTNode* create_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body,
                             int line);
ASTNode* create_range_expression(ASTNode* start, ASTNode* end, int inclusive);
ASTNode* create_mod_declaration(char* name, int line);
ASTNode* create_use_declaration(char* module_name, char* item_name, int line);
ASTNode* create_use_all_declaration(char* module_name, int line);

// Print macro AST node creators
ASTNode* create_print_stmt(int kind, char* format_str, ASTNode* args, int line);
ASTNode* create_argument_list(ASTNode* arg);
ASTNode* add_argument(ASTNode* list, ASTNode* arg);

#endif // AST_H
