#ifndef IR_H
#define IR_H

#include "ast.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class ModuleLoader;

class CodeGenerator
{
public:
    enum class OwnershipClass
    {
        Copy,
        MoveOnly
    };

    CodeGenerator(llvm::LLVMContext& ctx, llvm::IRBuilder<>& b,
                  std::unique_ptr<llvm::Module>& m, bool debug = false)
        : context(ctx), builder(b), module(m), hasError(false),
          stdioInitialized(false), stdlibInitialized(false),
          pthreadInitialized(false), debugEnabled(debug)
    {
    }

    void generateCode(ProgramNode* program);
    bool hadError() const
    {
        return hasError;
    }
    /// \brief Enable or disable test mode.
    ///
    /// When enabled, the compiler collects \c #[test] functions and generates
    /// a synthetic \c main() that invokes them (see generateTestMain()).
    ///
    /// \see \ref test_sample.mla — unit test example
    /// \see \ref mlang_attributes.mla — \c #[test] combined with \c #[derive(Debug)]
    /// \see \ref testing_mock_example.mla — mock-based testing with \c std::testing
    void setTestMode(bool enabled)
    {
        testMode = enabled;
    }

    /// \brief Control whether \c #[test] functions are compiled.
    ///
    /// When set to \c false (via \c --no-tests), test-attributed functions are
    /// excluded from code generation entirely.  Only meaningful when
    /// \c testMode is \c false; in test mode tests are always included.
    void setIncludeTests(bool enabled)
    {
        includeTests = enabled;
    }

    /// \brief Enable benchmark mode.
    ///
    /// When enabled the compiler generates a benchmark harness
    /// (see generateBenchmarkMain()) instead of the regular test harness.
    ///
    /// \see \ref bench_stdlib.mla — benchmark example
    void setBenchmarkMode(bool enabled)
    {
        benchmarkMode = enabled;
    }

    /// \brief Set the number of timed iterations for benchmarks.
    /// \param value  Positive iteration count (clamped to 1 minimum).
    void setBenchmarkIterations(int value)
    {
        benchmarkIterations = value > 0 ? value : 100000;
    }

    /// \brief Set the number of warm-up iterations before benchmark timing.
    /// \param value  Non-negative warm-up count (clamped to 0 minimum).
    void setBenchmarkWarmupIterations(int value)
    {
        benchmarkWarmupIterations = value >= 0 ? value : 10000;
    }

    /// \brief Set the source file path used for deriving the default test
    ///        suite name.
    void setSourceFile(const std::string& file)
    {
        sourceFileName = file;
    }

    /// \brief Enable or disable the warning for plain \c if/else-if with
    ///        colon syntax.
    void setWarnPlainColonIf(bool enabled)
    {
        warnPlainColonIf = enabled;
    }

    /// \brief Enable or disable the warning for plain \c while with colon
    ///        syntax.
    void setWarnPlainColonWhile(bool enabled)
    {
        warnPlainColonWhile = enabled;
    }

    /// \brief Enable or disable the warning for \c Result.unwrap() usage.
    void setWarnResultUnwrap(bool enabled)
    {
        warnResultUnwrap = enabled;
    }

    /// \brief Set a substring filter for test execution.
    ///
    /// When non-empty, only \c #[test] functions whose display name
    /// (\c suite.case) or raw function name contains \p filter as a
    /// substring will be included in the generated test harness.
    ///
    /// \param filter  Substring to match against test names.  An empty
    ///                string disables filtering (all tests run).
    ///
    /// \par Example
    /// \code
    ///   mlang test tests/test_sample.mla --filter "addition"
    /// \endcode
    ///
    /// \see \ref test_sample.mla — unit test example (good for trying \c --filter)
    void setTestFilter(const std::string& filter)
    {
        testFilter = filter;
    }

    /// \brief Attach the module loader used for resolving \c mod imports.
    void setModuleLoader(ModuleLoader* loader)
    {
        moduleLoader = loader;
    }

private:
    llvm::LLVMContext& context;
    llvm::IRBuilder<>& builder;
    std::unique_ptr<llvm::Module>& module;
    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, llvm::Value*> globalNamedValues;
    std::map<std::string, llvm::Type*> structTypes;
    struct StructFieldLayout
    {
        unsigned storageIndex = 0;
        bool packedBit = false;
        unsigned bitOffset = 0;
    };
    // Store struct member info: struct name -> vector of (member name, member
    // type)
    std::map<std::string, std::vector<std::pair<std::string, TypeNode*>>>
        structMembers;
    std::map<std::string, std::vector<StructFieldLayout>> structFieldLayouts;
    // Per-struct member default initializers (from `var x: T{expr};` /
    // `let x: T = expr;` field declarations). Missing entries mean the field
    // is zero-initialized at struct-literal construction.
    std::map<std::string, std::map<std::string, ExpressionNode*>>
        structMemberDefaults;
    // Track struct inheritance: derived struct name -> base struct name
    std::map<std::string, std::string> structBases;
    std::set<std::string> constantVariables;
    std::set<std::string> globalConstantVariables;
    std::set<std::string> movedVariables;
    // Inline closures: var inc = || { ... }; inc();
    // Maps closure variable name -> its AST node for inline body generation.
    std::map<std::string, ClosureNode*> closureVariables;
    // Re-entrancy guard: tracks closures currently being inlined.
    std::set<std::string> activeInlineClosures;
    std::map<std::string, std::string> pointerBorrowTarget;
    std::map<std::string, std::set<std::string>> activeBorrowers;
    // Tracks the single exclusive mutable borrower per owner variable.
    // Invariant: activeMutBorrower[owner] is set iff a &mut borrow is live;
    // activeBorrowers[owner] must be empty while this is set, and vice-versa.
    std::map<std::string, std::string> activeMutBorrower;
    std::map<std::string, int> variableScopeDepth;
    std::map<std::string, TypeNode::TypeKind> variableTypes;
    std::map<std::string, TypeNode::TypeKind> globalVariableTypes;
    // Track element types for generic lists and maps
    std::map<std::string, TypeNode*> listElementTypes;
    std::map<std::string, std::pair<TypeNode*, TypeNode*>> mapKeyValueTypes;
    // Track element types for pointers
    std::map<std::string, TypeNode*> pointerElementTypes;
    // Track tuple element types
    std::map<std::string, std::vector<TypeNode*>> tupleElementTypes;
    // Track struct variable types (var name -> struct type name)
    std::map<std::string, std::string> structVariableTypes;
    // Track enum variable types (var name -> enum type name)
    std::map<std::string, std::string> enumVariableTypes;
    std::map<std::string, std::string> globalStructVariableTypes;
    // Track enum variants: enum name -> variant name -> value
    std::map<std::string, std::map<std::string, int64_t>> enumValues;
    std::map<std::string, std::map<std::string, std::string>> enumStringValues;
    // Track enum variants in declaration order for iteration/printing.
    std::map<std::string, std::vector<std::pair<std::string, int64_t>>>
        enumVariantOrder;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>>
        enumStringVariantOrder;
    // Track enum backing integer kind per enum name.
    std::map<std::string, TypeNode::TypeKind> enumBaseTypes;
    std::set<std::string> debugStructs;
    bool hasError;
    bool debugEnabled;
    bool testMode = false;             ///< Compile and run \c #[test] functions.
    bool benchmarkMode = false;        ///< Generate benchmark harness instead of test harness.
    int benchmarkIterations = 100000;  ///< Number of timed benchmark iterations.
    int benchmarkWarmupIterations = 10000; ///< Number of warm-up iterations before timing.
    bool includeTests = true;          ///< Whether to compile \c #[test] functions at all.
    int unsafeDepth = 0;
    bool warnPlainColonIf = true;
    bool warnPlainColonWhile = true;
    bool warnResultUnwrap = true;
    std::string sourceFileName;        ///< Source file path for default suite name derivation.
    /// \brief Substring filter for selecting individual tests.
    ///
    /// Matched against both the display name (\c suite.case) and the raw
    /// function name.  Empty means no filtering (all tests run).
    std::string testFilter;

    // Visibility tracking for functions
    // Maps function signature key -> (isPublic, sourceModule)
    std::map<std::string, std::pair<bool, std::string>> functionVisibility;
    struct FunctionOverloadInfo
    {
        FunctionDefNode* node;
        llvm::Function* function;
        std::string signatureKey;
        std::string symbolName;
        bool isVarArg;
        bool isPublic;
        std::string sourceModule;
    };
    std::map<std::string, std::vector<FunctionOverloadInfo>> functionOverloads;
    std::vector<FunctionDefNode*> deferredModuleFunctionDefs;
    // Maps struct name -> (isPublic, sourceModule)
    std::map<std::string, std::pair<bool, std::string>> structVisibility;
    // Current module being compiled (empty string for main module)
    std::string currentModule;
    ModuleLoader* moduleLoader = nullptr;
    struct TypeAliasInfo
    {
        std::vector<std::string> typeParams;
        TypeNode* aliasedType = nullptr;
        int line = 0;
        int col = 0;
    };
    std::map<std::string, TypeAliasInfo> typeAliases;

    // Stdio function support
    bool stdioInitialized;
    llvm::FunctionCallee printfFunc;
    llvm::FunctionCallee fprintfFunc;
    llvm::FunctionCallee snprintfFunc;
    llvm::FunctionCallee strbufAlignFunc;
    llvm::Value* stderrPtr;
    // Stdlib support
    bool stdlibInitialized;
    llvm::FunctionCallee mallocFunc;
    llvm::FunctionCallee freeFunc;
    llvm::FunctionCallee strcmpFunc;
    llvm::FunctionCallee jsonEscapeFunc;
    llvm::FunctionCallee abortFunc;
    llvm::FunctionCallee exceptionsPushFrameFunc;
    llvm::FunctionCallee exceptionsFrameEnvFunc;
    llvm::FunctionCallee exceptionsSetjmpFunc;
    llvm::FunctionCallee exceptionsPopFrameFunc;
    llvm::FunctionCallee exceptionsThrowFunc;
    llvm::FunctionCallee exceptionsRethrowFunc;
    llvm::FunctionCallee exceptionsTakeTypeNameFunc;
    llvm::FunctionCallee exceptionsTakeMessageFunc;
    llvm::FunctionCallee exceptionsTakeSourceLineFunc;
    llvm::Value* currentFunctionExceptionFrame = nullptr;
    // Pthread support
    bool pthreadInitialized;
    llvm::FunctionCallee pthreadCreateFunc;
    llvm::FunctionCallee pthreadJoinFunc;
    llvm::FunctionCallee pthreadMutexInitFunc;
    llvm::FunctionCallee pthreadMutexDestroyFunc;
    llvm::FunctionCallee pthreadMutexLockFunc;
    llvm::FunctionCallee pthreadMutexUnlockFunc;
    llvm::FunctionCallee pthreadMutexAttrInitFunc;
    llvm::FunctionCallee pthreadMutexAttrSetTypeFunc;
    llvm::FunctionCallee pthreadMutexAttrDestroyFunc;

    // Loop control flow support (for break/continue)
    std::vector<llvm::BasicBlock*> loopBreakBlocks;
    std::vector<llvm::BasicBlock*> loopContinueBlocks;
    struct ScopeCleanup
    {
        enum class CallKind
        {
            ByValueFunction,
            ByPointerMethod
        };
        std::string varName;
        std::string structTypeName;
        llvm::Function* function = nullptr;
        CallKind callKind = CallKind::ByValueFunction;
    };
    std::vector<std::vector<ScopeCleanup>> cleanupScopes;
    struct PointerBorrowScopeEntry
    {
        std::string pointerVar;
        bool hadPreviousBorrow = false;
        std::string previousOwner;
    };
    std::vector<std::vector<PointerBorrowScopeEntry>> pointerBorrowScopes;
    struct VariableScopeDepthEntry
    {
        std::string varName;
        bool hadPreviousDepth = false;
        int previousDepth = 0;
    };
    std::vector<std::vector<VariableScopeDepthEntry>> variableScopeDepthScopes;

    // === GENERICS SUPPORT ===
    // Store generic struct templates: name -> StructDefNode*
    std::map<std::string, StructDefNode*> genericStructTemplates;
    // Store generic impl blocks: struct name -> vector of ImplBlockNode*
    std::map<std::string, std::vector<ImplBlockNode*>> genericImplBlocks;
    // Track which monomorphized types have been generated: mangled name -> true
    std::set<std::string> monomorphizedTypes;
    // Map mangled name back to original generic struct name
    std::map<std::string, std::string> mangledToGenericName;
    // Map mangled name to concrete type arguments used for monomorphization.
    std::map<std::string, std::vector<TypeNode*>> monomorphizedTypeArgs;
    // Active generic type bindings while generating a monomorphized method body.
    std::map<std::string, TypeNode*> activeTypeParamBindings;

    // Generics helper methods
    TypeNode* substituteTypeParams(TypeNode* type,
                                   const std::vector<std::string>& typeParams,
                                   const std::vector<TypeNode*>& typeArgs);
    void monomorphizeStruct(const std::string& genericName,
                            const std::vector<TypeNode*>& typeArgs,
                            const std::string& mangledName);
    void monomorphizeImplBlock(ImplBlockNode* impl,
                               const std::vector<std::string>& typeParams,
                               const std::vector<TypeNode*>& typeArgs,
                               const std::string& mangledStructName);
    std::string
    getOrCreateMonomorphizedStruct(const std::string& genericName,
                                   const std::vector<TypeNode*>& typeArgs);
    /// \brief Generate a synthetic \c main() that runs every collected
    ///        \c #[test] function and reports results.
    ///
    /// Tests are reported as \c suite.case where the suite name is derived
    /// from the source file and the case name from the function name.
    /// When \c testFilter is non-empty, only tests whose display name or
    /// raw function name contains the filter substring are emitted.
    ///
    /// \param tests  The list of \c #[test]-attributed functions to run.
    ///
    /// \see \ref test_sample.mla — unit test example
    /// \see \ref mlang_attributes.mla — \c #[test] with \c #[derive(Debug)]
    /// \see \ref testing_mock_example.mla — mock-based testing
    void generateTestMain(const std::vector<FunctionDefNode*>& tests);

    /// \brief Generate a synthetic \c main() that benchmarks every collected
    ///        \c #[test] function.
    ///
    /// Each function is first warmed up for \c benchmarkWarmupIterations
    /// iterations, then timed over \c benchmarkIterations iterations.
    ///
    /// \param tests  The list of \c #[test]-attributed functions to benchmark.
    ///
    /// \see \ref bench_stdlib.mla — benchmark example
    void generateBenchmarkMain(const std::vector<FunctionDefNode*>& tests);
    void buildTypeAliasTable(ProgramNode* program);
    void resolveTypeAliasesInProgram(ProgramNode* program);
    TypeNode* resolveTypeAliasNode(TypeNode* typeNode,
                                   const std::set<std::string>& typeParams,
                                   std::vector<std::string>& aliasStack);
    TypeNode* cloneTypeNode(TypeNode* typeNode);

    // Type helpers
    llvm::Type* getLLVMType(TypeNode::TypeKind kind);
    llvm::Type* getLLVMTypeFromNode(TypeNode* typeNode);
    bool isUnsignedType(TypeNode::TypeKind kind);
    bool isCopyType(TypeNode* typeNode);
    OwnershipClass classifyOwnership(TypeNode* typeNode);
    std::string ownershipClassName(TypeNode* typeNode);
    bool isMoveOnlyVariable(const std::string& name);
    bool isVariableMoved(const std::string& name) const;
    bool isVariableCurrentlyVisible(const std::string& name) const;
    bool validateVariableAccessible(const std::string& name, int line,
                                    int col = 0);
    void clearMovedVariable(const std::string& name);
    void clearPointerBorrow(const std::string& pointerVar);
    void registerPointerBorrow(const std::string& pointerVar,
                               ExpressionNode* expr, int line,
                               bool isMutable = false);
    bool isMutBorrower(const std::string& ptrVar) const;
    int currentScopeDepth() const;
    void recordVariableScopeDepth(const std::string& varName);
    void recordScopedPointerVariable(const std::string& pointerVar);
    std::string resolveBorrowOwnerFromLValue(ExpressionNode* expr) const;
    std::string getBorrowedOwnerForPointerExpression(
        ExpressionNode* expr) const;
    bool validatePointerDereference(ExpressionNode* pointerExpr, int line);
    bool evaluateCompileTimeInt(ExpressionNode* expr, int64_t& out);
    bool evaluateCompileTimeBool(ExpressionNode* expr, bool& out);
    bool convertValueToRuntimeBool(llvm::Value* value, int line,
                                   const std::string& context,
                                   llvm::Value*& outBool);
    bool validateNoEscapingBorrow(ExpressionNode* expr, int line,
                                  const std::string& action);
    void consumeMoveFromExpression(ExpressionNode* expr, int line,
                                   const std::string& context);
    llvm::StructType* getStructType(const std::string& name);
    const StructFieldLayout* getStructFieldLayout(const std::string& structName,
                                                  int fieldIndex) const;
    llvm::Value* loadStructFieldValue(const std::string& structTypeName,
                                      llvm::Value* structPtr, int fieldIndex,
                                      TypeNode* fieldType,
                                      const std::string& fieldName);
    void storeStructFieldValue(const std::string& structTypeName,
                               llvm::Value* structPtr, int fieldIndex,
                               TypeNode* fieldType, llvm::Value* value,
                               const std::string& fieldName, int line);
    std::string typeMangle(TypeNode* typeNode) const;
    std::string functionSignatureKey(FunctionDefNode* node) const;
    std::string functionSymbolName(FunctionDefNode* node) const;
    void registerFunctionOverload(FunctionDefNode* node,
                                  llvm::Function* function);
    bool isOverloadVisible(const FunctionOverloadInfo& info) const;
    bool canConvertType(llvm::Type* actualType, llvm::Type* expectedType,
                        int& cost) const;

    // Stdio initialization
    void initializeStdioFunctions();
    void initializeStdlibFunctions();
    void initializeFormatFunctions();
    void initializePthreadFunctions();
    llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* function,
                                             llvm::Type* type,
                                             const std::string& name);
    std::string convertFormatString(const std::string& mlaFormat,
                                    const std::vector<ExpressionNode*>& args,
                                    const std::vector<std::pair<std::string, ExpressionNode*>>& namedArgs,
                                    std::vector<llvm::Value*>& argValues,
                                    int line);
    std::string convertFormatString(const std::string& mlaFormat,
                                    const std::vector<ExpressionNode*>& args,
                                    std::vector<llvm::Value*>& argValues,
                                    int line);
    llvm::Value* buildAlignedString(llvm::Value* value,
                                    llvm::Value* widthValue, char align,
                                    int line);

    // Code generation for different node types
    llvm::Function* generateFunctionDeclaration(FunctionDefNode* node);
    llvm::Function* generateFunctionDefinition(FunctionDefNode* node);
    void generateStructDefinition(StructDefNode* node);
    void generateGlobalVarDeclaration(VarDeclNode* node);
    void seedFunctionScopeWithGlobals();

    void generateStatement(StatementNode* node);
    void generateReturnStatement(ReturnNode* node);
    void generateLetDeclaration(LetDeclNode* node);
    void generateVarDeclaration(VarDeclNode* node);
    void generateAssignment(AssignmentNode* node);
    void generateFieldAssignment(FieldAssignmentNode* node);
    void generateDerefAssignment(DerefAssignmentNode* node);
    void generateIfStatement(IfNode* node);
    void generateForStatement(ForNode* node);
    void generateWhileStatement(WhileNode* node);
    void generatePrintStatement(PrintNode* node);
    llvm::Value* generateFormatExpression(FormatNode* node);
    void generateAssert(AssertNode* node);
    void generateAssertEq(AssertEqNode* node);
    void generateStaticAssert(StaticAssertNode* node);
    void generateBreakStatement(BreakNode* node);
    void generateContinueStatement(ContinueNode* node);
    void generateThrowStatement(ThrowNode* node);
    void generateTryCatchStatement(TryCatchNode* node);
    void enterCleanupScope();
    void exitCleanupScope();
    void emitAllActiveCleanups();
    void emitActiveCleanupsDeeperThan(int scopeDepth);
    ScopeCleanup resolveDropFunctionForStruct(const std::string& structTypeName);
    void registerStructCleanupIfNeeded(const std::string& varName,
                                       const std::string& structTypeName);

    llvm::Value* generateExpression(ExpressionNode* node);
    llvm::Value* generateMatchExpression(MatchExpressionNode* node);
    llvm::Value* generateTernaryExpression(TernaryNode* node);
    llvm::Value* generateTryExpression(TryExpressionNode* node);
    llvm::Value* generateSizeofExpression(SizeofExpressionNode* node);
    /// \brief Lower an inline asm expression to LLVM inline asm.
    ///
    /// Current constraints are intentionally narrow so optimization behavior
    /// stays predictable:
    /// - operands must be integer or pointer values
    /// - result type must be integer, pointer, or `void`
    /// - non-`void` asm ties the first operand to the output register
    ///
    /// `asm volatile(...)` is emitted with LLVM `sideeffect`; plain `asm(...)`
    /// is emitted without it.
    llvm::Value* generateInlineAsm(InlineAsmNode* node);
    llvm::Value* generateBinaryOp(BinaryOpNode* node);
    llvm::Value* generateFoldExpression(FoldExpressionNode* node);
    llvm::Value* generateUnaryOp(UnaryOpNode* node);
    llvm::Value* generateUpdateExpression(UpdateExpressionNode* node);
    llvm::Value* generateIntLiteral(IntLiteralNode* node);
    llvm::Value* generateBoolLiteral(BoolLiteralNode* node);
    llvm::Value* generateFloatLiteral(FloatLiteralNode* node);
    llvm::Value* generateDoubleLiteral(DoubleLiteralNode* node);
    llvm::Value* generateStringLiteral(StringLiteralNode* node);
    llvm::Value* generateIdentifier(IdentifierNode* node);
    llvm::Value* generateEnumLiteral(EnumLiteralNode* node);
    llvm::Value* generateFieldAccess(FieldAccessNode* node);
    llvm::Value* generateFunctionCall(FunctionCallNode* node);
    llvm::Value* generateThreadSpawn(FunctionCallNode* node);
    llvm::Function* generateClosureFn(ClosureNode* node);
    llvm::Value* generateThreadJoin(FunctionCallNode* node);
    llvm::Value* buildHandleValue(const std::string& handleTypeName,
                                  llvm::Value* rawHandle, int line);
    llvm::Value* extractHandleValue(ExpressionNode* expr,
                                    const std::string& expectedHandleType,
                                    int line);
    llvm::Value* generateMutexCreate(FunctionCallNode* node);
    llvm::Value* generateMutexLock(FunctionCallNode* node);
    llvm::Value* generateMutexUnlock(FunctionCallNode* node);
    llvm::Value* generateMutexDestroy(FunctionCallNode* node);
    llvm::Value* generateAtomicI64New(FunctionCallNode* node);
    llvm::Value* generateAtomicI64Load(FunctionCallNode* node);
    llvm::Value* generateAtomicI64Store(FunctionCallNode* node);
    llvm::Value* generateAtomicI64Add(FunctionCallNode* node);
    llvm::Value* generateAtomicI64Free(FunctionCallNode* node);
    llvm::Value* generateMethodCall(MethodCallNode* node);
    llvm::Value* generateCastExpression(CastExpressionNode* node);
    llvm::Value* generateListLiteral(ListLiteralNode* node,
                                    llvm::Type* declaredElemType = nullptr);
    llvm::Value* generateArrayFill(ArrayFillNode* node,
                                   llvm::Type* declaredElemType = nullptr);
    llvm::Value* generateMapLiteral(MapLiteralNode* node);
    llvm::Value* generateIndexExpression(IndexExpressionNode* node);
    llvm::Value* generateTupleLiteral(TupleLiteralNode* node);
    llvm::Value* generateTupleAccess(TupleAccessNode* node);
    llvm::Value* generateStructLiteral(StructLiteralNode* node);
    llvm::Value* buildDebugString(ExpressionNode* expr, bool pretty, int line);
    llvm::Value* buildStructDebugString(llvm::Value* structVal,
                                        const std::string& structName,
                                        bool pretty, int line);
    llvm::Value* buildStructJsonString(llvm::Value* structVal,
                                       const std::string& structName,
                                       bool pretty, int line);
    bool isStringExpression(ExpressionNode* expr) const;
    std::string getStructTypeName(ExpressionNode* expr) const;
    std::string getEnumTypeName(ExpressionNode* expr, int line);
    std::string resolveVisibleEnumName(const std::string& enumName) const;
    bool structHasFieldNamed(const std::string& structTypeName,
                             const std::string& fieldName) const;
    std::string expressionTypeNameForLog(ExpressionNode* expr, int line);
    llvm::Value* buildEnumString(llvm::Value* enumVal,
                                 const std::string& enumName, int line);
    llvm::Value* getLValuePointer(ExpressionNode* expr, int line);
    TypeNode* getLValueType(ExpressionNode* expr, int line);
    TypeNode* getPointerElementType(ExpressionNode* expr, int line);
    llvm::Value* applyStructCopySemantics(llvm::Value* value);
    llvm::Value* applyStructCopySemantics(llvm::Value* value,
                                          TypeNode* semanticType);
    TypeNode* inferExpressionTypeNode(ExpressionNode* expr, int line);
    void appendFormatValue(ExpressionNode* expr, llvm::Value* value, bool debug,
                           bool pretty, bool json, std::string& cFormat,
                           std::vector<llvm::Value*>& argValues, int line);

    void generateEnumDefinition(EnumDefNode* node);
    void ensureResultBuiltin(ProgramNode* program);
    void ensureOptionBuiltin(ProgramNode* program);
    void ensureHandleBuiltin(ProgramNode* program);
    void ensureThreadBuiltin(ProgramNode* program);
    void ensureMutexBuiltin(ProgramNode* program);
    void ensureAtomic64Builtin(ProgramNode* program);

    // Struct method helpers
    void generateStructMethods(StructDefNode* node);
    llvm::Function* generateMethodDeclaration(const std::string& structName,
                                              StructMethodNode* method);
    llvm::Function* generateMethodDefinition(const std::string& structName,
                                             StructMethodNode* method);
    bool generateMutexPropertyMethodBody(const std::string& structName,
                                         StructMethodNode* method,
                                         llvm::Function* function);
    bool generateAtomicPropertyMethodBody(const std::string& structName,
                                          StructMethodNode* method,
                                          llvm::Function* function);
    llvm::Value* createInternalMutexHandle(bool recursive,
                                           const std::string& namePrefix);
    llvm::Value* ensurePropertyMutexHandle(llvm::Value* handleSlotPtr,
                                           bool recursive,
                                           const std::string& namePrefix);
    void destroyInternalMutexHandle(llvm::Value* rawHandle,
                                    const std::string& namePrefix);
    llvm::Value* resetCopiedStructState(llvm::Value* value,
                                        const std::string& structName);

    // Track struct method info: struct name -> method name -> (isPublic, method
    // node)
    std::map<std::string,
             std::map<std::string, std::pair<bool, StructMethodNode*>>>
        structMethods;
    // Track trait impls per concrete struct type name.
    std::map<std::string, std::set<std::string>> structImplementedTraits;

    // List/Map iteration helpers
    void generateForListLiteralIteration(ForNode* node,
                                         ListLiteralNode* listLit);
    void generateForListVariableIteration(ForNode* node,
                                          IdentifierNode* listId);
    void generateForEnumIteration(ForNode* node, const std::string& enumName);
    void generateForMapIteration(ForNode* node, MapIteratorNode* mapIter);

    // Collection type helpers
    llvm::StructType* getListStructType(llvm::Type* elementType);
    llvm::StructType* getMapStructType(llvm::Type* keyType,
                                       llvm::Type* valueType);

    // Field access helper for chained access (a.b.c)
    std::pair<llvm::Value*, std::string>
    getStructPtrAndType(ExpressionNode* expr, int line);

    void reportError(int line, const std::string& message);
    void reportError(int line, int col, const std::string& message);
    void reportWarning(int line, int col, const std::string& message);
};

class Backend
{
public:
    Backend(std::unique_ptr<llvm::Module>& m,
            const std::string& archOverride = "");
    bool emitObjectFile(const std::string& filename);
    bool emitAssemblyFile(const std::string& filename);
    bool emitLLVMIR(const std::string& filename);
    bool emitBitcode(const std::string& filename);
    bool compileToExecutable(const std::string& outputFile,
                             const std::vector<std::string>& linkArgs);
    void optimize(const std::string& level);
    std::string getTargetTriple() const
    {
        return targetTriple;
    }

private:
    std::unique_ptr<llvm::Module>& module;
    llvm::TargetMachine* targetMachine;
    std::string targetTriple;
    std::string targetArchOverride;

    bool initializeTarget();
    bool linkExecutable(const std::string& objectFile,
                        const std::string& outputFile,
                        const std::vector<std::string>& linkArgs);
};

#endif // IR_H
