#include "ir.h"
#include <functional>
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

llvm::Type* CodeGenerator::getLLVMTypeFromNode(TypeNode* typeNode)
{
    if(!typeNode)
        return nullptr;

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    // Must check this BEFORE StructTypeRefNode since GenericStructTypeRefNode
    // is a more specific case
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(typeNode))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        auto it = structTypes.find(mangledName);
        if(it != structTypes.end())
        {
            return it->second;
        }
        std::cerr << "Failed to monomorphize struct: "
                  << genStructRef->structName << std::endl;
        return nullptr;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(typeNode))
    {
        auto it = structTypes.find(structRef->structName);
        if(it != structTypes.end())
        {
            return it->second;
        }

        // Check if this is a type parameter (like T, U) - should not reach here
        // in properly monomorphized code
        std::cerr << "Unknown struct type: " << structRef->structName
                  << std::endl;
        return nullptr;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(typeNode))
    {
        std::vector<llvm::Type*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
                return nullptr;
            elemTypes.push_back(elemType);
        }
        return llvm::StructType::get(context, elemTypes);
    }

    // Handle generic list type
    if(auto* listType = dynamic_cast<GenericListTypeNode*>(typeNode))
    {
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* elemType = getLLVMTypeFromNode(listType->elementType);
        llvm::Type* ptrType = llvm::PointerType::get(elemType, 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        return llvm::StructType::get(context, listStructTypes);
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(typeNode))
    {
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        return llvm::StructType::get(context, mapStructTypes);
    }

    // Fall back to basic type kind
    return getLLVMType(typeNode->kind);
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
    // First, collect generic struct templates and impl blocks
    // These are NOT generated immediately - they're instantiated on demand
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(structDef->isGeneric())
            {
                // Store as template for later instantiation
                genericStructTemplates[structDef->name] = structDef;
            }
        }
    }

    // Collect generic impl blocks
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(!impl->typeParams.empty())
            {
                // This is a generic impl block
                genericImplBlocks[impl->structName].push_back(impl);
            }
        }
    }

    // Generate all NON-GENERIC struct definitions
    // We need to process base structs before derived structs
    if(program->structList)
    {
        // Build a map of struct names to their definitions
        std::map<std::string, StructDefNode*> structMap;
        for(auto structDef : program->structList->structs)
        {
            structMap[structDef->name] = structDef;
        }

        // Process structs in dependency order (bases before derived)
        std::set<std::string> processed;
        std::function<void(StructDefNode*)> processStruct =
            [&](StructDefNode* structDef)
        {
            if(processed.count(structDef->name))
                return;

            // Skip generic structs - they're instantiated on demand
            if(structDef->isGeneric())
            {
                processed.insert(structDef->name);
                return;
            }

            // Process base first if it exists
            if(!structDef->baseName.empty())
            {
                auto baseIt = structMap.find(structDef->baseName);
                if(baseIt != structMap.end())
                {
                    processStruct(baseIt->second);
                }
            }

            // Track struct visibility
            structVisibility[structDef->name] =
                std::make_pair(structDef->isPublic, structDef->sourceModule);
            generateStructDefinition(structDef);
            processed.insert(structDef->name);
        };

        for(auto structDef : program->structList->structs)
        {
            processStruct(structDef);
        }
    }

    // Track function visibility before generating declarations
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            // Track function visibility
            functionVisibility[funcDef->name] =
                std::make_pair(funcDef->isPublic, funcDef->sourceModule);
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

    // Generate NON-GENERIC struct method declarations and track visibility
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric())
            {
                generateStructMethods(structDef);
            }
        }
    }

    // Process non-generic impl blocks (add methods to existing structs)
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                // Non-generic impl block - process immediately
                for(auto method : impl->methods)
                {
                    // Register the method with the struct
                    structMethods[impl->structName][method->name] =
                        std::make_pair(method->isPublic, method);

                    // Generate method declaration
                    generateMethodDeclaration(impl->structName, method);
                }
            }
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

    // Generate NON-GENERIC struct method bodies
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric() && structDef->members)
            {
                for(auto method : structDef->members->methods)
                {
                    generateMethodDefinition(structDef->name, method);
                }
            }
        }
    }

    // Generate non-generic impl block method bodies
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                for(auto method : impl->methods)
                {
                    generateMethodDefinition(impl->structName, method);
                }
            }
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
        llvm::Type* paramType = getLLVMTypeFromNode(param->type);
        if(!paramType)
        {
            reportError(param->line,
                        "unknown parameter type for '" + param->name + "'");
            paramType = llvm::Type::getInt32Ty(context); // fallback
        }
        paramTypes.push_back(paramType);
    }

    llvm::Type* returnType = getLLVMTypeFromNode(node->returnType);
    if(!returnType)
    {
        reportError(node->line,
                    "unknown return type for function '" + node->name + "'");
        returnType = llvm::Type::getVoidTy(context); // fallback
    }

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

    // Check if function already has a body (was already defined)
    if(!function->empty())
    {
        // Function already defined, skip
        return function;
    }

    // Check if this function has a body to generate
    if(!node->body)
    {
        // No body - this shouldn't happen for valid functions
        reportError(node->line, "function '" + node->name + "' has no body");
        return function;
    }

    // Track which module this function is from (for visibility checks)
    std::string savedModule = currentModule;
    currentModule = node->sourceModule;

    // Create a new basic block for the function
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear the named values map and constant tracking for new function scope
    namedValues.clear();
    constantVariables.clear();
    variableTypes.clear();

    // Set up parameters
    unsigned paramIdx = 0;
    for(auto& arg : function->args())
    {
        // Allocate space for parameters so they can be modified
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        builder.CreateStore(&arg, alloca);
        namedValues[std::string(arg.getName())] = alloca;

        // Track parameter types
        if(paramIdx < node->parameters->parameters.size())
        {
            variableTypes[std::string(arg.getName())] =
                node->parameters->parameters[paramIdx]->type->kind;
        }
        paramIdx++;
    }

    // Generate the function body
    if(node->body)
    {
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
        }
    }

    // If the function is void and doesn't have a return, add one
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!currentBlock->getTerminator())
    {
        if(returnType->isVoidTy())
        {
            builder.CreateRetVoid();
        }
        else
        {
            // For non-void functions without a return, add unreachable
            // This indicates a bug in the source code but prevents LLVM crashes
            builder.CreateUnreachable();
        }
    }

    // Restore the previous module context
    currentModule = savedModule;

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
    else if(auto fieldAssignNode = dynamic_cast<FieldAssignmentNode*>(node))
    {
        generateFieldAssignment(fieldAssignNode);
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
    else if(auto breakNode = dynamic_cast<BreakNode*>(node))
    {
        generateBreakStatement(breakNode);
    }
    else if(auto continueNode = dynamic_cast<ContinueNode*>(node))
    {
        generateContinueStatement(continueNode);
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
    else if(auto boolLit = dynamic_cast<BoolLiteralNode*>(node))
    {
        return generateBoolLiteral(boolLit);
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
    else if(auto fieldAcc = dynamic_cast<FieldAccessNode*>(node))
    {
        return generateFieldAccess(fieldAcc);
    }
    else if(auto call = dynamic_cast<FunctionCallNode*>(node))
    {
        return generateFunctionCall(call);
    }
    else if(auto methodCall = dynamic_cast<MethodCallNode*>(node))
    {
        return generateMethodCall(methodCall);
    }
    else if(auto cast = dynamic_cast<CastExpressionNode*>(node))
    {
        return generateCastExpression(cast);
    }
    else if(auto listLit = dynamic_cast<ListLiteralNode*>(node))
    {
        return generateListLiteral(listLit);
    }
    else if(auto mapLit = dynamic_cast<MapLiteralNode*>(node))
    {
        return generateMapLiteral(mapLit);
    }
    else if(auto indexExpr = dynamic_cast<IndexExpressionNode*>(node))
    {
        return generateIndexExpression(indexExpr);
    }
    else if(auto tupleLit = dynamic_cast<TupleLiteralNode*>(node))
    {
        return generateTupleLiteral(tupleLit);
    }
    else if(auto tupleAcc = dynamic_cast<TupleAccessNode*>(node))
    {
        return generateTupleAccess(tupleAcc);
    }
    else if(auto structLit = dynamic_cast<StructLiteralNode*>(node))
    {
        return generateStructLiteral(structLit);
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

    // Handle type mismatches for integer operands
    if(!isFloat && L->getType()->isIntegerTy() && R->getType()->isIntegerTy())
    {
        unsigned LBits = L->getType()->getIntegerBitWidth();
        unsigned RBits = R->getType()->getIntegerBitWidth();

        if(LBits != RBits)
        {
            // Extend the smaller type to match the larger type
            if(LBits > RBits)
            {
                R = builder.CreateSExt(R, L->getType(), "sext");
            }
            else
            {
                L = builder.CreateSExt(L, R->getType(), "sext");
            }
        }
    }

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
    if(rangeExpr)
    {
        // Generate start and end values
        llvm::Value* startVal = generateExpression(rangeExpr->start);
        llvm::Value* endVal = generateExpression(rangeExpr->end);

        if(!startVal || !endVal)
        {
            reportError(node->line, "invalid range expression in for loop");
            return;
        }

        llvm::Type* loopType = llvm::Type::getInt64Ty(context);

        // Extend start and end values to i64 if necessary
        if(startVal->getType() != loopType)
        {
            if(startVal->getType()->isIntegerTy())
            {
                startVal = builder.CreateSExt(startVal, loopType, "start.ext");
            }
        }
        if(endVal->getType() != loopType)
        {
            if(endVal->getType()->isIntegerTy())
            {
                endVal = builder.CreateSExt(endVal, loopType, "end.ext");
            }
        }

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
        variableTypes[node->varName] = TypeNode::TYPE_I64;

        // Create basic blocks for loop structure
        llvm::BasicBlock* condBB =
            llvm::BasicBlock::Create(context, "for.cond", function);
        llvm::BasicBlock* bodyBB =
            llvm::BasicBlock::Create(context, "for.body");
        llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

        // Push loop blocks for break/continue support
        loopBreakBlocks.push_back(endBB);
        loopContinueBlocks.push_back(incBB);

        // Branch to condition check
        builder.CreateBr(condBB);

        // Condition block: check if loop variable < end (exclusive) or <= end
        // (inclusive)
        builder.SetInsertPoint(condBB);
        llvm::Value* currentVal =
            builder.CreateLoad(loopType, loopVar, node->varName);
        llvm::Value* cond;
        if(rangeExpr->inclusive)
        {
            cond = builder.CreateICmpSLE(currentVal, endVal, "loopcond");
        }
        else
        {
            cond = builder.CreateICmpSLT(currentVal, endVal, "loopcond");
        }
        builder.CreateCondBr(cond, bodyBB, endBB);

        // Body block
        bodyBB->insertInto(function);
        builder.SetInsertPoint(bodyBB);

        if(node->body)
        {
            for(auto stmt : node->body->statements)
            {
                generateStatement(stmt);
                if(builder.GetInsertBlock()->getTerminator())
                    break;
            }
        }

        if(!builder.GetInsertBlock()->getTerminator())
        {
            builder.CreateBr(incBB);
        }

        // Increment block
        incBB->insertInto(function);
        builder.SetInsertPoint(incBB);
        llvm::Value* nextVal = builder.CreateAdd(
            builder.CreateLoad(loopType, loopVar, ""),
            llvm::ConstantInt::get(context, llvm::APInt(64, 1)), "nextval");
        builder.CreateStore(nextVal, loopVar);
        builder.CreateBr(condBB);

        // End block
        endBB->insertInto(function);
        builder.SetInsertPoint(endBB);

        // Pop loop blocks
        loopBreakBlocks.pop_back();
        loopContinueBlocks.pop_back();

        // Restore old value
        if(oldVal)
            namedValues[node->varName] = oldVal;
        else
            namedValues.erase(node->varName);

        if(hadOldType)
            variableTypes[node->varName] = oldType;
        else
            variableTypes.erase(node->varName);
    }
    else if(auto* listLit = dynamic_cast<ListLiteralNode*>(node->iterable))
    {
        // Iterating over a list literal: for x in [1, 2, 3] { ... }
        generateForListLiteralIteration(node, listLit);
    }
    else if(auto* identifier = dynamic_cast<IdentifierNode*>(node->iterable))
    {
        // Check if it's a map variable (iterate over entries by default)
        auto mapIt = mapKeyValueTypes.find(identifier->name);
        if(mapIt != mapKeyValueTypes.end())
        {
            // Create a map entries iterator for direct map iteration
            auto* entriesIter =
                new MapIteratorNode(identifier, MapIteratorNode::ITER_ENTRIES);
            generateForMapIteration(node, entriesIter);
            delete entriesIter;
        }
        else
        {
            // Iterating over a list variable: for x in myList { ... }
            generateForListVariableIteration(node, identifier);
        }
    }
    else if(auto* mapIter = dynamic_cast<MapIteratorNode*>(node->iterable))
    {
        // Iterating over map.keys(), map.values(), or map.entries()
        generateForMapIteration(node, mapIter);
    }
    else
    {
        reportError(
            node->line,
            "for loops support range expressions (start..end), list "
            "iteration, and map iteration (.keys(), .values(), .entries())");
    }
}

void CodeGenerator::generateForListLiteralIteration(ForNode* node,
                                                    ListLiteralNode* listLit)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    if(!listLit->elements || listLit->elements->elements.empty())
    {
        // Empty list, nothing to iterate
        return;
    }

    // Generate all list elements first
    std::vector<llvm::Value*> elementValues;
    llvm::Type* elementType = nullptr;

    for(auto* elem : listLit->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
        {
            reportError(node->line, "failed to generate list element");
            return;
        }
        if(!elementType)
        {
            elementType = val->getType();
        }
        elementValues.push_back(val);
    }

    // Create index variable
    llvm::Type* indexType = llvm::Type::getInt64Ty(context);
    llvm::AllocaInst* indexVar =
        builder.CreateAlloca(indexType, nullptr, "idx");
    builder.CreateStore(llvm::ConstantInt::get(indexType, 0), indexVar);

    // Store list size
    int64_t listSize = static_cast<int64_t>(elementValues.size());

    // Create alloca for loop variable (the element)
    llvm::AllocaInst* loopVar =
        builder.CreateAlloca(elementType, nullptr, node->varName);

    // Save old values
    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = variableTypes.find(node->varName) != variableTypes.end();
    if(hadOldType)
        oldType = variableTypes[node->varName];

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = TypeNode::TYPE_I64; // Placeholder

    // Create basic blocks
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(incBB);

    builder.CreateBr(condBB);

    // Condition: index < size
    builder.SetInsertPoint(condBB);
    llvm::Value* currentIdx = builder.CreateLoad(indexType, indexVar, "idx");
    llvm::Value* sizeVal = llvm::ConstantInt::get(indexType, listSize);
    llvm::Value* cond = builder.CreateICmpSLT(currentIdx, sizeVal, "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    // Body: load element and execute body
    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    // Use switch to select the right element based on index
    // For small lists, we can use a series of comparisons
    llvm::Value* elemVal = elementValues[0]; // Default
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        llvm::Value* idxConst = llvm::ConstantInt::get(indexType, i);
        llvm::Value* isThis =
            builder.CreateICmpEQ(currentIdx, idxConst, "iseq");
        elemVal = builder.CreateSelect(isThis, elementValues[i], elemVal,
                                       "selectelem");
    }
    builder.CreateStore(elemVal, loopVar);

    if(node->body)
    {
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
    }

    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(incBB);
    }

    // Increment
    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextIdx =
        builder.CreateAdd(builder.CreateLoad(indexType, indexVar, ""),
                          llvm::ConstantInt::get(indexType, 1), "nextidx");
    builder.CreateStore(nextIdx, indexVar);
    builder.CreateBr(condBB);

    // End
    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    // Restore
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);
}

void CodeGenerator::generateForListVariableIteration(ForNode* node,
                                                     IdentifierNode* listId)
{
    // For iterating over a list variable, we need the list structure
    // Lists are stored as: { i64 size, ptr data }
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    llvm::Value* listPtr = namedValues[listId->name];
    if(!listPtr)
    {
        reportError(node->line, "unknown list variable: " + listId->name);
        return;
    }

    // Get the list struct type info
    auto it = listElementTypes.find(listId->name);
    if(it == listElementTypes.end())
    {
        reportError(node->line,
                    "cannot iterate: unknown element type for list '" +
                        listId->name + "'");
        return;
    }

    TypeNode* elemTypeNode = it->second;
    llvm::Type* elementType = getLLVMType(elemTypeNode->kind);

    // Load list pointer (which points to the list struct)
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType = llvm::PointerType::get(elementType, 0);
#endif

    // List struct: { i64 size, ptr data }
    std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
    llvm::StructType* listStructType =
        llvm::StructType::get(context, listStructTypes);

    // Load the list struct
    llvm::Value* listStruct =
        builder.CreateLoad(listStructType, listPtr, "list");

    // Extract size and data pointer
    llvm::Value* listSize = builder.CreateExtractValue(listStruct, 0, "size");
    llvm::Value* dataPtr = builder.CreateExtractValue(listStruct, 1, "data");

    // Create index variable
    llvm::AllocaInst* indexVar = builder.CreateAlloca(i64Type, nullptr, "idx");
    builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), indexVar);

    // Create loop variable
    llvm::AllocaInst* loopVar =
        builder.CreateAlloca(elementType, nullptr, node->varName);

    // Save old values
    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = variableTypes.find(node->varName) != variableTypes.end();
    if(hadOldType)
        oldType = variableTypes[node->varName];

    namedValues[node->varName] = loopVar;
    variableTypes[node->varName] = elemTypeNode->kind;

    // Create blocks
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(incBB);

    builder.CreateBr(condBB);

    // Condition
    builder.SetInsertPoint(condBB);
    llvm::Value* currentIdx = builder.CreateLoad(i64Type, indexVar, "idx");
    llvm::Value* cond = builder.CreateICmpSLT(currentIdx, listSize, "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    // Body
    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    // Load element at index
    llvm::Value* elemPtr =
        builder.CreateGEP(elementType, dataPtr, currentIdx, "elemptr");
    llvm::Value* elemVal = builder.CreateLoad(elementType, elemPtr, "elem");
    builder.CreateStore(elemVal, loopVar);

    if(node->body)
    {
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
    }

    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(incBB);
    }

    // Increment
    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextIdx =
        builder.CreateAdd(builder.CreateLoad(i64Type, indexVar, ""),
                          llvm::ConstantInt::get(i64Type, 1), "nextidx");
    builder.CreateStore(nextIdx, indexVar);
    builder.CreateBr(condBB);

    // End
    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    // Restore
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);
}

void CodeGenerator::generateForMapIteration(ForNode* node,
                                            MapIteratorNode* mapIter)
{
    llvm::Function* function = builder.GetInsertBlock()->getParent();

    // Get the map variable
    auto* mapId = dynamic_cast<IdentifierNode*>(mapIter->mapExpr);
    if(!mapId)
    {
        reportError(node->line, "map iteration requires a map variable");
        return;
    }

    llvm::Value* mapPtr = namedValues[mapId->name];
    if(!mapPtr)
    {
        reportError(node->line, "unknown map variable: " + mapId->name);
        return;
    }

    // Get map key/value types
    auto it = mapKeyValueTypes.find(mapId->name);
    if(it == mapKeyValueTypes.end())
    {
        reportError(node->line,
                    "cannot iterate: '" + mapId->name + "' is not a map");
        return;
    }

    TypeNode* keyTypeNode = it->second.first;
    TypeNode* valTypeNode = it->second.second;
    llvm::Type* keyType = getLLVMType(keyTypeNode->kind);
    llvm::Type* valueType = getLLVMType(valTypeNode->kind);

    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType = llvm::PointerType::get(keyType, 0);
#endif

    // Map struct: { i64 size, ptr keys, ptr values }
    std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
    llvm::StructType* mapStructType =
        llvm::StructType::get(context, mapStructTypes);

    // Load map struct
    llvm::Value* mapStruct = builder.CreateLoad(mapStructType, mapPtr, "map");
    llvm::Value* mapSize = builder.CreateExtractValue(mapStruct, 0, "size");
    llvm::Value* keysPtr = builder.CreateExtractValue(mapStruct, 1, "keys");
    llvm::Value* valsPtr = builder.CreateExtractValue(mapStruct, 2, "vals");

    // Create index variable
    llvm::AllocaInst* indexVar = builder.CreateAlloca(i64Type, nullptr, "idx");
    builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), indexVar);

    // Determine loop variable type based on iterator kind
    llvm::Type* loopVarType = nullptr;
    llvm::AllocaInst* loopVar = nullptr;
    llvm::AllocaInst* loopVar2 = nullptr; // For entries (key, value pair)

    // Save old values
    llvm::Value* oldVal = namedValues[node->varName];
    TypeNode::TypeKind oldType = TypeNode::TYPE_VOID;
    bool hadOldType = variableTypes.find(node->varName) != variableTypes.end();
    if(hadOldType)
        oldType = variableTypes[node->varName];

    switch(mapIter->kind)
    {
    case MapIteratorNode::ITER_KEYS:
        loopVarType = keyType;
        loopVar = builder.CreateAlloca(keyType, nullptr, node->varName);
        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = keyTypeNode->kind;
        break;

    case MapIteratorNode::ITER_VALUES:
        loopVarType = valueType;
        loopVar = builder.CreateAlloca(valueType, nullptr, node->varName);
        namedValues[node->varName] = loopVar;
        variableTypes[node->varName] = valTypeNode->kind;
        break;

    case MapIteratorNode::ITER_ENTRIES:
        // For entries, we create a tuple (key, value)
        {
            std::vector<llvm::Type*> entryTypes = {keyType, valueType};
            llvm::StructType* entryStructType =
                llvm::StructType::get(context, entryTypes);
            loopVar =
                builder.CreateAlloca(entryStructType, nullptr, node->varName);
            namedValues[node->varName] = loopVar;
            variableTypes[node->varName] = TypeNode::TYPE_TUPLE;

            // Store element types for tuple access
            std::vector<TypeNode*> elemTypes = {keyTypeNode, valTypeNode};
            tupleElementTypes[node->varName] = elemTypes;
        }
        break;
    }

    // Create basic blocks
    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "for.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "for.body");
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "for.inc");
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "for.end");

    loopBreakBlocks.push_back(endBB);
    loopContinueBlocks.push_back(incBB);

    builder.CreateBr(condBB);

    // Condition
    builder.SetInsertPoint(condBB);
    llvm::Value* currentIdx = builder.CreateLoad(i64Type, indexVar, "idx");
    llvm::Value* cond = builder.CreateICmpSLT(currentIdx, mapSize, "loopcond");
    builder.CreateCondBr(cond, bodyBB, endBB);

    // Body
    bodyBB->insertInto(function);
    builder.SetInsertPoint(bodyBB);

    // Load the appropriate value(s) based on iterator kind
    switch(mapIter->kind)
    {
    case MapIteratorNode::ITER_KEYS:
    {
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysPtr, currentIdx, "keyptr");
        llvm::Value* keyVal = builder.CreateLoad(keyType, keyPtr, "key");
        builder.CreateStore(keyVal, loopVar);
    }
    break;

    case MapIteratorNode::ITER_VALUES:
    {
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsPtr, currentIdx, "valptr");
        llvm::Value* valVal = builder.CreateLoad(valueType, valPtr, "val");
        builder.CreateStore(valVal, loopVar);
    }
    break;

    case MapIteratorNode::ITER_ENTRIES:
    {
        // Load both key and value, create tuple
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysPtr, currentIdx, "keyptr");
        llvm::Value* keyVal = builder.CreateLoad(keyType, keyPtr, "key");

        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsPtr, currentIdx, "valptr");
        llvm::Value* valVal = builder.CreateLoad(valueType, valPtr, "val");

        std::vector<llvm::Type*> entryTypes = {keyType, valueType};
        llvm::StructType* entryStructType =
            llvm::StructType::get(context, entryTypes);

        llvm::Value* entryVal = llvm::UndefValue::get(entryStructType);
        entryVal = builder.CreateInsertValue(entryVal, keyVal, 0, "entry.key");
        entryVal = builder.CreateInsertValue(entryVal, valVal, 1, "entry.val");
        builder.CreateStore(entryVal, loopVar);
    }
    break;
    }

    if(node->body)
    {
        for(auto stmt : node->body->statements)
        {
            generateStatement(stmt);
            if(builder.GetInsertBlock()->getTerminator())
                break;
        }
    }

    if(!builder.GetInsertBlock()->getTerminator())
    {
        builder.CreateBr(incBB);
    }

    // Increment
    incBB->insertInto(function);
    builder.SetInsertPoint(incBB);
    llvm::Value* nextIdx =
        builder.CreateAdd(builder.CreateLoad(i64Type, indexVar, ""),
                          llvm::ConstantInt::get(i64Type, 1), "nextidx");
    builder.CreateStore(nextIdx, indexVar);
    builder.CreateBr(condBB);

    // End
    endBB->insertInto(function);
    builder.SetInsertPoint(endBB);

    loopBreakBlocks.pop_back();
    loopContinueBlocks.pop_back();

    // Restore
    if(oldVal)
        namedValues[node->varName] = oldVal;
    else
        namedValues.erase(node->varName);

    if(hadOldType)
        variableTypes[node->varName] = oldType;
    else
        variableTypes.erase(node->varName);

    // Clean up tuple element types if we added them
    if(mapIter->kind == MapIteratorNode::ITER_ENTRIES)
    {
        tupleElementTypes.erase(node->varName);
    }
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

void CodeGenerator::generateBreakStatement(BreakNode* node)
{
    if(loopBreakBlocks.empty())
    {
        reportError(node->line, "'break' statement not within a loop");
        return;
    }
    builder.CreateBr(loopBreakBlocks.back());
}

void CodeGenerator::generateContinueStatement(ContinueNode* node)
{
    if(loopContinueBlocks.empty())
    {
        reportError(node->line, "'continue' statement not within a loop");
        return;
    }
    builder.CreateBr(loopContinueBlocks.back());
}

llvm::Value* CodeGenerator::generateIntLiteral(IntLiteralNode* node)
{
    // Use 64-bit for literals to support large values; truncation happens at
    // assignment
    return llvm::ConstantInt::get(context, llvm::APInt(64, node->value, true));
}

llvm::Value* CodeGenerator::generateBoolLiteral(BoolLiteralNode* node)
{
    return llvm::ConstantInt::get(context, llvm::APInt(1, node->value ? 1 : 0));
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
    std::vector<std::pair<std::string, TypeNode*>> members;

    // If this struct has a base, include base struct's fields first
    if(!node->baseName.empty())
    {
        auto baseMemIt = structMembers.find(node->baseName);
        if(baseMemIt != structMembers.end())
        {
            for(const auto& baseMember : baseMemIt->second)
            {
                memberTypes.push_back(getLLVMTypeFromNode(baseMember.second));
                members.push_back(baseMember);
            }
        }
        else
        {
            reportError(node->line,
                        "base struct '" + node->baseName + "' not found");
        }
    }

    // Add this struct's own members
    for(auto member : node->members->members)
    {
        memberTypes.push_back(getLLVMTypeFromNode(member->type));
        members.push_back({member->name, member->type});
    }

    llvm::StructType* structType =
        llvm::StructType::create(context, memberTypes, node->name);
    structTypes[node->name] = structType;
    structMembers[node->name] = members;

    // Track base name for inheritance lookups
    if(!node->baseName.empty())
    {
        structBases[node->name] = node->baseName;
    }
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

    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(node->type))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        llvm::Type* structType = getStructType(mangledName);
        if(!structType)
        {
            reportError(node->line, "failed to monomorphize struct: " +
                                        genStructRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = mangledName;
        constantVariables.insert(node->name);
        return;
    }

    // Handle generic list type
    if(auto* genListType = dynamic_cast<GenericListTypeNode*>(node->type))
    {
        // Store element type for iteration
        listElementTypes[node->name] = genListType->elementType;

        // List struct type: { i64, ptr }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMType(genListType->elementType->kind), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(listStructType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_LIST;
        constantVariables.insert(node->name);
        return;
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(node->type))
    {
        // Store key/value types
        mapKeyValueTypes[node->name] =
            std::make_pair(mapType->keyType, mapType->valueType);

        // Map struct type: { i64, ptr, ptr }
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(mapStructType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_MAP;
        constantVariables.insert(node->name);
        return;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(node->type))
    {
        // Store element types for tuple access
        std::vector<TypeNode*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            elemTypes.push_back(t);
        }
        tupleElementTypes[node->name] = elemTypes;

        // Create LLVM struct type for tuple based on declared types
        std::vector<llvm::Type*> tupleTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
            {
                reportError(node->line, "invalid type in tuple");
                return;
            }
            tupleTypes.push_back(elemType);
        }
        llvm::StructType* tupleStructType =
            llvm::StructType::get(context, tupleTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(tupleStructType, nullptr, node->name);

        // Convert tuple literal elements to match declared types
        if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->expression))
        {
            // Build tuple with proper type conversions
            llvm::Value* tupleVal = llvm::UndefValue::get(tupleStructType);

            for(size_t i = 0; i < tupleLit->elements->elements.size() &&
                              i < tupleType->elementTypes->types.size();
                ++i)
            {
                llvm::Value* elemVal =
                    generateExpression(tupleLit->elements->elements[i]);
                if(!elemVal)
                    return;

                llvm::Type* targetElemType = tupleTypes[i];
                llvm::Type* sourceElemType = elemVal->getType();

                // Convert if needed (only for primitive types)
                if(sourceElemType != targetElemType)
                {
                    if(sourceElemType->isIntegerTy() &&
                       targetElemType->isIntegerTy())
                    {
                        unsigned srcBits = sourceElemType->getIntegerBitWidth();
                        unsigned dstBits = targetElemType->getIntegerBitWidth();
                        if(srcBits > dstBits)
                        {
                            elemVal = builder.CreateTrunc(
                                elemVal, targetElemType, "trunc");
                        }
                        else if(srcBits < dstBits)
                        {
                            elemVal = builder.CreateSExt(
                                elemVal, targetElemType, "sext");
                        }
                    }
                    else if(sourceElemType->isIntegerTy() &&
                            targetElemType->isFloatingPointTy())
                    {
                        elemVal = builder.CreateSIToFP(elemVal, targetElemType,
                                                       "sitofp");
                    }
                    else if(sourceElemType->isFloatingPointTy() &&
                            targetElemType->isIntegerTy())
                    {
                        elemVal = builder.CreateFPToSI(elemVal, targetElemType,
                                                       "fptosi");
                    }
                    else if(sourceElemType->isFloatingPointTy() &&
                            targetElemType->isFloatingPointTy())
                    {
                        elemVal = builder.CreateFPCast(elemVal, targetElemType,
                                                       "fpcast");
                    }
                    // For struct types and other complex types, no conversion
                    // needed if types match
                }

                tupleVal = builder.CreateInsertValue(
                    tupleVal, elemVal, static_cast<unsigned>(i), "tuple.elem");
            }

            builder.CreateStore(tupleVal, alloca);
        }
        else
        {
            // Not a literal, just store (may fail if types mismatch)
            builder.CreateStore(initValue, alloca);
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_TUPLE;
        constantVariables.insert(node->name);
        return;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(node->type))
    {
        llvm::Type* structType = getStructType(structRef->structName);
        if(!structType)
        {
            reportError(node->line,
                        "unknown struct type: " + structRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);
        builder.CreateStore(initValue, alloca);
        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = structRef->structName;
        constantVariables.insert(node->name);
        return;
    }

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
    // Handle generic struct type reference (e.g., Pair<i32, i64>)
    if(auto* genStructRef = dynamic_cast<GenericStructTypeRefNode*>(node->type))
    {
        // Get or create the monomorphized struct type
        std::string mangledName = getOrCreateMonomorphizedStruct(
            genStructRef->structName, genStructRef->typeArgs);

        llvm::Type* structType = getStructType(mangledName);
        if(!structType)
        {
            reportError(node->line, "failed to monomorphize struct: " +
                                        genStructRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = mangledName;
        return;
    }

    // Handle generic list type
    if(auto* genListType = dynamic_cast<GenericListTypeNode*>(node->type))
    {
        listElementTypes[node->name] = genListType->elementType;

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMType(genListType->elementType->kind), 0);
#endif
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(listStructType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_LIST;
        return;
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(node->type))
    {
        mapKeyValueTypes[node->name] =
            std::make_pair(mapType->keyType, mapType->valueType);

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(mapStructType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_MAP;
        return;
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(node->type))
    {
        // Store element types for tuple access
        std::vector<TypeNode*> elemTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            elemTypes.push_back(t);
        }
        tupleElementTypes[node->name] = elemTypes;

        // Create LLVM struct type for tuple based on declared types
        std::vector<llvm::Type*> tupleTypes;
        for(auto* t : tupleType->elementTypes->types)
        {
            llvm::Type* elemType = getLLVMTypeFromNode(t);
            if(!elemType)
            {
                reportError(node->line, "invalid type in tuple");
                return;
            }
            tupleTypes.push_back(elemType);
        }
        llvm::StructType* tupleStructType =
            llvm::StructType::get(context, tupleTypes);

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(tupleStructType, nullptr, node->name);

        if(node->initExpr)
        {
            // Convert tuple literal elements to match declared types
            if(auto* tupleLit = dynamic_cast<TupleLiteralNode*>(node->initExpr))
            {
                // Build tuple with proper type conversions
                llvm::Value* tupleVal = llvm::UndefValue::get(tupleStructType);

                for(size_t i = 0; i < tupleLit->elements->elements.size() &&
                                  i < tupleType->elementTypes->types.size();
                    ++i)
                {
                    llvm::Value* elemVal =
                        generateExpression(tupleLit->elements->elements[i]);
                    if(!elemVal)
                        return;

                    llvm::Type* targetElemType = tupleTypes[i];
                    llvm::Type* sourceElemType = elemVal->getType();

                    // Convert if needed (only for primitive types)
                    if(sourceElemType != targetElemType)
                    {
                        if(sourceElemType->isIntegerTy() &&
                           targetElemType->isIntegerTy())
                        {
                            unsigned srcBits =
                                sourceElemType->getIntegerBitWidth();
                            unsigned dstBits =
                                targetElemType->getIntegerBitWidth();
                            if(srcBits > dstBits)
                            {
                                elemVal = builder.CreateTrunc(
                                    elemVal, targetElemType, "trunc");
                            }
                            else if(srcBits < dstBits)
                            {
                                elemVal = builder.CreateSExt(
                                    elemVal, targetElemType, "sext");
                            }
                        }
                        else if(sourceElemType->isIntegerTy() &&
                                targetElemType->isFloatingPointTy())
                        {
                            elemVal = builder.CreateSIToFP(
                                elemVal, targetElemType, "sitofp");
                        }
                        else if(sourceElemType->isFloatingPointTy() &&
                                targetElemType->isIntegerTy())
                        {
                            elemVal = builder.CreateFPToSI(
                                elemVal, targetElemType, "fptosi");
                        }
                        else if(sourceElemType->isFloatingPointTy() &&
                                targetElemType->isFloatingPointTy())
                        {
                            elemVal = builder.CreateFPCast(
                                elemVal, targetElemType, "fpcast");
                        }
                        // For struct types and other complex types, no
                        // conversion needed
                    }

                    tupleVal = builder.CreateInsertValue(
                        tupleVal, elemVal, static_cast<unsigned>(i),
                        "tuple.elem");
                }

                builder.CreateStore(tupleVal, alloca);
            }
            else
            {
                // Not a literal, just store
                llvm::Value* initValue = generateExpression(node->initExpr);
                if(initValue)
                {
                    builder.CreateStore(initValue, alloca);
                }
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_TUPLE;
        return;
    }

    // Handle struct type reference
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(node->type))
    {
        llvm::Type* structType = getStructType(structRef->structName);
        if(!structType)
        {
            reportError(node->line,
                        "unknown struct type: " + structRef->structName);
            return;
        }

        llvm::AllocaInst* alloca =
            builder.CreateAlloca(structType, nullptr, node->name);

        if(node->initExpr)
        {
            llvm::Value* initValue = generateExpression(node->initExpr);
            if(initValue)
            {
                builder.CreateStore(initValue, alloca);
            }
        }

        namedValues[node->name] = alloca;
        variableTypes[node->name] = TypeNode::TYPE_STRUCT;
        structVariableTypes[node->name] = structRef->structName;
        return;
    }

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
        llvm::Type* targetType = alloca->getAllocatedType();
        llvm::Type* valueType = value->getType();

        // Convert value to target type if necessary
        if(valueType != targetType)
        {
            if(valueType->isIntegerTy() && targetType->isIntegerTy())
            {
                unsigned valueBits = valueType->getIntegerBitWidth();
                unsigned targetBits = targetType->getIntegerBitWidth();
                if(valueBits > targetBits)
                {
                    value = builder.CreateTrunc(value, targetType, "trunc");
                }
                else if(valueBits < targetBits)
                {
                    value = builder.CreateSExt(value, targetType, "sext");
                }
            }
            else if(valueType->isIntegerTy() && targetType->isFloatingPointTy())
            {
                value = builder.CreateSIToFP(value, targetType, "sitofp");
            }
            else if(valueType->isFloatingPointTy() && targetType->isIntegerTy())
            {
                value = builder.CreateFPToSI(value, targetType, "fptosi");
            }
            else if(valueType->isFloatingPointTy() &&
                    targetType->isFloatingPointTy())
            {
                value = builder.CreateFPCast(value, targetType, "fpcast");
            }
        }

        builder.CreateStore(value, alloca);
    }
}

void CodeGenerator::generateFieldAssignment(FieldAssignmentNode* node)
{
    llvm::Value* structPtr;
    std::string structTypeName;
    std::string fieldName;

    // Handle chained assignment (a.b.c = x) vs simple assignment (a.b = x)
    if(node->target)
    {
        // Chained assignment: target is a FieldAccessNode representing the full
        // path
        auto* fieldAccess = dynamic_cast<FieldAccessNode*>(node->target);
        if(!fieldAccess)
        {
            reportError(node->line, "invalid assignment target");
            return;
        }

        fieldName = fieldAccess->fieldName;

        // Get the struct pointer for the object part (everything except the
        // last field)
        if(fieldAccess->object)
        {
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, node->line);
            if(!ptr)
                return;
            structPtr = ptr;
            structTypeName = typeName;
        }
        else
        {
            // Simple case within chained: the target is like "a.b", so get "a"
            structPtr = namedValues[fieldAccess->structName];
            if(!structPtr)
            {
                reportError(node->line,
                            "unknown variable: " + fieldAccess->structName);
                return;
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(node->line, "variable '" + fieldAccess->structName +
                                            "' is not a struct");
                return;
            }
            structTypeName = typeIt->second;

            // Handle self pointer
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
            {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if(allocaType->isPointerTy())
                {
                    structPtr = builder.CreateLoad(
                        allocaType, alloca, fieldAccess->structName + ".ptr");
                }
            }
        }
    }
    else
    {
        // Simple assignment: a.b = x
        fieldName = node->fieldName;

        structPtr = namedValues[node->structName];
        if(!structPtr)
        {
            reportError(node->line, "unknown variable: " + node->structName);
            return;
        }

        auto typeIt = structVariableTypes.find(node->structName);
        if(typeIt == structVariableTypes.end())
        {
            reportError(node->line,
                        "variable '" + node->structName + "' is not a struct");
            return;
        }
        structTypeName = typeIt->second;

        // Handle self pointer
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                structPtr = builder.CreateLoad(allocaType, alloca,
                                               node->structName + ".ptr");
            }
        }
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return;
    }

    // Find field index
    int fieldIndex = -1;
    TypeNode* fieldType = nullptr;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        if(members[i].first == fieldName)
        {
            fieldIndex = static_cast<int>(i);
            fieldType = members[i].second;
            break;
        }
    }

    if(fieldIndex < 0)
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no field named '" + fieldName + "'");
        return;
    }

    // Generate the value to assign
    llvm::Value* value = generateExpression(node->expression);
    if(!value)
        return;

    // Get struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
        return;

    // Convert value if needed
    llvm::Type* targetType = getLLVMTypeFromNode(fieldType);
    if(value->getType() != targetType)
    {
        if(value->getType()->isIntegerTy() && targetType->isIntegerTy())
        {
            unsigned srcBits = value->getType()->getIntegerBitWidth();
            unsigned dstBits = targetType->getIntegerBitWidth();
            if(srcBits > dstBits)
            {
                value = builder.CreateTrunc(value, targetType, "trunc");
            }
            else if(srcBits < dstBits)
            {
                value = builder.CreateSExt(value, targetType, "sext");
            }
        }
    }

    // Create GEP to field and store
    llvm::Value* fieldPtr = builder.CreateStructGEP(
        structType, structPtr, static_cast<unsigned>(fieldIndex),
        fieldName + "_ptr");
    builder.CreateStore(value, fieldPtr);
}

// Helper to get struct pointer and type name from an expression
// Returns {pointer, typeName} or {nullptr, ""} on error
std::pair<llvm::Value*, std::string>
CodeGenerator::getStructPtrAndType(ExpressionNode* expr, int line)
{
    // Case 1: Simple identifier (e.g., "myStruct")
    if(auto* id = dynamic_cast<IdentifierNode*>(expr))
    {
        std::cerr << "DEBUG getStructPtrAndType: identifier '" << id->name
                  << "'" << std::endl;
        llvm::Value* ptr = namedValues[id->name];
        if(!ptr)
        {
            reportError(line, "unknown variable: " + id->name);
            return {nullptr, ""};
        }

        auto typeIt = structVariableTypes.find(id->name);
        if(typeIt == structVariableTypes.end())
        {
            reportError(line, "variable '" + id->name + "' is not a struct");
            return {nullptr, ""};
        }

        // Handle self pointer (alloca containing pointer)
        llvm::Value* actualPtr = ptr;
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                actualPtr =
                    builder.CreateLoad(allocaType, alloca, id->name + ".ptr");
            }
        }

        return {actualPtr, typeIt->second};
    }

    // Case 2: Field access (e.g., "a.b" in "a.b.c")
    if(auto* fieldAccess = dynamic_cast<FieldAccessNode*>(expr))
    {
        std::cerr << "DEBUG getStructPtrAndType: field access structName='"
                  << fieldAccess->structName << "' fieldName='"
                  << fieldAccess->fieldName
                  << "' object=" << (fieldAccess->object ? "set" : "null")
                  << std::endl;

        // Recursively get the struct pointer for the object
        llvm::Value* objPtr;
        std::string objTypeName;

        if(fieldAccess->object)
        {
            // Chained: get pointer from the object expression
            auto [ptr, typeName] =
                getStructPtrAndType(fieldAccess->object, line);
            if(!ptr)
                return {nullptr, ""};
            objPtr = ptr;
            objTypeName = typeName;
        }
        else
        {
            // Simple: get pointer from structName
            objPtr = namedValues[fieldAccess->structName];
            if(!objPtr)
            {
                reportError(line,
                            "unknown variable: " + fieldAccess->structName);
                return {nullptr, ""};
            }

            auto typeIt = structVariableTypes.find(fieldAccess->structName);
            if(typeIt == structVariableTypes.end())
            {
                reportError(line, "variable '" + fieldAccess->structName +
                                      "' is not a struct");
                return {nullptr, ""};
            }
            objTypeName = typeIt->second;

            // Handle self pointer
            if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(objPtr))
            {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if(allocaType->isPointerTy())
                {
                    objPtr = builder.CreateLoad(
                        allocaType, alloca, fieldAccess->structName + ".ptr");
                }
            }
        }

        // Now access the field
        auto memberIt = structMembers.find(objTypeName);
        if(memberIt == structMembers.end())
        {
            reportError(line, "unknown struct type: " + objTypeName);
            return {nullptr, ""};
        }

        // Find field index and type
        int fieldIndex = -1;
        TypeNode* fieldType = nullptr;
        const auto& members = memberIt->second;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldAccess->fieldName)
            {
                fieldIndex = static_cast<int>(i);
                fieldType = members[i].second;
                break;
            }
        }

        if(fieldIndex < 0)
        {
            reportError(line, "struct '" + objTypeName +
                                  "' has no field named '" +
                                  fieldAccess->fieldName + "'");
            return {nullptr, ""};
        }

        // Check if the field is a struct type
        if(fieldType->kind == TypeNode::TYPE_STRUCT)
        {
            auto* structTypeRef = dynamic_cast<StructTypeRefNode*>(fieldType);
            if(!structTypeRef)
            {
                reportError(line, "internal error: expected StructTypeRefNode");
                return {nullptr, ""};
            }

            llvm::StructType* structType = getStructType(objTypeName);
            if(!structType)
                return {nullptr, ""};

            // Get pointer to the nested struct field
            llvm::Value* fieldPtr = builder.CreateStructGEP(
                structType, objPtr, static_cast<unsigned>(fieldIndex),
                fieldAccess->fieldName + "_ptr");

            return {fieldPtr, structTypeRef->structName};
        }
        else
        {
            reportError(line, "field '" + fieldAccess->fieldName +
                                  "' is not a struct type");
            return {nullptr, ""};
        }
    }

    reportError(line, "invalid expression for field access");
    return {nullptr, ""};
}

llvm::Value* CodeGenerator::generateFieldAccess(FieldAccessNode* node)
{
    llvm::Value* structPtr;
    std::string structTypeName;

    // Handle chained access (a.b.c) vs simple access (a.b)
    if(node->object)
    {
        // Chained access: evaluate the object expression first
        auto [ptr, typeName] = getStructPtrAndType(node->object, node->line);
        if(!ptr)
            return nullptr;
        structPtr = ptr;
        structTypeName = typeName;
    }
    else
    {
        // Simple access: get from structName
        structPtr = namedValues[node->structName];
        if(!structPtr)
        {
            reportError(node->line, "unknown variable: " + node->structName);
            return nullptr;
        }

        auto typeIt = structVariableTypes.find(node->structName);
        if(typeIt == structVariableTypes.end())
        {
            reportError(node->line,
                        "variable '" + node->structName + "' is not a struct");
            return nullptr;
        }
        structTypeName = typeIt->second;

        // Handle self pointer (alloca containing pointer)
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(structPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                structPtr = builder.CreateLoad(allocaType, alloca,
                                               node->structName + ".ptr");
            }
        }
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return nullptr;
    }

    // Find field index
    int fieldIndex = -1;
    TypeNode* fieldType = nullptr;
    const auto& members = memberIt->second;
    for(size_t i = 0; i < members.size(); ++i)
    {
        if(members[i].first == node->fieldName)
        {
            fieldIndex = static_cast<int>(i);
            fieldType = members[i].second;
            break;
        }
    }

    if(fieldIndex < 0)
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no field named '" + node->fieldName +
                                    "'");
        return nullptr;
    }

    // Get struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
        return nullptr;

    // Get field type
    llvm::Type* llvmFieldType = getLLVMTypeFromNode(fieldType);

    // Create GEP to field and load
    llvm::Value* fieldPtr = builder.CreateStructGEP(
        structType, structPtr, static_cast<unsigned>(fieldIndex),
        node->fieldName + "_ptr");
    return builder.CreateLoad(llvmFieldType, fieldPtr, node->fieldName);
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
    // Check visibility
    auto visIt = functionVisibility.find(node->name);
    if(visIt != functionVisibility.end())
    {
        bool isPublic = visIt->second.first;
        const std::string& sourceModule = visIt->second.second;

        // If the function is from a different module and is private, error
        if(!sourceModule.empty() && sourceModule != currentModule && !isPublic)
        {
            reportError(node->line, "function '" + node->name +
                                        "' is private in module '" +
                                        sourceModule + "'");
            return nullptr;
        }
    }

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

void CodeGenerator::generateStructMethods(StructDefNode* node)
{
    if(!node->members)
        return;

    // First, inherit methods from base struct if any
    if(!node->baseName.empty())
    {
        auto baseMethodsIt = structMethods.find(node->baseName);
        if(baseMethodsIt != structMethods.end())
        {
            // Copy all base methods to this struct
            for(const auto& methodPair : baseMethodsIt->second)
            {
                // Only inherit if not overridden by this struct
                bool overridden = false;
                for(auto method : node->members->methods)
                {
                    if(method->name == methodPair.first)
                    {
                        overridden = true;
                        break;
                    }
                }
                if(!overridden)
                {
                    structMethods[node->name][methodPair.first] =
                        methodPair.second;
                }
            }
        }
    }

    // Register all methods for this struct
    for(auto method : node->members->methods)
    {
        structMethods[node->name][method->name] =
            std::make_pair(method->isPublic, method);

        // Generate forward declaration
        generateMethodDeclaration(node->name, method);
    }
}

llvm::Function*
CodeGenerator::generateMethodDeclaration(const std::string& structName,
                                         StructMethodNode* method)
{
    // Method name is mangled: StructName_methodName
    std::string mangledName = structName + "_" + method->name;

    // Check if already declared
    if(module->getFunction(mangledName))
    {
        return module->getFunction(mangledName);
    }

    std::vector<llvm::Type*> paramTypes;

    // First parameter is pointer to struct (self)
    if(!method->isStatic)
    {
        llvm::Type* structType = getStructType(structName);
        if(structType)
        {
#if LLVM_VERSION_MAJOR >= 15
            paramTypes.push_back(llvm::PointerType::get(context, 0));
#else
            paramTypes.push_back(llvm::PointerType::get(structType, 0));
#endif
        }
    }

    // Add other parameters (skip 'self' if it's explicitly declared)
    for(auto param : method->parameters->parameters)
    {
        // Skip 'self' parameter - it's handled separately above
        if(param->name == "self")
            continue;

        llvm::Type* paramType = getLLVMTypeFromNode(param->type);
        if(!paramType)
        {
            reportError(param->line,
                        "unknown parameter type for '" + param->name + "'");
            paramType = llvm::Type::getInt32Ty(context);
        }
        paramTypes.push_back(paramType);
    }

    llvm::Type* returnType = getLLVMTypeFromNode(method->returnType);
    if(!returnType)
    {
        reportError(method->line,
                    "unknown return type for method '" + method->name + "'");
        returnType = llvm::Type::getVoidTy(context);
    }

    llvm::FunctionType* funcType =
        llvm::FunctionType::get(returnType, paramTypes, false);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, mangledName, module.get());

    // Set parameter names
    unsigned idx = 0;
    unsigned paramIdx = 0;
    for(auto& arg : function->args())
    {
        if(idx == 0 && !method->isStatic)
        {
            arg.setName("self");
        }
        else
        {
            // Find the next non-self parameter
            while(paramIdx < method->parameters->parameters.size() &&
                  method->parameters->parameters[paramIdx]->name == "self")
            {
                paramIdx++;
            }
            if(paramIdx < method->parameters->parameters.size())
            {
                arg.setName(method->parameters->parameters[paramIdx]->name);
                paramIdx++;
            }
        }
        idx++;
    }

    return function;
}

llvm::Function*
CodeGenerator::generateMethodDefinition(const std::string& structName,
                                        StructMethodNode* method)
{
    std::string mangledName = structName + "_" + method->name;

    llvm::Function* function = module->getFunction(mangledName);
    if(!function)
    {
        function = generateMethodDeclaration(structName, method);
    }

    // Check if already has a body
    if(!function->empty())
    {
        return function;
    }

    if(!method->body)
    {
        reportError(method->line, "method '" + method->name + "' has no body");
        return function;
    }

    // Create entry block
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear scope
    namedValues.clear();
    constantVariables.clear();
    variableTypes.clear();

    // Set up self parameter and other parameters
    unsigned argIdx = 0;
    unsigned methodParamIdx = 0;
    for(auto& arg : function->args())
    {
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        builder.CreateStore(&arg, alloca);
        namedValues[std::string(arg.getName())] = alloca;

        if(argIdx == 0 && !method->isStatic)
        {
            // 'self' is a pointer to the struct
            structVariableTypes["self"] = structName;
            variableTypes["self"] = TypeNode::TYPE_STRUCT;
        }
        else
        {
            // Find next non-self parameter
            while(methodParamIdx < method->parameters->parameters.size() &&
                  method->parameters->parameters[methodParamIdx]->name ==
                      "self")
            {
                methodParamIdx++;
            }
            if(methodParamIdx < method->parameters->parameters.size())
            {
                variableTypes[std::string(arg.getName())] =
                    method->parameters->parameters[methodParamIdx]->type->kind;
                methodParamIdx++;
            }
        }
        argIdx++;
    }

    // Generate body
    for(auto stmt : method->body->statements)
    {
        generateStatement(stmt);
    }

    // Add terminator if needed
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!currentBlock->getTerminator())
    {
        if(returnType->isVoidTy())
        {
            builder.CreateRetVoid();
        }
        else
        {
            builder.CreateUnreachable();
        }
    }

    llvm::verifyFunction(*function);
    return function;
}

llvm::Value* CodeGenerator::generateMethodCall(MethodCallNode* node)
{
    // Get the object struct pointer and type
    // This supports both simple (p.method()) and chained (a.b.method()) access
    llvm::Value* objPtr;
    std::string structTypeName;

    auto* objId = dynamic_cast<IdentifierNode*>(node->object);
    if(objId)
    {
        // Simple case: identifier.method()
        auto typeIt = structVariableTypes.find(objId->name);
        if(typeIt == structVariableTypes.end())
        {
            reportError(node->line,
                        "'" + objId->name + "' is not a struct variable");
            return nullptr;
        }
        structTypeName = typeIt->second;

        objPtr = namedValues[objId->name];
        if(!objPtr)
        {
            reportError(node->line, "unknown variable: " + objId->name);
            return nullptr;
        }

        // Handle self pointer (alloca containing pointer)
        if(auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(objPtr))
        {
            llvm::Type* allocaType = alloca->getAllocatedType();
            if(allocaType->isPointerTy())
            {
                objPtr = builder.CreateLoad(allocaType, alloca,
                                            objId->name + ".ptr");
            }
        }
    }
    else
    {
        // Chained case: a.b.method() - use helper to get struct pointer
        auto [ptr, typeName] = getStructPtrAndType(node->object, node->line);
        if(!ptr)
        {
            return nullptr;
        }
        objPtr = ptr;
        structTypeName = typeName;
    }

    // Check if method exists on this struct (including inherited methods)
    auto structIt = structMethods.find(structTypeName);
    if(structIt == structMethods.end())
    {
        reportError(node->line,
                    "struct '" + structTypeName + "' has no methods");
        return nullptr;
    }

    auto methodIt = structIt->second.find(node->methodName);
    if(methodIt == structIt->second.end())
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' has no method named '" +
                                    node->methodName + "'");
        return nullptr;
    }

    bool isPublic = methodIt->second.first;
    StructMethodNode* methodNode = methodIt->second.second;

    // Check visibility - if calling from outside the struct's module, must be
    // public For now, we allow all calls within the same compilation unit
    // TODO: Add proper cross-module visibility checking for methods

    // Find the actual struct that defines this method (may be a base struct)
    std::string definingStruct = structTypeName;
    std::string searchStruct = structTypeName;
    while(!searchStruct.empty())
    {
        // Check if this struct directly defines the method (has it in its
        // members)
        std::string mangledName = searchStruct + "_" + node->methodName;
        if(module->getFunction(mangledName))
        {
            definingStruct = searchStruct;
            break;
        }
        // Move to base struct
        auto baseIt = structBases.find(searchStruct);
        if(baseIt != structBases.end())
        {
            searchStruct = baseIt->second;
        }
        else
        {
            break;
        }
    }

    std::string mangledName = definingStruct + "_" + node->methodName;
    llvm::Function* callee = module->getFunction(mangledName);
    if(!callee)
    {
        reportError(node->line, "unknown method: " + node->methodName);
        return nullptr;
    }

    // Check if this is a monomorphized struct method that needs body generation
    // If the function is declared but has no body (empty), generate it now
    if(callee->empty() && monomorphizedTypes.count(definingStruct))
    {
        // Find the method node
        auto structIt = structMethods.find(definingStruct);
        if(structIt != structMethods.end())
        {
            auto methodIt = structIt->second.find(node->methodName);
            if(methodIt != structIt->second.end())
            {
                StructMethodNode* methodDef = methodIt->second.second;
                if(methodDef && methodDef->body)
                {
                    // Save current state - generateMethodDefinition will clear
                    // these
                    llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
                    auto savedNamedValues = namedValues;
                    auto savedConstantVariables = constantVariables;
                    auto savedVariableTypes = variableTypes;
                    auto savedStructVariableTypes = structVariableTypes;
                    auto savedListElementTypes = listElementTypes;
                    auto savedMapKeyValueTypes = mapKeyValueTypes;
                    auto savedTupleElementTypes = tupleElementTypes;

                    // Generate the method body
                    generateMethodDefinition(definingStruct, methodDef);

                    // Restore all state
                    namedValues = savedNamedValues;
                    constantVariables = savedConstantVariables;
                    variableTypes = savedVariableTypes;
                    structVariableTypes = savedStructVariableTypes;
                    listElementTypes = savedListElementTypes;
                    mapKeyValueTypes = savedMapKeyValueTypes;
                    tupleElementTypes = savedTupleElementTypes;

                    if(savedBlock)
                    {
                        builder.SetInsertPoint(savedBlock);
                    }
                }
            }
        }
    }

    // Build arguments - first is pointer to struct
    std::vector<llvm::Value*> args;
    args.push_back(objPtr);

    // Add other arguments
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
    return builder.CreateCall(callee, args, "methodcall");
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

llvm::Value* CodeGenerator::generateListLiteral(ListLiteralNode* node)
{
    // List structure: { i64 size, ptr data }
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    if(!node->elements || node->elements->elements.empty())
    {
        // Empty list: return {0, null}
        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
        listStruct = builder.CreateInsertValue(
            listStruct, llvm::ConstantInt::get(i64Type, 0), 0);
        listStruct = builder.CreateInsertValue(
            listStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            1);
        return listStruct;
    }

    // Generate all elements
    std::vector<llvm::Value*> elementValues;
    llvm::Type* elementType = nullptr;

    for(auto* elem : node->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
            return nullptr;
        if(!elementType)
        {
            elementType = val->getType();
        }
        elementValues.push_back(val);
    }

    int64_t listSize = static_cast<int64_t>(elementValues.size());

    // Allocate array for elements
    llvm::Value* arraySizeVal = llvm::ConstantInt::get(i64Type, listSize);
    llvm::Value* dataAlloc =
        builder.CreateAlloca(elementType, arraySizeVal, "listdata");

    // Store each element
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        llvm::Value* idx = llvm::ConstantInt::get(i64Type, i);
        llvm::Value* elemPtr =
            builder.CreateGEP(elementType, dataAlloc, idx, "elemptr");
        builder.CreateStore(elementValues[i], elemPtr);
    }

    // Create list struct
    std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
    llvm::StructType* listStructType =
        llvm::StructType::get(context, listStructTypes);

    llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
    listStruct = builder.CreateInsertValue(
        listStruct, llvm::ConstantInt::get(i64Type, listSize), 0);
    listStruct = builder.CreateInsertValue(listStruct, dataAlloc, 1);

    return listStruct;
}

llvm::Value* CodeGenerator::generateMapLiteral(MapLiteralNode* node)
{
    // Map structure: { i64 size, ptr keys, ptr values }
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif

    std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
    llvm::StructType* mapStructType =
        llvm::StructType::get(context, mapStructTypes);

    if(!node->entries || node->entries->entries.empty())
    {
        // Empty map
        llvm::Value* mapStruct = llvm::UndefValue::get(mapStructType);
        mapStruct = builder.CreateInsertValue(
            mapStruct, llvm::ConstantInt::get(i64Type, 0), 0);
        mapStruct = builder.CreateInsertValue(
            mapStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            1);
        mapStruct = builder.CreateInsertValue(
            mapStruct,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptrType)),
            2);
        return mapStruct;
    }

    // Generate all key-value pairs
    std::vector<llvm::Value*> keyValues;
    std::vector<llvm::Value*> valueValues;
    llvm::Type* keyType = nullptr;
    llvm::Type* valueType = nullptr;

    for(auto* entry : node->entries->entries)
    {
        llvm::Value* keyVal = generateExpression(entry->key);
        llvm::Value* valVal = generateExpression(entry->value);
        if(!keyVal || !valVal)
            return nullptr;

        if(!keyType)
        {
            keyType = keyVal->getType();
            valueType = valVal->getType();
        }

        keyValues.push_back(keyVal);
        valueValues.push_back(valVal);
    }

    int64_t mapSize = static_cast<int64_t>(keyValues.size());

    // Allocate arrays for keys and values
    llvm::Value* sizeVal = llvm::ConstantInt::get(i64Type, mapSize);
    llvm::Value* keysAlloc = builder.CreateAlloca(keyType, sizeVal, "mapkeys");
    llvm::Value* valsAlloc =
        builder.CreateAlloca(valueType, sizeVal, "mapvals");

    // Store each key-value pair
    for(size_t i = 0; i < keyValues.size(); ++i)
    {
        llvm::Value* idx = llvm::ConstantInt::get(i64Type, i);
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysAlloc, idx, "keyptr");
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsAlloc, idx, "valptr");
        builder.CreateStore(keyValues[i], keyPtr);
        builder.CreateStore(valueValues[i], valPtr);
    }

    // Create map struct
    llvm::Value* mapStruct = llvm::UndefValue::get(mapStructType);
    mapStruct = builder.CreateInsertValue(
        mapStruct, llvm::ConstantInt::get(i64Type, mapSize), 0);
    mapStruct = builder.CreateInsertValue(mapStruct, keysAlloc, 1);
    mapStruct = builder.CreateInsertValue(mapStruct, valsAlloc, 2);

    return mapStruct;
}

llvm::Value* CodeGenerator::generateIndexExpression(IndexExpressionNode* node)
{
    // Get the base (list or map variable)
    auto* baseId = dynamic_cast<IdentifierNode*>(node->base);
    if(!baseId)
    {
        reportError(node->line, "index expression requires an identifier");
        return nullptr;
    }

    llvm::Value* basePtr = namedValues[baseId->name];
    if(!basePtr)
    {
        reportError(node->line, "unknown variable: " + baseId->name);
        return nullptr;
    }

    llvm::Value* indexVal = generateExpression(node->index);
    if(!indexVal)
        return nullptr;

    // Check if it's a list
    auto listIt = listElementTypes.find(baseId->name);
    if(listIt != listElementTypes.end())
    {
        // List indexing
        TypeNode* elemTypeNode = listIt->second;
        llvm::Type* elementType = getLLVMType(elemTypeNode->kind);

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(elementType, 0);
#endif

        std::vector<llvm::Type*> listStructTypes = {i64Type, ptrType};
        llvm::StructType* listStructType =
            llvm::StructType::get(context, listStructTypes);

        // Load list struct
        llvm::Value* listStruct =
            builder.CreateLoad(listStructType, basePtr, "list");
        llvm::Value* dataPtr =
            builder.CreateExtractValue(listStruct, 1, "data");

        // Ensure index is i64
        if(indexVal->getType() != i64Type)
        {
            indexVal = builder.CreateSExtOrTrunc(indexVal, i64Type, "idx64");
        }

        // Get element pointer and load
        llvm::Value* elemPtr =
            builder.CreateGEP(elementType, dataPtr, indexVal, "elemptr");
        return builder.CreateLoad(elementType, elemPtr, "elem");
    }

    // Check if it's a map
    auto mapIt = mapKeyValueTypes.find(baseId->name);
    if(mapIt != mapKeyValueTypes.end())
    {
        // Map lookup - linear search for key
        TypeNode* keyTypeNode = mapIt->second.first;
        TypeNode* valTypeNode = mapIt->second.second;
        llvm::Type* keyType = getLLVMType(keyTypeNode->kind);
        llvm::Type* valueType = getLLVMType(valTypeNode->kind);

        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType = llvm::PointerType::get(keyType, 0);
#endif

        std::vector<llvm::Type*> mapStructTypes = {i64Type, ptrType, ptrType};
        llvm::StructType* mapStructType =
            llvm::StructType::get(context, mapStructTypes);

        // Load map struct
        llvm::Value* mapStruct =
            builder.CreateLoad(mapStructType, basePtr, "map");
        llvm::Value* mapSize = builder.CreateExtractValue(mapStruct, 0, "size");
        llvm::Value* keysPtr = builder.CreateExtractValue(mapStruct, 1, "keys");
        llvm::Value* valsPtr = builder.CreateExtractValue(mapStruct, 2, "vals");

        // Linear search loop for key
        llvm::Function* function = builder.GetInsertBlock()->getParent();

        llvm::AllocaInst* idxVar =
            builder.CreateAlloca(i64Type, nullptr, "mapidx");
        builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), idxVar);

        llvm::AllocaInst* resultVar =
            builder.CreateAlloca(valueType, nullptr, "mapresult");
        // Initialize with default value
        builder.CreateStore(llvm::Constant::getNullValue(valueType), resultVar);

        llvm::BasicBlock* condBB =
            llvm::BasicBlock::Create(context, "map.cond", function);
        llvm::BasicBlock* bodyBB =
            llvm::BasicBlock::Create(context, "map.body");
        llvm::BasicBlock* incBB = llvm::BasicBlock::Create(context, "map.inc");
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "map.end");

        builder.CreateBr(condBB);

        builder.SetInsertPoint(condBB);
        llvm::Value* currentIdx = builder.CreateLoad(i64Type, idxVar, "idx");
        llvm::Value* cond =
            builder.CreateICmpSLT(currentIdx, mapSize, "mapcond");
        builder.CreateCondBr(cond, bodyBB, endBB);

        bodyBB->insertInto(function);
        builder.SetInsertPoint(bodyBB);

        // Compare keys
        llvm::Value* keyPtr =
            builder.CreateGEP(keyType, keysPtr, currentIdx, "keyptr");
        llvm::Value* currentKey = builder.CreateLoad(keyType, keyPtr, "curkey");

        llvm::Value* keyMatch;
        if(keyType->isIntegerTy())
        {
            keyMatch = builder.CreateICmpEQ(currentKey, indexVal, "keymatch");
        }
        else if(keyType->isFloatingPointTy())
        {
            keyMatch = builder.CreateFCmpOEQ(currentKey, indexVal, "keymatch");
        }
        else
        {
            // For strings/pointers, need strcmp or pointer comparison
            keyMatch = builder.CreateICmpEQ(currentKey, indexVal, "keymatch");
        }

        llvm::BasicBlock* foundBB =
            llvm::BasicBlock::Create(context, "map.found", function);
        builder.CreateCondBr(keyMatch, foundBB, incBB);

        builder.SetInsertPoint(foundBB);
        llvm::Value* valPtr =
            builder.CreateGEP(valueType, valsPtr, currentIdx, "valptr");
        llvm::Value* foundVal =
            builder.CreateLoad(valueType, valPtr, "foundval");
        builder.CreateStore(foundVal, resultVar);
        builder.CreateBr(endBB);

        incBB->insertInto(function);
        builder.SetInsertPoint(incBB);
        llvm::Value* nextIdx =
            builder.CreateAdd(builder.CreateLoad(i64Type, idxVar, ""),
                              llvm::ConstantInt::get(i64Type, 1), "nextidx");
        builder.CreateStore(nextIdx, idxVar);
        builder.CreateBr(condBB);

        endBB->insertInto(function);
        builder.SetInsertPoint(endBB);

        return builder.CreateLoad(valueType, resultVar, "mapval");
    }

    reportError(node->line,
                "cannot index non-list/non-map variable: " + baseId->name);
    return nullptr;
}

llvm::Value* CodeGenerator::generateTupleLiteral(TupleLiteralNode* node)
{
    if(!node->elements || node->elements->elements.empty())
    {
        reportError(node->line, "tuple must have at least one element");
        return nullptr;
    }

    // Generate all elements and determine their types
    std::vector<llvm::Value*> elementValues;
    std::vector<llvm::Type*> elementTypes;

    for(auto* elem : node->elements->elements)
    {
        llvm::Value* val = generateExpression(elem);
        if(!val)
            return nullptr;
        elementValues.push_back(val);
        elementTypes.push_back(val->getType());
    }

    // Create the tuple struct type
    llvm::StructType* tupleType = llvm::StructType::get(context, elementTypes);

    // Create the tuple value
    llvm::Value* tupleVal = llvm::UndefValue::get(tupleType);
    for(size_t i = 0; i < elementValues.size(); ++i)
    {
        tupleVal = builder.CreateInsertValue(
            tupleVal, elementValues[i], static_cast<unsigned>(i), "tuple.elem");
    }

    return tupleVal;
}

llvm::Value* CodeGenerator::generateTupleAccess(TupleAccessNode* node)
{
    // Get the tuple variable
    auto* baseId = dynamic_cast<IdentifierNode*>(node->tuple);
    if(!baseId)
    {
        reportError(node->line, "tuple access requires an identifier");
        return nullptr;
    }

    llvm::Value* tuplePtr = namedValues[baseId->name];
    if(!tuplePtr)
    {
        reportError(node->line, "unknown variable: " + baseId->name);
        return nullptr;
    }

    // Check if it's a tuple
    auto it = tupleElementTypes.find(baseId->name);
    if(it == tupleElementTypes.end())
    {
        reportError(node->line, "cannot access tuple element: '" +
                                    baseId->name + "' is not a tuple");
        return nullptr;
    }

    const std::vector<TypeNode*>& elemTypes = it->second;

    // Bounds check
    if(node->index < 0 || static_cast<size_t>(node->index) >= elemTypes.size())
    {
        reportError(node->line, "tuple index " + std::to_string(node->index) +
                                    " out of bounds (tuple has " +
                                    std::to_string(elemTypes.size()) +
                                    " elements)");
        return nullptr;
    }

    // Build the tuple struct type using getLLVMTypeFromNode for proper struct
    // support
    std::vector<llvm::Type*> tupleTypes;
    for(auto* t : elemTypes)
    {
        llvm::Type* elemType = getLLVMTypeFromNode(t);
        if(!elemType)
        {
            reportError(node->line, "invalid type in tuple");
            return nullptr;
        }
        tupleTypes.push_back(elemType);
    }
    llvm::StructType* tupleStructType =
        llvm::StructType::get(context, tupleTypes);

    // Load the tuple struct
    llvm::Value* tupleVal =
        builder.CreateLoad(tupleStructType, tuplePtr, "tuple");

    // Extract the element
    return builder.CreateExtractValue(
        tupleVal, static_cast<unsigned>(node->index), "tuple.elem");
}

llvm::Value* CodeGenerator::generateStructLiteral(StructLiteralNode* node)
{
    std::string structTypeName = node->structName;

    // Check if this is a generic struct instantiation (has type arguments)
    if(!node->typeArgs.empty())
        if(!node->typeArgs.empty())
        {
            // Convert typeArgs from strings to TypeNodes
            // The typeArgs in StructLiteralNode are stored as strings from the
            // parser We need to look them up and create proper TypeNode
            // references
            std::vector<TypeNode*> typeArgNodes;
            for(const auto& typeArgStr : node->typeArgs)
            {
                // Try to create a TypeNode from the type argument string
                TypeNode* typeArg = nullptr;

                // Check if it's a basic type
                if(typeArgStr == "i8")
                    typeArg = new TypeNode(TypeNode::TYPE_I8);
                else if(typeArgStr == "i16")
                    typeArg = new TypeNode(TypeNode::TYPE_I16);
                else if(typeArgStr == "i32")
                    typeArg = new TypeNode(TypeNode::TYPE_I32);
                else if(typeArgStr == "i64")
                    typeArg = new TypeNode(TypeNode::TYPE_I64);
                else if(typeArgStr == "u8")
                    typeArg = new TypeNode(TypeNode::TYPE_U8);
                else if(typeArgStr == "u16")
                    typeArg = new TypeNode(TypeNode::TYPE_U16);
                else if(typeArgStr == "u32")
                    typeArg = new TypeNode(TypeNode::TYPE_U32);
                else if(typeArgStr == "u64")
                    typeArg = new TypeNode(TypeNode::TYPE_U64);
                else if(typeArgStr == "int")
                    typeArg = new TypeNode(TypeNode::TYPE_INT);
                else if(typeArgStr == "float")
                    typeArg = new TypeNode(TypeNode::TYPE_FLOAT);
                else if(typeArgStr == "double")
                    typeArg = new TypeNode(TypeNode::TYPE_DOUBLE);
                else if(typeArgStr == "bool")
                    typeArg = new TypeNode(TypeNode::TYPE_BOOL);
                else if(typeArgStr == "string")
                    typeArg = new TypeNode(TypeNode::TYPE_STRING);
                else
                {
                    // Assume it's a struct type reference
                    typeArg = new StructTypeRefNode(typeArgStr);
                }

                typeArgNodes.push_back(typeArg);
            }

            // Get or create the monomorphized struct type
            structTypeName =
                getOrCreateMonomorphizedStruct(node->structName, typeArgNodes);
        }

    // Get the struct type
    llvm::StructType* structType = getStructType(structTypeName);
    if(!structType)
    {
        reportError(node->line, "unknown struct type: " + structTypeName);
        return nullptr;
    }

    // Get struct member info
    auto memberIt = structMembers.find(structTypeName);
    if(memberIt == structMembers.end())
    {
        reportError(node->line, "no member info for struct: " + structTypeName);
        return nullptr;
    }
    const auto& members = memberIt->second;

    // Build the struct value
    llvm::Value* structVal = llvm::UndefValue::get(structType);

    // Process each field initialization
    for(const auto& fieldInit : node->fields)
    {
        const std::string& fieldName = fieldInit.first;
        ExpressionNode* valueExpr = fieldInit.second;

        // Find the member index
        int memberIndex = -1;
        for(size_t i = 0; i < members.size(); ++i)
        {
            if(members[i].first == fieldName)
            {
                memberIndex = static_cast<int>(i);
                break;
            }
        }

        if(memberIndex < 0)
        {
            reportError(node->line, "unknown field '" + fieldName +
                                        "' in struct '" + structTypeName + "'");
            return nullptr;
        }

        // Generate the field value
        llvm::Value* fieldValue = generateExpression(valueExpr);
        if(!fieldValue)
        {
            reportError(node->line, "failed to generate value for field '" +
                                        fieldName + "'");
            return nullptr;
        }

        // Get the expected field type from the struct
        llvm::Type* expectedType = structType->getElementType(memberIndex);
        llvm::Type* actualType = fieldValue->getType();

        // Convert value to expected type if needed
        if(actualType != expectedType)
        {
            if(actualType->isIntegerTy() && expectedType->isIntegerTy())
            {
                unsigned actualBits = actualType->getIntegerBitWidth();
                unsigned expectedBits = expectedType->getIntegerBitWidth();
                if(actualBits > expectedBits)
                {
                    fieldValue =
                        builder.CreateTrunc(fieldValue, expectedType, "trunc");
                }
                else if(actualBits < expectedBits)
                {
                    fieldValue =
                        builder.CreateSExt(fieldValue, expectedType, "sext");
                }
            }
            else if(actualType->isFloatingPointTy() &&
                    expectedType->isFloatingPointTy())
            {
                fieldValue =
                    builder.CreateFPCast(fieldValue, expectedType, "fpcast");
            }
            else if(actualType->isIntegerTy() &&
                    expectedType->isFloatingPointTy())
            {
                fieldValue =
                    builder.CreateSIToFP(fieldValue, expectedType, "sitofp");
            }
            else if(actualType->isFloatingPointTy() &&
                    expectedType->isIntegerTy())
            {
                fieldValue =
                    builder.CreateFPToSI(fieldValue, expectedType, "fptosi");
            }
            else
            {
                // Types are incompatible - report error
                std::string actualTypeStr, expectedTypeStr;

                // Get actual type name
                if(actualType->isIntegerTy())
                    actualTypeStr =
                        "i" + std::to_string(actualType->getIntegerBitWidth());
                else if(actualType->isFloatTy())
                    actualTypeStr = "float";
                else if(actualType->isDoubleTy())
                    actualTypeStr = "double";
                else if(actualType->isPointerTy())
                    actualTypeStr = "pointer";
                else if(actualType->isStructTy())
                    actualTypeStr = actualType->getStructName().str().empty()
                                        ? "struct"
                                        : actualType->getStructName().str();
                else
                    actualTypeStr = "unknown";

                // Get expected type name
                if(expectedType->isIntegerTy())
                    expectedTypeStr =
                        "i" +
                        std::to_string(expectedType->getIntegerBitWidth());
                else if(expectedType->isFloatTy())
                    expectedTypeStr = "float";
                else if(expectedType->isDoubleTy())
                    expectedTypeStr = "double";
                else if(expectedType->isPointerTy())
                    expectedTypeStr = "pointer";
                else if(expectedType->isStructTy())
                    expectedTypeStr =
                        expectedType->getStructName().str().empty()
                            ? "struct"
                            : expectedType->getStructName().str();
                else
                    expectedTypeStr = "unknown";

                reportError(node->line, "type mismatch for field '" +
                                            fieldName + "' in struct '" +
                                            structTypeName + "': expected '" +
                                            expectedTypeStr + "', got '" +
                                            actualTypeStr + "'");
                return nullptr;
            }
        }

        // Insert the value into the struct
        structVal = builder.CreateInsertValue(
            structVal, fieldValue, static_cast<unsigned>(memberIndex),
            structTypeName + "." + fieldName);
    }

    return structVal;
}

// ============================================================================
// GENERICS MONOMORPHIZATION IMPLEMENTATION
// ============================================================================

// Substitute type parameters with concrete types
// e.g., if typeParams = ["T", "U"] and typeArgs = [i32, i64],
// then a StructTypeRefNode("T") becomes a TypeNode(TYPE_I32)
TypeNode*
CodeGenerator::substituteTypeParams(TypeNode* type,
                                    const std::vector<std::string>& typeParams,
                                    const std::vector<TypeNode*>& typeArgs)
{
    if(!type)
        return nullptr;

    // Check if this is a struct type reference that matches a type parameter
    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
    {
        // Look for matching type parameter
        for(size_t i = 0; i < typeParams.size() && i < typeArgs.size(); ++i)
        {
            if(structRef->structName == typeParams[i])
            {
                // Return a copy of the concrete type
                return typeArgs[i];
            }
        }
        // Not a type parameter - return as-is (it's a concrete struct type)
        return type;
    }

    // Check if this is a generic struct type reference
    if(auto* genRef = dynamic_cast<GenericStructTypeRefNode*>(type))
    {
        // Recursively substitute type arguments
        auto* newRef = new GenericStructTypeRefNode(genRef->structName);
        for(auto* arg : genRef->typeArgs)
        {
            newRef->typeArgs.push_back(
                substituteTypeParams(arg, typeParams, typeArgs));
        }
        return newRef;
    }

    // Handle generic list type
    if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
    {
        TypeNode* newElemType =
            substituteTypeParams(listType->elementType, typeParams, typeArgs);
        return new GenericListTypeNode(newElemType);
    }

    // Handle map type
    if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
    {
        TypeNode* newKeyType =
            substituteTypeParams(mapType->keyType, typeParams, typeArgs);
        TypeNode* newValType =
            substituteTypeParams(mapType->valueType, typeParams, typeArgs);
        return new MapTypeNode(newKeyType, newValType);
    }

    // Handle tuple type
    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
    {
        auto* newTypeList = new TypeListNode();
        for(auto* elemType : tupleType->elementTypes->types)
        {
            newTypeList->addType(
                substituteTypeParams(elemType, typeParams, typeArgs));
        }
        return new TupleTypeNode(newTypeList);
    }

    // Basic types don't need substitution
    return type;
}

// Generate a mangled name for a monomorphized struct
static std::string generateMangledName(const std::string& baseName,
                                       const std::vector<TypeNode*>& typeArgs)
{
    std::string mangled = baseName;
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
            default:
                mangled += "unknown";
                break;
            }
        }
    }
    return mangled;
}

// Monomorphize a generic struct with concrete type arguments
void CodeGenerator::monomorphizeStruct(const std::string& genericName,
                                       const std::vector<TypeNode*>& typeArgs,
                                       const std::string& mangledName)
{
    // Find the generic template
    auto templateIt = genericStructTemplates.find(genericName);
    if(templateIt == genericStructTemplates.end())
    {
        std::cerr << "Error: Generic struct template '" << genericName
                  << "' not found" << std::endl;
        hasError = true;
        return;
    }

    StructDefNode* templateStruct = templateIt->second;
    const std::vector<std::string>& typeParams = templateStruct->typeParams;

    if(typeParams.size() != typeArgs.size())
    {
        std::cerr << "Error: Type argument count mismatch for '" << genericName
                  << "': expected " << typeParams.size() << ", got "
                  << typeArgs.size() << std::endl;
        hasError = true;
        return;
    }

    // Generate the monomorphized struct type
    std::vector<llvm::Type*> memberTypes;
    std::vector<std::pair<std::string, TypeNode*>> members;

    // Process each member, substituting type parameters
    if(templateStruct->members)
    {
        for(auto* member : templateStruct->members->members)
        {
            TypeNode* substitutedType =
                substituteTypeParams(member->type, typeParams, typeArgs);

            llvm::Type* llvmType = getLLVMTypeFromNode(substitutedType);
            if(!llvmType)
            {
                std::cerr << "Error: Failed to get LLVM type for member '"
                          << member->name << "' in " << mangledName
                          << std::endl;
                hasError = true;
                return;
            }

            memberTypes.push_back(llvmType);
            members.push_back({member->name, substitutedType});
        }
    }

    // Create the LLVM struct type
    llvm::StructType* structType =
        llvm::StructType::create(context, memberTypes, mangledName);

    // Register the monomorphized type
    structTypes[mangledName] = structType;
    structMembers[mangledName] = members;
    monomorphizedTypes.insert(mangledName);
    mangledToGenericName[mangledName] = genericName;

    // Copy visibility from template
    structVisibility[mangledName] =
        std::make_pair(templateStruct->isPublic, templateStruct->sourceModule);

    // Store the type params and args for later method generation
    // We'll generate methods lazily when they're called

    // Process impl blocks - just register methods, don't generate bodies yet
    auto implIt = genericImplBlocks.find(genericName);
    if(implIt != genericImplBlocks.end())
    {
        for(auto* impl : implIt->second)
        {
            for(auto* method : impl->methods)
            {
                // Substitute types in return type
                TypeNode* newReturnType = substituteTypeParams(
                    method->returnType, typeParams, typeArgs);

                // Substitute types in parameters
                auto* newParams = new ParameterListNode();
                for(auto* param : method->parameters->parameters)
                {
                    TypeNode* newParamType =
                        substituteTypeParams(param->type, typeParams, typeArgs);
                    newParams->parameters.push_back(
                        new ParameterNode(newParamType, param->name));
                }

                // Create monomorphized method node
                auto* newMethod = new StructMethodNode(
                    newReturnType, method->name, newParams, method->body,
                    method->isPublic, method->isStatic);

                // Register the method (but don't generate code yet)
                structMethods[mangledName][method->name] =
                    std::make_pair(method->isPublic, newMethod);

                // Generate only the declaration (forward declaration)
                generateMethodDeclaration(mangledName, newMethod);
            }
        }
    }

    // Process methods defined inside the struct template
    if(templateStruct->members)
    {
        for(auto* method : templateStruct->members->methods)
        {
            // Substitute types in return type and parameters
            TypeNode* newReturnType =
                substituteTypeParams(method->returnType, typeParams, typeArgs);

            auto* newParams = new ParameterListNode();
            for(auto* param : method->parameters->parameters)
            {
                TypeNode* newParamType =
                    substituteTypeParams(param->type, typeParams, typeArgs);
                newParams->parameters.push_back(
                    new ParameterNode(newParamType, param->name));
            }

            // Create a new method node with substituted types
            auto* newMethod = new StructMethodNode(
                newReturnType, method->name, newParams, method->body,
                method->isPublic, method->isStatic);

            // Register the method
            structMethods[mangledName][method->name] =
                std::make_pair(method->isPublic, newMethod);

            // Generate only the declaration
            generateMethodDeclaration(mangledName, newMethod);
        }
    }
}

// Monomorphize an impl block (Note: this is no longer called from
// monomorphizeStruct but kept for potential future use)
void CodeGenerator::monomorphizeImplBlock(
    ImplBlockNode* impl, const std::vector<std::string>& typeParams,
    const std::vector<TypeNode*>& typeArgs,
    const std::string& mangledStructName)
{
    for(auto* method : impl->methods)
    {
        // Substitute types in return type
        TypeNode* newReturnType =
            substituteTypeParams(method->returnType, typeParams, typeArgs);

        // Substitute types in parameters
        auto* newParams = new ParameterListNode();
        for(auto* param : method->parameters->parameters)
        {
            TypeNode* newParamType =
                substituteTypeParams(param->type, typeParams, typeArgs);
            newParams->parameters.push_back(
                new ParameterNode(newParamType, param->name));
        }

        // Create monomorphized method
        auto* newMethod = new StructMethodNode(
            newReturnType, method->name, newParams, method->body,
            method->isPublic, method->isStatic);

        // Register the method
        structMethods[mangledStructName][method->name] =
            std::make_pair(method->isPublic, newMethod);

        // Generate only declaration (body is generated lazily on first call)
        generateMethodDeclaration(mangledStructName, newMethod);
    }
}

// Get or create a monomorphized struct type
std::string CodeGenerator::getOrCreateMonomorphizedStruct(
    const std::string& genericName, const std::vector<TypeNode*>& typeArgs)
{
    // Generate the mangled name
    std::string mangledName = generateMangledName(genericName, typeArgs);

    // Check if already monomorphized
    if(monomorphizedTypes.count(mangledName))
    {
        return mangledName;
    }

    // Check if this is actually a generic struct
    if(genericStructTemplates.find(genericName) == genericStructTemplates.end())
    {
        // Not a generic struct - might be a non-generic struct being used
        // Just return the original name
        return genericName;
    }

    // Monomorphize the struct
    monomorphizeStruct(genericName, typeArgs, mangledName);

    return mangledName;
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
