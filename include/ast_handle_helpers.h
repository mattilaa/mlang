#ifndef AST_HANDLE_HELPERS_H
#define AST_HANDLE_HELPERS_H

#include <cstdint>

extern "C" {
int64_t mla_argument_list_create();
int64_t mla_argument_list_add(int64_t list, int64_t expr);
int64_t mla_function_call_create(const char* name, int line);
int64_t mla_function_call_set_args(int64_t call, int64_t args);
int64_t mla_function_call_add_arg(int64_t call, int64_t expr);

ASTNode* mla_ast_argument_list_create(ASTNode* arg);
ASTNode* mla_ast_argument_list_add(ASTNode* list, ASTNode* arg);
ASTNode* mla_ast_function_call_simple(char* name, ASTNode* arg1, ASTNode* arg2, int line);
ASTNode* mla_ast_function_call_from_list(char* name, ASTNode* args, int line);
ASTNode* mla_ast_statement_list_create(ASTNode* stmt);
ASTNode* mla_ast_statement_list_add(ASTNode* list, ASTNode* stmt);
ASTNode* mla_ast_map_entry(ASTNode* key, ASTNode* value);
ASTNode* mla_ast_map_entry_list_create(ASTNode* entry);
ASTNode* mla_ast_map_entry_list_add(ASTNode* list, ASTNode* entry);
ASTNode* mla_ast_map_literal(ASTNode* entries);
ASTNode* mla_ast_struct_field_init_list(char* field_name, ASTNode* value);
ASTNode* mla_ast_struct_field_init_list_add(ASTNode* list, char* field_name, ASTNode* value);
ASTNode* mla_ast_struct_literal(char* struct_name, ASTNode* type_args, ASTNode* fields, int line);
ASTNode* mla_ast_list_literal(ASTNode* elements);
ASTNode* mla_ast_list_element_list(ASTNode* element);
ASTNode* mla_ast_list_element_list_add(ASTNode* list, ASTNode* element);
ASTNode* mla_ast_array_fill(ASTNode* value, ASTNode* count);
ASTNode* mla_ast_result_constructor(char* variant, ASTNode* type_args, ASTNode* args, int line);
ASTNode* mla_ast_enum_variant(char* name, int has_explicit_value, long long explicit_value);
ASTNode* mla_ast_enum_variant_list(ASTNode* variant);
ASTNode* mla_ast_enum_variant_list_add(ASTNode* list, ASTNode* variant);
ASTNode* mla_ast_enum_variant_ref(char* name, char* ref_enum_name, char* ref_variant_name);
ASTNode* mla_ast_enum_literal(char* enum_name, char* variant_name, int line);
ASTNode* mla_ast_struct_member_list(ASTNode* member);
ASTNode* mla_ast_struct_member_list_add(ASTNode* list, ASTNode* member);
ASTNode* mla_ast_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* mla_ast_struct_method(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static);
ASTNode* mla_ast_struct_member_add_method(ASTNode* list, ASTNode* method);
ASTNode* mla_ast_type_alias(char* name, ASTNode* type_params, ASTNode* aliased_type);
ASTNode* mla_ast_trait_def(char* name, int line);
ASTNode* mla_ast_impl_block(char* struct_name, ASTNode* type_params, char* trait_name);
ASTNode* mla_ast_impl_add_method(ASTNode* impl, ASTNode* method);
ASTNode* mla_ast_block_statement(ASTNode* stmt_list);
ASTNode* mla_ast_match_arm(ASTNode* pattern, ASTNode* expr, int line);
ASTNode* mla_ast_match_arm_list(ASTNode* arm);
ASTNode* mla_ast_add_match_arm(ASTNode* list, ASTNode* arm);
ASTNode* mla_ast_match_expression(ASTNode* target, ASTNode* arms, int line);
ASTNode* mla_ast_for_range(char* var_name, ASTNode* range, ASTNode* body, int line);
ASTNode* mla_ast_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body, int line);
ASTNode* mla_ast_for_enumerate(char* index_var, char* val_var, ASTNode* iterable, ASTNode* body, int line);
ASTNode* mla_ast_while_statement(ASTNode* condition, ASTNode* body, int line, int uses_colon_without_guard);
ASTNode* mla_ast_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_extern);
ASTNode* mla_ast_top_level_list(ASTNode* item);
ASTNode* mla_ast_add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* mla_ast_program(ASTNode* top_level_list);
}

#endif // AST_HANDLE_HELPERS_H
