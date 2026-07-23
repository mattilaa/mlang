#include "ir.h"
#include "ir/expression_type_kind.h"

#include <llvm/Config/llvm-config.h>

namespace
{

static llvm::Value* create_global_cstring(llvm::IRBuilder<>& builder,
                                          llvm::StringRef text,
                                          const llvm::Twine& name = "")
{
#if LLVM_VERSION_MAJOR >= 21
    return builder.CreateGlobalString(text, name);
#else
    return builder.CreateGlobalStringPtr(text, name);
#endif
}

} // namespace

void CodeGenerator::appendFormatValue(ExpressionNode* expr, llvm::Value* value,
                                      bool debug, bool pretty, bool json,
                                      std::string& cFormat,
                                      std::vector<llvm::Value*>& argValues,
                                      int line)
{
    auto escapeJsonStringValue = [&](llvm::Value* strVal) -> llvm::Value*
    {
        if(!json)
            return strVal;
        return builder.CreateCall(jsonEscapeFunc, {strVal}, "json.escape");
    };

    llvm::Type* argType = value->getType();
    std::string enumTypeName = getEnumTypeName(expr, line);
    if(!enumTypeName.empty())
    {
        cFormat += json ? "\"%s\"" : "%s";
        llvm::Value* enumStr = buildEnumString(value, enumTypeName, line);
        argValues.push_back(escapeJsonStringValue(enumStr));
        return;
    }

    if(argType->isStructTy())
    {
        std::string structName = argType->getStructName().str();
        if(structName.empty())
            structName = getStructTypeName(expr);
        if(!(debug || (!structName.empty() && debugStructs.count(structName))))
        {
            // fall through to non-debug handling below
        }
        else
        {
            if(structName.empty())
            {
                reportError(line, "cannot debug-format unnamed struct");
                cFormat += "%s";
                argValues.push_back(create_global_cstring(builder, "<struct>"));
                return;
            }
            if(!debugStructs.count(structName))
            {
                reportError(line, "struct '" + structName +
                                      "' does not derive Debug");
            }
            llvm::Value* dbg =
                json ? buildStructJsonString(value, structName, pretty, line)
                     : buildStructDebugString(value, structName,
                                              debug ? pretty : false, line);
            cFormat += "%s";
            argValues.push_back(dbg);
            return;
        }
    }

    TypeNode::TypeKind typeKind = getExpressionTypeKind(expr, variableTypes);
    if(TypeNode* exprType = getLValueType(expr, line))
        typeKind = exprType->kind;
    bool isUnsigned = isUnsignedType(typeKind);

    if(argType->isIntegerTy(1))
    {
        if(json)
        {
#if LLVM_VERSION_MAJOR >= 21
            llvm::Value* trueStr =
                builder.CreateGlobalString("true", "json.true");
            llvm::Value* falseStr =
                builder.CreateGlobalString("false", "json.false");
#else
            llvm::Value* trueStr =
                builder.CreateGlobalStringPtr("true", "json.true");
            llvm::Value* falseStr =
                builder.CreateGlobalStringPtr("false", "json.false");
#endif
            cFormat += "%s";
            argValues.push_back(
                builder.CreateSelect(value, trueStr, falseStr, "json.bool"));
        }
        else
        {
            cFormat += "%d";
            llvm::Value* intVal = builder.CreateZExt(
                value, llvm::Type::getInt32Ty(context), "booltoInt");
            argValues.push_back(intVal);
        }
    }
    else if(argType->isIntegerTy(8))
    {
        if(isUnsigned)
        {
            cFormat += "%u";
            llvm::Value* intVal = builder.CreateZExt(
                value, llvm::Type::getInt32Ty(context), "u8toInt");
            argValues.push_back(intVal);
        }
        else
        {
            cFormat += "%d";
            llvm::Value* intVal = builder.CreateSExt(
                value, llvm::Type::getInt32Ty(context), "i8toInt");
            argValues.push_back(intVal);
        }
    }
    else if(argType->isIntegerTy(16))
    {
        if(isUnsigned)
        {
            cFormat += "%u";
            llvm::Value* intVal = builder.CreateZExt(
                value, llvm::Type::getInt32Ty(context), "u16toInt");
            argValues.push_back(intVal);
        }
        else
        {
            cFormat += "%d";
            llvm::Value* intVal = builder.CreateSExt(
                value, llvm::Type::getInt32Ty(context), "i16toInt");
            argValues.push_back(intVal);
        }
    }
    else if(argType->isIntegerTy(32))
    {
        cFormat += isUnsigned ? "%u" : "%d";
        argValues.push_back(value);
    }
    else if(argType->isIntegerTy(64))
    {
        cFormat += isUnsigned ? "%llu" : "%lld";
        argValues.push_back(value);
    }
    else if(argType->isFloatTy())
    {
        cFormat += "%f";
        llvm::Value* doubleVal = builder.CreateFPExt(
            value, llvm::Type::getDoubleTy(context), "floatToDouble");
        argValues.push_back(doubleVal);
    }
    else if(argType->isDoubleTy())
    {
        cFormat += "%f";
        argValues.push_back(value);
    }
    else if(argType->isPointerTy())
    {
        cFormat += json ? "\"%s\"" : "%s";
        argValues.push_back(escapeJsonStringValue(value));
    }
    else if(argType->isStructTy())
    {
        std::string structName = argType->getStructName().str().empty()
                                     ? "anonymous struct"
                                     : argType->getStructName().str();
        reportError(line, "cannot print struct type '" + structName +
                              "' directly; use {:?} with #[derive(Debug)]");
        cFormat += "%s";
        argValues.push_back(create_global_cstring(builder, "<struct>"));
    }
    else
    {
        reportError(line, "cannot print value of unknown type");
        cFormat += "%d";
        argValues.push_back(value);
    }
}

void CodeGenerator::generatePrintStatement(PrintNode* node)
{
    if(node->debugOnly && !debugEnabled)
        return;

    // Initialize stdio functions if not already done
    if(!stdioInitialized)
    {
        initializeStdioFunctions();
    }

    // Convert MLA format string to C format string and collect argument values
    std::vector<llvm::Value*> argValues;
    std::string cFormat =
        convertFormatString(node->formatString, node->arguments,
                            node->namedArguments, argValues, node->line);

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

llvm::Value* CodeGenerator::generateFormatExpression(FormatNode* node)
{
    initializeFormatFunctions();

    std::vector<llvm::Value*> argValues;
    std::string cFormat =
        convertFormatString(node->formatString, node->arguments,
                            node->namedArguments, argValues, node->line);

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "formatstr");
#else
    llvm::Value* formatStr =
        builder.CreateGlobalStringPtr(cFormat, "formatstr");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::Value* nullPtr =
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType));
    llvm::Value* zero = llvm::ConstantInt::get(int64Type, 0);

    std::vector<llvm::Value*> sizeArgs;
    sizeArgs.push_back(nullPtr);
    sizeArgs.push_back(zero);
    sizeArgs.push_back(formatStr);
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());

    llvm::Value* len32 = builder.CreateCall(snprintfFunc, sizeArgs, "fmtlen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "fmtlen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "fmtsz");

    llvm::Value* buffer = builder.CreateCall(mallocFunc, {size}, "fmtbuf");

    std::vector<llvm::Value*> writeArgs;
    writeArgs.push_back(buffer);
    writeArgs.push_back(size);
    writeArgs.push_back(formatStr);
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);

    return buffer;
}

void CodeGenerator::generateAssert(AssertNode* node)
{
    initializeFormatFunctions();

    llvm::Value* cond = generateExpression(node->condition);
    if(!cond)
        return;

    llvm::Value* condBool = nullptr;
    if(!convertValueToRuntimeBool(cond, node->line, "assert!", condBool))
        return;

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* okBB =
        llvm::BasicBlock::Create(context, "assert.ok", function);
    llvm::BasicBlock* failBB = llvm::BasicBlock::Create(context, "assert.fail");
    builder.CreateCondBr(condBool, okBB, failBB);

    failBB->insertInto(function);
    builder.SetInsertPoint(failBB);

    std::string cFormat = "assert! failed\n";
#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "assertmsg");
#else
    llvm::Value* formatStr =
        builder.CreateGlobalStringPtr(cFormat, "assertmsg");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Value* stderrVal = builder.CreateLoad(ptrType, stderrPtr, "stderr");
    builder.CreateCall(fprintfFunc, {stderrVal, formatStr});
    builder.CreateCall(abortFunc);
    builder.CreateUnreachable();

    builder.SetInsertPoint(okBB);
}

void CodeGenerator::generateAssertEq(AssertEqNode* node)
{
    initializeFormatFunctions();

    llvm::Value* lhs = generateExpression(node->left);
    llvm::Value* rhs = generateExpression(node->right);
    if(!lhs || !rhs)
        return;

    llvm::Value* cmp = nullptr;
    if(isStringExpression(node->left) || isStringExpression(node->right))
    {
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::Value* lhsPtr = builder.CreateBitCast(lhs, ptrType, "lhsstr");
        llvm::Value* rhsPtr = builder.CreateBitCast(rhs, ptrType, "rhsstr");
        llvm::Value* cmpVal = builder.CreateCall(strcmpFunc, {lhsPtr, rhsPtr});
        cmp = builder.CreateICmpEQ(
            cmpVal, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
            "streq");
    }
    else if(lhs->getType()->isFloatingPointTy() ||
            rhs->getType()->isFloatingPointTy())
    {
        llvm::Value* l = lhs;
        llvm::Value* r = rhs;
        if(!l->getType()->isDoubleTy())
            l = builder.CreateFPExt(l, llvm::Type::getDoubleTy(context), "l2d");
        if(!r->getType()->isDoubleTy())
            r = builder.CreateFPExt(r, llvm::Type::getDoubleTy(context), "r2d");
        cmp = builder.CreateFCmpOEQ(l, r, "feq");
    }
    else if(lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy())
    {
        llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
        llvm::Value* l = lhs;
        llvm::Value* r = rhs;
        if(l->getType() != int64Type)
            l = builder.CreateSExtOrTrunc(l, int64Type, "l2i64");
        if(r->getType() != int64Type)
            r = builder.CreateSExtOrTrunc(r, int64Type, "r2i64");
        cmp = builder.CreateICmpEQ(l, r, "ieq");
    }
    else
    {
        reportError(node->line,
                    "assert_eq! supports only numeric and string types");
        return;
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* okBB =
        llvm::BasicBlock::Create(context, "assert.ok", function);
    llvm::BasicBlock* failBB = llvm::BasicBlock::Create(context, "assert.fail");
    builder.CreateCondBr(cmp, okBB, failBB);

    failBB->insertInto(function);
    builder.SetInsertPoint(failBB);

    std::string msg = "assert_eq! failed: left = {:#?}, right = {:#?}";
    std::vector<ExpressionNode*> args = {node->left, node->right};
    std::vector<llvm::Value*> argValues;
    std::string cFormat = convertFormatString(msg, args, argValues, node->line);

    // Ensure newline
    cFormat += "\n";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "assertfmt");
#else
    llvm::Value* formatStr =
        builder.CreateGlobalStringPtr(cFormat, "assertfmt");
#endif

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Value* stderrVal = builder.CreateLoad(ptrType, stderrPtr, "stderr");

    std::vector<llvm::Value*> printArgs;
    printArgs.push_back(stderrVal);
    printArgs.push_back(formatStr);
    printArgs.insert(printArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(fprintfFunc, printArgs);

    builder.CreateCall(abortFunc, {});
    builder.CreateUnreachable();

    builder.SetInsertPoint(okBB);
}

void CodeGenerator::generateStaticAssert(StaticAssertNode* node)
{
    bool cond = false;
    if(!evaluateCompileTimeBool(node->condition, cond))
    {
        llvm::Value* folded =
            node->condition ? generateExpression(node->condition) : nullptr;
        if(auto* foldedInt = llvm::dyn_cast_or_null<llvm::ConstantInt>(folded))
        {
            cond = !foldedInt->isZero();
        }
        else
        {
            reportError(
                node->line,
                "static_assert! requires a compile-time boolean expression");
            return;
        }
    }
    if(!cond)
    {
        reportError(node->line, "static_assert! failed");
    }
}
