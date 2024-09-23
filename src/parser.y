%{
#include "AST.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <memory>

extern int yylex();
void yyerror(const char* s);

std::unique_ptr<FunctionDefinition> root;
%}

%union {
    bool bval;
    int ival;
    float fval;
    char* sval;
    FunctionDefinition* function;
    std::vector<std::pair<std::string, std::string>>* params;
    Statement* stmt;
    Expression* expr;
}

%token <sval> IDENTIFIER
%token <ival> INT_LITERAL
%token <fval> FLOAT_LITERAL
%token <bval> BOOL_LITERAL
%token FUNCTION RETURN VOID BOOL INT FLOAT
%token PLUS MINUS MULTIPLY DIVIDE ASSIGN
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA

%type <sval> type
%type <function> function_def
%type <params> parameter_list parameters
%type <stmt> statement
%type <expr> expression

%start program

%%

program
    : function_def { root = std::unique_ptr<FunctionDefinition>($1); }
    ;

function_def
    : FUNCTION type IDENTIFIER LPAREN parameter_list RPAREN LBRACE /* statement_list */ RBRACE
    {
        $$ = new FunctionDefinition();
        $$->returnType = $2;
        $$->name = $3;
        $$->parameters = *$5;
        delete $5;
        // Add statements to $$->body
    }
    ;

type
    : VOID   { $$ = strdup("void"); }
    | BOOL   { $$ = strdup("bool"); }
    | INT    { $$ = strdup("int"); }
    | FLOAT  { $$ = strdup("float"); }
    ;

parameter_list
    : parameters    { $$ = $1; }
    | /* empty */   { $$ = new std::vector<std::pair<std::string, std::string>>(); }
    ;

parameters
    : type IDENTIFIER {
        $$ = new std::vector<std::pair<std::string, std::string>>();
        $$->emplace_back($1, $2);
    }
    | parameters COMMA type IDENTIFIER {
        $$ = $1;
        $$->emplace_back($3, $4);
    }
    ;

/* Add rules for statements and expressions */

%%

void yyerror(const char* s) {
    fprintf(stderr, "Parse error: %s\n", s);
}
