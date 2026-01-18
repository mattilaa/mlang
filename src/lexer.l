%{
#include "parser.hpp"
#include <string.h>
#include <cctype>
#include <string>

static int yyinput(void);
static void yyunput(int, char*);

static int is_generic_struct_literal_start()
{
    std::string buf;
    buf.reserve(64);
    bool saw_gt = false;
    bool saw_lbrace = false;
    int limit = 256;
    int c = 0;

    while(limit-- > 0 && (c = yyinput()) != 0)
    {
        buf.push_back((char)c);
        if(c == '\n' || c == ';')
            break;
        if(c == '>')
        {
            saw_gt = true;
            break;
        }
        if(!(std::isalnum(c) || c == '_' || c == ',' || std::isspace(c) ||
             c == '<'))
        {
            break;
        }
    }

    if(saw_gt)
    {
        while(limit-- > 0 && (c = yyinput()) != 0)
        {
            buf.push_back((char)c);
            if(std::isspace(c))
                continue;
            if(c == '{')
                saw_lbrace = true;
            break;
        }
    }

    for(int i = (int)buf.size() - 1; i >= 0; --i)
        unput(buf[(size_t)i]);

    return (saw_gt && saw_lbrace) ? 1 : 0;
}
%}

%option noyywrap
%option yylineno

%x COMMENT
%x STRING_

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
\"              { yylval.sval = strdup(""); BEGIN(STRING_); }
<STRING_>{
    \"          { BEGIN(INITIAL); return STRING_LITERAL; }
    \\n         { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + 2);
                  strcat(yylval.sval, "\n"); }
    \\t         { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + 2);
                  strcat(yylval.sval, "\t"); }
    \\r         { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + 2);
                  strcat(yylval.sval, "\r"); }
    \\\"        { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + 2);
                  strcat(yylval.sval, "\""); }
    \\\\        { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + 2);
                  strcat(yylval.sval, "\\"); }
    \\0         { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + 2);
                  strcat(yylval.sval, "\0"); }
    [^\\\n\"]+  { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + strlen(yytext) + 1);
                  strcat(yylval.sval, yytext); }
}
"pub"           { return PUB; }
"fn"            { return FUNCTION; }
"impl"          { return IMPL; }
"return"        { return RETURN; }
"if"            { return IF; }
"else"          { return ELSE; }
"for"           { return FOR; }
"in"            { return IN; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }
"mod"           { return MOD; }
"use"           { return USE; }
"let"           { return LET; }
"var"           { return VAR; }
"void"          { return VOID; }
"bool"          { return BOOL; }
"true"          { return TRUE_LIT; }
"false"         { return FALSE_LIT; }
"int"           { return INT; }
"float"         { return FLOAT; }
"double"        { return DOUBLE; }
"string"        { return STRING; }
"list"          { return LIST; }
"map"           { return MAP; }
"tuple"         { return TUPLE; }
"struct"        { return STRUCT; }
"i8"            { return I8; }
"i16"           { return I16; }
"i32"           { return I32; }
"i64"           { return I64; }
"u8"            { return U8; }
"u16"           { return U16; }
"u32"           { return U32; }
"u64"           { return U64; }
"println!"      { return PRINTLN; }
"print!"        { return PRINT; }
"eprintln!"     { return EPRINTLN; }
"eprint!"       { return EPRINT; }
"int("          { return CAST_INT; }
"float("        { return CAST_FLOAT; }
"double("       { return CAST_DOUBLE; }
"->"            { return ARROW; }
"..="           { return DOTDOTEQ; }
".."            { return DOTDOT; }
"::"            { return COLONCOLON; }
"."             { return DOT; }
".keys()"       { return KEYS_METHOD; }
".values()"     { return VALUES_METHOD; }
".entries()"    { return ENTRIES_METHOD; }
"<"             { return is_generic_struct_literal_start() ? GENERIC_LT : LT; }
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
[0-9]+\.[0-9]+f { yylval.fval = atof(yytext); return FLOAT_LITERAL; }
[0-9]+\.[0-9]+  { yylval.dval = atof(yytext); return DOUBLE_LITERAL; }
[0-9]+          { yylval.ival = strtoll(yytext, NULL, 10); return INT_LITERAL; }
"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return MULTIPLY; }
"/"             { return DIVIDE; }
.               { printf("Unexpected character: %s\n", yytext); }
%%
