#pragma once

#include "ir.h"

#include <llvm/IR/IRBuilder.h>
#include <string>

namespace mlang::ir_detail
{

llvm::Value* create_global_cstring(llvm::IRBuilder<>& builder,
                                   llvm::StringRef text,
                                   const llvm::Twine& name = "");
bool is_same_module_family(const std::string& a, const std::string& b);
std::string type_name_for_error(TypeNode* typeNode);
bool isBitFieldTypeNode(TypeNode* type);
TypeNode::TypeKind normalizeInferredKind(TypeNode::TypeKind kind);
bool isIntegerInferKind(TypeNode::TypeKind kind);
bool isFloatInferKind(TypeNode::TypeKind kind);

} // namespace mlang::ir_detail
