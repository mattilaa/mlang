#include "ir.h"
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#if LLVM_VERSION_MAJOR >= 18
#include <llvm/TargetParser/Triple.h>
#else
#include <llvm/ADT/Triple.h>
#endif
#include <llvm/Bitcode/BitcodeWriter.h>

llvm::Type* CodeGenerator::getLLVMType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_VOID:
        return llvm::Type::getVoidTy(context);
    case TypeNode::TYPE_BOOL:
        return llvm::Type::getInt1Ty(context);
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I32:
        return llvm::Type::getInt32Ty(context);
    case TypeNode::TYPE_FLOAT:
        return llvm::Type::getFloatTy(context);
    case TypeNode::TYPE_DOUBLE:
        return llvm::Type::getDoubleTy(context);
    case TypeNode::TYPE_STRING:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    case TypeNode::TYPE_STR8:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    case TypeNode::TYPE_STR16:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt16Ty(context), 0);
#endif
    case TypeNode::TYPE_I8:
        return llvm::Type::getInt8Ty(context);
    case TypeNode::TYPE_I16:
        return llvm::Type::getInt16Ty(context);
    case TypeNode::TYPE_I64:
        return llvm::Type::getInt64Ty(context);
    case TypeNode::TYPE_U8:
        return llvm::Type::getInt8Ty(context);
    case TypeNode::TYPE_U16:
        return llvm::Type::getInt16Ty(context);
    case TypeNode::TYPE_U32:
        return llvm::Type::getInt32Ty(context);
    case TypeNode::TYPE_U64:
        return llvm::Type::getInt64Ty(context);
    case TypeNode::TYPE_PTR:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    default:
        return nullptr;
    }
}

bool CodeGenerator::isUnsignedType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
        return true;
    default:
        return false;
    }
}

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

        auto classifyStructByName = [&](const std::string& structName)
            -> OwnershipClass
        {
            auto enumIt = enumValues.find(structName);
            if(enumIt != enumValues.end())
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
            return OwnershipClass::Copy;
        case TypeNode::TYPE_VOID:
        case TypeNode::TYPE_STRING:
        case TypeNode::TYPE_STR8:
        case TypeNode::TYPE_STR16:
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
    pointerBorrowTarget.erase(pointerVar);
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

void CodeGenerator::registerPointerBorrow(const std::string& pointerVar,
                                          ExpressionNode* expr, int line)
{
    clearPointerBorrow(pointerVar);

    auto registerOwnerBorrow = [&](const std::string& ownerName,
                                   bool enforceExclusive) -> bool
    {
        if(globalNamedValues.find(ownerName) == globalNamedValues.end() &&
           isVariableMoved(ownerName))
        {
            reportError(line,
                        "cannot borrow moved value: '" + ownerName + "'");
            return false;
        }

        auto pointerDepthIt = variableScopeDepth.find(pointerVar);
        auto ownerDepthIt = variableScopeDepth.find(ownerName);
        if(pointerDepthIt != variableScopeDepth.end() &&
           ownerDepthIt != variableScopeDepth.end() &&
           ownerDepthIt->second > pointerDepthIt->second)
        {
            reportError(line, "cannot borrow '" + ownerName +
                                  "' into longer-lived pointer '" +
                                  pointerVar + "'");
            return false;
        }

        auto activeIt = activeBorrowers.find(ownerName);
        if(enforceExclusive && activeIt != activeBorrowers.end())
        {
            for(const auto& borrower : activeIt->second)
            {
                if(borrower != pointerVar)
                {
                    reportError(line, "cannot borrow '" + ownerName +
                                          "' while already borrowed by '" +
                                          borrower + "'");
                    return false;
                }
            }
        }

        pointerBorrowTarget[pointerVar] = ownerName;
        activeBorrowers[ownerName].insert(pointerVar);
        return true;
    };

    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto borrowIt = pointerBorrowTarget.find(idExpr->name);
        if(borrowIt != pointerBorrowTarget.end())
        {
            if(idExpr->name != pointerVar)
            {
                reportError(line, "cannot alias exclusive borrow from '" +
                                      idExpr->name + "' into '" + pointerVar +
                                      "'");
                return;
            }
            (void)registerOwnerBorrow(borrowIt->second, true);
            return;
        }
    }

    auto* unary = dynamic_cast<UnaryOpNode*>(expr);
    if(!unary || unary->op != UnaryOpNode::OP_ADDR)
        return;

    std::string ownerName = resolveBorrowOwnerFromLValue(unary->operand);
    if(ownerName.empty())
        return;

    bool enforceExclusive = true;
    auto pit = pointerElementTypes.find(pointerVar);
    TypeNode* ptrElemType = pit != pointerElementTypes.end() ? pit->second : nullptr;
    if(ptrElemType)
    {
        TypeNode::TypeKind ownerKind = TypeNode::TYPE_VOID;
        bool hasOwnerKind = false;
        auto vit = variableTypes.find(ownerName);
        if(vit != variableTypes.end())
        {
            ownerKind = vit->second;
            hasOwnerKind = true;
        }
        else
        {
            auto gvit = globalVariableTypes.find(ownerName);
            if(gvit != globalVariableTypes.end())
            {
                ownerKind = gvit->second;
                hasOwnerKind = true;
            }
        }

        if(hasOwnerKind)
        {
            bool sameOwnerType = false;
            if(ownerKind == TypeNode::TYPE_STRUCT &&
               ptrElemType->kind == TypeNode::TYPE_STRUCT)
            {
                std::string ownerStructName;
                auto sit = structVariableTypes.find(ownerName);
                if(sit != structVariableTypes.end())
                    ownerStructName = sit->second;
                else
                {
                    auto gsit = globalStructVariableTypes.find(ownerName);
                    if(gsit != globalStructVariableTypes.end())
                        ownerStructName = gsit->second;
                }

                if(auto* sr = dynamic_cast<StructTypeRefNode*>(ptrElemType))
                    sameOwnerType = (sr->structName == ownerStructName);
                else if(auto* gsr =
                            dynamic_cast<GenericStructTypeRefNode*>(ptrElemType))
                    sameOwnerType =
                        (getOrCreateMonomorphizedStruct(gsr->structName,
                                                        gsr->typeArgs) ==
                         ownerStructName);
                // Be conservative when owner struct metadata is unavailable.
                if(ownerStructName.empty())
                    sameOwnerType = true;
            }
            else
            {
                sameOwnerType = (ownerKind == ptrElemType->kind);
            }

            enforceExclusive = sameOwnerType;
        }
    }

    (void)registerOwnerBorrow(ownerName, enforceExclusive);
}

std::string CodeGenerator::resolveBorrowOwnerFromLValue(ExpressionNode* expr) const
{
    std::function<std::string(ExpressionNode*)> resolve =
        [&](ExpressionNode* e) -> std::string
    {
        if(!e)
            return "";

        if(auto* id = dynamic_cast<IdentifierNode*>(e))
            return id->name;

        if(auto* field = dynamic_cast<FieldAccessNode*>(e))
        {
            if(field->object)
                return resolve(field->object);
            return field->structName;
        }

        if(auto* index = dynamic_cast<IndexExpressionNode*>(e))
            return resolve(index->base);

        if(auto* tupleAccess = dynamic_cast<TupleAccessNode*>(e))
            return resolve(tupleAccess->tuple);

        if(auto* unary = dynamic_cast<UnaryOpNode*>(e))
        {
            if(unary->op == UnaryOpNode::OP_DEREF)
            {
                if(auto* pid = dynamic_cast<IdentifierNode*>(unary->operand))
                {
                    auto it = pointerBorrowTarget.find(pid->name);
                    if(it != pointerBorrowTarget.end())
                        return it->second;
                }
            }
            return resolve(unary->operand);
        }

        return "";
    };

    return resolve(expr);
}

std::string CodeGenerator::getBorrowedOwnerForPointerExpression(
    ExpressionNode* expr) const
{
    auto* idExpr = dynamic_cast<IdentifierNode*>(expr);
    if(!idExpr)
        return "";

    auto borrowIt = pointerBorrowTarget.find(idExpr->name);
    if(borrowIt == pointerBorrowTarget.end())
        return "";

    return borrowIt->second;
}

bool CodeGenerator::validatePointerDereference(ExpressionNode* pointerExpr,
                                               int line)
{
    std::string ownerName =
        getBorrowedOwnerForPointerExpression(pointerExpr);
    if(ownerName.empty())
        return true;

    if(globalNamedValues.find(ownerName) == globalNamedValues.end() &&
       isVariableMoved(ownerName))
    {
        reportError(line, "cannot dereference pointer to moved value: '" +
                              ownerName + "'");
        return false;
    }

    return true;
}

bool CodeGenerator::validateNoEscapingBorrow(ExpressionNode* expr, int line,
                                             const std::string& action)
{
    std::string ownerName;
    if(auto* idExpr = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = pointerBorrowTarget.find(idExpr->name);
        if(it != pointerBorrowTarget.end())
            ownerName = it->second;
    }
    else if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_ADDR)
        {
            ownerName = resolveBorrowOwnerFromLValue(unary->operand);
        }
    }

    if(ownerName.empty())
        return true;

    if(globalNamedValues.find(ownerName) != globalNamedValues.end())
        return true;

    if(namedValues.find(ownerName) != namedValues.end())
    {
        reportError(line, "cannot " + action +
                              " pointer that borrows local value: '" +
                              ownerName + "'");
        return false;
    }

    return true;
}

void CodeGenerator::consumeMoveFromExpression(ExpressionNode* expr, int line,
                                              const std::string& context)
{
    std::function<void(ExpressionNode*, bool)> consume =
        [&](ExpressionNode* e, bool borrowed) -> void
    {
        if(!e)
            return;

        if(auto* id = dynamic_cast<IdentifierNode*>(e))
        {
            if(globalNamedValues.find(id->name) != globalNamedValues.end())
                return;

            if(isVariableMoved(id->name))
            {
                reportError(line, "use of moved value: '" + id->name + "'");
                return;
            }

            if(!borrowed && isMoveOnlyVariable(id->name))
            {
                auto borIt = activeBorrowers.find(id->name);
                if(borIt != activeBorrowers.end() && !borIt->second.empty())
                {
                    std::string by = *borIt->second.begin();
                    reportError(line, "cannot move '" + id->name +
                                          "' while borrowed by '" + by + "'");
                    return;
                }
                movedVariables.insert(id->name);
            }
            return;
        }

        if(auto* unary = dynamic_cast<UnaryOpNode*>(e))
        {
            if(unary->op == UnaryOpNode::OP_ADDR)
            {
                consume(unary->operand, true);
            }
            else
            {
                consume(unary->operand, borrowed);
            }
            return;
        }

        if(auto* binary = dynamic_cast<BinaryOpNode*>(e))
        {
            consume(binary->left, false);
            consume(binary->right, false);
            return;
        }

        if(auto* call = dynamic_cast<FunctionCallNode*>(e))
        {
            (void)call;
            return;
        }

        if(auto* methodCall = dynamic_cast<MethodCallNode*>(e))
        {
            (void)methodCall;
            return;
        }

        if(auto* field = dynamic_cast<FieldAccessNode*>(e))
        {
            if(field->object)
            {
                consume(field->object, true);
            }
            else if(!field->structName.empty())
            {
                if(globalNamedValues.find(field->structName) ==
                       globalNamedValues.end() &&
                   isVariableMoved(field->structName))
                {
                    reportError(line, "use of moved value: '" +
                                          field->structName + "'");
                }
            }
            return;
        }

        if(auto* index = dynamic_cast<IndexExpressionNode*>(e))
        {
            consume(index->base, true);
            consume(index->index, false);
            return;
        }

        if(auto* castExpr = dynamic_cast<CastExpressionNode*>(e))
        {
            consume(castExpr->expression, false);
            return;
        }

        if(auto* tryExpr = dynamic_cast<TryExpressionNode*>(e))
        {
            consume(tryExpr->expression, false);
            return;
        }

        if(auto* ternary = dynamic_cast<TernaryNode*>(e))
        {
            consume(ternary->condition, false);
            auto incomingMoved = movedVariables;

            movedVariables = incomingMoved;
            consume(ternary->trueExpr, false);
            auto thenMoved = movedVariables;

            movedVariables = incomingMoved;
            consume(ternary->falseExpr, false);
            auto elseMoved = movedVariables;

            thenMoved.insert(elseMoved.begin(), elseMoved.end());
            movedVariables = std::move(thenMoved);
            return;
        }

        if(auto* listLit = dynamic_cast<ListLiteralNode*>(e))
        {
            if(listLit->elements)
            {
                for(auto* elem : listLit->elements->elements)
                    consume(elem, false);
            }
            return;
        }

        if(auto* mapLit = dynamic_cast<MapLiteralNode*>(e))
        {
            if(mapLit->entries)
            {
                for(auto* entry : mapLit->entries->entries)
                {
                    consume(entry->key, false);
                    consume(entry->value, false);
                }
            }
            return;
        }

        if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(e))
        {
            if(tupleLit->elements)
            {
                for(auto* elem : tupleLit->elements->elements)
                    consume(elem, false);
            }
            return;
        }

        if(auto* structLit = dynamic_cast<StructLiteralNode*>(e))
        {
            for(const auto& init : structLit->fields)
                consume(init.second, false);
            return;
        }
    };

    consume(expr, false);
}

llvm::Type* CodeGenerator::getLLVMTypeFromNode(TypeNode* typeNode)
{
    if(!typeNode)
        return nullptr;

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    // Must check this BEFORE StructTypeRefNode since GenericStructTypeRefNode
    // is a more specific case
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(typeNode))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        auto it = structTypes.find(mangledName);
        if(it != structTypes.end())
        {
            return it->second;
        }
        std::cerr << "Failed to monomorphize struct: "
                  << genStructRef->structName << std::endl;
        return nullptr;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(typeNode))
    {
        auto enumIt = enumValues.find(structRef->structName);
        if(enumIt != enumValues.end())
        {
            return llvm::Type::getInt32Ty(context);
        }

        auto it = structTypes.find(structRef->structName);
        if(it != structTypes.end())
        {
            return it->second;
        }

        // Check if this is a type parameter (like T, U) - should not reach here
        // in properly monomorphized code
        std::cerr << "Unknown struct type: " << structRef->structName
                  << std::endl;
        return nullptr;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(typeNode))
    {
        std::vector<llvm::Type*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
                return nullptr;
            elemTypes.push_back(elemType);
        }
        return llvm::StructType::get(context, elemTypes);
    }

    // Handle generic list type
    if(auto* listType = dynamic_cast<GenericListTypeNode*>(typeNode))
    {
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* elemType = getLLVMTypeFromNode(listType->elementType);
        llvm::Type* ptrType = llvm::PointerType::get(elemType, 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        return llvm::StructType::get(context, listStructTypes);
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(typeNode))
    {
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        return llvm::StructType::get(context, mapStructTypes);
    }

    // Handle pointer type
    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(typeNode))
    {
        llvm::Type* elemType = getLLVMTypeFromNode(ptrType->elementType);
        if(!elemType)
            return nullptr;
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(elemType, 0);
#endif
    }

    // Fall back to basic type kind
    return getLLVMType(typeNode->kind);
}

void CodeGenerator::initializeStdioFunctions()
{
    if(stdioInitialized)
        return;

    // Get pointer type for strings
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    // Declare printf: int printf(const char* format, ...)
    llvm::FunctionType* printfType =
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {ptrType},
                                true // variadic
        );
    printfFunc = module->getOrInsertFunction("printf", printfType);

    // Declare fprintf: int fprintf(FILE* stream, const char* format, ...)
    llvm::FunctionType* fprintfType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context), {ptrType, ptrType},
        true // variadic
    );
    fprintfFunc = module->getOrInsertFunction("fprintf", fprintfType);

    // Declare snprintf: int snprintf(char* str, size_t size, const char* format, ...)
    llvm::FunctionType* snprintfType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context), {ptrType, int64Type, ptrType},
        true // variadic
    );
    snprintfFunc = module->getOrInsertFunction("snprintf", snprintfType);

    // Get stderr - platform specific
    // On macOS/Darwin, stderr is accessed via __stderrp
    // On Linux/other platforms, it's just stderr
#if defined(__APPLE__) || defined(__MACH__)
    const char* stderrName = "__stderrp";
#else
    const char* stderrName = "stderr";
#endif

    stderrPtr = module->getOrInsertGlobal(stderrName, ptrType);
    if(auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(stderrPtr))
    {
        gv->setExternallyInitialized(true);
    }

    stdioInitialized = true;
}

void CodeGenerator::initializePthreadFunctions()
{
    if(pthreadInitialized)
        return;

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* intType = llvm::Type::getInt32Ty(context);

    llvm::FunctionType* pthreadCreateType =
        llvm::FunctionType::get(intType,
                                {ptrType, ptrType, ptrType, ptrType}, false);
    pthreadCreateFunc =
        module->getOrInsertFunction("pthread_create", pthreadCreateType);

    llvm::FunctionType* pthreadJoinType =
        llvm::FunctionType::get(intType, {ptrType, ptrType}, false);
    pthreadJoinFunc =
        module->getOrInsertFunction("pthread_join", pthreadJoinType);

    llvm::FunctionType* pthreadMutexInitType =
        llvm::FunctionType::get(intType, {ptrType, ptrType}, false);
    pthreadMutexInitFunc =
        module->getOrInsertFunction("pthread_mutex_init",
                                    pthreadMutexInitType);

    llvm::FunctionType* pthreadMutexDestroyType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexDestroyFunc =
        module->getOrInsertFunction("pthread_mutex_destroy",
                                    pthreadMutexDestroyType);

    llvm::FunctionType* pthreadMutexLockType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexLockFunc =
        module->getOrInsertFunction("pthread_mutex_lock",
                                    pthreadMutexLockType);

    llvm::FunctionType* pthreadMutexUnlockType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexUnlockFunc =
        module->getOrInsertFunction("pthread_mutex_unlock",
                                    pthreadMutexUnlockType);

    pthreadInitialized = true;
}

void CodeGenerator::initializeStdlibFunctions()
{
    if(stdlibInitialized)
        return;

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* intType = llvm::Type::getInt32Ty(context);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::FunctionType* mallocType =
        llvm::FunctionType::get(ptrType, {int64Type}, false);
    mallocFunc = module->getOrInsertFunction("malloc", mallocType);

    llvm::FunctionType* freeType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {ptrType},
                                false);
    freeFunc = module->getOrInsertFunction("free", freeType);

    llvm::FunctionType* strcmpType =
        llvm::FunctionType::get(intType, {ptrType, ptrType}, false);
    strcmpFunc = module->getOrInsertFunction("strcmp", strcmpType);

    llvm::FunctionType* abortType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {}, false);
    abortFunc = module->getOrInsertFunction("abort", abortType);

    stdlibInitialized = true;
}

void CodeGenerator::initializeFormatFunctions()
{
    initializeStdioFunctions();
    initializeStdlibFunctions();
}

// Helper to get the TypeKind from an expression (for identifiers)
TypeNode::TypeKind getExpressionTypeKind(
    ExpressionNode* expr,
    const std::map<std::string, TypeNode::TypeKind>& variableTypes)
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = variableTypes.find(id->name);
        if(it != variableTypes.end())
        {
            return it->second;
        }
    }
    // Default to signed int for literals and unknown expressions
    return TypeNode::TYPE_INT;
}

std::string
CodeGenerator::convertFormatString(const std::string& mlaFormat,
                                   const std::vector<ExpressionNode*>& args,
                                   std::vector<llvm::Value*>& argValues,
                                   int line)
{
    std::string cFormat;
    size_t argIndex = 0;

    auto trim = [](std::string s) {
        size_t start = 0;
        while(start < s.size() &&
              std::isspace(static_cast<unsigned char>(s[start])))
            ++start;
        size_t end = s.size();
        while(end > start &&
              std::isspace(static_cast<unsigned char>(s[end - 1])))
            --end;
        return s.substr(start, end - start);
    };

    for(size_t i = 0; i < mlaFormat.size(); ++i)
    {
        if(mlaFormat[i] == '{' && i + 1 < mlaFormat.size() &&
           mlaFormat[i + 1] == '{')
        {
            // Escaped {{ -> {
            cFormat += '{';
            i++;
            continue;
        }
        if(mlaFormat[i] == '}' && i + 1 < mlaFormat.size() &&
           mlaFormat[i + 1] == '}')
        {
            // Escaped }} -> }
            cFormat += '}';
            i++;
            continue;
        }
        if(mlaFormat[i] == '{')
        {
            size_t close = mlaFormat.find('}', i + 1);
            if(close == std::string::npos)
            {
                cFormat += '{';
                continue;
            }

            std::string inside = trim(mlaFormat.substr(i + 1, close - i - 1));
            std::string name;
            std::string spec;

            if(!inside.empty())
            {
                size_t colon = inside.find(':');
                if(colon == std::string::npos)
                {
                    if(inside == "?" || inside == "#?")
                        spec = inside;
                    else
                        name = inside;
                }
                else
                {
                    name = trim(inside.substr(0, colon));
                    spec = trim(inside.substr(colon + 1));
                }
            }

            bool debug = false;
            bool pretty = false;
            if(!spec.empty())
            {
                if(spec == "?")
                    debug = true;
                else if(spec == "#?")
                {
                    debug = true;
                    pretty = true;
                }
                else
                {
                    reportError(line, "unsupported format specifier: {" + spec +
                                          "}");
                }
            }

            ExpressionNode* argExpr = nullptr;
            if(!name.empty())
            {
                argExpr = new IdentifierNode(name);
            }
            else if(argIndex < args.size())
            {
                argExpr = args[argIndex++];
            }
            else
            {
                cFormat += "{" + inside + "}";
                i = close;
                continue;
            }

            llvm::Value* argVal = generateExpression(argExpr);
            if(argVal)
                appendFormatValue(argExpr, argVal, debug, pretty, cFormat,
                                  argValues, line);

            i = close;
            continue;
        }

        cFormat += mlaFormat[i];
    }

    return cFormat;
}

bool CodeGenerator::isStringExpression(ExpressionNode* expr) const
{
    if(dynamic_cast<StringLiteralNode*>(expr))
        return true;
    if(dynamic_cast<FormatNode*>(expr))
        return true;
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = variableTypes.find(id->name);
        return it != variableTypes.end() && it->second == TypeNode::TYPE_STRING;
    }
    return false;
}

std::string CodeGenerator::getStructTypeName(ExpressionNode* expr) const
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = structVariableTypes.find(id->name);
        if(it != structVariableTypes.end())
            return it->second;
    }
    if(auto* lit = dynamic_cast<StructLiteralNode*>(expr))
        return lit->structName;
    return {};
}

llvm::Value* CodeGenerator::getLValuePointer(ExpressionNode* expr, int line)
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = namedValues.find(id->name);
        if(it == namedValues.end())
        {
            reportError(line, "unknown variable: '" + id->name + "'");
            return nullptr;
        }
        return it->second;
    }

    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(expr))
    {
        llvm::Value* structPtr = nullptr;
        std::string structTypeName;

        if(fieldAccess->object)
        {
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, line);
            if(!ptr)
                return nullptr;
            structPtr = ptr;
            structTypeName = typeName;
        }
        else
        {
            structPtr = namedValues[fieldAccess->structName];
            if(!structPtr)
            {
                reportError(line,
                            "unknown variable: " + fieldAccess->structName);
                return nullptr;
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + fieldAccess->structName +
                                      "' is not a struct");
                return nullptr;
            }
            structTypeName = typeIt->second;

            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
            {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if(allocaType->isPointerTy())
                {
                    structPtr = builder.CreateLoad(
                        allocaType, alloca, fieldAccess->structName + ".ptr");
                }
            }
        }

        auto memberIt = structMembers.find(structTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(line, "unknown struct type: " + structTypeName);
            return nullptr;
        }

        int fieldIndex = -1;
        const auto& members = memberIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldAccess->fieldName)
            {
                fieldIndex = static_cast<int>(i);
                break;
            }
        }

        if(fieldIndex < 0)
        {
            reportError(line, "struct '" + structTypeName +
                                  "' has no field named '" +
                                  fieldAccess->fieldName + "'");
            return nullptr;
        }

        llvm::StructType* structType = getStructType(structTypeName);
        if(!structType)
            return nullptr;

        return builder.CreateStructGEP(
            structType, structPtr, static_cast<unsigned>(fieldIndex),
            fieldAccess->fieldName + "_ptr");
    }

    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_DEREF)
        {
            if(!validatePointerDereference(unary->operand, line))
                return nullptr;

            llvm::Value* ptrVal = generateExpression(unary->operand);
            if(!ptrVal)
                return nullptr;
            if(!ptrVal->getType()->isPointerTy())
            {
                reportError(line, "dereference requires a pointer value");
                return nullptr;
            }
            return ptrVal;
        }
    }

    reportError(line, "address-of operator requires an assignable expression");
    return nullptr;
}

TypeNode* CodeGenerator::getLValueType(ExpressionNode* expr, int line)
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto typeIt = variableTypes.find(id->name);
        if(typeIt == variableTypes.end())
        {
            reportError(line, "unknown variable: '" + id->name + "'");
            return nullptr;
        }

        TypeNode::TypeKind kind = typeIt->second;
        if(kind == TypeNode::TYPE_STRUCT)
        {
            auto structIt = structVariableTypes.find(id->name);
            if(structIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + id->name +
                                      "' is not a struct");
                return nullptr;
            }
            return new StructTypeRefNode(structIt->second);
        }
        if(kind == TypeNode::TYPE_LIST)
        {
            auto listIt = listElementTypes.find(id->name);
            if(listIt == listElementTypes.end())
            {
                reportError(line, "list element type not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            return new GenericListTypeNode(listIt->second);
        }
        if(kind == TypeNode::TYPE_MAP)
        {
            auto mapIt = mapKeyValueTypes.find(id->name);
            if(mapIt == mapKeyValueTypes.end())
            {
                reportError(line, "map key/value types not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            return new MapTypeNode(mapIt->second.first, mapIt->second.second);
        }
        if(kind == TypeNode::TYPE_TUPLE)
        {
            auto tupleIt = tupleElementTypes.find(id->name);
            if(tupleIt == tupleElementTypes.end())
            {
                reportError(line,
                            "tuple element types not known for '" + id->name +
                                "'");
                return nullptr;
            }
            auto* list = new TypeListNode();
            for(auto* t : tupleIt->second)
                list->addType(t);
            return new TupleTypeNode(list);
        }
        if(kind == TypeNode::TYPE_PTR)
        {
            auto ptrIt = pointerElementTypes.find(id->name);
            if(ptrIt == pointerElementTypes.end())
            {
                reportError(line, "pointer element type not known for '" +
                                      id->name + "'");
                return nullptr;
            }
            return new PointerTypeNode(ptrIt->second);
        }

        return new TypeNode(kind);
    }

    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(expr))
    {
        std::string structTypeName;
        if(fieldAccess->object)
        {
            TypeNode* objType = getLValueType(fieldAccess->object, line);
            if(!objType)
                return nullptr;
            if(objType->kind != TypeNode::TYPE_STRUCT)
            {
                reportError(line, "field access requires a struct value");
                return nullptr;
            }

            if(auto* structRef = dynamic_cast<StructTypeRefNode*>(objType))
            {
                structTypeName = structRef->structName;
            }
            else if(auto* genRef =
                        dynamic_cast<GenericStructTypeRefNode*>(objType))
            {
                structTypeName = getOrCreateMonomorphizedStruct(
                    genRef->structName, genRef->typeArgs);
            }
        }
        else
        {
            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + fieldAccess->structName +
                                      "' is not a struct");
                return nullptr;
            }
            structTypeName = typeIt->second;
        }

        auto memberIt = structMembers.find(structTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(line, "unknown struct type: " + structTypeName);
            return nullptr;
        }

        for(const auto& member : memberIt->second)
        {
            if(member.first == fieldAccess->fieldName)
                return member.second;
        }

        reportError(line, "struct '" + structTypeName +
                              "' has no field named '" +
                              fieldAccess->fieldName + "'");
        return nullptr;
    }

    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_DEREF)
        {
            TypeNode* ptrType = getLValueType(unary->operand, line);
            if(auto* ptrNode = dynamic_cast<PointerTypeNode*>(ptrType))
                return ptrNode->elementType;

            reportError(line, "dereference requires a pointer value");
            return nullptr;
        }
    }

    reportError(line, "cannot determine type of expression");
    return nullptr;
}

TypeNode* CodeGenerator::getPointerElementType(ExpressionNode* expr, int line)
{
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_ADDR)
        {
            return getLValueType(unary->operand, line);
        }
    }

    TypeNode* type = getLValueType(expr, line);
    if(auto* ptrNode = dynamic_cast<PointerTypeNode*>(type))
        return ptrNode->elementType;

    reportError(line, "dereference requires a pointer value");
    return nullptr;
}

void CodeGenerator::appendFormatValue(ExpressionNode* expr, llvm::Value* value,
                                      bool debug, bool pretty,
                                      std::string& cFormat,
                                      std::vector<llvm::Value*>& argValues,
                                      int line)
{
    llvm::Type* argType = value->getType();

    if(argType->isStructTy())
    {
        std::string structName = argType->getStructName().str();
        if(structName.empty())
            structName = getStructTypeName(expr);
        if(!(debug || ( !structName.empty() && debugStructs.count(structName))))
        {
            // fall through to non-debug handling below
        }
        else
        {
        if(structName.empty())
        {
            reportError(line, "cannot debug-format unnamed struct");
            cFormat += "%s";
            argValues.push_back(builder.CreateGlobalStringPtr("<struct>"));
            return;
        }
        if(!debugStructs.count(structName))
        {
            reportError(line, "struct '" + structName +
                                  "' does not derive Debug");
        }
        llvm::Value* dbg =
            buildStructDebugString(value, structName, debug ? pretty : false, line);
        cFormat += "%s";
        argValues.push_back(dbg);
        return;
        }
    }

    TypeNode::TypeKind typeKind = getExpressionTypeKind(expr, variableTypes);
    bool isUnsigned = isUnsignedType(typeKind);

    if(argType->isIntegerTy(1))
    {
        cFormat += "%d";
        llvm::Value* intVal = builder.CreateZExt(
            value, llvm::Type::getInt32Ty(context), "booltoInt");
        argValues.push_back(intVal);
    }
    else if(argType->isIntegerTy(8))
    {
        if(isUnsigned)
        {
            cFormat += "%u";
            llvm::Value* intVal = builder.CreateZExt(
                value, llvm::Type::getInt32Ty(context), "u8toInt");
            argValues.push_back(intVal);
        }
        else
        {
            cFormat += "%d";
            llvm::Value* intVal = builder.CreateSExt(
                value, llvm::Type::getInt32Ty(context), "i8toInt");
            argValues.push_back(intVal);
        }
    }
    else if(argType->isIntegerTy(16))
    {
        if(isUnsigned)
        {
            cFormat += "%u";
            llvm::Value* intVal = builder.CreateZExt(
                value, llvm::Type::getInt32Ty(context), "u16toInt");
            argValues.push_back(intVal);
        }
        else
        {
            cFormat += "%d";
            llvm::Value* intVal = builder.CreateSExt(
                value, llvm::Type::getInt32Ty(context), "i16toInt");
            argValues.push_back(intVal);
        }
    }
    else if(argType->isIntegerTy(32))
    {
        cFormat += isUnsigned ? "%u" : "%d";
        argValues.push_back(value);
    }
    else if(argType->isIntegerTy(64))
    {
        cFormat += isUnsigned ? "%llu" : "%lld";
        argValues.push_back(value);
    }
    else if(argType->isFloatTy())
    {
        cFormat += "%f";
        llvm::Value* doubleVal = builder.CreateFPExt(
            value, llvm::Type::getDoubleTy(context), "floatToDouble");
        argValues.push_back(doubleVal);
    }
    else if(argType->isDoubleTy())
    {
        cFormat += "%f";
        argValues.push_back(value);
    }
    else if(argType->isPointerTy())
    {
        cFormat += "%s";
        argValues.push_back(value);
    }
    else if(argType->isStructTy())
    {
        std::string structName =
            argType->getStructName().str().empty()
                ? "anonymous struct"
                : argType->getStructName().str();
        reportError(line, "cannot print struct type '" + structName +
                              "' directly; use {:?} with #[derive(Debug)]");
        cFormat += "%s";
        argValues.push_back(builder.CreateGlobalStringPtr("<struct>"));
    }
    else
    {
        reportError(line, "cannot print value of unknown type");
        cFormat += "%d";
        argValues.push_back(value);
    }
}

void CodeGenerator::generatePrintStatement(PrintNode* node)
{
    if(node->debugOnly && !debugEnabled)
        return;

    // Initialize stdio functions if not already done
    if(!stdioInitialized)
    {
        initializeStdioFunctions();
    }

    // Convert MLA format string to C format string and collect argument values
    std::vector<llvm::Value*> argValues;
    std::string cFormat =
        convertFormatString(node->formatString, node->arguments, argValues,
                            node->line);

    // Add newline for println!/eprintln!
    if(node->kind == PrintNode::PRINTLN_STDOUT ||
       node->kind == PrintNode::EPRINTLN_STDERR)
    {
        cFormat += "\n";
    }

    // Create the format string as a global constant
#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "printfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(cFormat, "printfmt");
#endif

    // Build the argument list
    std::vector<llvm::Value*> printArgs;

    if(node->kind == PrintNode::PRINT_STDERR ||
       node->kind == PrintNode::EPRINTLN_STDERR)
    {
        // For stderr, use fprintf(stderr, format, ...)
        // Load the stderr pointer
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::Value* stderrVal =
            builder.CreateLoad(ptrType, stderrPtr, "stderr");
        printArgs.push_back(stderrVal);
        printArgs.push_back(formatStr);
        printArgs.insert(printArgs.end(), argValues.begin(), argValues.end());

        builder.CreateCall(fprintfFunc, printArgs);
    }
    else
    {
        // For stdout, use printf(format, ...)
        printArgs.push_back(formatStr);
        printArgs.insert(printArgs.end(), argValues.begin(), argValues.end());

        builder.CreateCall(printfFunc, printArgs);
    }
}

llvm::Value* CodeGenerator::generateFormatExpression(FormatNode* node)
{
    initializeFormatFunctions();

    std::vector<llvm::Value*> argValues;
    std::string cFormat =
        convertFormatString(node->formatString, node->arguments, argValues,
                            node->line);

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "formatstr");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(cFormat, "formatstr");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);

    std::vector<llvm::Value*> sizeArgs;
    sizeArgs.push_back(nullPtr);
    sizeArgs.push_back(zero);
    sizeArgs.push_back(formatStr);
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());

    llvm::Value* len32 =
        builder.CreateCall(snprintfFunc, sizeArgs, "fmtlen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "fmtlen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "fmtsz");

    llvm::Value* buffer =
        builder.CreateCall(mallocFunc, {size}, "fmtbuf");

    std::vector<llvm::Value*> writeArgs;
    writeArgs.push_back(buffer);
    writeArgs.push_back(size);
    writeArgs.push_back(formatStr);
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);

    return buffer;
}

void CodeGenerator::generateAssertEq(AssertEqNode* node)
{
    initializeFormatFunctions();

    llvm::Value* lhs = generateExpression(node->left);
    llvm::Value* rhs = generateExpression(node->right);
    if(!lhs || !rhs)
        return;

    llvm::Value* cmp = nullptr;
    if(isStringExpression(node->left) || isStringExpression(node->right))
    {
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::Value* lhsPtr = builder.CreateBitCast(lhs, ptrType, "lhsstr");
        llvm::Value* rhsPtr = builder.CreateBitCast(rhs, ptrType, "rhsstr");
        llvm::Value* cmpVal = builder.CreateCall(strcmpFunc, {lhsPtr, rhsPtr});
        cmp = builder.CreateICmpEQ(
            cmpVal, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
            "streq");
    }
    else if(lhs->getType()->isFloatingPointTy() ||
            rhs->getType()->isFloatingPointTy())
    {
        llvm::Value* l = lhs;
        llvm::Value* r = rhs;
        if(!l->getType()->isDoubleTy())
            l = builder.CreateFPExt(l, llvm::Type::getDoubleTy(context), "l2d");
        if(!r->getType()->isDoubleTy())
            r = builder.CreateFPExt(r, llvm::Type::getDoubleTy(context), "r2d");
        cmp = builder.CreateFCmpOEQ(l, r, "feq");
    }
    else if(lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy())
    {
        llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
        llvm::Value* l = lhs;
        llvm::Value* r = rhs;
        if(l->getType() != int64Type)
            l = builder.CreateSExtOrTrunc(l, int64Type, "l2i64");
        if(r->getType() != int64Type)
            r = builder.CreateSExtOrTrunc(r, int64Type, "r2i64");
        cmp = builder.CreateICmpEQ(l, r, "ieq");
    }
    else
    {
        reportError(node->line, "assert_eq! supports only numeric and string types");
        return;
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* okBB =
        llvm::BasicBlock::Create(context, "assert.ok", function);
    llvm::BasicBlock* failBB =
        llvm::BasicBlock::Create(context, "assert.fail");
    builder.CreateCondBr(cmp, okBB, failBB);

    failBB->insertInto(function);
    builder.SetInsertPoint(failBB);

    std::string msg = "assert_eq! failed: left = {:#?}, right = {:#?}";
    std::vector<ExpressionNode*> args = {node->left, node->right};
    std::vector<llvm::Value*> argValues;
    std::string cFormat = convertFormatString(msg, args, argValues, node->line);

    // Ensure newline
    cFormat += "\n";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "assertfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(cFormat, "assertfmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Value* stderrVal =
        builder.CreateLoad(ptrType, stderrPtr, "stderr");

    std::vector<llvm::Value*> printArgs;
    printArgs.push_back(stderrVal);
    printArgs.push_back(formatStr);
    printArgs.insert(printArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(fprintfFunc, printArgs);

    builder.CreateCall(abortFunc, {});
    builder.CreateUnreachable();

    builder.SetInsertPoint(okBB);
}

llvm::Value* CodeGenerator::buildDebugString(ExpressionNode* expr, bool pretty,
                                             int line)
{
    llvm::Value* val = generateExpression(expr);
    if(!val)
        return builder.CreateGlobalStringPtr("<null>");
    if(val->getType()->isStructTy())
    {
        std::string structName = val->getType()->getStructName().str();
        if(structName.empty())
            structName = getStructTypeName(expr);
        return buildStructDebugString(val, structName, pretty, line);
    }

    std::vector<llvm::Value*> argValues;
    std::string cFormat;
    appendFormatValue(expr, val, false, false, cFormat, argValues, line);

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "dbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(cFormat, "dbgfmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 =
        builder.CreateCall(snprintfFunc, sizeArgs, "dbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "dbglen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "dbgsz");
    llvm::Value* buffer =
        builder.CreateCall(mallocFunc, {size}, "dbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

llvm::Value* CodeGenerator::buildStructDebugString(llvm::Value* structVal,
                                                   const std::string& structName,
                                                   bool pretty, int line)
{
    initializeFormatFunctions();

    auto it = structMembers.find(structName);
    if(it == structMembers.end())
    {
        reportError(line, "unknown struct for debug: " + structName);
        return builder.CreateGlobalStringPtr("<struct>");
    }

    std::string displayName = structName;
    if(auto mit = mangledToGenericName.find(structName);
       mit != mangledToGenericName.end())
    {
        displayName = mit->second;
    }

    std::string fmt = displayName + (pretty ? " {\n" : " { ");
    std::vector<llvm::Value*> argValues;

    for(size_t idx = 0; idx < it->second.size(); ++idx)
    {
        const auto& member = it->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;

        if(pretty)
            fmt += "    " + memberName + ": ";
        else
            fmt += memberName + ": ";

        llvm::Value* fieldVal =
            builder.CreateExtractValue(structVal, static_cast<unsigned>(idx),
                                       "dbgfield");

        bool handled = false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType))
        {
            std::string fieldStruct = structRef->structName;
            if(!debugStructs.count(fieldStruct))
            {
                reportError(line, "struct '" + fieldStruct +
                                      "' does not derive Debug");
            }
            llvm::Value* fieldStr =
                buildStructDebugString(fieldVal, fieldStruct, pretty, line);
            fmt += "%s";
            argValues.push_back(fieldStr);
            handled = true;
        }

        if(!handled)
        {
            switch(memberType->kind)
            {
            case TypeNode::TYPE_BOOL: {
                fmt += "%d";
                llvm::Value* intVal = builder.CreateZExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgbool");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_I8:
            case TypeNode::TYPE_I16:
            case TypeNode::TYPE_INT: {
                fmt += "%d";
                llvm::Value* intVal = builder.CreateSExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgint");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_U8:
            case TypeNode::TYPE_U16:
            case TypeNode::TYPE_U32: {
                fmt += "%u";
                llvm::Value* intVal = builder.CreateZExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgu");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_I32: {
                fmt += "%d";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_I64: {
                fmt += "%lld";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_U64: {
                fmt += "%llu";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_FLOAT: {
                fmt += "%f";
                llvm::Value* doubleVal = builder.CreateFPExt(
                    fieldVal, llvm::Type::getDoubleTy(context), "dbgfloat");
                argValues.push_back(doubleVal);
                break;
            }
            case TypeNode::TYPE_DOUBLE: {
                fmt += "%f";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_STRING:
            case TypeNode::TYPE_STR8:
            case TypeNode::TYPE_STR16: {
                fmt += "%s";
                argValues.push_back(fieldVal);
                break;
            }
            default:
                fmt += "<unsupported>";
                break;
            }
        }

        if(pretty)
            fmt += ",\n";
        else if(idx + 1 < it->second.size())
            fmt += ", ";
        else
            fmt += " ";
    }

    if(pretty)
        fmt += "}";
    else
        fmt += "}";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(fmt, "dbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(fmt, "dbgfmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 =
        builder.CreateCall(snprintfFunc, sizeArgs, "dbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "dbglen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "dbgsz");
    llvm::Value* buffer =
        builder.CreateCall(mallocFunc, {size}, "dbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

void CodeGenerator::generateCode(ProgramNode* program)
{
    globalNamedValues.clear();
    globalConstantVariables.clear();
    globalVariableTypes.clear();
    globalStructVariableTypes.clear();

    ensureHandleBuiltin(program);
    ensureThreadBuiltin(program);
    ensureMutexBuiltin(program);
    ensureAtomic64Builtin(program);
    ensureOptionBuiltin(program);
    ensureResultBuiltin(program);

    enum class MainArgMode
    {
        None,
        ArgsList,
        ArgsListWithCount
    };

    FunctionDefNode* mainDef = nullptr;
    MainArgMode mainArgMode = MainArgMode::None;
    GenericListTypeNode* mainArgsListType = nullptr;
    TypeNode::TypeKind mainArgcKind = TypeNode::TYPE_VOID;
    std::vector<FunctionDefNode*> testFunctions;

    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(fn && fn->name == "main" && !fn->isExtern)
            {
                mainDef = fn;
            }
            if(fn && fn->isTest && !fn->isExtern)
                testFunctions.push_back(fn);
        }
    }

    if(testMode && mainDef)
    {
        reportError(mainDef->line,
                    "main is not allowed in test mode; use #[test] functions");
    }

    if(mainDef)
    {
        size_t paramCount = mainDef->parameters->parameters.size();
        bool returnOk = mainDef->returnType &&
                        (mainDef->returnType->kind == TypeNode::TYPE_INT ||
                         mainDef->returnType->kind == TypeNode::TYPE_I32);
        if(!returnOk)
        {
            reportError(
                mainDef->line,
                "invalid signature for 'main': return type must be i32");
        }

        if(paramCount == 0)
        {
            mainArgMode = MainArgMode::None;
        }
        else if(paramCount == 1 || paramCount == 2)
        {
            size_t listIndex = paramCount - 1;
            if(paramCount == 2)
            {
                mainArgcKind =
                    mainDef->parameters->parameters[0]->type->kind;
                if(!(mainArgcKind == TypeNode::TYPE_INT ||
                     mainArgcKind == TypeNode::TYPE_I32 ||
                     mainArgcKind == TypeNode::TYPE_I64))
                {
                    reportError(
                        mainDef->line,
                        "invalid signature for 'main': argc must be i32/i64");
                }
            }

            auto* listType = dynamic_cast<GenericListTypeNode*>(
                mainDef->parameters->parameters[listIndex]->type);
            if(!listType)
            {
                reportError(
                    mainDef->line,
                    "invalid signature for 'main': argv must be list<str8>");
            }
            else
            {
                auto elemKind = listType->elementType->kind;
                if(!(elemKind == TypeNode::TYPE_STR8 ||
                     elemKind == TypeNode::TYPE_STRING))
                {
                    reportError(
                        mainDef->line,
                        "invalid signature for 'main': argv must be list<str8>");
                }
                else
                {
                    mainArgsListType = listType;
                }
            }

            mainArgMode = (paramCount == 1) ? MainArgMode::ArgsList
                                            : MainArgMode::ArgsListWithCount;
            mainDef->name = "__mlang_user_main";
        }
        else
        {
            reportError(mainDef->line,
                        "invalid signature for 'main': expected no args, "
                        "list<str8>, or (i32, list<str8>)");
        }
    }

    // Reserved type keywords and type/name conflicts.
    const std::unordered_set<std::string> reservedTypeNames = {
        "void",  "bool", "int",  "float", "double", "string", "str8", "str16",
        "list",  "map",  "tuple","i8",    "i16",    "i32",    "i64",
        "u8",    "u16",  "u32",  "u64"};

    std::map<std::string, std::pair<std::string, int>> typeDefs;
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(!st)
                continue;
            if(reservedTypeNames.count(st->name))
            {
                reportError(st->line, "type name '" + st->name +
                                          "' is a reserved keyword");
            }
            auto it = typeDefs.find(st->name);
            if(it != typeDefs.end())
            {
                reportError(st->line,
                            "type name '" + st->name +
                                "' conflicts with earlier " + it->second.first +
                                " defined at line " +
                                std::to_string(it->second.second));
            }
            else
            {
                typeDefs[st->name] = {"struct", st->line};
            }
        }
    }

    if(!program->globalVars.empty())
    {
        for(auto* gv : program->globalVars)
        {
            if(!gv)
                continue;
            generateGlobalVarDeclaration(gv);
        }
    }

    if(program->enumList)
    {
        for(auto* en : program->enumList->enums)
        {
            if(!en)
                continue;
            if(reservedTypeNames.count(en->name))
            {
                reportError(en->line, "type name '" + en->name +
                                          "' is a reserved keyword");
            }
            auto it = typeDefs.find(en->name);
            if(it != typeDefs.end())
            {
                reportError(en->line,
                            "type name '" + en->name +
                                "' conflicts with earlier " + it->second.first +
                                " defined at line " +
                                std::to_string(it->second.second));
            }
            else
            {
                typeDefs[en->name] = {"enum", en->line};
            }
        }
    }

    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(!fn)
                continue;
            auto it = typeDefs.find(fn->name);
            if(it != typeDefs.end())
            {
                reportError(fn->line,
                            "function name '" + fn->name +
                                "' conflicts with type '" + it->first + "'");
            }
        }
    }

    if(program->enumList)
    {
        for(auto enumDef : program->enumList->enums)
        {
            generateEnumDefinition(enumDef);
        }
    }

    // First, collect generic struct templates and impl blocks
    // These are NOT generated immediately - they're instantiated on demand
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(structDef->isGeneric())
            {
                // Store as template for later instantiation
                genericStructTemplates[structDef->name] = structDef;
            }
        }
    }

    // Collect generic impl blocks
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(!impl->typeParams.empty())
            {
                // This is a generic impl block
                genericImplBlocks[impl->structName].push_back(impl);
            }
        }
    }

    // Generate all NON-GENERIC struct definitions
    // We need to process base structs before derived structs
    if(program->structList)
    {
        // Build a map of struct names to their definitions
        std::map<std::string, StructDefNode*> structMap;
        for(auto structDef : program->structList->structs)
        {
            structMap[structDef->name] = structDef;
        }

        // Process structs in dependency order (bases before derived)
        std::set<std::string> processed;
        std::function<void(StructDefNode*)> processStruct =
            [&](StructDefNode* structDef)
        {
            if(processed.count(structDef->name))
                return;

            // Skip generic structs - they're instantiated on demand
            if(structDef->isGeneric())
            {
                processed.insert(structDef->name);
                return;
            }

            // Process base first if it exists
            if(!structDef->baseName.empty())
            {
                auto baseIt = structMap.find(structDef->baseName);
                if(baseIt != structMap.end())
                {
                    processStruct(baseIt->second);
                }
            }

            // Track struct visibility
            structVisibility[structDef->name] =
                std::make_pair(structDef->isPublic, structDef->sourceModule);
            generateStructDefinition(structDef);
            processed.insert(structDef->name);
        };

        for(auto structDef : program->structList->structs)
        {
            processStruct(structDef);
        }
    }

    // Generate forward declarations for all functions first
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(funcDef->isTest && !includeTests)
                continue;
            llvm::Function* decl = generateFunctionDeclaration(funcDef);
            registerFunctionOverload(funcDef, decl);
        }
    }

    // Generate NON-GENERIC struct method declarations and track visibility
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric())
            {
                generateStructMethods(structDef);
            }
        }
    }

    // Process non-generic impl blocks (add methods to existing structs)
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                // Non-generic impl block - process immediately
                for(auto method : impl->methods)
                {
                    // Register the method with the struct
                    structMethods[impl->structName][method->name] =
                        std::make_pair(method->isPublic, method);

                    // Generate method declaration
                    generateMethodDeclaration(impl->structName, method);
                }
            }
        }
    }

    // Then generate all function bodies
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(funcDef->isTest && !includeTests)
                continue;
            generateFunctionDefinition(funcDef);
        }
    }

    if(testMode && !testFunctions.empty())
    {
        for(auto* testFn : testFunctions)
        {
            if(!testFn || !testFn->parameters)
                continue;
            if(!testFn->parameters->parameters.empty())
            {
                reportError(testFn->line,
                            "test functions must have no parameters");
                continue;
            }
            if(testFn->returnType &&
               !(testFn->returnType->kind == TypeNode::TYPE_VOID ||
                 testFn->returnType->kind == TypeNode::TYPE_INT ||
                 testFn->returnType->kind == TypeNode::TYPE_I32))
            {
                reportError(testFn->line,
                            "test functions must return void or i32");
                continue;
            }
        }
        generateTestMain(testFunctions);
    }

    // Generate a C-compatible main wrapper if needed.
    if(!testMode && mainDef && mainArgMode != MainArgMode::None && mainArgsListType)
    {
        llvm::Type* i32Type = llvm::Type::getInt32Ty(context);
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
        llvm::Type* argvPtrType = ptrType;
#else
        llvm::Type* i8Type = llvm::Type::getInt8Ty(context);
        llvm::Type* i8PtrType = llvm::PointerType::get(i8Type, 0);
        llvm::Type* argvPtrType = llvm::PointerType::get(i8PtrType, 0);
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMTypeFromNode(mainArgsListType->elementType), 0);
#endif

        std::vector<llvm::Type*> wrapperParams = {i32Type, argvPtrType};
        llvm::FunctionType* wrapperType =
            llvm::FunctionType::get(i32Type, wrapperParams, false);
        llvm::Function* wrapper =
            llvm::Function::Create(wrapperType, llvm::Function::ExternalLinkage,
                                   "main", module.get());

        auto argIt = wrapper->arg_begin();
        llvm::Value* argcArg = &*argIt++;
        llvm::Value* argvArg = &*argIt++;
        argcArg->setName("argc");
        argvArg->setName("argv");

        llvm::BasicBlock* entry =
            llvm::BasicBlock::Create(context, "entry", wrapper);
        builder.SetInsertPoint(entry);

        llvm::Value* argc64 =
            builder.CreateSExt(argcArg, i64Type, "argc64");

        // Build list struct { size: i64, data: ptr }
        llvm::Type* listStructType = getLLVMTypeFromNode(mainArgsListType);
        llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
        listStruct =
            builder.CreateInsertValue(listStruct, argc64, 0, "args.size");

#if LLVM_VERSION_MAJOR >= 15
        llvm::Value* dataPtr = argvArg;
#else
        llvm::Value* dataPtr = argvArg;
        if(argvArg->getType() != ptrType)
            dataPtr = builder.CreateBitCast(argvArg, ptrType, "args.data");
#endif

        listStruct =
            builder.CreateInsertValue(listStruct, dataPtr, 1, "args.data");

        llvm::Function* userMain =
            module->getFunction(functionSymbolName(mainDef));
        if(!userMain)
            userMain = module->getFunction("__mlang_user_main");
        if(!userMain)
        {
            reportError(mainDef->line,
                        "failed to generate main wrapper: missing user main");
            builder.CreateRet(llvm::ConstantInt::get(i32Type, 1));
        }
        else
        {
            std::vector<llvm::Value*> callArgs;
            if(mainArgMode == MainArgMode::ArgsListWithCount)
            {
                llvm::Value* argcValue = argcArg;
                if(mainArgcKind == TypeNode::TYPE_I64)
                    argcValue = argc64;
                callArgs.push_back(argcValue);
            }
            callArgs.push_back(listStruct);
            llvm::Value* rc =
                builder.CreateCall(userMain, callArgs, "mainrc");
            builder.CreateRet(rc);
        }
    }

    // Generate NON-GENERIC struct method bodies
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric() && structDef->members)
            {
                for(auto method : structDef->members->methods)
                {
                    generateMethodDefinition(structDef->name, method);
                }
            }
        }
    }

    // Generate non-generic impl block method bodies
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                for(auto method : impl->methods)
                {
                    generateMethodDefinition(impl->structName, method);
                }
            }
        }
    }
}

void CodeGenerator::generateTestMain(
    const std::vector<FunctionDefNode*>& tests)
{
    initializeStdioFunctions();

    llvm::Type* i32Type = llvm::Type::getInt32Ty(context);
    llvm::FunctionType* mainType =
        llvm::FunctionType::get(i32Type, {}, false);
    llvm::Function* mainFn =
        llvm::Function::Create(mainType, llvm::Function::ExternalLinkage,
                               "main", module.get());

    llvm::BasicBlock* entry =
        llvm::BasicBlock::Create(context, "entry", mainFn);
    builder.SetInsertPoint(entry);

    llvm::AllocaInst* failures =
        builder.CreateAlloca(i32Type, nullptr, "failures");
    builder.CreateStore(llvm::ConstantInt::get(i32Type, 0), failures);
    llvm::AllocaInst* totalTests =
        builder.CreateAlloca(i32Type, nullptr, "total_tests");
    builder.CreateStore(llvm::ConstantInt::get(i32Type, 0), totalTests);

    auto make_cstr = [&](const std::string& s, const char* name) -> llvm::Value*
    {
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString(s, name);
#else
        return builder.CreateGlobalStringPtr(s, name);
#endif
    };

    llvm::Value* passFmt = make_cstr("[PASS] %s\n", "test.pass.fmt");
    llvm::Value* failFmt = make_cstr("[FAIL] %s (rc=%d)\n", "test.fail.fmt");
    llvm::Value* summaryFmt =
        make_cstr("[SUMMARY] total=%d pass=%d fail=%d\n", "test.summary.fmt");

    for(auto* testFn : tests)
    {
        if(!testFn || testFn->isExtern)
            continue;
        llvm::Function* callee =
            module->getFunction(functionSymbolName(testFn));
        if(!callee)
            continue;

        llvm::Value* totalCur =
            builder.CreateLoad(i32Type, totalTests, "tests.cur");
        llvm::Value* totalNext =
            builder.CreateAdd(totalCur, llvm::ConstantInt::get(i32Type, 1),
                              "tests.next");
        builder.CreateStore(totalNext, totalTests);

        llvm::Value* testName = make_cstr(testFn->name, "test.name");
        llvm::Value* result = builder.CreateCall(callee, {});
        if(callee->getReturnType()->isVoidTy())
        {
            builder.CreateCall(printfFunc, {passFmt, testName});
            continue;
        }

        llvm::Value* resultI32 = result;
        if(resultI32->getType() != i32Type)
        {
            if(resultI32->getType()->isIntegerTy())
            {
                resultI32 = builder.CreateIntCast(resultI32, i32Type, true,
                                                  "test.rc.cast");
            }
            else
            {
                resultI32 = llvm::ConstantInt::get(i32Type, 1);
            }
        }

        llvm::Value* isFail = builder.CreateICmpNE(
            resultI32, llvm::ConstantInt::get(i32Type, 0), "testfail");
        llvm::Value* failInc =
            builder.CreateZExt(isFail, i32Type, "failinc");
        llvm::Value* cur =
            builder.CreateLoad(i32Type, failures, "failures.cur");
        llvm::Value* next = builder.CreateAdd(cur, failInc, "failures.next");
        builder.CreateStore(next, failures);

        llvm::BasicBlock* passBB =
            llvm::BasicBlock::Create(context, "test.pass", mainFn);
        llvm::BasicBlock* failBB =
            llvm::BasicBlock::Create(context, "test.fail");
        llvm::BasicBlock* contBB =
            llvm::BasicBlock::Create(context, "test.cont");
        builder.CreateCondBr(isFail, failBB, passBB);

        builder.SetInsertPoint(passBB);
        builder.CreateCall(printfFunc, {passFmt, testName});
        builder.CreateBr(contBB);

        failBB->insertInto(mainFn);
        builder.SetInsertPoint(failBB);
        builder.CreateCall(printfFunc, {failFmt, testName, resultI32});
        builder.CreateBr(contBB);

        contBB->insertInto(mainFn);
        builder.SetInsertPoint(contBB);
    }

    llvm::Value* total =
        builder.CreateLoad(i32Type, failures, "failures.total");
    llvm::Value* totalCount =
        builder.CreateLoad(i32Type, totalTests, "tests.total");
    llvm::Value* passCount =
        builder.CreateSub(totalCount, total, "tests.pass");
    builder.CreateCall(printfFunc, {summaryFmt, totalCount, passCount, total});

    builder.CreateRet(total);
}

void CodeGenerator::ensureResultBuiltin(ProgramNode* program)
{
    if(!program)
        return;

    bool hasResult = false;
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(st && st->name == "Result")
            {
                hasResult = true;
                break;
            }
        }
    }

    if(hasResult)
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(false,
                                            new TypeNode(TypeNode::TYPE_BOOL),
                                            "is_ok", nullptr));
    members->addMember(new StructMemberNode(false,
                                            new StructTypeRefNode("T"),
                                            "ok", nullptr));
    members->addMember(new StructMemberNode(false,
                                            new StructTypeRefNode("E"),
                                            "err", nullptr));

    auto make_self_param = []() -> ParameterNode*
    {
        auto* selfType = new GenericStructTypeRefNode("Result");
        selfType->typeArgs.push_back(new StructTypeRefNode("T"));
        selfType->typeArgs.push_back(new StructTypeRefNode("E"));
        return new ParameterNode(selfType, "self");
    };

    auto make_simple_method = [&](const std::string& name, TypeNode* retType,
                                  ExpressionNode* retExpr) -> StructMethodNode*
    {
        auto* params = new ParameterListNode();
        params->parameters.push_back(make_self_param());
        auto* retStmt = new ReturnNode(retExpr);
        auto* body = new StatementListNode();
        body->statements.push_back(retStmt);
        auto* method =
            new StructMethodNode(retType, name, params, body, true, false);
        return method;
    };

    members->addMethod(
        make_simple_method("is_ok", new TypeNode(TypeNode::TYPE_BOOL),
                           static_cast<ExpressionNode*>(
                               create_field_access_expr(
                                   new IdentifierNode("self"),
                                   strdup("is_ok"), 0))));
    members->addMethod(
        make_simple_method("is_err", new TypeNode(TypeNode::TYPE_BOOL),
                           new BinaryOpNode(
                               BinaryOpNode::OP_EQ,
                               static_cast<ExpressionNode*>(
                                   create_field_access_expr(
                                       new IdentifierNode("self"),
                                       strdup("is_ok"), 0)),
                               new BoolLiteralNode(false))));
    members->addMethod(
        make_simple_method("unwrap", new StructTypeRefNode("T"),
                           static_cast<ExpressionNode*>(
                               create_field_access_expr(
                                   new IdentifierNode("self"),
                                   strdup("ok"), 0))));
    members->addMethod(
        make_simple_method("unwrap_err", new StructTypeRefNode("E"),
                           static_cast<ExpressionNode*>(
                               create_field_access_expr(
                                   new IdentifierNode("self"),
                                   strdup("err"), 0))));

    auto* resultDef = new StructDefNode("Result", "", members, true);
    resultDef->typeParams = {"T", "E"};
    resultDef->sourceModule = "";

    if(!program->structList)
        program->structList = new StructListNode();
    program->structList->addStruct(resultDef);
}

void CodeGenerator::ensureOptionBuiltin(ProgramNode* program)
{
    if(!program)
        return;

    bool hasOption = false;
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(st && st->name == "Option")
            {
                hasOption = true;
                break;
            }
        }
    }

    if(hasOption)
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(false,
                                            new TypeNode(TypeNode::TYPE_BOOL),
                                            "is_some", nullptr));
    members->addMember(new StructMemberNode(false,
                                            new StructTypeRefNode("T"),
                                            "value", nullptr));

    auto* optionDef = new StructDefNode("Option", "", members, true);
    optionDef->typeParams = {"T"};
    optionDef->sourceModule = "";

    if(!program->structList)
        program->structList = new StructListNode();
    program->structList->addStruct(optionDef);
}

void CodeGenerator::ensureHandleBuiltin(ProgramNode* program)
{
    if(!program)
        return;

    bool hasHandle = false;
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(st && st->name == "Handle")
            {
                hasHandle = true;
                break;
            }
        }
    }

    if(hasHandle)
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(false,
                                            new TypeNode(TypeNode::TYPE_I64),
                                            "raw", nullptr));

    auto* handleDef = new StructDefNode("Handle", "", members, true);
    handleDef->typeParams = {"T"};
    handleDef->sourceModule = "";

    if(!program->structList)
        program->structList = new StructListNode();
    program->structList->addStruct(handleDef);
}

void CodeGenerator::ensureThreadBuiltin(ProgramNode* program)
{
    if(!program)
        return;

    bool hasThread = false;
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(st && st->name == "Thread")
            {
                hasThread = true;
                break;
            }
        }
    }

    if(hasThread)
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(false,
                                            new TypeNode(TypeNode::TYPE_I8),
                                            "_pad", nullptr));

    auto* threadDef = new StructDefNode("Thread", "", members, true);
    threadDef->sourceModule = "";

    if(!program->structList)
        program->structList = new StructListNode();
    program->structList->addStruct(threadDef);
}

void CodeGenerator::ensureMutexBuiltin(ProgramNode* program)
{
    if(!program)
        return;

    bool hasMutex = false;
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(st && st->name == "Mutex")
            {
                hasMutex = true;
                break;
            }
        }
    }

    if(hasMutex)
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(false,
                                            new TypeNode(TypeNode::TYPE_I8),
                                            "_pad", nullptr));

    auto* mutexDef = new StructDefNode("Mutex", "", members, true);
    mutexDef->sourceModule = "";

    if(!program->structList)
        program->structList = new StructListNode();
    program->structList->addStruct(mutexDef);
}

void CodeGenerator::ensureAtomic64Builtin(ProgramNode* program)
{
    if(!program)
        return;

    bool hasAtomic = false;
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(st && st->name == "Atomic64")
            {
                hasAtomic = true;
                break;
            }
        }
    }

    if(hasAtomic)
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(false,
                                            new TypeNode(TypeNode::TYPE_I8),
                                            "_pad", nullptr));

    auto* atomicDef = new StructDefNode("Atomic64", "", members, true);
    atomicDef->sourceModule = "";

    if(!program->structList)
        program->structList = new StructListNode();
    program->structList->addStruct(atomicDef);
}

llvm::Function*
CodeGenerator::generateFunctionDeclaration(FunctionDefNode* node)
{
    std::string symbolName = functionSymbolName(node);
    std::vector<llvm::Type*> paramTypes;
    for(auto param : node->parameters->parameters)
    {
        llvm::Type* paramType = getLLVMTypeFromNode(param->type);
        if(!paramType)
        {
            reportError(param->line,
                        "unknown parameter type for '" + param->name + "'");
            paramType = llvm::Type::getInt32Ty(context); // fallback
        }
        paramTypes.push_back(paramType);
    }

    llvm::Type* returnType = getLLVMTypeFromNode(node->returnType);
    if(!returnType)
    {
        reportError(node->line,
                    "unknown return type for function '" + node->name + "'");
        returnType = llvm::Type::getVoidTy(context); // fallback
    }

    // Check if function already declared
    if(llvm::Function* existing = module->getFunction(symbolName))
    {
        if(existing->arg_size() != paramTypes.size() ||
           existing->getReturnType() != returnType ||
           existing->isVarArg() != node->parameters->isVarArg)
        {
            reportError(node->line,
                        "conflicting declaration for function '" +
                            node->name + "'");
        }
        return existing;
    }

    llvm::FunctionType* funcType =
        llvm::FunctionType::get(returnType, paramTypes,
                                node->parameters->isVarArg);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, symbolName, module.get());

    // Set parameter names
    unsigned idx = 0;
    for(auto& arg : function->args())
    {
        arg.setName(node->parameters->parameters[idx++]->name);
    }

    return function;
}

void CodeGenerator::seedFunctionScopeWithGlobals()
{
    for(const auto& it : globalNamedValues)
        namedValues[it.first] = it.second;
    for(const auto& it : globalVariableTypes)
        variableTypes[it.first] = it.second;
    for(const auto& it : globalStructVariableTypes)
        structVariableTypes[it.first] = it.second;
    for(const auto& n : globalConstantVariables)
        constantVariables.insert(n);
}

void CodeGenerator::generateGlobalVarDeclaration(VarDeclNode* node)
{
    if(!node)
        return;
    if(globalNamedValues.find(node->name) != globalNamedValues.end())
    {
        reportError(node->line, "duplicate global variable: '" + node->name +
                                    "'");
        return;
    }

    TypeNode::TypeKind kind = TypeNode::TYPE_INT;
    if(node->type)
        kind = node->type->kind;
    else if(node->initExpr)
    {
        if(dynamic_cast<BoolLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_BOOL;
        else if(dynamic_cast<FloatLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_FLOAT;
        else if(dynamic_cast<DoubleLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_DOUBLE;
        else if(dynamic_cast<StringLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_STRING;
    }

    llvm::Type* llvmTy = node->type ? getLLVMTypeFromNode(node->type)
                                    : getLLVMType(kind);
    if(!llvmTy)
    {
        reportError(node->line, "invalid global variable type for '" +
                                    node->name + "'");
        return;
    }

    llvm::Constant* init = llvm::Constant::getNullValue(llvmTy);
    if(node->initExpr)
    {
        if(auto* i = dynamic_cast<IntLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isIntegerTy())
            {
                reportError(node->line, "global integer initializer type mismatch for '" +
                                            node->name + "'");
                return;
            }
            init = llvm::ConstantInt::get(llvmTy, i->value, true);
        }
        else if(auto* b = dynamic_cast<BoolLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isIntegerTy(1))
            {
                reportError(node->line, "global bool initializer type mismatch for '" +
                                            node->name + "'");
                return;
            }
            init = llvm::ConstantInt::get(llvmTy, b->value ? 1 : 0, false);
        }
        else if(auto* f = dynamic_cast<FloatLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isFloatTy())
            {
                reportError(node->line, "global float initializer type mismatch for '" +
                                            node->name + "'");
                return;
            }
            init = llvm::ConstantFP::get(llvmTy, f->value);
        }
        else if(auto* d = dynamic_cast<DoubleLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isDoubleTy())
            {
                reportError(node->line, "global double initializer type mismatch for '" +
                                            node->name + "'");
                return;
            }
            init = llvm::ConstantFP::get(llvmTy, d->value);
        }
        else if(auto* s = dynamic_cast<StringLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isPointerTy())
            {
                reportError(node->line, "global string initializer type mismatch for '" +
                                            node->name + "'");
                return;
            }
#if LLVM_VERSION_MAJOR >= 21
            auto* gstr = builder.CreateGlobalString(s->value, node->name + ".gstr");
            init = llvm::dyn_cast<llvm::Constant>(gstr);
#else
            auto* gstr = builder.CreateGlobalStringPtr(s->value, node->name + ".gstr");
            init = llvm::dyn_cast<llvm::Constant>(gstr);
#endif
            if(!init)
                init = llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(llvmTy));
        }
        else
        {
            reportError(node->line,
                        "global initializer must be a literal constant");
            return;
        }
    }

    auto* gv = new llvm::GlobalVariable(
        *module, llvmTy, false, llvm::GlobalValue::InternalLinkage, init,
        "__mlang_global_" + node->name);
    globalNamedValues[node->name] = gv;
    globalVariableTypes[node->name] = kind;
}

llvm::Function* CodeGenerator::generateFunctionDefinition(FunctionDefNode* node)
{
    std::string symbolName = functionSymbolName(node);
    // Get the function (should already be declared)
    llvm::Function* function = module->getFunction(symbolName);
    if(!function)
    {
        // If not declared yet, declare it now
        function = generateFunctionDeclaration(node);
    }

    if(node->isExtern || !node->body)
    {
        return function;
    }

    // Check if function already has a body (was already defined)
    if(!function->empty())
    {
        // Function already defined, skip
        return function;
    }

    // Track which module this function is from (for visibility checks)
    std::string savedModule = currentModule;
    currentModule = node->sourceModule;

    // Create a new basic block for the function
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear the named values map and constant tracking for new function scope
    namedValues.clear();
    constantVariables.clear();
    movedVariables.clear();
    pointerBorrowTarget.clear();
    activeBorrowers.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    // Set up parameters
    unsigned paramIdx = 0;
    for(auto& arg : function->args())
    {
        // Allocate space for parameters so they can be modified
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        builder.CreateStore(&arg, alloca);
        namedValues[std::string(arg.getName())] = alloca;
        recordVariableScopeDepth(std::string(arg.getName()));

        // Track parameter types
        if(paramIdx < node->parameters->parameters.size())
        {
            auto* paramNode = node->parameters->parameters[paramIdx];
            variableTypes[std::string(arg.getName())] = paramNode->type->kind;
            if(auto* structType =
                   dynamic_cast<StructTypeRefNode*>(paramNode->type))
            {
                structVariableTypes[std::string(arg.getName())] =
                    structType->structName;
            }
            if(auto* genStructType =
                   dynamic_cast<GenericStructTypeRefNode*>(paramNode->type))
            {
                std::string mangled = getOrCreateMonomorphizedStruct(
                    genStructType->structName, genStructType->typeArgs);
                structVariableTypes[std::string(arg.getName())] = mangled;
            }
            if(auto* genListType =
                   dynamic_cast<GenericListTypeNode*>(paramNode->type))
            {
                listElementTypes[std::string(arg.getName())] =
                    genListType->elementType;
            }
            if(auto* mapType = dynamic_cast<MapTypeNode*>(paramNode->type))
            {
                mapKeyValueTypes[std::string(arg.getName())] =
                    std::make_pair(mapType->keyType, mapType->valueType);
            }
            if(auto* ptrType = dynamic_cast<PointerTypeNode*>(paramNode->type))
            {
                pointerElementTypes[std::string(arg.getName())] =
                    ptrType->elementType;
            }
        }
        paramIdx++;
    }

    // Generate the function body
    if(node->body)
    {
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
        }
    }

    // Run scope-exit destructors for locals at normal function fallthrough.
    exitCleanupScope();

    // If the function is void and doesn't have a return, add one
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!currentBlock->getTerminator())
    {
        if(returnType->isVoidTy())
        {
            builder.CreateRetVoid();
        }
        else
        {
            if(node->name == "main" || node->name == "__mlang_user_main")
            {
                // Default main return to 0 when no explicit return is present.
                builder.CreateRet(llvm::ConstantInt::get(returnType, 0, true));
            }
            else
            {
                // For non-void functions without a return, add unreachable
                // This indicates a bug in the source code but prevents LLVM crashes
                builder.CreateUnreachable();
            }
        }
    }

    // Restore the previous module context
    currentModule = savedModule;

    // Verify the function
    llvm::verifyFunction(*function);
    return function;
}

void CodeGenerator::generateStatement(StatementNode* node)
{
    if(auto returnNode = dynamic_cast<ReturnNode*>(node))
    {
        generateReturnStatement(returnNode);
    }
    else if(auto letNode = dynamic_cast<LetDeclNode*>(node))
    {
        generateLetDeclaration(letNode);
    }
    else if(auto varNode = dynamic_cast<VarDeclNode*>(node))
    {
        generateVarDeclaration(varNode);
    }
    else if(auto assignNode = dynamic_cast<AssignmentNode*>(node))
    {
        generateAssignment(assignNode);
    }
    else if(auto fieldAssignNode = dynamic_cast<FieldAssignmentNode*>(node))
    {
        generateFieldAssignment(fieldAssignNode);
    }
    else if(auto derefAssignNode = dynamic_cast<DerefAssignmentNode*>(node))
    {
        generateDerefAssignment(derefAssignNode);
    }
    else if(auto ifNode = dynamic_cast<IfNode*>(node))
    {
        generateIfStatement(ifNode);
    }
    else if(auto forNode = dynamic_cast<ForNode*>(node))
    {
        generateForStatement(forNode);
    }
    else if(auto blockNode = dynamic_cast<BlockStatementNode*>(node))
    {
        enterCleanupScope();
        for(auto stmt : blockNode->statements->statements)
        {
            generateStatement(stmt);
        }
        exitCleanupScope();
    }
    else if(auto printNode = dynamic_cast<PrintNode*>(node))
    {
        generatePrintStatement(printNode);
    }
    else if(auto assertNode = dynamic_cast<AssertEqNode*>(node))
    {
        generateAssertEq(assertNode);
    }
    else if(auto breakNode = dynamic_cast<BreakNode*>(node))
    {
        generateBreakStatement(breakNode);
    }
    else if(auto continueNode = dynamic_cast<ContinueNode*>(node))
    {
        generateContinueStatement(continueNode);
    }
    else if(auto exprStmt = dynamic_cast<ExpressionStatementNode*>(node))
    {
        generateExpression(exprStmt->expression);
    }
}

llvm::Value* CodeGenerator::generateExpression(ExpressionNode* node)
{
    if(auto intLit = dynamic_cast<IntLiteralNode*>(node))
    {
        return generateIntLiteral(intLit);
    }
    else if(auto boolLit = dynamic_cast<BoolLiteralNode*>(node))
    {
        return generateBoolLiteral(boolLit);
    }
    else if(auto floatLit = dynamic_cast<FloatLiteralNode*>(node))
    {
        return generateFloatLiteral(floatLit);
    }
    else if(auto doubleLit = dynamic_cast<DoubleLiteralNode*>(node))
    {
        return generateDoubleLiteral(doubleLit);
    }
    else if(auto stringLit = dynamic_cast<StringLiteralNode*>(node))
    {
        return generateStringLiteral(stringLit);
    }
    else if(auto formatExpr = dynamic_cast<FormatNode*>(node))
    {
        return generateFormatExpression(formatExpr);
    }
    else if(auto enumLit = dynamic_cast<EnumLiteralNode*>(node))
    {
        return generateEnumLiteral(enumLit);
    }
    else if(auto binOp = dynamic_cast<BinaryOpNode*>(node))
    {
        return generateBinaryOp(binOp);
    }
    else if(auto unaryOp = dynamic_cast<UnaryOpNode*>(node))
    {
        return generateUnaryOp(unaryOp);
    }
    else if(auto ternary = dynamic_cast<TernaryNode*>(node))
    {
        return generateTernaryExpression(ternary);
    }
    else if(auto tryExpr = dynamic_cast<TryExpressionNode*>(node))
    {
        return generateTryExpression(tryExpr);
    }
    else if(auto id = dynamic_cast<IdentifierNode*>(node))
    {
        return generateIdentifier(id);
    }
    else if(auto fieldAcc = dynamic_cast<FieldAccessNode*>(node))
    {
        return generateFieldAccess(fieldAcc);
    }
    else if(auto call = dynamic_cast<FunctionCallNode*>(node))
    {
        return generateFunctionCall(call);
    }
    else if(auto methodCall = dynamic_cast<MethodCallNode*>(node))
    {
        return generateMethodCall(methodCall);
    }
    else if(auto cast = dynamic_cast<CastExpressionNode*>(node))
    {
        return generateCastExpression(cast);
    }
    else if(auto listLit = dynamic_cast<ListLiteralNode*>(node))
    {
        return generateListLiteral(listLit);
    }
    else if(auto mapLit = dynamic_cast<MapLiteralNode*>(node))
    {
        return generateMapLiteral(mapLit);
    }
    else if(auto indexExpr = dynamic_cast<IndexExpressionNode*>(node))
    {
        return generateIndexExpression(indexExpr);
    }
    else if(auto tupleLit = dynamic_cast<TupleLiteralNode*>(node))
    {
        return generateTupleLiteral(tupleLit);
    }
    else if(auto tupleAcc = dynamic_cast<TupleAccessNode*>(node))
    {
        return generateTupleAccess(tupleAcc);
    }
    else if(auto structLit = dynamic_cast<StructLiteralNode*>(node))
    {
        return generateStructLiteral(structLit);
    }
    else if(auto matchExpr = dynamic_cast<MatchExpressionNode*>(node))
    {
        return generateMatchExpression(matchExpr);
    }
    return nullptr;
}

llvm::Value* CodeGenerator::generateBinaryOp(BinaryOpNode* node)
{
    llvm::Value* L = generateExpression(node->left);
    llvm::Value* R = generateExpression(node->right);

    if(!L || !R)
        return nullptr;

    // Check for struct types - cannot perform arithmetic on structs
    if(L->getType()->isStructTy())
    {
        std::string typeName = L->getType()->getStructName().str().empty()
                                   ? "struct"
                                   : L->getType()->getStructName().str();
        reportError(node->line,
                    "cannot perform binary operation on struct type '" +
                        typeName + "'");
        return nullptr;
    }
    if(R->getType()->isStructTy())
    {
        std::string typeName = R->getType()->getStructName().str().empty()
                                   ? "struct"
                                   : R->getType()->getStructName().str();
        reportError(node->line,
                    "cannot perform binary operation on struct type '" +
                        typeName + "'");
        return nullptr;
    }

    // Check if we're dealing with floating point or integer types
    bool isFloat =
        L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy();

    // Verify operand types are numeric
    bool LIsNumeric =
        L->getType()->isIntegerTy() || L->getType()->isFloatingPointTy();
    bool RIsNumeric =
        R->getType()->isIntegerTy() || R->getType()->isFloatingPointTy();

    if(!LIsNumeric || !RIsNumeric)
    {
        reportError(node->line,
                    "binary operations require numeric operands (integer or "
                    "floating-point)");
        return nullptr;
    }

    // Handle type mismatches for integer operands
    if(!isFloat && L->getType()->isIntegerTy() && R->getType()->isIntegerTy())
    {
        unsigned LBits = L->getType()->getIntegerBitWidth();
        unsigned RBits = R->getType()->getIntegerBitWidth();

        if(LBits != RBits)
        {
            // Extend the smaller type to match the larger type
            if(LBits > RBits)
            {
                R = builder.CreateSExt(R, L->getType(), "sext");
            }
            else
            {
                L = builder.CreateSExt(L, R->getType(), "sext");
            }
        }
    }

    switch(node->op)
    {
    case BinaryOpNode::OP_PLUS:
        return isFloat ? builder.CreateFAdd(L, R, "addtmp")
                       : builder.CreateAdd(L, R, "addtmp");
    case BinaryOpNode::OP_MINUS:
        return isFloat ? builder.CreateFSub(L, R, "subtmp")
                       : builder.CreateSub(L, R, "subtmp");
    case BinaryOpNode::OP_MULTIPLY:
        return isFloat ? builder.CreateFMul(L, R, "multmp")
                       : builder.CreateMul(L, R, "multmp");
    case BinaryOpNode::OP_DIVIDE:
    {
        // Check for division by zero with constant divisor
        if(auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(R))
        {
            if(constInt->isZero())
            {
                reportError(node->line, "division by zero");
                return nullptr;
            }
        }
        if(auto* constFP = llvm::dyn_cast<llvm::ConstantFP>(R))
        {
            if(constFP->isZero())
            {
                reportError(node->line, "division by zero");
                return nullptr;
            }
        }
        return isFloat ? builder.CreateFDiv(L, R, "divtmp")
                       : builder.CreateSDiv(L, R, "divtmp");
    }
    case BinaryOpNode::OP_MODULO:
    {
        // Check for modulo by zero with constant divisor
        if(auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(R))
        {
            if(constInt->isZero())
            {
                reportError(node->line, "modulo by zero");
                return nullptr;
            }
        }
        if(auto* constFP = llvm::dyn_cast<llvm::ConstantFP>(R))
        {
            if(constFP->isZero())
            {
                reportError(node->line, "modulo by zero");
                return nullptr;
            }
        }
        if(isFloat)
        {
            return builder.CreateFRem(L, R, "modtmp");
        }
        return builder.CreateSRem(L, R, "modtmp");
    }
    case BinaryOpNode::OP_LT:
        return isFloat ? builder.CreateFCmpOLT(L, R, "cmptmp")
                       : builder.CreateICmpSLT(L, R, "cmptmp");
    case BinaryOpNode::OP_GT:
        return isFloat ? builder.CreateFCmpOGT(L, R, "cmptmp")
                       : builder.CreateICmpSGT(L, R, "cmptmp");
    case BinaryOpNode::OP_LE:
        return isFloat ? builder.CreateFCmpOLE(L, R, "cmptmp")
                       : builder.CreateICmpSLE(L, R, "cmptmp");
    case BinaryOpNode::OP_GE:
        return isFloat ? builder.CreateFCmpOGE(L, R, "cmptmp")
                       : builder.CreateICmpSGE(L, R, "cmptmp");
    case BinaryOpNode::OP_EQ:
        return isFloat ? builder.CreateFCmpOEQ(L, R, "cmptmp")
                       : builder.CreateICmpEQ(L, R, "cmptmp");
    case BinaryOpNode::OP_NE:
        return isFloat ? builder.CreateFCmpONE(L, R, "cmptmp")
                       : builder.CreateICmpNE(L, R, "cmptmp");
    }
    return nullptr;
}

llvm::Value* CodeGenerator::generateUnaryOp(UnaryOpNode* node)
{
    switch(node->op)
    {
    case UnaryOpNode::OP_NEG:
    {
        llvm::Value* value = generateExpression(node->operand);
        if(!value)
            return nullptr;

        bool isFloat = value->getType()->isFloatingPointTy();
        bool isInt = value->getType()->isIntegerTy();

        if(!isFloat && !isInt)
        {
            reportError(node->line,
                        "unary '-' requires numeric operand (integer or float)");
            return nullptr;
        }
        return isFloat ? builder.CreateFNeg(value, "negtmp")
                       : builder.CreateNeg(value, "negtmp");
    }
    case UnaryOpNode::OP_ADDR:
    {
        llvm::Value* ptr = getLValuePointer(node->operand, node->line);
        if(!ptr)
            return nullptr;
        return ptr;
    }
    case UnaryOpNode::OP_DEREF:
    {
        if(!validatePointerDereference(node->operand, node->line))
            return nullptr;

        llvm::Value* ptrVal = generateExpression(node->operand);
        if(!ptrVal)
            return nullptr;
        if(!ptrVal->getType()->isPointerTy())
        {
            reportError(node->line, "dereference requires a pointer value");
            return nullptr;
        }

        TypeNode* elemTypeNode = getPointerElementType(node->operand, node->line);
        if(!elemTypeNode)
            return nullptr;
        llvm::Type* elemType = getLLVMTypeFromNode(elemTypeNode);
        if(!elemType)
            return nullptr;

        return builder.CreateLoad(elemType, ptrVal, "deref");
    }
    }
    return nullptr;
}

llvm::Value* CodeGenerator::generateTernaryExpression(TernaryNode* node)
{
    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
        return nullptr;

    llvm::Type* condType = condValue->getType();
    if(!condType->isIntegerTy() && !condType->isFloatingPointTy())
    {
        std::string typeStr;
        if(condType->isStructTy())
            typeStr = condType->getStructName().str().empty()
                          ? "struct"
                          : condType->getStructName().str();
        else if(condType->isPointerTy())
            typeStr = "pointer";
        else
            typeStr = "non-boolean";

        reportError(node->line,
                    "ternary condition must be a boolean or numeric type, got '" +
                        typeStr + "'");
        return nullptr;
    }

    if(!condValue->getType()->isIntegerTy(1))
    {
        if(condValue->getType()->isFloatingPointTy())
        {
            condValue = builder.CreateFCmpONE(
                condValue, llvm::ConstantFP::get(condValue->getType(), 0.0),
                "ternary.cond");
        }
        else
        {
            condValue = builder.CreateICmpNE(
                condValue, llvm::ConstantInt::get(condValue->getType(), 0),
                "ternary.cond");
        }
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* thenBB =
        llvm::BasicBlock::Create(context, "ternary.then", function);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context, "ternary.else");
    llvm::BasicBlock* mergeBB =
        llvm::BasicBlock::Create(context, "ternary.end");

    builder.CreateCondBr(condValue, thenBB, elseBB);

    // Then block
    builder.SetInsertPoint(thenBB);
    llvm::Value* thenVal = generateExpression(node->trueExpr);
    if(!thenVal)
        return nullptr;
    if(!builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(mergeBB);
    llvm::BasicBlock* thenEnd = builder.GetInsertBlock();

    // Else block
    elseBB->insertInto(function);
    builder.SetInsertPoint(elseBB);
    llvm::Value* elseVal = generateExpression(node->falseExpr);
    if(!elseVal)
        return nullptr;
    if(!builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(mergeBB);
    llvm::BasicBlock* elseEnd = builder.GetInsertBlock();

    std::vector<llvm::Type*> types = {thenVal->getType(), elseVal->getType()};
    auto commonTypeFrom = [&](const std::vector<llvm::Type*>& tps)
        -> llvm::Type*
    {
        if(tps.empty())
            return nullptr;
        llvm::Type* common = tps[0];
        bool anyFloat = common->isFloatingPointTy();
        bool anyInt = common->isIntegerTy();
        bool anyPtr = common->isPointerTy();
        bool anyStruct = common->isStructTy();

        unsigned maxIntBits = anyInt ? common->getIntegerBitWidth() : 0;
        bool anyDouble = common->isDoubleTy();

        for(size_t i = 1; i < tps.size(); ++i)
        {
            llvm::Type* t = tps[i];
            if(t == common)
                continue;
            if(t->isFloatingPointTy())
            {
                anyFloat = true;
                if(t->isDoubleTy())
                    anyDouble = true;
            }
            if(t->isIntegerTy())
            {
                anyInt = true;
                unsigned bits = t->getIntegerBitWidth();
                if(bits > maxIntBits)
                    maxIntBits = bits;
            }
            if(t->isPointerTy())
                anyPtr = true;
            if(t->isStructTy())
                anyStruct = true;
        }

        if(anyStruct)
        {
            for(auto* t : tps)
            {
                if(t != tps[0])
                    return nullptr;
            }
            return tps[0];
        }

        if(anyPtr && (anyFloat || anyInt || anyStruct))
            return nullptr;

        if(anyPtr && !(anyFloat || anyInt))
        {
            for(auto* t : tps)
            {
                if(!t->isPointerTy() || t != tps[0])
                    return nullptr;
            }
            return tps[0];
        }

        if(anyFloat)
            return anyDouble ? llvm::Type::getDoubleTy(context)
                             : llvm::Type::getFloatTy(context);
        if(anyInt)
            return llvm::Type::getIntNTy(context, maxIntBits);

        for(auto* t : tps)
        {
            if(t != tps[0])
                return nullptr;
        }
        return tps[0];
    };

    llvm::Type* commonType = commonTypeFrom(types);
    if(!commonType)
    {
        std::string t0 = thenVal->getType()->isStructTy()
                                  ? (thenVal->getType()->getStructName().str().empty()
                                         ? "struct"
                                         : thenVal->getType()->getStructName().str())
                                  : (thenVal->getType()->isPointerTy() ? "pointer"
                                                                       : "value");
        std::string t1 = elseVal->getType()->isStructTy()
                                  ? (elseVal->getType()->getStructName().str().empty()
                                         ? "struct"
                                         : elseVal->getType()->getStructName().str())
                                  : (elseVal->getType()->isPointerTy() ? "pointer"
                                                                       : "value");
        reportError(node->line, "ternary branches must return the same type (got " + t0 + " and " + t1 + ")");
        return nullptr;
    }
    if(commonType->isVoidTy())
    {
        reportError(node->line, "ternary branches must return a value");
        return nullptr;
    }

    if(thenVal->getType() != commonType)
    {
        llvm::IRBuilder<> castBuilder(thenEnd->getTerminator());
        llvm::Type* src = thenVal->getType();
        if(src->isIntegerTy() && commonType->isIntegerTy())
        {
            unsigned srcBits = src->getIntegerBitWidth();
            unsigned dstBits = commonType->getIntegerBitWidth();
            if(srcBits > dstBits)
                thenVal =
                    castBuilder.CreateTrunc(thenVal, commonType, "tern.trunc");
            else if(srcBits < dstBits)
                thenVal =
                    castBuilder.CreateSExt(thenVal, commonType, "tern.sext");
        }
        else if(src->isIntegerTy() && commonType->isFloatingPointTy())
        {
            thenVal =
                castBuilder.CreateSIToFP(thenVal, commonType, "tern.sitofp");
        }
        else if(src->isFloatingPointTy() && commonType->isFloatingPointTy())
        {
            thenVal =
                castBuilder.CreateFPCast(thenVal, commonType, "tern.fpcast");
        }
    }

    if(elseVal->getType() != commonType)
    {
        llvm::IRBuilder<> castBuilder(elseEnd->getTerminator());
        llvm::Type* src = elseVal->getType();
        if(src->isIntegerTy() && commonType->isIntegerTy())
        {
            unsigned srcBits = src->getIntegerBitWidth();
            unsigned dstBits = commonType->getIntegerBitWidth();
            if(srcBits > dstBits)
                elseVal =
                    castBuilder.CreateTrunc(elseVal, commonType, "tern.trunc");
            else if(srcBits < dstBits)
                elseVal =
                    castBuilder.CreateSExt(elseVal, commonType, "tern.sext");
        }
        else if(src->isIntegerTy() && commonType->isFloatingPointTy())
        {
            elseVal =
                castBuilder.CreateSIToFP(elseVal, commonType, "tern.sitofp");
        }
        else if(src->isFloatingPointTy() && commonType->isFloatingPointTy())
        {
            elseVal =
                castBuilder.CreateFPCast(elseVal, commonType, "tern.fpcast");
        }
    }

    mergeBB->insertInto(function);
    builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder.CreatePHI(commonType, 2, "ternary.result");
    phi->addIncoming(thenVal, thenEnd);
    phi->addIncoming(elseVal, elseEnd);
    return phi;
}

llvm::Value* CodeGenerator::generateTryExpression(TryExpressionNode* node)
{
    if(!node || !node->expression)
        return nullptr;

    llvm::Value* resultValue = generateExpression(node->expression);
    if(!resultValue)
        return nullptr;

    if(!resultValue->getType()->isStructTy())
    {
        reportError(node->line, "operator '?' expects Result<T, E> expression");
        return nullptr;
    }

    auto* resultStructType = llvm::cast<llvm::StructType>(resultValue->getType());
    std::string resultStructName = resultStructType->getName().str();
    auto resultMembersIt = structMembers.find(resultStructName);
    if(resultMembersIt == structMembers.end())
    {
        reportError(node->line, "operator '?' expects Result<T, E> expression");
        return nullptr;
    }

    int resultIsOkIndex = -1;
    int resultOkIndex = -1;
    int resultErrIndex = -1;
    TypeNode* resultOkType = nullptr;
    TypeNode* resultErrType = nullptr;
    for(size_t i = 0; i < resultMembersIt->second.size(); ++i)
    {
        const auto& mem = resultMembersIt->second[i];
        if(mem.first == "is_ok")
            resultIsOkIndex = static_cast<int>(i);
        else if(mem.first == "ok")
        {
            resultOkIndex = static_cast<int>(i);
            resultOkType = mem.second;
        }
        else if(mem.first == "err")
        {
            resultErrIndex = static_cast<int>(i);
            resultErrType = mem.second;
        }
    }

    if(resultIsOkIndex < 0 || resultOkIndex < 0 || resultErrIndex < 0)
    {
        reportError(node->line, "operator '?' expects Result<T, E> expression");
        return nullptr;
    }

    llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
    llvm::Type* expectedRetType = currentFunc->getReturnType();
    if(!expectedRetType->isStructTy())
    {
        reportError(node->line,
                    "operator '?' can only be used in functions returning Result<_, _>");
        return nullptr;
    }

    auto* expectedStructType = llvm::cast<llvm::StructType>(expectedRetType);
    std::string expectedStructName = expectedStructType->getName().str();
    auto expectedMembersIt = structMembers.find(expectedStructName);
    if(expectedMembersIt == structMembers.end())
    {
        reportError(node->line,
                    "operator '?' can only be used in functions returning Result<_, _>");
        return nullptr;
    }

    int expectedIsOkIndex = -1;
    int expectedOkIndex = -1;
    int expectedErrIndex = -1;
    TypeNode* expectedErrType = nullptr;
    for(size_t i = 0; i < expectedMembersIt->second.size(); ++i)
    {
        const auto& mem = expectedMembersIt->second[i];
        if(mem.first == "is_ok")
            expectedIsOkIndex = static_cast<int>(i);
        else if(mem.first == "ok")
            expectedOkIndex = static_cast<int>(i);
        else if(mem.first == "err")
        {
            expectedErrIndex = static_cast<int>(i);
            expectedErrType = mem.second;
        }
    }

    if(expectedIsOkIndex < 0 || expectedOkIndex < 0 || expectedErrIndex < 0 ||
       !expectedErrType)
    {
        reportError(node->line,
                    "operator '?' can only be used in functions returning Result<_, _>");
        return nullptr;
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* okBB = llvm::BasicBlock::Create(context, "try.ok", function);
    llvm::BasicBlock* errBB = llvm::BasicBlock::Create(context, "try.err");
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(context, "try.cont");

    llvm::Value* isOkVal =
        builder.CreateExtractValue(resultValue, resultIsOkIndex, "try.is_ok");
    llvm::Type* okLlvmType = getLLVMTypeFromNode(resultOkType);
    if(!okLlvmType)
    {
        reportError(node->line, "operator '?': failed to resolve Ok payload type");
        return nullptr;
    }
    llvm::AllocaInst* okSlot =
        builder.CreateAlloca(okLlvmType, nullptr, "try.ok.slot");

    builder.CreateCondBr(isOkVal, okBB, errBB);

    builder.SetInsertPoint(okBB);
    llvm::Value* okPayload =
        builder.CreateExtractValue(resultValue, resultOkIndex, "try.ok.payload");
    builder.CreateStore(okPayload, okSlot);
    builder.CreateBr(contBB);

    errBB->insertInto(function);
    builder.SetInsertPoint(errBB);
    llvm::Value* errPayload =
        builder.CreateExtractValue(resultValue, resultErrIndex, "try.err.payload");
    llvm::Type* expectedErrLlvmType = getLLVMTypeFromNode(expectedErrType);
    if(!expectedErrLlvmType)
    {
        reportError(node->line, "operator '?': failed to resolve Err payload type");
        return nullptr;
    }

    if(errPayload->getType() != expectedErrLlvmType)
    {
        if(errPayload->getType()->isIntegerTy() && expectedErrLlvmType->isIntegerTy())
        {
            errPayload = builder.CreateIntCast(errPayload, expectedErrLlvmType, true,
                                               "try.err.cast");
        }
        else if(errPayload->getType()->isIntegerTy() &&
                expectedErrLlvmType->isFloatingPointTy())
        {
            errPayload =
                builder.CreateSIToFP(errPayload, expectedErrLlvmType, "try.err.sitofp");
        }
        else if(errPayload->getType()->isFloatingPointTy() &&
                expectedErrLlvmType->isFloatingPointTy())
        {
            errPayload =
                builder.CreateFPCast(errPayload, expectedErrLlvmType, "try.err.fpcast");
        }
        else if(errPayload->getType()->isPointerTy() &&
                expectedErrLlvmType->isPointerTy())
        {
            errPayload = builder.CreateBitCast(errPayload, expectedErrLlvmType,
                                               "try.err.ptrcast");
        }
        else if(errPayload->getType()->isStructTy() &&
                expectedErrLlvmType->isStructTy() &&
                errPayload->getType() == expectedErrLlvmType)
        {
            // Same struct type, nothing to do.
        }
        else
        {
            reportError(node->line,
                        "operator '?': Err type does not match function return type");
            return nullptr;
        }
    }

    llvm::Value* retResult = llvm::UndefValue::get(expectedRetType);
    // For Result<_, string>, append call-site context on every `?` propagation.
    if(expectedErrType &&
       (expectedErrType->kind == TypeNode::TYPE_STRING ||
        expectedErrType->kind == TypeNode::TYPE_STR8))
    {
        llvm::Type* charPtrTy = getLLVMType(TypeNode::TYPE_STRING);
        llvm::FunctionCallee addContextFn = module->getOrInsertFunction(
            "__mlang_error_add_context",
            llvm::FunctionType::get(charPtrTy, {charPtrTy, charPtrTy}, false));
        std::string fnLabel = function->getName().str();
        size_t mangledPos = fnLabel.rfind("__");
        if(mangledPos != std::string::npos && mangledPos > 0)
            fnLabel = fnLabel.substr(0, mangledPos);
        if(fnLabel == "__mlang_user_main")
            fnLabel = "main";
        std::string tryContext = fnLabel + ":" + std::to_string(node->line);
        llvm::Value* ctxStr = builder.CreateGlobalStringPtr(tryContext, "try.ctx");
        errPayload = builder.CreateCall(addContextFn, {errPayload, ctxStr},
                                        "try.err.withctx");
    }

    retResult = builder.CreateInsertValue(
        retResult, llvm::ConstantInt::getFalse(context),
        static_cast<unsigned>(expectedIsOkIndex), "try.ret.is_ok");
    retResult = builder.CreateInsertValue(
        retResult, llvm::Constant::getNullValue(expectedRetType->getStructElementType(
                       static_cast<unsigned>(expectedOkIndex))),
        static_cast<unsigned>(expectedOkIndex), "try.ret.ok");
    retResult = builder.CreateInsertValue(retResult, errPayload,
                                          static_cast<unsigned>(expectedErrIndex),
                                          "try.ret.err");
    emitAllActiveCleanups();
    builder.CreateRet(retResult);

    contBB->insertInto(function);
    builder.SetInsertPoint(contBB);
    return builder.CreateLoad(okLlvmType, okSlot, "try.ok");
}

void CodeGenerator::generateIfStatement(IfNode* node)
{
    auto incomingMoved = movedVariables;
    auto incomingPointerBorrowTarget = pointerBorrowTarget;
    auto incomingActiveBorrowers = activeBorrowers;

    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
        return;

    // Check that condition is a valid boolean type
    llvm::Type* condType = condValue->getType();
    if(!condType->isIntegerTy() && !condType->isFloatingPointTy())
    {
        std::string typeStr;
        if(condType->isStructTy())
            typeStr = condType->getStructName().str().empty()
                          ? "struct"
                          : condType->getStructName().str();
        else if(condType->isPointerTy())
            typeStr = "pointer";
        else
            typeStr = "non-boolean";

        reportError(node->line,
                    "if condition must be a boolean or numeric type, got '" +
                        typeStr + "'");
        return;
    }

    // Convert condition to boolean if necessary
    if(!condValue->getType()->isIntegerTy(1))
    {
        if(condValue->getType()->isFloatingPointTy())
        {
            // Compare float/double to 0.0
            condValue = builder.CreateFCmpONE(
                condValue, llvm::ConstantFP::get(condValue->getType(), 0.0),
                "ifcond");
        }
        else
        {
            condValue = builder.CreateICmpNE(
                condValue, llvm::ConstantInt::get(condValue->getType(), 0),
                "ifcond");
        }
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* thenBB =
        llvm::BasicBlock::Create(context, "then", function);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context, "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "ifcont");

    builder.CreateCondBr(condValue, thenBB, elseBB);

    // Generate 'then' block
    builder.SetInsertPoint(thenBB);
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    activeBorrowers = incomingActiveBorrowers;
    enterCleanupScope();
    for(auto stmt : node->thenBranch->statements)
    {
        generateStatement(stmt);
    }
    exitCleanupScope();
    auto thenMoved = movedVariables;
    auto thenPointerBorrowTarget = pointerBorrowTarget;
    auto thenActiveBorrowers = activeBorrowers;
    llvm::BasicBlock* thenEnd = builder.GetInsertBlock();
    bool thenFallsThrough = thenEnd && !thenEnd->getTerminator();

    // Only add branch if block doesn't already have a terminator
    if(thenFallsThrough)
    {
        builder.CreateBr(mergeBB);
    }

    // Generate 'else' block
    elseBB->insertInto(function);
    builder.SetInsertPoint(elseBB);
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    activeBorrowers = incomingActiveBorrowers;

    if(node->elseIfBranch)
    {
        // Attach final else branch to the last else-if so it is not skipped.
        IfNode* last = node->elseIfBranch;
        while(last->elseIfBranch)
        {
            last = last->elseIfBranch;
        }
        if(!last->elseBranch)
        {
            last->elseBranch = node->elseBranch;
        }

        // Handle else-if chain
        generateIfStatement(node->elseIfBranch);
    }
    else if(node->elseBranch)
    {
        enterCleanupScope();
        for(auto stmt : node->elseBranch->statements)
        {
            generateStatement(stmt);
        }
        exitCleanupScope();
    }
    auto elseMoved = movedVariables;
    auto elsePointerBorrowTarget = pointerBorrowTarget;
    auto elseActiveBorrowers = activeBorrowers;
    llvm::BasicBlock* elseEnd = builder.GetInsertBlock();
    bool elseFallsThrough = elseEnd && !elseEnd->getTerminator();

    // Only add branch if block doesn't already have a terminator
    if(elseFallsThrough)
    {
        builder.CreateBr(mergeBB);
    }

    // Generate merge block
    mergeBB->insertInto(function);
    builder.SetInsertPoint(mergeBB);

    if(thenFallsThrough || elseFallsThrough)
    {
        std::set<std::string> mergedMoved;
        std::map<std::string, std::string> mergedPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> mergedActiveBorrowers;

        auto mergeOne = [&](const std::set<std::string>& movedState,
                            const std::map<std::string, std::string>& ptrState,
                            const std::map<std::string, std::set<std::string>>&
                                borrowersState)
        {
            mergedMoved.insert(movedState.begin(), movedState.end());
            for(const auto& kv : ptrState)
            {
                if(mergedPointerBorrowTarget.find(kv.first) ==
                   mergedPointerBorrowTarget.end())
                {
                    mergedPointerBorrowTarget[kv.first] = kv.second;
                }
            }
            for(const auto& kv : borrowersState)
            {
                auto& dst = mergedActiveBorrowers[kv.first];
                dst.insert(kv.second.begin(), kv.second.end());
            }
        };

        if(thenFallsThrough)
            mergeOne(thenMoved, thenPointerBorrowTarget, thenActiveBorrowers);
        if(elseFallsThrough)
            mergeOne(elseMoved, elsePointerBorrowTarget, elseActiveBorrowers);

        movedVariables = std::move(mergedMoved);
        pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
        activeBorrowers = std::move(mergedActiveBorrowers);
    }
    else
    {
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
    }
}

void CodeGenerator::generateForStatement(ForNode* node)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    // Check if it's a range expression
    auto* rangeExpr = dynamic_cast<RangeExpressionNode*>(node->iterable);
    if(rangeExpr)
    {
        // Generate start and end values
        llvm::Value* startVal = generateExpression(rangeExpr->start);
        llvm::Value* endVal = generateExpression(rangeExpr->end);

        if(!startVal || !endVal)
        {
            reportError(node->line, "invalid range expression in for loop");
            return;
        }

        // Check that range values are integers
        if(!startVal->getType()->isIntegerTy())
        {
            std::string typeStr;
            if(startVal->getType()->isFloatTy())
                typeStr = "float";
            else if(startVal->getType()->isDoubleTy())
                typeStr = "double";
            else if(startVal->getType()->isStructTy())
                typeStr = startVal->getType()->getStructName().str().empty()
                              ? "struct"
                              : startVal->getType()->getStructName().str();
            else
                typeStr = "non-integer";

            reportError(node->line,
                        "for loop range start must be an integer, got '" +
                            typeStr + "'");
            return;
        }

        if(!endVal->getType()->isIntegerTy())
        {
            std::string typeStr;
            if(endVal->getType()->isFloatTy())
                typeStr = "float";
            else if(endVal->getType()->isDoubleTy())
                typeStr = "double";
            else if(endVal->getType()->isStructTy())
                typeStr = endVal->getType()->getStructName().str().empty()
                              ? "struct"
                              : endVal->getType()->getStructName().str();
            else
                typeStr = "non-integer";

            reportError(node->line,
                        "for loop range end must be an integer, got '" +
                            typeStr + "'");
            return;
        }

        llvm::Type* loopType = llvm::Type::getInt64Ty(context);

        // Extend start and end values to i64 if necessary
        if(startVal->getType() != loopType)
        {
            if(startVal->getType()->isIntegerTy())
            {
                startVal = builder.CreateSExt(startVal, loopType, "start.ext");
            }
        }
        if(endVal->getType() != loopType)
        {
            if(endVal->getType()->isIntegerTy())
            {
                endVal = builder.CreateSExt(endVal, loopType, "end.ext");
            }
        }

        // Create alloca for loop variable
        llvm::AllocaInst* loopVar =
            builder.CreateAlloca(loopType, nullptr, node->varName);
        builder.CreateStore(startVal, loopVar);

        // Add loop variable to named values (it's mutable within the loop)
        llvm::Value* oldVal = namedValues[node->varName];
        TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
        bool hadOldType = false;
        auto typeIt = variableTypes.find(node->varName);
        if(typeIt != variableTypes.end())
        {
            oldType = typeIt->second;
            hadOldType = true;
        }
        bool hadOldMoved = isVariableMoved(node->varName);

        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = TypeNode::TYPE_I64;
        clearMovedVariable(node->varName);

        // Create basic blocks for loop structure
        llvm::BasicBlock* condBB =
            llvm::BasicBlock::Create(context, "for.cond", function);
        llvm::BasicBlock* bodyBB =
            llvm::BasicBlock::Create(context, "for.body");
        llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

        // Push loop blocks for break/continue support
        loopBreakBlocks.push_back(endBB);
        loopContinueBlocks.push_back(incBB);

        // Branch to condition check
        builder.CreateBr(condBB);

        // Condition block: check if loop variable < end (exclusive) or <= end
        // (inclusive)
        builder.SetInsertPoint(condBB);
        llvm::Value* currentVal =
            builder.CreateLoad(loopType, loopVar, node->varName);
        llvm::Value* cond;
        if(rangeExpr->inclusive)
        {
            cond = builder.CreateICmpSLE(currentVal, endVal, "loopcond");
        }
        else
        {
            cond = builder.CreateICmpSLT(currentVal, endVal, "loopcond");
        }
        builder.CreateCondBr(cond, bodyBB, endBB);

        // Body block
        bodyBB->insertInto(function);
        builder.SetInsertPoint(bodyBB);

        if(node->body)
        {
            enterCleanupScope();
            for(auto stmt : node->body->statements)
            {
                generateStatement(stmt);
                if(builder.GetInsertBlock()->getTerminator())
                    break;
            }
            exitCleanupScope();
        }

        if(!builder.GetInsertBlock()->getTerminator())
        {
            builder.CreateBr(incBB);
        }

        // Increment block
        incBB->insertInto(function);
        builder.SetInsertPoint(incBB);
        llvm::Value* nextVal = builder.CreateAdd(
            builder.CreateLoad(loopType, loopVar, ""),
            llvm::ConstantInt::get(context, llvm::APInt(64, 1)), "nextval");
        builder.CreateStore(nextVal, loopVar);
        builder.CreateBr(condBB);

        // End block
        endBB->insertInto(function);
        builder.SetInsertPoint(endBB);

        // Pop loop blocks
        loopBreakBlocks.pop_back();
        loopContinueBlocks.pop_back();

        // Restore old value
        if(oldVal)
            namedValues[node->varName] = oldVal;
        else
            namedValues.erase(node->varName);

        if(hadOldType)
            variableTypes[node->varName] = oldType;
        else
            variableTypes.erase(node->varName);

        if(hadOldMoved)
            movedVariables.insert(node->varName);
        else
            clearMovedVariable(node->varName);
    }
    else if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->iterable))
    {
        // Iterating over a list literal: for x in [1, 2, 3] { ... }
        generateForListLiteralIteration(node, listLit);
    }
    else if(auto* identifier = dynamic_cast<IdentifierNode*>(node->iterable))
    {
        // Check if it's a map variable (iterate over entries by default)
        auto mapIt = mapKeyValueTypes.find(identifier->name);
        if(mapIt != mapKeyValueTypes.end())
        {
            // Create a map entries iterator for direct map iteration
            auto* entriesIter =
                new MapIteratorNode(identifier, MapIteratorNode::ITER_ENTRIES);
            generateForMapIteration(node, entriesIter);
            delete entriesIter;
        }
        else
        {
            // Iterating over a list variable: for x in myList { ... }
            generateForListVariableIteration(node, identifier);
        }
    }
    else if(auto* mapIter = dynamic_cast<MapIteratorNode*>(node->iterable))
    {
        // Iterating over map.keys(), map.values(), or map.entries()
        generateForMapIteration(node, mapIter);
    }
    else
    {
        reportError(
            node->line,
            "for loops support range expressions (start..end), list "
            "iteration, and map iteration (.keys(), .values(), .entries())");
    }
}

void CodeGenerator::generateForListLiteralIteration(ForNode* node,
                                                    ListLiteralNode* listLit)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    if(!listLit->elements || listLit->elements->elements.empty())
    {
        // Empty list, nothing to iterate
        return;
    }

    // Generate all list elements first
    std::vector<llvm::Value*> elementValues;
    llvm::Type* elementType = nullptr;

    for(auto* elem : listLit->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
        {
            reportError(node->line, "failed to generate list element");
            return;
        }
        if(!elementType)
        {
            elementType = val->getType();
        }
        elementValues.push_back(val);
    }

    // Create index variable
    llvm::Type* indexType = llvm::Type::getInt64Ty(context);
    llvm::AllocaInst* indexVar =
        builder.CreateAlloca(indexType, nullptr, "idx");
    builder.CreateStore(llvm::ConstantInt::get(indexType, 0), indexVar);

    // Store list size
    int64_t listSize = static_cast<int64_t>(elementValues.size());

    // Create alloca for loop variable (the element)
    llvm::AllocaInst* loopVar =
        builder.CreateAlloca(elementType, nullptr, node->varName);

    // Save old values
    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = variableTypes.find(node->varName) != variableTypes.end();
    if(hadOldType)
        oldType = variableTypes[node->varName];
    bool hadOldMoved = isVariableMoved(node->varName);

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = TypeNode::TYPE_I64; // Placeholder
    clearMovedVariable(node->varName);

    // Create basic blocks
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(incBB);

    builder.CreateBr(condBB);

    // Condition: index < size
    builder.SetInsertPoint(condBB);
    llvm::Value* currentIdx = builder.CreateLoad(indexType, indexVar, "idx");
    llvm::Value* sizeVal = llvm::ConstantInt::get(indexType, listSize);
    llvm::Value* cond = builder.CreateICmpSLT(currentIdx, sizeVal, "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    // Body: load element and execute body
    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    // Use switch to select the right element based on index
    // For small lists, we can use a series of comparisons
    llvm::Value* elemVal = elementValues[0]; // Default
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        llvm::Value* idxConst = llvm::ConstantInt::get(indexType, i);
        llvm::Value* isThis =
            builder.CreateICmpEQ(currentIdx, idxConst, "iseq");
        elemVal = builder.CreateSelect(isThis, elementValues[i], elemVal,
                                       "selectelem");
    }
    builder.CreateStore(elemVal, loopVar);

    if(node->body)
    {
        enterCleanupScope();
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
        exitCleanupScope();
    }

    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(incBB);
    }

    // Increment
    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextIdx =
        builder.CreateAdd(builder.CreateLoad(indexType, indexVar, ""),
                          llvm::ConstantInt::get(indexType, 1), "nextidx");
    builder.CreateStore(nextIdx, indexVar);
    builder.CreateBr(condBB);

    // End
    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    // Restore
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);

    if(hadOldMoved)
        movedVariables.insert(node->varName);
    else
        clearMovedVariable(node->varName);
}

void CodeGenerator::generateForListVariableIteration(ForNode* node,
                                                     IdentifierNode* listId)
{
    // For iterating over a list variable, we need the list structure
    // Lists are stored as: { i64 size, ptr data }
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    llvm::Value* listPtr = namedValues[listId->name];
    if(!listPtr)
    {
        reportError(node->line, "unknown list variable: " + listId->name);
        return;
    }

    // Get the list struct type info
    auto it = listElementTypes.find(listId->name);
    if(it == listElementTypes.end())
    {
        reportError(node->line,
                    "cannot iterate: unknown element type for list '" +
                        listId->name + "'");
        return;
    }

    TypeNode* elemTypeNode = it->second;
    llvm::Type* elementType = getLLVMType(elemTypeNode->kind);

    // Load list pointer (which points to the list struct)
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType = llvm::PointerType::get(elementType, 0);
#endif

    // List struct: { i64 size, ptr data }
    std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
    llvm::StructType* listStructType =
        llvm::StructType::get(context, listStructTypes);

    // Load the list struct
    llvm::Value* listStruct =
        builder.CreateLoad(listStructType, listPtr, "list");

    // Extract size and data pointer
    llvm::Value* listSize = builder.CreateExtractValue(listStruct, 0, "size");
    llvm::Value* dataPtr = builder.CreateExtractValue(listStruct, 1, "data");

    // Create index variable
    llvm::AllocaInst* indexVar = builder.CreateAlloca(i64Type, nullptr, "idx");
    builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), indexVar);

    // Create loop variable
    llvm::AllocaInst* loopVar =
        builder.CreateAlloca(elementType, nullptr, node->varName);

    // Save old values
    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = variableTypes.find(node->varName) != variableTypes.end();
    if(hadOldType)
        oldType = variableTypes[node->varName];
    bool hadOldMoved = isVariableMoved(node->varName);

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = elemTypeNode->kind;
    clearMovedVariable(node->varName);

    // Create blocks
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(incBB);

    builder.CreateBr(condBB);

    // Condition
    builder.SetInsertPoint(condBB);
    llvm::Value* currentIdx = builder.CreateLoad(i64Type, indexVar, "idx");
    llvm::Value* cond = builder.CreateICmpSLT(currentIdx, listSize, "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    // Body
    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    // Load element at index
    llvm::Value* elemPtr =
        builder.CreateGEP(elementType, dataPtr, currentIdx, "elemptr");
    llvm::Value* elemVal = builder.CreateLoad(elementType, elemPtr, "elem");
    builder.CreateStore(elemVal, loopVar);

    if(node->body)
    {
        enterCleanupScope();
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
        exitCleanupScope();
    }

    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(incBB);
    }

    // Increment
    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextIdx =
        builder.CreateAdd(builder.CreateLoad(i64Type, indexVar, ""),
                          llvm::ConstantInt::get(i64Type, 1), "nextidx");
    builder.CreateStore(nextIdx, indexVar);
    builder.CreateBr(condBB);

    // End
    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    // Restore
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);

    if(hadOldMoved)
        movedVariables.insert(node->varName);
    else
        clearMovedVariable(node->varName);
}

void CodeGenerator::generateForMapIteration(ForNode* node,
                                            MapIteratorNode* mapIter)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    // Get the map variable
    auto* mapId = dynamic_cast<IdentifierNode*>(mapIter->mapExpr);
    if(!mapId)
    {
        reportError(node->line, "map iteration requires a map variable");
        return;
    }

    llvm::Value* mapPtr = namedValues[mapId->name];
    if(!mapPtr)
    {
        reportError(node->line, "unknown map variable: " + mapId->name);
        return;
    }

    // Get map key/value types
    auto it = mapKeyValueTypes.find(mapId->name);
    if(it == mapKeyValueTypes.end())
    {
        reportError(node->line,
                    "cannot iterate: '" + mapId->name + "' is not a map");
        return;
    }

    TypeNode* keyTypeNode = it->second.first;
    TypeNode* valTypeNode = it->second.second;
    llvm::Type* keyType = getLLVMType(keyTypeNode->kind);
    llvm::Type* valueType = getLLVMType(valTypeNode->kind);

    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType = llvm::PointerType::get(keyType, 0);
#endif

    // Map struct: { i64 size, ptr keys, ptr values }
    std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
    llvm::StructType* mapStructType =
        llvm::StructType::get(context, mapStructTypes);

    // Load map struct
    llvm::Value* mapStruct = builder.CreateLoad(mapStructType, mapPtr, "map");
    llvm::Value* mapSize = builder.CreateExtractValue(mapStruct, 0, "size");
    llvm::Value* keysPtr = builder.CreateExtractValue(mapStruct, 1, "keys");
    llvm::Value* valsPtr = builder.CreateExtractValue(mapStruct, 2, "vals");

    // Create index variable
    llvm::AllocaInst* indexVar = builder.CreateAlloca(i64Type, nullptr, "idx");
    builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), indexVar);

    // Determine loop variable type based on iterator kind
    llvm::Type* loopVarType = nullptr;
    llvm::AllocaInst* loopVar = nullptr;
    llvm::AllocaInst* loopVar2 = nullptr; // For entries (key, value pair)

    // Save old values
    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = variableTypes.find(node->varName) != variableTypes.end();
    if(hadOldType)
        oldType = variableTypes[node->varName];
    bool hadOldMoved = isVariableMoved(node->varName);

    switch(mapIter->kind)
    {
    case MapIteratorNode::ITER_KEYS:
        loopVarType = keyType;
        loopVar = builder.CreateAlloca(keyType, nullptr, node->varName);
        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = keyTypeNode->kind;
        clearMovedVariable(node->varName);
        break;

    case MapIteratorNode::ITER_VALUES:
        loopVarType = valueType;
        loopVar = builder.CreateAlloca(valueType, nullptr, node->varName);
        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = valTypeNode->kind;
        clearMovedVariable(node->varName);
        break;

    case MapIteratorNode::ITER_ENTRIES:
        // For entries, we create a tuple (key, value)
        {
            std::vector<llvm::Type*> entryTypes = {keyType, valueType};
            llvm::StructType* entryStructType =
                llvm::StructType::get(context, entryTypes);
            loopVar =
                builder.CreateAlloca(entryStructType, nullptr, node->varName);
            namedValues[node->varName] = loopVar;
            variableTypes[node->varName] = TypeNode::TYPE_TUPLE;
            clearMovedVariable(node->varName);

            // Store element types for tuple access
            std::vector<TypeNode*> elemTypes = {keyTypeNode, valTypeNode};
            tupleElementTypes[node->varName] = elemTypes;
        }
        break;
    }

    // Create basic blocks
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(incBB);

    builder.CreateBr(condBB);

    // Condition
    builder.SetInsertPoint(condBB);
    llvm::Value* currentIdx = builder.CreateLoad(i64Type, indexVar, "idx");
    llvm::Value* cond = builder.CreateICmpSLT(currentIdx, mapSize, "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    // Body
    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    // Load the appropriate value(s) based on iterator kind
    switch(mapIter->kind)
    {
    case MapIteratorNode::ITER_KEYS:
    {
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysPtr, currentIdx, "keyptr");
        llvm::Value* keyVal = builder.CreateLoad(keyType, keyPtr, "key");
        builder.CreateStore(keyVal, loopVar);
    }
    break;

    case MapIteratorNode::ITER_VALUES:
    {
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsPtr, currentIdx, "valptr");
        llvm::Value* valVal = builder.CreateLoad(valueType, valPtr, "val");
        builder.CreateStore(valVal, loopVar);
    }
    break;

    case MapIteratorNode::ITER_ENTRIES:
    {
        // Load both key and value, create tuple
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysPtr, currentIdx, "keyptr");
        llvm::Value* keyVal = builder.CreateLoad(keyType, keyPtr, "key");

        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsPtr, currentIdx, "valptr");
        llvm::Value* valVal = builder.CreateLoad(valueType, valPtr, "val");

        std::vector<llvm::Type*> entryTypes = {keyType, valueType};
        llvm::StructType* entryStructType =
            llvm::StructType::get(context, entryTypes);

        llvm::Value* entryVal = llvm::UndefValue::get(entryStructType);
        entryVal = builder.CreateInsertValue(entryVal, keyVal, 0, "entry.key");
        entryVal = builder.CreateInsertValue(entryVal, valVal, 1, "entry.val");
        builder.CreateStore(entryVal, loopVar);
    }
    break;
    }

    if(node->body)
    {
        enterCleanupScope();
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
        exitCleanupScope();
    }

    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(incBB);
    }

    // Increment
    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextIdx =
        builder.CreateAdd(builder.CreateLoad(i64Type, indexVar, ""),
                          llvm::ConstantInt::get(i64Type, 1), "nextidx");
    builder.CreateStore(nextIdx, indexVar);
    builder.CreateBr(condBB);

    // End
    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    // Restore
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);

    if(hadOldMoved)
        movedVariables.insert(node->varName);
    else
        clearMovedVariable(node->varName);

    // Clean up tuple element types if we added them
    if(mapIter->kind == MapIteratorNode::ITER_ENTRIES)
    {
        tupleElementTypes.erase(node->varName);
    }
}

void CodeGenerator::generateReturnStatement(ReturnNode* node)
{
    llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
    llvm::Type* expectedRetType = currentFunc->getReturnType();

    if(node->expression)
    {
        if(!validateNoEscapingBorrow(node->expression, node->line, "return"))
            return;

        llvm::Value* returnValue = generateExpression(node->expression);
        if(!returnValue)
            return;
        consumeMoveFromExpression(node->expression, node->line, "return");

        llvm::Type* actualType = returnValue->getType();

        // Check if return type matches
        if(expectedRetType->isVoidTy())
        {
            reportError(node->line,
                        "function with void return type cannot return a value");
            return;
        }

        // Try to convert if types don't match
        if(actualType != expectedRetType)
        {
            if(actualType->isIntegerTy() && expectedRetType->isIntegerTy())
            {
                unsigned actualBits = actualType->getIntegerBitWidth();
                unsigned expectedBits = expectedRetType->getIntegerBitWidth();
                if(actualBits > expectedBits)
                {
                    returnValue = builder.CreateTrunc(
                        returnValue, expectedRetType, "rettrunc");
                }
                else if(actualBits < expectedBits)
                {
                    returnValue = builder.CreateSExt(
                        returnValue, expectedRetType, "retsext");
                }
            }
            else if(actualType->isIntegerTy() &&
                    expectedRetType->isFloatingPointTy())
            {
                returnValue = builder.CreateSIToFP(returnValue, expectedRetType,
                                                   "retsitofp");
            }
            else if(actualType->isFloatingPointTy() &&
                    expectedRetType->isIntegerTy())
            {
                returnValue = builder.CreateFPToSI(returnValue, expectedRetType,
                                                   "retfptosi");
            }
            else if(actualType->isFloatingPointTy() &&
                    expectedRetType->isFloatingPointTy())
            {
                returnValue = builder.CreateFPCast(returnValue, expectedRetType,
                                                   "retfpcast");
            }
            else if(!(actualType->isPointerTy() &&
                      expectedRetType->isPointerTy()))
            {
                // Incompatible types
                std::string actualStr, expectedStr;

                if(actualType->isStructTy())
                    actualStr = actualType->getStructName().str().empty()
                                    ? "struct"
                                    : actualType->getStructName().str();
                else if(actualType->isIntegerTy())
                    actualStr =
                        "i" + std::to_string(actualType->getIntegerBitWidth());
                else if(actualType->isFloatTy())
                    actualStr = "float";
                else if(actualType->isDoubleTy())
                    actualStr = "double";
                else if(actualType->isPointerTy())
                    actualStr = "pointer/string";
                else
                    actualStr = "unknown";

                if(expectedRetType->isStructTy())
                    expectedStr = expectedRetType->getStructName().str().empty()
                                      ? "struct"
                                      : expectedRetType->getStructName().str();
                else if(expectedRetType->isIntegerTy())
                    expectedStr =
                        "i" +
                        std::to_string(expectedRetType->getIntegerBitWidth());
                else if(expectedRetType->isFloatTy())
                    expectedStr = "float";
                else if(expectedRetType->isDoubleTy())
                    expectedStr = "double";
                else if(expectedRetType->isPointerTy())
                    expectedStr = "pointer/string";
                else
                    expectedStr = "unknown";

                reportError(node->line, "return type mismatch: expected '" +
                                            expectedStr + "', got '" +
                                            actualStr + "'");
                return;
            }
        }

        emitAllActiveCleanups();
        builder.CreateRet(returnValue);
    }
    else
    {
        // No expression - check if function expects void
        if(!expectedRetType->isVoidTy())
        {
            reportError(node->line, "non-void function must return a value");
            return;
        }
        emitAllActiveCleanups();
        builder.CreateRetVoid();
    }
}

void CodeGenerator::generateBreakStatement(BreakNode* node)
{
    if(loopBreakBlocks.empty())
    {
        reportError(node->line, "'break' statement not within a loop");
        return;
    }
    builder.CreateBr(loopBreakBlocks.back());
}

void CodeGenerator::generateContinueStatement(ContinueNode* node)
{
    if(loopContinueBlocks.empty())
    {
        reportError(node->line, "'continue' statement not within a loop");
        return;
    }
    builder.CreateBr(loopContinueBlocks.back());
}

llvm::Value* CodeGenerator::generateIntLiteral(IntLiteralNode* node)
{
    // Use 64-bit for literals to support large values; truncation happens at
    // assignment
    return llvm::ConstantInt::get(context, llvm::APInt(64, node->value, true));
}

llvm::Value* CodeGenerator::generateBoolLiteral(BoolLiteralNode* node)
{
    return llvm::ConstantInt::get(context, llvm::APInt(1, node->value ? 1 : 0));
}

llvm::Value* CodeGenerator::generateFloatLiteral(FloatLiteralNode* node)
{
    return llvm::ConstantFP::get(context, llvm::APFloat(node->value));
}

llvm::Value* CodeGenerator::generateDoubleLiteral(DoubleLiteralNode* node)
{
    return llvm::ConstantFP::get(context, llvm::APFloat(node->value));
}

llvm::Value* CodeGenerator::generateStringLiteral(StringLiteralNode* node)
{
#if LLVM_VERSION_MAJOR >= 21
    return builder.CreateGlobalString(node->value);
#else
    return builder.CreateGlobalStringPtr(node->value);
#endif
}

llvm::Value* CodeGenerator::generateEnumLiteral(EnumLiteralNode* node)
{
    auto enumIt = enumValues.find(node->enumName);
    if(enumIt == enumValues.end())
    {
        reportError(node->line,
                    "unknown enum: '" + node->enumName + "'");
        return nullptr;
    }
    auto variantIt = enumIt->second.find(node->variantName);
    if(variantIt == enumIt->second.end())
    {
        reportError(node->line,
                    "unknown enum variant: '" + node->variantName + "'");
        return nullptr;
    }
    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context),
                                  variantIt->second, true);
}

llvm::Value* CodeGenerator::generateIdentifier(IdentifierNode* node)
{
    if(globalNamedValues.find(node->name) == globalNamedValues.end() &&
       isVariableMoved(node->name))
    {
        reportError(node->line, "use of moved value: '" + node->name + "'");
        return nullptr;
    }

    llvm::Value* value = namedValues[node->name];
    if(!value)
    {
        reportError(node->line, "unknown variable: '" + node->name + "'");
        return nullptr;
    }

    // If it's an alloca, load the value
    if(llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
    {
        return builder.CreateLoad(alloca->getAllocatedType(), alloca,
                                  node->name);
    }
    if(llvm::GlobalVariable* global = llvm::dyn_cast<llvm::GlobalVariable>(value))
    {
        return builder.CreateLoad(global->getValueType(), global, node->name);
    }

    return value;
}

void CodeGenerator::generateStructDefinition(StructDefNode* node)
{
    std::vector<llvm::Type*> memberTypes;
    std::vector<std::pair<std::string, TypeNode*>> members;

    // If this struct has a base, include base struct's fields first
    if(!node->baseName.empty())
    {
        auto baseMemIt = structMembers.find(node->baseName);
        if(baseMemIt != structMembers.end())
        {
            for(const auto& baseMember : baseMemIt->second)
            {
                memberTypes.push_back(getLLVMTypeFromNode(baseMember.second));
                members.push_back(baseMember);
            }
        }
        else
        {
            reportError(node->line,
                        "base struct '" + node->baseName + "' not found");
        }
    }

    // Add this struct's own members
    for(auto member : node->members->members)
    {
        memberTypes.push_back(getLLVMTypeFromNode(member->type));
        members.push_back({member->name, member->type});
    }

    llvm::StructType* structType =
        llvm::StructType::create(context, memberTypes, node->name);
    structTypes[node->name] = structType;
    structMembers[node->name] = members;
    if(node->deriveDebug)
        debugStructs.insert(node->name);

    // Track base name for inheritance lookups
    if(!node->baseName.empty())
    {
        structBases[node->name] = node->baseName;
    }
}

void CodeGenerator::generateEnumDefinition(EnumDefNode* node)
{
    if(!node)
        return;

    if(enumValues.find(node->name) != enumValues.end())
    {
        reportError(node->line, "duplicate enum: '" + node->name + "'");
        return;
    }

    std::map<std::string, int64_t> variants;
    int64_t nextValue = 0;
    if(node->variants)
    {
        for(auto* variant : node->variants->variants)
        {
            if(!variant)
                continue;
            if(variants.find(variant->name) != variants.end())
            {
                reportError(node->line,
                            "duplicate enum variant: '" + variant->name + "'");
                return;
            }
            variants[variant->name] = nextValue++;
        }
    }

    enumValues[node->name] = variants;
}

llvm::StructType* CodeGenerator::getStructType(const std::string& name)
{
    auto it = structTypes.find(name);
    if(it != structTypes.end())
    {
        return llvm::cast<llvm::StructType>(it->second);
    }
    return nullptr;
}

std::string CodeGenerator::typeMangle(TypeNode* typeNode) const
{
    if(!typeNode)
        return "void";

    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(typeNode))
        return "ptr_" + typeMangle(ptrType->elementType);

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
        return "int";
    case TypeNode::TYPE_FLOAT:
        return "float";
    case TypeNode::TYPE_DOUBLE:
        return "double";
    case TypeNode::TYPE_STRING:
        return "string";
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
    default:
        return "unknown";
    }
}

std::string CodeGenerator::functionSignatureKey(FunctionDefNode* node) const
{
    std::string key = node->name + "(";
    if(node->parameters)
    {
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            if(i > 0)
                key += ",";
            key += typeMangle(node->parameters->parameters[i]->type);
        }
        if(node->parameters->isVarArg)
        {
            if(!node->parameters->parameters.empty())
                key += ",";
            key += "...";
        }
    }
    key += ")";
    return key;
}

std::string CodeGenerator::functionSymbolName(FunctionDefNode* node) const
{
    if(node->isExtern)
        return node->name;
    if(node->name == "main")
        return "main";

    std::string suffix;
    if(node->parameters && !node->parameters->parameters.empty())
    {
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            if(i > 0)
                suffix += "_";
            suffix += typeMangle(node->parameters->parameters[i]->type);
        }
    }
    else
    {
        suffix = "void";
    }
    if(node->parameters && node->parameters->isVarArg)
    {
        if(!suffix.empty())
            suffix += "_";
        suffix += "vararg";
    }
    return node->name + "__" + suffix;
}

void CodeGenerator::registerFunctionOverload(FunctionDefNode* node,
                                              llvm::Function* function)
{
    if(!node || !function)
        return;

    std::string signatureKey = functionSignatureKey(node);
    auto& overloads = functionOverloads[node->name];
    for(const auto& info : overloads)
    {
        if(info.signatureKey == signatureKey)
        {
            reportError(node->line,
                        "duplicate function overload: '" + signatureKey + "'");
            return;
        }
    }

    FunctionOverloadInfo info{
        node, function, signatureKey, function->getName().str(),
        node->parameters ? node->parameters->isVarArg : false, node->isPublic,
        node->sourceModule};
    overloads.push_back(info);

    functionVisibility[signatureKey] =
        std::make_pair(node->isPublic, node->sourceModule);
}

bool CodeGenerator::isOverloadVisible(const FunctionOverloadInfo& info) const
{
    if(info.sourceModule.empty())
        return true;
    if(info.sourceModule == currentModule)
        return true;
    return info.isPublic;
}

bool CodeGenerator::canConvertType(llvm::Type* actualType,
                                   llvm::Type* expectedType, int& cost) const
{
    if(actualType == expectedType)
    {
        cost = 0;
        return true;
    }
    if(actualType->isPointerTy() && expectedType->isPointerTy())
    {
        cost = 0;
        return true;
    }
    if(actualType->isIntegerTy() && expectedType->isIntegerTy())
    {
        cost = 1;
        return true;
    }
    if(actualType->isFloatingPointTy() && expectedType->isFloatingPointTy())
    {
        cost = 1;
        return true;
    }
    if(actualType->isIntegerTy() && expectedType->isFloatingPointTy())
    {
        cost = 2;
        return true;
    }
    if(actualType->isFloatingPointTy() && expectedType->isIntegerTy())
    {
        cost = 2;
        return true;
    }
    return false;
}

void CodeGenerator::generateLetDeclaration(LetDeclNode* node)
{
    recordScopedPointerVariable(node->name);
    llvm::Value* initValue = generateExpression(node->expression);
    if(!initValue)
        return;
    consumeMoveFromExpression(node->expression, node->line,
                              "initializing '" + node->name + "'");
    clearMovedVariable(node->name);
    clearPointerBorrow(node->name);
    recordVariableScopeDepth(node->name);

    if(!node->type)
    {
        llvm::AllocaInst* alloca =
            builder.CreateAlloca(initValue->getType(), nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;

        auto infer_kind_from_expr = [&](ExpressionNode* expr) -> TypeNode::TypeKind
        {
            if(dynamic_cast<BoolLiteralNode*>(expr))
                return TypeNode::TYPE_BOOL;
            if(dynamic_cast<FloatLiteralNode*>(expr))
                return TypeNode::TYPE_FLOAT;
            if(dynamic_cast<DoubleLiteralNode*>(expr))
                return TypeNode::TYPE_DOUBLE;
            if(dynamic_cast<StringLiteralNode*>(expr))
                return TypeNode::TYPE_STRING;
            if(dynamic_cast<ListLiteralNode*>(expr))
                return TypeNode::TYPE_LIST;
            if(dynamic_cast<MapLiteralNode*>(expr))
                return TypeNode::TYPE_MAP;
            if(dynamic_cast<TupleLiteralNode*>(expr))
                return TypeNode::TYPE_TUPLE;
            if(dynamic_cast<StructLiteralNode*>(expr))
                return TypeNode::TYPE_STRUCT;
            if(auto* id = dynamic_cast<IdentifierNode*>(expr))
            {
                auto it = variableTypes.find(id->name);
                if(it != variableTypes.end())
                    return it->second;
            }
            if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
            {
                if(call->name == "String::new" ||
                   call->name == "String::with_capacity")
                    return TypeNode::TYPE_STRING;
            }

            llvm::Type* t = initValue->getType();
            if(t->isIntegerTy(1))
                return TypeNode::TYPE_BOOL;
            if(t->isFloatingPointTy())
            {
                if(t->isFloatTy())
                    return TypeNode::TYPE_FLOAT;
                if(t->isDoubleTy())
                    return TypeNode::TYPE_DOUBLE;
            }
            if(t->isPointerTy())
                return TypeNode::TYPE_PTR;
            if(t->isStructTy())
                return TypeNode::TYPE_STRUCT;
            return TypeNode::TYPE_INT;
        };

        TypeNode::TypeKind inferredKind = infer_kind_from_expr(node->expression);
        variableTypes[node->name] = inferredKind;

        if(auto* id = dynamic_cast<IdentifierNode*>(node->expression))
        {
            auto sit = structVariableTypes.find(id->name);
            if(sit != structVariableTypes.end())
                structVariableTypes[node->name] = sit->second;
                registerStructCleanupIfNeeded(node->name, sit->second);
            auto lit = listElementTypes.find(id->name);
            if(lit != listElementTypes.end())
                listElementTypes[node->name] = lit->second;
            auto mit = mapKeyValueTypes.find(id->name);
            if(mit != mapKeyValueTypes.end())
                mapKeyValueTypes[node->name] = mit->second;
            auto tit = tupleElementTypes.find(id->name);
            if(tit != tupleElementTypes.end())
                tupleElementTypes[node->name] = tit->second;
            auto pit = pointerElementTypes.find(id->name);
            if(pit != pointerElementTypes.end())
                pointerElementTypes[node->name] = pit->second;
        }

        if(auto* call = dynamic_cast<FunctionCallNode*>(node->expression))
        {
            if(call->name == "String::new" ||
               call->name == "String::with_capacity")
            {
                variableTypes[node->name] = TypeNode::TYPE_STRING;
            }
        }

        if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_LIST;
            TypeNode::TypeKind elemKind = TypeNode::TYPE_INT;
            if(listLit->elements && !listLit->elements->elements.empty())
                elemKind = infer_kind_from_expr(listLit->elements->elements[0]);
            listElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(elemKind));
        }
        else if(auto* mapLit = dynamic_cast<MapLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_MAP;
            TypeNode::TypeKind keyKind = TypeNode::TYPE_INT;
            TypeNode::TypeKind valKind = TypeNode::TYPE_INT;
            if(mapLit->entries && !mapLit->entries->entries.empty())
            {
                auto* first = mapLit->entries->entries[0];
                keyKind = infer_kind_from_expr(first->key);
                valKind = infer_kind_from_expr(first->value);
            }
            mapKeyValueTypes[node->name] = std::make_pair(
                static_cast<TypeNode*>(create_type_node(keyKind)),
                static_cast<TypeNode*>(create_type_node(valKind)));
        }
        else if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_TUPLE;
            std::vector<TypeNode*> elems;
            if(tupleLit->elements)
            {
                for(auto* e : tupleLit->elements->elements)
                {
                    elems.push_back(
                        static_cast<TypeNode*>(create_type_node(infer_kind_from_expr(e))));
                }
            }
            tupleElementTypes[node->name] = elems;
        }
        else if(auto* structLit = dynamic_cast<StructLiteralNode*>(node->expression))
        {
            variableTypes[node->name] = TypeNode::TYPE_STRUCT;
            structVariableTypes[node->name] = structLit->structName;
            registerStructCleanupIfNeeded(node->name, structLit->structName);
        }
        else if(variableTypes[node->name] == TypeNode::TYPE_PTR)
        {
            pointerElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(TypeNode::TYPE_I8));
        }
        registerPointerBorrow(node->name, node->expression, node->line);

        constantVariables.insert(node->name);
        return;
    }

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(node->type))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        llvm::Type* structType = getStructType(mangledName);
        if(!structType)
        {
            reportError(node->line, "failed to monomorphize struct: " +
                                        genStructRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = mangledName;
        registerStructCleanupIfNeeded(node->name, mangledName);
        constantVariables.insert(node->name);
        return;
    }

    // Handle generic list type
    if(auto* genListType = dynamic_cast<GenericListTypeNode*>(node->type))
    {
        // Store element type for iteration
        listElementTypes[node->name] = genListType->elementType;

        // List struct type: { i64, ptr }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMType(genListType->elementType->kind), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(listStructType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_LIST;
        constantVariables.insert(node->name);
        return;
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(node->type))
    {
        // Store key/value types
        mapKeyValueTypes[node->name] =
            std::make_pair(mapType->keyType, mapType->valueType);

        // Map struct type: { i64, ptr, ptr }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(mapStructType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_MAP;
        constantVariables.insert(node->name);
        return;
    }

    // Handle pointer type
    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(node->type))
    {
        pointerElementTypes[node->name] = ptrType->elementType;

        llvm::Type* llvmPtrType = getLLVMTypeFromNode(ptrType);
        if(!llvmPtrType)
            return;

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(llvmPtrType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_PTR;
        registerPointerBorrow(node->name, node->expression, node->line);
        constantVariables.insert(node->name);
        return;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(node->type))
    {
        // Store element types for tuple access
        std::vector<TypeNode*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            elemTypes.push_back(t);
        }
        tupleElementTypes[node->name] = elemTypes;

        // Create LLVM struct type for tuple based on declared types
        std::vector<llvm::Type*> tupleTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
            {
                reportError(node->line, "invalid type in tuple");
                return;
            }
            tupleTypes.push_back(elemType);
        }
        llvm::StructType* tupleStructType =
            llvm::StructType::get(context, tupleTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(tupleStructType, nullptr, node->name);

        // Convert tuple literal elements to match declared types
        if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->expression))
        {
            // Build tuple with proper type conversions
            llvm::Value* tupleVal = llvm::UndefValue::get(tupleStructType);

            for(size_t i = 0; i < tupleLit->elements->elements.size() &&
                              i < tupleType->elementTypes->types.size();
                ++i)
            {
                llvm::Value* elemVal =
                    generateExpression(tupleLit->elements->elements[i]);
                if(!elemVal)
                    return;

                llvm::Type* targetElemType = tupleTypes[i];
                llvm::Type* sourceElemType = elemVal->getType();

                // Convert if needed (only for primitive types)
                if(sourceElemType != targetElemType)
                {
                    if(sourceElemType->isIntegerTy() &&
                       targetElemType->isIntegerTy())
                    {
                        unsigned srcBits = sourceElemType->getIntegerBitWidth();
                        unsigned dstBits = targetElemType->getIntegerBitWidth();
                        if(srcBits > dstBits)
                        {
                            elemVal = builder.CreateTrunc(
                                elemVal, targetElemType, "trunc");
                        }
                        else if(srcBits < dstBits)
                        {
                            elemVal = builder.CreateSExt(
                                elemVal, targetElemType, "sext");
                        }
                    }
                    else if(sourceElemType->isIntegerTy() &&
                            targetElemType->isFloatingPointTy())
                    {
                        elemVal = builder.CreateSIToFP(elemVal, targetElemType,
                                                       "sitofp");
                    }
                    else if(sourceElemType->isFloatingPointTy() &&
                            targetElemType->isIntegerTy())
                    {
                        elemVal = builder.CreateFPToSI(elemVal, targetElemType,
                                                       "fptosi");
                    }
                    else if(sourceElemType->isFloatingPointTy() &&
                            targetElemType->isFloatingPointTy())
                    {
                        elemVal = builder.CreateFPCast(elemVal, targetElemType,
                                                       "fpcast");
                    }
                    // For struct types and other complex types, no conversion
                    // needed if types match
                }

                tupleVal = builder.CreateInsertValue(
                    tupleVal, elemVal, static_cast<unsigned>(i), "tuple.elem");
            }

            builder.CreateStore(tupleVal, alloca);
        }
        else
        {
            // Not a literal, just store (may fail if types mismatch)
            builder.CreateStore(initValue, alloca);
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_TUPLE;
        constantVariables.insert(node->name);
        return;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(node->type))
    {
        auto enumIt = enumValues.find(structRef->structName);
        if(enumIt != enumValues.end())
        {
            llvm::Type* targetType = llvm::Type::getInt32Ty(context);
            llvm::AllocaInst* alloca =
                builder.CreateAlloca(targetType, nullptr, node->name);

            llvm::Value* initValue = nullptr;
            if(node->expression)
            {
                initValue = generateExpression(node->expression);
            }
            if(!initValue)
            {
                initValue = llvm::ConstantInt::get(targetType, 0, true);
            }

            if(initValue->getType() != targetType)
            {
                if(initValue->getType()->isIntegerTy())
                {
                    initValue = builder.CreateSExt(initValue, targetType,
                                                   "enum.sext");
                }
                else
                {
                    reportError(node->line,
                                "enum initializer must be integer");
                    return;
                }
            }

            builder.CreateStore(initValue, alloca);
            namedValues[node->name] = alloca;
            variableTypes[node->name] = TypeNode::TYPE_INT;
            constantVariables.insert(node->name);
            return;
        }

        llvm::Type* structType = getStructType(structRef->structName);
        if(!structType)
        {
            reportError(node->line,
                        "unknown struct type: " + structRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = structRef->structName;
        registerStructCleanupIfNeeded(node->name, structRef->structName);
        constantVariables.insert(node->name);
        return;
    }

    llvm::Type* targetType = getLLVMType(node->type->kind);
    llvm::AllocaInst* alloca =
        builder.CreateAlloca(targetType, nullptr, node->name);

    // Convert init value to target type if necessary
    llvm::Type* initType = initValue->getType();
    if(initType != targetType)
    {
        if(initType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned initBits = initType->getIntegerBitWidth();
            unsigned targetBits = targetType->getIntegerBitWidth();
            if(initBits > targetBits)
            {
                // Truncate (e.g., i64 -> i8)
                initValue = builder.CreateTrunc(initValue, targetType, "trunc");
            }
            else if(initBits < targetBits)
            {
                // Extend - use ZExt for unsigned target, SExt for signed
                if(isUnsignedType(node->type->kind))
                {
                    initValue =
                        builder.CreateZExt(initValue, targetType, "zext");
                }
                else
                {
                    initValue =
                        builder.CreateSExt(initValue, targetType, "sext");
                }
            }
        }
        else if(initType->isIntegerTy() && targetType->isFloatingPointTy())
        {
            initValue = builder.CreateSIToFP(initValue, targetType, "sitofp");
        }
        else if(initType->isFloatingPointTy() && targetType->isIntegerTy())
        {
            initValue = builder.CreateFPToSI(initValue, targetType, "fptosi");
        }
        else if(initType->isFloatingPointTy() &&
                targetType->isFloatingPointTy())
        {
            initValue = builder.CreateFPCast(initValue, targetType, "fpcast");
        }
    }

    builder.CreateStore(initValue, alloca);
    namedValues[node->name] = alloca;
    variableTypes[node->name] = node->type->kind;

    // Mark this variable as constant (declared with 'let')
    constantVariables.insert(node->name);
}

void CodeGenerator::generateVarDeclaration(VarDeclNode* node)
{
    recordScopedPointerVariable(node->name);
    clearPointerBorrow(node->name);
    if(node->initExpr)
    {
        consumeMoveFromExpression(node->initExpr, node->line,
                                  "initializing '" + node->name + "'");
    }
    clearMovedVariable(node->name);
    if(node->isStaticStorage || node->isGlobalStorage)
    {
        std::string storageName = node->name;
        if(node->isStaticStorage)
        {
            llvm::Function* fn = builder.GetInsertBlock()
                                     ? builder.GetInsertBlock()->getParent()
                                     : nullptr;
            const std::string owner = fn ? fn->getName().str() : "global";
            storageName = "__mlang_static_" + owner + "_" + node->name;
        }
        else
        {
            storageName = "__mlang_global_" + node->name;
        }

        TypeNode::TypeKind kind = TypeNode::TYPE_INT;
        llvm::Type* targetType = nullptr;
        std::string structTypeName;
        if(node->type)
        {
            kind = node->type->kind;
            targetType = getLLVMTypeFromNode(node->type);
            if(auto* sr = dynamic_cast<StructTypeRefNode*>(node->type))
            {
                kind = TypeNode::TYPE_STRUCT;
                structTypeName = sr->structName;
            }
            else if(auto* gsr =
                        dynamic_cast<GenericStructTypeRefNode*>(node->type))
            {
                kind = TypeNode::TYPE_STRUCT;
                structTypeName = getOrCreateMonomorphizedStruct(
                    gsr->structName, gsr->typeArgs);
            }
        }

        if(!targetType)
        {
            if(!node->initExpr)
            {
                reportError(node->line,
                            "static/global var declaration without type requires initializer");
                return;
            }
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(!initValue)
                return;
            targetType = initValue->getType();
            if(targetType->isIntegerTy(1))
                kind = TypeNode::TYPE_BOOL;
            else if(targetType->isFloatTy())
                kind = TypeNode::TYPE_FLOAT;
            else if(targetType->isDoubleTy())
                kind = TypeNode::TYPE_DOUBLE;
            else if(targetType->isPointerTy())
                kind = TypeNode::TYPE_PTR;
            else
                kind = TypeNode::TYPE_INT;
        }

        auto* gv = module->getGlobalVariable(storageName);
        if(!gv)
        {
            gv = new llvm::GlobalVariable(*module, targetType, false,
                                          llvm::GlobalValue::InternalLinkage,
                                          llvm::Constant::getNullValue(targetType),
                                          storageName);
        }

        if(node->initExpr)
        {
            if((kind == TypeNode::TYPE_PTR || targetType->isPointerTy()) &&
               !validateNoEscapingBorrow(
                   node->initExpr, node->line,
                   "store in global/static '" + node->name + "'"))
            {
                return;
            }

            if(node->isStaticStorage)
            {
                std::string guardName = storageName + "__inited";
                auto* guard = module->getGlobalVariable(guardName);
                if(!guard)
                {
                    guard = new llvm::GlobalVariable(
                        *module, llvm::Type::getInt1Ty(context), false,
                        llvm::GlobalValue::InternalLinkage,
                        llvm::ConstantInt::getFalse(context), guardName);
                }

                llvm::Function* fn = builder.GetInsertBlock()->getParent();
                auto* initBB = llvm::BasicBlock::Create(context, "static.init", fn);
                auto* contBB = llvm::BasicBlock::Create(context, "static.cont", fn);
                llvm::Value* isInit =
                    builder.CreateLoad(guard->getValueType(), guard, "static.inited");
                builder.CreateCondBr(isInit, contBB, initBB);

                builder.SetInsertPoint(initBB);
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(!initValue)
                    return;
                if(initValue->getType() != targetType)
                {
                    if(initValue->getType()->isIntegerTy() && targetType->isIntegerTy())
                        initValue = builder.CreateSExtOrTrunc(initValue, targetType, "static.cast");
                    else if(initValue->getType()->isIntegerTy() && targetType->isFloatingPointTy())
                        initValue = builder.CreateSIToFP(initValue, targetType, "static.sitofp");
                    else if(initValue->getType()->isFloatingPointTy() && targetType->isIntegerTy())
                        initValue = builder.CreateFPToSI(initValue, targetType, "static.fptosi");
                    else if(initValue->getType()->isFloatingPointTy() && targetType->isFloatingPointTy())
                        initValue = builder.CreateFPCast(initValue, targetType, "static.fpcast");
                    else
                    {
                        reportError(node->line, "type mismatch in static initializer for '" + node->name + "'");
                        return;
                    }
                }
                builder.CreateStore(initValue, gv);
                builder.CreateStore(llvm::ConstantInt::getTrue(context), guard);
                builder.CreateBr(contBB);
                builder.SetInsertPoint(contBB);
            }
            else if(auto* cinit = llvm::dyn_cast<llvm::Constant>(generateExpression(node->initExpr)))
            {
                gv->setInitializer(cinit);
            }
            else
            {
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(!initValue)
                    return;
                if(initValue->getType() != targetType)
                {
                    if(initValue->getType()->isIntegerTy() && targetType->isIntegerTy())
                        initValue = builder.CreateSExtOrTrunc(initValue, targetType, "global.cast");
                    else if(initValue->getType()->isIntegerTy() && targetType->isFloatingPointTy())
                        initValue = builder.CreateSIToFP(initValue, targetType, "global.sitofp");
                    else if(initValue->getType()->isFloatingPointTy() && targetType->isIntegerTy())
                        initValue = builder.CreateFPToSI(initValue, targetType, "global.fptosi");
                    else if(initValue->getType()->isFloatingPointTy() && targetType->isFloatingPointTy())
                        initValue = builder.CreateFPCast(initValue, targetType, "global.fpcast");
                    else
                    {
                        reportError(node->line, "type mismatch in global initializer for '" + node->name + "'");
                        return;
                    }
                }
                builder.CreateStore(initValue, gv);
            }
        }

        namedValues[node->name] = gv;
        variableTypes[node->name] = kind;
        if(!structTypeName.empty())
            structVariableTypes[node->name] = structTypeName;
        if(node->isGlobalStorage)
        {
            globalNamedValues[node->name] = gv;
            globalVariableTypes[node->name] = kind;
            if(!structTypeName.empty())
                globalStructVariableTypes[node->name] = structTypeName;
        }
        return;
    }

    recordVariableScopeDepth(node->name);

    if(!node->type)
    {
        if(!node->initExpr)
        {
            reportError(node->line,
                        "var declaration without type requires initializer");
            return;
        }

        llvm::Value* initValue = generateExpression(node->initExpr);
        if(!initValue)
            return;

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(initValue->getType(), nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;

        auto infer_kind_from_expr = [&](ExpressionNode* expr) -> TypeNode::TypeKind
        {
            if(dynamic_cast<BoolLiteralNode*>(expr))
                return TypeNode::TYPE_BOOL;
            if(dynamic_cast<FloatLiteralNode*>(expr))
                return TypeNode::TYPE_FLOAT;
            if(dynamic_cast<DoubleLiteralNode*>(expr))
                return TypeNode::TYPE_DOUBLE;
            if(dynamic_cast<StringLiteralNode*>(expr))
                return TypeNode::TYPE_STRING;
            if(dynamic_cast<ListLiteralNode*>(expr))
                return TypeNode::TYPE_LIST;
            if(dynamic_cast<MapLiteralNode*>(expr))
                return TypeNode::TYPE_MAP;
            if(dynamic_cast<TupleLiteralNode*>(expr))
                return TypeNode::TYPE_TUPLE;
            if(dynamic_cast<StructLiteralNode*>(expr))
                return TypeNode::TYPE_STRUCT;
            if(auto* id = dynamic_cast<IdentifierNode*>(expr))
            {
                auto it = variableTypes.find(id->name);
                if(it != variableTypes.end())
                    return it->second;
            }
            if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
            {
                if(call->name == "String::new" ||
                   call->name == "String::with_capacity")
                    return TypeNode::TYPE_STRING;
            }

            llvm::Type* t = initValue->getType();
            if(t->isIntegerTy(1))
                return TypeNode::TYPE_BOOL;
            if(t->isFloatingPointTy())
            {
                if(t->isFloatTy())
                    return TypeNode::TYPE_FLOAT;
                if(t->isDoubleTy())
                    return TypeNode::TYPE_DOUBLE;
            }
            if(t->isPointerTy())
                return TypeNode::TYPE_PTR;
            if(t->isStructTy())
                return TypeNode::TYPE_STRUCT;
            return TypeNode::TYPE_INT;
        };

        TypeNode::TypeKind inferredKind = infer_kind_from_expr(node->initExpr);
        variableTypes[node->name] = inferredKind;

        if(auto* id = dynamic_cast<IdentifierNode*>(node->initExpr))
        {
            auto sit = structVariableTypes.find(id->name);
            if(sit != structVariableTypes.end())
                structVariableTypes[node->name] = sit->second;
                registerStructCleanupIfNeeded(node->name, sit->second);
            auto lit = listElementTypes.find(id->name);
            if(lit != listElementTypes.end())
                listElementTypes[node->name] = lit->second;
            auto mit = mapKeyValueTypes.find(id->name);
            if(mit != mapKeyValueTypes.end())
                mapKeyValueTypes[node->name] = mit->second;
            auto tit = tupleElementTypes.find(id->name);
            if(tit != tupleElementTypes.end())
                tupleElementTypes[node->name] = tit->second;
            auto pit = pointerElementTypes.find(id->name);
            if(pit != pointerElementTypes.end())
                pointerElementTypes[node->name] = pit->second;
        }

        if(auto* call = dynamic_cast<FunctionCallNode*>(node->initExpr))
        {
            if(call->name == "String::new" ||
               call->name == "String::with_capacity")
            {
                variableTypes[node->name] = TypeNode::TYPE_STRING;
            }
        }

        if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_LIST;
            TypeNode::TypeKind elemKind = TypeNode::TYPE_INT;
            if(listLit->elements && !listLit->elements->elements.empty())
                elemKind = infer_kind_from_expr(listLit->elements->elements[0]);
            listElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(elemKind));
        }
        else if(auto* mapLit = dynamic_cast<MapLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_MAP;
            TypeNode::TypeKind keyKind = TypeNode::TYPE_INT;
            TypeNode::TypeKind valKind = TypeNode::TYPE_INT;
            if(mapLit->entries && !mapLit->entries->entries.empty())
            {
                auto* first = mapLit->entries->entries[0];
                keyKind = infer_kind_from_expr(first->key);
                valKind = infer_kind_from_expr(first->value);
            }
            mapKeyValueTypes[node->name] = std::make_pair(
                static_cast<TypeNode*>(create_type_node(keyKind)),
                static_cast<TypeNode*>(create_type_node(valKind)));
        }
        else if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_TUPLE;
            std::vector<TypeNode*> elems;
            if(tupleLit->elements)
            {
                for(auto* e : tupleLit->elements->elements)
                {
                    elems.push_back(
                        static_cast<TypeNode*>(create_type_node(infer_kind_from_expr(e))));
                }
            }
            tupleElementTypes[node->name] = elems;
        }
        else if(auto* structLit = dynamic_cast<StructLiteralNode*>(node->initExpr))
        {
            variableTypes[node->name] = TypeNode::TYPE_STRUCT;
            structVariableTypes[node->name] = structLit->structName;
            registerStructCleanupIfNeeded(node->name, structLit->structName);
        }
        else if(variableTypes[node->name] == TypeNode::TYPE_PTR)
        {
            pointerElementTypes[node->name] =
                static_cast<TypeNode*>(create_type_node(TypeNode::TYPE_I8));
        }
        registerPointerBorrow(node->name, node->initExpr, node->line);

        return;
    }

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(node->type))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        llvm::Type* structType = getStructType(mangledName);
        if(!structType)
        {
            reportError(node->line, "failed to monomorphize struct: " +
                                        genStructRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = mangledName;
        registerStructCleanupIfNeeded(node->name, mangledName);
        return;
    }

    // Handle generic list type
    if(auto* genListType = dynamic_cast<GenericListTypeNode*>(node->type))
    {
        listElementTypes[node->name] = genListType->elementType;

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMType(genListType->elementType->kind), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(listStructType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_LIST;
        return;
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(node->type))
    {
        mapKeyValueTypes[node->name] =
            std::make_pair(mapType->keyType, mapType->valueType);

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(mapStructType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_MAP;
        return;
    }

    // Handle pointer type
    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(node->type))
    {
        pointerElementTypes[node->name] = ptrType->elementType;

        llvm::Type* llvmPtrType = getLLVMTypeFromNode(ptrType);
        if(!llvmPtrType)
            return;

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(llvmPtrType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_PTR;
        registerPointerBorrow(node->name, node->initExpr, node->line);
        return;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(node->type))
    {
        // Store element types for tuple access
        std::vector<TypeNode*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            elemTypes.push_back(t);
        }
        tupleElementTypes[node->name] = elemTypes;

        // Create LLVM struct type for tuple based on declared types
        std::vector<llvm::Type*> tupleTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
            {
                reportError(node->line, "invalid type in tuple");
                return;
            }
            tupleTypes.push_back(elemType);
        }
        llvm::StructType* tupleStructType =
            llvm::StructType::get(context, tupleTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(tupleStructType, nullptr, node->name);

        if(node->initExpr)
        {
            // Convert tuple literal elements to match declared types
            if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->initExpr))
            {
                // Build tuple with proper type conversions
                llvm::Value* tupleVal = llvm::UndefValue::get(tupleStructType);

                for(size_t i = 0; i < tupleLit->elements->elements.size() &&
                                  i < tupleType->elementTypes->types.size();
                    ++i)
                {
                    llvm::Value* elemVal =
                        generateExpression(tupleLit->elements->elements[i]);
                    if(!elemVal)
                        return;

                    llvm::Type* targetElemType = tupleTypes[i];
                    llvm::Type* sourceElemType = elemVal->getType();

                    // Convert if needed (only for primitive types)
                    if(sourceElemType != targetElemType)
                    {
                        if(sourceElemType->isIntegerTy() &&
                           targetElemType->isIntegerTy())
                        {
                            unsigned srcBits =
                                sourceElemType->getIntegerBitWidth();
                            unsigned dstBits =
                                targetElemType->getIntegerBitWidth();
                            if(srcBits > dstBits)
                            {
                                elemVal = builder.CreateTrunc(
                                    elemVal, targetElemType, "trunc");
                            }
                            else if(srcBits < dstBits)
                            {
                                elemVal = builder.CreateSExt(
                                    elemVal, targetElemType, "sext");
                            }
                        }
                        else if(sourceElemType->isIntegerTy() &&
                                targetElemType->isFloatingPointTy())
                        {
                            elemVal = builder.CreateSIToFP(
                                elemVal, targetElemType, "sitofp");
                        }
                        else if(sourceElemType->isFloatingPointTy() &&
                                targetElemType->isIntegerTy())
                        {
                            elemVal = builder.CreateFPToSI(
                                elemVal, targetElemType, "fptosi");
                        }
                        else if(sourceElemType->isFloatingPointTy() &&
                                targetElemType->isFloatingPointTy())
                        {
                            elemVal = builder.CreateFPCast(
                                elemVal, targetElemType, "fpcast");
                        }
                        // For struct types and other complex types, no
                        // conversion needed
                    }

                    tupleVal = builder.CreateInsertValue(
                        tupleVal, elemVal, static_cast<unsigned>(i),
                        "tuple.elem");
                }

                builder.CreateStore(tupleVal, alloca);
            }
            else
            {
                // Not a literal, just store
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(initValue)
                {
                    builder.CreateStore(initValue, alloca);
                }
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_TUPLE;
        return;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(node->type))
    {
        auto enumIt = enumValues.find(structRef->structName);
        if(enumIt != enumValues.end())
        {
            llvm::Type* targetType = llvm::Type::getInt32Ty(context);
            llvm::AllocaInst* alloca =
                builder.CreateAlloca(targetType, nullptr, node->name);

            if(node->initExpr)
            {
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(initValue)
                {
                    if(initValue->getType() != targetType)
                    {
                        if(initValue->getType()->isIntegerTy())
                        {
                            initValue = builder.CreateSExt(
                                initValue, targetType, "enum.sext");
                        }
                        else
                        {
                            reportError(node->line,
                                        "enum initializer must be integer");
                            return;
                        }
                    }
                    builder.CreateStore(initValue, alloca);
                }
            }

            namedValues[node->name] = alloca;
            variableTypes[node->name] = TypeNode::TYPE_INT;
            return;
        }

        llvm::Type* structType = getStructType(structRef->structName);
        if(!structType)
        {
            reportError(node->line,
                        "unknown struct type: " + structRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = structRef->structName;
        registerStructCleanupIfNeeded(node->name, structRef->structName);
        return;
    }

    llvm::Type* targetType = getLLVMType(node->type->kind);
    llvm::AllocaInst* alloca =
        builder.CreateAlloca(targetType, nullptr, node->name);

    if(node->initExpr)
    {
        llvm::Value* initValue = generateExpression(node->initExpr);
        if(initValue)
        {
            // Convert init value to target type if necessary
            llvm::Type* initType = initValue->getType();
            if(initType != targetType)
            {
                if(initType->isIntegerTy() && targetType->isIntegerTy())
                {
                    unsigned initBits = initType->getIntegerBitWidth();
                    unsigned targetBits = targetType->getIntegerBitWidth();
                    if(initBits > targetBits)
                    {
                        // Truncate (e.g., i64 -> i8)
                        initValue =
                            builder.CreateTrunc(initValue, targetType, "trunc");
                    }
                    else if(initBits < targetBits)
                    {
                        // Extend - use ZExt for unsigned target, SExt for
                        // signed
                        if(isUnsignedType(node->type->kind))
                        {
                            initValue = builder.CreateZExt(initValue,
                                                           targetType, "zext");
                        }
                        else
                        {
                            initValue = builder.CreateSExt(initValue,
                                                           targetType, "sext");
                        }
                    }
                }
                else if(initType->isIntegerTy() &&
                        targetType->isFloatingPointTy())
                {
                    initValue =
                        builder.CreateSIToFP(initValue, targetType, "sitofp");
                }
                else if(initType->isFloatingPointTy() &&
                        targetType->isIntegerTy())
                {
                    initValue =
                        builder.CreateFPToSI(initValue, targetType, "fptosi");
                }
                else if(initType->isFloatingPointTy() &&
                        targetType->isFloatingPointTy())
                {
                    initValue =
                        builder.CreateFPCast(initValue, targetType, "fpcast");
                }
            }
            builder.CreateStore(initValue, alloca);
        }
    }

    namedValues[node->name] = alloca;
    variableTypes[node->name] = node->type->kind;
}

void CodeGenerator::generateAssignment(AssignmentNode* node)
{
    // Check if trying to assign to a constant (let) variable
    if(constantVariables.find(node->name) != constantVariables.end())
    {
        reportError(node->line, "cannot assign to constant variable '" +
                                    node->name + "' (declared with 'let')");
        return;
    }

    llvm::Value* variable = namedValues[node->name];
    if(!variable)
    {
        reportError(node->line, "unknown variable: '" + node->name + "'");
        return;
    }

    llvm::Value* value = generateExpression(node->expression);
    if(!value)
        return;

    auto borIt = activeBorrowers.find(node->name);
    if(borIt != activeBorrowers.end() && !borIt->second.empty())
    {
        std::string by = *borIt->second.begin();
        reportError(node->line, "cannot assign to '" + node->name +
                                    "' while borrowed by '" + by + "'");
        return;
    }
    consumeMoveFromExpression(node->expression, node->line,
                              "assigning to '" + node->name + "'");

    llvm::Type* targetType = nullptr;
    llvm::AllocaInst* targetAlloca = llvm::dyn_cast<llvm::AllocaInst>(variable);
    llvm::GlobalVariable* targetGlobal =
        llvm::dyn_cast<llvm::GlobalVariable>(variable);
    if(targetAlloca)
        targetType = targetAlloca->getAllocatedType();
    else if(targetGlobal)
        targetType = targetGlobal->getValueType();
    else
    {
        reportError(node->line, "assignment target is not addressable: '" +
                                    node->name + "'");
        return;
    }

    llvm::Type* valueType = value->getType();

    // Convert value to target type if necessary
    if(valueType != targetType)
    {
        if(valueType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned valueBits = valueType->getIntegerBitWidth();
            unsigned targetBits = targetType->getIntegerBitWidth();
            if(valueBits > targetBits)
            {
                value = builder.CreateTrunc(value, targetType, "trunc");
            }
            else if(valueBits < targetBits)
            {
                value = builder.CreateSExt(value, targetType, "sext");
            }
        }
        else if(valueType->isIntegerTy() && targetType->isFloatingPointTy())
        {
            value = builder.CreateSIToFP(value, targetType, "sitofp");
        }
        else if(valueType->isFloatingPointTy() && targetType->isIntegerTy())
        {
            value = builder.CreateFPToSI(value, targetType, "fptosi");
        }
        else if(valueType->isFloatingPointTy() &&
                targetType->isFloatingPointTy())
        {
            value = builder.CreateFPCast(value, targetType, "fpcast");
        }
        else
        {
            reportError(node->line,
                        "type mismatch in assignment to variable '" +
                            node->name + "'");
            return;
        }
    }

    builder.CreateStore(value, variable);
    clearMovedVariable(node->name);
    if(targetType->isPointerTy())
    {
        if(targetGlobal &&
           !validateNoEscapingBorrow(
               node->expression, node->line,
               "store in global/static '" + node->name + "'"))
        {
            return;
        }
        registerPointerBorrow(node->name, node->expression, node->line);
    }
    else
    {
        clearPointerBorrow(node->name);
    }
}

void CodeGenerator::generateFieldAssignment(FieldAssignmentNode* node)
{
    llvm::Value* structPtr;
    std::string structTypeName;
    std::string fieldName;
    bool targetGlobalStorage = false;

    std::function<bool(ExpressionNode*)> isGlobalBackedStructExpr =
        [&](ExpressionNode* expr) -> bool
    {
        if(!expr)
            return false;
        if(auto* id = dynamic_cast<IdentifierNode*>(expr))
        {
            auto it = namedValues.find(id->name);
            return it != namedValues.end() &&
                   llvm::isa<llvm::GlobalVariable>(it->second);
        }
        if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
        {
            if(field->object)
                return isGlobalBackedStructExpr(field->object);
            auto it = namedValues.find(field->structName);
            return it != namedValues.end() &&
                   llvm::isa<llvm::GlobalVariable>(it->second);
        }
        return false;
    };

    // Handle chained assignment (a.b.c = x) vs simple assignment (a.b = x)
    if(node->target)
    {
        // Chained assignment: target is a FieldAccessNode representing the full
        // path
        auto* fieldAccess = dynamic_cast<FieldAccessNode*>(node->target);
        if(!fieldAccess)
        {
            reportError(node->line, "invalid assignment target");
            return;
        }

        fieldName = fieldAccess->fieldName;

        // Get the struct pointer for the object part (everything except the
        // last field)
        if(fieldAccess->object)
        {
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, node->line);
            if(!ptr)
                return;
            structPtr = ptr;
            structTypeName = typeName;
            targetGlobalStorage = isGlobalBackedStructExpr(fieldAccess->object);
        }
        else
        {
            // Simple case within chained: the target is like "a.b", so get "a"
            structPtr = namedValues[fieldAccess->structName];
            if(!structPtr)
            {
                reportError(node->line,
                            "unknown variable: " + fieldAccess->structName);
                return;
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(node->line, "variable '" + fieldAccess->structName +
                                            "' is not a struct");
                return;
            }
            structTypeName = typeIt->second;
            targetGlobalStorage =
                llvm::isa<llvm::GlobalVariable>(structPtr);

            // Handle self pointer
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
            {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if(allocaType->isPointerTy())
                {
                    structPtr = builder.CreateLoad(
                        allocaType, alloca, fieldAccess->structName + ".ptr");
                }
            }
        }
    }
    else
    {
        // Simple assignment: a.b = x
        fieldName = node->fieldName;

        structPtr = namedValues[node->structName];
        if(!structPtr)
        {
            reportError(node->line, "unknown variable: " + node->structName);
            return;
        }

        auto typeIt = structVariableTypes.find(node->structName);
        if(typeIt == structVariableTypes.end())
        {
            reportError(node->line,
                        "variable '" + node->structName + "' is not a struct");
            return;
        }
        structTypeName = typeIt->second;
        targetGlobalStorage = llvm::isa<llvm::GlobalVariable>(structPtr);

        // Handle self pointer
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                structPtr = builder.CreateLoad(allocaType, alloca,
                                               node->structName + ".ptr");
            }
        }
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return;
    }

    // Find field index
    int fieldIndex = -1;
    TypeNode* fieldType = nullptr;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        if(members[i].first == fieldName)
        {
            fieldIndex = static_cast<int>(i);
            fieldType = members[i].second;
            break;
        }
    }

    if(fieldIndex < 0)
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no field named '" + fieldName + "'");
        return;
    }

    // Generate the value to assign
    llvm::Value* value = generateExpression(node->expression);
    if(!value)
        return;
    consumeMoveFromExpression(node->expression, node->line,
                              "assigning to field '" + fieldName + "'");

    // Get struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
        return;

    // Convert value if needed
    llvm::Type* targetType = getLLVMTypeFromNode(fieldType);
    if(targetType->isPointerTy() && targetGlobalStorage &&
       !validateNoEscapingBorrow(
           node->expression, node->line,
           "store in global/static field '" + fieldName + "'"))
    {
        return;
    }
    llvm::Type* valueType = value->getType();

    if(valueType != targetType)
    {
        if(valueType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned srcBits = valueType->getIntegerBitWidth();
            unsigned dstBits = targetType->getIntegerBitWidth();
            if(srcBits > dstBits)
            {
                value = builder.CreateTrunc(value, targetType, "trunc");
            }
            else if(srcBits < dstBits)
            {
                value = builder.CreateSExt(value, targetType, "sext");
            }
        }
        else if(valueType->isFloatingPointTy() &&
                targetType->isFloatingPointTy())
        {
            value = builder.CreateFPCast(value, targetType, "fpcast");
        }
        else if(valueType->isIntegerTy() && targetType->isFloatingPointTy())
        {
            value = builder.CreateSIToFP(value, targetType, "sitofp");
        }
        else if(valueType->isFloatingPointTy() && targetType->isIntegerTy())
        {
            value = builder.CreateFPToSI(value, targetType, "fptosi");
        }
        else
        {
            // Incompatible types
            std::string valueTypeStr, targetTypeStr;

            if(valueType->isIntegerTy())
                valueTypeStr =
                    "i" + std::to_string(valueType->getIntegerBitWidth());
            else if(valueType->isFloatTy())
                valueTypeStr = "float";
            else if(valueType->isDoubleTy())
                valueTypeStr = "double";
            else if(valueType->isPointerTy())
                valueTypeStr = "pointer";
            else if(valueType->isStructTy())
                valueTypeStr = valueType->getStructName().str().empty()
                                   ? "struct"
                                   : valueType->getStructName().str();
            else
                valueTypeStr = "unknown";

            if(targetType->isIntegerTy())
                targetTypeStr =
                    "i" + std::to_string(targetType->getIntegerBitWidth());
            else if(targetType->isFloatTy())
                targetTypeStr = "float";
            else if(targetType->isDoubleTy())
                targetTypeStr = "double";
            else if(targetType->isPointerTy())
                targetTypeStr = "pointer";
            else if(targetType->isStructTy())
                targetTypeStr = targetType->getStructName().str().empty()
                                    ? "struct"
                                    : targetType->getStructName().str();
            else
                targetTypeStr = "unknown";

            reportError(node->line, "type mismatch in assignment to field '" +
                                        fieldName + "': expected '" +
                                        targetTypeStr + "', got '" +
                                        valueTypeStr + "'");
            return;
        }
    }

    // Create GEP to field and store
    llvm::Value* fieldPtr = builder.CreateStructGEP(
        structType, structPtr, static_cast<unsigned>(fieldIndex),
        fieldName + "_ptr");
    builder.CreateStore(value, fieldPtr);
}

void CodeGenerator::generateDerefAssignment(DerefAssignmentNode* node)
{
    if(!validatePointerDereference(node->pointerExpr, node->line))
        return;

    llvm::Value* ptrVal = generateExpression(node->pointerExpr);
    if(!ptrVal)
        return;

    if(!ptrVal->getType()->isPointerTy())
    {
        reportError(node->line, "dereference requires a pointer value");
        return;
    }

    TypeNode* elemTypeNode =
        getPointerElementType(node->pointerExpr, node->line);
    if(!elemTypeNode)
        return;

    llvm::Type* elemType = getLLVMTypeFromNode(elemTypeNode);
    if(!elemType)
        return;

    llvm::Value* value = generateExpression(node->value);
    if(!value)
        return;
    consumeMoveFromExpression(node->value, node->line,
                              "assigning through dereference");

    llvm::Type* valueType = value->getType();
    if(valueType != elemType)
    {
        if(valueType->isIntegerTy() && elemType->isIntegerTy())
        {
            unsigned valueBits = valueType->getIntegerBitWidth();
            unsigned targetBits = elemType->getIntegerBitWidth();
            if(valueBits > targetBits)
            {
                value = builder.CreateTrunc(value, elemType, "trunc");
            }
            else if(valueBits < targetBits)
            {
                value = builder.CreateSExt(value, elemType, "sext");
            }
        }
        else if(valueType->isIntegerTy() && elemType->isFloatingPointTy())
        {
            value = builder.CreateSIToFP(value, elemType, "sitofp");
        }
        else if(valueType->isFloatingPointTy() && elemType->isIntegerTy())
        {
            value = builder.CreateFPToSI(value, elemType, "fptosi");
        }
        else if(valueType->isFloatingPointTy() &&
                elemType->isFloatingPointTy())
        {
            value = builder.CreateFPCast(value, elemType, "fpcast");
        }
        else
        {
            reportError(node->line,
                        "type mismatch in deref assignment");
            return;
        }
    }

    builder.CreateStore(value, ptrVal);
}

// Helper to get struct pointer and type name from an expression
// Returns {pointer, typeName} or {nullptr, ""} on error
std::pair<llvm::Value*, std::string>
CodeGenerator::getStructPtrAndType(ExpressionNode* expr, int line)
{
    // Case 1: Simple identifier (e.g., "myStruct")
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        if(globalNamedValues.find(id->name) == globalNamedValues.end() &&
           isVariableMoved(id->name))
        {
            reportError(line, "use of moved value: '" + id->name + "'");
            return {nullptr, ""};
        }

        llvm::Value* ptr = namedValues[id->name];
        if(!ptr)
        {
            reportError(line, "unknown variable: " + id->name);
            return {nullptr, ""};
        }

        auto typeIt = structVariableTypes.find(id->name);
        std::string inferredStructName;
        if(typeIt == structVariableTypes.end())
        {
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr))
            {
                llvm::Type* at = alloca->getAllocatedType();
                if(at && at->isStructTy())
                {
                    auto* st = llvm::cast<llvm::StructType>(at);
                    inferredStructName = st->getName().str();
                }
            }
            if(inferredStructName.empty())
            {
                reportError(line, "variable '" + id->name + "' is not a struct");
                return {nullptr, ""};
            }
        }

        // Handle self pointer (alloca containing pointer)
        llvm::Value* actualPtr = ptr;
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                actualPtr =
                    builder.CreateLoad(allocaType, alloca, id->name + ".ptr");
            }
        }

        return {actualPtr,
                typeIt != structVariableTypes.end() ? typeIt->second
                                                    : inferredStructName};
    }

    // Case 2: Field access (e.g., "a.b" in "a.b.c")
    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(expr))
    {
        // Recursively get the struct pointer for the object
        llvm::Value* objPtr;
        std::string objTypeName;

        if(fieldAccess->object)
        {
            // Chained: get pointer from the object expression
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, line);
            if(!ptr)
                return {nullptr, ""};
            objPtr = ptr;
            objTypeName = typeName;
        }
        else
        {
            // Simple: get pointer from structName
            objPtr = namedValues[fieldAccess->structName];
            if(!objPtr)
            {
                reportError(line,
                            "unknown variable: " + fieldAccess->structName);
                return {nullptr, ""};
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + fieldAccess->structName +
                                      "' is not a struct");
                return {nullptr, ""};
            }
            objTypeName = typeIt->second;

            // Handle self pointer
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(objPtr))
            {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if(allocaType->isPointerTy())
                {
                    objPtr = builder.CreateLoad(
                        allocaType, alloca, fieldAccess->structName + ".ptr");
                }
            }
        }

        // Now access the field
        auto memberIt = structMembers.find(objTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(line, "unknown struct type: " + objTypeName);
            return {nullptr, ""};
        }

        // Find field index and type
        int fieldIndex = -1;
        TypeNode* fieldType = nullptr;
        const auto& members = memberIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldAccess->fieldName)
            {
                fieldIndex = static_cast<int>(i);
                fieldType = members[i].second;
                break;
            }
        }

        if(fieldIndex < 0)
        {
            reportError(line, "struct '" + objTypeName +
                                  "' has no field named '" +
                                  fieldAccess->fieldName + "'");
            return {nullptr, ""};
        }

        // Check if the field is a struct type
        if(fieldType->kind == TypeNode::TYPE_STRUCT)
        {
            auto* structTypeRef = dynamic_cast<StructTypeRefNode*>(fieldType);
            if(!structTypeRef)
            {
                reportError(line, "internal error: expected StructTypeRefNode");
                return {nullptr, ""};
            }

            llvm::StructType* structType = getStructType(objTypeName);
            if(!structType)
                return {nullptr, ""};

            // Get pointer to the nested struct field
            llvm::Value* fieldPtr = builder.CreateStructGEP(
                structType, objPtr, static_cast<unsigned>(fieldIndex),
                fieldAccess->fieldName + "_ptr");

            return {fieldPtr, structTypeRef->structName};
        }
        else
        {
            reportError(line, "field '" + fieldAccess->fieldName +
                                  "' is not a struct type");
            return {nullptr, ""};
        }
    }

    // Case 3: Dereference of a pointer to struct (e.g., "*p")
    if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
    {
        if(unary->op == UnaryOpNode::OP_DEREF)
        {
            if(!validatePointerDereference(unary->operand, line))
                return {nullptr, ""};

            llvm::Value* ptrVal = generateExpression(unary->operand);
            if(!ptrVal)
                return {nullptr, ""};
            if(!ptrVal->getType()->isPointerTy())
            {
                reportError(line, "dereference requires a pointer value");
                return {nullptr, ""};
            }

            TypeNode* elemType = getPointerElementType(unary->operand, line);
            if(!elemType)
                return {nullptr, ""};
            if(elemType->kind != TypeNode::TYPE_STRUCT)
            {
                reportError(line, "dereference does not yield a struct value");
                return {nullptr, ""};
            }

            if(auto* structRef = dynamic_cast<StructTypeRefNode*>(elemType))
            {
                return {ptrVal, structRef->structName};
            }
            if(auto* genRef =
                   dynamic_cast<GenericStructTypeRefNode*>(elemType))
            {
                std::string mangled =
                    getOrCreateMonomorphizedStruct(genRef->structName,
                                                   genRef->typeArgs);
                return {ptrVal, mangled};
            }
        }
    }

    reportError(line, "invalid expression for field access");
    return {nullptr, ""};
}

llvm::Value* CodeGenerator::generateFieldAccess(FieldAccessNode* node)
{
    llvm::Value* structPtr;
    std::string structTypeName;

    // Handle chained access (a.b.c) vs simple access (a.b)
    if(node->object)
    {
        // Chained access: evaluate the object expression first
        auto [ptr, typeName] = getStructPtrAndType(node->object, node->line);
        if(!ptr)
            return nullptr;
        structPtr = ptr;
        structTypeName = typeName;
    }
    else
    {
        // Simple access: get from structName
        structPtr = namedValues[node->structName];
        if(!structPtr)
        {
            reportError(node->line, "unknown variable: " + node->structName);
            return nullptr;
        }

        auto typeIt = structVariableTypes.find(node->structName);
        if(typeIt == structVariableTypes.end())
        {
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
            {
                llvm::Type* at = alloca->getAllocatedType();
                if(at && at->isStructTy())
                {
                    auto* st = llvm::cast<llvm::StructType>(at);
                    structTypeName = st->getName().str();
                }
            }
            if(structTypeName.empty())
            {
                reportError(node->line,
                            "variable '" + node->structName +
                                "' is not a struct");
                return nullptr;
            }
        }
        else
        {
            structTypeName = typeIt->second;
        }

        // Handle self pointer (alloca containing pointer)
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                structPtr = builder.CreateLoad(allocaType, alloca,
                                               node->structName + ".ptr");
            }
        }
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return nullptr;
    }

    // Find field index
    int fieldIndex = -1;
    TypeNode* fieldType = nullptr;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        if(members[i].first == node->fieldName)
        {
            fieldIndex = static_cast<int>(i);
            fieldType = members[i].second;
            break;
        }
    }

    if(fieldIndex < 0)
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no field named '" + node->fieldName +
                                    "'");
        return nullptr;
    }

    // Get struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
        return nullptr;

    // Get field type
    llvm::Type* llvmFieldType = getLLVMTypeFromNode(fieldType);

    // Create GEP to field and load
    llvm::Value* fieldPtr = builder.CreateStructGEP(
        structType, structPtr, static_cast<unsigned>(fieldIndex),
        node->fieldName + "_ptr");
    return builder.CreateLoad(llvmFieldType, fieldPtr, node->fieldName);
}

void CodeGenerator::reportError(int line, const std::string& message)
{
    if(line > 0)
    {
        std::cerr << "Error (line " << line << "): " << message << std::endl;
    }
    else
    {
        std::cerr << "Error: " << message << std::endl;
    }
    hasError = true;
}

void CodeGenerator::enterCleanupScope()
{
    cleanupScopes.emplace_back();
    pointerBorrowScopes.emplace_back();
    variableScopeDepthScopes.emplace_back();
}

void CodeGenerator::exitCleanupScope()
{
    if(cleanupScopes.empty() || pointerBorrowScopes.empty() ||
       variableScopeDepthScopes.empty())
        return;

    auto actions = std::move(cleanupScopes.back());
    cleanupScopes.pop_back();
    auto pointerEntries = std::move(pointerBorrowScopes.back());
    pointerBorrowScopes.pop_back();
    auto depthEntries = std::move(variableScopeDepthScopes.back());
    variableScopeDepthScopes.pop_back();

    for(auto it = pointerEntries.rbegin(); it != pointerEntries.rend(); ++it)
    {
        clearPointerBorrow(it->pointerVar);
        if(it->hadPreviousBorrow && !it->previousOwner.empty())
        {
            pointerBorrowTarget[it->pointerVar] = it->previousOwner;
            activeBorrowers[it->previousOwner].insert(it->pointerVar);
        }
    }
    for(auto it = depthEntries.rbegin(); it != depthEntries.rend(); ++it)
    {
        if(it->hadPreviousDepth)
            variableScopeDepth[it->varName] = it->previousDepth;
        else
            variableScopeDepth.erase(it->varName);
    }

    if(!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator())
        return;

    for(auto it = actions.rbegin(); it != actions.rend(); ++it)
    {
        if(!it->dropFunction)
            continue;
        auto nv = namedValues.find(it->varName);
        if(nv == namedValues.end() || !nv->second)
            continue;
        auto sv = structVariableTypes.find(it->varName);
        if(sv == structVariableTypes.end() || sv->second != it->structTypeName)
            continue;

        auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(nv->second);
        if(!alloca)
            continue;
        llvm::Type* allocaType = alloca->getAllocatedType();
        if(!allocaType || !allocaType->isStructTy())
            continue;

        llvm::Value* value = builder.CreateLoad(allocaType, alloca,
                                                it->varName + ".dropval");
        builder.CreateCall(it->dropFunction, {value});
    }
}

void CodeGenerator::emitAllActiveCleanups()
{
    if(!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator())
        return;

    for(auto scopeIt = cleanupScopes.rbegin(); scopeIt != cleanupScopes.rend();
        ++scopeIt)
    {
        auto& actions = *scopeIt;
        for(auto it = actions.rbegin(); it != actions.rend(); ++it)
        {
            if(!it->dropFunction)
                continue;
            auto nv = namedValues.find(it->varName);
            if(nv == namedValues.end() || !nv->second)
                continue;
            auto sv = structVariableTypes.find(it->varName);
            if(sv == structVariableTypes.end() ||
               sv->second != it->structTypeName)
                continue;

            auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(nv->second);
            if(!alloca)
                continue;
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(!allocaType || !allocaType->isStructTy())
                continue;

            llvm::Value* value = builder.CreateLoad(
                allocaType, alloca, it->varName + ".dropval.ret");
            builder.CreateCall(it->dropFunction, {value});
        }
    }
}

llvm::Function*
CodeGenerator::resolveDropFunctionForStruct(const std::string& structTypeName)
{
    auto* structType = getStructType(structTypeName);
    if(!structType)
        return nullptr;

    auto find_drop = [&](const std::string& fnName) -> llvm::Function* {
        auto it = functionOverloads.find(fnName);
        if(it == functionOverloads.end())
            return nullptr;
        for(auto& info : it->second)
        {
            llvm::Function* fn = info.function;
            if(!fn || fn->arg_size() != 1 || !fn->getReturnType()->isVoidTy())
                continue;
            llvm::Type* p0 = fn->getArg(0)->getType();
            if(p0 == structType)
                return fn;
        }
        return nullptr;
    };

    if(auto* f = find_drop("__drop"))
        return f;
    return find_drop("drop");
}

void CodeGenerator::registerStructCleanupIfNeeded(
    const std::string& varName, const std::string& structTypeName)
{
    if(varName.empty() || structTypeName.empty() || cleanupScopes.empty())
        return;
    if(auto* dropFn = resolveDropFunctionForStruct(structTypeName))
    {
        cleanupScopes.back().push_back(
            ScopeCleanup{varName, structTypeName, dropFn});
    }
}

llvm::Value* CodeGenerator::generateFunctionCall(FunctionCallNode* node)
{
    auto validateTemporaryBorrowArguments =
        [&](const std::vector<ExpressionNode*>& args,
            const std::string& calleeName) -> bool
    {
        std::set<std::string> borrowedInCall;
        for(auto* arg : args)
        {
            auto* unary = dynamic_cast<UnaryOpNode*>(arg);
            if(!unary || unary->op != UnaryOpNode::OP_ADDR)
                continue;

            std::string owner = resolveBorrowOwnerFromLValue(unary->operand);
            if(owner.empty())
                continue;

            if(globalNamedValues.find(owner) == globalNamedValues.end() &&
               isVariableMoved(owner))
            {
                reportError(node->line,
                            "cannot borrow moved value: '" + owner + "'");
                return false;
            }

            auto activeIt = activeBorrowers.find(owner);
            if(activeIt != activeBorrowers.end() && !activeIt->second.empty())
            {
                std::string by = *activeIt->second.begin();
                reportError(node->line, "cannot borrow '" + owner +
                                            "' while already borrowed by '" +
                                            by + "'");
                return false;
            }

            if(!borrowedInCall.insert(owner).second)
            {
                reportError(node->line,
                            "cannot borrow '" + owner +
                                "' multiple times in call to '" +
                                calleeName + "'");
                return false;
            }
        }
        return true;
    };

    if(node->name == "String::new")
    {
        if(!node->arguments.empty())
        {
            reportError(node->line, "String::new expects no arguments");
            return nullptr;
        }
        initializeStdlibFunctions();
        llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
        llvm::Type* int8Type = llvm::Type::getInt8Ty(context);
        llvm::Value* size = llvm::ConstantInt::get(int64Type, 1);
        llvm::Value* ptr = builder.CreateCall(mallocFunc, {size}, "string_new");
        builder.CreateStore(llvm::ConstantInt::get(int8Type, 0), ptr);
        return ptr;
    }
    if(node->name == "String::with_capacity")
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line,
                        "String::with_capacity expects one argument");
            return nullptr;
        }
        initializeStdlibFunctions();
        llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
        llvm::Type* int8Type = llvm::Type::getInt8Ty(context);
        llvm::Value* cap = generateExpression(node->arguments[0]);
        if(!cap)
            return nullptr;
        if(!cap->getType()->isIntegerTy())
        {
            reportError(
                node->line,
                "String::with_capacity expects an integer capacity");
            return nullptr;
        }
        if(cap->getType()->getIntegerBitWidth() < 64)
            cap = builder.CreateSExt(cap, int64Type, "cap_sext");
        else if(cap->getType()->getIntegerBitWidth() > 64)
            cap = builder.CreateTrunc(cap, int64Type, "cap_trunc");
        llvm::Value* size = builder.CreateAdd(
            cap, llvm::ConstantInt::get(int64Type, 1), "string_cap_plus_one");
        llvm::Value* ptr =
            builder.CreateCall(mallocFunc, {size}, "string_with_capacity");
        builder.CreateStore(llvm::ConstantInt::get(int8Type, 0), ptr);
        return ptr;
    }
    if(node->name == "String::free")
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line, "String::free expects one argument");
            return nullptr;
        }
        initializeStdlibFunctions();
        llvm::Value* ptr = generateExpression(node->arguments[0]);
        if(!ptr)
            return nullptr;
        consumeMoveFromExpression(node->arguments[0], node->line,
                                  "passing argument to String::free");
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        if(ptr->getType() != ptrType && ptr->getType()->isPointerTy())
            ptr = builder.CreateBitCast(ptr, ptrType, "string_free_cast");
        if(!ptr->getType()->isPointerTy())
        {
            reportError(node->line, "String::free expects a string/pointer");
            return nullptr;
        }
        return builder.CreateCall(freeFunc, {ptr});
    }

    if(node->name == "thread_spawn")
        return generateThreadSpawn(node);
    if(node->name == "thread_join")
        return generateThreadJoin(node);
    if(node->name == "mutex_create")
        return generateMutexCreate(node);
    if(node->name == "mutex_lock")
        return generateMutexLock(node);
    if(node->name == "mutex_unlock")
        return generateMutexUnlock(node);
    if(node->name == "mutex_destroy")
        return generateMutexDestroy(node);
    if(node->name == "atomic_i64_new")
        return generateAtomicI64New(node);
    if(node->name == "atomic_i64_load")
        return generateAtomicI64Load(node);
    if(node->name == "atomic_i64_store")
        return generateAtomicI64Store(node);
    if(node->name == "atomic_i64_add")
        return generateAtomicI64Add(node);
    if(node->name == "atomic_i64_free")
        return generateAtomicI64Free(node);

    auto overloadIt = functionOverloads.find(node->name);
    if(overloadIt == functionOverloads.end())
    {
        // Static struct method call syntax: Type::method(...)
        size_t scopePos = node->name.find("::");
        if(scopePos != std::string::npos)
        {
            std::string structName = node->name.substr(0, scopePos);
            std::string methodName = node->name.substr(scopePos + 2);

            auto sit = structMethods.find(structName);
            std::string resolvedStructName = structName;
            if(sit == structMethods.end())
            {
                // Imported structs can be namespaced internally. Allow calling
                // with the visible tail name (e.g. File::open).
                for(auto it = structMethods.begin(); it != structMethods.end();
                    ++it)
                {
                    const std::string& candidate = it->first;
                    if(candidate == structName)
                    {
                        sit = it;
                        resolvedStructName = candidate;
                        break;
                    }
                    if(candidate.size() > structName.size() &&
                       candidate.compare(candidate.size() - structName.size(),
                                         structName.size(), structName) == 0)
                    {
                        char sep =
                            candidate[candidate.size() - structName.size() - 1];
                        if(sep == ':' || sep == '.' || sep == '_')
                        {
                            sit = it;
                            resolvedStructName = candidate;
                            break;
                        }
                    }
                }
            }

            if(sit != structMethods.end())
            {
                auto mit = sit->second.find(methodName);
                if(mit != sit->second.end() && mit->second.second &&
                   mit->second.second->isStatic)
                {
                    std::string mangledName =
                        resolvedStructName + "_" + methodName;
                    llvm::Function* callee = module->getFunction(mangledName);
                    if(!callee)
                    {
                        callee =
                            generateMethodDeclaration(resolvedStructName,
                                                      mit->second.second);
                    }
                    if(callee && callee->empty())
                    {
                        callee =
                            generateMethodDefinition(resolvedStructName,
                                                     mit->second.second);
                    }
                    if(!callee)
                    {
                        reportError(node->line,
                                    "unknown static method: '" + node->name +
                                        "'");
                        return nullptr;
                    }

                    if(callee->arg_size() != node->arguments.size())
                    {
                        reportError(
                            node->line,
                            "wrong number of arguments for static method '" +
                                node->name + "': expected " +
                                std::to_string(callee->arg_size()) + ", got " +
                                std::to_string(node->arguments.size()));
                        return nullptr;
                    }

                    if(!validateTemporaryBorrowArguments(node->arguments,
                                                         node->name))
                    {
                        return nullptr;
                    }

                    std::vector<llvm::Value*> callArgs;
                    callArgs.reserve(node->arguments.size());
                    size_t idx = 0;
                    for(auto* argNode : node->arguments)
                    {
                        llvm::Value* argVal = generateExpression(argNode);
                        if(!argVal)
                            return nullptr;
                        consumeMoveFromExpression(
                            argNode, node->line,
                            "passing argument to static method '" + node->name +
                                "'");

                        llvm::Type* expectedType = callee->getArg(idx)->getType();
                        if(argVal->getType() != expectedType)
                        {
                            int convCost = 0;
                            if(!canConvertType(argVal->getType(), expectedType,
                                               convCost))
                            {
                                reportError(node->line,
                                            "argument type mismatch for static "
                                            "method '" +
                                                node->name + "'");
                                return nullptr;
                            }

                            if(argVal->getType()->isIntegerTy() &&
                               expectedType->isIntegerTy())
                            {
                                argVal = builder.CreateIntCast(
                                    argVal, expectedType, true, "arg.cast");
                            }
                            else if(argVal->getType()->isIntegerTy() &&
                                    expectedType->isFloatingPointTy())
                            {
                                argVal = builder.CreateSIToFP(
                                    argVal, expectedType, "arg.sitofp");
                            }
                            else if(argVal->getType()->isFloatingPointTy() &&
                                    expectedType->isFloatingPointTy())
                            {
                                argVal = builder.CreateFPCast(
                                    argVal, expectedType, "arg.fpcast");
                            }
                            else if(argVal->getType()->isPointerTy() &&
                                    expectedType->isPointerTy())
                            {
                                argVal = builder.CreateBitCast(
                                    argVal, expectedType, "arg.ptrcast");
                            }
                        }

                        callArgs.push_back(argVal);
                        ++idx;
                    }

                    if(callee->getReturnType()->isVoidTy())
                        return builder.CreateCall(callee, callArgs);
                    return builder.CreateCall(callee, callArgs, "staticcall");
                }
            }
        }

        reportError(node->line, "unknown function: '" + node->name + "'");
        return nullptr;
    }

    if(!validateTemporaryBorrowArguments(node->arguments, node->name))
        return nullptr;

    std::vector<llvm::Value*> argVals;
    argVals.reserve(node->arguments.size());
    for(auto arg : node->arguments)
    {
        llvm::Value* argVal = generateExpression(arg);
        if(!argVal)
            return nullptr;
        consumeMoveFromExpression(arg, node->line,
                                  "passing argument to function '" +
                                      node->name + "'");
        argVals.push_back(argVal);
    }

    FunctionOverloadInfo* best = nullptr;
    int bestCost = std::numeric_limits<int>::max();
    bool ambiguous = false;
    std::string privateModule;

    for(auto& info : overloadIt->second)
    {
        if(!isOverloadVisible(info))
        {
            if(privateModule.empty() && !info.sourceModule.empty())
                privateModule = info.sourceModule;
            continue;
        }

        llvm::Function* callee = info.function;
        size_t expectedArgs = callee->arg_size();
        size_t actualArgs = argVals.size();
        bool isVarArg = callee->isVarArg();

        if(!isVarArg && expectedArgs != actualArgs)
            continue;
        if(isVarArg && actualArgs < expectedArgs)
            continue;

        int totalCost = 0;
        bool ok = true;
        for(size_t i = 0; i < expectedArgs; ++i)
        {
            int cost = 0;
            if(!canConvertType(argVals[i]->getType(),
                               callee->getArg(i)->getType(), cost))
            {
                ok = false;
                break;
            }
            totalCost += cost;
        }
        if(!ok)
            continue;

        if(totalCost < bestCost)
        {
            bestCost = totalCost;
            best = &info;
            ambiguous = false;
        }
        else if(totalCost == bestCost)
        {
            ambiguous = true;
        }
    }

    if(!best)
    {
        if(!privateModule.empty())
        {
            reportError(node->line, "function '" + node->name +
                                        "' is private in module '" +
                                        privateModule + "'");
        }
        else
        {
            reportError(node->line,
                        "no matching overload for function '" + node->name +
                            "'");
        }
        return nullptr;
    }
    if(ambiguous)
    {
        reportError(node->line,
                    "ambiguous call to function '" + node->name + "'");
        return nullptr;
    }

    llvm::Function* callee = best->function;
    size_t expectedArgs = callee->arg_size();
    bool isVarArg = callee->isVarArg();

    if(isVarArg && argVals.size() < expectedArgs)
    {
        reportError(node->line,
                    "function '" + node->name + "' requires at least " +
                        std::to_string(expectedArgs) + " argument(s), but " +
                        std::to_string(argVals.size()) + " provided");
        return nullptr;
    }

    std::vector<llvm::Value*> args;
    unsigned paramIdx = 0;
    for(auto* argValIn : argVals)
    {
        llvm::Value* argVal = argValIn;
        if(paramIdx < expectedArgs)
        {
            llvm::Type* expectedType = callee->getArg(paramIdx)->getType();
            llvm::Type* actualType = argVal->getType();

            if(actualType != expectedType)
            {
                if(actualType->isIntegerTy() && expectedType->isIntegerTy())
                {
                    unsigned actualBits = actualType->getIntegerBitWidth();
                    unsigned expectedBits = expectedType->getIntegerBitWidth();
                    if(actualBits > expectedBits)
                    {
                        argVal =
                            builder.CreateTrunc(argVal, expectedType, "trunc");
                    }
                    else if(actualBits < expectedBits)
                    {
                        argVal =
                            builder.CreateSExt(argVal, expectedType, "sext");
                    }
                }
                else if(actualType->isIntegerTy() &&
                        expectedType->isFloatingPointTy())
                {
                    argVal =
                        builder.CreateSIToFP(argVal, expectedType, "sitofp");
                }
                else if(actualType->isFloatingPointTy() &&
                        expectedType->isIntegerTy())
                {
                    argVal =
                        builder.CreateFPToSI(argVal, expectedType, "fptosi");
                }
                else if(actualType->isFloatingPointTy() &&
                        expectedType->isFloatingPointTy())
                {
                    argVal =
                        builder.CreateFPCast(argVal, expectedType, "fpcast");
                }
                else if(!(actualType->isPointerTy() &&
                          expectedType->isPointerTy()))
                {
                    std::string actualStr, expectedStr;

                    if(actualType->isStructTy())
                        actualStr = actualType->getStructName().str().empty()
                                        ? "struct"
                                        : actualType->getStructName().str();
                    else if(actualType->isIntegerTy())
                        actualStr = "i" + std::to_string(
                                              actualType->getIntegerBitWidth());
                    else if(actualType->isFloatTy())
                        actualStr = "float";
                    else if(actualType->isDoubleTy())
                        actualStr = "double";
                    else
                        actualStr = "unknown";

                    if(expectedType->isStructTy())
                        expectedStr =
                            expectedType->getStructName().str().empty()
                                ? "struct"
                                : expectedType->getStructName().str();
                    else if(expectedType->isIntegerTy())
                        expectedStr =
                            "i" +
                            std::to_string(expectedType->getIntegerBitWidth());
                    else if(expectedType->isFloatTy())
                        expectedStr = "float";
                    else if(expectedType->isDoubleTy())
                        expectedStr = "double";
                    else
                        expectedStr = "unknown";

                    reportError(node->line,
                                "argument " + std::to_string(paramIdx + 1) +
                                    " of function '" + node->name +
                                    "' has wrong type: expected '" +
                                    expectedStr + "', got '" + actualStr + "'");
                    return nullptr;
                }
            }
        }

        args.push_back(argVal);
        paramIdx++;
    }

    if(callee->getReturnType()->isVoidTy())
        return builder.CreateCall(callee, args);
    return builder.CreateCall(callee, args, "calltmp");
}

llvm::Value* CodeGenerator::generateThreadSpawn(FunctionCallNode* node)
{
    if(node->arguments.size() < 1 || node->arguments.size() > 5)
    {
        reportError(node->line,
                    "thread_spawn expects a function name and up to 4 integer "
                    "arguments");
        return nullptr;
    }

    auto* targetId = dynamic_cast<IdentifierNode*>(node->arguments[0]);
    if(!targetId)
    {
        reportError(node->line,
                    "thread_spawn expects a function name identifier");
        return nullptr;
    }

    size_t argCount = node->arguments.size() - 1;

    llvm::Function* targetFunc = nullptr;
    auto overloadIt = functionOverloads.find(targetId->name);
    if(overloadIt != functionOverloads.end())
    {
        for(auto& info : overloadIt->second)
        {
            if(!isOverloadVisible(info))
                continue;
            llvm::Function* candidate = info.function;
            if(!candidate || candidate->isVarArg())
                continue;
            if(candidate->arg_size() != argCount)
                continue;
            if(targetFunc && targetFunc != candidate)
            {
                reportError(node->line,
                            "ambiguous function: '" + targetId->name +
                                "' for thread_spawn");
                return nullptr;
            }
            targetFunc = candidate;
        }
    }

    if(!targetFunc)
        targetFunc = module->getFunction(targetId->name);
    if(!targetFunc)
    {
        reportError(node->line,
                    "unknown function: '" + targetId->name + "'");
        return nullptr;
    }

    if(targetFunc->arg_size() != argCount)
    {
        reportError(node->line,
                    "thread_spawn target argument count mismatch");
        return nullptr;
    }

    for(size_t i = 0; i < argCount; ++i)
    {
        llvm::Type* paramType = targetFunc->getFunctionType()->getParamType(i);
        if(paramType->isIntegerTy())
            continue;
        if(paramType->isStructTy())
        {
            auto* structType = llvm::cast<llvm::StructType>(paramType);
            std::string structName = structType->getName().str();
            auto memIt = structMembers.find(structName);
            int rawIndex = -1;
            if(memIt != structMembers.end())
            {
                for(size_t m = 0; m < memIt->second.size(); ++m)
                {
                    if(memIt->second[m].first == "raw")
                    {
                        rawIndex = static_cast<int>(m);
                        break;
                    }
                }
            }
            if(rawIndex >= 0)
                continue;
        }
        reportError(node->line,
                    "thread_spawn arguments must be integer or handle types");
        return nullptr;
    }

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::string wrapperName = "__mlang_thread_wrapper_" +
                              targetFunc->getName().str();
    if(argCount > 0)
        wrapperName += "_args" + std::to_string(argCount);
    llvm::Function* wrapperFunc = module->getFunction(wrapperName);
    if(!wrapperFunc)
    {
        llvm::FunctionType* wrapperType =
            llvm::FunctionType::get(ptrType, {ptrType}, false);
        wrapperFunc = llvm::Function::Create(
            wrapperType, llvm::Function::PrivateLinkage, wrapperName,
            module.get());

        auto savedIP = builder.saveIP();
        llvm::BasicBlock* entry =
            llvm::BasicBlock::Create(context, "entry", wrapperFunc);
        builder.SetInsertPoint(entry);

        std::vector<llvm::Value*> callArgs;
        if(argCount > 0)
        {
            llvm::Value* basePtr = wrapperFunc->getArg(0);
            for(size_t i = 0; i < argCount; ++i)
            {
                llvm::Value* offset = llvm::ConstantInt::get(
                    int64Type, static_cast<uint64_t>(i * 8), false);
                llvm::Value* bytePtr = builder.CreateGEP(
                    llvm::Type::getInt8Ty(context), basePtr, offset,
                    "thread.argbyte");
#if LLVM_VERSION_MAJOR < 15
                llvm::Type* int64PtrType =
                    llvm::PointerType::get(int64Type, 0);
                llvm::Value* argPtr =
                    builder.CreateBitCast(bytePtr, int64PtrType,
                                          "thread.argptr");
                llvm::Value* argVal =
                    builder.CreateLoad(int64Type, argPtr, "thread.arg");
#else
                llvm::Value* argVal =
                    builder.CreateLoad(int64Type, bytePtr, "thread.arg");
#endif
                llvm::Type* expectedType =
                    targetFunc->getFunctionType()->getParamType(i);
                llvm::Value* callArg = argVal;
                if(expectedType->isIntegerTy())
                {
                    if(expectedType != int64Type)
                    {
                        unsigned srcBits = int64Type->getIntegerBitWidth();
                        unsigned dstBits = expectedType->getIntegerBitWidth();
                        if(srcBits > dstBits)
                            callArg = builder.CreateTrunc(
                                callArg, expectedType, "thread.trunc");
                        else if(srcBits < dstBits)
                            callArg = builder.CreateSExt(
                                callArg, expectedType, "thread.sext");
                    }
                    callArgs.push_back(callArg);
                }
                else if(expectedType->isStructTy())
                {
                    auto* structType =
                        llvm::cast<llvm::StructType>(expectedType);
                    std::string structName = structType->getName().str();
                    llvm::Value* handleVal =
                        buildHandleValue(structName, callArg, node->line);
                    if(!handleVal)
                        return nullptr;
                    callArgs.push_back(handleVal);
                }
                else
                {
                    reportError(node->line,
                                "thread_spawn arg type unsupported");
                    return nullptr;
                }
            }
            builder.CreateCall(targetFunc, callArgs);
            builder.CreateCall(freeFunc, {wrapperFunc->getArg(0)});
        }
        else
        {
            builder.CreateCall(targetFunc, {});
        }

        llvm::Value* nullPtr =
            llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
                llvm::cast<llvm::PointerType>(ptrType)
#else
                llvm::cast<llvm::PointerType>(ptrType)
#endif
            );
        builder.CreateRet(nullPtr);
        builder.restoreIP(savedIP);
    }

    llvm::AllocaInst* threadHandle =
        builder.CreateAlloca(ptrType, nullptr, "thread.handle");
    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
            llvm::cast<llvm::PointerType>(ptrType)
#else
            llvm::cast<llvm::PointerType>(ptrType)
#endif
        );
    llvm::Value* wrapperPtr =
        builder.CreateBitCast(wrapperFunc, ptrType, "thread.wrapper");
    llvm::Value* argPtr = nullPtr;
    if(argCount > 0)
    {
        llvm::Value* sizeVal = llvm::ConstantInt::get(
            int64Type, static_cast<uint64_t>(argCount * 8), false);
        argPtr = builder.CreateCall(mallocFunc, {sizeVal}, "thread.argptr");
        for(size_t i = 0; i < argCount; ++i)
        {
            llvm::Value* rawArg =
                generateExpression(node->arguments[i + 1]);
            if(!rawArg)
                return nullptr;
            if(rawArg->getType()->isIntegerTy())
            {
                if(rawArg->getType() != int64Type)
                {
                    rawArg = builder.CreateSExt(rawArg, int64Type,
                                                "thread.argsext");
                }
            }
            else if(rawArg->getType()->isStructTy())
            {
                auto* structType = llvm::cast<llvm::StructType>(
                    rawArg->getType());
                std::string structName = structType->getName().str();
                auto memIt = structMembers.find(structName);
                int rawIndex = -1;
                if(memIt != structMembers.end())
                {
                    for(size_t m = 0; m < memIt->second.size(); ++m)
                    {
                        if(memIt->second[m].first == "raw")
                        {
                            rawIndex = static_cast<int>(m);
                            break;
                        }
                    }
                }
                if(rawIndex < 0)
                {
                    reportError(node->line,
                                "thread_spawn handle arg missing raw field");
                    return nullptr;
                }
                rawArg = builder.CreateExtractValue(rawArg, rawIndex,
                                                   "thread.handle.raw");
                if(rawArg->getType() != int64Type)
                {
                    rawArg = builder.CreateSExt(rawArg, int64Type,
                                                "thread.argsext");
                }
            }
            else
            {
                reportError(node->line,
                            "thread_spawn arguments must be integer or handle");
                return nullptr;
            }
            llvm::Value* offset = llvm::ConstantInt::get(
                int64Type, static_cast<uint64_t>(i * 8), false);
            llvm::Value* bytePtr = builder.CreateGEP(
                llvm::Type::getInt8Ty(context), argPtr, offset,
                "thread.argbyte");
#if LLVM_VERSION_MAJOR < 15
            llvm::Type* int64PtrType =
                llvm::PointerType::get(int64Type, 0);
            llvm::Value* typedPtr =
                builder.CreateBitCast(bytePtr, int64PtrType,
                                      "thread.argptr_i64");
            builder.CreateStore(rawArg, typedPtr);
#else
            builder.CreateStore(rawArg, bytePtr);
#endif
        }
    }

    builder.CreateCall(pthreadCreateFunc,
                       {threadHandle, nullPtr, wrapperPtr, argPtr});

    llvm::Value* threadVal =
        builder.CreateLoad(ptrType, threadHandle, "thread.value");
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(threadVal, int64Type, "thread.handle_i64");

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Thread"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    return buildHandleValue(handleTypeName, rawHandle, node->line);
}

llvm::Value* CodeGenerator::generateThreadJoin(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "thread_join expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Thread"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* threadPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "thread.ptr");
    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
            llvm::cast<llvm::PointerType>(ptrType)
#else
            llvm::cast<llvm::PointerType>(ptrType)
#endif
        );

    return builder.CreateCall(pthreadJoinFunc, {threadPtr, nullPtr},
                              "thread.join");
}

llvm::Value* CodeGenerator::buildHandleValue(const std::string& handleTypeName,
                                              llvm::Value* rawHandle, int line)
{
    if(!rawHandle)
        return nullptr;

    llvm::StructType* handleType = getStructType(handleTypeName);
    if(!handleType)
    {
        reportError(line, "unknown handle type: " + handleTypeName);
        return nullptr;
    }

    auto memIt = structMembers.find(handleTypeName);
    if(memIt == structMembers.end())
    {
        reportError(line, "unknown handle struct members: " + handleTypeName);
        return nullptr;
    }

    int rawIndex = -1;
    for(size_t i = 0; i < memIt->second.size(); ++i)
    {
        if(memIt->second[i].first == "raw")
        {
            rawIndex = static_cast<int>(i);
            break;
        }
    }

    if(rawIndex < 0)
    {
        reportError(line, "handle type missing raw field: " + handleTypeName);
        return nullptr;
    }

    llvm::Type* expectedType = handleType->getElementType(rawIndex);
    llvm::Value* rawVal = rawHandle;
    if(rawVal->getType() != expectedType)
    {
        if(rawVal->getType()->isIntegerTy() && expectedType->isIntegerTy())
        {
            unsigned srcBits = rawVal->getType()->getIntegerBitWidth();
            unsigned dstBits = expectedType->getIntegerBitWidth();
            if(srcBits > dstBits)
                rawVal =
                    builder.CreateTrunc(rawVal, expectedType, "handle.trunc");
            else if(srcBits < dstBits)
                rawVal =
                    builder.CreateSExt(rawVal, expectedType, "handle.sext");
        }
        else
        {
            reportError(line, "handle raw type mismatch");
            return nullptr;
        }
    }

    llvm::Value* handleVal = llvm::Constant::getNullValue(handleType);
    return builder.CreateInsertValue(handleVal, rawVal,
                                     static_cast<unsigned>(rawIndex),
                                     "handle.raw");
}

llvm::Value* CodeGenerator::extractHandleValue(ExpressionNode* expr,
                                                const std::string& expectedHandleType,
                                                int line)
{
    llvm::Value* val = generateExpression(expr);
    if(!val)
        return nullptr;

    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    llvm::Type* valType = val->getType();

    if(valType->isIntegerTy())
    {
        if(valType != int64Type)
            val = builder.CreateSExt(val, int64Type, "handle.sext");
        return val;
    }

    if(valType->isStructTy())
    {
        auto* structType = llvm::cast<llvm::StructType>(valType);
        std::string structName = structType->getName().str();
        if(!expectedHandleType.empty() && structName != expectedHandleType)
        {
            reportError(line, "handle type mismatch: expected " +
                                   expectedHandleType + ", got " + structName);
            return nullptr;
        }

        auto memIt = structMembers.find(structName);
        if(memIt == structMembers.end())
        {
            reportError(line, "unknown handle struct members: " + structName);
            return nullptr;
        }
        int rawIndex = -1;
        for(size_t i = 0; i < memIt->second.size(); ++i)
        {
            if(memIt->second[i].first == "raw")
            {
                rawIndex = static_cast<int>(i);
                break;
            }
        }
        if(rawIndex < 0)
        {
            reportError(line, "handle type missing raw field: " + structName);
            return nullptr;
        }
        llvm::Value* rawVal =
            builder.CreateExtractValue(val, rawIndex, "handle.raw");
        if(rawVal->getType() != int64Type)
            rawVal =
                builder.CreateSExt(rawVal, int64Type, "handle.sext");
        return rawVal;
    }

    reportError(line, "expected handle or integer value");
    return nullptr;
}

llvm::Value* CodeGenerator::generateMutexCreate(FunctionCallNode* node)
{
    if(!node->arguments.empty())
    {
        reportError(node->line, "mutex_create expects no arguments");
        return nullptr;
    }

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* sizeVal = llvm::ConstantInt::get(int64Type, 64, false);
    llvm::Value* mem = builder.CreateCall(mallocFunc, {sizeVal}, "mutex.mem");
    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
            llvm::cast<llvm::PointerType>(ptrType)
#else
            llvm::cast<llvm::PointerType>(ptrType)
#endif
        );
    builder.CreateCall(pthreadMutexInitFunc, {mem, nullPtr});
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(mem, int64Type, "mutex.handle_i64");
    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    return buildHandleValue(handleTypeName, rawHandle, node->line);
}

llvm::Value* CodeGenerator::generateMutexLock(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "mutex_lock expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    return builder.CreateCall(pthreadMutexLockFunc, {mutexPtr},
                              "mutex.lock");
}

llvm::Value* CodeGenerator::generateMutexUnlock(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "mutex_unlock expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    return builder.CreateCall(pthreadMutexUnlockFunc, {mutexPtr},
                              "mutex.unlock");
}

llvm::Value* CodeGenerator::generateMutexDestroy(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "mutex_destroy expects one argument");
        return nullptr;
    }

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Mutex"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    llvm::Value* result =
        builder.CreateCall(pthreadMutexDestroyFunc, {mutexPtr},
                           "mutex.destroy");
    builder.CreateCall(freeFunc, {mutexPtr});
    return result;
}

llvm::Value* CodeGenerator::generateAtomicI64New(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "atomic_i64_new expects one argument");
        return nullptr;
    }

    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* initVal = generateExpression(node->arguments[0]);
    if(!initVal)
        return nullptr;
    if(!initVal->getType()->isIntegerTy())
    {
        reportError(node->line, "atomic_i64_new expects integer value");
        return nullptr;
    }
    if(initVal->getType() != int64Type)
        initVal =
            builder.CreateSExt(initVal, int64Type, "atomic.sext");

    llvm::Value* sizeVal = llvm::ConstantInt::get(int64Type, 8, false);
    llvm::Value* mem = builder.CreateCall(mallocFunc, {sizeVal}, "atomic.mem");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType =
        llvm::PointerType::get(int64Type, 0);
    llvm::Value* typedPtr =
        builder.CreateBitCast(mem, int64PtrType, "atomic.ptr");
    builder.CreateStore(initVal, typedPtr);
#else
    builder.CreateStore(initVal, mem);
#endif
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(mem, int64Type, "atomic.handle_i64");
    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    return buildHandleValue(handleTypeName, rawHandle, node->line);
}

llvm::Value* CodeGenerator::generateAtomicI64Load(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "atomic_i64_load expects one argument");
        return nullptr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* ptr =
        builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType =
        llvm::PointerType::get(int64Type, 0);
    ptr = builder.CreateBitCast(ptr, int64PtrType, "atomic.ptr_i64");
#endif
    auto* loadInst = builder.CreateLoad(int64Type, ptr, "atomic.load");
    loadInst->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
    return loadInst;
}

llvm::Value* CodeGenerator::generateAtomicI64Store(FunctionCallNode* node)
{
    if(node->arguments.size() != 2)
    {
        reportError(node->line, "atomic_i64_store expects two arguments");
        return nullptr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    llvm::Value* valueVal = generateExpression(node->arguments[1]);
    if(!handleVal || !valueVal)
        return nullptr;
    if(valueVal->getType() != int64Type)
        valueVal =
            builder.CreateSExt(valueVal, int64Type, "atomic.sextval");

    llvm::Value* ptr =
        builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType =
        llvm::PointerType::get(int64Type, 0);
    ptr = builder.CreateBitCast(ptr, int64PtrType, "atomic.ptr_i64");
#endif
    auto* storeInst = builder.CreateStore(valueVal, ptr);
    storeInst->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
    return valueVal;
}

llvm::Value* CodeGenerator::generateAtomicI64Add(FunctionCallNode* node)
{
    if(node->arguments.size() != 2)
    {
        reportError(node->line, "atomic_i64_add expects two arguments");
        return nullptr;
    }

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    llvm::Value* addVal = generateExpression(node->arguments[1]);
    if(!handleVal || !addVal)
        return nullptr;
    if(addVal->getType() != int64Type)
        addVal = builder.CreateSExt(addVal, int64Type, "atomic.sextval");

    llvm::Value* ptr =
        builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType =
        llvm::PointerType::get(int64Type, 0);
    ptr = builder.CreateBitCast(ptr, int64PtrType, "atomic.ptr_i64");
#endif
    return builder.CreateAtomicRMW(
        llvm::AtomicRMWInst::Add, ptr, addVal, llvm::MaybeAlign(),
        llvm::AtomicOrdering::SequentiallyConsistent);
}

llvm::Value* CodeGenerator::generateAtomicI64Free(FunctionCallNode* node)
{
    if(node->arguments.size() != 1)
    {
        reportError(node->line, "atomic_i64_free expects one argument");
        return nullptr;
    }

    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    std::vector<TypeNode*> typeArgs;
    typeArgs.push_back(new StructTypeRefNode("Atomic64"));
    std::string handleTypeName =
        getOrCreateMonomorphizedStruct("Handle", typeArgs);
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], handleTypeName, node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* ptr =
        builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
    return builder.CreateCall(freeFunc, {ptr});
}

void CodeGenerator::generateStructMethods(StructDefNode* node)
{
    if(!node->members)
        return;

    // First, inherit methods from base struct if any
    if(!node->baseName.empty())
    {
        auto baseMethodsIt = structMethods.find(node->baseName);
        if(baseMethodsIt != structMethods.end())
        {
            // Copy all base methods to this struct
            for(const auto& methodPair : baseMethodsIt->second)
            {
                // Only inherit if not overridden by this struct
                bool overridden = false;
                for(auto method : node->members->methods)
                {
                    if(method->name == methodPair.first)
                    {
                        overridden = true;
                        break;
                    }
                }
                if(!overridden)
                {
                    structMethods[node->name][methodPair.first] =
                        methodPair.second;
                }
            }
        }
    }

    // Register all methods for this struct
    for(auto method : node->members->methods)
    {
        structMethods[node->name][method->name] =
            std::make_pair(method->isPublic, method);

        // Generate forward declaration
        generateMethodDeclaration(node->name, method);
    }
}

llvm::Function*
CodeGenerator::generateMethodDeclaration(const std::string& structName,
                                         StructMethodNode* method)
{
    // Method name is mangled: StructName_methodName
    std::string mangledName = structName + "_" + method->name;

    // Check if already declared
    if(module->getFunction(mangledName))
    {
        return module->getFunction(mangledName);
    }

    std::vector<llvm::Type*> paramTypes;

    // First parameter is pointer to struct (self)
    if(!method->isStatic)
    {
        llvm::Type* structType = getStructType(structName);
        if(structType)
        {
#if LLVM_VERSION_MAJOR >= 15
            paramTypes.push_back(llvm::PointerType::get(context, 0));
#else
            paramTypes.push_back(llvm::PointerType::get(structType, 0));
#endif
        }
    }

    // Add other parameters (skip 'self' if it's explicitly declared)
    for(auto param : method->parameters->parameters)
    {
        // Skip 'self' parameter - it's handled separately above
        if(param->name == "self")
            continue;

        llvm::Type* paramType = getLLVMTypeFromNode(param->type);
        if(!paramType)
        {
            reportError(param->line,
                        "unknown parameter type for '" + param->name + "'");
            paramType = llvm::Type::getInt32Ty(context);
        }
        paramTypes.push_back(paramType);
    }

    llvm::Type* returnType = getLLVMTypeFromNode(method->returnType);
    if(!returnType)
    {
        reportError(method->line,
                    "unknown return type for method '" + method->name + "'");
        returnType = llvm::Type::getVoidTy(context);
    }

    llvm::FunctionType* funcType =
        llvm::FunctionType::get(returnType, paramTypes, false);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, mangledName, module.get());

    // Set parameter names
    unsigned idx = 0;
    unsigned paramIdx = 0;
    for(auto& arg : function->args())
    {
        if(idx == 0 && !method->isStatic)
        {
            arg.setName("self");
        }
        else
        {
            // Find the next non-self parameter
            while(paramIdx < method->parameters->parameters.size() &&
                  method->parameters->parameters[paramIdx]->name == "self")
            {
                paramIdx++;
            }
            if(paramIdx < method->parameters->parameters.size())
            {
                arg.setName(method->parameters->parameters[paramIdx]->name);
                paramIdx++;
            }
        }
        idx++;
    }

    // Register static methods as callable qualified functions:
    // StructName::method(...)
    if(method->isStatic)
    {
        std::string qname = structName + "::" + method->name;
        auto& overloads = functionOverloads[qname];
        bool exists = false;
        for(const auto& ov : overloads)
        {
            if(ov.function == function)
            {
                exists = true;
                break;
            }
        }
        if(!exists)
        {
            std::string srcModule;
            auto sit = structVisibility.find(structName);
            if(sit != structVisibility.end())
                srcModule = sit->second.second;

            std::string signatureKey = qname + "#" + function->getName().str();

            FunctionOverloadInfo info{nullptr,
                                      function,
                                      signatureKey,
                                      function->getName().str(),
                                      false,
                                      method->isPublic,
                                      srcModule};
            overloads.push_back(info);
        }
    }

    return function;
}

llvm::Function*
CodeGenerator::generateMethodDefinition(const std::string& structName,
                                        StructMethodNode* method)
{
    std::string mangledName = structName + "_" + method->name;

    llvm::Function* function = module->getFunction(mangledName);
    if(!function)
    {
        function = generateMethodDeclaration(structName, method);
    }

    // Check if already has a body
    if(!function->empty())
    {
        return function;
    }

    if(!method->body)
    {
        reportError(method->line, "method '" + method->name + "' has no body");
        return function;
    }

    // Create entry block
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear scope
    namedValues.clear();
    constantVariables.clear();
    movedVariables.clear();
    pointerBorrowTarget.clear();
    activeBorrowers.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    // Set up self parameter and other parameters
    unsigned argIdx = 0;
    unsigned methodParamIdx = 0;
    for(auto& arg : function->args())
    {
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        builder.CreateStore(&arg, alloca);
        namedValues[std::string(arg.getName())] = alloca;
        recordVariableScopeDepth(std::string(arg.getName()));

        if(argIdx == 0 && !method->isStatic)
        {
            // 'self' is a pointer to the struct
            structVariableTypes["self"] = structName;
            variableTypes["self"] = TypeNode::TYPE_STRUCT;
        }
        else
        {
            // Find next non-self parameter
            while(methodParamIdx < method->parameters->parameters.size() &&
                  method->parameters->parameters[methodParamIdx]->name ==
                      "self")
            {
                methodParamIdx++;
            }
            if(methodParamIdx < method->parameters->parameters.size())
            {
                auto* paramNode =
                    method->parameters->parameters[methodParamIdx];
                variableTypes[std::string(arg.getName())] =
                    paramNode->type->kind;
                if(auto* structType =
                       dynamic_cast<StructTypeRefNode*>(paramNode->type))
                {
                    structVariableTypes[std::string(arg.getName())] =
                        structType->structName;
                }
                if(auto* genStructType =
                       dynamic_cast<GenericStructTypeRefNode*>(paramNode->type))
                {
                    std::string mangled = getOrCreateMonomorphizedStruct(
                        genStructType->structName, genStructType->typeArgs);
                    structVariableTypes[std::string(arg.getName())] = mangled;
                }
                if(auto* genListType =
                       dynamic_cast<GenericListTypeNode*>(paramNode->type))
                {
                    listElementTypes[std::string(arg.getName())] =
                        genListType->elementType;
                }
                if(auto* mapType = dynamic_cast<MapTypeNode*>(paramNode->type))
                {
                    mapKeyValueTypes[std::string(arg.getName())] =
                        std::make_pair(mapType->keyType, mapType->valueType);
                }
                if(auto* tupleType =
                       dynamic_cast<TupleTypeNode*>(paramNode->type))
                {
                    std::vector<TypeNode*> elemTypes;
                    for(auto* t : tupleType->elementTypes->types)
                        elemTypes.push_back(t);
                    tupleElementTypes[std::string(arg.getName())] = elemTypes;
                }
                if(auto* ptrType =
                       dynamic_cast<PointerTypeNode*>(paramNode->type))
                {
                    pointerElementTypes[std::string(arg.getName())] =
                        ptrType->elementType;
                }
                methodParamIdx++;
            }
        }
        argIdx++;
    }

    // Generate body
    for(auto stmt : method->body->statements)
    {
        generateStatement(stmt);
    }

    // Run scope-exit destructors for locals at normal method fallthrough.
    exitCleanupScope();

    // Add terminator if needed
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!currentBlock->getTerminator())
    {
        if(returnType->isVoidTy())
        {
            builder.CreateRetVoid();
        }
        else
        {
            builder.CreateUnreachable();
        }
    }

    llvm::verifyFunction(*function);
    return function;
}

llvm::Value* CodeGenerator::generateMethodCall(MethodCallNode* node)
{
    auto validateTemporaryBorrowArguments =
        [&](const std::vector<ExpressionNode*>& args,
            const std::string& calleeName) -> bool
    {
        std::set<std::string> borrowedInCall;
        for(auto* arg : args)
        {
            auto* unary = dynamic_cast<UnaryOpNode*>(arg);
            if(!unary || unary->op != UnaryOpNode::OP_ADDR)
                continue;

            std::string owner = resolveBorrowOwnerFromLValue(unary->operand);
            if(owner.empty())
                continue;

            if(globalNamedValues.find(owner) == globalNamedValues.end() &&
               isVariableMoved(owner))
            {
                reportError(node->line,
                            "cannot borrow moved value: '" + owner + "'");
                return false;
            }

            auto activeIt = activeBorrowers.find(owner);
            if(activeIt != activeBorrowers.end() && !activeIt->second.empty())
            {
                std::string by = *activeIt->second.begin();
                reportError(node->line, "cannot borrow '" + owner +
                                            "' while already borrowed by '" +
                                            by + "'");
                return false;
            }

            if(!borrowedInCall.insert(owner).second)
            {
                reportError(node->line,
                            "cannot borrow '" + owner +
                                "' multiple times in call to '" +
                                calleeName + "'");
                return false;
            }
        }
        return true;
    };

    // Get the object struct pointer and type
    // This supports both simple (p.method()) and chained (a.b.method()) access
    llvm::Value* objPtr;
    std::string structTypeName;

    auto* objId = dynamic_cast<IdentifierNode*>(node->object);
    if(objId)
    {
        if(globalNamedValues.find(objId->name) == globalNamedValues.end() &&
           isVariableMoved(objId->name))
        {
            reportError(node->line,
                        "use of moved value: '" + objId->name + "'");
            return nullptr;
        }

        // Simple case: identifier.method()
        auto typeIt = structVariableTypes.find(objId->name);
        if(typeIt == structVariableTypes.end())
        {
            reportError(node->line,
                        "'" + objId->name + "' is not a struct variable");
            return nullptr;
        }
        structTypeName = typeIt->second;

        objPtr = namedValues[objId->name];
        if(!objPtr)
        {
            reportError(node->line, "unknown variable: " + objId->name);
            return nullptr;
        }

        // Handle self pointer (alloca containing pointer)
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(objPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                objPtr = builder.CreateLoad(allocaType, alloca,
                                            objId->name + ".ptr");
            }
        }
    }
    else
    {
        // Chained case: a.b.method() - use helper to get struct pointer
        auto [ptr, typeName] = getStructPtrAndType(node->object, node->line);
        if(!ptr)
        {
            return nullptr;
        }
        objPtr = ptr;
        structTypeName = typeName;
    }

    // Check if method exists on this struct (including inherited methods)
    auto structIt = structMethods.find(structTypeName);
    if(structIt == structMethods.end())
    {
        reportError(node->line,
                    "struct '" + structTypeName + "' has no methods");
        return nullptr;
    }

    auto methodIt = structIt->second.find(node->methodName);
    if(methodIt == structIt->second.end())
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no method named '" +
                                    node->methodName + "'");
        return nullptr;
    }

    auto isResultType = [&](const std::string& typeName) -> bool
    {
        if(typeName == "Result")
            return true;
        auto it = mangledToGenericName.find(typeName);
        return it != mangledToGenericName.end() && it->second == "Result";
    };

    if(node->methodName == "unwrap" && isResultType(structTypeName))
    {
        if(node->line > 0)
        {
            std::cerr << "Warning (line " << node->line
                      << "): Result.unwrap() may panic on Err; consider "
                         "match/is_ok/is_err"
                      << std::endl;
        }
        else
        {
            std::cerr
                << "Warning: Result.unwrap() may panic on Err; consider "
                   "match/is_ok/is_err"
                << std::endl;
        }
    }

    bool isPublic = methodIt->second.first;
    StructMethodNode* methodNode = methodIt->second.second;

    // Check visibility - if calling from outside the struct's module, must be
    // public For now, we allow all calls within the same compilation unit
    // TODO: Add proper cross-module visibility checking for methods

    // Find the actual struct that defines this method (may be a base struct)
    std::string definingStruct = structTypeName;
    std::string searchStruct = structTypeName;
    while(!searchStruct.empty())
    {
        // Check if this struct directly defines the method (has it in its
        // members)
        std::string mangledName = searchStruct + "_" + node->methodName;
        if(module->getFunction(mangledName))
        {
            definingStruct = searchStruct;
            break;
        }
        // Move to base struct
        auto baseIt = structBases.find(searchStruct);
        if(baseIt != structBases.end())
        {
            searchStruct = baseIt->second;
        }
        else
        {
            break;
        }
    }

    std::string mangledName = definingStruct + "_" + node->methodName;
    llvm::Function* callee = module->getFunction(mangledName);
    if(!callee)
    {
        reportError(node->line, "unknown method: " + node->methodName);
        return nullptr;
    }

    // Check if this is a monomorphized struct method that needs body generation
    // If the function is declared but has no body (empty), generate it now
    if(callee->empty() && monomorphizedTypes.count(definingStruct))
    {
        // Find the method node
        auto structIt = structMethods.find(definingStruct);
        if(structIt != structMethods.end())
        {
            auto methodIt = structIt->second.find(node->methodName);
            if(methodIt != structIt->second.end())
            {
                StructMethodNode* methodDef = methodIt->second.second;
                if(methodDef && methodDef->body)
                {
                    // Save current state - generateMethodDefinition will clear
                    // these
                    llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
                    auto savedNamedValues = namedValues;
                    auto savedConstantVariables = constantVariables;
                    auto savedVariableTypes = variableTypes;
                    auto savedStructVariableTypes = structVariableTypes;
                    auto savedListElementTypes = listElementTypes;
                    auto savedMapKeyValueTypes = mapKeyValueTypes;
                    auto savedTupleElementTypes = tupleElementTypes;
                    auto savedPointerElementTypes = pointerElementTypes;
                    auto savedMovedVariables = movedVariables;
                    auto savedPointerBorrowTarget = pointerBorrowTarget;
                    auto savedActiveBorrowers = activeBorrowers;
                    auto savedVariableScopeDepth = variableScopeDepth;
                    auto savedCleanupScopes = cleanupScopes;
                    auto savedPointerBorrowScopes = pointerBorrowScopes;
                    auto savedVariableScopeDepthScopes =
                        variableScopeDepthScopes;

                    // Generate the method body
                    generateMethodDefinition(definingStruct, methodDef);

                    // Restore all state
                    namedValues = savedNamedValues;
                    constantVariables = savedConstantVariables;
                    variableTypes = savedVariableTypes;
                    structVariableTypes = savedStructVariableTypes;
                    listElementTypes = savedListElementTypes;
                    mapKeyValueTypes = savedMapKeyValueTypes;
                    tupleElementTypes = savedTupleElementTypes;
                    pointerElementTypes = savedPointerElementTypes;
                    movedVariables = savedMovedVariables;
                    pointerBorrowTarget = savedPointerBorrowTarget;
                    activeBorrowers = savedActiveBorrowers;
                    variableScopeDepth = savedVariableScopeDepth;
                    cleanupScopes = savedCleanupScopes;
                    pointerBorrowScopes = savedPointerBorrowScopes;
                    variableScopeDepthScopes =
                        savedVariableScopeDepthScopes;

                    if(savedBlock)
                    {
                        builder.SetInsertPoint(savedBlock);
                    }
                }
            }
        }
    }

    // Build arguments - first is pointer to struct
    std::vector<llvm::Value*> args;
    args.push_back(objPtr);

    if(!validateTemporaryBorrowArguments(node->arguments, node->methodName))
        return nullptr;

    // Add other arguments
    for(auto arg : node->arguments)
    {
        llvm::Value* argVal = generateExpression(arg);
        if(!argVal)
            return nullptr;
        consumeMoveFromExpression(arg, node->line,
                                  "passing argument to method '" +
                                      node->methodName + "'");
        args.push_back(argVal);
    }

    if(callee->getReturnType()->isVoidTy())
    {
        return builder.CreateCall(callee, args);
    }
    return builder.CreateCall(callee, args, "methodcall");
}

llvm::Value* CodeGenerator::generateCastExpression(CastExpressionNode* node)
{
    llvm::Value* value = generateExpression(node->expression);
    if(!value)
        return nullptr;

    llvm::Type* targetType = getLLVMType(node->targetType);
    llvm::Type* sourceType = value->getType();

    if(sourceType == targetType)
        return value;

    // Integer to float/double
    if(sourceType->isIntegerTy())
    {
        if(targetType->isFloatTy() || targetType->isDoubleTy())
        {
            return builder.CreateSIToFP(value, targetType, "casttmp");
        }
    }

    // Float/double to integer
    if(sourceType->isFloatingPointTy())
    {
        if(targetType->isIntegerTy())
        {
            return builder.CreateFPToSI(value, targetType, "casttmp");
        }
        // Float to double or double to float
        if(targetType->isFloatingPointTy())
        {
            return builder.CreateFPCast(value, targetType, "casttmp");
        }
    }

    return value;
}

llvm::Value* CodeGenerator::generateListLiteral(ListLiteralNode* node)
{
    // List structure: { i64 size, ptr data }
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    if(!node->elements || node->elements->elements.empty())
    {
        // Empty list: return {0, null}
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
        listStruct = builder.CreateInsertValue(
            listStruct, llvm::ConstantInt::get(i64Type, 0), 0);
        listStruct = builder.CreateInsertValue(
            listStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            1);
        return listStruct;
    }

    // Generate all elements
    std::vector<llvm::Value*> elementValues;
    llvm::Type* elementType = nullptr;

    for(auto* elem : node->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
            return nullptr;
        if(!elementType)
        {
            elementType = val->getType();
        }
        elementValues.push_back(val);
    }

    int64_t listSize = static_cast<int64_t>(elementValues.size());

    // Allocate array for elements
    llvm::Value* arraySizeVal = llvm::ConstantInt::get(i64Type, listSize);
    llvm::Value* dataAlloc =
        builder.CreateAlloca(elementType, arraySizeVal, "listdata");

    // Store each element
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        llvm::Value* idx = llvm::ConstantInt::get(i64Type, i);
        llvm::Value* elemPtr =
            builder.CreateGEP(elementType, dataAlloc, idx, "elemptr");
        builder.CreateStore(elementValues[i], elemPtr);
    }

    // Create list struct
    std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
    llvm::StructType* listStructType =
        llvm::StructType::get(context, listStructTypes);

    llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
    listStruct = builder.CreateInsertValue(
        listStruct, llvm::ConstantInt::get(i64Type, listSize), 0);
    listStruct = builder.CreateInsertValue(listStruct, dataAlloc, 1);

    return listStruct;
}

llvm::Value* CodeGenerator::generateMapLiteral(MapLiteralNode* node)
{
    // Map structure: { i64 size, ptr keys, ptr values }
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
    llvm::StructType* mapStructType =
        llvm::StructType::get(context, mapStructTypes);

    if(!node->entries || node->entries->entries.empty())
    {
        // Empty map
        llvm::Value* mapStruct = llvm::UndefValue::get(mapStructType);
        mapStruct = builder.CreateInsertValue(
            mapStruct, llvm::ConstantInt::get(i64Type, 0), 0);
        mapStruct = builder.CreateInsertValue(
            mapStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            1);
        mapStruct = builder.CreateInsertValue(
            mapStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            2);
        return mapStruct;
    }

    // Generate all key-value pairs
    std::vector<llvm::Value*> keyValues;
    std::vector<llvm::Value*> valueValues;
    llvm::Type* keyType = nullptr;
    llvm::Type* valueType = nullptr;

    for(auto* entry : node->entries->entries)
    {
        llvm::Value* keyVal = generateExpression(entry->key);
        llvm::Value* valVal = generateExpression(entry->value);
        if(!keyVal || !valVal)
            return nullptr;

        if(!keyType)
        {
            keyType = keyVal->getType();
            valueType = valVal->getType();
        }

        keyValues.push_back(keyVal);
        valueValues.push_back(valVal);
    }

    int64_t mapSize = static_cast<int64_t>(keyValues.size());

    // Allocate arrays for keys and values
    llvm::Value* sizeVal = llvm::ConstantInt::get(i64Type, mapSize);
    llvm::Value* keysAlloc = builder.CreateAlloca(keyType, sizeVal, "mapkeys");
    llvm::Value* valsAlloc =
        builder.CreateAlloca(valueType, sizeVal, "mapvals");

    // Store each key-value pair
    for(size_t i = 0; i < keyValues.size(); ++i)
    {
        llvm::Value* idx = llvm::ConstantInt::get(i64Type, i);
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysAlloc, idx, "keyptr");
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsAlloc, idx, "valptr");
        builder.CreateStore(keyValues[i], keyPtr);
        builder.CreateStore(valueValues[i], valPtr);
    }

    // Create map struct
    llvm::Value* mapStruct = llvm::UndefValue::get(mapStructType);
    mapStruct = builder.CreateInsertValue(
        mapStruct, llvm::ConstantInt::get(i64Type, mapSize), 0);
    mapStruct = builder.CreateInsertValue(mapStruct, keysAlloc, 1);
    mapStruct = builder.CreateInsertValue(mapStruct, valsAlloc, 2);

    return mapStruct;
}

llvm::Value* CodeGenerator::generateIndexExpression(IndexExpressionNode* node)
{
    // Get the base (list or map variable)
    auto* baseId = dynamic_cast<IdentifierNode*>(node->base);
    if(!baseId)
    {
        reportError(node->line, "index expression requires an identifier");
        return nullptr;
    }

    llvm::Value* basePtr = namedValues[baseId->name];
    if(!basePtr)
    {
        reportError(node->line, "unknown variable: " + baseId->name);
        return nullptr;
    }

    llvm::Value* indexVal = generateExpression(node->index);
    if(!indexVal)
        return nullptr;

    // Check if it's a list
    auto listIt = listElementTypes.find(baseId->name);
    if(listIt != listElementTypes.end())
    {
        // List indexing
        TypeNode* elemTypeNode = listIt->second;
        llvm::Type* elementType = getLLVMType(elemTypeNode->kind);

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(elementType, 0);
#endif

        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        // Load list struct
        llvm::Value* listStruct =
            builder.CreateLoad(listStructType, basePtr, "list");
        llvm::Value* dataPtr =
            builder.CreateExtractValue(listStruct, 1, "data");

        // Ensure index is i64
        if(indexVal->getType() != i64Type)
        {
            indexVal = builder.CreateSExtOrTrunc(indexVal, i64Type, "idx64");
        }

        // Get element pointer and load
        llvm::Value* elemPtr =
            builder.CreateGEP(elementType, dataPtr, indexVal, "elemptr");
        return builder.CreateLoad(elementType, elemPtr, "elem");
    }

    // Check if it's a map
    auto mapIt = mapKeyValueTypes.find(baseId->name);
    if(mapIt != mapKeyValueTypes.end())
    {
        // Map lookup - linear search for key
        TypeNode* keyTypeNode = mapIt->second.first;
        TypeNode* valTypeNode = mapIt->second.second;
        llvm::Type* keyType = getLLVMType(keyTypeNode->kind);
        llvm::Type* valueType = getLLVMType(valTypeNode->kind);

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(keyType, 0);
#endif

        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        // Load map struct
        llvm::Value* mapStruct =
            builder.CreateLoad(mapStructType, basePtr, "map");
        llvm::Value* mapSize = builder.CreateExtractValue(mapStruct, 0, "size");
        llvm::Value* keysPtr = builder.CreateExtractValue(mapStruct, 1, "keys");
        llvm::Value* valsPtr = builder.CreateExtractValue(mapStruct, 2, "vals");

        // Linear search loop for key
        llvm::Function* function = builder.GetInsertBlock()->getParent();

        llvm::AllocaInst* idxVar =
            builder.CreateAlloca(i64Type, nullptr, "mapidx");
        builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), idxVar);

        llvm::AllocaInst* resultVar =
            builder.CreateAlloca(valueType, nullptr, "mapresult");
        // Initialize with default value
        builder.CreateStore(llvm::Constant::getNullValue(valueType), resultVar);

        llvm::BasicBlock* condBB =
            llvm::BasicBlock::Create(context, "map.cond", function);
        llvm::BasicBlock* bodyBB =
            llvm::BasicBlock::Create(context, "map.body");
        llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "map.inc");
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "map.end");

        builder.CreateBr(condBB);

        builder.SetInsertPoint(condBB);
        llvm::Value* currentIdx = builder.CreateLoad(i64Type, idxVar, "idx");
        llvm::Value* cond =
            builder.CreateICmpSLT(currentIdx, mapSize, "mapcond");
        builder.CreateCondBr(cond, bodyBB, endBB);

        bodyBB->insertInto(function);
        builder.SetInsertPoint(bodyBB);

        // Compare keys
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysPtr, currentIdx, "keyptr");
        llvm::Value* currentKey = builder.CreateLoad(keyType, keyPtr, "curkey");

        llvm::Value* keyMatch;
        if(keyType->isIntegerTy())
        {
            keyMatch = builder.CreateICmpEQ(currentKey, indexVal, "keymatch");
        }
        else if(keyType->isFloatingPointTy())
        {
            keyMatch = builder.CreateFCmpOEQ(currentKey, indexVal, "keymatch");
        }
        else
        {
            // For strings/pointers, need strcmp or pointer comparison
            keyMatch = builder.CreateICmpEQ(currentKey, indexVal, "keymatch");
        }

        llvm::BasicBlock* foundBB =
            llvm::BasicBlock::Create(context, "map.found", function);
        builder.CreateCondBr(keyMatch, foundBB, incBB);

        builder.SetInsertPoint(foundBB);
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsPtr, currentIdx, "valptr");
        llvm::Value* foundVal =
            builder.CreateLoad(valueType, valPtr, "foundval");
        builder.CreateStore(foundVal, resultVar);
        builder.CreateBr(endBB);

        incBB->insertInto(function);
        builder.SetInsertPoint(incBB);
        llvm::Value* nextIdx =
            builder.CreateAdd(builder.CreateLoad(i64Type, idxVar, ""),
                              llvm::ConstantInt::get(i64Type, 1), "nextidx");
        builder.CreateStore(nextIdx, idxVar);
        builder.CreateBr(condBB);

        endBB->insertInto(function);
        builder.SetInsertPoint(endBB);

        return builder.CreateLoad(valueType, resultVar, "mapval");
    }

    reportError(node->line,
                "cannot index non-list/non-map variable: " + baseId->name);
    return nullptr;
}

llvm::Value* CodeGenerator::generateTupleLiteral(TupleLiteralNode* node)
{
    if(!node->elements || node->elements->elements.empty())
    {
        reportError(node->line, "tuple must have at least one element");
        return nullptr;
    }

    // Generate all elements and determine their types
    std::vector<llvm::Value*> elementValues;
    std::vector<llvm::Type*> elementTypes;

    for(auto* elem : node->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
            return nullptr;
        elementValues.push_back(val);
        elementTypes.push_back(val->getType());
    }

    // Create the tuple struct type
    llvm::StructType* tupleType = llvm::StructType::get(context, elementTypes);

    // Create the tuple value
    llvm::Value* tupleVal = llvm::UndefValue::get(tupleType);
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        tupleVal = builder.CreateInsertValue(
            tupleVal, elementValues[i], static_cast<unsigned>(i), "tuple.elem");
    }

    return tupleVal;
}

llvm::Value* CodeGenerator::generateTupleAccess(TupleAccessNode* node)
{
    // Get the tuple variable
    auto* baseId = dynamic_cast<IdentifierNode*>(node->tuple);
    if(!baseId)
    {
        reportError(node->line, "tuple access requires an identifier");
        return nullptr;
    }

    llvm::Value* tuplePtr = namedValues[baseId->name];
    if(!tuplePtr)
    {
        reportError(node->line, "unknown variable: " + baseId->name);
        return nullptr;
    }

    // Check if it's a tuple
    auto it = tupleElementTypes.find(baseId->name);
    if(it == tupleElementTypes.end())
    {
        reportError(node->line, "cannot access tuple element: '" +
                                    baseId->name + "' is not a tuple");
        return nullptr;
    }

    const std::vector<TypeNode*>& elemTypes = it->second;

    // Bounds check
    if(node->index < 0 || static_cast<size_t>(node->index) >= elemTypes.size())
    {
        reportError(node->line, "tuple index " + std::to_string(node->index) +
                                    " out of bounds (tuple has " +
                                    std::to_string(elemTypes.size()) +
                                    " elements)");
        return nullptr;
    }

    // Build the tuple struct type using getLLVMTypeFromNode for proper struct
    // support
    std::vector<llvm::Type*> tupleTypes;
    for(auto* t : elemTypes)
    {
        llvm::Type* elemType = getLLVMTypeFromNode(t);
        if(!elemType)
        {
            reportError(node->line, "invalid type in tuple");
            return nullptr;
        }
        tupleTypes.push_back(elemType);
    }
    llvm::StructType* tupleStructType =
        llvm::StructType::get(context, tupleTypes);

    // Load the tuple struct
    llvm::Value* tupleVal =
        builder.CreateLoad(tupleStructType, tuplePtr, "tuple");

    // Extract the element
    return builder.CreateExtractValue(
        tupleVal, static_cast<unsigned>(node->index), "tuple.elem");
}

llvm::Value* CodeGenerator::generateStructLiteral(StructLiteralNode* node)
{
    std::string structTypeName = node->structName;

    // Check if this is a generic struct instantiation (has type arguments)
    if(node->structName == "Result" && node->typeArgs.empty())
    {
        reportError(node->line,
                    "Result literals require type arguments (e.g. "
                    "Ok<i32, string>(...))");
        return nullptr;
    }
    if(node->structName == "Option" && node->typeArgs.empty())
    {
        reportError(node->line,
                    "Option literals require type arguments (e.g. "
                    "Some<i32>(...))");
        return nullptr;
    }
    if(!node->typeArgs.empty())
        if(!node->typeArgs.empty())
        {
            // Convert typeArgs from strings to TypeNodes
            // The typeArgs in StructLiteralNode are stored as strings from the
            // parser We need to look them up and create proper TypeNode
            // references
            std::vector<TypeNode*> typeArgNodes;
            for(const auto& typeArgStr : node->typeArgs)
            {
                // Try to create a TypeNode from the type argument string
                TypeNode* typeArg = nullptr;

                // Check if it's a basic type
                if(typeArgStr == "i8")
                    typeArg = new TypeNode(TypeNode::TYPE_I8);
                else if(typeArgStr == "i16")
                    typeArg = new TypeNode(TypeNode::TYPE_I16);
                else if(typeArgStr == "i32")
                    typeArg = new TypeNode(TypeNode::TYPE_I32);
                else if(typeArgStr == "i64")
                    typeArg = new TypeNode(TypeNode::TYPE_I64);
                else if(typeArgStr == "u8")
                    typeArg = new TypeNode(TypeNode::TYPE_U8);
                else if(typeArgStr == "u16")
                    typeArg = new TypeNode(TypeNode::TYPE_U16);
                else if(typeArgStr == "u32")
                    typeArg = new TypeNode(TypeNode::TYPE_U32);
                else if(typeArgStr == "u64")
                    typeArg = new TypeNode(TypeNode::TYPE_U64);
                else if(typeArgStr == "int")
                    typeArg = new TypeNode(TypeNode::TYPE_INT);
                else if(typeArgStr == "float")
                    typeArg = new TypeNode(TypeNode::TYPE_FLOAT);
                else if(typeArgStr == "double")
                    typeArg = new TypeNode(TypeNode::TYPE_DOUBLE);
                else if(typeArgStr == "bool")
                    typeArg = new TypeNode(TypeNode::TYPE_BOOL);
                else if(typeArgStr == "string")
                    typeArg = new TypeNode(TypeNode::TYPE_STRING);
                else if(typeArgStr == "str8")
                    typeArg = new TypeNode(TypeNode::TYPE_STR8);
                else if(typeArgStr == "str16")
                    typeArg = new TypeNode(TypeNode::TYPE_STR16);
                else
                {
                    // Assume it's a struct type reference
                    typeArg = new StructTypeRefNode(typeArgStr);
                }

                typeArgNodes.push_back(typeArg);
            }

            // Get or create the monomorphized struct type
            structTypeName =
                getOrCreateMonomorphizedStruct(node->structName, typeArgNodes);
        }

    // Get the struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return nullptr;
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "no member info for struct: " + structTypeName);
        return nullptr;
    }
    const auto& members = memberIt->second;

    // Build the struct value
    llvm::Value* structVal = llvm::Constant::getNullValue(structType);

    // Process each field initialization
    for(const auto& fieldInit : node->fields)
    {
        const std::string& fieldName = fieldInit.first;
        ExpressionNode* valueExpr = fieldInit.second;

        // Find the member index
        int memberIndex = -1;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldName)
            {
                memberIndex = static_cast<int>(i);
                break;
            }
        }

        if(memberIndex < 0)
        {
            reportError(node->line, "unknown field '" + fieldName +
                                        "' in struct '" + structTypeName + "'");
            return nullptr;
        }

        // Generate the field value
        llvm::Value* fieldValue = generateExpression(valueExpr);
        if(!fieldValue)
        {
            reportError(node->line, "failed to generate value for field '" +
                                        fieldName + "'");
            return nullptr;
        }

        // Get the expected field type from the struct
        llvm::Type* expectedType = structType->getElementType(memberIndex);
        llvm::Type* actualType = fieldValue->getType();

        // Convert value to expected type if needed
        if(actualType != expectedType)
        {
            if(actualType->isIntegerTy() && expectedType->isIntegerTy())
            {
                unsigned actualBits = actualType->getIntegerBitWidth();
                unsigned expectedBits = expectedType->getIntegerBitWidth();
                if(actualBits > expectedBits)
                {
                    fieldValue =
                        builder.CreateTrunc(fieldValue, expectedType, "trunc");
                }
                else if(actualBits < expectedBits)
                {
                    fieldValue =
                        builder.CreateSExt(fieldValue, expectedType, "sext");
                }
            }
            else if(actualType->isFloatingPointTy() &&
                    expectedType->isFloatingPointTy())
            {
                fieldValue =
                    builder.CreateFPCast(fieldValue, expectedType, "fpcast");
            }
            else if(actualType->isIntegerTy() &&
                    expectedType->isFloatingPointTy())
            {
                fieldValue =
                    builder.CreateSIToFP(fieldValue, expectedType, "sitofp");
            }
            else if(actualType->isFloatingPointTy() &&
                    expectedType->isIntegerTy())
            {
                fieldValue =
                    builder.CreateFPToSI(fieldValue, expectedType, "fptosi");
            }
            else
            {
                // Types are incompatible - report error
                std::string actualTypeStr, expectedTypeStr;

                // Get actual type name
                if(actualType->isIntegerTy())
                    actualTypeStr =
                        "i" + std::to_string(actualType->getIntegerBitWidth());
                else if(actualType->isFloatTy())
                    actualTypeStr = "float";
                else if(actualType->isDoubleTy())
                    actualTypeStr = "double";
                else if(actualType->isPointerTy())
                    actualTypeStr = "pointer";
                else if(actualType->isStructTy())
                    actualTypeStr = actualType->getStructName().str().empty()
                                        ? "struct"
                                        : actualType->getStructName().str();
                else
                    actualTypeStr = "unknown";

                // Get expected type name
                if(expectedType->isIntegerTy())
                    expectedTypeStr =
                        "i" +
                        std::to_string(expectedType->getIntegerBitWidth());
                else if(expectedType->isFloatTy())
                    expectedTypeStr = "float";
                else if(expectedType->isDoubleTy())
                    expectedTypeStr = "double";
                else if(expectedType->isPointerTy())
                    expectedTypeStr = "pointer";
                else if(expectedType->isStructTy())
                    expectedTypeStr =
                        expectedType->getStructName().str().empty()
                            ? "struct"
                            : expectedType->getStructName().str();
                else
                    expectedTypeStr = "unknown";

                reportError(node->line, "type mismatch for field '" +
                                            fieldName + "' in struct '" +
                                            structTypeName + "': expected '" +
                                            expectedTypeStr + "', got '" +
                                            actualTypeStr + "'");
                return nullptr;
            }
        }

        // Insert the value into the struct
        structVal = builder.CreateInsertValue(
            structVal, fieldValue, static_cast<unsigned>(memberIndex),
            structTypeName + "." + fieldName);
    }

    return structVal;
}

llvm::Value* CodeGenerator::generateMatchExpression(MatchExpressionNode* node)
{
    if(!node || !node->target)
        return nullptr;

    llvm::Value* matchVal = generateExpression(node->target);
    if(!matchVal)
        return nullptr;
    auto incomingMoved = movedVariables;
    auto incomingPointerBorrowTarget = pointerBorrowTarget;
    auto incomingActiveBorrowers = activeBorrowers;

    bool hasOk = false;
    bool hasErr = false;
    bool hasLiteral = false;
    bool hasSome = false;
    bool hasNone = false;
    MatchArmNode* okArm = nullptr;
    MatchArmNode* errArm = nullptr;
    MatchArmNode* someArm = nullptr;
    MatchArmNode* noneArm = nullptr;
    MatchArmNode* wildcardArm = nullptr;
    std::vector<MatchArmNode*> literalArms;
    for(auto* arm : node->arms)
    {
        if(!arm || !arm->pattern)
            continue;
        switch(arm->pattern->kind)
        {
        case MatchPatternNode::PATTERN_OK:
            hasOk = true;
            if(okArm)
            {
                reportError(node->line, "duplicate Ok match arm");
                return nullptr;
            }
            okArm = arm;
            break;
        case MatchPatternNode::PATTERN_ERR:
            hasErr = true;
            if(errArm)
            {
                reportError(node->line, "duplicate Err match arm");
                return nullptr;
            }
            errArm = arm;
            break;
        case MatchPatternNode::PATTERN_SOME:
            hasSome = true;
            if(someArm)
            {
                reportError(node->line, "duplicate Some match arm");
                return nullptr;
            }
            someArm = arm;
            break;
        case MatchPatternNode::PATTERN_NONE:
            hasNone = true;
            if(noneArm)
            {
                reportError(node->line, "duplicate None match arm");
                return nullptr;
            }
            noneArm = arm;
            break;
        case MatchPatternNode::PATTERN_LITERAL:
            hasLiteral = true;
            literalArms.push_back(arm);
            break;
        case MatchPatternNode::PATTERN_WILDCARD:
            if(wildcardArm)
            {
                reportError(node->line, "duplicate wildcard match arm");
                return nullptr;
            }
            wildcardArm = arm;
            break;
        }
    }

    bool hasResult = hasOk || hasErr;
    bool hasOption = hasSome || hasNone;
    if(hasResult && (hasLiteral || hasOption))
    {
        reportError(node->line,
                    "match cannot mix Result and other patterns");
        return nullptr;
    }
    if(hasOption && hasLiteral)
    {
        reportError(node->line,
                    "match cannot mix Option and literal patterns");
        return nullptr;
    }

    auto bindValue = [&](const std::string& name, TypeNode* type,
                         llvm::Value* value)
    {
        if(name.empty() || !type || !value)
            return;

        llvm::Type* llvmType = getLLVMTypeFromNode(type);
        if(!llvmType)
            return;

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(llvmType, nullptr, name);
        builder.CreateStore(value, alloca);
        namedValues[name] = alloca;

        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_STRUCT;
            structVariableTypes[name] = structRef->structName;
        }
        else if(auto* genRef =
                    dynamic_cast<GenericStructTypeRefNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_STRUCT;
            structVariableTypes[name] = getOrCreateMonomorphizedStruct(
                genRef->structName, genRef->typeArgs);
        }
        else if(auto* listType =
                    dynamic_cast<GenericListTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_LIST;
            listElementTypes[name] = listType->elementType;
        }
        else if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_MAP;
            mapKeyValueTypes[name] =
                std::make_pair(mapType->keyType, mapType->valueType);
        }
        else if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_TUPLE;
            std::vector<TypeNode*> elemTypes;
            for(auto* t : tupleType->elementTypes->types)
                elemTypes.push_back(t);
            tupleElementTypes[name] = elemTypes;
        }
        else if(auto* ptrType = dynamic_cast<PointerTypeNode*>(type))
        {
            variableTypes[name] = TypeNode::TYPE_PTR;
            pointerElementTypes[name] = ptrType->elementType;
        }
        else
        {
            variableTypes[name] = type->kind;
        }
    };

    auto generateArmValue =
        [&](MatchArmNode* arm, int valueIndex, TypeNode* valueType,
            std::set<std::string>* outMovedState,
            std::map<std::string, std::string>* outPointerBorrowTarget,
            std::map<std::string, std::set<std::string>>* outActiveBorrowers)
        -> llvm::Value*
    {
        auto savedNamedValues = namedValues;
        auto savedVariableTypes = variableTypes;
        auto savedStructVariableTypes = structVariableTypes;
        auto savedListElementTypes = listElementTypes;
        auto savedMapKeyValueTypes = mapKeyValueTypes;
        auto savedTupleElementTypes = tupleElementTypes;
        auto savedPointerElementTypes = pointerElementTypes;
        auto savedMovedVariables = movedVariables;
        auto savedPointerBorrowTarget = pointerBorrowTarget;
        auto savedActiveBorrowers = activeBorrowers;
        auto savedCleanupScopes = cleanupScopes;

        std::string binding =
            arm && arm->pattern ? arm->pattern->binding : "";
        if(!binding.empty())
        {
            llvm::Value* payload = builder.CreateExtractValue(
                matchVal, valueIndex, "match.val");
            bindValue(binding, valueType, payload);
        }

        llvm::Value* armValue =
            arm ? generateExpression(arm->expression) : nullptr;
        if(outMovedState)
            *outMovedState = movedVariables;
        if(outPointerBorrowTarget)
            *outPointerBorrowTarget = pointerBorrowTarget;
        if(outActiveBorrowers)
            *outActiveBorrowers = activeBorrowers;

        namedValues = savedNamedValues;
        variableTypes = savedVariableTypes;
        structVariableTypes = savedStructVariableTypes;
        listElementTypes = savedListElementTypes;
        mapKeyValueTypes = savedMapKeyValueTypes;
        tupleElementTypes = savedTupleElementTypes;
        pointerElementTypes = savedPointerElementTypes;
        movedVariables = savedMovedVariables;
        pointerBorrowTarget = savedPointerBorrowTarget;
        activeBorrowers = savedActiveBorrowers;
        cleanupScopes = savedCleanupScopes;

        return armValue;
    };

    if(hasOk || hasErr)
    {
        llvm::Type* matchType = matchVal->getType();
        if(!matchType->isStructTy())
        {
            reportError(node->line, "match expects a Result value");
            return nullptr;
        }

        auto* structType = llvm::cast<llvm::StructType>(matchType);
        std::string structName = structType->getName().str();
        if(structName.empty())
        {
            reportError(node->line, "match expects a named struct type");
            return nullptr;
        }

        auto memIt = structMembers.find(structName);
        if(memIt == structMembers.end())
        {
            reportError(node->line, "unknown struct type: " + structName);
            return nullptr;
        }

        int isOkIndex = -1;
        int okIndex = -1;
        int errIndex = -1;
        TypeNode* okType = nullptr;
        TypeNode* errType = nullptr;

        const auto& members = memIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == "is_ok")
                isOkIndex = static_cast<int>(i);
            else if(members[i].first == "ok")
            {
                okIndex = static_cast<int>(i);
                okType = members[i].second;
            }
            else if(members[i].first == "err")
            {
                errIndex = static_cast<int>(i);
                errType = members[i].second;
            }
        }

        if(isOkIndex < 0 || okIndex < 0 || errIndex < 0)
        {
            reportError(node->line,
                        "match expects Result with is_ok/ok/err fields");
            return nullptr;
        }

        if(!okArm)
            okArm = wildcardArm;
        if(!errArm)
            errArm = wildcardArm;
        if(!okArm || !errArm)
        {
            reportError(node->line,
                        "match requires Ok and Err arms or wildcard");
            return nullptr;
        }

        llvm::Function* func = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* okBB =
            llvm::BasicBlock::Create(context, "match.ok", func);
        llvm::BasicBlock* errBB =
            llvm::BasicBlock::Create(context, "match.err", func);
        llvm::BasicBlock* mergeBB =
            llvm::BasicBlock::Create(context, "match.merge", func);

        llvm::Value* isOkVal =
            builder.CreateExtractValue(matchVal, isOkIndex, "match.is_ok");
        builder.CreateCondBr(isOkVal, okBB, errBB);

        builder.SetInsertPoint(okBB);
        std::set<std::string> okMovedState;
        std::map<std::string, std::string> okPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> okActiveBorrowers;
        llvm::Value* okValue =
            generateArmValue(okArm, okIndex, okType, &okMovedState,
                             &okPointerBorrowTarget, &okActiveBorrowers);
        if(!okValue)
            return nullptr;
        llvm::BasicBlock* okEnd = builder.GetInsertBlock();
        bool okFallsThrough = (okEnd->getTerminator() == nullptr);
        if(okFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(errBB);
        std::set<std::string> errMovedState;
        std::map<std::string, std::string> errPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> errActiveBorrowers;
        llvm::Value* errValue =
            generateArmValue(errArm, errIndex, errType, &errMovedState,
                             &errPointerBorrowTarget, &errActiveBorrowers);
        if(!errValue)
            return nullptr;
        llvm::BasicBlock* errEnd = builder.GetInsertBlock();
        bool errFallsThrough = (errEnd->getTerminator() == nullptr);
        if(errFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(mergeBB);
        std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> armValues;
        std::vector<std::set<std::string>> armMovedStates;
        std::vector<std::map<std::string, std::string>> armPointerBorrowStates;
        std::vector<std::map<std::string, std::set<std::string>>> armActiveBorrowerStates;
        if(okFallsThrough)
        {
            armValues.push_back({okValue, okEnd});
            armMovedStates.push_back(okMovedState);
            armPointerBorrowStates.push_back(okPointerBorrowTarget);
            armActiveBorrowerStates.push_back(okActiveBorrowers);
        }
        if(errFallsThrough)
        {
            armValues.push_back({errValue, errEnd});
            armMovedStates.push_back(errMovedState);
            armPointerBorrowStates.push_back(errPointerBorrowTarget);
            armActiveBorrowerStates.push_back(errActiveBorrowers);
        }
        if(armValues.empty())
        {
            reportError(node->line,
                        "match expression has no continuing arm");
            return nullptr;
        }
        llvm::Type* okValueType = armValues[0].first->getType();

        auto castInBlock = [&](llvm::Value* val, llvm::Type* target,
                               llvm::BasicBlock* block) -> llvm::Value*
        {
            if(!val || !target || !block)
                return val;
            llvm::Type* src = val->getType();
            if(src == target)
                return val;
            llvm::IRBuilder<> castBuilder(block->getTerminator());
            if(src->isIntegerTy() && target->isIntegerTy())
            {
                unsigned srcBits = src->getIntegerBitWidth();
                unsigned dstBits = target->getIntegerBitWidth();
                if(srcBits > dstBits)
                    return castBuilder.CreateTrunc(val, target, "match.trunc");
                if(srcBits < dstBits)
                    return castBuilder.CreateSExt(val, target, "match.sext");
                return val;
            }
            if(src->isIntegerTy() && target->isFloatingPointTy())
                return castBuilder.CreateSIToFP(val, target, "match.sitofp");
            if(src->isFloatingPointTy() && target->isFloatingPointTy())
                return castBuilder.CreateFPCast(val, target, "match.fpcast");
            return val;
        };

        if(armValues.size() == 2 &&
           armValues[0].first->getType() != armValues[1].first->getType())
        {
            llvm::Type* errValueType = armValues[1].first->getType();
            llvm::Type* commonType = nullptr;
            if(okValueType->isIntegerTy() && errValueType->isIntegerTy())
            {
                unsigned okBits = okValueType->getIntegerBitWidth();
                unsigned errBits = errValueType->getIntegerBitWidth();
                commonType = okBits >= errBits ? okValueType : errValueType;
            }
            else if(okValueType->isFloatingPointTy() &&
                    errValueType->isFloatingPointTy())
            {
                commonType = okValueType->isDoubleTy() ||
                                     errValueType->isDoubleTy()
                                 ? llvm::Type::getDoubleTy(context)
                                 : llvm::Type::getFloatTy(context);
            }
            else if(okValueType->isFloatingPointTy() &&
                    errValueType->isIntegerTy())
            {
                commonType = okValueType;
            }
            else if(okValueType->isIntegerTy() &&
                    errValueType->isFloatingPointTy())
            {
                commonType = errValueType;
            }

            if(!commonType)
            {
                reportError(node->line, "match arm types do not match");
                return nullptr;
            }

            armValues[0].first =
                castInBlock(armValues[0].first, commonType, armValues[0].second);
            armValues[1].first =
                castInBlock(armValues[1].first, commonType, armValues[1].second);
            okValueType = commonType;
        }

        if(okValueType->isVoidTy())
        {
            reportError(node->line, "match arms must return a value");
            return nullptr;
        }

        llvm::PHINode* phi =
            builder.CreatePHI(okValueType, (unsigned)armValues.size(),
                              "match.result");
        for(const auto& pair : armValues)
            phi->addIncoming(pair.first, pair.second);
        std::set<std::string> mergedMoved;
        std::map<std::string, std::string> mergedPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> mergedActiveBorrowers;
        for(const auto& st : armMovedStates)
            mergedMoved.insert(st.begin(), st.end());
        for(const auto& ptrState : armPointerBorrowStates)
        {
            for(const auto& kv : ptrState)
            {
                if(mergedPointerBorrowTarget.find(kv.first) ==
                   mergedPointerBorrowTarget.end())
                {
                    mergedPointerBorrowTarget[kv.first] = kv.second;
                }
            }
        }
        for(const auto& borrowersState : armActiveBorrowerStates)
        {
            for(const auto& kv : borrowersState)
            {
                auto& dst = mergedActiveBorrowers[kv.first];
                dst.insert(kv.second.begin(), kv.second.end());
            }
        }
        movedVariables = std::move(mergedMoved);
        pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
        activeBorrowers = std::move(mergedActiveBorrowers);
        return phi;
    }

    if(hasOption)
    {
        llvm::Type* matchType = matchVal->getType();
        if(!matchType->isStructTy())
        {
            reportError(node->line, "match expects an Option value");
            return nullptr;
        }

        auto* structType = llvm::cast<llvm::StructType>(matchType);
        std::string structName = structType->getName().str();
        if(structName.empty())
        {
            reportError(node->line, "match expects a named struct type");
            return nullptr;
        }

        auto memIt = structMembers.find(structName);
        if(memIt == structMembers.end())
        {
            reportError(node->line, "unknown struct type: " + structName);
            return nullptr;
        }

        int isSomeIndex = -1;
        int valueIndex = -1;
        TypeNode* valueType = nullptr;

        const auto& members = memIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == "is_some")
                isSomeIndex = static_cast<int>(i);
            else if(members[i].first == "value")
            {
                valueIndex = static_cast<int>(i);
                valueType = members[i].second;
            }
        }

        if(isSomeIndex < 0 || valueIndex < 0)
        {
            reportError(node->line,
                        "match expects Option with is_some/value fields");
            return nullptr;
        }

        if(!someArm)
            someArm = wildcardArm;
        if(!noneArm)
            noneArm = wildcardArm;
        if(!someArm || !noneArm)
        {
            reportError(node->line,
                        "match requires Some and None arms or wildcard");
            return nullptr;
        }

        llvm::Function* func = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* someBB =
            llvm::BasicBlock::Create(context, "match.some", func);
        llvm::BasicBlock* noneBB =
            llvm::BasicBlock::Create(context, "match.none", func);
        llvm::BasicBlock* mergeBB =
            llvm::BasicBlock::Create(context, "match.merge", func);

        llvm::Value* isSomeVal =
            builder.CreateExtractValue(matchVal, isSomeIndex, "match.is_some");
        builder.CreateCondBr(isSomeVal, someBB, noneBB);

        builder.SetInsertPoint(someBB);
        std::set<std::string> someMovedState;
        std::map<std::string, std::string> somePointerBorrowTarget;
        std::map<std::string, std::set<std::string>> someActiveBorrowers;
        llvm::Value* someValue = generateArmValue(someArm, valueIndex,
                                                  valueType, &someMovedState,
                                                  &somePointerBorrowTarget,
                                                  &someActiveBorrowers);
        if(!someValue)
            return nullptr;
        llvm::BasicBlock* someEnd = builder.GetInsertBlock();
        bool someFallsThrough = (someEnd->getTerminator() == nullptr);
        if(someFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(noneBB);
        std::set<std::string> noneMovedState;
        std::map<std::string, std::string> nonePointerBorrowTarget;
        std::map<std::string, std::set<std::string>> noneActiveBorrowers;
        llvm::Value* noneValue = generateArmValue(noneArm, valueIndex,
                                                  valueType, &noneMovedState,
                                                  &nonePointerBorrowTarget,
                                                  &noneActiveBorrowers);
        if(!noneValue)
            return nullptr;
        llvm::BasicBlock* noneEnd = builder.GetInsertBlock();
        bool noneFallsThrough = (noneEnd->getTerminator() == nullptr);
        if(noneFallsThrough)
            builder.CreateBr(mergeBB);

        builder.SetInsertPoint(mergeBB);
        std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> armValues;
        std::vector<std::set<std::string>> armMovedStates;
        std::vector<std::map<std::string, std::string>> armPointerBorrowStates;
        std::vector<std::map<std::string, std::set<std::string>>> armActiveBorrowerStates;
        if(someFallsThrough)
        {
            armValues.push_back({someValue, someEnd});
            armMovedStates.push_back(someMovedState);
            armPointerBorrowStates.push_back(somePointerBorrowTarget);
            armActiveBorrowerStates.push_back(someActiveBorrowers);
        }
        if(noneFallsThrough)
        {
            armValues.push_back({noneValue, noneEnd});
            armMovedStates.push_back(noneMovedState);
            armPointerBorrowStates.push_back(nonePointerBorrowTarget);
            armActiveBorrowerStates.push_back(noneActiveBorrowers);
        }
        if(armValues.empty())
        {
            reportError(node->line,
                        "match expression has no continuing arm");
            return nullptr;
        }
        llvm::Type* someValueType = armValues[0].first->getType();

        auto castInBlock = [&](llvm::Value* val, llvm::Type* target,
                               llvm::BasicBlock* block) -> llvm::Value*
        {
            if(!val || !target || !block)
                return val;
            llvm::Type* src = val->getType();
            if(src == target)
                return val;
            llvm::IRBuilder<> castBuilder(block->getTerminator());
            if(src->isIntegerTy() && target->isIntegerTy())
            {
                unsigned srcBits = src->getIntegerBitWidth();
                unsigned dstBits = target->getIntegerBitWidth();
                if(srcBits > dstBits)
                    return castBuilder.CreateTrunc(val, target, "match.trunc");
                if(srcBits < dstBits)
                    return castBuilder.CreateSExt(val, target, "match.sext");
                return val;
            }
            if(src->isIntegerTy() && target->isFloatingPointTy())
                return castBuilder.CreateSIToFP(val, target, "match.sitofp");
            if(src->isFloatingPointTy() && target->isFloatingPointTy())
                return castBuilder.CreateFPCast(val, target, "match.fpcast");
            return val;
        };

        if(armValues.size() == 2 &&
           armValues[0].first->getType() != armValues[1].first->getType())
        {
            llvm::Type* noneValueType = armValues[1].first->getType();
            llvm::Type* commonType = nullptr;
            if(someValueType->isIntegerTy() && noneValueType->isIntegerTy())
            {
                unsigned someBits = someValueType->getIntegerBitWidth();
                unsigned noneBits = noneValueType->getIntegerBitWidth();
                commonType =
                    someBits >= noneBits ? someValueType : noneValueType;
            }
            else if(someValueType->isFloatingPointTy() &&
                    noneValueType->isFloatingPointTy())
            {
                commonType = someValueType->isDoubleTy() ||
                                     noneValueType->isDoubleTy()
                                 ? llvm::Type::getDoubleTy(context)
                                 : llvm::Type::getFloatTy(context);
            }
            else if(someValueType->isFloatingPointTy() &&
                    noneValueType->isIntegerTy())
            {
                commonType = someValueType;
            }
            else if(someValueType->isIntegerTy() &&
                    noneValueType->isFloatingPointTy())
            {
                commonType = noneValueType;
            }

            if(!commonType)
            {
                reportError(node->line, "match arm types do not match");
                return nullptr;
            }

            armValues[0].first = castInBlock(armValues[0].first, commonType,
                                             armValues[0].second);
            armValues[1].first = castInBlock(armValues[1].first, commonType,
                                             armValues[1].second);
            someValueType = commonType;
        }

        if(someValueType->isVoidTy())
        {
            reportError(node->line, "match arms must return a value");
            return nullptr;
        }

        llvm::PHINode* phi =
            builder.CreatePHI(someValueType, (unsigned)armValues.size(),
                              "match.result");
        for(const auto& pair : armValues)
            phi->addIncoming(pair.first, pair.second);
        std::set<std::string> mergedMoved;
        std::map<std::string, std::string> mergedPointerBorrowTarget;
        std::map<std::string, std::set<std::string>> mergedActiveBorrowers;
        for(const auto& st : armMovedStates)
            mergedMoved.insert(st.begin(), st.end());
        for(const auto& ptrState : armPointerBorrowStates)
        {
            for(const auto& kv : ptrState)
            {
                if(mergedPointerBorrowTarget.find(kv.first) ==
                   mergedPointerBorrowTarget.end())
                {
                    mergedPointerBorrowTarget[kv.first] = kv.second;
                }
            }
        }
        for(const auto& borrowersState : armActiveBorrowerStates)
        {
            for(const auto& kv : borrowersState)
            {
                auto& dst = mergedActiveBorrowers[kv.first];
                dst.insert(kv.second.begin(), kv.second.end());
            }
        }
        movedVariables = std::move(mergedMoved);
        pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
        activeBorrowers = std::move(mergedActiveBorrowers);
        return phi;
    }

    if(!literalArms.empty() && !wildcardArm)
    {
        reportError(node->line,
                    "literal match requires a wildcard arm");
        return nullptr;
    }

    if(literalArms.empty() && wildcardArm)
    {
        return generateExpression(wildcardArm->expression);
    }

    llvm::Type* matchType = matchVal->getType();
    if(matchType->isStructTy())
    {
        reportError(node->line, "literal match expects a scalar value");
        return nullptr;
    }

    auto buildLiteralCompare =
        [&](llvm::Value* litVal) -> llvm::Value*
    {
        if(!litVal)
            return nullptr;
        llvm::Type* targetType = matchType;
        llvm::Type* litType = litVal->getType();
        if(targetType->isIntegerTy())
        {
            if(!litType->isIntegerTy())
            {
                reportError(node->line,
                            "literal pattern type does not match target");
                return nullptr;
            }
            unsigned dstBits = targetType->getIntegerBitWidth();
            unsigned srcBits = litType->getIntegerBitWidth();
            if(srcBits > dstBits)
                litVal = builder.CreateTrunc(litVal, targetType, "match.lit");
            else if(srcBits < dstBits)
                litVal = builder.CreateSExt(litVal, targetType, "match.lit");
            return builder.CreateICmpEQ(matchVal, litVal, "match.cmp");
        }
        if(targetType->isFloatingPointTy())
        {
            if(litType->isIntegerTy())
                litVal = builder.CreateSIToFP(litVal, targetType, "match.lit");
            else if(litType->isFloatingPointTy() && litType != targetType)
                litVal = builder.CreateFPCast(litVal, targetType, "match.lit");
            else if(!litType->isFloatingPointTy())
            {
                reportError(node->line,
                            "literal pattern type does not match target");
                return nullptr;
            }
            return builder.CreateFCmpOEQ(matchVal, litVal, "match.fcmp");
        }
        if(targetType->isPointerTy())
        {
            if(!litType->isPointerTy())
            {
                reportError(node->line,
                            "literal pattern type does not match target");
                return nullptr;
            }
            return builder.CreateICmpEQ(matchVal, litVal, "match.pcmp");
        }
        reportError(node->line, "literal match expects numeric or pointer type");
        return nullptr;
    };

    llvm::Function* func = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* mergeBB =
        llvm::BasicBlock::Create(context, "match.merge", func);
    llvm::BasicBlock* wildcardBB = nullptr;
    if(wildcardArm)
        wildcardBB = llvm::BasicBlock::Create(context, "match.wildcard", func);

    std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> armValues;
    std::vector<std::set<std::string>> armMovedStates;
    std::vector<std::map<std::string, std::string>> armPointerBorrowStates;
    std::vector<std::map<std::string, std::set<std::string>>> armActiveBorrowerStates;
    std::vector<llvm::BasicBlock*> armBlocks;

    llvm::BasicBlock* nextBB = nullptr;
    for(size_t i = 0; i < literalArms.size(); ++i)
    {
        MatchArmNode* arm = literalArms[i];
        llvm::BasicBlock* armBB =
            llvm::BasicBlock::Create(context, "match.case", func);
        bool isLast = (i + 1 == literalArms.size());
        llvm::BasicBlock* fallBB =
            isLast ? (wildcardBB ? wildcardBB : mergeBB)
                   : llvm::BasicBlock::Create(context, "match.next", func);

        llvm::Value* litVal =
            generateExpression(arm->pattern->literal);
        llvm::Value* cmp = buildLiteralCompare(litVal);
        if(!cmp)
            return nullptr;
        builder.CreateCondBr(cmp, armBB, fallBB);

        builder.SetInsertPoint(armBB);
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        llvm::Value* armVal = generateExpression(arm->expression);
        if(!armVal)
            return nullptr;
        llvm::BasicBlock* armEnd = builder.GetInsertBlock();
        if(!armEnd->getTerminator())
        {
            armMovedStates.push_back(movedVariables);
            armPointerBorrowStates.push_back(pointerBorrowTarget);
            armActiveBorrowerStates.push_back(activeBorrowers);
            builder.CreateBr(mergeBB);
            armValues.push_back({armVal, builder.GetInsertBlock()});
        }
        armBlocks.push_back(armBB);
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;

        builder.SetInsertPoint(fallBB);
        nextBB = fallBB;
    }

    if(wildcardArm)
    {
        builder.SetInsertPoint(wildcardBB);
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        llvm::Value* armVal = generateExpression(wildcardArm->expression);
        if(!armVal)
            return nullptr;
        llvm::BasicBlock* wildcardEnd = builder.GetInsertBlock();
        if(!wildcardEnd->getTerminator())
        {
            armMovedStates.push_back(movedVariables);
            armPointerBorrowStates.push_back(pointerBorrowTarget);
            armActiveBorrowerStates.push_back(activeBorrowers);
            builder.CreateBr(mergeBB);
            armValues.push_back({armVal, builder.GetInsertBlock()});
        }
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
    }
    else
    {
        if(nextBB && nextBB != mergeBB)
            builder.SetInsertPoint(nextBB);
    }

    std::vector<llvm::Type*> armTypes;
    if(armValues.empty())
    {
        reportError(node->line, "match expression has no continuing arm");
        return nullptr;
    }
    armTypes.reserve(armValues.size());
    for(const auto& pair : armValues)
        armTypes.push_back(pair.first->getType());

    auto commonTypeFrom = [&](const std::vector<llvm::Type*>& types)
        -> llvm::Type*
    {
        if(types.empty())
            return nullptr;
        llvm::Type* common = types[0];
        bool anyFloat = common->isFloatingPointTy();
        bool anyInt = common->isIntegerTy();
        bool anyPtr = common->isPointerTy();
        bool anyStruct = common->isStructTy();

        unsigned maxIntBits = anyInt ? common->getIntegerBitWidth() : 0;
        bool anyDouble = common->isDoubleTy();

        for(size_t i = 1; i < types.size(); ++i)
        {
            llvm::Type* t = types[i];
            if(t == common)
                continue;
            if(t->isFloatingPointTy())
            {
                anyFloat = true;
                if(t->isDoubleTy())
                    anyDouble = true;
            }
            if(t->isIntegerTy())
            {
                anyInt = true;
                unsigned bits = t->getIntegerBitWidth();
                if(bits > maxIntBits)
                    maxIntBits = bits;
            }
            if(t->isPointerTy())
                anyPtr = true;
            if(t->isStructTy())
                anyStruct = true;
        }

        if(anyStruct)
        {
            for(auto* t : types)
            {
                if(t != types[0])
                    return nullptr;
            }
            return types[0];
        }

        if(anyPtr && !(anyFloat || anyInt))
        {
            for(auto* t : types)
            {
                if(!t->isPointerTy() || t != types[0])
                    return nullptr;
            }
            return types[0];
        }

        if(anyFloat)
            return anyDouble ? llvm::Type::getDoubleTy(context)
                             : llvm::Type::getFloatTy(context);
        if(anyInt)
            return llvm::Type::getIntNTy(context, maxIntBits);

        for(auto* t : types)
        {
            if(t != types[0])
                return nullptr;
        }
        return types[0];
    };

    llvm::Type* commonType = commonTypeFrom(armTypes);
    if(!commonType)
    {
        reportError(node->line, "match arm types do not match");
        return nullptr;
    }
    if(commonType->isVoidTy())
    {
        reportError(node->line, "match arms must return a value");
        return nullptr;
    }

    for(auto& pair : armValues)
    {
        llvm::Value* val = pair.first;
        llvm::BasicBlock* blk = pair.second;
        if(val->getType() == commonType)
            continue;
        llvm::IRBuilder<> castBuilder(blk->getTerminator());
        llvm::Value* casted = val;
        llvm::Type* src = val->getType();
        if(src->isIntegerTy() && commonType->isIntegerTy())
        {
            unsigned srcBits = src->getIntegerBitWidth();
            unsigned dstBits = commonType->getIntegerBitWidth();
            if(srcBits > dstBits)
                casted = castBuilder.CreateTrunc(val, commonType, "match.trunc");
            else if(srcBits < dstBits)
                casted = castBuilder.CreateSExt(val, commonType, "match.sext");
        }
        else if(src->isIntegerTy() && commonType->isFloatingPointTy())
        {
            casted = castBuilder.CreateSIToFP(val, commonType, "match.sitofp");
        }
        else if(src->isFloatingPointTy() && commonType->isFloatingPointTy())
        {
            casted = castBuilder.CreateFPCast(val, commonType, "match.fpcast");
        }
        pair.first = casted;
    }
    builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi =
        builder.CreatePHI(commonType, (unsigned)armValues.size(), "match.result");
    for(const auto& pair : armValues)
        phi->addIncoming(pair.first, pair.second);
    std::set<std::string> mergedMoved;
    std::map<std::string, std::string> mergedPointerBorrowTarget;
    std::map<std::string, std::set<std::string>> mergedActiveBorrowers;
    for(const auto& st : armMovedStates)
        mergedMoved.insert(st.begin(), st.end());
    for(const auto& ptrState : armPointerBorrowStates)
    {
        for(const auto& kv : ptrState)
        {
            if(mergedPointerBorrowTarget.find(kv.first) ==
               mergedPointerBorrowTarget.end())
            {
                mergedPointerBorrowTarget[kv.first] = kv.second;
            }
        }
    }
    for(const auto& borrowersState : armActiveBorrowerStates)
    {
        for(const auto& kv : borrowersState)
        {
            auto& dst = mergedActiveBorrowers[kv.first];
            dst.insert(kv.second.begin(), kv.second.end());
        }
    }
    movedVariables = std::move(mergedMoved);
    pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
    activeBorrowers = std::move(mergedActiveBorrowers);
    return phi;
}

// ============================================================================
// GENERICS MONOMORPHIZATION IMPLEMENTATION
// ============================================================================

// Substitute type parameters with concrete types
// e.g., if typeParams = ["T", "U"] and typeArgs = [i32, i64],
// then a StructTypeRefNode("T") becomes a TypeNode(TYPE_I32)
TypeNode*
CodeGenerator::substituteTypeParams(TypeNode* type,
                                    const std::vector<std::string>& typeParams,
                                    const std::vector<TypeNode*>& typeArgs)
{
    if(!type)
        return nullptr;

    // Check if this is a struct type reference that matches a type parameter
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
    {
        // Look for matching type parameter
        for(size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i)
        {
            if(structRef->structName == typeParams[i])
            {
                // Return a copy of the concrete type
                return typeArgs[i];
            }
        }
        // Not a type parameter - return as-is (it's a concrete struct type)
        return type;
    }

    // Check if this is a generic struct type reference
    if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(type))
    {
        // Recursively substitute type arguments
        auto* newRef = new GenericStructTypeRefNode(genRef->structName);
        for(auto* arg : genRef->typeArgs)
        {
            newRef->typeArgs.push_back(
                substituteTypeParams(arg, typeParams, typeArgs));
        }
        return newRef;
    }

    // Handle generic list type
    if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
    {
        TypeNode* newElemType =
            substituteTypeParams(listType->elementType, typeParams, typeArgs);
        return new GenericListTypeNode(newElemType);
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
    {
        TypeNode* newKeyType =
            substituteTypeParams(mapType->keyType, typeParams, typeArgs);
        TypeNode* newValType =
            substituteTypeParams(mapType->valueType, typeParams, typeArgs);
        return new MapTypeNode(newKeyType, newValType);
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
    {
        auto* newTypeList = new TypeListNode();
        for(auto* elemType : tupleType->elementTypes->types)
        {
            newTypeList->addType(
                substituteTypeParams(elemType, typeParams, typeArgs));
        }
        return new TupleTypeNode(newTypeList);
    }

    // Basic types don't need substitution
    return type;
}

// Generate a mangled name for a monomorphized struct
static std::string generateMangledName(const std::string& baseName,
                                       const std::vector<TypeNode*>& typeArgs)
{
    std::string mangled = baseName;
    for(auto* typeArg : typeArgs)
    {
        mangled += "_";
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(typeArg))
        {
            mangled += structRef->structName;
        }
        else if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(typeArg))
        {
            mangled += genRef->getMangledName();
        }
        else
        {
            switch(typeArg->kind)
            {
            case TypeNode::TYPE_BOOL:
                mangled += "bool";
                break;
            case TypeNode::TYPE_INT:
                mangled += "int";
                break;
            case TypeNode::TYPE_I8:
                mangled += "i8";
                break;
            case TypeNode::TYPE_I16:
                mangled += "i16";
                break;
            case TypeNode::TYPE_I32:
                mangled += "i32";
                break;
            case TypeNode::TYPE_I64:
                mangled += "i64";
                break;
            case TypeNode::TYPE_U8:
                mangled += "u8";
                break;
            case TypeNode::TYPE_U16:
                mangled += "u16";
                break;
            case TypeNode::TYPE_U32:
                mangled += "u32";
                break;
            case TypeNode::TYPE_U64:
                mangled += "u64";
                break;
            case TypeNode::TYPE_FLOAT:
                mangled += "float";
                break;
            case TypeNode::TYPE_DOUBLE:
                mangled += "double";
                break;
            case TypeNode::TYPE_STRING:
                mangled += "string";
                break;
            case TypeNode::TYPE_STR8:
                mangled += "str8";
                break;
            case TypeNode::TYPE_STR16:
                mangled += "str16";
                break;
            default:
                mangled += "unknown";
                break;
            }
        }
    }
    return mangled;
}

// Monomorphize a generic struct with concrete type arguments
void CodeGenerator::monomorphizeStruct(const std::string& genericName,
                                       const std::vector<TypeNode*>& typeArgs,
                                       const std::string& mangledName)
{
    // Find the generic template
    auto templateIt = genericStructTemplates.find(genericName);
    if(templateIt == genericStructTemplates.end())
    {
        std::cerr << "Error: Generic struct template '" << genericName
                  << "' not found" << std::endl;
        hasError = true;
        return;
    }

    StructDefNode* templateStruct = templateIt->second;
    const std::vector<std::string>& typeParams = templateStruct->typeParams;

    if(typeParams.size() != typeArgs.size())
    {
        std::cerr << "Error: Type argument count mismatch for '" << genericName
                  << "': expected " << typeParams.size() << ", got "
                  << typeArgs.size() << std::endl;
        hasError = true;
        return;
    }

    // Generate the monomorphized struct type
    std::vector<llvm::Type*> memberTypes;
    std::vector<std::pair<std::string, TypeNode*>> members;

    // Process each member, substituting type parameters
    if(templateStruct->members)
    {
        for(auto* member : templateStruct->members->members)
        {
            TypeNode* substitutedType =
                substituteTypeParams(member->type, typeParams, typeArgs);

            llvm::Type* llvmType = getLLVMTypeFromNode(substitutedType);
            if(!llvmType)
            {
                std::cerr << "Error: Failed to get LLVM type for member '"
                          << member->name << "' in " << mangledName
                          << std::endl;
                hasError = true;
                return;
            }

            memberTypes.push_back(llvmType);
            members.push_back({member->name, substitutedType});
        }
    }

    // Create the LLVM struct type
    llvm::StructType* structType =
        llvm::StructType::create(context, memberTypes, mangledName);

    // Register the monomorphized type
    structTypes[mangledName] = structType;
    structMembers[mangledName] = members;
    monomorphizedTypes.insert(mangledName);
    mangledToGenericName[mangledName] = genericName;
    if(templateStruct->deriveDebug)
        debugStructs.insert(mangledName);

    // Copy visibility from template
    structVisibility[mangledName] =
        std::make_pair(templateStruct->isPublic, templateStruct->sourceModule);

    // Store the type params and args for later method generation
    // We'll generate methods lazily when they're called

    // Process impl blocks - just register methods, don't generate bodies yet
    auto implIt = genericImplBlocks.find(genericName);
    if(implIt != genericImplBlocks.end())
    {
        for(auto* impl : implIt->second)
        {
            for(auto* method : impl->methods)
            {
                // Substitute types in return type
                TypeNode* newReturnType = substituteTypeParams(
                    method->returnType, typeParams, typeArgs);

                // Substitute types in parameters
                auto* newParams = new ParameterListNode();
                for(auto* param : method->parameters->parameters)
                {
                    TypeNode* newParamType =
                        substituteTypeParams(param->type, typeParams, typeArgs);
                    newParams->parameters.push_back(
                        new ParameterNode(newParamType, param->name));
                }

                // Create monomorphized method node
                auto* newMethod = new StructMethodNode(
                    newReturnType, method->name, newParams, method->body,
                    method->isPublic, method->isStatic);

                // Register the method (but don't generate code yet)
                structMethods[mangledName][method->name] =
                    std::make_pair(method->isPublic, newMethod);

                // Generate only the declaration (forward declaration)
                generateMethodDeclaration(mangledName, newMethod);
            }
        }
    }

    // Process methods defined inside the struct template
    if(templateStruct->members)
    {
        for(auto* method : templateStruct->members->methods)
        {
            // Substitute types in return type and parameters
            TypeNode* newReturnType =
                substituteTypeParams(method->returnType, typeParams, typeArgs);

            auto* newParams = new ParameterListNode();
            for(auto* param : method->parameters->parameters)
            {
                TypeNode* newParamType =
                    substituteTypeParams(param->type, typeParams, typeArgs);
                newParams->parameters.push_back(
                    new ParameterNode(newParamType, param->name));
            }

            // Create a new method node with substituted types
            auto* newMethod = new StructMethodNode(
                newReturnType, method->name, newParams, method->body,
                method->isPublic, method->isStatic);

            // Register the method
            structMethods[mangledName][method->name] =
                std::make_pair(method->isPublic, newMethod);

            // Generate only the declaration
            generateMethodDeclaration(mangledName, newMethod);
        }
    }
}

// Monomorphize an impl block (Note: this is no longer called from
// monomorphizeStruct but kept for potential future use)
void CodeGenerator::monomorphizeImplBlock(
    ImplBlockNode* impl, const std::vector<std::string>& typeParams,
    const std::vector<TypeNode*>& typeArgs,
    const std::string& mangledStructName)
{
    for(auto* method : impl->methods)
    {
        // Substitute types in return type
        TypeNode* newReturnType =
            substituteTypeParams(method->returnType, typeParams, typeArgs);

        // Substitute types in parameters
        auto* newParams = new ParameterListNode();
        for(auto* param : method->parameters->parameters)
        {
            TypeNode* newParamType =
                substituteTypeParams(param->type, typeParams, typeArgs);
            newParams->parameters.push_back(
                new ParameterNode(newParamType, param->name));
        }

        // Create monomorphized method
        auto* newMethod = new StructMethodNode(
            newReturnType, method->name, newParams, method->body,
            method->isPublic, method->isStatic);

        // Register the method
        structMethods[mangledStructName][method->name] =
            std::make_pair(method->isPublic, newMethod);

        // Generate only declaration (body is generated lazily on first call)
        generateMethodDeclaration(mangledStructName, newMethod);
    }
}

// Get or create a monomorphized struct type
std::string CodeGenerator::getOrCreateMonomorphizedStruct(
    const std::string& genericName, const std::vector<TypeNode*>& typeArgs)
{
    // Generate the mangled name
    std::string mangledName = generateMangledName(genericName, typeArgs);

    // Check if already monomorphized
    if(monomorphizedTypes.count(mangledName))
    {
        return mangledName;
    }

    // Check if this is actually a generic struct
    if(genericStructTemplates.find(genericName) == genericStructTemplates.end())
    {
        // Not a generic struct - might be a non-generic struct being used
        // Just return the original name
        return genericName;
    }

    // Monomorphize the struct
    monomorphizeStruct(genericName, typeArgs, mangledName);

    return mangledName;
}

// Backend implementation
Backend::Backend(std::unique_ptr<llvm::Module>& m)
    : module(m), targetMachine(nullptr)
{
    initializeTarget();
}

bool Backend::initializeTarget()
{
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    targetTriple = llvm::sys::getDefaultTargetTriple();

    // Normalize macOS version in triple to avoid linker warnings
    // The default triple may contain a newer macOS version than the linker
    // expects
#if defined(__APPLE__)
    {
        llvm::Triple triple(targetTriple);
        if(triple.isMacOSX())
        {
            // Reset to a base macOS version to avoid version mismatch warnings
            triple.setOSName("macosx10.15.0");
            targetTriple = triple.str();
        }
    }
#endif

#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(llvm::Triple(targetTriple));
#else
    module->setTargetTriple(targetTriple);
#endif

    std::string error;
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if(!target)
    {
        std::cerr << "Error looking up target: " << error << std::endl;
        return false;
    }

    std::string cpu = "generic";
    std::string features = "";

    llvm::TargetOptions opt;
    auto relocModel = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);

#if LLVM_VERSION_MAJOR >= 21
    llvm::Triple tripleObj(targetTriple);
    targetMachine =
        target->createTargetMachine(tripleObj, cpu, features, opt, relocModel);
#else
    targetMachine = target->createTargetMachine(targetTriple, cpu, features,
                                                opt, relocModel);
#endif

    if(!targetMachine)
    {
        std::cerr << "Error creating target machine" << std::endl;
        return false;
    }

    module->setDataLayout(targetMachine->createDataLayout());
    return true;
}

bool Backend::emitObjectFile(const std::string& filename)
{
    if(!targetMachine)
    {
        std::cerr << "Target machine not initialized" << std::endl;
        return false;
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pass;
#if LLVM_VERSION_MAJOR >= 18
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CodeGenFileType::ObjectFile))
#else
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CGFT_ObjectFile))
#endif
    {
        std::cerr << "Target machine can't emit object file" << std::endl;
        return false;
    }

    pass.run(*module);
    dest.flush();

    std::cout << "Object file written to: " << filename << std::endl;
    return true;
}

bool Backend::emitAssemblyFile(const std::string& filename)
{
    if(!targetMachine)
    {
        std::cerr << "Target machine not initialized" << std::endl;
        return false;
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pass;
#if LLVM_VERSION_MAJOR >= 18
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CodeGenFileType::AssemblyFile))
#else
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CGFT_AssemblyFile))
#endif
    {
        std::cerr << "Target machine can't emit assembly file" << std::endl;
        return false;
    }

    pass.run(*module);
    dest.flush();

    std::cout << "Assembly file written to: " << filename << std::endl;
    return true;
}

bool Backend::emitLLVMIR(const std::string& filename)
{
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    module->print(dest, nullptr);
    dest.flush();

    std::cout << "LLVM IR written to: " << filename << std::endl;
    return true;
}

bool Backend::emitBitcode(const std::string& filename)
{
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::WriteBitcodeToFile(*module, dest);
    dest.flush();

    std::cout << "Bitcode written to: " << filename << std::endl;
    return true;
}

bool Backend::linkExecutable(const std::string& objectFile,
                             const std::string& outputFile,
                             const std::vector<std::string>& linkArgs)
{
    // Use C++ driver so C++ stdlib symbols from native stdlib objects resolve.
    std::string command = "c++ -o " + outputFile + " " + objectFile;
    for(const auto& arg : linkArgs)
    {
        command += " " + arg;
    }
    command += " 2>&1";
    std::cout << "Linking: " << command << std::endl;

    int result = system(command.c_str());
    if(result != 0)
    {
        std::cerr << "Linking failed with error code: " << result << std::endl;
        return false;
    }

    std::cout << "Executable created: " << outputFile << std::endl;
    return true;
}

bool Backend::compileToExecutable(const std::string& outputFile,
                                  const std::vector<std::string>& linkArgs)
{
    std::string objectFile = outputFile + ".o";

    if(!emitObjectFile(objectFile))
    {
        return false;
    }

    return linkExecutable(objectFile, outputFile, linkArgs);
}

void Backend::optimize(int level)
{
    if(level < 0 || level > 3)
        level = 2;

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::OptimizationLevel optLevel;
    switch(level)
    {
    case 0:
        optLevel = llvm::OptimizationLevel::O0;
        break;
    case 1:
        optLevel = llvm::OptimizationLevel::O1;
        break;
    case 2:
        optLevel = llvm::OptimizationLevel::O2;
        break;
    case 3:
        optLevel = llvm::OptimizationLevel::O3;
        break;
    default:
        optLevel = llvm::OptimizationLevel::O2;
    }

    llvm::ModulePassManager MPM;
    if(level > 0)
    {
        MPM = PB.buildPerModuleDefaultPipeline(optLevel);
    }

    MPM.run(*module, MAM);
    std::cout << "Optimization level O" << level << " applied" << std::endl;
}
