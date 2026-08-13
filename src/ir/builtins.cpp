#include "ir.h"

#include <cstring>

namespace
{

bool program_has_struct(ProgramNode* program, const std::string& name)
{
    if(!program || !program->structList)
        return false;
    for(auto* st : program->structList->structs)
    {
        if(st && st->name == name)
            return true;
    }
    return false;
}

void add_builtin_struct(ProgramNode* program, StructDefNode* def)
{
    if(!program || !def)
        return;
    if(!program->structList)
        program->structList = new StructListNode();
    program->structList->addStruct(def);
}

} // namespace

void CodeGenerator::ensureResultBuiltin(ProgramNode* program)
{
    if(!program || program_has_struct(program, "result"))
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(
        false, new TypeNode(TypeNode::TYPE_BOOL), "is_ok", nullptr));
    members->addMember(
        new StructMemberNode(false, new StructTypeRefNode("T"), "ok", nullptr));
    members->addMember(new StructMemberNode(false, new StructTypeRefNode("E"),
                                            "err", nullptr));

    auto make_self_param = []() -> ParameterNode*
    {
        auto* selfType = new GenericStructTypeRefNode("result");
        selfType->typeArgs.push_back(new StructTypeRefNode("T"));
        selfType->typeArgs.push_back(new StructTypeRefNode("E"));
        return new ParameterNode(selfType, "self");
    };

    auto make_simple_method = [&](const std::string& name, TypeNode* retType,
                                  ExpressionNode* retExpr) -> StructMethodNode*
    {
        auto* params = new ParameterListNode();
        params->parameters.push_back(make_self_param());
        auto* retStmt = new ReturnNode(retExpr);
        auto* body = new StatementListNode();
        body->statements.push_back(retStmt);
        return new StructMethodNode(retType, name, params, body, true, false);
    };

    members->addMethod(make_simple_method(
        "is_ok", new TypeNode(TypeNode::TYPE_BOOL),
        static_cast<ExpressionNode*>(create_field_access_expr(
            new IdentifierNode("self"), strdup("is_ok"), 0))));
    members->addMethod(make_simple_method(
        "is_err", new TypeNode(TypeNode::TYPE_BOOL),
        new BinaryOpNode(BinaryOpNode::OP_EQ,
                         static_cast<ExpressionNode*>(create_field_access_expr(
                             new IdentifierNode("self"), strdup("is_ok"), 0)),
                         new BoolLiteralNode(false))));
    members->addMethod(make_simple_method(
        "unwrap", new StructTypeRefNode("T"),
        static_cast<ExpressionNode*>(create_field_access_expr(
            new IdentifierNode("self"), strdup("ok"), 0))));
    members->addMethod(make_simple_method(
        "unwrap_err", new StructTypeRefNode("E"),
        static_cast<ExpressionNode*>(create_field_access_expr(
            new IdentifierNode("self"), strdup("err"), 0))));

    auto* resultDef = new StructDefNode("result", "", members, true);
    resultDef->typeParams = {"T", "E"};
    resultDef->sourceModule = "";
    add_builtin_struct(program, resultDef);
}

void CodeGenerator::ensureOptionBuiltin(ProgramNode* program)
{
    if(!program || program_has_struct(program, "option"))
        return;

    auto* members = new StructMemberListNode();
    members->addMember(new StructMemberNode(
        false, new TypeNode(TypeNode::TYPE_BOOL), "is_some", nullptr));
    members->addMember(new StructMemberNode(false, new StructTypeRefNode("T"),
                                            "value", nullptr));

    auto* optionDef = new StructDefNode("option", "", members, true);
    optionDef->typeParams = {"T"};
    optionDef->sourceModule = "";
    add_builtin_struct(program, optionDef);
}
