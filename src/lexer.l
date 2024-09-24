%{
#include "parser.hpp"
#include <string.h>
%}

%option noyywrap
%option yylineno

%x COMMENT

%%
[ \t\n]+        { /* Ignore whitespace */ }
"//".*          { /* Ignore single-line comments */ }
"/*"            { BEGIN(COMMENT); }
<COMMENT>{
    "*/"        { BEGIN(INITIAL); }
    [^*\n]+     { /* Ignore anything that's not a '*' */ }
    "*"         { /* Ignore '*'s that aren't followed by '/' */ }
    \n          { /* Ignore newlines */ }
}
"fn"            { return FUNCTION; }
"return"        { return RETURN; }
"if"            { return IF; }
"else"          { return ELSE; }
"let"           { return LET; }
"var"           { return VAR; }
"void"          { return VOID; }
"bool"          { return BOOL; }
"int"           { return INT; }
"float"         { return FLOAT; }
"double"        { return DOUBLE; }
"list"          { return LIST; }
"struct"        { return STRUCT; }
"int("          { return CAST_INT; }
"float("        { return CAST_FLOAT; }
"double("       { return CAST_DOUBLE; }
"->"            { return ARROW; }
"<"             { return LT; }
">"             { return GT; }
"<="            { return LE; }
">="            { return GE; }
"=="            { return EQ; }
"!="            { return NE; }
":"             { return COLON; }
";"             { return SEMICOLON; }
"{"             { return LBRACE; }
"}"             { return RBRACE; }
"("             { return LPAREN; }
")"             { return RPAREN; }
"["             { return LBRACKET; }
"]"             { return RBRACKET; }
"="             { return ASSIGN; }
","             { return COMMA; }
[a-zA-Z_][a-zA-Z0-9_]* { yylval.sval = strdup(yytext); return IDENTIFIER; }
[0-9]+\.[0-9]+  { yylval.fval = atof(yytext); return FLOAT_LITERAL; }
[0-9]+          { yylval.ival = atoi(yytext); return INT_LITERAL; }
"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return MULTIPLY; }
"/"             { return DIVIDE; }
.               { printf("Unexpected character: %s\n", yytext); }
%%
