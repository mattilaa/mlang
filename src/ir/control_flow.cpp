#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"

#include <llvm/Config/llvm-config.h>

using mlang::ir_detail::ast_analysis::contains_unsupported_try_control_flow;
using mlang::ir_detail::ast_analysis::strip_iter_methods;
using mlang::ir_detail::common::Helpers;

void CodeGenerator::generateReturnStatement(ReturnNode* node)
{
    llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
    llvm::Type* expectedRetType = currentFunc->getReturnType();

    if(node->expression)
    {
        if(!validateNoEscapingBorrow(node->expression, node->line, "return"))
            return;

        llvm::Value* returnValue = nullptr;
        if(auto* traitObj =
               dynamic_cast<TraitObjectTypeNode*>(currentSemanticReturnType))
        {
            llvm::Type* traitObjType = getLLVMTypeFromNode(traitObj);
            TypeNode* exprType = getLValueType(node->expression, node->line);
            if(dynamic_cast<TraitObjectTypeNode*>(exprType))
            {
                returnValue = generateExpression(node->expression);
                returnValue = coerceTraitObjectValue(returnValue, traitObjType,
                                                     node->line);
            }
            else if(exprType)
                returnValue = buildTraitObjectValue(
                    node->expression, traitObj->traitName, node->line, true);
            else
            {
                returnValue = generateExpression(node->expression);
                returnValue = coerceTraitObjectValue(returnValue, traitObjType,
                                                     node->line);
            }
        }
        else
        {
            if(auto* returnList =
                   dynamic_cast<GenericListTypeNode*>(
                       currentSemanticReturnType))
            {
                if(auto* returnArray =
                       dynamic_cast<ArrayTypeNode*>(
                           currentSemanticReturnType))
                {
                    if(!validateFixedArrayInitializer(
                           returnArray, node->expression, node->line))
                        return;
                }

                llvm::Type* elemType =
                    getLLVMTypeFromNode(returnList->elementType);
                if(auto* listLit =
                       dynamic_cast<ListLiteralNode*>(node->expression))
                    returnValue = generateListLiteral(listLit, elemType);
                else if(auto* arrFill =
                            dynamic_cast<ArrayFillNode*>(node->expression))
                    returnValue = generateArrayFill(arrFill, elemType);
            }
            else if(auto* returnMap =
                        dynamic_cast<MapTypeNode*>(currentSemanticReturnType))
            {
                if(auto* mapLit =
                       dynamic_cast<MapLiteralNode*>(node->expression))
                {
                    llvm::Type* keyType =
                        getLLVMTypeFromNode(returnMap->keyType);
                    llvm::Type* valueType =
                        getLLVMTypeFromNode(returnMap->valueType);
                    returnValue =
                        generateMapLiteral(mapLit, keyType, valueType);
                }
            }

            if(!returnValue)
                returnValue = generateExpression(node->expression);
        }
        if(!returnValue)
            return;
        consumeMoveFromExpression(node->expression, node->line, "return");
        returnValue = applyStructCopySemantics(returnValue);

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
                    actualStr = "f32";
                else if(actualType->isDoubleTy())
                    actualStr = "f64";
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
                    expectedStr = "f32";
                else if(expectedRetType->isDoubleTy())
                    expectedStr = "f64";
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

        if(currentFunctionExceptionFrame)
            builder.CreateCall(exceptionsPopFrameFunc,
                               {currentFunctionExceptionFrame});
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
        if(currentFunctionExceptionFrame)
            builder.CreateCall(exceptionsPopFrameFunc,
                               {currentFunctionExceptionFrame});
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

void CodeGenerator::generateThrowStatement(ThrowNode* node)
{
    if(!node || !node->expression)
    {
        reportError(node ? node->line : 0, "throw requires an exception value");
        return;
    }

    llvm::Value* exceptionValue = generateExpression(node->expression);
    if(!exceptionValue)
        return;

    llvm::Type* exceptionType = exceptionValue->getType();
    if(!exceptionType->isStructTy())
    {
        reportError(node->line, "throw expects a struct exception value");
        return;
    }

    auto* exceptionStructType = llvm::cast<llvm::StructType>(exceptionType);
    std::string exceptionStructName = exceptionStructType->getName().str();
    auto membersIt = structMembers.find(exceptionStructName);
    if(membersIt == structMembers.end())
    {
        reportError(node->line, "throw expects a named exception struct");
        return;
    }

    int typeNameIndex = -1;
    int messageIndex = -1;
    int sourceLineIndex = -1;
    for(size_t i = 0; i < membersIt->second.size(); ++i)
    {
        const auto& member = membersIt->second[i];
        if(member.first == "type_name")
            typeNameIndex = static_cast<int>(i);
        else if(member.first == "message")
            messageIndex = static_cast<int>(i);
        else if(member.first == "source_line")
            sourceLineIndex = static_cast<int>(i);
    }

    if(typeNameIndex < 0 || messageIndex < 0)
    {
        reportError(node->line,
                    "throw expects fields 'type_name' and 'message'");
        return;
    }

    initializeStdlibFunctions();
    llvm::Value* typeName = builder.CreateExtractValue(
        exceptionValue, typeNameIndex, "throw.type_name");
    llvm::Value* message = builder.CreateExtractValue(
        exceptionValue, messageIndex, "throw.message");
    llvm::Value* sourceLine =
        sourceLineIndex >= 0
            ? builder.CreateExtractValue(exceptionValue, sourceLineIndex,
                                         "throw.source_line")
            : llvm::ConstantInt::get(llvm::Type::getInt32Ty(context),
                                     node->line);

    if(sourceLine->getType()->isIntegerTy() &&
       sourceLine->getType() != llvm::Type::getInt32Ty(context))
    {
        sourceLine =
            builder.CreateIntCast(sourceLine, llvm::Type::getInt32Ty(context),
                                  true, "throw.line.cast");
    }

    consumeMoveFromExpression(node->expression, node->line, "throw");
    builder.CreateCall(exceptionsThrowFunc, {typeName, message, sourceLine});
    builder.CreateUnreachable();
}

void CodeGenerator::generateTryCatchStatement(TryCatchNode* node)
{
    if(!node || !node->tryBlock || !node->catchBlock || !node->catchType)
        return;

    if(contains_unsupported_try_control_flow(node->tryBlock))
    {
        reportError(node->line, "return, break, and continue are not yet "
                                "supported inside try blocks");
        return;
    }

    auto* catchStruct = dynamic_cast<StructTypeRefNode*>(node->catchType);
    if(!catchStruct)
    {
        reportError(node->line,
                    "catch currently requires a struct exception type");
        return;
    }

    llvm::Type* catchLlvmType = getLLVMTypeFromNode(node->catchType);
    auto* catchStructType =
        llvm::dyn_cast_or_null<llvm::StructType>(catchLlvmType);
    if(!catchStructType)
    {
        reportError(node->line, "catch type must lower to a struct");
        return;
    }

    std::string catchStructName = catchStructType->getName().str();
    auto catchMembersIt = structMembers.find(catchStructName);
    if(catchMembersIt == structMembers.end())
    {
        reportError(node->line,
                    "unknown catch type: " + catchStruct->structName);
        return;
    }

    int typeNameIndex = -1;
    int messageIndex = -1;
    int sourceLineIndex = -1;
    int ownedIndex = -1;
    for(size_t i = 0; i < catchMembersIt->second.size(); ++i)
    {
        const auto& member = catchMembersIt->second[i];
        if(member.first == "type_name")
            typeNameIndex = static_cast<int>(i);
        else if(member.first == "message")
            messageIndex = static_cast<int>(i);
        else if(member.first == "source_line")
            sourceLineIndex = static_cast<int>(i);
        else if(member.first == "owned")
            ownedIndex = static_cast<int>(i);
    }
    if(typeNameIndex < 0 || messageIndex < 0 || sourceLineIndex < 0)
    {
        reportError(
            node->line,
            "catch type must contain type_name, message, and source_line");
        return;
    }

    initializeStdlibFunctions();
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::Value* frameHandle =
        builder.CreateCall(exceptionsPushFrameFunc, {}, "try.exc.frame");
    llvm::Value* frameEnv = builder.CreateCall(exceptionsFrameEnvFunc,
                                               {frameHandle}, "try.exc.env");
    auto* trySetjmpCall =
        builder.CreateCall(exceptionsSetjmpFunc, {frameEnv}, "try.exc.state");
    trySetjmpCall->setCanReturnTwice();
    llvm::Value* frameState = trySetjmpCall;

    llvm::BasicBlock* tryBodyBB =
        llvm::BasicBlock::Create(context, "try.body", function);
    llvm::BasicBlock* catchBB =
        llvm::BasicBlock::Create(context, "try.catch", function);
    llvm::BasicBlock* continueBB =
        llvm::BasicBlock::Create(context, "try.cont", function);

    llvm::Value* enteredNormally = builder.CreateICmpEQ(
        frameState, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
        "try.exc.ok");
    builder.CreateCondBr(enteredNormally, tryBodyBB, catchBB);

    int handlerScopeDepth = currentScopeDepth();

    builder.SetInsertPoint(tryBodyBB);
    enterCleanupScope();
    if(node->tryBlock->isUnsafe)
        unsafeDepth++;
    if(node->tryBlock->statements)
    {
        for(auto* stmt : node->tryBlock->statements->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
    }

    auto tryNamedValues = namedValues;
    auto tryStructVariableTypes = structVariableTypes;
    auto tryCleanupScopes = cleanupScopes;
    auto tryMovedVariables = movedVariables;

    if(node->tryBlock->isUnsafe)
        unsafeDepth--;
    exitCleanupScope();
    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateCall(exceptionsPopFrameFunc, {frameHandle});
        builder.CreateBr(continueBB);
    }

    builder.SetInsertPoint(catchBB);
    builder.CreateCall(exceptionsPopFrameFunc, {frameHandle});
    auto outerNamedValues = namedValues;
    auto outerStructVariableTypes = structVariableTypes;
    auto outerCleanupScopes = cleanupScopes;
    auto outerMovedVariables = movedVariables;
    namedValues = tryNamedValues;
    structVariableTypes = tryStructVariableTypes;
    cleanupScopes = tryCleanupScopes;
    movedVariables = tryMovedVariables;
    emitActiveCleanupsDeeperThan(handlerScopeDepth);
    namedValues = outerNamedValues;
    structVariableTypes = outerStructVariableTypes;
    cleanupScopes = outerCleanupScopes;
    movedVariables = outerMovedVariables;

    llvm::Value* caughtTypeName =
        builder.CreateCall(exceptionsTakeTypeNameFunc, {}, "catch.type_name");
    llvm::Value* caughtMessage =
        builder.CreateCall(exceptionsTakeMessageFunc, {}, "catch.message");
    llvm::Value* caughtSourceLine = builder.CreateCall(
        exceptionsTakeSourceLineFunc, {}, "catch.source_line");

    llvm::Value* catchValue = llvm::UndefValue::get(catchStructType);
    catchValue = builder.CreateInsertValue(
        catchValue, caughtTypeName, {static_cast<unsigned>(typeNameIndex)});
    catchValue = builder.CreateInsertValue(
        catchValue, caughtMessage, {static_cast<unsigned>(messageIndex)});
    catchValue = builder.CreateInsertValue(
        catchValue, caughtSourceLine, {static_cast<unsigned>(sourceLineIndex)});
    if(ownedIndex >= 0)
    {
        llvm::Type* ownedType =
            catchMembersIt->second[static_cast<size_t>(ownedIndex)].second
                ? getLLVMTypeFromNode(
                      catchMembersIt->second[static_cast<size_t>(ownedIndex)]
                          .second)
                : llvm::Type::getInt1Ty(context);
        llvm::Value* ownedValue = nullptr;
        if(ownedType->isIntegerTy(1))
            ownedValue = llvm::ConstantInt::get(ownedType, 1, false);
        else if(ownedType->isIntegerTy())
            ownedValue = llvm::ConstantInt::get(ownedType, 1, false);
        if(ownedValue)
        {
            catchValue = builder.CreateInsertValue(
                catchValue, ownedValue, {static_cast<unsigned>(ownedIndex)});
        }
    }

    enterCleanupScope();
    llvm::AllocaInst* catchAlloca =
        builder.CreateAlloca(catchStructType, nullptr, node->catchName);
    builder.CreateStore(catchValue, catchAlloca);
    namedValues[node->catchName] = catchAlloca;
    variableTypes[node->catchName] = TypeNode::TYPE_STRUCT;
    structVariableTypes[node->catchName] = catchStructName;
    clearMovedVariable(node->catchName);
    recordVariableScopeDepth(node->catchName);
    registerStructCleanupIfNeeded(node->catchName, catchStructName);

    generateStatement(node->catchBlock);
    exitCleanupScope();
    if(!builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(continueBB);

    builder.SetInsertPoint(continueBB);
}

void CodeGenerator::generateIfStatement(IfNode* node)
{
    if(node->usesColonWithoutGuard && warnPlainColonIf)
    {
        int warnLine = (node->condition && node->condition->line > 0)
                           ? node->condition->line
                           : node->line;
        int warnCol = (node->condition && node->condition->col > 0)
                          ? node->condition->col
                          : node->col;
        reportWarning(warnLine, warnCol,
                      "plain if/else-if with ':' is discouraged; use "
                      "'if cond { ... }' and reserve ':' for guard forms");
    }
    if(node->thenBranch && node->thenBranch->statements.empty())
    {
        int warnLine = (node->condition && node->condition->line > 0)
                           ? node->condition->line
                           : node->line;
        int warnCol = (node->condition && node->condition->col > 0)
                          ? node->condition->col
                          : node->col;
        reportWarning(warnLine, warnCol, "empty block");
    }
    if(node->elseBranch && node->elseBranch->statements.empty())
    {
        int warnLine = (node->condition && node->condition->line > 0)
                           ? node->condition->line
                           : node->line;
        int warnCol = (node->condition && node->condition->col > 0)
                          ? node->condition->col
                          : node->col;
        reportWarning(warnLine, warnCol, "empty block");
    }

    auto incomingNamedValues = namedValues;
    auto incomingConstantVariables = constantVariables;
    auto incomingVariableTypes = variableTypes;
    auto incomingStructVariableTypes = structVariableTypes;
    auto incomingEnumVariableTypes = enumVariableTypes;
    auto incomingListElementTypes = listElementTypes;
    auto incomingMapKeyValueTypes = mapKeyValueTypes;
    auto incomingPointerElementTypes = pointerElementTypes;
    auto incomingMoved = movedVariables;
    auto incomingPointerBorrowTarget = pointerBorrowTarget;
    auto incomingActiveBorrowers = activeBorrowers;
    auto incomingActiveMutBorrower = activeMutBorrower;
    auto restoreIncomingState = [&]()
    {
        namedValues = incomingNamedValues;
        constantVariables = incomingConstantVariables;
        variableTypes = incomingVariableTypes;
        structVariableTypes = incomingStructVariableTypes;
        enumVariableTypes = incomingEnumVariableTypes;
        listElementTypes = incomingListElementTypes;
        mapKeyValueTypes = incomingMapKeyValueTypes;
        pointerElementTypes = incomingPointerElementTypes;
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;
    };

    if(node->conditionInit)
    {
        generateStatement(node->conditionInit);
    }

    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
    {
        restoreIncomingState();
        return;
    }

    auto condNamedValues = namedValues;
    auto condConstantVariables = constantVariables;
    auto condVariableTypes = variableTypes;
    auto condStructVariableTypes = structVariableTypes;
    auto condEnumVariableTypes = enumVariableTypes;
    auto condListElementTypes = listElementTypes;
    auto condMapKeyValueTypes = mapKeyValueTypes;
    auto condPointerElementTypes = pointerElementTypes;

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
        restoreIncomingState();
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
    namedValues = condNamedValues;
    constantVariables = condConstantVariables;
    variableTypes = condVariableTypes;
    structVariableTypes = condStructVariableTypes;
    enumVariableTypes = condEnumVariableTypes;
    listElementTypes = condListElementTypes;
    mapKeyValueTypes = condMapKeyValueTypes;
    pointerElementTypes = condPointerElementTypes;
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    activeBorrowers = incomingActiveBorrowers;
    activeMutBorrower = incomingActiveMutBorrower;
    enterCleanupScope();
    for(auto stmt : node->thenBranch->statements)
    {
        generateStatement(stmt);
    }
    exitCleanupScope();
    auto thenMoved = movedVariables;
    auto thenPointerBorrowTarget = pointerBorrowTarget;
    auto thenActiveBorrowers = activeBorrowers;
    auto thenActiveMutBorrower = activeMutBorrower;
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
    namedValues = incomingNamedValues;
    constantVariables = incomingConstantVariables;
    variableTypes = incomingVariableTypes;
    structVariableTypes = incomingStructVariableTypes;
    enumVariableTypes = incomingEnumVariableTypes;
    listElementTypes = incomingListElementTypes;
    mapKeyValueTypes = incomingMapKeyValueTypes;
    pointerElementTypes = incomingPointerElementTypes;
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    activeBorrowers = incomingActiveBorrowers;
    activeMutBorrower = incomingActiveMutBorrower;

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
    auto elseActiveMutBorrower = activeMutBorrower;
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

    namedValues = incomingNamedValues;
    constantVariables = incomingConstantVariables;
    variableTypes = incomingVariableTypes;
    structVariableTypes = incomingStructVariableTypes;
    enumVariableTypes = incomingEnumVariableTypes;
    listElementTypes = incomingListElementTypes;
    mapKeyValueTypes = incomingMapKeyValueTypes;
    pointerElementTypes = incomingPointerElementTypes;

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
        // Merge &mut: conservative — keep only those live in both branches
        std::map<std::string, std::string> mergedMutBorrower;
        for(const auto& kv : thenActiveMutBorrower)
            if(elseActiveMutBorrower.count(kv.first))
                mergedMutBorrower[kv.first] = kv.second;
        activeMutBorrower = std::move(mergedMutBorrower);
    }
    else
    {
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;
    }
}

void CodeGenerator::generateCexprIfStatement(CexprIfNode* node)
{
    if(!node || !node->condition)
        return;

    ConstexprValue condValue;
    std::string errorMessage;
    if(!evalConstexprExpression(node->condition, condValue, &errorMessage,
                                nullptr, 0))
    {
        reportError(node->line,
                    errorMessage.empty()
                        ? "cexpr if condition requires a compile-time "
                          "expression"
                        : errorMessage);
        return;
    }

    if(condValue.kind == ConstexprValue::Kind::OpaqueStruct ||
       condValue.kind == ConstexprValue::Kind::Type)
    {
        reportError(node->line,
                    "cexpr if condition requires a bool or numeric value, "
                    "not a struct or type_id placeholder");
        return;
    }

    const bool takeThen =
        condValue.kind == ConstexprValue::Kind::Bool
            ? condValue.boolValue
            : constexprValueAsDouble(condValue) != 0.0;
    if(!takeThen && node->elseIfBranch)
    {
        CexprIfNode* last = node->elseIfBranch;
        while(last->elseIfBranch)
            last = last->elseIfBranch;
        StatementListNode* savedFinalElse = last->elseBranch;
        if(!last->elseBranch)
            last->elseBranch = node->elseBranch;
        generateCexprIfStatement(node->elseIfBranch);
        last->elseBranch = savedFinalElse;
        return;
    }

    StatementListNode* selected = takeThen ? node->thenBranch
                                           : node->elseBranch;
    if(!selected)
        return;

    auto savedConstexprValues = constexprValues;
    enterCleanupScope();
    for(auto* stmt : selected->statements)
        generateStatement(stmt);
    exitCleanupScope();
    constexprValues = std::move(savedConstexprValues);
}

// Strip .iter() / .into_iter() / .enumerate() method wrappers from a
// for-loop iterable, returning the underlying collection expression.
// This lets "arr.iter().enumerate()" and "arr.into_iter()" work as
// aliases for the plain collection "arr".
void CodeGenerator::generateWhileStatement(WhileNode* node)
{
    if(node->usesColonWithoutGuard && warnPlainColonWhile)
    {
        int warnLine = (node->condition && node->condition->line > 0)
                           ? node->condition->line
                           : node->line;
        int warnCol = (node->condition && node->condition->col > 0)
                          ? node->condition->col
                          : node->col;
        reportWarning(warnLine, warnCol,
                      "plain while with ':' is discouraged; use "
                      "'while cond { ... }' and reserve ':' for guard forms");
    }
    if(node->body && node->body->statements.empty())
    {
        int warnLine = (node->condition && node->condition->line > 0)
                           ? node->condition->line
                           : node->line;
        int warnCol = (node->condition && node->condition->col > 0)
                          ? node->condition->col
                          : node->col;
        reportWarning(warnLine, warnCol, "empty block");
    }

    auto incomingMoved = movedVariables;
    auto incomingPointerBorrowTarget = pointerBorrowTarget;
    auto incomingActiveBorrowers = activeBorrowers;
    auto incomingActiveMutBorrower = activeMutBorrower;

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "while.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "while.body");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "while.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(condBB);

    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
    {
        loopBreakBlocks.pop_back();
        loopContinueBlocks.pop_back();
        return;
    }

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
                    "while condition must be a boolean or numeric type, got '" +
                        typeStr + "'");
        loopBreakBlocks.pop_back();
        loopContinueBlocks.pop_back();
        return;
    }

    if(!condValue->getType()->isIntegerTy(1))
    {
        if(condValue->getType()->isFloatingPointTy())
        {
            condValue = builder.CreateFCmpONE(
                condValue, llvm::ConstantFP::get(condValue->getType(), 0.0),
                "whilecond");
        }
        else
        {
            condValue = builder.CreateICmpNE(
                condValue, llvm::ConstantInt::get(condValue->getType(), 0),
                "whilecond");
        }
    }

    builder.CreateCondBr(condValue, bodyBB, endBB);

    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    if(node->body)
    {
        enterCleanupScope();
        for(auto* stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
        exitCleanupScope();
    }
    llvm::BasicBlock* bodyEnd = builder.GetInsertBlock();
    bool bodyFallsThrough = bodyEnd && !bodyEnd->getTerminator();
    auto bodyMoved = movedVariables;
    auto bodyPointerBorrowTarget = pointerBorrowTarget;
    auto bodyActiveBorrowers = activeBorrowers;
    auto bodyActiveMutBorrower = activeMutBorrower;

    if(bodyFallsThrough)
    {
        builder.CreateBr(condBB);
    }

    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    activeBorrowers = incomingActiveBorrowers;
    activeMutBorrower = incomingActiveMutBorrower;

    if(bodyFallsThrough)
    {
        movedVariables.insert(bodyMoved.begin(), bodyMoved.end());
        for(const auto& kv : bodyPointerBorrowTarget)
        {
            if(pointerBorrowTarget.find(kv.first) == pointerBorrowTarget.end())
            {
                pointerBorrowTarget[kv.first] = kv.second;
            }
        }
        for(const auto& kv : bodyActiveBorrowers)
        {
            auto& setRef = activeBorrowers[kv.first];
            setRef.insert(kv.second.begin(), kv.second.end());
        }
        for(const auto& kv : bodyActiveMutBorrower)
        {
            if(activeMutBorrower.find(kv.first) == activeMutBorrower.end())
            {
                activeMutBorrower[kv.first] = kv.second;
            }
        }
    }
}

void CodeGenerator::generateForStatement(ForNode* node)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    // Strip .iter() / .into_iter() / .enumerate() wrappers from the iterable.
    // The actual iteration is always over the underlying collection.
    ExpressionNode* iterableExpr = strip_iter_methods(node->iterable);

    // Check if it's a range expression
    auto* rangeExpr = dynamic_cast<RangeExpressionNode*>(iterableExpr);
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
                typeStr = "f32";
            else if(startVal->getType()->isDoubleTy())
                typeStr = "f64";
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
                typeStr = "f32";
            else if(endVal->getType()->isDoubleTy())
                typeStr = "f64";
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
        auto oldDepthIt = variableScopeDepth.find(node->varName);
        bool hadOldDepth = oldDepthIt != variableScopeDepth.end();
        int oldDepth = hadOldDepth ? oldDepthIt->second : 0;
        auto stateBeforeLoopMoved = movedVariables;
        auto stateBeforeLoopPointerBorrowTarget = pointerBorrowTarget;
        auto stateBeforeLoopActiveBorrowers = activeBorrowers;

        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = TypeNode::TYPE_I64;
        clearMovedVariable(node->varName);
        recordVariableScopeDepth(node->varName);

        bool rangeNeverExecutes = false;
        if(auto* startConst = llvm::dyn_cast<llvm::ConstantInt>(startVal))
        {
            if(auto* endConst = llvm::dyn_cast<llvm::ConstantInt>(endVal))
            {
                int64_t startInt = startConst->getSExtValue();
                int64_t endInt = endConst->getSExtValue();
                if(rangeExpr->inclusive)
                    rangeNeverExecutes = startInt > endInt;
                else
                    rangeNeverExecutes = startInt >= endInt;
            }
        }

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

        if(hadOldDepth)
            variableScopeDepth[node->varName] = oldDepth;
        else
            variableScopeDepth.erase(node->varName);

        if(hadOldMoved)
            movedVariables.insert(node->varName);
        else
            clearMovedVariable(node->varName);

        // If the range is provably empty, the loop body does not execute.
        // Preserve ownership state from before loop-body evaluation.
        if(rangeNeverExecutes)
        {
            movedVariables = std::move(stateBeforeLoopMoved);
            pointerBorrowTarget = std::move(stateBeforeLoopPointerBorrowTarget);
            activeBorrowers = std::move(stateBeforeLoopActiveBorrowers);
        }
    }
    else if(auto* listLit = dynamic_cast<ListLiteralNode*>(iterableExpr))
    {
        // Iterating over a list literal: for x in [1, 2, 3] { ... }
        generateForListLiteralIteration(node, listLit);
    }
    else if(auto* arrFill = dynamic_cast<ArrayFillNode*>(iterableExpr))
    {
        // Iterating over [val; N] — materialise as a list literal iteration
        // by generating the fill value into a temporary list first.
        llvm::Value* listVal = generateArrayFill(arrFill);
        if(!listVal)
            return;
        // Store the generated list into a temp alloca so the list-variable
        // iteration path can load it.
        llvm::Type* listType = listVal->getType();
        llvm::AllocaInst* tmpList =
            builder.CreateAlloca(listType, nullptr, "arrtmp");
        builder.CreateStore(listVal, tmpList);

        TypeNode* elemTypeNode =
            inferExpressionTypeNode(arrFill->value, node->line);
        if(!elemTypeNode)
            return;

        std::string tmpName = "__arr_fill_tmp";
        listElementTypes[tmpName] = elemTypeNode;
        namedValues[tmpName] = tmpList;

        auto* synthId = new IdentifierNode(tmpName);
        synthId->line = node->line;
        generateForListVariableIteration(node, synthId);
        delete synthId;
        namedValues.erase(tmpName);
        listElementTypes.erase(tmpName);
        delete elemTypeNode;
    }
    else if(auto* identifier = dynamic_cast<IdentifierNode*>(iterableExpr))
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
        else if(!resolveVisibleEnumName(identifier->name).empty())
        {
            generateForEnumIteration(node,
                                     resolveVisibleEnumName(identifier->name));
        }
        else
        {
            // Iterating over a list variable: for x in myList { ... }
            generateForListVariableIteration(node, identifier);
        }
    }
    else if(auto* mapIter = dynamic_cast<MapIteratorNode*>(iterableExpr))
    {
        // Iterating over map.keys(), map.values(), or map.entries()
        generateForMapIteration(node, mapIter);
    }
    else
    {
        reportError(node->line,
                    "for loops support range expressions (start..end), list "
                    "iteration, enum iteration, and map iteration "
                    "(.keys(), .values(), .entries())");
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

    TypeNode* elemTypeNode =
        inferExpressionTypeNode(listLit->elements->elements.front(),
                                node->line);
    if(!elemTypeNode)
    {
        reportError(node->line, "cannot infer list literal element type");
        return;
    }

    llvm::Type* elementType = getLLVMTypeFromNode(elemTypeNode);
    if(!elementType)
    {
        reportError(node->line,
                    "list literal iteration has unsupported element type");
        delete elemTypeNode;
        return;
    }

    // Generate all list elements first
    std::vector<llvm::Value*> elementValues;

    for(auto* elem : listLit->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
        {
            reportError(node->line, "failed to generate list element");
            delete elemTypeNode;
            return;
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
    auto oldStructIt = structVariableTypes.find(node->varName);
    bool hadOldStructType = oldStructIt != structVariableTypes.end();
    std::string oldStructType =
        hadOldStructType ? oldStructIt->second : std::string();
    auto oldEnumIt = enumVariableTypes.find(node->varName);
    bool hadOldEnumType = oldEnumIt != enumVariableTypes.end();
    std::string oldEnumType =
        hadOldEnumType ? oldEnumIt->second : std::string();
    auto oldListIt = listElementTypes.find(node->varName);
    bool hadOldListType = oldListIt != listElementTypes.end();
    TypeNode* oldListType =
        hadOldListType ? oldListIt->second : nullptr;
    auto oldMapIt = mapKeyValueTypes.find(node->varName);
    bool hadOldMapType = oldMapIt != mapKeyValueTypes.end();
    std::pair<TypeNode*, TypeNode*> oldMapType =
        hadOldMapType ? oldMapIt->second
                      : std::make_pair(nullptr, nullptr);
    auto oldPointerIt = pointerElementTypes.find(node->varName);
    bool hadOldPointerType = oldPointerIt != pointerElementTypes.end();
    TypeNode* oldPointerType =
        hadOldPointerType ? oldPointerIt->second : nullptr;
    auto oldTupleIt = tupleElementTypes.find(node->varName);
    bool hadOldTupleType = oldTupleIt != tupleElementTypes.end();
    std::vector<TypeNode*> oldTupleType =
        hadOldTupleType ? oldTupleIt->second : std::vector<TypeNode*>();
    bool hadOldMoved = isVariableMoved(node->varName);
    auto oldDepthIt = variableScopeDepth.find(node->varName);
    bool hadOldDepth = oldDepthIt != variableScopeDepth.end();
    int oldDepth = hadOldDepth ? oldDepthIt->second : 0;

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = elemTypeNode->kind;
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(elemTypeNode))
    {
        std::string resolvedEnumName =
            resolveVisibleEnumName(structRef->structName);
        if(!resolvedEnumName.empty())
        {
            variableTypes[node->varName] = TypeNode::TYPE_INT;
            enumVariableTypes[node->varName] = resolvedEnumName;
            structVariableTypes.erase(node->varName);
        }
        else
        {
            variableTypes[node->varName] = TypeNode::TYPE_STRUCT;
            structVariableTypes[node->varName] = structRef->structName;
            enumVariableTypes.erase(node->varName);
        }
    }
    else if(auto* genRef =
                dynamic_cast<GenericStructTypeRefNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->varName] = getOrCreateMonomorphizedStruct(
            genRef->structName, genRef->typeArgs);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* listType = dynamic_cast<GenericListTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_LIST;
        listElementTypes[node->varName] = listType->elementType;
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* mapType = dynamic_cast<MapTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_MAP;
        mapKeyValueTypes[node->varName] =
            std::make_pair(mapType->keyType, mapType->valueType);
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* tupleType = dynamic_cast<TupleTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_TUPLE;
        tupleElementTypes[node->varName] = tupleType->elementTypes->types;
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* ptrType = dynamic_cast<PointerTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_PTR;
        pointerElementTypes[node->varName] = ptrType->elementType;
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else
    {
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    clearMovedVariable(node->varName);
    recordVariableScopeDepth(node->varName);

    // Enumerate support: expose index alloca under indexVarName
    bool hasIdxVar = !node->indexVarName.empty();
    llvm::Value* oldIdxVal = nullptr;
    TypeNode::TypeKind oldIdxType = TypeNode::TYPE_VOID;
    bool hadOldIdxType = false;
    bool hadOldIdxDepth = false;
    int oldIdxDepth = 0;
    if(hasIdxVar)
    {
        oldIdxVal = namedValues.count(node->indexVarName)
                        ? namedValues[node->indexVarName]
                        : nullptr;
        hadOldIdxType =
            variableTypes.find(node->indexVarName) != variableTypes.end();
        if(hadOldIdxType)
            oldIdxType = variableTypes[node->indexVarName];
        auto oldIdxDepthIt = variableScopeDepth.find(node->indexVarName);
        hadOldIdxDepth = oldIdxDepthIt != variableScopeDepth.end();
        oldIdxDepth = hadOldIdxDepth ? oldIdxDepthIt->second : 0;
        namedValues[node->indexVarName] = indexVar;
        variableTypes[node->indexVarName] = TypeNode::TYPE_I64;
        recordVariableScopeDepth(node->indexVarName);
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

    // Restore index var if used
    if(hasIdxVar)
    {
        if(oldIdxVal)
            namedValues[node->indexVarName] = oldIdxVal;
        else
            namedValues.erase(node->indexVarName);
        if(hadOldIdxType)
            variableTypes[node->indexVarName] = oldIdxType;
        else
            variableTypes.erase(node->indexVarName);
        if(hadOldIdxDepth)
            variableScopeDepth[node->indexVarName] = oldIdxDepth;
        else
            variableScopeDepth.erase(node->indexVarName);
    }

    // Restore value var
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);

    if(hadOldStructType)
        structVariableTypes[node->varName] = oldStructType;
    else
        structVariableTypes.erase(node->varName);

    if(hadOldEnumType)
        enumVariableTypes[node->varName] = oldEnumType;
    else
        enumVariableTypes.erase(node->varName);

    if(hadOldListType)
        listElementTypes[node->varName] = oldListType;
    else
        listElementTypes.erase(node->varName);

    if(hadOldMapType)
        mapKeyValueTypes[node->varName] = oldMapType;
    else
        mapKeyValueTypes.erase(node->varName);

    if(hadOldPointerType)
        pointerElementTypes[node->varName] = oldPointerType;
    else
        pointerElementTypes.erase(node->varName);

    if(hadOldTupleType)
        tupleElementTypes[node->varName] = oldTupleType;
    else
        tupleElementTypes.erase(node->varName);

    if(hadOldDepth)
        variableScopeDepth[node->varName] = oldDepth;
    else
        variableScopeDepth.erase(node->varName);

    if(hadOldMoved)
        movedVariables.insert(node->varName);
    else
        clearMovedVariable(node->varName);

    delete elemTypeNode;
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
    llvm::Type* elementType = getLLVMTypeFromNode(elemTypeNode);
    if(!elementType)
    {
        reportError(node->line,
                    "list iteration has unsupported element type for '" +
                        listId->name + "'");
        return;
    }

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
    auto oldStructIt = structVariableTypes.find(node->varName);
    bool hadOldStructType = oldStructIt != structVariableTypes.end();
    std::string oldStructType =
        hadOldStructType ? oldStructIt->second : std::string();
    auto oldEnumIt = enumVariableTypes.find(node->varName);
    bool hadOldEnumType = oldEnumIt != enumVariableTypes.end();
    std::string oldEnumType =
        hadOldEnumType ? oldEnumIt->second : std::string();
    auto oldListIt = listElementTypes.find(node->varName);
    bool hadOldListType = oldListIt != listElementTypes.end();
    TypeNode* oldListType =
        hadOldListType ? oldListIt->second : nullptr;
    auto oldMapIt = mapKeyValueTypes.find(node->varName);
    bool hadOldMapType = oldMapIt != mapKeyValueTypes.end();
    std::pair<TypeNode*, TypeNode*> oldMapType =
        hadOldMapType ? oldMapIt->second
                      : std::make_pair(nullptr, nullptr);
    auto oldPointerIt = pointerElementTypes.find(node->varName);
    bool hadOldPointerType = oldPointerIt != pointerElementTypes.end();
    TypeNode* oldPointerType =
        hadOldPointerType ? oldPointerIt->second : nullptr;
    auto oldTupleIt = tupleElementTypes.find(node->varName);
    bool hadOldTupleType = oldTupleIt != tupleElementTypes.end();
    std::vector<TypeNode*> oldTupleType =
        hadOldTupleType ? oldTupleIt->second : std::vector<TypeNode*>();
    bool hadOldMoved = isVariableMoved(node->varName);
    auto oldDepthIt = variableScopeDepth.find(node->varName);
    bool hadOldDepth = oldDepthIt != variableScopeDepth.end();
    int oldDepth = hadOldDepth ? oldDepthIt->second : 0;

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = elemTypeNode->kind;
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(elemTypeNode))
    {
        std::string resolvedEnumName =
            resolveVisibleEnumName(structRef->structName);
        if(!resolvedEnumName.empty())
        {
            variableTypes[node->varName] = TypeNode::TYPE_INT;
            enumVariableTypes[node->varName] = resolvedEnumName;
            structVariableTypes.erase(node->varName);
        }
        else
        {
            variableTypes[node->varName] = TypeNode::TYPE_STRUCT;
            structVariableTypes[node->varName] = structRef->structName;
            enumVariableTypes.erase(node->varName);
        }
    }
    else if(auto* genRef =
                dynamic_cast<GenericStructTypeRefNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->varName] = getOrCreateMonomorphizedStruct(
            genRef->structName, genRef->typeArgs);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* listType = dynamic_cast<GenericListTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_LIST;
        listElementTypes[node->varName] = listType->elementType;
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* mapType = dynamic_cast<MapTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_MAP;
        mapKeyValueTypes[node->varName] =
            std::make_pair(mapType->keyType, mapType->valueType);
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* tupleType = dynamic_cast<TupleTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_TUPLE;
        tupleElementTypes[node->varName] = tupleType->elementTypes->types;
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else if(auto* ptrType = dynamic_cast<PointerTypeNode*>(elemTypeNode))
    {
        variableTypes[node->varName] = TypeNode::TYPE_PTR;
        pointerElementTypes[node->varName] = ptrType->elementType;
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    else
    {
        structVariableTypes.erase(node->varName);
        enumVariableTypes.erase(node->varName);
    }
    clearMovedVariable(node->varName);
    recordVariableScopeDepth(node->varName);

    // If this is an enumerate loop (for (i, x) in ...), expose the index alloca
    // under indexVarName so body code can read it.
    bool hasIdxVar = !node->indexVarName.empty();
    llvm::Value* oldIdxVal = nullptr;
    TypeNode::TypeKind oldIdxType = TypeNode::TYPE_VOID;
    bool hadOldIdxType = false;
    bool hadOldIdxDepth = false;
    int oldIdxDepth = 0;
    if(hasIdxVar)
    {
        oldIdxVal = namedValues.count(node->indexVarName)
                        ? namedValues[node->indexVarName]
                        : nullptr;
        hadOldIdxType =
            variableTypes.find(node->indexVarName) != variableTypes.end();
        if(hadOldIdxType)
            oldIdxType = variableTypes[node->indexVarName];
        auto oldIdxDepthIt = variableScopeDepth.find(node->indexVarName);
        hadOldIdxDepth = oldIdxDepthIt != variableScopeDepth.end();
        oldIdxDepth = hadOldIdxDepth ? oldIdxDepthIt->second : 0;
        namedValues[node->indexVarName] = indexVar;
        variableTypes[node->indexVarName] = TypeNode::TYPE_I64;
        recordVariableScopeDepth(node->indexVarName);
    }

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

    // Restore index var if used
    if(hasIdxVar)
    {
        if(oldIdxVal)
            namedValues[node->indexVarName] = oldIdxVal;
        else
            namedValues.erase(node->indexVarName);
        if(hadOldIdxType)
            variableTypes[node->indexVarName] = oldIdxType;
        else
            variableTypes.erase(node->indexVarName);
        if(hadOldIdxDepth)
            variableScopeDepth[node->indexVarName] = oldIdxDepth;
        else
            variableScopeDepth.erase(node->indexVarName);
    }

    // Restore value var
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);

    if(hadOldStructType)
        structVariableTypes[node->varName] = oldStructType;
    else
        structVariableTypes.erase(node->varName);

    if(hadOldEnumType)
        enumVariableTypes[node->varName] = oldEnumType;
    else
        enumVariableTypes.erase(node->varName);

    if(hadOldListType)
        listElementTypes[node->varName] = oldListType;
    else
        listElementTypes.erase(node->varName);

    if(hadOldMapType)
        mapKeyValueTypes[node->varName] = oldMapType;
    else
        mapKeyValueTypes.erase(node->varName);

    if(hadOldPointerType)
        pointerElementTypes[node->varName] = oldPointerType;
    else
        pointerElementTypes.erase(node->varName);

    if(hadOldTupleType)
        tupleElementTypes[node->varName] = oldTupleType;
    else
        tupleElementTypes.erase(node->varName);

    if(hadOldDepth)
        variableScopeDepth[node->varName] = oldDepth;
    else
        variableScopeDepth.erase(node->varName);

    if(hadOldMoved)
        movedVariables.insert(node->varName);
    else
        clearMovedVariable(node->varName);
}

void CodeGenerator::generateForEnumIteration(ForNode* node,
                                             const std::string& enumName)
{
    auto orderIt = enumVariantOrder.find(enumName);
    auto strOrderIt = enumStringVariantOrder.find(enumName);
    bool hasIntOrder =
        orderIt != enumVariantOrder.end() && !orderIt->second.empty();
    bool hasStrOrder = strOrderIt != enumStringVariantOrder.end() &&
                       !strOrderIt->second.empty();
    if(!hasIntOrder && !hasStrOrder)
        return;

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::Type* indexType = llvm::Type::getInt64Ty(context);

    TypeNode::TypeKind baseKind = TypeNode::TYPE_I32;
    auto baseIt = enumBaseTypes.find(enumName);
    if(baseIt != enumBaseTypes.end())
        baseKind = baseIt->second;
    llvm::Type* enumTy = getLLVMType(baseKind);

    llvm::AllocaInst* indexVar =
        builder.CreateAlloca(indexType, nullptr, "enum.idx");
    builder.CreateStore(llvm::ConstantInt::get(indexType, 0), indexVar);

    llvm::AllocaInst* loopVar =
        builder.CreateAlloca(enumTy, nullptr, node->varName);

    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = variableTypes.find(node->varName) != variableTypes.end();
    if(hadOldType)
        oldType = variableTypes[node->varName];
    bool hadOldMoved = isVariableMoved(node->varName);
    auto oldDepthIt = variableScopeDepth.find(node->varName);
    bool hadOldDepth = oldDepthIt != variableScopeDepth.end();
    int oldDepth = hadOldDepth ? oldDepthIt->second : 0;
    auto oldEnumTypeIt = enumVariableTypes.find(node->varName);
    bool hadOldEnumType = oldEnumTypeIt != enumVariableTypes.end();
    std::string oldEnumType = hadOldEnumType ? oldEnumTypeIt->second : "";

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = baseKind;
    enumVariableTypes[node->varName] = enumName;
    clearMovedVariable(node->varName);
    recordVariableScopeDepth(node->varName);

    bool hasIdxVar = !node->indexVarName.empty();
    llvm::Value* oldIdxVal = nullptr;
    TypeNode::TypeKind oldIdxType = TypeNode::TYPE_VOID;
    bool hadOldIdxType = false;
    bool hadOldIdxDepth = false;
    int oldIdxDepth = 0;
    if(hasIdxVar)
    {
        oldIdxVal = namedValues.count(node->indexVarName)
                        ? namedValues[node->indexVarName]
                        : nullptr;
        hadOldIdxType =
            variableTypes.find(node->indexVarName) != variableTypes.end();
        if(hadOldIdxType)
            oldIdxType = variableTypes[node->indexVarName];
        auto oldIdxDepthIt = variableScopeDepth.find(node->indexVarName);
        hadOldIdxDepth = oldIdxDepthIt != variableScopeDepth.end();
        oldIdxDepth = hadOldIdxDepth ? oldIdxDepthIt->second : 0;
        namedValues[node->indexVarName] = indexVar;
        variableTypes[node->indexVarName] = TypeNode::TYPE_I64;
        recordVariableScopeDepth(node->indexVarName);
    }

    const int64_t variantCount = static_cast<int64_t>(
        hasIntOrder ? orderIt->second.size() : strOrderIt->second.size());

    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(incBB);

    builder.CreateBr(condBB);

    builder.SetInsertPoint(condBB);
    llvm::Value* currentIdx = builder.CreateLoad(indexType, indexVar, "idx");
    llvm::Value* cond = builder.CreateICmpSLT(
        currentIdx, llvm::ConstantInt::get(indexType, variantCount),
        "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    if(Helpers::isEnumStringType(baseKind))
    {
        llvm::Value* enumVal = nullptr;
        for(size_t i = 0; i < strOrderIt->second.size(); ++i)
        {
            llvm::Value* idxConst = llvm::ConstantInt::get(indexType, i);
            llvm::Value* isThis =
                builder.CreateICmpEQ(currentIdx, idxConst, "iseq");
#if LLVM_VERSION_MAJOR >= 21
            llvm::Value* variantConst =
                builder.CreateGlobalString(strOrderIt->second[i].second);
#else
            llvm::Value* variantConst =
                builder.CreateGlobalStringPtr(strOrderIt->second[i].second);
#endif
            enumVal = enumVal ? builder.CreateSelect(isThis, variantConst,
                                                     enumVal, "selectenumstr")
                              : variantConst;
        }
        builder.CreateStore(enumVal, loopVar);
    }
    else
    {
        llvm::Value* enumVal = llvm::ConstantInt::get(
            enumTy, orderIt->second.front().second, !Helpers::enumIsUnsigned(baseKind));
        for(size_t i = 0; i < orderIt->second.size(); ++i)
        {
            llvm::Value* idxConst = llvm::ConstantInt::get(indexType, i);
            llvm::Value* isThis =
                builder.CreateICmpEQ(currentIdx, idxConst, "iseq");
            llvm::Value* variantConst = llvm::ConstantInt::get(
                enumTy, orderIt->second[i].second, !Helpers::enumIsUnsigned(baseKind));
            enumVal = builder.CreateSelect(isThis, variantConst, enumVal,
                                           "selectenum");
        }
        builder.CreateStore(enumVal, loopVar);
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
        builder.CreateBr(incBB);

    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextIdx =
        builder.CreateAdd(builder.CreateLoad(indexType, indexVar, ""),
                          llvm::ConstantInt::get(indexType, 1), "nextidx");
    builder.CreateStore(nextIdx, indexVar);
    builder.CreateBr(condBB);

    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    if(hasIdxVar)
    {
        if(oldIdxVal)
            namedValues[node->indexVarName] = oldIdxVal;
        else
            namedValues.erase(node->indexVarName);
        if(hadOldIdxType)
            variableTypes[node->indexVarName] = oldIdxType;
        else
            variableTypes.erase(node->indexVarName);
        if(hadOldIdxDepth)
            variableScopeDepth[node->indexVarName] = oldIdxDepth;
        else
            variableScopeDepth.erase(node->indexVarName);
    }

    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);

    if(hadOldDepth)
        variableScopeDepth[node->varName] = oldDepth;
    else
        variableScopeDepth.erase(node->varName);

    if(hadOldEnumType)
        enumVariableTypes[node->varName] = oldEnumType;
    else
        enumVariableTypes.erase(node->varName);

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
    auto oldDepthIt = variableScopeDepth.find(node->varName);
    bool hadOldDepth = oldDepthIt != variableScopeDepth.end();
    int oldDepth = hadOldDepth ? oldDepthIt->second : 0;

    switch(mapIter->kind)
    {
    case MapIteratorNode::ITER_KEYS:
        loopVarType = keyType;
        loopVar = builder.CreateAlloca(keyType, nullptr, node->varName);
        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = keyTypeNode->kind;
        clearMovedVariable(node->varName);
        recordVariableScopeDepth(node->varName);
        break;

    case MapIteratorNode::ITER_VALUES:
        loopVarType = valueType;
        loopVar = builder.CreateAlloca(valueType, nullptr, node->varName);
        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = valTypeNode->kind;
        clearMovedVariable(node->varName);
        recordVariableScopeDepth(node->varName);
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
            recordVariableScopeDepth(node->varName);

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

    if(hadOldDepth)
        variableScopeDepth[node->varName] = oldDepth;
    else
        variableScopeDepth.erase(node->varName);

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
