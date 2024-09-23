%{
#include "parser.hpp"
#include <string.h>
%}

%option noyywrap

%%
[ \t\n]+        { /* Ignore whitespace */ }
"function"      { return FUNCTION; }
"return"        { return RETURN; }
"void"          { return VOID; }
"bool"          { return BOOL; }
"int"           { return INT; }
"float"         { return FLOAT; }
[a-zA-Z_][a-zA-Z0-9_]* { yylval.sval = strdup(yytext); return IDENTIFIER; }
[0-9]+\.[0-9]+  { yylval.fval = atof(yytext); return FLOAT_LITERAL; }
[0-9]+          { yylval.ival = atoi(yytext); return INT_LITERAL; }
"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return MULTIPLY; }
"/"             { return DIVIDE; }
"="             { return ASSIGN; }
"{"             { return LBRACE; }
"}"             { return RBRACE; }
"("             { return LPAREN; }
")"             { return RPAREN; }
";"             { return SEMICOLON; }
","             { return COMMA; }
.               { printf("Unexpected character: %s\n", yytext); }
%%
