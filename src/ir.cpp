#include "ir.h"
#include <iostream>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#if LLVM_VERSION_MAJOR >= 18
#include <llvm/TargetParser/Triple.h>
#else
#include <llvm/ADT/Triple.h>
#endif
#include <llvm/Bitcode/BitcodeWriter.h>

llvm::Type* CodeGenerator::getLLVMType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_VOID:
        return llvm::Type::getVoidTy(context);
    case TypeNode::TYPE_BOOL:
        return llvm::Type::getInt1Ty(context);
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I32:
        return llvm::Type::getInt32Ty(context);
    case TypeNode::TYPE_FLOAT:
        return llvm::Type::getFloatTy(context);
    case TypeNode::TYPE_DOUBLE:
        return llvm::Type::getDoubleTy(context);
    case TypeNode::TYPE_STRING:
#if LLVM_VERSION_MAJOR >= 15
        return llvm::PointerType::get(context, 0);
#else
        return llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    case TypeNode::TYPE_I8:
        return llvm::Type::getInt8Ty(context);
    case TypeNode::TYPE_I16:
        return llvm::Type::getInt16Ty(context);
    case TypeNode::TYPE_I64:
        return llvm::Type::getInt64Ty(context);
    case TypeNode::TYPE_U8:
        return llvm::Type::getInt8Ty(context);
    case TypeNode::TYPE_U16:
        return llvm::Type::getInt16Ty(context);
    case TypeNode::TYPE_U32:
        return llvm::Type::getInt32Ty(context);
    case TypeNode::TYPE_U64:
        return llvm::Type::getInt64Ty(context);
    default:
        return nullptr;
    }
}

bool CodeGenerator::isUnsignedType(TypeNode::TypeKind kind)
{
    switch(kind)
    {
    case TypeNode::TYPE_U8:
    case TypeNode::TYPE_U16:
    case TypeNode::TYPE_U32:
    case TypeNode::TYPE_U64:
        return true;
    default:
        return false;
    }
}

void CodeGenerator::initializeStdioFunctions()
{
    if(stdioInitialized)
        return;

    // Get pointer type for strings
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    // Declare printf: int printf(const char* format, ...)
    llvm::FunctionType* printfType =
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {ptrType},
                                true // variadic
        );
    printfFunc = module->getOrInsertFunction("printf", printfType);

    // Declare fprintf: int fprintf(FILE* stream, const char* format, ...)
    llvm::FunctionType* fprintfType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context), {ptrType, ptrType},
        true // variadic
    );
    fprintfFunc = module->getOrInsertFunction("fprintf", fprintfType);

    // Get stderr - platform specific
    // On macOS/Darwin, stderr is accessed via __stderrp
    // On Linux/other platforms, it's just stderr
#if defined(__APPLE__) || defined(__MACH__)
    const char* stderrName = "__stderrp";
#else
    const char* stderrName = "stderr";
#endif

    stderrPtr = module->getOrInsertGlobal(stderrName, ptrType);
    if(auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(stderrPtr))
    {
        gv->setExternallyInitialized(true);
    }

    stdioInitialized = true;
}

// Helper to get the TypeKind from an expression (for identifiers)
TypeNode::TypeKind getExpressionTypeKind(
    ExpressionNode* expr,
    const std::map<std::string, TypeNode::TypeKind>& variableTypes)
{
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        auto it = variableTypes.find(id->name);
        if(it != variableTypes.end())
        {
            return it->second;
        }
    }
    // Default to signed int for literals and unknown expressions
    return TypeNode::TYPE_INT;
}

std::string
CodeGenerator::convertFormatString(const std::string& mlaFormat,
                                   const std::vector<ExpressionNode*>& args,
                                   std::vector<llvm::Value*>& argValues)
{
    std::string cFormat;
    size_t argIndex = 0;

    for(size_t i = 0; i < mlaFormat.size(); ++i)
    {
        if(mlaFormat[i] == '{' && i + 1 < mlaFormat.size() &&
           mlaFormat[i + 1] == '}')
        {
            // Found a {} placeholder
            if(argIndex < args.size())
            {
                ExpressionNode* argExpr = args[argIndex];
                llvm::Value* argVal = generateExpression(argExpr);
                if(argVal)
                {
                    llvm::Type* argType = argVal->getType();
                    TypeNode::TypeKind typeKind =
                        getExpressionTypeKind(argExpr, variableTypes);
                    bool isUnsigned = isUnsignedType(typeKind);

                    // Determine format specifier based on type
                    if(argType->isIntegerTy(1))
                    {
                        // Bool: convert to int for printing
                        cFormat += "%d";
                        llvm::Value* intVal = builder.CreateZExt(
                            argVal, llvm::Type::getInt32Ty(context),
                            "booltoInt");
                        argValues.push_back(intVal);
                    }
                    else if(argType->isIntegerTy(8))
                    {
                        if(isUnsigned)
                        {
                            cFormat += "%u";
                            llvm::Value* intVal = builder.CreateZExt(
                                argVal, llvm::Type::getInt32Ty(context),
                                "u8toInt");
                            argValues.push_back(intVal);
                        }
                        else
                        {
                            cFormat += "%d";
                            llvm::Value* intVal = builder.CreateSExt(
                                argVal, llvm::Type::getInt32Ty(context),
                                "i8toInt");
                            argValues.push_back(intVal);
                        }
                    }
                    else if(argType->isIntegerTy(16))
                    {
                        if(isUnsigned)
                        {
                            cFormat += "%u";
                            llvm::Value* intVal = builder.CreateZExt(
                                argVal, llvm::Type::getInt32Ty(context),
                                "u16toInt");
                            argValues.push_back(intVal);
                        }
                        else
                        {
                            cFormat += "%d";
                            llvm::Value* intVal = builder.CreateSExt(
                                argVal, llvm::Type::getInt32Ty(context),
                                "i16toInt");
                            argValues.push_back(intVal);
                        }
                    }
                    else if(argType->isIntegerTy(32))
                    {
                        if(isUnsigned)
                        {
                            cFormat += "%u";
                        }
                        else
                        {
                            cFormat += "%d";
                        }
                        argValues.push_back(argVal);
                    }
                    else if(argType->isIntegerTy(64))
                    {
                        if(isUnsigned)
                        {
                            cFormat += "%llu";
                        }
                        else
                        {
                            cFormat += "%lld";
                        }
                        argValues.push_back(argVal);
                    }
                    else if(argType->isFloatTy())
                    {
                        // Float needs to be promoted to double for printf
                        cFormat += "%f";
                        llvm::Value* doubleVal = builder.CreateFPExt(
                            argVal, llvm::Type::getDoubleTy(context),
                            "floatToDouble");
                        argValues.push_back(doubleVal);
                    }
                    else if(argType->isDoubleTy())
                    {
                        cFormat += "%f";
                        argValues.push_back(argVal);
                    }
                    else if(argType->isPointerTy())
                    {
                        // Assume it's a string pointer
                        cFormat += "%s";
                        argValues.push_back(argVal);
                    }
                    else
                    {
                        // Unknown type, try as int
                        cFormat += "%d";
                        argValues.push_back(argVal);
                    }
                }
                argIndex++;
            }
            else
            {
                // More placeholders than arguments, keep literal {}
                cFormat += "{}";
            }
            i++; // Skip the '}'
        }
        else if(mlaFormat[i] == '{' && i + 1 < mlaFormat.size() &&
                mlaFormat[i + 1] == '{')
        {
            // Escaped {{ -> {
            cFormat += '{';
            i++; // Skip the second {
        }
        else if(mlaFormat[i] == '}' && i + 1 < mlaFormat.size() &&
                mlaFormat[i + 1] == '}')
        {
            // Escaped }} -> }
            cFormat += '}';
            i++; // Skip the second }
        }
        else
        {
            cFormat += mlaFormat[i];
        }
    }

    return cFormat;
}

void CodeGenerator::generatePrintStatement(PrintNode* node)
{
    // Initialize stdio functions if not already done
    if(!stdioInitialized)
    {
        initializeStdioFunctions();
    }

    // Convert MLA format string to C format string and collect argument values
    std::vector<llvm::Value*> argValues;
    std::string cFormat =
        convertFormatString(node->formatString, node->arguments, argValues);

    // Add newline for println!/eprintln!
    if(node->kind == PrintNode::PRINTLN_STDOUT ||
       node->kind == PrintNode::EPRINTLN_STDERR)
    {
        cFormat += "\n";
    }

    // Create the format string as a global constant
#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "printfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(cFormat, "printfmt");
#endif

    // Build the argument list
    std::vector<llvm::Value*> printArgs;

    if(node->kind == PrintNode::PRINT_STDERR ||
       node->kind == PrintNode::EPRINTLN_STDERR)
    {
        // For stderr, use fprintf(stderr, format, ...)
        // Load the stderr pointer
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::Value* stderrVal =
            builder.CreateLoad(ptrType, stderrPtr, "stderr");
        printArgs.push_back(stderrVal);
        printArgs.push_back(formatStr);
        printArgs.insert(printArgs.end(), argValues.begin(), argValues.end());

        builder.CreateCall(fprintfFunc, printArgs);
    }
    else
    {
        // For stdout, use printf(format, ...)
        printArgs.push_back(formatStr);
        printArgs.insert(printArgs.end(), argValues.begin(), argValues.end());

        builder.CreateCall(printfFunc, printArgs);
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

    // Generate forward declarations for all functions first
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            generateFunctionDeclaration(funcDef);
        }
    }

    // Then generate all function bodies
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            generateFunctionDefinition(funcDef);
        }
    }
}

llvm::Function*
CodeGenerator::generateFunctionDeclaration(FunctionDefNode* node)
{
    // Check if function already declared
    if(module->getFunction(node->name))
    {
        return module->getFunction(node->name);
    }

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

    return function;
}

llvm::Function* CodeGenerator::generateFunctionDefinition(FunctionDefNode* node)
{
    // Get the function (should already be declared)
    llvm::Function* function = module->getFunction(node->name);
    if(!function)
    {
        // If not declared yet, declare it now
        function = generateFunctionDeclaration(node);
    }

    // Create a new basic block for the function
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear the named values map and constant tracking for new function scope
    namedValues.clear();
    constantVariables.clear();
    variableTypes.clear();

    for(auto& arg : function->args())
    {
        // Allocate space for parameters so they can be modified
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        builder.CreateStore(&arg, alloca);
        namedValues[std::string(arg.getName())] = alloca;
    }

    // Generate the function body
    for(auto stmt : node->body->statements)
    {
        generateStatement(stmt);
    }

    // If the function is void and doesn't have a return, add one
    llvm::Type* returnType = function->getReturnType();
    if(returnType->isVoidTy())
    {
        llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
        if(!currentBlock->getTerminator())
        {
            builder.CreateRetVoid();
        }
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
    else if(auto forNode = dynamic_cast<ForNode*>(node))
    {
        generateForStatement(forNode);
    }
    else if(auto blockNode = dynamic_cast<BlockStatementNode*>(node))
    {
        for(auto stmt : blockNode->statements->statements)
        {
            generateStatement(stmt);
        }
    }
    else if(auto printNode = dynamic_cast<PrintNode*>(node))
    {
        generatePrintStatement(printNode);
    }
    else if(auto exprStmt = dynamic_cast<ExpressionStatementNode*>(node))
    {
        generateExpression(exprStmt->expression);
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

    // Check if we're dealing with floating point or integer types
    bool isFloat =
        L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy();

    switch(node->op)
    {
    case BinaryOpNode::OP_PLUS:
        return isFloat ? builder.CreateFAdd(L, R, "addtmp")
                       : builder.CreateAdd(L, R, "addtmp");
    case BinaryOpNode::OP_MINUS:
        return isFloat ? builder.CreateFSub(L, R, "subtmp")
                       : builder.CreateSub(L, R, "subtmp");
    case BinaryOpNode::OP_MULTIPLY:
        return isFloat ? builder.CreateFMul(L, R, "multmp")
                       : builder.CreateMul(L, R, "multmp");
    case BinaryOpNode::OP_DIVIDE:
        return isFloat ? builder.CreateFDiv(L, R, "divtmp")
                       : builder.CreateSDiv(L, R, "divtmp");
    case BinaryOpNode::OP_LT:
        return isFloat ? builder.CreateFCmpOLT(L, R, "cmptmp")
                       : builder.CreateICmpSLT(L, R, "cmptmp");
    case BinaryOpNode::OP_GT:
        return isFloat ? builder.CreateFCmpOGT(L, R, "cmptmp")
                       : builder.CreateICmpSGT(L, R, "cmptmp");
    case BinaryOpNode::OP_LE:
        return isFloat ? builder.CreateFCmpOLE(L, R, "cmptmp")
                       : builder.CreateICmpSLE(L, R, "cmptmp");
    case BinaryOpNode::OP_GE:
        return isFloat ? builder.CreateFCmpOGE(L, R, "cmptmp")
                       : builder.CreateICmpSGE(L, R, "cmptmp");
    case BinaryOpNode::OP_EQ:
        return isFloat ? builder.CreateFCmpOEQ(L, R, "cmptmp")
                       : builder.CreateICmpEQ(L, R, "cmptmp");
    case BinaryOpNode::OP_NE:
        return isFloat ? builder.CreateFCmpONE(L, R, "cmptmp")
                       : builder.CreateICmpNE(L, R, "cmptmp");
    }
    return nullptr;
}

void CodeGenerator::generateIfStatement(IfNode* node)
{
    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
        return;

    // Convert condition to boolean if necessary
    if(!condValue->getType()->isIntegerTy(1))
    {
        condValue = builder.CreateICmpNE(
            condValue, llvm::ConstantInt::get(condValue->getType(), 0),
            "ifcond");
    }

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

    // Only add branch if block doesn't already have a terminator
    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(mergeBB);
    }

    // Generate 'else' block
    elseBB->insertInto(function);
    builder.SetInsertPoint(elseBB);

    if(node->elseIfBranch)
    {
        // Handle else-if chain
        generateIfStatement(node->elseIfBranch);
    }
    else if(node->elseBranch)
    {
        for(auto stmt : node->elseBranch->statements)
        {
            generateStatement(stmt);
        }
    }

    // Only add branch if block doesn't already have a terminator
    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(mergeBB);
    }

    // Generate merge block
    mergeBB->insertInto(function);
    builder.SetInsertPoint(mergeBB);
}

void CodeGenerator::generateForStatement(ForNode* node)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    // Check if it's a range expression
    auto* rangeExpr = dynamic_cast<RangeExpressionNode*>(node->iterable);
    if(!rangeExpr)
    {
        reportError(
            node->line,
            "for loops currently only support range expressions (start..end)");
        return;
    }

    // Generate start and end values
    llvm::Value* startVal = generateExpression(rangeExpr->start);
    llvm::Value* endVal = generateExpression(rangeExpr->end);

    if(!startVal || !endVal)
    {
        reportError(node->line, "invalid range expression in for loop");
        return;
    }

    llvm::Type* loopType = llvm::Type::getInt64Ty(context);

    // Create alloca for loop variable
    llvm::AllocaInst* loopVar =
        builder.CreateAlloca(loopType, nullptr, node->varName);
    builder.CreateStore(startVal, loopVar);

    // Add loop variable to named values (it's mutable within the loop)
    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = false;
    auto typeIt = variableTypes.find(node->varName);
    if(typeIt != variableTypes.end())
    {
        oldType = typeIt->second;
        hadOldType = true;
    }

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = TypeNode::TYPE_I64; // Loop variable is i32

    // Create basic blocks for loop structure
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    // Branch to condition check
    builder.CreateBr(condBB);

    // Condition block: check if loop variable < end
    builder.SetInsertPoint(condBB);
    llvm::Value* currentVal =
        builder.CreateLoad(loopType, loopVar, node->varName);
    llvm::Value* cond = builder.CreateICmpSLT(currentVal, endVal, "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    // Body block
    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    if(node->body)
    {
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
            // Stop if we hit a terminator (e.g., return)
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
    }

    // Only branch to increment if no terminator
    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(incBB);
    }

    // Increment block
    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextVal = builder.CreateAdd(
        builder.CreateLoad(loopType, loopVar, ""),
        llvm::ConstantInt::get(context, llvm::APInt(32, 1)), "nextval");
    builder.CreateStore(nextVal, loopVar);
    builder.CreateBr(condBB);

    // End block
    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    // Restore old value if there was one
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    // Restore old type if there was one
    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);
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
    // Use 64-bit for literals to support large values; truncation happens at
    // assignment
    return llvm::ConstantInt::get(context, llvm::APInt(64, node->value, true));
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
#if LLVM_VERSION_MAJOR >= 21
    return builder.CreateGlobalString(node->value);
#else
    return builder.CreateGlobalStringPtr(node->value);
#endif
}

llvm::Value* CodeGenerator::generateIdentifier(IdentifierNode* node)
{
    llvm::Value* value = namedValues[node->name];
    if(!value)
    {
        std::cerr << "Unknown variable: " << node->name << std::endl;
        return nullptr;
    }

    // If it's an alloca, load the value
    if(llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
    {
        return builder.CreateLoad(alloca->getAllocatedType(), alloca,
                                  node->name);
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
    if(!initValue)
        return;

    llvm::Type* targetType = getLLVMType(node->type->kind);
    llvm::AllocaInst* alloca =
        builder.CreateAlloca(targetType, nullptr, node->name);

    // Convert init value to target type if necessary
    llvm::Type* initType = initValue->getType();
    if(initType != targetType)
    {
        if(initType->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned initBits = initType->getIntegerBitWidth();
            unsigned targetBits = targetType->getIntegerBitWidth();
            if(initBits > targetBits)
            {
                // Truncate (e.g., i64 -> i8)
                initValue = builder.CreateTrunc(initValue, targetType, "trunc");
            }
            else if(initBits < targetBits)
            {
                // Extend - use ZExt for unsigned target, SExt for signed
                if(isUnsignedType(node->type->kind))
                {
                    initValue =
                        builder.CreateZExt(initValue, targetType, "zext");
                }
                else
                {
                    initValue =
                        builder.CreateSExt(initValue, targetType, "sext");
                }
            }
        }
        else if(initType->isIntegerTy() && targetType->isFloatingPointTy())
        {
            initValue = builder.CreateSIToFP(initValue, targetType, "sitofp");
        }
        else if(initType->isFloatingPointTy() && targetType->isIntegerTy())
        {
            initValue = builder.CreateFPToSI(initValue, targetType, "fptosi");
        }
        else if(initType->isFloatingPointTy() &&
                targetType->isFloatingPointTy())
        {
            initValue = builder.CreateFPCast(initValue, targetType, "fpcast");
        }
    }

    builder.CreateStore(initValue, alloca);
    namedValues[node->name] = alloca;
    variableTypes[node->name] = node->type->kind;

    // Mark this variable as constant (declared with 'let')
    constantVariables.insert(node->name);
}

void CodeGenerator::generateVarDeclaration(VarDeclNode* node)
{
    llvm::Type* targetType = getLLVMType(node->type->kind);
    llvm::AllocaInst* alloca =
        builder.CreateAlloca(targetType, nullptr, node->name);

    if(node->initExpr)
    {
        llvm::Value* initValue = generateExpression(node->initExpr);
        if(initValue)
        {
            // Convert init value to target type if necessary
            llvm::Type* initType = initValue->getType();
            if(initType != targetType)
            {
                if(initType->isIntegerTy() && targetType->isIntegerTy())
                {
                    unsigned initBits = initType->getIntegerBitWidth();
                    unsigned targetBits = targetType->getIntegerBitWidth();
                    if(initBits > targetBits)
                    {
                        // Truncate (e.g., i64 -> i8)
                        initValue =
                            builder.CreateTrunc(initValue, targetType, "trunc");
                    }
                    else if(initBits < targetBits)
                    {
                        // Extend - use ZExt for unsigned target, SExt for
                        // signed
                        if(isUnsignedType(node->type->kind))
                        {
                            initValue = builder.CreateZExt(initValue,
                                                           targetType, "zext");
                        }
                        else
                        {
                            initValue = builder.CreateSExt(initValue,
                                                           targetType, "sext");
                        }
                    }
                }
                else if(initType->isIntegerTy() &&
                        targetType->isFloatingPointTy())
                {
                    initValue =
                        builder.CreateSIToFP(initValue, targetType, "sitofp");
                }
                else if(initType->isFloatingPointTy() &&
                        targetType->isIntegerTy())
                {
                    initValue =
                        builder.CreateFPToSI(initValue, targetType, "fptosi");
                }
                else if(initType->isFloatingPointTy() &&
                        targetType->isFloatingPointTy())
                {
                    initValue =
                        builder.CreateFPCast(initValue, targetType, "fpcast");
                }
            }
            builder.CreateStore(initValue, alloca);
        }
    }

    namedValues[node->name] = alloca;
    variableTypes[node->name] = node->type->kind;
}

void CodeGenerator::generateAssignment(AssignmentNode* node)
{
    // Check if trying to assign to a constant (let) variable
    if(constantVariables.find(node->name) != constantVariables.end())
    {
        reportError(node->line, "cannot assign to constant variable '" +
                                    node->name + "' (declared with 'let')");
        return;
    }

    llvm::Value* value = generateExpression(node->expression);
    llvm::Value* variable = namedValues[node->name];
    if(!variable || !value)
        return;

    if(llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(variable))
    {
        builder.CreateStore(value, alloca);
    }
}

void CodeGenerator::reportError(int line, const std::string& message)
{
    if(line > 0)
    {
        std::cerr << "Error (line " << line << "): " << message << std::endl;
    }
    else
    {
        std::cerr << "Error: " << message << std::endl;
    }
    hasError = true;
}

llvm::Value* CodeGenerator::generateFunctionCall(FunctionCallNode* node)
{
    llvm::Function* callee = module->getFunction(node->name);
    if(!callee)
    {
        std::cerr << "Unknown function: " << node->name << std::endl;
        return nullptr;
    }

    std::vector<llvm::Value*> args;
    for(auto arg : node->arguments)
    {
        llvm::Value* argVal = generateExpression(arg);
        if(!argVal)
            return nullptr;
        args.push_back(argVal);
    }

    if(callee->getReturnType()->isVoidTy())
    {
        return builder.CreateCall(callee, args);
    }
    return builder.CreateCall(callee, args, "calltmp");
}

llvm::Value* CodeGenerator::generateCastExpression(CastExpressionNode* node)
{
    llvm::Value* value = generateExpression(node->expression);
    if(!value)
        return nullptr;

    llvm::Type* targetType = getLLVMType(node->targetType);
    llvm::Type* sourceType = value->getType();

    if(sourceType == targetType)
        return value;

    // Integer to float/double
    if(sourceType->isIntegerTy())
    {
        if(targetType->isFloatTy() || targetType->isDoubleTy())
        {
            return builder.CreateSIToFP(value, targetType, "casttmp");
        }
    }

    // Float/double to integer
    if(sourceType->isFloatingPointTy())
    {
        if(targetType->isIntegerTy())
        {
            return builder.CreateFPToSI(value, targetType, "casttmp");
        }
        // Float to double or double to float
        if(targetType->isFloatingPointTy())
        {
            return builder.CreateFPCast(value, targetType, "casttmp");
        }
    }

    return value;
}

// Backend implementation
Backend::Backend(std::unique_ptr<llvm::Module>& m)
    : module(m), targetMachine(nullptr)
{
    initializeTarget();
}

bool Backend::initializeTarget()
{
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    targetTriple = llvm::sys::getDefaultTargetTriple();

    // Normalize macOS version in triple to avoid linker warnings
    // The default triple may contain a newer macOS version than the linker
    // expects
#if defined(__APPLE__)
    {
        llvm::Triple triple(targetTriple);
        if(triple.isMacOSX())
        {
            // Reset to a base macOS version to avoid version mismatch warnings
            triple.setOSName("macosx10.15.0");
            targetTriple = triple.str();
        }
    }
#endif

#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(llvm::Triple(targetTriple));
#else
    module->setTargetTriple(targetTriple);
#endif

    std::string error;
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(targetTriple, error);

    if(!target)
    {
        std::cerr << "Error looking up target: " << error << std::endl;
        return false;
    }

    std::string cpu = "generic";
    std::string features = "";

    llvm::TargetOptions opt;
    auto relocModel = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);

#if LLVM_VERSION_MAJOR >= 21
    llvm::Triple tripleObj(targetTriple);
    targetMachine =
        target->createTargetMachine(tripleObj, cpu, features, opt, relocModel);
#else
    targetMachine = target->createTargetMachine(targetTriple, cpu, features,
                                                opt, relocModel);
#endif

    if(!targetMachine)
    {
        std::cerr << "Error creating target machine" << std::endl;
        return false;
    }

    module->setDataLayout(targetMachine->createDataLayout());
    return true;
}

bool Backend::emitObjectFile(const std::string& filename)
{
    if(!targetMachine)
    {
        std::cerr << "Target machine not initialized" << std::endl;
        return false;
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pass;
#if LLVM_VERSION_MAJOR >= 18
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CodeGenFileType::ObjectFile))
#else
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CGFT_ObjectFile))
#endif
    {
        std::cerr << "Target machine can't emit object file" << std::endl;
        return false;
    }

    pass.run(*module);
    dest.flush();

    std::cout << "Object file written to: " << filename << std::endl;
    return true;
}

bool Backend::emitAssemblyFile(const std::string& filename)
{
    if(!targetMachine)
    {
        std::cerr << "Target machine not initialized" << std::endl;
        return false;
    }

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pass;
#if LLVM_VERSION_MAJOR >= 18
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CodeGenFileType::AssemblyFile))
#else
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CGFT_AssemblyFile))
#endif
    {
        std::cerr << "Target machine can't emit assembly file" << std::endl;
        return false;
    }

    pass.run(*module);
    dest.flush();

    std::cout << "Assembly file written to: " << filename << std::endl;
    return true;
}

bool Backend::emitLLVMIR(const std::string& filename)
{
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    module->print(dest, nullptr);
    dest.flush();

    std::cout << "LLVM IR written to: " << filename << std::endl;
    return true;
}

bool Backend::emitBitcode(const std::string& filename)
{
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::WriteBitcodeToFile(*module, dest);
    dest.flush();

    std::cout << "Bitcode written to: " << filename << std::endl;
    return true;
}

bool Backend::linkExecutable(const std::string& objectFile,
                             const std::string& outputFile)
{
    // Use system linker (cc/clang/gcc)
    std::string command = "cc -o " + outputFile + " " + objectFile + " 2>&1";
    std::cout << "Linking: " << command << std::endl;

    int result = system(command.c_str());
    if(result != 0)
    {
        std::cerr << "Linking failed with error code: " << result << std::endl;
        return false;
    }

    std::cout << "Executable created: " << outputFile << std::endl;
    return true;
}

bool Backend::compileToExecutable(const std::string& outputFile)
{
    std::string objectFile = outputFile + ".o";

    if(!emitObjectFile(objectFile))
    {
        return false;
    }

    return linkExecutable(objectFile, outputFile);
}

void Backend::optimize(int level)
{
    if(level < 0 || level > 3)
        level = 2;

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::OptimizationLevel optLevel;
    switch(level)
    {
    case 0:
        optLevel = llvm::OptimizationLevel::O0;
        break;
    case 1:
        optLevel = llvm::OptimizationLevel::O1;
        break;
    case 2:
        optLevel = llvm::OptimizationLevel::O2;
        break;
    case 3:
        optLevel = llvm::OptimizationLevel::O3;
        break;
    default:
        optLevel = llvm::OptimizationLevel::O2;
    }

    llvm::ModulePassManager MPM;
    if(level > 0)
    {
        MPM = PB.buildPerModuleDefaultPipeline(optLevel);
    }

    MPM.run(*module, MAM);
    std::cout << "Optimization level O" << level << " applied" << std::endl;
}
