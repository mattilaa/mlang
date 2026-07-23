#include "ir.h"

namespace
{

std::string displayTypeName(TypeNode* type)
{
    if(!type)
        return "unknown";

    if(auto* ref = dynamic_cast<ReferenceTypeNode*>(type))
    {
        return ref->isMutable ? "&mut " + displayTypeName(ref->elementType)
                              : "&" + displayTypeName(ref->elementType);
    }
    if(auto* ptr = dynamic_cast<PointerTypeNode*>(type))
        return "ptr<" + displayTypeName(ptr->elementType) + ">";
    if(auto* gl = dynamic_cast<GenericListTypeNode*>(type))
        return "list<" + displayTypeName(gl->elementType) + ">";
    if(auto* mapTy = dynamic_cast<MapTypeNode*>(type))
    {
        return "map<" + displayTypeName(mapTy->keyType) + ", " +
               displayTypeName(mapTy->valueType) + ">";
    }
    if(auto* tupleTy = dynamic_cast<TupleTypeNode*>(type))
    {
        std::string out = "tuple<";
        if(tupleTy->elementTypes)
        {
            for(size_t i = 0; i < tupleTy->elementTypes->types.size(); ++i)
            {
                if(i > 0)
                    out += ", ";
                out += displayTypeName(tupleTy->elementTypes->types[i]);
            }
        }
        out += ">";
        return out;
    }
    if(auto* s = dynamic_cast<StructTypeRefNode*>(type))
        return s->structName;
    if(auto* gs = dynamic_cast<GenericStructTypeRefNode*>(type))
    {
        std::string out = gs->structName + "<";
        for(size_t i = 0; i < gs->typeArgs.size(); ++i)
        {
            if(i > 0)
                out += ", ";
            out += displayTypeName(gs->typeArgs[i]);
        }
        out += ">";
        return out;
    }
    if(auto* trait = dynamic_cast<TraitObjectTypeNode*>(type))
        return "dyn " + trait->traitName;

    switch(type->kind)
    {
    case TypeNode::TYPE_VOID:
        return "void";
    case TypeNode::TYPE_BOOL:
        return "bool";
    case TypeNode::TYPE_BIT:
        return "bit";
    case TypeNode::TYPE_INT:
        return "i32";
    case TypeNode::TYPE_FLOAT:
        return "f32";
    case TypeNode::TYPE_DOUBLE:
        return "f64";
    case TypeNode::TYPE_STRING:
    case TypeNode::TYPE_STR8:
        return "str8";
    case TypeNode::TYPE_STR16:
        return "str16";
    case TypeNode::TYPE_LIST:
        return "list";
    case TypeNode::TYPE_MAP:
        return "map";
    case TypeNode::TYPE_TUPLE:
        return "tuple";
    case TypeNode::TYPE_PTR:
        return "ptr";
    case TypeNode::TYPE_STRUCT:
        return "struct";
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
    case TypeNode::TYPE_REF:
    case TypeNode::TYPE_REF_MUT:
        return "reference";
    case TypeNode::TYPE_TRAIT_OBJECT:
        return "dyn";
    }

    return "unknown";
}

} // namespace

std::string CodeGenerator::expressionTypeNameForLog(ExpressionNode* expr,
                                                    int line)
{
    if(!expr)
        return "unknown";

    if(dynamic_cast<IntLiteralNode*>(expr))
        return "i64";
    if(dynamic_cast<BoolLiteralNode*>(expr))
        return "bool";
    if(dynamic_cast<FloatLiteralNode*>(expr))
        return "f32";
    if(dynamic_cast<DoubleLiteralNode*>(expr))
        return "f64";
    if(dynamic_cast<StringLiteralNode*>(expr) ||
       dynamic_cast<FormatNode*>(expr))
        return "str8";

    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto enumIt = enumVariableTypes.find(id->name);
        if(enumIt != enumVariableTypes.end())
            return enumIt->second;
    }

    TypeNode* typeNode = getLValueType(expr, line);
    if(typeNode)
        return displayTypeName(typeNode);
    return "unknown";
}
