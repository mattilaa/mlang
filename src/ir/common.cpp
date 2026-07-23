#include "ir/common.h"

#include <llvm/Config/llvm-config.h>

namespace mlang::ir_detail
{

llvm::Value* create_global_cstring(llvm::IRBuilder<>& builder,
                                   llvm::StringRef text,
                                   const llvm::Twine& name)
{
#if LLVM_VERSION_MAJOR >= 21
    return builder.CreateGlobalString(text, name);
#else
    return builder.CreateGlobalStringPtr(text, name);
#endif
}

bool is_same_module_family(const std::string& a, const std::string& b)
{
    if(a.empty() || b.empty())
        return a == b;
    if(a == b)
        return true;
    auto is_nested = [](const std::string& parent, const std::string& child)
    {
        if(child.size() <= parent.size())
            return false;
        if(child.compare(0, parent.size(), parent) != 0)
            return false;
        return child.compare(parent.size(), 2, "::") == 0;
    };
    return is_nested(a, b) || is_nested(b, a);
}

std::string type_name_for_error(TypeNode* typeNode)
{
    return typeNode ? typeNode->toString() : "<unknown>";
}

bool isBitFieldTypeNode(TypeNode* type)
{
    if(!type)
        return false;
    if(type->kind == TypeNode::TYPE_BIT)
        return true;
    if(auto* ref = dynamic_cast<StructTypeRefNode*>(type))
        return ref->structName == "bit";
    return false;
}

TypeNode::TypeKind normalizeInferredKind(TypeNode::TypeKind kind)
{
    if(kind == TypeNode::TYPE_INT)
        return TypeNode::TYPE_I32;
    if(kind == TypeNode::TYPE_STR8 || kind == TypeNode::TYPE_STR16)
        return TypeNode::TYPE_STRING;
    return kind;
}

bool isIntegerInferKind(TypeNode::TypeKind kind)
{
    switch(normalizeInferredKind(kind))
    {
    case TypeNode::TYPE_I8:
    case TypeNode::TYPE_I16:
    case TypeNode::TYPE_I32:
    case TypeNode::TYPE_I64:
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
        return true;
    default:
        return false;
    }
}

bool isFloatInferKind(TypeNode::TypeKind kind)
{
    kind = normalizeInferredKind(kind);
    return kind == TypeNode::TYPE_FLOAT || kind == TypeNode::TYPE_DOUBLE;
}

} // namespace mlang::ir_detail
