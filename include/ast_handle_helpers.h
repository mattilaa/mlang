#ifndef AST_HANDLE_HELPERS_H
#define AST_HANDLE_HELPERS_H

#include <cstdint>

extern "C" {
int64_t mla_argument_list_create();
int64_t mla_argument_list_add(int64_t list, int64_t expr);
int64_t mla_function_call_create(const char* name, int line);
int64_t mla_function_call_set_args(int64_t call, int64_t args);
int64_t mla_function_call_add_arg(int64_t call, int64_t expr);
}

#endif // AST_HANDLE_HELPERS_H
