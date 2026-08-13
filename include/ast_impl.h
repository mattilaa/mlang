// Generated helper declarations when bridging AST creation from MLang helpers.
#ifndef AST_IMPL_H
#define AST_IMPL_H

#include "ast.h"

ASTNode* create_program_impl(ASTNode* top_level_list);
ASTNode* create_top_level_list_impl(ASTNode* item);
ASTNode* add_to_top_level_list_impl(ASTNode* list, ASTNode* item);
ASTNode* create_struct_list_impl(ASTNode* struct_def);
ASTNode* add_struct_to_list_impl(ASTNode* list, ASTNode* struct_def);
ASTNode* create_function_list_impl(ASTNode* function);
ASTNode* add_function_to_list_impl(ASTNode* list, ASTNode* function);
ASTNode* create_function_def_impl(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_extern);
ASTNode* create_type_node_impl(TypeNode::TypeKind type);
ASTNode* create_pointer_type_impl(ASTNode* element_type);
ASTNode* create_reference_type_impl(ASTNode* element_type, int is_mutable);
ASTNode* create_trait_object_type_impl(char* trait_name);
ASTNode* create_parameter_list_impl(ASTNode* param);
ASTNode* create_empty_parameter_list_impl();
ASTNode* set_parameter_list_vararg_impl(ASTNode* list);
ASTNode* create_parameter_impl(ASTNode* type, char* name);
ASTNode* add_parameter_impl(ASTNode* list, ASTNode* param);
ASTNode* create_statement_list_impl(ASTNode* stmt);
ASTNode* create_empty_statement_list_impl();
ASTNode* add_statement_impl(ASTNode* list, ASTNode* stmt);
ASTNode* create_assignment_impl(char* name, ASTNode* expr, int line);
ASTNode* create_deref_assignment_impl(ASTNode* pointer_expr, ASTNode* expr, int line);
ASTNode* create_return_stmt_impl(ASTNode* expr);
ASTNode* create_break_stmt_impl(int line);
ASTNode* create_continue_stmt_impl(int line);
ASTNode* create_throw_stmt_impl(ASTNode* expr, int line);
ASTNode* create_int_literal_impl(int64_t value);
ASTNode* create_bool_literal_impl(int value);
ASTNode* create_float_literal_impl(float value);
ASTNode* create_double_literal_impl(double value);
ASTNode* create_string_literal_impl(char* value);
ASTNode* create_identifier_impl(char* name);
ASTNode* create_identifier_line_impl(char* name, int line);
ASTNode* create_identifier_at_impl(char* name, int line, int col);
ASTNode* create_binary_op_impl(int op, ASTNode* left, ASTNode* right);
ASTNode* create_fold_expression_impl(int op, ASTNode* pack_expr, int is_right_fold);
ASTNode* create_unary_op_impl(int op, ASTNode* operand);
ASTNode* create_update_expression_impl(int kind, int is_prefix, ASTNode* operand, int line);
ASTNode* create_ternary_expression_impl(ASTNode* cond, ASTNode* t, ASTNode* f, int line);
ASTNode* create_try_expression_impl(ASTNode* expr, int line);
ASTNode* create_sizeof_type_expression_impl(ASTNode* type, int line);
ASTNode* create_sizeof_value_expression_impl(ASTNode* expr, int line);
ASTNode* create_cexpr_expression_impl(ASTNode* expr, int line);
ASTNode* create_inline_asm_impl(ASTNode* type, char* asm_text, char* arch_name,
                                ASTNode* args, int is_volatile, int line);
ASTNode* create_module_asm_impl(char* asm_text, char* arch_name, int line);
ASTNode* create_function_call_impl(char* name, ASTNode* arg1, ASTNode* arg2, int line);
ASTNode* create_function_call_multi_impl(char* name, ASTNode* args, int line);
ASTNode* create_result_constructor_impl(char* variant, ASTNode* type_args, ASTNode* args, int line);
ASTNode* create_if_statement_impl(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_if_statement_with_init_impl(ASTNode* condition_init, ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_cexpr_if_statement_impl(ASTNode* condition,
                                        ASTNode* then_branch,
                                        ASTNode* else_if_branch,
                                        ASTNode* else_branch);
ASTNode* create_let_declaration_impl(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_cexpr_declaration_impl(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_var_declaration_impl(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_cast_expression_impl(int type, ASTNode* expr);
ASTNode* create_struct_def_impl(char* name, char* base_name, ASTNode* members,
                                int is_public, int derive_debug,
                                int derive_json);
ASTNode* create_struct_member_list_impl(ASTNode* member);
ASTNode* add_struct_member_impl(ASTNode* list, ASTNode* member);
ASTNode* create_struct_member_impl(int is_var, ASTNode* type, char* name, ASTNode* init_expr);
ASTNode* create_struct_member_with_property_impl(int is_var, ASTNode* type, char* name, ASTNode* init_expr, int is_property, int is_readonly, int property_flags);
ASTNode* create_struct_method_impl(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static);
ASTNode* add_struct_method_impl(ASTNode* list, ASTNode* method);
ASTNode* create_struct_init_impl(char* type_name, char* var_name);
ASTNode* create_method_call_expr_impl(ASTNode* object, char* method_name, ASTNode* args, int line);
ASTNode* create_method_call_impl(ASTNode* object, char* method, ASTNode* args, int line);
ASTNode* create_list_type_impl();
ASTNode* create_list_literal_impl(ASTNode* elements);
ASTNode* create_list_element_list_impl(ASTNode* element);
ASTNode* add_list_element_impl(ASTNode* list, ASTNode* element);
ASTNode* create_array_fill_impl(ASTNode* value, ASTNode* count);
ASTNode* create_expression_statement_impl(ASTNode* expr);
ASTNode* create_block_statement_impl(ASTNode* stmt_list);
ASTNode* create_else_if_impl(ASTNode* condition, ASTNode* body);
ASTNode* create_else_if_with_init_impl(ASTNode* condition_init, ASTNode* condition, ASTNode* body);
ASTNode* add_else_if_impl(ASTNode* else_if_list, ASTNode* else_if);
ASTNode* create_for_range_impl(char* var_name, ASTNode* range, ASTNode* body, int line);
ASTNode* create_for_iterator_impl(char* var_name, ASTNode* iterable, ASTNode* body, int line);
ASTNode* create_for_enumerate_impl(char* index_var, char* val_var, ASTNode* iterable, ASTNode* body, int line);
ASTNode* create_while_statement_impl(ASTNode* condition, ASTNode* body, int line, int uses_colon_without_guard);
ASTNode* create_try_catch_stmt_impl(ASTNode* try_block, char* catch_name,
                                    ASTNode* catch_type, ASTNode* catch_block,
                                    int line);
ASTNode* create_range_expression_impl(ASTNode* start, ASTNode* end, int inclusive);
ASTNode* create_mod_declaration_impl(char* name, int line);
ASTNode* create_use_declaration_impl(char* module_name, char* item_name, int line);
ASTNode* create_use_declaration_alias_impl(char* module_name, char* item_name,
                                           char* alias_name, int line);
ASTNode* create_use_module_alias_declaration_impl(char* module_name,
                                                  char* alias_name, int line);
ASTNode* create_use_all_declaration_impl(char* module_name, int line);
ASTNode* create_type_alias_impl(char* name, ASTNode* type_params, ASTNode* aliased_type);
ASTNode* create_print_stmt_impl(int kind, char* format_str, ASTNode* args, int line);
ASTNode* create_debug_print_stmt_impl(char* format_str, ASTNode* args, int line);
ASTNode* create_print_expr_stmt_impl(int kind, ASTNode* expr, int line);
ASTNode* create_argument_list_impl(ASTNode* arg);
ASTNode* add_argument_impl(ASTNode* list, ASTNode* arg);
ASTNode* create_format_argument_impl(char* name, ASTNode* value);
ASTNode* create_format_argument_list_impl(ASTNode* arg);
ASTNode* add_format_argument_impl(ASTNode* list, ASTNode* arg);
ASTNode* create_format_expr_impl(char* format_str, ASTNode* args, int line);
ASTNode* create_assert_eq_impl(ASTNode* left, ASTNode* right, int line);
ASTNode* create_assert_impl(ASTNode* condition, int line);
ASTNode* create_static_assert_impl(ASTNode* condition, int line);
ASTNode* create_unsafe_block_impl(ASTNode* block, int line);
ASTNode* create_generic_list_type_impl(ASTNode* element_type);
ASTNode* create_map_type_impl(ASTNode* key_type, ASTNode* value_type);
ASTNode* create_map_literal_impl(ASTNode* entries);
ASTNode* create_map_entry_list_impl(ASTNode* entry);
ASTNode* add_map_entry_impl(ASTNode* list, ASTNode* entry);
ASTNode* create_map_entry_impl(ASTNode* key, ASTNode* value);
ASTNode* create_index_expression_impl(ASTNode* base, ASTNode* index, int line);
ASTNode* create_tuple_type_impl(ASTNode* type_list);
ASTNode* create_type_list_impl(ASTNode* type);
ASTNode* add_type_to_list_impl(ASTNode* list, ASTNode* type);
ASTNode* create_tuple_literal_impl(ASTNode* elements);
ASTNode* create_tuple_access_impl(ASTNode* tuple, int index, int line);
ASTNode* create_map_keys_iterator_impl(ASTNode* map_expr, int line);
ASTNode* create_map_values_iterator_impl(ASTNode* map_expr, int line);
ASTNode* create_map_entries_iterator_impl(ASTNode* map_expr, int line);
ASTNode* create_struct_type_ref_impl(char* name);
ASTNode* create_field_access_impl(char* struct_name, char* field_name, int line);
ASTNode* create_field_access_expr_impl(ASTNode* object, char* field_name, int line);
ASTNode* create_field_assignment_impl(char* struct_name, char* field_name, ASTNode* expr, int line);
ASTNode* create_chained_field_assignment_impl(ASTNode* target, ASTNode* expr, int line);
ASTNode* create_match_pattern_impl(char* name, char* binding, int line);
ASTNode* create_len_expression_impl(ASTNode* expr, int line);
ASTNode* create_match_literal_pattern_impl(ASTNode* literal, int line);
ASTNode* create_match_arm_impl(ASTNode* pattern, ASTNode* expr, int line);
ASTNode* create_match_arm_list_impl(ASTNode* arm);
ASTNode* add_match_arm_impl(ASTNode* list, ASTNode* arm);
ASTNode* create_match_expression_impl(ASTNode* target, ASTNode* arms, int line);
ASTNode* create_enum_def_impl(char* name, ASTNode* variants, int is_public, int backing_type);
ASTNode* create_enum_variant_impl(char* name, int has_explicit_value, long long explicit_value);
ASTNode* create_enum_variant_string_impl(char* name, char* explicit_string_value);
ASTNode* create_enum_variant_ref_impl(char* name, char* ref_enum_name, char* ref_variant_name);
ASTNode* create_enum_variant_list_impl(ASTNode* variant);
ASTNode* add_enum_variant_impl(ASTNode* list, ASTNode* variant);
ASTNode* create_enum_literal_impl(char* enum_name, char* variant_name, int line);
ASTNode* create_closure_impl(ASTNode* body);
ASTNode* create_closure_with_params_impl(ASTNode* params, ASTNode* body);
ASTNode* create_type_param_list_impl(char* param);
ASTNode* add_type_param_impl(ASTNode* list, char* param);
ASTNode* create_bounded_type_param_list_impl(char* param, char* trait_name);
ASTNode* add_bounded_type_param_impl(ASTNode* list, char* param, char* trait_name);
ASTNode* create_generic_struct_def_impl(char* name, char* base_name,
                                        ASTNode* type_params,
                                        ASTNode* members, int is_public,
                                        int derive_debug, int derive_json);
ASTNode* create_trait_def_impl(char* name, int line);
ASTNode* add_trait_method_impl(ASTNode* trait, ASTNode* method);
ASTNode* create_impl_block_impl(char* struct_name, ASTNode* type_params, char* trait_name);
ASTNode* add_impl_method_impl(ASTNode* impl, ASTNode* method);
ASTNode* create_struct_literal_impl(char* struct_name, ASTNode* type_args, ASTNode* fields, int line);
ASTNode* create_struct_field_init_list_impl(char* field_name, ASTNode* value);
ASTNode* add_struct_field_init_impl(ASTNode* list, char* field_name, ASTNode* value);
ASTNode* create_generic_struct_type_ref_impl(char* name, ASTNode* type_args);

#endif // AST_IMPL_H
