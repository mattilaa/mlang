#include "ast.h"
#include "ast_impl.h"

extern "C" {

ASTNode* create_program(ASTNode* top_level_list)
{
    return create_program_impl(top_level_list);
}

ASTNode* create_top_level_list(ASTNode* item)
{
    return create_top_level_list_impl(item);
}

ASTNode* add_to_top_level_list(ASTNode* list, ASTNode* item)
{
    return add_to_top_level_list_impl(list, item);
}

ASTNode* create_struct_list(ASTNode* struct_def)
{
    return create_struct_list_impl(struct_def);
}

ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def)
{
    return add_struct_to_list_impl(list, struct_def);
}

ASTNode* create_function_list(ASTNode* function)
{
    return create_function_list_impl(function);
}

ASTNode* add_function_to_list(ASTNode* list, ASTNode* function)
{
    return add_function_to_list_impl(list, function);
}

ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_extern)
{
    return create_function_def_impl(type, name, params, body, is_public, is_extern);
}

ASTNode* create_type_node(TypeNode::TypeKind type)
{
    return create_type_node_impl(type);
}

ASTNode* create_pointer_type(ASTNode* element_type)
{
    return create_pointer_type_impl(element_type);
}

ASTNode* create_reference_type(ASTNode* element_type, int is_mutable)
{
    return create_reference_type_impl(element_type, is_mutable);
}

ASTNode* create_trait_object_type(char* trait_name)
{
    return create_trait_object_type_impl(trait_name);
}

ASTNode* create_parameter_list(ASTNode* param)
{
    return create_parameter_list_impl(param);
}

ASTNode* create_empty_parameter_list()
{
    return create_empty_parameter_list_impl();
}

ASTNode* set_parameter_list_vararg(ASTNode* list)
{
    return set_parameter_list_vararg_impl(list);
}

ASTNode* create_parameter(ASTNode* type, char* name)
{
    return create_parameter_impl(type, name);
}

ASTNode* add_parameter(ASTNode* list, ASTNode* param)
{
    return add_parameter_impl(list, param);
}

ASTNode* create_statement_list(ASTNode* stmt)
{
    return create_statement_list_impl(stmt);
}

ASTNode* create_empty_statement_list()
{
    return create_empty_statement_list_impl();
}

ASTNode* add_statement(ASTNode* list, ASTNode* stmt)
{
    return add_statement_impl(list, stmt);
}

ASTNode* create_assignment(char* name, ASTNode* expr, int line)
{
    return create_assignment_impl(name, expr, line);
}

ASTNode* create_deref_assignment(ASTNode* pointer_expr, ASTNode* expr, int line)
{
    return create_deref_assignment_impl(pointer_expr, expr, line);
}

ASTNode* create_return_stmt(ASTNode* expr)
{
    return create_return_stmt_impl(expr);
}

ASTNode* create_break_stmt(int line)
{
    return create_break_stmt_impl(line);
}

ASTNode* create_continue_stmt(int line)
{
    return create_continue_stmt_impl(line);
}

ASTNode* create_throw_stmt(ASTNode* expr, int line)
{
    return create_throw_stmt_impl(expr, line);
}

ASTNode* create_int_literal(int64_t value)
{
    return create_int_literal_impl(value);
}

ASTNode* create_bool_literal(int value)
{
    return create_bool_literal_impl(value);
}

ASTNode* create_float_literal(float value)
{
    return create_float_literal_impl(value);
}

ASTNode* create_double_literal(double value)
{
    return create_double_literal_impl(value);
}

ASTNode* create_string_literal(char* value)
{
    return create_string_literal_impl(value);
}

ASTNode* create_identifier(char* name)
{
    return create_identifier_impl(name);
}

ASTNode* create_identifier_line(char* name, int line)
{
    return create_identifier_line_impl(name, line);
}

ASTNode* create_identifier_at(char* name, int line, int col)
{
    return create_identifier_at_impl(name, line, col);
}

ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right)
{
    return create_binary_op_impl(op, left, right);
}

ASTNode* create_fold_expression(int op, ASTNode* pack_expr, int is_right_fold)
{
    return create_fold_expression_impl(op, pack_expr, is_right_fold);
}

ASTNode* create_unary_op(int op, ASTNode* operand)
{
    return create_unary_op_impl(op, operand);
}

ASTNode* create_update_expression(int kind, int is_prefix, ASTNode* operand, int line)
{
    return create_update_expression_impl(kind, is_prefix, operand, line);
}

ASTNode* create_ternary_expression(ASTNode* cond, ASTNode* t, ASTNode* f, int line)
{
    return create_ternary_expression_impl(cond, t, f, line);
}

ASTNode* create_try_expression(ASTNode* expr, int line)
{
    return create_try_expression_impl(expr, line);
}

ASTNode* create_sizeof_type_expression(ASTNode* type, int line)
{
    return create_sizeof_type_expression_impl(type, line);
}

ASTNode* create_sizeof_value_expression(ASTNode* expr, int line)
{
    return create_sizeof_value_expression_impl(expr, line);
}

ASTNode* create_cexpr_expression(ASTNode* expr, int line)
{
    return create_cexpr_expression_impl(expr, line);
}

ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2, int line)
{
    return create_function_call_impl(name, arg1, arg2, line);
}

ASTNode* create_function_call_multi(char* name, ASTNode* args, int line)
{
    return create_function_call_multi_impl(name, args, line);
}

ASTNode* create_result_constructor(char* variant, ASTNode* type_args, ASTNode* args, int line)
{
    return create_result_constructor_impl(variant, type_args, args, line);
}

ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch)
{
    return create_if_statement_impl(condition, then_branch, else_if_branch, else_branch);
}

ASTNode* create_if_statement_with_init(ASTNode* condition_init, ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch)
{
    return create_if_statement_with_init_impl(condition_init, condition, then_branch, else_if_branch, else_branch);
}

ASTNode* create_cexpr_if_statement(ASTNode* condition, ASTNode* then_branch,
                                   ASTNode* else_if_branch,
                                   ASTNode* else_branch)
{
    return create_cexpr_if_statement_impl(condition, then_branch,
                                          else_if_branch, else_branch);
}

ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr)
{
    return create_let_declaration_impl(type, name, expr);
}

ASTNode* create_cexpr_declaration(ASTNode* type, char* name, ASTNode* expr)
{
    return create_cexpr_declaration_impl(type, name, expr);
}

ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr)
{
    return create_var_declaration_impl(type, name, expr);
}

ASTNode* create_cast_expression(int type, ASTNode* expr)
{
    return create_cast_expression_impl(type, expr);
}

ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members,
                           int is_public, int derive_debug,
                           int derive_json)
{
    return create_struct_def_impl(name, base_name, members, is_public,
                                  derive_debug, derive_json);
}

ASTNode* create_struct_member_list(ASTNode* member)
{
    return create_struct_member_list_impl(member);
}

ASTNode* add_struct_member(ASTNode* list, ASTNode* member)
{
    return add_struct_member_impl(list, member);
}

ASTNode* create_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr)
{
    return create_struct_member_impl(is_var, type, name, init_expr);
}

ASTNode* create_struct_method(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static)
{
    return create_struct_method_impl(type, name, params, body, is_public, is_static);
}

ASTNode* add_struct_method(ASTNode* list, ASTNode* method)
{
    return add_struct_method_impl(list, method);
}

ASTNode* create_struct_init(char* type_name, char* var_name)
{
    return create_struct_init_impl(type_name, var_name);
}

ASTNode* create_method_call_expr(ASTNode* object, char* method_name, ASTNode* args, int line)
{
    return create_method_call_expr_impl(object, method_name, args, line);
}

ASTNode* create_method_call(ASTNode* object, char* method, ASTNode* args, int line)
{
    return create_method_call_impl(object, method, args, line);
}

ASTNode* create_list_type()
{
    return create_list_type_impl();
}

ASTNode* create_list_literal(ASTNode* elements)
{
    return create_list_literal_impl(elements);
}

ASTNode* create_list_element_list(ASTNode* element)
{
    return create_list_element_list_impl(element);
}

ASTNode* add_list_element(ASTNode* list, ASTNode* element)
{
    return add_list_element_impl(list, element);
}

ASTNode* create_array_fill(ASTNode* value, ASTNode* count)
{
    return create_array_fill_impl(value, count);
}

ASTNode* create_expression_statement(ASTNode* expr)
{
    return create_expression_statement_impl(expr);
}

ASTNode* create_block_statement(ASTNode* stmt_list)
{
    return create_block_statement_impl(stmt_list);
}

ASTNode* create_else_if(ASTNode* condition, ASTNode* body)
{
    return create_else_if_impl(condition, body);
}

ASTNode* create_else_if_with_init(ASTNode* condition_init, ASTNode* condition, ASTNode* body)
{
    return create_else_if_with_init_impl(condition_init, condition, body);
}

ASTNode* add_else_if(ASTNode* else_if_list, ASTNode* else_if)
{
    return add_else_if_impl(else_if_list, else_if);
}

ASTNode* create_for_range(char* var_name, ASTNode* range, ASTNode* body, int line)
{
    return create_for_range_impl(var_name, range, body, line);
}

ASTNode* create_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body, int line)
{
    return create_for_iterator_impl(var_name, iterable, body, line);
}

ASTNode* create_for_enumerate(char* index_var, char* val_var, ASTNode* iterable, ASTNode* body, int line)
{
    return create_for_enumerate_impl(index_var, val_var, iterable, body, line);
}

ASTNode* create_while_statement(ASTNode* condition, ASTNode* body, int line, int uses_colon_without_guard)
{
    return create_while_statement_impl(condition, body, line, uses_colon_without_guard);
}

ASTNode* create_try_catch_stmt(ASTNode* try_block, char* catch_name,
                               ASTNode* catch_type, ASTNode* catch_block,
                               int line)
{
    return create_try_catch_stmt_impl(try_block, catch_name, catch_type,
                                      catch_block, line);
}

ASTNode* create_range_expression(ASTNode* start, ASTNode* end, int inclusive)
{
    return create_range_expression_impl(start, end, inclusive);
}

ASTNode* create_mod_declaration(char* name, int line)
{
    return create_mod_declaration_impl(name, line);
}

ASTNode* create_use_declaration(char* module_name, char* item_name, int line)
{
    return create_use_declaration_impl(module_name, item_name, line);
}

ASTNode* create_use_declaration_alias(char* module_name, char* item_name,
                                      char* alias_name, int line)
{
    return create_use_declaration_alias_impl(module_name, item_name, alias_name,
                                             line);
}

ASTNode* create_use_module_alias_declaration(char* module_name,
                                             char* alias_name, int line)
{
    return create_use_module_alias_declaration_impl(module_name, alias_name,
                                                    line);
}

ASTNode* create_use_all_declaration(char* module_name, int line)
{
    return create_use_all_declaration_impl(module_name, line);
}

ASTNode* create_type_alias(char* name, ASTNode* type_params, ASTNode* aliased_type)
{
    return create_type_alias_impl(name, type_params, aliased_type);
}

ASTNode* create_print_stmt(int kind, char* format_str, ASTNode* args, int line)
{
    return create_print_stmt_impl(kind, format_str, args, line);
}

ASTNode* create_debug_print_stmt(char* format_str, ASTNode* args, int line)
{
    return create_debug_print_stmt_impl(format_str, args, line);
}

ASTNode* create_print_expr_stmt(int kind, ASTNode* expr, int line)
{
    return create_print_expr_stmt_impl(kind, expr, line);
}

ASTNode* create_argument_list(ASTNode* arg)
{
    return create_argument_list_impl(arg);
}

ASTNode* add_argument(ASTNode* list, ASTNode* arg)
{
    return add_argument_impl(list, arg);
}

ASTNode* create_format_expr(char* format_str, ASTNode* args, int line)
{
    return create_format_expr_impl(format_str, args, line);
}

ASTNode* create_assert_eq(ASTNode* left, ASTNode* right, int line)
{
    return create_assert_eq_impl(left, right, line);
}

ASTNode* create_generic_list_type(ASTNode* element_type)
{
    return create_generic_list_type_impl(element_type);
}

ASTNode* create_map_type(ASTNode* key_type, ASTNode* value_type)
{
    return create_map_type_impl(key_type, value_type);
}

ASTNode* create_map_literal(ASTNode* entries)
{
    return create_map_literal_impl(entries);
}

ASTNode* create_map_entry_list(ASTNode* entry)
{
    return create_map_entry_list_impl(entry);
}

ASTNode* add_map_entry(ASTNode* list, ASTNode* entry)
{
    return add_map_entry_impl(list, entry);
}

ASTNode* create_map_entry(ASTNode* key, ASTNode* value)
{
    return create_map_entry_impl(key, value);
}

ASTNode* create_index_expression(ASTNode* base, ASTNode* index, int line)
{
    return create_index_expression_impl(base, index, line);
}

ASTNode* create_tuple_type(ASTNode* type_list)
{
    return create_tuple_type_impl(type_list);
}

ASTNode* create_type_list(ASTNode* type)
{
    return create_type_list_impl(type);
}

ASTNode* add_type_to_list(ASTNode* list, ASTNode* type)
{
    return add_type_to_list_impl(list, type);
}

ASTNode* create_tuple_literal(ASTNode* elements)
{
    return create_tuple_literal_impl(elements);
}

ASTNode* create_tuple_access(ASTNode* tuple, int index, int line)
{
    return create_tuple_access_impl(tuple, index, line);
}

ASTNode* create_map_keys_iterator(ASTNode* map_expr, int line)
{
    return create_map_keys_iterator_impl(map_expr, line);
}

ASTNode* create_map_values_iterator(ASTNode* map_expr, int line)
{
    return create_map_values_iterator_impl(map_expr, line);
}

ASTNode* create_map_entries_iterator(ASTNode* map_expr, int line)
{
    return create_map_entries_iterator_impl(map_expr, line);
}

ASTNode* create_struct_type_ref(char* name)
{
    return create_struct_type_ref_impl(name);
}

ASTNode* create_field_access(char* struct_name, char* field_name, int line)
{
    return create_field_access_impl(struct_name, field_name, line);
}

ASTNode* create_field_access_expr(ASTNode* object, char* field_name, int line)
{
    return create_field_access_expr_impl(object, field_name, line);
}

ASTNode* create_field_assignment(char* struct_name, char* field_name, ASTNode* expr, int line)
{
    return create_field_assignment_impl(struct_name, field_name, expr, line);
}

ASTNode* create_chained_field_assignment(ASTNode* target, ASTNode* expr, int line)
{
    return create_chained_field_assignment_impl(target, expr, line);
}

ASTNode* create_match_pattern(char* name, char* binding, int line)
{
    return create_match_pattern_impl(name, binding, line);
}

ASTNode* create_len_expression(ASTNode* expr, int line)
{
    return create_len_expression_impl(expr, line);
}

ASTNode* create_match_literal_pattern(ASTNode* literal, int line)
{
    return create_match_literal_pattern_impl(literal, line);
}

ASTNode* create_match_arm(ASTNode* pattern, ASTNode* expr, int line)
{
    return create_match_arm_impl(pattern, expr, line);
}

ASTNode* create_match_arm_list(ASTNode* arm)
{
    return create_match_arm_list_impl(arm);
}

ASTNode* add_match_arm(ASTNode* list, ASTNode* arm)
{
    return add_match_arm_impl(list, arm);
}

ASTNode* create_match_expression(ASTNode* target, ASTNode* arms, int line)
{
    return create_match_expression_impl(target, arms, line);
}

ASTNode* create_enum_def(char* name, ASTNode* variants, int is_public, int backing_type)
{
    return create_enum_def_impl(
        name, variants, is_public, static_cast<TypeNode::TypeKind>(backing_type));
}

ASTNode* create_enum_variant(char* name, int has_explicit_value, long long explicit_value)
{
    return create_enum_variant_impl(name, has_explicit_value, explicit_value);
}

ASTNode* create_enum_variant_string(char* name, char* explicit_string_value)
{
    return create_enum_variant_string_impl(name, explicit_string_value);
}

ASTNode* create_enum_variant_ref(char* name, char* ref_enum_name, char* ref_variant_name)
{
    return create_enum_variant_ref_impl(name, ref_enum_name, ref_variant_name);
}

ASTNode* create_enum_variant_list(ASTNode* variant)
{
    return create_enum_variant_list_impl(variant);
}

ASTNode* add_enum_variant(ASTNode* list, ASTNode* variant)
{
    return add_enum_variant_impl(list, variant);
}

ASTNode* create_enum_literal(char* enum_name, char* variant_name, int line)
{
    return create_enum_literal_impl(enum_name, variant_name, line);
}

ASTNode* create_closure(ASTNode* body)
{
    return create_closure_impl(body);
}

ASTNode* create_closure_with_params(ASTNode* params, ASTNode* body)
{
    return create_closure_with_params_impl(params, body);
}

ASTNode* create_type_param_list(char* param)
{
    return create_type_param_list_impl(param);
}

ASTNode* add_type_param(ASTNode* list, char* param)
{
    return add_type_param_impl(list, param);
}

ASTNode* create_bounded_type_param_list(char* param, char* trait_name)
{
    return create_bounded_type_param_list_impl(param, trait_name);
}

ASTNode* add_bounded_type_param(ASTNode* list, char* param, char* trait_name)
{
    return add_bounded_type_param_impl(list, param, trait_name);
}

ASTNode* create_generic_struct_def(char* name, char* base_name,
                                   ASTNode* type_params, ASTNode* members,
                                   int is_public, int derive_debug,
                                   int derive_json)
{
    return create_generic_struct_def_impl(name, base_name, type_params,
                                          members, is_public, derive_debug,
                                          derive_json);
}

ASTNode* create_trait_def(char* name, int line)
{
    return create_trait_def_impl(name, line);
}

ASTNode* create_impl_block(char* struct_name, ASTNode* type_params, char* trait_name)
{
    return create_impl_block_impl(struct_name, type_params, nullptr);
}

ASTNode* add_impl_method(ASTNode* impl, ASTNode* method)
{
    return add_impl_method_impl(impl, method);
}

ASTNode* create_struct_literal(char* struct_name, ASTNode* type_args, ASTNode* fields, int line)
{
    return create_struct_literal_impl(struct_name, type_args, fields, line);
}

ASTNode* create_struct_field_init_list(char* field_name, ASTNode* value)
{
    return create_struct_field_init_list_impl(field_name, value);
}

ASTNode* add_struct_field_init(ASTNode* list, char* field_name, ASTNode* value)
{
    return add_struct_field_init_impl(list, field_name, value);
}

ASTNode* create_generic_struct_type_ref(char* name, ASTNode* type_args)
{
    return create_generic_struct_type_ref_impl(name, type_args);
}

} // extern "C"
