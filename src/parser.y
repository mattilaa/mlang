%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

static char* join_module_path(char* left, char* right)
{
    size_t len = strlen(left) + 2 + strlen(right) + 1;
    char* out = (char*)malloc(len);
    if(!out)
        return left;
    snprintf(out, len, "%s::%s", left, right);
    return out;
}

extern int yylex();
extern int yylineno;
extern char* yytext;
void yyerror(const char* s);

// External reference to programRoot defined in globals.cpp
extern "C" {
    extern ASTNode* programRoot;
    extern bool parseHadError;
}

// Function prototypes for AST node creation
ASTNode* create_program(ASTNode* top_level_list);
ASTNode* create_top_level_list(ASTNode* item);
ASTNode* add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* create_struct_list(ASTNode* struct_def);
ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_extern);
ASTNode* create_type_node(int type);
ASTNode* create_parameter_list(ASTNode* param);
ASTNode* create_empty_parameter_list();
ASTNode* set_parameter_list_vararg(ASTNode* list);
ASTNode* create_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* param);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
ASTNode* create_assignment(char* name, ASTNode* expr, int line);
ASTNode* create_field_access(char* struct_name, char* field_name, int line);
ASTNode* create_field_assignment(char* struct_name, char* field_name, ASTNode* expr, int line);
ASTNode* create_chained_field_assignment(ASTNode* target, ASTNode* expr, int line);
ASTNode* create_return_stmt(ASTNode* expr);
ASTNode* create_int_literal(int value);
ASTNode* create_bool_literal(int value);
ASTNode* create_float_literal(float value);
ASTNode* create_double_literal(float value);
ASTNode* create_string_literal(char* value);
ASTNode* create_identifier(char* name);
ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* create_ternary_expression(ASTNode* cond, ASTNode* t, ASTNode* f, int line);
ASTNode* create_try_expression(ASTNode* expr, int line);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2, int line);
ASTNode* create_function_call_multi(char* name, ASTNode* args, int line);
ASTNode* create_result_constructor(char* variant, ASTNode* type_args, ASTNode* args, int line);
ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_cast_expression(int type, ASTNode* expr);
ASTNode* create_field_access_expr(ASTNode* object, char* field_name, int line);
ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members, int is_public, int derive_debug);
ASTNode* create_struct_member_list(ASTNode* member);
ASTNode* add_struct_member(ASTNode* list, ASTNode* member);
ASTNode* create_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* create_struct_method(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static);
ASTNode* add_struct_method(ASTNode* list, ASTNode* method);
ASTNode* create_struct_init(char* type_name, char* var_name);
ASTNode* create_method_call_expr(ASTNode* object, char* method_name, ASTNode* args, int line);
ASTNode* create_list_type();
ASTNode* create_list_literal(ASTNode* elements);
ASTNode* create_list_element_list(ASTNode* element);
ASTNode* add_list_element(ASTNode* list, ASTNode* element);
ASTNode* create_for_range(char* var_name, ASTNode* range, ASTNode* body, int line);
ASTNode* create_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body, int line);
ASTNode* create_range_expression(ASTNode* start, ASTNode* end, int inclusive);
ASTNode* create_mod_declaration(char* name, int line);
ASTNode* create_use_declaration(char* module_name, char* item_name, int line);
ASTNode* create_use_all_declaration(char* module_name, int line);
ASTNode* create_print_stmt(int kind, char* format_str, ASTNode* args, int line);
ASTNode* create_debug_print_stmt(char* format_str, ASTNode* args, int line);
ASTNode* create_print_expr_stmt(int kind, ASTNode* expr, int line);
ASTNode* create_argument_list(ASTNode* arg);
ASTNode* add_argument(ASTNode* list, ASTNode* arg);
ASTNode* create_format_expr(char* format_str, ASTNode* args, int line);
ASTNode* create_assert_eq(ASTNode* left, ASTNode* right, int line);
ASTNode* create_break_stmt(int line);
ASTNode* create_continue_stmt(int line);
ASTNode* create_generic_list_type(ASTNode* element_type);
ASTNode* create_map_type(ASTNode* key_type, ASTNode* value_type);
ASTNode* create_map_literal(ASTNode* entries);
ASTNode* create_map_entry_list(ASTNode* entry);
ASTNode* add_map_entry(ASTNode* list, ASTNode* entry);
ASTNode* create_map_entry(ASTNode* key, ASTNode* value);
ASTNode* create_index_expression(ASTNode* base, ASTNode* index, int line);
ASTNode* create_tuple_type(ASTNode* type_list);
ASTNode* create_type_list(ASTNode* type);
ASTNode* add_type_to_list(ASTNode* list, ASTNode* type);
ASTNode* create_tuple_literal(ASTNode* elements);
ASTNode* create_tuple_access(ASTNode* tuple, int index, int line);
ASTNode* create_map_keys_iterator(ASTNode* map_expr, int line);
ASTNode* create_map_values_iterator(ASTNode* map_expr, int line);
ASTNode* create_map_entries_iterator(ASTNode* map_expr, int line);
ASTNode* create_struct_type_ref(char* name);
ASTNode* create_len_expression(ASTNode* expr, int line);
ASTNode* create_method_call(ASTNode* object, char* method, ASTNode* args, int line);
// Generic structs and impl blocks
ASTNode* create_type_param_list(char* param);
ASTNode* add_type_param(ASTNode* list, char* param);
ASTNode* create_generic_struct_def(char* name, char* base_name, ASTNode* type_params, ASTNode* members, int is_public, int derive_debug);
ASTNode* create_impl_block(char* struct_name, ASTNode* type_params);
ASTNode* add_impl_method(ASTNode* impl, ASTNode* method);
ASTNode* create_struct_literal(char* struct_name, ASTNode* type_args, ASTNode* fields, int line);
ASTNode* create_struct_field_init_list(char* field_name, ASTNode* value);
ASTNode* add_struct_field_init(ASTNode* list, char* field_name, ASTNode* value);
ASTNode* create_generic_struct_type_ref(char* name, ASTNode* type_args);
ASTNode* create_match_pattern(char* name, char* binding, int line);
ASTNode* create_match_literal_pattern(ASTNode* literal, int line);
ASTNode* create_match_arm(ASTNode* pattern, ASTNode* expr, int line);
ASTNode* create_match_arm_list(ASTNode* arm);
ASTNode* add_match_arm(ASTNode* list, ASTNode* arm);
ASTNode* create_match_expression(ASTNode* target, ASTNode* arms, int line);
ASTNode* create_enum_def(char* name, ASTNode* variants, int is_public);
ASTNode* create_enum_variant(char* name);
ASTNode* create_enum_variant_list(ASTNode* variant);
ASTNode* add_enum_variant(ASTNode* list, ASTNode* variant);
ASTNode* create_enum_literal(char* enum_name, char* variant_name, int line);
ASTNode* create_pointer_type(ASTNode* element_type);
ASTNode* create_deref_assignment(ASTNode* pointer_expr, ASTNode* expr, int line);
%}

%union {
    long long ival;
    float fval;
    double dval;
    char* sval;
    struct ASTNode* ast;
}

%token <sval> IDENTIFIER STRING_LITERAL
%token <ival> INT_LITERAL
%token <fval> FLOAT_LITERAL
%token <dval> DOUBLE_LITERAL
%token FUNCTION RETURN IF ELSE VOID BOOL INT FLOAT DOUBLE STRING STR8 STR16 LIST MAP TUPLE PTR STRUCT ENUM
%token QUESTION TRY_QUESTION
%token ELLIPSIS
%token MATCH
%token PUB IMPL
%token EXTERN
%token STATIC
%token TRUE_LIT FALSE_LIT
%token I8 I16 I32 I64 U8 U16 U32 U64
%token LET VAR
%token FOR IN DOTDOT DOTDOTEQ BREAK CONTINUE
%token MOD USE COLONCOLON
%token PRINTLN PRINT EPRINTLN EPRINT DEBUGPRINT FORMAT ASSERT_EQ
%token PLUS MINUS MULTIPLY DIVIDE MODULO ASSIGN AMP
%token LT GT LE GE EQ NE
%token LBRACE RBRACE LPAREN RPAREN LBRACKET RBRACKET SEMICOLON COMMA ARROW COLON DOT
%token FAT_ARROW
%token GENERIC_LT
%token KEYS_METHOD VALUES_METHOD ENTRIES_METHOD
%token CAST_INT CAST_FLOAT CAST_DOUBLE
%token DERIVE_DEBUG
%token TEST_ATTR

%type <ast> program top_level_list top_level_item test_function_def
%type <ast> struct_def enum_def enum_variant_list enum_variant
%type <ast> function_def type parameter_list parameters parameter
%type <ast> statement_list statement expression ternary_expression cast_expression
%type <ast> if_statement else_if_list else_if optional_else
%type <ast> struct_member_list struct_member struct_method struct_init
%type <ast> list_literal list_elements
%type <ast> let_statement var_statement assignment_statement expression_statement
%type <ast> return_statement block_statement for_statement range_expression
%type <ast> break_statement continue_statement
%type <ast> primary_expression postfix_expression unary_expression binary_expression function_call
%type <ast> mod_declaration use_declaration
%type <sval> module_path
%type <ast> print_statement argument_list assert_eq_statement
%type <ast> global_var_statement static_var_statement
%type <ast> map_literal map_entries map_entry index_expression
%type <ast> tuple_type type_list tuple_literal tuple_elements
%type <ast> map_iterator
%type <ast> type_param_list impl_block impl_method_list struct_literal struct_field_init_list
%type <ast> match_expression match_arm_list match_arm match_pattern match_target match_atom match_binary_expression

%left LT GT LE GE EQ NE
%left PLUS MINUS
%left MULTIPLY DIVIDE MODULO

%start program

%%

program
    : top_level_list {
        $$ = create_program($1);
        programRoot = $$;  /* Store the result in the global variable */
    }
    ;

top_level_list
    : top_level_item { $$ = create_top_level_list($1); }
    | top_level_list top_level_item { $$ = add_to_top_level_list($1, $2); }
    ;

top_level_item
    : struct_def
    | enum_def
    | function_def
    | test_function_def
    | mod_declaration
    | use_declaration
    | impl_block
    | global_var_statement
    ;

module_path
    : IDENTIFIER
        { $$ = $1; }
    | module_path COLONCOLON IDENTIFIER
        { $$ = join_module_path($1, $3); }
    ;

mod_declaration
    : MOD module_path SEMICOLON
        { $$ = create_mod_declaration($2, yylineno); }
    ;

use_declaration
    : USE module_path COLONCOLON IDENTIFIER SEMICOLON
        { $$ = create_use_declaration($2, $4, yylineno); }
    | USE module_path COLONCOLON MULTIPLY SEMICOLON
        { $$ = create_use_all_declaration($2, yylineno); }
    ;

struct_def
    : STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($2, NULL, $4, 0, 0); node->line = yylineno; $$ = node; }
    | STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($2, $4, $6, 0, 0); node->line = yylineno; $$ = node; }
    | PUB STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($3, NULL, $5, 1, 0); node->line = yylineno; $$ = node; }
    | PUB STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($3, $5, $7, 1, 0); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($3, NULL, $5, 0, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($3, $5, $7, 0, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG PUB STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($4, NULL, $6, 1, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG PUB STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_struct_def($4, $6, $8, 1, 1); node->line = yylineno; $$ = node; }
    /* Generic struct definitions */
    | STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_generic_struct_def($2, NULL, $4, $7, 0, 0); node->line = yylineno; $$ = node; }
    | PUB STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_generic_struct_def($3, NULL, $5, $8, 1, 0); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_generic_struct_def($3, NULL, $5, $8, 0, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG PUB STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = create_generic_struct_def($4, NULL, $6, $9, 1, 1); node->line = yylineno; $$ = node; }
    ;

enum_def
    : ENUM IDENTIFIER LBRACE enum_variant_list RBRACE SEMICOLON
        { auto* node = create_enum_def($2, $4, 0); node->line = yylineno; $$ = node; }
    | PUB ENUM IDENTIFIER LBRACE enum_variant_list RBRACE SEMICOLON
        { auto* node = create_enum_def($3, $5, 1); node->line = yylineno; $$ = node; }
    ;

enum_variant_list
    : enum_variant { $$ = create_enum_variant_list($1); }
    | enum_variant_list COMMA enum_variant { $$ = add_enum_variant($1, $3); }
    ;

enum_variant
    : IDENTIFIER { $$ = create_enum_variant($1); }
    ;

type_param_list
    : IDENTIFIER { $$ = create_type_param_list($1); }
    | type_param_list COMMA IDENTIFIER { $$ = add_type_param($1, $3); }
    ;

impl_block
    : IMPL IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = create_impl_block($2, NULL);
            // Copy methods from temp list
            $$ = impl;
            // Methods are added via impl_method_list
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($4);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
        }
    | IMPL LT type_param_list GT IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = create_impl_block($5, $3);
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($7);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            $$ = impl;
        }
    | IMPL GENERIC_LT type_param_list GT IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = create_impl_block($5, $3);
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($7);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            $$ = impl;
        }
    ;

impl_method_list
    : /* empty */ { $$ = create_impl_block("", NULL); }
    | impl_method_list struct_method
        {
            $$ = add_impl_method($1, $2);
        }
    ;
    ;

struct_member_list
    : struct_member { $$ = create_struct_member_list($1); }
    | struct_member_list struct_member { $$ = add_struct_member($1, $2); }
    | struct_method { $$ = create_struct_member_list($1); }
    | struct_member_list struct_method { $$ = add_struct_method($1, $2); }
    ;

struct_member
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_struct_member(0, $4, $2, $6); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = create_struct_member(1, $4, $2, NULL); }
    ;

struct_method
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { $$ = create_struct_method($7, $2, $4, $9, 0, 0); }
    | PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { $$ = create_struct_method($8, $3, $5, $10, 1, 0); }
    ;

function_def
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { auto* node = create_function_def($7, $2, $4, $9, 0, 0); node->line = yylineno; $$ = node; }
    | PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { auto* node = create_function_def($8, $3, $5, $10, 1, 0); node->line = yylineno; $$ = node; }
    | EXTERN FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type SEMICOLON
        { auto* node = create_function_def($8, $3, $5, NULL, 0, 1); node->line = yylineno; $$ = node; }
    | EXTERN PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type SEMICOLON
        { auto* node = create_function_def($9, $4, $6, NULL, 1, 1); node->line = yylineno; $$ = node; }
    ;

test_function_def
    : TEST_ATTR function_def
        { static_cast<FunctionDefNode*>($2)->isTest = true; $$ = $2; }
    ;

parameter_list
    : /* empty */ { $$ = create_empty_parameter_list(); }
    | parameters
    | parameters COMMA ELLIPSIS { $$ = set_parameter_list_vararg($1); }
    | ELLIPSIS { $$ = set_parameter_list_vararg(create_empty_parameter_list()); }
    ;

parameters
    : parameter { $$ = create_parameter_list($1); }
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
    | DOUBLE { $$ = create_type_node(TypeNode::TYPE_DOUBLE); }
    | STRING { $$ = create_type_node(TypeNode::TYPE_STRING); }
    | STR8   { $$ = create_type_node(TypeNode::TYPE_STR8); }
    | STR16  { $$ = create_type_node(TypeNode::TYPE_STR16); }
    | LIST   { $$ = create_list_type(); }
    | LIST GENERIC_LT type GT { $$ = create_generic_list_type($3); }
    | MAP GENERIC_LT type COMMA type GT { $$ = create_map_type($3, $5); }
    | tuple_type
    | PTR GENERIC_LT type GT { $$ = create_pointer_type($3); }
    | I8     { $$ = create_type_node(TypeNode::TYPE_I8); }
    | I16    { $$ = create_type_node(TypeNode::TYPE_I16); }
    | I32    { $$ = create_type_node(TypeNode::TYPE_I32); }
    | I64    { $$ = create_type_node(TypeNode::TYPE_I64); }
    | U8     { $$ = create_type_node(TypeNode::TYPE_U8); }
    | U16    { $$ = create_type_node(TypeNode::TYPE_U16); }
    | U32    { $$ = create_type_node(TypeNode::TYPE_U32); }
    | U64    { $$ = create_type_node(TypeNode::TYPE_U64); }
    | IDENTIFIER { $$ = create_struct_type_ref($1); }
    | IDENTIFIER GENERIC_LT type_list GT { $$ = create_generic_struct_type_ref($1, $3); }
    ;

tuple_type
    : TUPLE GENERIC_LT type_list GT { $$ = create_tuple_type($3); }
    ;

type_list
    : type { $$ = create_type_list($1); }
    | type_list COMMA type { $$ = add_type_to_list($1, $3); }
    ;

statement_list
    : statement { $$ = create_statement_list($1); }
    | statement_list statement { $$ = add_statement($1, $2); }
    ;

statement
    : let_statement
    | var_statement
    | static_var_statement
    | assignment_statement
    | expression_statement
    | return_statement
    | if_statement
    | for_statement
    | block_statement
    | struct_init
    | print_statement
    | assert_eq_statement
    | break_statement
    | continue_statement
    ;

let_statement
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_let_declaration($4, $2, $6); }
    | LET IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_let_declaration(NULL, $2, $4); }
    ;

var_statement
    : VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_var_declaration($4, $2, $6); }
    | VAR IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_var_declaration(NULL, $2, $4); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = create_var_declaration($4, $2, NULL); }
    ;

global_var_statement
    : VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        {
            $$ = create_var_declaration($4, $2, $6);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isGlobalStorage = true;
        }
    | VAR IDENTIFIER ASSIGN expression SEMICOLON
        {
            $$ = create_var_declaration(NULL, $2, $4);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isGlobalStorage = true;
        }
    | VAR IDENTIFIER COLON type SEMICOLON
        {
            $$ = create_var_declaration($4, $2, NULL);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isGlobalStorage = true;
        }
    ;

static_var_statement
    : STATIC VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        {
            $$ = create_var_declaration($5, $3, $7);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isStaticStorage = true;
        }
    | STATIC VAR IDENTIFIER ASSIGN expression SEMICOLON
        {
            $$ = create_var_declaration(NULL, $3, $5);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isStaticStorage = true;
        }
    | STATIC VAR IDENTIFIER COLON type SEMICOLON
        {
            $$ = create_var_declaration($5, $3, NULL);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isStaticStorage = true;
        }
    ;

assignment_statement
    : postfix_expression ASSIGN expression SEMICOLON
        {
            if(auto* id = dynamic_cast<IdentifierNode*>($1))
                $$ = create_assignment(const_cast<char*>(id->name.c_str()), $3, yylineno);
            else
                $$ = create_chained_field_assignment($1, $3, yylineno);
        }
    | MULTIPLY unary_expression ASSIGN expression SEMICOLON
        { $$ = create_deref_assignment($2, $4, yylineno); }
    ;

expression_statement
    : expression SEMICOLON { $$ = create_expression_statement($1); }
    ;

return_statement
    : RETURN expression SEMICOLON { $$ = create_return_stmt($2); }
    | RETURN SEMICOLON { $$ = create_return_stmt(NULL); }
    ;

break_statement
    : BREAK SEMICOLON { $$ = create_break_stmt(yylineno); }
    ;

continue_statement
    : CONTINUE SEMICOLON { $$ = create_continue_stmt(yylineno); }
    ;

block_statement
    : LBRACE statement_list RBRACE { $$ = create_block_statement($2); }
    ;

for_statement
    : FOR IDENTIFIER IN range_expression block_statement
        { $$ = create_for_range($2, $4, $5, yylineno); }
    | FOR IDENTIFIER IN primary_expression block_statement
        { $$ = create_for_iterator($2, $4, $5, yylineno); }
    | FOR IDENTIFIER IN primary_expression KEYS_METHOD block_statement
        { $$ = create_for_iterator($2, create_map_keys_iterator($4, yylineno), $6, yylineno); }
    | FOR IDENTIFIER IN primary_expression VALUES_METHOD block_statement
        { $$ = create_for_iterator($2, create_map_values_iterator($4, yylineno), $6, yylineno); }
    | FOR IDENTIFIER IN primary_expression ENTRIES_METHOD block_statement
        { $$ = create_for_iterator($2, create_map_entries_iterator($4, yylineno), $6, yylineno); }
    ;

range_expression
    : primary_expression DOTDOT primary_expression
        { $$ = create_range_expression($1, $3, 0); }
    | primary_expression DOTDOTEQ primary_expression
        { $$ = create_range_expression($1, $3, 1); }
    | primary_expression DOTDOT primary_expression PLUS primary_expression
        { $$ = create_range_expression($1, create_binary_op(PLUS, $3, $5), 0); }
    | primary_expression DOTDOT primary_expression MINUS primary_expression
        { $$ = create_range_expression($1, create_binary_op(MINUS, $3, $5), 0); }
    | primary_expression DOTDOTEQ primary_expression PLUS primary_expression
        { $$ = create_range_expression($1, create_binary_op(PLUS, $3, $5), 1); }
    | primary_expression DOTDOTEQ primary_expression MINUS primary_expression
        { $$ = create_range_expression($1, create_binary_op(MINUS, $3, $5), 1); }
    | primary_expression PLUS primary_expression DOTDOT primary_expression
        { $$ = create_range_expression(create_binary_op(PLUS, $1, $3), $5, 0); }
    | primary_expression MINUS primary_expression DOTDOT primary_expression
        { $$ = create_range_expression(create_binary_op(MINUS, $1, $3), $5, 0); }
    | primary_expression PLUS primary_expression DOTDOTEQ primary_expression
        { $$ = create_range_expression(create_binary_op(PLUS, $1, $3), $5, 1); }
    | primary_expression MINUS primary_expression DOTDOTEQ primary_expression
        { $$ = create_range_expression(create_binary_op(MINUS, $1, $3), $5, 1); }
    ;

struct_init
    : IDENTIFIER IDENTIFIER SEMICOLON
        { $$ = create_struct_init($1, $2); }
    ;

print_statement
    : PRINTLN LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_print_stmt(1, $3, NULL, yylineno); }
    | PRINTLN LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_print_stmt(1, $3, $5, yylineno); }
    | PRINTLN LPAREN expression RPAREN SEMICOLON
        { $$ = create_print_expr_stmt(1, $3, yylineno); }
    | PRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_print_stmt(0, $3, NULL, yylineno); }
    | PRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_print_stmt(0, $3, $5, yylineno); }
    | PRINT LPAREN expression RPAREN SEMICOLON
        { $$ = create_print_expr_stmt(0, $3, yylineno); }
    | EPRINTLN LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_print_stmt(3, $3, NULL, yylineno); }
    | EPRINTLN LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_print_stmt(3, $3, $5, yylineno); }
    | EPRINTLN LPAREN expression RPAREN SEMICOLON
        { $$ = create_print_expr_stmt(3, $3, yylineno); }
    | EPRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_print_stmt(2, $3, NULL, yylineno); }
    | EPRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_print_stmt(2, $3, $5, yylineno); }
    | EPRINT LPAREN expression RPAREN SEMICOLON
        { $$ = create_print_expr_stmt(2, $3, yylineno); }
    | DEBUGPRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_debug_print_stmt($3, NULL, yylineno); }
    | DEBUGPRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_debug_print_stmt($3, $5, yylineno); }
    ;

assert_eq_statement
    : ASSERT_EQ LPAREN expression COMMA expression RPAREN SEMICOLON
        { $$ = create_assert_eq($3, $5, yylineno); }
    ;

argument_list
    : expression { $$ = create_argument_list($1); }
    | argument_list COMMA expression { $$ = add_argument($1, $3); }
    ;

if_statement
    : IF expression COLON block_statement else_if_list optional_else
        { $$ = create_if_statement($2, $4, $5, $6); }
    | IF expression COLON statement else_if_list optional_else
        { $$ = create_if_statement($2, create_statement_list($4), $5, $6); }
    ;

else_if_list
    : /* empty */ { $$ = NULL; }
    | else_if_list else_if { $$ = add_else_if($1, $2); }
    ;

else_if
    : ELSE IF expression COLON block_statement { $$ = create_else_if($3, $5); }
    | ELSE IF expression COLON statement { $$ = create_else_if($3, create_statement_list($5)); }
    ;

optional_else
    : /* empty */ { $$ = NULL; }
    | ELSE COLON block_statement { $$ = $3; }
    | ELSE COLON statement { $$ = create_statement_list($3); }
    ;

expression
    : ternary_expression
    ;

ternary_expression
    : binary_expression QUESTION ternary_expression COLON ternary_expression
        { $$ = create_ternary_expression($1, $3, $5, yylineno); }
    | binary_expression
    | unary_expression
    ;

unary_expression
    : MINUS unary_expression
        { $$ = create_unary_op(MINUS, $2); if($$) $$->line = yylineno; }
    | AMP unary_expression
        { $$ = create_unary_op(AMP, $2); if($$) $$->line = yylineno; }
    | MULTIPLY unary_expression
        { $$ = create_unary_op(MULTIPLY, $2); if($$) $$->line = yylineno; }
    | postfix_expression TRY_QUESTION
        { $$ = create_try_expression($1, yylineno); }
    | function_call TRY_QUESTION
        { $$ = create_try_expression($1, yylineno); }
    | postfix_expression
    | function_call
    | cast_expression
    | list_literal
    | map_literal
    | index_expression
    | tuple_literal
    | map_iterator
    | struct_literal
    ;

match_expression
    : MATCH match_target LBRACE match_arm_list RBRACE
        { $$ = create_match_expression($2, $4, yylineno); }
    ;

match_target
    : match_atom
    | match_binary_expression
    ;

match_atom
    : postfix_expression
    | function_call
    | cast_expression
    | list_literal
    | index_expression
    | tuple_literal
    | map_iterator
    ;

match_binary_expression
    : match_target PLUS match_target { $$ = create_binary_op(PLUS, $1, $3); }
    | match_target MINUS match_target { $$ = create_binary_op(MINUS, $1, $3); }
    | match_target MULTIPLY match_target { $$ = create_binary_op(MULTIPLY, $1, $3); }
    | match_target DIVIDE match_target { $$ = create_binary_op(DIVIDE, $1, $3); }
    | match_target MODULO match_target { $$ = create_binary_op(MODULO, $1, $3); }
    | match_target LT match_target { $$ = create_binary_op(LT, $1, $3); }
    | match_target GT match_target { $$ = create_binary_op(GT, $1, $3); }
    | match_target LE match_target { $$ = create_binary_op(LE, $1, $3); }
    | match_target GE match_target { $$ = create_binary_op(GE, $1, $3); }
    | match_target EQ match_target { $$ = create_binary_op(EQ, $1, $3); }
    | match_target NE match_target { $$ = create_binary_op(NE, $1, $3); }
    ;

match_arm_list
    : match_arm { $$ = create_match_arm_list($1); }
    | match_arm_list COMMA match_arm { $$ = add_match_arm($1, $3); }
    ;

match_arm
    : match_pattern FAT_ARROW expression
        { $$ = create_match_arm($1, $3, yylineno); }
    | match_pattern ASSIGN GT expression
        { $$ = create_match_arm($1, $4, yylineno); }
    ;

match_pattern
    : IDENTIFIER LPAREN IDENTIFIER RPAREN
        { $$ = create_match_pattern($1, $3, yylineno); }
    | IDENTIFIER COLONCOLON IDENTIFIER
        { $$ = create_match_literal_pattern(create_enum_literal($1, $3, yylineno), yylineno); }
    | IDENTIFIER
        { $$ = create_match_pattern($1, NULL, yylineno); }
    | INT_LITERAL
        { $$ = create_match_literal_pattern(create_int_literal($1), yylineno); }
    | FLOAT_LITERAL
        { $$ = create_match_literal_pattern(create_float_literal($1), yylineno); }
    | DOUBLE_LITERAL
        { $$ = create_match_literal_pattern(create_double_literal($1), yylineno); }
    | STRING_LITERAL
        { $$ = create_match_literal_pattern(create_string_literal($1), yylineno); }
    | TRUE_LIT
        { $$ = create_match_literal_pattern(create_bool_literal(1), yylineno); }
    | FALSE_LIT
        { $$ = create_match_literal_pattern(create_bool_literal(0), yylineno); }
    ;

primary_expression
    : INT_LITERAL { $$ = create_int_literal($1); }
    | FLOAT_LITERAL { $$ = create_float_literal($1); }
    | DOUBLE_LITERAL { $$ = create_double_literal($1); }
    | STRING_LITERAL { $$ = create_string_literal($1); }
    | TRUE_LIT { $$ = create_bool_literal(1); }
    | FALSE_LIT { $$ = create_bool_literal(0); }
    | FORMAT LPAREN STRING_LITERAL RPAREN
        { $$ = create_format_expr($3, NULL, yylineno); }
    | FORMAT LPAREN STRING_LITERAL COMMA argument_list RPAREN
        { $$ = create_format_expr($3, $5, yylineno); }
    | IDENTIFIER COLONCOLON IDENTIFIER
        { $$ = create_enum_literal($1, $3, yylineno); }
    | IDENTIFIER { $$ = create_identifier($1); }
    | LPAREN expression RPAREN { $$ = $2; }
    | match_expression { $$ = $1; }
    | list_literal { $$ = $1; }
    | map_literal { $$ = $1; }
    ;

/* Struct literal: StructName { field: value, ... } */
struct_literal
    : IDENTIFIER LBRACE struct_field_init_list RBRACE
        { $$ = create_struct_literal($1, NULL, $3, yylineno); }
    | IDENTIFIER GENERIC_LT type_list GT LBRACE struct_field_init_list RBRACE
        { $$ = create_struct_literal($1, $3, $6, yylineno); }
    ;

struct_field_init_list
    : /* empty */ { $$ = NULL; }
    | IDENTIFIER COLON expression
        { $$ = create_struct_field_init_list($1, $3); }
    | struct_field_init_list COMMA IDENTIFIER COLON expression
        { $$ = add_struct_field_init($1, $3, $5); }
    ;

postfix_expression
    : primary_expression { $$ = $1; }
    | postfix_expression DOT IDENTIFIER { $$ = create_field_access_expr($1, $3, yylineno); }
    | postfix_expression DOT IDENTIFIER LPAREN RPAREN
        { $$ = create_method_call_expr($1, $3, NULL, yylineno); }
    | postfix_expression DOT IDENTIFIER LPAREN argument_list RPAREN
        { $$ = create_method_call_expr($1, $3, $5, yylineno); }
    | postfix_expression DOT INT_LITERAL { $$ = create_tuple_access($1, $3, yylineno); }
    ;

binary_expression
    : expression PLUS expression { $$ = create_binary_op(PLUS, $1, $3); }
    | expression MINUS expression { $$ = create_binary_op(MINUS, $1, $3); }
    | expression MULTIPLY expression { $$ = create_binary_op(MULTIPLY, $1, $3); }
    | expression DIVIDE expression { $$ = create_binary_op(DIVIDE, $1, $3); }
    | expression MODULO expression { $$ = create_binary_op(MODULO, $1, $3); }
    | expression LT expression { $$ = create_binary_op(LT, $1, $3); }
    | expression GT expression { $$ = create_binary_op(GT, $1, $3); }
    | expression LE expression { $$ = create_binary_op(LE, $1, $3); }
    | expression GE expression { $$ = create_binary_op(GE, $1, $3); }
    | expression EQ expression { $$ = create_binary_op(EQ, $1, $3); }
    | expression NE expression { $$ = create_binary_op(NE, $1, $3); }
    ;

function_call
    : IDENTIFIER LPAREN RPAREN { $$ = create_function_call($1, NULL, NULL, yylineno); }
    | IDENTIFIER LPAREN argument_list RPAREN { $$ = create_function_call_multi($1, $3, yylineno); }
    | IDENTIFIER COLONCOLON IDENTIFIER LPAREN RPAREN
        {
            char* qname = join_module_path($1, $3);
            $$ = create_function_call(qname, NULL, NULL, yylineno);
        }
    | IDENTIFIER COLONCOLON IDENTIFIER LPAREN argument_list RPAREN
        {
            char* qname = join_module_path($1, $3);
            $$ = create_function_call_multi(qname, $5, yylineno);
        }
    | IDENTIFIER GENERIC_LT type_list GT LPAREN RPAREN
        { $$ = create_result_constructor($1, $3, NULL, yylineno); }
    | IDENTIFIER GENERIC_LT type_list GT LPAREN argument_list RPAREN
        { $$ = create_result_constructor($1, $3, $6, yylineno); }
    ;

cast_expression
    : CAST_INT expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_INT, $2); }
    | CAST_FLOAT expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_FLOAT, $2); }
    | CAST_DOUBLE expression RPAREN { $$ = create_cast_expression(TypeNode::TYPE_DOUBLE, $2); }
    ;

list_literal
    : LBRACKET list_elements RBRACKET { $$ = create_list_literal($2); }
    | LBRACKET RBRACKET { $$ = create_list_literal(NULL); }
    ;

list_elements
    : expression { $$ = create_list_element_list($1); }
    | list_elements COMMA expression { $$ = add_list_element($1, $3); }
    ;

map_literal
    : LBRACE map_entries RBRACE { $$ = create_map_literal($2); }
    | LBRACE RBRACE { $$ = create_map_literal(NULL); }
    ;

map_entries
    : map_entry { $$ = create_map_entry_list($1); }
    | map_entries COMMA map_entry { $$ = add_map_entry($1, $3); }
    ;

map_entry
    : expression COLON expression { $$ = create_map_entry($1, $3); }
    ;

index_expression
    : primary_expression LBRACKET expression RBRACKET { $$ = create_index_expression($1, $3, yylineno); }
    ;

tuple_literal
    : LPAREN tuple_elements RPAREN { $$ = create_tuple_literal($2); }
    ;

tuple_elements
    : expression COMMA expression {
        ASTNode* list = create_list_element_list($1);
        $$ = add_list_element(list, $3);
    }
    | tuple_elements COMMA expression { $$ = add_list_element($1, $3); }
    ;

map_iterator
    : primary_expression KEYS_METHOD { $$ = create_map_keys_iterator($1, yylineno); }
    | primary_expression VALUES_METHOD { $$ = create_map_values_iterator($1, yylineno); }
    | primary_expression ENTRIES_METHOD { $$ = create_map_entries_iterator($1, yylineno); }
    ;

%%

static bool is_reserved_type_keyword(const char* s)
{
    if(!s || !*s)
        return false;
    return strcmp(s, "void") == 0 || strcmp(s, "bool") == 0 ||
           strcmp(s, "int") == 0 || strcmp(s, "float") == 0 ||
           strcmp(s, "double") == 0 || strcmp(s, "string") == 0 ||
           strcmp(s, "str8") == 0 || strcmp(s, "str16") == 0 ||
           strcmp(s, "list") == 0 || strcmp(s, "map") == 0 ||
           strcmp(s, "tuple") == 0 || strcmp(s, "i8") == 0 ||
           strcmp(s, "i16") == 0 || strcmp(s, "i32") == 0 ||
           strcmp(s, "i64") == 0 || strcmp(s, "u8") == 0 ||
           strcmp(s, "u16") == 0 || strcmp(s, "u32") == 0 ||
           strcmp(s, "u64") == 0 || strcmp(s, "ptr") == 0;
}

void yyerror(const char* s) {
    parseHadError = true;
    if(is_reserved_type_keyword(yytext))
    {
        fprintf(stderr,
                "error: expected identifier, found keyword '%s' (line %d)\n",
                yytext, yylineno);
        return;
    }
    fprintf(stderr, "Error at line %d: %s\n", yylineno, s);
}
