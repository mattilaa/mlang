#include "ir.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/DerivedTypes.h>

void CodeGenerator::initializeStdioFunctions()
{
    if(stdioInitialized)
        return;

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::FunctionType* printfType =
        llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {ptrType},
                                true);
    printfFunc = module->getOrInsertFunction("printf", printfType);

    llvm::FunctionType* fprintfType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context), {ptrType, ptrType}, true);
    fprintfFunc = module->getOrInsertFunction("fprintf", fprintfType);

    llvm::FunctionType* snprintfType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(context), {ptrType, int64Type, ptrType}, true);
    snprintfFunc = module->getOrInsertFunction("snprintf", snprintfType);

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

void CodeGenerator::initializePthreadFunctions()
{
    if(pthreadInitialized)
        return;

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* intType = llvm::Type::getInt32Ty(context);

    llvm::FunctionType* pthreadCreateType = llvm::FunctionType::get(
        intType, {ptrType, ptrType, ptrType, ptrType}, false);
    pthreadCreateFunc =
        module->getOrInsertFunction("pthread_create", pthreadCreateType);

    llvm::FunctionType* pthreadJoinType =
        llvm::FunctionType::get(intType, {ptrType, ptrType}, false);
    pthreadJoinFunc =
        module->getOrInsertFunction("pthread_join", pthreadJoinType);

    llvm::FunctionType* pthreadMutexInitType =
        llvm::FunctionType::get(intType, {ptrType, ptrType}, false);
    pthreadMutexInitFunc =
        module->getOrInsertFunction("pthread_mutex_init", pthreadMutexInitType);

    llvm::FunctionType* pthreadMutexDestroyType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexDestroyFunc = module->getOrInsertFunction(
        "pthread_mutex_destroy", pthreadMutexDestroyType);

    llvm::FunctionType* pthreadMutexLockType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexLockFunc =
        module->getOrInsertFunction("pthread_mutex_lock", pthreadMutexLockType);

    llvm::FunctionType* pthreadMutexUnlockType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexUnlockFunc = module->getOrInsertFunction(
        "pthread_mutex_unlock", pthreadMutexUnlockType);

    llvm::FunctionType* pthreadMutexAttrInitType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexAttrInitFunc = module->getOrInsertFunction(
        "pthread_mutexattr_init", pthreadMutexAttrInitType);

    llvm::FunctionType* pthreadMutexAttrSetTypeType =
        llvm::FunctionType::get(intType, {ptrType, intType}, false);
    pthreadMutexAttrSetTypeFunc = module->getOrInsertFunction(
        "pthread_mutexattr_settype", pthreadMutexAttrSetTypeType);

    llvm::FunctionType* pthreadMutexAttrDestroyType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    pthreadMutexAttrDestroyFunc = module->getOrInsertFunction(
        "pthread_mutexattr_destroy", pthreadMutexAttrDestroyType);

    pthreadInitialized = true;
}

void CodeGenerator::initializeStdlibFunctions()
{
    if(stdlibInitialized)
        return;

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* intType = llvm::Type::getInt32Ty(context);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    llvm::FunctionType* mallocType =
        llvm::FunctionType::get(ptrType, {int64Type}, false);
    mallocFunc = module->getOrInsertFunction("malloc", mallocType);

    llvm::FunctionType* freeType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), {ptrType}, false);
    freeFunc = module->getOrInsertFunction("free", freeType);

    llvm::FunctionType* strcmpType =
        llvm::FunctionType::get(intType, {ptrType, ptrType}, false);
    strcmpFunc = module->getOrInsertFunction("strcmp", strcmpType);

    llvm::FunctionType* jsonEscapeType =
        llvm::FunctionType::get(ptrType, {ptrType}, false);
    jsonEscapeFunc = module->getOrInsertFunction(
        "__mlang_std_strbuf_json_escape", jsonEscapeType);

    llvm::FunctionType* jsonParseType =
        llvm::FunctionType::get(int64Type, {ptrType}, false);
    jsonParseFunc = module->getOrInsertFunction("__mlang_std_json_parse",
                                                jsonParseType);

    llvm::FunctionType* jsonDocFreeType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {int64Type},
                                false);
    jsonDocFreeFunc = module->getOrInsertFunction("__mlang_std_json_doc_free",
                                                  jsonDocFreeType);

    llvm::FunctionType* jsonLastErrorType =
        llvm::FunctionType::get(ptrType, {}, false);
    jsonLastErrorFunc = module->getOrInsertFunction(
        "__mlang_std_json_last_error", jsonLastErrorType);

    llvm::FunctionType* jsonDocRootType =
        llvm::FunctionType::get(int64Type, {int64Type}, false);
    jsonDocRootFunc = module->getOrInsertFunction("__mlang_std_json_doc_root",
                                                  jsonDocRootType);

    llvm::FunctionType* jsonValueFreeType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {int64Type},
                                false);
    jsonValueFreeFunc = module->getOrInsertFunction(
        "__mlang_std_json_value_free", jsonValueFreeType);

    llvm::FunctionType* jsonValueKindType =
        llvm::FunctionType::get(intType, {int64Type}, false);
    jsonValueKindFunc = module->getOrInsertFunction(
        "__mlang_std_json_value_kind", jsonValueKindType);

    llvm::FunctionType* jsonObjectGetType =
        llvm::FunctionType::get(int64Type, {int64Type, ptrType}, false);
    jsonObjectGetFunc = module->getOrInsertFunction(
        "__mlang_std_json_object_get", jsonObjectGetType);

    llvm::FunctionType* jsonArrayGetType =
        llvm::FunctionType::get(int64Type, {int64Type, int64Type}, false);
    jsonArrayGetFunc = module->getOrInsertFunction(
        "__mlang_std_json_array_get", jsonArrayGetType);

    llvm::FunctionType* jsonAsBoolType =
        llvm::FunctionType::get(intType, {int64Type}, false);
    jsonAsBoolFunc = module->getOrInsertFunction(
        "__mlang_std_json_value_as_bool", jsonAsBoolType);

    llvm::FunctionType* jsonAsI64Type =
        llvm::FunctionType::get(int64Type, {int64Type}, false);
    jsonAsI64Func = module->getOrInsertFunction(
        "__mlang_std_json_value_as_i64", jsonAsI64Type);

    llvm::FunctionType* jsonAsF64Type =
        llvm::FunctionType::get(llvm::Type::getDoubleTy(context), {int64Type},
                                false);
    jsonAsF64Func = module->getOrInsertFunction(
        "__mlang_std_json_value_as_f64", jsonAsF64Type);

    llvm::FunctionType* jsonAsStringType =
        llvm::FunctionType::get(ptrType, {int64Type}, false);
    jsonAsStringFunc = module->getOrInsertFunction(
        "__mlang_std_json_value_as_string", jsonAsStringType);

    llvm::FunctionType* abortType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {}, false);
    abortFunc = module->getOrInsertFunction("abort", abortType);

    llvm::FunctionType* pushFrameType =
        llvm::FunctionType::get(int64Type, {}, false);
    exceptionsPushFrameFunc = module->getOrInsertFunction(
        "__mlang_std_exceptions_push_frame", pushFrameType);

    llvm::FunctionType* frameEnvType =
        llvm::FunctionType::get(ptrType, {int64Type}, false);
    exceptionsFrameEnvFunc = module->getOrInsertFunction(
        "__mlang_std_exceptions_frame_env", frameEnvType);

    llvm::FunctionType* setjmpType =
        llvm::FunctionType::get(intType, {ptrType}, false);
    exceptionsSetjmpFunc = module->getOrInsertFunction("_setjmp", setjmpType);
    if(auto* setjmpFn =
           llvm::dyn_cast<llvm::Function>(exceptionsSetjmpFunc.getCallee()))
    {
        setjmpFn->addFnAttr(llvm::Attribute::ReturnsTwice);
    }

    llvm::FunctionType* popFrameType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), {int64Type}, false);
    exceptionsPopFrameFunc = module->getOrInsertFunction(
        "__mlang_std_exceptions_pop_frame", popFrameType);

    llvm::FunctionType* throwType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), {ptrType, ptrType, intType}, false);
    exceptionsThrowFunc =
        module->getOrInsertFunction("__mlang_std_exceptions_throw", throwType);

    llvm::FunctionType* rethrowType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), {}, false);
    exceptionsRethrowFunc = module->getOrInsertFunction(
        "__mlang_std_exceptions_rethrow_current", rethrowType);

    llvm::FunctionType* takeStringType =
        llvm::FunctionType::get(ptrType, {}, false);
    exceptionsTakeTypeNameFunc = module->getOrInsertFunction(
        "__mlang_std_exceptions_take_type_name", takeStringType);
    exceptionsTakeMessageFunc = module->getOrInsertFunction(
        "__mlang_std_exceptions_take_message", takeStringType);

    llvm::FunctionType* takeLineType =
        llvm::FunctionType::get(intType, {}, false);
    exceptionsTakeSourceLineFunc = module->getOrInsertFunction(
        "__mlang_std_exceptions_take_source_line", takeLineType);

    stdlibInitialized = true;
}

void CodeGenerator::initializeFormatFunctions()
{
    initializeStdioFunctions();
    initializeStdlibFunctions();
}

llvm::Value* CodeGenerator::buildAlignedString(llvm::Value* value,
                                               llvm::Value* widthValue,
                                               char align, int line)
{
    initializeFormatFunctions();

#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
    llvm::Type* ptrType =
        llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
    llvm::Type* int32Type = llvm::Type::getInt32Ty(context);
    llvm::Type* int64Type = llvm::Type::getInt64Ty(context);

    if(!value->getType()->isPointerTy())
    {
        reportError(line,
                    "alignment format specifiers currently require a string");
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString("<format-error>");
#else
        return builder.CreateGlobalStringPtr("<format-error>");
#endif
    }

    if(!widthValue->getType()->isIntegerTy())
    {
        reportError(line, "format width must be an integer");
        widthValue = llvm::ConstantInt::get(int64Type, 0);
    }
    else if(widthValue->getType() != int64Type)
    {
        widthValue =
            builder.CreateSExtOrTrunc(widthValue, int64Type, "fmt.width");
    }

    if(!strbufAlignFunc)
    {
        llvm::FunctionType* alignType = llvm::FunctionType::get(
            ptrType, {ptrType, int64Type, int32Type}, false);
        strbufAlignFunc =
            module->getOrInsertFunction("__mlang_std_strbuf_align", alignType);
    }

    int32_t alignCode = 2;
    if(align == '<')
        alignCode = 0;
    else if(align == '^')
        alignCode = 1;

    return builder.CreateCall(
        strbufAlignFunc,
        {value, widthValue, llvm::ConstantInt::get(int32Type, alignCode)},
        "fmt.align");
}

llvm::AllocaInst*
CodeGenerator::createEntryBlockAlloca(llvm::Function* function,
                                      llvm::Type* type, const std::string& name)
{
    llvm::IRBuilder<> entryBuilder(&function->getEntryBlock(),
                                   function->getEntryBlock().begin());
    return entryBuilder.CreateAlloca(type, nullptr, name);
}
