%{
#include "parser.hpp"
#include "diagnostics.h"
#include "ast.h"
#include <string.h>
#include <cctype>
#include <cstdlib>
#include <string>

extern bool parseHadError;
extern const char* g_sourceFile;

// Parse the type-suffix portion of a typed integer literal (e.g. "i64",
// "u8") into a TypeNode::TypeKind. Returns TYPE_VOID for unrecognized
// suffixes so the caller can decide how to fail. The pointer passed in
// must point at the leading 'i' or 'u'.
static int mla_lex_int_suffix_kind(const char* suffix)
{
    if(!suffix || (suffix[0] != 'i' && suffix[0] != 'u'))
        return TypeNode::TYPE_VOID;
    bool isUnsigned = (suffix[0] == 'u');
    int bits = std::atoi(suffix + 1);
    switch(bits)
    {
        case 8:  return isUnsigned ? TypeNode::TYPE_U8  : TypeNode::TYPE_I8;
        case 16: return isUnsigned ? TypeNode::TYPE_U16 : TypeNode::TYPE_I16;
        case 32: return isUnsigned ? TypeNode::TYPE_U32 : TypeNode::TYPE_I32;
        case 64: return isUnsigned ? TypeNode::TYPE_U64 : TypeNode::TYPE_I64;
        default: return TypeNode::TYPE_VOID;
    }
}

// Build a typed-integer-literal AST node from a parsed value plus the
// suffix start in yytext. Returns the node as a generic ASTNode* so the
// parser's <ast> yylval slot can hold it.
static ASTNode* mla_lex_make_typed_int(int64_t value, const char* suffix)
{
    auto* node = new IntLiteralNode(value);
    int kind = mla_lex_int_suffix_kind(suffix);
    if(kind != TypeNode::TYPE_VOID)
        node->explicitType = static_cast<TypeNode::TypeKind>(kind);
    return node;
}

static void mla_lex_append_byte(char byte)
{
    size_t old_len = yylval.sval ? strlen(yylval.sval) : 0;
    yylval.sval = (char*)realloc(yylval.sval, old_len + 2);
    yylval.sval[old_len] = byte;
    yylval.sval[old_len + 1] = '\0';
}

static int mla_lex_hex_value(char ch)
{
    if(ch >= '0' && ch <= '9')
        return ch - '0';
    if(ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if(ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return 0;
}

int yycolumn = 1;       // current column (end of last-consumed token)
int yycolumn_token = 1; // column at start of the current token

#define YY_USER_ACTION                          \
    yycolumn_token = yycolumn;                  \
    for(int _yci = 0; _yci < yyleng; _yci++) { \
        if(yytext[_yci] == '\n') yycolumn = 1;  \
        else                     yycolumn++;    \
    }

static int yyinput(void);
static void yyunput(int, char*);
static int generic_depth = 0;
static int switch_case_header_depth = 0;

static int is_generic_struct_literal_start()
{
    std::string buf;
    buf.reserve(64);
    bool saw_gt = false;
    bool saw_generic_tail = false;
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
        if(!(std::isalnum(c) || c == '_' || c == ',' || c == ':' ||
             std::isspace(c) || c == '<' || c == '+'))
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
            if(c == '{' || c == '(' || c == ')' || c == ',' || c == ';' ||
               c == '=' || c == ':' || c == '.' || c == ']' || c == '>')
            {
                saw_generic_tail = true;
            }
            break;
        }
    }

    for(int i = (int)buf.size() - 1; i >= 0; --i)
        unput(buf[(size_t)i]);

    return (saw_gt && saw_generic_tail) ? 1 : 0;
}

static int is_try_question_context()
{
    std::string buf;
    buf.reserve(16);
    int c = 0;

    while((c = yyinput()) != 0)
    {
        buf.push_back((char)c);
        if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        break;
    }

    for(int i = (int)buf.size() - 1; i >= 0; --i)
        yyunput(buf[(size_t)i], yytext);

    if(c == 0 || c == ';' || c == ')' || c == ']' || c == '}' || c == ',' ||
       c == ':')
    {
        return 1;
    }
    return 0;
}
%}

%option noyywrap
%option yylineno

%x COMMENT
%x STRING_

%%
[ \t\n]+        { /* Ignore whitespace */ }
(\302\240)+     { /* Ignore UTF-8 non-breaking spaces (U+00A0) */ }
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
    \\x[0-9a-fA-F]{2} {
                  int value = (mla_lex_hex_value(yytext[2]) << 4) |
                              mla_lex_hex_value(yytext[3]);
                  mla_lex_append_byte(static_cast<char>(value));
                }
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
    \n          { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + 2);
                  strcat(yylval.sval, "\n"); }
    [^\\\n\"]+  { yylval.sval = (char*)realloc(yylval.sval, strlen(yylval.sval) + strlen(yytext) + 1);
                  strcat(yylval.sval, yytext); }
}
"pub"           { return PUB; }
"extern"        { return EXTERN; }
"static"        { return STATIC; }
"cexpr"         { return CEXPR; }
"generic"       { return GENERIC; }
"fn"            { return FUNCTION; }
"impl"          { return IMPL; }
"trait"         { return TRAIT; }
"dyn"           { return DYN; }
"return"        { return RETURN; }
"if"            { return IF; }
"else"          { return ELSE; }
"try"           { return TRY; }
"catch"         { return CATCH; }
"throw"         { return THROW; }
"match"         { return MATCH; }
"switch"        { return SWITCH; }
"case"          { switch_case_header_depth = 1; return CASE; }
"default"       { switch_case_header_depth = 1; return DEFAULT; }
"enum"          { return ENUM; }
"for"           { return FOR; }
"while"         { return WHILE; }
"unsafe"        { return UNSAFE; }
"size_of"       { return SIZEOF; }
"asm"           { return ASM; }
"volatile"      { return VOLATILE; }
"in"            { return IN; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }
"mod"           { return MOD; }
"namespace"     { return NAMESPACE; }
"use"           { return USE; }
"as"            { return AS; }
"alias"         { return ALIAS; }
"type"          { return TYPE_KW; }
"object"        { return OBJECT; }
"let"           { return LET; }
"var"           { return VAR; }
"void"          { return VOID; }
"bool"          { return BOOL; }
"bit"           { return BIT; }
"true"          { return TRUE_LIT; }
"false"         { return FALSE_LIT; }
"f32"           { return FLOAT; }
"f64"           { return DOUBLE; }
"str8"          { return STR8; }
"str16"         { return STR16; }
"array"         { return ARRAY; }
"list"          { return LIST; }
"map"           { return MAP; }
"tuple"         { return TUPLE; }
"ptr"           { return PTR; }
"struct"        { return STRUCT; }
"field"         { return FIELD; }
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
"debug!"        { return DEBUGPRINT; }
"debug_json!"   { return DEBUGJSONPRINT; }
"format!"       { return FORMAT; }
"assert_eq!"    { return ASSERT_EQ; }
"assert!"       { return ASSERT; }
"windows!"      { return WINDOWS_MACRO; }
"posix!"        { return POSIX_MACRO; }
"linux!"        { return LINUX_MACRO; }
"macos!"        { return MACOS_MACRO; }
"x64!"          { return X64_MACRO; }
"aarch64!"      { return AARCH64_MACRO; }
"static_assert!" { return STATIC_ASSERT; }
"vec!"          { return VEC_MACRO; }
"#[derive(Debug)]"    { return DERIVE_DEBUG; }
"#[derive(Json)]"     { return DERIVE_JSON; }
"#[test]"             { return TEST_ATTR; }
"#[fixture]"          { return FIXTURE_ATTR; }
"@property"           { return PROPERTY_ATTR; }
"#[x86-64]"          { return X86_64_ATTR; }
"#[aarch64]"         { return AARCH64_ATTR; }
"#[inline(always)]"   { return INLINE_ALWAYS_ATTR; }
"#[inline(never)]"    { return INLINE_NEVER_ATTR; }
"#[inline]"           { return INLINE_ATTR; }
"i32("          { return CAST_INT; }
"f32("          { return CAST_FLOAT; }
"f64("          { return CAST_DOUBLE; }
"->"            { return ARROW; }
"=>"            { return FAT_ARROW; }
":"[[:space:]]*"{"     {
                  if(switch_case_header_depth > 0) {
                      switch_case_header_depth = 0;
                      yyless(1);
                      return COLON;
                  }
                  return COLON_BLOCK;
                }
"+"[[:space:]]*"..."   { return PLUS_ELLIPSIS; }
"*"[[:space:]]*[a-zA-Z_][a-zA-Z0-9_]*[[:space:]]*"=" {
                  const char* p = yytext + 1;
                  while(*p && isspace(static_cast<unsigned char>(*p)))
                      ++p;
                  const char* start = p;
                  while(*p && (isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
                      ++p;
                  size_t len = static_cast<size_t>(p - start);
                  yylval.sval = (char*)malloc(len + 1);
                  memcpy(yylval.sval, start, len);
                  yylval.sval[len] = '\0';
                  return DEREF_ASSIGN_IDENT;
                }
"*"[[:space:]]*"..."   { return MULTIPLY_ELLIPSIS; }
"&&"[[:space:]]*"..."  { return AMP_AMP_ELLIPSIS; }
"||"[[:space:]]*"..."  { return PIPE_PIPE_ELLIPSIS; }
"..."           { return ELLIPSIS; }
"?"             { return is_try_question_context() ? TRY_QUESTION : QUESTION; }
"..="           { return DOTDOTEQ; }
".."            { return DOTDOT; }
"::"            { return COLONCOLON; }
"."             { return DOT; }
".keys()"       { return KEYS_METHOD; }
".values()"     { return VALUES_METHOD; }
".entries()"    { return ENTRIES_METHOD; }
".iter()"       { return ITER_METHOD; }
".into_iter()"  { return INTO_ITER_METHOD; }
".iter().enumerate()"      { return ITER_ENUMERATE_METHOD; }
".into_iter().enumerate()" { return INTO_ITER_ENUMERATE_METHOD; }
".enumerate()"  { return ENUMERATE_METHOD; }
"<"             {
                  if(is_generic_struct_literal_start()) {
                      generic_depth++;
                      return GENERIC_LT;
                  }
                  return LT;
                }
"<<"            { return SHL; }
">>="           { return SHR_ASSIGN; }
">>"            {
                  if(generic_depth > 0) {
                      generic_depth--;
                      yyless(1); /* return '>' now; second '>' stays for next token */
                      return GT;
                  }
                  return SHR;
                }
">"             {
                  if(generic_depth > 0) {
                      generic_depth--;
                  }
                  return GT;
                }
"<=>"           { return SPACESHIP; }
"<="            { return LE; }
">="            { return GE; }
"=="            { return EQ; }
"!="            { return NE; }
"!"             { return NOT; }
"~"             { return TILDE; }
":"             {
                  if(switch_case_header_depth > 0)
                      switch_case_header_depth = 0;
                  return COLON;
                }
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
0[bB][01]+(i|u)(8|16|32|64) {
                  // Typed binary literal, e.g. 0b1010i64
                  const char* suffix = yytext + 2;
                  while(*suffix == '0' || *suffix == '1') suffix++;
                  int64_t value = strtoll(yytext + 2, NULL, 2);
                  yylval.ast = mla_lex_make_typed_int(value, suffix);
                  return TYPED_INT_LITERAL;
                }
0[xX][0-9a-fA-F]+(i|u)(8|16|32|64) {
                  // Typed hex literal, e.g. 0xFFi64. The suffix begins at
                  // the first 'i' or 'u' that isn't a hex digit; locate it
                  // by scanning past hex chars.
                  const char* suffix = yytext + 2;
                  while((*suffix >= '0' && *suffix <= '9') ||
                        (*suffix >= 'a' && *suffix <= 'f') ||
                        (*suffix >= 'A' && *suffix <= 'F'))
                      suffix++;
                  int64_t value = strtoll(yytext + 2, NULL, 16);
                  yylval.ast = mla_lex_make_typed_int(value, suffix);
                  return TYPED_INT_LITERAL;
                }
[0-9]+(i|u)(8|16|32|64) {
                  // Typed decimal literal, e.g. 3i64, 255u8.
                  const char* suffix = yytext;
                  while(*suffix >= '0' && *suffix <= '9') suffix++;
                  int64_t value = strtoll(yytext, NULL, 10);
                  yylval.ast = mla_lex_make_typed_int(value, suffix);
                  return TYPED_INT_LITERAL;
                }
0[bB][01]+      { yylval.ival = strtoll(yytext + 2, NULL, 2); return INT_LITERAL; }
0[xX][0-9a-fA-F]+ { yylval.ival = strtoll(yytext + 2, NULL, 16); return INT_LITERAL; }
[0-9]+          { yylval.ival = strtoll(yytext, NULL, 10); return INT_LITERAL; }
"++"            { return PLUS_PLUS; }
"--"            { return MINUS_MINUS; }
"+="            { return PLUS_ASSIGN; }
"-="            { return MINUS_ASSIGN; }
"*="            { return MULTIPLY_ASSIGN; }
"/="            { return DIVIDE_ASSIGN; }
"%="            { return MODULO_ASSIGN; }
"|="            { return PIPE_ASSIGN; }
"^="            { return CARET_ASSIGN; }
"<<="           { return SHL_ASSIGN; }
"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return MULTIPLY; }
"/"             { return DIVIDE; }
"%"             { return MODULO; }
"&mut"          { return AMP_MUT; }
"&&"            { return AMP_AMP; }
"&"             { return AMP; }
"^"             { return CARET; }
"||"            { return PIPE_PIPE; }
"|>"            { return PIPE_GT; }
"|"             { return PIPE; }
.               {
                    parseHadError = true;
                    const int col = yycolumn_token > 0 ? yycolumn_token : 1;
                    const std::string msg =
                        "unexpected character: '" + std::string(yytext) + "'";
                    fprintf(stderr, "%s:%d:%d: error: %s\n", g_sourceFile,
                            yylineno, col,
                            mlang::diag::format_message_with_code("MLANG-E1100", msg).c_str());
                }
%%

YY_BUFFER_STATE mlang_scan_bytes(const char* bytes, size_t len)
{
    return yy_scan_bytes(bytes, static_cast<yy_size_t>(len));
}

void mlang_delete_buffer(YY_BUFFER_STATE buffer)
{
    yy_delete_buffer(buffer);
}
