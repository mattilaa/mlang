#ifndef IR_H
#define IR_H

#include "ast.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class CodeGenerator
{
public:
    CodeGenerator(llvm::LLVMContext& ctx, llvm::IRBuilder<>& b,
                  std::unique_ptr<llvm::Module>& m)
        : context(ctx), builder(b), module(m), hasError(false),
          stdioInitialized(false)
    {
    }

    void generateCode(ProgramNode* program);
    bool hadError() const { return hasError; }

private:
    llvm::LLVMContext& context;
    llvm::IRBuilder<>& builder;
    std::unique_ptr<llvm::Module>& module;
    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, llvm::Type*> structTypes;
    std::set<std::string> constantVariables;
    std::map<std::string, TypeNode::TypeKind> variableTypes;
    // Track element types for generic lists and maps
    std::map<std::string, TypeNode*> listElementTypes;
    std::map<std::string, std::pair<TypeNode*, TypeNode*>> mapKeyValueTypes;
    bool hasError;

    // Stdio function support
    bool stdioInitialized;
    llvm::FunctionCallee printfFunc;
    llvm::FunctionCallee fprintfFunc;
    llvm::Value* stderrPtr;

    // Loop control flow support (for break/continue)
    std::vector<llvm::BasicBlock*> loopBreakBlocks;
    std::vector<llvm::BasicBlock*> loopContinueBlocks;

    // Type helpers
    llvm::Type* getLLVMType(TypeNode::TypeKind kind);
    bool isUnsignedType(TypeNode::TypeKind kind);
    llvm::StructType* getStructType(const std::string& name);

    // Stdio initialization
    void initializeStdioFunctions();
    std::string convertFormatString(const std::string& mlaFormat,
                                    const std::vector<ExpressionNode*>& args,
                                    std::vector<llvm::Value*>& argValues);

    // Code generation for different node types
    llvm::Function* generateFunctionDeclaration(FunctionDefNode* node);
    llvm::Function* generateFunctionDefinition(FunctionDefNode* node);
    void generateStructDefinition(StructDefNode* node);

    void generateStatement(StatementNode* node);
    void generateReturnStatement(ReturnNode* node);
    void generateLetDeclaration(LetDeclNode* node);
    void generateVarDeclaration(VarDeclNode* node);
    void generateAssignment(AssignmentNode* node);
    void generateIfStatement(IfNode* node);
    void generateForStatement(ForNode* node);
    void generatePrintStatement(PrintNode* node);
    void generateBreakStatement(BreakNode* node);
    void generateContinueStatement(ContinueNode* node);

    llvm::Value* generateExpression(ExpressionNode* node);
    llvm::Value* generateBinaryOp(BinaryOpNode* node);
    llvm::Value* generateIntLiteral(IntLiteralNode* node);
    llvm::Value* generateFloatLiteral(FloatLiteralNode* node);
    llvm::Value* generateDoubleLiteral(DoubleLiteralNode* node);
    llvm::Value* generateStringLiteral(StringLiteralNode* node);
    llvm::Value* generateIdentifier(IdentifierNode* node);
    llvm::Value* generateFunctionCall(FunctionCallNode* node);
    llvm::Value* generateCastExpression(CastExpressionNode* node);
    llvm::Value* generateListLiteral(ListLiteralNode* node);
    llvm::Value* generateMapLiteral(MapLiteralNode* node);
    llvm::Value* generateIndexExpression(IndexExpressionNode* node);

    // List/Map iteration helpers
    void generateForListLiteralIteration(ForNode* node, ListLiteralNode* listLit);
    void generateForListVariableIteration(ForNode* node, IdentifierNode* listId);
    void generateForMapIteration(ForNode* node, llvm::Value* mapPtr);

    // Collection type helpers
    llvm::StructType* getListStructType(llvm::Type* elementType);
    llvm::StructType* getMapStructType(llvm::Type* keyType,
                                       llvm::Type* valueType);

    void reportError(int line, const std::string& message);
};

class Backend
{
public:
    Backend(std::unique_ptr<llvm::Module>& m);

    bool emitObjectFile(const std::string& filename);
    bool emitAssemblyFile(const std::string& filename);
    bool emitLLVMIR(const std::string& filename);
    bool emitBitcode(const std::string& filename);
    bool compileToExecutable(const std::string& outputFile);
    void optimize(int level);
    std::string getTargetTriple() const { return targetTriple; }

private:
    std::unique_ptr<llvm::Module>& module;
    llvm::TargetMachine* targetMachine;
    std::string targetTriple;

    bool initializeTarget();
    bool linkExecutable(const std::string& objectFile,
                        const std::string& outputFile);
};

#endif // IR_H
