#pragma once

#include "ir.h"

#include <string>
#include <unordered_map>

namespace mlang::ir_detail::return_inference
{

bool infer_function_return_type(
    FunctionDefNode* fn,
    const std::unordered_map<std::string, TypeNode::TypeKind>& fnReturnKinds,
    TypeNode::TypeKind& inferred,
    std::string& reason);

} // namespace mlang::ir_detail::return_inference
