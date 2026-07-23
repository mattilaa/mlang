#pragma once

#include "ir.h"

#include <set>
#include <string>

namespace mlang::ir_detail::ast_analysis
{

void collect_used_idents(ASTNode* node, std::set<std::string>& out);
bool contains_update_expression(ASTNode* node);
bool contains_exception_control_flow(ASTNode* node);
bool contains_unsupported_try_control_flow(StatementNode* node);
ExpressionNode* strip_iter_methods(ExpressionNode* expr);

} // namespace mlang::ir_detail::ast_analysis
