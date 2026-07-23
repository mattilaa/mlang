#include "ir.h"
#include "ir/common.h"
#include "ir/expression_type_kind.h"

#include <llvm/Config/llvm-config.h>

using mlang::ir_detail::common::Helpers;


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
                argValues.push_back(Helpers::create_global_cstring(builder, "<struct>"));
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
        argValues.push_back(Helpers::create_global_cstring(builder, "<struct>"));
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

llvm::Value* CodeGenerator::buildDebugString(ExpressionNode* expr, bool pretty,
                                             int line)
{
    llvm::Value* val = generateExpression(expr);
    if(!val)
        return Helpers::create_global_cstring(builder, "<null>");
    if(val->getType()->isStructTy())
    {
        std::string structName = val->getType()->getStructName().str();
        if(structName.empty())
            structName = getStructTypeName(expr);
        return buildStructDebugString(val, structName, pretty, line);
    }

    std::vector<llvm::Value*> argValues;
    std::string cFormat;
    appendFormatValue(expr, val, false, false, false, cFormat, argValues, line);

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(cFormat, "dbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(cFormat, "dbgfmt");
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
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 = builder.CreateCall(snprintfFunc, sizeArgs, "dbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "dbglen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "dbgsz");
    llvm::Value* buffer = builder.CreateCall(mallocFunc, {size}, "dbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

llvm::Value*
CodeGenerator::buildStructDebugString(llvm::Value* structVal,
                                      const std::string& structName,
                                      bool pretty, int line)
{
    initializeFormatFunctions();

    auto it = structMembers.find(structName);
    if(it == structMembers.end())
    {
        reportError(line, "unknown struct for debug: " + structName);
        return Helpers::create_global_cstring(builder, "<struct>");
    }

    std::string displayName = structName;
    if(auto dit = structDebugDisplayNames.find(structName);
       dit != structDebugDisplayNames.end())
    {
        displayName = dit->second;
    }
    if(auto mit = mangledToGenericName.find(structName);
       mit != mangledToGenericName.end())
    {
        displayName = mit->second;
    }

    std::string fmt = displayName + (pretty ? " {\n" : " { ");
    std::vector<llvm::Value*> argValues;

    for(size_t idx = 0; idx < it->second.size(); ++idx)
    {
        const auto& member = it->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;

        if(pretty)
            fmt += "    " + memberName + ": ";
        else
            fmt += memberName + ": ";

        llvm::Value* fieldVal = builder.CreateExtractValue(
            structVal, static_cast<unsigned>(idx), "dbgfield");

        bool handled = false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType))
        {
            std::string fieldStruct = structRef->structName;
            if(!debugStructs.count(fieldStruct))
            {
                reportError(line, "struct '" + fieldStruct +
                                      "' does not derive Debug");
            }
            llvm::Value* fieldStr =
                buildStructDebugString(fieldVal, fieldStruct, pretty, line);
            fmt += "%s";
            argValues.push_back(fieldStr);
            handled = true;
        }

        if(!handled)
        {
            switch(memberType->kind)
            {
            case TypeNode::TYPE_BOOL:
            {
                fmt += "%d";
                llvm::Value* intVal = builder.CreateZExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgbool");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_I8:
            case TypeNode::TYPE_I16:
            case TypeNode::TYPE_INT:
            {
                fmt += "%d";
                llvm::Value* intVal = builder.CreateSExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgint");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_U8:
            case TypeNode::TYPE_U16:
            case TypeNode::TYPE_U32:
            {
                fmt += "%u";
                llvm::Value* intVal = builder.CreateZExt(
                    fieldVal, llvm::Type::getInt32Ty(context), "dbgu");
                argValues.push_back(intVal);
                break;
            }
            case TypeNode::TYPE_I32:
            {
                fmt += "%d";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_I64:
            {
                fmt += "%lld";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_U64:
            {
                fmt += "%llu";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_FLOAT:
            {
                fmt += "%f";
                llvm::Value* doubleVal = builder.CreateFPExt(
                    fieldVal, llvm::Type::getDoubleTy(context), "dbgfloat");
                argValues.push_back(doubleVal);
                break;
            }
            case TypeNode::TYPE_DOUBLE:
            {
                fmt += "%f";
                argValues.push_back(fieldVal);
                break;
            }
            case TypeNode::TYPE_STRING:
            case TypeNode::TYPE_STR8:
            case TypeNode::TYPE_STR16:
            {
                fmt += "%s";
                argValues.push_back(fieldVal);
                break;
            }
            default:
                fmt += "<unsupported>";
                break;
            }
        }

        if(pretty)
            fmt += ",\n";
        else if(idx + 1 < it->second.size())
            fmt += ", ";
        else
            fmt += " ";
    }

    if(pretty)
        fmt += "}";
    else
        fmt += "}";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(fmt, "dbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(fmt, "dbgfmt");
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
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 = builder.CreateCall(snprintfFunc, sizeArgs, "dbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "dbglen64");
    llvm::Value* size =
        builder.CreateAdd(len64, llvm::ConstantInt::get(int64Type, 1), "dbgsz");
    llvm::Value* buffer = builder.CreateCall(mallocFunc, {size}, "dbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

llvm::Value* CodeGenerator::buildStructJsonString(llvm::Value* structVal,
                                                  const std::string& structName,
                                                  bool pretty, int line,
                                                  int indentLevel)
{
    initializeFormatFunctions();

    auto escapeJsonStringValue = [&](llvm::Value* strVal) -> llvm::Value*
    { return builder.CreateCall(jsonEscapeFunc, {strVal}, "json.escape"); };

    auto it = structMembers.find(structName);
    if(it == structMembers.end())
    {
        reportError(line, "unknown struct for json debug: " + structName);
        return Helpers::create_global_cstring(builder, "{}");
    }

    std::string displayName = structName;
    if(auto dit = structDebugDisplayNames.find(structName);
       dit != structDebugDisplayNames.end())
    {
        displayName = dit->second;
    }
    if(auto mit = mangledToGenericName.find(structName);
       mit != mangledToGenericName.end())
    {
        displayName = mit->second;
    }

    std::string currentIndent(indentLevel * 2, ' ');
    std::string childIndent((indentLevel + 1) * 2, ' ');
    std::string innerSep = pretty ? ",\n" + childIndent : ",";
    std::string fmt = "{";
    if(pretty)
        fmt += "\n" + childIndent;
    fmt += "\"type\": \"" + displayName + "\"";
    std::vector<llvm::Value*> argValues;

    for(size_t idx = 0; idx < it->second.size(); ++idx)
    {
        const auto& member = it->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;
        llvm::Value* fieldVal = builder.CreateExtractValue(
            structVal, static_cast<unsigned>(idx), "jsonfield");

        fmt += innerSep + "\"" + memberName + "\": ";

        bool handled = false;

        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType))
        {
            std::string resolvedEnumName =
                resolveVisibleEnumName(structRef->structName);
            if(!resolvedEnumName.empty())
            {
                fmt += "\"%s\"";
                llvm::Value* enumStr =
                    buildEnumString(fieldVal, resolvedEnumName, line);
                argValues.push_back(escapeJsonStringValue(enumStr));
                handled = true;
            }
            else
            {
                std::string fieldStruct = structRef->structName;
                if(!debugStructs.count(fieldStruct))
                {
                    reportError(line, "struct '" + fieldStruct +
                                          "' does not derive Debug");
                }
                llvm::Value* fieldStr = buildStructJsonString(
                    fieldVal, fieldStruct, pretty, line, indentLevel + 1);
                fmt += "%s";
                argValues.push_back(fieldStr);
                handled = true;
            }
        }

        if(handled)
            continue;

        switch(memberType ? memberType->kind : TypeNode::TYPE_VOID)
        {
        case TypeNode::TYPE_BOOL:
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
            fmt += "%s";
            argValues.push_back(builder.CreateSelect(
                fieldVal, trueStr, falseStr, "json.field.bool"));
            break;
        }
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
            fmt += "%d";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_I64:
            fmt += "%lld";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
            fmt += "%u";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U64:
            fmt += "%llu";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_FLOAT:
        {
            fmt += "%f";
            llvm::Value* doubleVal = builder.CreateFPExt(
                fieldVal, llvm::Type::getDoubleTy(context), "json.float");
            argValues.push_back(doubleVal);
            break;
        }
        case TypeNode::TYPE_DOUBLE:
            fmt += "%f";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_STR8:
        case TypeNode::TYPE_STR16:
        case TypeNode::TYPE_PTR:
            fmt += "\"%s\"";
            argValues.push_back(escapeJsonStringValue(fieldVal));
            break;
        default:
            fmt += "\"<unsupported>\"";
            break;
        }
    }

    if(pretty)
        fmt += "\n" + currentIndent;
    fmt += "}";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(fmt, "jsondbgfmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(fmt, "jsondbgfmt");
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
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 =
        builder.CreateCall(snprintfFunc, sizeArgs, "jsondbglen");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "jsondbglen64");
    llvm::Value* size = builder.CreateAdd(
        len64, llvm::ConstantInt::get(int64Type, 1), "jsondbgsz");
    llvm::Value* buffer = builder.CreateCall(mallocFunc, {size}, "jsondbgbuf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

llvm::Value* CodeGenerator::buildStructSerdeJsonString(
    llvm::Value* structVal, const std::string& structName, int line,
    int indentLevel)
{
    initializeFormatFunctions();

    auto escapeJsonStringValue = [&](llvm::Value* strVal) -> llvm::Value*
    { return builder.CreateCall(jsonEscapeFunc, {strVal}, "json.escape"); };

    auto it = structMembers.find(structName);
    if(it == structMembers.end())
    {
        reportError(line, "unknown struct for json serde: " + structName);
        return Helpers::create_global_cstring(builder, "{}");
    }

    std::string displayName = structName;
    if(auto dit = structDebugDisplayNames.find(structName);
       dit != structDebugDisplayNames.end())
    {
        displayName = dit->second;
    }
    if(auto mit = mangledToGenericName.find(structName);
       mit != mangledToGenericName.end())
    {
        displayName = mit->second;
    }

    std::string currentIndent(indentLevel * 2, ' ');
    std::string childIndent((indentLevel + 1) * 2, ' ');
    std::string innerSep = ",\n" + childIndent;
    std::string fmt = "{\n" + childIndent + "\"type\": \"" + displayName + "\"";
    std::vector<llvm::Value*> argValues;

    std::string propFmt;
    std::vector<llvm::Value*> propArgs;
    bool hasPropertyMetadata = false;

    for(size_t idx = 0; idx < it->second.size(); ++idx)
    {
        const auto* access = getStructFieldAccessInfo(structName,
                                                      static_cast<int>(idx));
        if(access && access->isSynthesizedPropertyStorage)
            continue;

        const auto& member = it->second[idx];
        const std::string& memberName = member.first;
        TypeNode* memberType = member.second;
        llvm::Value* fieldVal = builder.CreateExtractValue(
            structVal, static_cast<unsigned>(idx), "jsonserde.field");

        fmt += innerSep + "\"" + memberName + "\": ";

        bool handled = false;
        if(auto* structRef = dynamic_cast<StructTypeRefNode*>(memberType))
        {
            std::string resolvedEnumName =
                resolveVisibleEnumName(structRef->structName);
            if(!resolvedEnumName.empty())
            {
                fmt += "\"%s\"";
                llvm::Value* enumStr =
                    buildEnumString(fieldVal, resolvedEnumName, line);
                argValues.push_back(escapeJsonStringValue(enumStr));
                handled = true;
            }
            else
            {
                std::string fieldStruct = structRef->structName;
                if(!jsonStructs.count(fieldStruct))
                {
                    reportError(line, "struct '" + fieldStruct +
                                          "' does not derive Json");
                    return Helpers::create_global_cstring(builder, "{}");
                }
                llvm::Value* fieldStr = buildStructSerdeJsonString(
                    fieldVal, fieldStruct, line, indentLevel + 1);
                fmt += "%s";
                argValues.push_back(fieldStr);
                handled = true;
            }
        }

        if(handled)
            goto append_property_metadata;

        switch(memberType ? memberType->kind : TypeNode::TYPE_VOID)
        {
        case TypeNode::TYPE_BOOL:
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
            fmt += "%s";
            argValues.push_back(builder.CreateSelect(
                fieldVal, trueStr, falseStr, "jsonserde.bool"));
            break;
        }
        case TypeNode::TYPE_I8:
        case TypeNode::TYPE_I16:
        case TypeNode::TYPE_INT:
        case TypeNode::TYPE_I32:
            fmt += "%d";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_I64:
            fmt += "%lld";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U8:
        case TypeNode::TYPE_U16:
        case TypeNode::TYPE_U32:
            fmt += "%u";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_U64:
            fmt += "%llu";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_FLOAT:
        {
            fmt += "%f";
            llvm::Value* doubleVal = builder.CreateFPExt(
                fieldVal, llvm::Type::getDoubleTy(context),
                "jsonserde.float");
            argValues.push_back(doubleVal);
            break;
        }
        case TypeNode::TYPE_DOUBLE:
            fmt += "%f";
            argValues.push_back(fieldVal);
            break;
        case TypeNode::TYPE_STR8:
            fmt += "\"%s\"";
            argValues.push_back(escapeJsonStringValue(fieldVal));
            break;
        default:
            reportError(line, "field '" + memberName + "' of struct '" +
                                  structName +
                                  "' has unsupported Json derive type");
            return Helpers::create_global_cstring(builder, "{}");
        }

append_property_metadata:
        if(access && access->isProperty)
        {
            if(!hasPropertyMetadata)
            {
                propFmt = "\"@property\": {";
                hasPropertyMetadata = true;
            }
            else
            {
                propFmt += ",";
            }

            propFmt += "\"" + memberName + "\": {"
                       "\"hidden\": %s,"
                       "\"protected\": %s,"
                       "\"atomic\": %s,"
                       "\"mutex\": %s,"
                       "\"recursive\": %s}";

#if LLVM_VERSION_MAJOR >= 21
            llvm::Value* trueStr =
                builder.CreateGlobalString("true", "json.meta.true");
            llvm::Value* falseStr =
                builder.CreateGlobalString("false", "json.meta.false");
#else
            llvm::Value* trueStr =
                builder.CreateGlobalStringPtr("true", "json.meta.true");
            llvm::Value* falseStr =
                builder.CreateGlobalStringPtr("false", "json.meta.false");
#endif

            auto boolStr = [&](bool flag, const char* name) -> llvm::Value* {
                return flag ? trueStr : falseStr;
            };

            propArgs.push_back(boolStr(
                access->encapsulation == FieldEncapsulation::Hidden,
                "hidden"));
            propArgs.push_back(boolStr(
                access->encapsulation == FieldEncapsulation::Protected,
                "protected"));
            propArgs.push_back(
                boolStr(access->isAtomicProperty, "atomic"));
            propArgs.push_back(boolStr(access->isMutexProperty, "mutex"));
            propArgs.push_back(
                boolStr(access->isRecursiveProperty, "recursive"));
        }
    }

    if(hasPropertyMetadata)
    {
        propFmt += "}";
        fmt += innerSep + propFmt;
        argValues.insert(argValues.end(), propArgs.begin(), propArgs.end());
    }

    fmt += "\n" + currentIndent + "}";

#if LLVM_VERSION_MAJOR >= 21
    llvm::Value* formatStr = builder.CreateGlobalString(fmt, "jsonserdefmt");
#else
    llvm::Value* formatStr = builder.CreateGlobalStringPtr(fmt, "jsonserdefmt");
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
    std::vector<llvm::Value*> sizeArgs = {nullPtr, zero, formatStr};
    sizeArgs.insert(sizeArgs.end(), argValues.begin(), argValues.end());
    llvm::Value* len32 =
        builder.CreateCall(snprintfFunc, sizeArgs, "jsonserde.len");
    llvm::Value* len64 = builder.CreateSExt(len32, int64Type, "jsonserde.len64");
    llvm::Value* size = builder.CreateAdd(
        len64, llvm::ConstantInt::get(int64Type, 1), "jsonserde.size");
    llvm::Value* buffer =
        builder.CreateCall(mallocFunc, {size}, "jsonserde.buf");
    std::vector<llvm::Value*> writeArgs = {buffer, size, formatStr};
    writeArgs.insert(writeArgs.end(), argValues.begin(), argValues.end());
    builder.CreateCall(snprintfFunc, writeArgs);
    return buffer;
}

