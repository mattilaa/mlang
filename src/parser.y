%{
#include <stdio.h>
#include <stdlib.h>
#include "ast.h"

extern int yylex();
extern int yylineno;
extern char* yytext;
void yyerror(const char* s);

// External reference to programRoot defined in globals.cpp
extern "C" {
    extern ASTNode* programRoot;
}

// Function prototypes for AST node creation
ASTNode* create_program(ASTNode* top_level_list);
ASTNode* create_top_level_list(ASTNode* item);
ASTNode* add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* create_struct_list(ASTNode* struct_def);
ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body);
ASTNode* create_type_node(int type);
ASTNode* create_parameter_list();
ASTNode* create_empty_parameter_list();
ASTNode* create_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* param);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
ASTNode* create_assignment(char* name, ASTNode* expr, int line);
ASTNode* create_return_stmt(ASTNode* expr);
ASTNode* create_int_literal(int value);
ASTNode* create_float_literal(float value);
ASTNode* create_double_literal(float value);
ASTNode* create_string_literal(char* value);
ASTNode* create_identifier(char* name);
ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2);
ASTNode* create_function_call_multi(char* name, ASTNode* args);
ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_cast_expression(int type, ASTNode* expr);
ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members);
ASTNode* create_struct_member_list(ASTNode* member);
ASTNode* add_struct_member(ASTNode* list, ASTNode* member);
ASTNode* create_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* create_struct_init(char* type_name, char* var_name);
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
ASTNode* create_argument_list(ASTNode* arg);
ASTNode* add_argument(ASTNode* list, ASTNode* arg);
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
ASTNode* create_len_expression(ASTNode* expr, int line);
ASTNode* create_method_call(ASTNode* object, char* method, ASTNode* args, int line);
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
%token FUNCTION RETURN IF ELSE VOID BOOL INT FLOAT DOUBLE STRING LIST MAP TUPLE STRUCT
%token I8 I16 I32 I64 U8 U16 U32 U64
%token LET VAR
%token FOR IN DOTDOT BREAK CONTINUE
%token MOD USE COLONCOLON
%token PRINTLN PRINT EPRINTLN EPRINT
%token PLUS MINUS MULTIPLY DIVIDE ASSIGN
%token LT GT LE GE EQ NE
%token LBRACE RBRACE LPAREN RPAREN LBRACKET RBRACKET SEMICOLON COMMA ARROW COLON DOT
%token KEYS_METHOD VALUES_METHOD ENTRIES_METHOD
%token CAST_INT CAST_FLOAT CAST_DOUBLE

%type <ast> program top_level_list top_level_item
%type <ast> struct_def function_def type parameter_list parameters parameter
%type <ast> statement_list statement expression cast_expression
%type <ast> if_statement else_if_list else_if optional_else
%type <ast> struct_member_list struct_member struct_init
%type <ast> list_literal list_elements
%type <ast> let_statement var_statement assignment_statement expression_statement
%type <ast> return_statement block_statement for_statement range_expression
%type <ast> break_statement continue_statement
%type <ast> primary_expression binary_expression function_call
%type <ast> mod_declaration use_declaration
%type <ast> print_statement argument_list
%type <ast> map_literal map_entries map_entry index_expression
%type <ast> tuple_type type_list tuple_literal tuple_access tuple_elements
%type <ast> map_iterator

%left LT GT LE GE EQ NE
%left PLUS MINUS
%left MULTIPLY DIVIDE

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
    | function_def
    | mod_declaration
    | use_declaration
    ;

mod_declaration
    : MOD IDENTIFIER SEMICOLON
        { $$ = create_mod_declaration($2, yylineno); }
    ;

use_declaration
    : USE IDENTIFIER COLONCOLON IDENTIFIER SEMICOLON
        { $$ = create_use_declaration($2, $4, yylineno); }
    | USE IDENTIFIER COLONCOLON MULTIPLY SEMICOLON
        { $$ = create_use_all_declaration($2, yylineno); }
    ;

struct_def
    : STRUCT IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { $$ = create_struct_def($2, NULL, $4); }
    | STRUCT IDENTIFIER COLON IDENTIFIER LBRACE struct_member_list RBRACE SEMICOLON
        { $$ = create_struct_def($2, $4, $6); }
    ;

struct_member_list
    : struct_member { $$ = create_struct_member_list($1); }
    | struct_member_list struct_member { $$ = add_struct_member($1, $2); }
    ;

struct_member
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_struct_member(0, $4, $2, $6); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = create_struct_member(1, $4, $2, NULL); }
    ;

function_def
    : FUNCTION IDENTIFIER LPAREN parameter_list RPAREN ARROW type LBRACE statement_list RBRACE
        { $$ = create_function_def($7, $2, $4, $9); }
    ;

parameter_list
    : /* empty */ { $$ = create_empty_parameter_list(); }
    | parameters
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
    | LIST   { $$ = create_list_type(); }
    | LIST LT type GT { $$ = create_generic_list_type($3); }
    | MAP LT type COMMA type GT { $$ = create_map_type($3, $5); }
    | tuple_type
    | I8     { $$ = create_type_node(TypeNode::TYPE_I8); }
    | I16    { $$ = create_type_node(TypeNode::TYPE_I16); }
    | I32    { $$ = create_type_node(TypeNode::TYPE_I32); }
    | I64    { $$ = create_type_node(TypeNode::TYPE_I64); }
    | U8     { $$ = create_type_node(TypeNode::TYPE_U8); }
    | U16    { $$ = create_type_node(TypeNode::TYPE_U16); }
    | U32    { $$ = create_type_node(TypeNode::TYPE_U32); }
    | U64    { $$ = create_type_node(TypeNode::TYPE_U64); }
    ;

tuple_type
    : TUPLE LT type_list GT { $$ = create_tuple_type($3); }
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
    | assignment_statement
    | expression_statement
    | return_statement
    | if_statement
    | for_statement
    | block_statement
    | struct_init
    | print_statement
    | break_statement
    | continue_statement
    ;

let_statement
    : LET IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_let_declaration($4, $2, $6); }
    ;

var_statement
    : VAR IDENTIFIER COLON type ASSIGN expression SEMICOLON
        { $$ = create_var_declaration($4, $2, $6); }
    | VAR IDENTIFIER COLON type SEMICOLON
        { $$ = create_var_declaration($4, $2, NULL); }
    ;

assignment_statement
    : IDENTIFIER ASSIGN expression SEMICOLON
        { $$ = create_assignment($1, $3, yylineno); }
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
    | FOR IDENTIFIER IN expression block_statement
        { $$ = create_for_iterator($2, $4, $5, yylineno); }
    ;

range_expression
    : expression DOTDOT expression
        { $$ = create_range_expression($1, $3, 0); }
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
    | PRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_print_stmt(0, $3, NULL, yylineno); }
    | PRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_print_stmt(0, $3, $5, yylineno); }
    | EPRINTLN LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_print_stmt(3, $3, NULL, yylineno); }
    | EPRINTLN LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_print_stmt(3, $3, $5, yylineno); }
    | EPRINT LPAREN STRING_LITERAL RPAREN SEMICOLON
        { $$ = create_print_stmt(2, $3, NULL, yylineno); }
    | EPRINT LPAREN STRING_LITERAL COMMA argument_list RPAREN SEMICOLON
        { $$ = create_print_stmt(2, $3, $5, yylineno); }
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
    : primary_expression
    | binary_expression
    | function_call
    | cast_expression
    | list_literal
    | map_literal
    | index_expression
    | tuple_literal
    | tuple_access
    | map_iterator
    ;

primary_expression
    : INT_LITERAL { $$ = create_int_literal($1); }
    | FLOAT_LITERAL { $$ = create_float_literal($1); }
    | DOUBLE_LITERAL { $$ = create_double_literal($1); }
    | STRING_LITERAL { $$ = create_string_literal($1); }
    | IDENTIFIER { $$ = create_identifier($1); }
    | LPAREN expression RPAREN { $$ = $2; }
    ;

binary_expression
    : expression PLUS expression { $$ = create_binary_op(PLUS, $1, $3); }
    | expression MINUS expression { $$ = create_binary_op(MINUS, $1, $3); }
    | expression MULTIPLY expression { $$ = create_binary_op(MULTIPLY, $1, $3); }
    | expression DIVIDE expression { $$ = create_binary_op(DIVIDE, $1, $3); }
    | expression LT expression { $$ = create_binary_op(LT, $1, $3); }
    | expression GT expression { $$ = create_binary_op(GT, $1, $3); }
    | expression LE expression { $$ = create_binary_op(LE, $1, $3); }
    | expression GE expression { $$ = create_binary_op(GE, $1, $3); }
    | expression EQ expression { $$ = create_binary_op(EQ, $1, $3); }
    | expression NE expression { $$ = create_binary_op(NE, $1, $3); }
    ;

function_call
    : IDENTIFIER LPAREN RPAREN { $$ = create_function_call($1, NULL, NULL); }
    | IDENTIFIER LPAREN argument_list RPAREN { $$ = create_function_call_multi($1, $3); }
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

tuple_access
    : primary_expression DOT INT_LITERAL { $$ = create_tuple_access($1, $3, yylineno); }
    ;

map_iterator
    : primary_expression KEYS_METHOD { $$ = create_map_keys_iterator($1, yylineno); }
    | primary_expression VALUES_METHOD { $$ = create_map_values_iterator($1, yylineno); }
    | primary_expression ENTRIES_METHOD { $$ = create_map_entries_iterator($1, yylineno); }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Error at line %d: %s\n", yylineno, s);
}
