#include "ir.h"

#include <algorithm>
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

        auto structHasCleanup = [&](const std::string& structName) {
            static const std::vector<std::string> cleanupNames = {
                "__drop", "drop", "free", "destroy"};
            auto methods = structMethods.find(structName);
            if(methods != structMethods.end())
                for(const auto& cleanupName : cleanupNames)
                {
                    auto method = methods->second.find(cleanupName);
                    if(method == methods->second.end() || !method->second.second)
                        continue;
                    StructMethodNode* node = method->second.second;
                    if(node->isStatic)
                        continue;
                    size_t nonSelfParams = 0;
                    if(node->parameters)
                        for(auto* parameter : node->parameters->parameters)
                            if(parameter && parameter->name != "self")
                                ++nonSelfParams;
                    if(nonSelfParams == 0)
                        return true;
                }

            llvm::StructType* llvmStruct = getStructType(structName);
            if(!llvmStruct)
                return false;
            for(const auto& cleanupName : cleanupNames)
            {
                auto overloads = functionOverloads.find(cleanupName);
                if(overloads == functionOverloads.end())
                    continue;
                for(const auto& overload : overloads->second)
                {
                    llvm::Function* function = overload.function;
                    if(function && function->arg_size() == 1 &&
                       function->getReturnType()->isVoidTy() &&
                       function->getArg(0)->getType() == llvmStruct)
                        return true;
                }
            }
            return false;
        };

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

            if(structHasCleanup(structName))
                return OwnershipClass::MoveOnly;

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
    for(auto moved = movedVariables.begin(); moved != movedVariables.end();)
    {
        const bool isDescendant =
            moved->size() > name.size() &&
            moved->compare(0, name.size(), name) == 0 &&
            (moved->at(name.size()) == '.' || moved->at(name.size()) == '[');
        const bool isUnresolvedIndexMove =
            *moved == name && moved->find("[*]") != std::string::npos;
        if((*moved == name && !isUnresolvedIndexMove) || isDescendant)
            moved = movedVariables.erase(moved);
        else
            ++moved;
    }
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
    // Keep the legacy root-owner entry populated while any field borrow of
    // that owner remains active, so whole-object checks stay conservative.
    for(const auto& borrow : pointerBorrowTarget)
    {
        if(borrow.first == pointerVar ||
           activeMutBorrower.find(borrow.second) != activeMutBorrower.end())
            continue;
        auto candidate = std::find_if(
            activeMutBorrower.begin(), activeMutBorrower.end(),
            [&](const auto& active) {
                auto owner = pointerBorrowTarget.find(active.second);
                return owner != pointerBorrowTarget.end() &&
                       owner->second == borrow.second;
            });
        if(candidate != activeMutBorrower.end())
        {
            activeMutBorrower[borrow.second] = candidate->second;
            if(candidate->first != borrow.second)
                activeMutBorrower.erase(candidate);
        }
    }
    pointerBorrowTarget.erase(pointerVar);
    pointerBorrowPlace.erase(pointerVar);
    pointerBorrowParent.erase(pointerVar);
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
        auto namedValue = namedValues.find(varName);
        if(namedValue != namedValues.end())
        {
            entry.hadPreviousNamedValue = true;
            entry.previousNamedValue = namedValue->second;
        }
        auto valueType = variableTypes.find(varName);
        if(valueType != variableTypes.end())
        {
            entry.hadPreviousVariableType = true;
            entry.previousVariableType = valueType->second;
        }
        auto structType = structVariableTypes.find(varName);
        if(structType != structVariableTypes.end())
        {
            entry.hadPreviousStructVariableType = true;
            entry.previousStructVariableType = structType->second;
        }
        entry.previousWasConstant = constantVariables.count(varName) != 0;
        entry.previousWasImmutableReference =
            immutableReferenceVariables.count(varName) != 0;
        auto capture = [&](auto& mapping, auto& previous) {
            auto value = mapping.find(varName);
            if(value != mapping.end())
            {
                previous.existed = true;
                previous.value = value->second;
            }
        };
        capture(listElementTypes, entry.previousListElementType);
        capture(arrayCapacities, entry.previousArrayCapacity);
        capture(arrayKnownLengths, entry.previousArrayKnownLength);
        capture(multiarrayMutability, entry.previousMultiarrayMutability);
        capture(mapKeyValueTypes, entry.previousMapKeyValueTypes);
        capture(pointerElementTypes, entry.previousPointerElementType);
        capture(pointerKnownNull, entry.previousPointerKnownNull);
        capture(tupleElementTypes, entry.previousTupleElementTypes);
        capture(enumVariableTypes, entry.previousEnumVariableType);
        capture(traitObjectVariableTypes,
                entry.previousTraitObjectVariableType);
        capture(functionTypeVariables, entry.previousFunctionTypeVariable);
        capture(closureVariables, entry.previousClosureVariable);
        capture(runtimeClosureVariables,
                entry.previousRuntimeClosureVariable);
        capture(runtimeClosureReturnTypes,
                entry.previousRuntimeClosureReturnType);
        capture(runtimeClosureBorrowOwners,
                entry.previousRuntimeClosureBorrowOwners);
        auto belongsToVariable = [&](const std::string& place) {
            return place == varName ||
                   (place.size() > varName.size() &&
                    place.compare(0, varName.size(), varName) == 0 &&
                    (place[varName.size()] == '.' ||
                     place[varName.size()] == '['));
        };
        for(const auto& borrowers : activeBorrowers)
            if(belongsToVariable(borrowers.first))
                entry.previousActiveBorrowers.insert(borrowers);
        for(const auto& borrower : activeMutBorrower)
            if(belongsToVariable(borrower.first))
                entry.previousActiveMutBorrowers.insert(borrower);
        for(auto borrowers = activeBorrowers.begin();
            borrowers != activeBorrowers.end();)
        {
            if(belongsToVariable(borrowers->first))
                borrowers = activeBorrowers.erase(borrowers);
            else
                ++borrowers;
        }
        for(auto borrower = activeMutBorrower.begin();
            borrower != activeMutBorrower.end();)
        {
            if(belongsToVariable(borrower->first))
                borrower = activeMutBorrower.erase(borrower);
            else
                ++borrower;
        }
    }
    for(const auto& moved : movedVariables)
    {
        if(moved == varName ||
           (moved.size() > varName.size() &&
            moved.compare(0, varName.size(), varName) == 0 &&
            (moved[varName.size()] == '.' ||
             moved[varName.size()] == '[')))
            entry.previousMovedValues.insert(moved);
    }
    auto aggregate = aggregateBorrowOrigins.find(varName);
    if(aggregate != aggregateBorrowOrigins.end())
    {
        entry.hadPreviousAggregateBorrowOrigins = true;
        entry.previousAggregateBorrowOrigins = aggregate->second;
    }
    auto places = aggregateBorrowPlaces.find(varName);
    if(places != aggregateBorrowPlaces.end())
    {
        entry.hadPreviousAggregateBorrowPlaces = true;
        entry.previousAggregateBorrowPlaces = places->second;
    }
    auto fieldPlaces = aggregateBorrowFieldPlaces.find(varName);
    if(fieldPlaces != aggregateBorrowFieldPlaces.end())
    {
        entry.hadPreviousAggregateBorrowFieldPlaces = true;
        entry.previousAggregateBorrowFieldPlaces = fieldPlaces->second;
    }
    auto place = pointerBorrowPlace.find(varName);
    if(place != pointerBorrowPlace.end())
    {
        entry.hadPreviousAggregateBorrowPlace = true;
        entry.previousAggregateBorrowPlace = place->second;
    }
    auto functionItem = functionItemVariables.find(varName);
    if(functionItem != functionItemVariables.end())
    {
        entry.hadPreviousFunctionItemTarget = true;
        entry.previousFunctionItemTargets = functionItem->second;
    }
    variableScopeDepth[varName] = currentScopeDepth();
    immutableReferenceVariables.erase(varName);
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
        auto parent = pointerBorrowParent.find(pointerVar);
        if(parent != pointerBorrowParent.end())
            entry.previousParent = parent->second;
        auto place = pointerBorrowPlace.find(pointerVar);
        if(place != pointerBorrowPlace.end())
            entry.previousPlace = place->second;
        for(const auto& borrow : activeMutBorrower)
        {
            if(borrow.second == pointerVar)
            {
                entry.previousWasMutable = true;
                entry.previousBorrowKey = borrow.first;
                break;
            }
        }

        entry.shadowAlias = pointerVar + " (outer shadow " +
                            std::to_string(currentScopeDepth()) + "." +
                            std::to_string(pointerBorrowScopes.back().size()) +
                            ")";
        pointerBorrowTarget[entry.shadowAlias] = entry.previousOwner;
        if(!entry.previousParent.empty())
            pointerBorrowParent[entry.shadowAlias] = entry.previousParent;
        if(!entry.previousPlace.empty())
            pointerBorrowPlace[entry.shadowAlias] = entry.previousPlace;

        for(auto& borrowers : activeBorrowers)
            if(borrowers.second.erase(pointerVar))
                borrowers.second.insert(entry.shadowAlias);
        for(auto& borrower : activeMutBorrower)
            if(borrower.second == pointerVar)
                borrower.second = entry.shadowAlias;
        for(auto& parent : pointerBorrowParent)
            if(parent.first != entry.shadowAlias &&
               parent.second == pointerVar)
                parent.second = entry.shadowAlias;

        pointerBorrowTarget.erase(pointerVar);
        pointerBorrowParent.erase(pointerVar);
        pointerBorrowPlace.erase(pointerVar);
    }
    pointerBorrowScopes.back().push_back(std::move(entry));
}
