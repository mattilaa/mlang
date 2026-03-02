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

ASTNode* mla_ast_function_call_simple(char* name, ASTNode* arg1, ASTNode* arg2, int line)
{
    return create_function_call_impl(name, arg1, arg2, line);
}

ASTNode* mla_ast_function_call_from_list(char* name, ASTNode* args, int line)
{
    return create_function_call_multi_impl(name, args, line);
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

} // extern "C"
