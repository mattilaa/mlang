#include "ast.h"
#include "parser.hpp"
#include <cstring>
#include <stdexcept>

ASTNode* create_program(ASTNode* top_level_list)
{
    auto program = new ProgramNode();
    auto* list = static_cast<TopLevelListNode*>(top_level_list);

    for(auto* item : list->items)
    {
        if(auto* structDef = dynamic_cast<StructDefNode*>(item))
        {
            if(!program->structList)
            {
                program->structList = new StructListNode();
            }
            program->structList->addStruct(structDef);
        }
        else if(auto* funcDef = dynamic_cast<FunctionDefNode*>(item))
        {
            if(!program->functionList)
            {
                program->functionList = new FunctionListNode();
            }
            program->functionList->functions.push_back(funcDef);
        }
        else if(auto* modDecl = dynamic_cast<ModDeclNode*>(item))
        {
            program->modules.push_back(modDecl);
        }
        else if(auto* useDecl = dynamic_cast<UseDeclNode*>(item))
        {
            program->imports.push_back(useDecl);
        }
        else if(auto* implBlock = dynamic_cast<ImplBlockNode*>(item))
        {
            if(!program->implList)
            {
                program->implList = new ImplListNode();
            }
            program->implList->addImpl(implBlock);
        }
        else if(auto* enumDef = dynamic_cast<EnumDefNode*>(item))
        {
            if(!program->enumList)
            {
                program->enumList = new EnumListNode();
            }
            program->enumList->addEnum(enumDef);
        }
    }

    return program;
}

ASTNode* create_top_level_list(ASTNode* item)
{
    auto list = new TopLevelListNode();
    list->items.push_back(item);
    return list;
}

ASTNode* add_to_top_level_list(ASTNode* list, ASTNode* item)
{
    auto topLevelList = static_cast<TopLevelListNode*>(list);
    topLevelList->items.push_back(item);
    return topLevelList;
}

ASTNode* create_function_list(ASTNode* function)
{
    auto list = new FunctionListNode();
    list->functions.push_back(static_cast<FunctionDefNode*>(function));
    return list;
}

ASTNode* add_function_to_list(ASTNode* list, ASTNode* function)
{
    auto func_list = static_cast<FunctionListNode*>(list);
    func_list->functions.push_back(static_cast<FunctionDefNode*>(function));
    return func_list;
}

ASTNode* create_function_def(ASTNode* type, char* name, ASTNode* params,
                             ASTNode* body, int is_public, int is_extern)
{
    return new FunctionDefNode(static_cast<TypeNode*>(type), std::string(name),
                               static_cast<ParameterListNode*>(params),
                               static_cast<StatementListNode*>(body),
                               is_public != 0,
                               is_extern != 0);
}

ASTNode* create_type_node(TypeNode::TypeKind type)
{
    return new TypeNode(type);
}

ASTNode* create_empty_parameter_list()
{
    return new ParameterListNode();
}

ASTNode* create_parameter_list(ASTNode* param)
{
    auto list = new ParameterListNode();
    if(param)
    {
        list->parameters.push_back(static_cast<ParameterNode*>(param));
    }
    return list;
}

ASTNode* create_parameter(ASTNode* type, char* name)
{
    return new ParameterNode(static_cast<TypeNode*>(type), std::string(name));
}

ASTNode* set_parameter_list_vararg(ASTNode* list)
{
    auto param_list = static_cast<ParameterListNode*>(list);
    if(param_list)
        param_list->isVarArg = true;
    return param_list;
}

ASTNode* add_parameter(ASTNode* list, ASTNode* param)
{
    auto param_list = static_cast<ParameterListNode*>(list);
    param_list->parameters.push_back(static_cast<ParameterNode*>(param));
    return param_list;
}

ASTNode* create_statement_list(ASTNode* stmt)
{
    auto list = new StatementListNode();
    list->statements.push_back(static_cast<StatementNode*>(stmt));
    return list;
}

ASTNode* add_statement(ASTNode* list, ASTNode* stmt)
{
    auto stmt_list = static_cast<StatementListNode*>(list);
    stmt_list->statements.push_back(static_cast<StatementNode*>(stmt));
    return stmt_list;
}

ASTNode* create_assignment(char* name, ASTNode* expr, int line)
{
    auto* node = new AssignmentNode(std::string(name),
                                    static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_deref_assignment(ASTNode* pointer_expr, ASTNode* expr, int line)
{
    auto* node = new DerefAssignmentNode(
        static_cast<ExpressionNode*>(pointer_expr),
        static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_return_stmt(ASTNode* expr)
{
    return new ReturnNode(static_cast<ExpressionNode*>(expr));
}

ASTNode* create_struct_init(char* type_name, char* var_name)
{
    return new StructInitNode(std::string(type_name), std::string(var_name));
}

ASTNode* create_int_literal(int64_t value)
{
    return new IntLiteralNode(value);
}

ASTNode* create_bool_literal(int value)
{
    return new BoolLiteralNode(value != 0);
}

ASTNode* create_float_literal(float value)
{
    return new FloatLiteralNode(value);
}

ASTNode* create_double_literal(double value)
{
    return new DoubleLiteralNode(value);
}

ASTNode* create_string_literal(char* value)
{
    return new StringLiteralNode(std::string(value));
}

ASTNode* create_identifier(char* name)
{
    return new IdentifierNode(std::string(name));
}

ASTNode* create_binary_op(int op, ASTNode* left, ASTNode* right)
{
    BinaryOpNode::OpType opType;
    switch(op)
    {
    case PLUS:
        opType = BinaryOpNode::OP_PLUS;
        break;
    case MINUS:
        opType = BinaryOpNode::OP_MINUS;
        break;
    case MULTIPLY:
        opType = BinaryOpNode::OP_MULTIPLY;
        break;
    case DIVIDE:
        opType = BinaryOpNode::OP_DIVIDE;
        break;
    case LT:
        opType = BinaryOpNode::OP_LT;
        break;
    case GT:
        opType = BinaryOpNode::OP_GT;
        break;
    case LE:
        opType = BinaryOpNode::OP_LE;
        break;
    case GE:
        opType = BinaryOpNode::OP_GE;
        break;
    case EQ:
        opType = BinaryOpNode::OP_EQ;
        break;
    case NE:
        opType = BinaryOpNode::OP_NE;
        break;
    case COLON:
        return nullptr;
    default:
        throw std::runtime_error("Unknown binary operator");
    }
    return new BinaryOpNode(opType, static_cast<ExpressionNode*>(left),
                            static_cast<ExpressionNode*>(right));
}

ASTNode* create_unary_op(int op, ASTNode* operand)
{
    UnaryOpNode::OpType opType;
    switch(op)
    {
    case MINUS:
        opType = UnaryOpNode::OP_NEG;
        break;
    case AMP:
        opType = UnaryOpNode::OP_ADDR;
        break;
    case MULTIPLY:
        opType = UnaryOpNode::OP_DEREF;
        break;
    default:
        throw std::runtime_error("Unknown unary operator");
    }
    return new UnaryOpNode(opType, static_cast<ExpressionNode*>(operand));
}

ASTNode* create_ternary_expression(ASTNode* cond, ASTNode* t, ASTNode* f,
                                   int line)
{
    auto* node = new TernaryNode(static_cast<ExpressionNode*>(cond),
                                 static_cast<ExpressionNode*>(t),
                                 static_cast<ExpressionNode*>(f));
    node->line = line;
    return node;
}

ASTNode* create_try_expression(ASTNode* expr, int line)
{
    auto* node = new TryExpressionNode(static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2,
                              int line)
{
    auto call = new FunctionCallNode(std::string(name));
    call->line = line;
    if(arg1)
        call->arguments.push_back(static_cast<ExpressionNode*>(arg1));
    if(arg2)
        call->arguments.push_back(static_cast<ExpressionNode*>(arg2));
    return call;
}

// Helper class to store argument list temporarily
class ArgumentListNode : public ASTNode
{
public:
    std::vector<ExpressionNode*> args;
    std::string toString() const override
    {
        return "ArgumentList";
    }
};

ASTNode* create_argument_list(ASTNode* arg)
{
    auto list = new ArgumentListNode();
    if(arg)
    {
        list->args.push_back(static_cast<ExpressionNode*>(arg));
    }
    return list;
}

ASTNode* add_argument(ASTNode* list, ASTNode* arg)
{
    auto argList = static_cast<ArgumentListNode*>(list);
    if(arg)
    {
        argList->args.push_back(static_cast<ExpressionNode*>(arg));
    }
    return argList;
}

ASTNode* create_function_call_multi(char* name, ASTNode* args, int line)
{
    auto call = new FunctionCallNode(std::string(name));
    call->line = line;
    if(args)
    {
        auto argList = static_cast<ArgumentListNode*>(args);
        call->arguments = argList->args;
    }
    return call;
}

ASTNode* create_result_constructor(char* variant, ASTNode* type_args,
                                   ASTNode* args, int line)
{
    std::string variantStr = variant ? variant : "";
    bool isResult = (variantStr == "Ok" || variantStr == "Err");
    bool isOption = (variantStr == "Some" || variantStr == "None");
    if(!isResult && !isOption)
    {
        fprintf(stderr, "Error (line %d): generic function calls are not "
                        "supported\n",
                line);
        return create_function_call_multi(variant, args, line);
    }

    ExpressionNode* valueExpr = nullptr;
    if(args)
    {
        auto* argList = static_cast<ArgumentListNode*>(args);
        if(isOption && variantStr == "None")
        {
            fprintf(stderr,
                    "Error (line %d): %s expects zero arguments\n",
                    line, variantStr.c_str());
        }
        else if(argList->args.size() == 1)
        {
            valueExpr = argList->args[0];
        }
        else
        {
            fprintf(stderr, "Error (line %d): %s expects %s arguments\n", line,
                    variantStr.c_str(),
                    isOption && variantStr == "None" ? "zero" : "one");
        }
    }
    else
    {
        if(!(isOption && variantStr == "None"))
        {
            fprintf(stderr,
                    "Error (line %d): %s expects one argument\n",
                    line, variantStr.c_str());
        }
    }

    const char* flagField = isResult ? "is_ok" : "is_some";
    ASTNode* fields = create_struct_field_init_list(
        strdup(flagField), create_bool_literal(variantStr == "Ok" ||
                                               variantStr == "Some"));

    if(valueExpr)
    {
        if(variantStr == "Ok")
        {
            fields =
                add_struct_field_init(fields, strdup("ok"), valueExpr);
        }
        else if(variantStr == "Err")
        {
            fields =
                add_struct_field_init(fields, strdup("err"), valueExpr);
        }
        else if(variantStr == "Some")
        {
            fields =
                add_struct_field_init(fields, strdup("value"), valueExpr);
        }
    }

    return create_struct_literal(strdup(isResult ? "Result" : "Option"),
                                 type_args, fields, line);
}

ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch,
                             ASTNode* else_if_branch, ASTNode* else_branch)
{
    auto* thenBlock = dynamic_cast<BlockStatementNode*>(then_branch);
    auto* elseBlock = dynamic_cast<BlockStatementNode*>(else_branch);
    StatementListNode* thenList =
        thenBlock ? thenBlock->statements
                  : dynamic_cast<StatementListNode*>(then_branch);
    StatementListNode* elseList =
        elseBlock ? elseBlock->statements
                  : dynamic_cast<StatementListNode*>(else_branch);

    return new IfNode(static_cast<ExpressionNode*>(condition), thenList,
                      static_cast<IfNode*>(else_if_branch), elseList);
}

ASTNode* create_let_declaration(ASTNode* type, char* name, ASTNode* expr)
{
    return new LetDeclNode(static_cast<TypeNode*>(type), std::string(name),
                           static_cast<ExpressionNode*>(expr));
}

ASTNode* create_var_declaration(ASTNode* type, char* name, ASTNode* expr)
{
    return new VarDeclNode(static_cast<TypeNode*>(type), std::string(name),
                           static_cast<ExpressionNode*>(expr));
}

ASTNode* create_cast_expression(int type, ASTNode* expr)
{
    TypeNode::TypeKind targetType;
    switch(type)
    {
    case TypeNode::TYPE_INT:
        targetType = TypeNode::TYPE_INT;
        break;
    case TypeNode::TYPE_FLOAT:
        targetType = TypeNode::TYPE_FLOAT;
        break;
    case TypeNode::TYPE_DOUBLE:
        targetType = TypeNode::TYPE_DOUBLE;
        break;
    default:
        throw std::runtime_error("Unsupported cast type");
    }
    return new CastExpressionNode(targetType,
                                  static_cast<ExpressionNode*>(expr));
}

ASTNode* create_struct_list(ASTNode* struct_def)
{
    auto list = new StructListNode();
    list->addStruct(static_cast<StructDefNode*>(struct_def));
    return list;
}

ASTNode* add_struct_to_list(ASTNode* list, ASTNode* struct_def)
{
    auto structList = static_cast<StructListNode*>(list);
    structList->addStruct(static_cast<StructDefNode*>(struct_def));
    return structList;
}

ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members,
                           int is_public, int derive_debug)
{
    return new StructDefNode(
        std::string(name), base_name ? std::string(base_name) : "",
        static_cast<StructMemberListNode*>(members), is_public != 0,
        derive_debug != 0);
}

ASTNode* create_enum_def(char* name, ASTNode* variants, int is_public)
{
    return new EnumDefNode(std::string(name),
                           static_cast<EnumVariantListNode*>(variants),
                           is_public != 0);
}

ASTNode* create_enum_variant(char* name)
{
    return new EnumVariantNode(std::string(name));
}

ASTNode* create_enum_variant_list(ASTNode* variant)
{
    auto* list = new EnumVariantListNode();
    if(variant)
        list->addVariant(static_cast<EnumVariantNode*>(variant));
    return list;
}

ASTNode* add_enum_variant(ASTNode* list, ASTNode* variant)
{
    auto* variantList = static_cast<EnumVariantListNode*>(list);
    variantList->addVariant(static_cast<EnumVariantNode*>(variant));
    return variantList;
}

ASTNode* create_struct_member_list(ASTNode* member)
{
    auto list = new StructMemberListNode();
    // Check if it's a member or method
    if(auto* memberNode = dynamic_cast<StructMemberNode*>(member))
    {
        list->addMember(memberNode);
    }
    else if(auto* methodNode = dynamic_cast<StructMethodNode*>(member))
    {
        list->addMethod(methodNode);
    }
    return list;
}

ASTNode* add_struct_member(ASTNode* list, ASTNode* member)
{
    auto memberList = static_cast<StructMemberListNode*>(list);
    if(auto* memberNode = dynamic_cast<StructMemberNode*>(member))
    {
        memberList->addMember(memberNode);
    }
    else if(auto* methodNode = dynamic_cast<StructMethodNode*>(member))
    {
        memberList->addMethod(methodNode);
    }
    return memberList;
}

ASTNode* create_struct_member(int is_var, ASTNode* type, char* name,
                              ASTNode* init_expr)
{
    return new StructMemberNode(is_var != 0, static_cast<TypeNode*>(type),
                                std::string(name),
                                static_cast<ExpressionNode*>(init_expr));
}

ASTNode* create_struct_method(ASTNode* type, char* name, ASTNode* params,
                              ASTNode* body, int is_public, int is_static)
{
    auto* p = static_cast<ParameterListNode*>(params);
    bool staticMethod = (is_static != 0);
    // If no explicit self parameter is present, treat as static-style method
    // callable via Type::method(...).
    if(!staticMethod)
    {
        if(!p || p->parameters.empty() || p->parameters[0]->name != "self")
            staticMethod = true;
    }

    return new StructMethodNode(static_cast<TypeNode*>(type), std::string(name),
                                p,
                                static_cast<StatementListNode*>(body),
                                is_public != 0, staticMethod);
}

ASTNode* add_struct_method(ASTNode* list, ASTNode* method)
{
    auto memberList = static_cast<StructMemberListNode*>(list);
    memberList->addMethod(static_cast<StructMethodNode*>(method));
    return memberList;
}

ASTNode* create_method_call_expr(ASTNode* object, char* method_name,
                                 ASTNode* args, int line)
{
    auto* node = new MethodCallNode(static_cast<ExpressionNode*>(object),
                                    std::string(method_name));
    node->line = line;
    if(args)
    {
        // ArgumentListNode is defined locally in ast.cpp
        class ArgumentListNode : public ASTNode
        {
        public:
            std::vector<ExpressionNode*> args;
            std::string toString() const override
            {
                return "ArgumentList";
            }
        };
        auto* argList = static_cast<ArgumentListNode*>(args);
        node->arguments = argList->args;
    }
    return node;
}

ASTNode* create_match_pattern(char* name, char* binding, int line)
{
    std::string nameStr = name ? name : "";
    std::string bindStr = binding ? binding : "";

    MatchPatternNode::PatternKind kind = MatchPatternNode::PATTERN_WILDCARD;
    if(nameStr == "Ok")
        kind = MatchPatternNode::PATTERN_OK;
    else if(nameStr == "Err")
        kind = MatchPatternNode::PATTERN_ERR;
    else if(nameStr == "Some")
        kind = MatchPatternNode::PATTERN_SOME;
    else if(nameStr == "None")
        kind = MatchPatternNode::PATTERN_NONE;
    else if(nameStr == "_")
        kind = MatchPatternNode::PATTERN_WILDCARD;
    else
    {
        fprintf(stderr,
                "Error (line %d): unknown match pattern '%s'\n",
                line, nameStr.c_str());
        kind = MatchPatternNode::PATTERN_WILDCARD;
    }

    if(kind == MatchPatternNode::PATTERN_NONE && !bindStr.empty())
    {
        fprintf(stderr,
                "Error (line %d): None pattern cannot bind a value\n", line);
        bindStr.clear();
    }

    return new MatchPatternNode(kind, bindStr);
}

ASTNode* create_match_literal_pattern(ASTNode* literal, int line)
{
    auto* lit = static_cast<ExpressionNode*>(literal);
    auto* node =
        new MatchPatternNode(MatchPatternNode::PATTERN_LITERAL, "", lit);
    node->line = line;
    return node;
}

ASTNode* create_match_arm(ASTNode* pattern, ASTNode* expr, int line)
{
    auto* node = new MatchArmNode(static_cast<MatchPatternNode*>(pattern),
                                  static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_match_arm_list(ASTNode* arm)
{
    auto* list = new MatchArmListNode();
    if(arm)
        list->addArm(static_cast<MatchArmNode*>(arm));
    return list;
}

ASTNode* add_match_arm(ASTNode* list, ASTNode* arm)
{
    auto* armList = static_cast<MatchArmListNode*>(list);
    armList->addArm(static_cast<MatchArmNode*>(arm));
    return armList;
}

ASTNode* create_match_expression(ASTNode* target, ASTNode* arms, int line)
{
    auto* node = new MatchExpressionNode(static_cast<ExpressionNode*>(target),
                                         static_cast<MatchArmListNode*>(arms));
    node->line = line;
    return node;
}

ASTNode* create_enum_literal(char* enum_name, char* variant_name, int line)
{
    auto* node =
        new EnumLiteralNode(std::string(enum_name), std::string(variant_name));
    node->line = line;
    return node;
}

ASTNode* create_list_type()
{
    return new ListTypeNode();
}

ASTNode* create_pointer_type(ASTNode* element_type)
{
    return new PointerTypeNode(static_cast<TypeNode*>(element_type));
}

ASTNode* create_list_literal(ASTNode* elements)
{
    return new ListLiteralNode(static_cast<ListElementsNode*>(elements));
}

ASTNode* create_list_element_list(ASTNode* element)
{
    auto list = new ListElementsNode();
    list->addElement(static_cast<ExpressionNode*>(element));
    return list;
}

ASTNode* add_list_element(ASTNode* list, ASTNode* element)
{
    auto elemList = static_cast<ListElementsNode*>(list);
    elemList->addElement(static_cast<ExpressionNode*>(element));
    return elemList;
}

ASTNode* create_expression_statement(ASTNode* expr)
{
    return new ExpressionStatementNode(static_cast<ExpressionNode*>(expr));
}

ASTNode* create_block_statement(ASTNode* stmt_list)
{
    return new BlockStatementNode(static_cast<StatementListNode*>(stmt_list));
}

ASTNode* create_else_if(ASTNode* condition, ASTNode* body)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList =
        blockBody ? blockBody->statements
                  : dynamic_cast<StatementListNode*>(body);

    return new IfNode(static_cast<ExpressionNode*>(condition), stmtList);
}

ASTNode* add_else_if(ASTNode* else_if_list, ASTNode* else_if)
{
    if(!else_if_list)
    {
        return else_if;
    }

    // Chain else-if nodes
    IfNode* current = static_cast<IfNode*>(else_if_list);
    while(current->elseIfBranch)
    {
        current = current->elseIfBranch;
    }
    current->elseIfBranch = static_cast<IfNode*>(else_if);

    return else_if_list;
}

// Print statement creation
ASTNode* create_print_stmt(int kind, char* format_str, ASTNode* args, int line)
{
    PrintNode::PrintKind printKind;
    switch(kind)
    {
    case 0:
        printKind = PrintNode::PRINT_STDOUT;
        break;
    case 1:
        printKind = PrintNode::PRINTLN_STDOUT;
        break;
    case 2:
        printKind = PrintNode::PRINT_STDERR;
        break;
    case 3:
        printKind = PrintNode::EPRINTLN_STDERR;
        break;
    default:
        printKind = PrintNode::PRINTLN_STDOUT;
    }

    auto* node = new PrintNode(printKind, std::string(format_str), false);
    node->line = line;

    if(args)
    {
        auto* argList = static_cast<ArgumentListNode*>(args);
        for(auto* arg : argList->args)
        {
            node->addArgument(arg);
        }
    }

    return node;
}

ASTNode* create_debug_print_stmt(char* format_str, ASTNode* args, int line)
{
    auto* node =
        new PrintNode(PrintNode::EPRINTLN_STDERR, std::string(format_str), true);
    node->line = line;

    if(args)
    {
        auto* argList = static_cast<ArgumentListNode*>(args);
        for(auto* arg : argList->args)
        {
            node->addArgument(arg);
        }
    }

    return node;
}

ASTNode* create_print_expr_stmt(int kind, ASTNode* expr, int line)
{
    auto* node = new PrintNode(static_cast<PrintNode::PrintKind>(kind), "{}", false);
    node->line = line;
    if(expr)
        node->addArgument(static_cast<ExpressionNode*>(expr));
    return node;
}

ASTNode* create_format_expr(char* format_str, ASTNode* args, int line)
{
    auto* node = new FormatNode(std::string(format_str));
    node->line = line;

    if(args)
    {
        auto* argList = static_cast<ArgumentListNode*>(args);
        for(auto* arg : argList->args)
        {
            node->addArgument(arg);
        }
    }

    return node;
}

ASTNode* create_assert_eq(ASTNode* left, ASTNode* right, int line)
{
    auto* node = new AssertEqNode(static_cast<ExpressionNode*>(left),
                                  static_cast<ExpressionNode*>(right));
    node->line = line;
    return node;
}

// toString() implementations

std::string TypeNode::toString() const
{
    switch(kind)
    {
    case TYPE_VOID:
        return "void";
    case TYPE_BOOL:
        return "bool";
    case TYPE_INT:
        return "int";
    case TYPE_FLOAT:
        return "float";
    case TYPE_DOUBLE:
        return "double";
    case TYPE_STRING:
        return "string";
    case TYPE_STR8:
        return "str8";
    case TYPE_STR16:
        return "str16";
    case TYPE_LIST:
        return "list";
    case TYPE_MAP:
        return "map";
    case TYPE_TUPLE:
        return "tuple";
    case TYPE_PTR:
        return "ptr";
    case TYPE_STRUCT:
        return "struct";
    case TYPE_I8:
        return "i8";
    case TYPE_I16:
        return "i16";
    case TYPE_I32:
        return "i32";
    case TYPE_I64:
        return "i64";
    case TYPE_U8:
        return "u8";
    case TYPE_U16:
        return "u16";
    case TYPE_U32:
        return "u32";
    case TYPE_U64:
        return "u64";
    default:
        return "unknown";
    }
}

std::string PointerTypeNode::toString() const
{
    return "ptr<" + (elementType ? elementType->toString() : "void") + ">";
}

std::string ParameterNode::toString() const
{
    return name + ": " + type->toString();
}

std::string ParameterListNode::toString() const
{
    std::string result;
    for(size_t i = 0; i < parameters.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += parameters[i]->toString();
    }
    if(isVarArg)
    {
        if(!result.empty())
            result += ", ";
        result += "...";
    }
    return result;
}

std::string FunctionDefNode::toString() const
{
    std::string result;
    if(isExtern)
    {
        result += "extern ";
    }
    result += isPublic ? "pub fn " : "fn ";
    result += name + "(" + parameters->toString() + ") -> ";
    result += returnType->toString();
    if(isExtern)
    {
        result += ";\n";
        return result;
    }
    result += " {\n";
    result += body ? body->toString() : "";
    result += "}\n";
    return result;
}

std::string FunctionListNode::toString() const
{
    std::string result;
    for(const auto& func : functions)
    {
        result += func->toString() + "\n";
    }
    return result;
}

std::string StatementListNode::toString() const
{
    std::string result;
    for(const auto& stmt : statements)
    {
        result += "    " + stmt->toString() + "\n";
    }
    return result;
}

std::string AssignmentNode::toString() const
{
    return name + " = " + expression->toString() + ";";
}

std::string DerefAssignmentNode::toString() const
{
    return "*" + pointerExpr->toString() + " = " + value->toString() + ";";
}

std::string FieldAccessNode::toString() const
{
    if(object)
    {
        return object->toString() + "." + fieldName;
    }
    return structName + "." + fieldName;
}

std::string FieldAssignmentNode::toString() const
{
    if(target)
    {
        return target->toString() + " = " + expression->toString() + ";";
    }
    return structName + "." + fieldName + " = " + expression->toString() + ";";
}

ASTNode* create_field_access(char* struct_name, char* field_name, int line)
{
    auto* node =
        new FieldAccessNode(std::string(struct_name), std::string(field_name));
    node->line = line;
    return node;
}

ASTNode* create_field_access_expr(ASTNode* object, char* field_name, int line)
{
    // For simple identifier, use the string-based constructor for backwards
    // compatibility
    if(auto* id = dynamic_cast<IdentifierNode*>(object))
    {
        auto* node = new FieldAccessNode(id->name, std::string(field_name));
        node->line = line;
        return node;
    }

    // For chained access (e.g., a.b.c), use the expression-based constructor
    auto* node = new FieldAccessNode(static_cast<ExpressionNode*>(object),
                                     std::string(field_name));
    node->line = line;
    return node;
}

ASTNode* create_field_assignment(char* struct_name, char* field_name,
                                 ASTNode* expr, int line)
{
    auto* node = new FieldAssignmentNode(std::string(struct_name),
                                         std::string(field_name),
                                         static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_chained_field_assignment(ASTNode* target, ASTNode* expr,
                                         int line)
{
    // Check if target is a simple field access (a.b)
    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(target))
    {
        // If it's a simple field access (no chaining), use the string-based
        // version
        if(!fieldAccess->object && !fieldAccess->structName.empty())
        {
            auto* node = new FieldAssignmentNode(
                fieldAccess->structName, fieldAccess->fieldName,
                static_cast<ExpressionNode*>(expr));
            node->line = line;
            return node;
        }
    }

    // For chained access or other expressions, use the expression-based version
    auto* node = new FieldAssignmentNode(static_cast<ExpressionNode*>(target),
                                         static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

std::string ReturnNode::toString() const
{
    if(expression)
        return "return " + expression->toString() + ";";
    return "return;";
}

std::string IntLiteralNode::toString() const
{
    return std::to_string(value);
}

std::string BoolLiteralNode::toString() const
{
    return value ? "true" : "false";
}

std::string FloatLiteralNode::toString() const
{
    return std::to_string(value);
}

std::string DoubleLiteralNode::toString() const
{
    return std::to_string(value);
}

std::string StringLiteralNode::toString() const
{
    return "\"" + value + "\"";
}

std::string IdentifierNode::toString() const
{
    return name;
}

std::string EnumLiteralNode::toString() const
{
    return enumName + "::" + variantName;
}

std::string BinaryOpNode::toString() const
{
    std::string op_str;
    switch(op)
    {
    case OP_PLUS:
        op_str = "+";
        break;
    case OP_MINUS:
        op_str = "-";
        break;
    case OP_MULTIPLY:
        op_str = "*";
        break;
    case OP_DIVIDE:
        op_str = "/";
        break;
    case OP_MODULO:
        op_str = "%";
        break;
    case OP_LT:
        op_str = "<";
        break;
    case OP_GT:
        op_str = ">";
        break;
    case OP_LE:
        op_str = "<=";
        break;
    case OP_GE:
        op_str = ">=";
        break;
    case OP_EQ:
        op_str = "==";
        break;
    case OP_NE:
        op_str = "!=";
        break;
    }
    return "(" + left->toString() + " " + op_str + " " + right->toString() +
           ")";
}

std::string UnaryOpNode::toString() const
{
    std::string op_str;
    switch(op)
    {
    case OP_NEG:
        op_str = "-";
        break;
    case OP_ADDR:
        op_str = "&";
        break;
    case OP_DEREF:
        op_str = "*";
        break;
    }
    return "(" + op_str + operand->toString() + ")";
}

std::string TernaryNode::toString() const
{
    return "(" + condition->toString() + " ? " + trueExpr->toString() + " : " +
           falseExpr->toString() + ")";
}

std::string TryExpressionNode::toString() const
{
    return expression->toString() + "?";
}

std::string FunctionCallNode::toString() const
{
    std::string result = name + "(";
    for(size_t i = 0; i < arguments.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += arguments[i]->toString();
    }
    result += ")";
    return result;
}

std::string MethodCallNode::toString() const
{
    std::string result = object->toString() + "." + methodName + "(";
    for(size_t i = 0; i < arguments.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += arguments[i]->toString();
    }
    result += ")";
    return result;
}

std::string MatchPatternNode::toString() const
{
    switch(kind)
    {
    case PATTERN_OK:
        return "Ok(" + binding + ")";
    case PATTERN_ERR:
        return "Err(" + binding + ")";
    case PATTERN_SOME:
        return "Some(" + binding + ")";
    case PATTERN_NONE:
        return "None";
    case PATTERN_LITERAL:
        return literal ? literal->toString() : "_";
    case PATTERN_WILDCARD:
        return "_";
    }
    return "_";
}

std::string MatchArmNode::toString() const
{
    return pattern->toString() + " => " + expression->toString();
}

std::string MatchArmListNode::toString() const
{
    std::string result;
    for(size_t i = 0; i < arms.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += arms[i]->toString();
    }
    return result;
}

std::string MatchExpressionNode::toString() const
{
    std::string result = "match " + target->toString() + " { ";
    for(size_t i = 0; i < arms.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += arms[i]->toString();
    }
    result += " }";
    return result;
}

std::string IfNode::toString() const
{
    std::string result = "if " + condition->toString() + ": ";
    result += thenBranch->toString();

    if(elseIfBranch)
    {
        result += "else " + elseIfBranch->toString();
    }

    if(elseBranch)
    {
        result += "else: " + elseBranch->toString();
    }

    return result;
}

std::string LetDeclNode::toString() const
{
    if(type)
    {
        return "let " + name + ": " + type->toString() + " = " +
               expression->toString() + ";";
    }
    return "let " + name + " = " + expression->toString() + ";";
}

std::string VarDeclNode::toString() const
{
    std::string result = "var " + name;
    if(type)
        result += ": " + type->toString();
    if(initExpr)
    {
        result += " = " + initExpr->toString();
    }
    return result + ";";
}

std::string CastExpressionNode::toString() const
{
    std::string typeName;
    switch(targetType)
    {
    case TypeNode::TYPE_INT:
        typeName = "int";
        break;
    case TypeNode::TYPE_FLOAT:
        typeName = "float";
        break;
    case TypeNode::TYPE_DOUBLE:
        typeName = "double";
        break;
    default:
        typeName = "unknown";
    }
    return typeName + "(" + expression->toString() + ")";
}

std::string StructMemberNode::toString() const
{
    std::string result =
        (isVar ? "var " : "let ") + name + ": " + type->toString();
    if(initExpr)
    {
        result += " = " + initExpr->toString();
    }
    return result + ";";
}

std::string StructMemberListNode::toString() const
{
    std::string result;
    for(const auto& member : members)
    {
        result += "    " + member->toString() + "\n";
    }
    for(const auto& method : methods)
    {
        result += "    " + method->toString() + "\n";
    }
    return result;
}

std::string StructMethodNode::toString() const
{
    std::string result = isPublic ? "pub fn " : "fn ";
    result += name + "(";
    if(parameters)
    {
        result += parameters->toString();
    }
    result += ") -> " + returnType->toString() + " {\n";
    if(body)
    {
        result += body->toString();
    }
    result += "    }";
    return result;
}

std::string StructDefNode::toString() const
{
    std::string result;
    if(deriveDebug)
        result += "#[derive(Debug)]\n";
    result += isPublic ? "pub struct " : "struct ";
    result += name;
    if(!baseName.empty())
    {
        result += " : " + baseName;
    }
    result += " {\n" + members->toString() + "};\n";
    return result;
}

std::string EnumVariantNode::toString() const
{
    return name;
}

std::string EnumVariantListNode::toString() const
{
    std::string result;
    for(size_t i = 0; i < variants.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += variants[i]->toString();
    }
    return result;
}

std::string EnumDefNode::toString() const
{
    std::string result = isPublic ? "pub enum " : "enum ";
    result += name + " { ";
    if(variants)
        result += variants->toString();
    result += " };\n";
    return result;
}

std::string EnumListNode::toString() const
{
    std::string result;
    for(const auto& enumDef : enums)
    {
        result += enumDef->toString() + "\n";
    }
    return result;
}

std::string StructListNode::toString() const
{
    std::string result;
    for(const auto& structDef : structs)
    {
        result += structDef->toString() + "\n";
    }
    return result;
}

std::string StructInitNode::toString() const
{
    return typeName + " " + varName + ";";
}

std::string ProgramNode::toString() const
{
    std::string result;
    for(const auto& mod : modules)
    {
        result += mod->toString() + "\n";
    }
    for(const auto& use : imports)
    {
        result += use->toString() + "\n";
    }
    if(enumList)
    {
        result += enumList->toString();
    }
    if(structList)
    {
        result += structList->toString();
    }
    if(functionList)
    {
        result += functionList->toString();
    }
    return result;
}

std::string TopLevelListNode::toString() const
{
    std::string result;
    for(const auto& item : items)
    {
        result += item->toString() + "\n";
    }
    return result;
}

std::string ListElementsNode::toString() const
{
    std::string result;
    for(size_t i = 0; i < elements.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += elements[i]->toString();
    }
    return result;
}

std::string ListLiteralNode::toString() const
{
    return "[" + (elements ? elements->toString() : "") + "]";
}

std::string BlockStatementNode::toString() const
{
    std::string result = "{\n";
    if(statements)
    {
        result += statements->toString();
    }
    result += "}\n";
    return result;
}

std::string ExpressionStatementNode::toString() const
{
    return expression->toString() + ";";
}

std::string RangeExpressionNode::toString() const
{
    return start->toString() + ".." + end->toString();
}

std::string ForNode::toString() const
{
    std::string result =
        "for " + varName + " in " + iterable->toString() + " {\n";
    if(body)
    {
        result += body->toString();
    }
    result += "}\n";
    return result;
}

ASTNode* create_for_range(char* var_name, ASTNode* range, ASTNode* body,
                          int line)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList = blockBody ? blockBody->statements : nullptr;

    auto* node = new ForNode(std::string(var_name),
                             static_cast<ExpressionNode*>(range), stmtList);
    node->line = line;
    return node;
}

ASTNode* create_for_iterator(char* var_name, ASTNode* iterable, ASTNode* body,
                             int line)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList = blockBody ? blockBody->statements : nullptr;

    auto* node = new ForNode(std::string(var_name),
                             static_cast<ExpressionNode*>(iterable), stmtList);
    node->line = line;
    return node;
}

ASTNode* create_range_expression(ASTNode* start, ASTNode* end, int inclusive)
{
    return new RangeExpressionNode(static_cast<ExpressionNode*>(start),
                                   static_cast<ExpressionNode*>(end),
                                   inclusive != 0);
}

ASTNode* create_mod_declaration(char* name, int line)
{
    auto* node = new ModDeclNode(std::string(name));
    node->line = line;
    return node;
}

ASTNode* create_use_declaration(char* module_name, char* item_name, int line)
{
    auto* node = new UseDeclNode(std::string(module_name),
                                 std::string(item_name), false);
    node->line = line;
    return node;
}

ASTNode* create_use_all_declaration(char* module_name, int line)
{
    auto* node = new UseDeclNode(std::string(module_name), "*", true);
    node->line = line;
    return node;
}

std::string ModDeclNode::toString() const
{
    return "mod " + moduleName + ";";
}

std::string UseDeclNode::toString() const
{
    if(importAll)
    {
        return "use " + moduleName + "::*;";
    }
    return "use " + moduleName + "::" + itemName + ";";
}

// Break and Continue statement creation
ASTNode* create_break_stmt(int line)
{
    auto* node = new BreakNode();
    node->line = line;
    return node;
}

ASTNode* create_continue_stmt(int line)
{
    auto* node = new ContinueNode();
    node->line = line;
    return node;
}

std::string BreakNode::toString() const
{
    return "break;";
}

std::string ContinueNode::toString() const
{
    return "continue;";
}

std::string PrintNode::toString() const
{
    std::string result;
    if(debugOnly)
    {
        result = "debug!(\"";
    }
    else
    switch(kind)
    {
    case PRINT_STDOUT:
        result = "print!(\"";
        break;
    case PRINTLN_STDOUT:
        result = "println!(\"";
        break;
    case PRINT_STDERR:
        result = "eprint!(\"";
        break;
    case EPRINTLN_STDERR:
        result = "eprintln!(\"";
        break;
    }
    result += formatString + "\"";
    for(const auto& arg : arguments)
    {
        result += ", " + arg->toString();
    }
    result += ");";
    return result;
}

std::string FormatNode::toString() const
{
    std::string result = "format!(\"";
    result += formatString + "\"";
    for(size_t i = 0; i < arguments.size(); ++i)
    {
        result += ", " + arguments[i]->toString();
    }
    result += ")";
    return result;
}

std::string AssertEqNode::toString() const
{
    return "assert_eq!(" + left->toString() + ", " + right->toString() + ");";
}

// Generic list type
ASTNode* create_generic_list_type(ASTNode* element_type)
{
    return new GenericListTypeNode(static_cast<TypeNode*>(element_type));
}

std::string GenericListTypeNode::toString() const
{
    return "list<" + elementType->toString() + ">";
}

// Map type
ASTNode* create_map_type(ASTNode* key_type, ASTNode* value_type)
{
    return new MapTypeNode(static_cast<TypeNode*>(key_type),
                           static_cast<TypeNode*>(value_type));
}

std::string MapTypeNode::toString() const
{
    return "map<" + keyType->toString() + ", " + valueType->toString() + ">";
}

// Map entry
ASTNode* create_map_entry(ASTNode* key, ASTNode* value)
{
    return new MapEntryNode(static_cast<ExpressionNode*>(key),
                            static_cast<ExpressionNode*>(value));
}

std::string MapEntryNode::toString() const
{
    return key->toString() + ": " + value->toString();
}

// Map entries list
ASTNode* create_map_entry_list(ASTNode* entry)
{
    auto* list = new MapEntriesNode();
    list->addEntry(static_cast<MapEntryNode*>(entry));
    return list;
}

ASTNode* add_map_entry(ASTNode* list, ASTNode* entry)
{
    auto* entriesList = static_cast<MapEntriesNode*>(list);
    entriesList->addEntry(static_cast<MapEntryNode*>(entry));
    return entriesList;
}

std::string MapEntriesNode::toString() const
{
    std::string result;
    for(size_t i = 0; i < entries.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += entries[i]->toString();
    }
    return result;
}

// Map literal
ASTNode* create_map_literal(ASTNode* entries)
{
    return new MapLiteralNode(static_cast<MapEntriesNode*>(entries));
}

std::string MapLiteralNode::toString() const
{
    return "{" + (entries ? entries->toString() : "") + "}";
}

// Index expression
ASTNode* create_index_expression(ASTNode* base, ASTNode* index, int line)
{
    auto* node = new IndexExpressionNode(static_cast<ExpressionNode*>(base),
                                         static_cast<ExpressionNode*>(index));
    node->line = line;
    return node;
}

std::string IndexExpressionNode::toString() const
{
    return base->toString() + "[" + index->toString() + "]";
}

// Type list
ASTNode* create_type_list(ASTNode* type)
{
    auto* list = new TypeListNode();
    list->addType(static_cast<TypeNode*>(type));
    return list;
}

ASTNode* add_type_to_list(ASTNode* list, ASTNode* type)
{
    auto* typeList = static_cast<TypeListNode*>(list);
    typeList->addType(static_cast<TypeNode*>(type));
    return typeList;
}

std::string TypeListNode::toString() const
{
    std::string result;
    for(size_t i = 0; i < types.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += types[i]->toString();
    }
    return result;
}

// Tuple type
ASTNode* create_tuple_type(ASTNode* type_list)
{
    return new TupleTypeNode(static_cast<TypeListNode*>(type_list));
}

std::string TupleTypeNode::toString() const
{
    return "tuple<" + elementTypes->toString() + ">";
}

// Struct type reference
ASTNode* create_struct_type_ref(char* name)
{
    return new StructTypeRefNode(std::string(name));
}

std::string StructTypeRefNode::toString() const
{
    return structName;
}

std::string GenericStructTypeRefNode::getMangledName() const
{
    std::string mangled = structName;
    for(auto* typeArg : typeArgs)
    {
        mangled += "_";
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(typeArg))
        {
            mangled += structRef->structName;
        }
        else if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(typeArg))
        {
            mangled += genRef->getMangledName();
        }
        else
        {
            // Basic type
            switch(typeArg->kind)
            {
            case TypeNode::TYPE_BOOL:
                mangled += "bool";
                break;
            case TypeNode::TYPE_INT:
                mangled += "int";
                break;
            case TypeNode::TYPE_I8:
                mangled += "i8";
                break;
            case TypeNode::TYPE_I16:
                mangled += "i16";
                break;
            case TypeNode::TYPE_I32:
                mangled += "i32";
                break;
            case TypeNode::TYPE_I64:
                mangled += "i64";
                break;
            case TypeNode::TYPE_U8:
                mangled += "u8";
                break;
            case TypeNode::TYPE_U16:
                mangled += "u16";
                break;
            case TypeNode::TYPE_U32:
                mangled += "u32";
                break;
            case TypeNode::TYPE_U64:
                mangled += "u64";
                break;
            case TypeNode::TYPE_FLOAT:
                mangled += "float";
                break;
            case TypeNode::TYPE_DOUBLE:
                mangled += "double";
                break;
            case TypeNode::TYPE_STRING:
                mangled += "string";
                break;
            case TypeNode::TYPE_STR8:
                mangled += "str8";
                break;
            case TypeNode::TYPE_STR16:
                mangled += "str16";
                break;
            default:
                mangled += "unknown";
                break;
            }
        }
    }
    return mangled;
}

std::string GenericStructTypeRefNode::toString() const
{
    std::string result = structName + "<";
    for(size_t i = 0; i < typeArgs.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += typeArgs[i]->toString();
    }
    result += ">";
    return result;
}

// Tuple literal
ASTNode* create_tuple_literal(ASTNode* elements)
{
    return new TupleLiteralNode(static_cast<ListElementsNode*>(elements));
}

std::string TupleLiteralNode::toString() const
{
    return "(" + (elements ? elements->toString() : "") + ")";
}

// Tuple access
ASTNode* create_tuple_access(ASTNode* tuple, int index, int line)
{
    auto* node =
        new TupleAccessNode(static_cast<ExpressionNode*>(tuple), index);
    node->line = line;
    return node;
}

std::string TupleAccessNode::toString() const
{
    return tuple->toString() + "." + std::to_string(index);
}

// Map iterator
ASTNode* create_map_keys_iterator(ASTNode* map_expr, int line)
{
    auto* node = new MapIteratorNode(static_cast<ExpressionNode*>(map_expr),
                                     MapIteratorNode::ITER_KEYS);
    node->line = line;
    return node;
}

ASTNode* create_map_values_iterator(ASTNode* map_expr, int line)
{
    auto* node = new MapIteratorNode(static_cast<ExpressionNode*>(map_expr),
                                     MapIteratorNode::ITER_VALUES);
    node->line = line;
    return node;
}

ASTNode* create_map_entries_iterator(ASTNode* map_expr, int line)
{
    auto* node = new MapIteratorNode(static_cast<ExpressionNode*>(map_expr),
                                     MapIteratorNode::ITER_ENTRIES);
    node->line = line;
    return node;
}

std::string MapIteratorNode::toString() const
{
    std::string method;
    switch(kind)
    {
    case ITER_KEYS:
        method = ".keys()";
        break;
    case ITER_VALUES:
        method = ".values()";
        break;
    case ITER_ENTRIES:
        method = ".entries()";
        break;
    }
    return mapExpr->toString() + method;
}

// TypeParamListNode toString
std::string TypeParamListNode::toString() const
{
    std::string result = "<";
    for(size_t i = 0; i < params.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += params[i];
    }
    result += ">";
    return result;
}

// ImplBlockNode toString
std::string ImplBlockNode::toString() const
{
    std::string result = "impl";
    if(!typeParams.empty())
    {
        result += "<";
        for(size_t i = 0; i < typeParams.size(); ++i)
        {
            if(i > 0)
                result += ", ";
            result += typeParams[i];
        }
        result += ">";
    }
    result += " " + structName + " {\n";
    for(auto method : methods)
    {
        result += "    " + method->toString() + "\n";
    }
    result += "}";
    return result;
}

// ImplListNode toString
std::string ImplListNode::toString() const
{
    std::string result;
    for(auto impl : impls)
    {
        result += impl->toString() + "\n";
    }
    return result;
}

// StructLiteralNode toString
std::string StructLiteralNode::toString() const
{
    std::string result = structName;
    if(!typeArgs.empty())
    {
        result += "<";
        for(size_t i = 0; i < typeArgs.size(); ++i)
        {
            if(i > 0)
                result += ", ";
            result += typeArgs[i];
        }
        result += ">";
    }
    result += " { ";
    for(size_t i = 0; i < fields.size(); ++i)
    {
        if(i > 0)
            result += ", ";
        result += fields[i].first + ": " + fields[i].second->toString();
    }
    result += " }";
    return result;
}

// Helper functions for generic structs and impl blocks

ASTNode* create_type_param_list(char* param)
{
    auto* node = new TypeParamListNode();
    node->params.push_back(std::string(param));
    return node;
}

ASTNode* add_type_param(ASTNode* list, char* param)
{
    auto* paramList = static_cast<TypeParamListNode*>(list);
    paramList->params.push_back(std::string(param));
    return paramList;
}

ASTNode* create_generic_struct_def(char* name, char* base_name,
                                   ASTNode* type_params, ASTNode* members,
                                   int is_public, int derive_debug)
{
    std::string baseName = base_name ? std::string(base_name) : "";
    auto* node = new StructDefNode(std::string(name), baseName,
                                   static_cast<StructMemberListNode*>(members),
                                   is_public != 0, derive_debug != 0);

    if(type_params)
    {
        auto* paramList = static_cast<TypeParamListNode*>(type_params);
        node->typeParams = paramList->params;
    }

    return node;
}

ASTNode* create_impl_block(char* struct_name, ASTNode* type_params)
{
    auto* node = new ImplBlockNode(std::string(struct_name));

    if(type_params)
    {
        auto* paramList = static_cast<TypeParamListNode*>(type_params);
        node->typeParams = paramList->params;
    }

    return node;
}

ASTNode* add_impl_method(ASTNode* impl, ASTNode* method)
{
    auto* implBlock = static_cast<ImplBlockNode*>(impl);
    implBlock->methods.push_back(static_cast<StructMethodNode*>(method));
    return implBlock;
}

ASTNode* create_struct_literal(char* struct_name, ASTNode* type_args,
                               ASTNode* fields, int line)
{
    auto* node = new StructLiteralNode(std::string(struct_name));
    node->line = line;

    // type_args would be a TypeListNode if present
    if(type_args)
    {
        auto* typeList = dynamic_cast<TypeListNode*>(type_args);
        if(typeList)
        {
            for(auto t : typeList->types)
            {
                if(auto* structRef = dynamic_cast<StructTypeRefNode*>(t))
                {
                    node->typeArgs.push_back(structRef->structName);
                }
                else
                {
                    // Convert TypeNode to string representation
                    node->typeArgs.push_back(t->toString());
                }
            }
        }
    }

    // fields is the StructLiteralNode with fields already added
    if(fields)
    {
        auto* existingLit = dynamic_cast<StructLiteralNode*>(fields);
        if(existingLit)
        {
            node->fields = existingLit->fields;
        }
    }

    return node;
}

ASTNode* create_struct_field_init_list(char* field_name, ASTNode* value)
{
    auto* node = new StructLiteralNode("");
    node->fields.push_back(
        {std::string(field_name), static_cast<ExpressionNode*>(value)});
    return node;
}

ASTNode* add_struct_field_init(ASTNode* list, char* field_name, ASTNode* value)
{
    auto* lit = static_cast<StructLiteralNode*>(list);
    lit->fields.push_back(
        {std::string(field_name), static_cast<ExpressionNode*>(value)});
    return lit;
}

ASTNode* create_generic_struct_type_ref(char* name, ASTNode* type_args)
{
    auto* node = new GenericStructTypeRefNode(std::string(name));

    // Copy type arguments from TypeListNode
    if(auto* typeList = dynamic_cast<TypeListNode*>(type_args))
    {
        for(auto* t : typeList->types)
        {
            node->typeArgs.push_back(t);
        }
    }

    return node;
}
