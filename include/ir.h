#ifndef IR_H
#define IR_H

#include "ast.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/Target/TargetMachine.h>
#include <map>
#include <memory>
#include <set>
#include <string>

class CodeGenerator
{
private:
    llvm::LLVMContext& context;
    llvm::IRBuilder<>& builder;
    std::unique_ptr<llvm::Module>& module;
    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, TypeNode::TypeKind>
        variableTypes; // Track original MLA types
    std::map<std::string, llvm::Type*> structTypes;
    std::set<std::string>
        constantVariables; // Track variables declared with 'let'
    bool hasError = false; // Track if any errors occurred

    // Cached printf/fprintf functions
    bool stdioInitialized = false;
    llvm::FunctionCallee printfFunc;
    llvm::FunctionCallee fprintfFunc;
    llvm::Value* stderrPtr;

    // Helper methods
    llvm::Type* getLLVMType(TypeNode::TypeKind kind);
    bool isUnsignedType(TypeNode::TypeKind kind);
    llvm::Value* getNamedValue(const std::string& name);
    void setNamedValue(const std::string& name, llvm::Value* value);
    void reportError(int line, const std::string& message);

    // Initialize stdio functions (printf, fprintf, stderr)
    void initializeStdioFunctions();

    // Convert MLA format string ({}) to C format string (%d, %s, etc.)
    std::string convertFormatString(const std::string& mlaFormat,
                                    const std::vector<ExpressionNode*>& args,
                                    std::vector<llvm::Value*>& argValues);

public:
    CodeGenerator(llvm::LLVMContext& ctx, llvm::IRBuilder<>& b,
                  std::unique_ptr<llvm::Module>& m)
        : context(ctx), builder(b), module(m), stdioInitialized(false),
          printfFunc(nullptr), fprintfFunc(nullptr), stderrPtr(nullptr)
    {
    }

    // Check if compilation had errors
    bool hadError() const
    {
        return hasError;
    }

    // Main generation methods
    void generateCode(ProgramNode* program);
    llvm::Function* generateFunctionDeclaration(FunctionDefNode* node);
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
    void generateForStatement(ForNode* node);
    void generateReturnStatement(ReturnNode* node);
    void generateLetDeclaration(LetDeclNode* node);
    void generateVarDeclaration(VarDeclNode* node);
    void generateAssignment(AssignmentNode* node);
    void generatePrintStatement(PrintNode* node);

    // Struct related methods
    void generateStructDefinition(StructDefNode* node);
    llvm::StructType* getStructType(const std::string& name);
};

// Backend compilation class
class Backend
{
private:
    std::unique_ptr<llvm::Module>& module;
    llvm::TargetMachine* targetMachine;
    std::string targetTriple;

    bool initializeTarget();

public:
    explicit Backend(std::unique_ptr<llvm::Module>& m);
    ~Backend() = default;

    // Emit object file (.o)
    bool emitObjectFile(const std::string& filename);

    // Emit assembly file (.s)
    bool emitAssemblyFile(const std::string& filename);

    // Emit LLVM IR to file (.ll)
    bool emitLLVMIR(const std::string& filename);

    // Emit LLVM bitcode (.bc)
    bool emitBitcode(const std::string& filename);

    // Link object file to executable using system linker
    static bool linkExecutable(const std::string& objectFile,
                               const std::string& outputFile);

    // Convenience method: compile and link to executable
    bool compileToExecutable(const std::string& outputFile);

    // Run optimization passes on the module
    void optimize(int level = 2);

    // Get the target triple
    const std::string& getTargetTriple() const
    {
        return targetTriple;
    }
};

#endif
