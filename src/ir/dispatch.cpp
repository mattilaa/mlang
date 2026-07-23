#include "ir.h"
#include "ir/ast_analysis.h"

#include <llvm/Config/llvm-config.h>

using mlang::ir_detail::ast_analysis::collect_used_idents;

void CodeGenerator::generateStatement(StatementNode* node)
{
    if(auto returnNode = dynamic_cast<ReturnNode*>(node))
    {
        generateReturnStatement(returnNode);
    }
    else if(auto cexprNode = dynamic_cast<CexprDeclNode*>(node))
    {
        generateCexprDeclaration(cexprNode);
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
    else if(auto cexprIfNode = dynamic_cast<CexprIfNode*>(node))
    {
        generateCexprIfStatement(cexprIfNode);
    }
    else if(auto forNode = dynamic_cast<ForNode*>(node))
    {
        generateForStatement(forNode);
    }
    else if(auto whileNode = dynamic_cast<WhileNode*>(node))
    {
        generateWhileStatement(whileNode);
    }
    else if(auto tryCatchNode = dynamic_cast<TryCatchNode*>(node))
    {
        generateTryCatchStatement(tryCatchNode);
    }
    else if(auto blockNode = dynamic_cast<BlockStatementNode*>(node))
    {
        auto savedConstexprValues = constexprValues;
        enterCleanupScope();
        if(blockNode->isUnsafe)
            unsafeDepth++;
        const auto& blkStmts = blockNode->statements->statements;
        if(blkStmts.empty())
        {
            reportWarning(blockNode->line, blockNode->col, "empty block");
        }
        for(size_t si = 0; si < blkStmts.size(); si++)
        {
            generateStatement(blkStmts[si]);
            // NLL: expire borrow variables not referenced in remaining stmts
            if(!pointerBorrowTarget.empty())
            {
                std::set<std::string> futureIdents;
                for(size_t sj = si + 1; sj < blkStmts.size(); sj++)
                    collect_used_idents(blkStmts[sj], futureIdents);
                std::vector<std::string> toClear;
                for(const auto& kv : pointerBorrowTarget)
                {
                    if(futureIdents.count(kv.first))
                        continue;
                    // Don't NLL-expire exclusive struct borrows (ptr<T> where T
                    // is a struct). They remain active until scope exit or
                    // explicit reassignment so a second borrow of the same
                    // owner is rejected.
                    auto peit = pointerElementTypes.find(kv.first);
                    if(peit != pointerElementTypes.end() && peit->second &&
                       peit->second->kind == TypeNode::TYPE_STRUCT)
                        continue;
                    toClear.push_back(kv.first);
                }
                for(const auto& ptr : toClear)
                    clearPointerBorrow(ptr);
            }
        }
        if(blockNode->isUnsafe)
            unsafeDepth--;
        exitCleanupScope();
        constexprValues = std::move(savedConstexprValues);
    }
    else if(auto printNode = dynamic_cast<PrintNode*>(node))
    {
        generatePrintStatement(printNode);
    }
    else if(auto assertNode = dynamic_cast<AssertNode*>(node))
    {
        generateAssert(assertNode);
    }
    else if(auto assertEqNode = dynamic_cast<AssertEqNode*>(node))
    {
        generateAssertEq(assertEqNode);
    }
    else if(auto staticAssertNode = dynamic_cast<StaticAssertNode*>(node))
    {
        generateStaticAssert(staticAssertNode);
    }
    else if(auto breakNode = dynamic_cast<BreakNode*>(node))
    {
        generateBreakStatement(breakNode);
    }
    else if(auto throwNode = dynamic_cast<ThrowNode*>(node))
    {
        generateThrowStatement(throwNode);
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
    else if(auto* foldOp = dynamic_cast<FoldExpressionNode*>(node))
    {
        return generateFoldExpression(foldOp);
    }
    else if(auto unaryOp = dynamic_cast<UnaryOpNode*>(node))
    {
        return generateUnaryOp(unaryOp);
    }
    else if(auto* updateOp = dynamic_cast<UpdateExpressionNode*>(node))
    {
        return generateUpdateExpression(updateOp);
    }
    else if(auto ternary = dynamic_cast<TernaryNode*>(node))
    {
        return generateTernaryExpression(ternary);
    }
    else if(auto tryExpr = dynamic_cast<TryExpressionNode*>(node))
    {
        return generateTryExpression(tryExpr);
    }
    else if(auto* sizeofExpr = dynamic_cast<SizeofExpressionNode*>(node))
    {
        return generateSizeofExpression(sizeofExpr);
    }
    else if(auto* cexprExpr = dynamic_cast<CexprExpressionNode*>(node))
    {
        return generateCexprExpression(cexprExpr);
    }
    else if(auto* inlineAsm = dynamic_cast<InlineAsmNode*>(node))
    {
        return generateInlineAsm(inlineAsm);
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
    else if(auto* closure = dynamic_cast<ClosureNode*>(node))
    {
        llvm::Function* fn = generateClosureFn(closure);
        if(!fn)
            return nullptr;
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        return builder.CreateBitCast(fn, ptrType, "closure.ptr");
    }
    else if(auto* arrFill = dynamic_cast<ArrayFillNode*>(node))
    {
        return generateArrayFill(arrFill);
    }
    return nullptr;
}

