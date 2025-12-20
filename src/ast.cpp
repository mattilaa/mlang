#include "ast.h"
#include "parser.hpp"
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
                             ASTNode* body)
{
    return new FunctionDefNode(static_cast<TypeNode*>(type), std::string(name),
                               static_cast<ParameterListNode*>(params),
                               static_cast<StatementListNode*>(body));
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

ASTNode* create_function_call(char* name, ASTNode* arg1, ASTNode* arg2)
{
    auto call = new FunctionCallNode(std::string(name));
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

ASTNode* create_function_call_multi(char* name, ASTNode* args)
{
    auto call = new FunctionCallNode(std::string(name));
    if(args)
    {
        auto argList = static_cast<ArgumentListNode*>(args);
        call->arguments = argList->args;
    }
    return call;
}

ASTNode* create_if_statement(ASTNode* condition, ASTNode* then_branch,
                             ASTNode* else_if_branch, ASTNode* else_branch)
{
    return new IfNode(static_cast<ExpressionNode*>(condition),
                      static_cast<StatementListNode*>(then_branch),
                      static_cast<IfNode*>(else_if_branch),
                      static_cast<StatementListNode*>(else_branch));
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

ASTNode* create_struct_def(char* name, char* base_name, ASTNode* members)
{
    return new StructDefNode(std::string(name),
                             base_name ? std::string(base_name) : "",
                             static_cast<StructMemberListNode*>(members));
}

ASTNode* create_struct_member_list(ASTNode* member)
{
    auto list = new StructMemberListNode();
    list->addMember(static_cast<StructMemberNode*>(member));
    return list;
}

ASTNode* add_struct_member(ASTNode* list, ASTNode* member)
{
    auto memberList = static_cast<StructMemberListNode*>(list);
    memberList->addMember(static_cast<StructMemberNode*>(member));
    return memberList;
}

ASTNode* create_struct_member(int is_var, ASTNode* type, char* name,
                              ASTNode* init_expr)
{
    return new StructMemberNode(is_var != 0, static_cast<TypeNode*>(type),
                                std::string(name),
                                static_cast<ExpressionNode*>(init_expr));
}

ASTNode* create_list_type()
{
    return new ListTypeNode();
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
    StatementListNode* stmtList = blockBody ? blockBody->statements : nullptr;

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

    auto* node = new PrintNode(printKind, std::string(format_str));
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
    case TYPE_LIST:
        return "list";
    case TYPE_MAP:
        return "map";
    case TYPE_TUPLE:
        return "tuple";
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
    return result;
}

std::string FunctionDefNode::toString() const
{
    std::string result = "fn " + name + "(" + parameters->toString() + ") -> ";
    result += returnType->toString() + " {\n";
    result += body->toString();
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
    return "let " + name + ": " + type->toString() + " = " +
           expression->toString() + ";";
}

std::string VarDeclNode::toString() const
{
    std::string result = "var " + name + ": " + type->toString();
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
    return result;
}

std::string StructDefNode::toString() const
{
    std::string result = "struct " + name;
    if(!baseName.empty())
    {
        result += " : " + baseName;
    }
    result += " {\n" + members->toString() + "};\n";
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
