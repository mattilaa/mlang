#include "ast.h"
#include "ast_handle_helpers.h"
#include "parser.hpp"
#include <cctype>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace {

inline int64_t node_to_handle(ASTNode* node)
{
    return reinterpret_cast<int64_t>(node);
}

inline ASTNode* handle_to_node(int64_t handle)
{
    return reinterpret_cast<ASTNode*>(handle);
}

} // namespace

ASTNode* create_program_impl(ASTNode* top_level_list)
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
        else if(auto* varDecl = dynamic_cast<VarDeclNode*>(item))
        {
            program->globalVars.push_back(varDecl);
        }
        else if(auto* aliasDef = dynamic_cast<TypeAliasNode*>(item))
        {
            program->typeAliases.push_back(aliasDef);
        }
        else if(auto* traitDef = dynamic_cast<TraitDefNode*>(item))
        {
            program->traitDefs.push_back(traitDef);
        }
    }

    return program;
}

ASTNode* create_top_level_list_impl(ASTNode* item)
{
    auto list = new TopLevelListNode();
    list->items.push_back(item);
    return list;
}

ASTNode* add_to_top_level_list_impl(ASTNode* list, ASTNode* item)
{
    auto topLevelList = static_cast<TopLevelListNode*>(list);
    topLevelList->items.push_back(item);
    return topLevelList;
}

ASTNode* create_function_list_impl(ASTNode* function)
{
    auto list = new FunctionListNode();
    list->functions.push_back(static_cast<FunctionDefNode*>(function));
    return list;
}

ASTNode* add_function_to_list_impl(ASTNode* list, ASTNode* function)
{
    auto func_list = static_cast<FunctionListNode*>(list);
    func_list->functions.push_back(static_cast<FunctionDefNode*>(function));
    return func_list;
}

ASTNode* create_function_def_impl(ASTNode* type, char* name, ASTNode* params,
                                  ASTNode* body, int is_public, int is_extern)
{
    return new FunctionDefNode(static_cast<TypeNode*>(type), std::string(name),
                               static_cast<ParameterListNode*>(params),
                               static_cast<StatementListNode*>(body),
                               is_public != 0,
                               is_extern != 0);
}

ASTNode* create_type_node_impl(TypeNode::TypeKind type)
{
    return new TypeNode(type);
}

ASTNode* create_empty_parameter_list_impl()
{
    return new ParameterListNode();
}

ASTNode* create_parameter_list_impl(ASTNode* param)
{
    auto list = new ParameterListNode();
    if(param)
    {
        list->parameters.push_back(static_cast<ParameterNode*>(param));
    }
    return list;
}

ASTNode* create_parameter_impl(ASTNode* type, char* name)
{
    return new ParameterNode(static_cast<TypeNode*>(type), std::string(name));
}

ASTNode* set_parameter_list_vararg_impl(ASTNode* list)
{
    auto param_list = static_cast<ParameterListNode*>(list);
    if(param_list)
        param_list->isVarArg = true;
    return param_list;
}

ASTNode* add_parameter_impl(ASTNode* list, ASTNode* param)
{
    auto param_list = static_cast<ParameterListNode*>(list);
    param_list->parameters.push_back(static_cast<ParameterNode*>(param));
    return param_list;
}

ASTNode* create_statement_list_impl(ASTNode* stmt)
{
    auto list = new StatementListNode();
    list->statements.push_back(static_cast<StatementNode*>(stmt));
    return list;
}

ASTNode* create_empty_statement_list_impl()
{
    return new StatementListNode();
}

ASTNode* add_statement_impl(ASTNode* list, ASTNode* stmt)
{
    auto stmt_list = static_cast<StatementListNode*>(list);
    stmt_list->statements.push_back(static_cast<StatementNode*>(stmt));
    return stmt_list;
}

ASTNode* create_assignment_impl(char* name, ASTNode* expr, int line)
{
    auto* node = new AssignmentNode(std::string(name),
                                    static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_deref_assignment_impl(ASTNode* pointer_expr, ASTNode* expr, int line)
{
    auto* node = new DerefAssignmentNode(
        static_cast<ExpressionNode*>(pointer_expr),
        static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_return_stmt_impl(ASTNode* expr)
{
    return new ReturnNode(static_cast<ExpressionNode*>(expr));
}

ASTNode* create_struct_init_impl(char* type_name, char* var_name)
{
    return new StructInitNode(std::string(type_name), std::string(var_name));
}

ASTNode* create_int_literal_impl(int64_t value)
{
    return new IntLiteralNode(value);
}

ASTNode* create_bool_literal_impl(int value)
{
    return new BoolLiteralNode(value != 0);
}

ASTNode* create_float_literal_impl(float value)
{
    return new FloatLiteralNode(value);
}

ASTNode* create_double_literal_impl(double value)
{
    return new DoubleLiteralNode(value);
}

ASTNode* create_string_literal_impl(char* value)
{
    return new StringLiteralNode(std::string(value));
}

ASTNode* create_identifier_impl(char* name)
{
    return new IdentifierNode(std::string(name));
}

ASTNode* create_identifier_line_impl(char* name, int line)
{
    auto* node = new IdentifierNode(std::string(name));
    node->line = line;
    return node;
}

ASTNode* create_identifier_at_impl(char* name, int line, int col)
{
    auto* node = new IdentifierNode(std::string(name));
    node->line = line;
    node->col  = col;
    return node;
}

ASTNode* create_binary_op_impl(int op, ASTNode* left, ASTNode* right)
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
    case MODULO:
        opType = BinaryOpNode::OP_MODULO;
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
    case SPACESHIP:
        opType = BinaryOpNode::OP_SPACESHIP;
        break;
    case AMP:
        opType = BinaryOpNode::OP_BITAND;
        break;
    case PIPE:
        opType = BinaryOpNode::OP_BITOR;
        break;
    case CARET:
        opType = BinaryOpNode::OP_BITXOR;
        break;
    case SHL:
        opType = BinaryOpNode::OP_SHL;
        break;
    case SHR:
        opType = BinaryOpNode::OP_SHR;
        break;
    case AMP_AMP:
        opType = BinaryOpNode::OP_AND;
        break;
    case PIPE_PIPE:
        opType = BinaryOpNode::OP_OR;
        break;
    case COLON:
        return nullptr;
    default:
        throw std::runtime_error("Unknown binary operator");
    }
    return new BinaryOpNode(opType, static_cast<ExpressionNode*>(left),
                            static_cast<ExpressionNode*>(right));
}

ASTNode* create_fold_expression_impl(int op, ASTNode* pack_expr, int is_right_fold)
{
    BinaryOpNode::OpType opType;
    switch(op)
    {
    case PLUS:
        opType = BinaryOpNode::OP_PLUS;
        break;
    case MULTIPLY:
        opType = BinaryOpNode::OP_MULTIPLY;
        break;
    case AMP_AMP:
        opType = BinaryOpNode::OP_AND;
        break;
    case PIPE_PIPE:
        opType = BinaryOpNode::OP_OR;
        break;
    default:
        throw std::runtime_error("Unsupported fold operator");
    }
    return new FoldExpressionNode(opType,
                                  static_cast<ExpressionNode*>(pack_expr),
                                  is_right_fold != 0);
}

ASTNode* create_unary_op_impl(int op, ASTNode* operand)
{
    UnaryOpNode::OpType opType;
    switch(op)
    {
    case MINUS:
        opType = UnaryOpNode::OP_NEG;
        break;
    case NOT:
        opType = UnaryOpNode::OP_NOT;
        break;
    case TILDE:
        opType = UnaryOpNode::OP_BITNOT;
        break;
    case AMP:
        opType = UnaryOpNode::OP_ADDR;
        break;
    case AMP_MUT:
        opType = UnaryOpNode::OP_ADDR_MUT;
        break;
    case MULTIPLY:
        opType = UnaryOpNode::OP_DEREF;
        break;
    default:
        throw std::runtime_error("Unknown unary operator");
    }
    return new UnaryOpNode(opType, static_cast<ExpressionNode*>(operand));
}

ASTNode* create_update_expression_impl(int kind, int is_prefix, ASTNode* operand,
                                       int line)
{
    UpdateExpressionNode::Kind opKind =
        (kind == 1) ? UpdateExpressionNode::KIND_DECREMENT
                    : UpdateExpressionNode::KIND_INCREMENT;
    auto* node = new UpdateExpressionNode(
        opKind, is_prefix != 0, static_cast<ExpressionNode*>(operand));
    node->line = line;
    return node;
}

ASTNode* create_ternary_expression_impl(ASTNode* cond, ASTNode* t, ASTNode* f,
                                   int line)
{
    auto* node = new TernaryNode(static_cast<ExpressionNode*>(cond),
                                 static_cast<ExpressionNode*>(t),
                                 static_cast<ExpressionNode*>(f));
    node->line = line;
    return node;
}

ASTNode* create_try_expression_impl(ASTNode* expr, int line)
{
    auto* node = new TryExpressionNode(static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_function_call_impl(char* name, ASTNode* arg1, ASTNode* arg2,
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

ASTNode* create_argument_list_impl(ASTNode* arg)
{
    auto list = new ArgumentListNode();
    if(arg)
    {
        list->args.push_back(static_cast<ExpressionNode*>(arg));
    }
    return list;
}

ASTNode* add_argument_impl(ASTNode* list, ASTNode* arg)
{
    auto argList = static_cast<ArgumentListNode*>(list);
    if(arg)
    {
        argList->args.push_back(static_cast<ExpressionNode*>(arg));
    }
    return argList;
}

ASTNode* create_format_argument_impl(char* name, ASTNode* value)
{
    if(name && *name)
    {
        return new FormatArgumentNode(std::string(name),
                                      static_cast<ExpressionNode*>(value));
    }
    return new FormatArgumentNode(static_cast<ExpressionNode*>(value));
}

ASTNode* create_format_argument_list_impl(ASTNode* arg)
{
    auto* list = new FormatArgumentListNode();
    if(arg)
    {
        list->args.push_back(static_cast<FormatArgumentNode*>(arg));
    }
    return list;
}

ASTNode* add_format_argument_impl(ASTNode* list, ASTNode* arg)
{
    auto* argList = static_cast<FormatArgumentListNode*>(list);
    if(arg)
    {
        argList->args.push_back(static_cast<FormatArgumentNode*>(arg));
    }
    return argList;
}

ASTNode* create_function_call_multi_impl(char* name, ASTNode* args, int line)
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

ASTNode* create_result_constructor_impl(char* variant, ASTNode* type_args,
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

ASTNode* create_if_statement_impl(ASTNode* condition, ASTNode* then_branch,
                             ASTNode* else_if_branch, ASTNode* else_branch)
{
    return create_if_statement_with_init(nullptr, condition, then_branch,
                                         else_if_branch, else_branch);
}

ASTNode* create_if_statement_with_init_impl(ASTNode* condition_init,
                                       ASTNode* condition,
                                       ASTNode* then_branch,
                                       ASTNode* else_if_branch,
                                       ASTNode* else_branch)
{
    auto* thenBlock = dynamic_cast<BlockStatementNode*>(then_branch);
    auto* elseBlock = dynamic_cast<BlockStatementNode*>(else_branch);
    StatementListNode* thenList =
        thenBlock ? thenBlock->statements
                  : dynamic_cast<StatementListNode*>(then_branch);
    StatementListNode* elseList =
        elseBlock ? elseBlock->statements
                  : dynamic_cast<StatementListNode*>(else_branch);

    auto* ifNode = new IfNode(static_cast<StatementNode*>(condition_init),
                              static_cast<ExpressionNode*>(condition), thenList,
                              static_cast<IfNode*>(else_if_branch), elseList);
    if (ifNode->conditionInit && ifNode->conditionInit->line <= 0) {
        int inferred_line = 0;
        if (ifNode->thenBranch && !ifNode->thenBranch->statements.empty() &&
            ifNode->thenBranch->statements[0] &&
            ifNode->thenBranch->statements[0]->line > 1) {
            inferred_line = ifNode->thenBranch->statements[0]->line - 1;
        } else if (ifNode->condition && ifNode->condition->line > 0) {
            inferred_line = ifNode->condition->line;
        }
        if (inferred_line > 0) {
            ifNode->conditionInit->line = inferred_line;
        }
    }
    return ifNode;
}

ASTNode* create_let_declaration_impl(ASTNode* type, char* name, ASTNode* expr)
{
    return new LetDeclNode(static_cast<TypeNode*>(type), std::string(name),
                           static_cast<ExpressionNode*>(expr));
}

ASTNode* create_var_declaration_impl(ASTNode* type, char* name, ASTNode* expr)
{
    return new VarDeclNode(static_cast<TypeNode*>(type), std::string(name),
                           static_cast<ExpressionNode*>(expr));
}

ASTNode* create_cast_expression_impl(int type, ASTNode* expr)
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

ASTNode* create_struct_list_impl(ASTNode* struct_def)
{
    auto list = new StructListNode();
    list->addStruct(static_cast<StructDefNode*>(struct_def));
    return list;
}

ASTNode* add_struct_to_list_impl(ASTNode* list, ASTNode* struct_def)
{
    auto structList = static_cast<StructListNode*>(list);
    structList->addStruct(static_cast<StructDefNode*>(struct_def));
    return structList;
}

ASTNode* create_struct_def_impl(char* name, char* base_name, ASTNode* members,
                                int is_public, int derive_debug)
{
    auto* def = new StructDefNode(
        std::string(name), base_name ? std::string(base_name) : "",
        static_cast<StructMemberListNode*>(members), is_public != 0,
        derive_debug != 0);

    // Qualify nested enum names as Struct::Enum for type/literal resolution.
    if(def->members)
    {
        for(auto* en : def->members->enums)
        {
            if(en && en->name.find("::") == std::string::npos)
            {
                en->name = def->name + "::" + en->name;
            }
        }
    }

    return def;
}

ASTNode* create_enum_def_impl(char* name, ASTNode* variants, int is_public,
                         int backing_type)
{
    return new EnumDefNode(
        std::string(name), static_cast<EnumVariantListNode*>(variants),
        is_public != 0, static_cast<TypeNode::TypeKind>(backing_type));
}

ASTNode* create_enum_variant_impl(char* name, int has_explicit_value,
                             long long explicit_value)
{
    return new EnumVariantNode(std::string(name), has_explicit_value != 0,
                               static_cast<int64_t>(explicit_value));
}

ASTNode* create_enum_variant_ref_impl(char* name, char* ref_enum_name,
                                 char* ref_variant_name)
{
    auto* node = new EnumVariantNode(std::string(name), false, 0);
    node->hasReferenceValue = true;
    node->refEnumName = std::string(ref_enum_name);
    node->refVariantName = std::string(ref_variant_name);
    return node;
}

ASTNode* create_enum_variant_list_impl(ASTNode* variant)
{
    auto* list = new EnumVariantListNode();
    if(variant)
        list->addVariant(static_cast<EnumVariantNode*>(variant));
    return list;
}

ASTNode* add_enum_variant_impl(ASTNode* list, ASTNode* variant)
{
    auto* variantList = static_cast<EnumVariantListNode*>(list);
    variantList->addVariant(static_cast<EnumVariantNode*>(variant));
    return variantList;
}

ASTNode* create_struct_member_list_impl(ASTNode* member)
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
    else if(auto* enumNode = dynamic_cast<EnumDefNode*>(member))
    {
        list->addEnum(enumNode);
    }
    return list;
}

ASTNode* add_struct_member_impl(ASTNode* list, ASTNode* member)
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
    else if(auto* enumNode = dynamic_cast<EnumDefNode*>(member))
    {
        memberList->addEnum(enumNode);
    }
    return memberList;
}

ASTNode* create_struct_member_impl(int is_var, ASTNode* type, char* name,
                                   ASTNode* init_expr)
{
    return new StructMemberNode(is_var != 0, static_cast<TypeNode*>(type),
                                std::string(name),
                                static_cast<ExpressionNode*>(init_expr));
}

ASTNode* create_struct_method_impl(ASTNode* type, char* name, ASTNode* params,
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

ASTNode* add_struct_method_impl(ASTNode* list, ASTNode* method)
{
    auto memberList = static_cast<StructMemberListNode*>(list);
    memberList->addMethod(static_cast<StructMethodNode*>(method));
    return memberList;
}

ASTNode* create_method_call_expr_impl(ASTNode* object, char* method_name,
                                 ASTNode* args, int line)
{
    auto* node = new MethodCallNode(static_cast<ExpressionNode*>(object),
                                    std::string(method_name));
    node->line = line;
    if(args)
    {
        auto* argList = static_cast<ArgumentListNode*>(args);
        node->arguments = argList->args;
    }
    return node;
}

ASTNode* create_method_call_impl(ASTNode* object, char* method, ASTNode* args, int line)
{
    return create_method_call_expr_impl(object, method, args, line);
}

ASTNode* create_len_expression_impl(ASTNode* expr, int line)
{
    return create_method_call_impl(expr, const_cast<char*>("len"), nullptr, line);
}

ASTNode* create_match_pattern_impl(char* name, char* binding, int line)
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

ASTNode* create_match_literal_pattern_impl(ASTNode* literal, int line)
{
    auto* lit = static_cast<ExpressionNode*>(literal);
    auto* node =
        new MatchPatternNode(MatchPatternNode::PATTERN_LITERAL, "", lit);
    node->line = line;
    return node;
}

ASTNode* create_match_arm_impl(ASTNode* pattern, ASTNode* expr, int line)
{
    auto* node = new MatchArmNode(static_cast<MatchPatternNode*>(pattern),
                                  static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_match_arm_list_impl(ASTNode* arm)
{
    auto* list = new MatchArmListNode();
    if(arm)
        list->addArm(static_cast<MatchArmNode*>(arm));
    return list;
}

ASTNode* add_match_arm_impl(ASTNode* list, ASTNode* arm)
{
    auto* armList = static_cast<MatchArmListNode*>(list);
    armList->addArm(static_cast<MatchArmNode*>(arm));
    return armList;
}

ASTNode* create_match_expression_impl(ASTNode* target, ASTNode* arms, int line)
{
    auto* node = new MatchExpressionNode(static_cast<ExpressionNode*>(target),
                                         static_cast<MatchArmListNode*>(arms));
    node->line = line;
    return node;
}

ASTNode* create_enum_literal_impl(char* enum_name, char* variant_name, int line)
{
    auto* node =
        new EnumLiteralNode(std::string(enum_name), std::string(variant_name));
    node->line = line;
    return node;
}

ASTNode* create_list_type_impl()
{
    return new ListTypeNode();
}

ASTNode* create_pointer_type_impl(ASTNode* element_type)
{
    return new PointerTypeNode(static_cast<TypeNode*>(element_type));
}

ASTNode* create_reference_type_impl(ASTNode* element_type, int is_mutable)
{
    return new ReferenceTypeNode(
        static_cast<TypeNode*>(element_type), is_mutable != 0);
}

ASTNode* create_closure_impl(ASTNode* body)
{
    return new ClosureNode(nullptr, static_cast<StatementListNode*>(body));
}

ASTNode* create_closure_with_params_impl(ASTNode* params, ASTNode* body)
{
    return new ClosureNode(static_cast<ParameterListNode*>(params),
                           static_cast<StatementListNode*>(body));
}

std::string ClosureNode::toString() const
{
    if(parameters && !parameters->parameters.empty())
    {
        return "|" + parameters->toString() + "| { ... }";
    }
    return "|| { ... }";
}

std::string ReferenceTypeNode::toString() const
{
    std::string m = isMutable ? "mut " : "";
    return "&" + m + (elementType ? elementType->toString() : "void");
}

ASTNode* create_list_literal_impl(ASTNode* elements)
{
    return new ListLiteralNode(static_cast<ListElementsNode*>(elements));
}

ASTNode* create_list_element_list_impl(ASTNode* element)
{
    auto list = new ListElementsNode();
    list->addElement(static_cast<ExpressionNode*>(element));
    return list;
}

ASTNode* add_list_element_impl(ASTNode* list, ASTNode* element)
{
    auto elemList = static_cast<ListElementsNode*>(list);
    elemList->addElement(static_cast<ExpressionNode*>(element));
    return elemList;
}

ASTNode* create_expression_statement_impl(ASTNode* expr)
{
    return new ExpressionStatementNode(static_cast<ExpressionNode*>(expr));
}

ASTNode* create_block_statement_impl(ASTNode* stmt_list)
{
    return new BlockStatementNode(static_cast<StatementListNode*>(stmt_list));
}

ASTNode* create_else_if_impl(ASTNode* condition, ASTNode* body)
{
    return create_else_if_with_init(nullptr, condition, body);
}

ASTNode* create_else_if_with_init_impl(ASTNode* condition_init, ASTNode* condition,
                                  ASTNode* body)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList =
        blockBody ? blockBody->statements
                  : dynamic_cast<StatementListNode*>(body);

    return new IfNode(static_cast<StatementNode*>(condition_init),
                      static_cast<ExpressionNode*>(condition), stmtList);
}

ASTNode* add_else_if_impl(ASTNode* else_if_list, ASTNode* else_if)
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
ASTNode* create_print_stmt_impl(int kind, char* format_str, ASTNode* args, int line)
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
        auto* argList = static_cast<FormatArgumentListNode*>(args);
        for(auto* arg : argList->args)
        {
            if(arg->isNamed())
                node->addNamedArgument(arg->name, arg->value);
            else
                node->addArgument(arg->value);
        }
    }

    return node;
}

ASTNode* create_debug_print_stmt_impl(char* format_str, ASTNode* args, int line)
{
    auto* node =
        new PrintNode(PrintNode::EPRINTLN_STDERR, std::string(format_str), true);
    node->line = line;

    if(args)
    {
        auto* argList = static_cast<FormatArgumentListNode*>(args);
        for(auto* arg : argList->args)
        {
            if(arg->isNamed())
                node->addNamedArgument(arg->name, arg->value);
            else
                node->addArgument(arg->value);
        }
    }

    return node;
}

ASTNode* create_print_expr_stmt_impl(int kind, ASTNode* expr, int line)
{
    auto* node = new PrintNode(static_cast<PrintNode::PrintKind>(kind), "{}", false);
    node->line = line;
    if(expr)
        node->addArgument(static_cast<ExpressionNode*>(expr));
    return node;
}

ASTNode* create_format_expr_impl(char* format_str, ASTNode* args, int line)
{
    auto* node = new FormatNode(std::string(format_str));
    node->line = line;

    if(args)
    {
        auto* argList = static_cast<FormatArgumentListNode*>(args);
        for(auto* arg : argList->args)
        {
            if(arg->isNamed())
                node->addNamedArgument(arg->name, arg->value);
            else
                node->addArgument(arg->value);
        }
    }

    return node;
}

ASTNode* create_assert_eq_impl(ASTNode* left, ASTNode* right, int line)
{
    auto* node = new AssertEqNode(static_cast<ExpressionNode*>(left),
                                  static_cast<ExpressionNode*>(right));
    node->line = line;
    return node;
}

ASTNode* create_assert_impl(ASTNode* condition, int line)
{
    auto* node = new AssertNode(static_cast<ExpressionNode*>(condition));
    node->line = line;
    return node;
}

ASTNode* create_static_assert_impl(ASTNode* condition, int line)
{
    auto* node = new StaticAssertNode(static_cast<ExpressionNode*>(condition));
    node->line = line;
    return node;
}

ASTNode* create_unsafe_block_impl(ASTNode* block, int line)
{
    auto* node = dynamic_cast<BlockStatementNode*>(block);
    if(node)
    {
        node->isUnsafe = true;
        node->line = line;
    }
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
        return "i32";
    case TYPE_FLOAT:
        return "f32";
    case TYPE_DOUBLE:
        return "f64";
    case TYPE_STRING:
        return "str8";
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

ASTNode* create_field_access_impl(char* struct_name, char* field_name, int line)
{
    auto* node =
        new FieldAccessNode(std::string(struct_name), std::string(field_name));
    node->line = line;
    return node;
}

ASTNode* create_field_access_expr_impl(ASTNode* object, char* field_name, int line)
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

ASTNode* create_field_assignment_impl(char* struct_name, char* field_name,
                                 ASTNode* expr, int line)
{
    auto* node = new FieldAssignmentNode(std::string(struct_name),
                                         std::string(field_name),
                                         static_cast<ExpressionNode*>(expr));
    node->line = line;
    return node;
}

ASTNode* create_chained_field_assignment_impl(ASTNode* target, ASTNode* expr,
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
    case OP_SPACESHIP:
        op_str = "<=>";
        break;
    case OP_BITAND:
        op_str = "&";
        break;
    case OP_BITOR:
        op_str = "|";
        break;
    case OP_BITXOR:
        op_str = "^";
        break;
    case OP_SHL:
        op_str = "<<";
        break;
    case OP_SHR:
        op_str = ">>";
        break;
    case OP_AND:
        op_str = "&&";
        break;
    case OP_OR:
        op_str = "||";
        break;
    }
    return "(" + left->toString() + " " + op_str + " " + right->toString() +
           ")";
}

std::string FoldExpressionNode::toString() const
{
    std::string op_str;
    switch(op)
    {
    case BinaryOpNode::OP_PLUS:
        op_str = "+";
        break;
    case BinaryOpNode::OP_MULTIPLY:
        op_str = "*";
        break;
    case BinaryOpNode::OP_AND:
        op_str = "&&";
        break;
    case BinaryOpNode::OP_OR:
        op_str = "||";
        break;
    default:
        op_str = "?";
        break;
    }

    if(isRightFold)
        return "(" + packExpr->toString() + " " + op_str + " ...)";
    return "(... " + op_str + " " + packExpr->toString() + ")";
}

std::string UnaryOpNode::toString() const
{
    std::string op_str;
    switch(op)
    {
    case OP_NEG:
        op_str = "-";
        break;
    case OP_NOT:
        op_str = "!";
        break;
    case OP_BITNOT:
        op_str = "~";
        break;
    case OP_ADDR:
        op_str = "&";
        break;
    case OP_ADDR_MUT:
        op_str = "&mut ";
        break;
    case OP_DEREF:
        op_str = "*";
        break;
    }
    return "(" + op_str + operand->toString() + ")";
}

std::string UpdateExpressionNode::toString() const
{
    std::string op_str = (kind == UpdateExpressionNode::KIND_INCREMENT) ? "++" : "--";
    if(isPrefix)
        return "(" + op_str + operand->toString() + ")";
    return "(" + operand->toString() + op_str + ")";
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
    std::string result = "if ";
    if(conditionInit)
    {
        std::string initText = conditionInit->toString();
        if(!initText.empty() && initText.back() == ';')
            initText.pop_back();
        result += initText + ": ";
    }
    result += condition->toString() + ": ";
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
    std::string result;
    if(isStaticStorage)
        result += "static ";
    result += "var " + name;
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
        typeName = "i32";
        break;
    case TypeNode::TYPE_FLOAT:
        typeName = "f32";
        break;
    case TypeNode::TYPE_DOUBLE:
        typeName = "f64";
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
    for(const auto& en : enums)
    {
        result += "    " + en->toString() + "\n";
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
    if(hasReferenceValue)
        return name + " = " + refEnumName + "::" + refVariantName;
    if(hasExplicitValue)
        return name + " = " + std::to_string(explicitValue);
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
    auto backingTypeName = [](TypeNode::TypeKind kind) -> std::string {
        switch(kind)
        {
        case TypeNode::TYPE_INT:
            return "i32";
        case TypeNode::TYPE_I8:
            return "i8";
        case TypeNode::TYPE_I16:
            return "i16";
        case TypeNode::TYPE_I32:
            return "i32";
        case TypeNode::TYPE_I64:
            return "i64";
        case TypeNode::TYPE_U8:
            return "u8";
        case TypeNode::TYPE_U16:
            return "u16";
        case TypeNode::TYPE_U32:
            return "u32";
        case TypeNode::TYPE_U64:
            return "u64";
        default:
            return "i32";
        }
    };

    std::string result = isPublic ? "pub enum " : "enum ";
    result += name + " : " + backingTypeName(backingType) + " { ";
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
    for(const auto& aliasDef : typeAliases)
    {
        result += aliasDef->toString() + "\n";
    }
    if(enumList)
    {
        result += enumList->toString();
    }
    for(const auto& traitDef : traitDefs)
    {
        result += traitDef->toString() + "\n";
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
    return isUnsafe ? ("unsafe " + result) : result;
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
    std::string varPart = indexVarName.empty()
                              ? varName
                              : "(" + indexVarName + ", " + varName + ")";
    std::string result =
        "for " + varPart + " in " + iterable->toString() + " {\n";
    if(body)
    {
        result += body->toString();
    }
    result += "}\n";
    return result;
}

std::string WhileNode::toString() const
{
    std::string result = "while " + condition->toString() + " {\n";
    if(body)
    {
        result += body->toString();
    }
    result += "}\n";
    return result;
}

std::string ArrayFillNode::toString() const
{
    return "[" + (value ? value->toString() : "") + "; " +
           (count ? count->toString() : "") + "]";
}

ASTNode* create_for_range_impl(char* var_name, ASTNode* range, ASTNode* body,
                          int line)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList = blockBody ? blockBody->statements : nullptr;

    auto* node = new ForNode(std::string(var_name),
                             static_cast<ExpressionNode*>(range), stmtList);
    node->line = line;
    return node;
}

ASTNode* create_for_iterator_impl(char* var_name, ASTNode* iterable, ASTNode* body,
                             int line)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList = blockBody ? blockBody->statements : nullptr;

    auto* node = new ForNode(std::string(var_name),
                             static_cast<ExpressionNode*>(iterable), stmtList);
    node->line = line;
    return node;
}

ASTNode* create_while_statement_impl(ASTNode* condition, ASTNode* body, int line,
                                int uses_colon_without_guard)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList =
        blockBody ? blockBody->statements
                  : dynamic_cast<StatementListNode*>(body);
    auto* node = new WhileNode(static_cast<ExpressionNode*>(condition), stmtList,
                               uses_colon_without_guard != 0);
    node->line = line;
    return node;
}

ASTNode* create_try_catch_stmt_impl(ASTNode* try_block, char* catch_name,
                                    ASTNode* catch_type, ASTNode* catch_block,
                                    int line)
{
    auto* tryStmt = dynamic_cast<BlockStatementNode*>(try_block);
    auto* catchStmt = dynamic_cast<BlockStatementNode*>(catch_block);
    auto* node = new TryCatchNode(tryStmt,
                                  catch_name ? std::string(catch_name)
                                             : std::string(),
                                  static_cast<TypeNode*>(catch_type),
                                  catchStmt);
    node->line = line;
    return node;
}

ASTNode* create_for_enumerate_impl(char* index_var, char* val_var, ASTNode* iterable,
                               ASTNode* body, int line)
{
    auto* blockBody = dynamic_cast<BlockStatementNode*>(body);
    StatementListNode* stmtList = blockBody ? blockBody->statements : nullptr;

    auto* node = new ForNode(std::string(val_var),
                             static_cast<ExpressionNode*>(iterable), stmtList,
                             std::string(index_var));
    node->line = line;
    return node;
}

ASTNode* create_array_fill_impl(ASTNode* value, ASTNode* count)
{
    return new ArrayFillNode(static_cast<ExpressionNode*>(value),
                             static_cast<ExpressionNode*>(count));
}

ASTNode* create_range_expression_impl(ASTNode* start, ASTNode* end, int inclusive)
{
    return new RangeExpressionNode(static_cast<ExpressionNode*>(start),
                                   static_cast<ExpressionNode*>(end),
                                   inclusive != 0);
}

ASTNode* create_mod_declaration_impl(char* name, int line)
{
    auto* node = new ModDeclNode(std::string(name));
    node->line = line;
    return node;
}

ASTNode* create_use_declaration_impl(char* module_name, char* item_name, int line)
{
    auto* node = new UseDeclNode(std::string(module_name),
                                 std::string(item_name), false);
    node->line = line;
    return node;
}

ASTNode* create_use_declaration_alias_impl(char* module_name, char* item_name,
                                           char* alias_name, int line)
{
    auto* node = new UseDeclNode(std::string(module_name),
                                 std::string(item_name), false,
                                 std::string(alias_name));
    node->line = line;
    return node;
}

ASTNode* create_use_module_alias_declaration_impl(char* module_name,
                                                  char* alias_name, int line)
{
    auto* node = new UseDeclNode(std::string(module_name), "", false,
                                 std::string(alias_name), true);
    node->line = line;
    return node;
}

ASTNode* create_use_all_declaration_impl(char* module_name, int line)
{
    auto* node = new UseDeclNode(std::string(module_name), "*", true);
    node->line = line;
    return node;
}

ASTNode* create_type_alias_impl(char* name, ASTNode* type_params,
                           ASTNode* aliased_type)
{
    auto* node =
        new TypeAliasNode(std::string(name), static_cast<TypeNode*>(aliased_type));
    if(type_params)
    {
        auto* params = static_cast<TypeParamListNode*>(type_params);
        node->typeParams = params->params;
    }
    return node;
}

std::string ModDeclNode::toString() const
{
    return "mod " + moduleName + ";";
}

std::string UseDeclNode::toString() const
{
    if(moduleAlias)
    {
        return "use " + moduleName + " as " + aliasName + ";";
    }
    if(importAll)
    {
        return "use " + moduleName + "::*;";
    }
    if(!aliasName.empty())
    {
        return "use " + moduleName + "::" + itemName + " as " + aliasName + ";";
    }
    return "use " + moduleName + "::" + itemName + ";";
}

std::string TypeAliasNode::toString() const
{
    std::string out = "using " + name;
    if(!typeParams.empty())
    {
        out += "<";
        for(size_t i = 0; i < typeParams.size(); ++i)
        {
            if(i > 0)
                out += ", ";
            out += typeParams[i];
        }
        out += ">";
    }
    out += " = " + (aliasedType ? aliasedType->toString() : "<?>") + ";";
    return out;
}

// Break and Continue statement creation
ASTNode* create_break_stmt_impl(int line)
{
    auto* node = new BreakNode();
    node->line = line;
    return node;
}

ASTNode* create_continue_stmt_impl(int line)
{
    auto* node = new ContinueNode();
    node->line = line;
    return node;
}

ASTNode* create_throw_stmt_impl(ASTNode* expr, int line)
{
    auto* node = new ThrowNode(static_cast<ExpressionNode*>(expr));
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

std::string ThrowNode::toString() const
{
    return "throw " + (expression ? expression->toString() : std::string())
           + ";";
}

std::string TryCatchNode::toString() const
{
    std::string out = "try ";
    out += tryBlock ? tryBlock->toString() : "{}";
    out += " catch ";
    out += catchName;
    out += ": ";
    out += catchType ? catchType->toString() : "<?>"; 
    out += " ";
    out += catchBlock ? catchBlock->toString() : "{}";
    return out;
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
    for(const auto& namedArg : namedArguments)
    {
        result += ", " + namedArg.first + "=" + namedArg.second->toString();
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
    for(const auto& namedArg : namedArguments)
    {
        result += ", " + namedArg.first + "=" + namedArg.second->toString();
    }
    result += ")";
    return result;
}

std::string FormatArgumentNode::toString() const
{
    if(isNamed())
        return name + "=" + (value ? value->toString() : "");
    return value ? value->toString() : "";
}

std::string AssertEqNode::toString() const
{
    return "assert_eq!(" + left->toString() + ", " + right->toString() + ");";
}

std::string AssertNode::toString() const
{
    return "assert!(" + (condition ? condition->toString() : "false") + ");";
}

std::string StaticAssertNode::toString() const
{
    return "static_assert!(" +
           (condition ? condition->toString() : "false") + ");";
}

// Generic list type
ASTNode* create_generic_list_type_impl(ASTNode* element_type)
{
    return new GenericListTypeNode(static_cast<TypeNode*>(element_type));
}

std::string GenericListTypeNode::toString() const
{
    return "list<" + elementType->toString() + ">";
}

// Map type
ASTNode* create_map_type_impl(ASTNode* key_type, ASTNode* value_type)
{
    return new MapTypeNode(static_cast<TypeNode*>(key_type),
                           static_cast<TypeNode*>(value_type));
}

std::string MapTypeNode::toString() const
{
    return "map<" + keyType->toString() + ", " + valueType->toString() + ">";
}

// Map entry
ASTNode* create_map_entry_impl(ASTNode* key, ASTNode* value)
{
    return new MapEntryNode(static_cast<ExpressionNode*>(key),
                            static_cast<ExpressionNode*>(value));
}

std::string MapEntryNode::toString() const
{
    return key->toString() + ": " + value->toString();
}

// Map entries list
ASTNode* create_map_entry_list_impl(ASTNode* entry)
{
    auto* list = new MapEntriesNode();
    list->addEntry(static_cast<MapEntryNode*>(entry));
    return list;
}

ASTNode* add_map_entry_impl(ASTNode* list, ASTNode* entry)
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
ASTNode* create_map_literal_impl(ASTNode* entries)
{
    return new MapLiteralNode(static_cast<MapEntriesNode*>(entries));
}

std::string MapLiteralNode::toString() const
{
    return "{" + (entries ? entries->toString() : "") + "}";
}

// Index expression
ASTNode* create_index_expression_impl(ASTNode* base, ASTNode* index, int line)
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
ASTNode* create_type_list_impl(ASTNode* type)
{
    auto* list = new TypeListNode();
    list->addType(static_cast<TypeNode*>(type));
    return list;
}

ASTNode* add_type_to_list_impl(ASTNode* list, ASTNode* type)
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
ASTNode* create_tuple_type_impl(ASTNode* type_list)
{
    return new TupleTypeNode(static_cast<TypeListNode*>(type_list));
}

std::string TupleTypeNode::toString() const
{
    return "tuple<" + elementTypes->toString() + ">";
}

// Struct type reference
ASTNode* create_struct_type_ref_impl(char* name)
{
    return new StructTypeRefNode(std::string(name));
}

std::string StructTypeRefNode::toString() const
{
    return structName;
}

std::string GenericStructTypeRefNode::getMangledName() const
{
    auto normalize_type_name = [](const std::string& n) -> std::string
    {
        std::string out;
        out.reserve(n.size());
        char prev = '\0';
        for(size_t i = 0; i < n.size(); ++i)
        {
            char c = n[i];
            bool keep = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            char w = keep ? c : '_';
            if(w == '_' && prev == '_')
                continue;
            out.push_back(w);
            prev = w;
        }
        while(!out.empty() && out.back() == '_')
            out.pop_back();
        return out.empty() ? "unknown" : out;
    };

    std::function<std::string(TypeNode*)> mangle_type = [&](TypeNode* t)
        -> std::string
    {
        if(!t)
            return "unknown";
        if(auto* sr = dynamic_cast<StructTypeRefNode*>(t))
            return normalize_type_name(sr->structName);
        if(auto* gr = dynamic_cast<GenericStructTypeRefNode*>(t))
            return gr->getMangledName();
        if(auto* gl = dynamic_cast<GenericListTypeNode*>(t))
            return "list_" + mangle_type(gl->elementType);
        if(auto* mp = dynamic_cast<MapTypeNode*>(t))
            return "map_" + mangle_type(mp->keyType) + "_" +
                   mangle_type(mp->valueType);
        if(auto* tp = dynamic_cast<TupleTypeNode*>(t))
        {
            std::string out = "tuple";
            if(tp->elementTypes)
            {
                for(auto* e : tp->elementTypes->types)
                    out += "_" + mangle_type(e);
            }
            return out;
        }
        switch(t->kind)
        {
        case TypeNode::TYPE_BOOL:
            return "bool";
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
            return "i32";
        case TypeNode::TYPE_I8:
            return "i8";
        case TypeNode::TYPE_I16:
            return "i16";
        case TypeNode::TYPE_I64:
            return "i64";
        case TypeNode::TYPE_U8:
            return "u8";
        case TypeNode::TYPE_U16:
            return "u16";
        case TypeNode::TYPE_U32:
            return "u32";
        case TypeNode::TYPE_U64:
            return "u64";
        case TypeNode::TYPE_FLOAT:
            return "f32";
        case TypeNode::TYPE_DOUBLE:
            return "f64";
        case TypeNode::TYPE_STRING:
            return "str8";
        case TypeNode::TYPE_STR8:
            return "str8";
        case TypeNode::TYPE_STR16:
            return "str16";
        case TypeNode::TYPE_LIST:
            return "list";
        case TypeNode::TYPE_MAP:
            return "map";
        case TypeNode::TYPE_TUPLE:
            return "tuple";
        default:
            return "unknown";
        }
    };

    std::string mangled = structName;
    for(auto* typeArg : typeArgs)
    {
        mangled += "_" + mangle_type(typeArg);
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
ASTNode* create_tuple_literal_impl(ASTNode* elements)
{
    return new TupleLiteralNode(static_cast<ListElementsNode*>(elements));
}

std::string TupleLiteralNode::toString() const
{
    return "(" + (elements ? elements->toString() : "") + ")";
}

// Tuple access
ASTNode* create_tuple_access_impl(ASTNode* tuple, int index, int line)
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
ASTNode* create_map_keys_iterator_impl(ASTNode* map_expr, int line)
{
    auto* node = new MapIteratorNode(static_cast<ExpressionNode*>(map_expr),
                                     MapIteratorNode::ITER_KEYS);
    node->line = line;
    return node;
}

ASTNode* create_map_values_iterator_impl(ASTNode* map_expr, int line)
{
    auto* node = new MapIteratorNode(static_cast<ExpressionNode*>(map_expr),
                                     MapIteratorNode::ITER_VALUES);
    node->line = line;
    return node;
}

ASTNode* create_map_entries_iterator_impl(ASTNode* map_expr, int line)
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

std::string TraitDefNode::toString() const
{
    return "trait " + name + " {}";
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
    if(!traitName.empty())
    {
        result += " " + traitName + " for " + structName + " {\n";
    }
    else
    {
        result += " " + structName + " {\n";
    }
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

ASTNode* create_type_param_list_impl(char* param)
{
    auto* node = new TypeParamListNode();
    node->params.push_back(std::string(param));
    return node;
}

ASTNode* add_type_param_impl(ASTNode* list, char* param)
{
    auto* paramList = static_cast<TypeParamListNode*>(list);
    paramList->params.push_back(std::string(param));
    return paramList;
}

ASTNode* create_generic_struct_def_impl(char* name, char* base_name,
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

ASTNode* create_trait_def_impl(char* name, int line)
{
    auto* node = new TraitDefNode(std::string(name));
    node->line = line;
    return node;
}

ASTNode* create_impl_block_impl(char* struct_name, ASTNode* type_params,
                           char* trait_name)
{
    auto* node = new ImplBlockNode(std::string(struct_name));
    if(trait_name)
    {
        node->traitName = std::string(trait_name);
    }

    if(type_params)
    {
        auto* paramList = static_cast<TypeParamListNode*>(type_params);
        node->typeParams = paramList->params;
    }

    return node;
}

ASTNode* add_impl_method_impl(ASTNode* impl, ASTNode* method)
{
    auto* implBlock = static_cast<ImplBlockNode*>(impl);
    implBlock->methods.push_back(static_cast<StructMethodNode*>(method));
    return implBlock;
}

ASTNode* create_struct_literal_impl(char* struct_name, ASTNode* type_args,
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

ASTNode* create_struct_field_init_list_impl(char* field_name, ASTNode* value)
{
    auto* node = new StructLiteralNode("");
    node->fields.push_back(
        {std::string(field_name), static_cast<ExpressionNode*>(value)});
    return node;
}

ASTNode* add_struct_field_init_impl(ASTNode* list, char* field_name, ASTNode* value)
{
    auto* lit = static_cast<StructLiteralNode*>(list);
    lit->fields.push_back(
        {std::string(field_name), static_cast<ExpressionNode*>(value)});
    return lit;
}

ASTNode* create_generic_struct_type_ref_impl(char* name, ASTNode* type_args)
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
