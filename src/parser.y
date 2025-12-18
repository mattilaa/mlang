%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern "C" {
    extern ASTNode* programRoot;
}
extern int yylex();
extern int yylineno;
extern char* yytext;
void yyerror(const char* s);

// Function prototypes for AST node creation
ASTNode* create_program(ASTNode* top_level_list);
ASTNode* create_top_level_list(ASTNode* item);
ASTNode* add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* create_struct_list(ASTNode* struct_def);
ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body);
ASTNode* create_type_node(int type);
ASTNode* create_parameter_list();
ASTNode* create_empty_parameter_list();
ASTNode* create_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* param);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
ASTNode* create_assignment(char* name, ASTNode* expr);
ASTNode* create_return_stmt(ASTNode* expr);
ASTNode* create_int_literal(int value);
ASTNode* create_float_literal(float value);
ASTNode* create_double_literal(float value);
ASTNode* create_string_literal(char* value);
ASTNode* create_identifier(char* name);
ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2);
ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_cast_expression(int type, ASTNode* expr);
ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members);
ASTNode* create_struct_member_list(ASTNode* member);
ASTNode* add_struct_member(ASTNode* list, ASTNode* member);
ASTNode* create_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* create_struct_init(char* type_name, char* var_name);
ASTNode* create_list_type();
ASTNode* create_list_literal(ASTNode* elements);
ASTNode* create_list_element_list(ASTNode* element);
ASTNode* add_list_element(ASTNode* list, ASTNode* element);
%}

%union {
    int ival;
    float fval;
    double dval;
    char* sval;
    struct ASTNode* ast;
}

%token <sval> IDENTIFIER STRING_LITERAL
%token <ival> INT_LITERAL
%token <fval> FLOAT_LITERAL
%token <dval> DOUBLE_LITERAL
%token FUNCTION RETURN IF ELSE VOID BOOL INT FLOAT DOUBLE STRING LIST STRUCT
%token LET VAR
%token PLUS MINUS MULTIPLY DIVIDE ASSIGN
%token LT GT LE GE EQ NE
%token LBRACE RBRACE LPAREN RPAREN LBRACKET RBRACKET SEMICOLON COMMA ARROW COLON
%token CAST_INT CAST_FLOAT CAST_DOUBLE

%type <ast> program top_level_list top_level_item
%type <ast> struct_def function_def type parameter_list parameters parameter
%type <ast> statement_list statement expression cast_expression
%type <ast> if_statement else_if_list else_if optional_else
%type <ast> struct_member_list struct_member struct_init
%type <ast> list_literal list_elements
%type <ast> let_statement var_statement assignment_statement expression_statement
%type <ast> return_statement block_statement
%type <ast> primary_expression binary_expression function_call

%left LT GT LE GE EQ NE
%left PLUS MINUS
%left MULTIPLY DIVIDE

%start program

%%

program
    : top_level_list {
        $$ = create_program($1);
        programRoot = $$;
    }
    ;

top_level_list
    : top_level_item { $$ = create_top_level_list($1); }
    | top_level_list top_level_item { $$ = add_to_top_level_list($1, $2); }
    ;

top_level_item
    : struct_def
    | function_def
    ;

struct_def
    : STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { $$ = create_struct_def($2, NULL, $4); }
    | STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { $$ = create_struct_def($2, $4, $6); }
    ;

struct_member_list
    : struct_member { $$ = create_struct_member_list($1); }
    | struct_member_list struct_member { $$ = add_struct_member($1, $2); }
    ;

struct_member
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_struct_member(0, $4, $2, $6); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = create_struct_member(1, $4, $2, NULL); }
    ;

function_def
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { $$ = create_function_def($7, $2, $4, $9); }
    ;

parameter_list
    : /* empty */ { $$ = create_empty_parameter_list(); }
    | parameters
    ;

parameters
    : parameter { $$ = create_parameter_list($1); }
    | parameters COMMA parameter { $$ = add_parameter($1, $3); }
    ;

parameter
    : IDENTIFIER COLON type { $$ = create_parameter($3, $1); }
    ;

type
    : VOID   { $$ = create_type_node(TypeNode::TYPE_VOID); }
    | BOOL   { $$ = create_type_node(TypeNode::TYPE_BOOL); }
    | INT    { $$ = create_type_node(TypeNode::TYPE_INT); }
    | FLOAT  { $$ = create_type_node(TypeNode::TYPE_FLOAT); }
    | DOUBLE { $$ = create_type_node(TypeNode::TYPE_DOUBLE); }
    | STRING { $$ = create_type_node(TypeNode::TYPE_STRING); }
    | LIST   { $$ = create_list_type(); }
    ;

statement_list
    : statement { $$ = create_statement_list($1); }
    | statement_list statement { $$ = add_statement($1, $2); }
    ;

statement
    : let_statement
    | var_statement
    | assignment_statement
    | expression_statement
    | return_statement
    | if_statement
    | block_statement
    | struct_init
    ;

let_statement
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_let_declaration($4, $2, $6); }
    ;

var_statement
    : VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_var_declaration($4, $2, $6); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = create_var_declaration($4, $2, NULL); }
    ;

assignment_statement
    : IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_assignment($1, $3); }
    ;

expression_statement
    : expression SEMICOLON { $$ = create_expression_statement($1); }
    ;

return_statement
    : RETURN expression SEMICOLON { $$ = create_return_stmt($2); }
    | RETURN SEMICOLON { $$ = create_return_stmt(NULL); }
    ;

block_statement
    : LBRACE statement_list RBRACE { $$ = create_block_statement($2); }
    ;

struct_init
    : IDENTIFIER IDENTIFIER SEMICOLON
        { $$ = create_struct_init($1, $2); }
    ;

if_statement
    : IF expression COLON block_statement else_if_list optional_else
        { $$ = create_if_statement($2, $4, $5, $6); }
    | IF expression COLON statement else_if_list optional_else
        { $$ = create_if_statement($2, create_statement_list($4), $5, $6); }
    ;

else_if_list
    : /* empty */ { $$ = NULL; }
    | else_if_list else_if { $$ = add_else_if($1, $2); }
    ;

else_if
    : ELSE IF expression COLON block_statement { $$ = create_else_if($3, $5); }
    | ELSE IF expression COLON statement { $$ = create_else_if($3, create_statement_list($5)); }
    ;

optional_else
    : /* empty */ { $$ = NULL; }
    | ELSE COLON block_statement { $$ = $3; }
    | ELSE COLON statement { $$ = create_statement_list($3); }
    ;

expression
    : primary_expression
    | binary_expression
    | function_call
    | cast_expression
    | list_literal
    ;

primary_expression
    : INT_LITERAL { $$ = create_int_literal($1); }
    | FLOAT_LITERAL { $$ = create_float_literal($1); }
    | DOUBLE_LITERAL { $$ = create_double_literal($1); }
    | STRING_LITERAL { $$ = create_string_literal($1); }
    | IDENTIFIER { $$ = create_identifier($1); }
    | LPAREN expression RPAREN { $$ = $2; }
    ;

binary_expression
    : expression PLUS expression { $$ = create_binary_op(PLUS, $1, $3); }
    | expression MINUS expression { $$ = create_binary_op(MINUS, $1, $3); }
    | expression MULTIPLY expression { $$ = create_binary_op(MULTIPLY, $1, $3); }
    | expression DIVIDE expression { $$ = create_binary_op(DIVIDE, $1, $3); }
    | expression LT expression { $$ = create_binary_op(LT, $1, $3); }
    | expression GT expression { $$ = create_binary_op(GT, $1, $3); }
    | expression LE expression { $$ = create_binary_op(LE, $1, $3); }
    | expression GE expression { $$ = create_binary_op(GE, $1, $3); }
    | expression EQ expression { $$ = create_binary_op(EQ, $1, $3); }
    | expression NE expression { $$ = create_binary_op(NE, $1, $3); }
    ;

function_call
    : IDENTIFIER LPAREN RPAREN { $$ = create_function_call($1, NULL, NULL); }
    | IDENTIFIER LPAREN expression RPAREN { $$ = create_function_call($1, $3, NULL); }
    | IDENTIFIER LPAREN expression COMMA expression RPAREN { $$ = create_function_call($1, $3, $5); }
    ;

cast_expression
    : CAST_INT expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_INT, $2); }
    | CAST_FLOAT expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_FLOAT, $2); }
    | CAST_DOUBLE expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_DOUBLE, $2); }
    ;

list_literal
    : LBRACKET list_elements RBRACKET { $$ = create_list_literal($2); }
    | LBRACKET RBRACKET { $$ = create_list_literal(NULL); }
    ;

list_elements
    : expression { $$ = create_list_element_list($1); }
    | list_elements COMMA expression { $$ = add_list_element($1, $3); }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Error: %s\n", s);
}
