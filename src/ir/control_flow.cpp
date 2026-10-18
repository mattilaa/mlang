#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"
#include "llvm_compat.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/MDBuilder.h>

#include <cstdint>

using mlang::ir_detail::ast_analysis::contains_unsupported_try_control_flow;
using mlang::ir_detail::ast_analysis::collect_used_idents;
using mlang::ir_detail::ast_analysis::strip_iter_methods;
using mlang::ir_detail::common::Helpers;

namespace
{
class MutableCallableTargetsAcrossLoop
{
public:
    MutableCallableTargetsAcrossLoop(
        std::map<std::string, std::set<std::string>>& targets,
        const std::set<std::string>& constants)
        : targets_(targets), constants_(constants)
    {
        eraseMutableTargets();
    }

    ~MutableCallableTargetsAcrossLoop()
    {
        eraseMutableTargets();
    }

private:
    void eraseMutableTargets()
    {
        for(auto target = targets_.begin(); target != targets_.end();)
        {
            if(!constants_.count(target->first))
                target = targets_.erase(target);
            else
                ++target;
        }
    }

    std::map<std::string, std::set<std::string>>& targets_;
    const std::set<std::string>& constants_;
};
} // namespace

void CodeGenerator::expireUnusedLocalBorrows(
    const std::vector<StatementNode*>& statements, size_t statementIndex,
    int scopeDepth)
{
    std::set<std::string> futureIdents;
    for(size_t index = statementIndex + 1; index < statements.size(); ++index)
        collect_used_idents(statements[index], futureIdents);

    std::set<std::string> capturedPointersInLiveClosures;
    for(const auto& closure : runtimeClosureBorrowOwners)
    {
        if(!futureIdents.count(closure.first))
            continue;
        for(const auto& capture : closure.second)
            if(pointerBorrowTarget.find(capture) != pointerBorrowTarget.end() ||
               aggregateBorrowOrigins.find(capture) !=
                   aggregateBorrowOrigins.end())
                capturedPointersInLiveClosures.insert(capture);
    }

    std::vector<std::string> toClear;
    for(const auto& borrow : pointerBorrowTarget)
    {
        auto depth = variableScopeDepth.find(borrow.first);
        if(depth != variableScopeDepth.end() && depth->second == scopeDepth &&
           !futureIdents.count(borrow.first) &&
           !capturedPointersInLiveClosures.count(borrow.first))
            toClear.push_back(borrow.first);
    }
    for(const auto& pointer : toClear)
        clearPointerBorrow(pointer);

    std::vector<std::string> aggregatesToClear;
    for(const auto& aggregate : aggregateBorrowOrigins)
    {
        auto depth = variableScopeDepth.find(aggregate.first);
        if(depth != variableScopeDepth.end() && depth->second == scopeDepth &&
           !futureIdents.count(aggregate.first) &&
           !capturedPointersInLiveClosures.count(aggregate.first))
            aggregatesToClear.push_back(aggregate.first);
    }
    for(const auto& aggregate : aggregatesToClear)
        clearAggregateBorrowOrigins(aggregate);

    std::vector<std::string> closuresToRelease;
    for(const auto& closure : runtimeClosureBorrowOwners)
    {
        auto depth = variableScopeDepth.find(closure.first);
        if(depth != variableScopeDepth.end() && depth->second == scopeDepth &&
           !futureIdents.count(closure.first))
            closuresToRelease.push_back(closure.first);
    }
    for(const auto& closure : closuresToRelease)
        releaseRuntimeClosureBorrows(closure);
}

void CodeGenerator::generateReturnStatement(ReturnNode* node)
{
    if(!inlineClosureReturnStates.empty())
    {
        InlineClosureReturnState* state = inlineClosureReturnStates.back();
        llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
        llvm::Function* currentFunction = currentBlock->getParent();
        if(node->expression)
        {
            llvm::Value* value = generateExpression(node->expression);
            if(!value)
                return;
            if(!state->resultStorage)
            {
                state->resultStorage = createEntryBlockAlloca(
                    currentFunction, value->getType(), "lambda.return");
            }
            else if(value->getType() !=
                    state->resultStorage->getAllocatedType())
            {
                llvm::Type* expected =
                    state->resultStorage->getAllocatedType();
                if(value->getType()->isIntegerTy() && expected->isIntegerTy())
                    value = builder.CreateIntCast(value, expected, true,
                                                  "lambda.return.cast");
                else if(value->getType()->isFloatingPointTy() &&
                        expected->isFloatingPointTy())
                    value = builder.CreateFPCast(value, expected,
                                                "lambda.return.cast");
                else if(value->getType()->isPointerTy() &&
                        expected->isPointerTy())
                    value = builder.CreateBitCast(value, expected,
                                                 "lambda.return.cast");
                else
                {
                    reportError(node->line,
                                "inconsistent return types in inline closure");
                    return;
                }
            }
            builder.CreateStore(value, state->resultStorage);
        }
        builder.CreateBr(state->exitBlock);
        return;
    }

    llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
    llvm::Type* expectedRetType = currentFunc->getReturnType();

    if(node->expression)
    {
        if(!validateBorrowRead(node->expression, node->line))
            return;
        if(!validateNoEscapingBorrow(node->expression, node->line, "return"))
            return;

        llvm::Value* returnValue = nullptr;
        if(dynamic_cast<FunctionTypeNode*>(currentSemanticReturnType))
        {
            if(auto* id = dynamic_cast<IdentifierNode*>(node->expression))
            {
                auto functionType = functionTypeVariables.find(id->name);
                auto value = namedValues.find(id->name);
                auto* slot = value == namedValues.end()
                                 ? nullptr
                                 : llvm::dyn_cast<llvm::AllocaInst>(
                                       value->second);
                if(functionType != functionTypeVariables.end() && slot)
                    returnValue = builder.CreateLoad(
                        slot->getAllocatedType(), slot,
                        id->name + ".closure.return");
            }
        }
        if(!returnValue)
        {
            if(auto* traitObj = dynamic_cast<TraitObjectTypeNode*>(
                   currentSemanticReturnType))
            {
                llvm::Type* traitObjType = getLLVMTypeFromNode(traitObj);
                TypeNode* exprType =
                    getLValueType(node->expression, node->line);
                if(dynamic_cast<TraitObjectTypeNode*>(exprType))
                {
                    returnValue = generateExpression(node->expression);
                    returnValue = coerceTraitObjectValue(
                        returnValue, traitObjType, node->line);
                }
                else if(exprType)
                    returnValue = buildTraitObjectValue(
                        node->expression, traitObj->traitName, node->line,
                        true);
                else
                {
                    returnValue = generateExpression(node->expression);
                    returnValue = coerceTraitObjectValue(
                        returnValue, traitObjType, node->line);
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
                else if(auto* returnMap = dynamic_cast<MapTypeNode*>(
                            currentSemanticReturnType))
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
                    returnValue = generateExpression(
                        node->expression, currentSemanticReturnType);
            }
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
        emitInoutWriteBack();
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
        emitInoutWriteBack();
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
    LoopExitBorrowState state;
    state.variableScopeDepth = variableScopeDepth;
    state.movedVariables = movedVariables;
    state.pointerBorrowTarget = pointerBorrowTarget;
    state.pointerBorrowParent = pointerBorrowParent;
    state.aggregateBorrowOrigins = aggregateBorrowOrigins;
    state.aggregateBorrowPlaces = aggregateBorrowPlaces;
    state.aggregateBorrowFieldPlaces = aggregateBorrowFieldPlaces;
    state.pointerBorrowPlace = pointerBorrowPlace;
    state.activeBorrowers = activeBorrowers;
    state.activeMutBorrower = activeMutBorrower;
    state.runtimeClosureBorrowOwners = runtimeClosureBorrowOwners;
    if(!loopBreakBorrowStates.empty())
        loopBreakBorrowStates.back().push_back(std::move(state));
    builder.CreateBr(loopBreakBlocks.back());
}

void CodeGenerator::mergeLoopExitBorrowStates(
    const std::vector<LoopExitBorrowState>& states, int loopScopeDepth,
    const std::set<std::string>& loopVariables)
{
    if(states.empty())
        return;

    auto survivesLoopScope = [&](const LoopExitBorrowState& state,
                                 const std::string& name) {
        if(loopVariables.count(name))
            return false;
        auto depth = state.variableScopeDepth.find(name);
        return depth == state.variableScopeDepth.end() ||
               depth->second <= loopScopeDepth;
    };
    for(const auto& state : states)
    {
        for(const auto& moved : state.movedVariables)
            if(survivesLoopScope(state, moved))
                movedVariables.insert(moved);
        for(const auto& borrow : state.pointerBorrowTarget)
            if(survivesLoopScope(state, borrow.first))
                pointerBorrowTarget.emplace(borrow.first, borrow.second);
        for(const auto& aggregate : state.aggregateBorrowOrigins)
            if(survivesLoopScope(state, aggregate.first))
                for(const auto& owner : aggregate.second)
                    if(survivesLoopScope(state, owner))
                        aggregateBorrowOrigins[aggregate.first].insert(owner);
        for(const auto& owner : state.activeBorrowers)
            if(survivesLoopScope(state, owner.first))
                for(const auto& borrower : owner.second)
                    if(survivesLoopScope(state, borrower))
                        activeBorrowers[owner.first].insert(borrower);
        for(const auto& borrow : state.activeMutBorrower)
            if(survivesLoopScope(state, borrow.second))
                activeMutBorrower.emplace(borrow.first, borrow.second);
        for(const auto& closure : state.runtimeClosureBorrowOwners)
            if(survivesLoopScope(state, closure.first))
                for(const auto& owner : closure.second)
                    if(survivesLoopScope(state, owner))
                        runtimeClosureBorrowOwners[closure.first].insert(owner);
    }

    for(auto parent = pointerBorrowParent.begin();
        parent != pointerBorrowParent.end();)
    {
        const bool common = std::all_of(
            states.begin(), states.end(), [&](const auto& state) {
                auto branchParent = state.pointerBorrowParent.find(parent->first);
                return branchParent != state.pointerBorrowParent.end() &&
                       branchParent->second == parent->second;
            });
        if(!common)
            parent = pointerBorrowParent.erase(parent);
        else
            ++parent;
    }
    for(auto place = pointerBorrowPlace.begin();
        place != pointerBorrowPlace.end();)
    {
        const bool common = std::all_of(
            states.begin(), states.end(), [&](const auto& state) {
                auto branchPlace = state.pointerBorrowPlace.find(place->first);
                return branchPlace != state.pointerBorrowPlace.end() &&
                       branchPlace->second == place->second;
            });
        if(!common)
            place = pointerBorrowPlace.erase(place);
        else
            ++place;
    }
    // Keep precise paths for pointers that remain live across the exit join.
    // Aggregate values are still made conservative because their contents may
    // have changed on a different break path.
    for(auto places = aggregateBorrowPlaces.begin();
        places != aggregateBorrowPlaces.end();)
    {
        bool livePointerBorrow = false;
        if(pointerBorrowTarget.count(places->first))
        {
            livePointerBorrow = isMutBorrower(places->first);
            if(!livePointerBorrow)
                for(const auto& owner : activeBorrowers)
                    if(owner.second.count(places->first))
                    {
                        livePointerBorrow = true;
                        break;
                    }
        }
        if(!livePointerBorrow)
            places = aggregateBorrowPlaces.erase(places);
        else
            ++places;
    }
    aggregateBorrowFieldPlaces.clear();
}

void CodeGenerator::generateContinueStatement(ContinueNode* node)
{
    if(loopContinueBlocks.empty())
    {
        reportError(node->line, "'continue' statement not within a loop");
        return;
    }
    LoopExitBorrowState state;
    state.variableScopeDepth = variableScopeDepth;
    state.movedVariables = movedVariables;
    state.pointerBorrowTarget = pointerBorrowTarget;
    state.pointerBorrowParent = pointerBorrowParent;
    state.aggregateBorrowOrigins = aggregateBorrowOrigins;
    state.aggregateBorrowPlaces = aggregateBorrowPlaces;
    state.aggregateBorrowFieldPlaces = aggregateBorrowFieldPlaces;
    state.pointerBorrowPlace = pointerBorrowPlace;
    state.activeBorrowers = activeBorrowers;
    state.activeMutBorrower = activeMutBorrower;
    state.runtimeClosureBorrowOwners = runtimeClosureBorrowOwners;
    if(!loopContinueBorrowStates.empty())
        loopContinueBorrowStates.back().push_back(std::move(state));
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
            if(mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
    auto incomingFunctionItemVariables = functionItemVariables;
    auto incomingPointerBorrowTarget = pointerBorrowTarget;
    auto incomingPointerBorrowParent = pointerBorrowParent;
    auto incomingAggregateBorrowOrigins = aggregateBorrowOrigins;
    auto incomingAggregateBorrowPlaces = aggregateBorrowPlaces;
    auto incomingAggregateBorrowFieldPlaces = aggregateBorrowFieldPlaces;
    auto incomingPointerBorrowPlace = pointerBorrowPlace;
    auto incomingActiveBorrowers = activeBorrowers;
    auto incomingActiveMutBorrower = activeMutBorrower;
    auto incomingRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;
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
        functionItemVariables = incomingFunctionItemVariables;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        pointerBorrowParent = incomingPointerBorrowParent;
        aggregateBorrowOrigins = incomingAggregateBorrowOrigins;
        aggregateBorrowPlaces = incomingAggregateBorrowPlaces;
        aggregateBorrowFieldPlaces = incomingAggregateBorrowFieldPlaces;
        pointerBorrowPlace = incomingPointerBorrowPlace;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;
        runtimeClosureBorrowOwners = incomingRuntimeClosureBorrowOwners;
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
    auto condFunctionItemVariables = functionItemVariables;
    auto condAggregateBorrowOrigins = aggregateBorrowOrigins;
    auto condAggregateBorrowPlaces = aggregateBorrowPlaces;
    auto condAggregateBorrowFieldPlaces = aggregateBorrowFieldPlaces;
    auto condRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;

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

    llvm::BranchInst* branch = builder.CreateCondBr(condValue, thenBB, elseBB);
    if(node->branchPrediction != BranchPrediction::None)
    {
        constexpr uint32_t preferredWeight = 2000;
        constexpr uint32_t otherWeight = 1;
        llvm::MDBuilder metadata(context);
        const bool trueIsLikely =
            node->branchPrediction == BranchPrediction::Likely;
        branch->setMetadata(
            llvm::LLVMContext::MD_prof,
            metadata.createBranchWeights(
                trueIsLikely ? preferredWeight : otherWeight,
                trueIsLikely ? otherWeight : preferredWeight));
    }

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
    functionItemVariables = condFunctionItemVariables;
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    pointerBorrowParent = incomingPointerBorrowParent;
    aggregateBorrowOrigins = condAggregateBorrowOrigins;
    aggregateBorrowPlaces = condAggregateBorrowPlaces;
    aggregateBorrowFieldPlaces = condAggregateBorrowFieldPlaces;
    runtimeClosureBorrowOwners = condRuntimeClosureBorrowOwners;
    pointerBorrowPlace = incomingPointerBorrowPlace;
    activeBorrowers = incomingActiveBorrowers;
    activeMutBorrower = incomingActiveMutBorrower;
    enterCleanupScope();
    for(auto stmt : node->thenBranch->statements)
    {
        generateStatement(stmt);
    }
    exitCleanupScope();
    auto thenMoved = movedVariables;
    auto thenFunctionItemVariables = functionItemVariables;
    auto thenPointerBorrowTarget = pointerBorrowTarget;
    auto thenPointerBorrowParent = pointerBorrowParent;
    auto thenAggregateBorrowOrigins = aggregateBorrowOrigins;
    auto thenAggregateBorrowPlaces = aggregateBorrowPlaces;
    auto thenPointerBorrowPlace = pointerBorrowPlace;
    auto thenActiveBorrowers = activeBorrowers;
    auto thenActiveMutBorrower = activeMutBorrower;
    auto thenRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;
    llvm::BasicBlock* thenEnd = builder.GetInsertBlock();
    bool thenFallsThrough = thenEnd && !mlang::llvm_compat::terminatorOrNull(thenEnd);

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
    functionItemVariables = condFunctionItemVariables;
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    pointerBorrowParent = incomingPointerBorrowParent;
    aggregateBorrowOrigins = condAggregateBorrowOrigins;
    aggregateBorrowPlaces = condAggregateBorrowPlaces;
    aggregateBorrowFieldPlaces = condAggregateBorrowFieldPlaces;
    runtimeClosureBorrowOwners = condRuntimeClosureBorrowOwners;
    pointerBorrowPlace = incomingPointerBorrowPlace;
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
    auto elseFunctionItemVariables = functionItemVariables;
    auto elsePointerBorrowTarget = pointerBorrowTarget;
    auto elsePointerBorrowParent = pointerBorrowParent;
    auto elseAggregateBorrowOrigins = aggregateBorrowOrigins;
    auto elseAggregateBorrowPlaces = aggregateBorrowPlaces;
    auto elsePointerBorrowPlace = pointerBorrowPlace;
    auto elseActiveBorrowers = activeBorrowers;
    auto elseActiveMutBorrower = activeMutBorrower;
    auto elseRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;
    llvm::BasicBlock* elseEnd = builder.GetInsertBlock();
    bool elseFallsThrough = elseEnd && !mlang::llvm_compat::terminatorOrNull(elseEnd);

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

    if(thenFallsThrough && elseFallsThrough)
    {
        functionItemVariables.clear();
        for(const auto& target : thenFunctionItemVariables)
        {
            auto other = elseFunctionItemVariables.find(target.first);
            if(other != elseFunctionItemVariables.end())
            {
                std::set<std::string> possibleTargets = target.second;
                possibleTargets.insert(other->second.begin(),
                                       other->second.end());
                functionItemVariables.emplace(target.first,
                                              std::move(possibleTargets));
            }
        }
    }
    else if(thenFallsThrough)
        functionItemVariables = std::move(thenFunctionItemVariables);
    else if(elseFallsThrough)
        functionItemVariables = std::move(elseFunctionItemVariables);
    else
        functionItemVariables = incomingFunctionItemVariables;

    if(thenFallsThrough || elseFallsThrough)
    {
        std::set<std::string> mergedMoved;
        std::map<std::string, std::string> mergedPointerBorrowTarget;
        std::map<std::string, std::string> mergedPointerBorrowParent;
        std::map<std::string, std::set<std::string>>
            mergedAggregateBorrowOrigins;
        std::map<std::string, std::string> mergedPointerBorrowPlace;
        std::map<std::string, std::set<std::string>> mergedActiveBorrowers;

        auto mergeOne = [&](const std::set<std::string>& movedState,
                            const std::map<std::string, std::string>& ptrState,
                            const std::map<std::string, std::set<std::string>>&
                                aggregateState,
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
            for(const auto& kv : aggregateState)
            {
                auto& dst = mergedAggregateBorrowOrigins[kv.first];
                dst.insert(kv.second.begin(), kv.second.end());
            }
            for(const auto& kv : borrowersState)
            {
                auto& dst = mergedActiveBorrowers[kv.first];
                dst.insert(kv.second.begin(), kv.second.end());
            }
        };

        if(thenFallsThrough)
            mergeOne(thenMoved, thenPointerBorrowTarget,
                     thenAggregateBorrowOrigins, thenActiveBorrowers);
        if(elseFallsThrough)
            mergeOne(elseMoved, elsePointerBorrowTarget,
                     elseAggregateBorrowOrigins, elseActiveBorrowers);

        const auto& firstParentState =
            thenFallsThrough ? thenPointerBorrowParent : elsePointerBorrowParent;
        mergedPointerBorrowParent = firstParentState;
        if(thenFallsThrough && elseFallsThrough)
        {
            for(auto it = mergedPointerBorrowParent.begin();
                it != mergedPointerBorrowParent.end();)
            {
                auto other = elsePointerBorrowParent.find(it->first);
                if(other == elsePointerBorrowParent.end() ||
                   other->second != it->second)
                    it = mergedPointerBorrowParent.erase(it);
                else
                    ++it;
            }
        }

        movedVariables = std::move(mergedMoved);
        pointerBorrowTarget = std::move(mergedPointerBorrowTarget);
        pointerBorrowParent = std::move(mergedPointerBorrowParent);
        aggregateBorrowOrigins = std::move(mergedAggregateBorrowOrigins);
        aggregateBorrowPlaces.clear();
        aggregateBorrowFieldPlaces.clear();
        auto mergeAggregatePlaces = [&](
            const auto& branchOrigins, const auto& branchPlaces) {
            for(const auto& aggregate : mergedAggregateBorrowOrigins)
            {
                auto branchAggregate = branchOrigins.find(aggregate.first);
                if(branchAggregate == branchOrigins.end())
                    continue;
                for(const auto& owner : aggregate.second)
                {
                    if(branchAggregate->second.count(owner) == 0)
                        continue;
                    auto places = branchPlaces.find(aggregate.first);
                    if(places == branchPlaces.end())
                        continue;
                    auto ownerPlaces = places->second.find(owner);
                    if(ownerPlaces == places->second.end() ||
                       ownerPlaces->second.empty())
                        continue;
                    aggregateBorrowPlaces[aggregate.first][owner].insert(
                        ownerPlaces->second.begin(),
                        ownerPlaces->second.end());
                }
            }
        };
        if(thenFallsThrough)
            mergeAggregatePlaces(thenAggregateBorrowOrigins,
                                 thenAggregateBorrowPlaces);
        if(elseFallsThrough)
            mergeAggregatePlaces(elseAggregateBorrowOrigins,
                                 elseAggregateBorrowPlaces);
        // Any branch with an origin but no precise place makes that owner's
        // merged borrow conservative. (The per-owner erase above may be
        // superseded by a later branch, so explicitly remove unknown owners.)
        for(const auto& aggregate : mergedAggregateBorrowOrigins)
            for(const auto& owner : aggregate.second)
            {
                bool hasUnknownPath = false;
                auto checkBranch = [&](const auto& branchOrigins,
                                       const auto& branchPlaces) {
                    auto branchAggregate =
                        branchOrigins.find(aggregate.first);
                    if(branchAggregate == branchOrigins.end() ||
                       branchAggregate->second.count(owner) == 0)
                        return;
                    auto places = branchPlaces.find(aggregate.first);
                    if(places == branchPlaces.end())
                    {
                        hasUnknownPath = true;
                        return;
                    }
                    auto ownerPlaces = places->second.find(owner);
                    if(ownerPlaces == places->second.end() ||
                       ownerPlaces->second.empty())
                        hasUnknownPath = true;
                };
                if(thenFallsThrough)
                    checkBranch(thenAggregateBorrowOrigins,
                                thenAggregateBorrowPlaces);
                if(elseFallsThrough)
                    checkBranch(elseAggregateBorrowOrigins,
                                elseAggregateBorrowPlaces);
                if(hasUnknownPath)
                    aggregateBorrowPlaces[aggregate.first].erase(owner);
            }
        for(const auto& borrow : pointerBorrowTarget)
        {
            std::string commonPlace;
            bool hasCommonPlace = true;
            const auto mergePlaceState = [&](const auto& placeState) {
                auto place = placeState.find(borrow.first);
                if(place == placeState.end())
                {
                    hasCommonPlace = false;
                    return;
                }
                if(commonPlace.empty())
                    commonPlace = place->second;
                else if(commonPlace != place->second)
                    hasCommonPlace = false;
            };
            if(thenFallsThrough)
                mergePlaceState(thenPointerBorrowPlace);
            if(elseFallsThrough)
                mergePlaceState(elsePointerBorrowPlace);
            if(hasCommonPlace && !commonPlace.empty())
                mergedPointerBorrowPlace.emplace(borrow.first, commonPlace);
        }
        pointerBorrowPlace = std::move(mergedPointerBorrowPlace);

        // Retain owner-specific paths for mutable pointers whose targets
        // differ across the branches. The single pointerBorrowTarget entry is
        // only a representative; without these paths, checks either miss a
        // possible owner or must conservatively block its whole object.
        auto mergeJoinedPointerPlaces = [&](const auto& branchTargets,
                                            const auto& branchPlaces,
                                            const auto& branchMutBorrowers) {
            for(const auto& borrow : branchMutBorrowers)
            {
                const std::string& pointer = borrow.second;
                auto target = branchTargets.find(pointer);
                if(target == branchTargets.end())
                    continue;
                const std::string& owner = target->second;
                auto place = branchPlaces.find(pointer);
                std::string ownerPlace = owner;
                if(place != branchPlaces.end() &&
                   place->second.compare(0, owner.size(), owner) == 0)
                {
                    ownerPlace = owner + place->second.substr(owner.size());
                }
                aggregateBorrowPlaces[pointer][owner].insert(ownerPlace);
            }
        };
        if(thenFallsThrough)
            mergeJoinedPointerPlaces(thenPointerBorrowTarget,
                                     thenPointerBorrowPlace,
                                     thenActiveMutBorrower);
        if(elseFallsThrough)
            mergeJoinedPointerPlaces(elsePointerBorrowTarget,
                                     elsePointerBorrowPlace,
                                     elseActiveMutBorrower);

        // Shared pointers also need an owner-specific place when their
        // target differs by branch. A representative target alone loses the
        // other branch during a later nested reborrow.
        auto mergeJoinedSharedPointerPlaces = [&](const auto& branchTargets,
                                                  const auto& branchPlaces,
                                                  const auto& branchBorrowers) {
            for(const auto& ownerBorrowers : branchBorrowers)
                for(const auto& pointer : ownerBorrowers.second)
                {
                    auto target = branchTargets.find(pointer);
                    if(target == branchTargets.end() ||
                       target->second != ownerBorrowers.first)
                        continue;
                    auto place = branchPlaces.find(pointer);
                    std::string ownerPlace = ownerBorrowers.first;
                    if(place != branchPlaces.end() &&
                       place->second.compare(0, ownerPlace.size(),
                                             ownerPlace) == 0)
                        ownerPlace += place->second.substr(ownerPlace.size());
                    aggregateBorrowPlaces[pointer][ownerBorrowers.first]
                        .insert(ownerPlace);
                }
        };
        if(thenFallsThrough)
            mergeJoinedSharedPointerPlaces(thenPointerBorrowTarget,
                                           thenPointerBorrowPlace,
                                           thenActiveBorrowers);
        if(elseFallsThrough)
            mergeJoinedSharedPointerPlaces(elsePointerBorrowTarget,
                                           elsePointerBorrowPlace,
                                           elseActiveBorrowers);

        activeBorrowers = std::move(mergedActiveBorrowers);
        // A mutable borrow on either reachable path may be live after the
        // merge. Keep the union so owner accesses cannot slip through on the
        // path that did not create the borrow.
        std::map<std::string, std::string> mergedMutBorrower;
        if(thenFallsThrough)
            mergedMutBorrower.insert(thenActiveMutBorrower.begin(),
                                     thenActiveMutBorrower.end());
        if(elseFallsThrough)
            for(const auto& kv : elseActiveMutBorrower)
                mergedMutBorrower.emplace(kv.first, kv.second);
        activeMutBorrower = std::move(mergedMutBorrower);

        // A callable assigned on either branch may retain any capture from
        // that branch. Merge capture-owner sets for outer closures so last-use
        // cleanup releases every possible borrow; discard branch-local
        // closure entries that cannot survive the merge scope.
        std::map<std::string, std::set<std::string>>
            mergedRuntimeClosureBorrowOwners;
        auto mergeRuntimeClosureOwners = [&](const auto& branchOwners) {
            for(const auto& closure : branchOwners)
            {
                auto depth = variableScopeDepth.find(closure.first);
                if(depth == variableScopeDepth.end() ||
                   depth->second > currentScopeDepth())
                    continue;
                auto& owners =
                    mergedRuntimeClosureBorrowOwners[closure.first];
                owners.insert(closure.second.begin(), closure.second.end());
            }
        };
        if(thenFallsThrough)
            mergeRuntimeClosureOwners(thenRuntimeClosureBorrowOwners);
        if(elseFallsThrough)
            mergeRuntimeClosureOwners(elseRuntimeClosureBorrowOwners);
        runtimeClosureBorrowOwners =
            std::move(mergedRuntimeClosureBorrowOwners);
    }
    else
    {
        movedVariables = incomingMoved;
        pointerBorrowTarget = incomingPointerBorrowTarget;
        aggregateBorrowOrigins = incomingAggregateBorrowOrigins;
        aggregateBorrowPlaces = incomingAggregateBorrowPlaces;
        aggregateBorrowFieldPlaces = incomingAggregateBorrowFieldPlaces;
        pointerBorrowPlace = incomingPointerBorrowPlace;
        activeBorrowers = incomingActiveBorrowers;
        activeMutBorrower = incomingActiveMutBorrower;
        runtimeClosureBorrowOwners = incomingRuntimeClosureBorrowOwners;
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
    auto incomingPointerBorrowParent = pointerBorrowParent;
    auto incomingAggregateBorrowOrigins = aggregateBorrowOrigins;
    auto incomingAggregateBorrowPlaces = aggregateBorrowPlaces;
    auto incomingAggregateBorrowFieldPlaces = aggregateBorrowFieldPlaces;
    auto incomingPointerBorrowPlace = pointerBorrowPlace;
    auto incomingActiveBorrowers = activeBorrowers;
    auto incomingActiveMutBorrower = activeMutBorrower;
    auto incomingRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;
    const int loopScopeDepth = currentScopeDepth();

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "while.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "while.body");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "while.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(condBB);
    loopBreakBorrowStates.emplace_back();
    loopContinueBorrowStates.emplace_back();

    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
    {
        loopBreakBlocks.pop_back();
        loopContinueBlocks.pop_back();
        loopBreakBorrowStates.pop_back();
        loopContinueBorrowStates.pop_back();
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
        loopBreakBorrowStates.pop_back();
        loopContinueBorrowStates.pop_back();
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

    MutableCallableTargetsAcrossLoop callableTargetsAcrossLoop(
        functionItemVariables, constantVariables);

    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    if(node->body)
    {
        enterCleanupScope();
        const int bodyScopeDepth = currentScopeDepth();
        for(size_t index = 0; index < node->body->statements.size(); ++index)
        {
            auto* stmt = node->body->statements[index];
            generateStatement(stmt);
            if(mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                break;
            expireUnusedLocalBorrows(node->body->statements, index,
                                     bodyScopeDepth);
        }
        exitCleanupScope();
    }
    llvm::BasicBlock* bodyEnd = builder.GetInsertBlock();
    bool bodyFallsThrough = bodyEnd && !mlang::llvm_compat::terminatorOrNull(bodyEnd);
    auto bodyMoved = movedVariables;
    auto bodyPointerBorrowTarget = pointerBorrowTarget;
    auto bodyPointerBorrowParent = pointerBorrowParent;
    auto bodyAggregateBorrowOrigins = aggregateBorrowOrigins;
    auto bodyAggregateBorrowPlaces = aggregateBorrowPlaces;
    auto bodyPointerBorrowPlace = pointerBorrowPlace;
    auto bodyActiveBorrowers = activeBorrowers;
    auto bodyActiveMutBorrower = activeMutBorrower;
    auto bodyRuntimeClosureBorrowOwners = runtimeClosureBorrowOwners;

    if(bodyFallsThrough)
    {
        builder.CreateBr(condBB);
    }

    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();
    auto breakBorrowStates = std::move(loopBreakBorrowStates.back());
    loopBreakBorrowStates.pop_back();
    auto continueBorrowStates = std::move(loopContinueBorrowStates.back());
    loopContinueBorrowStates.pop_back();
    breakBorrowStates.insert(breakBorrowStates.end(),
                             continueBorrowStates.begin(),
                             continueBorrowStates.end());

    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    pointerBorrowParent = incomingPointerBorrowParent;
    aggregateBorrowOrigins = incomingAggregateBorrowOrigins;
    aggregateBorrowPlaces = incomingAggregateBorrowPlaces;
    aggregateBorrowFieldPlaces = incomingAggregateBorrowFieldPlaces;
    pointerBorrowPlace = incomingPointerBorrowPlace;
    activeBorrowers = incomingActiveBorrowers;
    activeMutBorrower = incomingActiveMutBorrower;
    runtimeClosureBorrowOwners = incomingRuntimeClosureBorrowOwners;

    if(bodyFallsThrough)
    {
        for(auto it = pointerBorrowParent.begin();
            it != pointerBorrowParent.end();)
        {
            auto bodyParent = bodyPointerBorrowParent.find(it->first);
            if(bodyParent == bodyPointerBorrowParent.end() ||
               bodyParent->second != it->second)
                it = pointerBorrowParent.erase(it);
            else
                ++it;
        }
        movedVariables.insert(bodyMoved.begin(), bodyMoved.end());
        for(const auto& aggregate : bodyAggregateBorrowOrigins)
        {
            auto& origins = aggregateBorrowOrigins[aggregate.first];
            origins.insert(aggregate.second.begin(), aggregate.second.end());
        }
        for(auto place = pointerBorrowPlace.begin();
            place != pointerBorrowPlace.end();)
        {
            auto bodyPlace = bodyPointerBorrowPlace.find(place->first);
            if(bodyPlace == bodyPointerBorrowPlace.end() ||
               bodyPlace->second != place->second)
                place = pointerBorrowPlace.erase(place);
            else
                ++place;
        }
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
        // Preserve precise paths for live exclusive pointers across the
        // zero-or-more-iterations join. Other aggregate path sets remain
        // conservative because a loop may replace them on later iterations.
        std::map<std::string, std::map<std::string, std::set<std::string>>>
            loopPointerBorrowPlaces;
        auto isSharedPointerIn = [](const auto& borrowers,
                                    const std::string& pointer) {
            for(const auto& owner : borrowers)
                if(owner.second.count(pointer))
                    return true;
            return false;
        };
        auto mergeLoopPointerPlaces = [&](const auto& branchPlaces) {
            for(const auto& borrower : branchPlaces)
            {
                const bool sharedPointer =
                    pointerBorrowTarget.count(borrower.first) &&
                    (isSharedPointerIn(incomingActiveBorrowers, borrower.first) ||
                     isSharedPointerIn(bodyActiveBorrowers, borrower.first));
                if(!isMutBorrower(borrower.first) && !sharedPointer)
                    continue;
                for(const auto& owner : borrower.second)
                    loopPointerBorrowPlaces[borrower.first][owner.first]
                        .insert(owner.second.begin(), owner.second.end());
            }
        };
        mergeLoopPointerPlaces(incomingAggregateBorrowPlaces);
        mergeLoopPointerPlaces(bodyAggregateBorrowPlaces);
        auto addLoopPointerPlace = [&](const auto& targets,
                                       const auto& places,
                                       const auto& borrowers) {
            for(const auto& target : targets)
            {
                if(!isMutBorrower(target.first) &&
                   !isSharedPointerIn(borrowers, target.first))
                    continue;
                auto& ownerPlaces =
                    loopPointerBorrowPlaces[target.first][target.second];
                if(!ownerPlaces.empty())
                    continue;
                auto place = places.find(target.first);
                ownerPlaces.insert(place == places.end() ? target.second
                                                         : place->second);
            }
        };
        addLoopPointerPlace(incomingPointerBorrowTarget,
                            incomingPointerBorrowPlace,
                            incomingActiveBorrowers);
        addLoopPointerPlace(bodyPointerBorrowTarget, bodyPointerBorrowPlace,
                            bodyActiveBorrowers);
        aggregateBorrowPlaces = std::move(loopPointerBorrowPlaces);
        aggregateBorrowFieldPlaces.clear();
        for(const auto& closure : bodyRuntimeClosureBorrowOwners)
        {
            auto closureDepth = variableScopeDepth.find(closure.first);
            if(closureDepth != variableScopeDepth.end() &&
               closureDepth->second > loopScopeDepth)
                continue;
            auto& owners = runtimeClosureBorrowOwners[closure.first];
            owners.insert(closure.second.begin(), closure.second.end());
        }
    }

    // A break exits through endBB without falling through to the loop's next
    // condition. Preserve its borrow facts separately from the back-edge join.
    for(const auto& state : breakBorrowStates)
    {
        auto survivesLoopScope = [&](const std::string& name) {
            auto depth = state.variableScopeDepth.find(name);
            return depth == state.variableScopeDepth.end() ||
                   depth->second <= loopScopeDepth;
        };
        movedVariables.insert(state.movedVariables.begin(),
                              state.movedVariables.end());
        for(const auto& borrow : state.pointerBorrowTarget)
            if(survivesLoopScope(borrow.first))
                pointerBorrowTarget.emplace(borrow.first, borrow.second);
        for(const auto& aggregate : state.aggregateBorrowOrigins)
            if(survivesLoopScope(aggregate.first))
                for(const auto& owner : aggregate.second)
                    if(survivesLoopScope(owner))
                        aggregateBorrowOrigins[aggregate.first].insert(owner);
        for(const auto& aggregate : state.aggregateBorrowPlaces)
            if(survivesLoopScope(aggregate.first))
                for(const auto& owner : aggregate.second)
                    if(survivesLoopScope(owner.first))
                        aggregateBorrowPlaces[aggregate.first][owner.first]
                            .insert(owner.second.begin(), owner.second.end());
        for(const auto& aggregate : state.aggregateBorrowFieldPlaces)
            if(survivesLoopScope(aggregate.first))
                for(const auto& field : aggregate.second)
                    for(const auto& owner : field.second)
                        if(survivesLoopScope(owner.first))
                            aggregateBorrowFieldPlaces[aggregate.first]
                                                      [field.first][owner.first]
                                                          .insert(
                                                              owner.second.begin(),
                                                              owner.second.end());
        for(const auto& place : state.pointerBorrowPlace)
        {
            if(!survivesLoopScope(place.first))
                continue;
            auto existing = pointerBorrowPlace.find(place.first);
            if(existing != pointerBorrowPlace.end() &&
               existing->second != place.second)
                pointerBorrowPlace.erase(existing);
        }
        for(const auto& owner : state.activeBorrowers)
            if(survivesLoopScope(owner.first))
                for(const auto& borrower : owner.second)
                    if(survivesLoopScope(borrower))
                        activeBorrowers[owner.first].insert(borrower);
        for(const auto& borrow : state.activeMutBorrower)
            if(survivesLoopScope(borrow.second))
                activeMutBorrower.emplace(borrow.first, borrow.second);
        for(const auto& closure : state.runtimeClosureBorrowOwners)
            if(survivesLoopScope(closure.first))
                for(const auto& owner : closure.second)
                    if(survivesLoopScope(owner))
                        runtimeClosureBorrowOwners[closure.first].insert(owner);
    }
    if(!breakBorrowStates.empty())
    {
        // Break exits may carry a pointer assignment that does not occur on
        // the ordinary loop back-edge. Preserve its exact owner place
        // separately from the single representative pointer target.
        for(const auto& state : breakBorrowStates)
        {
            auto survivesState = [&](const std::string& name) {
                auto depth = state.variableScopeDepth.find(name);
                return depth == state.variableScopeDepth.end() ||
                       depth->second <= loopScopeDepth;
            };
            std::map<std::string, std::set<std::string>> pointerOwners;
            for(const auto& borrow : state.activeMutBorrower)
                pointerOwners[borrow.second];
            for(const auto& owner : state.activeBorrowers)
                for(const auto& borrower : owner.second)
                    if(state.pointerBorrowTarget.count(borrower))
                        pointerOwners[borrower].insert(owner.first);
            for(auto& pointer : pointerOwners)
            {
                auto target = state.pointerBorrowTarget.find(pointer.first);
                if(!survivesState(pointer.first))
                    continue;
                if(target != state.pointerBorrowTarget.end())
                    pointer.second.insert(target->second);
                for(const auto& owner : pointer.second)
                {
                    if(!survivesState(owner))
                        continue;
                    auto aggregate = state.aggregateBorrowPlaces.find(
                        pointer.first);
                    if(aggregate != state.aggregateBorrowPlaces.end())
                    {
                        auto places = aggregate->second.find(owner);
                        if(places != aggregate->second.end() &&
                           !places->second.empty())
                        {
                            aggregateBorrowPlaces[pointer.first][owner]
                                .insert(places->second.begin(),
                                        places->second.end());
                            continue;
                        }
                    }
                    std::string place = owner;
                    auto pointerPlace =
                        state.pointerBorrowPlace.find(pointer.first);
                    if(target != state.pointerBorrowTarget.end() &&
                       target->second == owner &&
                       pointerPlace != state.pointerBorrowPlace.end())
                        place = pointerPlace->second;
                    aggregateBorrowPlaces[pointer.first][owner].insert(place);
                }
            }
        }

        // A reborrow's parent/place is only precise when every exiting path
        // agrees. Keep only refinements present on all recorded break paths.
        for(auto parent = pointerBorrowParent.begin();
            parent != pointerBorrowParent.end();)
        {
            const bool common = std::all_of(
                breakBorrowStates.begin(), breakBorrowStates.end(),
                [&](const auto& state) {
                    auto branchParent =
                        state.pointerBorrowParent.find(parent->first);
                    return branchParent != state.pointerBorrowParent.end() &&
                           branchParent->second == parent->second;
                });
            if(!common)
                parent = pointerBorrowParent.erase(parent);
            else
                ++parent;
        }
        for(auto place = pointerBorrowPlace.begin();
            place != pointerBorrowPlace.end();)
        {
            const bool common = std::all_of(
                breakBorrowStates.begin(), breakBorrowStates.end(),
                [&](const auto& state) {
                    auto branchPlace = state.pointerBorrowPlace.find(place->first);
                    return branchPlace != state.pointerBorrowPlace.end() &&
                           branchPlace->second == place->second;
                });
            if(!common)
                place = pointerBorrowPlace.erase(place);
            else
                ++place;
        }
        for(auto places = aggregateBorrowPlaces.begin();
            places != aggregateBorrowPlaces.end();)
        {
            bool sharedPointer = false;
            if(pointerBorrowTarget.count(places->first))
                for(const auto& owner : activeBorrowers)
                    if(owner.second.count(places->first))
                    {
                        sharedPointer = true;
                        break;
                    }
            if(!isMutBorrower(places->first) && !sharedPointer)
                places = aggregateBorrowPlaces.erase(places);
            else
                ++places;
        }
        aggregateBorrowFieldPlaces.clear();
    }
}

void CodeGenerator::generateForStatement(ForNode* node)
{
    MutableCallableTargetsAcrossLoop callableTargetsAcrossLoop(
        functionItemVariables, constantVariables);
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
        auto stateBeforeLoopPointerBorrowPlace = pointerBorrowPlace;
        auto stateBeforeLoopPointerBorrowParent = pointerBorrowParent;
        auto stateBeforeLoopAggregateBorrowOrigins = aggregateBorrowOrigins;
        auto stateBeforeLoopAggregateBorrowPlaces = aggregateBorrowPlaces;
        auto stateBeforeLoopAggregateBorrowFieldPlaces =
            aggregateBorrowFieldPlaces;
        auto stateBeforeLoopActiveBorrowers = activeBorrowers;
        auto stateBeforeLoopActiveMutBorrower = activeMutBorrower;
        const int loopScopeDepth = currentScopeDepth();

        namedValues[node->varName] = loopVar;

        debugDeclareVariable(node->varName, loopVar, nullptr, node);  // mlang -g
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
        loopBreakBorrowStates.emplace_back();
        loopContinueBorrowStates.emplace_back();

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
            const int bodyScopeDepth = currentScopeDepth();
            for(size_t index = 0; index < node->body->statements.size(); ++index)
            {
                auto stmt = node->body->statements[index];
                generateStatement(stmt);
                if(mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                    break;
                expireUnusedLocalBorrows(node->body->statements, index,
                                         bodyScopeDepth);
            }
            exitCleanupScope();
        }
        auto bodyPointerBorrowTarget = pointerBorrowTarget;
        auto bodyPointerBorrowPlace = pointerBorrowPlace;
        auto bodyAggregateBorrowPlaces = aggregateBorrowPlaces;
        auto bodyActiveBorrowers = activeBorrowers;
        auto bodyActiveMutBorrower = activeMutBorrower;
        auto bodyPointerBorrowParent = pointerBorrowParent;

        if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
        auto breakBorrowStates = std::move(loopBreakBorrowStates.back());
        loopBreakBorrowStates.pop_back();
        auto continueBorrowStates =
            std::move(loopContinueBorrowStates.back());
        loopContinueBorrowStates.pop_back();
        breakBorrowStates.insert(breakBorrowStates.end(),
                                 continueBorrowStates.begin(),
                                 continueBorrowStates.end());

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
            pointerBorrowPlace =
                std::move(stateBeforeLoopPointerBorrowPlace);
            pointerBorrowParent =
                std::move(stateBeforeLoopPointerBorrowParent);
            aggregateBorrowOrigins =
                std::move(stateBeforeLoopAggregateBorrowOrigins);
            aggregateBorrowPlaces =
                std::move(stateBeforeLoopAggregateBorrowPlaces);
            aggregateBorrowFieldPlaces =
                std::move(stateBeforeLoopAggregateBorrowFieldPlaces);
            activeBorrowers = std::move(stateBeforeLoopActiveBorrowers);
        }
        else
        {
            for(auto it = stateBeforeLoopPointerBorrowParent.begin();
                it != stateBeforeLoopPointerBorrowParent.end();)
            {
                auto bodyParent = bodyPointerBorrowParent.find(it->first);
                if(bodyParent == bodyPointerBorrowParent.end() ||
                   bodyParent->second != it->second)
                    it = stateBeforeLoopPointerBorrowParent.erase(it);
                else
                    ++it;
            }
            pointerBorrowParent =
                std::move(stateBeforeLoopPointerBorrowParent);
            for(const auto& aggregate : aggregateBorrowOrigins)
            {
                auto& origins =
                    stateBeforeLoopAggregateBorrowOrigins[aggregate.first];
                origins.insert(aggregate.second.begin(),
                               aggregate.second.end());
            }
            aggregateBorrowOrigins =
                std::move(stateBeforeLoopAggregateBorrowOrigins);
            aggregateBorrowPlaces.clear();
            aggregateBorrowFieldPlaces.clear();
            for(const auto& owner : activeBorrowers)
            {
                auto& borrowers = stateBeforeLoopActiveBorrowers[owner.first];
                borrowers.insert(owner.second.begin(), owner.second.end());
            }
            activeBorrowers = std::move(stateBeforeLoopActiveBorrowers);
            for(const auto& borrow : activeMutBorrower)
                stateBeforeLoopActiveMutBorrower.emplace(borrow);
            activeMutBorrower =
                std::move(stateBeforeLoopActiveMutBorrower);

            std::map<std::string,
                     std::map<std::string, std::set<std::string>>>
                loopPointerPlaces;
            auto addBranchPointerPlaces = [&](const auto& targets,
                                             const auto& places,
                                             const auto& aggregatePlaces,
                                             const auto& sharedBorrowers,
                                             const auto& mutableBorrowers) {
                std::map<std::string, std::set<std::string>> pointerOwners;
                for(const auto& target : targets)
                    pointerOwners[target.first].insert(target.second);
                for(const auto& borrow : mutableBorrowers)
                    pointerOwners[borrow.second];
                for(const auto& owner : sharedBorrowers)
                    for(const auto& pointer : owner.second)
                        if(targets.count(pointer))
                            pointerOwners[pointer].insert(owner.first);
                for(const auto& pointer : pointerOwners)
                {
                    auto target = targets.find(pointer.first);
                    if(target != targets.end())
                        pointerOwners[pointer.first].insert(target->second);
                    auto existingPaths = aggregatePlaces.find(pointer.first);
                    if(existingPaths != aggregatePlaces.end())
                        for(const auto& owner : existingPaths->second)
                            loopPointerPlaces[pointer.first][owner.first]
                                .insert(owner.second.begin(),
                                        owner.second.end());
                    for(const auto& owner : pointerOwners[pointer.first])
                    {
                        auto existingOwnerPaths =
                            loopPointerPlaces[pointer.first].find(owner);
                        if(existingOwnerPaths !=
                               loopPointerPlaces[pointer.first].end() &&
                           !existingOwnerPaths->second.empty())
                            continue;
                        std::string ownerPlace = owner;
                        auto place = places.find(pointer.first);
                        if(target != targets.end() && target->second == owner &&
                           place != places.end())
                            ownerPlace = place->second;
                        loopPointerPlaces[pointer.first][owner].insert(
                            ownerPlace);
                    }
                }
            };
            addBranchPointerPlaces(stateBeforeLoopPointerBorrowTarget,
                                   stateBeforeLoopPointerBorrowPlace,
                                   stateBeforeLoopAggregateBorrowPlaces,
                                   stateBeforeLoopActiveBorrowers,
                                   stateBeforeLoopActiveMutBorrower);
            addBranchPointerPlaces(bodyPointerBorrowTarget,
                                   bodyPointerBorrowPlace,
                                   bodyAggregateBorrowPlaces,
                                   bodyActiveBorrowers,
                                   bodyActiveMutBorrower);
            aggregateBorrowPlaces = std::move(loopPointerPlaces);
        }
        mergeLoopExitBorrowStates(
            breakBorrowStates, loopScopeDepth,
            {node->varName, node->indexVarName});
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
    const int loopScopeDepth = currentScopeDepth();

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

    debugDeclareVariable(node->varName, loopVar, nullptr, node);  // mlang -g
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
        debugDeclareVariable(node->indexVarName, indexVar, nullptr, node);  // mlang -g
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
    loopBreakBorrowStates.emplace_back();
    loopContinueBorrowStates.emplace_back();

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
        const int bodyScopeDepth = currentScopeDepth();
        for(size_t index = 0; index < node->body->statements.size(); ++index)
        {
            auto stmt = node->body->statements[index];
            generateStatement(stmt);
            if(mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                break;
            expireUnusedLocalBorrows(node->body->statements, index,
                                     bodyScopeDepth);
        }
        exitCleanupScope();
    }

    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
    auto breakBorrowStates = std::move(loopBreakBorrowStates.back());
    loopBreakBorrowStates.pop_back();
    auto continueBorrowStates = std::move(loopContinueBorrowStates.back());
    loopContinueBorrowStates.pop_back();
    breakBorrowStates.insert(breakBorrowStates.end(),
                             continueBorrowStates.begin(),
                             continueBorrowStates.end());

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

    mergeLoopExitBorrowStates(
        breakBorrowStates, loopScopeDepth,
        {node->varName, node->indexVarName});
    delete elemTypeNode;
}

void CodeGenerator::generateForListVariableIteration(ForNode* node,
                                                     IdentifierNode* listId)
{
    // For iterating over a list variable, we need the list structure
    // Lists are stored as: { i64 size, ptr data }
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    const int loopScopeDepth = currentScopeDepth();

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

    debugDeclareVariable(node->varName, loopVar, nullptr, node);  // mlang -g
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
        debugDeclareVariable(node->indexVarName, indexVar, nullptr, node);  // mlang -g
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
    loopBreakBorrowStates.emplace_back();
    loopContinueBorrowStates.emplace_back();

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
        const int bodyScopeDepth = currentScopeDepth();
        for(size_t index = 0; index < node->body->statements.size(); ++index)
        {
            auto stmt = node->body->statements[index];
            generateStatement(stmt);
            if(mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                break;
            expireUnusedLocalBorrows(node->body->statements, index,
                                     bodyScopeDepth);
        }
        exitCleanupScope();
    }

    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
    auto breakBorrowStates = std::move(loopBreakBorrowStates.back());
    loopBreakBorrowStates.pop_back();
    auto continueBorrowStates = std::move(loopContinueBorrowStates.back());
    loopContinueBorrowStates.pop_back();
    breakBorrowStates.insert(breakBorrowStates.end(),
                             continueBorrowStates.begin(),
                             continueBorrowStates.end());

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
    mergeLoopExitBorrowStates(
        breakBorrowStates, loopScopeDepth,
        {node->varName, node->indexVarName});
}

void CodeGenerator::generateForEnumIteration(ForNode* node,
                                             const std::string& enumName)
{
    const int loopScopeDepth = currentScopeDepth();
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

    debugDeclareVariable(node->varName, loopVar, nullptr, node);  // mlang -g
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
        debugDeclareVariable(node->indexVarName, indexVar, nullptr, node);  // mlang -g
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
    loopBreakBorrowStates.emplace_back();
    loopContinueBorrowStates.emplace_back();

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
        const int bodyScopeDepth = currentScopeDepth();
        for(size_t index = 0; index < node->body->statements.size(); ++index)
        {
            auto stmt = node->body->statements[index];
            generateStatement(stmt);
            if(mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                break;
            expireUnusedLocalBorrows(node->body->statements, index,
                                     bodyScopeDepth);
        }
        exitCleanupScope();
    }

    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
    auto breakBorrowStates = std::move(loopBreakBorrowStates.back());
    loopBreakBorrowStates.pop_back();
    auto continueBorrowStates = std::move(loopContinueBorrowStates.back());
    loopContinueBorrowStates.pop_back();
    breakBorrowStates.insert(breakBorrowStates.end(),
                             continueBorrowStates.begin(),
                             continueBorrowStates.end());

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
    mergeLoopExitBorrowStates(
        breakBorrowStates, loopScopeDepth,
        {node->varName, node->indexVarName});
}

void CodeGenerator::generateForMapIteration(ForNode* node,
                                            MapIteratorNode* mapIter)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    const int loopScopeDepth = currentScopeDepth();

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
        debugDeclareVariable(node->varName, loopVar, nullptr, node);  // mlang -g
        variableTypes[node->varName] = keyTypeNode->kind;
        clearMovedVariable(node->varName);
        recordVariableScopeDepth(node->varName);
        break;

    case MapIteratorNode::ITER_VALUES:
        loopVarType = valueType;
        loopVar = builder.CreateAlloca(valueType, nullptr, node->varName);
        namedValues[node->varName] = loopVar;
        debugDeclareVariable(node->varName, loopVar, nullptr, node);  // mlang -g
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
            debugDeclareVariable(node->varName, loopVar, nullptr, node);  // mlang -g
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
    loopBreakBorrowStates.emplace_back();
    loopContinueBorrowStates.emplace_back();

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
        const int bodyScopeDepth = currentScopeDepth();
        for(size_t index = 0; index < node->body->statements.size(); ++index)
        {
            auto stmt = node->body->statements[index];
            generateStatement(stmt);
            if(mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
                break;
            expireUnusedLocalBorrows(node->body->statements, index,
                                     bodyScopeDepth);
        }
        exitCleanupScope();
    }

    if(!mlang::llvm_compat::terminatorOrNull(builder.GetInsertBlock()))
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
    auto breakBorrowStates = std::move(loopBreakBorrowStates.back());
    loopBreakBorrowStates.pop_back();
    auto continueBorrowStates = std::move(loopContinueBorrowStates.back());
    loopContinueBorrowStates.pop_back();
    breakBorrowStates.insert(breakBorrowStates.end(),
                             continueBorrowStates.begin(),
                             continueBorrowStates.end());

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
    mergeLoopExitBorrowStates(breakBorrowStates, loopScopeDepth,
                               {node->varName, node->indexVarName});
}
