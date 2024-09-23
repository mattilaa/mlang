%{
#include <stdio.h>
#include <stdlib.h>
#include "AST.h"

extern int yylex();
void yyerror(const char* s);
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
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA

%type <ast> program function_def type parameter_list parameters
%type <ast> statement_list statement expression

%start program

%%

program
    : function_def { /* Handle root of AST */ }
    ;

function_def
    : FUNCTION type IDENTIFIER LPAREN parameter_list RPAREN LBRACE statement_list RBRACE
    { /* Create function definition AST node */ }
    ;

type
    : VOID   { /* Create type AST node */ }
    | BOOL   { /* Create type AST node */ }
    | INT    { /* Create type AST node */ }
    | FLOAT  { /* Create type AST node */ }
    ;

parameter_list
    : parameters
    | /* empty */
    ;

parameters
    : type IDENTIFIER
    | parameters COMMA type IDENTIFIER
    ;

statement_list
    : statement
    | statement_list statement
    ;

statement
    : expression SEMICOLON
    | RETURN expression SEMICOLON
    | LBRACE statement_list RBRACE
    ;

expression
    : INT_LITERAL
    | FLOAT_LITERAL
    | IDENTIFIER
    | expression PLUS expression
    | expression MINUS expression
    | expression MULTIPLY expression
    | expression DIVIDE expression
    | LPAREN expression RPAREN
    | IDENTIFIER LPAREN RPAREN  // Function call without arguments
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Parse error: %s\n", s);
    exit(1);
}
