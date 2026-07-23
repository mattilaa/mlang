#pragma once

#include "ir.h"

#include <map>
#include <string>

TypeNode::TypeKind getExpressionTypeKind(
    ExpressionNode* expr,
    const std::map<std::string, TypeNode::TypeKind>& variableTypes);
