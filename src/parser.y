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
ASTNode* create_type_node(TypeNode::TypeKind type);
ASTNode* create_parameter_list();
ASTNode* add_parameter(ASTNode* list, ASTNode* type, char* name);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
ASTNode* create_var_decl(ASTNode* type, char* name);
ASTNode* create_assignment(char* name, ASTNode* expr);
ASTNode* create_return_stmt(ASTNode* expr);
ASTNode* create_int_literal(int value);
ASTNode* create_float_literal(float value);
ASTNode* create_identifier(char* name);
ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2);
ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_branch);
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
%token FUNCTION RETURN IF ELSE VOID BOOL INT FLOAT
%token PLUS MINUS MULTIPLY DIVIDE ASSIGN
%token LT GT LE GE EQ NE
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA ARROW COLON

%type <ast> program function_def_list function_def type parameter_list parameters
%type <ast> statement_list statement expression if_statement else_if_list else_if
%type <ast> optional_else

%left LT GT LE GE EQ NE
%left PLUS MINUS
%left MULTIPLY DIVIDE

%start program

%%

program
    : function_def_list { $$ = create_program($1); }
    ;

function_def_list
    : function_def { $$ = create_function_list($1); }
    | function_def_list function_def { $$ = add_function_to_list($1, $2); }
    ;

function_def
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
    { $$ = create_function_def($7, $2, $4, $9); }
    ;

type
    : VOID   { $$ = create_type_node(TypeNode::TYPE_VOID); }
    | BOOL   { $$ = create_type_node(TypeNode::TYPE_BOOL); }
    | INT    { $$ = create_type_node(TypeNode::TYPE_INT); }
    | FLOAT  { $$ = create_type_node(TypeNode::TYPE_FLOAT); }
    ;

parameter_list
    : parameters { $$ = $1; }
    | /* empty */ { $$ = create_parameter_list(); }
    ;

parameters
    : type IDENTIFIER { $$ = add_parameter(create_parameter_list(), $1, $2); }
    | parameters COMMA type IDENTIFIER { $$ = add_parameter($1, $3, $4); }
    ;

statement_list
    : statement { $$ = create_statement_list($1); }
    | statement_list statement { $$ = add_statement($1, $2); }
    ;

statement
    : type IDENTIFIER SEMICOLON { $$ = create_var_decl($1, $2); }
    | IDENTIFIER ASSIGN expression SEMICOLON { $$ = create_assignment($1, $3); }
    | expression SEMICOLON { $$ = $1; }
    | RETURN expression SEMICOLON { $$ = create_return_stmt($2); }
    | RETURN SEMICOLON { $$ = create_return_stmt(NULL); }
    | LBRACE statement_list RBRACE { $$ = $2; }
    | if_statement { $$ = $1; }
    ;

if_statement
    : IF expression COLON LBRACE statement_list RBRACE else_if_list optional_else
    { $$ = create_if_statement($2, $5, create_if_statement($7, $8, NULL)); }
    ;

else_if_list
    : /* empty */ { $$ = NULL; }
    | else_if_list else_if { $$ = create_if_statement($2, NULL, $1); }
    ;

else_if
    : ELSE IF expression COLON LBRACE statement_list RBRACE { $$ = create_if_statement($3, $6, NULL); }
    ;

optional_else
    : /* empty */ { $$ = NULL; }
    | ELSE LBRACE statement_list RBRACE { $$ = $3; }
    ;

expression
    : INT_LITERAL { $$ = create_int_literal($1); }
    | FLOAT_LITERAL { $$ = create_float_literal($1); }
    | IDENTIFIER { $$ = create_identifier($1); }
    | expression PLUS expression {
        $$ = create_binary_op(PLUS, $1, $3);
        if (!$$) YYERROR;
    }
    | expression MINUS expression {
        $$ = create_binary_op(MINUS, $1, $3);
        if (!$$) YYERROR;
    }
    | expression MULTIPLY expression {
        $$ = create_binary_op(MULTIPLY, $1, $3);
        if (!$$) YYERROR;
    }
    | expression DIVIDE expression {
        $$ = create_binary_op(DIVIDE, $1, $3);
        if (!$$) YYERROR;
    }
    | expression LT expression {
        $$ = create_binary_op(LT, $1, $3);
        if (!$$) YYERROR;
    }
    | expression GT expression {
        $$ = create_binary_op(GT, $1, $3);
        if (!$$) YYERROR;
    }
    | expression LE expression {
        $$ = create_binary_op(LE, $1, $3);
        if (!$$) YYERROR;
    }
    | expression GE expression {
        $$ = create_binary_op(GE, $1, $3);
        if (!$$) YYERROR;
    }
    | expression EQ expression {
        $$ = create_binary_op(EQ, $1, $3);
        if (!$$) YYERROR;
    }
    | expression NE expression {
        $$ = create_binary_op(NE, $1, $3);
        if (!$$) YYERROR;
    }
    | LPAREN expression RPAREN { $$ = $2; }
    | IDENTIFIER LPAREN RPAREN { $$ = create_function_call($1, NULL, NULL); }
    | IDENTIFIER LPAREN expression RPAREN { $$ = create_function_call($1, $3, NULL); }
    | IDENTIFIER LPAREN expression COMMA expression RPAREN { $$ = create_function_call($1, $3, $5); }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Parse error at line %d: %s\n", yylineno, s);
    fprintf(stderr, "Near token: %s\n", yytext);
    exit(1);
}
