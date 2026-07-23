#include "ir.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{

static std::string normalizeTestSuiteName(std::string name)
{
    if(name.empty())
        return "Main";

    for(char& c : name)
    {
        if(c == ':' || c == '/' || c == '\\' || c == '-' || c == ' ')
            c = '.';
    }
    while(name.find("..") != std::string::npos)
        name.replace(name.find(".."), 2, ".");
    if(!name.empty() && name.front() == '.')
        name.erase(name.begin());
    if(!name.empty() && name.back() == '.')
        name.pop_back();
    if(name.empty())
        return "Main";
    return name;
}

/// \brief Derive a default test suite name from the source file path.
///
/// Extracts the filename stem and passes it through
/// normalizeTestSuiteName().  Returns \c "Main" for the special
/// \c __mlang_test_root file or when the path is empty.
///
/// \see \ref test_sample.mla — produces suite name \c "test_sample"
static std::string defaultSuiteFromSourceFile(const std::string& sourceFileName)
{
    if(sourceFileName.empty())
        return "Main";
    std::error_code ec;
    std::filesystem::path p(sourceFileName);
    std::string stem = p.stem().string();
    if(ec || stem.empty() || stem == "__mlang_test_root")
        return "Main";
    return normalizeTestSuiteName(stem);
}

/// \brief Convert a raw test function name into a human-readable case name.
///
/// Strips a leading \c test_ prefix (if present) and replaces underscores
/// with spaces.  For example, \c test_result_ok becomes \c "result ok".
///
/// \see \ref test_sample.mla — contains \c test_addition and \c test_result_ok
static std::string humanizeTestCaseName(std::string name)
{
    if(name.rfind("test_", 0) == 0)
        name.erase(0, 5);
    for(char& c : name)
    {
        if(c == '_')
            c = ' ';
    }
    return name.empty() ? "unnamed test" : name;
}

} // namespace

void CodeGenerator::generateTestMain(
    const std::vector<FunctionDefNode*>& tests,
    const std::vector<FixtureTestEntry>& fixtureTests)
{
    initializeStdioFunctions();

    llvm::Type* i32Type = llvm::Type::getInt32Ty(context);
    llvm::Type* i8PtrType = llvm::PointerType::get(context, 0);
    llvm::FunctionType* mainType = llvm::FunctionType::get(i32Type, {}, false);
    llvm::Function* mainFn = llvm::Function::Create(
        mainType, llvm::Function::ExternalLinkage, "main", module.get());

    llvm::BasicBlock* entry =
        llvm::BasicBlock::Create(context, "entry", mainFn);
    builder.SetInsertPoint(entry);

    llvm::AllocaInst* failures =
        builder.CreateAlloca(i32Type, nullptr, "failures");
    builder.CreateStore(llvm::ConstantInt::get(i32Type, 0), failures);
    llvm::AllocaInst* totalTests =
        builder.CreateAlloca(i32Type, nullptr, "total_tests");
    builder.CreateStore(llvm::ConstantInt::get(i32Type, 0), totalTests);

    auto make_cstr = [&](const std::string& s, const char* name) -> llvm::Value*
    {
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString(s, name);
#else
        return builder.CreateGlobalStringPtr(s, name);
#endif
    };
    llvm::FunctionType* reportTestType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        {i32Type, i32Type, i8PtrType, i8PtrType, i32Type}, false);
    llvm::FunctionType* reportSummaryType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), {i32Type, i32Type, i32Type}, false);
    llvm::FunctionType* setCurrentTestType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), {i32Type, i32Type, i8PtrType}, false);
    llvm::FunctionCallee reportTestFunc = module->getOrInsertFunction(
        "__mlang_std_testing_report_test", reportTestType);
    llvm::FunctionCallee reportSummaryFunc = module->getOrInsertFunction(
        "__mlang_std_testing_report_summary", reportSummaryType);
    llvm::FunctionCallee setCurrentTestFunc = module->getOrInsertFunction(
        "__mlang_std_testing_set_current_test", setCurrentTestType);
    llvm::Value* passStatus = make_cstr("PASS", "test.status.pass");
    llvm::Value* failStatus = make_cstr("FAIL", "test.status.fail");
    const std::string defaultSuite = defaultSuiteFromSourceFile(sourceFileName);

    for(auto* testFn : tests)
    {
        if(!testFn || testFn->isExtern)
            continue;
        llvm::Function* callee =
            module->getFunction(functionSymbolName(testFn));
        if(!callee)
            continue;

        std::string suiteName =
            !testFn->sourceModule.empty()
                ? normalizeTestSuiteName(testFn->sourceModule)
                : defaultSuite;
        std::string displayName =
            suiteName + "." + humanizeTestCaseName(testFn->name);

        if(!testFilter.empty() &&
           displayName.find(testFilter) == std::string::npos &&
           testFn->name.find(testFilter) == std::string::npos)
            continue;

        llvm::Value* totalCur =
            builder.CreateLoad(i32Type, totalTests, "tests.cur");
        llvm::Value* totalNext = builder.CreateAdd(
            totalCur, llvm::ConstantInt::get(i32Type, 1), "tests.next");
        builder.CreateStore(totalNext, totalTests);
        llvm::Value* testName = make_cstr(displayName, "test.name");
        llvm::Value* testIndex = totalNext;
        builder.CreateCall(setCurrentTestFunc,
                           {testIndex, totalNext, testName});
        llvm::Value* result = builder.CreateCall(callee, {});
        if(callee->getReturnType()->isVoidTy())
        {
            builder.CreateCall(reportTestFunc,
                               {testIndex, totalNext, passStatus, testName,
                                llvm::ConstantInt::get(i32Type, 0)});
            continue;
        }

        llvm::Value* resultI32 = result;
        if(resultI32->getType() != i32Type)
        {
            if(resultI32->getType()->isIntegerTy())
            {
                resultI32 = builder.CreateIntCast(resultI32, i32Type, true,
                                                  "test.rc.cast");
            }
            else
            {
                resultI32 = llvm::ConstantInt::get(i32Type, 1);
            }
        }

        llvm::Value* isFail = builder.CreateICmpNE(
            resultI32, llvm::ConstantInt::get(i32Type, 0), "testfail");
        llvm::Value* failInc = builder.CreateZExt(isFail, i32Type, "failinc");
        llvm::Value* cur =
            builder.CreateLoad(i32Type, failures, "failures.cur");
        llvm::Value* next = builder.CreateAdd(cur, failInc, "failures.next");
        builder.CreateStore(next, failures);

        llvm::BasicBlock* passBB =
            llvm::BasicBlock::Create(context, "test.pass", mainFn);
        llvm::BasicBlock* failBB =
            llvm::BasicBlock::Create(context, "test.fail");
        llvm::BasicBlock* contBB =
            llvm::BasicBlock::Create(context, "test.cont");
        builder.CreateCondBr(isFail, failBB, passBB);

        builder.SetInsertPoint(passBB);
        builder.CreateCall(reportTestFunc,
                           {testIndex, totalNext, passStatus, testName,
                            llvm::ConstantInt::get(i32Type, 0)});
        builder.CreateBr(contBB);

        failBB->insertInto(mainFn);
        builder.SetInsertPoint(failBB);
        builder.CreateCall(reportTestFunc, {testIndex, totalNext, failStatus,
                                            testName, resultI32});
        builder.CreateBr(contBB);

        contBB->insertInto(mainFn);
        builder.SetInsertPoint(contBB);
    }

    // Fixture-test methods: each runs against a fresh stack-allocated,
    // zero-initialized instance. We call <Struct>_setup, then the test method
    // (passing the instance pointer as self), then <Struct>_teardown.
    for(const auto& fxEntry : fixtureTests)
    {
        ImplBlockNode* impl = fxEntry.implBlock;
        StructMethodNode* method = fxEntry.method;
        if(!impl || !method)
            continue;

        const std::string& structName = impl->structName;
        llvm::StructType* structType = getStructType(structName);
        if(!structType)
        {
            reportError(method->line, "fixture struct '" + structName +
                                          "' is not defined or has no fields");
            continue;
        }

        std::string testMangled = structName + "_" + method->name;
        llvm::Function* methodFn = module->getFunction(testMangled);
        if(!methodFn)
        {
            reportError(method->line, "fixture test method '" + structName +
                                          "::" + method->name +
                                          "' was not generated");
            continue;
        }

        std::string suiteName =
            !method->sourceModule.empty()
                ? normalizeTestSuiteName(method->sourceModule)
                : defaultSuite;
        std::string displayName = suiteName + "." + structName + "_" +
                                  humanizeTestCaseName(method->name);

        if(!testFilter.empty() &&
           displayName.find(testFilter) == std::string::npos &&
           method->name.find(testFilter) == std::string::npos)
            continue;

        llvm::Value* totalCur =
            builder.CreateLoad(i32Type, totalTests, "tests.cur");
        llvm::Value* totalNext = builder.CreateAdd(
            totalCur, llvm::ConstantInt::get(i32Type, 1), "tests.next");
        builder.CreateStore(totalNext, totalTests);
        llvm::Value* testName = make_cstr(displayName, "fxtest.name");
        llvm::Value* testIndex = totalNext;
        builder.CreateCall(setCurrentTestFunc,
                           {testIndex, totalNext, testName});

        // Fresh, zero-initialized fixture instance per test.
        llvm::AllocaInst* fxAlloca =
            builder.CreateAlloca(structType, nullptr, "fixture");
        builder.CreateStore(llvm::Constant::getNullValue(structType), fxAlloca);

        // Optional setup hook.
        if(llvm::Function* setupFn = module->getFunction(structName + "_setup"))
        {
            builder.CreateCall(setupFn, {fxAlloca});
        }

        // Run the test method.
        llvm::Value* result = builder.CreateCall(methodFn, {fxAlloca});

        // Optional teardown hook (always runs, even when test fails
        // non-fatally).
        llvm::Function* teardownFn =
            module->getFunction(structName + "_teardown");

        if(methodFn->getReturnType()->isVoidTy())
        {
            if(teardownFn)
                builder.CreateCall(teardownFn, {fxAlloca});
            builder.CreateCall(reportTestFunc,
                               {testIndex, totalNext, passStatus, testName,
                                llvm::ConstantInt::get(i32Type, 0)});
            continue;
        }

        llvm::Value* resultI32 = result;
        if(resultI32->getType() != i32Type)
        {
            if(resultI32->getType()->isIntegerTy())
            {
                resultI32 = builder.CreateIntCast(resultI32, i32Type, true,
                                                  "fxtest.rc.cast");
            }
            else
            {
                resultI32 = llvm::ConstantInt::get(i32Type, 1);
            }
        }

        if(teardownFn)
            builder.CreateCall(teardownFn, {fxAlloca});

        llvm::Value* isFail = builder.CreateICmpNE(
            resultI32, llvm::ConstantInt::get(i32Type, 0), "fxtestfail");
        llvm::Value* failInc = builder.CreateZExt(isFail, i32Type, "fxfailinc");
        llvm::Value* cur =
            builder.CreateLoad(i32Type, failures, "fxfailures.cur");
        llvm::Value* next = builder.CreateAdd(cur, failInc, "fxfailures.next");
        builder.CreateStore(next, failures);

        llvm::BasicBlock* passBB =
            llvm::BasicBlock::Create(context, "fxtest.pass", mainFn);
        llvm::BasicBlock* failBB =
            llvm::BasicBlock::Create(context, "fxtest.fail");
        llvm::BasicBlock* contBB =
            llvm::BasicBlock::Create(context, "fxtest.cont");
        builder.CreateCondBr(isFail, failBB, passBB);

        builder.SetInsertPoint(passBB);
        builder.CreateCall(reportTestFunc,
                           {testIndex, totalNext, passStatus, testName,
                            llvm::ConstantInt::get(i32Type, 0)});
        builder.CreateBr(contBB);

        failBB->insertInto(mainFn);
        builder.SetInsertPoint(failBB);
        builder.CreateCall(reportTestFunc, {testIndex, totalNext, failStatus,
                                            testName, resultI32});
        builder.CreateBr(contBB);

        contBB->insertInto(mainFn);
        builder.SetInsertPoint(contBB);
    }

    llvm::Value* total =
        builder.CreateLoad(i32Type, failures, "failures.total");
    llvm::Value* totalCount =
        builder.CreateLoad(i32Type, totalTests, "tests.total");
    llvm::Value* passCount = builder.CreateSub(totalCount, total, "tests.pass");
    builder.CreateCall(reportSummaryFunc, {totalCount, passCount, total});

    builder.CreateRet(total);
}

void CodeGenerator::generateBenchmarkMain(
    const std::vector<FunctionDefNode*>& tests)
{
    initializeStdioFunctions();

    llvm::Type* i32Type = llvm::Type::getInt32Ty(context);
    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
    llvm::FunctionType* mainType = llvm::FunctionType::get(i32Type, {}, false);
    llvm::Function* mainFn = llvm::Function::Create(
        mainType, llvm::Function::ExternalLinkage, "main", module.get());

    llvm::BasicBlock* entry =
        llvm::BasicBlock::Create(context, "entry", mainFn);
    builder.SetInsertPoint(entry);

    llvm::FunctionCallee nowNsFunc = module->getOrInsertFunction(
        "__mlang_std_time_now_ns", llvm::FunctionType::get(i64Type, {}, false));

    llvm::AllocaInst* failures =
        builder.CreateAlloca(i32Type, nullptr, "bench_failures");
    builder.CreateStore(llvm::ConstantInt::get(i32Type, 0), failures);

    auto make_cstr = [&](const std::string& s, const char* name) -> llvm::Value*
    {
#if LLVM_VERSION_MAJOR >= 21
        return builder.CreateGlobalString(s, name);
#else
        return builder.CreateGlobalStringPtr(s, name);
#endif
    };

    const std::string defaultSuite = defaultSuiteFromSourceFile(sourceFileName);
    size_t benchNameWidth = std::string("warmup(iters)").size();
    benchNameWidth = std::max(benchNameWidth, std::string("name").size());
    for(auto* testFn : tests)
    {
        if(!testFn || testFn->isExtern)
            continue;
        std::string suiteName =
            !testFn->sourceModule.empty()
                ? normalizeTestSuiteName(testFn->sourceModule)
                : defaultSuite;
        std::string displayName = suiteName + "." + testFn->name;
        benchNameWidth = std::max(benchNameWidth, displayName.size());
    }
    benchNameWidth += 2;
    llvm::Value* benchNameWidthVal =
        llvm::ConstantInt::get(i32Type, static_cast<int>(benchNameWidth));

    llvm::Value* headerFmt =
        make_cstr("[BENCH] %-*s %12s %12s %10s\n", "bench.header.fmt");
    llvm::Value* lineFmt =
        make_cstr("[BENCH] %-*s %12lld %12lld %10d\n", "bench.line.fmt");
    llvm::Value* failFmt =
        make_cstr("[BENCH-FAIL] %-*s failures=%d\n", "bench.fail.fmt");

    llvm::Value* nsTotalHdr = make_cstr("total_ns", "bench.hdr.total");
    llvm::Value* nsPerOpHdr = make_cstr("ns/op", "bench.hdr.nsop");
    llvm::Value* itersHdr = make_cstr("iters", "bench.hdr.iters");
    llvm::Value* warmupLabel = make_cstr("warmup(iters)", "bench.warmup.label");
    llvm::Value* warmupFmt =
        make_cstr("[BENCH] %-*s %12d\n", "bench.warmup.fmt");
    builder.CreateCall(printfFunc, {warmupFmt, benchNameWidthVal, warmupLabel,
                                    llvm::ConstantInt::get(
                                        i32Type, benchmarkWarmupIterations)});
    builder.CreateCall(printfFunc, {headerFmt, benchNameWidthVal,
                                    make_cstr("name", "bench.hdr.name"),
                                    nsTotalHdr, nsPerOpHdr, itersHdr});

    for(auto* testFn : tests)
    {
        if(!testFn || testFn->isExtern)
            continue;
        llvm::Function* callee =
            module->getFunction(functionSymbolName(testFn));
        if(!callee)
            continue;

        std::string suiteName =
            !testFn->sourceModule.empty()
                ? normalizeTestSuiteName(testFn->sourceModule)
                : defaultSuite;
        std::string displayName = suiteName + "." + testFn->name;
        llvm::Value* testName = make_cstr(displayName, "bench.name");
        llvm::AllocaInst* localFails =
            builder.CreateAlloca(i32Type, nullptr, "bench.local_fails");
        builder.CreateStore(llvm::ConstantInt::get(i32Type, 0), localFails);

        auto emitLoop = [&](int loopCount)
        {
            llvm::AllocaInst* idx =
                builder.CreateAlloca(i32Type, nullptr, "bench.i");
            builder.CreateStore(llvm::ConstantInt::get(i32Type, 0), idx);

            llvm::BasicBlock* condBB =
                llvm::BasicBlock::Create(context, "bench.cond", mainFn);
            llvm::BasicBlock* bodyBB =
                llvm::BasicBlock::Create(context, "bench.body", mainFn);
            llvm::BasicBlock* endBB =
                llvm::BasicBlock::Create(context, "bench.end", mainFn);
            builder.CreateBr(condBB);

            builder.SetInsertPoint(condBB);
            llvm::Value* iCur = builder.CreateLoad(i32Type, idx, "bench.i.cur");
            llvm::Value* cond = builder.CreateICmpSLT(
                iCur, llvm::ConstantInt::get(i32Type, loopCount),
                "bench.loopcond");
            builder.CreateCondBr(cond, bodyBB, endBB);

            builder.SetInsertPoint(bodyBB);
            llvm::Value* rc = builder.CreateCall(callee, {});
            if(!callee->getReturnType()->isVoidTy())
            {
                llvm::Value* rcI32 = rc;
                if(rcI32->getType() != i32Type &&
                   rcI32->getType()->isIntegerTy())
                    rcI32 = builder.CreateIntCast(rcI32, i32Type, true,
                                                  "bench.rc.cast");
                llvm::Value* isFail = builder.CreateICmpNE(
                    rcI32, llvm::ConstantInt::get(i32Type, 0), "bench.isfail");
                llvm::Value* failInc =
                    builder.CreateZExt(isFail, i32Type, "bench.failinc");
                llvm::Value* cur =
                    builder.CreateLoad(i32Type, localFails, "bench.fail.cur");
                builder.CreateStore(builder.CreateAdd(cur, failInc),
                                    localFails);
            }
            llvm::Value* iNext = builder.CreateAdd(
                iCur, llvm::ConstantInt::get(i32Type, 1), "bench.i.next");
            builder.CreateStore(iNext, idx);
            builder.CreateBr(condBB);

            builder.SetInsertPoint(endBB);
        };

        if(benchmarkWarmupIterations > 0)
            emitLoop(benchmarkWarmupIterations);

        llvm::Value* startNs = builder.CreateCall(nowNsFunc, {}, "bench.start");
        emitLoop(benchmarkIterations);
        llvm::Value* endNs = builder.CreateCall(nowNsFunc, {}, "bench.end");

        llvm::Value* elapsedNs =
            builder.CreateSub(endNs, startNs, "bench.elapsed");
        llvm::Value* iterI64 =
            llvm::ConstantInt::get(i64Type, benchmarkIterations);
        llvm::Value* nsPerOp =
            builder.CreateSDiv(elapsedNs, iterI64, "bench.nsop");

        builder.CreateCall(
            printfFunc,
            {lineFmt, benchNameWidthVal, testName, elapsedNs, nsPerOp,
             llvm::ConstantInt::get(i32Type, benchmarkIterations)});

        llvm::Value* lf =
            builder.CreateLoad(i32Type, localFails, "bench.localfails");
        llvm::Value* hasFails = builder.CreateICmpNE(
            lf, llvm::ConstantInt::get(i32Type, 0), "bench.hasfails");
        llvm::BasicBlock* okBB =
            llvm::BasicBlock::Create(context, "bench.ok", mainFn);
        llvm::BasicBlock* failBB =
            llvm::BasicBlock::Create(context, "bench.fail", mainFn);
        llvm::BasicBlock* contBB =
            llvm::BasicBlock::Create(context, "bench.cont", mainFn);
        builder.CreateCondBr(hasFails, failBB, okBB);

        builder.SetInsertPoint(failBB);
        builder.CreateCall(printfFunc,
                           {failFmt, benchNameWidthVal, testName, lf});
        llvm::Value* totalFailCur =
            builder.CreateLoad(i32Type, failures, "bench.totalfail.cur");
        builder.CreateStore(builder.CreateAdd(totalFailCur, lf), failures);
        builder.CreateBr(contBB);

        builder.SetInsertPoint(okBB);
        builder.CreateBr(contBB);

        builder.SetInsertPoint(contBB);
    }

    llvm::Value* totalFails =
        builder.CreateLoad(i32Type, failures, "bench.failures.total");
    builder.CreateRet(totalFails);
}
