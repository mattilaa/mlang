%{
#include <stdio.h>
#include <stdlib.h>
#include "AST.h"

extern int yylex();
extern int yylineno;
extern char* yytext;
void yyerror(const char* s);

// Function prototypes for AST node creation
// ... (keep existing prototypes)
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
%token FUNCTION RETURN VOID BOOL INT FLOAT
%token PLUS MINUS MULTIPLY DIVIDE ASSIGN
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA ARROW

%type <ast> program function_def_list function_def type parameter_list parameters
%type <ast> statement_list statement expression

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
    ;

expression
    : INT_LITERAL { $$ = create_int_literal($1); }
    | FLOAT_LITERAL { $$ = create_float_literal($1); }
    | IDENTIFIER { $$ = create_identifier($1); }
    | expression PLUS expression { $$ = create_binary_op(PLUS, $1, $3); }
    | expression MINUS expression { $$ = create_binary_op(MINUS, $1, $3); }
    | expression MULTIPLY expression { $$ = create_binary_op(MULTIPLY, $1, $3); }
    | expression DIVIDE expression { $$ = create_binary_op(DIVIDE, $1, $3); }
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
