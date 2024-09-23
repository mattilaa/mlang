%{
#include <stdio.h>
#include <stdlib.h>
#include "AST.h"

extern int yylex();
extern int yylineno;
extern char* yytext;
void yyerror(const char* s);

// Function prototypes for AST node creation
ASTNode* create_program(ASTNode* function_list);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body);
ASTNode* create_type_node(int type);
ASTNode* create_parameter_list();
ASTNode* create_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* type, char* name);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
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
ASTNode* create_cast_expression(int type, ASTNode* expr);
ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members);
ASTNode* create_struct_member_list(ASTNode* member);
ASTNode* add_struct_member(ASTNode* list, ASTNode* member);
ASTNode* create_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
%}

%union {
    int ival;
    float fval;
    char* sval;
    struct ASTNode* ast;
}

%token <sval> IDENTIFIER
%token <ival> INT_LITERAL
%token <fval> FLOAT_LITERAL
%token FUNCTION RETURN IF ELSE VOID BOOL INT FLOAT STRUCT
%token LET VAR
%token PLUS MINUS MULTIPLY DIVIDE ASSIGN
%token LT GT LE GE EQ NE
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA ARROW COLON
%token CAST_INT CAST_FLOAT

%type <ast> program function_def_list function_def type parameter_list parameters parameter
%type <ast> statement_list statement expression cast_expression
%type <ast> if_statement else_if_list else_if optional_else block_or_statement
%type <ast> struct_def struct_member_list struct_member

%left LT GT LE GE EQ NE
%left PLUS MINUS
%left MULTIPLY DIVIDE

%start program

%%

program
    : function_def_list { $$ = create_program($1); }
    | struct_def function_def_list { $$ = create_program($2); /* TODO: Add struct to program */ }
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

function_def_list
    : function_def { $$ = create_function_list($1); }
    | function_def_list function_def { $$ = add_function_to_list($1, $2); }
    ;

function_def
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
    { $$ = create_function_def($7, $2, $4, $9); }
    ;

parameter_list
    : parameters { $$ = $1; }
    | /* empty */ { $$ = create_parameter_list(); }
    ;

parameters
    : parameter { $$ = add_parameter(create_parameter_list(), $1); }
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
    ;

statement_list
    : statement { $$ = create_statement_list($1); }
    | statement_list statement { $$ = add_statement($1, $2); }
    ;

statement
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_let_declaration($4, $2, $6); }
    | VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_var_declaration($4, $2, $6); }
    | IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_assignment($1, $3); }
    | expression SEMICOLON
        { $$ = $1; }
    | RETURN expression SEMICOLON
        { $$ = create_return_stmt($2); }
    | RETURN SEMICOLON
        { $$ = create_return_stmt(NULL); }
    | if_statement
        { $$ = $1; }
    | LBRACE statement_list RBRACE
        { $$ = $2; }
    ;

if_statement
    : IF expression COLON block_or_statement else_if_list optional_else
    { $$ = create_if_statement($2, $4, $5, $6); }
    ;

else_if_list
    : /* empty */ { $$ = NULL; }
    | else_if_list else_if { $$ = create_if_statement($2, NULL, $1, NULL); }
    ;

else_if
    : ELSE IF expression COLON block_or_statement { $$ = create_if_statement($3, $5, NULL, NULL); }
    ;

optional_else
    : /* empty */ { $$ = NULL; }
    | ELSE COLON block_or_statement { $$ = $3; }
    ;

block_or_statement
    : LBRACE statement_list RBRACE { $$ = $2; }
    | statement { $$ = create_statement_list($1); }
    ;

expression
    : INT_LITERAL { $$ = create_int_literal($1); }
    | FLOAT_LITERAL { $$ = create_float_literal($1); }
    | IDENTIFIER { $$ = create_identifier($1); }
    | expression PLUS expression { $$ = create_binary_op(PLUS, $1, $3); }
    | expression MINUS expression { $$ = create_binary_op(MINUS, $1, $3); }
    | expression MULTIPLY expression { $$ = create_binary_op(MULTIPLY, $1, $3); }
    | expression DIVIDE expression { $$ = create_binary_op(DIVIDE, $1, $3); }
    | expression LT expression { $$ = create_binary_op(LT, $1, $3); }
    | expression GT expression { $$ = create_binary_op(GT, $1, $3); }
    | expression LE expression { $$ = create_binary_op(LE, $1, $3); }
    | expression GE expression { $$ = create_binary_op(GE, $1, $3); }
    | expression EQ expression { $$ = create_binary_op(EQ, $1, $3); }
    | expression NE expression { $$ = create_binary_op(NE, $1, $3); }
    | LPAREN expression RPAREN { $$ = $2; }
    | cast_expression { $$ = $1; }
    | IDENTIFIER LPAREN RPAREN { $$ = create_function_call($1, NULL, NULL); }
    | IDENTIFIER LPAREN expression RPAREN { $$ = create_function_call($1, $3, NULL); }
    | IDENTIFIER LPAREN expression COMMA expression RPAREN { $$ = create_function_call($1, $3, $5); }
    ;

cast_expression
    : CAST_INT expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_INT, $2); }
    | CAST_FLOAT expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_FLOAT, $2); }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Parse error at line %d: %s\n", yylineno, s);
    fprintf(stderr, "Near token: %s\n", yytext);
    exit(1);
}
