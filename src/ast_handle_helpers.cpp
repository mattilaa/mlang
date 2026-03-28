#include "ast.h"
#include "ast_impl.h"
#include "ast_handle_helpers.h"

#include <string>

namespace {

ASTNode* node_from_handle(int64_t handle)
{
    return reinterpret_cast<ASTNode*>(handle);
}

int64_t handle_from_node(ASTNode* node)
{
    return reinterpret_cast<int64_t>(node);
}

ExpressionNode* expression_from_handle(int64_t handle)
{
    return reinterpret_cast<ExpressionNode*>(handle);
}

ArgumentListNode* argument_list_from_handle(int64_t handle)
{
    return reinterpret_cast<ArgumentListNode*>(handle);
}

FunctionCallNode* function_call_from_handle(int64_t handle)
{
    return reinterpret_cast<FunctionCallNode*>(handle);
}

} // namespace

extern "C" {

int64_t mla_argument_list_create()
{
    return handle_from_node(new ArgumentListNode());
}

int64_t mla_argument_list_add(int64_t list, int64_t expr)
{
    auto* node = argument_list_from_handle(list);
    if(node && expr)
    {
        node->args.push_back(expression_from_handle(expr));
    }
    return list;
}

int64_t mla_function_call_create(const char* name, int line)
{
    auto* node = new FunctionCallNode(name ? std::string(name) : std::string());
    node->line = line;
    return handle_from_node(node);
}

int64_t mla_function_call_set_args(int64_t call, int64_t args)
{
    auto* node = function_call_from_handle(call);
    auto* argument_list = argument_list_from_handle(args);
    if(node && argument_list)
    {
        node->arguments = argument_list->args;
    }
    return call;
}

int64_t mla_function_call_add_arg(int64_t call, int64_t expr)
{
    auto* node = function_call_from_handle(call);
    if(node && expr)
    {
        node->arguments.push_back(expression_from_handle(expr));
    }
    return call;
}

ASTNode* mla_ast_argument_list_create(ASTNode* arg)
{
    return create_argument_list_impl(arg);
}

ASTNode* mla_ast_argument_list_add(ASTNode* list, ASTNode* arg)
{
    return add_argument_impl(list, arg);
}

ASTNode* mla_ast_format_argument(char* name, ASTNode* value)
{
    return create_format_argument_impl(name, value);
}

ASTNode* mla_ast_format_argument_list_create(ASTNode* arg)
{
    return create_format_argument_list_impl(arg);
}

ASTNode* mla_ast_format_argument_list_add(ASTNode* list, ASTNode* arg)
{
    return add_format_argument_impl(list, arg);
}

ASTNode* mla_ast_function_call_simple(char* name, ASTNode* arg1, ASTNode* arg2, int line)
{
    return create_function_call_impl(name, arg1, arg2, line);
}

ASTNode* mla_ast_function_call_from_list(char* name, ASTNode* args, int line)
{
    return create_function_call_multi_impl(name, args, line);
}

ASTNode* mla_ast_parameter(ASTNode* type, char* name)
{
    return create_parameter_impl(type, name);
}

ASTNode* mla_ast_parameter_list(ASTNode* param)
{
    return create_parameter_list_impl(param);
}

ASTNode* mla_ast_empty_parameter_list()
{
    return create_empty_parameter_list_impl();
}

ASTNode* mla_ast_struct_init(char* type_name, char* var_name)
{
    return create_struct_init_impl(type_name, var_name);
}

ASTNode* mla_ast_statement_list_create(ASTNode* stmt)
{
    return create_statement_list_impl(stmt);
}

ASTNode* mla_ast_statement_list_add(ASTNode* list, ASTNode* stmt)
{
    return add_statement_impl(list, stmt);
}

ASTNode* mla_ast_map_entry(ASTNode* key, ASTNode* value)
{
    return create_map_entry_impl(key, value);
}

ASTNode* mla_ast_map_entry_list_create(ASTNode* entry)
{
    return create_map_entry_list_impl(entry);
}

ASTNode* mla_ast_map_entry_list_add(ASTNode* list, ASTNode* entry)
{
    return add_map_entry_impl(list, entry);
}

ASTNode* mla_ast_map_literal(ASTNode* entries)
{
    return create_map_literal_impl(entries);
}

ASTNode* mla_ast_struct_field_init_list(char* field_name, ASTNode* value)
{
    return create_struct_field_init_list_impl(field_name, value);
}

ASTNode* mla_ast_struct_field_init_list_add(ASTNode* list, char* field_name, ASTNode* value)
{
    return add_struct_field_init_impl(list, field_name, value);
}

ASTNode* mla_ast_struct_literal(char* struct_name, ASTNode* type_args, ASTNode* fields, int line)
{
    return create_struct_literal_impl(struct_name, type_args, fields, line);
}

ASTNode* mla_ast_list_literal(ASTNode* elements)
{
    return create_list_literal_impl(elements);
}

ASTNode* mla_ast_list_element_list(ASTNode* element)
{
    return create_list_element_list_impl(element);
}

ASTNode* mla_ast_list_element_list_add(ASTNode* list, ASTNode* element)
{
    return add_list_element_impl(list, element);
}

ASTNode* mla_ast_array_fill(ASTNode* value, ASTNode* count)
{
    return create_array_fill_impl(value, count);
}

ASTNode* mla_ast_result_constructor(char* variant, ASTNode* type_args, ASTNode* args, int line)
{
    return create_result_constructor_impl(variant, type_args, args, line);
}

ASTNode* mla_ast_enum_variant(char* name, int has_explicit_value, long long explicit_value)
{
    return create_enum_variant_impl(name, has_explicit_value, explicit_value);
}

ASTNode* mla_ast_enum_variant_list(ASTNode* variant)
{
    return create_enum_variant_list_impl(variant);
}

ASTNode* mla_ast_enum_variant_list_create(ASTNode* variant)
{
    return create_enum_variant_list_impl(variant);
}

ASTNode* mla_ast_enum_variant_list_add(ASTNode* list, ASTNode* variant)
{
    return add_enum_variant_impl(list, variant);
}

ASTNode* mla_ast_enum_variant_ref(char* name, char* ref_enum_name, char* ref_variant_name)
{
    return create_enum_variant_ref_impl(name, ref_enum_name, ref_variant_name);
}

ASTNode* mla_ast_enum_literal(char* enum_name, char* variant_name, int line)
{
    return create_enum_literal_impl(enum_name, variant_name, line);
}

ASTNode* mla_ast_struct_member_list(ASTNode* member)
{
    return create_struct_member_list_impl(member);
}

ASTNode* mla_ast_struct_member_list_add(ASTNode* list, ASTNode* member)
{
    return add_struct_member_impl(list, member);
}

ASTNode* mla_ast_struct_member(int is_var, ASTNode* type, char* name, ASTNode* init_expr)
{
    return create_struct_member_impl(is_var, type, name, init_expr);
}

ASTNode* mla_ast_struct_method(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_static)
{
    return create_struct_method_impl(type, name, params, body, is_public, is_static);
}

ASTNode* mla_ast_struct_member_add_method(ASTNode* list, ASTNode* method)
{
    return add_struct_method_impl(list, method);
}

ASTNode* mla_ast_trait_def(char* name, int line)
{
    return create_trait_def_impl(name, line);
}

ASTNode* mla_ast_impl_block(char* struct_name, ASTNode* type_params, char* trait_name)
{
    return create_impl_block_impl(struct_name, type_params, trait_name);
}

ASTNode* mla_ast_impl_add_method(ASTNode* impl, ASTNode* method)
{
    return add_impl_method_impl(impl, method);
}

ASTNode* mla_ast_type_alias(char* name, ASTNode* type_params, ASTNode* aliased_type)
{
    return create_type_alias_impl(name, type_params, aliased_type);
}

ASTNode* mla_ast_block_statement(ASTNode* stmt_list)
{
    return create_block_statement(stmt_list);
}

ASTNode* mla_ast_match_arm(ASTNode* pattern, ASTNode* expr, int line)
{
    return create_match_arm(pattern, expr, line);
}

ASTNode* mla_ast_match_arm_list(ASTNode* arm)
{
    return create_match_arm_list(arm);
}

ASTNode* mla_ast_add_match_arm(ASTNode* list, ASTNode* arm)
{
    return add_match_arm(list, arm);
}

ASTNode* mla_ast_match_expression(ASTNode* target, ASTNode* arms, int line)
{
    return create_match_expression(target, arms, line);
}

ASTNode* mla_ast_for_range(char* var_name, ASTNode* range, ASTNode* body, int line)
{
    return create_for_range(var_name, range, body, line);
}

ASTNode* mla_ast_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body, int line)
{
    return create_for_iterator(var_name, iterable, body, line);
}

ASTNode* mla_ast_for_enumerate(char* index_var, char* val_var, ASTNode* iterable, ASTNode* body, int line)
{
    return create_for_enumerate(index_var, val_var, iterable, body, line);
}

ASTNode* mla_ast_while_statement(ASTNode* condition, ASTNode* body, int line, int uses_colon_without_guard)
{
    return create_while_statement(condition, body, line, uses_colon_without_guard);
}

ASTNode* mla_ast_if_statement(ASTNode* condition, ASTNode* then_branch, ASTNode* else_if_branch, ASTNode* else_branch)
{
    return create_if_statement_impl(condition, then_branch, else_if_branch, else_branch);
}

ASTNode* mla_ast_if_statement_with_init(ASTNode* condition_init, ASTNode* condition, ASTNode* then_branch,
                                        ASTNode* else_if_branch, ASTNode* else_branch)
{
    return create_if_statement_with_init_impl(condition_init, condition, then_branch,
                                              else_if_branch, else_branch);
}

ASTNode* mla_ast_else_if(ASTNode* condition, ASTNode* body)
{
    return create_else_if_impl(condition, body);
}

ASTNode* mla_ast_else_if_with_init(ASTNode* condition_init, ASTNode* condition, ASTNode* body)
{
    return create_else_if_with_init_impl(condition_init, condition, body);
}

ASTNode* mla_ast_let_declaration(ASTNode* type, char* name, ASTNode* expr)
{
    return create_let_declaration_impl(type, name, expr);
}

ASTNode* mla_ast_var_declaration(ASTNode* type, char* name, ASTNode* expr)
{
    return create_var_declaration_impl(type, name, expr);
}

ASTNode* mla_ast_cast_expression(int type, ASTNode* expr)
{
    return create_cast_expression_impl(type, expr);
}

ASTNode* mla_ast_range_expression(ASTNode* start, ASTNode* end, int inclusive)
{
    return create_range_expression_impl(start, end, inclusive);
}

ASTNode* mla_ast_print_stmt(int kind, char* format_str, ASTNode* args, int line)
{
    return create_print_stmt_impl(kind, format_str, args, line);
}

ASTNode* mla_ast_print_expr_stmt(int kind, ASTNode* expr, int line)
{
    return create_print_expr_stmt_impl(kind, expr, line);
}

ASTNode* mla_ast_debug_print_stmt(char* format_str, ASTNode* args, int line)
{
    return create_debug_print_stmt_impl(format_str, args, line);
}

ASTNode* mla_ast_assert_eq(ASTNode* left, ASTNode* right, int line)
{
    return create_assert_eq_impl(left, right, line);
}

ASTNode* mla_ast_assert(ASTNode* condition, int line)
{
    return create_assert_impl(condition, line);
}

ASTNode* mla_ast_static_assert(ASTNode* condition, int line)
{
    return create_static_assert_impl(condition, line);
}

ASTNode* mla_ast_unsafe_block(ASTNode* block, int line)
{
    return create_unsafe_block_impl(block, line);
}

ASTNode* mla_ast_break_stmt(int line)
{
    return create_break_stmt_impl(line);
}

ASTNode* mla_ast_continue_stmt(int line)
{
    return create_continue_stmt_impl(line);
}

ASTNode* mla_ast_expression_statement(ASTNode* expr)
{
    return create_expression_statement_impl(expr);
}

ASTNode* mla_ast_deref_assignment(ASTNode* pointer_expr, ASTNode* expr, int line)
{
    return create_deref_assignment_impl(pointer_expr, expr, line);
}

ASTNode* mla_ast_mod_declaration(char* name, int line)
{
    return create_mod_declaration_impl(name, line);
}

ASTNode* mla_ast_use_declaration(char* module_name, char* item_name, int line)
{
    return create_use_declaration_impl(module_name, item_name, line);
}

ASTNode* mla_ast_use_declaration_alias(char* module_name, char* item_name,
                                       char* alias_name, int line)
{
    return create_use_declaration_alias_impl(module_name, item_name, alias_name,
                                             line);
}

ASTNode* mla_ast_use_module_alias_declaration(char* module_name,
                                              char* alias_name, int line)
{
    return create_use_module_alias_declaration_impl(module_name, alias_name,
                                                    line);
}

ASTNode* mla_ast_use_all_declaration(char* module_name, int line)
{
    return create_use_all_declaration_impl(module_name, line);
}

ASTNode* mla_ast_function_def(ASTNode* type, char* name, ASTNode* params, ASTNode* body, int is_public, int is_extern)
{
    return create_function_def(type, name, params, body, is_public, is_extern);
}

ASTNode* mla_ast_top_level_list(ASTNode* item)
{
    return create_top_level_list(item);
}

ASTNode* mla_ast_add_to_top_level_list(ASTNode* list, ASTNode* item)
{
    return add_to_top_level_list(list, item);
}

ASTNode* mla_ast_program(ASTNode* top_level_list)
{
    return create_program(top_level_list);
}

ASTNode* mla_ast_type_node(int type)
{
    return create_type_node(static_cast<TypeNode::TypeKind>(type));
}

ASTNode* mla_ast_list_type()
{
    return create_list_type();
}

ASTNode* mla_ast_generic_list_type(ASTNode* element_type)
{
    return create_generic_list_type(element_type);
}

ASTNode* mla_ast_map_type(ASTNode* key_type, ASTNode* value_type)
{
    return create_map_type(key_type, value_type);
}

ASTNode* mla_ast_tuple_type(ASTNode* type_list)
{
    return create_tuple_type(type_list);
}

ASTNode* mla_ast_type_list(ASTNode* type)
{
    return create_type_list(type);
}

ASTNode* mla_ast_struct_type_ref(char* name)
{
    return create_struct_type_ref(name);
}

ASTNode* mla_ast_generic_struct_type_ref(char* name, ASTNode* type_args)
{
    return create_generic_struct_type_ref(name, type_args);
}

ASTNode* mla_ast_pointer_type(ASTNode* element_type)
{
    return create_pointer_type(element_type);
}

ASTNode* mla_ast_reference_type(ASTNode* element_type, int is_mutable)
{
    return create_reference_type(element_type, is_mutable);
}

ASTNode* mla_ast_struct_def(char* name, char* base_name, ASTNode* members, int is_public, int derive_debug)
{
    return create_struct_def(name, base_name, members, is_public, derive_debug);
}

ASTNode* mla_ast_generic_struct_def(char* name, char* base_name, ASTNode* type_params, ASTNode* members, int is_public, int derive_debug)
{
    return create_generic_struct_def(name, base_name, type_params, members, is_public, derive_debug);
}

ASTNode* mla_ast_enum_def(char* name, ASTNode* variants, int is_public, int backing_type)
{
    return create_enum_def(name, variants, is_public, backing_type);
}

ASTNode* mla_ast_assignment(char* name, ASTNode* expr, int line)
{
    return create_assignment(name, expr, line);
}

ASTNode* mla_ast_field_access(char* struct_name, char* field_name, int line)
{
    return create_field_access(struct_name, field_name, line);
}

ASTNode* mla_ast_field_assignment(char* struct_name, char* field_name, ASTNode* expr, int line)
{
    return create_field_assignment(struct_name, field_name, expr, line);
}

ASTNode* mla_ast_chained_field_assignment(ASTNode* target, ASTNode* expr, int line)
{
    return create_chained_field_assignment(target, expr, line);
}

ASTNode* mla_ast_field_access_expr(ASTNode* object, char* field_name, int line)
{
    return create_field_access_expr(object, field_name, line);
}

ASTNode* mla_ast_identifier(char* name)
{
    return create_identifier_impl(name);
}

ASTNode* mla_ast_identifier_line(char* name, int line)
{
    return create_identifier_line_impl(name, line);
}

ASTNode* mla_ast_identifier_at(char* name, int line, int col)
{
    return create_identifier_at_impl(name, line, col);
}

ASTNode* mla_ast_struct_list(ASTNode* struct_def)
{
    return create_struct_list_impl(struct_def);
}

ASTNode* mla_ast_function_list(ASTNode* function)
{
    return create_function_list_impl(function);
}

ASTNode* mla_ast_return_stmt(ASTNode* expr)
{
    return create_return_stmt(expr);
}

ASTNode* mla_ast_literal_int(int value)
{
    return create_int_literal(value);
}

ASTNode* mla_ast_literal_bool(int value)
{
    return create_bool_literal(value);
}

ASTNode* mla_ast_literal_float(float value)
{
    return create_float_literal(value);
}

ASTNode* mla_ast_literal_double(float value)
{
    return create_double_literal(value);
}

ASTNode* mla_ast_literal_string(char* value)
{
    return create_string_literal(value);
}

ASTNode* mla_ast_binary_op(int op, ASTNode* left, ASTNode* right)
{
    return create_binary_op(op, left, right);
}

ASTNode* mla_ast_fold_expression(int op, ASTNode* pack_expr, int is_right_fold)
{
    return create_fold_expression(op, pack_expr, is_right_fold);
}

ASTNode* mla_ast_ternary_expression(ASTNode* cond, ASTNode* t, ASTNode* f, int line)
{
    return create_ternary_expression(cond, t, f, line);
}

ASTNode* mla_ast_try_expression(ASTNode* expr, int line)
{
    return create_try_expression(expr, line);
}

ASTNode* mla_ast_update_expression(int kind, int is_prefix, ASTNode* operand, int line)
{
    return create_update_expression_impl(kind, is_prefix, operand, line);
}

ASTNode* mla_ast_match_pattern(char* name, char* binding, int line)
{
    return create_match_pattern(name, binding, line);
}

ASTNode* mla_ast_match_literal_pattern(ASTNode* literal, int line)
{
    return create_match_literal_pattern(literal, line);
}

ASTNode* mla_ast_tuple_literal(ASTNode* elements)
{
    return create_tuple_literal(elements);
}

ASTNode* mla_ast_tuple_access(ASTNode* tuple, int index, int line)
{
    return create_tuple_access(tuple, index, line);
}

ASTNode* mla_ast_index_expression(ASTNode* base, ASTNode* index, int line)
{
    return create_index_expression(base, index, line);
}

ASTNode* mla_ast_map_keys_iterator(ASTNode* map_expr, int line)
{
    return create_map_keys_iterator(map_expr, line);
}

ASTNode* mla_ast_map_values_iterator(ASTNode* map_expr, int line)
{
    return create_map_values_iterator(map_expr, line);
}

ASTNode* mla_ast_map_entries_iterator(ASTNode* map_expr, int line)
{
    return create_map_entries_iterator(map_expr, line);
}

ASTNode* mla_ast_closure(ASTNode* body)
{
    return create_closure(body);
}

ASTNode* mla_ast_closure_with_params(ASTNode* params, ASTNode* body)
{
    return create_closure_with_params(params, body);
}

ASTNode* mla_ast_method_call_expr(ASTNode* object, char* method_name, ASTNode* args, int line)
{
    return create_method_call_expr_impl(object, method_name, args, line);
}

ASTNode* mla_ast_method_call(ASTNode* object, char* method, ASTNode* args, int line)
{
    return create_method_call_impl(object, method, args, line);
}

ASTNode* mla_ast_format_expr(char* format_str, ASTNode* args, int line)
{
    return create_format_expr_impl(format_str, args, line);
}

} // extern "C"
