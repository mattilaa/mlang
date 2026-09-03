#include "ir.h"
#include "ir/common.h"
#include "module.h"

#include <llvm/Config/llvm-config.h>
#include <algorithm>
#include <functional>
#include <limits>

using mlang::ir_detail::common::Helpers;

llvm::Value* CodeGenerator::generateFunctionCall(FunctionCallNode* node)
{
    auto validateTemporaryBorrowArguments =
        [&](const std::vector<ExpressionNode*>& args,
            const std::string& calleeName,
            const std::string& implicitWholeOwner) -> bool
    {
        std::set<std::string> wholeOwnersInCall;
        std::map<std::string, std::set<std::string>> subpathsInCall;
        std::set<std::string> movedOwnersInCall;
        if(!implicitWholeOwner.empty())
            wholeOwnersInCall.insert(implicitWholeOwner);
        auto describeBorrowPath =
            [&](ExpressionNode* expr, std::string& ownerOut,
                std::string& pathOut, bool& isWholeOwnerOut) -> bool
        {
            ownerOut = resolveBorrowOwnerFromLValue(expr);
            if(ownerOut.empty())
                return false;

            isWholeOwnerOut = true;
            pathOut.clear();

            std::vector<std::string> fields;
            ExpressionNode* cur = expr;
            while(auto* field = dynamic_cast<FieldAccessNode*>(cur))
            {
                fields.push_back(field->fieldName);
                if(field->object)
                    cur = field->object;
                else
                    break;
            }

            if(!fields.empty())
            {
                isWholeOwnerOut = false;
                std::reverse(fields.begin(), fields.end());
                for(size_t i = 0; i < fields.size(); ++i)
                {
                    if(i)
                        pathOut += ".";
                    pathOut += fields[i];
                }
            }
            return true;
        };
        std::function<void(ExpressionNode*, bool)> collectMovedOwners =
            [&](ExpressionNode* expr, bool borrowed) -> void
        {
            if(!expr)
                return;

            if(auto* id = dynamic_cast<IdentifierNode*>(expr))
            {
                if(globalNamedValues.find(id->name) != globalNamedValues.end())
                    return;
                if(!borrowed && isMoveOnlyVariable(id->name))
                    movedOwnersInCall.insert(id->name);
                return;
            }

            if(auto* unary = dynamic_cast<UnaryOpNode*>(expr))
            {
                if(unary->op == UnaryOpNode::OP_ADDR ||
                   unary->op == UnaryOpNode::OP_ADDR_MUT)
                    collectMovedOwners(unary->operand, true);
                else
                    collectMovedOwners(unary->operand, borrowed);
                return;
            }

            if(auto* binary = dynamic_cast<BinaryOpNode*>(expr))
            {
                collectMovedOwners(binary->left, false);
                collectMovedOwners(binary->right, false);
                return;
            }

            if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
            {
                if(field->object)
                    collectMovedOwners(field->object, true);
                return;
            }

            if(auto* index = dynamic_cast<IndexExpressionNode*>(expr))
            {
                collectMovedOwners(index->base, true);
                collectMovedOwners(index->index, false);
                return;
            }

            if(auto* castExpr = dynamic_cast<CastExpressionNode*>(expr))
            {
                collectMovedOwners(castExpr->expression, false);
                return;
            }

            if(auto* tryExpr = dynamic_cast<TryExpressionNode*>(expr))
            {
                collectMovedOwners(tryExpr->expression, false);
                return;
            }

            if(auto* ternary = dynamic_cast<TernaryNode*>(expr))
            {
                collectMovedOwners(ternary->condition, false);
                collectMovedOwners(ternary->trueExpr, false);
                collectMovedOwners(ternary->falseExpr, false);
                return;
            }

            if(auto* listLit = dynamic_cast<ListLiteralNode*>(expr))
            {
                if(listLit->elements)
                {
                    for(auto* elem : listLit->elements->elements)
                        collectMovedOwners(elem, false);
                }
                return;
            }

            if(auto* mapLit = dynamic_cast<MapLiteralNode*>(expr))
            {
                if(mapLit->entries)
                {
                    for(auto* entry : mapLit->entries->entries)
                    {
                        collectMovedOwners(entry->key, false);
                        collectMovedOwners(entry->value, false);
                    }
                }
                return;
            }

            if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(expr))
            {
                if(tupleLit->elements)
                {
                    for(auto* elem : tupleLit->elements->elements)
                        collectMovedOwners(elem, false);
                }
                return;
            }

            if(auto* structLit = dynamic_cast<StructLiteralNode*>(expr))
            {
                for(const auto& init : structLit->fields)
                    collectMovedOwners(init.second, false);
                return;
            }
        };

        for(auto* arg : args)
        {
            collectMovedOwners(arg, false);

            auto registerBorrowInCall = [&](const std::string& owner,
                                            const std::string& path,
                                            bool isWholeOwner) -> bool
            {
                if(isWholeOwner)
                {
                    if(wholeOwnersInCall.count(owner) ||
                       (subpathsInCall.count(owner) &&
                        !subpathsInCall[owner].empty()))
                    {
                        reportError(node->line,
                                    "cannot borrow overlapping parts of '" +
                                        owner + "' in call to '" + calleeName +
                                        "'");
                        return false;
                    }
                    wholeOwnersInCall.insert(owner);
                    return true;
                }

                if(wholeOwnersInCall.count(owner))
                {
                    reportError(node->line,
                                "cannot borrow overlapping parts of '" + owner +
                                    "' in call to '" + calleeName + "'");
                    return false;
                }
                auto& paths = subpathsInCall[owner];
                bool overlaps = false;
                for(const auto& existing : paths)
                {
                    if(existing == path)
                    {
                        overlaps = true;
                        break;
                    }
                    if(path.size() > existing.size() &&
                       path.compare(0, existing.size(), existing) == 0 &&
                       path[existing.size()] == '.')
                    {
                        overlaps = true;
                        break;
                    }
                    if(existing.size() > path.size() &&
                       existing.compare(0, path.size(), path) == 0 &&
                       existing[path.size()] == '.')
                    {
                        overlaps = true;
                        break;
                    }
                }
                if(overlaps)
                {
                    reportError(node->line,
                                "cannot borrow '" + owner + "." + path +
                                    "' multiple times in call to '" +
                                    calleeName + "'");
                    return false;
                }
                paths.insert(path);
                return true;
            };

            if(auto* idArg = dynamic_cast<IdentifierNode*>(arg))
            {
                auto pit = pointerBorrowTarget.find(idArg->name);
                if(pit != pointerBorrowTarget.end())
                {
                    auto vit = variableTypes.find(idArg->name);
                    if(vit == variableTypes.end() ||
                       vit->second != TypeNode::TYPE_PTR)
                    {
                        continue;
                    }
                    if(!isMoveOnlyVariable(pit->second))
                    {
                        continue;
                    }
                    if(!registerBorrowInCall(pit->second, "", true))
                        return false;
                    continue;
                }
            }

            auto* unary = dynamic_cast<UnaryOpNode*>(arg);
            if(!unary || (unary->op != UnaryOpNode::OP_ADDR &&
                          unary->op != UnaryOpNode::OP_ADDR_MUT))
                continue;

            std::string owner;
            std::string path;
            bool isWholeOwner = true;
            if(!describeBorrowPath(unary->operand, owner, path, isWholeOwner))
                continue;

            if(globalNamedValues.find(owner) == globalNamedValues.end() &&
               isVariableMoved(owner))
            {
                reportError(node->line,
                            "cannot borrow moved value: '" + owner + "'");
                return false;
            }

            const bool wantsMutable = (unary->op == UnaryOpNode::OP_ADDR_MUT);
            auto activeIt = activeBorrowers.find(owner);
            auto mutIt = activeMutBorrower.find(owner);
            if(wantsMutable)
            {
                if(activeIt != activeBorrowers.end() &&
                   !activeIt->second.empty())
                {
                    reportError(
                        node->line,
                        "cannot borrow '" + owner +
                            "' as mutable because it is already borrowed");
                    return false;
                }
                if(mutIt != activeMutBorrower.end())
                {
                    reportError(node->line,
                                "cannot borrow '" + owner +
                                    "' as mutable more than once at a time");
                    return false;
                }
            }
            else
            {
                if(mutIt != activeMutBorrower.end())
                {
                    reportError(
                        node->line,
                        "cannot borrow '" + owner +
                            "' as immutable because it is also borrowed "
                            "as mutable by '" +
                            mutIt->second + "'");
                    return false;
                }
                if(activeIt != activeBorrowers.end() &&
                   !activeIt->second.empty())
                {
                    std::string by = *activeIt->second.begin();
                    reportError(node->line,
                                "cannot borrow '" + owner +
                                    "' while already borrowed by '" + by + "'");
                    return false;
                }
            }

            if(!registerBorrowInCall(owner, path, isWholeOwner))
                return false;
        }

        for(const auto& movedOwner : movedOwnersInCall)
        {
            if(wholeOwnersInCall.count(movedOwner) ||
               subpathsInCall.find(movedOwner) != subpathsInCall.end())
            {
                reportError(node->line, "cannot move '" + movedOwner +
                                            "' while borrowed in call to '" +
                                            calleeName + "'");
                return false;
            }
        }

        return true;
    };
    auto isFreeLikeFunctionName = [&](const std::string& fnName) -> bool
    {
        if(fnName == "String::free" || fnName == "free")
            return true;
        return fnName.size() > 5 &&
               fnName.compare(fnName.size() - 5, 5, "_free") == 0;
    };
    auto precheckFreeLikeArgument = [&](ExpressionNode* argExpr,
                                        const std::string& fnName) -> bool
    {
        if(!argExpr)
            return true;
        if((fnName == "String::free" || fnName == "free") &&
           dynamic_cast<StringLiteralNode*>(argExpr))
        {
            reportError(
                node->line,
                "cannot free string literal; free only heap-allocated strings");
            return false;
        }
        if(auto* idArg = dynamic_cast<IdentifierNode*>(argExpr))
        {
            if(globalNamedValues.find(idArg->name) == globalNamedValues.end() &&
               isVariableMoved(idArg->name))
            {
                reportError(node->line, idArg->col,
                            "double free or use-after-free of value: '" +
                                idArg->name + "'");
                return false;
            }
        }
        return true;
    };
    auto markFreeLikeConsumed = [&](ExpressionNode* argExpr) -> void
    {
        if(auto* idArg = dynamic_cast<IdentifierNode*>(argExpr))
        {
            if(globalNamedValues.find(idArg->name) == globalNamedValues.end())
                movedVariables.insert(idArg->name);
        }
    };

    // Vec::new() — returns an empty list struct {0, null}
    if(node->name == "Vec::new")
    {
        if(!node->arguments.empty())
        {
            reportError(node->line, "Vec::new expects no arguments");
            return nullptr;
        }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);
        return llvm::ConstantAggregateZero::get(listStructType);
    }
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
            reportError(node->line,
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
        ExpressionNode* argExpr = node->arguments[0];
        if(!precheckFreeLikeArgument(argExpr, node->name))
            return nullptr;
        initializeStdlibFunctions();
        llvm::Value* ptr = generateExpression(argExpr);
        if(!ptr)
            return nullptr;
        consumeMoveFromExpression(argExpr, node->line,
                                  "passing argument to String::free");
        markFreeLikeConsumed(argExpr);
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
    if(node->name == "String::from" || node->name == "String::to_utf8")
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line, node->name + " expects one argument");
            return nullptr;
        }
        llvm::Value* arg = generateExpression(node->arguments[0]);
        if(!arg)
            return nullptr;
        initializeStdlibFunctions();
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::FunctionType* cloneFnType =
            llvm::FunctionType::get(ptrType, {ptrType}, false);
        llvm::FunctionCallee cloneFn = module->getOrInsertFunction(
            "__mlang_std_strbuf_clone", cloneFnType);
        return builder.CreateCall(cloneFn, {arg}, "string_from");
    }

    if(node->name == "thread_spawn" || node->name == "thread::spawn")
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

    // Inline closure/lambda call: inc(...)
    {
        auto closIt = closureVariables.find(node->name);
        if(closIt != closureVariables.end())
        {
            // Re-entrancy guard — recursive inline closures are unsupported.
            if(!activeInlineClosures.insert(node->name).second)
            {
                reportError(node->line, "recursive call to inline closure '" +
                                            node->name + "' is not supported");
                return nullptr;
            }
            ClosureNode* closure = closIt->second;
            size_t expectedArgs =
                (closure->parameters ? closure->parameters->parameters.size()
                                     : 0);
            if(node->arguments.size() != expectedArgs)
            {
                reportError(node->line,
                            "inline lambda '" + node->name + "' expects " +
                                std::to_string(expectedArgs) +
                                " argument(s), got " +
                                std::to_string(node->arguments.size()));
                activeInlineClosures.erase(node->name);
                return nullptr;
            }

            auto savedNamedValues = namedValues;
            auto savedConstantVariables = constantVariables;
            auto savedVariableTypes = variableTypes;
            auto savedStructVariableTypes = structVariableTypes;
            auto savedTraitObjectVariableTypes = traitObjectVariableTypes;
            auto savedEnumVariableTypes = enumVariableTypes;
            auto savedListElementTypes = listElementTypes;
            auto savedMapKeyValueTypes = mapKeyValueTypes;
            auto savedPointerElementTypes = pointerElementTypes;

            auto restoreInlineState = [&]()
            {
                namedValues = savedNamedValues;
                constantVariables = savedConstantVariables;
                variableTypes = savedVariableTypes;
                structVariableTypes = savedStructVariableTypes;
                traitObjectVariableTypes = savedTraitObjectVariableTypes;
                enumVariableTypes = savedEnumVariableTypes;
                listElementTypes = savedListElementTypes;
                mapKeyValueTypes = savedMapKeyValueTypes;
                pointerElementTypes = savedPointerElementTypes;
            };

            // Bind lambda arguments to local parameter variables.
            for(size_t i = 0; i < expectedArgs; ++i)
            {
                auto* param = closure->parameters->parameters[i];
                llvm::Value* argVal = generateExpression(node->arguments[i]);
                if(!argVal)
                {
                    restoreInlineState();
                    activeInlineClosures.erase(node->name);
                    return nullptr;
                }
                consumeMoveFromExpression(
                    node->arguments[i], node->line,
                    "passing argument to inline lambda '" + node->name + "'");
                llvm::Type* expectedType = getLLVMTypeFromNode(param->type);
                if(!expectedType)
                {
                    restoreInlineState();
                    activeInlineClosures.erase(node->name);
                    return nullptr;
                }
                if(argVal->getType() != expectedType)
                {
                    int cost = 0;
                    if(!canConvertType(argVal->getType(), expectedType, cost))
                    {
                        reportError(node->line,
                                    "argument " + std::to_string(i + 1) +
                                        " has wrong type for inline lambda '" +
                                        node->name + "'");
                        restoreInlineState();
                        activeInlineClosures.erase(node->name);
                        return nullptr;
                    }
                    if(argVal->getType()->isIntegerTy() &&
                       expectedType->isIntegerTy())
                    {
                        unsigned srcBits =
                            argVal->getType()->getIntegerBitWidth();
                        unsigned dstBits = expectedType->getIntegerBitWidth();
                        if(srcBits > dstBits)
                            argVal = builder.CreateTrunc(argVal, expectedType,
                                                         "lam.arg.trunc");
                        else if(srcBits < dstBits)
                            argVal = builder.CreateSExt(argVal, expectedType,
                                                        "lam.arg.sext");
                    }
                    else if(argVal->getType()->isIntegerTy() &&
                            expectedType->isFloatingPointTy())
                    {
                        argVal = builder.CreateSIToFP(argVal, expectedType,
                                                      "lam.arg.sitofp");
                    }
                    else if(argVal->getType()->isFloatingPointTy() &&
                            expectedType->isIntegerTy())
                    {
                        argVal = builder.CreateFPToSI(argVal, expectedType,
                                                      "lam.arg.fptosi");
                    }
                    else if(argVal->getType()->isFloatingPointTy() &&
                            expectedType->isFloatingPointTy())
                    {
                        argVal = builder.CreateFPCast(argVal, expectedType,
                                                      "lam.arg.fpcast");
                    }
                    else if(argVal->getType()->isPointerTy() &&
                            expectedType->isPointerTy())
                    {
                        argVal = builder.CreateBitCast(argVal, expectedType,
                                                       "lam.arg.ptrcast");
                    }
                }

                llvm::AllocaInst* alloca = builder.CreateAlloca(
                    expectedType, nullptr, param->name + ".lam.addr");
                argVal = applyStructCopySemantics(argVal, param->type);
                builder.CreateStore(argVal, alloca);
                namedValues[param->name] = alloca;
                recordVariableScopeDepth(param->name);
                variableTypes[param->name] = param->type->kind;

                if(auto* structType =
                       dynamic_cast<StructTypeRefNode*>(param->type))
                {
                    std::string resolvedEnumName =
                        resolveVisibleEnumName(structType->structName);
                    if(!resolvedEnumName.empty())
                    {
                        variableTypes[param->name] = TypeNode::TYPE_INT;
                        enumVariableTypes[param->name] = resolvedEnumName;
                    }
                    else
                        structVariableTypes[param->name] =
                            structType->structName;
                }
                if(auto* genListType =
                       dynamic_cast<GenericListTypeNode*>(param->type))
                {
                    listElementTypes[param->name] = genListType->elementType;
                }
                if(auto* mapType = dynamic_cast<MapTypeNode*>(param->type))
                {
                    mapKeyValueTypes[param->name] =
                        std::make_pair(mapType->keyType, mapType->valueType);
                }
                if(auto* ptrType = dynamic_cast<PointerTypeNode*>(param->type))
                {
                    pointerElementTypes[param->name] = ptrType->elementType;
                }
            }

            // Clear loop context so break/continue don't escape the closure.
            auto savedBreak = loopBreakBlocks;
            auto savedContinue = loopContinueBlocks;
            loopBreakBlocks.clear();
            loopContinueBlocks.clear();

            enterCleanupScope();
            if(closure->body)
            {
                for(auto* stmt : closure->body->statements)
                {
                    generateStatement(stmt);
                    if(builder.GetInsertBlock() &&
                       builder.GetInsertBlock()->getTerminator())
                        break;
                }
            }
            exitCleanupScope();

            loopBreakBlocks = std::move(savedBreak);
            loopContinueBlocks = std::move(savedContinue);
            restoreInlineState();
            activeInlineClosures.erase(node->name);
            return nullptr; // inline closures return void
        }
    }

    auto hasRegisteredOverload = [&](FunctionDefNode* fn) -> bool
    {
        if(!fn)
            return false;
        auto it = functionOverloads.find(fn->name);
        if(it == functionOverloads.end())
            return false;
        std::string sig = functionSignatureKey(fn);
        for(const auto& info : it->second)
        {
            if(info.signatureKey == sig &&
               info.sourceModule == fn->sourceModule)
                return true;
        }
        return false;
    };

    auto isCompositeSemanticType = [&](TypeNode* type) -> bool
    {
        while(auto* refType = dynamic_cast<ReferenceTypeNode*>(type))
            type = refType->elementType;
        if(!type)
            return false;
        return dynamic_cast<GenericListTypeNode*>(type) != nullptr ||
               dynamic_cast<MapTypeNode*>(type) != nullptr ||
               dynamic_cast<TupleTypeNode*>(type) != nullptr;
    };

    auto concreteTypeImplementsTrait = [&](TypeNode* type,
                                           const std::string& traitName) -> bool
    {
        if(!type)
            return false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
        {
            std::string resolved =
                resolveVisibleStructName(structRef->structName);
            if(resolved.empty())
                resolved = structRef->structName;
            auto it = structImplementedTraits.find(resolved);
            return it != structImplementedTraits.end() &&
                   it->second.find(traitName) != it->second.end();
        }
        if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(type))
        {
            std::string resolved = getOrCreateMonomorphizedStruct(
                genRef->structName, genRef->typeArgs);
            auto it = structImplementedTraits.find(resolved);
            return it != structImplementedTraits.end() &&
                   it->second.find(traitName) != it->second.end();
        }
        return false;
    };

    auto semanticArgumentType = [&](ExpressionNode* expr) -> TypeNode*
    {
        if(!expr)
            return nullptr;
        if(TypeNode* ty = getLValueType(expr, node->line))
            return ty;

        if(dynamic_cast<StringLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_STR8);
        if(dynamic_cast<BoolLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_BOOL);
        if(dynamic_cast<IntLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_I64);
        if(dynamic_cast<FloatLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_FLOAT);
        if(dynamic_cast<DoubleLiteralNode*>(expr))
            return new TypeNode(TypeNode::TYPE_DOUBLE);
        return nullptr;
    };

    auto registerModuleFunctionsOnDemand = [&](const std::string& moduleName)
    {
        if(!moduleLoader || moduleName.empty())
            return;
        auto moduleFns = moduleLoader->getModuleFunctions(moduleName);
        for(auto* fn : moduleFns)
        {
            if(!fn || fn->name.empty() || hasRegisteredOverload(fn))
                continue;
            llvm::Function* decl = generateFunctionDeclaration(fn);
            registerFunctionOverload(fn, decl);
            bool alreadyQueued = false;
            for(auto* queued : deferredModuleFunctionDefs)
            {
                if(queued == fn)
                {
                    alreadyQueued = true;
                    break;
                }
            }
            if(!alreadyQueued)
                deferredModuleFunctionDefs.push_back(fn);
        }
    };

    auto overloadIt = functionOverloads.find(node->name);
    std::string qualifiedModuleFilter;
    bool usingTailQualifiedLookup = false;
    size_t qualifiedPos = node->name.rfind("::");
    if(qualifiedPos != std::string::npos)
    {
        qualifiedModuleFilter = node->name.substr(0, qualifiedPos);
        bool treatAsModulePath = false;
        if(moduleLoader && !qualifiedModuleFilter.empty())
        {
            if(moduleLoader->getModule(qualifiedModuleFilter) != nullptr)
                treatAsModulePath = true;
        }
        if(treatAsModulePath)
        {
            registerModuleFunctionsOnDemand(qualifiedModuleFilter);
            if(overloadIt == functionOverloads.end())
            {
                std::string tailName = node->name.substr(qualifiedPos + 2);
                auto tailIt = functionOverloads.find(tailName);
                if(tailIt != functionOverloads.end())
                {
                    overloadIt = tailIt;
                    usingTailQualifiedLookup = true;
                }
            }
        }
    }
    if(overloadIt == functionOverloads.end())
    {
        // Static struct method call syntax: Type::method(...)
        size_t scopePos = node->name.rfind("::");
        if(scopePos != std::string::npos)
        {
            std::string structName = node->name.substr(0, scopePos);
            std::string methodName = node->name.substr(scopePos + 2);
            std::string displayStructName = structName;

            if(TypeNode* parsedType = Helpers::type_from_text(structName))
            {
                std::set<std::string> emptyTypeParams;
                std::vector<std::string> aliasStack;
                parsedType = resolveTypeAliasNode(parsedType, emptyTypeParams,
                                                  aliasStack);
                if(auto* genericStructType =
                       dynamic_cast<GenericStructTypeRefNode*>(parsedType))
                {
                    std::string visibleStructName =
                        Helpers::resolve_visible_struct_base_name(
                            genericStructType->structName,
                            genericStructTemplates, structMethods,
                            structVisibility);
                    structName = getOrCreateMonomorphizedStruct(
                        visibleStructName, genericStructType->typeArgs);
                }
                else if(auto* structType =
                            dynamic_cast<StructTypeRefNode*>(parsedType))
                {
                    structName = Helpers::resolve_visible_struct_base_name(
                        structType->structName, genericStructTemplates,
                        structMethods, structVisibility);
                }
            }

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
                if(mit != sit->second.end() && mit->second.second)
                {
                    if(!mit->second.second->isStatic)
                    {
                        reportError(node->line,
                                    "instance method '" + displayStructName +
                                        "::" + methodName +
                                        "' must be called on a value");
                        return nullptr;
                    }

                    std::string mangledName =
                        resolvedStructName + "_" + methodName;
                    llvm::Function* callee = module->getFunction(mangledName);
                    if(!callee)
                    {
                        callee = generateMethodDeclaration(resolvedStructName,
                                                           mit->second.second);
                    }
                    if(callee && callee->empty())
                    {
                        llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
                        auto savedNamedValues = namedValues;
                        auto savedConstantVariables = constantVariables;
                        auto savedVariableTypes = variableTypes;
                        auto savedStructVariableTypes = structVariableTypes;
                        auto savedTraitObjectVariableTypes =
                            traitObjectVariableTypes;
                        auto savedEnumVariableTypes = enumVariableTypes;
                        auto savedListElementTypes = listElementTypes;
                        auto savedMapKeyValueTypes = mapKeyValueTypes;
                        auto savedTupleElementTypes = tupleElementTypes;
                        auto savedPointerElementTypes = pointerElementTypes;
                        auto savedMovedVariables = movedVariables;
                        auto savedPointerBorrowTarget = pointerBorrowTarget;
                        auto savedActiveBorrowers = activeBorrowers;
                        auto savedActiveMutBorrower = activeMutBorrower;
                        auto savedVariableScopeDepth = variableScopeDepth;
                        auto savedCleanupScopes = cleanupScopes;
                        auto savedPointerBorrowScopes = pointerBorrowScopes;
                        auto savedVariableScopeDepthScopes =
                            variableScopeDepthScopes;

                        callee = generateMethodDefinition(resolvedStructName,
                                                          mit->second.second);

                        namedValues = savedNamedValues;
                        constantVariables = savedConstantVariables;
                        variableTypes = savedVariableTypes;
                        structVariableTypes = savedStructVariableTypes;
                        traitObjectVariableTypes =
                            savedTraitObjectVariableTypes;
                        enumVariableTypes = savedEnumVariableTypes;
                        listElementTypes = savedListElementTypes;
                        mapKeyValueTypes = savedMapKeyValueTypes;
                        tupleElementTypes = savedTupleElementTypes;
                        pointerElementTypes = savedPointerElementTypes;
                        movedVariables = savedMovedVariables;
                        pointerBorrowTarget = savedPointerBorrowTarget;
                        activeBorrowers = savedActiveBorrowers;
                        activeMutBorrower = savedActiveMutBorrower;
                        variableScopeDepth = savedVariableScopeDepth;
                        cleanupScopes = savedCleanupScopes;
                        pointerBorrowScopes = savedPointerBorrowScopes;
                        variableScopeDepthScopes =
                            savedVariableScopeDepthScopes;

                        if(savedBlock)
                            builder.SetInsertPoint(savedBlock);
                    }
                    if(!callee)
                    {
                        reportError(node->line, "unknown static method: '" +
                                                    node->name + "'");
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
                                                         node->name, ""))
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

                        llvm::Type* expectedType =
                            callee->getArg(idx)->getType();
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

    if(!validateTemporaryBorrowArguments(node->arguments, node->name, ""))
        return nullptr;

    const bool isFreeLikeCall =
        isFreeLikeFunctionName(node->name) && node->arguments.size() == 1;
    if(isFreeLikeCall &&
       !precheckFreeLikeArgument(node->arguments[0], node->name))
    {
        return nullptr;
    }

    std::vector<llvm::Value*> argVals;
    argVals.reserve(node->arguments.size());
    for(auto arg : node->arguments)
    {
        llvm::Value* argVal = generateExpression(arg);
        if(!argVal)
            return nullptr;
        // &s / &mut s where s is a string: generateExpression(OP_ADDR) returns
        // the alloca (char**). Load through to get the actual char* so the
        // callee receives the string pointer, not the stack address.
        if(auto* unary = dynamic_cast<UnaryOpNode*>(arg))
        {
            if(unary->op == UnaryOpNode::OP_ADDR ||
               unary->op == UnaryOpNode::OP_ADDR_MUT)
            {
                if(auto* id = dynamic_cast<IdentifierNode*>(unary->operand))
                {
                    auto tit = variableTypes.find(id->name);
                    if(tit != variableTypes.end() &&
                       (tit->second == TypeNode::TYPE_STRING ||
                        tit->second == TypeNode::TYPE_STR8 ||
                        tit->second == TypeNode::TYPE_STR16))
                    {
                        argVal = builder.CreateLoad(argVal->getType(), argVal,
                                                    id->name + ".str_deref");
                    }
                }
            }
        }
        argVals.push_back(argVal);
    }

    FunctionOverloadInfo* best = nullptr;
    int bestCost = std::numeric_limits<int>::max();
    bool ambiguous = false;
    std::string privateModule;

    for(auto& info : overloadIt->second)
    {
        if(usingTailQualifiedLookup && !qualifiedModuleFilter.empty() &&
           !Helpers::is_same_module_family(qualifiedModuleFilter, info.sourceModule))
        {
            continue;
        }
        if(!isOverloadVisible(info))
        {
            if(privateModule.empty() && !info.sourceModule.empty())
                privateModule = info.sourceModule;
            continue;
        }

        llvm::Function* callee = info.function;
        if(!callee)
            continue;
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
            if(info.node && info.node->parameters &&
               i < info.node->parameters->parameters.size())
            {
                TypeNode* expectedSemantic =
                    info.node->parameters->parameters[i]->type;
                if(auto* refType =
                       dynamic_cast<ReferenceTypeNode*>(expectedSemantic))
                {
                    expectedSemantic = refType->elementType;
                }

                TypeNode* actualSemantic =
                    semanticArgumentType(node->arguments[i]);
                if(auto* traitObj =
                       dynamic_cast<TraitObjectTypeNode*>(expectedSemantic))
                {
                    if(auto* actualTraitObj =
                           dynamic_cast<TraitObjectTypeNode*>(actualSemantic))
                    {
                        if(Helpers::trait_names_equivalent(actualTraitObj->traitName,
                                                  traitObj->traitName))
                            continue;
                    }
                    if(!concreteTypeImplementsTrait(actualSemantic,
                                                    traitObj->traitName))
                    {
                        ok = false;
                        break;
                    }
                    continue;
                }
                if((isCompositeSemanticType(expectedSemantic) ||
                    isCompositeSemanticType(actualSemantic)) &&
                   actualSemantic &&
                   typeMangle(actualSemantic) != typeMangle(expectedSemantic))
                {
                    ok = false;
                    break;
                }
            }

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
            reportError(node->line, "no matching overload for function '" +
                                        node->name + "'");
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
                else
                {
                    bool convertedTraitObject = false;
                    if(best->node && best->node->parameters &&
                       paramIdx <
                           (size_t)best->node->parameters->parameters.size())
                    {
                        auto* declParam =
                            best->node->parameters->parameters[paramIdx];
                        if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(
                               declParam ? declParam->type : nullptr))
                        {
                            llvm::Value* traitObjVal = nullptr;
                            TypeNode* actualSemantic = semanticArgumentType(
                                node->arguments[paramIdx]);
                            if(dynamic_cast<TraitObjectTypeNode*>(actualSemantic))
                            {
                                traitObjVal = coerceTraitObjectValue(
                                    argVal, expectedType, node->line);
                            }
                            else
                            {
                                traitObjVal = buildTraitObjectValue(
                                    node->arguments[paramIdx],
                                    traitObj->traitName, node->line);
                            }
                            if(!traitObjVal)
                                return nullptr;
                            args.push_back(traitObjVal);
                            paramIdx++;
                            convertedTraitObject = true;
                        }
                    }

                    if(convertedTraitObject)
                        continue;

                    if(actualType->isPointerTy() && expectedType->isPointerTy())
                    {
                        argVal = builder.CreateBitCast(argVal, expectedType,
                                                       "arg.ptrcast");
                    }
                    else
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
                            actualStr = "f32";
                        else if(actualType->isDoubleTy())
                            actualStr = "f64";
                        else
                            actualStr = "unknown";

                        if(expectedType->isStructTy())
                            expectedStr =
                                expectedType->getStructName().str().empty()
                                    ? "struct"
                                    : expectedType->getStructName().str();
                        else if(expectedType->isIntegerTy())
                            expectedStr = "i" + std::to_string(
                                                       expectedType->getIntegerBitWidth());
                        else if(expectedType->isFloatTy())
                            expectedStr = "f32";
                        else if(expectedType->isDoubleTy())
                            expectedStr = "f64";
                        else
                            expectedStr = "unknown";

                        reportError(
                            node->line,
                            "argument " + std::to_string(paramIdx + 1) +
                                " of function '" + node->name +
                                "' has wrong type: expected '" + expectedStr +
                                "', got '" + actualStr + "'");
                        return nullptr;
                    }
                }
            }
        }

        // &mut T parameter requires caller to pass &mut value.
        // &T accepts both plain value and &val (string is Copy).
        if(best->node &&
           paramIdx < (size_t)best->node->parameters->parameters.size() &&
           paramIdx < node->arguments.size())
        {
            auto* declParam = best->node->parameters->parameters[paramIdx];
            if(auto* refType =
                   dynamic_cast<ReferenceTypeNode*>(declParam->type))
            {
                if(refType->isMutable)
                {
                    auto* argExpr = node->arguments[paramIdx];
                    auto* unary = dynamic_cast<UnaryOpNode*>(argExpr);
                    bool isRefMut =
                        unary && unary->op == UnaryOpNode::OP_ADDR_MUT;
                    if(!isRefMut)
                    {
                        reportError(node->line, "parameter '" +
                                                    declParam->name +
                                                    "' expects &mut argument");
                        return nullptr;
                    }
                }
            }
        }

        // Consume move-only values only for non-reference parameters.
        // Reference parameters (&T / &mut T) borrow and must not move.
        bool shouldConsume = true;
        if(best->node &&
           paramIdx < (size_t)best->node->parameters->parameters.size() &&
           paramIdx < node->arguments.size())
        {
            auto* declParam = best->node->parameters->parameters[paramIdx];
            if(dynamic_cast<ReferenceTypeNode*>(declParam->type))
                shouldConsume = false;
        }
        if(shouldConsume && paramIdx < node->arguments.size())
        {
            consumeMoveFromExpression(node->arguments[paramIdx], node->line,
                                      "passing argument to function '" +
                                          node->name + "'");
        }

        args.push_back(argVal);
        paramIdx++;
    }

    llvm::Value* callResult = nullptr;
    if(callee->getReturnType()->isVoidTy())
        callResult = builder.CreateCall(callee, args);
    else
        callResult = builder.CreateCall(callee, args, "calltmp");

    if(isFreeLikeCall)
        markFreeLikeConsumed(node->arguments[0]);

    return callResult;
}

llvm::Function* CodeGenerator::generateClosureFn(ClosureNode* node)
{
    if(node->parameters && !node->parameters->parameters.empty())
    {
        reportError(node->line,
                    "thread_spawn closure does not support parameters");
        return nullptr;
    }

    static int closureSeq = 0;
    std::string closureName = "__mlang_closure_" + std::to_string(closureSeq++);

    llvm::FunctionType* closureFnType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {}, false);
    llvm::Function* closureFn =
        llvm::Function::Create(closureFnType, llvm::Function::PrivateLinkage,
                               closureName, module.get());

    // Save current codegen state so we can restore it after the closure body
    auto savedIP = builder.saveIP();
    auto savedNamedValues = namedValues;
    auto savedConstantVars = constantVariables;
    auto savedMovedVars = movedVariables;
    auto savedPtrBorrowTarget = pointerBorrowTarget;
    auto savedActiveBorrowers = activeBorrowers;
    auto savedActiveMutBorrow = activeMutBorrower;
    auto savedVarScopeDepth = variableScopeDepth;
    auto savedVarTypes = variableTypes;
    auto savedStructVarTypes = structVariableTypes;
    auto savedEnumVarTypes = enumVariableTypes;
    auto savedCleanupScopes = cleanupScopes;
    auto savedPtrBorrowScopes = pointerBorrowScopes;
    auto savedVarDepthScopes = variableScopeDepthScopes;
    auto savedLoopBreak = loopBreakBlocks;
    auto savedLoopContinue = loopContinueBlocks;
    auto savedModule = currentModule;
    auto savedClosureVars = closureVariables;
    auto savedActiveInline = activeInlineClosures;

    // Initialise a fresh scope for the closure body
    namedValues.clear();
    constantVariables.clear();
    movedVariables.clear();
    closureVariables.clear();
    activeInlineClosures.clear();
    pointerBorrowTarget.clear();
    pointerKnownNull.clear();
    activeBorrowers.clear();
    activeMutBorrower.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    enumVariableTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    loopBreakBlocks.clear();
    loopContinueBlocks.clear();
    currentModule = "";

    llvm::BasicBlock* entry =
        llvm::BasicBlock::Create(context, "entry", closureFn);
    builder.SetInsertPoint(entry);

    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    if(node->body)
    {
        for(auto* stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock() &&
               builder.GetInsertBlock()->getTerminator())
                break;
        }
    }

    exitCleanupScope();

    if(!builder.GetInsertBlock()->getTerminator())
        builder.CreateRetVoid();

    // Restore caller state
    namedValues = std::move(savedNamedValues);
    constantVariables = std::move(savedConstantVars);
    movedVariables = std::move(savedMovedVars);
    pointerBorrowTarget = std::move(savedPtrBorrowTarget);
    activeBorrowers = std::move(savedActiveBorrowers);
    activeMutBorrower = std::move(savedActiveMutBorrow);
    variableScopeDepth = std::move(savedVarScopeDepth);
    variableTypes = std::move(savedVarTypes);
    structVariableTypes = std::move(savedStructVarTypes);
    enumVariableTypes = std::move(savedEnumVarTypes);
    cleanupScopes = std::move(savedCleanupScopes);
    pointerBorrowScopes = std::move(savedPtrBorrowScopes);
    variableScopeDepthScopes = std::move(savedVarDepthScopes);
    loopBreakBlocks = std::move(savedLoopBreak);
    loopContinueBlocks = std::move(savedLoopContinue);
    currentModule = std::move(savedModule);
    closureVariables = std::move(savedClosureVars);
    activeInlineClosures = std::move(savedActiveInline);
    builder.restoreIP(savedIP);

    return closureFn;
}

llvm::Value* CodeGenerator::generateThreadSpawn(FunctionCallNode* node)
{
    // A thread spawn can occur in a transitively imported module whose structs
    // were not merged into the root program. Load the stdlib-owned return type
    // before lowering the intrinsic; the compiler does not synthesize it.
    if(!getStructType("thread") && moduleLoader)
    {
        for(auto* structDef : moduleLoader->getModuleStructs("std::thread"))
        {
            if(structDef && structDef->name == "thread")
            {
                structVisibility[structDef->name] =
                    std::make_pair(structDef->isPublic,
                                   structDef->sourceModule);
                generateStructDefinition(structDef);
                break;
            }
        }
    }

    if(node->arguments.size() < 1 || node->arguments.size() > 5)
    {
        reportError(node->line,
                    "thread_spawn expects a function name and up to 4 integer "
                    "arguments");
        return nullptr;
    }

    size_t argCount = 0;
    llvm::Function* targetFunc = nullptr;

    if(auto* closureArg = dynamic_cast<ClosureNode*>(node->arguments[0]))
    {
        if(node->arguments.size() != 1)
        {
            reportError(node->line,
                        "closure-based thread_spawn takes no extra arguments");
            return nullptr;
        }
        targetFunc = generateClosureFn(closureArg);
        if(!targetFunc)
            return nullptr;
        // argCount stays 0 — closure captures nothing via the arg buffer
    }
    else
    {
        auto* targetId = dynamic_cast<IdentifierNode*>(node->arguments[0]);
        if(!targetId)
        {
            reportError(
                node->line,
                "thread_spawn expects a function name identifier or closure");
            return nullptr;
        }

        argCount = node->arguments.size() - 1;

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
                    reportError(node->line, "ambiguous function: '" +
                                                targetId->name +
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
            llvm::Type* paramType =
                targetFunc->getFunctionType()->getParamType(i);
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
            reportError(
                node->line,
                "thread_spawn arguments must be integer or handle types");
            return nullptr;
        }
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

    std::string wrapperName =
        "__mlang_thread_wrapper_" + targetFunc->getName().str();
    if(argCount > 0)
        wrapperName += "_args" + std::to_string(argCount);
    llvm::Function* wrapperFunc = module->getFunction(wrapperName);
    if(!wrapperFunc)
    {
        llvm::FunctionType* wrapperType =
            llvm::FunctionType::get(ptrType, {ptrType}, false);
        wrapperFunc =
            llvm::Function::Create(wrapperType, llvm::Function::PrivateLinkage,
                                   wrapperName, module.get());

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
                llvm::Value* bytePtr =
                    builder.CreateGEP(llvm::Type::getInt8Ty(context), basePtr,
                                      offset, "thread.argbyte");
#if LLVM_VERSION_MAJOR < 15
                llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
                llvm::Value* argPtr = builder.CreateBitCast(
                    bytePtr, int64PtrType, "thread.argptr");
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
                            callArg = builder.CreateTrunc(callArg, expectedType,
                                                          "thread.trunc");
                        else if(srcBits < dstBits)
                            callArg = builder.CreateSExt(callArg, expectedType,
                                                         "thread.sext");
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

        llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
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
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
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
            llvm::Value* rawArg = generateExpression(node->arguments[i + 1]);
            if(!rawArg)
                return nullptr;
            if(rawArg->getType()->isIntegerTy())
            {
                if(rawArg->getType() != int64Type)
                {
                    rawArg =
                        builder.CreateSExt(rawArg, int64Type, "thread.argsext");
                }
            }
            else if(rawArg->getType()->isStructTy())
            {
                auto* structType =
                    llvm::cast<llvm::StructType>(rawArg->getType());
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
                    rawArg =
                        builder.CreateSExt(rawArg, int64Type, "thread.argsext");
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
            llvm::Value* bytePtr =
                builder.CreateGEP(llvm::Type::getInt8Ty(context), argPtr,
                                  offset, "thread.argbyte");
#if LLVM_VERSION_MAJOR < 15
            llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
            llvm::Value* typedPtr = builder.CreateBitCast(bytePtr, int64PtrType,
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

    return buildHandleValue("thread", rawHandle, node->line);
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

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* threadPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "thread.ptr");
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
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
    return builder.CreateInsertValue(
        handleVal, rawVal, static_cast<unsigned>(rawIndex), "handle.raw");
}

llvm::Value* CodeGenerator::extractHandleValue(
    ExpressionNode* expr, const std::string& expectedHandleType, int line)
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
            rawVal = builder.CreateSExt(rawVal, int64Type, "handle.sext");
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
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
#if LLVM_VERSION_MAJOR >= 15
        llvm::cast<llvm::PointerType>(ptrType)
#else
        llvm::cast<llvm::PointerType>(ptrType)
#endif
    );
    builder.CreateCall(pthreadMutexInitFunc, {mem, nullPtr});
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(mem, int64Type, "mutex.handle_i64");
    return rawHandle;
}

llvm::Value*
CodeGenerator::createInternalMutexHandle(bool recursive,
                                         const std::string& namePrefix)
{
    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int32Type = llvm::Type::getInt32Ty(context);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* mutexSize = llvm::ConstantInt::get(int64Type, 64, false);
    llvm::Value* mutexMem =
        builder.CreateCall(mallocFunc, {mutexSize}, namePrefix + ".mutex.mem");
    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType));

    if(recursive)
    {
        llvm::Value* attrSize = llvm::ConstantInt::get(
            int64Type, static_cast<uint64_t>(sizeof(pthread_mutexattr_t)),
            false);
        llvm::Value* attrMem = builder.CreateCall(mallocFunc, {attrSize},
                                                  namePrefix + ".attr.mem");
        builder.CreateCall(pthreadMutexAttrInitFunc, {attrMem});
        builder.CreateCall(
            pthreadMutexAttrSetTypeFunc,
            {attrMem,
             llvm::ConstantInt::get(
                 int32Type, static_cast<uint64_t>(PTHREAD_MUTEX_RECURSIVE),
                 false)});
        builder.CreateCall(pthreadMutexInitFunc, {mutexMem, attrMem});
        builder.CreateCall(pthreadMutexAttrDestroyFunc, {attrMem});
        builder.CreateCall(freeFunc, {attrMem});
    }
    else
    {
        builder.CreateCall(pthreadMutexInitFunc, {mutexMem, nullPtr});
    }

    return builder.CreatePtrToInt(mutexMem, int64Type, namePrefix + ".handle");
}

void CodeGenerator::destroyInternalMutexHandle(llvm::Value* rawHandle,
                                               const std::string& namePrefix)
{
    if(!rawHandle)
        return;

    initializePthreadFunctions();
    initializeStdlibFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(rawHandle, ptrType, namePrefix + ".ptr");
    builder.CreateCall(pthreadMutexDestroyFunc, {mutexPtr});
    builder.CreateCall(freeFunc, {mutexPtr});
}

llvm::Value* CodeGenerator::ensurePropertyMutexHandle(
    llvm::Value* handleSlotPtr, bool recursive, const std::string& namePrefix)
{
    if(!handleSlotPtr)
        return nullptr;

    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
    auto* currentHandle =
        builder.CreateLoad(int64Type, handleSlotPtr, namePrefix + ".cur");
    currentHandle->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);

    llvm::Function* curFn = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* entryBB = builder.GetInsertBlock();
    llvm::BasicBlock* initBB =
        llvm::BasicBlock::Create(context, namePrefix + ".init", curFn);
    llvm::BasicBlock* doneBB =
        llvm::BasicBlock::Create(context, namePrefix + ".done", curFn);
    llvm::Value* hasHandle = builder.CreateICmpNE(
        currentHandle, llvm::ConstantInt::get(int64Type, 0),
        namePrefix + ".has_handle");
    builder.CreateCondBr(hasHandle, doneBB, initBB);

    builder.SetInsertPoint(initBB);
    llvm::Value* candidate =
        createInternalMutexHandle(recursive, namePrefix + ".new");
    auto* cmpxchg = builder.CreateAtomicCmpXchg(
        handleSlotPtr, llvm::ConstantInt::get(int64Type, 0), candidate,
        llvm::MaybeAlign(), llvm::AtomicOrdering::SequentiallyConsistent,
        llvm::AtomicOrdering::SequentiallyConsistent);
    cmpxchg->setWeak(false);
    llvm::Value* installed =
        builder.CreateExtractValue(cmpxchg, 0, namePrefix + ".installed");
    llvm::Value* success =
        builder.CreateExtractValue(cmpxchg, 1, namePrefix + ".success");
    llvm::BasicBlock* winnerBB =
        llvm::BasicBlock::Create(context, namePrefix + ".winner", curFn);
    llvm::BasicBlock* loserBB =
        llvm::BasicBlock::Create(context, namePrefix + ".loser", curFn);
    builder.CreateCondBr(success, winnerBB, loserBB);

    builder.SetInsertPoint(winnerBB);
    builder.CreateBr(doneBB);

    builder.SetInsertPoint(loserBB);
    destroyInternalMutexHandle(candidate, namePrefix + ".discard");
    builder.CreateBr(doneBB);

    builder.SetInsertPoint(doneBB);
    auto* phi = builder.CreatePHI(int64Type, 3, namePrefix + ".handle");
    phi->addIncoming(currentHandle, entryBB);
    phi->addIncoming(candidate, winnerBB);
    phi->addIncoming(installed, loserBB);
    return phi;
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

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    return builder.CreateCall(pthreadMutexLockFunc, {mutexPtr}, "mutex.lock");
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

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
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

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* mutexPtr =
        builder.CreateIntToPtr(handleVal, ptrType, "mutex.ptr");
    llvm::Value* result = builder.CreateCall(pthreadMutexDestroyFunc,
                                             {mutexPtr}, "mutex.destroy");
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
        initVal = builder.CreateSExt(initVal, int64Type, "atomic.sext");

    llvm::Value* sizeVal = llvm::ConstantInt::get(int64Type, 8, false);
    llvm::Value* mem = builder.CreateCall(mallocFunc, {sizeVal}, "atomic.mem");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
    llvm::Value* typedPtr =
        builder.CreateBitCast(mem, int64PtrType, "atomic.ptr");
    builder.CreateStore(initVal, typedPtr);
#else
    builder.CreateStore(initVal, mem);
#endif
    llvm::Value* rawHandle =
        builder.CreatePtrToInt(mem, int64Type, "atomic.handle_i64");
    return rawHandle;
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

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
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

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    llvm::Value* valueVal = generateExpression(node->arguments[1]);
    if(!handleVal || !valueVal)
        return nullptr;
    if(valueVal->getType() != int64Type)
        valueVal = builder.CreateSExt(valueVal, int64Type, "atomic.sextval");

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
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

    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    llvm::Value* addVal = generateExpression(node->arguments[1]);
    if(!handleVal || !addVal)
        return nullptr;
    if(addVal->getType() != int64Type)
        addVal = builder.CreateSExt(addVal, int64Type, "atomic.sextval");

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
#if LLVM_VERSION_MAJOR < 15
    llvm::Type* int64PtrType = llvm::PointerType::get(int64Type, 0);
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

    std::string freeOwner = resolveBorrowOwnerFromLValue(node->arguments[0]);
    if(!freeOwner.empty() &&
       globalNamedValues.find(freeOwner) == globalNamedValues.end() &&
       isVariableMoved(freeOwner))
    {
        reportError(node->line, "double free or use-after-free of value: '" +
                                    freeOwner + "'");
        return nullptr;
    }
    llvm::Value* handleVal =
        extractHandleValue(node->arguments[0], "", node->line);
    if(!handleVal)
        return nullptr;

    llvm::Value* ptr = builder.CreateIntToPtr(handleVal, ptrType, "atomic.ptr");
    if(!freeOwner.empty() &&
       globalNamedValues.find(freeOwner) == globalNamedValues.end())
    {
        movedVariables.insert(freeOwner);
    }
    return builder.CreateCall(freeFunc, {ptr});
}
