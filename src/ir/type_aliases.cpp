#include "ir.h"
#include "ir/common.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using mlang::ir_detail::type_name_for_error;

std::string CodeGenerator::typeMangle(TypeNode* typeNode) const
{
    if(!typeNode)
        return "void";

    if(auto* refType = dynamic_cast<ReferenceTypeNode*>(typeNode))
    {
        return refType->isMutable
                   ? "ref_mut_" + typeMangle(refType->elementType)
                   : "ref_" + typeMangle(refType->elementType);
    }

    if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(typeNode))
        return "dyn_" + traitObj->traitName;

    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(typeNode))
        return "ptr_" + typeMangle(ptrType->elementType);

    if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(typeNode))
    {
        return "array_" + typeMangle(arrayType->elementType) + "_" +
               std::to_string(arrayType->capacity);
    }

    if(auto* genList = dynamic_cast<GenericListTypeNode*>(typeNode))
        return "list_" + typeMangle(genList->elementType);

    if(auto* mapType = dynamic_cast<MapTypeNode*>(typeNode))
    {
        return "map_" + typeMangle(mapType->keyType) + "_" +
               typeMangle(mapType->valueType);
    }

    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(typeNode))
    {
        std::string out = "tuple";
        if(tupleType->elementTypes)
        {
            for(auto* elem : tupleType->elementTypes->types)
            {
                out += "_" + typeMangle(elem);
            }
        }
        return out;
    }

    if(auto* genStruct = dynamic_cast<GenericStructTypeRefNode*>(typeNode))
    {
        std::string out = "struct_" + genStruct->structName;
        for(auto* arg : genStruct->typeArgs)
        {
            out += "_" + typeMangle(arg);
        }
        return out;
    }

    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(typeNode))
        return "struct_" + structRef->structName;

    switch(typeNode->kind)
    {
    case TypeNode::TYPE_VOID:
        return "void";
    case TypeNode::TYPE_BOOL:
        return "bool";
    case TypeNode::TYPE_INT:
        return "i32";
    case TypeNode::TYPE_FLOAT:
        return "f32";
    case TypeNode::TYPE_DOUBLE:
        return "f64";
    case TypeNode::TYPE_STRING:
        return "str8";
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
    case TypeNode::TYPE_REF:
        return "ref";
    case TypeNode::TYPE_REF_MUT:
        return "ref_mut";
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
    default:
        return "unknown";
    }
}

TypeNode* CodeGenerator::cloneTypeNode(TypeNode* typeNode)
{
    if(!typeNode)
        return nullptr;

    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(typeNode))
        return new PointerTypeNode(cloneTypeNode(ptrType->elementType));

    if(auto* refType = dynamic_cast<ReferenceTypeNode*>(typeNode))
    {
        return new ReferenceTypeNode(cloneTypeNode(refType->elementType),
                                     refType->isMutable);
    }

    if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(typeNode))
        return new TraitObjectTypeNode(traitObj->traitName);

    if(auto* arrayType = dynamic_cast<ArrayTypeNode*>(typeNode))
    {
        return new ArrayTypeNode(cloneTypeNode(arrayType->elementType),
                                 arrayType->capacity);
    }

    if(auto* listType = dynamic_cast<GenericListTypeNode*>(typeNode))
        return new GenericListTypeNode(cloneTypeNode(listType->elementType));

    if(auto* mapType = dynamic_cast<MapTypeNode*>(typeNode))
    {
        return new MapTypeNode(cloneTypeNode(mapType->keyType),
                               cloneTypeNode(mapType->valueType));
    }

    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(typeNode))
    {
        auto* list = new TypeListNode();
        if(tupleType->elementTypes)
        {
            for(auto* elem : tupleType->elementTypes->types)
                list->addType(cloneTypeNode(elem));
        }
        return new TupleTypeNode(list);
    }

    if(auto* structType = dynamic_cast<StructTypeRefNode*>(typeNode))
        return new StructTypeRefNode(structType->structName);

    if(auto* genStruct = dynamic_cast<GenericStructTypeRefNode*>(typeNode))
    {
        auto* copy = new GenericStructTypeRefNode(genStruct->structName);
        for(auto* arg : genStruct->typeArgs)
            copy->typeArgs.push_back(cloneTypeNode(arg));
        return copy;
    }

    return new TypeNode(typeNode->kind);
}

TypeNode* CodeGenerator::resolveTypeAliasNode(
    TypeNode* typeNode, const std::set<std::string>& scopeTypeParams,
    std::vector<std::string>& aliasStack)
{
    if(!typeNode)
        return nullptr;

    auto in_stack = [&](const std::string& name) -> bool
    {
        for(const auto& n : aliasStack)
            if(n == name)
                return true;
        return false;
    };
    auto find_alias = [&](const std::string& name)
    {
        auto it = typeAliases.find(name);
        if(it != typeAliases.end())
            return std::make_pair(name, it);

        size_t scopePos = name.rfind("::");
        if(scopePos != std::string::npos && scopePos + 2 < name.size())
        {
            std::string tail = name.substr(scopePos + 2);
            it = typeAliases.find(tail);
            if(it != typeAliases.end())
                return std::make_pair(tail, it);
        }

        return std::make_pair(std::string(), typeAliases.end());
    };

    if(auto* listType = dynamic_cast<GenericListTypeNode*>(typeNode))
    {
        listType->elementType = resolveTypeAliasNode(
            listType->elementType, scopeTypeParams, aliasStack);
        return listType;
    }

    if(auto* mapType = dynamic_cast<MapTypeNode*>(typeNode))
    {
        mapType->keyType =
            resolveTypeAliasNode(mapType->keyType, scopeTypeParams, aliasStack);
        mapType->valueType = resolveTypeAliasNode(mapType->valueType,
                                                  scopeTypeParams, aliasStack);
        return mapType;
    }

    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(typeNode))
    {
        if(tupleType->elementTypes)
        {
            for(size_t i = 0; i < tupleType->elementTypes->types.size(); ++i)
            {
                tupleType->elementTypes->types[i] =
                    resolveTypeAliasNode(tupleType->elementTypes->types[i],
                                         scopeTypeParams, aliasStack);
            }
        }
        return tupleType;
    }

    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(typeNode))
    {
        ptrType->elementType = resolveTypeAliasNode(
            ptrType->elementType, scopeTypeParams, aliasStack);
        return ptrType;
    }

    if(auto* refType = dynamic_cast<ReferenceTypeNode*>(typeNode))
    {
        refType->elementType = resolveTypeAliasNode(
            refType->elementType, scopeTypeParams, aliasStack);
        return refType;
    }

    if(auto* structType = dynamic_cast<StructTypeRefNode*>(typeNode))
    {
        if(scopeTypeParams.count(structType->structName))
            return typeNode;

        auto [aliasName, it] = find_alias(structType->structName);
        if(it == typeAliases.end())
            return typeNode;

        if(!it->second.typeParams.empty())
        {
            reportError(typeNode->line, "generic type alias '" +
                                            structType->structName +
                                            "' requires type arguments");
            return typeNode;
        }

        if(in_stack(aliasName))
        {
            reportError(typeNode->line, "cyclic type alias detected for '" +
                                            structType->structName + "'");
            return typeNode;
        }

        aliasStack.push_back(aliasName);
        TypeNode* expanded = cloneTypeNode(it->second.aliasedType);
        expanded = resolveTypeAliasNode(expanded, scopeTypeParams, aliasStack);
        aliasStack.pop_back();
        return expanded;
    }

    if(auto* genStruct = dynamic_cast<GenericStructTypeRefNode*>(typeNode))
    {
        for(size_t i = 0; i < genStruct->typeArgs.size(); ++i)
        {
            genStruct->typeArgs[i] = resolveTypeAliasNode(
                genStruct->typeArgs[i], scopeTypeParams, aliasStack);
        }

        if(scopeTypeParams.count(genStruct->structName))
            return typeNode;

        auto [aliasName, it] = find_alias(genStruct->structName);
        if(it == typeAliases.end())
            return typeNode;

        if(it->second.typeParams.empty())
        {
            reportError(typeNode->line, "type alias '" + genStruct->structName +
                                            "' does not take type arguments");
            return typeNode;
        }

        if(genStruct->typeArgs.size() != it->second.typeParams.size())
        {
            reportError(typeNode->line,
                        "type alias '" + genStruct->structName + "' expects " +
                            std::to_string(it->second.typeParams.size()) +
                            " type arguments, got " +
                            std::to_string(genStruct->typeArgs.size()));
            return typeNode;
        }

        if(!validateTypeArgumentTraitBounds(
               it->second.typeParams, it->second.typeParamTraitBounds,
               genStruct->typeArgs, scopeTypeParams, typeNode->line,
               "type alias", genStruct->structName))
        {
            return typeNode;
        }

        if(in_stack(aliasName))
        {
            reportError(typeNode->line, "cyclic type alias detected for '" +
                                            genStruct->structName + "'");
            return typeNode;
        }

        aliasStack.push_back(aliasName);
        TypeNode* aliasType = cloneTypeNode(it->second.aliasedType);
        TypeNode* expanded = substituteTypeParams(
            aliasType, it->second.typeParams, genStruct->typeArgs);
        expanded = resolveTypeAliasNode(expanded, scopeTypeParams, aliasStack);
        aliasStack.pop_back();
        return expanded;
    }

    return typeNode;
}

bool CodeGenerator::validateTypeArgumentTraitBounds(
    const std::vector<std::string>& typeParams,
    const std::map<std::string, std::string>& traitBounds,
    const std::vector<TypeNode*>& typeArgs,
    const std::set<std::string>& scopeTypeParams, int line,
    const std::string& ownerKind, const std::string& ownerName,
    bool reportFailures)
{
    if(typeParams.size() != typeArgs.size())
        return true;

    std::function<bool(TypeNode*)> containsScopedTypeParam =
        [&](TypeNode* type) -> bool
    {
        if(!type)
            return false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
            return scopeTypeParams.count(structRef->structName) != 0;
        if(auto* genericRef = dynamic_cast<GenericStructTypeRefNode*>(type))
        {
            for(auto* arg : genericRef->typeArgs)
            {
                if(containsScopedTypeParam(arg))
                    return true;
            }
            return false;
        }
        if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
            return containsScopedTypeParam(listType->elementType);
        if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
            return containsScopedTypeParam(mapType->keyType) ||
                   containsScopedTypeParam(mapType->valueType);
        if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
        {
            if(!tupleType->elementTypes)
                return false;
            for(auto* elem : tupleType->elementTypes->types)
            {
                if(containsScopedTypeParam(elem))
                    return true;
            }
            return false;
        }
        if(auto* ptrType = dynamic_cast<PointerTypeNode*>(type))
            return containsScopedTypeParam(ptrType->elementType);
        if(auto* refType = dynamic_cast<ReferenceTypeNode*>(type))
            return containsScopedTypeParam(refType->elementType);
        return false;
    };

    for(size_t i = 0; i < typeParams.size(); ++i)
    {
        auto boundIt = traitBounds.find(typeParams[i]);
        if(boundIt == traitBounds.end() || boundIt->second.empty())
            continue;

        TypeNode* typeArg = typeArgs[i];
        if(!typeArg || containsScopedTypeParam(typeArg))
            continue;

        std::vector<std::string> aliasStack;
        TypeNode* resolvedType = resolveTypeAliasNode(
            cloneTypeNode(typeArg), scopeTypeParams, aliasStack);
        if(!resolvedType || containsScopedTypeParam(resolvedType))
            continue;

        std::string concreteTypeName;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(resolvedType))
        {
            concreteTypeName = structRef->structName;
        }
        else if(auto* genericRef =
                    dynamic_cast<GenericStructTypeRefNode*>(resolvedType))
        {
            concreteTypeName = getOrCreateMonomorphizedStruct(
                genericRef->structName, genericRef->typeArgs);
        }

        // Bound list is stored as a `+`-joined string (e.g. "Foo+Bar") to
        // keep the storage type unchanged. Split and require every trait.
        std::vector<std::string> requiredTraits;
        {
            const std::string& joined = boundIt->second;
            size_t start = 0;
            while(start < joined.size())
            {
                size_t plus = joined.find('+', start);
                if(plus == std::string::npos)
                    plus = joined.size();
                std::string single = joined.substr(start, plus - start);
                if(!single.empty())
                    requiredTraits.push_back(single);
                start = plus + 1;
            }
        }

        for(const auto& requiredTrait : requiredTraits)
        {
            bool satisfiesBound = false;
            if(!concreteTypeName.empty())
            {
                auto traitIt = structImplementedTraits.find(concreteTypeName);
                satisfiesBound = traitIt != structImplementedTraits.end() &&
                                 traitIt->second.find(requiredTrait) !=
                                     traitIt->second.end();
            }

            if(!satisfiesBound)
            {
                if(reportFailures)
                {
                    const int errorLine =
                        line > 0 ? line
                                 : (typeArg->line > 0 ? typeArg->line : 0);
                    reportError(errorLine,
                                "type argument '" +
                                    type_name_for_error(typeArg) + "' for " +
                                    ownerKind + " '" + ownerName +
                                    "' must implement trait '" + requiredTrait +
                                    "' required by type parameter '" +
                                    typeParams[i] + "'");
                }
                return false;
            }
        }
    }

    return true;
}

void CodeGenerator::buildTypeAliasTable(ProgramNode* program)
{
    typeAliases.clear();
    if(!program || program->typeAliases.empty())
        return;

    std::set<std::string> knownTypeNames = {
        "void", "bool", "str8", "str16", "list", "map", "tuple", "ptr", "i8",
        "i16",  "i32",  "i64",  "u8",    "u16",  "u32", "u64",   "f32", "f64"};
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
            if(st)
            {
                knownTypeNames.insert(st->name);
                if(st->members)
                {
                    for(auto* nestedEnum : st->members->enums)
                        if(nestedEnum)
                            knownTypeNames.insert(nestedEnum->name);
                }
            }
    }
    if(program->enumList)
    {
        for(auto* en : program->enumList->enums)
            if(en)
                knownTypeNames.insert(en->name);
    }

    auto is_param_candidate = [](const std::string& name) -> bool
    {
        if(name.empty())
            return false;
        if(!(name[0] >= 'A' && name[0] <= 'Z'))
            return false;
        for(char c : name)
        {
            if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                return false;
        }
        return true;
    };

    std::function<void(TypeNode*, std::vector<std::string>&)> collect_params =
        [&](TypeNode* t, std::vector<std::string>& out)
    {
        if(!t)
            return;
        if(auto* sr = dynamic_cast<StructTypeRefNode*>(t))
        {
            if(knownTypeNames.count(sr->structName) == 0 &&
               is_param_candidate(sr->structName))
            {
                bool seen = false;
                for(const auto& p : out)
                {
                    if(p == sr->structName)
                    {
                        seen = true;
                        break;
                    }
                }
                if(!seen)
                    out.push_back(sr->structName);
            }
            return;
        }
        if(auto* gs = dynamic_cast<GenericStructTypeRefNode*>(t))
        {
            for(auto* arg : gs->typeArgs)
                collect_params(arg, out);
            return;
        }
        if(auto* l = dynamic_cast<GenericListTypeNode*>(t))
        {
            collect_params(l->elementType, out);
            return;
        }
        if(auto* m = dynamic_cast<MapTypeNode*>(t))
        {
            collect_params(m->keyType, out);
            collect_params(m->valueType, out);
            return;
        }
        if(auto* tup = dynamic_cast<TupleTypeNode*>(t))
        {
            if(tup->elementTypes)
            {
                for(auto* elem : tup->elementTypes->types)
                    collect_params(elem, out);
            }
            return;
        }
        if(auto* p = dynamic_cast<PointerTypeNode*>(t))
        {
            collect_params(p->elementType, out);
            return;
        }
        if(auto* r = dynamic_cast<ReferenceTypeNode*>(t))
        {
            collect_params(r->elementType, out);
            return;
        }
    };

    for(auto* aliasDef : program->typeAliases)
    {
        if(!aliasDef)
            continue;
        if(typeAliases.find(aliasDef->name) != typeAliases.end())
        {
            const auto& prev = typeAliases[aliasDef->name];
            std::string prevAt = prev.col > 0
                                     ? ("line " + std::to_string(prev.line) +
                                        ", column " + std::to_string(prev.col))
                                     : ("line " + std::to_string(prev.line));
            reportError(aliasDef->line, aliasDef->col,
                        "type alias '" + aliasDef->name +
                            "' overlaps with previous alias declaration at " +
                            prevAt);
            continue;
        }

        TypeAliasInfo info;
        info.aliasedType = aliasDef->aliasedType;
        info.line = aliasDef->line;
        info.col = aliasDef->col;
        info.typeParams = aliasDef->typeParams;
        info.typeParamTraitBounds = aliasDef->typeParamTraitBounds;

        if(info.typeParams.empty())
        {
            collect_params(aliasDef->aliasedType, info.typeParams);
        }

        typeAliases[aliasDef->name] = info;
        knownTypeNames.insert(aliasDef->name);
    }
}

void CodeGenerator::resolveTypeAliasesInProgram(ProgramNode* program)
{
    buildTypeAliasTable(program);
    if(!program)
        return;

    std::function<void(StatementNode*)> scan_stmt;
    std::function<void(StatementListNode*)> scan_stmt_list;
    scan_stmt_list = [&](StatementListNode* list)
    {
        if(!list)
            return;
        std::map<std::string, std::pair<int, int>> localAliases;
        for(auto* stmt : list->statements)
        {
            if(auto* aliasStmt = dynamic_cast<TypeAliasNode*>(stmt))
            {
                auto it = localAliases.find(aliasStmt->name);
                if(it != localAliases.end())
                {
                    const int prevLine = it->second.first;
                    const int prevCol = it->second.second;
                    std::string prevAt =
                        prevCol > 0 ? ("line " + std::to_string(prevLine) +
                                       ", column " + std::to_string(prevCol))
                                    : ("line " + std::to_string(prevLine));
                    reportError(
                        aliasStmt->line, aliasStmt->col,
                        "type alias '" + aliasStmt->name +
                            "' overlaps with previous alias declaration in "
                            "the same scope at " +
                            prevAt);
                }
                else
                {
                    localAliases[aliasStmt->name] =
                        std::make_pair(aliasStmt->line, aliasStmt->col);
                }
            }
            scan_stmt(stmt);
        }
    };
    scan_stmt = [&](StatementNode* s)
    {
        if(!s)
            return;
        if(auto* ifNode = dynamic_cast<IfNode*>(s))
        {
            scan_stmt(ifNode->conditionInit);
            scan_stmt_list(ifNode->thenBranch);
            scan_stmt(ifNode->elseIfBranch);
            scan_stmt_list(ifNode->elseBranch);
            return;
        }
        if(auto* cexprIf = dynamic_cast<CexprIfNode*>(s))
        {
            scan_stmt_list(cexprIf->thenBranch);
            scan_stmt(cexprIf->elseIfBranch);
            scan_stmt_list(cexprIf->elseBranch);
            return;
        }
        if(auto* forNode = dynamic_cast<ForNode*>(s))
        {
            scan_stmt_list(forNode->body);
            return;
        }
        if(auto* whileNode = dynamic_cast<WhileNode*>(s))
        {
            scan_stmt_list(whileNode->body);
            return;
        }
        if(auto* block = dynamic_cast<BlockStatementNode*>(s))
        {
            scan_stmt_list(block->statements);
            return;
        }
    };

    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(!st || !st->members)
                continue;
            for(auto* method : st->members->methods)
            {
                if(method)
                    scan_stmt_list(method->body);
            }
        }
    }
    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(fn)
                scan_stmt_list(fn->body);
        }
    }
    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl)
                continue;
            for(auto* method : impl->methods)
            {
                if(method)
                    scan_stmt_list(method->body);
            }
        }
    }

    auto make_scope = [](const std::vector<std::string>& params)
    {
        std::set<std::string> out;
        for(const auto& p : params)
            out.insert(p);
        return out;
    };

    std::function<void(ExpressionNode*, const std::set<std::string>&)>
        resolve_expr;
    std::function<void(StatementNode*, const std::set<std::string>&)>
        resolve_stmt;
    std::function<void(StatementListNode*, const std::set<std::string>&)>
        resolve_stmt_list;

    auto resolve_type = [&](TypeNode*& type, const std::set<std::string>& scope)
    {
        if(!type)
            return;
        std::vector<std::string> aliasStack;
        type = resolveTypeAliasNode(type, scope, aliasStack);
    };

    auto is_param_candidate = [](const std::string& name) -> bool
    {
        if(name.empty())
            return false;
        if(!(name[0] >= 'A' && name[0] <= 'Z'))
            return false;
        for(char c : name)
        {
            if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                return false;
        }
        return true;
    };

    std::function<void(TypeNode*, std::vector<std::string>&,
                       const std::set<std::string>&)>
        collect_alias_params = [&](TypeNode* t, std::vector<std::string>& out,
                                   const std::set<std::string>& scope)
    {
        if(!t)
            return;
        if(auto* sr = dynamic_cast<StructTypeRefNode*>(t))
        {
            if(scope.count(sr->structName) == 0 &&
               typeAliases.count(sr->structName) == 0 &&
               is_param_candidate(sr->structName))
            {
                bool seen = false;
                for(const auto& p : out)
                {
                    if(p == sr->structName)
                    {
                        seen = true;
                        break;
                    }
                }
                if(!seen)
                    out.push_back(sr->structName);
            }
            return;
        }
        if(auto* gs = dynamic_cast<GenericStructTypeRefNode*>(t))
        {
            for(auto* arg : gs->typeArgs)
                collect_alias_params(arg, out, scope);
            return;
        }
        if(auto* l = dynamic_cast<GenericListTypeNode*>(t))
        {
            collect_alias_params(l->elementType, out, scope);
            return;
        }
        if(auto* m = dynamic_cast<MapTypeNode*>(t))
        {
            collect_alias_params(m->keyType, out, scope);
            collect_alias_params(m->valueType, out, scope);
            return;
        }
        if(auto* tup = dynamic_cast<TupleTypeNode*>(t))
        {
            if(tup->elementTypes)
            {
                for(auto* elem : tup->elementTypes->types)
                    collect_alias_params(elem, out, scope);
            }
            return;
        }
        if(auto* p = dynamic_cast<PointerTypeNode*>(t))
        {
            collect_alias_params(p->elementType, out, scope);
            return;
        }
        if(auto* r = dynamic_cast<ReferenceTypeNode*>(t))
        {
            collect_alias_params(r->elementType, out, scope);
            return;
        }
    };

    resolve_expr = [&](ExpressionNode* e, const std::set<std::string>& scope)
    {
        if(!e)
            return;
        if(auto* bin = dynamic_cast<BinaryOpNode*>(e))
        {
            resolve_expr(bin->left, scope);
            resolve_expr(bin->right, scope);
            return;
        }
        if(auto* un = dynamic_cast<UnaryOpNode*>(e))
        {
            resolve_expr(un->operand, scope);
            return;
        }
        if(auto* tern = dynamic_cast<TernaryNode*>(e))
        {
            resolve_expr(tern->condition, scope);
            resolve_expr(tern->trueExpr, scope);
            resolve_expr(tern->falseExpr, scope);
            return;
        }
        if(auto* call = dynamic_cast<FunctionCallNode*>(e))
        {
            for(auto*& arg : call->arguments)
                resolve_expr(arg, scope);
            return;
        }
        if(auto* mc = dynamic_cast<MethodCallNode*>(e))
        {
            resolve_expr(mc->object, scope);
            for(auto*& arg : mc->arguments)
                resolve_expr(arg, scope);
            return;
        }
        if(auto* fa = dynamic_cast<FieldAccessNode*>(e))
        {
            resolve_expr(fa->object, scope);
            return;
        }
        if(auto* cast = dynamic_cast<CastExpressionNode*>(e))
        {
            resolve_expr(cast->expression, scope);
            return;
        }
        if(auto* fmt = dynamic_cast<FormatNode*>(e))
        {
            for(auto*& arg : fmt->arguments)
                resolve_expr(arg, scope);
            for(auto& namedArg : fmt->namedArguments)
                resolve_expr(namedArg.second, scope);
            return;
        }
        if(auto* listLit = dynamic_cast<ListLiteralNode*>(e))
        {
            if(listLit->elements)
            {
                for(auto*& elem : listLit->elements->elements)
                    resolve_expr(elem, scope);
            }
            return;
        }
        if(auto* mapLit = dynamic_cast<MapLiteralNode*>(e))
        {
            if(mapLit->entries)
            {
                for(auto* entry : mapLit->entries->entries)
                {
                    if(!entry)
                        continue;
                    resolve_expr(entry->key, scope);
                    resolve_expr(entry->value, scope);
                }
            }
            return;
        }
        if(auto* range = dynamic_cast<RangeExpressionNode*>(e))
        {
            resolve_expr(range->start, scope);
            resolve_expr(range->end, scope);
            return;
        }
        if(auto* fill = dynamic_cast<ArrayFillNode*>(e))
        {
            resolve_expr(fill->value, scope);
            resolve_expr(fill->count, scope);
            return;
        }
        if(auto* index = dynamic_cast<IndexExpressionNode*>(e))
        {
            resolve_expr(index->base, scope);
            resolve_expr(index->index, scope);
            return;
        }
        if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(e))
        {
            if(tupleLit->elements)
            {
                for(auto*& elem : tupleLit->elements->elements)
                    resolve_expr(elem, scope);
            }
            return;
        }
        if(auto* tupleAcc = dynamic_cast<TupleAccessNode*>(e))
        {
            resolve_expr(tupleAcc->tuple, scope);
            return;
        }
        if(auto* mapIter = dynamic_cast<MapIteratorNode*>(e))
        {
            resolve_expr(mapIter->mapExpr, scope);
            return;
        }
        if(auto* structLit = dynamic_cast<StructLiteralNode*>(e))
        {
            for(auto& field : structLit->fields)
                resolve_expr(field.second, scope);
            return;
        }
        if(auto* closure = dynamic_cast<ClosureNode*>(e))
        {
            resolve_stmt_list(closure->body, scope);
            return;
        }
        if(auto* matchExpr = dynamic_cast<MatchExpressionNode*>(e))
        {
            resolve_expr(matchExpr->target, scope);
            for(auto* arm : matchExpr->arms)
            {
                if(!arm)
                    continue;
                if(arm->pattern && arm->pattern->literal)
                    resolve_expr(arm->pattern->literal, scope);
                resolve_expr(arm->expression, scope);
            }
            return;
        }
        if(auto* tryExpr = dynamic_cast<TryExpressionNode*>(e))
        {
            resolve_expr(tryExpr->expression, scope);
            return;
        }
    };

    resolve_stmt = [&](StatementNode* s, const std::set<std::string>& scope)
    {
        if(!s)
            return;
        if(auto* letDecl = dynamic_cast<LetDeclNode*>(s))
        {
            resolve_type(letDecl->type, scope);
            resolve_expr(letDecl->expression, scope);
            return;
        }
        if(auto* varDecl = dynamic_cast<VarDeclNode*>(s))
        {
            resolve_type(varDecl->type, scope);
            resolve_expr(varDecl->initExpr, scope);
            return;
        }
        if(auto* exprStmt = dynamic_cast<ExpressionStatementNode*>(s))
        {
            resolve_expr(exprStmt->expression, scope);
            return;
        }
        if(auto* ret = dynamic_cast<ReturnNode*>(s))
        {
            resolve_expr(ret->expression, scope);
            return;
        }
        if(auto* throwNode = dynamic_cast<ThrowNode*>(s))
        {
            resolve_expr(throwNode->expression, scope);
            return;
        }
        if(auto* assign = dynamic_cast<AssignmentNode*>(s))
        {
            resolve_expr(assign->expression, scope);
            return;
        }
        if(auto* fa = dynamic_cast<FieldAssignmentNode*>(s))
        {
            resolve_expr(fa->target, scope);
            resolve_expr(fa->expression, scope);
            return;
        }
        if(auto* da = dynamic_cast<DerefAssignmentNode*>(s))
        {
            resolve_expr(da->pointerExpr, scope);
            resolve_expr(da->value, scope);
            return;
        }
        if(auto* ifNode = dynamic_cast<IfNode*>(s))
        {
            resolve_stmt(ifNode->conditionInit, scope);
            resolve_expr(ifNode->condition, scope);
            resolve_stmt_list(ifNode->thenBranch, scope);
            resolve_stmt(ifNode->elseIfBranch, scope);
            resolve_stmt_list(ifNode->elseBranch, scope);
            return;
        }
        if(auto* cexprIf = dynamic_cast<CexprIfNode*>(s))
        {
            resolve_expr(cexprIf->condition, scope);
            resolve_stmt_list(cexprIf->thenBranch, scope);
            resolve_stmt(cexprIf->elseIfBranch, scope);
            resolve_stmt_list(cexprIf->elseBranch, scope);
            return;
        }
        if(auto* forNode = dynamic_cast<ForNode*>(s))
        {
            resolve_expr(forNode->iterable, scope);
            resolve_stmt_list(forNode->body, scope);
            return;
        }
        if(auto* whileNode = dynamic_cast<WhileNode*>(s))
        {
            resolve_expr(whileNode->condition, scope);
            resolve_stmt_list(whileNode->body, scope);
            return;
        }
        if(auto* tryCatch = dynamic_cast<TryCatchNode*>(s))
        {
            resolve_type(tryCatch->catchType, scope);
            if(tryCatch->tryBlock)
                resolve_stmt_list(tryCatch->tryBlock->statements, scope);
            if(tryCatch->catchBlock)
                resolve_stmt_list(tryCatch->catchBlock->statements, scope);
            return;
        }
        if(auto* printNode = dynamic_cast<PrintNode*>(s))
        {
            for(auto*& arg : printNode->arguments)
                resolve_expr(arg, scope);
            for(auto& namedArg : printNode->namedArguments)
                resolve_expr(namedArg.second, scope);
            return;
        }
        if(auto* assertStmt = dynamic_cast<AssertNode*>(s))
        {
            resolve_expr(assertStmt->condition, scope);
            return;
        }
        if(auto* assertEq = dynamic_cast<AssertEqNode*>(s))
        {
            resolve_expr(assertEq->left, scope);
            resolve_expr(assertEq->right, scope);
            return;
        }
        if(auto* staticAssertStmt = dynamic_cast<StaticAssertNode*>(s))
        {
            resolve_expr(staticAssertStmt->condition, scope);
            return;
        }
        if(auto* block = dynamic_cast<BlockStatementNode*>(s))
        {
            resolve_stmt_list(block->statements, scope);
            return;
        }
    };

    resolve_stmt_list =
        [&](StatementListNode* list, const std::set<std::string>& scope)
    {
        if(!list)
            return;

        struct AliasRestore
        {
            std::string name;
            bool hadPrevious = false;
            TypeAliasInfo previous;
        };
        std::vector<AliasRestore> restores;

        for(auto* stmt : list->statements)
        {
            if(auto* aliasStmt = dynamic_cast<TypeAliasNode*>(stmt))
            {
                TypeAliasInfo info;
                info.aliasedType = aliasStmt->aliasedType;
                info.line = aliasStmt->line;
                info.col = aliasStmt->col;
                info.typeParams = aliasStmt->typeParams;
                if(info.typeParams.empty())
                    collect_alias_params(aliasStmt->aliasedType,
                                         info.typeParams, scope);

                std::set<std::string> aliasScope = scope;
                for(const auto& p : info.typeParams)
                    aliasScope.insert(p);
                std::vector<std::string> aliasStack;
                info.aliasedType = resolveTypeAliasNode(info.aliasedType,
                                                        aliasScope, aliasStack);

                AliasRestore restore;
                restore.name = aliasStmt->name;
                auto prev = typeAliases.find(aliasStmt->name);
                if(prev != typeAliases.end())
                {
                    restore.hadPrevious = true;
                    restore.previous = prev->second;
                }
                restores.push_back(restore);
                typeAliases[aliasStmt->name] = info;
                continue;
            }

            resolve_stmt(stmt, scope);
        }

        for(size_t i = restores.size(); i > 0; --i)
        {
            const auto& r = restores[i - 1];
            if(r.hadPrevious)
                typeAliases[r.name] = r.previous;
            else
                typeAliases.erase(r.name);
        }
    };

    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(!st)
                continue;
            auto scope = make_scope(st->typeParams);
            if(st->members)
            {
                for(auto* member : st->members->members)
                {
                    if(!member)
                        continue;
                    resolve_type(member->type, scope);
                    resolve_expr(member->initExpr, scope);
                }
                for(auto* method : st->members->methods)
                {
                    if(!method)
                        continue;
                    resolve_type(method->returnType, scope);
                    if(method->parameters)
                    {
                        for(auto* p : method->parameters->parameters)
                            if(p)
                                resolve_type(p->type, scope);
                    }
                    resolve_stmt_list(method->body, scope);
                }
            }
        }
    }

    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(!fn)
                continue;
            std::set<std::string> scope(fn->typeParams.begin(),
                                        fn->typeParams.end());
            resolve_type(fn->returnType, scope);
            if(fn->parameters)
            {
                for(auto* p : fn->parameters->parameters)
                    if(p)
                        resolve_type(p->type, scope);
            }
            resolve_stmt_list(fn->body, scope);
        }
    }

    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl)
                continue;
            auto scope = make_scope(impl->typeParams);
            for(auto* method : impl->methods)
            {
                if(!method)
                    continue;
                resolve_type(method->returnType, scope);
                if(method->parameters)
                {
                    for(auto* p : method->parameters->parameters)
                        if(p)
                            resolve_type(p->type, scope);
                }
                resolve_stmt_list(method->body, scope);
            }
        }
    }

    if(!program->globalVars.empty())
    {
        const std::set<std::string> emptyScope;
        for(auto* gv : program->globalVars)
        {
            if(!gv)
                continue;
            resolve_type(gv->type, emptyScope);
            resolve_expr(gv->initExpr, emptyScope);
        }
    }
}
