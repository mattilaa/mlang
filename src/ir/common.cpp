#include "ir/common.h"

#include <llvm/Config/llvm-config.h>
#include <cstdint>

namespace mlang::ir_detail::common
{

llvm::Value* Helpers::create_global_cstring(llvm::IRBuilder<>& builder,
                                            llvm::StringRef text,
                                            const llvm::Twine& name)
{
#if LLVM_VERSION_MAJOR >= 21
    return builder.CreateGlobalString(text, name);
#else
    return builder.CreateGlobalStringPtr(text, name);
#endif
}

bool Helpers::is_same_module_family(const std::string& a,
                                    const std::string& b)
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

std::string Helpers::type_name_for_error(TypeNode* typeNode)
{
    return typeNode ? typeNode->toString() : "<unknown>";
}

bool Helpers::isBitFieldTypeNode(TypeNode* type)
{
    if(!type)
        return false;
    if(type->kind == TypeNode::TYPE_BIT)
        return true;
    if(auto* ref = dynamic_cast<StructTypeRefNode*>(type))
        return ref->structName == "bit";
    return false;
}

bool Helpers::isEnumIntegralType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_INT:
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

bool Helpers::isEnumStringType(TypeNode::TypeKind kind)
{
    return kind == TypeNode::TYPE_STR8 || kind == TypeNode::TYPE_STRING;
}

unsigned Helpers::enumBitWidth(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_I8:
    case TypeNode::TYPE_U8:
        return 8;
    case TypeNode::TYPE_I16:
    case TypeNode::TYPE_U16:
        return 16;
    case TypeNode::TYPE_I64:
    case TypeNode::TYPE_U64:
        return 64;
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I32:
    case TypeNode::TYPE_U32:
    default:
        return 32;
    }
}

bool Helpers::enumIsUnsigned(TypeNode::TypeKind kind)
{
    return kind == TypeNode::TYPE_U8 || kind == TypeNode::TYPE_U16 ||
           kind == TypeNode::TYPE_U32 || kind == TypeNode::TYPE_U64;
}

std::string Helpers::enumBaseTypeName(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_INT:
        return "i32";
    case TypeNode::TYPE_I8:
        return "i8";
    case TypeNode::TYPE_I16:
        return "i16";
    case TypeNode::TYPE_I32:
        return "i32";
    case TypeNode::TYPE_I64:
        return "i64";
    case TypeNode::TYPE_U8:
        return "u8";
    case TypeNode::TYPE_U16:
        return "u16";
    case TypeNode::TYPE_U32:
        return "u32";
    case TypeNode::TYPE_U64:
        return "u64";
    default:
        return "i32";
    }
}

bool Helpers::fitsInEnumBaseType(TypeNode::TypeKind kind, int64_t value)
{
    const unsigned bits = Helpers::enumBitWidth(kind);
    if(Helpers::enumIsUnsigned(kind))
    {
        if(value < 0)
            return false;
        if(bits >= 64)
            return true;
        const uint64_t maxVal = (uint64_t{1} << bits) - 1u;
        return static_cast<uint64_t>(value) <= maxVal;
    }

    if(bits >= 64)
        return true;

    const int64_t minVal = -(int64_t{1} << (bits - 1));
    const int64_t maxVal = (int64_t{1} << (bits - 1)) - 1;
    return value >= minVal && value <= maxVal;
}

TypeNode::TypeKind Helpers::normalizeInferredKind(TypeNode::TypeKind kind)
{
    if(kind == TypeNode::TYPE_INT)
        return TypeNode::TYPE_I32;
    if(kind == TypeNode::TYPE_STR8 || kind == TypeNode::TYPE_STR16)
        return TypeNode::TYPE_STRING;
    return kind;
}

bool Helpers::isIntegerInferKind(TypeNode::TypeKind kind)
{
    switch(Helpers::normalizeInferredKind(kind))
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

bool Helpers::isFloatInferKind(TypeNode::TypeKind kind)
{
    kind = Helpers::normalizeInferredKind(kind);
    return kind == TypeNode::TYPE_FLOAT || kind == TypeNode::TYPE_DOUBLE;
}

} // namespace mlang::ir_detail::common
