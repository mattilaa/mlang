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
}

#endif // AST_HANDLE_HELPERS_H
