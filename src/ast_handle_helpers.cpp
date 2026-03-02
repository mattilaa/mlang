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

} // extern "C"
