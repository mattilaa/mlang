#pragma once

#include "ir.h"

#include <llvm/IR/IRBuilder.h>
#include <string>

namespace mlang::ir_detail::common
{

class Helpers
{
public:
    static llvm::Value* create_global_cstring(llvm::IRBuilder<>& builder,
                                              llvm::StringRef text,
                                              const llvm::Twine& name = "");
    static bool is_same_module_family(const std::string& a,
                                      const std::string& b);
    static std::string type_name_for_error(TypeNode* typeNode);
    static bool isBitFieldTypeNode(TypeNode* type);
    static bool isEnumIntegralType(TypeNode::TypeKind kind);
    static bool isEnumStringType(TypeNode::TypeKind kind);
    static unsigned enumBitWidth(TypeNode::TypeKind kind);
    static bool enumIsUnsigned(TypeNode::TypeKind kind);
    static std::string enumBaseTypeName(TypeNode::TypeKind kind);
    static bool fitsInEnumBaseType(TypeNode::TypeKind kind, int64_t value);
    static TypeNode::TypeKind normalizeInferredKind(TypeNode::TypeKind kind);
    static bool isIntegerInferKind(TypeNode::TypeKind kind);
    static bool isFloatInferKind(TypeNode::TypeKind kind);
};

} // namespace mlang::ir_detail::common
