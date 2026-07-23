#include "ir.h"

namespace
{

static bool containsUnsupportedTryControlFlow(StatementNode* node)
{
    if(!node)
        return false;
    if(dynamic_cast<ReturnNode*>(node) || dynamic_cast<BreakNode*>(node) ||
       dynamic_cast<ContinueNode*>(node))
        return true;
    if(auto* block = dynamic_cast<BlockStatementNode*>(node))
    {
        if(!block->statements)
            return false;
        for(auto* stmt : block->statements->statements)
        {
            if(containsUnsupportedTryControlFlow(stmt))
                return true;
        }
        return false;
    }
    if(auto* ifNode = dynamic_cast<IfNode*>(node))
    {
        if(containsUnsupportedTryControlFlow(ifNode->conditionInit))
            return true;
        if(ifNode->thenBranch)
        {
            for(auto* stmt : ifNode->thenBranch->statements)
            {
                if(containsUnsupportedTryControlFlow(stmt))
                    return true;
            }
        }
        if(containsUnsupportedTryControlFlow(ifNode->elseIfBranch))
            return true;
        if(ifNode->elseBranch)
        {
            for(auto* stmt : ifNode->elseBranch->statements)
            {
                if(containsUnsupportedTryControlFlow(stmt))
                    return true;
            }
        }
        return false;
    }
    if(auto* forNode = dynamic_cast<ForNode*>(node))
    {
        if(!forNode->body)
            return false;
        for(auto* stmt : forNode->body->statements)
        {
            if(containsUnsupportedTryControlFlow(stmt))
                return true;
        }
        return false;
    }
    if(auto* whileNode = dynamic_cast<WhileNode*>(node))
    {
        if(!whileNode->body)
            return false;
        for(auto* stmt : whileNode->body->statements)
        {
            if(containsUnsupportedTryControlFlow(stmt))
                return true;
        }
        return false;
    }
    if(auto* tc = dynamic_cast<TryCatchNode*>(node))
    {
        return containsUnsupportedTryControlFlow(tc->tryBlock) ||
               containsUnsupportedTryControlFlow(tc->catchBlock);
    }
    return false;
}

} // namespace

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

    if(containsUnsupportedTryControlFlow(node->tryBlock))
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
