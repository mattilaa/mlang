#include "ir.h"

#include <functional>
#include <set>

bool CodeGenerator::isCopyType(TypeNode* typeNode)
{
    return classifyOwnership(typeNode) == OwnershipClass::Copy;
}

CodeGenerator::OwnershipClass
CodeGenerator::classifyOwnership(TypeNode* typeNode)
{
    std::set<std::string> visitingStructs;
    std::function<OwnershipClass(TypeNode*)> classify =
        [&](TypeNode* t) -> OwnershipClass
    {
        if(!t)
            return OwnershipClass::MoveOnly;

        if(auto* tupleType = dynamic_cast<TupleTypeNode*>(t))
        {
            if(!tupleType->elementTypes)
                return OwnershipClass::MoveOnly;
            for(auto* elem : tupleType->elementTypes->types)
            {
                if(classify(elem) != OwnershipClass::Copy)
                    return OwnershipClass::MoveOnly;
            }
            return OwnershipClass::Copy;
        }

        if(dynamic_cast<PointerTypeNode*>(t))
            return OwnershipClass::Copy;

        auto classifyStructByName =
            [&](const std::string& structName) -> OwnershipClass
        {
            if(!resolveVisibleEnumName(structName).empty())
                return OwnershipClass::Copy;

            auto membersIt = structMembers.find(structName);
            if(membersIt == structMembers.end())
            {
                // Unknown/user-generic type parameter: conservative default.
                return OwnershipClass::MoveOnly;
            }

            if(visitingStructs.count(structName))
            {
                // Recursive type cycle: conservative default.
                return OwnershipClass::MoveOnly;
            }

            visitingStructs.insert(structName);
            for(const auto& member : membersIt->second)
            {
                if(classify(member.second) != OwnershipClass::Copy)
                {
                    visitingStructs.erase(structName);
                    return OwnershipClass::MoveOnly;
                }
            }
            visitingStructs.erase(structName);
            // Structs are Copy when all stored fields are Copy. This includes
            // user-defined value types and synthesized property lock slots.
            return OwnershipClass::Copy;
        };

        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(t))
            return classifyStructByName(structRef->structName);
        if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(t))
        {
            std::string mangledName = getOrCreateMonomorphizedStruct(
                genStructRef->structName, genStructRef->typeArgs);
            return classifyStructByName(mangledName);
        }

        switch(t->kind)
        {
        case TypeNode::TYPE_BOOL:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_FLOAT:
        case TypeNode::TYPE_DOUBLE:
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_I32:
        case TypeNode::TYPE_I64:
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
        case TypeNode::TYPE_U64:
        case TypeNode::TYPE_PTR:
        // string/str8/str16 are char* - pointer-sized, implicitly shared.
        // Mutation safety is enforced via activeBorrowers, not move tracking.
        case TypeNode::TYPE_STRING:
        case TypeNode::TYPE_STR8:
        case TypeNode::TYPE_STR16:
            return OwnershipClass::Copy;
        case TypeNode::TYPE_VOID:
        case TypeNode::TYPE_LIST:
        case TypeNode::TYPE_MAP:
        case TypeNode::TYPE_STRUCT:
        case TypeNode::TYPE_TUPLE:
        default:
            return OwnershipClass::MoveOnly;
        }
    };
    return classify(typeNode);
}

std::string CodeGenerator::ownershipClassName(TypeNode* typeNode)
{
    return classifyOwnership(typeNode) == OwnershipClass::Copy ? "Copy"
                                                               : "MoveOnly";
}

bool CodeGenerator::isMoveOnlyVariable(const std::string& name)
{
    auto typeIt = variableTypes.find(name);
    if(typeIt == variableTypes.end())
        return false;

    TypeNode::TypeKind kind = typeIt->second;
    if(kind == TypeNode::TYPE_STRUCT)
    {
        auto sit = structVariableTypes.find(name);
        if(sit == structVariableTypes.end())
            return true;
        StructTypeRefNode t(sit->second);
        return !isCopyType(&t);
    }
    if(kind == TypeNode::TYPE_TUPLE)
    {
        auto tit = tupleElementTypes.find(name);
        if(tit == tupleElementTypes.end())
            return true;
        TypeListNode typeList;
        for(auto* t : tit->second)
            typeList.addType(t);
        TupleTypeNode tupleType(&typeList);
        return !isCopyType(&tupleType);
    }
    if(kind == TypeNode::TYPE_LIST)
    {
        auto lit = listElementTypes.find(name);
        if(lit != listElementTypes.end())
        {
            GenericListTypeNode listType(lit->second);
            return !isCopyType(&listType);
        }
    }
    if(kind == TypeNode::TYPE_MAP)
    {
        auto mit = mapKeyValueTypes.find(name);
        if(mit != mapKeyValueTypes.end())
        {
            MapTypeNode mapType(mit->second.first, mit->second.second);
            return !isCopyType(&mapType);
        }
    }
    if(kind == TypeNode::TYPE_PTR)
    {
        auto pit = pointerElementTypes.find(name);
        if(pit != pointerElementTypes.end())
        {
            PointerTypeNode ptrType(pit->second);
            return !isCopyType(&ptrType);
        }
    }

    TypeNode scalarType(kind);
    return !isCopyType(&scalarType);
}

bool CodeGenerator::isVariableMoved(const std::string& name) const
{
    return movedVariables.find(name) != movedVariables.end();
}

bool CodeGenerator::isVariableCurrentlyVisible(const std::string& name) const
{
    if(globalNamedValues.find(name) != globalNamedValues.end())
        return true;

    auto depthIt = variableScopeDepth.find(name);
    if(depthIt == variableScopeDepth.end())
        return false;
    return depthIt->second <= currentScopeDepth();
}

bool CodeGenerator::validateVariableAccessible(const std::string& name,
                                               int line, int col)
{
    if(globalNamedValues.find(name) == globalNamedValues.end() &&
       isVariableMoved(name))
    {
        reportError(line, col, "use of moved value: '" + name + "'");
        return false;
    }

    if(!isVariableCurrentlyVisible(name))
    {
        reportError(line, col, "unknown variable: '" + name + "'");
        return false;
    }

    auto valueIt = namedValues.find(name);
    if(valueIt == namedValues.end() || !valueIt->second)
    {
        reportError(line, col, "unknown variable: '" + name + "'");
        return false;
    }
    return true;
}

void CodeGenerator::clearMovedVariable(const std::string& name)
{
    movedVariables.erase(name);
}

void CodeGenerator::clearPointerBorrow(const std::string& pointerVar)
{
    for(auto it = activeBorrowers.begin(); it != activeBorrowers.end();)
    {
        it->second.erase(pointerVar);
        if(it->second.empty())
            it = activeBorrowers.erase(it);
        else
            ++it;
    }
    // Also clear any exclusive mutable borrow this variable held.
    for(auto it = activeMutBorrower.begin(); it != activeMutBorrower.end();)
    {
        if(it->second == pointerVar)
            it = activeMutBorrower.erase(it);
        else
            ++it;
    }
    pointerBorrowTarget.erase(pointerVar);
}

bool CodeGenerator::isMutBorrower(const std::string& ptrVar) const
{
    for(const auto& kv : activeMutBorrower)
        if(kv.second == ptrVar)
            return true;
    return false;
}

int CodeGenerator::currentScopeDepth() const
{
    return static_cast<int>(cleanupScopes.size());
}

void CodeGenerator::recordVariableScopeDepth(const std::string& varName)
{
    if(variableScopeDepthScopes.empty())
        return;

    VariableScopeDepthEntry entry;
    entry.varName = varName;
    auto it = variableScopeDepth.find(varName);
    if(it != variableScopeDepth.end())
    {
        entry.hadPreviousDepth = true;
        entry.previousDepth = it->second;
    }
    variableScopeDepth[varName] = currentScopeDepth();
    variableScopeDepthScopes.back().push_back(std::move(entry));
}

void CodeGenerator::recordScopedPointerVariable(const std::string& pointerVar)
{
    if(pointerBorrowScopes.empty())
        return;

    PointerBorrowScopeEntry entry;
    entry.pointerVar = pointerVar;
    auto it = pointerBorrowTarget.find(pointerVar);
    if(it != pointerBorrowTarget.end())
    {
        entry.hadPreviousBorrow = true;
        entry.previousOwner = it->second;
    }
    pointerBorrowScopes.back().push_back(std::move(entry));
}
