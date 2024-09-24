#pragma once

#include "ast.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <map>
#include <memory>

class CodeGenerator
{
private:
    llvm::LLVMContext& context;
    llvm::IRBuilder<>& builder;
    std::unique_ptr<llvm::Module>& module;
    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, llvm::Type*> structTypes;

    // Helper methods
    llvm::Type* getLLVMType(TypeNode::TypeKind kind);
    llvm::Value* getNamedValue(const std::string& name);
    void setNamedValue(const std::string& name, llvm::Value* value);

public:
    CodeGenerator(llvm::LLVMContext& ctx, llvm::IRBuilder<>& b,
                  std::unique_ptr<llvm::Module>& m)
        : context(ctx), builder(b), module(m)
    {
    }

    // Main generation methods
    void generateCode(ProgramNode* program);
    llvm::Function* generateFunctionDefinition(FunctionDefNode* node);
    llvm::Value* generateExpression(ExpressionNode* node);
    void generateStatement(StatementNode* node);

    // Expression generation methods
    llvm::Value* generateBinaryOp(BinaryOpNode* node);
    llvm::Value* generateIntLiteral(IntLiteralNode* node);
    llvm::Value* generateFloatLiteral(FloatLiteralNode* node);
    llvm::Value* generateDoubleLiteral(DoubleLiteralNode* node);
    llvm::Value* generateStringLiteral(StringLiteralNode* node);
    llvm::Value* generateIdentifier(IdentifierNode* node);
    llvm::Value* generateFunctionCall(FunctionCallNode* node);
    llvm::Value* generateCastExpression(CastExpressionNode* node);

    // Statement generation methods
    void generateIfStatement(IfNode* node);
    void generateReturnStatement(ReturnNode* node);
    void generateLetDeclaration(LetDeclNode* node);
    void generateVarDeclaration(VarDeclNode* node);
    void generateAssignment(AssignmentNode* node);

    // Struct related methods
    void generateStructDefinition(StructDefNode* node);
    llvm::StructType* getStructType(const std::string& name);
};
