%{
#include "parser.hpp"
#include <string.h>
%}

%option noyywrap
%option yylineno

%%
[ \t\n]+        { /* Ignore whitespace */ }
"fn"            { return FUNCTION; }
"return"        { return RETURN; }
"if"            { return IF; }
"else"          { return ELSE; }
"void"          { return VOID; }
"bool"          { return BOOL; }
"int"           { return INT; }
"float"         { return FLOAT; }
"->"            { return ARROW; }
"<"             { return LT; }
">"             { return GT; }
"<="            { return LE; }
">="            { return GE; }
"=="            { return EQ; }
"!="            { return NE; }
":"             { return COLON; }
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
