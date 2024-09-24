#include "ir.h"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>

llvm::Type* CodeGenerator::getLLVMType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_VOID:
        return llvm::Type::getVoidTy(context);
    case TypeNode::TYPE_BOOL:
        return llvm::Type::getInt1Ty(context);
    case TypeNode::TYPE_INT:
        return llvm::Type::getInt32Ty(context);
    case TypeNode::TYPE_FLOAT:
        return llvm::Type::getFloatTy(context);
    case TypeNode::TYPE_DOUBLE:
        return llvm::Type::getDoubleTy(context);
    case TypeNode::TYPE_STRING:
        // Create a pointer to i8 (char*)
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
    default:
        return nullptr;
    }
}

void CodeGenerator::generateCode(ProgramNode* program)
{
    // First generate all struct definitions
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            generateStructDefinition(structDef);
        }
    }

    // Then generate all function definitions
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            generateFunctionDefinition(funcDef);
        }
    }
}

llvm::Function* CodeGenerator::generateFunctionDefinition(FunctionDefNode* node)
{
    std::vector<llvm::Type*> paramTypes;
    for(auto param : node->parameters->parameters)
    {
        paramTypes.push_back(getLLVMType(param->type->kind));
    }

    llvm::Type* returnType = getLLVMType(node->returnType->kind);
    llvm::FunctionType* funcType =
        llvm::FunctionType::get(returnType, paramTypes, false);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, node->name, module.get());

    // Set parameter names
    unsigned idx = 0;
    for(auto& arg : function->args())
    {
        arg.setName(node->parameters->parameters[idx++]->name);
    }

    // Create a new basic block for the function
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear the named values map and add the parameters
    namedValues.clear();
    for(auto& arg : function->args())
    {
        namedValues[std::string(arg.getName())] = &arg;
    }

    // Generate the function body
    for(auto stmt : node->body->statements)
    {
        generateStatement(stmt);
    }

    // Verify the function
    llvm::verifyFunction(*function);
    return function;
}

void CodeGenerator::generateStatement(StatementNode* node)
{
    if(auto returnNode = dynamic_cast<ReturnNode*>(node))
    {
        generateReturnStatement(returnNode);
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
    else if(auto ifNode = dynamic_cast<IfNode*>(node))
    {
        generateIfStatement(ifNode);
    }
}

llvm::Value* CodeGenerator::generateExpression(ExpressionNode* node)
{
    if(auto intLit = dynamic_cast<IntLiteralNode*>(node))
    {
        return generateIntLiteral(intLit);
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
    else if(auto binOp = dynamic_cast<BinaryOpNode*>(node))
    {
        return generateBinaryOp(binOp);
    }
    else if(auto id = dynamic_cast<IdentifierNode*>(node))
    {
        return generateIdentifier(id);
    }
    else if(auto call = dynamic_cast<FunctionCallNode*>(node))
    {
        return generateFunctionCall(call);
    }
    else if(auto cast = dynamic_cast<CastExpressionNode*>(node))
    {
        return generateCastExpression(cast);
    }
    return nullptr;
}

llvm::Value* CodeGenerator::generateBinaryOp(BinaryOpNode* node)
{
    llvm::Value* L = generateExpression(node->left);
    llvm::Value* R = generateExpression(node->right);

    if(!L || !R)
        return nullptr;

    switch(node->op)
    {
    case BinaryOpNode::OP_PLUS:
        return builder.CreateAdd(L, R, "addtmp");
    case BinaryOpNode::OP_MINUS:
        return builder.CreateSub(L, R, "subtmp");
    case BinaryOpNode::OP_MULTIPLY:
        return builder.CreateMul(L, R, "multmp");
    case BinaryOpNode::OP_DIVIDE:
        return builder.CreateSDiv(L, R, "divtmp");
    case BinaryOpNode::OP_LT:
        return builder.CreateICmpSLT(L, R, "cmptmp");
    case BinaryOpNode::OP_GT:
        return builder.CreateICmpSGT(L, R, "cmptmp");
    case BinaryOpNode::OP_LE:
        return builder.CreateICmpSLE(L, R, "cmptmp");
    case BinaryOpNode::OP_GE:
        return builder.CreateICmpSGE(L, R, "cmptmp");
    case BinaryOpNode::OP_EQ:
        return builder.CreateICmpEQ(L, R, "cmptmp");
    case BinaryOpNode::OP_NE:
        return builder.CreateICmpNE(L, R, "cmptmp");
    }
    return nullptr;
}

void CodeGenerator::generateIfStatement(IfNode* node)
{
    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
        return;

    llvm::Function* function = builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* thenBB =
        llvm::BasicBlock::Create(context, "then", function);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context, "else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "ifcont");

    builder.CreateCondBr(condValue, thenBB, elseBB);

    // Generate 'then' block
    builder.SetInsertPoint(thenBB);
    for(auto stmt : node->thenBranch->statements)
    {
        generateStatement(stmt);
    }
    builder.CreateBr(mergeBB);

    // Generate 'else' block
    // Instead of using getBasicBlockList directly, insert the block into the
    // function
    elseBB->insertInto(function);
    builder.SetInsertPoint(elseBB);
    if(node->elseBranch)
    {
        for(auto stmt : node->elseBranch->statements)
        {
            generateStatement(stmt);
        }
    }
    builder.CreateBr(mergeBB);

    // Generate merge block
    // Insert the merge block into the function
    mergeBB->insertInto(function);
    builder.SetInsertPoint(mergeBB);
}

void CodeGenerator::generateReturnStatement(ReturnNode* node)
{
    if(node->expression)
    {
        llvm::Value* returnValue = generateExpression(node->expression);
        builder.CreateRet(returnValue);
    }
    else
    {
        builder.CreateRetVoid();
    }
}

llvm::Value* CodeGenerator::generateIntLiteral(IntLiteralNode* node)
{
    return llvm::ConstantInt::get(context, llvm::APInt(32, node->value, true));
}

llvm::Value* CodeGenerator::generateFloatLiteral(FloatLiteralNode* node)
{
    return llvm::ConstantFP::get(context, llvm::APFloat(node->value));
}

llvm::Value* CodeGenerator::generateDoubleLiteral(DoubleLiteralNode* node)
{
    return llvm::ConstantFP::get(context, llvm::APFloat(node->value));
}

llvm::Value* CodeGenerator::generateStringLiteral(StringLiteralNode* node)
{
    return builder.CreateGlobalStringPtr(node->value);
}

llvm::Value* CodeGenerator::generateIdentifier(IdentifierNode* node)
{
    llvm::Value* value = namedValues[node->name];
    if(!value)
    {
        // Check if it's a global variable
        value = module->getGlobalVariable(node->name);
    }
    return value;
}

void CodeGenerator::generateStructDefinition(StructDefNode* node)
{
    std::vector<llvm::Type*> memberTypes;
    for(auto member : node->members->members)
    {
        memberTypes.push_back(getLLVMType(member->type->kind));
    }

    llvm::StructType* structType =
        llvm::StructType::create(context, memberTypes, node->name);
    structTypes[node->name] = structType;
}

llvm::StructType* CodeGenerator::getStructType(const std::string& name)
{
    auto it = structTypes.find(name);
    if(it != structTypes.end())
    {
        return llvm::cast<llvm::StructType>(it->second);
    }
    return nullptr;
}

void CodeGenerator::generateLetDeclaration(LetDeclNode* node)
{
    llvm::Value* initValue = generateExpression(node->expression);
    llvm::AllocaInst* alloca = builder.CreateAlloca(
        getLLVMType(node->type->kind), nullptr, node->name);
    builder.CreateStore(initValue, alloca);
    namedValues[node->name] = alloca;
}

void CodeGenerator::generateVarDeclaration(VarDeclNode* node)
{
    llvm::AllocaInst* alloca = builder.CreateAlloca(
        getLLVMType(node->type->kind), nullptr, node->name);
    if(node->initExpr)
    {
        llvm::Value* initValue = generateExpression(node->initExpr);
        builder.CreateStore(initValue, alloca);
    }
    namedValues[node->name] = alloca;
}

void CodeGenerator::generateAssignment(AssignmentNode* node)
{
    llvm::Value* value = generateExpression(node->expression);
    llvm::Value* variable = namedValues[node->name];
    if(llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(variable))
    {
        builder.CreateStore(value, alloca);
    }
}

llvm::Value* CodeGenerator::generateFunctionCall(FunctionCallNode* node)
{
    llvm::Function* callee = module->getFunction(node->name);
    if(!callee)
        return nullptr;

    std::vector<llvm::Value*> args;
    for(auto arg : node->arguments)
    {
        args.push_back(generateExpression(arg));
    }

    return builder.CreateCall(callee, args, "calltmp");
}

llvm::Value* CodeGenerator::generateCastExpression(CastExpressionNode* node)
{
    llvm::Value* value = generateExpression(node->expression);
    llvm::Type* targetType = getLLVMType(node->targetType);

    if(!value || !targetType)
        return nullptr;

    // Handle different type conversions
    if(value->getType()->isIntegerTy() && targetType->isFloatingPointTy())
    {
        return builder.CreateSIToFP(value, targetType, "casttmp");
    }
    else if(value->getType()->isFloatingPointTy() && targetType->isIntegerTy())
    {
        return builder.CreateFPToSI(value, targetType, "casttmp");
    }
    else if(value->getType()->isFloatingPointTy() &&
            targetType->isFloatingPointTy())
    {
        return builder.CreateFPCast(value, targetType, "casttmp");
    }
    else if(value->getType()->isIntegerTy() && targetType->isIntegerTy())
    {
        return builder.CreateIntCast(value, targetType, true, "casttmp");
    }

    return nullptr;
}
