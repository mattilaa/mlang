#pragma once

#include "ir.h"

#include <llvm/IR/IRBuilder.h>
#include <map>
#include <string>
#include <vector>

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
    static bool isSynthesizedPropertyLockFieldName(
        const std::string& fieldName);
    static bool trait_names_equivalent(const std::string& lhs,
                                       const std::string& rhs);
    static std::string type_name_for_error(TypeNode* typeNode);
    static TypeNode* type_from_text(std::string text);
    static std::string resolve_visible_struct_base_name(
        const std::string& structName,
        const std::map<std::string, StructDefNode*>& genericStructTemplates,
        const std::map<
            std::string,
            std::map<std::string, std::pair<bool, StructMethodNode*>>>&
            structMethods,
        const std::map<std::string, std::pair<bool, std::string>>&
            structVisibility);
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
