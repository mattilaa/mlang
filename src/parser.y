%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "ast_handle_helpers.h"

ASTNode* create_identifier_at(char* name, int line, int col);
ASTNode* mla_ast_enum_literal(char* enum_name, char* variant_name, int line);

static char* join_module_path(char* left, char* right)
{
    size_t len = strlen(left) + 2 + strlen(right) + 1;
    char* out = (char*)malloc(len);
    if(!out)
        return left;
    snprintf(out, len, "%s::%s", left, right);
    return out;
}

static ASTNode* create_enum_or_ident_from_path(char* path, int line)
{
    const char* last = NULL;
    for(const char* p = path; p && *p; ++p)
    {
        if(p[0] == ':' && p[1] == ':')
            last = p;
    }

    if(!last)
        return create_identifier_at(path, line, 0);

    size_t enumLen = (size_t)(last - path);
    char* enumName = (char*)malloc(enumLen + 1);
    if(!enumName)
        return create_identifier_at(path, line, 0);
    memcpy(enumName, path, enumLen);
    enumName[enumLen] = '\0';

    char* variant = strdup(last + 2);
    if(!variant)
        return create_identifier_at(path, line, 0);

    return mla_ast_enum_literal(enumName, variant, line);
}

extern int yylex();
extern int yylineno;
extern int yycolumn_token;
extern char* yytext;
void yyerror(const char* s);

// Source file name used in error messages (set by main before yyparse())
const char* g_sourceFile = "<input>";

// External reference to programRoot defined in globals.cpp
extern "C" {
    extern ASTNode* programRoot;
    extern bool parseHadError;
}

// Function prototypes for AST node creation
ASTNode* mla_ast_program(ASTNode* top_level_list);
ASTNode* mla_ast_top_level_list(ASTNode* item);
ASTNode* mla_ast_add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* create_struct_list(ASTNode* struct_def);
ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* mla_ast_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_extern);
ASTNode* mla_ast_type_node(int type);
ASTNode* mla_ast_parameter_list(ASTNode* param);
ASTNode* mla_ast_empty_parameter_list();
ASTNode* set_parameter_list_vararg(ASTNode* list);
ASTNode* mla_ast_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* param);
ASTNode* mla_ast_statement_list_create(ASTNode* stmt);
ASTNode* create_empty_statement_list();
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
ASTNode* mla_ast_statement_list_add(ASTNode* list, ASTNode* stmt);
ASTNode* mla_ast_assignment(char* name, ASTNode* expr, int line);
ASTNode* create_field_access(char* struct_name, char* field_name, int line);
ASTNode* mla_ast_field_assignment(char* struct_name, char* field_name, ASTNode* expr, int line);
ASTNode* mla_ast_chained_field_assignment(ASTNode* target, ASTNode* expr, int line);
ASTNode* mla_ast_return_stmt(ASTNode* expr);
ASTNode* mla_ast_literal_int(int value);
ASTNode* mla_ast_literal_bool(int value);
ASTNode* mla_ast_literal_float(float value);
ASTNode* mla_ast_literal_double(float value);
ASTNode* mla_ast_literal_string(char* value);
ASTNode* create_identifier(char* name);
ASTNode* create_identifier_line(char* name, int line);
ASTNode* create_identifier_at(char* name, int line, int col);
ASTNode* mla_ast_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* mla_ast_fold_expression(int op, ASTNode* pack_expr, int is_right_fold);
ASTNode* mla_ast_ternary_expression(ASTNode* cond, ASTNode* t, ASTNode* f, int line);
ASTNode* mla_ast_try_expression(ASTNode* expr, int line);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2, int line);
ASTNode* create_function_call_multi(char* name, ASTNode* args, int line);
ASTNode* mla_argument_list_create(ASTNode* arg);
ASTNode* mla_argument_list_add(ASTNode* list, ASTNode* arg);
ASTNode* mla_function_call_simple(char* name, ASTNode* arg1, ASTNode* arg2, int line);
ASTNode* mla_function_call_from_list(char* name, ASTNode* args, int line);
ASTNode* mla_ast_result_constructor(char* variant, ASTNode* type_args, ASTNode* args, int line);
ASTNode* mla_ast_list_literal(ASTNode* elements);
ASTNode* mla_ast_list_element_list(ASTNode* element);
ASTNode* mla_ast_list_element_list_add(ASTNode* list, ASTNode* element);
ASTNode* mla_ast_array_fill(ASTNode* value, ASTNode* count);
ASTNode* mla_ast_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* mla_ast_if_statement_with_init(ASTNode* condition_init, ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* mla_ast_var_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* mla_ast_cast_expression(int type, ASTNode* expr);
ASTNode* mla_ast_field_access_expr(ASTNode* object, char* field_name, int line);
ASTNode* mla_ast_struct_def(char* name, char* base_name, ASTNode* members, int is_public, int derive_debug);
ASTNode* mla_ast_struct_member_list(ASTNode* member);
ASTNode* mla_ast_struct_member_list_add(ASTNode* list, ASTNode* member);
ASTNode* mla_ast_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* mla_ast_struct_method(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static);
ASTNode* mla_ast_struct_member_add_method(ASTNode* list, ASTNode* method);
ASTNode* mla_ast_trait_def(char* name, int line);
ASTNode* mla_ast_impl_block(char* struct_name, ASTNode* type_params, char* trait_name);
ASTNode* mla_ast_impl_add_method(ASTNode* impl, ASTNode* method);

ASTNode* mla_ast_struct_member_list(ASTNode* member);
ASTNode* mla_ast_struct_member_list_add(ASTNode* list, ASTNode* member);
ASTNode* mla_ast_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* mla_ast_struct_method(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static);
ASTNode* mla_ast_struct_member_add_method(ASTNode* list, ASTNode* method);
ASTNode* mla_ast_struct_init(char* type_name, char* var_name);
ASTNode* mla_ast_method_call_expr(ASTNode* object, char* method_name, ASTNode* args, int line);
ASTNode* mla_ast_list_type();
ASTNode* mla_ast_list_literal(ASTNode* elements);
ASTNode* mla_ast_list_element_list(ASTNode* element);
ASTNode* mla_ast_list_element_list_add(ASTNode* list, ASTNode* element);
ASTNode* mla_ast_for_range(char* var_name, ASTNode* range, ASTNode* body, int line);
ASTNode* mla_ast_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body, int line);
ASTNode* mla_ast_while_statement(ASTNode* condition, ASTNode* body, int line,
                                int uses_colon_without_guard);
ASTNode* mla_ast_range_expression(ASTNode* start, ASTNode* end, int inclusive);
ASTNode* mla_ast_mod_declaration(char* name, int line);
ASTNode* mla_ast_use_declaration(char* module_name, char* item_name, int line);
ASTNode* mla_ast_use_all_declaration(char* module_name, int line);
ASTNode* mla_ast_type_alias(char* name, ASTNode* type_params,
                           ASTNode* aliased_type);
ASTNode* mla_ast_print_stmt(int kind, char* format_str, ASTNode* args, int line);
ASTNode* mla_ast_debug_print_stmt(char* format_str, ASTNode* args, int line);
ASTNode* mla_ast_print_expr_stmt(int kind, ASTNode* expr, int line);
ASTNode* mla_ast_argument_list(ASTNode* arg);

ASTNode* mla_ast_enum_variant(char* name, int has_explicit_value, long long explicit_value);
ASTNode* mla_ast_enum_variant_list(ASTNode* variant);
ASTNode* mla_ast_enum_variant_list_add(ASTNode* list, ASTNode* variant);
ASTNode* mla_ast_enum_variant_ref(char* name, char* ref_enum_name, char* ref_variant_name);
ASTNode* mla_ast_enum_literal(char* enum_name, char* variant_name, int line);

ASTNode* add_argument(ASTNode* list, ASTNode* arg);
ASTNode* mla_ast_format_expr(char* format_str, ASTNode* args, int line);
ASTNode* mla_ast_assert_eq(ASTNode* left, ASTNode* right, int line);
ASTNode* mla_ast_assert(ASTNode* condition, int line);
ASTNode* mla_ast_static_assert(ASTNode* condition, int line);
ASTNode* mla_ast_unsafe_block(ASTNode* block, int line);
ASTNode* mla_ast_break_stmt(int line);
ASTNode* mla_ast_continue_stmt(int line);
ASTNode* mla_ast_generic_list_type(ASTNode* element_type);
ASTNode* mla_ast_map_type(ASTNode* key_type, ASTNode* value_type);
ASTNode* mla_ast_map_literal(ASTNode* entries);
ASTNode* mla_ast_map_entry_list(ASTNode* entry);
ASTNode* add_map_entry(ASTNode* list, ASTNode* entry);
ASTNode* mla_ast_map_entry(ASTNode* key, ASTNode* value);
ASTNode* mla_ast_map_entry(ASTNode* key, ASTNode* value);
ASTNode* mla_ast_map_entry_list_create(ASTNode* entry);
ASTNode* mla_ast_map_entry_list_add(ASTNode* list, ASTNode* entry);
ASTNode* mla_ast_map_literal(ASTNode* entries);
ASTNode* mla_ast_index_expression(ASTNode* base, ASTNode* index, int line);
ASTNode* mla_ast_tuple_type(ASTNode* type_list);
ASTNode* mla_ast_type_list(ASTNode* type);
ASTNode* add_type_to_list(ASTNode* list, ASTNode* type);
ASTNode* mla_ast_tuple_literal(ASTNode* elements);
ASTNode* mla_ast_tuple_access(ASTNode* tuple, int index, int line);
ASTNode* create_map_keys_iterator(ASTNode* map_expr, int line);
ASTNode* create_map_values_iterator(ASTNode* map_expr, int line);
ASTNode* create_map_entries_iterator(ASTNode* map_expr, int line);
ASTNode* mla_ast_struct_type_ref(char* name);
ASTNode* mla_ast_generic_struct_type_ref(char* name, ASTNode* type_args);
ASTNode* create_len_expression(ASTNode* expr, int line);
ASTNode* mla_ast_method_call(ASTNode* object, char* method, ASTNode* args, int line);
// Generic structs and impl blocks
ASTNode* create_type_param_list(char* param);
ASTNode* add_type_param(ASTNode* list, char* param);
ASTNode* mla_ast_generic_struct_def(char* name, char* base_name, ASTNode* type_params, ASTNode* members, int is_public, int derive_debug);
ASTNode* mla_ast_trait_def(char* name, int line);
ASTNode* mla_ast_impl_block(char* struct_name, ASTNode* type_params, char* trait_name);
ASTNode* mla_ast_impl_add_method(ASTNode* impl, ASTNode* method);
ASTNode* mla_ast_struct_literal(char* struct_name, ASTNode* type_args, ASTNode* fields, int line);
ASTNode* mla_ast_struct_field_init_list(char* field_name, ASTNode* value);
ASTNode* mla_ast_struct_field_init_list_add(ASTNode* list, char* field_name, ASTNode* value);
ASTNode* create_match_pattern(char* name, char* binding, int line);
ASTNode* create_match_literal_pattern(ASTNode* literal, int line);
ASTNode* create_match_arm(ASTNode* pattern, ASTNode* expr, int line);
ASTNode* create_match_arm_list(ASTNode* arm);
ASTNode* add_match_arm(ASTNode* list, ASTNode* arm);
ASTNode* create_match_expression(ASTNode* target, ASTNode* arms, int line);
ASTNode* mla_ast_enum_variant_list(ASTNode* variant);
ASTNode* mla_ast_enum_variant_list_add(ASTNode* list, ASTNode* variant);
ASTNode* mla_ast_enum_literal(char* enum_name, char* variant_name, int line);
ASTNode* mla_ast_pointer_type(ASTNode* element_type);
ASTNode* mla_ast_reference_type(ASTNode* element_type, int is_mutable);
ASTNode* create_closure(ASTNode* body);
ASTNode* create_closure_with_params(ASTNode* params, ASTNode* body);
ASTNode* mla_ast_for_enumerate(char* index_var, char* val_var, ASTNode* iterable,
                            ASTNode* body, int line);
ASTNode* mla_ast_array_fill(ASTNode* value, ASTNode* count);
ASTNode* mla_ast_update_expression(int kind, int is_prefix, ASTNode* operand,
                                   int line);

// Desugar `lhs op= rhs` into `lhs = lhs op rhs`.
// Only simple identifier LHS is supported; for other LHS forms an error is
// set and a dummy node is returned.
static ASTNode* make_compound_assign(ASTNode* lhs, int op, ASTNode* rhs,
                                     int line)
{
    if(auto* id = dynamic_cast<IdentifierNode*>(lhs))
    {
        // Fresh IdentifierNode for the read side — must not reuse lhs pointer.
        auto* readExpr = new IdentifierNode(id->name);
        readExpr->line = line;
        return mla_ast_assignment(const_cast<char*>(id->name.c_str()),
                                 mla_ast_binary_op(op, readExpr, rhs), line);
    }
    // Compound assignment on field/index LHS: build a chained assignment where
    // the lhs node serves as the write target and a clone serves as the read.
    // For now we reuse lhs in the read position (DAG), which is safe as long as
    // the AST is read-only after construction and nodes are never freed.
    return create_chained_field_assignment(
        lhs, mla_ast_binary_op(op, lhs, rhs), line);
}
ASTNode* create_deref_assignment(ASTNode* pointer_expr, ASTNode* expr, int line);

static void bind_impl_self_types(ImplBlockNode* implBlock)
{
    if(!implBlock)
    {
        return;
    }
    for(auto* method : implBlock->methods)
    {
        if(!method || !method->parameters)
        {
            continue;
        }
        for(auto* param : method->parameters->parameters)
        {
            if(!param || param->name != "self")
            {
                continue;
            }
            auto* selfType = dynamic_cast<StructTypeRefNode*>(param->type);
            if(selfType && selfType->structName == "Self")
            {
                param->type = new StructTypeRefNode(implBlock->structName);
            }
        }
    }
}
enum UpdateKind
{
    UPDATE_INCREMENT = 0,
    UPDATE_DECREMENT = 1
};

enum UpdatePosition
{
    UPDATE_PREFIX = 1,
    UPDATE_POSTFIX = 0
};
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
%token FUNCTION RETURN IF ELSE VOID BOOL FLOAT DOUBLE STR8 STR16 LIST MAP TUPLE PTR STRUCT ENUM
%token QUESTION TRY_QUESTION
%token ELLIPSIS
%token MATCH
%token PUB IMPL TRAIT
%token EXTERN
%token STATIC
%token TRUE_LIT FALSE_LIT
%token I8 I16 I32 I64 U8 U16 U32 U64
%token LET VAR
%token FOR WHILE IN DOTDOT DOTDOTEQ BREAK CONTINUE
%token MOD USE TYPE_KW COLONCOLON
%token PRINTLN PRINT EPRINTLN EPRINT DEBUGPRINT FORMAT ASSERT_EQ ASSERT STATIC_ASSERT UNSAFE
%token PLUS_PLUS MINUS_MINUS
%token PLUS MINUS MULTIPLY DIVIDE MODULO ASSIGN AMP AMP_MUT AMP_AMP PIPE PIPE_PIPE CARET NOT TILDE SHL SHR
%token PLUS_ASSIGN MINUS_ASSIGN MULTIPLY_ASSIGN DIVIDE_ASSIGN MODULO_ASSIGN PIPE_ASSIGN CARET_ASSIGN SHL_ASSIGN SHR_ASSIGN
%token LT GT LE GE EQ NE SPACESHIP
%token LBRACE RBRACE LPAREN RPAREN LBRACKET RBRACKET SEMICOLON COMMA ARROW COLON DOT
%token FAT_ARROW
%token GENERIC_LT
%token KEYS_METHOD VALUES_METHOD ENTRIES_METHOD
%token ITER_METHOD INTO_ITER_METHOD ENUMERATE_METHOD
%token ITER_ENUMERATE_METHOD INTO_ITER_ENUMERATE_METHOD
%token CAST_INT CAST_FLOAT CAST_DOUBLE
%token VEC_MACRO
%token DERIVE_DEBUG
%token TEST_ATTR
%token INLINE_ATTR
%token INLINE_ALWAYS_ATTR
%token INLINE_NEVER_ATTR

%type <ast> program top_level_list top_level_item test_function_def
%type <ast> inline_function_def
%type <ast> type_alias_def
%type <ast> struct_def enum_def enum_variant_list enum_variant
%type <ast> function_def type parameter_list parameters parameter
%type <ast> statement_list statement expression ternary_expression cast_expression
%type <ast> condition_expression condition_logical_or condition_logical_and
%type <ast> condition_equality condition_relational condition_additive
%type <ast> condition_multiplicative condition_unary condition_postfix
%type <ast> condition_primary
%type <ast> if_statement else_if_list else_if optional_else
%type <ast> struct_member_list struct_member struct_method struct_init
%type <ast> list_literal list_elements
%type <ast> let_statement var_statement assignment_statement expression_statement
%type <ast> return_statement block_statement for_statement while_statement range_expression
%type <ast> break_statement continue_statement
%type <ast> primary_expression postfix_expression unary_expression binary_expression function_call
%type <ast> mod_declaration use_declaration
%type <sval> module_path
%type <ast> print_statement argument_list assert_eq_statement assert_statement static_assert_statement
%type <ast> global_var_statement static_var_statement
%type <ast> map_literal map_entries map_entry index_expression
%type <ast> tuple_type type_list tuple_literal tuple_elements
%type <ast> map_iterator
%type <ast> type_param_list impl_block impl_method_list struct_literal struct_field_init_list trait_def trait_method_decl_list trait_method_decl
%type <ast> match_expression match_arm_list match_arm match_pattern match_target match_atom match_binary_expression
%type <ival> enum_base_type_opt enum_int_type

%left PIPE_PIPE
%left AMP_AMP
%left PIPE
%left CARET
%left AMP
%left LT GT LE GE EQ NE SPACESHIP
%left SHL SHR
%left PLUS MINUS
%left MULTIPLY DIVIDE MODULO

%start program

%%

program
    : top_level_list {
        $$ = mla_ast_program($1);
        programRoot = $$;  /* Store the result in the global variable */
    }
    ;

top_level_list
    : top_level_item { $$ = mla_ast_top_level_list($1); }
    | top_level_list top_level_item { $$ = mla_ast_add_to_top_level_list($1, $2); }
    ;

top_level_item
    : struct_def
    | enum_def
    | trait_def
    | function_def
    | test_function_def
    | inline_function_def
    | mod_declaration
    | use_declaration
    | type_alias_def
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
        { $$ = mla_ast_mod_declaration($2, yylineno); }
    ;

use_declaration
    : USE module_path COLONCOLON IDENTIFIER SEMICOLON
        { $$ = mla_ast_use_declaration($2, $4, yylineno); }
    | USE module_path COLONCOLON MULTIPLY SEMICOLON
        { $$ = mla_ast_use_all_declaration($2, yylineno); }
    ;

type_alias_def
    : USE TYPE_KW IDENTIFIER ASSIGN type SEMICOLON
        { auto* node = mla_ast_type_alias($3, NULL, $5); node->line = yylineno; node->col = yycolumn_token; $$ = node; }
    | USE TYPE_KW IDENTIFIER GENERIC_LT type_param_list GT ASSIGN type SEMICOLON
        { auto* node = mla_ast_type_alias($3, $5, $8); node->line = yylineno; node->col = yycolumn_token; $$ = node; }
    ;

struct_def
    : STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($2, NULL, $4, 0, 0); node->line = yylineno; $$ = node; }
    | STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($2, $4, $6, 0, 0); node->line = yylineno; $$ = node; }
    | PUB STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($3, NULL, $5, 1, 0); node->line = yylineno; $$ = node; }
    | PUB STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($3, $5, $7, 1, 0); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($3, NULL, $5, 0, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($3, $5, $7, 0, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG PUB STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($4, NULL, $6, 1, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG PUB STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_struct_def($4, $6, $8, 1, 1); node->line = yylineno; $$ = node; }
    /* Generic struct definitions */
    | STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_generic_struct_def($2, NULL, $4, $7, 0, 0); node->line = yylineno; $$ = node; }
    | PUB STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_generic_struct_def($3, NULL, $5, $8, 1, 0); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_generic_struct_def($3, NULL, $5, $8, 0, 1); node->line = yylineno; $$ = node; }
    | DERIVE_DEBUG PUB STRUCT IDENTIFIER GENERIC_LT type_param_list GT LBRACE struct_member_list RBRACE SEMICOLON
        { auto* node = mla_ast_generic_struct_def($4, NULL, $6, $9, 1, 1); node->line = yylineno; $$ = node; }
    ;

enum_def
    : ENUM IDENTIFIER enum_base_type_opt LBRACE enum_variant_list RBRACE SEMICOLON
        { auto* node = mla_ast_enum_def($2, $5, 0, static_cast<int>($3)); node->line = yylineno; $$ = node; }
    | PUB ENUM IDENTIFIER enum_base_type_opt LBRACE enum_variant_list RBRACE SEMICOLON
        { auto* node = mla_ast_enum_def($3, $6, 1, static_cast<int>($4)); node->line = yylineno; $$ = node; }
    ;

enum_base_type_opt
    : /* empty */ { $$ = TypeNode::TYPE_I32; }
    | COLON enum_int_type { $$ = $2; }
    ;

enum_int_type
    : I8  { $$ = TypeNode::TYPE_I8; }
    | I16 { $$ = TypeNode::TYPE_I16; }
    | I32 { $$ = TypeNode::TYPE_I32; }
    | I64 { $$ = TypeNode::TYPE_I64; }
    | U8  { $$ = TypeNode::TYPE_U8; }
    | U16 { $$ = TypeNode::TYPE_U16; }
    | U32 { $$ = TypeNode::TYPE_U32; }
    | U64 { $$ = TypeNode::TYPE_U64; }
    ;

enum_variant_list
    : enum_variant { $$ = mla_ast_enum_variant_list($1); }
    | enum_variant_list COMMA enum_variant { $$ = mla_ast_enum_variant_list_add($1, $3); }
    ;

enum_variant
    : IDENTIFIER { $$ = mla_ast_enum_variant($1, 0, 0); }
    | IDENTIFIER ASSIGN INT_LITERAL { $$ = mla_ast_enum_variant($1, 1, $3); }
    | IDENTIFIER ASSIGN MINUS INT_LITERAL { $$ = mla_ast_enum_variant($1, 1, -$4); }
    | IDENTIFIER ASSIGN module_path COLONCOLON IDENTIFIER
        { $$ = mla_ast_enum_variant_ref($1, $3, $5); }
    ;

trait_def
    : TRAIT IDENTIFIER LBRACE trait_method_decl_list RBRACE
        { $$ = mla_ast_trait_def($2, yylineno); }
    ;

trait_method_decl_list
    : /* empty */ { $$ = NULL; }
    | trait_method_decl_list trait_method_decl { $$ = NULL; }
    ;

trait_method_decl
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type SEMICOLON
        { $$ = NULL; }
    | PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type SEMICOLON
        { $$ = NULL; }
    ;

type_param_list
    : IDENTIFIER { $$ = create_type_param_list($1); }
    | type_param_list COMMA IDENTIFIER { $$ = add_type_param($1, $3); }
    ;

impl_block
    : IMPL IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = mla_ast_impl_block($2, NULL, NULL);
            // Copy methods from temp list
            $$ = impl;
            // Methods are added via impl_method_list
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($4);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            bind_impl_self_types(implBlock);
        }
    | IMPL IDENTIFIER FOR IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = mla_ast_impl_block($4, NULL, $2);
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($6);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            bind_impl_self_types(implBlock);
            $$ = impl;
        }
    | IMPL LT type_param_list GT IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = mla_ast_impl_block($5, $3, NULL);
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($7);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            bind_impl_self_types(implBlock);
            $$ = impl;
        }
    | IMPL GENERIC_LT type_param_list GT IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = mla_ast_impl_block($5, $3, NULL);
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($7);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            bind_impl_self_types(implBlock);
            $$ = impl;
        }
    | IMPL LT type_param_list GT IDENTIFIER FOR IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = mla_ast_impl_block($7, $3, $5);
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($9);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            bind_impl_self_types(implBlock);
            $$ = impl;
        }
    | IMPL GENERIC_LT type_param_list GT IDENTIFIER FOR IDENTIFIER LBRACE impl_method_list RBRACE
        {
            ASTNode* impl = mla_ast_impl_block($7, $3, $5);
            auto* implBlock = static_cast<ImplBlockNode*>(impl);
            auto* methodList = static_cast<ImplBlockNode*>($9);
            if(methodList) {
                implBlock->methods = methodList->methods;
            }
            bind_impl_self_types(implBlock);
            $$ = impl;
        }
    ;

impl_method_list
    : /* empty */ { $$ = mla_ast_impl_block(strdup(""), NULL, NULL); }
    | impl_method_list struct_method
        {
            $$ = mla_ast_impl_add_method($1, $2);
        }
    ;
    ;

struct_member_list
    : struct_member { $$ = mla_ast_struct_member_list($1); }
    | struct_member_list struct_member { $$ = mla_ast_struct_member_list_add($1, $2); }
    | struct_method { $$ = mla_ast_struct_member_list($1); }
    | struct_member_list struct_method { $$ = mla_ast_struct_member_add_method($1, $2); }
    ;

struct_member
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = mla_ast_struct_member(0, $4, $2, $6); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = mla_ast_struct_member(1, $4, $2, NULL); }
    | enum_def
        { $$ = $1; }
    ;

struct_method
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { $$ = mla_ast_struct_method($7, $2, $4, $9, 0, 0); }
    | PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { $$ = mla_ast_struct_method($8, $3, $5, $10, 1, 0); }
    ;

function_def
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { auto* node = mla_ast_function_def($7, $2, $4, $9, 0, 0); node->line = yylineno; $$ = node; }
    | PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { auto* node = mla_ast_function_def($8, $3, $5, $10, 1, 0); node->line = yylineno; $$ = node; }
    | FUNCTION IDENTIFIER LPAREN parameter_list RPAREN LBRACE statement_list RBRACE
        {
            TypeNode* inferred = nullptr;
            if(strcmp($2, "main") == 0)
                inferred = static_cast<TypeNode*>(mla_ast_type_node(TypeNode::TYPE_I32));
            auto* node = mla_ast_function_def(inferred, $2, $4, $7, 0, 0);
            node->line = yylineno;
            $$ = node;
        }
    | PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN LBRACE statement_list RBRACE
        {
            TypeNode* inferred = nullptr;
            if(strcmp($3, "main") == 0)
                inferred = static_cast<TypeNode*>(mla_ast_type_node(TypeNode::TYPE_I32));
            auto* node = mla_ast_function_def(inferred, $3, $5, $8, 1, 0);
            node->line = yylineno;
            $$ = node;
        }
    | EXTERN FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type SEMICOLON
        { auto* node = mla_ast_function_def($8, $3, $5, NULL, 0, 1); node->line = yylineno; $$ = node; }
    | EXTERN PUB FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type SEMICOLON
        { auto* node = mla_ast_function_def($9, $4, $6, NULL, 1, 1); node->line = yylineno; $$ = node; }
    ;

test_function_def
    : TEST_ATTR function_def
        { static_cast<FunctionDefNode*>($2)->isTest = true; $$ = $2; }
    ;

inline_function_def
    : INLINE_ATTR function_def
        { static_cast<FunctionDefNode*>($2)->isInline = true; $$ = $2; }
    | INLINE_ALWAYS_ATTR function_def
        { static_cast<FunctionDefNode*>($2)->isInlineAlways = true; $$ = $2; }
    | INLINE_NEVER_ATTR function_def
        { static_cast<FunctionDefNode*>($2)->isInlineNever = true; $$ = $2; }
    ;

parameter_list
    : /* empty */ { $$ = mla_ast_empty_parameter_list(); }
    | parameters
    | parameters COMMA ELLIPSIS { $$ = set_parameter_list_vararg($1); }
    | ELLIPSIS { $$ = set_parameter_list_vararg(mla_ast_empty_parameter_list()); }
    ;

parameters
    : parameter { $$ = mla_ast_parameter_list($1); }
    | parameters COMMA parameter { $$ = add_parameter($1, $3); }
    ;

parameter
    : IDENTIFIER COLON type { $$ = mla_ast_parameter($3, $1); }
    | AMP IDENTIFIER
        { $$ = mla_ast_parameter(mla_ast_struct_type_ref(strdup("Self")), $2); }
    ;

type
    : VOID   { $$ = mla_ast_type_node(TypeNode::TYPE_VOID); }
    | BOOL   { $$ = mla_ast_type_node(TypeNode::TYPE_BOOL); }
    | FLOAT  { $$ = mla_ast_type_node(TypeNode::TYPE_FLOAT); }
    | DOUBLE { $$ = mla_ast_type_node(TypeNode::TYPE_DOUBLE); }
    | STR8   { $$ = mla_ast_type_node(TypeNode::TYPE_STR8); }
    | STR16  { $$ = mla_ast_type_node(TypeNode::TYPE_STR16); }
    | LIST   { $$ = mla_ast_list_type(); }
    | LIST GENERIC_LT type GT { $$ = mla_ast_generic_list_type($3); }
    | MAP GENERIC_LT type COMMA type GT { $$ = mla_ast_map_type($3, $5); }
    | tuple_type
    | PTR GENERIC_LT type GT { $$ = mla_ast_pointer_type($3); }
    | AMP type               { $$ = mla_ast_reference_type($2, 0); }
    | AMP_MUT type           { $$ = mla_ast_reference_type($2, 1); }
    | LBRACKET type SEMICOLON expression RBRACKET
        { $$ = mla_ast_generic_list_type($2); /* [T; N] is list<T>, N ignored */ }
    | I8     { $$ = mla_ast_type_node(TypeNode::TYPE_I8); }
    | I16    { $$ = mla_ast_type_node(TypeNode::TYPE_I16); }
    | I32    { $$ = mla_ast_type_node(TypeNode::TYPE_I32); }
    | I64    { $$ = mla_ast_type_node(TypeNode::TYPE_I64); }
    | U8     { $$ = mla_ast_type_node(TypeNode::TYPE_U8); }
    | U16    { $$ = mla_ast_type_node(TypeNode::TYPE_U16); }
    | U32    { $$ = mla_ast_type_node(TypeNode::TYPE_U32); }
    | U64    { $$ = mla_ast_type_node(TypeNode::TYPE_U64); }
    | module_path { $$ = mla_ast_struct_type_ref($1); }
    | module_path GENERIC_LT type_list GT
        {
            auto* list = static_cast<TypeListNode*>($3);
            // Vec<T> is a built-in alias for list<T>; for N != 1 fall back to generic struct.
            if(strcmp($1, "Vec") == 0 && list && list->types.size() == 1)
                $$ = mla_ast_generic_list_type(list->types[0]);
            else
                $$ = mla_ast_generic_struct_type_ref($1, $3);
        }
    ;

tuple_type
    : TUPLE GENERIC_LT type_list GT { $$ = mla_ast_tuple_type($3); }
    ;

type_list
    : type { $$ = mla_ast_type_list($1); }
    | type_list COMMA type { $$ = add_type_to_list($1, $3); }
    ;

statement_list
    : statement { $$ = mla_ast_statement_list_create($1); }
    | statement_list statement { $$ = mla_ast_statement_list_add($1, $2); }
    ;

statement
    : let_statement
    | var_statement
    | type_alias_def
    | static_var_statement
    | assignment_statement
    | expression_statement
    | return_statement
    | if_statement
    | for_statement
    | while_statement
    | block_statement
    | struct_init
    | print_statement
    | assert_eq_statement
    | assert_statement
    | static_assert_statement
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
        { $$ = mla_ast_var_declaration($4, $2, $6); }
    | VAR IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = mla_ast_var_declaration(NULL, $2, $4); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = mla_ast_var_declaration($4, $2, NULL); }
    ;

global_var_statement
    : VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        {
            $$ = mla_ast_var_declaration($4, $2, $6);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isGlobalStorage = true;
        }
    | VAR IDENTIFIER ASSIGN expression SEMICOLON
        {
            $$ = mla_ast_var_declaration(NULL, $2, $4);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isGlobalStorage = true;
        }
    | VAR IDENTIFIER COLON type SEMICOLON
        {
            $$ = mla_ast_var_declaration($4, $2, NULL);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isGlobalStorage = true;
        }
    ;

static_var_statement
    : STATIC VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        {
            $$ = mla_ast_var_declaration($5, $3, $7);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isStaticStorage = true;
        }
    | STATIC VAR IDENTIFIER ASSIGN expression SEMICOLON
        {
            $$ = mla_ast_var_declaration(NULL, $3, $5);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isStaticStorage = true;
        }
    | STATIC VAR IDENTIFIER COLON type SEMICOLON
        {
            $$ = mla_ast_var_declaration($5, $3, NULL);
            if(auto* n = dynamic_cast<VarDeclNode*>($$))
                n->isStaticStorage = true;
        }
    ;

assignment_statement
    : postfix_expression ASSIGN expression SEMICOLON
        {
            if(auto* id = dynamic_cast<IdentifierNode*>($1))
                $$ = mla_ast_assignment(const_cast<char*>(id->name.c_str()), $3, yylineno);
            else
                $$ = mla_ast_chained_field_assignment($1, $3, yylineno);
        }
    | postfix_expression PLUS_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, PLUS, $3, yylineno); }
    | postfix_expression MINUS_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, MINUS, $3, yylineno); }
    | postfix_expression MULTIPLY_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, MULTIPLY, $3, yylineno); }
    | postfix_expression DIVIDE_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, DIVIDE, $3, yylineno); }
    | postfix_expression MODULO_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, MODULO, $3, yylineno); }
    | postfix_expression PIPE_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, PIPE, $3, yylineno); }
    | postfix_expression CARET_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, CARET, $3, yylineno); }
    | postfix_expression SHL_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, SHL, $3, yylineno); }
    | postfix_expression SHR_ASSIGN expression SEMICOLON
        { $$ = make_compound_assign($1, SHR, $3, yylineno); }
    | MULTIPLY unary_expression ASSIGN expression SEMICOLON
        { $$ = mla_ast_deref_assignment($2, $4, yylineno); }
    ;

expression_statement
    : expression SEMICOLON { $$ = mla_ast_expression_statement($1); }
    ;

return_statement
    : RETURN expression SEMICOLON { $$ = mla_ast_return_stmt($2); }
    | RETURN SEMICOLON { $$ = mla_ast_return_stmt(NULL); }
    ;

break_statement
    : BREAK SEMICOLON { $$ = mla_ast_break_stmt(yylineno); }
    ;

continue_statement
    : CONTINUE SEMICOLON { $$ = mla_ast_continue_stmt(yylineno); }
    ;

block_statement
    : LBRACE statement_list RBRACE { $$ = mla_ast_block_statement($2); static_cast<BlockStatementNode*>($$)->line = yylineno; static_cast<BlockStatementNode*>($$)->col = yycolumn_token; }
    | LBRACE RBRACE { $$ = mla_ast_block_statement(create_empty_statement_list()); static_cast<BlockStatementNode*>($$)->line = yylineno; static_cast<BlockStatementNode*>($$)->col = yycolumn_token; }
    | UNSAFE block_statement
        { $$ = mla_ast_unsafe_block($2, yylineno); }
    ;

for_statement
    : FOR IDENTIFIER IN range_expression block_statement
        { $$ = mla_ast_for_range($2, $4, $5, yylineno); }
    | FOR IDENTIFIER IN primary_expression block_statement
        { $$ = mla_ast_for_iterator($2, $4, $5, yylineno); }
    | FOR IDENTIFIER IN primary_expression KEYS_METHOD block_statement
        { $$ = mla_ast_for_iterator($2, mla_ast_map_keys_iterator($4, yylineno), $6, yylineno); }
    | FOR IDENTIFIER IN primary_expression VALUES_METHOD block_statement
        { $$ = mla_ast_for_iterator($2, mla_ast_map_values_iterator($4, yylineno), $6, yylineno); }
    | FOR IDENTIFIER IN primary_expression ENTRIES_METHOD block_statement
        { $$ = mla_ast_for_iterator($2, mla_ast_map_entries_iterator($4, yylineno), $6, yylineno); }
    /* for x in coll.iter() / .into_iter() — strip the no-op method */
    | FOR IDENTIFIER IN primary_expression ITER_METHOD block_statement
        { $$ = mla_ast_for_iterator($2, $4, $6, yylineno); }
    | FOR IDENTIFIER IN primary_expression INTO_ITER_METHOD block_statement
        { $$ = mla_ast_for_iterator($2, $4, $6, yylineno); }
    /* for (i, x) in coll  — enumerate style (index var, value var) */
    | FOR LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN IN primary_expression block_statement
        { $$ = mla_ast_for_enumerate($3, $5, $8, $9, yylineno); }
    | FOR LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN IN range_expression block_statement
        { $$ = mla_ast_for_enumerate($3, $5, $8, $9, yylineno); }
    /* for (i, x) in coll.enumerate() / .iter().enumerate() / .into_iter().enumerate() */
    | FOR LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN IN primary_expression ENUMERATE_METHOD block_statement
        { $$ = mla_ast_for_enumerate($3, $5, $8, $10, yylineno); }
    | FOR LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN IN primary_expression ITER_ENUMERATE_METHOD block_statement
        { $$ = mla_ast_for_enumerate($3, $5, $8, $10, yylineno); }
    | FOR LPAREN IDENTIFIER COMMA IDENTIFIER RPAREN IN primary_expression INTO_ITER_ENUMERATE_METHOD block_statement
        { $$ = mla_ast_for_enumerate($3, $5, $8, $10, yylineno); }
    ;

while_statement
    : WHILE condition_expression COLON block_statement
        { $$ = mla_ast_while_statement($2, $4, yylineno, 1); static_cast<WhileNode*>($$)->col = yycolumn_token; }
    | WHILE condition_expression COLON statement
        { $$ = mla_ast_while_statement($2, mla_ast_statement_list_create($4), yylineno, 1); static_cast<WhileNode*>($$)->col = yycolumn_token; }
    | WHILE condition_expression COLON expression block_statement
        { $$ = mla_ast_while_statement(mla_ast_binary_op(AMP_AMP, $2, $4), $5, yylineno, 0); static_cast<WhileNode*>($$)->col = yycolumn_token; }
    | WHILE condition_expression COLON expression statement
        { $$ = mla_ast_while_statement(mla_ast_binary_op(AMP_AMP, $2, $4), mla_ast_statement_list_create($5), yylineno, 0); static_cast<WhileNode*>($$)->col = yycolumn_token; }
    | WHILE condition_expression block_statement
        { $$ = mla_ast_while_statement($2, $3, yylineno, 0); static_cast<WhileNode*>($$)->col = yycolumn_token; }
    ;

range_expression
    : postfix_expression DOTDOT postfix_expression
        { $$ = mla_ast_range_expression($1, $3, 0); }
    | postfix_expression DOTDOTEQ postfix_expression
        { $$ = mla_ast_range_expression($1, $3, 1); }
    | postfix_expression DOTDOT postfix_expression PLUS postfix_expression
        { $$ = mla_ast_range_expression($1, mla_ast_binary_op(PLUS, $3, $5), 0); }
    | postfix_expression DOTDOT postfix_expression MINUS postfix_expression
        { $$ = mla_ast_range_expression($1, mla_ast_binary_op(MINUS, $3, $5), 0); }
    | postfix_expression DOTDOTEQ postfix_expression PLUS postfix_expression
        { $$ = mla_ast_range_expression($1, mla_ast_binary_op(PLUS, $3, $5), 1); }
    | postfix_expression DOTDOTEQ postfix_expression MINUS postfix_expression
        { $$ = mla_ast_range_expression($1, mla_ast_binary_op(MINUS, $3, $5), 1); }
    | postfix_expression PLUS postfix_expression DOTDOT postfix_expression
        { $$ = mla_ast_range_expression(mla_ast_binary_op(PLUS, $1, $3), $5, 0); }
    | postfix_expression MINUS postfix_expression DOTDOT postfix_expression
        { $$ = mla_ast_range_expression(mla_ast_binary_op(MINUS, $1, $3), $5, 0); }
    | postfix_expression PLUS postfix_expression DOTDOTEQ postfix_expression
        { $$ = mla_ast_range_expression(mla_ast_binary_op(PLUS, $1, $3), $5, 1); }
    | postfix_expression MINUS postfix_expression DOTDOTEQ postfix_expression
        { $$ = mla_ast_range_expression(mla_ast_binary_op(MINUS, $1, $3), $5, 1); }
    ;

struct_init
    : IDENTIFIER IDENTIFIER SEMICOLON
        { $$ = mla_ast_struct_init($1, $2); }
    ;

print_statement
    : PRINTLN LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(1, $3, NULL, yylineno); }
    | PRINTLN LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(1, $3, $5, yylineno); }
    | PRINTLN LPAREN expression RPAREN SEMICOLON
        { $$ = mla_ast_print_expr_stmt(1, $3, yylineno); }
    | PRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(0, $3, NULL, yylineno); }
    | PRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(0, $3, $5, yylineno); }
    | PRINT LPAREN expression RPAREN SEMICOLON
        { $$ = mla_ast_print_expr_stmt(0, $3, yylineno); }
    | EPRINTLN LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(3, $3, NULL, yylineno); }
    | EPRINTLN LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(3, $3, $5, yylineno); }
    | EPRINTLN LPAREN expression RPAREN SEMICOLON
        { $$ = mla_ast_print_expr_stmt(3, $3, yylineno); }
    | EPRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(2, $3, NULL, yylineno); }
    | EPRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = mla_ast_print_stmt(2, $3, $5, yylineno); }
    | EPRINT LPAREN expression RPAREN SEMICOLON
        { $$ = mla_ast_print_expr_stmt(2, $3, yylineno); }
    | DEBUGPRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = mla_ast_debug_print_stmt($3, NULL, yylineno); }
    | DEBUGPRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = mla_ast_debug_print_stmt($3, $5, yylineno); }
    ;

assert_eq_statement
    : ASSERT_EQ LPAREN expression COMMA expression RPAREN SEMICOLON
        { $$ = mla_ast_assert_eq($3, $5, yylineno); }
    ;

assert_statement
    : ASSERT LPAREN expression RPAREN SEMICOLON
        { $$ = mla_ast_assert($3, yylineno); }
    ;

static_assert_statement
    : STATIC_ASSERT LPAREN expression RPAREN SEMICOLON
        { $$ = mla_ast_static_assert($3, yylineno); }
    ;

argument_list
    : expression { $$ = mla_ast_argument_list_create($1); }
    | argument_list COMMA expression { $$ = mla_ast_argument_list_add($1, $3); }
    ;

if_statement
    : IF condition_expression COLON block_statement else_if_list optional_else
        { $$ = mla_ast_if_statement($2, $4, $5, $6); static_cast<IfNode*>($$)->usesColonWithoutGuard = true; static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | IF condition_expression COLON statement else_if_list optional_else
        { $$ = mla_ast_if_statement($2, mla_ast_statement_list_create($4), $5, $6); static_cast<IfNode*>($$)->usesColonWithoutGuard = true; static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | IF condition_expression COLON expression block_statement else_if_list optional_else
        { $$ = mla_ast_if_statement(mla_ast_binary_op(AMP_AMP, $2, $4), $5, $6, $7); static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | IF condition_expression COLON expression statement else_if_list optional_else
        { $$ = mla_ast_if_statement(mla_ast_binary_op(AMP_AMP, $2, $4), mla_ast_statement_list_create($5), $6, $7); static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | IF condition_expression block_statement else_if_list optional_else
        { $$ = mla_ast_if_statement($2, $3, $4, $5); static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | IF LET IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON block_statement else_if_list optional_else
        { ASTNode* __init = mla_ast_let_declaration($5, $3, $7); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $9, $11, $12, $13); }
    | IF LET IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON statement else_if_list optional_else
        { ASTNode* __init = mla_ast_let_declaration($5, $3, $7); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $9, mla_ast_statement_list_create($11), $12, $13); }
    | IF LET IDENTIFIER ASSIGN expression COLON condition_expression COLON block_statement else_if_list optional_else
        { ASTNode* __init = mla_ast_let_declaration(NULL, $3, $5); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $7, $9, $10, $11); }
    | IF LET IDENTIFIER ASSIGN expression COLON condition_expression COLON statement else_if_list optional_else
        { ASTNode* __init = mla_ast_let_declaration(NULL, $3, $5); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $7, mla_ast_statement_list_create($9), $10, $11); }
    | IF LET IDENTIFIER EQ expression COLON condition_expression block_statement else_if_list optional_else
        { ASTNode* __init = mla_ast_let_declaration(NULL, $3, $5); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $7, $8, $9, $10); }
    | IF LET IDENTIFIER EQ expression COLON condition_expression statement else_if_list optional_else
        { ASTNode* __init = mla_ast_let_declaration(NULL, $3, $5); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $7, mla_ast_statement_list_create($8), $9, $10); }
    | IF VAR IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON block_statement else_if_list optional_else
        { ASTNode* __init = mla_ast_var_declaration($5, $3, $7); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $9, $11, $12, $13); }
    | IF VAR IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON statement else_if_list optional_else
        { ASTNode* __init = mla_ast_var_declaration($5, $3, $7); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $9, mla_ast_statement_list_create($11), $12, $13); }
    | IF VAR IDENTIFIER ASSIGN expression COLON condition_expression COLON block_statement else_if_list optional_else
        { ASTNode* __init = mla_ast_var_declaration(NULL, $3, $5); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $7, $9, $10, $11); }
    | IF VAR IDENTIFIER ASSIGN expression COLON condition_expression COLON statement else_if_list optional_else
        { ASTNode* __init = mla_ast_var_declaration(NULL, $3, $5); __init->line = @2.first_line; $$ = mla_ast_if_statement_with_init(__init, $7, mla_ast_statement_list_create($9), $10, $11); }
    ;

else_if_list
    : /* empty */ { $$ = NULL; }
    | else_if_list else_if { $$ = add_else_if($1, $2); }
    ;

else_if
    : ELSE IF condition_expression COLON block_statement { $$ = mla_ast_else_if($3, $5); static_cast<IfNode*>($$)->usesColonWithoutGuard = true; static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | ELSE IF condition_expression COLON statement { $$ = mla_ast_else_if($3, mla_ast_statement_list_create($5)); static_cast<IfNode*>($$)->usesColonWithoutGuard = true; static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | ELSE IF condition_expression COLON expression block_statement { $$ = mla_ast_else_if(mla_ast_binary_op(AMP_AMP, $3, $5), $6); static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | ELSE IF condition_expression COLON expression statement { $$ = mla_ast_else_if(mla_ast_binary_op(AMP_AMP, $3, $5), mla_ast_statement_list_create($6)); static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | ELSE IF condition_expression block_statement { $$ = mla_ast_else_if($3, $4); static_cast<IfNode*>($$)->line = yylineno; static_cast<IfNode*>($$)->col = yycolumn_token; }
    | ELSE IF LET IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON block_statement
        { ASTNode* __init = mla_ast_let_declaration($6, $4, $8); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $10, $12); }
    | ELSE IF LET IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON statement
        { ASTNode* __init = mla_ast_let_declaration($6, $4, $8); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $10, mla_ast_statement_list_create($12)); }
    | ELSE IF LET IDENTIFIER ASSIGN expression COLON condition_expression COLON block_statement
        { ASTNode* __init = mla_ast_let_declaration(NULL, $4, $6); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $8, $10); }
    | ELSE IF LET IDENTIFIER ASSIGN expression COLON condition_expression COLON statement
        { ASTNode* __init = mla_ast_let_declaration(NULL, $4, $6); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $8, mla_ast_statement_list_create($10)); }
    | ELSE IF LET IDENTIFIER EQ expression COLON condition_expression block_statement
        { ASTNode* __init = mla_ast_let_declaration(NULL, $4, $6); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $8, $9); }
    | ELSE IF LET IDENTIFIER EQ expression COLON condition_expression statement
        { ASTNode* __init = mla_ast_let_declaration(NULL, $4, $6); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $8, mla_ast_statement_list_create($9)); }
    | ELSE IF VAR IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON block_statement
        { ASTNode* __init = mla_ast_var_declaration($6, $4, $8); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $10, $12); }
    | ELSE IF VAR IDENTIFIER COLON type ASSIGN expression COLON condition_expression COLON statement
        { ASTNode* __init = mla_ast_var_declaration($6, $4, $8); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $10, mla_ast_statement_list_create($12)); }
    | ELSE IF VAR IDENTIFIER ASSIGN expression COLON condition_expression COLON block_statement
        { ASTNode* __init = mla_ast_var_declaration(NULL, $4, $6); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $8, $10); }
    | ELSE IF VAR IDENTIFIER ASSIGN expression COLON condition_expression COLON statement
        { ASTNode* __init = mla_ast_var_declaration(NULL, $4, $6); __init->line = @3.first_line; $$ = mla_ast_else_if_with_init(__init, $8, mla_ast_statement_list_create($10)); }
    ;

condition_expression
    : condition_logical_or
    ;

condition_logical_or
    : condition_logical_or PIPE_PIPE condition_logical_and
        { $$ = mla_ast_binary_op(PIPE_PIPE, $1, $3); }
    | condition_logical_and
    ;

condition_logical_and
    : condition_logical_and AMP_AMP condition_equality
        { $$ = mla_ast_binary_op(AMP_AMP, $1, $3); }
    | condition_equality
    ;

condition_equality
    : condition_equality EQ condition_relational
        { $$ = mla_ast_binary_op(EQ, $1, $3); }
    | condition_equality NE condition_relational
        { $$ = mla_ast_binary_op(NE, $1, $3); }
    | condition_relational
    ;

condition_relational
    : condition_relational LT condition_additive
        { $$ = mla_ast_binary_op(LT, $1, $3); }
    | condition_relational GT condition_additive
        { $$ = mla_ast_binary_op(GT, $1, $3); }
    | condition_relational LE condition_additive
        { $$ = mla_ast_binary_op(LE, $1, $3); }
    | condition_relational GE condition_additive
        { $$ = mla_ast_binary_op(GE, $1, $3); }
    | condition_relational SPACESHIP condition_additive
        { $$ = mla_ast_binary_op(SPACESHIP, $1, $3); }
    | condition_additive
    ;

condition_additive
    : condition_additive PLUS condition_multiplicative
        { $$ = mla_ast_binary_op(PLUS, $1, $3); }
    | condition_additive MINUS condition_multiplicative
        { $$ = mla_ast_binary_op(MINUS, $1, $3); }
    | condition_multiplicative
    ;

condition_multiplicative
    : condition_multiplicative MULTIPLY condition_unary
        { $$ = mla_ast_binary_op(MULTIPLY, $1, $3); }
    | condition_multiplicative DIVIDE condition_unary
        { $$ = mla_ast_binary_op(DIVIDE, $1, $3); }
    | condition_multiplicative MODULO condition_unary
        { $$ = mla_ast_binary_op(MODULO, $1, $3); }
    | condition_unary
    ;

condition_unary
    : MINUS condition_unary
        { $$ = create_unary_op(MINUS, $2); if($$) $$->line = yylineno; }
    | NOT condition_unary
        { $$ = create_unary_op(NOT, $2); if($$) $$->line = yylineno; }
    | TILDE condition_unary
        { $$ = create_unary_op(TILDE, $2); if($$) $$->line = yylineno; }
    | AMP condition_unary
        { $$ = create_unary_op(AMP, $2); if($$) $$->line = yylineno; }
    | AMP_MUT condition_unary
        { $$ = create_unary_op(AMP_MUT, $2); if($$) $$->line = yylineno; }
    | MULTIPLY condition_unary
        { $$ = create_unary_op(MULTIPLY, $2); if($$) $$->line = yylineno; }
    | condition_postfix TRY_QUESTION
        { $$ = mla_ast_try_expression($1, yylineno); }
    | function_call TRY_QUESTION
        { $$ = mla_ast_try_expression($1, yylineno); }
    | condition_postfix
    | function_call
    | cast_expression
    | list_literal
    | map_literal
    | index_expression
    | tuple_literal
    | map_iterator
    ;

condition_postfix
    : condition_primary { $$ = $1; }
    | condition_postfix DOT IDENTIFIER { $$ = mla_ast_field_access_expr($1, $3, yylineno); }
    | condition_postfix DOT IDENTIFIER LPAREN RPAREN
        { $$ = mla_ast_method_call_expr($1, $3, NULL, yylineno); }
    | condition_postfix DOT IDENTIFIER LPAREN argument_list RPAREN
        { $$ = mla_ast_method_call_expr($1, $3, $5, yylineno); }
    | condition_postfix DOT INT_LITERAL { $$ = mla_ast_tuple_access($1, $3, yylineno); }
    | condition_postfix LBRACKET expression RBRACKET
        { $$ = mla_ast_index_expression($1, $3, yylineno); }
    ;

condition_primary
    : INT_LITERAL { $$ = mla_ast_literal_int($1); }
    | FLOAT_LITERAL { $$ = mla_ast_literal_float($1); }
    | DOUBLE_LITERAL { $$ = mla_ast_literal_double($1); }
    | STRING_LITERAL { $$ = mla_ast_literal_string($1); }
    | TRUE_LIT { $$ = mla_ast_literal_bool(1); }
    | FALSE_LIT { $$ = mla_ast_literal_bool(0); }
    | FORMAT LPAREN STRING_LITERAL RPAREN
        { $$ = mla_ast_format_expr($3, NULL, yylineno); }
    | FORMAT LPAREN STRING_LITERAL COMMA argument_list RPAREN
        { $$ = mla_ast_format_expr($3, $5, yylineno); }
    | module_path
        { $$ = create_enum_or_ident_from_path($1, yylineno); }
    | LPAREN condition_expression RPAREN { $$ = $2; }
    | match_expression { $$ = $1; }
    | list_literal { $$ = $1; }
    | map_literal { $$ = $1; }
    | PIPE_PIPE LBRACE RBRACE
        { $$ = create_closure(NULL); }
    | PIPE_PIPE LBRACE statement_list RBRACE
        { $$ = create_closure($3); }
    | PIPE parameters PIPE LBRACE RBRACE
        { $$ = create_closure_with_params($2, NULL); }
    | PIPE parameters PIPE LBRACE statement_list RBRACE
        { $$ = create_closure_with_params($2, $5); }
    | LPAREN ELLIPSIS PLUS expression RPAREN
        { $$ = mla_ast_fold_expression(PLUS, $4, 0); }
    | LPAREN postfix_expression PLUS ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(PLUS, $2, 1); }
    | LPAREN ELLIPSIS MULTIPLY expression RPAREN
        { $$ = mla_ast_fold_expression(MULTIPLY, $4, 0); }
    | LPAREN postfix_expression MULTIPLY ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(MULTIPLY, $2, 1); }
    | LPAREN ELLIPSIS AMP_AMP expression RPAREN
        { $$ = mla_ast_fold_expression(AMP_AMP, $4, 0); }
    | LPAREN postfix_expression AMP_AMP ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(AMP_AMP, $2, 1); }
    | LPAREN ELLIPSIS PIPE_PIPE expression RPAREN
        { $$ = mla_ast_fold_expression(PIPE_PIPE, $4, 0); }
    | LPAREN postfix_expression PIPE_PIPE ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(PIPE_PIPE, $2, 1); }
    ;

optional_else
    : /* empty */ { $$ = NULL; }
    | ELSE COLON block_statement { $$ = $3; }
    | ELSE COLON statement { $$ = mla_ast_statement_list_create($3); }
    | ELSE block_statement { $$ = $2; }
    ;

expression
    : ternary_expression
    ;

ternary_expression
    : binary_expression QUESTION ternary_expression COLON ternary_expression
        { $$ = mla_ast_ternary_expression($1, $3, $5, yylineno); }
    | function_call QUESTION ternary_expression COLON ternary_expression
        { $$ = mla_ast_ternary_expression($1, $3, $5, yylineno); }
    | binary_expression
    | unary_expression
    ;

unary_expression
    : PLUS_PLUS unary_expression
        { $$ = mla_ast_update_expression(UPDATE_INCREMENT, UPDATE_PREFIX, $2, yylineno); }
    | MINUS_MINUS unary_expression
        { $$ = mla_ast_update_expression(UPDATE_DECREMENT, UPDATE_PREFIX, $2, yylineno); }
    | MINUS unary_expression
        { $$ = create_unary_op(MINUS, $2); if($$) $$->line = yylineno; }
    | NOT unary_expression
        { $$ = create_unary_op(NOT, $2); if($$) $$->line = yylineno; }
    | TILDE unary_expression
        { $$ = create_unary_op(TILDE, $2); if($$) $$->line = yylineno; }
    | AMP unary_expression
        { $$ = create_unary_op(AMP, $2); if($$) $$->line = yylineno; }
    | AMP_MUT unary_expression
        { $$ = create_unary_op(AMP_MUT, $2); if($$) $$->line = yylineno; }
    | MULTIPLY unary_expression
        { $$ = create_unary_op(MULTIPLY, $2); if($$) $$->line = yylineno; }
    | postfix_expression TRY_QUESTION
        { $$ = mla_ast_try_expression($1, yylineno); }
    | function_call TRY_QUESTION
        { $$ = mla_ast_try_expression($1, yylineno); }
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
        { $$ = mla_ast_match_expression($2, $4, yylineno); }
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
    : match_target PLUS match_target { $$ = mla_ast_binary_op(PLUS, $1, $3); }
    | match_target MINUS match_target { $$ = mla_ast_binary_op(MINUS, $1, $3); }
    | match_target MULTIPLY match_target { $$ = mla_ast_binary_op(MULTIPLY, $1, $3); }
    | match_target DIVIDE match_target { $$ = mla_ast_binary_op(DIVIDE, $1, $3); }
    | match_target MODULO match_target { $$ = mla_ast_binary_op(MODULO, $1, $3); }
    | match_target LT match_target { $$ = mla_ast_binary_op(LT, $1, $3); }
    | match_target GT match_target { $$ = mla_ast_binary_op(GT, $1, $3); }
    | match_target LE match_target { $$ = mla_ast_binary_op(LE, $1, $3); }
    | match_target GE match_target { $$ = mla_ast_binary_op(GE, $1, $3); }
    | match_target EQ match_target { $$ = mla_ast_binary_op(EQ, $1, $3); }
    | match_target NE match_target { $$ = mla_ast_binary_op(NE, $1, $3); }
    | match_target SPACESHIP match_target { $$ = mla_ast_binary_op(SPACESHIP, $1, $3); }
    ;

match_arm_list
    : match_arm { $$ = mla_ast_match_arm_list($1); }
    | match_arm_list COMMA match_arm { $$ = mla_ast_add_match_arm($1, $3); }
    ;

match_arm
    : match_pattern FAT_ARROW expression
        { $$ = mla_ast_match_arm($1, $3, yylineno); }
    | match_pattern ASSIGN GT expression
        { $$ = mla_ast_match_arm($1, $4, yylineno); }
    ;

match_pattern
    : IDENTIFIER LPAREN IDENTIFIER RPAREN
        { $$ = mla_ast_match_pattern($1, $3, yylineno); }
    | module_path
        {
            if(strstr($1, "::"))
                $$ = mla_ast_match_literal_pattern(create_enum_or_ident_from_path($1, yylineno), yylineno);
            else
                $$ = mla_ast_match_pattern($1, NULL, yylineno);
        }
    | INT_LITERAL
        { $$ = mla_ast_match_literal_pattern(mla_ast_literal_int($1), yylineno); }
    | FLOAT_LITERAL
        { $$ = mla_ast_match_literal_pattern(mla_ast_literal_float($1), yylineno); }
    | DOUBLE_LITERAL
        { $$ = mla_ast_match_literal_pattern(mla_ast_literal_double($1), yylineno); }
    | STRING_LITERAL
        { $$ = mla_ast_match_literal_pattern(mla_ast_literal_string($1), yylineno); }
    | TRUE_LIT
        { $$ = mla_ast_match_literal_pattern(mla_ast_literal_bool(1), yylineno); }
    | FALSE_LIT
        { $$ = mla_ast_match_literal_pattern(mla_ast_literal_bool(0), yylineno); }
    ;

primary_expression
    : INT_LITERAL { $$ = mla_ast_literal_int($1); }
    | FLOAT_LITERAL { $$ = mla_ast_literal_float($1); }
    | DOUBLE_LITERAL { $$ = mla_ast_literal_double($1); }
    | STRING_LITERAL { $$ = mla_ast_literal_string($1); }
    | TRUE_LIT { $$ = mla_ast_literal_bool(1); }
    | FALSE_LIT { $$ = mla_ast_literal_bool(0); }
    | FORMAT LPAREN STRING_LITERAL RPAREN
        { $$ = mla_ast_format_expr($3, NULL, yylineno); }
    | FORMAT LPAREN STRING_LITERAL COMMA argument_list RPAREN
        { $$ = mla_ast_format_expr($3, $5, yylineno); }
    | module_path
        { $$ = create_enum_or_ident_from_path($1, yylineno); }
    | LPAREN expression RPAREN { $$ = $2; }
    | match_expression { $$ = $1; }
    | list_literal { $$ = $1; }
    | map_literal { $$ = $1; }
    | PIPE_PIPE LBRACE RBRACE
        { $$ = create_closure(NULL); }
    | PIPE_PIPE LBRACE statement_list RBRACE
        { $$ = create_closure($3); }
    | PIPE parameters PIPE LBRACE RBRACE
        { $$ = create_closure_with_params($2, NULL); }
    | PIPE parameters PIPE LBRACE statement_list RBRACE
        { $$ = create_closure_with_params($2, $5); }
    | LPAREN ELLIPSIS PLUS expression RPAREN
        { $$ = mla_ast_fold_expression(PLUS, $4, 0); }
    | LPAREN expression PLUS ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(PLUS, $2, 1); }
    | LPAREN ELLIPSIS MULTIPLY expression RPAREN
        { $$ = mla_ast_fold_expression(MULTIPLY, $4, 0); }
    | LPAREN expression MULTIPLY ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(MULTIPLY, $2, 1); }
    | LPAREN ELLIPSIS AMP_AMP expression RPAREN
        { $$ = mla_ast_fold_expression(AMP_AMP, $4, 0); }
    | LPAREN expression AMP_AMP ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(AMP_AMP, $2, 1); }
    | LPAREN ELLIPSIS PIPE_PIPE expression RPAREN
        { $$ = mla_ast_fold_expression(PIPE_PIPE, $4, 0); }
    | LPAREN expression PIPE_PIPE ELLIPSIS RPAREN
        { $$ = mla_ast_fold_expression(PIPE_PIPE, $2, 1); }
    ;

/* Struct literal: StructName { field: value, ... } */
struct_literal
    : IDENTIFIER LBRACE struct_field_init_list RBRACE
        { $$ = mla_ast_struct_literal($1, NULL, $3, yylineno); }
    | IDENTIFIER GENERIC_LT type_list GT LBRACE struct_field_init_list RBRACE
        { $$ = mla_ast_struct_literal($1, $3, $6, yylineno); }
    ;

struct_field_init_list
    : /* empty */ { $$ = NULL; }
    | IDENTIFIER COLON expression
        { $$ = mla_ast_struct_field_init_list($1, $3); }
    | struct_field_init_list COMMA IDENTIFIER COLON expression
        { $$ = mla_ast_struct_field_init_list_add($1, $3, $5); }
    ;

postfix_expression
    : primary_expression { $$ = $1; }
    | postfix_expression DOT IDENTIFIER { $$ = mla_ast_field_access_expr($1, $3, yylineno); }
    | postfix_expression DOT IDENTIFIER LPAREN RPAREN
        { $$ = mla_ast_method_call_expr($1, $3, NULL, yylineno); }
    | postfix_expression DOT IDENTIFIER LPAREN argument_list RPAREN
        { $$ = mla_ast_method_call_expr($1, $3, $5, yylineno); }
    | postfix_expression DOT INT_LITERAL { $$ = mla_ast_tuple_access($1, $3, yylineno); }
    | postfix_expression LBRACKET expression RBRACKET
        { $$ = mla_ast_index_expression($1, $3, yylineno); }
    | postfix_expression PLUS_PLUS { $$ = mla_ast_update_expression(UPDATE_INCREMENT, UPDATE_POSTFIX, $1, yylineno); }
    | postfix_expression MINUS_MINUS { $$ = mla_ast_update_expression(UPDATE_DECREMENT, UPDATE_POSTFIX, $1, yylineno); }
    ;

binary_expression
    : expression PLUS expression { $$ = mla_ast_binary_op(PLUS, $1, $3); }
    | expression MINUS expression { $$ = mla_ast_binary_op(MINUS, $1, $3); }
    | expression MULTIPLY expression { $$ = mla_ast_binary_op(MULTIPLY, $1, $3); }
    | expression DIVIDE expression { $$ = mla_ast_binary_op(DIVIDE, $1, $3); }
    | expression MODULO expression { $$ = mla_ast_binary_op(MODULO, $1, $3); }
    | expression LT expression { $$ = mla_ast_binary_op(LT, $1, $3); }
    | expression GT expression { $$ = mla_ast_binary_op(GT, $1, $3); }
    | expression LE expression { $$ = mla_ast_binary_op(LE, $1, $3); }
    | expression GE expression { $$ = mla_ast_binary_op(GE, $1, $3); }
    | expression EQ expression { $$ = mla_ast_binary_op(EQ, $1, $3); }
    | expression NE expression { $$ = mla_ast_binary_op(NE, $1, $3); }
    | expression SPACESHIP expression { $$ = mla_ast_binary_op(SPACESHIP, $1, $3); }
    | expression AMP expression { $$ = mla_ast_binary_op(AMP, $1, $3); }
    | expression PIPE expression { $$ = mla_ast_binary_op(PIPE, $1, $3); }
    | expression CARET expression { $$ = mla_ast_binary_op(CARET, $1, $3); }
    | expression SHL expression { $$ = mla_ast_binary_op(SHL, $1, $3); }
    | expression SHR expression { $$ = mla_ast_binary_op(SHR, $1, $3); }
    | expression AMP_AMP expression { $$ = mla_ast_binary_op(AMP_AMP, $1, $3); }
    | expression PIPE_PIPE expression { $$ = mla_ast_binary_op(PIPE_PIPE, $1, $3); }
    ;

function_call
    : module_path LPAREN RPAREN
        { $$ = mla_ast_function_call_simple($1, NULL, NULL, yylineno); }
    | module_path LPAREN argument_list RPAREN
        { $$ = mla_ast_function_call_from_list($1, $3, yylineno); }
    | IDENTIFIER GENERIC_LT type_list GT LPAREN RPAREN
        { $$ = mla_ast_result_constructor($1, $3, NULL, yylineno); }
    | IDENTIFIER GENERIC_LT type_list GT LPAREN argument_list RPAREN
        { $$ = mla_ast_result_constructor($1, $3, $6, yylineno); }
    ;

cast_expression
    : CAST_INT expression RPAREN { $$ = mla_ast_cast_expression(TypeNode::TYPE_INT, $2); }
    | CAST_FLOAT expression RPAREN { $$ = mla_ast_cast_expression(TypeNode::TYPE_FLOAT, $2); }
    | CAST_DOUBLE expression RPAREN { $$ = mla_ast_cast_expression(TypeNode::TYPE_DOUBLE, $2); }
    | I32 LPAREN expression RPAREN { $$ = mla_ast_cast_expression(TypeNode::TYPE_I32, $3); }
    | FLOAT LPAREN expression RPAREN { $$ = mla_ast_cast_expression(TypeNode::TYPE_FLOAT, $3); }
    | DOUBLE LPAREN expression RPAREN { $$ = mla_ast_cast_expression(TypeNode::TYPE_DOUBLE, $3); }
    ;

list_literal
    : LBRACKET list_elements RBRACKET { $$ = mla_ast_list_literal($2); }
    | LBRACKET RBRACKET { $$ = mla_ast_list_literal(NULL); }
    | LBRACKET expression SEMICOLON expression RBRACKET
        { $$ = mla_ast_array_fill($2, $4); }   /* [val; N] fill literal */
    /* vec![...] macro forms — same semantics as list literals */
    | VEC_MACRO LBRACKET list_elements RBRACKET { $$ = mla_ast_list_literal($3); }
    | VEC_MACRO LBRACKET RBRACKET               { $$ = mla_ast_list_literal(NULL); }
    | VEC_MACRO LBRACKET expression SEMICOLON expression RBRACKET
        { $$ = mla_ast_array_fill($3, $5); }   /* vec![val; N] fill macro */
    ;

list_elements
    : expression { $$ = mla_ast_list_element_list($1); }
    | list_elements COMMA expression { $$ = mla_ast_list_element_list_add($1, $3); }
    ;

map_literal
    : LBRACE map_entries RBRACE { $$ = mla_ast_map_literal($2); }
    | LBRACE RBRACE { $$ = mla_ast_map_literal(NULL); }
    ;

map_entries
    : map_entry { $$ = mla_ast_map_entry_list_create($1); }
    | map_entries COMMA map_entry { $$ = mla_ast_map_entry_list_add($1, $3); }
    ;

map_entry
    : expression COLON expression { $$ = mla_ast_map_entry($1, $3); }
    ;

index_expression
    : primary_expression LBRACKET expression RBRACKET { $$ = mla_ast_index_expression($1, $3, yylineno); }
    ;

tuple_literal
    : LPAREN tuple_elements RPAREN { $$ = mla_ast_tuple_literal($2); }
    ;

tuple_elements
    : expression COMMA expression {
        ASTNode* list = mla_ast_list_element_list($1);
        $$ = mla_ast_list_element_list_add(list, $3);
    }
    | tuple_elements COMMA expression { $$ = mla_ast_list_element_list_add($1, $3); }
    ;

map_iterator
    : primary_expression KEYS_METHOD { $$ = mla_ast_map_keys_iterator($1, yylineno); }
    | primary_expression VALUES_METHOD { $$ = mla_ast_map_values_iterator($1, yylineno); }
    | primary_expression ENTRIES_METHOD { $$ = mla_ast_map_entries_iterator($1, yylineno); }
    ;

%%

static bool is_reserved_type_keyword(const char* s)
{
    if(!s || !*s)
        return false;
    return strcmp(s, "void") == 0 || strcmp(s, "bool") == 0 ||
           strcmp(s, "f32") == 0 ||
           strcmp(s, "f64") == 0 ||
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
    int col = yycolumn_token > 0 ? yycolumn_token : 1;
    if(is_reserved_type_keyword(yytext))
    {
        fprintf(stderr,
                "%s:%d:%d: error: expected identifier, found keyword '%s'\n",
                g_sourceFile, yylineno, col, yytext);
        return;
    }
    if(s && strstr(s, "syntax error") != NULL)
    {
        if(yytext && strcmp(yytext, ")") == 0)
        {
            fprintf(stderr,
                    "%s:%d:%d: error: syntax error: unexpected ')' (possible extra closing ')' )\n",
                    g_sourceFile, yylineno, col);
            return;
        }
        if(yytext && strcmp(yytext, "]") == 0)
        {
            fprintf(stderr,
                    "%s:%d:%d: error: syntax error: unexpected ']' (possible extra closing ']' )\n",
                    g_sourceFile, yylineno, col);
            return;
        }
        if(yytext && strcmp(yytext, ";") == 0)
        {
            fprintf(stderr,
                    "%s:%d:%d: error: syntax error: possible missing closing ')' before ';'\n",
                    g_sourceFile, yylineno, col);
            return;
        }
        if(yytext && strcmp(yytext, "}") == 0)
        {
            fprintf(stderr,
                    "%s:%d:%d: error: syntax error: possible missing closing ')' before '}'\n",
                    g_sourceFile, yylineno, col);
            return;
        }
        if(!yytext || yytext[0] == '\0')
        {
            fprintf(stderr,
                    "%s:%d:%d: error: syntax error: unexpected end of file (possible missing ')' )\n",
                    g_sourceFile, yylineno, col);
            return;
        }
        if(yytext && strcmp(yytext, ",") == 0)
        {
            fprintf(stderr,
                    "%s:%d:%d: error: syntax error: unexpected ',' (possible missing expression before or after ',')\n",
                    g_sourceFile, yylineno, col);
            return;
        }
    }
    fprintf(stderr, "%s:%d:%d: error: %s\n", g_sourceFile, yylineno, col, s);
}
