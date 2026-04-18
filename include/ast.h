#ifndef AST_H
#define AST_H

#include <cstdint>
#include <string>
#include <vector>

// Forward declarations
class ASTNode;
class ExpressionNode;
class StatementNode;
class TypeNode;
class ParameterNode;
class ParameterListNode;
class FunctionDefNode;
class FunctionListNode;
class StatementListNode;
class ProgramNode;
class StructDefNode;
class StructListNode;
class TopLevelListNode;
class TypeParamListNode;
class ImplBlockNode;
class ImplListNode;
class StructLiteralNode;
class EnumDefNode;
class EnumListNode;
class EnumVariantNode;
class EnumVariantListNode;
class EnumLiteralNode;
class TypeAliasNode;

// Base AST node
class ASTNode
{
public:
    int line = 0;
    int col  = 0;
    virtual ~ASTNode() = default;
    virtual std::string toString() const = 0;
};

// Type node
class TypeNode : public ASTNode
{
public:
    enum TypeKind
    {
        TYPE_VOID,
        TYPE_BOOL,
        TYPE_BIT,
        TYPE_INT,
        TYPE_FLOAT,
        TYPE_DOUBLE,
        TYPE_STRING,
        TYPE_STR8,
        TYPE_STR16,
        TYPE_LIST,
        TYPE_MAP,
        TYPE_TUPLE,
        TYPE_PTR,
        TYPE_STRUCT,
        TYPE_I8,
        TYPE_I16,
        TYPE_I32,
        TYPE_I64,
        TYPE_U8,
        TYPE_U16,
        TYPE_U32,
        TYPE_U64,
        TYPE_REF,      // &T  -- immutable reference
        TYPE_REF_MUT   // &mut T -- mutable reference
    };

    TypeKind kind;

    TypeNode(TypeKind k) : kind(k) {}
    std::string toString() const override;
};

// Pointer type node: ptr<T>
class PointerTypeNode : public TypeNode
{
public:
    TypeNode* elementType;
    PointerTypeNode(TypeNode* elemType)
        : TypeNode(TYPE_PTR), elementType(elemType)
    {
    }
    std::string toString() const override;
};

// Reference type node: &T / &mut T
class ReferenceTypeNode : public TypeNode
{
public:
    TypeNode* elementType;
    bool      isMutable;   // true for &mut T
    ReferenceTypeNode(TypeNode* elemType, bool mut)
        : TypeNode(mut ? TYPE_REF_MUT : TYPE_REF),
          elementType(elemType), isMutable(mut)
    {
    }
    std::string toString() const override;
};

// List type node
class ListTypeNode : public TypeNode
{
public:
    ListTypeNode() : TypeNode(TYPE_LIST) {}
};

// Generic list type node with element type: list<T>
class GenericListTypeNode : public TypeNode
{
public:
    TypeNode* elementType;
    GenericListTypeNode(TypeNode* elemType)
        : TypeNode(TYPE_LIST), elementType(elemType)
    {
    }
    std::string toString() const override;
};

// Map type node: map<K, V>
class MapTypeNode : public TypeNode
{
public:
    TypeNode* keyType;
    TypeNode* valueType;
    MapTypeNode(TypeNode* kt, TypeNode* vt)
        : TypeNode(TYPE_MAP), keyType(kt), valueType(vt)
    {
    }
    std::string toString() const override;
};

// Type list node (for tuple types)
class TypeListNode : public ASTNode
{
public:
    std::vector<TypeNode*> types;
    void addType(TypeNode* t)
    {
        types.push_back(t);
    }
    std::string toString() const override;
};

// Tuple type node: tuple<T1, T2, ...>
class TupleTypeNode : public TypeNode
{
public:
    TypeListNode* elementTypes;
    TupleTypeNode(TypeListNode* types)
        : TypeNode(TYPE_TUPLE), elementTypes(types)
    {
    }
    std::string toString() const override;
};

// Struct type reference node: for using struct names as types
class StructTypeRefNode : public TypeNode
{
public:
    std::string structName;
    StructTypeRefNode(const std::string& name)
        : TypeNode(TYPE_STRUCT), structName(name)
    {
    }
    std::string toString() const override;
};

// Generic struct type reference node: for types like Pair<i32, i64>
class GenericStructTypeRefNode : public TypeNode
{
public:
    std::string structName;
    std::vector<TypeNode*> typeArgs; // The concrete type arguments

    GenericStructTypeRefNode(const std::string& name)
        : TypeNode(TYPE_STRUCT), structName(name)
    {
    }

    // Get the mangled name for this instantiation (e.g., "Pair_i32_i64")
    std::string getMangledName() const;

    std::string toString() const override;
};

// Expression nodes
class ExpressionNode : public ASTNode
{
};

class ArgumentListNode : public ASTNode
{
public:
    std::vector<ExpressionNode*> args;

    std::string toString() const override
    {
        return "ArgumentList";
    }
};

class FormatArgumentNode : public ASTNode
{
public:
    std::string name;
    ExpressionNode* value;

    FormatArgumentNode(ExpressionNode* v) : value(v) {}
    FormatArgumentNode(const std::string& n, ExpressionNode* v)
        : name(n), value(v)
    {
    }

    bool isNamed() const
    {
        return !name.empty();
    }

    std::string toString() const override;
};

class FormatArgumentListNode : public ASTNode
{
public:
    std::vector<FormatArgumentNode*> args;

    std::string toString() const override
    {
        return "FormatArgumentList";
    }
};

class IntLiteralNode : public ExpressionNode
{
public:
    int64_t value;
    IntLiteralNode(int64_t v) : value(v) {}
    std::string toString() const override;
};

class BoolLiteralNode : public ExpressionNode
{
public:
    bool value;
    BoolLiteralNode(bool v) : value(v) {}
    std::string toString() const override;
};

class FloatLiteralNode : public ExpressionNode
{
public:
    float value;
    FloatLiteralNode(float v) : value(v) {}
    std::string toString() const override;
};

class DoubleLiteralNode : public ExpressionNode
{
public:
    double value;
    DoubleLiteralNode(double v) : value(v) {}
    std::string toString() const override;
};

class StringLiteralNode : public ExpressionNode
{
public:
    std::string value;
    StringLiteralNode(const std::string& v) : value(v) {}
    std::string toString() const override;
};

class IdentifierNode : public ExpressionNode
{
public:
    std::string name;
    IdentifierNode(const std::string& n) : name(n) {}
    std::string toString() const override;
};

class BinaryOpNode : public ExpressionNode
{
public:
    enum OpType
    {
        OP_PLUS,
        OP_MINUS,
        OP_MULTIPLY,
        OP_DIVIDE,
        OP_MODULO,
        OP_LT,
        OP_GT,
        OP_LE,
        OP_GE,
        OP_EQ,
        OP_NE,
        OP_SPACESHIP,
        OP_BITAND,
        OP_BITOR,
        OP_BITXOR,
        OP_SHL,
        OP_SHR,
        OP_AND,
        OP_OR
    };

    OpType op;
    ExpressionNode* left;
    ExpressionNode* right;

    BinaryOpNode(OpType o, ExpressionNode* l, ExpressionNode* r)
        : op(o), left(l), right(r)
    {
    }
    std::string toString() const override;
};

// Fold expression node (C++-style shape over list values):
//   (... + xs)  /  (xs + ...)
class FoldExpressionNode : public ExpressionNode
{
public:
    BinaryOpNode::OpType op;
    ExpressionNode* packExpr;
    bool isRightFold;

    FoldExpressionNode(BinaryOpNode::OpType o, ExpressionNode* pack,
                       bool rightFold)
        : op(o), packExpr(pack), isRightFold(rightFold)
    {
    }
    std::string toString() const override;
};

class UnaryOpNode : public ExpressionNode
{
public:
    enum OpType
    {
        OP_NEG,
        OP_NOT,
        OP_BITNOT,
        OP_ADDR,
        OP_ADDR_MUT, // &mut x — exclusive mutable borrow
        OP_DEREF
    };

    OpType op;
    ExpressionNode* operand;

    UnaryOpNode(OpType o, ExpressionNode* value) : op(o), operand(value) {}
    std::string toString() const override;
};

class UpdateExpressionNode : public ExpressionNode
{
public:
    enum Kind
    {
        KIND_INCREMENT,
        KIND_DECREMENT
    };

    Kind kind;
    bool isPrefix;
    ExpressionNode* operand;

    UpdateExpressionNode(Kind k, bool prefix, ExpressionNode* expr)
        : kind(k), isPrefix(prefix), operand(expr)
    {
    }

    std::string toString() const override;
};

class TernaryNode : public ExpressionNode
{
public:
    ExpressionNode* condition;
    ExpressionNode* trueExpr;
    ExpressionNode* falseExpr;

    TernaryNode(ExpressionNode* cond, ExpressionNode* t, ExpressionNode* f)
        : condition(cond), trueExpr(t), falseExpr(f)
    {
    }
    std::string toString() const override;
};

class TryExpressionNode : public ExpressionNode
{
public:
    ExpressionNode* expression;

    TryExpressionNode(ExpressionNode* expr) : expression(expr) {}
    std::string toString() const override;
};

class SizeofExpressionNode : public ExpressionNode
{
public:
    TypeNode* typeTarget = nullptr;
    ExpressionNode* expressionTarget = nullptr;

    explicit SizeofExpressionNode(TypeNode* t) : typeTarget(t) {}
    explicit SizeofExpressionNode(ExpressionNode* e) : expressionTarget(e) {}
    std::string toString() const override;
};

/// \brief Inline assembly expression lowered directly to LLVM inline asm.
///
/// Supported first-version source forms are:
/// - `asm(T, "template", args...)`
/// - `asm volatile(T, "template", args...)`
/// - `asm aarch64(T, "template", args...)`
/// - `asm volatile x64(T, "template", args...)`
///
/// The result type is explicit in source so the frontend can preserve type
/// information through parsing and IR lowering. Non-`void` forms currently use
/// the first operand as the tied input/output register in code generation.
class InlineAsmNode : public ExpressionNode
{
public:
    /// \brief Declared result type for the asm expression, or `void`.
    TypeNode* resultType;
    /// \brief Raw assembler template string forwarded to LLVM.
    std::string asmTemplate;
    /// \brief Optional required target architecture (`x86`, `x64`, `aarch64`).
    std::string requiredArch;
    /// \brief Positional operands supplied by the user.
    std::vector<ExpressionNode*> arguments;
    /// \brief Whether LLVM should treat the asm block as side-effecting.
    bool isVolatile;

    InlineAsmNode(TypeNode* type, const std::string& text,
                  const std::string& arch, bool isVolatileAsm)
        : resultType(type), asmTemplate(text), requiredArch(arch),
          isVolatile(isVolatileAsm)
    {
    }
    std::string toString() const override;
};

class FunctionCallNode : public ExpressionNode
{
public:
    std::string name;
    std::vector<ExpressionNode*> arguments;

    FunctionCallNode(const std::string& n) : name(n) {}
    std::string toString() const override;
};

// Method call on a struct instance: obj.method(args)
class MethodCallNode : public ExpressionNode
{
public:
    ExpressionNode* object; // The object to call the method on
    std::string methodName;
    std::vector<ExpressionNode*> arguments;

    MethodCallNode(ExpressionNode* obj, const std::string& method)
        : object(obj), methodName(method)
    {
    }
    std::string toString() const override;
};

class MatchPatternNode : public ASTNode
{
public:
    enum PatternKind
    {
        PATTERN_OK,
        PATTERN_ERR,
        PATTERN_SOME,
        PATTERN_NONE,
        PATTERN_LITERAL,
        PATTERN_WILDCARD
    };

    PatternKind kind;
    std::string binding;
    ExpressionNode* literal = nullptr;

    MatchPatternNode(PatternKind k, const std::string& b,
                     ExpressionNode* lit = nullptr)
        : kind(k), binding(b), literal(lit)
    {
    }
    std::string toString() const override;
};

class MatchArmNode : public ASTNode
{
public:
    MatchPatternNode* pattern;
    ExpressionNode* expression;

    MatchArmNode(MatchPatternNode* p, ExpressionNode* e)
        : pattern(p), expression(e)
    {
    }
    std::string toString() const override;
};

class MatchArmListNode : public ASTNode
{
public:
    std::vector<MatchArmNode*> arms;

    void addArm(MatchArmNode* arm)
    {
        arms.push_back(arm);
    }
    std::string toString() const override;
};

class MatchExpressionNode : public ExpressionNode
{
public:
    ExpressionNode* target;
    std::vector<MatchArmNode*> arms;

    MatchExpressionNode(ExpressionNode* t, MatchArmListNode* list)
        : target(t)
    {
        if(list)
            arms = list->arms;
    }
    std::string toString() const override;
};

class EnumLiteralNode : public ExpressionNode
{
public:
    std::string enumName;
    std::string variantName;

    EnumLiteralNode(const std::string& e, const std::string& v)
        : enumName(e), variantName(v)
    {
    }
    std::string toString() const override;
};

class CastExpressionNode : public ExpressionNode
{
public:
    TypeNode::TypeKind targetType;
    ExpressionNode* expression;

    CastExpressionNode(TypeNode::TypeKind t, ExpressionNode* e)
        : targetType(t), expression(e)
    {
    }
    std::string toString() const override;
};

class RangeExpressionNode : public ExpressionNode
{
public:
    ExpressionNode* start;
    ExpressionNode* end;
    bool inclusive;

    RangeExpressionNode(ExpressionNode* s, ExpressionNode* e, bool incl)
        : start(s), end(e), inclusive(incl)
    {
    }
    std::string toString() const override;
};

class ListElementsNode : public ASTNode
{
public:
    std::vector<ExpressionNode*> elements;
    void addElement(ExpressionNode* e)
    {
        elements.push_back(e);
    }
    std::string toString() const override;
};

class ListLiteralNode : public ExpressionNode
{
public:
    ListElementsNode* elements;
    ListLiteralNode(ListElementsNode* e) : elements(e) {}
    std::string toString() const override;
};

// Map entry node (key: value pair)
class MapEntryNode : public ASTNode
{
public:
    ExpressionNode* key;
    ExpressionNode* value;
    MapEntryNode(ExpressionNode* k, ExpressionNode* v) : key(k), value(v) {}
    std::string toString() const override;
};

// Map entries list node
class MapEntriesNode : public ASTNode
{
public:
    std::vector<MapEntryNode*> entries;
    void addEntry(MapEntryNode* e)
    {
        entries.push_back(e);
    }
    std::string toString() const override;
};

// Map literal node: {k1: v1, k2: v2, ...}
class MapLiteralNode : public ExpressionNode
{
public:
    MapEntriesNode* entries;
    MapLiteralNode(MapEntriesNode* e) : entries(e) {}
    std::string toString() const override;
};

// Closure literal: || { body }
class ClosureNode : public ExpressionNode
{
public:
    ParameterListNode* parameters; // optional parameters for |x: T| { ... }
    StatementListNode* body; // may be null for empty body
    ClosureNode(ParameterListNode* p, StatementListNode* b)
        : parameters(p), body(b)
    {
    }
    std::string toString() const override;
};

// Array fill literal: [val; N]
// Semantics: a list of N copies of val
class ArrayFillNode : public ExpressionNode
{
public:
    ExpressionNode* value; // the fill value
    ExpressionNode* count; // number of elements
    ArrayFillNode(ExpressionNode* v, ExpressionNode* c) : value(v), count(c) {}
    std::string toString() const override;
};

// Index expression node: arr[index] or map[key]
class IndexExpressionNode : public ExpressionNode
{
public:
    ExpressionNode* base;
    ExpressionNode* index;
    IndexExpressionNode(ExpressionNode* b, ExpressionNode* i)
        : base(b), index(i)
    {
    }
    std::string toString() const override;
};

// Tuple literal node: (expr1, expr2, ...)
class TupleLiteralNode : public ExpressionNode
{
public:
    ListElementsNode* elements;
    TupleLiteralNode(ListElementsNode* e) : elements(e) {}
    std::string toString() const override;
};

// Tuple access node: tuple.0, tuple.1, etc.
class TupleAccessNode : public ExpressionNode
{
public:
    ExpressionNode* tuple;
    int index;
    TupleAccessNode(ExpressionNode* t, int i) : tuple(t), index(i) {}
    std::string toString() const override;
};

// Map iterator node: map.keys(), map.values(), map.entries()
class MapIteratorNode : public ExpressionNode
{
public:
    enum IteratorKind
    {
        ITER_KEYS,
        ITER_VALUES,
        ITER_ENTRIES
    };

    ExpressionNode* mapExpr;
    IteratorKind kind;

    MapIteratorNode(ExpressionNode* m, IteratorKind k) : mapExpr(m), kind(k) {}
    std::string toString() const override;
};

// Statement nodes
class StatementNode : public ASTNode
{
};

class StatementListNode : public ASTNode
{
public:
    std::vector<StatementNode*> statements;
    std::string toString() const override;
};

class BlockStatementNode : public StatementNode
{
public:
    StatementListNode* statements;
    bool isUnsafe = false;
    BlockStatementNode(StatementListNode* s) : statements(s) {}
    std::string toString() const override;
};

class DerefAssignmentNode : public StatementNode
{
public:
    ExpressionNode* pointerExpr;
    ExpressionNode* value;
    DerefAssignmentNode(ExpressionNode* p, ExpressionNode* v)
        : pointerExpr(p), value(v)
    {
    }
    std::string toString() const override;
};

class ExpressionStatementNode : public StatementNode
{
public:
    ExpressionNode* expression;
    ExpressionStatementNode(ExpressionNode* e) : expression(e) {}
    std::string toString() const override;
};

class ReturnNode : public StatementNode
{
public:
    ExpressionNode* expression;
    ReturnNode(ExpressionNode* e) : expression(e) {}
    std::string toString() const override;
};

class BreakNode : public StatementNode
{
public:
    BreakNode() = default;
    std::string toString() const override;
};

class ContinueNode : public StatementNode
{
public:
    ContinueNode() = default;
    std::string toString() const override;
};

class ThrowNode : public StatementNode
{
public:
    ExpressionNode* expression;
    ThrowNode(ExpressionNode* e) : expression(e) {}
    std::string toString() const override;
};

class LetDeclNode : public StatementNode
{
public:
    TypeNode* type;
    std::string name;
    ExpressionNode* expression;

    LetDeclNode(TypeNode* t, const std::string& n, ExpressionNode* e)
        : type(t), name(n), expression(e)
    {
    }
    std::string toString() const override;
};

class VarDeclNode : public StatementNode
{
public:
    TypeNode* type;
    std::string name;
    ExpressionNode* initExpr;
    bool isStaticStorage = false;
    bool isGlobalStorage = false;

    VarDeclNode(TypeNode* t, const std::string& n, ExpressionNode* e)
        : type(t), name(n), initExpr(e)
    {
    }
    std::string toString() const override;
};

class AssignmentNode : public StatementNode
{
public:
    std::string name;
    ExpressionNode* expression;

    AssignmentNode(const std::string& n, ExpressionNode* e)
        : name(n), expression(e)
    {
    }
    std::string toString() const override;
};

// Struct field access: struct_var.field_name
class FieldAccessNode : public ExpressionNode
{
public:
    std::string
        structName; // Used when object is nullptr (simple case: var.field)
    std::string fieldName;
    ExpressionNode*
        object; // For chained access (a.b.c), nullptr for simple case

    FieldAccessNode(const std::string& s, const std::string& f)
        : structName(s), fieldName(f), object(nullptr)
    {
    }

    FieldAccessNode(ExpressionNode* obj, const std::string& f)
        : structName(""), fieldName(f), object(obj)
    {
    }

    std::string toString() const override;
};

// Struct field assignment: struct_var.field_name = expr
class FieldAssignmentNode : public StatementNode
{
public:
    std::string structName; // Used when target is nullptr (simple case)
    std::string fieldName;
    ExpressionNode* expression;
    ExpressionNode*
        target; // For chained assignment (a.b.c = x), nullptr for simple case

    FieldAssignmentNode(const std::string& s, const std::string& f,
                        ExpressionNode* e)
        : structName(s), fieldName(f), expression(e), target(nullptr)
    {
    }

    // Constructor for chained assignment
    FieldAssignmentNode(ExpressionNode* t, ExpressionNode* e)
        : structName(""), fieldName(""), expression(e), target(t)
    {
    }

    std::string toString() const override;
};

class IfNode : public StatementNode
{
public:
    StatementNode* conditionInit;
    ExpressionNode* condition;
    StatementListNode* thenBranch;
    IfNode* elseIfBranch;
    StatementListNode* elseBranch;
    bool usesColonWithoutGuard;

    IfNode(StatementNode* ci, ExpressionNode* c, StatementListNode* t,
           IfNode* ei = nullptr, StatementListNode* e = nullptr,
           bool usesColonNoGuard = false)
        : conditionInit(ci), condition(c), thenBranch(t), elseIfBranch(ei),
          elseBranch(e), usesColonWithoutGuard(usesColonNoGuard)
    {
    }
    std::string toString() const override;
};

class ForNode : public StatementNode
{
public:
    std::string varName;
    std::string indexVarName; // non-empty for "for (i, x) in ..." enumerate style
    ExpressionNode* iterable;
    StatementListNode* body;

    ForNode(const std::string& v, ExpressionNode* it, StatementListNode* b,
            const std::string& idxVar = "")
        : varName(v), indexVarName(idxVar), iterable(it), body(b)
    {
    }
    std::string toString() const override;
};

class WhileNode : public StatementNode
{
public:
    ExpressionNode* condition;
    StatementListNode* body;
    bool usesColonWithoutGuard;

    WhileNode(ExpressionNode* c, StatementListNode* b,
              bool usesColonNoGuard = false)
        : condition(c), body(b), usesColonWithoutGuard(usesColonNoGuard)
    {
    }
    std::string toString() const override;
};

class TryCatchNode : public StatementNode
{
public:
    BlockStatementNode* tryBlock;
    std::string catchName;
    TypeNode* catchType;
    BlockStatementNode* catchBlock;

    TryCatchNode(BlockStatementNode* t, const std::string& name, TypeNode* type,
                 BlockStatementNode* c)
        : tryBlock(t), catchName(name), catchType(type), catchBlock(c)
    {
    }
    std::string toString() const override;
};

class PrintNode : public StatementNode
{
public:
    enum PrintKind
    {
        PRINT_STDOUT,
        PRINTLN_STDOUT,
        PRINT_STDERR,
        EPRINTLN_STDERR
    };

    PrintKind kind;
    std::string formatString;
    std::vector<ExpressionNode*> arguments;
    std::vector<std::pair<std::string, ExpressionNode*>> namedArguments;
    bool debugOnly = false;

    PrintNode(PrintKind k, const std::string& fmt, bool debug = false)
        : kind(k), formatString(fmt), debugOnly(debug)
    {
    }
    void addArgument(ExpressionNode* arg)
    {
        arguments.push_back(arg);
    }
    void addNamedArgument(const std::string& name, ExpressionNode* arg)
    {
        namedArguments.push_back({name, arg});
    }
    std::string toString() const override;
};

class FormatNode : public ExpressionNode
{
public:
    std::string formatString;
    std::vector<ExpressionNode*> arguments;
    std::vector<std::pair<std::string, ExpressionNode*>> namedArguments;

    FormatNode(const std::string& fmt) : formatString(fmt) {}
    void addArgument(ExpressionNode* arg)
    {
        arguments.push_back(arg);
    }
    void addNamedArgument(const std::string& name, ExpressionNode* arg)
    {
        namedArguments.push_back({name, arg});
    }
    std::string toString() const override;
};

class AssertEqNode : public StatementNode
{
public:
    ExpressionNode* left;
    ExpressionNode* right;

    AssertEqNode(ExpressionNode* l, ExpressionNode* r) : left(l), right(r) {}
    std::string toString() const override;
};

class AssertNode : public StatementNode
{
public:
    ExpressionNode* condition;

    AssertNode(ExpressionNode* c) : condition(c) {}
    std::string toString() const override;
};

class StaticAssertNode : public StatementNode
{
public:
    ExpressionNode* condition;

    StaticAssertNode(ExpressionNode* c) : condition(c) {}
    std::string toString() const override;
};

enum PropertyFlags
{
    PROPERTY_FLAG_NONE = 0,
    PROPERTY_FLAG_ATOMIC = 1 << 0,
    PROPERTY_FLAG_MUTEX = 1 << 1,
    PROPERTY_FLAG_RECURSIVE = 1 << 2,
};

// Struct-related nodes
class StructMemberNode : public ASTNode
{
public:
    bool isVar;
    TypeNode* type;
    std::string name;
    ExpressionNode* initExpr;
    bool isProperty = false;  // @property
    bool isReadonly = false;  // #[property(readonly)] — suppress setter
    bool isAtomicProperty = false; // @property(atomic)
    bool isMutexProperty = false; // @property(mutex)
    bool isRecursiveProperty = false; // @property(mutex, recursive)
    bool isSynthesizedPropertyStorage = false;
    std::string propertyLockFieldName;

    StructMemberNode(bool v, TypeNode* t, const std::string& n,
                     ExpressionNode* e)
        : isVar(v), type(t), name(n), initExpr(e)
    {
    }
    std::string toString() const override;
};

// Forward declaration
class StatementListNode;
class ParameterListNode;

// Struct method node - a function defined inside a struct
class StructMethodNode : public ASTNode
{
public:
    TypeNode* returnType;
    std::string name;
    ParameterListNode* parameters;
    StatementListNode* body;
    bool isPublic;
    bool isStatic; // static methods don't have 'self' parameter
    bool isSynthesizedPropertyAccessor = false;
    bool isAtomicPropertyAccessor = false;
    bool isMutexPropertyAccessor = false;
    bool isRecursiveMutexPropertyAccessor = false;
    bool isPropertySetter = false;
    std::string propertyFieldName;
    std::string propertyLockFieldName;

    StructMethodNode(TypeNode* rt, const std::string& n, ParameterListNode* p,
                     StatementListNode* b, bool pub = false, bool stat = false)
        : returnType(rt), name(n), parameters(p), body(b), isPublic(pub),
          isStatic(stat)
    {
    }
    std::string toString() const override;
};

class StructMemberListNode : public ASTNode
{
public:
    std::vector<StructMemberNode*> members;
    std::vector<StructMethodNode*> methods;
    std::vector<EnumDefNode*> enums;

    void addMember(StructMemberNode* m)
    {
        members.push_back(m);
    }
    void addMethod(StructMethodNode* m)
    {
        methods.push_back(m);
    }
    void addEnum(EnumDefNode* e)
    {
        enums.push_back(e);
    }
    std::string toString() const override;
};

class StructDefNode : public ASTNode
{
public:
    std::string name;
    std::string baseName;
    std::string debugDisplayName;
    StructMemberListNode* members;
    bool isPublic;
    bool deriveDebug = false;
    std::string sourceModule; // Module this struct was defined in (for
                              // visibility checks)
    std::vector<std::string>
        typeParams; // Generic type parameters like T, U, etc.

    StructDefNode(const std::string& n, const std::string& b,
                  StructMemberListNode* m, bool pub = false,
                  bool derive = false)
        : name(n), baseName(b), members(m), isPublic(pub),
          deriveDebug(derive)
    {
    }
    std::string toString() const override;

    // Get all methods including inherited ones
    std::vector<StructMethodNode*> getAllMethods() const;

    // Check if this is a generic struct
    bool isGeneric() const
    {
        return !typeParams.empty();
    }
};

class EnumVariantNode : public ASTNode
{
public:
    std::string name;
    bool hasExplicitValue = false;
    int64_t explicitValue = 0;
    bool hasExplicitStringValue = false;
    std::string explicitStringValue;
    bool hasReferenceValue = false;
    std::string refEnumName;
    std::string refVariantName;

    EnumVariantNode(const std::string& n, bool hasValue = false,
                    int64_t value = 0)
        : name(n), hasExplicitValue(hasValue), explicitValue(value)
    {
    }
    EnumVariantNode(const std::string& n, const std::string& stringValue)
        : name(n), hasExplicitStringValue(true),
          explicitStringValue(stringValue)
    {
    }
    std::string toString() const override;
};

class EnumVariantListNode : public ASTNode
{
public:
    std::vector<EnumVariantNode*> variants;

    void addVariant(EnumVariantNode* v)
    {
        variants.push_back(v);
    }
    std::string toString() const override;
};

class EnumDefNode : public ASTNode
{
public:
    std::string name;
    EnumVariantListNode* variants;
    bool isPublic;
    TypeNode::TypeKind backingType;
    std::string sourceModule;

    EnumDefNode(const std::string& n, EnumVariantListNode* v, bool pub = false,
                TypeNode::TypeKind backing = TypeNode::TYPE_I32)
        : name(n), variants(v), isPublic(pub), backingType(backing)
    {
    }
    std::string toString() const override;
};

class EnumListNode : public ASTNode
{
public:
    std::vector<EnumDefNode*> enums;
    void addEnum(EnumDefNode* e)
    {
        enums.push_back(e);
    }
    std::string toString() const override;
};

// Type parameter list node for generic types
class TypeParamListNode : public ASTNode
{
public:
    std::vector<std::string> params;

    void addParam(const std::string& p)
    {
        params.push_back(p);
    }
    std::string toString() const override;
};

class TraitDefNode : public ASTNode
{
public:
    std::string name;
    std::string sourceModule;

    TraitDefNode(const std::string& n) : name(n) {}
    std::string toString() const override;
};

// Impl block for adding methods to a struct
class ImplBlockNode : public ASTNode
{
public:
    std::string structName;
    std::string traitName; // Empty when this is an inherent impl
    std::vector<std::string> typeParams; // Generic type parameters
    std::vector<StructMethodNode*> methods;

    ImplBlockNode(const std::string& name) : structName(name) {}
    std::string toString() const override;
};

// List of impl blocks
class ImplListNode : public ASTNode
{
public:
    std::vector<ImplBlockNode*> impls;

    void addImpl(ImplBlockNode* impl)
    {
        impls.push_back(impl);
    }
    std::string toString() const override;
};

// Struct literal expression: StructName { field1: value1, field2: value2 }
class StructLiteralNode : public ExpressionNode
{
public:
    std::string structName;
    std::string displayTypeName;
    std::vector<std::string> typeArgs; // For generic types like Box<int>
    std::vector<std::pair<std::string, ExpressionNode*>> fields;

    StructLiteralNode(const std::string& name) : structName(name) {}

    void addField(const std::string& fieldName, ExpressionNode* value)
    {
        fields.push_back({fieldName, value});
    }
    std::string toString() const override;
};

class StructListNode : public ASTNode
{
public:
    std::vector<StructDefNode*> structs;
    void addStruct(StructDefNode* s)
    {
        structs.push_back(s);
    }
    std::string toString() const override;
};

class StructInitNode : public StatementNode
{
public:
    std::string typeName;
    std::string varName;

    StructInitNode(const std::string& t, const std::string& v)
        : typeName(t), varName(v)
    {
    }
    std::string toString() const override;
};

// Function-related nodes
class ParameterNode : public ASTNode
{
public:
    TypeNode* type;
    std::string name;

    ParameterNode(TypeNode* t, const std::string& n) : type(t), name(n) {}
    std::string toString() const override;
};

class ParameterListNode : public ASTNode
{
public:
    std::vector<ParameterNode*> parameters;
    bool isVarArg = false;
    std::string toString() const override;
};

class FunctionDefNode : public ASTNode
{
public:
    TypeNode* returnType;
    std::string name;
    ParameterListNode* parameters;
    StatementListNode* body;
    bool isPublic;
    bool isExtern;
    bool isTest = false;
    bool isInline = false;
    bool isInlineAlways = false;
    bool isInlineNever = false;
    std::string sourceModule; // Module this function was defined in (for
                              // visibility checks)

    FunctionDefNode(TypeNode* rt, const std::string& n, ParameterListNode* p,
                    StatementListNode* b, bool pub = false, bool ext = false)
        : returnType(rt),
          name(n),
          parameters(p),
          body(b),
          isPublic(pub),
          isExtern(ext)
    {
    }
    std::string toString() const override;
};

class FunctionListNode : public ASTNode
{
public:
    std::vector<FunctionDefNode*> functions;
    std::string toString() const override;
};

// Module-related nodes
class ModDeclNode : public ASTNode
{
public:
    std::string moduleName;
    std::string filePath;

    ModDeclNode(const std::string& n) : moduleName(n) {}
    std::string toString() const override;
};

class UseDeclNode : public ASTNode
{
public:
    std::string moduleName;
    std::string itemName;
    std::string aliasName;
    bool importAll;
    bool moduleAlias;

    UseDeclNode(const std::string& m, const std::string& i, bool all,
                const std::string& alias = "", bool isModuleAlias = false)
        : moduleName(m), itemName(i), aliasName(alias), importAll(all),
          moduleAlias(isModuleAlias)
    {
    }
    std::string toString() const override;
};

class TypeAliasNode : public StatementNode
{
public:
    std::string name;
    std::vector<std::string> typeParams;
    TypeNode* aliasedType;

    TypeAliasNode(const std::string& n, TypeNode* t)
        : name(n), aliasedType(t)
    {
    }
    std::string toString() const override;
};

// Top-level nodes
class TopLevelListNode : public ASTNode
{
public:
    std::vector<ASTNode*> items;
    std::string toString() const override;
};

class ProgramNode : public ASTNode
{
public:
    StructListNode* structList = nullptr;
    FunctionListNode* functionList = nullptr;
    ImplListNode* implList = nullptr;
    EnumListNode* enumList = nullptr;
    std::vector<TraitDefNode*> traitDefs;
    std::vector<ModDeclNode*> modules;
    std::vector<UseDeclNode*> imports;
    std::vector<VarDeclNode*> globalVars;
    std::vector<TypeAliasNode*> typeAliases;

    std::string toString() const override;
};

#ifdef __cplusplus
extern "C" {
#endif
// Helper function declarations for parser
ASTNode* create_program(ASTNode* top_level_list);
ASTNode* create_top_level_list(ASTNode* item);
ASTNode* add_to_top_level_list(ASTNode* list, ASTNode* item);
ASTNode* create_struct_list(ASTNode* struct_def);
ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def);
ASTNode* create_function_list(ASTNode* function);
ASTNode* add_function_to_list(ASTNode* list, ASTNode* function);
ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params,
                             ASTNode* body, int is_public, int is_extern);
ASTNode* create_type_node(TypeNode::TypeKind type);
ASTNode* create_pointer_type(ASTNode* element_type);
ASTNode* create_reference_type(ASTNode* element_type, int is_mutable);
ASTNode* create_parameter_list(ASTNode* param);
ASTNode* create_empty_parameter_list();
ASTNode* set_parameter_list_vararg(ASTNode* list);
ASTNode* create_parameter(ASTNode* type, char* name);
ASTNode* add_parameter(ASTNode* list, ASTNode* param);
ASTNode* create_statement_list(ASTNode* stmt);
ASTNode* create_empty_statement_list();
ASTNode* add_statement(ASTNode* list, ASTNode* stmt);
ASTNode* create_assignment(char* name, ASTNode* expr, int line);
ASTNode* create_deref_assignment(ASTNode* pointer_expr, ASTNode* expr,
                                 int line);
ASTNode* create_return_stmt(ASTNode* expr);
ASTNode* create_break_stmt(int line);
ASTNode* create_continue_stmt(int line);
ASTNode* create_throw_stmt(ASTNode* expr, int line);
ASTNode* create_int_literal(int64_t value);
ASTNode* create_bool_literal(int value);
ASTNode* create_float_literal(float value);
ASTNode* create_double_literal(double value);
ASTNode* create_string_literal(char* value);
ASTNode* create_identifier(char* name);
ASTNode* create_identifier_line(char* name, int line);
ASTNode* create_identifier_at(char* name, int line, int col);
ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right);
ASTNode* create_fold_expression(int op, ASTNode* pack_expr, int is_right_fold);
ASTNode* create_unary_op(int op, ASTNode* operand);
ASTNode* create_ternary_expression(ASTNode* cond, ASTNode* t, ASTNode* f,
                                   int line);
ASTNode* create_try_expression(ASTNode* expr, int line);
ASTNode* create_sizeof_type_expression(ASTNode* type, int line);
ASTNode* create_sizeof_value_expression(ASTNode* expr, int line);
ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2,
                              int line);
ASTNode* create_function_call_multi(char* name, ASTNode* args, int line);
ASTNode* create_result_constructor(char* variant, ASTNode* type_args,
                                   ASTNode* args, int line);
ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch,
                             ASTNode* else_if_branch, ASTNode* else_branch);
ASTNode* create_if_statement_with_init(ASTNode* condition_init,
                                       ASTNode* condition,
                                       ASTNode* then_branch,
                                       ASTNode* else_if_branch,
                                       ASTNode* else_branch);
ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr);
ASTNode* create_cast_expression(int type, ASTNode* expr);
ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members,
                           int is_public, int derive_debug);
ASTNode* create_struct_member_list(ASTNode* member);
ASTNode* add_struct_member(ASTNode* list, ASTNode* member);
ASTNode* create_struct_member(int is_var, ASTNode* type, char* name,
                              ASTNode* init_expr);
ASTNode* create_struct_method(ASTNode* type, char* name, ASTNode* params,
                              ASTNode* body, int is_public, int is_static);
ASTNode* add_struct_method(ASTNode* list, ASTNode* method);
ASTNode* create_struct_init(char* type_name, char* var_name);
ASTNode* create_method_call_expr(ASTNode* object, char* method_name,
                                 ASTNode* args, int line);
ASTNode* create_method_call(ASTNode* object, char* method, ASTNode* args, int line);
ASTNode* create_list_type();
ASTNode* create_list_literal(ASTNode* elements);
ASTNode* create_list_element_list(ASTNode* element);
ASTNode* add_list_element(ASTNode* list, ASTNode* element);
ASTNode* create_array_fill(ASTNode* value, ASTNode* count);
ASTNode* create_expression_statement(ASTNode* expr);
ASTNode* create_block_statement(ASTNode* stmt_list);
ASTNode* create_else_if(ASTNode* condition, ASTNode* body);
ASTNode* create_else_if_with_init(ASTNode* condition_init, ASTNode* condition,
                                  ASTNode* body);
ASTNode* add_else_if(ASTNode* else_if_list, ASTNode* else_if);
ASTNode* create_for_range(char* var_name, ASTNode* range, ASTNode* body,
                          int line);
ASTNode* create_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body,
                             int line);
ASTNode* create_for_enumerate(char* index_var, char* val_var,
                             ASTNode* iterable, ASTNode* body, int line);
ASTNode* create_while_statement(ASTNode* condition, ASTNode* body, int line,
                                int uses_colon_without_guard);
ASTNode* create_try_catch_stmt(ASTNode* try_block, char* catch_name,
                               ASTNode* catch_type, ASTNode* catch_block,
                               int line);
ASTNode* create_range_expression(ASTNode* start, ASTNode* end, int inclusive);
ASTNode* create_mod_declaration(char* name, int line);
ASTNode* create_use_declaration(char* module_name, char* item_name, int line);
ASTNode* create_use_declaration_alias(char* module_name, char* item_name,
                                      char* alias_name, int line);
ASTNode* create_use_module_alias_declaration(char* module_name,
                                             char* alias_name, int line);
ASTNode* create_use_all_declaration(char* module_name, int line);
ASTNode* create_type_alias(char* name, ASTNode* type_params,
                           ASTNode* aliased_type);
ASTNode* create_print_stmt(int kind, char* format_str, ASTNode* args, int line);
ASTNode* create_debug_print_stmt(char* format_str, ASTNode* args, int line);
ASTNode* create_print_expr_stmt(int kind, ASTNode* expr, int line);
ASTNode* create_argument_list(ASTNode* arg);
ASTNode* add_argument(ASTNode* list, ASTNode* arg);
ASTNode* create_format_argument(char* name, ASTNode* value);
ASTNode* create_format_argument_list(ASTNode* arg);
ASTNode* add_format_argument(ASTNode* list, ASTNode* arg);
ASTNode* create_format_expr(char* format_str, ASTNode* args, int line);
ASTNode* create_assert_eq(ASTNode* left, ASTNode* right, int line);
ASTNode* create_generic_list_type(ASTNode* element_type);
ASTNode* create_map_type(ASTNode* key_type, ASTNode* value_type);
ASTNode* create_map_literal(ASTNode* entries);
ASTNode* create_map_entry_list(ASTNode* entry);
ASTNode* add_map_entry(ASTNode* list, ASTNode* entry);
ASTNode* create_map_entry(ASTNode* key, ASTNode* value);
ASTNode* create_index_expression(ASTNode* base, ASTNode* index, int line);
ASTNode* create_tuple_type(ASTNode* type_list);
ASTNode* create_type_list(ASTNode* type);
ASTNode* add_type_to_list(ASTNode* list, ASTNode* type);
ASTNode* create_tuple_literal(ASTNode* elements);
ASTNode* create_tuple_access(ASTNode* tuple, int index, int line);
ASTNode* create_map_keys_iterator(ASTNode* map_expr, int line);
ASTNode* create_map_values_iterator(ASTNode* map_expr, int line);
ASTNode* create_map_entries_iterator(ASTNode* map_expr, int line);
ASTNode* create_struct_type_ref(char* name);
ASTNode* create_field_access(char* struct_name, char* field_name, int line);
ASTNode* create_field_access_expr(ASTNode* object, char* field_name, int line);
ASTNode* create_field_assignment(char* struct_name, char* field_name,
                                 ASTNode* expr, int line);
ASTNode* create_chained_field_assignment(ASTNode* target, ASTNode* expr,
                                         int line);
ASTNode* create_match_pattern(char* name, char* binding, int line);
ASTNode* create_len_expression(ASTNode* expr, int line);
ASTNode* create_match_literal_pattern(ASTNode* literal, int line);
ASTNode* create_match_arm(ASTNode* pattern, ASTNode* expr, int line);
ASTNode* create_match_arm_list(ASTNode* arm);
ASTNode* add_match_arm(ASTNode* list, ASTNode* arm);
ASTNode* create_match_expression(ASTNode* target, ASTNode* arms, int line);
ASTNode* create_enum_def(char* name, ASTNode* variants, int is_public,
                         int backing_type = TypeNode::TYPE_I32);
ASTNode* create_enum_variant(char* name, int has_explicit_value = 0,
                             long long explicit_value = 0);
ASTNode* create_enum_variant_string(char* name, char* explicit_string_value);
ASTNode* create_enum_variant_ref(char* name, char* ref_enum_name,
                                 char* ref_variant_name);
ASTNode* create_enum_variant_list(ASTNode* variant);
ASTNode* add_enum_variant(ASTNode* list, ASTNode* variant);
ASTNode* create_enum_literal(char* enum_name, char* variant_name, int line);
ASTNode* create_closure(ASTNode* body);
ASTNode* create_closure_with_params(ASTNode* params, ASTNode* body);

// Generic structs and impl blocks
ASTNode* create_type_param_list(char* param);
ASTNode* add_type_param(ASTNode* list, char* param);
ASTNode* create_generic_struct_def(char* name, char* base_name,
                                   ASTNode* type_params, ASTNode* members,
                                   int is_public, int derive_debug);
ASTNode* create_trait_def(char* name, int line);
ASTNode* create_impl_block(char* struct_name, ASTNode* type_params,
                           char* trait_name = nullptr);
ASTNode* add_impl_method(ASTNode* impl, ASTNode* method);
ASTNode* create_struct_literal(char* struct_name, ASTNode* type_args,
                               ASTNode* fields, int line);
ASTNode* create_struct_field_init_list(char* field_name, ASTNode* value);
ASTNode* add_struct_field_init(ASTNode* list, char* field_name, ASTNode* value);
ASTNode* create_generic_struct_type_ref(char* name, ASTNode* type_args);
#ifdef __cplusplus
}
#endif

#endif // AST_H
