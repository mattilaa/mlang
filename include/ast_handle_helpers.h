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
ASTNode* mla_ast_format_argument(char* name, ASTNode* value);
ASTNode* mla_ast_format_argument_list_create(ASTNode* arg);
ASTNode* mla_ast_format_argument_list_add(ASTNode* list, ASTNode* arg);
ASTNode* mla_ast_function_call_simple(char* name, ASTNode* arg1, ASTNode* arg2, int line);
ASTNode* mla_ast_function_call_from_list(char* name, ASTNode* args, int line);
ASTNode* mla_ast_struct_init(char* type_name, char* var_name);
ASTNode* mla_ast_identifier(char* name);
ASTNode* mla_ast_identifier_line(char* name, int line);
ASTNode* mla_ast_identifier_at(char* name, int line, int col);
ASTNode* mla_ast_struct_list(ASTNode* struct_def);
ASTNode* mla_ast_function_list(ASTNode* function);
ASTNode* mla_ast_parameter(ASTNode* type, char* name);
ASTNode* mla_ast_parameter_list(ASTNode* param);
ASTNode* mla_ast_empty_parameter_list();
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
ASTNode* mla_ast_enum_variant_string(char* name, char* explicit_string_value);
ASTNode* mla_ast_enum_variant_list(ASTNode* variant);
ASTNode* mla_ast_enum_variant_list_add(ASTNode* list, ASTNode* variant);
ASTNode* mla_ast_enum_variant_ref(char* name, char* ref_enum_name, char* ref_variant_name);
ASTNode* mla_ast_enum_literal(char* enum_name, char* variant_name, int line);
ASTNode* mla_ast_struct_member_list(ASTNode* member);
ASTNode* mla_ast_struct_member_list_add(ASTNode* list, ASTNode* member);
ASTNode* mla_ast_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* mla_ast_struct_member_with_property(int is_var, ASTNode* type, char* name, ASTNode* init_expr, int is_property, int is_readonly, int property_flags);
ASTNode* mla_ast_struct_method(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static);
ASTNode* mla_ast_struct_member_add_method(ASTNode* list, ASTNode* method);
ASTNode* mla_ast_type_alias(char* name, ASTNode* type_params, ASTNode* aliased_type);
ASTNode* mla_ast_trait_def(char* name, int line);
ASTNode* mla_ast_trait_add_method(ASTNode* trait, ASTNode* method);
ASTNode* mla_ast_impl_block(char* struct_name, ASTNode* type_params, char* trait_name);
ASTNode* mla_ast_impl_add_method(ASTNode* impl, ASTNode* method);
ASTNode* mla_ast_block_statement(ASTNode* stmt_list);
ASTNode* mla_ast_match_arm(ASTNode* pattern, ASTNode* expr, int line);
ASTNode* mla_ast_match_arm_list(ASTNode* arm);
ASTNode* mla_ast_add_match_arm(ASTNode* list, ASTNode* arm);
ASTNode* mla_ast_match_expression(ASTNode* target, ASTNode* arms, int line);
ASTNode* mla_ast_match_pattern(char* name, char* binding, int line);
ASTNode* mla_ast_match_literal_pattern(ASTNode* literal, int line);
ASTNode* mla_ast_for_range(char* var_name, ASTNode* range, ASTNode* body, int line);
ASTNode* mla_ast_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body, int line);
ASTNode* mla_ast_for_enumerate(char* index_var, char* val_var, ASTNode* iterable, ASTNode* body, int line);
ASTNode* mla_ast_while_statement(ASTNode* condition, ASTNode* body, int line, int uses_colon_without_guard);
ASTNode* mla_ast_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_extern);
ASTNode* mla_ast_top_level_list(ASTNode* item);
ASTNode* mla_ast_add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* mla_ast_program(ASTNode* top_level_list);
ASTNode* mla_ast_type_node(int type);
ASTNode* mla_ast_list_type();
ASTNode* mla_ast_generic_list_type(ASTNode* element_type);
ASTNode* mla_ast_map_type(ASTNode* key_type, ASTNode* value_type);
ASTNode* mla_ast_tuple_type(ASTNode* type_list);
ASTNode* mla_ast_type_list(ASTNode* type);
ASTNode* mla_ast_struct_type_ref(char* name);
ASTNode* mla_ast_generic_struct_type_ref(char* name, ASTNode* type_args);
ASTNode* mla_ast_pointer_type(ASTNode* element_type);
ASTNode* mla_ast_reference_type(ASTNode* element_type, int is_mutable);
ASTNode* mla_ast_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* mla_ast_if_statement_with_init(ASTNode* condition_init, ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* mla_ast_else_if(ASTNode* condition, ASTNode* body);
ASTNode* mla_ast_else_if_with_init(ASTNode* condition_init, ASTNode* condition, ASTNode* body);
ASTNode* mla_ast_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* mla_ast_var_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* mla_ast_cast_expression(int type, ASTNode* expr);
ASTNode* mla_ast_range_expression(ASTNode* start, ASTNode* end, int inclusive);
ASTNode* mla_ast_print_stmt(int kind, char* format_str, ASTNode* args, int line);
ASTNode* mla_ast_print_expr_stmt(int kind, ASTNode* expr, int line);
ASTNode* mla_ast_debug_print_stmt(char* format_str, ASTNode* args, int line);
ASTNode* mla_ast_assert_eq(ASTNode* left, ASTNode* right, int line);
ASTNode* mla_ast_assert(ASTNode* condition, int line);
ASTNode* mla_ast_static_assert(ASTNode* condition, int line);
ASTNode* mla_ast_unsafe_block(ASTNode* block, int line);
ASTNode* mla_ast_break_stmt(int line);
ASTNode* mla_ast_continue_stmt(int line);
ASTNode* mla_ast_expression_statement(ASTNode* expr);
ASTNode* mla_ast_deref_assignment(ASTNode* pointer_expr, ASTNode* expr, int line);
ASTNode* mla_ast_mod_declaration(char* name, int line);
ASTNode* mla_ast_use_declaration(char* module_name, char* item_name, int line);
ASTNode* mla_ast_use_declaration_alias(char* module_name, char* item_name,
                                       char* alias_name, int line);
ASTNode* mla_ast_use_module_alias_declaration(char* module_name,
                                              char* alias_name, int line);
ASTNode* mla_ast_use_all_declaration(char* module_name, int line);
    ASTNode* mla_ast_struct_def(char* name, char* base_name, ASTNode* members,
                                int is_public, int derive_debug,
                                int derive_json);
    ASTNode* mla_ast_generic_struct_def(char* name, char* base_name,
                                        ASTNode* type_params,
                                        ASTNode* members, int is_public,
                                        int derive_debug, int derive_json);
    ASTNode* mla_ast_enum_def(char* name, ASTNode* variants, int is_public, int backing_type);
    ASTNode* mla_ast_assignment(char* name, ASTNode* expr, int line);
    ASTNode* mla_ast_field_access(char* struct_name, char* field_name, int line);
    ASTNode* mla_ast_field_access_expr(ASTNode* object, char* field_name, int line);
    ASTNode* mla_ast_field_assignment(char* struct_name, char* field_name, ASTNode* expr, int line);
    ASTNode* mla_ast_chained_field_assignment(ASTNode* target, ASTNode* expr, int line);
ASTNode* mla_ast_return_stmt(ASTNode* expr);
ASTNode* mla_ast_literal_int(int64_t value);
ASTNode* mla_ast_literal_bool(int value);
ASTNode* mla_ast_literal_float(float value);
ASTNode* mla_ast_literal_double(float value);
ASTNode* mla_ast_literal_string(char* value);
ASTNode* mla_ast_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* mla_ast_fold_expression(int op, ASTNode* pack_expr, int is_right_fold);
ASTNode* mla_ast_ternary_expression(ASTNode* cond, ASTNode* t, ASTNode* f, int line);
ASTNode* mla_ast_try_expression(ASTNode* expr, int line);
ASTNode* mla_ast_sizeof_type_expression(ASTNode* type, int line);
ASTNode* mla_ast_sizeof_value_expression(ASTNode* expr, int line);
ASTNode* mla_ast_cexpr_expression(ASTNode* expr, int line);
ASTNode* mla_ast_cexpr_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* mla_ast_inline_asm(ASTNode* type, char* asm_text, char* arch_name,
                            ASTNode* args, int is_volatile, int line);
ASTNode* mla_ast_update_expression(int kind, int is_prefix, ASTNode* operand, int line);
ASTNode* mla_ast_tuple_literal(ASTNode* elements);
ASTNode* mla_ast_tuple_access(ASTNode* tuple, int index, int line);
ASTNode* mla_ast_index_expression(ASTNode* base, ASTNode* index, int line);
ASTNode* mla_ast_map_keys_iterator(ASTNode* map_expr, int line);
ASTNode* mla_ast_map_values_iterator(ASTNode* map_expr, int line);
ASTNode* mla_ast_map_entries_iterator(ASTNode* map_expr, int line);
ASTNode* mla_ast_closure(ASTNode* body);
ASTNode* mla_ast_closure_with_params(ASTNode* params, ASTNode* body);
ASTNode* mla_ast_method_call_expr(ASTNode* object, char* method_name, ASTNode* args, int line);
ASTNode* mla_ast_method_call(ASTNode* object, char* method, ASTNode* args, int line);
ASTNode* mla_ast_format_expr(char* format_str, ASTNode* args, int line);
ASTNode* mla_ast_bounded_type_param_list(char* param, char* trait_name);
ASTNode* mla_ast_bounded_type_param_list_add(ASTNode* list, char* param, char* trait_name);
}

#endif // AST_HANDLE_HELPERS_H
