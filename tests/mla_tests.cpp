/**
 * MLA Language Test Suite
 *
 * This test suite uses Google Test to verify the functionality of the MLA
 * compiler. Tests compile MLA source code, run the resulting executable,
 * and verify the output matches expectations.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// Test fixture for MLA compiler tests
class MLATest : public ::testing::Test
{
public:
    static std::string compilerPath;
    static std::string stdlibLibDir;

protected:
    std::string testDir;
    std::string sourceFile;
    std::string outputExe;

    static void SetUpTestSuite()
    {
        // Try environment variable first, then compile-time default
        const char* envPath = std::getenv("MLA_COMPILER");
        if(envPath && std::strlen(envPath) > 0)
        {
            compilerPath = envPath;
        }
#ifdef DEFAULT_COMPILER_PATH
        else
        {
            compilerPath = DEFAULT_COMPILER_PATH;
        }
#endif

        const char* envLibDir = std::getenv("MLANG_STDLIB_LIB_PATH");
        if(envLibDir && std::strlen(envLibDir) > 0)
        {
            stdlibLibDir = envLibDir;
        }
#ifdef DEFAULT_MLANG_LIB_DIR
        else
        {
            stdlibLibDir = DEFAULT_MLANG_LIB_DIR;
        }
#endif
    }

    void SetUp() override
    {
        // Create a temporary directory for test files
        testDir =
            fs::temp_directory_path() /
            ("mla_test_" + std::to_string(static_cast<long long>(getpid())) +
             "_" + std::to_string(rand()));
        fs::create_directories(testDir);
        sourceFile = testDir + "/test.mla";
        outputExe = testDir + "/test_exe";
    }

    void TearDown() override
    {
        // Clean up temporary files
        fs::remove_all(testDir);
    }

    // Write MLA source code to a file
    void writeSource(const std::string& code)
    {
        std::ofstream file(sourceFile);
        file << code;
        file.close();
    }

    // Compile the MLA source file
    // Returns true if compilation succeeded
    bool compile(
        bool expectSuccess = true,
        const std::string& options = std::string())
    {
        std::string cmd = compilerPath + " -o " + outputExe + " " + sourceFile;
        if(!options.empty())
            cmd += " " + options;
        if(!stdlibLibDir.empty())
            cmd += " -L " + stdlibLibDir + " -lmlang_std";
        cmd += " 2>&1";
        int result = system(cmd.c_str());
        if(expectSuccess)
        {
            return result == 0;
        }
        return result != 0;
    }

    std::string compileCapture(
        int& exitCode,
        const std::string& options = std::string())
    {
        std::string cmd = compilerPath + " -o " + outputExe + " " + sourceFile;
        if(!options.empty())
            cmd += " " + options;
        if(!stdlibLibDir.empty())
            cmd += " -L " + stdlibLibDir + " -lmlang_std";
        cmd += " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if(!pipe)
        {
            exitCode = -1;
            return "";
        }
        std::string output;
        char buffer[256];
        while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            output += buffer;
        }
        int rc = pclose(pipe);
        if(WIFEXITED(rc))
            exitCode = WEXITSTATUS(rc);
        else
            exitCode = -1;
        return output;
    }

    std::string compilePathCapture(const fs::path& srcPath, int& exitCode)
    {
        std::string cmd =
            compilerPath + " -o " + outputExe + " " + srcPath.string();
        if(!stdlibLibDir.empty())
            cmd += " -L " + stdlibLibDir + " -lmlang_std";
        cmd += " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if(!pipe)
        {
            exitCode = -1;
            return "";
        }
        std::string output;
        char buffer[256];
        while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            output += buffer;
        }
        int rc = pclose(pipe);
        if(WIFEXITED(rc))
            exitCode = WEXITSTATUS(rc);
        else
            exitCode = -1;
        return output;
    }

    // Run the compiled executable and capture stdout
    std::string run()
    {
        std::string cmd = outputExe + " 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if(!pipe)
            return "";

        std::string result;
        char buffer[256];
        while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }

    // Run the compiled executable and capture stderr
    std::string runStderr()
    {
        std::string cmd = outputExe + " 2>&1 1>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if(!pipe)
            return "";

        std::string result;
        char buffer[256];
        while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }

    // Run and get exit code
    int runExitCode()
    {
        std::string cmd = outputExe;
        int status = system(cmd.c_str());
        if(status == -1)
            return -1;
        if(WIFEXITED(status))
            return WEXITSTATUS(status);
        if(WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        return -1;
    }

    // Compile and run, returning stdout
    std::string compileAndRun(const std::string& code)
    {
        writeSource(code);
        if(!compile())
        {
            return "COMPILE_ERROR";
        }
        return run();
    }

    // Compile and run, returning exit code
    int compileAndRunExitCode(const std::string& code)
    {
        writeSource(code);
        if(!compile())
        {
            return -1;
        }
        return runExitCode();
    }
};

std::string MLATest::compilerPath = "./mlang";
std::string MLATest::stdlibLibDir = "";

// ============================================================================
// Basic Program Tests
// ============================================================================

TEST_F(MLATest, EmptyMain)
{
    std::string code = R"(
        fn main() -> i32 {
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MainReturnsValue)
{
    std::string code = R"(
        fn main() -> i32 {
            return 42;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 42);
}

TEST_F(MLATest, CompoundAssignmentOperators)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn main() -> i32 {
            var i: i32 = 3;
            i += 1;
            if i != 4 { return 1; }
            i *= 2;
            if i != 8 { return 2; }
            i ^= 2;
            if i != 10 { return 3; }
            i &= 7;
            if i != 2 { return 4; }
            i |= 8;
            if i != 10 { return 5; }
            i -= 2;
            if i != 8 { return 6; }
            i /= 2;
            if i != 4 { return 7; }
            i %= 3;
            if i != 1 { return 8; }
            i <<= 4;
            if i != 16 { return 9; }
            i >>= 2;
            return i == 4 ? 0 : 10;
        }
    )"),
        0);
}

TEST_F(MLATest, BitShiftsAcrossIntegerWidths)
{
    for(const std::string type :
        {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"})
    {
        SCOPED_TRACE(type);
        EXPECT_EQ(
            compileAndRunExitCode(
                "fn main() -> i32 {"
                "    let value: " +
                type +
                " = 12;"
                "    if (value << 2) != 48 { return 1; }"
                "    if (value >> 2) != 3 { return 2; }"
                "    if (value << 0) != value { return 3; }"
                "    if (value >> 0) != value { return 4; }"
                "    if (value << 1) != 24 { return 5; }"
                "    if (value << 3) != 96 { return 6; }"
                "    if (value >> 1) != 6 { return 7; }"
                "    if (value >> 3) != 1 { return 8; }"
                "    if (value >> 4) != 0 { return 9; }"
                "    return 0;"
                "}"),
            0);
    }
}

TEST_F(MLATest, BitShiftRightSignedAndUnsigned)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn main() -> i32 {
            let signed32: i32 = -8;
            let signed64: i64 = -8;
            let unsigned32: u32 = 0x80000000u32;
            // Construct the high bit without an out-of-range signed literal.
            let unsigned64: u64 = 0x4000000000000000u64 + 0x4000000000000000u64;
            if (signed32 >> 1) != -4 { return 1; }
            if (signed64 >> 1i64) != -4i64 { return 2; }
            if (unsigned32 >> 1u32) != 0x40000000u32 { return 3; }
            if (unsigned64 >> 1u64) != 0x4000000000000000u64 { return 4; }
            if (unsigned32 >> 31u32) != 1u32 { return 5; }
            if (unsigned64 >> 63u64) != 1u64 { return 6; }
            if (1u32 << 31u32) != unsigned32 { return 7; }
            if (1u64 << 63u64) != unsigned64 { return 8; }
            if (0x80u8 >> 7u8) != 1u8 { return 9; }
            if (1u8 << 7u8) != 0x80u8 { return 10; }
            if (0x8000u16 >> 15u16) != 1u16 { return 11; }
            if (1u16 << 15u16) != 0x8000u16 { return 12; }
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, BitShiftsWithRuntimeCountsAndCompoundAssignments)
{
    writeSource(R"(
        fn check(count: i64, wide_count: u64, expected_signed: i32,
                 expected_unsigned: u32, expected_narrow: u8) -> i32 {
            var signed_value: i32 = -16;
            var unsigned_value: u32 = 0x80000000u32;
            var narrow: u8 = 3u8;
            if (signed_value >> count) != expected_signed { return 1; }
            if (unsigned_value >> wide_count) != expected_unsigned { return 2; }
            if (narrow << count) != expected_narrow { return 3; }
            signed_value >>= count;
            unsigned_value >>= wide_count;
            narrow <<= count;
            if signed_value != expected_signed { return 4; }
            if unsigned_value != expected_unsigned { return 5; }
            if narrow != expected_narrow { return 6; }
            narrow >>= count;
            unsigned_value <<= wide_count;
            return narrow == 3u8 && unsigned_value == 0x80000000u32 ? 0 : 7;
        }
        fn main() -> i32 {
            if check(0i64, 0u64, -16, 0x80000000u32, 3u8) != 0 { return 1; }
            if check(1i64, 1u64, -8, 0x40000000u32, 6u8) != 0 { return 2; }
            if check(2i64, 2u64, -4, 0x20000000u32, 12u8) != 0 { return 3; }
            if check(3i64, 3u64, -2, 0x10000000u32, 24u8) != 0 { return 4; }
            if check(4i64, 4u64, -1, 0x08000000u32, 48u8) != 0 { return 5; }
            return 0;
        }
    )");
    for(const std::string options : {"-O0", "-O2"})
    {
        SCOPED_TRACE(options);
        ASSERT_TRUE(compile(true, options));
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, BitShiftsRejectFloatingPointOperands)
{
    for(const std::string expression :
        {"1.0 << 2", "1 >> 2.0", "1.0 >> 2", "1 << 2.0"})
    {
        SCOPED_TRACE(expression);
        writeSource(
            "fn main() -> i32 { let value = " + expression + "; return 0; }");
        int rc = 0;
        const std::string out = compileCapture(rc);
        EXPECT_NE(rc, 0);
        EXPECT_NE(
            out.find("bitwise operations require integer operands"),
            std::string::npos)
            << out;
    }
}

TEST_F(MLATest, BitwiseAndAssignmentOnMutableTargets)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        struct Flags { var value: u32; };
        fn main() -> i32 {
            var flags = Flags { value: 15u32 };
            flags.value &= 6u32;
            var values: mutmultiarray<i32, 2> = {15, 7};
            values[0] &= 5;
            var narrow: u8 = 15u8;
            narrow &= 3u8;
            return flags.value == 6u32 && values[0] == 5 && narrow == 3u8 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, BitwiseAndAssignmentRejectsImmutableTarget)
{
    writeSource(R"(
        fn main() -> i32 {
            let flags: i32 = 15;
            flags &= 7;
            return flags;
        }
    )");
    EXPECT_TRUE(compile(false));
}

TEST_F(MLATest, UserDefinedLiteralDemo)
{
    const fs::path example = fs::path(__FILE__).parent_path().parent_path() /
                             "examples/user_defined_literals_demo.mla";
    std::ifstream input(example);
    ASSERT_TRUE(input.is_open()) << example;
    std::ostringstream source;
    source << input.rdbuf();
    writeSource(source.str());
    for(const std::string options : {"-O0", "-O2"})
    {
        SCOPED_TRACE(options);
        ASSERT_TRUE(compile(true, options));
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, UserDefinedLiteralDispatchesByLiteralCategory)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn operator""_kind(value: u64) -> i32 { return 1; }
        fn operator "" _kind(value: f64) -> i32 { return 2; }
        fn operator ""_kind(value: str8) -> i32 { return 3; }
        fn operator "" _64(value: u64) -> u64 { return value; }
        fn operator "" __units(value: f64) -> f64 { return value; }
        fn main() -> i32 {
            if 42_kind != 1 { return 1; }
            if 42.0_kind != 2 { return 2; }
            if "42"_kind != 3 { return 3; }
            if 4_64 != 4u64 || 1.5__units != 1.5 { return 4; }
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, UserDefinedLiteralIntegerBasesAndFullUnsignedRange)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn operator "" _value(value: u64) -> u64 { return value; }
        fn main() -> i32 {
            if 42_value != 0x2A_value { return 1; }
            if 0b101010_value != 42_value { return 2; }
            if 00042_value != 42u64 { return 3; }
            if 18446744073709551615_value != ~0u64 { return 4; }
            if 0xFFFFFFFFFFFFFFFF_value != ~0u64 { return 5; }
            if 0b1111111111111111111111111111111111111111111111111111111111111111_value != ~0u64 { return 6; }
            if 9223372036854775808_value != (1u64 << 63) { return 7; }
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, UserDefinedLiteralFloatingFormsAndPrecedence)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn operator "" _scaled(value: f64) -> f64 { return value * 2.0 + 1.0; }
        fn operator "" _raw(value: f64) -> f64 { return value; }
        fn main() -> i32 {
            if 1.5_scaled != 4.0 { return 1; }
            if .5_scaled != 2.0 { return 2; }
            if 1._scaled != 3.0 { return 3; }
            if 1e2_scaled != 201.0 { return 4; }
            if 1.5e+1_scaled != 31.0 { return 5; }
            if 2.5e-1_scaled != 1.5 { return 6; }
            if -1.5_scaled != -4.0 { return 7; }
            if 2.0 * 1.5_scaled + 1.0 != 9.0 { return 8; }
            if 5e-324_raw <= 0.0 { return 9; }
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, UserDefinedLiteralStringsDecodeEscapes)
{
    EXPECT_EQ(
        compileAndRun(R"(
        fn operator "" _text(value: str8) -> str8 { return value; }
        fn main() -> i32 {
            println!("{}", "hello\n\"world\"\t\x21"_text);
            println!("{}", ""_text);
            return 0;
        }
    )"),
        "hello\n\"world\"\t!\n\n");
}

TEST_F(MLATest, UserDefinedLiteralBuiltinResultsAndExistingSuffixes)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn operator "" _small(value: u64) -> u8 { return u8(value); }
        fn operator "" _positive(value: f64) -> bool { return value > 0.0; }
        fn operator "" _single(value: f64) -> f32 { return f32(value); }
        fn main() -> i32 {
            let small: u8 = 7_small;
            if small != 7u8 { return 1; }
            if !1.5_positive { return 2; }
            if 1.5_single != 1.5f { return 3; }
            let typed: i64 = 3i64;
            let hex: u8 = 0xFFu8;
            let binary: u8 = 0b11u8;
            return typed == 3 && hex == 255u8 && binary == 3u8 ? 0 : 4;
        }
    )"),
        0);
}

TEST_F(MLATest, UserDefinedLiteralNumericConstexpr)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        cexpr fn operator "" _twice(value: u64) -> u64 { return value * 2u64; }
        cexpr fn operator "" _half(value: f64) -> f64 { return value / 2.0; }
        cexpr count: u64 = 3_twice;
        fn main() -> i32 {
            cexpr half: f64 = 9.0_half;
            if count != 6u64 || half != 4.5 { return 1; }
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, UserDefinedLiteralConstexprPreservesUnsignedSemantics)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        cexpr fn operator "" _shift(value: u64) -> u64 { return value >> 1u64; }
        cexpr fn operator "" _divide(value: u64) -> u64 { return value / 2u64; }
        cexpr fn operator "" _increment(value: u64) -> u64 { return value + 1u64; }
        cexpr fn operator "" _positive(value: u64) -> bool { return value > 0u64; }
        cexpr fn operator "" _odd(value: u64) -> bool { return value % 2u64 == 1u64; }
        fn main() -> i32 {
            cexpr shifted: u64 = 18446744073709551615_shift;
            cexpr divided: u64 = 18446744073709551615_divide;
            cexpr wrapped: u64 = 18446744073709551615_increment;
            cexpr positive: bool = 18446744073709551615_positive;
            cexpr odd: bool = 18446744073709551615_odd;
            if shifted != 0x7FFFFFFFFFFFFFFFu64 || divided != shifted { return 1; }
            if wrapped != 0u64 || !positive || !odd { return 2; }
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, UserDefinedLiteralLooksUpEnclosingNamespaces)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn operator "" _fallback(value: u64) -> u64 { return value + 10u64; }
        namespace first {
            cexpr fn operator "" _unit(value: u64) -> u64 { return value * 10u64; }
            cexpr fn amount() -> u64 { return 4_unit; }
            fn operator "" _raw(value: u64) -> u64 { return value; }
            fn high_half() -> u64 { return 18446744073709551615_raw >> 1; }
            fn fallback() -> u64 { return 1_fallback; }
        }
        namespace second {
            fn operator "" _unit(value: u64) -> u64 { return value * 100u64; }
            fn amount() -> u64 { return 4_unit; }
        }
        fn main() -> i32 {
            cexpr first_value: u64 = first::amount();
            if first_value != 40u64 { return 1; }
            if second::amount() != 400u64 { return 2; }
            if first::high_half() != 0x7FFFFFFFFFFFFFFFu64 { return 4; }
            return first::fallback() == 11u64 ? 0 : 3;
        }
    )"),
        0);
}

TEST_F(MLATest, UserDefinedLiteralRejectsMissingCategory)
{
    writeSource(R"(
        fn operator "" _float_only(value: f64) -> f64 { return value; }
        fn main() -> i32 { let value = 1_float_only; return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find(
            "no user-defined literal operator for suffix "
            "'_float_only' with input type 'u64'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, UserDefinedLiteralRejectsUnknownSuffix)
{
    writeSource(R"(fn main() -> i32 { return 42_unknown; })");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("no user-defined literal operator for suffix '_unknown'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, UserDefinedLiteralRejectsInvalidSignatures)
{
    for(const std::string definition :
        {"fn operator \"\" _bad() -> i64 { return 0; }",
         "fn operator \"\" _bad(value: i64) -> i64 { return value; }",
         "fn operator \"\" _bad(value: f32) -> f32 { return value; }",
         "fn operator \"\" _bad(value: &u64) -> u64 { return value; }",
         "fn operator \"\" _bad(value: u64, other: u64) -> u64 { return value; "
         "}",
         "fn operator \"\" _bad(value: u64, ...) -> u64 { return value; }",
         "fn operator \"\" _bad(value: u64) -> void { return; }",
         "fn operator \"\" _bad(value: u64) -> list<i32> { return [1]; }"})
    {
        SCOPED_TRACE(definition);
        writeSource(definition + " fn main() -> i32 { return 0; }");
        int rc = 0;
        const std::string out = compileCapture(rc);
        EXPECT_EQ(rc, 1);
        EXPECT_NE(out.find("MLANG-E1019"), std::string::npos) << out;
    }
}

TEST_F(MLATest, UserDefinedLiteralRejectsInvalidSuffixName)
{
    writeSource(R"(
        fn operator "" bad(value: u64) -> u64 { return value; }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("suffix must begin with '_'"), std::string::npos) << out;
}

TEST_F(MLATest, UserDefinedLiteralRejectsOutOfRangeNumbers)
{
    for(const std::string literal :
        {"18446744073709551616_value",
         "0x10000000000000000_value",
         "1e9999_value"})
    {
        SCOPED_TRACE(literal);
        writeSource(
            "fn main() -> i32 { let value = " + literal + "; return 0; }");
        int rc = 0;
        const std::string out = compileCapture(rc);
        EXPECT_EQ(rc, 1);
        EXPECT_NE(out.find("MLANG-E1019"), std::string::npos) << out;
        EXPECT_NE(out.find("out of range"), std::string::npos) << out;
    }
}

TEST_F(MLATest, UserDefinedLiteralRejectsDuplicateCategory)
{
    writeSource(R"(
        fn operator "" _dup(value: u64) -> u64 { return value; }
        fn operator "" _dup(value: u64) -> u64 { return value + 1u64; }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("duplicate literal operator"), std::string::npos) << out;
}

TEST_F(MLATest, UserDefinedLiteralImportsPublicModuleOperators)
{
    {
        std::ofstream module(testDir + "/literal_units.mla");
        module << R"(
            cexpr fn operator "" _factor(value: u64) -> u64 { return value; }
            pub cexpr fn operator "" _tick(value: u64) -> u64 { return value * 10_factor; }
        )";
    }
    writeSource(R"(
        mod literal_units;
        use literal_units::*;
        fn main() -> i32 {
            cexpr ticks: u64 = 4_tick;
            return ticks == 40u64 ? 0 : 1;
        }
    )");
    ASSERT_TRUE(compile());
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, UserDefinedLiteralPrivateModuleOperatorIsNotVisible)
{
    {
        std::ofstream module(testDir + "/literal_private.mla");
        module << R"(
            cexpr fn operator "" _hidden(value: u64) -> u64 { return value; }
        )";
    }
    for(const std::string declaration :
        {"let value = 4_hidden;", "cexpr value: u64 = 4_hidden;"})
    {
        SCOPED_TRACE(declaration);
        writeSource(
            "mod literal_private; use literal_private::*; fn main() -> i32 { " +
            declaration + " return 0; }");
        int rc = 0;
        const std::string out = compileCapture(rc);
        EXPECT_EQ(rc, 1) << out;
    }
}

TEST_F(MLATest, UserDefinedLiteralRejectsAmbiguousPublicImports)
{
    for(const std::string name : {"literal_first", "literal_second"})
    {
        std::ofstream module(testDir + "/" + name + ".mla");
        module
            << R"(pub fn operator "" _same(value: u64) -> u64 { return value; })";
    }
    writeSource(R"(
        mod literal_first;
        mod literal_second;
        use literal_first::*;
        use literal_second::*;
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("ambiguous literal operator"), std::string::npos) << out;
}

TEST_F(MLATest, ConceptsDemo)
{
    const fs::path example = fs::path(__FILE__).parent_path().parent_path() /
                             "examples/concepts_demo.mla";
    std::ifstream input(example);
    ASSERT_TRUE(input.is_open()) << example;
    std::ostringstream source;
    source << input.rdbuf();
    writeSource(source.str());
    for(const std::string options : {"-O0", "-O2"})
    {
        SCOPED_TRACE(options);
        ASSERT_TRUE(compile(true, options));
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, ConceptsBooleanQueriesAndAliases)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Integral<T> = type_id(T) == i32 || type_id(T) == i64;
        concept Same<T, U> = type_id(T) == type_id(U);
        alias Number = i32;
        cexpr global_ok: bool = Integral<i32>;
        fn main() -> i32 {
            static_assert!(Integral<Number>);
            static_assert!(!Integral<f64>);
            static_assert!(Same<ptr<i32>, ptr<i32>>);
            static_assert!(!Same<i32, f64>);
            let value = Integral<i64>;
            return global_ok && value ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsRequiresClauseFiltersBeforeBodyInstantiation)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Whole<T> = type_id(T) == i32;
        generic<T> where Whole<T>
        fn choose(value: T) -> i32 { return value + 1; }
        generic<T>
        fn choose(value: T) -> i32 { return 7; }
        struct Marker { var ignored: i32; };
        fn main() -> i32 {
            let number: i32 = 41;
            if choose(number) != 42 { return 1; }
            let marker = Marker { ignored: 0 };
            return choose(marker) == 7 ? 0 : 2;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsLosingViableOverloadDoesNotInstantiateBody)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Whole<T> = type_id(T) == i32;
        generic<T>
        fn choose(value: T) -> i32 { return value.missing; }
        generic<T> where Whole<T>
        fn choose(value: T) -> i32 { return 42; }
        fn main() -> i32 {
            let value: i32 = 1;
            return choose(value) == 42 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsConjunctionAndDisjunctionSubsumption)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Whole<T> = type_id(T) == i32 || type_id(T) == i64;
        concept Floating<T> = type_id(T) == f64;
        concept Numeric<T> = Whole<T> || Floating<T>;
        concept Narrow<T> = Whole<T> && type_id(T) == i32;
        generic<T> where Numeric<T>
        fn rank(value: T) -> i32 { return 1; }
        generic<T> where Whole<T>
        fn rank(value: T) -> i32 { return 2; }
        generic<T> where Narrow<T>
        fn rank(value: T) -> i32 { return 3; }
        fn main() -> i32 {
            let small: i32 = 1;
            let wide: i64 = 1;
            return rank(small) == 3 && rank(wide) == 2 && rank(1.0) == 1 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsIncomparableOverloadsAreAmbiguous)
{
    writeSource(R"(
        concept First<T> = type_id(T) == i32;
        concept Second<T> = type_id(T) == i32;
        generic<T> where First<T>
        fn choose(value: T) -> i32 { return 1; }
        generic<T> where Second<T>
        fn choose(value: T) -> i32 { return 2; }
        fn main() -> i32 { let value: i32 = 1; return choose(value); }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("ambiguous call"), std::string::npos) << out;
}

TEST_F(MLATest, ConceptsGenericStructBounds)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Whole<T> = type_id(T) == i32;
        struct Box<T: Whole> { var value: T; };
        fn main() -> i32 {
            let box: Box<i32> = Box<i32> { value: 42 };
            return box.value == 42 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsRejectUnsatisfiedStructBound)
{
    writeSource(R"(
        concept Whole<T> = type_id(T) == i32;
        struct Box<T: Whole> { var value: T; };
        fn main() -> i32 { let box: Box<f64> = Box<f64> { value: 1.0 }; return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("does not satisfy concept 'Whole'"), std::string::npos)
        << out;
}

TEST_F(MLATest, ConceptsFalseRequiresHasDiagnostic)
{
    writeSource(R"(
        concept Whole<T> = type_id(T) == i32;
        generic<T> where Whole<T>
        fn only_whole(value: T) -> T { return value; }
        fn main() -> i32 { let result = only_whole(1.5); return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("constraints not satisfied"), std::string::npos) << out;
    EXPECT_NE(out.find("f64"), std::string::npos) << out;
}

TEST_F(MLATest, ConceptsRejectNonBooleanPredicate)
{
    writeSource(R"(
        concept Wrong<T> = 1;
        fn main() -> i32 { static_assert!(Wrong<i32>); return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("must evaluate to bool"), std::string::npos) << out;
}

TEST_F(MLATest, ConceptsDetectRecursion)
{
    writeSource(R"(
        concept Recursive<T> = Recursive<T>;
        fn main() -> i32 { static_assert!(Recursive<i32>); return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("recursive concept"), std::string::npos) << out;
}

TEST_F(MLATest, ConceptsBooleanShortCircuit)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Safe<T> = true || (1 / 0 == 0);
        concept Never<T> = false && (1 / 0 == 0);
        fn main() -> i32 {
            static_assert!(Safe<i32>);
            static_assert!(!Never<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsMultiParameterRequires)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Same<T, U> = type_id(T) == type_id(U);
        generic<T, U> where Same<T, U>
        fn match_pair(first: T, second: U) -> i32 { return 42; }
        fn main() -> i32 {
            let first: i32 = 1;
            let second: i32 = 2;
            return match_pair(first, second) == 42 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsConstexprConstrainedOverloadSelection)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Whole<T> = type_id(T) == i32;
        generic<T>
        cexpr fn category(value: T) -> i32 { return 1; }
        generic<T> where Whole<T>
        cexpr fn category(value: T) -> i32 { return 2; }
        fn main() -> i32 {
            cexpr first: i32 = category(1i32);
            cexpr second: i32 = category(1.0);
            return first == 2 && second == 1 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsPublicImportAndPrivateImplementation)
{
    {
        std::ofstream module(testDir + "/concept_module.mla");
        module << R"(
            concept Hidden<T> = type_id(T) == i32;
            pub concept Public<T> = Hidden<T>;
            generic<T> where Hidden<T>
            pub fn accepted(value: T) -> i32 { return 42; }
        )";
    }
    writeSource(R"(
        mod concept_module;
        use concept_module::*;
        fn main() -> i32 {
            static_assert!(Public<i32>);
            let value: i32 = 1;
            return accepted(value) == 42 ? 0 : 1;
        }
    )");
    ASSERT_TRUE(compile());
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, ConceptsPrivateImportCannotBeQueried)
{
    {
        std::ofstream module(testDir + "/private_concept.mla");
        module << "concept Hidden<T> = type_id(T) == i32;";
    }
    writeSource(R"(
        mod private_concept;
        use private_concept::*;
        fn main() -> i32 { static_assert!(Hidden<i32>); return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("is private"), std::string::npos) << out;
}

TEST_F(MLATest, ConceptsNamespaceLookup)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        namespace rules {
            concept Whole<T> = type_id(T) == i32;
            concept Same<T, U> = type_id(T) == type_id(U);
            generic<T> where Whole<T>
            fn accept(value: T) -> i32 { return 42; }
        }
        fn main() -> i32 {
            static_assert!(rules::Same<i32, i32>);
            let value: i32 = 1;
            return rules::accept(value) == 42 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsRejectWrongTypeArityAndUnknownTypes)
{
    for(const std::string query : {"Same<i32>", "Same<i32, MissingType>"})
    {
        SCOPED_TRACE(query);
        writeSource(
            "concept Same<T, U> = type_id(T) == type_id(U); fn main() "
            "-> i32 { static_assert!(" +
            query + "); return 0; }");
        int rc = 0;
        const std::string out = compileCapture(rc);
        EXPECT_EQ(rc, 1);
        EXPECT_NE(out.find("concept"), std::string::npos) << out;
    }
}

TEST_F(MLATest, ConceptsRejectUnknownNamesInUnusedRequirements)
{
    writeSource(R"(
        generic<T> where MissingConcept<T>
        fn unused(value: T) -> i32 { return 0; }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("unknown concept 'MissingConcept'"), std::string::npos)
        << out;
}

TEST_F(MLATest, ConceptsQualifiedModuleAndAliases)
{
    {
        std::ofstream module(testDir + "/module_concepts.mla");
        module << R"(
            concept Internal<T> = type_id(T) == i32;
            pub concept Whole<T> = Internal<T>;
        )";
    }
    for(const std::string imports :
        {"",
         "use module_concepts as rules;",
         "use module_concepts::Whole as Accepted;"})
    {
        SCOPED_TRACE(imports);
        const std::string query =
            imports.empty() ? "module_concepts::Whole<i32>"
            : imports.find(" as rules") != std::string::npos
                ? "rules::Whole<i32>"
                : "Accepted<i32>";
        writeSource(
            "mod module_concepts; " + imports +
            " fn main() -> i32 { static_assert!(" + query + "); return 0; }");
        ASSERT_TRUE(compile());
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, ConceptsRejectDuplicateConstrainedOverloads)
{
    writeSource(R"(
        concept Whole<T> = type_id(T) == i32;
        generic<T> where Whole<T>
        fn repeated(value: T) -> i32 { return 1; }
        generic<T> where Whole<T>
        fn repeated(value: T) -> i32 { return 2; }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("duplicate constrained function"), std::string::npos)
        << out;
}

TEST_F(MLATest, ConceptsVariadicFunctionChecksFixedTypeConstraints)
{
    writeSource(R"(
        concept Whole<T> = type_id(T) == i32;
        generic<T, Args...> where Whole<T>
        fn many(first: T, args: Args...) -> i32 { return 0; }
        fn main() -> i32 { return many(1.5, 2, 3); }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("constraints not satisfied"), std::string::npos) << out;
}

TEST_F(MLATest, ConceptsQualifiedStandardReferenceTypes)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        fn main() -> i32 {
            static_assert!(std::concepts::same_as<&mut &mut i32, &mut &mut i32>);
            static_assert!(!std::concepts::same_as<&i32, &mut i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, ConceptsQueriesInGenericConstexprBranches)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        use std::concepts::*;
        generic<T>
        fn evaluate(value: T) -> i32 {
            cexpr if integral<T> { return i32(value + 1); }
            else { return 7; }
        }
        struct Marker { var value: i32; };
        fn main() -> i32 {
            let number: i32 = 41;
            let marker = Marker { value: 1 };
            return evaluate(number) == 42 && evaluate(marker) == 7 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresBuiltinExpressions)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Addable<T> = req(a: T, b: T) { a + b; a == b; };
        concept CppParameters<T> = req(T a, T b) { a * b; };
        struct Marker { var value: i32; };
        fn main() -> i32 {
            static_assert!(Addable<i32>);
            static_assert!(Addable<f64>);
            static_assert!(CppParameters<i64>);
            static_assert!(!Addable<Marker>);
            static_assert!(req { 1 + 2; false; });
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresMembersAndMethods)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept HasValue<T> = req(value: T) { value.value; value.read(); };
        struct Good {
            var value: i32;
            fn read(self: &Self) -> i32 { return self.value; }
        };
        struct Missing { var other: i32; };
        fn main() -> i32 {
            static_assert!(HasValue<Good>);
            static_assert!(!HasValue<Missing>);
            static_assert!(!HasValue<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresCompoundResultConstraints)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        use std::concepts::*;
        concept AddsToSelf<T> = req(a: T, b: T) {
            { a + b } -> same_as<T>;
            { a == b } -> same_as<bool>;
        };
        concept AddsToInteger<T> = req(a: T) { { a + a } -> integral; };
        fn main() -> i32 {
            static_assert!(AddsToSelf<i32>);
            static_assert!(AddsToSelf<f64>);
            static_assert!(AddsToInteger<i64>);
            static_assert!(!AddsToInteger<f64>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresTypeAndNestedRequirements)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        use std::concepts::*;
        alias Number = i32;
        struct Box<T> { var value: T; };
        struct IntegerBox<T: integral> { var value: T; };
        concept Complete<T> = req { typename T; type Box<T>; where integral<T>; };
        concept Missing<T> = req { typename DoesNotExist; };
        fn main() -> i32 {
            static_assert!(Complete<Number>);
            static_assert!(!Complete<f64>);
            static_assert!(!Missing<i32>);
            static_assert!(req { typename void; });
            static_assert!(!req { typename IntegerBox<f64>; });
            static_assert!(!req { typename Box<i32, i64>; });
            static_assert!(!req { where false; });
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresFiltersOverloadsAndOrdersRefinement)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept HasValue<T> = req(value: T) { value.value; };
        concept HasBoth<T> = HasValue<T> && req(value: T) { value.other; };
        generic<T> fn select(value: T) -> i32 { return 0; }
        generic<T> where HasValue<T> fn select(value: T) -> i32 { return 1; }
        generic<T> where HasBoth<T> fn select(value: T) -> i32 { return 2; }
        struct One { var value: i32; };
        struct Two { var value: i32; var other: i32; };
        fn main() -> i32 {
            let a = One { value: 1 };
            let b = Two { value: 1, other: 2 };
            return select(1i32) == 0 && select(a) == 1 && select(b) == 2 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresDoesNotEvaluateOrEmitCode)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        var calls: i32 = 0;
        fn touch(value: i32) -> i32 { calls = calls + 1; return value; }
        extern fn never_linked(value: i32) -> i32;
        concept Callable<T> = req(value: T) { touch(value); never_linked(value); };
        fn main() -> i32 {
            static_assert!(Callable<i32>);
            static_assert!(req { calls + 1; });
            static_assert!(req { 1 / 0; });
            return calls;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresRejectsMissingCallsAndWrongArguments)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        struct Item { var value: i32; };
        fn consume(value: Item) -> i32 { return value.value; }
        concept Accepts<T> = req(value: T) { consume(value); };
        concept Unknown<T> = req(value: T) { no_such_function(value); };
        fn main() -> i32 {
            static_assert!(Accepts<Item>);
            static_assert!(!Accepts<i32>);
            static_assert!(!Unknown<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresDoesNotInstantiateCalledBodies)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        generic<T> fn declared(value: T) -> i32 { return value.nonexistent; }
        generic<T> cexpr fn compile_time(value: T) -> i32 { return value.nonexistent; }
        concept Declared<T> = req(value: T) { declared(value); compile_time(value); };
        fn main() -> i32 { static_assert!(Declared<i32>); return 0; }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresReferenceMutability)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept SharedIncrement<T> = req(value: &T) { &mut value; };
        concept MutableIncrement<T> = req(value: &mut T) { &mut value; };
        fn main() -> i32 {
            static_assert!(!SharedIncrement<i32>);
            static_assert!(MutableIncrement<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresNoexcept)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn callable(value: i32) -> i32 { return value; }
        concept SafeAdd<T> = req(value: T) { { value + value } noexcept; };
        concept SafeCall<T> = req(value: T) { { callable(value) } noexcept; };
        fn main() -> i32 {
            static_assert!(SafeAdd<i32>);
            static_assert!(!SafeCall<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresGlobalAndConstexprQueries)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Addable<T> = req(value: T) { value + value; };
        cexpr global_ok: bool = Addable<i32>;
        generic<T> cexpr fn category(value: T) -> i32 { return 0; }
        generic<T> where Addable<T> cexpr fn category(value: T) -> i32 { return 1; }
        struct Marker { var value: i32; };
        fn main() -> i32 {
            static_assert!(global_ok);
            static_assert!(category(1i32) == 1);
            static_assert!(!Addable<Marker>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresShortCircuitAndNestedExpressions)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept NestedParameter<T> = req(value: T) { where req { value + value; }; };
        concept Nested<T> = req {
            where req(value: T) { value + value; };
            where true || (1 / 0 == 1);
        };
        struct Marker { var value: i32; };
        fn main() -> i32 {
            static_assert!(Nested<i32>);
            static_assert!(!Nested<Marker>);
            static_assert!(NestedParameter<i32>);
            static_assert!(!NestedParameter<Marker>);
            static_assert!(true || req(value: Marker) { value.nope; });
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresQualifiedCompoundConcept)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        concept SameResult<T> = req(value: T) { { value + value } -> std::concepts::same_as<T>; };
        fn main() -> i32 { static_assert!(SameResult<u32>); return 0; }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresIndexesAndDereferences)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Indexed<T> = req(value: T) { value[0]; };
        concept Dereference<T> = req(value: T) { *value; };
        fn main() -> i32 {
            static_assert!(Indexed<list<i32>>);
            static_assert!(!Indexed<i32>);
            static_assert!(Dereference<ptr<i32>>);
            static_assert!(!Dereference<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresLexicalLocalsAndNoScopeLeak)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept NoCallerLocal<T> = req { caller_only; };
        fn main() -> i32 {
            var caller_only: i32 = 5;
            let immutable: i32 = 3;
            static_assert!(req { caller_only + 1; &mut caller_only; });
            static_assert!(!req { &mut immutable; });
            static_assert!(!NoCallerLocal<i32>);
            static_assert!(req(caller_only: f64) { caller_only + 1.0; });
            return caller_only == 5 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresInvalidParametersAndRuntimeNestedCondition)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept VoidParameter<T> = req(value: void) { where true; };
        concept Duplicate<T> = req(value: T, value: T) { where true; };
        concept Runtime<T> = req(value: T) { where value == 0; };
        fn main() -> i32 {
            static_assert!(!VoidParameter<i32>);
            static_assert!(!Duplicate<i32>);
            static_assert!(!Runtime<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresNamespaceAndImportedPrivateHelper)
{
    {
        std::ofstream module(testDir + "/structural_module.mla");
        module << R"(
            fn helper(value: i32) -> i32 { return value; }
            pub concept Accepted<T> = req(value: T) { helper(value); };
        )";
    }
    writeSource(R"(
        mod structural_module;
        use structural_module::*;
        namespace local {
            fn helper(value: i32) -> i32 { return value; }
            concept Accepted<T> = req(value: T) { helper(value); };
        }
        fn main() -> i32 {
            static_assert!(Accepted<i32>);
            static_assert!(local::Accepted<i32>);
            static_assert!(!req { helper(1i32); });
            return 0;
        }
    )");
    ASSERT_TRUE(compile());
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, StructuralRequiresGlobalStructAndFunctionContext)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        struct Item { var value: i32; };
        fn read(value: Item) -> i32 { return value.value; }
        impl Item { pub fn get(self: &Self) -> i32 { return self.value; } }
        concept Readable<T> = req(value: T) { value.value; value.get(); read(value); };
        cexpr global_ok: bool = Readable<Item>;
        fn main() -> i32 { static_assert!(global_ok); return 0; }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresDirectClauseAndGenericResult)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        use std::concepts::*;
        struct Box<T> { var value: T; };
        concept BoxValue<T> = req(value: Box<T>) { { value.value } -> same_as<T>; };
        concept Identity<T> = req(value: T) { { value } -> same_as<T>; };
        concept NestedValue<T> = req(value: &mut &mut T) { { *value } -> same_as<T>; };
        generic<T> where req(value: T) { value + value; }
        fn twice(value: T) -> T { return value + value; }
        fn main() -> i32 {
            static_assert!(BoxValue<i32>);
            static_assert!(Identity<Box<i32>>);
            static_assert!(NestedValue<i32>);
            return twice(21i32) == 42 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresNamespacedTypes)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        namespace local {
            struct Item { var value: i32; };
            concept HasItem<T> = req(value: Item) { typename Item; value.value; };
        }
        fn main() -> i32 { static_assert!(local::HasItem<i32>); return 0; }
    )"),
        0);
}

TEST_F(MLATest, StructuralRequiresDemo)
{
    const fs::path example = fs::path(__FILE__).parent_path().parent_path() /
                             "examples/structural_concepts_demo.mla";
    std::ifstream input(example);
    ASSERT_TRUE(input.is_open()) << example;
    std::ostringstream source;
    source << input.rdbuf();
    writeSource(source.str());
    for(const std::string options : {"-O0", "-O2"})
    {
        SCOPED_TRACE(options);
        ASSERT_TRUE(compile(true, options));
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, StructuralRequiresArrayAndFunctionTypes)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Indexed<T> = req(value: T) { value[0]; };
        concept Callable<T> = req(value: T) { value(1i32); };
        alias Callback = fn(i32) -> i32;
        alias WrongCallback = fn() -> i32;
        fn main() -> i32 {
            static_assert!(Indexed<array<i32, 4>>);
            static_assert!(Callable<Callback>);
            static_assert!(!Callable<WrongCallback>);
            static_assert!(!Callable<i32>);
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, WhereConstraintNamedPredicatesAndFallback)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        use std::concepts::*;
        generic<T> where arithmetic<T> && same_as<T, i32>
        fn twice(value: T) -> T { return value + value; }
        generic<T> fn category(value: T) -> i32 { return 0; }
        generic<T> where integral<T>
        fn category(value: T) -> i32 { return 1; }
        fn main() -> i32 {
            return twice(21i32) == 42 && category(1i32) == 1 && category(1.5) == 0 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, WhereConstraintInlineStructuralExpression)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        generic<T> fn read(value: T) -> i32 { return 7; }
        generic<T> where req(value: T) { value.value; }
        fn read(value: T) -> i32 { return value.value; }
        struct Item { var value: i32; };
        fn main() -> i32 {
            let item = Item { value: 42 };
            return read(item) == 42 && read(1i32) == 7 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, WhereConstraintNestedPredicatesAndStructuralChecks)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        mod std::concepts;
        use std::concepts::*;
        concept WholeAddable<T> = req(value: T) {
            where integral<T>;
            where req { value + value; };
        };
        fn main() -> i32 {
            static_assert!(WholeAddable<i32>);
            static_assert!(!WholeAddable<f64>);
            static_assert!(req { false; });
            static_assert!(!req { where false; });
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, WhereConstraintRejectsDuplicateDefinitions)
{
    writeSource(R"(
        concept Whole<T> = type_id(T) == i32;
        generic<T> where Whole<T> fn same(value: T) -> i32 { return 1; }
        generic<T> where Whole<T> fn same(value: T) -> i32 { return 2; }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("duplicate constrained function overload"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, WhereConstraintRejectsNonBooleanPredicates)
{
    writeSource(R"(
        generic<T> where 1 fn invalid(value: T) -> i32 { return 0; }
        fn main() -> i32 { return invalid(1i32); }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("constraint clause must evaluate to bool"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, WhereConstraintExamples)
{
    for(const std::string name :
        {"where_constraints_demo.mla", "req_expression_demo.mla"})
    {
        const fs::path example =
            fs::path(__FILE__).parent_path().parent_path() / "examples" / name;
        std::ifstream input(example);
        ASSERT_TRUE(input.is_open()) << example;
        std::ostringstream source;
        source << input.rdbuf();
        writeSource(source.str());
        for(const std::string options : {"-O0", "-O2"})
        {
            SCOPED_TRACE(name + " " + options);
            ASSERT_TRUE(compile(true, options));
            EXPECT_EQ(runExitCode(), 0);
        }
    }
}

TEST_F(MLATest, ReqCanonicalSyntax)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        concept Whole<T> = req(value: T) { value + value; where type_id(T) == i32; };
        generic<T> where req(value: T) { value + value; }
        fn twice(value: T) -> T { return value + value; }
        fn main() -> i32 {
            static_assert!(Whole<i32>);
            static_assert!(!Whole<f64>);
            return twice(21i32) == 42 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, RemovedRequiresSyntaxIsRejected)
{
    for(const std::string source :
        {"generic<T> requires true fn old(value: T) -> T { return value; } fn "
         "main() -> i32 { return 0; }",
         "concept Old<T> = requires(value: T) { value + value; }; fn main() -> "
         "i32 { return 0; }",
         "concept Old<T> = requires { true; }; fn main() -> i32 { return 0; }",
         "concept Old<T> = req { requires true; }; fn main() -> i32 { return "
         "0; }"})
    {
        SCOPED_TRACE(source);
        writeSource(source);
        int rc = 0;
        const std::string output = compileCapture(rc);
        EXPECT_EQ(rc, 1) << output;
        EXPECT_NE(output.find("MLANG-E1020"), std::string::npos) << output;
        EXPECT_NE(output.find("'requires' was removed"), std::string::npos)
            << output;
        EXPECT_NE(output.find("'where'"), std::string::npos) << output;
        EXPECT_NE(output.find("'req(...) { ... }'"), std::string::npos)
            << output;
    }
}

TEST_F(MLATest, ReqDoesNotIntroduceBooleanConstraintClauses)
{
    writeSource(R"(
        generic<T> req true fn invalid(value: T) -> T { return value; }
        fn main() -> i32 { return invalid(0i32); }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1) << out;
}

TEST_F(MLATest, ReqDoesNotIntroduceNestedBooleanPredicates)
{
    writeSource(R"(
        concept Invalid<T> = req { req true; };
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1) << out;
}

TEST_F(MLATest, MainReturnsExpression)
{
    std::string code = R"(
        fn main() -> i32 {
            return 10 + 32;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 42);
}

// ============================================================================
// Integer Type Tests
// ============================================================================

TEST_F(MLATest, NarrowCastRejectsInvalidConstant)
{
    writeSource(R"(
fn main() -> i32 {
    return narrow_cast<u32>(-1i32);
}
)");
    int exitCode = 0;
    std::string output = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(output.find("MLANG-E2009"), std::string::npos);
    EXPECT_NE(
        output.find("narrow_cast constant value cannot be represented as u32"),
        std::string::npos);
}

TEST_F(MLATest, NarrowCastDebugFailureReportsSourceLine)
{
    writeSource(R"(fn checked(value: i32) -> u32 {
    return narrow_cast<u32>(value);
}

fn main() -> i32 {
    return i32(checked(-1i32));
}
)");
    ASSERT_TRUE(compile(true, "-Og"));
    std::string output = runStderr();
    EXPECT_NE(
        output.find(
            "narrow_cast panic at " + sourceFile +
            ":2: value cannot be represented as u32"),
        std::string::npos);
}

TEST_F(MLATest, I8MaxValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i8 = 127;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "127\n");
}

TEST_F(MLATest, I8MinValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i8 = -128;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "-128\n");
}

TEST_F(MLATest, I16MaxValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i16 = 32767;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "32767\n");
}

TEST_F(MLATest, I32MaxValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 2147483647;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "2147483647\n");
}

TEST_F(MLATest, I64MaxValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i64 = 9223372036854775807;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "9223372036854775807\n");
}

TEST_F(MLATest, U8MaxValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: u8 = 255;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "255\n");
}

TEST_F(MLATest, U16MaxValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: u16 = 65535;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "65535\n");
}

TEST_F(MLATest, U32MaxValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: u32 = 4294967295;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "4294967295\n");
}

TEST_F(MLATest, U64LargeValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: u64 = 1000000000000;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "1000000000000\n");
}

// ============================================================================
// Floating Point Tests
// ============================================================================

TEST_F(MLATest, FloatLiteral)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: f32 = 3.14f;
            println!("{}", x);
            return 0;
        }
    )";
    std::string output = compileAndRun(code);
    EXPECT_TRUE(output.find("3.14") != std::string::npos);
}

TEST_F(MLATest, DoubleLiteral)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: f64 = 3.14159;
            println!("{}", x);
            return 0;
        }
    )";
    std::string output = compileAndRun(code);
    EXPECT_TRUE(output.find("3.14159") != std::string::npos);
}

// ============================================================================
// String Tests
// ============================================================================

TEST_F(MLATest, StringLiteral)
{
    std::string code = R"(
        fn main() -> i32 {
            let s: str8 = "Hello, World!";
            println!("{}", s);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Hello, World!\n");
}

TEST_F(MLATest, StringEscapeNewline)
{
    std::string code = R"(
        fn main() -> i32 {
            println!("Line1\nLine2");
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Line1\nLine2\n");
}

TEST_F(MLATest, StringEscapeTab)
{
    std::string code = R"(
        fn main() -> i32 {
            println!("Col1\tCol2");
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Col1\tCol2\n");
}

TEST_F(MLATest, StringEscapeHexAndStrTypes)
{
    std::string code = R"(
        fn main() -> i32 {
            let y: str8 = "\x1b[38;2;164;255;82m\x1b[48;2;98;0;0mhello\x1b[0m world";
            let z: str16 = "wide";
            println!("{}", y);
            println!("{}", z);
            return 0;
        }
    )";
    EXPECT_EQ(
        compileAndRun(code),
        "\x1b[38;2;164;255;82m\x1b[48;2;98;0;0mhello\x1b[0m world\nwide\n");
}

TEST_F(MLATest, StringConcatenationPlus)
{
    std::string code = R"(
        fn main() -> i32 {
            let a: str8 = "Hello, ";
            let b: str8 = "MLA!";
            let c = a + b;
            println!("{}", c);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Hello, MLA!\n");
}

TEST_F(MLATest, StringConcatenationRejectsMixedNumeric)
{
    std::string code = R"(
        fn main() -> i32 {
            let a: str8 = "x=";
            let b: i32 = 42;
            let c = a + b;
            println!("{}", c);
            return 0;
        }
    )";
    writeSource(code);
    int exitCode = 0;
    std::string out = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(
        out.find(
            "string concatenation requires both operands to be string types"),
        std::string::npos);
}

TEST_F(MLATest, StringConcatenationRejectsMismatchedStringKinds)
{
    std::string code = R"(
        mod std::strbuf;
        use std::strbuf::to_utf16;

        fn main() -> i32 {
            let a: str8 = "left";
            let b: str16 = to_utf16("right");
            let c = a + b;
            println!("{}", c);
            return 0;
        }
    )";
    writeSource(code);
    int exitCode = 0;
    std::string out = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(
        out.find("string concatenation requires matching operand types"),
        std::string::npos);
}

TEST_F(MLATest, ColonInsteadOfSemicolonSuggestsSemicolon)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 1:
            return x;
        }
    )";
    writeSource(code);
    int exitCode = 0;
    std::string out = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(out.find("MLANG-E1017"), std::string::npos);
    EXPECT_NE(out.find("use ';' instead of ':'"), std::string::npos);
}

// ============================================================================
// Print Macro Tests
// ============================================================================

TEST_F(MLATest, PrintlnBasic)
{
    std::string code = R"(
        fn main() -> i32 {
            println!("Hello, MLA!");
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Hello, MLA!\n");
}

TEST_F(MLATest, PrintNoNewline)
{
    std::string code = R"(
        fn main() -> i32 {
            print!("Hello");
            print!(", ");
            println!("World!");
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Hello, World!\n");
}

TEST_F(MLATest, PrintlnWithInt)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 42;
            println!("Value: {}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Value: 42\n");
}

TEST_F(MLATest, PrintlnMultipleArgs)
{
    std::string code = R"(
        fn main() -> i32 {
            let a: i32 = 10;
            let b: i32 = 20;
            println!("{} + {} = {}", a, b, a + b);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "10 + 20 = 30\n");
}

TEST_F(MLATest, EprintlnToStderr)
{
    std::string code = R"(
        fn main() -> i32 {
            eprintln!("Error message");
            return 0;
        }
    )";
    writeSource(code);
    ASSERT_TRUE(compile());
    EXPECT_EQ(runStderr(), "Error message\n");
}

// ============================================================================
// Variable Declaration Tests
// ============================================================================

TEST_F(MLATest, LetDeclaration)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 100;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "100\n");
}

TEST_F(MLATest, VarDeclarationWithInit)
{
    std::string code = R"(
        fn main() -> i32 {
            var x: i32 = 100;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "100\n");
}

TEST_F(MLATest, ListInitializerWithBracesSuggestsBrackets)
{
    std::string code = R"(
        fn main() -> i32 {
            let numbers: list<i32> = {1, 2, 3, 4, 5};
            return 0;
        }
    )";
    writeSource(code);
    int exitCode = 0;
    std::string out = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(
        out.find("list initializer for type 'list<i32>' uses braces"),
        std::string::npos);
    EXPECT_NE(
        out.find("use '[' and ']' instead of '{' and '}'"),
        std::string::npos);
}

TEST_F(MLATest, FixedArrayAllowsBraceInitializerWithinCapacity)
{
    std::string code = R"(
        fn main() -> i32 {
            let arr: array<i32, 6> = {1, 3, 4, 5, 6, 7};
            if arr.len() != 6 {
                return 1;
            }
            if arr[0] != 1 || arr[5] != 7 {
                return 2;
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarraySupportsOneOrMoreFixedDimensions)
{
    std::string code = R"(
        fn main() -> i32 {
            let line: multiarray<i32, 3> = {10, 20, 30};
            let cube: multiarray<i32, 2, 2, 2> = {
                {{1, 2}, {3, 4}},
                {{5, 6}, {7, 8}}
            };

            if line[1] != 20 {
                return 1;
            }
            if cube[0][1][0] != 3 || cube[1][1][1] != 8 {
                return 2;
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarrayRejectsOversizedNestedInitializer)
{
    std::string code = R"(
        fn main() -> i32 {
            let matrix: multiarray<i32, 2, 2> = {
                {1, 2, 3},
                {4, 5}
            };
            return matrix[0][0];
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("array initializer has 3 elements"), std::string::npos);
    EXPECT_NE(out.find("array<i32, 2> capacity is 2"), std::string::npos);
}

TEST_F(MLATest, MultiarrayMatrixElementwiseOperationsAndSum)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            let a: multiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
            let b: multiarray<i32, 2, 2> = {{5, 6}, {7, 8}};
            let added: multiarray<i32, 2, 2> = a.add(b);
            let subtracted: multiarray<i32, 2, 2> = b.subtract(a);
            let products: multiarray<i32, 2, 2> = a.hadamard(b);
            let shifted: multiarray<i32, 2, 2> = a.offset(2);
            let scaled: multiarray<i32, 2, 2> = a.scale(3);
            let cube: multiarray<i32, 2, 1, 2> = {{{1, 2}}, {{3, 4}}};
            let cube_shifted: multiarray<i32, 2, 1, 2> = cube.offset(1);
            let floats: multiarray<f64, 1, 2> = {{1.5, 2.0}};
            let float_scaled: multiarray<f64, 1, 2> = floats.scale(2.0);
            if added.sum() != 36 || subtracted.sum() != 16 {
                return 1;
            }
            if products[1][1] != 32 || shifted[0][0] != 3 {
                return 2;
            }
            if cube_shifted.sum() != 14 { return 3; }
            if float_scaled.sum() != 7.0 { return 4; }
            return scaled[1][0] == 9 ? 0 : 5;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarrayMatrixMultiplySupportsRectangularShapes)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            let a: multiarray<i32, 2, 3> = {{1, 2, 3}, {4, 5, 6}};
            let b: multiarray<i32, 3, 2> = {{7, 8}, {9, 10}, {11, 12}};
            let c: multiarray<i32, 2, 2> = a.matmul(b);
            return c[0][0] == 58 && c[0][1] == 64 &&
                   c[1][0] == 139 && c[1][1] == 154 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarrayMatrixTransposeSupportsRectangularShapes)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            let matrix: multiarray<i32, 2, 3> = {{1, 2, 3}, {4, 5, 6}};
            let transposed: multiarray<i32, 3, 2> = matrix.transpose();
            return transposed[0][0] == 1 && transposed[0][1] == 4 &&
                   transposed[1][0] == 2 && transposed[1][1] == 5 &&
                   transposed[2][0] == 3 && transposed[2][1] == 6 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarrayMatrixDeterminantAndInverse)
{
    std::string code = R"(
        mod std::matrix;

        fn near(value:f64, expected:f64) -> bool {
            let difference:f64 = value - expected;
            return difference > -0.000001 && difference < 0.000001;
        }

        fn near32(value:f32, expected:f32) -> bool {
            let difference:f32 = value - expected;
            let tolerance:f32 = 0.0001;
            return difference > -tolerance && difference < tolerance;
        }

        fn main() -> i32 {
            let matrix: multiarray<f64, 2, 2> = {{4.0, 7.0}, {2.0, 6.0}};
            if !near(matrix.determinant(), 10.0) { return 1; }
            let matrix32: multiarray<f32, 2, 2> = {{1.0, 2.0}, {3.0, 4.0}};
            let expected32:f32 = -2.0;
            if !near32(matrix32.determinant(), expected32) { return 5; }
            let inverse: multiarray<f64, 2, 2> = matrix.inverse();
            if !near(inverse[0][0], 0.6) || !near(inverse[0][1], -0.7) {
                return 2;
            }
            if !near(inverse[1][0], -0.2) || !near(inverse[1][1], 0.4) {
                return 3;
            }
            let identity: multiarray<f64, 2, 2> = matrix.matmul(inverse);
            return near(identity[0][0], 1.0) && near(identity[0][1], 0.0) &&
                   near(identity[1][0], 0.0) && near(identity[1][1], 1.0)
                       ? 0 : 4;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarraySymmetricMatrixEigenvaluesAndEigenvectors)
{
    std::string code = R"(
        mod std::matrix;

        fn near(value:f64, expected:f64) -> bool {
            let difference:f64 = value - expected;
            return difference > -0.000001 && difference < 0.000001;
        }

        fn main() -> i32 {
            let matrix: multiarray<f64, 2, 2> = {{2.0, 1.0}, {1.0, 2.0}};
            let values: multiarray<f64, 2> = matrix.eigenvalues();
            let vectors: multiarray<f64, 2, 2> = matrix.eigenvectors();
            if !near(values[0], 1.0) || !near(values[1], 3.0) { return 1; }
            if !near(2.0 * vectors[0][0] + vectors[1][0],
                     values[0] * vectors[0][0]) ||
               !near(vectors[0][0] + 2.0 * vectors[1][0],
                     values[0] * vectors[1][0]) {
                return 2;
            }
            if !near(2.0 * vectors[0][1] + vectors[1][1],
                     values[1] * vectors[0][1]) ||
               !near(vectors[0][1] + 2.0 * vectors[1][1],
                     values[1] * vectors[1][1]) {
                return 3;
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarrayAdvancedMatrixOperationsValidateInputs)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            let matrix: multiarray<f64, 2, 3> =
                {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
            let value = matrix.determinant();
            return value == 0.0 ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("requires a square matrix"), std::string::npos);
}

TEST_F(MLATest, MultiarrayInverseRejectsSingularMatrixAtRuntime)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            let matrix: multiarray<f64, 2, 2> = {{1.0, 2.0}, {2.0, 4.0}};
            let inverse = matrix.inverse();
            return inverse[0][0] == 0.0 ? 0 : 1;
        }
    )";
    writeSource(code);
    ASSERT_TRUE(compile());
    EXPECT_NE(runExitCode(), 0);
    EXPECT_NE(
        runStderr().find("requires a non-singular matrix"),
        std::string::npos);
}

TEST_F(MLATest, MultiarrayEigenOperationsRejectNonSymmetricMatrixAtRuntime)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            let matrix: multiarray<f64, 2, 2> = {{1.0, 2.0}, {3.0, 4.0}};
            let values = matrix.eigenvalues();
            return values[0] == 0.0 ? 0 : 1;
        }
    )";
    writeSource(code);
    ASSERT_TRUE(compile());
    EXPECT_NE(runExitCode(), 0);
    EXPECT_NE(
        runStderr().find("requires a real symmetric matrix"),
        std::string::npos);
}

TEST_F(MLATest, MutableMultiarrayMatrixAssignOperations)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            var a: mutmultiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
            let b: multiarray<i32, 2, 2> = {{2, 3}, {4, 5}};
            a.add_assign(b);
            a.offset_assign(1);
            a.scale_assign(2);
            return a[0][0] == 8 && a[1][1] == 20 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarrayMatrixRejectsIncompatibleMultiplyShapes)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            let a: multiarray<i32, 2, 3> = {{1, 2, 3}, {4, 5, 6}};
            let b: multiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
            let c = a.matmul(b);
            return c[0][0];
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("inner dimensions"), std::string::npos);
}

TEST_F(MLATest, MultiarrayMatrixAssignRequiresMutableVar)
{
    std::string code = R"(
        mod std::matrix;
        fn main() -> i32 {
            var values: multiarray<i32, 1, 2> = {{1, 2}};
            values.offset_assign(1);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("requires a mutmultiarray variable"), std::string::npos);
}

TEST_F(MLATest, MutableMultiarraySupportsCheckedRuntimeAssignment)
{
    std::string code = R"(
        fn main() -> i32 {
            var matrix: mutmultiarray<i32, 2, 3> = {
                {1, 2, 3},
                {4, 5, 6}
            };
            matrix[0][1] = 42;
            matrix[1][2] += 10;
            var row: i32 = 1;
            matrix[row][0] = 99;
            if matrix[0][1] != 42 || matrix[1][2] != 16 {
                return 1;
            }
            return matrix[1][0] == 99 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ImmutableMultiarrayRejectsIndexedAssignment)
{
    std::string code = R"(
        fn main() -> i32 {
            var matrix: multiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
            matrix[0][1] = 42;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("indexed assignment requires mutmultiarray"),
        std::string::npos);
}

TEST_F(MLATest, ImmutableListRejectsIndexedAssignment)
{
    std::string code = R"(
        fn main() -> i32 {
            let values: list<i32> = [1, 2];
            values[0] = 7;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot mutate indexed container"), std::string::npos);
}

TEST_F(MLATest, ImmutableMultiarrayRejectsMutatingMethods)
{
    std::string code = R"(
        fn main() -> i32 {
            var matrix: multiarray<i32, 1, 2> = {{1, 2}};
            matrix.clear();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("mutation requires mutmultiarray"), std::string::npos);
}

// A `let` binding must keep even a mutable multiarray immutable.
TEST_F(MLATest, LetMutableMultiarrayRejectsMutation)
{
    std::string code = R"(
        fn main() -> i32 {
            let matrix: mutmultiarray<i32, 1, 2> = {{1, 2}};
            matrix[0][0] = 7;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("because it is immutable; use var or &mut"),
        std::string::npos);
}

TEST_F(MLATest, MutableMultiarrayWriteChecksDynamicIndex)
{
    std::string code = R"(
        fn main() -> i32 {
            var matrix: mutmultiarray<i32, 1, 2> = {{1, 2}};
            var column: i32 = 2;
            matrix[0][column] = 7;
            return 0;
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MultiarrayRejectsConstantOutOfBoundsIndex)
{
    std::string code = R"(
        fn main() -> i32 {
            let matrix: multiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
            return matrix[2][0];
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("array index out of bounds"), std::string::npos);
}

TEST_F(MLATest, MutableMultiarrayGetSupportsOptionChecksAndUnwrap)
{
    std::string code = R"(
        mod std::multiarray;
        mod std::mutmultiarray;

        fn main() -> i32 {
            var matrix: mutmultiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
            var row: i32 = 1;
            var column: i32 = 0;
            if !matrix.get(row, column).is_some() {
                return 1;
            }
            if matrix.get(2, 0).is_none() == false {
                return 2;
            }
            return matrix.get(row, column).unwrap() == 3 ? 0 : 3;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MutableMultiarrayGetReturnsNoneWithoutPanicking)
{
    std::string code = R"(
        fn main() -> i32 {
            var matrix: mutmultiarray<i32, 1, 2> = {{1, 2}};
            var column: i32 = 2;
            return matrix.get(0, column).is_none() ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MutableMultiarrayGetUnwrapPanicsOutOfBounds)
{
    std::string code = R"(
        fn main() -> i32 {
            var matrix: mutmultiarray<i32, 1, 2> = {{1, 2}};
            var column: i32 = 2;
            return matrix.get(0, column).unwrap();
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayAllowsPartialBraceInitializer)
{
    std::string code = R"(
        fn main() -> i32 {
            let arr: array<i32, 6> = {1, 3, 4};
            if arr.len() != 3 {
                return 1;
            }
            if arr[0] != 1 || arr[2] != 4 {
                return 2;
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayAllowsListElementsFromArrayFirstLast)
{
    std::string code = R"(
        fn main() -> i32 {
            let xs: list<i32> = [1, 2];
            let ys: list<i32> = [3, 4];
            let source: array<list<i32>, 2> = {xs, ys};
            let arr: array<list<i32>, 2> = {source.first(), source.last()};
            let first_list: list<i32> = arr[0];
            let last_list: list<i32> = arr[1];
            if first_list.first() != 1 {
                return 1;
            }
            return last_list.last() == 4 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayAllowsMapElementsFromArrayFirstLast)
{
    std::string code = R"(
        fn main() -> i32 {
            let first: map<str8, i32> = {"a": 1};
            let second: map<str8, i32> = {"b": 2};
            let source: array<map<str8, i32>, 2> = {first, second};
            let arr: array<map<str8, i32>, 2> = {source.first(), source.last()};
            let first_map: map<str8, i32> = arr[0];
            let last_map: map<str8, i32> = arr[1];
            if first_map["a"] != 1 {
                return 1;
            }
            return last_map["b"] == 2 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayEmptyBraceInitializerIsMutable)
{
    std::string code = R"(
        fn main() -> i32 {
            var arr: array<i32, 6> = {};
            arr.push(10);
            arr.push(20);
            if arr.len() != 2 {
                return 1;
            }
            if arr[1] != 20 {
                return 2;
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StringListPushUsesAValidVoidSuccessSentinel)
{
    std::string code = R"(
        fn main() -> i32 {
            var headers: list<str8> = [];
            headers.push("stdio.h");
            headers.push("stdlib.h");
            return headers.len() == 2 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayFillSetsAllSlots)
{
    std::string code = R"(
        fn main() -> i32 {
            var arr: array<i32, 4> = {};
            arr.fill(7);
            if arr.len() != 4 {
                return 1;
            }
            if arr[0] != 7 || arr[3] != 7 {
                return 2;
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayExtendsFromVecWithinCapacity)
{
    std::string code = R"(
        fn main() -> i32 {
            var arr: array<i32, 6> = {1, 2};
            arr.extend(vec![3, 4, 5]);
            if arr.len() != 5 {
                return 1;
            }
            if arr[4] != 5 {
                return 2;
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayExtendsWithNestedListElements)
{
    std::string code = R"(
        fn main() -> i32 {
            let xs: list<i32> = [1, 2];
            let ys: list<i32> = [3, 4];
            let source: array<list<i32>, 2> = {xs, ys};
            var arr: array<list<i32>, 3> = {};
            arr.extend(source);
            let last_list: list<i32> = arr[1];
            return last_list.last() == 4 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayExtendsWithNestedMapElements)
{
    std::string code = R"(
        fn main() -> i32 {
            let first: map<str8, i32> = {"a": 1};
            let second: map<str8, i32> = {"b": 2};
            let source: array<map<str8, i32>, 2> = {first, second};
            var arr: array<map<str8, i32>, 3> = {};
            arr.extend(source);
            let last_map: map<str8, i32> = arr[1];
            return last_map["b"] == 2 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayRejectsProvableExtendOverflowAtCompileTime)
{
    std::string code = R"(
        fn main() -> i32 {
            var arr: array<i32, 3> = {1, 2};
            arr.extend(vec![3, 4]);
            return arr.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("extend() would exceed"), std::string::npos);
    EXPECT_NE(out.find("capacity=3"), std::string::npos);
}

TEST_F(MLATest, FixedArrayRejectsNestedMapExtendOverflowAtCompileTime)
{
    std::string code = R"(
        fn main() -> i32 {
            let first: map<str8, i32> = {"a": 1};
            let second: map<str8, i32> = {"b": 2};
            let source: array<map<str8, i32>, 2> = {first, second};
            var arr: array<map<str8, i32>, 1> = {};
            arr.extend(source);
            return arr.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("extend() would exceed"), std::string::npos);
    EXPECT_NE(out.find("capacity=1"), std::string::npos);
}

TEST_F(MLATest, FixedArrayRejectsProvablePushOverflowAtCompileTime)
{
    std::string code = R"(
        fn main() -> i32 {
            var arr: array<i32, 2> = {};
            arr.fill(9);
            arr.push(10);
            return arr.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("push() would exceed"), std::string::npos);
    EXPECT_NE(out.find("capacity=2"), std::string::npos);
}

TEST_F(MLATest, FixedArrayKeepsRuntimeGuardForUnknownLengthOverflow)
{
    std::string code = R"(
        fn values() -> list<i32> {
            return vec![3, 4];
        }

        fn main() -> i32 {
            var arr: array<i32, 3> = {1, 2};
            arr.extend(values());
            return arr.len();
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayRejectsTooManyBraceElements)
{
    std::string code = R"(
        fn main() -> i32 {
            let arr: array<i32, 3> = {1, 2, 3, 4};
            return arr.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("array initializer has 4 elements"), std::string::npos);
    EXPECT_NE(out.find("array<i32, 3> capacity is 3"), std::string::npos);
}

TEST_F(MLATest, FixedArrayRejectsTooManyListElementsFromArrayFirstLast)
{
    std::string code = R"(
        fn main() -> i32 {
            let xs: list<i32> = [1, 2];
            let ys: list<i32> = [3, 4];
            let source: array<list<i32>, 2> = {xs, ys};
            let arr: array<list<i32>, 1> = {source.first(), source.last()};
            return arr.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("array initializer has 2 elements"), std::string::npos);
    EXPECT_NE(out.find("array<list<i32>, 1> capacity is 1"), std::string::npos);
}

TEST_F(MLATest, FixedArrayRejectsTooLargeFillCount)
{
    std::string code = R"(
        fn main() -> i32 {
            let arr: array<i32, 3> = [0; 4];
            return arr.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("array initializer has 4 elements"), std::string::npos);
}

TEST_F(MLATest, StructArrayFieldRejectsTooManyListElements)
{
    std::string code = R"(
        struct Bag {
            var items: array<i32, 3>;
        };

        fn main() -> i32 {
            let bag: Bag = Bag { items: [1, 2, 3, 4] };
            return bag.items.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("array initializer for field 'items' has 4 elements"),
        std::string::npos);
    EXPECT_NE(out.find("array<i32, 3> capacity is 3"), std::string::npos);
}

TEST_F(MLATest, StructArrayFieldAllowsListInitializerWithinCapacity)
{
    std::string code = R"(
        struct Bag {
            var items: array<i32, 4>;
        };

        fn main() -> i32 {
            var bag: Bag = Bag { items: [1, 2, 3] };
            bag.items.push(4);
            return bag.items[3] == 4 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListLiteralBackedStorageCanGrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [1, 2, 3];
            values.push(4);
            return values[3] == 4 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ReturnedListLiteralUsesDeclaredElementWidthAndCanGrow)
{
    std::string code = R"(
        fn values() -> list<i32> {
            return [1, 2, 3];
        }

        fn main() -> i32 {
            var xs: list<i32> = values();
            xs.push(4);
            return xs[3] == 4 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListExtendsFromList)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [1];
            let more: list<i32> = [2, 3];
            values.extend(more);
            if values.len() != 3 {
                return 1;
            }
            return values[2] == 3 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListExtendsFromArray)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [1];
            let more: array<i32, 2> = {2, 3};
            values.extend(more);
            if values.len() != 3 {
                return 1;
            }
            return values[1] == 2 && values[2] == 3 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructListFieldExtendsFromLiteral)
{
    std::string code = R"(
        struct Bag {
            var items: list<i32>;
        };

        fn main() -> i32 {
            var bag: Bag = Bag { items: [1] };
            bag.items.extend([2, 3]);
            if bag.items.len() != 3 {
                return 1;
            }
            return bag.items[2] == 3 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListExtendsWithNestedListElements)
{
    std::string code = R"(
        fn main() -> i32 {
            let xs: list<i32> = [1, 2];
            let ys: list<i32> = [3, 4];
            let source: array<list<i32>, 2> = {xs, ys};
            var values: list<list<i32>> = [];
            values.extend(source);
            let last_list: list<i32> = values[1];
            return last_list.last() == 4 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListExtendsWithNestedMapElements)
{
    std::string code = R"(
        fn main() -> i32 {
            let first: map<str8, i32> = {"a": 1};
            let second: map<str8, i32> = {"b": 2};
            let source: array<map<str8, i32>, 2> = {first, second};
            var values: list<map<str8, i32>> = [];
            values.extend(source);
            let last_map: map<str8, i32> = values[1];
            return last_map["b"] == 2 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ArrayFillBackedStorageCanGrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [7; 3];
            values.push(9);
            return values[3] == 9 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ReturnedArrayFillUsesDeclaredElementWidthAndCanGrow)
{
    std::string code = R"(
        fn values() -> list<i32> {
            return [7; 3];
        }

        fn main() -> i32 {
            var xs: list<i32> = values();
            xs.push(9);
            return xs[3] == 9 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ArrayFillForLoopEvaluatesFillExpressionOnce)
{
    std::string code = R"(
        var calls: i32 = 0;

        fn next() -> i32 {
            calls += 1;
            return 7;
        }

        fn main() -> i32 {
            var sum: i32 = 0;
            for value in [next(); 3] {
                sum += value;
            }
            if sum != 21 {
                return 1;
            }
            return calls == 1 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListLiteralForLoopKeepsStructElementType)
{
    std::string code = R"(
        struct Item {
            var value: i32;
        };

        fn main() -> i32 {
            var sum: i32 = 0;
            let item: Item = Item { value: 10 };
            for item in [Item { value: 2 }, Item { value: 3 }] {
                sum += item.value;
            }
            if item.value != 10 {
                return 1;
            }
            return sum == 5 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ReturnedArrayLiteralRejectsTooManyElements)
{
    std::string code = R"(
        fn values() -> array<i32, 3> {
            return [1, 2, 3, 4];
        }

        fn main() -> i32 {
            let xs: array<i32, 3> = values();
            return xs.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("array initializer has 4 elements"), std::string::npos);
}

TEST_F(MLATest, StructArrayFieldRejectsTooLargeFillCount)
{
    std::string code = R"(
        struct Bag {
            var items: array<i32, 3>;
        };

        fn main() -> i32 {
            let bag: Bag = Bag { items: [0; 4] };
            return bag.items.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("array initializer for field 'items' has 4 elements"),
        std::string::npos);
}

TEST_F(MLATest, FixedArrayRejectsConstantOutOfBoundsIndexAtCompileTime)
{
    std::string code = R"(
        fn main() -> i32 {
            let arr: array<i32, 3> = {1, 2, 3};
            return arr[3];
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("array index out of bounds"), std::string::npos);
    EXPECT_NE(out.find("capacity=3"), std::string::npos);
}

TEST_F(MLATest, FixedArrayKeepsRuntimeGuardForDynamicOutOfBoundsIndex)
{
    std::string code = R"(
        fn get_index() -> i32 {
            return 3;
        }

        fn main() -> i32 {
            let arr: array<i32, 3> = {1, 2, 3};
            return arr[get_index()];
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructListFieldIndexingReadsInBounds)
{
    std::string code = R"(
        struct Bag {
            var items: list<i32>;
        };

        fn main() -> i32 {
            let bag: Bag = Bag { items: vec![10, 20, 30] };
            return bag.items[1] == 20 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructListFieldIndexingKeepsRuntimeBoundsGuard)
{
    std::string code = R"(
        struct Bag {
            var items: list<i32>;
        };

        fn get_index() -> i32 {
            return 3;
        }

        fn main() -> i32 {
            let bag: Bag = Bag { items: vec![10, 20, 30] };
            return bag.items[get_index()];
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructListFieldFirstLastReadInBounds)
{
    std::string code = R"(
        struct Bag {
            var items: list<i32>;
        };

        fn main() -> i32 {
            let bag: Bag = Bag { items: vec![10, 20, 30] };
            if bag.items.first() != 10 {
                return 1;
            }
            return bag.items.last() == 30 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructListFieldFirstKeepsRuntimeGuardForEmptyList)
{
    std::string code = R"(
        struct Bag {
            var items: list<i32>;
        };

        fn main() -> i32 {
            let bag: Bag = Bag { items: vec![] };
            return bag.items.first();
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructListFieldSearchMethodsWork)
{
    std::string code = R"(
        struct Bag {
            var items: list<i32>;
        };

        fn main() -> i32 {
            var bag: Bag = Bag { items: vec![10, 20, 30] };
            if bag.items.contains(20) == false {
                return 1;
            }
            return bag.items.index_of(30) == 2 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructListFieldOrderingMethodsWork)
{
    std::string code = R"(
        struct Bag {
            var items: list<i32>;
        };

        fn main() -> i32 {
            var bag: Bag = Bag { items: vec![3, 1, 2, 2] };
            bag.items.sort();
            if bag.items.first() != 1 || bag.items.last() != 3 {
                return 1;
            }
            bag.items.dedup();
            if bag.items.len() != 3 {
                return 2;
            }
            bag.items.reverse();
            if bag.items.first() != 3 || bag.items.last() != 1 {
                return 3;
            }
            bag.items.sort_desc();
            return bag.items.first() == 3 ? 0 : 4;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayRejectsKnownEmptyFirstAtCompileTime)
{
    std::string code = R"(
        fn main() -> i32 {
            var arr: array<i32, 3> = {};
            return arr.first();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("first() requires a non-empty array"),
        std::string::npos);
}

TEST_F(MLATest, ListFirstKeepsRuntimeGuardForEmptyList)
{
    std::string code = R"(
        fn values() -> list<i32> {
            return vec![];
        }

        fn main() -> i32 {
            let xs: list<i32> = values();
            return xs.first();
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FixedArrayRejectsKnownEmptyPopAtCompileTime)
{
    std::string code = R"(
        fn main() -> i32 {
            var arr: array<i32, 3> = {};
            return arr.pop();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("pop() requires a non-empty array"), std::string::npos);
}

TEST_F(MLATest, ListPopKeepsRuntimeGuardForEmptyList)
{
    std::string code = R"(
        fn values() -> list<i32> {
            return vec![];
        }

        fn main() -> i32 {
            let xs: list<i32> = values();
            return xs.pop();
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MapIndexReturnsValueForExistingKey)
{
    std::string code = R"(
        fn main() -> i32 {
            let scores: map<i32, i32> = {1: 95, 2: 87};
            return scores[1] == 95 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MapExtendsFromMap)
{
    std::string code = R"(
        fn main() -> i32 {
            var scores: map<str8, i32> = {"a": 1};
            let more: map<str8, i32> = {"b": 2};
            scores.extend(more);
            if scores.len() != 2 {
                return 1;
            }
            return scores["b"] == 2 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MapExtendsFromLiteral)
{
    std::string code = R"(
        fn main() -> i32 {
            var scores: map<str8, i32> = {"a": 1};
            scores.extend({"b": 2, "c": 3});
            if scores.len() != 3 {
                return 1;
            }
            return scores["c"] == 3 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructMapFieldExtendsFromMap)
{
    std::string code = R"(
        struct Bag {
            var scores: map<str8, i32>;
        };

        fn main() -> i32 {
            var bag: Bag = Bag { scores: {"a": 1} };
            let more: map<str8, i32> = {"b": 2};
            bag.scores.extend(more);
            if bag.scores.len() != 2 {
                return 1;
            }
            return bag.scores["b"] == 2 ? 0 : 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, MapExtendRejectsMismatchedKeyValueTypes)
{
    std::string code = R"(
        fn main() -> i32 {
            var scores: map<str8, i32> = {"a": 1};
            let more: map<str8, i64> = {"b": 2};
            scores.extend(more);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "map.extend() argument key/value types do not match "
            "destination map"),
        std::string::npos);
}

TEST_F(MLATest, MapIndexMissingKeyAbortsInsteadOfDefaultValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let scores: map<i32, i32> = {1: 95, 2: 87};
            return scores[3];
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructMapFieldIndexReturnsValueForExistingKey)
{
    std::string code = R"(
        struct Bag {
            var scores: map<i32, i32>;
        };

        fn main() -> i32 {
            let bag: Bag = Bag { scores: {1: 95, 2: 87} };
            return bag.scores[2] == 87 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StructMapFieldIndexMissingKeyAborts)
{
    std::string code = R"(
        struct Bag {
            var scores: map<i32, i32>;
        };

        fn main() -> i32 {
            let bag: Bag = Bag { scores: {1: 95, 2: 87} };
            return bag.scores[3];
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ReturnedMapLiteralBackedStorageSurvivesCallerLookup)
{
    std::string code = R"(
        fn scores() -> map<i32, i32> {
            return {1: 95, 2: 87};
        }

        fn main() -> i32 {
            let m: map<i32, i32> = scores();
            return m[2] == 87 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, VarReassignment)
{
    std::string code = R"(
        fn main() -> i32 {
            var x: i32 = 10;
            x = 20;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "20\n");
}

TEST_F(MLATest, LetInferenceFromIdentifier)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 41;
            let y = x;
            println!("{}", y + 1);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "42\n");
}

TEST_F(MLATest, VarInferenceFromIdentifier)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 10;
            var y = x;
            y = y + 5;
            println!("{}", y);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "15\n");
}

// Verifies field access and mutation through pointers to mutable borrows.
TEST_F(MLATest, PointerAccess)
{
    std::string code = R"(
        struct Foo { var value: i32; };
        fn main() -> i32 {
            var foo: Foo = Foo { value: 10 };
            let p: ptr<Foo> = &mut foo;
            (*p).value = 22;
            println!("{}", foo.value);
            let pi: ptr<i32> = &mut foo.value;
            *pi = 7;
            println!("{}", foo.value);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "22\n7\n");
}

TEST_F(MLATest, LetCannotReassign)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 10;
            x = 20;
            return 0;
        }
    )";
    writeSource(code);
    EXPECT_TRUE(compile(false)); // Should fail compilation
}

// ============================================================================
// Arithmetic Tests
// ============================================================================

TEST_F(MLATest, Addition)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = 5 + 3;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "8\n");
}

TEST_F(MLATest, Subtraction)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = 10 - 3;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "7\n");
}

TEST_F(MLATest, Multiplication)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = 6 * 7;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "42\n");
}

TEST_F(MLATest, Division)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = 20 / 4;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "5\n");
}

TEST_F(MLATest, Modulo)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = 20 % 6;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "2\n");
}

TEST_F(MLATest, FloatModulo)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: f32 = 5.5f % 2.0f;
            if result < 1.49f: { return 1; }
            if result > 1.51f: { return 2; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, DoubleModulo)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: f64 = 5.5 % 2.0;
            if result < 1.49: { return 1; }
            if result > 1.51: { return 2; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ComplexExpression)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = (10 + 5) * 2 - 6 / 2 + 7 % 4;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "30\n");
}

TEST_F(MLATest, OperatorPrecedence)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = 2 + 3 * 4;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "14\n");
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_F(MLATest, LessThan)
{
    std::string code = R"(
        fn main() -> i32 {
            if 5 < 10: {
                println!("yes");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "yes\n");
}

TEST_F(MLATest, GreaterThan)
{
    std::string code = R"(
        fn main() -> i32 {
            if 10 > 5: {
                println!("yes");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "yes\n");
}

TEST_F(MLATest, LessEqual)
{
    std::string code = R"(
        fn main() -> i32 {
            if 5 <= 5: {
                println!("yes");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "yes\n");
}

TEST_F(MLATest, GreaterEqual)
{
    std::string code = R"(
        fn main() -> i32 {
            if 5 >= 5: {
                println!("yes");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "yes\n");
}

TEST_F(MLATest, Equal)
{
    std::string code = R"(
        fn main() -> i32 {
            if 42 == 42: {
                println!("yes");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "yes\n");
}

TEST_F(MLATest, NotEqual)
{
    std::string code = R"(
        fn main() -> i32 {
            if 10 != 20: {
                println!("yes");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "yes\n");
}

// ============================================================================
// If Statement Tests
// ============================================================================

TEST_F(MLATest, IfTrue)
{
    std::string code = R"(
        fn main() -> i32 {
            if 1: {
                println!("true");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "true\n");
}

TEST_F(MLATest, IfFalse)
{
    std::string code = R"(
        fn main() -> i32 {
            if 0: {
                println!("true");
            }
            println!("done");
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "done\n");
}

TEST_F(MLATest, IfElse)
{
    std::string code = R"(
        fn main() -> i32 {
            if 0: {
                println!("if");
            } else: {
                println!("else");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "else\n");
}

TEST_F(MLATest, IfElseIf)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 2;
            if x == 1: {
                println!("one");
            } else if x == 2: {
                println!("two");
            } else: {
                println!("other");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "two\n");
}

TEST_F(MLATest, IfElseIfElse)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 3;
            if x == 1: {
                println!("one");
            } else if x == 2: {
                println!("two");
            } else: {
                println!("other");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "other\n");
}

TEST_F(MLATest, BranchPredictionHintsPreserveRuntimeBehavior)
{
    std::string code = R"(
        fn is_positive(value: i32) -> bool {
            return value > 0;
        }

        fn classify(value: i32) -> i32 {
            likely
                /* This declaration is expected to initialize to true. */
                let positive: bool = is_positive(value);
            unlikely
                var zero: bool = value == 0;
            likely if positive {
                return 1;
            } else unlikely if zero {
                return 0;
            } else {
                return -1;
            }
        }

        fn main() -> i32 {
            return classify(4) + classify(0) + classify(-2) == 0 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, AtomicOperationsCarryNaturalAlignment)
{
    // IR is generated before the module has a DataLayout, so an atomic access
    // that does not state its alignment gets LLVM's default (i64 at align 4).
    // LLVM then expands it to the generic __atomic_load/__atomic_store, which
    // live in libatomic on Linux and in libSystem on macOS: the same IR links
    // on one platform and fails on the other. Keep every atomic lock-free.
    writeSource(R"(
        mod std::thread;
        use std::thread::*;
        struct Counter {
            @property(atomic) var total: i64;
            @property(atomic) var flag: bool;
        };
        fn main() -> i32 {
            let cell: atomic64 = atomic_new(1);
            atomic_store_value(cell, 41);
            let loaded: i64 = atomic_load_value(cell);
            let previous: i64 = atomic_add_value(cell, 1);
            atomic_free_handle(cell);
            var counter: Counter = Counter {};
            counter.setTotal(loaded + previous);
            counter.setFlag(true);
            return counter.getTotal() == 82 && counter.getFlag() ? 0 : 1;
        }
    )");
    ASSERT_TRUE(compile(true, "-O0"));
    EXPECT_EQ(runExitCode(), 0);

    const fs::path irFile = fs::path(testDir) / "atomic_align.ll";
    ASSERT_EQ(
        system((compilerPath + " -O0 -emit-llvm -o " + irFile.string() + " " +
                sourceFile)
                   .c_str()),
        0);
    std::ifstream input(irFile);
    ASSERT_TRUE(input.is_open());
    std::string line;
    int atomics = 0;
    while(std::getline(input, line))
    {
        const bool isAtomic = line.find("load atomic ") != std::string::npos ||
                              line.find("store atomic ") != std::string::npos ||
                              line.find("atomicrmw ") != std::string::npos ||
                              line.find("cmpxchg ") != std::string::npos;
        if(!isAtomic)
            continue;
        ++atomics;
        const auto alignAt = line.rfind("align ");
        ASSERT_NE(alignAt, std::string::npos) << line;
        const int alignment = std::stoi(line.substr(alignAt + 6));
        // i64/i32 cells are the widest atomics the stdlib exposes; a bool
        // property is one byte. Anything narrower than its own access width
        // becomes a libcall.
        int width = 8;
        if(line.find(" i32") != std::string::npos)
            width = 4;
        if(line.find(" i8") != std::string::npos ||
           line.find(" i1 ") != std::string::npos)
            width = 1;
        EXPECT_GE(alignment, width) << line;
    }
    EXPECT_GT(atomics, 0);
}

TEST_F(MLATest, LoopLocalsHaveBoundedStackAtO0)
{
    writeSource(R"(
        mod std::thread;
        use std::thread::*;
        fn poll_worker(count: i64) -> i32 {
            var index: i64 = 0;
            var sum: i64 = 0;
            while index < count {
                let lost: i64 = index + 1;
                var current: i64 = lost;
                if index % 2 == 0 {
                    let nested: i64 = current;
                    current = nested;
                }
                sum = sum + current;
                index = index + 1;
            }
            return sum == count * (count + 1) / 2 ? 0 : 1;
        }
        fn main() -> i32 {
            let worker: thread = thread::spawn(poll_worker, 1000000);
            return join(worker);
        }
    )");
    ASSERT_TRUE(compile(true, "-O0"));
    // The old lowering exhausts even a large pthread stack. Avoid core files.
    int status = system(("ulimit -c 0; " + outputExe).c_str());
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);

    const fs::path irFile = fs::path(testDir) / "stack_locals.ll";
    ASSERT_EQ(
        system((compilerPath + " -O0 -emit-llvm -o " + irFile.string() + " " +
                sourceFile)
                   .c_str()),
        0);
    std::ifstream input(irFile);
    std::string line, block;
    int allocations = 0;
    while(std::getline(input, line))
    {
        if(line.find("define ") == 0)
            block.clear();
        if(!line.empty() && line[0] != ' ' &&
           line.find(':') != std::string::npos)
            block = line.substr(0, line.find(':'));
        if(line.find(" = alloca ") != std::string::npos)
        {
            ++allocations;
            EXPECT_EQ(block, "entry") << line;
        }
    }
    EXPECT_GT(allocations, 0);
}

TEST_F(MLATest, BranchPredictionHintsEmitLLVMWeights)
{
    writeSource(R"(
        fn positive(value: i32) -> i32 {
            likely if value > 0 { return 1; } else { return 0; }
        }

        fn zero(value: i32) -> i32 {
            unlikely if value == 0 { return 1; } else { return 0; }
        }

        fn predicted_values(value: i32) -> bool {
            likely let positive: bool = value > 0;
            unlikely var zero = value == 0;
            return positive || zero;
        }

        fn main() -> i32 {
            return positive(1) == 1 && zero(2) == 0 && predicted_values(1)
                ? 0 : 1;
        }
    )");

    const fs::path irFile = fs::path(testDir) / "branch_hints.ll";
    std::string cmd = compilerPath + " -O0 -emit-llvm -o " + irFile.string() +
                      " " + sourceFile + " 2>&1";
    ASSERT_EQ(system(cmd.c_str()), 0);

    std::ifstream input(irFile);
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string ir = buffer.str();
    EXPECT_NE(ir.find("branch_weights\", i32 2000, i32 1"), std::string::npos);
    EXPECT_NE(ir.find("branch_weights\", i32 1, i32 2000"), std::string::npos);
    EXPECT_NE(ir.find("@llvm.expect.i1(i1"), std::string::npos);
    EXPECT_NE(ir.find("i1 true)"), std::string::npos);
    EXPECT_NE(ir.find("i1 false)"), std::string::npos);
}

TEST_F(MLATest, BranchPredictionKeywordRejectsNonIfStatement)
{
    writeSource(R"(
        fn main() -> i32 {
            likely while true {
                return 0;
            }
            return 1;
        }
    )");
    int exitCode = 0;
    std::string out = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(out.find("MLANG-E1018"), std::string::npos);
    EXPECT_NE(
        out.find("must appear immediately before an if, let, or var statement"),
        std::string::npos);
}

TEST_F(MLATest, BranchPredictionKeywordRejectsExpressionUse)
{
    writeSource(R"(
        fn main() -> i32 {
            let value: bool = unlikely;
            return value ? 0 : 1;
        }
    )");
    int exitCode = 0;
    std::string out = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(out.find("MLANG-E1018"), std::string::npos);
}

TEST_F(MLATest, BranchPredictionDeclarationRequiresBooleanInitializer)
{
    writeSource(R"(
        fn main() -> i32 {
            likely let value: i32 = 42;
            return value;
        }
    )");
    int exitCode = 0;
    std::string out = compileCapture(exitCode);
    EXPECT_NE(exitCode, 0);
    EXPECT_NE(out.find("MLANG-E2002"), std::string::npos);
    EXPECT_NE(out.find("requires a bool declaration"), std::string::npos);
}

TEST_F(MLATest, NestedIf)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 5;
            let y: i32 = 10;
            if x > 0: {
                if y > 5: {
                    println!("both");
                }
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "both\n");
}

// ============================================================================
// For Loop Tests
// ============================================================================

TEST_F(MLATest, ForLoopBasic)
{
    std::string code = R"(
        fn main() -> i32 {
            for i in 0..3 {
                println!("{}", i);
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "0\n1\n2\n");
}

TEST_F(MLATest, ForLoopSum)
{
    std::string code = R"(
        fn main() -> i32 {
            var sum: i32 = 0;
            for i in 1..6 {
                sum = sum + i;
            }
            println!("{}", sum);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "15\n");
}

TEST_F(MLATest, ForLoopNested)
{
    std::string code = R"(
        fn main() -> i32 {
            for i in 0..2 {
                for j in 0..2 {
                    println!("{} {}", i, j);
                }
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "0 0\n0 1\n1 0\n1 1\n");
}

// ============================================================================
// Function Tests
// ============================================================================

TEST_F(MLATest, FunctionNoParams)
{
    std::string code = R"(
        fn get_value() -> i32 {
            return 42;
        }

        fn main() -> i32 {
            let x: i32 = get_value();
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "42\n");
}

TEST_F(MLATest, FunctionWithParams)
{
    std::string code = R"(
        fn add(a: i32, b: i32) -> i32 {
            return a + b;
        }

        fn main() -> i32 {
            let result: i32 = add(10, 20);
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "30\n");
}

TEST_F(MLATest, FunctionVoid)
{
    std::string code = R"(
        fn say_hello() -> void {
            println!("Hello!");
            return;
        }

        fn main() -> i32 {
            say_hello();
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Hello!\n");
}

TEST_F(MLATest, FunctionRecursive)
{
    std::string code = R"(
        fn factorial(n: i32) -> i32 {
            if n <= 1: {
                return 1;
            }
            return n * factorial(n - 1);
        }

        fn main() -> i32 {
            let result: i32 = factorial(5);
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "120\n");
}

TEST_F(MLATest, FunctionFibonacci)
{
    std::string code = R"(
        fn fib(n: i32) -> i32 {
            if n <= 1: {
                return n;
            }
            return fib(n - 1) + fib(n - 2);
        }

        fn main() -> i32 {
            println!("{}", fib(10));
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "55\n");
}

TEST_F(MLATest, MultipleFunctions)
{
    std::string code = R"(
        fn double_val(x: i32) -> i32 {
            return x * 2;
        }

        fn triple_val(x: i32) -> i32 {
            return x * 3;
        }

        fn main() -> i32 {
            let a: i32 = double_val(5);
            let b: i32 = triple_val(5);
            println!("{} {}", a, b);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "10 15\n");
}

TEST_F(MLATest, TraitImplWithRefSelf)
{
    std::string code = R"(
        trait Summary {
            fn summarize(&self) -> str8;
        }

        struct SocialPost {
            var username: str8;
            var content: str8;
            var reply: bool;
            var repost: bool;
        };

        impl Summary for SocialPost {
            fn summarize(&self) -> str8 {
                return format!("{}: {}", self.username, self.content);
            }
        }

        fn main() -> i32 {
            let p: SocialPost = SocialPost {
                username: "alice",
                content: "hello",
                reply: false,
                repost: false
            };
            println!("{}", p.summarize());
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "alice: hello\n");
}

TEST_F(MLATest, TraitObjectAllowsAliasedImmutableReferenceArguments)
{
    std::string code = R"(
        trait Sum {
            fn sum(&self, left: &i32, right: &i32) -> i32;
        }
        struct Counter { var value: i32; };
        impl Sum for Counter {
            fn sum(&self, left: &i32, right: &i32) -> i32 {
                return left + right;
            }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 0 };
            let item: dyn Sum = counter;
            let value: i32 = 6;
            return item.sum(&value, &value);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, TraitObjectFieldAllowsAliasedImmutableReferenceArguments)
{
    std::string code = R"(
        trait Sum {
            fn sum(&self, left: &i32, right: &i32) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder { var item: dyn Sum; };
        impl Sum for Counter {
            fn sum(&self, left: &i32, right: &i32) -> i32 {
                return left + right;
            }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 0 };
            let holder: Holder = Holder { item: counter };
            let value: i32 = 6;
            return holder.item.sum(&value, &value);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, TraitObjectRejectsAliasedSharedAndMutableReferenceArguments)
{
    std::string code = R"(
        trait Update {
            fn update(&self, read: &i32, write: &mut i32) -> void;
        }
        struct Counter { var value: i32; };
        impl Update for Counter {
            fn update(&self, read: &i32, write: &mut i32) -> void {
                write = read;
            }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 0 };
            let item: dyn Update = counter;
            var value: i32 = 6;
            item.update(&value, &mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("overlapping parts"), std::string::npos);
}

TEST_F(MLATest, TraitObjectRejectsMutatingMethodOnImmutableReceiver)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> i32;
        }
        struct Counter { var value: i32; };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> i32 {
                self.value += 1;
                return self.value;
            }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 1 };
            let item: dyn CounterOps = counter;
            return item.bump();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot call mutating method on immutable value"),
        std::string::npos);
}

TEST_F(MLATest, TraitObjectFieldRejectsMutatingMethodOnImmutableReceiver)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder { var item: dyn CounterOps; };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> i32 {
                self.value += 1;
                return self.value;
            }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 1 };
            let holder: Holder = Holder { item: counter };
            return holder.item.bump();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot call mutating method on immutable value"),
        std::string::npos);
}

TEST_F(MLATest, TraitObjectSharedMethodWorksOnImmutableReceiver)
{
    std::string code = R"(
        trait CounterOps {
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        impl CounterOps for Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 9 };
            let item: dyn CounterOps = counter;
            return item.get();
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, TraitObjectVoidMutatingMethodRuns)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> void;
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> void { self.value += 1; }
            fn get(&self) -> i32 { return self.value; }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var item: dyn CounterOps = counter;
            item.bump();
            return item.get();
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, TraitObjectMutatingMethodRejectsLiveSharedBorrow)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> void;
        }
        struct Counter { var value: i32; };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> void { self.value += 1; }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var item: dyn CounterOps = counter;
            let held = &item;
            item.bump();
            return held == held ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
}

TEST_F(MLATest, TraitObjectSharedMethodRejectsLiveMutableBorrow)
{
    std::string code = R"(
        trait CounterOps {
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        impl CounterOps for Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var item: dyn CounterOps = counter;
            let held = &mut item;
            let current = item.get();
            return held == held ? current : 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot call method on 'item' while borrowed"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, TraitObjectCanMutateAfterSharedBorrowScopeEnds)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> void;
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> void { self.value += 1; }
            fn get(&self) -> i32 { return self.value; }
        }
        fn observe(item: ptr<dyn CounterOps>) -> void { return; }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var item: dyn CounterOps = counter;
            {
                let held = &item;
                let current = item.get();
                observe(held);
            }
            item.bump();
            return item.get();
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, TraitObjectFieldMutationRejectsLiveSharedBorrow)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> void;
        }
        struct Counter { var value: i32; };
        struct Holder { var item: dyn CounterOps; };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> void { self.value += 1; }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var holder: Holder = Holder { item: counter };
            let held = &holder.item;
            holder.item.bump();
            return held == held ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, TraitObjectFieldSharedMethodAllowsSharedBorrows)
{
    std::string code = R"(
        trait CounterOps {
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder { var item: dyn CounterOps; };
        impl CounterOps for Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn observe(item: ptr<dyn CounterOps>) -> void { return; }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 9 };
            var holder: Holder = Holder { item: counter };
            let first = &holder.item;
            let second = &holder.item;
            let value = holder.item.get();
            observe(first);
            observe(second);
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, TraitObjectFieldSharedMethodRejectsLiveMutableBorrow)
{
    std::string code = R"(
        trait CounterOps {
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder { var item: dyn CounterOps; };
        impl CounterOps for Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 9 };
            var holder: Holder = Holder { item: counter };
            let held = &mut holder.item;
            let value = holder.item.get();
            return held == held ? value : 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_TRUE(
        out.find("while borrowed") != std::string::npos ||
        out.find("use of moved value") != std::string::npos ||
        out.find("cannot borrow moved value") != std::string::npos)
        << out;
}

TEST_F(MLATest, TraitObjectFieldSharedMethodAllowsDisjointMutableBorrow)
{
    std::string code = R"(
        trait CounterOps {
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder {
            var item: dyn CounterOps;
            var count: i32;
        };
        impl CounterOps for Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn observe_count(count: ptr<i32>) -> void { return; }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 9 };
            var holder: Holder = Holder { item: counter, count: 2 };
            let held_count = &mut holder.count;
            let value = holder.item.get();
            observe_count(held_count);
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, TraitObjectFieldMutatingMethodAllowsDisjointSharedBorrow)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> void;
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder {
            var item: dyn CounterOps;
            var count: i32;
        };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> void { self.value += 1; }
            fn get(&self) -> i32 { return self.value; }
        }
        fn observe_count(count: ptr<i32>) -> void { return; }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var holder: Holder = Holder { item: counter, count: 2 };
            let held_count = &holder.count;
            holder.item.bump();
            observe_count(held_count);
            return holder.item.get();
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, StructFieldMethodAllowsDisjointMutableBorrow)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        struct Holder {
            var counter: Counter;
            var count: i32;
        };
        impl Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn observe_count(count: ptr<i32>) -> void { return; }
        fn main() -> i32 {
            var holder: Holder = Holder {
                counter: Counter { value: 9 },
                count: 2
            };
            let held_count = &mut holder.count;
            let value = holder.counter.get();
            observe_count(held_count);
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, FieldBorrowPlaceRestoresAfterShadowedPointerScope)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> void;
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder {
            var item: dyn CounterOps;
            var count: i32;
        };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> void { self.value += 1; }
            fn get(&self) -> i32 { return self.value; }
        }
        fn observe_count(count: ptr<i32>) -> void { return; }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var holder: Holder = Holder { item: counter, count: 2 };
            let held = &mut holder.count;
            {
                let held = &holder.item;
                let value = holder.item.get();
                if value != 8 { return 1; }
            }
            holder.item.bump();
            observe_count(held);
            return holder.item.get();
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, FieldAssignmentAllowsDisjointLiveBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn observe(value: ptr<i32>) -> void { return; }
        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let held = &mut pair.left;
            pair.right = 7;
            observe(held);
            return pair.right;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, FieldAssignmentRejectsOverlappingLiveBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn observe(value: ptr<i32>) -> void { return; }
        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let held = &pair.left;
            pair.left = 7;
            observe(held);
            return pair.left;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'pair' while borrowed by 'held'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCanHoldMutableBorrowsToDisjointFields)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn sum(a: ptr<i32>, b: ptr<i32>) -> i32 {
            unsafe { return *a + *b; }
        }
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 5 };
            var left = &mut pair.left;
            let right = &mut pair.right;
            left = &mut pair.left;
            return sum(left, right);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, OwnershipRejectsOverlappingMutableFieldBorrows)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn sum(a: ptr<i32>, b: ptr<i32>) -> i32 { return 0; }
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 5 };
            let first = &mut pair.left;
            let second = &mut pair.left;
            return sum(first, second);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'pair' as mutable more than once"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCanHoldMutableBorrowsToDisjointListElements)
{
    std::string code = R"(
        fn sum(a: ptr<i32>, b: ptr<i32>) -> i32 {
            unsafe { return *a + *b; }
        }
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &mut values[0];
            let second = &mut values[1];
            return sum(first, second);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, OwnershipRejectsOverlappingMutableListElementBorrows)
{
    std::string code = R"(
        fn sum(a: ptr<i32>, b: ptr<i32>) -> i32 { return 0; }
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &mut values[0];
            let second = &mut values[0];
            return sum(first, second);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'values' as mutable more than once"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, DynamicIndexBorrowOverlapsEveryListElement)
{
    std::string code = R"(
        fn sum(a: ptr<i32>, b: ptr<i32>) -> i32 { return 0; }
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let index: i64 = 1;
            let first = &mut values[index];
            let second = &mut values[0];
            return sum(first, second);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'values' as mutable more than once"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, IndexedAssignmentAllowsDisjointLiveElementBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &mut values[0];
            values[1] = 8;
            unsafe { return *first + values[1]; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, IndexedAssignmentRejectsOverlappingLiveElementBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &mut values[0];
            values[0] = 8;
            unsafe { return *first; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate indexed element of 'values'"),
        std::string::npos)
        << out;
}

// A joined mutable element borrow must protect each possible list owner.
TEST_F(MLATest, JoinedElementBorrowProtectsSecondListOwner)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: list<i32> = [4, 5];
            var second: list<i32> = [6, 7];
            var pointer: ptr<i32> = &mut first[0];
            let flag: bool = true;
            if flag {
                pointer = &mut first[0];
            } else {
                pointer = &mut second[0];
            }
            second[0] = 8;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate indexed element of 'second'"),
        std::string::npos)
        << out;
}

// A joined borrow of the same field on either owner leaves sibling fields free.
TEST_F(MLATest, JoinedFieldBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        var outside: i32 = 0;
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &mut outside;
            let flag: bool = true;
            if flag {
                pointer = &mut first.left;
            } else {
                pointer = &mut second.left;
            }
            second.right = 8;
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A ternary mutable borrow must preserve each owner's precise field path.
TEST_F(MLATest, TernaryJoinedFieldBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let flag: bool = true;
            let pointer: ptr<i32> = flag ? &mut first.left : &mut second.left;
            second.right = 8;
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A ternary mutable borrow must still reject writes to its borrowed field.
TEST_F(MLATest, TernaryJoinedFieldBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let flag: bool = true;
            let pointer: ptr<i32> = flag ? &mut first.left : &mut second.left;
            second.left = 8;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, StructListBorrowAllowsDisjointFieldsAndElements)
{
    std::string code = R"(
        struct Cell { var left: i32; var right: i32; };
        fn main() -> i32 {
            var cells: list<Cell> = [
                Cell { left: 4, right: 5 },
                Cell { left: 6, right: 7 }
            ];
            let held = &mut cells[0].left;
            cells[0].right = 8;
            cells[1].left = 9;
            unsafe { return *held + cells[0].right + cells[1].left; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 21);
}

TEST_F(MLATest, StructListBorrowRejectsAssignmentToBorrowedField)
{
    std::string code = R"(
        struct Cell { var left: i32; var right: i32; };
        fn main() -> i32 {
            var cells: list<Cell> = [Cell { left: 4, right: 5 }];
            let held = &mut cells[0].left;
            cells[0].left = 8;
            unsafe { return *held; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to field of 'cells'"), std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCanHoldMutableBorrowsToDisjointTupleElements)
{
    std::string code = R"(
        fn sum(a: ptr<i32>, b: ptr<i32>) -> i32 {
            unsafe { return *a + *b; }
        }
        fn main() -> i32 {
            var pair: tuple<i32, i32> = (4, 5);
            let first = &mut pair.0;
            let second = &mut pair.1;
            return sum(first, second);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, OwnershipRejectsOverlappingMutableTupleElementBorrows)
{
    std::string code = R"(
        fn sum(a: ptr<i32>, b: ptr<i32>) -> i32 { return 0; }
        fn main() -> i32 {
            var pair: tuple<i32, i32> = (4, 5);
            let first = &mut pair.0;
            let second = &mut pair.0;
            return sum(first, second);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'pair' as mutable more than once"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, TupleAssignmentAllowsDisjointLiveElementBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var pair: tuple<i32, i32> = (4, 5);
            let first = &mut pair.0;
            pair.1 = 8;
            unsafe { return *first + pair.1; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, TupleAssignmentRejectsOverlappingLiveElementBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var pair: tuple<i32, i32> = (4, 5);
            let first = &mut pair.0;
            pair.0 = 8;
            unsafe { return *first; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to tuple element of 'pair'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListGrowthRejectsLiveElementBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &mut values[0];
            values.push(6);
            unsafe { return *first; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate container 'values' while borrowed by 'first'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListGrowthRejectsLiveSharedElementBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &values[0];
            values.push(6);
            return *first;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate container 'values' while borrowed by 'first'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ReadFromDisjointListElementAllowedDuringMutableBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &mut values[0];
            unsafe { return *first + values[1]; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, WriteToDisjointListElementWithConstantExpressionBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5, 6];
            let selected = &mut values[1 + 1];
            values[0] = 7;
            unsafe { return *selected + values[0]; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 13);
}

TEST_F(MLATest, WriteToDisjointListElementWithCexprIndexBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5, 6];
            cexpr selected_index: i32 = 1 + 1;
            let selected = &mut values[selected_index];
            values[0] = 7;
            unsafe { return *selected + values[0]; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 13);
}

TEST_F(MLATest, ReadFromBorrowedListElementRejected)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [4, 5];
            let first = &mut values[0];
            unsafe { return *first + values[0]; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot read overlapping part of 'values'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, WholeValueReadRejectedWhileMutablyBorrowed)
{
    std::string code = R"(
        fn main() -> i32 {
            var scalar: i32 = 4;
            let held = &mut scalar;
            let snapshot = scalar;
            unsafe { return snapshot + *held; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot read overlapping part of 'scalar'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ReadFromDisjointStructFieldAllowedDuringMutableBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 5 };
            let left = &mut pair.left;
            unsafe { return *left + pair.right; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, ReadFromBorrowedStructFieldRejected)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 5 };
            let left = &mut pair.left;
            unsafe { return *left + pair.left; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot read overlapping part of 'pair'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, MutableStructReferenceCanReadThroughItsOwnBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 5 };
            let borrowed: ptr<Pair> = &mut pair;
            unsafe { return (*borrowed).left; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

TEST_F(MLATest, DisjointPointerReadsRemainAllowedDuringMutableBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 5 };
            let left = &mut pair.left;
            let right = &pair.right;
            unsafe { return *left + *right; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, DisjointMutablePointerWritesRemainAllowed)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 5 };
            let left = &mut pair.left;
            let right = &mut pair.right;
            unsafe {
                *left = 7;
                *right = 8;
                return *left + *right;
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 15);
}

TEST_F(MLATest, SecondaryMutableFieldBorrowBlocksMethodOnThatField)
{
    std::string code = R"(
        trait CounterOps { fn get(&self) -> i32; }
        struct Counter { var value: i32; };
        struct Holder {
            var item: dyn CounterOps;
            var count: i32;
        };
        impl CounterOps for Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn observe_count(value: ptr<i32>) -> void { return; }
        fn observe_item(value: ptr<dyn CounterOps>) -> void { return; }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 9 };
            var holder: Holder = Holder { item: counter, count: 2 };
            let held_count = &mut holder.count;
            let held_item = &mut holder.item;
            let value = holder.item.get();
            observe_count(held_count);
            observe_item(held_item);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot call method on 'holder' while borrowed by 'held_item'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, TraitObjectFieldVoidMutatingMethodRuns)
{
    std::string code = R"(
        trait CounterOps {
            fn bump(self: &mut Self) -> void;
            fn get(&self) -> i32;
        }
        struct Counter { var value: i32; };
        struct Holder { var item: dyn CounterOps; };
        impl CounterOps for Counter {
            fn bump(self: &mut Counter) -> void { self.value += 1; }
            fn get(&self) -> i32 { return self.value; }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 8 };
            var holder: Holder = Holder { item: counter };
            holder.item.bump();
            return holder.item.get();
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

// ============================================================================
// Type Cast Tests
// ============================================================================

TEST_F(MLATest, CastIntToFloat)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 42;
            let y: f64 = f64(x);
            println!("{}", y);
            return 0;
        }
    )";
    std::string output = compileAndRun(code);
    EXPECT_TRUE(output.find("42") != std::string::npos);
}

TEST_F(MLATest, CastFloatToInt)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: f64 = 3.7;
            let y: i32 = i32(x);
            println!("{}", y);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "3\n");
}

TEST_F(MLATest, F32F64TypeKeywords)
{
    std::string code = R"(
        fn main() -> i32 {
            let a: f32 = 1.5f;
            let b: f64 = 2.25;
            println!("{} {}", a, b);
            return 0;
        }
    )";
    std::string output = compileAndRun(code);
    EXPECT_TRUE(output.find("1.500000") != std::string::npos);
    EXPECT_TRUE(output.find("2.250000") != std::string::npos);
}

TEST_F(MLATest, F32F64Casts)
{
    std::string code = R"(
        fn main() -> i32 {
            let i: i32 = 7;
            let a: f32 = f32(i);
            let b: f64 = f64(i);
            println!("{} {}", a, b);
            return 0;
        }
    )";
    std::string output = compileAndRun(code);
    EXPECT_TRUE(output.find("7.000000 7.000000") != std::string::npos);
}

TEST_F(MLATest, IntegerWidthCasts)
{
    std::string code = R"(
        fn main() -> i32 {
            let wide: i64 = i64(42);
            let unsigned: u32 = u32(wide);
            let small: i8 = i8(unsigned);
            let tiny_unsigned: u8 = u8(small);
            let restored: i32 = i32(tiny_unsigned);
            return restored - 42;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, LegacyBuiltinTypeAliasesAreRejected)
{
    for(const char* typeName :
        {"int", "float", "double", "char", "string", "utf8", "utf16"})
    {
        std::string code = std::string(R"(
            fn main() -> i32 {
                let value: )") +
                           typeName + R"( = 0;
                return 0;
            }
        )";
        writeSource(code);
        int rc = 0;
        std::string out = compileCapture(rc);
        EXPECT_NE(rc, 0) << typeName;
        EXPECT_NE(
            out.find(std::string("unknown struct type: ") + typeName),
            std::string::npos)
            << out;
    }
}

// ============================================================================
// Comment Tests
// ============================================================================

TEST_F(MLATest, SingleLineComment)
{
    std::string code = R"(
        fn main() -> i32 {
            // This is a comment
            println!("Hello");
            return 0; // Another comment
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Hello\n");
}

TEST_F(MLATest, MultiLineComment)
{
    std::string code = R"(
        fn main() -> i32 {
            /* This is a
               multi-line comment */
            println!("Hello");
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Hello\n");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(MLATest, NegativeNumbers)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 0 - 42;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "-42\n");
}

TEST_F(MLATest, ZeroValue)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 0;
            println!("{}", x);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "0\n");
}

TEST_F(MLATest, TypedVarStructWithoutInitializerIsZeroInitialized)
{
    std::string code = R"(
        struct PairStamp {
            var left: i64;
            var right: i64;
        };

        fn main() -> i32 {
            var stamp: PairStamp;
            return (stamp.left == 0 && stamp.right == 0) ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(
        out.find("implicit zero-initialization for typed var 'stamp'"),
        std::string::npos);
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, TypedVarScalarBraceZeroInit)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i64 {};
            return value == 0 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, TypedVarDerivedStructBraceZeroInit)
{
    std::string code = R"(
        struct BaseStamp {
            var left: i64;
        };

        struct PairStamp : BaseStamp {
            var right: i64;
        };

        fn main() -> i32 {
            var stamp: PairStamp {};
            return (stamp.left == 0 && stamp.right == 0) ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out.find("implicit zero-initialization"), std::string::npos);
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, TypedVarGenericStructBraceZeroInit)
{
    std::string code = R"(
        struct Pair<T, U> {
            var left: T;
            var right: U;
        };

        fn main() -> i32 {
            var pair: Pair<i64, i64> {};
            return (pair.left == 0 && pair.right == 0) ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out.find("implicit zero-initialization"), std::string::npos);
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, GenericStructGetMethodIsNotTreatedAsMultiarrayGet)
{
    std::string code = R"(
        struct Box<T> {
            var value: T;
        };

        impl<T> Box {
            pub fn get(self: Box<T>) -> T {
                return self.value;
            }
        }

        fn main() -> i32 {
            let boxed: Box<i32> = Box<i32> { value: 42 };
            return boxed.get() == 42 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, TypedVarScalarImplicitZeroInitWarns)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i64;
            return value == 0 ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(
        out.find("implicit zero-initialization for typed var 'value'"),
        std::string::npos);
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, TypedVarScalarBraceZeroInitDoesNotWarn)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i64 {};
            return value == 0 ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out.find("implicit zero-initialization"), std::string::npos);
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, DeriveJsonRoundTripsDerivedStructAndPropertyMetadata)
{
    std::string code = R"(
        #[derive(Json)]
        struct Base {
            @property(hidden) var secret: i32;
            var x: i32;
        };

        #[derive(Json)]
        struct Leaf : Base {
            var name: str8;
        };

        fn main() -> i32 {
            var leaf: Leaf {};
            leaf.setSecret(7);
            leaf.x = 3;
            leaf.name = String::from("ok");

            let text: str8 = leaf.to_json();
            let decoded_r: result<Leaf, str8> = Leaf::from_json(text);
            if decoded_r.is_err() {
                return 11;
            }

            let decoded: Leaf = decoded_r.unwrap();
            if decoded.getSecret() != 7 {
                return 12;
            }
            if decoded.x != 3 {
                return 13;
            }
            if decoded.name != "ok" {
                return 14;
            }

            print!(text);
            return 0;
        }
    )";
    writeSource(code);
    EXPECT_TRUE(compile());
    std::string out = run();
    EXPECT_EQ(runExitCode(), 0);
    EXPECT_NE(out.find("\"type\": \"Leaf\""), std::string::npos);
    EXPECT_NE(out.find("\"secret\": 7"), std::string::npos);
    EXPECT_NE(out.find("\"@property\""), std::string::npos);
    EXPECT_NE(out.find("\"hidden\": true"), std::string::npos);
}

TEST_F(MLATest, DeriveJsonMissingFieldReturnsErr)
{
    std::string code = R"(
        #[derive(Json)]
        struct Packet {
            var id: i32;
            var name: str8;
        };

        fn main() -> i32 {
            let parsed: result<Packet, str8> =
                Packet::from_json("{\"type\":\"Packet\",\"id\":5}");
            return parsed.is_err() ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, LargeExpression)
{
    std::string code = R"(
        fn main() -> i32 {
            let result: i32 = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10;
            println!("{}", result);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "55\n");
}

TEST_F(MLATest, TernaryReturn)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 5;
            return x > 3 ? 7 : 9;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, TernaryTypeMismatch)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 1;
            let y: i32 = x > 0 ? 1 : "no";
            return y;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("ternary branches must return the same type"),
        std::string::npos);
}

TEST_F(MLATest, ListOfTuplesUsesDeclaredFieldTypes)
{
    std::string code = R"(
        generic<K, V>
        fn second_value(values: &list<tuple<K, V>>) -> V {
            let entry: tuple<K, V> = values[1];
            return entry.1;
        }

        fn main() -> i32 {
            let pairs: list<tuple<i32, i32>> = [(1, 10), (2, 20)];
            let first: tuple<i32, i32> = pairs[0];
            if first.0 != 1 || first.1 != 10 { return 1; }
            if second_value(pairs) != 20 { return 2; }

            let mixed: list<tuple<i32, f64>> = [(3, 2.5), (4, 7.5)];
            let last: tuple<i32, f64> = mixed[1];
            if last.0 != 4 || last.1 != 7.5 { return 3; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Generic range conversion must materialize maps from tuple and projected
// pairs.
TEST_F(MLATest, RangesMaterializeBuiltinMapsFromGenericPairs)
{
    std::string code = R"(
        mod std::algorithm::ranges;
        use std::algorithm::ranges::to_map;
        use std::algorithm::ranges::to_map_projected;

        struct RangeMapEntry {
            var key: i32;
            var value: i32;
        };

        fn main() -> i32 {
            let pairs: list<tuple<i32, i32>> =
                [(3, 30), (1, 10), (3, 33), (2, 20)];
            let converted: map<i32, i32> = to_map(pairs);
            if converted.len() != 3 || converted[1] != 10 ||
               converted[2] != 20 || converted[3] != 33 { return 1; }

            let records: list<RangeMapEntry> = [
                RangeMapEntry { key: 3, value: 30 },
                RangeMapEntry { key: 1, value: 10 },
                RangeMapEntry { key: 3, value: 33 },
                RangeMapEntry { key: 2, value: 20 }
            ];
            let project = |item: RangeMapEntry| {
                return (item.key, item.value);
            };
            let projected: map<i32, i32> =
                to_map_projected(records, project);
            if projected.len() != 3 || projected[1] != 10 ||
               projected[2] != 20 || projected[3] != 33 { return 2; }

            let empty: list<tuple<i32, i32>> = [];
            let empty_map: map<i32, i32> = to_map(empty);
            if empty_map.len() != 0 { return 3; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, TernaryListLiteralsUseDeclaredElementType)
{
    std::string code = R"(
        fn second(values: list<i32>) -> i32 {
            return values[1];
        }

        fn choose(cond: bool) -> list<i32> {
            return cond ? [82, 65] : ([63, 64]);
        }

        fn main() -> i32 {
            let choose_letters: bool = true;
            let letters: list<i32> =
                choose_letters ? [82, 65] : ([63, 64]);
            if letters[0] != 82 { return 1; }
            if letters[1] != 65 { return 2; }

            let choose_fallback: bool = false;
            let fallback: list<i32> =
                choose_fallback ? [82, 65] : ([63, 64]);
            if fallback[0] != 63 { return 3; }
            if fallback[1] != 64 { return 4; }

            var assigned: list<i32> = [0, 0];
            assigned = choose_fallback ? [82, 65] : ([63, 64]);
            if assigned[1] != 64 { return 5; }

            if second(choose_letters ? [82, 65] : ([63, 64])) != 65 {
                return 6;
            }

            let returned: list<i32> = choose(false);
            if returned[1] != 64 { return 7; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, TernaryPrecedence)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 1 + 2 > 2 ? 4 : 5;
            return x;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

TEST_F(MLATest, TernaryArithmeticInConditionAndArm)
{
    std::string code = R"(
        fn chunk_end(start: i64, chunk_size: i64, n: i64) -> i64 {
            let end: i64 = start + chunk_size < n
                ? start + chunk_size
                : n;
            return end;
        }

        fn main() -> i32 {
            if chunk_end(0, 3, 8) != 3 { return 1; }
            if chunk_end(7, 3, 8) != 8 { return 2; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, TernaryWithFunctionCallCondition)
{
    std::string code = R"(
        mod std::strbuf;
        use std::strbuf::eq;
        fn main() -> i32 {
            let s: str8 = "ok";
            return eq(s, "ok") == 1 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, TernaryMultiline)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 5;
            let y: i32 = x > 3 ?
                7 :
                9;
            return y;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, ResultIsOkAndUnwrap)
{
    std::string code = R"(
        fn main() -> i32 {
            let r: result<i32, str8> = Ok<i32, str8>(123);
            if r.is_ok(): {
                let v: i32 = r.unwrap();
                println!("{}", v);
            } else: {
                println!("bad");
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "123\n");
}

TEST_F(MLATest, ResultUnwrapWarns)
{
    std::string code = R"(
        fn main() -> i32 {
            let r: result<i32, str8> = Ok<i32, str8>(1);
            let v: i32 = r.unwrap();
            return v;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("result.unwrap() may panic"), std::string::npos);
}

TEST_F(MLATest, UppercaseSumTypesAreNotBuiltins)
{
    std::string code = R"(
        fn main() -> i32 {
            let r: Result<i32, str8> = Ok<i32, str8>(1);
            let o: Option<i32> = Some<i32>(2);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("Result"), std::string::npos);
}

TEST_F(MLATest, LegacyRuntimeHandleTypesAreNotBuiltins)
{
    std::string code = R"(
        fn main() -> i32 {
            var handle: Handle<Thread>;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("Handle"), std::string::npos);
}

TEST_F(MLATest, FloatModuloByZeroReportsError)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: f32 = 5.5f % 0.0f;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("modulo by zero"), std::string::npos);
}

TEST_F(MLATest, ReturnInferenceMixedReturnFormsReportsError)
{
    std::string code = R"(
        fn bad(flag: bool) {
            if flag == true {
                return 1;
            }
            return;
        }

        fn main() -> i32 {
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot infer return type for function 'bad'"),
        std::string::npos);
    EXPECT_NE(
        out.find("function mixes 'return;' and 'return value;'"),
        std::string::npos);
}

TEST_F(MLATest, ReturnInferenceIncompatibleTypesReportsError)
{
    std::string code = R"(
        fn bad(flag: bool) {
            if flag == true {
                return 1;
            }
            return "oops";
        }

        fn main() -> i32 {
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot infer return type for function 'bad'"),
        std::string::npos);
    EXPECT_NE(out.find("incompatible return types"), std::string::npos);
}

TEST_F(MLATest, UpdateExpressionStructWithoutTraitReportsError)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        fn main() -> i32 {
            var c: Counter = Counter { value: 0 };
            c++;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("must implement trait 'Increment'"), std::string::npos);
}

TEST_F(MLATest, BinaryAddStructWithoutAddTraitReportsError)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        fn main() -> i32 {
            let a: Counter = Counter { value: 1 };
            let b: Counter = Counter { value: 2 };
            let c: Counter = a + b;
            return c.value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("must implement trait 'Add'"), std::string::npos);
}

TEST_F(MLATest, UnaryNegStructWithoutNegTraitReportsError)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        fn main() -> i32 {
            let a: Counter = Counter { value: 1 };
            let b: Counter = -a;
            return b.value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("must implement trait 'Neg'"), std::string::npos);
}

TEST_F(MLATest, IndexExpressionWithPostfixUpdateReportsError)
{
    std::string code = R"(
        fn main() -> i32 {
            let xs: list<i32> = [10, 20, 30];
            var i: i32 = 0;
            let v: i32 = xs[i++];
            return v;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("index expression does not allow pre/post ++/--"),
        std::string::npos);
}

TEST_F(MLATest, SpaceshipStructWithoutCompareTraitReportsError)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        fn main() -> i32 {
            let a: Counter = Counter { value: 1 };
            let b: Counter = Counter { value: 2 };
            let c: i32 = a <=> b;
            return c;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("must implement trait 'Compare'"), std::string::npos);
}

TEST_F(MLATest, OwnershipUseAfterMoveReportsError)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let b: Post = a;
            return take_post(a);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value"), std::string::npos);
}

TEST_F(MLATest, OwnershipCopyTypeRemainsUsableAfterAssignment)
{
    std::string code = R"(
        fn keep(x: i32) -> i32 {
            return x;
        }

        fn main() -> i32 {
            let a: i32 = 7;
            let b: i32 = a;
            return keep(a) + b;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 14);
}

TEST_F(MLATest, OwnershipAddressOfIsNonConsumingRead)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn borrow_ptr(p: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let p: ptr<Post> = &a;
            let _ok: i32 = borrow_ptr(p);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipIfBranchMoveMakesValueUnavailableAfterIf)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            if 1: {
                let b: Post = a;
            }
            return take_post(a);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value"), std::string::npos);
}

TEST_F(MLATest, OwnershipMatchArmMoveMakesValueUnavailableAfterMatch)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let flag: i32 = 1;
            let x: i32 = match flag {
                1 => take_post(a),
                _ => 20
            };
            return take_post(a);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value"), std::string::npos);
}

TEST_F(MLATest, OwnershipTernaryBranchMovesArePathSensitiveDuringEvaluation)
{
    std::string code = R"(
        fn take_msg(s: str8) -> i32 {
            return 5;
        }

        fn main() -> i32 {
            let flag: i32 = 0;
            let msg: str8 = "hello";
            let x: i32 = flag > 0 ? take_msg(msg) : take_msg(msg);
            return x;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

TEST_F(MLATest, OwnershipTernaryMoveStillConsumesAfterMerge)
{
    // Verify that if a MoveOnly value is consumed in the ternary "then" branch,
    // using it again after the merge is a "use of moved value" error.
    // Uses a struct (Post) which is correctly MoveOnly; string is Copy and
    // would not trigger move semantics.
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 1;
        }

        fn main() -> i32 {
            let flag: i32 = 1;
            let msg: Post = Post { content: [1] };
            let x: i32 = flag > 0 ? take_post(msg) : 0;
            return take_post(msg) + x;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanMoveAfterSharedPointerLastUse)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let p: ptr<Post> = &a;
            let moved: Post = a;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCannotMoveWhileMutablyBorrowedByPointer)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            var a: Post = Post { content: [1] };
            let p: ptr<Post> = &mut a;
            let moved: Post = a;
            unsafe { return (*p).content[0]; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("mutably borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, MutableBorrowDoesNotMoveMoveOnlyOwner)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            var a: Post = Post { content: [7] };
            let p: ptr<Post> = &mut a;
            unsafe { return (*p).content[0]; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, OwnershipStringDoubleFreeReportsError)
{
    std::string code = R"(
        fn main() -> i32 {
            let s: str8 = String::from("hello");
            String::free(s);
            String::free(s);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("double free or use-after-free"), std::string::npos);
}

TEST_F(MLATest, OwnershipStringUseAfterFreeReportsError)
{
    std::string code = R"(
        fn main() -> i32 {
            let s: str8 = String::from("hello");
            String::free(s);
            println!("{}", s);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value"), std::string::npos);
}

TEST_F(MLATest, OwnershipStringLiteralFreeReportsError)
{
    std::string code = R"(
        fn main() -> i32 {
            String::free("hello");
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot free string literal"), std::string::npos);
}

TEST_F(MLATest, OwnershipFreeAliasDoubleFreeReportsError)
{
    std::string code = R"(
        fn free(s: str8) {
            String::free(s);
        }

        fn main() -> i32 {
            let s: str8 = String::from("hello");
            free(s);
            free(s);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("double free or use-after-free"), std::string::npos);
}

// Calling free() explicitly consumes the value and suppresses its scope-exit
// cleanup.
TEST_F(MLATest, OwnershipExplicitFreeRunsCleanupExactlyOnce)
{
    std::string code = R"(
        struct Owned {
            var marker: i32;
        };

        impl Owned {
            fn free(self: Owned) -> void {
                println!("free {}", self.marker);
            }
        }

        fn main() -> i32 {
            let owned: Owned = Owned { marker: 7 };
            owned.free();
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "free 7\n");
}

TEST_F(MLATest, OwnershipScopeExitFreeMethodConsumesValue)
{
    std::string code = R"(
        struct OwnedText {
            var raw: str8;
        };

        impl OwnedText {
            pub fn free(self: OwnedText) -> i32 {
                String::free(self.raw);
                return 0;
            }
        }

        fn main() -> i32 {
            let s: OwnedText = OwnedText { raw: String::from("hello") };
            s.free();
            println!("{}", s.raw);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value"), std::string::npos);
}

// A cleanup still runs on a branch where an outer value was not moved.
TEST_F(MLATest, OwnershipConditionalMoveCleansUpUnmovedPath)
{
    std::string code = R"(
        struct Owned {
            var marker: i32;
            var items: list<i32>;
        };

        impl Owned {
            fn free(self: Owned) -> i32 {
                println!("free {}", self.marker);
                return 0;
            }
        }

        fn choose() -> bool { return false; }

        fn main() -> i32 {
            var owned: Owned = Owned { marker: 7, items: [1] };
            if choose() {
                let moved: Owned = owned;
            }
            return 0;
        }
    )";
    writeSource(code);
    ASSERT_TRUE(compile());
    EXPECT_EQ(run(), "free 7\n");
}

// Moving a value in the taken branch cleans it once through the destination.
TEST_F(MLATest, OwnershipConditionalMoveCleansUpMovedPathExactlyOnce)
{
    std::string code = R"(
        struct Owned {
            var marker: i32;
            var items: list<i32>;
        };

        impl Owned {
            fn free(self: Owned) -> i32 {
                println!("free {}", self.marker);
                return 0;
            }
        }

        fn choose() -> bool { return true; }

        fn main() -> i32 {
            var owned: Owned = Owned { marker: 7, items: [1] };
            if choose() {
                let moved: Owned = owned;
            }
            return 0;
        }
    )";
    writeSource(code);
    ASSERT_TRUE(compile());
    EXPECT_EQ(run(), "free 7\n");
}

// Reinitializing a moved cleanup type enables cleanup for the new value.
TEST_F(MLATest, OwnershipConditionalMoveThenReinitializeCleansBothValues)
{
    std::string code = R"(
        struct Owned {
            var marker: i32;
            var items: list<i32>;
        };

        impl Owned {
            fn free(self: Owned) -> i32 {
                println!("free {}", self.marker);
                return 0;
            }
        }

        fn choose() -> bool { return true; }

        fn main() -> i32 {
            var owned: Owned = Owned { marker: 7, items: [1] };
            if choose() {
                let moved: Owned = owned;
                owned = Owned { marker: 8, items: [2] };
            }
            return 0;
        }
    )";
    writeSource(code);
    ASSERT_TRUE(compile());
    EXPECT_EQ(run(), "free 7\nfree 8\n");
}

// A type with cleanup remains move-only even when all fields are Copy.
TEST_F(MLATest, OwnershipCleanupTypeCannotBeCopiedByAssignment)
{
    std::string code = R"(
        struct Owned { var marker: i32; };

        impl Owned {
            fn free(self: Owned) -> i32 {
                println!("free {}", self.marker);
                return 0;
            }
        }

        fn main() -> i32 {
            let first: Owned = Owned { marker: 7 };
            let second: Owned = first;
            return first.marker + second.marker;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'first'"), std::string::npos)
        << out;
}

// Moving a cleanup type transfers responsibility and invokes cleanup once.
TEST_F(MLATest, OwnershipCleanupTypeMoveTransfersCleanupResponsibility)
{
    std::string code = R"(
        struct Owned { var marker: i32; };

        impl Owned {
            fn free(self: Owned) -> i32 {
                println!("free {}", self.marker);
                return 0;
            }
        }

        fn main() -> i32 {
            let first: Owned = Owned { marker: 7 };
            let second: Owned = first;
            return second.marker - 7;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "free 7\n");
}

// Shadowed cleanup variables each drop their own value in reverse scope order.
TEST_F(MLATest, OwnershipShadowedCleanupVariablesDropTheirOwnValues)
{
    std::string code = R"(
        struct Owned { var marker: i32; };

        impl Owned {
            fn free(self: Owned) -> i32 {
                println!("free {}", self.marker);
                return 0;
            }
        }

        fn main() -> i32 {
            let owned: Owned = Owned { marker: 1 };
            {
                let owned: Owned = Owned { marker: 2 };
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "free 2\nfree 1\n");
}

// Reassignment drops the old value immediately and the replacement at scope
// exit.
TEST_F(MLATest, OwnershipReassignmentDropsReplacedCleanupValue)
{
    std::string code = R"(
        struct Owned { var marker: i32; };

        impl Owned {
            fn free(self: Owned) -> i32 {
                println!("free {}", self.marker);
                return 0;
            }
        }

        fn main() -> i32 {
            var owned: Owned = Owned { marker: 1 };
            owned = Owned { marker: 2 };
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "free 1\nfree 2\n");
}

// Moving an outer variable remains visible as moved after an inner shadow ends.
TEST_F(MLATest, OwnershipShadowDoesNotRestoreMovedOuterVariable)
{
    std::string code = R"(
        struct Owned { var marker: i32; var items: list<i32>; };

        fn main() -> i32 {
            let owned: Owned = Owned { marker: 1, items: [1] };
            let moved: Owned = owned;
            {
                let owned: Owned = Owned { marker: 2, items: [2] };
                println!("{}", owned.marker);
            }
            return owned.marker;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'owned'"), std::string::npos)
        << out;
}

// A move of an inner shadow must not make the outer variable unavailable.
TEST_F(MLATest, OwnershipInnerShadowMoveDoesNotMoveOuterVariable)
{
    std::string code = R"(
        struct Owned { var marker: i32; var items: list<i32>; };

        fn main() -> i32 {
            let owned: Owned = Owned { marker: 1, items: [1] };
            {
                let owned: Owned = Owned { marker: 2, items: [2] };
                let moved: Owned = owned;
            }
            return owned.marker - 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A borrow of an outer binding must not prohibit mutation of a same-named
// shadow.
TEST_F(MLATest, OwnershipBorrowOfOuterDoesNotBlockShadowMutation)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let pointer = &pair.left;
            {
                var pair: Pair = Pair { left: 3, right: 4 };
                pair.left = 9;
            }
            unsafe { return *pointer - 1; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// The outer binding's borrow restrictions resume after its shadow exits.
TEST_F(MLATest, OwnershipOuterBorrowRestoredAfterOwnerShadowScope)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let pointer = &pair.left;
            {
                var pair: Pair = Pair { left: 3, right: 4 };
                pair.left = 9;
            }
            pair.left = 11;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'pointer'"), std::string::npos)
        << out;
}

// A borrow created by an inner shadow must not remain attached to the outer
// value.
TEST_F(MLATest, OwnershipInnerShadowBorrowDoesNotLeakToOuter)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            {
                var pair: Pair = Pair { left: 3, right: 4 };
                let pointer = &pair.left;
                unsafe { if *pointer != 3 { return 1; } }
            }
            pair.left = 11;
            return pair.left - 11;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Container element type metadata must return to the outer binding after
// shadowing.
TEST_F(MLATest, OwnershipListElementTypeRestoresAfterShadowScope)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: list<i32> = [7];
            {
                var values: list<ptr<i32>> = [];
            }
            return values[0] - 7;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A mutable local shadow must not inherit an immutable-reference parameter's
// restriction.
TEST_F(MLATest, OwnershipMutableShadowOfImmutableReferenceCanBeMutated)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn use_pair(pair: &Pair) -> i32 {
            {
                var pair: Pair = Pair { left: 3, right: 4 };
                pair.left = 9;
            }
            return pair.left - 1;
        }

        fn main() -> i32 {
            let pair: Pair = Pair { left: 1, right: 2 };
            return use_pair(&pair);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Shadowing an immutable reference must restore its restriction after scope
// exit.
TEST_F(MLATest, OwnershipImmutableReferenceRestrictionRestoredAfterShadow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn use_pair(pair: &Pair) -> i32 {
            {
                var pair: Pair = Pair { left: 3, right: 4 };
                pair.left = 9;
            }
            pair.left = 11;
            return 0;
        }

        fn main() -> i32 {
            let pair: Pair = Pair { left: 1, right: 2 };
            return use_pair(&pair);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate through immutable reference 'pair'"),
        std::string::npos)
        << out;
}

// Shadowing a mutable pointer must not release the hidden outer borrow.
TEST_F(MLATest, OwnershipPointerShadowKeepsOuterMutableBorrowLive)
{
    std::string code = R"(
        fn main() -> i32 {
            var owner: i32 = 1;
            var other: i32 = 2;
            let pointer = &mut owner;
            {
                let pointer = &mut other;
                owner = 3;
            }
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'owner' directly while mutably borrowed"),
        std::string::npos)
        << out;
}

// A shadowed shared pointer must keep the outer owner read-only as well.
TEST_F(MLATest, OwnershipPointerShadowKeepsOuterSharedBorrowLive)
{
    std::string code = R"(
        fn main() -> i32 {
            var owner: i32 = 1;
            var other: i32 = 2;
            let pointer = &owner;
            {
                let pointer = &other;
                owner = 3;
            }
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'owner' while borrowed by 'pointer"),
        std::string::npos)
        << out;
}

// A hidden mutable pointer's exclusive borrow must be restored after its shadow
// exits.
TEST_F(MLATest, OwnershipPointerShadowRestoresOuterMutableBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var owner: i32 = 1;
            var other: i32 = 2;
            let pointer = &mut owner;
            {
                let pointer = &mut other;
            }
            owner = 3;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'owner' directly while mutably borrowed"),
        std::string::npos)
        << out;
}

// Exiting a deeper same-name pointer shadow restores the enclosing borrow.
TEST_F(MLATest, OwnershipNestedPointerShadowRestoresIntermediateBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var outer: i32 = 1;
            var middle: i32 = 2;
            var inner: i32 = 3;
            let pointer = &mut outer;
            {
                let pointer = &mut middle;
                {
                    let pointer = &mut inner;
                }
                middle = 4;
                unsafe { return *pointer; }
            }
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'middle' directly while mutably borrowed"),
        std::string::npos)
        << out;
}

// Restoring a nested shadow chain must eventually revive the outermost borrow.
TEST_F(MLATest, OwnershipNestedPointerShadowRestoresOutermostBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var outer: i32 = 1;
            var middle: i32 = 2;
            var inner: i32 = 3;
            let pointer = &mut outer;
            {
                let pointer = &mut middle;
                {
                    let pointer = &mut inner;
                }
            }
            outer = 4;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'outer' directly while mutably borrowed"),
        std::string::npos)
        << out;
}

// Dropping a same-named closure shadow must not release the outer capture
// borrow.
TEST_F(MLATest, RuntimeClosureShadowDoesNotReleaseOuterCaptureBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            let read: fn() -> i32 = || { return value; };
            {
                let read: fn() -> i32 = || { return value; };
            }
            value = 2;
            return read();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'read'"), std::string::npos) << out;
}

// A shadowed closure's capture metadata must not keep the outer borrow past
// last use.
TEST_F(MLATest, RuntimeClosureShadowRestoresOuterLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            let read: fn() -> i32 = || { return value; };
            {
                var other: i32 = 3;
                let read: fn() -> i32 = || { return other; };
            }
            let result: i32 = read();
            value = 2;
            return result;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 1);
}

TEST_F(MLATest, OwnershipUseOutsideBlockReportsUnknownVariable)
{
    std::string code = R"(
        fn main() -> i32 {
            {
                let s: str8 = String::from("hello");
                println!("{}", s);
            }
            println!("{}", s);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("unknown variable"), std::string::npos);
}

TEST_F(MLATest, OwnershipHandleFreeDoubleFreeReportsError)
{
    std::string code = R"(
        mod std::thread;
        use std::thread::*;

        fn main() -> i32 {
            let a: atomic64 = atomic_new(1);
            atomic_i64_free(a.raw);
            atomic_i64_free(a.raw);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("double free or use-after-free"), std::string::npos);
}

TEST_F(MLATest, OwnershipPointerReassignReleasesPreviousBorrow)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let b: Post = Post { content: [1] };
            var p: ptr<Post> = &a;
            p = &b;
            return take_post(a);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipIfReturnBranchBorrowDoesNotLeak)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let b: Post = Post { content: [1] };
            var p: ptr<Post> = &b;
            let cond: i32 = 1;
            if cond: {
                p = &a;
                return 0;
            }
            let moved: Post = a;
            return take_post(moved);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCannotCreateSecondActiveBorrow)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            var p: ptr<Post> = &a;
            var q: ptr<Post> = &a;
            unsafe { return (*p).content[0] + (*q).content[0]; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("already borrowed"), std::string::npos);
}

TEST_F(MLATest, OwnershipRebindingSamePointerBorrowAllowed)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            var p: ptr<Post> = &a;
            p = &a;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCanAliasSharedBorrowFromPointerVariable)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let b: Post = Post { content: [1] };
            var p: ptr<Post> = &a;
            var q: ptr<Post> = &b;
            q = p;
            unsafe { return (*p).content[0] + (*q).content[0]; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 2);
}

TEST_F(MLATest, OwnershipCannotAliasMutableBorrowFromPointerVariable)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            var a: Post = Post { content: [1] };
            let p: ptr<Post> = &mut a;
            let q: ptr<Post> = p;
            unsafe { return (*q).content[0]; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot alias mutable borrow"), std::string::npos);
}

// Verifies that an exclusive pointer can create a usable child reborrow.
TEST_F(MLATest, ExclusivePointerCanBeReborrowed)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let parent: ptr<i32> = &mut value;
            let child: ptr<i32> = &mut *parent;
            unsafe { *child = 12; }
            unsafe { return *parent; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

// Verifies that nested exclusive reborrows permit child access and restore
// each parent pointer as its child loan expires.
TEST_F(MLATest, ExclusivePointerSupportsNestedReborrows)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let parent: ptr<i32> = &mut value;
            let child: ptr<i32> = &mut *parent;
            let grandchild: ptr<i32> = &mut *child;
            unsafe { *grandchild = 15; }
            unsafe { let child_value: i32 = *child; }
            unsafe { return *parent; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 15);
}

// Verifies that a synchronous call can use a temporary exclusive reborrow.
TEST_F(MLATest, ExclusivePointerCanBeReborrowedForCall)
{
    std::string code = R"(
        fn write_value(pointer: ptr<i32>) -> i32 {
            unsafe { *pointer = 21; }
            return 0;
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let parent: ptr<i32> = &mut value;
            write_value(&mut *parent);
            unsafe { return *parent; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 21);
}

// Verifies that a call still rejects a second overlapping exclusive argument.
TEST_F(MLATest, ExclusiveReborrowCallRejectsAliasedSecondArgument)
{
    std::string code = R"(
        fn take_two(first: ptr<i32>, second: ptr<i32>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let parent: ptr<i32> = &mut value;
            take_two(&mut *parent, &mut value);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'value' as mutable more than once"),
        std::string::npos)
        << out;
}

// Verifies that a shared pointer cannot mutate the place it borrows.
TEST_F(MLATest, SharedBorrowCannotWriteThroughItsPointer)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            unsafe { *shared = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot write through shared borrow 'shared'"),
        std::string::npos)
        << out;
}

// A temporary shared borrow must not be made writable by immediate dereference.
TEST_F(MLATest, SharedBorrowCannotWriteThroughTemporaryPointer)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            unsafe { *(&value) = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A function returning a shared argument must not hide a write from
// validation.
TEST_F(MLATest, SharedBorrowReturnedFromFunctionCannotBeWrittenThrough)
{
    std::string code = R"(
        fn identity(pointer: ptr<i32>) -> ptr<i32> {
            return pointer;
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            unsafe { *identity(shared) = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A function returning a global shared borrow must not allow direct mutation.
TEST_F(MLATest, GlobalSharedBorrowReturnedFromFunctionCannotBeWrittenThrough)
{
    std::string code = R"(
        var global_value: i32 = 4;

        fn get_shared() -> ptr<i32> {
            return &global_value;
        }

        fn main() -> i32 {
            unsafe { *get_shared() = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A local alias must not erase a shared borrow created inside a function.
TEST_F(
    MLATest,
    GlobalSharedBorrowAliasReturnedFromFunctionCannotBeWrittenThrough)
{
    std::string code = R"(
        var global_value: i32 = 4;

        fn get_shared() -> ptr<i32> {
            let saved_pointer: ptr<i32> = &global_value;
            return saved_pointer;
        }

        fn main() -> i32 {
            unsafe { *get_shared() = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Returning a pointer field from a local aggregate preserves shared access.
TEST_F(
    MLATest,
    GlobalSharedBorrowFieldReturnedFromFunctionCannotBeWrittenThrough)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        var global_value: i32 = 4;

        fn get_shared() -> ptr<i32> {
            let box: PointerBox = PointerBox { value: &global_value };
            return box.value;
        }

        fn main() -> i32 {
            unsafe { *get_shared() = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A shared pointer field returned through a function remains read-only locally.
TEST_F(
    MLATest,
    StoredGlobalSharedBorrowFieldReturnedFromFunctionCannotBeWrittenThrough)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        var global_value: i32 = 4;

        fn get_shared() -> ptr<i32> {
            let box: PointerBox = PointerBox { value: &global_value };
            return box.value;
        }

        fn main() -> i32 {
            let pointer: ptr<i32> = get_shared();
            unsafe { *pointer = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A shared borrow returned through a local alias stays read-only when stored.
TEST_F(
    MLATest,
    StoredGlobalSharedBorrowAliasReturnedFromFunctionCannotBeWrittenThrough)
{
    std::string code = R"(
        var global_value: i32 = 4;

        fn get_shared() -> ptr<i32> {
            let saved_pointer: ptr<i32> = &global_value;
            return saved_pointer;
        }

        fn main() -> i32 {
            let pointer: ptr<i32> = get_shared();
            unsafe { *pointer = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A returned global shared borrow remains read-only after storing it locally.
TEST_F(
    MLATest,
    StoredGlobalSharedBorrowReturnedFromFunctionCannotBeWrittenThrough)
{
    std::string code = R"(
        var global_value: i32 = 4;

        fn get_shared() -> ptr<i32> {
            return &global_value;
        }

        fn main() -> i32 {
            let pointer: ptr<i32> = get_shared();
            unsafe { *pointer = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A function returning an exclusive global borrow remains writable.
TEST_F(MLATest, GlobalExclusiveBorrowReturnedFromFunctionCanBeWrittenThrough)
{
    std::string code = R"(
        var global_value: i32 = 4;

        fn get_mutable() -> ptr<i32> {
            return &mut global_value;
        }

        fn main() -> i32 {
            unsafe { *get_mutable() = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0) << out;
}

// A function-local alias of an exclusive global borrow remains writable.
TEST_F(MLATest, StoredGlobalExclusiveBorrowAliasRemainsWritable)
{
    std::string code = R"(
        var global_value: i32 = 4;

        fn get_mutable() -> ptr<i32> {
            let saved_pointer: ptr<i32> = &mut global_value;
            return saved_pointer;
        }

        fn main() -> i32 {
            let pointer: ptr<i32> = get_mutable();
            unsafe { *pointer = 9; }
            return global_value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0) << out;
}

// A stored exclusive return alias prevents a second mutable borrow.
TEST_F(MLATest, StoredGlobalExclusiveBorrowAliasBlocksSecondBorrow)
{
    std::string code = R"(
        var global_value: i32 = 4;

        fn get_mutable() -> ptr<i32> {
            let saved_pointer: ptr<i32> = &mut global_value;
            return saved_pointer;
        }

        fn main() -> i32 {
            let pointer: ptr<i32> = get_mutable();
            let second: ptr<i32> = &mut global_value;
            unsafe { return *pointer + *second; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'global_value' as mutable more than once"),
        std::string::npos)
        << out;
}

// Returning an exclusive argument remains writable through the call result.
TEST_F(MLATest, ExclusiveBorrowReturnedFromFunctionCanBeWrittenThrough)
{
    std::string code = R"(
        fn identity(pointer: ptr<i32>) -> ptr<i32> {
            return pointer;
        }

        fn main() -> i32 {
            var value: i32 = 4;
            unsafe { *identity(&mut value) = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0) << out;
}

// Callable-value invocation must keep a returned shared argument read-only.
TEST_F(MLATest, SharedBorrowReturnedThroughCallableCannotBeWrittenThrough)
{
    std::string code = R"(
        fn identity(pointer: ptr<i32>) -> ptr<i32> {
            return pointer;
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            let relay: fn(ptr<i32>) -> ptr<i32> = identity;
            unsafe { *relay(shared) = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A returned pointer must retain shared provenance from a list element
// argument.
TEST_F(MLATest, SharedBorrowFromListCannotBeWrittenThroughReturnedPointer)
{
    std::string code = R"(
        fn identity(pointer: ptr<i32>) -> ptr<i32> {
            return pointer;
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let pointers: list<ptr<i32>> = [&value];
            unsafe { *identity(pointers[0]) = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A closure returning a captured shared pointer must not hide a direct write.
TEST_F(MLATest, SharedBorrowReturnedFromClosureCannotBeWrittenThrough)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            let callback: fn() -> ptr<i32> = || { return shared; };
            unsafe { *callback() = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A closure forwarding its pointer argument must preserve shared provenance.
TEST_F(MLATest, SharedBorrowForwardedThroughClosureCannotBeWrittenThrough)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            let callback: fn(ptr<i32>) -> ptr<i32> =
                |pointer: ptr<i32>| { return pointer; };
            unsafe { *callback(shared) = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A nested dereference must retain the shared borrow restriction on its place.
TEST_F(MLATest, SharedBorrowCannotWriteThroughNestedPointer)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointer: ptr<i32> = &first;
            var slot: ptr<ptr<i32>> = &pointer;
            let shared_slot: ptr<ptr<ptr<i32>>> = &slot;
            unsafe { *(*shared_slot) = &mut second; }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A mutable borrow of a pointer slot cannot upgrade its stored shared pointer.
TEST_F(MLATest, MutableSlotCannotUpgradeStoredSharedPointer)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointer: ptr<i32> = &first;
            var slot: ptr<ptr<i32>> = &pointer;
            let mutable_slot: ptr<ptr<ptr<i32>>> = &mut slot;
            unsafe { *(*mutable_slot) = &mut second; }
            let keep_slot_live: ptr<ptr<i32>> = slot;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Keeps a stored shared pointer read-only through an indirect slot after its
// last direct use.
TEST_F(MLATest, MutableSlotCannotUpgradeStoredSharedPointerAfterLastDirectUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointer: ptr<i32> = &first;
            var slot: ptr<ptr<i32>> = &pointer;
            let mutable_slot: ptr<ptr<ptr<i32>>> = &mut slot;
            unsafe { *(*mutable_slot) = &mut second; }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Releases a pointer's deferred borrow when the last borrowed slot leaves
// scope.
TEST_F(MLATest, DeferredPointerBorrowEndsWithBorrowedSlotScope)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            var pointer: ptr<i32> = &value;
            {
                var slot: ptr<ptr<i32>> = &pointer;
                let pointer_again: ptr<i32> = *slot;
            }
            let exclusive: ptr<i32> = &mut value;
            unsafe { *exclusive = 2; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0) << out;
}

// Retains pointer provenance until every slot borrowing that pointer has ended.
TEST_F(MLATest, DeferredPointerBorrowWaitsForEverySlot)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointer: ptr<i32> = &first;
            var first_slot: ptr<ptr<i32>> = &pointer;
            {
                var second_slot: ptr<ptr<i32>> = &pointer;
                let pointer_again: ptr<i32> = *second_slot;
            }
            let mutable_slot: ptr<ptr<ptr<i32>>> = &mut first_slot;
            unsafe { *(*mutable_slot) = &mut second; }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Preserves shared-borrow restrictions when pointer origins merge at an
// if-join.
TEST_F(MLATest, MergedSharedPointerCannotBeReplacedThroughBorrowedSlot)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var replacement: i32 = 3;
            var pointer: ptr<i32> = &first;
            let flag: bool = true;
            if flag {
                pointer = &second;
            }
            var slot: ptr<ptr<i32>> = &pointer;
            let mutable_slot: ptr<ptr<ptr<i32>>> = &mut slot;
            unsafe { *(*mutable_slot) = &mut replacement; }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Verifies that a shared borrow remains read-only when stored in an aggregate.
TEST_F(MLATest, SharedBorrowCannotWriteThroughAggregatePointerField)
{
    std::string code = R"(
        struct PointerBox { var pointer: ptr<i32>; };

        fn main() -> i32 {
            var value: i32 = 4;
            let box = PointerBox { pointer: &value };
            unsafe { *box.pointer = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Verifies that a mutating pointer parameter cannot receive a shared borrow.
TEST_F(MLATest, SharedBorrowCannotPassToMutatingPointerParameter)
{
    std::string code = R"(
        fn write_value(pointer: ptr<i32>) -> void {
            let copied_pointer: ptr<i32> = pointer;
            unsafe { *copied_pointer = 9; }
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            write_value(shared);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating"),
        std::string::npos)
        << out;
}

// Verifies that an opaque pointer parameter cannot retain a shared local
// borrow.
TEST_F(MLATest, SharedBorrowCannotPassToOpaquePointerParameter)
{
    std::string code = R"(
        extern fn update_value(pointer: ptr<i32>) -> void;

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            update_value(shared);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Opaque calls must reject a joined exclusive pointer with a local origin.
TEST_F(MLATest, JoinedExclusivePointerWithLocalOriginCannotReachOpaqueParameter)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            var selected: ptr<i32> = &mut global_value;
            if flag {
                selected = &mut global_value;
            } else {
                selected = &mut local_value;
            }
            retain_pointer(selected);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Joined reborrows must preserve local provenance for opaque-call checks.
TEST_F(MLATest, JoinedReborrowWithLocalOriginCannotReachOpaqueParameter)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            let global_parent: ptr<i32> = &mut global_value;
            let local_parent: ptr<i32> = &mut local_value;
            var selected: ptr<i32> = &mut *global_parent;
            if flag {
                selected = &mut *global_parent;
            } else {
                selected = &mut *local_parent;
            }
            retain_pointer(selected);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Pointer shadow cleanup must restore every joined reborrow origin.
TEST_F(MLATest, JoinedReborrowOriginSurvivesPointerShadowCleanup)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            let global_parent: ptr<i32> = &mut global_value;
            let local_parent: ptr<i32> = &mut local_value;
            var selected: ptr<i32> = &mut *global_parent;
            if flag {
                selected = &mut *global_parent;
            } else {
                selected = &mut *local_parent;
            }
            {
                let selected = &mut global_value;
            }
            retain_pointer(selected);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// A child reborrow must inherit every possible owner from its joined parent.
TEST_F(MLATest, ChildReborrowOfJoinedPointerCannotEscapeLocalOrigin)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            var parent: ptr<i32> = &mut global_value;
            if flag {
                parent = &mut global_value;
            } else {
                parent = &mut local_value;
            }
            let child: ptr<i32> = &mut *parent;
            retain_pointer(child);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Nested reborrows must continue carrying every owner from a joined ancestor.
TEST_F(MLATest, NestedReborrowOfJoinedPointerCannotEscapeLocalOrigin)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            var parent: ptr<i32> = &mut global_value;
            if flag {
                parent = &mut global_value;
            } else {
                parent = &mut local_value;
            }
            let child: ptr<i32> = &mut *parent;
            let grandchild: ptr<i32> = &mut *child;
            retain_pointer(grandchild);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Ternary reborrow joins must retain every parent owner for escape checks.
TEST_F(MLATest, TernaryReborrowOfLocalParentCannotEscapeToOpaqueCall)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            let flag: bool = true;
            let global_parent: ptr<i32> = &mut global_value;
            let local_parent: ptr<i32> = &mut local_value;
            let selected: ptr<i32> =
                flag ? &mut *global_parent : &mut *local_parent;
            retain_pointer(selected);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Loop-exit joins must retain local owners before a later child reborrow.
TEST_F(MLATest, LoopJoinedPointerReborrowCannotEscapeLocalOrigin)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var again: bool = true;
            var parent: ptr<i32> = &mut global_value;
            while again {
                parent = &mut local_value;
                again = false;
            }
            let child: ptr<i32> = &mut *parent;
            retain_pointer(child);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Verifies a conditional expression cannot hide a shared borrow from a
// writer.
TEST_F(MLATest, SharedBorrowInTernaryCannotPassToMutatingPointerParameter)
{
    std::string code = R"(
        fn write_value(pointer: ptr<i32>) -> void {
            unsafe { *pointer = 9; }
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            write_value(true ? shared : &mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("to mutating pointer parameter 'pointer'"),
        std::string::npos)
        << out;
}

// A joined pointer with any shared origin cannot reach a mutating parameter.
TEST_F(MLATest, MixedSharedAndExclusivePointerCannotPassToMutatingParameter)
{
    std::string code = R"(
        fn write_value(pointer: ptr<i32>) -> void {
            unsafe { *pointer = 9; }
        }

        fn main() -> i32 {
            var exclusive_value: i32 = 4;
            var shared_value: i32 = 5;
            let flag: bool = true;
            let pointer: ptr<i32> =
                flag ? &mut exclusive_value : &shared_value;
            write_value(pointer);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("to mutating pointer parameter 'pointer'"),
        std::string::npos)
        << out;
}

// Verifies a shared pointer extracted from a list is not passed to a writer.
TEST_F(MLATest, SharedBorrowExtractedFromListCannotReachWriter)
{
    std::string code = R"(
        fn write_value(pointer: ptr<i32>) -> void {
            unsafe { *pointer = 9; }
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let pointers: list<ptr<i32>> = [&value];
            write_value(pointers[0]);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("to mutating pointer parameter 'pointer'"),
        std::string::npos)
        << out;
}

// A shared pointer extracted from a list remains read-only when dereferenced.
TEST_F(MLATest, SharedBorrowExtractedFromListCannotBeWrittenThrough)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let pointers: list<ptr<i32>> = [&value];
            let extracted: ptr<i32> = pointers[0];
            unsafe { *extracted = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A shared pointer indexed directly from a list must also remain read-only.
TEST_F(MLATest, SharedBorrowIndexedFromListCannotBeWrittenThrough)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let pointers: list<ptr<i32>> = [&value];
            unsafe { *pointers[0] = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// A shared pointer accessed directly from a tuple field must remain read-only.
TEST_F(MLATest, SharedBorrowTupleFieldCannotBeWrittenThrough)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let pointers: tuple<ptr<i32>, i32> = (&value, 0);
            unsafe { *pointers.0 = 9; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Verifies an exclusive borrow live on one branch remains active after merge.
TEST_F(MLATest, ConditionalExclusiveBorrowBlocksOwnerMutationAfterMerge)
{
    std::string code = R"(
        var global_value: i32 = 0;
        fn main() -> i32 {
            var value: i32 = 4;
            var flag: bool = true;
            var pointer: ptr<i32> = &mut global_value;
            if flag {
                pointer = &mut value;
            }
            value = 9;
            unsafe { *pointer = 10; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while mutably borrowed"), std::string::npos) << out;
}

// Verifies a pointer remains usable after a conditional exclusive borrow
// merge.
TEST_F(MLATest, ConditionalExclusiveBorrowRemainsUsableAfterMerge)
{
    std::string code = R"(
        var global_value: i32 = 0;
        fn main() -> i32 {
            var value: i32 = 4;
            var flag: bool = true;
            var pointer: ptr<i32> = &mut global_value;
            if flag {
                pointer = &mut value;
            }
            unsafe { *pointer = 10; }
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 10);
}

// Verifies a ternary-created exclusive borrow blocks owner mutation
// afterward.
TEST_F(MLATest, TernaryExclusiveBorrowBlocksOwnerMutationAfterMerge)
{
    std::string code = R"(
        var global_value: i32 = 0;
        fn main() -> i32 {
            var value: i32 = 4;
            var flag: bool = true;
            var pointer: ptr<i32> = &global_value;
            pointer = flag ? &mut value : &mut global_value;
            value = 9;
            unsafe { *pointer = 10; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while mutably borrowed"), std::string::npos) << out;
}

// Verifies a pointer from a ternary can still use its active exclusive
// borrow.
TEST_F(MLATest, TernaryExclusiveBorrowRemainsUsableAfterMerge)
{
    std::string code = R"(
        var global_value: i32 = 0;
        fn main() -> i32 {
            var value: i32 = 4;
            var flag: bool = true;
            var pointer: ptr<i32> = &global_value;
            pointer = flag ? &mut value : &mut global_value;
            unsafe { *pointer = 10; }
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 10);
}

// A conditional pointer with a shared branch must not be writable through.
TEST_F(MLATest, TernaryMixedSharedAndExclusiveBorrowCannotWriteThrough)
{
    std::string code = R"(
        fn main() -> i32 {
            var exclusive_value: i32 = 1;
            var shared_value: i32 = 2;
            let flag: bool = true;
            let pointer: ptr<i32> =
                flag ? &mut exclusive_value : &shared_value;
            unsafe { *pointer = 3; }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot write through shared borrow"), std::string::npos)
        << out;
}

// Verifies even a single-arm match result registers its exclusive borrow.
TEST_F(MLATest, SingleArmMatchExclusiveBorrowBlocksOwnerMutation)
{
    std::string code = R"(
        enum Choice : i32 { Borrow = 1 };
        fn main() -> i32 {
            var value: i32 = 4;
            let choice: Choice = Choice::Borrow;
            var pointer: ptr<i32> = &value;
            pointer = match choice { _ => &mut value };
            value = 9;
            unsafe { *pointer = 10; }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while mutably borrowed"), std::string::npos) << out;
}

// Verifies that opaque pointer calls cannot retain a local mutable borrow.
TEST_F(MLATest, OpaquePointerCannotRetainLocalBorrowOutsideUnsafe)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        fn main() -> i32 {
            var value: i32 = 4;
            retain_pointer(&mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("callee may retain it"), std::string::npos) << out;
}

// Verifies that read-only pointer parameters still accept shared borrows.
TEST_F(MLATest, SharedBorrowCanPassToReadOnlyPointerParameter)
{
    std::string code = R"(
        fn read_value(pointer: ptr<i32>) -> i32 {
            unsafe { return *pointer; }
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            return read_value(shared);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0) << out;
}

// Verifies that shared-borrow checks follow named calls to pointer writers.
TEST_F(MLATest, SharedBorrowCannotReachMutatingPointerThroughHelper)
{
    std::string code = R"(
        fn write_value(pointer: ptr<i32>) -> i32 {
            unsafe { *pointer = 9; }
            return 0;
        }

        fn forward_pointer(pointer: ptr<i32>) -> void {
            let ignored: i32 = 1 + write_value(pointer);
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            forward_pointer(shared);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating"),
        std::string::npos)
        << out;
}

// Verifies that pointer writes hidden inside a local closure count as effects.
TEST_F(MLATest, SharedBorrowCannotReachMutatingPointerThroughCapturedClosure)
{
    std::string code = R"(
        fn forward_pointer(pointer: ptr<i32>) -> void {
            let write = || {
                unsafe { *pointer = 9; }
            };
            write();
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            forward_pointer(shared);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating"),
        std::string::npos)
        << out;
}

// Verifies that method calls propagate pointer mutation effects to callers.
TEST_F(MLATest, SharedBorrowCannotReachMutatingPointerThroughMethod)
{
    std::string code = R"(
        struct Writer { var marker: i32; };
        impl Writer {
            fn write(&self, pointer: ptr<i32>) -> i32 {
                unsafe { *pointer = 9; }
                return 0;
            }
        }

        fn forward_pointer(pointer: ptr<i32>) -> void {
            let writer = Writer { marker: 0 };
            let ignored: i32 = writer.write(pointer);
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            forward_pointer(shared);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating"),
        std::string::npos)
        << out;
}

// Verifies that opaque callable parameters cannot mutate a shared borrow.
TEST_F(MLATest, SharedBorrowCannotReachMutatingPointerThroughCallable)
{
    std::string code = R"(
        fn forward_pointer(
            pointer: ptr<i32>,
            callback: fn(ptr<i32>) -> void
        ) -> void {
            callback(pointer);
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            let writer = |pointer: ptr<i32>| {
                unsafe { *pointer = 9; }
            };
            forward_pointer(shared, writer);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating"),
        std::string::npos)
        << out;
}

// Verifies that invoking a runtime callable with a local borrow requires
// unsafe.
TEST_F(MLATest, CallablePointerArgumentRequiresUnsafeForLocalBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let callback: fn(ptr<i32>) -> void = |pointer| {
                unsafe { *pointer = 9; }
            };
            callback(&mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("outside unsafe because the callable may retain it"),
        std::string::npos)
        << out;
}

// Verifies callback parameters cannot retain borrows of the callee's locals.
TEST_F(MLATest, CallableParameterCannotRetainCalleeLocalBorrow)
{
    std::string code = R"(
        fn invoke(callback: fn(ptr<i32>) -> void) -> void {
            var local: i32 = 4;
            callback(&mut local);
        }
        fn main() -> i32 {
            invoke(|pointer| {});
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("outside unsafe because the callable may retain it"),
        std::string::npos)
        << out;
}

// A named function stored as a callable local is not treated as opaque when
// passed through a wrapper that immediately invokes the callback.
TEST_F(MLATest, NamedFunctionItemCallbackMayReceiveForwardedLocalBorrow)
{
    std::string code = R"(
        fn inspect(pointer: ptr<i32>) -> void {
            unsafe { let observed: i32 = *pointer; }
        }
        fn forward_pointer(pointer: ptr<i32>,
                           callback: fn(ptr<i32>) -> void) -> void {
            callback(pointer);
        }
        fn main() -> i32 {
            var value: i32 = 4;
            let callback: fn(ptr<i32>) -> void = inspect;
            forward_pointer(&value, callback);
            value = 8;
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 8);
}

// A known callback that writes through its pointer rejects a shared borrow
// forwarded through the wrapper.
TEST_F(MLATest, NamedFunctionItemCallbackCannotMutateSharedBorrow)
{
    std::string code = R"(
        fn write(pointer: ptr<i32>) -> void {
            unsafe { *pointer = 9; }
        }
        fn forward_pointer(pointer: ptr<i32>,
                           callback: fn(ptr<i32>) -> void) -> void {
            callback(pointer);
        }
        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            let callback: fn(ptr<i32>) -> void = write;
            forward_pointer(shared, callback);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating pointer"),
        std::string::npos)
        << out;
}

// Verifies a match expression cannot hide a local borrow from a callable
// guard.
TEST_F(MLATest, CallablePointerArgumentDetectsBorrowInsideMatchExpression)
{
    std::string code = R"(
        enum Choice : i32 { Borrow = 1, Other = 2 };
        fn main() -> i32 {
            var value: i32 = 4;
            let choice: Choice = Choice::Borrow;
            let callback: fn(ptr<i32>) -> void = |pointer| {};
            callback(match choice {
                Choice::Borrow => &mut value,
                _ => &mut value
            });
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("outside unsafe because the callable may retain it"),
        std::string::npos)
        << out;
}

// Verifies that unsafe callable invocation accepts an exclusive local borrow.
TEST_F(MLATest, CallablePointerArgumentAcceptsUnsafeExclusiveBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let callback: fn(ptr<i32>) -> void = |pointer| {
                unsafe { *pointer = 9; }
            };
            unsafe { callback(&mut value); }
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

// Verifies nested closures do not treat local callable values as checked
// parameters.
TEST_F(MLATest, NestedClosureLocalCallableRequiresUnsafeForLocalBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let outer = || {
                let callback: fn(ptr<i32>) -> void = |pointer| {};
                callback(&mut value);
            };
            outer();
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("outside unsafe because the callable may retain it"),
        std::string::npos)
        << out;
}

// Verifies a generic callback cannot forward a local borrow to an opaque
// function.
TEST_F(MLATest, GenericCallbackCannotForwardLocalBorrowToExternFunction)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        generic<T> fn forward(callback: fn(ptr<T>) -> void,
                              pointer: ptr<T>) -> void {
            callback(pointer);
        }

        fn main() -> i32 {
            var value: i32 = 4;
            forward(retain_pointer, &mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc, "-c");
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// Verifies unsafe explicitly permits forwarding a local borrow to an opaque
// callback.
TEST_F(MLATest, GenericCallbackCanForwardLocalBorrowToExternFunctionInUnsafe)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        generic<T> fn forward(callback: fn(ptr<T>) -> void,
                              pointer: ptr<T>) -> void {
            callback(pointer);
        }

        fn main() -> i32 {
            var value: i32 = 4;
            unsafe { forward(retain_pointer, &mut value); }
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    compileCapture(rc, "-c");
    EXPECT_EQ(rc, 0);
}

// Verifies that a read-only same-named method is not confused with a writer.
TEST_F(MLATest, SharedBorrowCanReachReadOnlyPointerMethod)
{
    std::string code = R"(
        struct Reader { var marker: i32; };
        struct Writer { var marker: i32; };
        impl Reader {
            fn access(&self, pointer: ptr<i32>) -> i32 {
                unsafe { return *pointer; }
            }
        }
        impl Writer {
            fn access(&self, pointer: ptr<i32>) -> i32 {
                unsafe { *pointer = 9; }
                return 0;
            }
        }

        fn read_through_method(pointer: ptr<i32>) -> i32 {
            let reader = Reader { marker: 0 };
            return reader.access(pointer);
        }

        fn main() -> i32 {
            var value: i32 = 4;
            let shared: ptr<i32> = &value;
            return read_through_method(shared);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0) << out;
}

// Verifies that a live exclusive child reborrow blocks writes through parent.
TEST_F(MLATest, ExclusiveReborrowPreventsAccessThroughParent)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let parent: ptr<i32> = &mut value;
            let child: ptr<i32> = &mut *parent;
            unsafe { *parent = 8; }
            unsafe { *child = 12; }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'child'"), std::string::npos) << out;
}

// Verifies that the parent borrow remains active after its child expires.
TEST_F(MLATest, ExclusiveReborrowRestoresParentBorrowAfterChildLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let parent: ptr<i32> = &mut value;
            let child: ptr<i32> = &mut *parent;
            unsafe { *child = 12; }
            value = 20;
            unsafe { return *parent; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while mutably borrowed by 'parent'"), std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipBorrowEndsWhenPointerGoesOutOfScope)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            if 1: {
                let p: ptr<Post> = &a;
            }
            let moved: Post = a;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipMutableBorrowEndsBeforeOwnerMove)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            var a: Post = Post { content: [1] };
            let p: ptr<Post> = &mut a;
            unsafe { println!("{}", (*p).content[0]); }
            let moved: Post = a;
            return moved.content[0] - 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipSharedBorrowEndsAfterLastUseBeforeOwnerMove)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let p: ptr<Post> = &a;
            unsafe { println!("{}", (*p).content[0]); }
            let moved: Post = a;
            return moved.content[0] - 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipLoopLocalBorrowEndsAfterLastUseInWhileBody)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var box: Box = Box { value: 1 };
            var index: i32 = 0;
            while index < 1 {
                let p: ptr<Box> = &box;
                let observed: i32 = (*p).value;
                box.value = observed + 1;
                index += 1;
            }
            return box.value - 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipLoopLocalBorrowEndsAfterLastUseInRangeBody)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var box: Box = Box { value: 1 };
            for index in 0..1 {
                let p: ptr<Box> = &box;
                let observed: i32 = (*p).value;
                box.value = observed + 1;
            }
            return box.value - 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipLoopLocalBorrowRemainsLiveUntilLastUse)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var box: Box = Box { value: 1 };
            for index in 0..1 {
                let p: ptr<Box> = &box;
                box.value = 2;
                let observed: i32 = (*p).value;
                if observed == 0 { return 1; }
            }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
}

TEST_F(MLATest, OwnershipLoopLocalBorrowExpiresAcrossIteratorKinds)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };
        enum Direction { North, South };

        fn main() -> i32 {
            var box: Box = Box { value: 0 };
            for item in [0] {
                let p: ptr<Box> = &box;
                let observed: i32 = (*p).value;
                box.value = observed + 1;
            }
            let items: list<i32> = [0];
            for item in items {
                let p: ptr<Box> = &box;
                let observed: i32 = (*p).value;
                box.value = observed + 1;
            }
            for direction in Direction {
                let p: ptr<Box> = &box;
                let observed: i32 = (*p).value;
                box.value = observed + 1;
            }
            let values: map<str8, i32> = {"key": 0};
            for value in values.values() {
                let p: ptr<Box> = &box;
                let observed: i32 = (*p).value;
                box.value = observed + 1;
            }
            return box.value - 5;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ClosureCaptureBorrowExpiresAfterLastUseInLoopBody)
{
    std::string code = R"(
        fn main() -> i32 {
            var index: i32 = 0;
            var total: i32 = 0;
            while index < 1 {
                let bump = || { total += 1; };
                bump();
                total += 1;
                index += 1;
            }
            return total - 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ClosureCaptureBorrowRemainsLiveThroughLoopLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var index: i32 = 0;
            var total: i32 = 0;
            while index < 1 {
                let bump = || { total += 1; };
                total += 1;
                bump();
                index += 1;
            }
            return total;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'bump'"), std::string::npos);
}

TEST_F(MLATest, OwnershipOuterSharedPointerBorrowEndsAfterLastUse)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let b: Post = Post { content: [1] };
            let p: ptr<Post> = &a;
            if 1: {
                let p: ptr<Post> = &b;
            }
            let moved: Post = a;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipLoopLocalPointerBorrowDoesNotLeakAfterLoop)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            for i in 0..1 {
                let p: ptr<Post> = &a;
            }
            let moved: Post = a;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipEmptyRangeLoopDoesNotMoveOuterValue)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 7;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            for i in 0..0 {
                let b: Post = a;
            }
            return take_post(a);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, OwnershipNonEmptyRangeLoopStillMovesOuterValue)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            for i in 0..1 {
                let b: Post = a;
            }
            return take_post(a);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value"), std::string::npos);
}

TEST_F(MLATest, OwnershipOuterSharedPointerBorrowEndsAfterLastUseAcrossLoop)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn main() -> i32 {
            let a: Post = Post { content: [1] };
            let b: Post = Post { content: [1] };
            let p: ptr<Post> = &a;
            for i in 0..1 {
                let q: ptr<Post> = &b;
            }
            let moved: Post = a;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCannotReturnPointerBorrowingLocal)
{
    std::string code = R"(
        fn bad_ptr() -> ptr<i32> {
            let x: i32 = 7;
            return &x;
        }

        fn main() -> i32 {
            let p: ptr<i32> = bad_ptr();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos);
}

// A closure call must not return a pointer borrowed from its captured local.
TEST_F(MLATest, OwnershipCannotReturnPointerFromBorrowingClosureCall)
{
    std::string code = R"(
        fn leak() -> ptr<i32> {
            var local_value: i32 = 7;
            let callback: fn() -> ptr<i32> = || { return &local_value; };
            return callback();
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = leak();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot return"), std::string::npos) << out;
}

// A forwarding helper must not hide a pointer to storage local to the
// returning function.
TEST_F(MLATest, OwnershipCannotReturnLocalPointerThroughForwardingFunction)
{
    std::string code = R"(
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            return pointer;
        }
        fn leak() -> ptr<i32> {
            var local_value: i32 = 7;
            return relay(&local_value);
        }
        fn main() -> i32 {
            let pointer = leak();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos)
        << out;
}

// A forwarding call with a proven global result does not inherit unrelated
// borrows.
TEST_F(MLATest, OwnershipGlobalPointerReturnDoesNotBorrowUnrelatedArgument)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn get_global(ignored: ptr<i32>) -> ptr<i32> {
            return &global_value;
        }
        fn relay_global(pointer: ptr<i32>) -> ptr<i32> {
            return get_global(pointer);
        }
        fn main() -> i32 {
            var local_value: i32 = 7;
            let global_pointer = relay_global(&local_value);
            local_value = 9;
            unsafe { return *global_pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 11);
}

// A closure must not return a borrowed pointer hidden in a captured aggregate.
TEST_F(MLATest, OwnershipCannotReturnPointerFromClosureCapturedAggregate)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn leak() -> ptr<i32> {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &local_value };
            let callback: fn() -> ptr<i32> = || { return boxed.value; };
            return callback();
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = leak();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot return"), std::string::npos) << out;
}

// A closure-returned field pointer must borrow its owner through the local
// alias.
TEST_F(MLATest, OwnershipClosureReturnedPointerKeepsAggregateBorrowLive)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn main() -> i32 {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &local_value };
            let callback: fn() -> ptr<i32> = || { return boxed.value; };
            let pointer: ptr<i32> = callback();
            local_value = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// A closure-returned pointer borrow ends after that pointer's final use.
TEST_F(MLATest, OwnershipClosureReturnedPointerBorrowEndsAfterLastUse)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn main() -> i32 {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &local_value };
            let callback: fn() -> ptr<i32> = || { return boxed.value; };
            let pointer: ptr<i32> = callback();
            var observed: i32 = 0;
            unsafe { observed = *pointer; }
            local_value = 9;
            return observed + local_value - 16;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// An opaque callback parameter cannot return an untracked pointer past its
// call.
TEST_F(MLATest, OwnershipCannotReturnPointerFromCallableParameter)
{
    std::string code = R"(
        fn relay(callback: fn() -> ptr<i32>) -> ptr<i32> {
            return callback();
        }
        fn main() -> i32 {
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("capture lifetime is unknown"), std::string::npos)
        << out;
}

// A closure may still return a global pointer while capturing unrelated state.
TEST_F(MLATest, OwnershipCanReturnGlobalPointerFromBorrowingClosure)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn get_global_pointer() -> ptr<i32> {
            var local_value: i32 = 7;
            let callback: fn() -> ptr<i32> = || {
                if local_value > 0 { return &global_value; }
                return &global_value;
            };
            return callback();
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = get_global_pointer();
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 11);
}

// A method must preserve the borrow stored in its receiver's pointer field.
TEST_F(MLATest, OwnershipCannotReturnPointerFromBorrowingAggregateMethod)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn get(self: PointerBox) -> ptr<i32> { return self.value; }
        }
        fn leak() -> ptr<i32> {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &local_value };
            return boxed.get();
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = leak();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos)
        << out;
}

// A method-returned field pointer must retain the receiver's borrow locally.
TEST_F(MLATest, OwnershipMethodReturnedPointerKeepsReceiverBorrowLive)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn get(self: PointerBox) -> ptr<i32> { return self.value; }
        }
        fn main() -> i32 {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &local_value };
            let pointer: ptr<i32> = boxed.get();
            local_value = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// A method-returned field borrow ends after the result pointer's final use.
TEST_F(MLATest, OwnershipMethodReturnedPointerBorrowEndsAfterLastUse)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn get(self: PointerBox) -> ptr<i32> { return self.value; }
        }
        fn main() -> i32 {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &local_value };
            let pointer: ptr<i32> = boxed.get();
            var observed: i32 = 0;
            unsafe { observed = *pointer; }
            local_value = 9;
            return observed + local_value - 16;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A method-returned pointer parameter must retain the call argument's borrow.
TEST_F(MLATest, OwnershipMethodReturnedArgumentPointerKeepsBorrowLive)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn forward(self: PointerBox, pointer: ptr<i32>) -> ptr<i32> {
                return pointer;
            }
        }
        fn main() -> i32 {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox {};
            let pointer: ptr<i32> = boxed.forward(&local_value);
            local_value = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// A method-forwarded argument borrow ends after the caller's final pointer use.
TEST_F(MLATest, OwnershipMethodReturnedArgumentBorrowEndsAfterLastUse)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn forward(self: PointerBox, pointer: ptr<i32>) -> ptr<i32> {
                return pointer;
            }
        }
        fn main() -> i32 {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox {};
            let pointer: ptr<i32> = boxed.forward(&local_value);
            var observed: i32 = 0;
            unsafe { observed = *pointer; }
            local_value = 9;
            return observed + local_value - 16;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A method call must not hide a local pointer when its enclosing function
// returns.
TEST_F(MLATest, OwnershipCannotReturnLocalPointerThroughMethodArgument)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn forward(self: PointerBox, pointer: ptr<i32>) -> ptr<i32> {
                return pointer;
            }
        }
        fn leak() -> ptr<i32> {
            var local_value: i32 = 7;
            let boxed: PointerBox = PointerBox {};
            return boxed.forward(&local_value);
        }
        fn main() -> i32 {
            let pointer = leak();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos)
        << out;
}

// Returning a pointer indexed from a local list must retain its borrow origin.
TEST_F(MLATest, OwnershipCannotReturnPointerIndexedFromBorrowingList)
{
    std::string code = R"(
        fn leak() -> ptr<i32> {
            var local_value: i32 = 7;
            let pointers = [&local_value];
            return pointers[0];
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = leak();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot return"), std::string::npos) << out;
}

// Returning an unrelated global pointer field must remain allowed.
TEST_F(MLATest, OwnershipCanReturnUnborrowedPointerFromAggregateMethod)
{
    std::string code = R"(
        struct PointerPair {
            var local_pointer: ptr<i32>;
            var global_pointer: ptr<i32>;
        };
        impl PointerPair {
            fn get_global(self: PointerPair) -> ptr<i32> {
                return self.global_pointer;
            }
        }
        var global_value: i32 = 11;
        fn get_global_pointer() -> ptr<i32> {
            var local_value: i32 = 7;
            let pair: PointerPair = PointerPair {
                local_pointer: &local_value,
                global_pointer: &global_value
            };
            return pair.get_global();
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = get_global_pointer();
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 11);
}

TEST_F(MLATest, ClosureCannotReturnBorrowingLocalCapture)
{
    std::string code = R"(
        fn bad_closure() -> ptr<void> {
            var count: i32 = 0;
            let bump = || { count += 1; };
            return bump;
        }
        fn main() -> i32 { return 0; }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot return closure 'bump'"), std::string::npos);
    EXPECT_NE(out.find("borrows local variable 'count'"), std::string::npos);
}

TEST_F(MLATest, ClosureBorrowBlocksMutationUntilLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var count: i32 = 0;
            let bump = || { count += 1; };
            count = 5;
            bump();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'count' while borrowed by 'bump'"),
        std::string::npos);
}

// A closure's pointer reassignment must transfer the resulting borrow to the
// pointer.
TEST_F(MLATest, ClosurePointerReassignmentPreservesBorrowAfterCall)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            let bind = || { pointer = &mut value; };
            bind();
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A closure-produced mutable pointer remains usable through its new borrow.
TEST_F(MLATest, ClosurePointerReassignmentAllowsPointerUse)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            let bind = || { pointer = &mut value; };
            bind();
            return *pointer;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

// A later straight-line pointer assignment replaces the earlier borrow origin.
TEST_F(MLATest, ClosurePointerReassignmentReleasesOverwrittenBorrow)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var earlier: i32 = 7;
            var later: i32 = 5;
            var pointer: ptr<i32> = &mut outside;
            let bind = || {
                pointer = &mut earlier;
                pointer = &mut later;
            };
            bind();
            earlier = 9;
            return *pointer;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

// An if branch without an else must retain the pointer's incoming borrow path.
TEST_F(MLATest, ClosurePointerReassignmentPreservesUnchangedBranchBorrow)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var original: i32 = 7;
            var alternate: i32 = 5;
            var pointer: ptr<i32> = &mut original;
            var flag: bool = true;
            let maybe_rebind = || {
                if flag {
                    pointer = &mut alternate;
                }
            };
            maybe_rebind();
            original = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'original' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A return from one branch must keep that branch's final pointer origin.
TEST_F(MLATest, ClosurePointerReassignmentKeepsOriginOnEarlyReturn)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var alternate: i32 = 5;
            var later: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            let bind = || {
                if flag {
                    pointer = &mut alternate;
                    return;
                }
                pointer = &mut later;
            };
            bind();
            alternate = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'alternate' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A break must stop analysis of the rest of its loop-body path.
TEST_F(MLATest, ClosurePointerReassignmentKeepsOriginBeforeBreak)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var alternate: i32 = 5;
            var later: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            let bind = || {
                while flag {
                    pointer = &mut alternate;
                    break;
                    pointer = &mut later;
                }
            };
            bind();
            alternate = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'alternate' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A continue must stop the rest of the loop body and retain its pointer origin.
TEST_F(MLATest, ClosurePointerReassignmentKeepsOriginBeforeContinue)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var alternate: i32 = 5;
            var later: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            let bind = || {
                while flag {
                    pointer = &mut alternate;
                    flag = false;
                    continue;
                    pointer = &mut later;
                }
            };
            bind();
            alternate = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'alternate' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// Pointer origins from every closure branch must survive the runtime call.
TEST_F(MLATest, ClosurePointerReassignmentMergesBranches)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var other: i32 = 5;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            let bind = || {
                if flag {
                    pointer = &mut value;
                } else {
                    pointer = &mut other;
                }
            };
            bind();
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// Multiple independent branches must retain each possible final pointer origin.
TEST_F(MLATest, ClosurePointerReassignmentMergesSequentialBranches)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var third: i32 = 3;
            var pointer: ptr<i32> = &mut outside;
            var choose_first: bool = true;
            var choose_second: bool = false;
            var choose_third: bool = false;
            let bind = || {
                if choose_first {
                    pointer = &mut first;
                }
                if choose_second {
                    pointer = &mut second;
                }
                if choose_third {
                    pointer = &mut third;
                }
            };
            bind();
            first = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'first' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A throw terminates the path but retains pointer assignments made before it.
TEST_F(MLATest, ClosurePointerReassignmentKeepsOriginOnThrow)
{
    std::string code = R"(
        struct BorrowError {
            var type_name: str8;
            var message: str8;
            var source_line: i32;
        };

        var outside: i32 = 3;

        fn main() -> i32 {
            var alternate: i32 = 5;
            var later: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            let bind = || {
                pointer = &mut alternate;
                throw BorrowError {
                    type_name: "BorrowError",
                    message: "test",
                    source_line: 1
                };
                pointer = &mut later;
            };
            try {
                bind();
            } catch error: BorrowError {
                println!("caught");
            }
            alternate = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'alternate' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A copied runtime closure must propagate pointer-reassignment borrow effects.
TEST_F(MLATest, CopiedClosurePointerReassignmentKeepsBorrow)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            let bind = || { pointer = &mut value; };
            let copied = bind;
            copied();
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ClosureBorrowCannotEscapeThroughFunctionArgument)
{
    std::string code = R"(
        fn store_callback(callback: ptr<void>) -> ptr<void> {
            return callback;
        }
        fn main() -> i32 {
            var count: i32 = 0;
            let bump = || { count += 1; };
            store_callback(bump);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot pass closure 'bump'"), std::string::npos);
    EXPECT_NE(out.find("borrows local variable 'count'"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosureAcceptsTypedArguments)
{
    std::string code = R"(
        fn main() -> i32 {
            var count: i32 = 2;
            let add = |amount: i32| { count += amount; };
            add(5);
            return count;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, RuntimeClosurePreservesMutableReferenceParameterSemantics)
{
    std::string code = R"(
        fn main() -> i32 {
            let bump: fn(&mut i32) -> void = |value| {
                value += 1;
            };
            var count: i32 = 2;
            bump(&mut count);
            return count;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 3);
}

TEST_F(MLATest, RuntimeClosureWritesBackMutableReferenceBeforeReturn)
{
    std::string code = R"(
        fn main() -> i32 {
            let bump: fn(&mut i32) -> i32 = |value| {
                value += 1;
                return value;
            };
            var count: i32 = 2;
            let result: i32 = bump(&mut count);
            return count + result;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

TEST_F(MLATest, RuntimeClosureWritesBackMutableStructReference)
{
    std::string code = R"(
        struct Counter {
            var value: i32;
        };
        fn main() -> i32 {
            let increment: fn(&mut Counter) -> void = |counter| {
                counter.value += 1;
            };
            var counter: Counter = Counter { value: 4 };
            increment(&mut counter);
            return counter.value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

TEST_F(MLATest, RuntimeClosureWritesBackMutableFieldReference)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };
        fn main() -> i32 {
            let increment: fn(&mut i32) -> void = |value| {
                value += 1;
            };
            var pair: Pair = Pair { left: 4, right: 9 };
            increment(&mut pair.left);
            return pair.left + pair.right;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 14);
}

TEST_F(MLATest, RuntimeClosureAllowsDisjointMutableFieldReferences)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };
        fn main() -> i32 {
            let increment_both: fn(&mut i32, &mut i32) -> void = |left, right| {
                left += 1;
                right += 1;
            };
            var pair: Pair = Pair { left: 4, right: 9 };
            increment_both(&mut pair.left, &mut pair.right);
            return pair.left + pair.right;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 15);
}

TEST_F(MLATest, RuntimeClosureRejectsAliasedMutableReferenceArguments)
{
    std::string code = R"(
        fn main() -> i32 {
            let update_both: fn(&mut i32, &mut i32) -> void = |left, right| {
                left += 1;
                right += 1;
            };
            var value: i32 = 0;
            update_both(&mut value, &mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot borrow"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosureRejectsSharedAndMutableAliasing)
{
    std::string code = R"(
        fn main() -> i32 {
            let update: fn(&i32, &mut i32) -> void = |read, write| {
                write += read;
            };
            var value: i32 = 1;
            update(&value, &mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot borrow"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosureRespectsExistingSharedBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            let held = &value;
            let update: fn(&mut i32) -> void = |item| { item = 9; };
            update(&mut value);
            unsafe { return *held; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot borrow 'value' as mutable"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosureRespectsExistingMutableBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            let held = &mut value;
            let read: fn(&i32) -> i32 = |item| { return item; };
            let result: i32 = read(&value);
            unsafe { return *held + result; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'value' as immutable"),
        std::string::npos);
}

TEST_F(MLATest, RuntimeClosureRequiresExplicitMutableReferenceArgument)
{
    std::string code = R"(
        fn main() -> i32 {
            let update: fn(&mut i32) -> void = |value| { value += 1; };
            var count: i32 = 0;
            update(count);
            return count;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("expects &mut argument"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosureReadsImmutableReferenceParameter)
{
    std::string code = R"(
        fn main() -> i32 {
            let increment: fn(&i32) -> i32 = |value| {
                return value + 1;
            };
            let count: i32 = 4;
            return increment(&count);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

TEST_F(MLATest, FunctionAcceptsExplicitImmutableReferenceArgument)
{
    std::string code = R"(
        fn increment(value: &i32) -> i32 { return value + 1; }
        fn main() -> i32 {
            let count: i32 = 4;
            return increment(&count);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

TEST_F(MLATest, FunctionAllowsAliasedImmutableReferenceArguments)
{
    std::string code = R"(
        fn sum(left: &i32, right: &i32) -> i32 { return left + right; }
        fn main() -> i32 {
            let value: i32 = 6;
            return sum(&value, &value);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, OverloadedFunctionAcceptsAliasedImmutableReferences)
{
    std::string code = R"(
        fn sum(left: &i32, right: &i32) -> i32 { return left + right; }
        fn sum(left: i64, right: i64) -> i32 {
            return 0;
        }
        fn main() -> i32 {
            let value: i32 = 6;
            return sum(&value, &value);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, RuntimeClosureAllowsAliasedImmutableReferences)
{
    std::string code = R"(
        fn main() -> i32 {
            let sum: fn(&i32, &i32) -> i32 = |left, right| {
                return left + right;
            };
            let value: i32 = 6;
            return sum(&value, &value);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, RuntimeClosureAllowsSharedBorrowWhileAnotherIsLive)
{
    std::string code = R"(
        fn main() -> i32 {
            let value: i32 = 6;
            let held = &value;
            let read: fn(&i32) -> i32 = |item| { return item; };
            let current: i32 = read(&value);
            unsafe { return *held + current; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, FunctionAllowsSharedBorrowWhileAnotherIsLive)
{
    std::string code = R"(
        fn read(item: &i32) -> i32 { return item; }
        fn main() -> i32 {
            let value: i32 = 6;
            let held = &value;
            let current: i32 = read(&value);
            unsafe { return *held + current; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, FunctionRejectsSharedAndMutableAliasing)
{
    std::string code = R"(
        fn update(read: &i32, write: &mut i32) -> void { write = read; }
        fn main() -> i32 {
            var value: i32 = 1;
            update(&value, &mut value);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot borrow"), std::string::npos);
}

TEST_F(MLATest, NestedMutableReferenceDoesNotAcceptSingleBorrow)
{
    writeSource(R"(
        fn set(value: &mut &mut i32) -> void { value = 42; }
        fn main() -> i32 {
            var value: i32 = 1;
            set(&mut value);
            return 0;
        }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("no matching overload for function 'set'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, NestedMutableReferenceDemoMutatesIntegerAndStruct)
{
    const fs::path example = fs::path(__FILE__).parent_path().parent_path() /
                             "examples/nested_mutable_references_demo.mla";
    std::ifstream input(example);
    ASSERT_TRUE(input.is_open()) << example;
    std::ostringstream source;
    source << input.rdbuf();
    writeSource(source.str());
    for(const std::string options : {"-O0", "-O2"})
    {
        SCOPED_TRACE(options);
        ASSERT_TRUE(compile(true, options));
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, NestedMutableReferenceLocalsAndForwarding)
{
    writeSource(R"(
        fn set(reference: &mut &mut i32) -> void { *reference = 42; }
        fn forward(reference: &mut &mut i32) -> void { set(&mut reference); }
        fn set_deep(reference: &mut &mut &mut i32) -> void { *(*reference) = 43; }
        fn main() -> i32 {
            var value: i32 = 1;
            var inner: &mut i32 = &mut value;
            forward(&mut inner);
            var outer: &mut &mut i32 = &mut inner;
            set_deep(&mut outer);
            *(*outer) = 44;
            return value == 44 ? 0 : 1;
        }
    )");
    for(const std::string options : {"-O0", "-O2"})
    {
        SCOPED_TRACE(options);
        ASSERT_TRUE(compile(true, options));
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, NestedMutableReferenceGenericFunction)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        generic<T>
        fn replace_value(reference: &mut &mut T, value: T) -> void {
            *reference = value;
        }
        struct Counter { var value: i32; };
        fn main() -> i32 {
            var number: i32 = 1;
            var number_reference: &mut i32 = &mut number;
            let replacement: i32 = 42;
            replace_value(&mut number_reference, replacement);
            if number != 42 { return 1; }
            var counter = Counter { value: 1 };
            var counter_reference: &mut Counter = &mut counter;
            replace_value(&mut counter_reference, Counter { value: 42 });
            return counter.value == 42 ? 0 : 2;
        }
    )"),
        0);
}

TEST_F(MLATest, NestedReferencesAllowReadingSharedLayers)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn read_shared_inner(reference: &mut &i32) -> i32 { return *reference; }
        fn read_shared_outer(reference: & &mut i32) -> i32 { return *reference; }
        fn main() -> i32 {
            var value: i32 = 42;
            var shared: &i32 = &value;
            if read_shared_inner(&mut shared) != 42 { return 1; }
            var exclusive: &mut i32 = &mut value;
            if read_shared_outer(&exclusive) != 42 { return 2; }
            if read_shared_outer(&mut exclusive) != 42 { return 3; }
            return 0;
        }
    )"),
        0);
}

TEST_F(MLATest, NestedMutableReferenceRejectsSharedInnerReference)
{
    writeSource(R"(
        fn set(reference: &mut &i32) -> void { *reference = 42; }
        fn main() -> i32 {
            var value: i32 = 1;
            var inner: &i32 = &value;
            set(&mut inner);
            return 0;
        }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("cannot write through shared reference"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, NestedMutableReferenceRejectsSharedOuterReference)
{
    writeSource(R"(
        fn set(reference: & &mut i32) -> void { *reference = 42; }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("cannot write through shared reference"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, NestedMutableReferenceRejectsWrongPointee)
{
    writeSource(R"(
        struct Counter { var value: i32; };
        fn set(reference: &mut &mut Counter) -> void { (*reference).value = 42; }
        fn main() -> i32 {
            var value: i32 = 1;
            var inner: &mut i32 = &mut value;
            set(&mut inner);
            return 0;
        }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1) << out;
}

TEST_F(MLATest, NestedMutableReferenceKeepsOriginalBorrowLive)
{
    writeSource(R"(
        fn set(reference: &mut &mut i32) -> void { *reference = 42; }
        fn main() -> i32 {
            var value: i32 = 1;
            var inner: &mut i32 = &mut value;
            var outer: &mut &mut i32 = &mut inner;
            value = 2;
            set(&mut *outer);
            return 0;
        }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("cannot assign to 'value'"), std::string::npos) << out;
}

TEST_F(MLATest, NestedMutableReferenceRejectsRebindingParameterToLocal)
{
    writeSource(R"(
        fn replace(reference: &mut &mut i32) -> void {
            var local: i32 = 42;
            reference = &mut local;
        }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("rebinding a nested reference parameter"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ReferenceBindingRejectsMissingOrMismatchedInitializer)
{
    for(const std::string declaration :
        {"var reference: &mut i32;",
         "var reference: &mut i32 = 42;",
         "var reference: &mut i32 = &value;",
         "var reference: &mut i64 = &mut value;",
         "static var reference: &mut i32 = &mut value;"})
    {
        SCOPED_TRACE(declaration);
        writeSource(
            "fn main() -> i32 { var value: i32 = 1; " + declaration +
            " return 0; }");
        int rc = 0;
        const std::string out = compileCapture(rc);
        EXPECT_EQ(rc, 1);
        EXPECT_EQ(out.find("Segmentation fault"), std::string::npos) << out;
        EXPECT_NE(out.find("reference"), std::string::npos) << out;
    }
}

TEST_F(MLATest, NestedMutableReferenceRejectsReplacingBorrowedSlot)
{
    writeSource(R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var inner: &mut i32 = &mut first;
            var outer: &mut &mut i32 = &mut inner;
            *outer = &mut second;
            return 0;
        }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(
        out.find("replacing a reference through a borrowed slot"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, StoredReferenceRejectsEscapingReturn)
{
    writeSource(R"(
        fn dangling() -> &mut i32 {
            var value: i32 = 1;
            let reference: &mut i32 = &mut value;
            return reference;
        }
        fn main() -> i32 { return 0; }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("cannot return a stored reference"), std::string::npos)
        << out;
}

TEST_F(MLATest, StoredReferenceRejectsDuplicatingMutableBorrow)
{
    writeSource(R"(
        fn main() -> i32 {
            var value: i32 = 1;
            var reference: &mut i32 = &mut value;
            let copy = reference;
            *reference = 2;
            unsafe { *copy = 3; }
            return value;
        }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("cannot alias mutable borrow"), std::string::npos)
        << out;
}

TEST_F(MLATest, StoredReferenceSlotCannotEscapeThroughRawPointerParameter)
{
    writeSource(R"(
        fn retain(slot: ptr<ptr<i32>>) -> void { return; }
        fn main() -> i32 {
            var value: i32 = 1;
            var reference: &mut i32 = &mut value;
            retain(&mut reference);
            return 0;
        }
    )");
    int rc = 0;
    const std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 1);
    EXPECT_NE(out.find("callee may retain it"), std::string::npos) << out;
}

TEST_F(MLATest, SharedReferenceReturnsKeepValueABI)
{
    EXPECT_EQ(
        compileAndRunExitCode(R"(
        fn identity(value: &i32) -> &i32 { return value; }
        fn main() -> i32 {
            let value: i32 = 42;
            let result = identity(&value);
            return result == 42 ? 0 : 1;
        }
    )"),
        0);
}

TEST_F(MLATest, NestedPointerDemoReplacesInnerPointer)
{
    const fs::path example = fs::path(__FILE__).parent_path().parent_path() /
                             "examples/nested_pointer_demo.mla";
    std::ifstream input(example);
    ASSERT_TRUE(input.is_open()) << example;
    std::ostringstream source;
    source << input.rdbuf();
    writeSource(source.str());
    for(const std::string options : {"-O0", "-O2"})
    {
        SCOPED_TRACE(options);
        ASSERT_TRUE(compile(true, options));
        EXPECT_EQ(runExitCode(), 0);
    }
}

TEST_F(MLATest, FunctionRejectsMutableBorrowWhileSharedBorrowIsLive)
{
    std::string code = R"(
        fn update(value: &mut i32) -> void { value = 9; }
        fn main() -> i32 {
            var value: i32 = 1;
            let held = &value;
            update(&mut value);
            unsafe { return *held; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot borrow"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosureRejectsMutationThroughImmutableReference)
{
    std::string code = R"(
        fn main() -> i32 {
            let mutate: fn(&i32) -> void = |value| {
                value += 1;
            };
            let count: i32 = 4;
            mutate(count);
            return count;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign"), std::string::npos);
}

TEST_F(MLATest, FunctionRejectsFieldMutationThroughImmutableReference)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        fn mutate(counter: &Counter) -> void { counter.value = 9; }
        fn main() -> i32 {
            var counter: Counter = Counter { value: 1 };
            mutate(&counter);
            return counter.value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate through immutable reference"),
        std::string::npos);
}

TEST_F(MLATest, RuntimeClosureRejectsFieldMutationThroughImmutableReference)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        fn main() -> i32 {
            let mutate: fn(&Counter) -> void = |counter| {
                counter.value = 9;
            };
            var counter: Counter = Counter { value: 1 };
            mutate(counter);
            return counter.value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate through immutable reference"),
        std::string::npos);
}

TEST_F(MLATest, ImmutableMethodReceiverRejectsFieldMutation)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        impl Counter {
            fn mutate(&self) -> void { self.value = 9; }
        }
        fn main() -> i32 {
            var counter: Counter = Counter { value: 1 };
            counter.mutate();
            return counter.value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate through immutable reference"),
        std::string::npos);
}

TEST_F(MLATest, ImmutableMethodReceiverRejectsContainerMutation)
{
    std::string code = R"(
        struct Counter { var values: list<i32>; };
        impl Counter {
            fn mutate(&self) -> void { self.values.push(9); }
        }
        fn main() -> i32 {
            var counter: Counter = Counter { values: [1] };
            counter.mutate();
            return counter.values.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot mutate through immutable reference"),
        std::string::npos);
}

TEST_F(MLATest, ImmutableMethodReceiverRejectsNestedMutableMethodCall)
{
    std::string code = R"(
        struct Inner { var value: i32; };
        impl Inner {
            fn mutate(self: &mut Inner) -> void { self.value = 9; }
        }
        struct Outer { var inner: Inner; };
        impl Outer {
            fn mutate(&self) -> void { self.inner.mutate(); }
        }
        fn main() -> i32 {
            var outer: Outer = Outer { inner: Inner { value: 1 } };
            outer.mutate();
            return outer.inner.value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot call mutating method through immutable "
            "reference"),
        std::string::npos);
}

TEST_F(MLATest, MutableMethodReceiverCanMutateVariable)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        impl Counter {
            fn set(self: &mut Counter, value: i32) -> void {
                self.value = value;
            }
        }
        fn main() -> i32 {
            var counter: Counter = Counter { value: 1 };
            counter.set(9);
            return counter.value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, SharedMethodReceiverAllowsExistingSharedBorrow)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        impl Counter {
            fn get(&self) -> i32 { return self.value; }
        }
        fn main() -> i32 {
            var counter: Counter = Counter { value: 9 };
            let held = &counter.value;
            unsafe { return counter.get() + *held; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 18);
}

TEST_F(MLATest, MutableMethodReceiverRejectsExistingSharedBorrow)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        impl Counter {
            fn set(self: &mut Counter, value: i32) -> void {
                self.value = value;
            }
        }
        fn main() -> i32 {
            var counter: Counter = Counter { value: 1 };
            let held = &counter.value;
            counter.set(9);
            unsafe { return *held; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosurePreservesFixedArrayParameterMetadata)
{
    std::string code = R"(
        fn main() -> i32 {
            let second: fn(array<i32, 2>) -> i32 = |items| {
                return items.len() + items[1];
            };
            let values: array<i32, 2> = {4, 10};
            return second(values);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, RuntimeClosurePreservesMutableMultiarrayParameterMetadata)
{
    std::string code = R"(
        fn main() -> i32 {
            let update: fn(mutmultiarray<i32, 1, 2>) -> i32 = |matrix| {
                matrix[0][1] = 9;
                return matrix[0][1];
            };
            let matrix: mutmultiarray<i32, 1, 2> = {{1, 2}};
            return update(matrix) - 9;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CallableLocalTypeInfersInlineClosureParameter)
{
    std::string code = R"(
        fn main() -> i32 {
            let add_one: fn(i32) -> i32 = |value| { return value + 1; };
            let constant_one: fn(i32) -> i32 = |value| { return 1; };
            return add_one(8) + constant_one(0);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 10);
}

TEST_F(MLATest, ConcreteCallbackSignatureContextualizesInlineClosure)
{
    std::string code = R"(
        fn invoke(callback: fn(i32) -> i32) -> i32 {
            return callback(0);
        }
        fn main() -> i32 {
            return invoke(|value| { return 7; });
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, RuntimeClosureCanBeCopiedWithinBorrowScope)
{
    std::string code = R"(
        fn main() -> i32 {
            var count: i32 = 0;
            let bump = || { count += 1; };
            let another = bump;
            another();
            return count;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 1);
}

TEST_F(MLATest, RuntimeClosureReturnsValueAndBorrowsCapture)
{
    std::string code = R"(
        fn main() -> i32 {
            let base: i32 = 7;
            let add_base = |value: i32| { return value + base; };
            return add_base(5);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, RuntimeClosureCaptureBorrowEndsAfterLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 4;
            let read = || { return value; };
            let captured: i32 = read();
            value = 5;
            return captured + value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, RuntimeClosurePointerCaptureKeepsBorrowedOwnerLive)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn inspect(items: ptr<list<i32>>) -> i32 { return 0; }

        fn main() -> i32 {
            let holder: Holder = Holder { items: [1, 2] };
            let pointer = &holder.items;
            let read = || { return inspect(pointer); };
            let moved = consume(holder.items);
            return read() + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosurePointerCaptureKeepsFieldSensitiveBorrow)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var spare: list<i32>;
        };
        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn inspect(items: ptr<list<i32>>) -> i32 { return 0; }

        fn main() -> i32 {
            let holder: Holder = Holder { items: [1, 2], spare: [3] };
            let pointer = &holder.items;
            let read = || { return inspect(pointer); };
            let moved = consume(holder.spare);
            return read() + moved;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 1);
}

TEST_F(MLATest, RuntimeClosurePointerCaptureBorrowSurvivesBlockScope)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn inspect(items: ptr<list<i32>>) -> i32 { return 0; }

        fn main() -> i32 {
            let holder: Holder = Holder { items: [1, 2] };
            {
                let pointer = &holder.items;
                let read = || { return inspect(pointer); };
                let moved = consume(holder.items);
                return read() + moved;
            }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
}

TEST_F(MLATest, RuntimeClosureCanBeReassignedWithMatchingSignature)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var current = |delta: i32| { first += delta; };
            let alternate = |delta: i32| { second += delta; };
            current = alternate;
            current(3);
            return first + second;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

TEST_F(MLATest, RuntimeClosureCannotBeAssignedPastCaptureScope)
{
    std::string code = R"(
        fn main() -> i32 {
            var selected = || {};
            if true {
                var local: i32 = 3;
                let captures_local = || { local += 1; };
                selected = captures_local;
            }
            selected();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign closure 'captures_local' to longer-lived "
            "'selected'"),
        std::string::npos);
}

TEST_F(MLATest, FunctionTypedClosureRunsAcrossFunctionBoundaries)
{
    std::string code = R"(
        var observed: i32 = 0;
        fn apply(callback: fn(i32) -> void, value: i32) -> void {
            callback(value);
        }
        fn main() -> i32 {
            let callback = |value: i32| { observed = value; };
            apply(callback, 5);
            return observed + 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, FunctionTypeAliasWorksForCallbackParameters)
{
    std::string code = R"(
        alias IntMapper = fn(i32) -> i32;
        fn apply(mapper: IntMapper, value: i32) -> i32 {
            return mapper(value);
        }
        fn main() -> i32 {
            let triple = |value: i32| { return value * 3; };
            return apply(triple, 7);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 21);
}

TEST_F(MLATest, NamedFunctionCanBePassedAsCallableValue)
{
    std::string code = R"(
        fn square(value: i32) -> i32 {
            return value * value;
        }
        fn square(value: i64) -> i64 {
            return value * value;
        }
        fn apply(mapper: fn(i32) -> i32, value: i32) -> i32 {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(square, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, NamedFunctionCanInitializeCallableLocals)
{
    std::string code = R"(
        fn square(value: i32) -> i32 { return value * value; }
        fn increment(value: i32) -> i32 { return value + 1; }
        fn main() -> i32 {
            let square_fn: fn(i32) -> i32 = square;
            var selected: fn(i32) -> i32 = increment;
            selected = square_fn;
            return selected(8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

// A function item can be returned and invoked through its callable type.
TEST_F(MLATest, NamedFunctionCanBeReturnedAsCallableValue)
{
    std::string code = R"(
        fn square(value: i32) -> i32 { return value * value; }
        fn get_square() -> fn(i32) -> i32 {
            return square;
        }
        fn main() -> i32 {
            return get_square()(8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

// The declared callable return type selects the matching named-function
// overload.
TEST_F(MLATest, ReturnTypeSelectsNamedFunctionOverload)
{
    std::string code = R"(
        fn square(value: i32) -> i32 { return value * value; }
        fn square(value: i64) -> i64 { return value * value; }
        fn get_square() -> fn(i32) -> i32 {
            return square;
        }
        fn main() -> i32 {
            return get_square()(8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionCanInvokeCallableParameter)
{
    std::string code = R"(
        fn square(value: i32) -> i32 { return value * value; }
        fn square(value: i64) -> i64 { return value * value; }
        generic<T> fn apply(mapper: fn(T) -> T, value: T) -> T {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(square, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionCanInvokeBoundClosureParameter)
{
    std::string code = R"(
        generic<T> fn apply(mapper: fn(T) -> T, value: T) -> T {
            return mapper(value);
        }
        fn main() -> i32 {
            let square = |value: i64| { return value * value; };
            return apply(square, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionCanInvokeInlineClosureParameter)
{
    std::string code = R"(
        generic<T> fn apply(mapper: fn(T) -> T, value: T) -> T {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(|value: i64| { return value * value; }, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionInfersResultFromZeroArgumentClosure)
{
    std::string code = R"(
        generic<T> fn provide(make: fn() -> T) -> T {
            return make();
        }
        fn main() -> i32 {
            return provide(|| { return 7; });
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, GenericFunctionInfersResultFromNestedClosureReturns)
{
    std::string code = R"(
        generic<T> fn provide(make: fn() -> T) -> T {
            return make();
        }
        fn main() -> i32 {
            return provide(|| {
                if (true) {
                    return 7;
                }
                return 9;
            });
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, GenericFunctionInfersInlineClosureParameterType)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(|value| { return value * value; }, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultThroughLocal)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(|value| {
                let squared = value * value;
                return squared;
            }, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultThroughNestedLocal)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(|value| {
                if (value > 0) {
                    let squared = value * value;
                    return squared;
                }
                return value;
            }, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromListLiteral)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let values = apply(|value| { return [value, value + 1]; }, 8);
            return values.len() == 2 && values[0] == 8 && values[1] == 9
                       ? 0
                       : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromGenericCall)
{
    std::string code = R"(
        generic<T> fn identity(value: T) -> T {
            return value;
        }
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(|value| { return identity(value + 1); }, 63);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 64);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromGenericStructCall)
{
    std::string code = R"(
        struct Box<T> { var value: T; };
        generic<T> fn extract(box: Box<T>) -> T {
            return box.value;
        }
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let boxed: Box<i32> = Box<i32> { value: 101 };
            return apply(|item| { return extract(item); }, boxed);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 101);
}

TEST_F(MLATest, ClosureWithFixedArrayParameterInfersResult)
{
    std::string code = R"(
        fn apply(mapper: fn(array<i32, 2>) -> i32,
                 value: array<i32, 2>) -> i32 {
            return mapper(value);
        }
        fn main() -> i32 {
            let values: array<i32, 2> = { 103, 104 };
            return apply(|items: array<i32, 2>| {
                return items[0];
            }, values);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 103);
}

TEST_F(MLATest, GenericFunctionAcceptsFixedArrayArgument)
{
    std::string code = R"(
        generic<T> fn head(values: array<T, 2>) -> T {
            return values[0];
        }
        fn main() -> i32 {
            let values: array<i32, 2> = { 105, 106 };
            return head(values);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 105);
}

TEST_F(MLATest, GenericFunctionInfersExplicitFixedArrayCallback)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let values: array<i32, 2> = { 111, 112 };
            return apply(|items: array<i32, 2>| {
                return items[0];
            }, values);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 111);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromGenericMapCall)
{
    std::string code = R"(
        generic<K, V> fn lookup(values: map<K, V>, key: K) -> V {
            return values[key];
        }
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let scores: map<str8, i32> = {"mlang": 107};
            return apply(|values| { return lookup(values, "mlang"); },
                         scores);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 107);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromPointerCall)
{
    std::string code = R"(
        generic<T> fn read(pointer: ptr<T>) -> T {
            unsafe { return *pointer; }
        }
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            var value: i32 = 109;
            return apply(|pointer| { return read(pointer); }, &value);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 109);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromArrayFill)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let repeated = apply(|value| { return [value; 2]; }, 8);
            return repeated.len() == 2 && repeated[0] == 8 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromTupleAccess)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(tuple<T, i64>) -> U,
                               value: tuple<T, i64>) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let pair: tuple<i64, i64> = (8, 2);
            return apply(|value| { return value.0; }, pair);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 8);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromMatchExpression)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            return apply(|value| {
                return match value {
                    0 => 21,
                    _ => value + 1
                };
            }, 8);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, GenericFunctionRejectsInconsistentClosureMatchResults)
{
    std::string code = R"(
        generic<T, U> fn apply(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let invalid = apply(|value| {
                return match value {
                    0 => 21,
                    _ => true
                };
            }, 8);
            return 0;
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromNestedFields)
{
    std::string code = R"(
        struct Point { var x: i32; };
        struct Entity { var point: Point; };
        generic<T, U> fn project(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let entity: Entity = Entity { point: Point { x: 73 } };
            return project(|item| { return item.point.x; }, entity);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 73);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromGenericStructField)
{
    std::string code = R"(
        struct Box<T> { var value: T; };
        generic<T, U> fn project(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let boxed: Box<i32> = Box<i32> { value: 83 };
            return project(|item| { return item.value; }, boxed);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 83);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromIndexAndUnaryOps)
{
    std::string code = R"(
        generic<T, U> fn project(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let selected = project(|items| { return items[1]; }, [12, 42]);
            let negated = project(|number| { return -number; }, selected);
            let inverted = project(|flag| { return !flag; }, true);
            return selected == 42 && negated == -42 && !inverted ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromTernary)
{
    std::string code = R"(
        generic<T, U> fn project(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            return project(|flag| { return flag ? 17 : 29; }, true) == 17
                       ? 0
                       : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromCollectionMethods)
{
    std::string code = R"(
        generic<T, U> fn project(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let count = project(|items| { return items.len(); }, [3, 5, 8]);
            return count == 3 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromCollectionPredicate)
{
    std::string code = R"(
        generic<T, U> fn project(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            return project(|items| { return items.is_empty(); }, [3]) ? 1 : 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionInfersClosureResultFromGenericMethod)
{
    std::string code = R"(
        struct Box<T> { var value: T; };
        impl<T> Box {
            fn get(self: Box<T>) -> T { return self.value; }
        }
        generic<T, U> fn project(mapper: fn(T) -> U, value: T) -> U {
            return mapper(value);
        }
        fn main() -> i32 {
            let boxed: Box<i32> = Box<i32> { value: 97 };
            return project(|item| { return item.get(); }, boxed);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 97);
}

TEST_F(MLATest, RuntimeClosureCanInvokeCallableParameter)
{
    std::string code = R"(
        fn answer() -> i32 { return 101; }
        fn main() -> i32 {
            let callback: fn() -> i32 = answer;
            let invoke: fn(fn() -> i32) -> i32 = |callable| {
                return callable();
            };
            return invoke(callback);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 101);
}

TEST_F(MLATest, GenericFunctionNeedsInputTypeForUnannotatedClosureParameter)
{
    std::string code = R"(
        generic<T> fn apply(mapper: fn(T) -> T) -> T {
            return mapper(8);
        }
        fn main() -> i32 {
            return apply(|value| { return value * value; });
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericFunctionCanInvokeInlineClosureWithBorrowedCapture)
{
    std::string code = R"(
        generic<T> fn apply(mapper: fn(T) -> T, value: T) -> T {
            return mapper(value);
        }
        fn main() -> i32 {
            var offset: i64 = 3;
            let result = apply(|value: i64| {
                offset = offset + value;
                return offset;
            }, 8);
            return result + offset;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 22);
}

TEST_F(MLATest, CapturingInlineCallbackCannotEscape)
{
    std::string code = R"(
        fn retain(mapper: fn(i64) -> i64) -> fn(i64) -> i64 {
            return mapper;
        }
        fn main() -> i32 {
            var offset: i64 = 3;
            let retained = retain(|value: i64| { return value + offset; });
            return retained(8);
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CapturingInlineCallbackCannotAliasMutableArgument)
{
    std::string code = R"(
        fn invoke(mapper: fn(i64) -> i64, value: &mut i64) -> i64 {
            return mapper(8);
        }
        fn main() -> i32 {
            var value: i64 = 8;
            return invoke(|input: i64| { return input + value; }, &mut value);
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, UniqueNamedFunctionInfersCallableLocalType)
{
    std::string code = R"(
        fn square(value: i32) -> i32 { return value * value; }
        fn increment(value: i32) -> i32 { return value + 1; }
        fn main() -> i32 {
            let square_fn = square;
            var increment_fn = increment;
            return square_fn(8) + increment_fn(1);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 66);
}

TEST_F(MLATest, OverloadedNamedFunctionNeedsCallableAnnotationForInference)
{
    std::string code = R"(
        fn square(value: i32) -> i32 { return value * value; }
        fn square(value: i64) -> i64 { return value * value; }
        fn main() -> i32 {
            let square_fn = square;
            return square_fn(8);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("is overloaded; add an explicit fn(...) -> ... annotation"),
        std::string::npos);
}

TEST_F(MLATest, FunctionTypedClosureRejectsBorrowedCaptureEscape)
{
    std::string code = R"(
        fn bad() -> fn() -> i32 {
            var local: i32 = 9;
            let read = || { return local; };
            return read;
        }
        fn main() -> i32 { return 0; }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot return closure 'read' because it borrows local "
            "variable 'local'"),
        std::string::npos);
}

TEST_F(MLATest, BorrowedClosureCanPassToProvenNoEscapeParameter)
{
    std::string code = R"(
        fn invoke(callback: fn() -> i32) -> i32 {
            return callback();
        }
        fn main() -> i32 {
            var local: i32 = 13;
            let read = || { return local; };
            return invoke(read);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 13);
}

// A callback invocation nested in a return expression is still non-escaping.
TEST_F(MLATest, BorrowedClosureCanPassToNoEscapeParameterInExpression)
{
    std::string code = R"(
        fn invoke(callback: fn() -> i32) -> i32 {
            return callback() + 5;
        }
        fn main() -> i32 {
            var local: i32 = 13;
            let read = || { return local; };
            return invoke(read);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 18);
}

// A callback's pointer result cannot escape when its capture lifetime is
// unknown.
TEST_F(MLATest, CallableParameterCannotReturnUnknownBorrowedPointer)
{
    std::string code = R"(
        fn forward(callback: fn() -> ptr<i32>) -> ptr<i32> {
            return callback();
        }
        fn bad() -> ptr<i32> {
            var local: i32 = 13;
            let address: fn() -> ptr<i32> = || { return &local; };
            return forward(address);
        }
        fn main() -> i32 { return 0; }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("pointer returned by callable parameter 'callback'"),
        std::string::npos)
        << out;
}

// A callback remains non-escaping when invoked only inside conditional
// branches.
TEST_F(MLATest, BorrowedClosureCanPassToNoEscapeParameterWithBranches)
{
    std::string code = R"(
        fn invoke(callback: fn() -> i32, select: bool) -> i32 {
            if select {
                return callback();
            } else {
                return callback();
            }
        }
        fn main() -> i32 {
            var local: i32 = 17;
            let read = || { return local; };
            return invoke(read, true);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 17);
}

// A synchronous loop that invokes its callback does not let that callback
// escape.
TEST_F(MLATest, BorrowedClosureCanPassToNoEscapeParameterWithLoop)
{
    std::string code = R"(
        fn invoke(callback: fn() -> i32) -> i32 {
            for index in 0..1 {
                callback();
            }
            return 0;
        }
        fn main() -> i32 {
            var local: i32 = 19;
            let read = || { return local; };
            invoke(read);
            return local;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 19);
}

// A callback invoked in a try body remains synchronous and non-escaping.
TEST_F(MLATest, BorrowedClosureCanPassToNoEscapeParameterWithTryCatch)
{
    std::string code = R"(
        struct BorrowError {
            var type_name: str8;
            var message: str8;
            var source_line: i32;
        };
        fn invoke(callback: fn() -> i32) -> void {
            try {
                callback();
            } catch error: BorrowError {
            }
        }
        fn main() -> i32 {
            var local: i32 = 23;
            let read = || { return local; };
            invoke(read);
            return local;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 23);
}

// A callback used only as a formatting result is invoked without escaping.
TEST_F(MLATest, BorrowedClosureCanPassToNoEscapeParameterUsedByPrint)
{
    std::string code = R"(
        fn invoke(callback: fn() -> i32) -> void {
            println!("{}", callback());
        }
        fn main() -> i32 {
            var local: i32 = 29;
            let read = || { return local; };
            invoke(read);
            return local;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 29);
}

// A callback invoked by a print statement must still propagate pointer writes.
TEST_F(MLATest, PrintCallbackWrapperPropagatesPointerBorrow)
{
    std::string code = R"(
        fn write(pointer: ptr<i32>) -> i32 {
            unsafe { *pointer = 11; }
            return 0;
        }
        fn invoke(callback: fn(ptr<i32>) -> i32,
                  pointer: ptr<i32>) -> void {
            println!("{}", callback(pointer));
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let shared: ptr<i32> = &value;
            invoke(write, shared);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating"),
        std::string::npos)
        << out;
}

// Pointer-borrow effects still flow through a callback invoked conditionally.
TEST_F(MLATest, ConditionalCallbackWrapperPropagatesPointerBorrow)
{
    std::string code = R"(
        fn invoke(callback: fn(ptr<i32>) -> void,
                  pointer: ptr<i32>, select: bool) -> void {
            if select {
                callback(pointer);
            } else {
                callback(pointer);
            }
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let shared: ptr<i32> = &value;
            let write = |pointer: ptr<i32>| {
                unsafe { *pointer = 11; }
            };
            invoke(write, shared, true);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass shared borrow 'shared' to mutating"),
        std::string::npos)
        << out;
}

// A no-escape wrapper must propagate the callback's pointer borrow effects.
TEST_F(MLATest, CallbackWrapperPropagatesClosurePointerBorrow)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn invoke(callback: fn() -> void) -> void {
            callback();
        }

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            let bind = || { pointer = &mut value; };
            invoke(bind);
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A directly supplied inline closure must propagate pointer borrow effects too.
TEST_F(MLATest, CallbackWrapperPropagatesInlineClosurePointerBorrow)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn invoke(callback: fn() -> void) -> void {
            callback();
        }

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            invoke(|| { pointer = &mut value; });
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, BorrowedClosureCannotPassToEscapingParameter)
{
    std::string code = R"(
        extern fn retain(callback: fn() -> i32);
        fn main() -> i32 {
            var local: i32 = 13;
            let read = || { return local; };
            retain(read);
            return local;
        }
    )";
    writeSource(code);
    int rc = 0;
    compileCapture(rc);
    EXPECT_NE(rc, 0);
}

TEST_F(MLATest, NonBorrowingClosureCanBeReturnedAndInvoked)
{
    std::string code = R"(
        fn make_reader() -> fn() -> i32 {
            let read = || { return 17; };
            return read;
        }
        fn main() -> i32 {
            let read = make_reader();
            var another_read = make_reader();
            return read() + another_read();
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 34);
}

TEST_F(MLATest, ReturnedClosureCanReplaceMutableCallable)
{
    std::string code = R"(
        fn make_incrementer() -> fn(i32) -> i32 {
            let increment = |value: i32| { return value + 1; };
            return increment;
        }
        fn make_doubler() -> fn(i32) -> i32 {
            let double = |value: i32| { return value * 2; };
            return double;
        }
        fn main() -> i32 {
            var transform = make_incrementer();
            transform = make_doubler();
            return transform(9);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 18);
}

TEST_F(MLATest, MutableCallableAssignmentInfersInlineClosureParameter)
{
    std::string code = R"(
        fn main() -> i32 {
            var count: i32 = 0;
            var transform: fn(i32) -> i32 = |value| {
                return value;
            };
            transform = |value| {
                count += value;
                return count;
            };
            let result = transform(9);
            return result + count;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 18);
}

TEST_F(MLATest, MutableCallableAssignmentRejectsShorterLivedCapture)
{
    std::string code = R"(
        fn main() -> i32 {
            var transform: fn(i32) -> i32 = |value| {
                return value;
            };
            {
                let offset: i32 = 3;
                transform = |value| { return value + offset; };
            }
            return transform(1);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign closure borrowing local 'offset'"),
        std::string::npos);
}

TEST_F(MLATest, OwnershipCanReturnPointerBorrowingGlobal)
{
    std::string code = R"(
        var G: i32 = 7;

        fn get_g() -> ptr<i32> {
            return &G;
        }

        fn main() -> i32 {
            let p: ptr<i32> = get_g();
            unsafe {
                return *p;
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, OwnershipCannotStoreLocalBorrowInGlobalPointer)
{
    std::string code = R"(
        var GP: ptr<i32>;

        fn main() -> i32 {
            let x: i32 = 7;
            GP = &x;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot store in global/static"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanStoreGlobalBorrowInGlobalPointer)
{
    std::string code = R"(
        var G: i32 = 7;
        var GP: ptr<i32>;

        fn main() -> i32 {
            GP = &G;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCannotBorrowInnerLocalIntoOuterPointer)
{
    std::string code = R"(
        fn main() -> i32 {
            var p: ptr<i32>;
            if 1: {
                let x: i32 = 7;
                p = &x;
            }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("longer-lived pointer"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanBorrowIntoPointerWhenLifetimesMatch)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 7;
            var p: ptr<i32> = &x;
            unsafe {
                return *p;
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, OwnershipCannotBorrowSameOwnerTwiceInSingleCall)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn inspect(a: ptr<Post>, b: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: [1] };
            return inspect(&p, &p);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("overlapping parts"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanBorrowCallArgAfterSharedPointerLastUse)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn inspect(a: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: [1] };
            let q: ptr<Post> = &p;
            return inspect(&p);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCanBorrowDisjointFieldsInSingleCall)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn add2(a: ptr<i32>, b: ptr<i32>) -> i32 {
            unsafe {
                return *a + *b;
            }
        }

        fn main() -> i32 {
            let p: Pair = Pair { left: 2, right: 3 };
            return add2(&p.left, &p.right);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

TEST_F(
    MLATest,
    OwnershipCanCallWithDisjointFieldWhileAnotherFieldIsMutablyBorrowed)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn inspect(value: ptr<i32>) -> i32 { return 0; }

        fn main() -> i32 {
            var pair: Pair = Pair { left: 2, right: 3 };
            let left = &mut pair.left;
            let result = inspect(&pair.right);
            *left = 7;
            return pair.left + pair.right + result;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 10);
}

TEST_F(MLATest, OwnershipCannotCallWithFieldOverlappingActiveMutableBorrow)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn inspect(value: ptr<i32>) -> i32 { return 0; }

        fn main() -> i32 {
            var pair: Pair = Pair { left: 2, right: 3 };
            let left = &mut pair.left;
            let result = inspect(&pair.left);
            *left = 7;
            return result;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed as mutable"), std::string::npos);
}

TEST_F(MLATest, OwnershipCannotMoveFieldWhileItIsBorrowed)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var marker: i32;
        };

        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn inspect(items: ptr<list<i32>>) -> i32 { return 0; }

        fn main() -> i32 {
            let holder: Holder = Holder { items: [1, 2], marker: 3 };
            let borrowed = &holder.items;
            let moved = consume(holder.items);
            return inspect(borrowed) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
}

// Moving a non-Copy field makes that field unavailable until it is replaced.
TEST_F(MLATest, OwnershipRejectsUseAfterMovingStructField)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var marker: i32;
        };

        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2], marker: 3 };
            let moved = consume(holder.items);
            return consume(holder.items) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'holder.items'"), std::string::npos)
        << out;
}

// A type with automatic cleanup cannot be partially moved before its cleanup.
TEST_F(MLATest, OwnershipRejectsPartialMoveFromTypeWithDropMethod)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var marker: i32;
        };

        impl Holder {
            fn free(self: Holder) -> i32 { return 0; }
        }

        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2], marker: 3 };
            return consume(holder.items);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot partially move 'holder.items'"),
        std::string::npos)
        << out;
}

// A type-level cleanup function also makes its aggregate non-partially-movable.
TEST_F(MLATest, OwnershipRejectsPartialMoveFromTypeWithCleanupFunction)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var marker: i32;
        };

        fn free(holder: Holder) -> void { return; }
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2], marker: 3 };
            return consume(holder.items);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot partially move 'holder.items'"),
        std::string::npos)
        << out;
}

// A runtime closure cannot capture aggregate storage after one field moved.
TEST_F(MLATest, OwnershipRejectsClosureCaptureOfPartiallyMovedAggregate)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2] };
            let moved = consume(holder.items);
            let read: fn() -> i32 = || { return holder.items.len(); };
            return read() + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot capture moved value: 'holder.items'"),
        std::string::npos)
        << out;
}

// A partially moved field cannot be borrowed again through its old storage.
TEST_F(MLATest, OwnershipRejectsBorrowOfPartiallyMovedField)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2] };
            let moved = consume(holder.items);
            let borrowed: ptr<list<i32>> = &holder.items;
            return moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow moved value: 'holder.items'"),
        std::string::npos)
        << out;
}

// A disjoint field can still be borrowed after a sibling field was moved.
TEST_F(MLATest, OwnershipAllowsBorrowOfDisjointFieldAfterPartialMove)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var marker: i32;
        };
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2], marker: 3 };
            let moved = consume(holder.items);
            let borrowed: ptr<i32> = &holder.marker;
            unsafe { return *borrowed + moved - 5; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Borrowing the whole aggregate overlaps every partially moved field.
TEST_F(MLATest, OwnershipRejectsWholeAggregateBorrowAfterPartialMove)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2] };
            let moved = consume(holder.items);
            let borrowed: ptr<Holder> = &holder;
            return moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow moved value: 'holder.items'"),
        std::string::npos)
        << out;
}

// Passing a temporary borrow of a moved field to a call is rejected.
TEST_F(MLATest, OwnershipRejectsPassingBorrowOfPartiallyMovedField)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn inspect(items: ptr<list<i32>>) -> i32 { return 0; }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2] };
            let moved = consume(holder.items);
            return inspect(&holder.items) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow moved value: 'holder.items'"),
        std::string::npos)
        << out;
}

// Method-call reference arguments must reject moved fields as well.
TEST_F(MLATest, OwnershipRejectsMethodBorrowOfPartiallyMovedField)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        struct Inspector { var tag: i32; };

        impl Inspector {
            fn inspect(&self, items: ptr<list<i32>>) -> i32 { return 0; }
        }

        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2] };
            let inspector: Inspector = Inspector { tag: 1 };
            let moved = consume(holder.items);
            return inspector.inspect(&holder.items) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow moved value: 'holder.items'"),
        std::string::npos)
        << out;
}

// A partial move leaves disjoint fields usable and permits field
// reinitialization.
TEST_F(MLATest, OwnershipPartialStructMoveAllowsSiblingAndReinitialization)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var spare: list<i32>;
        };

        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2], spare: [3, 4, 5] };
            let moved = consume(holder.items);
            let sibling = consume(holder.spare);
            holder.items = [6, 7, 8, 9];
            return consume(holder.items) + moved + sibling - 9;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A field moved on only one branch stays unavailable after the branch join.
TEST_F(MLATest, OwnershipPartialMoveOnOneBranchRemainsUnavailable)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn choose() -> bool { return true; }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2] };
            if choose() {
                let moved = consume(holder.items);
            } else {
                holder.items = [3];
            }
            return consume(holder.items);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'holder.items'"), std::string::npos)
        << out;
}

// Reinitializing a moved field in every branch makes it usable after the join.
TEST_F(MLATest, OwnershipPartialMoveReinitializedOnEveryBranch)
{
    std::string code = R"(
        struct Holder { var items: list<i32>; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn choose() -> bool { return true; }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2] };
            if choose() {
                let moved = consume(holder.items);
                holder.items = [3, 4];
            } else {
                let moved = consume(holder.items);
                holder.items = [5];
            }
            return consume(holder.items) - 2;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A whole aggregate read is rejected if any non-Copy field was moved out.
TEST_F(MLATest, OwnershipRejectsWholeAggregateReadAfterPartialMove)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var marker: i32;
        };

        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var holder: Holder = Holder { items: [1, 2], marker: 3 };
            let moved = consume(holder.items);
            let copy = holder;
            return moved + copy.marker;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'holder.items'"), std::string::npos)
        << out;
}

// Tuple partial moves preserve sibling access and allow replacing the element.
TEST_F(MLATest, OwnershipTuplePartialMoveAllowsSiblingAndReinitialization)
{
    std::string code = R"(
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var pair: tuple<list<i32>, list<i32>> = ([1, 2], [3]);
            let moved = consume(pair.0);
            let sibling = consume(pair.1);
            pair.0 = [4, 5, 6];
            return consume(pair.0) + moved + sibling - 6;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Moving a non-Copy list element prevents later reads of the same element.
TEST_F(MLATest, OwnershipRejectsUseAfterMovingListElement)
{
    std::string code = R"(
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var items: list<list<i32>> = [[1, 2], [3]];
            let moved = consume(items[0]);
            return consume(items[0]) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'items[0]'"), std::string::npos)
        << out;
}

// Constant-index moves preserve sibling elements and allow reinitialization.
TEST_F(MLATest, OwnershipListPartialMoveAllowsSiblingAndReinitialization)
{
    std::string code = R"(
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var items: list<list<i32>> = [[1, 2], [3]];
            let moved = consume(items[0]);
            let sibling = consume(items[1]);
            items[0] = [4, 5, 6];
            return consume(items[0]) + moved + sibling - 6;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Moving through a dynamic index conservatively invalidates every element.
TEST_F(MLATest, OwnershipDynamicIndexMoveInvalidatesPossibleElements)
{
    std::string code = R"(
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var items: list<list<i32>> = [[1, 2], [3]];
            let index: i32 = 0;
            let moved = consume(items[index]);
            return consume(items[1]) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'items[*]'"), std::string::npos)
        << out;
}

// Receiver materialization must not bypass partial-move validation.
TEST_F(MLATest, OwnershipRejectsMethodCallOnPartiallyMovedNestedReceiver)
{
    std::string code = R"(
        struct Inner {
            var items: list<i32>;
            var marker: i32;
        };

        impl Inner {
            fn read_marker(&self) -> i32 { return self.marker; }
        }

        struct Outer { var inner: Inner; };
        fn consume(items: list<i32>) -> i32 { return items.len(); }

        fn main() -> i32 {
            var outer: Outer = Outer {
                inner: Inner { items: [1], marker: 7 }
            };
            let moved = consume(outer.inner.items);
            return outer.inner.read_marker() + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("use of moved value: 'outer.inner.items'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCanMoveDisjointFieldWhileSiblingIsBorrowed)
{
    std::string code = R"(
        struct Holder {
            var items: list<i32>;
            var spare: list<i32>;
        };

        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn inspect(items: ptr<list<i32>>) -> i32 { return 0; }

        fn main() -> i32 {
            let holder: Holder = Holder { items: [1, 2], spare: [3, 4, 5] };
            let borrowed = &holder.items;
            let moved = consume(holder.spare);
            return inspect(borrowed) + moved;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 3);
}

TEST_F(MLATest, OwnershipCannotMoveTupleElementWhileItIsBorrowed)
{
    std::string code = R"(
        fn consume(items: list<i32>) -> i32 { return items.len(); }
        fn inspect(items: ptr<list<i32>>) -> i32 { return 0; }

        fn main() -> i32 {
            let pair: tuple<list<i32>, i32> = ([1, 2], 3);
            let borrowed = &pair.0;
            let moved = consume(pair.0);
            return inspect(borrowed) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
}

TEST_F(MLATest, OwnershipCannotMoveListElementWhileItIsBorrowed)
{
    std::string code = R"(
        struct Cell { var items: list<i32>; };
        fn consume(cell: Cell) -> i32 { return cell.items.len(); }
        fn inspect(cell: ptr<Cell>) -> i32 { return 0; }

        fn main() -> i32 {
            var cells: list<Cell> = [
                Cell { items: [1, 2] }, Cell { items: [3] }
            ];
            let borrowed = &cells[0];
            let moved = consume(cells[0]);
            return inspect(borrowed) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanMoveDisjointListElementWhileSiblingIsBorrowed)
{
    std::string code = R"(
        struct Cell { var items: list<i32>; };
        fn consume(cell: Cell) -> i32 { return cell.items.len(); }
        fn inspect(cell: ptr<Cell>) -> i32 { return 0; }

        fn main() -> i32 {
            var cells: list<Cell> = [
                Cell { items: [1, 2] }, Cell { items: [3] }
            ];
            let borrowed = &cells[0];
            let moved = consume(cells[1]);
            return inspect(borrowed) + moved;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 1);
}

TEST_F(MLATest, OwnershipTreatsDynamicElementBorrowAsOverlappingMove)
{
    std::string code = R"(
        struct Cell { var items: list<i32>; };
        fn consume(cell: Cell) -> i32 { return cell.items.len(); }
        fn inspect(cell: ptr<Cell>) -> i32 { return 0; }

        fn main() -> i32 {
            var cells: list<Cell> = [
                Cell { items: [1, 2] }, Cell { items: [3] }
            ];
            let index: i32 = 0;
            let borrowed = &cells[index];
            let moved = consume(cells[1]);
            return inspect(borrowed) + moved;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
}

TEST_F(MLATest, OwnershipCannotBorrowWholeAndFieldInSingleCall)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn consume(a: ptr<Pair>, b: ptr<i32>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Pair = Pair { left: 2, right: 3 };
            return consume(&p, &p.left);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("overlapping parts"), std::string::npos);
}

TEST_F(MLATest, OwnershipCannotBorrowNestedOverlappingFieldsInSingleCall)
{
    std::string code = R"(
        struct Inner {
            var x: i32;
        };
        struct Outer {
            var inner: Inner;
            var other: i32;
        };

        fn consume(a: ptr<Inner>, b: ptr<i32>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let o: Outer = Outer { inner: Inner { x: 2 }, other: 3 };
            return consume(&o.inner, &o.inner.x);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("multiple times in call"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanBorrowNestedDisjointFieldsInSingleCall)
{
    std::string code = R"(
        struct Inner {
            var x: i32;
        };
        struct Outer {
            var inner: Inner;
            var other: i32;
        };

        fn add2(a: ptr<i32>, b: ptr<i32>) -> i32 {
            unsafe {
                return *a + *b;
            }
        }

        fn main() -> i32 {
            let o: Outer = Outer { inner: Inner { x: 2 }, other: 3 };
            return add2(&o.inner.x, &o.other);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

TEST_F(MLATest, OwnershipCanCallSharedMethodWhileObjectBorrowed)
{
    std::string code = R"(
        trait Summary {
            fn summarize(&self) -> str8;
        }

        struct SocialPost {
            var username: str8;
            var content: str8;
            var reply: bool;
            var repost: bool;
        };

        impl Summary for SocialPost {
            fn summarize(&self) -> str8 {
                return format!("{}: {}", self.username, self.content);
            }
        }

        fn main() -> i32 {
            let p: SocialPost = SocialPost {
                username: "alice",
                content: "hello",
                reply: false,
                repost: false
            };
            let q: ptr<SocialPost> = &p;
            println!("{}", p.summarize());
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCannotBorrowMethodReceiverAndFieldArgSameObject)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        impl Pair {
            fn add_with(&self, x: ptr<i32>) -> i32 {
                unsafe {
                    return self.left + *x;
                }
            }
        }

        fn main() -> i32 {
            let p: Pair = Pair { left: 2, right: 3 };
            return p.add_with(&p.right);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("overlapping parts"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanShareBorrowMethodReceiverAndFieldArg)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        impl Pair {
            fn add_with(&self, x: &i32) -> i32 {
                return self.left + x;
            }
        }

        fn main() -> i32 {
            let p: Pair = Pair { left: 2, right: 3 };
            return p.add_with(&p.right);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 5);
}

TEST_F(MLATest, OwnershipCanPassAlreadySharedFieldToSharedMethod)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        impl Pair {
            fn add_with(&self, x: &i32) -> i32 {
                return self.left + x;
            }
        }

        fn main() -> i32 {
            let p: Pair = Pair { left: 2, right: 3 };
            let held = &p.right;
            return p.add_with(&p.right) + *held;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 8);
}

TEST_F(MLATest, MethodAllowsAliasedImmutableReferenceArguments)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        impl Counter {
            fn sum(&self, left: &i32, right: &i32) -> i32 {
                return left + right;
            }
        }
        fn main() -> i32 {
            let counter: Counter = Counter { value: 6 };
            return counter.sum(&counter.value, &counter.value);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, MethodRejectsAliasedSharedAndMutableReferenceArguments)
{
    std::string code = R"(
        struct Counter { var value: i32; };
        impl Counter {
            fn update(&self, read: &i32, write: &mut i32) -> void {
                write = read;
            }
        }
        fn main() -> i32 {
            var counter: Counter = Counter { value: 6 };
            counter.update(&counter.value, &mut counter.value);
            return counter.value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("overlapping parts"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanBorrowMethodReceiverAndArgFromDifferentObject)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        impl Pair {
            fn add_with(&self, x: ptr<i32>) -> i32 {
                unsafe {
                    return self.left + *x;
                }
            }
        }

        fn main() -> i32 {
            let a: Pair = Pair { left: 2, right: 3 };
            let b: Pair = Pair { left: 10, right: 20 };
            return a.add_with(&b.right);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 22);
}

TEST_F(MLATest, OwnershipCannotMixBorrowAndMoveSameCall)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn consume_two(a: ptr<Post>, b: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: [1] };
            return consume_two(&p, p);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot move 'p' while borrowed in call"),
        std::string::npos);
}

TEST_F(MLATest, OwnershipCannotMixMoveAndBorrowSameCall)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn consume_two(a: Post, b: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: [1] };
            return consume_two(p, &p);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot move 'p' while borrowed in call"),
        std::string::npos);
}

TEST_F(MLATest, OwnershipCannotMoveMethodReceiverAndPassAsArg)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        impl Post {
            fn consume_with(&self, other: Post) -> i32 {
                return 0;
            }
        }

        fn main() -> i32 {
            let p: Post = Post { content: [1] };
            return p.consume_with(p);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot move 'p' while borrowed in call"),
        std::string::npos);
}

TEST_F(MLATest, OwnershipCannotMixBorrowPointerVarAndMoveSameCall)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn consume_two(a: ptr<Post>, b: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: [1] };
            let q: ptr<Post> = &p;
            return consume_two(q, p);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot move 'p' while borrowed in call"),
        std::string::npos);
}

TEST_F(MLATest, OwnershipCannotBorrowPointerVarAndAddressSameOwnerInCall)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn inspect_two(a: ptr<Post>, b: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: [1] };
            let q: ptr<Post> = &p;
            return inspect_two(q, &p);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot borrow 'p'"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanBorrowMutInCallAfterSharedPointerLastUse)
{
    std::string code = R"(
        struct Post {
            var content: list<i32>;
        };

        fn inspect_one(a: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            var p: Post = Post { content: [1] };
            let q: ptr<Post> = &p;
            return inspect_one(&mut p);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, OwnershipCanAssignFieldAfterSharedPointerLastUse)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var b: Box = Box { value: 1 };
            let p: ptr<Box> = &b;
            b.value = 2;
            return b.value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 2);
}

TEST_F(MLATest, OwnershipKeepsBorrowLiveForMatchExpressionUse)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var box: Box = Box { value: 1 };
            let p: ptr<Box> = &box;
            box.value = 2;
            let matched: i32 = match (*p).value {
                1 => 10,
                _ => 20
            };
            return matched;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
}

TEST_F(MLATest, OwnershipKeepsBorrowLiveForListLiteralUse)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var box: Box = Box { value: 1 };
            let p: ptr<Box> = &box;
            box.value = 2;
            let values: list<i32> = [(*p).value];
            return values[0];
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
}

TEST_F(MLATest, OwnershipKeepsBorrowLiveForAssertUse)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var box: Box = Box { value: 1 };
            let p: ptr<Box> = &box;
            box.value = 2;
            assert!((*p).value == 1);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
}

TEST_F(MLATest, OwnershipCanAssignFieldAfterBorrowScopeEnds)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn main() -> i32 {
            var b: Box = Box { value: 1 };
            if 1: {
                let p: ptr<Box> = &b;
            }
            b.value = 2;
            return b.value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 2);
}

TEST_F(MLATest, OwnershipCannotReturnPointerBorrowingLocalField)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };

        fn bad_field_ptr() -> ptr<i32> {
            let b: Box = Box { value: 7 };
            return &b.value;
        }

        fn main() -> i32 {
            let p: ptr<i32> = bad_field_ptr();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos);
}

// Returning a pointer-valued ternary must reject any branch borrowing a local.
TEST_F(MLATest, OwnershipCannotReturnTernaryPointerBorrowingLocal)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn choose(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            return flag ? &local_value : &global_value;
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = choose(true);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos)
        << out;
}

// A branch-merged pointer cannot escape if any reachable arm borrows locally.
TEST_F(MLATest, OwnershipCannotReturnBranchMergedPointerBorrowingLocal)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn choose(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var pointer: ptr<i32> = &global_value;
            if flag {
                pointer = &global_value;
            } else {
                pointer = &local_value;
            }
            return pointer;
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = choose(false);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos)
        << out;
}

// Returning a match-produced pointer must reject a local borrow in any arm.
TEST_F(MLATest, OwnershipCannotReturnMatchPointerBorrowingLocal)
{
    std::string code = R"(
        enum Choice : i32 { Local = 0, Global = 1 };
        var global_value: i32 = 11;
        fn choose(choice: Choice) -> ptr<i32> {
            var local_value: i32 = 7;
            return match choice {
                Choice::Local => &local_value,
                Choice::Global => &global_value
            };
        }
        fn main() -> i32 {
            let pointer: ptr<i32> = choose(Choice::Local);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos)
        << out;
}

// A pointer forwarding function keeps its caller's borrowed owner protected.
TEST_F(MLATest, OwnershipCannotMutateOwnerWhileFunctionReturnedPointerLive)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let forwarded = relay(&value);
            value = 9;
            unsafe { return *forwarded; }
        }
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            let forwarded = pointer;
            return forwarded;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// The forwarded borrow ends once the caller's returned pointer is last used.
TEST_F(MLATest, OwnershipFunctionReturnedPointerBorrowEndsAfterLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let forwarded = relay(&value);
            var observed: i32 = 0;
            unsafe { observed = *forwarded; }
            value = 9;
            return observed + value - 16;
        }
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            let forwarded = pointer;
            return forwarded;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A reassigned return alias should retain only its final parameter origin.
TEST_F(MLATest, OwnershipFunctionReturnedPointerDropsOverwrittenOrigin)
{
    std::string code = R"(
        fn choose(a: ptr<i32>, b: ptr<i32>) -> ptr<i32> {
            var pointer: ptr<i32> = a;
            pointer = b;
            return pointer;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let selected: ptr<i32> = choose(&first, &second);
            first = 3;
            unsafe { return *selected + first - 5; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A definite reassignment to a global pointer must discard a local borrow
// origin.
TEST_F(MLATest, OwnershipFunctionReturnedPointerDropsOverwrittenLocalBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var pointer: ptr<i32> = &local_value;
            pointer = &global_value;
            return pointer;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A branch-dependent local borrow must remain in the returned pointer origins.
TEST_F(MLATest, OwnershipFunctionReturnedPointerKeepsConditionalLocalBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var pointer: ptr<i32> = &global_value;
            if flag {
                pointer = &local_value;
            }
            return pointer;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// Replacing a pointer field must discard its previously stored local origin.
TEST_F(MLATest, OwnershipFunctionReturnedPointerDropsOverwrittenFieldBorrow)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        var global_value: i32 = 11;

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var box: PointerBox = PointerBox { value: &local_value };
            box.value = &global_value;
            return box.value;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A branch-dependent pointer-field origin must still be retained.
TEST_F(MLATest, OwnershipFunctionReturnedPointerKeepsConditionalFieldBorrow)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        var global_value: i32 = 11;

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var box: PointerBox = PointerBox { value: &global_value };
            if flag {
                box.value = &local_value;
            }
            return box.value;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// Replacing a list element must discard its old pointer-borrow origin.
TEST_F(MLATest, OwnershipFunctionReturnedPointerDropsOverwrittenListBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var pointers: list<ptr<i32>> = [&local_value];
            pointers[0] = &global_value;
            return pointers[0];
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A branch-dependent list-element borrow must remain in return provenance.
TEST_F(MLATest, OwnershipFunctionReturnedPointerKeepsConditionalListBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var pointers: list<ptr<i32>> = [&global_value];
            if flag {
                pointers[0] = &local_value;
            }
            return pointers[0];
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// Replacing a map value must discard its old pointer-borrow origin.
TEST_F(MLATest, OwnershipFunctionReturnedPointerDropsOverwrittenMapBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var pointers: map<str8, ptr<i32>> = {"value": &local_value};
            pointers = {"value": &global_value};
            return pointers["value"];
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A conditional map-value borrow must remain in returned pointer provenance.
TEST_F(MLATest, OwnershipFunctionReturnedPointerKeepsConditionalMapBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var pointers: map<str8, ptr<i32>> = {"value": &global_value};
            if flag {
                pointers = {"value": &local_value};
            }
            return pointers["value"];
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// Conditional pointer assignments must conservatively retain both origins.
TEST_F(MLATest, OwnershipFunctionReturnedPointerMergesBranchOrigins)
{
    std::string code = R"(
        fn choose(flag: bool, a: ptr<i32>, b: ptr<i32>) -> ptr<i32> {
            var pointer: ptr<i32> = a;
            if flag {
                pointer = b;
            }
            return pointer;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let selected: ptr<i32> = choose(true, &first, &second);
            first = 3;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'first' while borrowed by 'selected'"),
        std::string::npos)
        << out;
}

// Definite assignments in every branch eliminate the initial pointer origin.
TEST_F(MLATest, OwnershipFunctionReturnedPointerDropsDefiniteBranchOrigin)
{
    std::string code = R"(
        fn choose(flag: bool, a: ptr<i32>, b: ptr<i32>, c: ptr<i32>) -> ptr<i32> {
            var pointer: ptr<i32> = a;
            if flag {
                pointer = b;
            } else {
                pointer = c;
            }
            return pointer;
        }
        fn main() -> i32 {
            var initial: i32 = 1;
            var first: i32 = 2;
            var second: i32 = 3;
            let selected: ptr<i32> = choose(true, &initial, &first, &second);
            initial = 4;
            unsafe { return *selected + initial - 6; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Return paths do not leak their alias state into the surviving branch.
TEST_F(MLATest, OwnershipFunctionReturnedPointerExcludesTerminatedBranchOrigin)
{
    std::string code = R"(
        fn choose(flag: bool, a: ptr<i32>, b: ptr<i32>, c: ptr<i32>) -> ptr<i32> {
            var pointer: ptr<i32> = a;
            if flag {
                return b;
            } else {
                pointer = c;
            }
            return pointer;
        }
        fn main() -> i32 {
            var initial: i32 = 1;
            var first: i32 = 2;
            var second: i32 = 3;
            let selected: ptr<i32> = choose(true, &initial, &first, &second);
            initial = 4;
            unsafe { return *selected + initial - 6; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A mutation before a local alias shadow must survive restoration of the outer.
TEST_F(MLATest, OwnershipFunctionReturnedPointerRestoresUpdatedShadowedAlias)
{
    std::string code = R"(
        var global_value: i32 = 9;
        fn choose(flag: bool, a: ptr<i32>, b: ptr<i32>) -> ptr<i32> {
            var pointer: ptr<i32> = a;
            if flag {
                pointer = &*b;
                let pointer: ptr<i32> = &global_value;
            } else {
                pointer = b;
            }
            return pointer;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let selected: ptr<i32> = choose(true, &first, &second);
            first = 3;
            unsafe { return *selected + first - 5; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A local pointer shadow must not replace the outer return provenance.
TEST_F(MLATest, OwnershipFunctionReturnedPointerRestoresShadowedAlias)
{
    std::string code = R"(
        fn choose(a: ptr<i32>, b: ptr<i32>) -> ptr<i32> {
            var pointer: ptr<i32> = a;
            {
                let pointer: ptr<i32> = b;
            }
            return pointer;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let selected: ptr<i32> = choose(&first, &second);
            first = 3;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'first' while borrowed by 'selected'"),
        std::string::npos)
        << out;
}

// Returning a pointer field must not let a by-value borrowing aggregate escape.
TEST_F(MLATest, OwnershipCannotReturnBorrowFromByValueAggregateParameter)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn extract(box: PointerBox) -> ptr<i32> {
            return box.value;
        }
        fn main() -> i32 {
            var owner: i32 = 7;
            let box: PointerBox = PointerBox { value: &owner };
            let pointer: ptr<i32> = extract(box);
            owner = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass aggregate 'box' containing a borrow"),
        std::string::npos)
        << out;
}

// Pointer provenance from try/catch returns must reach the caller summary.
TEST_F(MLATest, OwnershipTryCatchReturnedPointerKeepsBorrowLive)
{
    std::string code = R"(
        struct BorrowError {
            var type_name: str8;
            var message: str8;
            var source_line: i32;
        };
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            var result: ptr<i32>;
            try {
                result = pointer;
            } catch error: BorrowError {
                result = pointer;
            }
            return result;
        }
        fn main() -> i32 {
            var owner: i32 = 7;
            let forwarded: ptr<i32> = relay(&owner);
            owner = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'owner' while borrowed by 'forwarded'"),
        std::string::npos)
        << out;
}

// A pointer routed through a local aggregate must keep its parameter origin.
TEST_F(MLATest, OwnershipAggregateForwardedPointerKeepsBorrowLive)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            var box: PointerBox = PointerBox {};
            box.value = pointer;
            return box.value;
        }
        fn main() -> i32 {
            var owner: i32 = 7;
            let forwarded: ptr<i32> = relay(&owner);
            owner = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'owner' while borrowed by 'forwarded'"),
        std::string::npos)
        << out;
}

// Field-specific return provenance should not borrow unrelated pointer args.
TEST_F(MLATest, OwnershipAggregateForwardedPointerTracksSelectedFieldOrigin)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn relay(unused: ptr<i32>, selected: ptr<i32>) -> ptr<i32> {
            var box: PointerBox = PointerBox {};
            box.value = selected;
            return box.value;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded: ptr<i32> = relay(&first, &second);
            first = 3;
            unsafe { return *forwarded + first - 5; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Struct literal construction should retain the exact pointer field origin.
TEST_F(MLATest, OwnershipAggregateLiteralTracksSelectedPointerOrigin)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn relay(unused: ptr<i32>, selected: ptr<i32>) -> ptr<i32> {
            let box: PointerBox = PointerBox { value: selected };
            return box.value;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded: ptr<i32> = relay(&first, &second);
            first = 3;
            unsafe { return *forwarded + first - 5; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Replacing an aggregate alias must replace its field borrow origins too.
TEST_F(MLATest, OwnershipAggregateReassignmentReplacesPointerFieldOrigin)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn relay(a: ptr<i32>, b: ptr<i32>) -> ptr<i32> {
            var destination: PointerBox = PointerBox { value: a };
            let source: PointerBox = PointerBox { value: b };
            destination = source;
            return destination.value;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded: ptr<i32> = relay(&first, &second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'second' while borrowed by 'forwarded'"),
        std::string::npos)
        << out;
}

// A list used as a local forwarding container must preserve pointer parameters.
TEST_F(MLATest, OwnershipListForwardedPointerParameterKeepsBorrowLive)
{
    std::string code = R"(
        fn relay(unused: ptr<i32>, selected: ptr<i32>) -> ptr<i32> {
            let pointers: list<ptr<i32>> = [selected];
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 7;
            var second: i32 = 8;
            let forwarded: ptr<i32> = relay(&first, &second);
            second = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'second' while borrowed by 'forwarded'"),
        std::string::npos)
        << out;
}

// List indexing should borrow only the pointer stored at that constant index.
TEST_F(MLATest, OwnershipListForwardedPointerTracksSelectedElementOrigin)
{
    std::string code = R"(
        fn relay(unused: ptr<i32>, selected: ptr<i32>) -> ptr<i32> {
            let pointers: list<ptr<i32>> = [selected];
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded: ptr<i32> = relay(&first, &second);
            first = 3;
            unsafe { return *forwarded + first - 5; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Tuple access should preserve only the selected pointer element's origin.
TEST_F(MLATest, OwnershipTupleForwardedPointerTracksSelectedElementOrigin)
{
    std::string code = R"(
        fn relay(unused: ptr<i32>, selected: ptr<i32>) -> ptr<i32> {
            let pointers = (unused, selected);
            return pointers.1;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded: ptr<i32> = relay(&first, &second);
            first = 3;
            unsafe { return *forwarded + first - 5; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Dynamic list indexes retain every pointer origin stored in that list.
TEST_F(MLATest, OwnershipListForwardedPointerDynamicIndexKeepsAllOrigins)
{
    std::string code = R"(
        fn relay(index: i32, first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            let pointers: list<ptr<i32>> = [first, second];
            return pointers[index];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded: ptr<i32> = relay(0, &first, &second);
            first = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'first' while borrowed by 'forwarded'"),
        std::string::npos)
        << out;
}

// A dynamic list index should not borrow unrelated pointer parameters.
TEST_F(MLATest, OwnershipListForwardedDynamicIndexExcludesUnstoredOrigins)
{
    std::string code = R"(
        fn relay(index: i32, first: ptr<i32>, second: ptr<i32>,
                 unrelated: ptr<i32>) -> ptr<i32> {
            let pointers: list<ptr<i32>> = [first, second];
            return pointers[index];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var unrelated: i32 = 3;
            let forwarded = relay(0, &first, &second, &unrelated);
            unrelated = 4;
            unsafe { return *forwarded - 1; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Mutating a tracked pointer list must not discard newly inserted origins.
TEST_F(MLATest, OwnershipListForwardedPointerMutationKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(index: i32, first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            pointers.push(second);
            return pointers[index];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(1, &first, &second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'second' while borrowed by 'forwarded'"),
        std::string::npos)
        << out;
}

// Replacing a tracked list element must replace its prior pointer origin.
TEST_F(MLATest, OwnershipListForwardedPointerElementAssignmentKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            pointers[0] = second;
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'second' directly while mutably borrowed by "
            "'forwarded'"),
        std::string::npos)
        << out;
}

// A helper with mutable access can replace an element's pointer origin.
TEST_F(MLATest, OwnershipListForwardedPointerHelperMutationKeepsNewOrigin)
{
    std::string code = R"(
        fn replace_first(values: &mut list<ptr<i32>>, replacement: ptr<i32>) -> void {
            values[0] = replacement;
        }
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            replace_first(&mut pointers, second);
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// A helper mutating a dynamic list element invalidates pointer origins.
TEST_F(MLATest, OwnershipListForwardedDynamicHelperMutationKeepsNewOrigin)
{
    std::string code = R"(
        fn replace_at(values: &mut list<ptr<i32>>, index: i32,
                      replacement: ptr<i32>) -> void {
            values[index] = replacement;
        }
        fn relay(index: i32, first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            replace_at(&mut pointers, index, second);
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(0, &mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// A closure that mutates a tracked pointer list must invalidate its summary.
TEST_F(MLATest, OwnershipListForwardedClosureMutationKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            let replace = || { pointers[0] = second; };
            replace();
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// An uncalled closure must not make unrelated returned-pointer origins escape.
TEST_F(MLATest, OwnershipListUnusedMutatingClosurePreservesOriginPrecision)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            let replace = || { pointers[0] = second; };
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded + second - 4; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Selecting a mutating closure across branches preserves its effects.
TEST_F(MLATest, OwnershipListBranchSelectedClosureMutationKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(choose_replace: bool, first: ptr<i32>,
                 second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            let replace = || { pointers[0] = second; };
            let noop = || {};
            var selected = || {};
            if choose_replace {
                selected = replace;
            } else {
                selected = noop;
            }
            selected();
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(true, &mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// A mutating helper inside a match arm must invalidate aggregate summaries.
TEST_F(MLATest, OwnershipListMatchArmHelperMutationKeepsNewOrigin)
{
    std::string code = R"(
        fn replace_at(values: &mut list<ptr<i32>>, replacement: ptr<i32>) -> i32 {
            values[0] = replacement;
            return 0;
        }
        fn relay(replace: bool, first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var pointers: list<ptr<i32>> = [first];
            let ignored = match replace {
                true => replace_at(&mut pointers, second),
                _ => 0
            };
            return pointers[0];
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(true, &mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// An indirect write to a pointer local must invalidate its tracked origin.
TEST_F(MLATest, OwnershipIndirectPointerAssignmentKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slot: ptr<ptr<i32>> = &mut selected;
            unsafe { *slot = second; }
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// An indirect overwrite with a global pointer must discard the local origin.
TEST_F(MLATest, OwnershipIndirectPointerAssignmentDropsOverwrittenLocalBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            unsafe { *slot = &global_value; }
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A conditional indirect write must retain the possible local origin.
TEST_F(MLATest, OwnershipIndirectPointerAssignmentKeepsConditionalLocalBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            if flag {
                unsafe { *slot = &local_value; }
            }
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// An indirect write through a conditional slot must update every possible
// target.
TEST_F(MLATest, OwnershipIndirectPointerAssignmentTracksConditionalSlotTargets)
{
    std::string code = R"(
        var global_value: i32 = 11;
        var other_global: i32 = 13;

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            var other: ptr<i32> = &other_global;
            let slot: ptr<ptr<i32>> = flag ? &mut selected : &mut other;
            unsafe { *slot = &local_value; }
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// A helper's definite pointer-slot overwrite must replace the caller's origin.
TEST_F(MLATest, OwnershipHelperSlotOverwriteDropsLocalBorrowOrigin)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn replace(slot: ptr<ptr<i32>>, replacement: ptr<i32>) -> void {
            unsafe { *slot = replacement; }
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A helper that may write a local pointer into a slot must preserve that
// origin.
TEST_F(MLATest, OwnershipHelperConditionalSlotWriteKeepsLocalBorrowOrigin)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn maybe_replace(flag: bool, slot: ptr<ptr<i32>>,
                         replacement: ptr<i32>) -> void {
            if flag {
                unsafe { *slot = replacement; }
            }
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            maybe_replace(flag, slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// Pointer-slot mutation summaries must propagate through unconditional
// wrappers.
TEST_F(MLATest, OwnershipNestedHelperSlotOverwriteDropsLocalBorrowOrigin)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn z_replace_inner(slot: ptr<ptr<i32>>, replacement: ptr<i32>) -> void {
            unsafe { *slot = replacement; }
        }
        fn a_replace_wrapper(slot: ptr<ptr<i32>>,
                           replacement: ptr<i32>) -> void {
            z_replace_inner(slot, replacement);
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            a_replace_wrapper(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Conditional slot mutations must propagate through helper wrappers as
// may-writes.
TEST_F(MLATest, OwnershipNestedHelperConditionalSlotWriteKeepsLocalBorrowOrigin)
{
    std::string code = R"(
        var global_value: i32 = 11;

        fn z_replace_inner(slot: ptr<ptr<i32>>, replacement: ptr<i32>) -> void {
            unsafe { *slot = replacement; }
        }
        fn a_maybe_replace(flag: bool, slot: ptr<ptr<i32>>,
                         replacement: ptr<i32>) -> void {
            if flag {
                z_replace_inner(slot, replacement);
            }
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            a_maybe_replace(flag, slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// A method's definite pointer-slot overwrite must discard a stale local origin.
TEST_F(MLATest, OwnershipMethodSlotOverwriteDropsLocalBorrowOrigin)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            var replacer: Replacer = Replacer { value: 0 };
            replacer.replace(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A direct field-receiver method overwrite must drop a stale local pointer
// origin.
TEST_F(MLATest, OwnershipFieldReceiverMethodOverwriteDropsLocalBorrowOrigin)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        struct Holder { var inner: Replacer; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            var holder: Holder = Holder {
                inner: Replacer { value: 0 }
            };
            holder.inner.replace(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// An inferred aggregate field receiver must resolve its definite slot write.
TEST_F(
    MLATest,
    OwnershipInferredFieldReceiverMethodOverwriteDropsLocalBorrowOrigin)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        struct Holder { var inner: Replacer; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            var holder = Holder {
                inner: Replacer { value: 0 }
            };
            holder.inner.replace(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// An alias of an inferred struct must retain its field receiver type.
TEST_F(
    MLATest,
    OwnershipAliasedInferredFieldReceiverDropsOverwrittenBorrowOrigin)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        struct Holder { var inner: Replacer; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            var holder = Holder {
                inner: Replacer { value: 0 }
            };
            var holder_copy = holder;
            holder_copy.inner.replace(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A conditional method write must retain the possible local pointer origin.
TEST_F(MLATest, OwnershipMethodConditionalSlotWriteKeepsLocalBorrowOrigin)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn maybe_replace(self: &mut Replacer, flag: bool,
                             slot: ptr<ptr<i32>>,
                             replacement: ptr<i32>) -> void {
                if flag {
                    unsafe { *slot = replacement; }
                }
            }
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            var replacer: Replacer = Replacer { value: 0 };
            replacer.maybe_replace(flag, slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// Reassigning a method-local slot alias must kill its old caller-slot origin.
TEST_F(MLATest, OwnershipMethodSlotAliasReassignmentKeepsUnchangedBorrow)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace_unrelated(self: &mut Replacer,
                                 slot: ptr<ptr<i32>>,
                                 replacement: ptr<i32>) -> void {
                var slot_copy: ptr<ptr<i32>> = slot;
                var unrelated: ptr<i32> = &global_value;
                slot_copy = (&mut unrelated);
                unsafe { *slot_copy = replacement; }
            }
        }

        fn return_local() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            var replacer: Replacer = Replacer { value: 0 };
            replacer.replace_unrelated(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = return_local();
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// Method analysis must retain all targets of a conditional slot alias.
TEST_F(MLATest, OwnershipMethodConditionalSlotAliasKeepsBorrowOrigins)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace_selected(self: &mut Replacer, flag: bool,
                                first: ptr<ptr<i32>>,
                                second: ptr<ptr<i32>>,
                                replacement: ptr<i32>) -> void {
                let selected_slot = flag ? first : second;
                unsafe { *selected_slot = replacement; }
            }
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            var other: ptr<i32> = &global_value;
            let first_slot: ptr<ptr<i32>> = &mut selected;
            let second_slot: ptr<ptr<i32>> = &mut other;
            var replacer: Replacer = Replacer { value: 0 };
            replacer.replace_selected(flag, first_slot, second_slot,
                                      &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(false);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// A wrapper function must propagate conditional pointer-slot writes from a
// method.
TEST_F(MLATest, OwnershipMethodPointerSlotEffectPropagatesThroughFunction)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn maybe_replace(self: &mut Replacer, flag: bool,
                             slot: ptr<ptr<i32>>,
                             replacement: ptr<i32>) -> void {
                if flag {
                    unsafe { *slot = replacement; }
                }
            }
        }

        fn replace_through_method(flag: bool,
                                  slot: ptr<ptr<i32>>,
                                  replacement: ptr<i32>) -> void {
            var replacer = Replacer { value: 0 };
            replacer.maybe_replace(flag, slot, replacement);
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace_through_method(flag, slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// Method effects through a struct-field receiver must reach the wrapper caller.
TEST_F(MLATest, OwnershipFieldReceiverMethodEffectPropagatesThroughFunction)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        struct Holder { var inner: Replacer; };
        var global_value: i32 = 11;
        fn replace_through_field(flag: bool,
                                 slot: ptr<ptr<i32>>,
                                 replacement: ptr<i32>) -> void {
            var holder: Holder = Holder {
                inner: Replacer { value: 0 }
            };
            if flag {
                holder.inner.replace(slot, replacement);
            }
        }
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace_through_field(flag, slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// Nested method calls must preserve pointer-slot effects through wrappers.
TEST_F(MLATest, OwnershipNestedMethodPointerSlotEffectPropagatesThroughFunction)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
            fn forward(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                self.replace(slot, replacement);
            }
        }

        fn replace_through_method(slot: ptr<ptr<i32>>,
                                  replacement: ptr<i32>) -> void {
            var replacer = Replacer { value: 0 };
            replacer.forward(slot, replacement);
        }

        fn maybe_local() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace_through_method(slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local();
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// Inferred local method receivers must retain nested pointer-slot effects.
TEST_F(MLATest, OwnershipInferredNestedMethodReceiverPropagatesSlotEffect)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
            fn forward(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                var child = Replacer { value: 0 };
                child.replace(slot, replacement);
            }
        }

        fn replace_through_method(slot: ptr<ptr<i32>>,
                                  replacement: ptr<i32>) -> void {
            var replacer = Replacer { value: 0 };
            replacer.forward(slot, replacement);
        }

        fn maybe_local() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace_through_method(slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local();
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// An unconditional method overwrite must remain definite through a wrapper.
TEST_F(MLATest, OwnershipMethodDefiniteSlotEffectPropagatesThroughFunction)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
            fn forward(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                self.replace(slot, replacement);
            }
        }

        fn replace_through_method(slot: ptr<ptr<i32>>,
                                  replacement: ptr<i32>) -> void {
            var replacer = Replacer { value: 0 };
            replacer.forward(slot, replacement);
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace_through_method(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A conditionally invoked method must not become a definite wrapper effect.
TEST_F(MLATest, OwnershipConditionalMethodCallKeepsPossibleBorrowOrigin)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        var global_value: i32 = 11;
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
        }

        fn maybe_replace(flag: bool, slot: ptr<ptr<i32>>,
                         replacement: ptr<i32>) -> void {
            var replacer = Replacer { value: 0 };
            if flag {
                replacer.replace(slot, replacement);
            }
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            maybe_replace(flag, slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(true);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// Function summaries must kill a caller-slot alias when its local copy is
// reassigned.
TEST_F(MLATest, OwnershipHelperSlotAliasReassignmentKeepsUnchangedBorrow)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn replace_unrelated(slot: ptr<ptr<i32>>,
                             replacement: ptr<i32>) -> void {
            var slot_copy: ptr<ptr<i32>> = slot;
            var unrelated: ptr<i32> = &global_value;
            slot_copy = (&mut unrelated);
            unsafe { *slot_copy = replacement; }
        }

        fn return_local() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace_unrelated(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = return_local();
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// A conditional local slot alias must retain every possible write target.
TEST_F(MLATest, OwnershipHelperConditionalSlotAliasKeepsBorrowOrigins)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn replace_selected(flag: bool, first: ptr<ptr<i32>>,
                            second: ptr<ptr<i32>>,
                            replacement: ptr<i32>) -> void {
            let selected_slot = flag ? first : second;
            unsafe { *selected_slot = replacement; }
        }

        fn maybe_local(flag: bool) -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &global_value;
            var other: ptr<i32> = &global_value;
            let first_slot: ptr<ptr<i32>> = &mut selected;
            let second_slot: ptr<ptr<i32>> = &mut other;
            replace_selected(flag, first_slot, second_slot, &local_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = maybe_local(false);
            local_value = 4;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("borrowed by 'selected'"), std::string::npos) << out;
}

// Self-assignment must preserve a helper's definite pointer-slot alias.
TEST_F(MLATest, OwnershipHelperSelfAssignedSlotAliasKeepsDefiniteWrite)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn replace(slot: ptr<ptr<i32>>, replacement: ptr<i32>) -> void {
            var slot_copy: ptr<ptr<i32>> = slot;
            slot_copy = slot_copy;
            unsafe { *slot_copy = replacement; }
        }

        fn select_global() -> ptr<i32> {
            var local_value: i32 = 7;
            var selected: ptr<i32> = &local_value;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace(slot, &global_value);
            return selected;
        }

        fn main() -> i32 {
            var local_value: i32 = 3;
            let selected = select_global();
            local_value = 4;
            unsafe { return *selected - 11; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A helper's indirect pointer write must update the caller's returned origin.
TEST_F(MLATest, OwnershipHelperIndirectPointerAssignmentKeepsNewOrigin)
{
    std::string code = R"(
        fn replace(slot: ptr<ptr<i32>>, replacement: ptr<i32>) -> void {
            unsafe { *slot = replacement; }
        }
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slot: ptr<ptr<i32>> = &mut selected;
            replace(slot, second);
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// A mutating helper called with a direct mutable address must update origins.
TEST_F(MLATest, OwnershipHelperIndirectPointerAddressKeepsNewOrigin)
{
    std::string code = R"(
        fn replace(slot: ptr<ptr<i32>>, replacement: ptr<i32>) -> void {
            unsafe { *slot = replacement; }
        }
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            replace(&mut selected, second);
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// A method receiving a tracked pointer slot must not leave a stale origin.
TEST_F(MLATest, OwnershipMethodIndirectPointerAssignmentKeepsNewOrigin)
{
    std::string code = R"(
        struct Replacer { var value: i32; };
        impl Replacer {
            fn replace(self: &mut Replacer, slot: ptr<ptr<i32>>,
                       replacement: ptr<i32>) -> void {
                unsafe { *slot = replacement; }
            }
        }
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slot: ptr<ptr<i32>> = &mut selected;
            var replacer: Replacer = Replacer { value: 0 };
            replacer.replace(slot, second);
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// Reading through a pointer slot must not make unrelated origins escape.
TEST_F(MLATest, OwnershipReadOnlyPointerSlotHelperPreservesOriginPrecision)
{
    std::string code = R"(
        fn read(slot: ptr<ptr<i32>>) -> i32 {
            unsafe { return **slot; }
        }
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slot: ptr<ptr<i32>> = &mut selected;
            let snapshot = read(slot);
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded + second - 4; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A called closure writing through a captured pointer slot must update origins.
TEST_F(MLATest, OwnershipClosureIndirectPointerAssignmentKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slot: ptr<ptr<i32>> = &mut selected;
            let replace = || { unsafe { *slot = second; } };
            replace();
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// An uncalled pointer-slot-writing closure must not erase origin precision.
TEST_F(MLATest, OwnershipUnusedPointerSlotClosurePreservesOriginPrecision)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slot: ptr<ptr<i32>> = &mut selected;
            let replace = || { unsafe { *slot = second; } };
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded + second - 4; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A called closure reassigning a captured pointer must update return origins.
TEST_F(MLATest, OwnershipClosurePointerReassignmentKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let replace = || { selected = second; };
            replace();
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// An uncalled captured-pointer reassignment must preserve origin precision.
TEST_F(MLATest, OwnershipUnusedPointerReassignmentClosurePreservesPrecision)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let replace = || { selected = second; };
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded + second - 4; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A closure writing through a pointer slot stored in a field must retain
// origins.
TEST_F(MLATest, OwnershipClosureFieldPointerSlotWriteKeepsNewOrigin)
{
    std::string code = R"(
        struct SlotHolder { var slot: ptr<ptr<i32>>; };
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            {
                let holder: SlotHolder = SlotHolder { slot: &mut selected };
                let replace = || { unsafe { *holder.slot = second; } };
                replace();
            }
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// Writing through a data-pointer field must not widen unrelated return origins.
TEST_F(MLATest, OwnershipClosureDataPointerFieldPreservesOriginPrecision)
{
    std::string code = R"(
        struct DataPointerHolder { var value: ptr<i32>; };
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            {
                var local: i32 = 4;
                let holder: DataPointerHolder =
                    DataPointerHolder { value: &mut local };
                let update = || { unsafe { *holder.value = 5; } };
                update();
            }
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded + second - 4; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A closure write through a pointer slot in a list must preserve its origin.
TEST_F(MLATest, OwnershipClosureListPointerSlotWriteKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slots: list<ptr<ptr<i32>>> = [&mut selected];
            let replace = || { unsafe { *slots[0] = second; } };
            replace();
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// Tuple copies must retain pointer-slot paths used by a captured closure.
TEST_F(MLATest, OwnershipClosureCopiedTuplePointerSlotKeepsNewOrigin)
{
    std::string code = R"(
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let slots: tuple<ptr<ptr<i32>>, i32> = (&mut selected, 0);
            let copied = slots;
            let replace = || { unsafe { *copied.0 = second; } };
            replace();
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// Nested aggregate reassignment must preserve a closure's pointer-slot path.
TEST_F(MLATest, OwnershipClosureNestedAggregatePointerSlotKeepsNewOrigin)
{
    std::string code = R"(
        struct SlotHolder { var slot: ptr<ptr<i32>>; };
        struct OuterHolder { var inner: SlotHolder; };
        fn relay(first: ptr<i32>, second: ptr<i32>) -> ptr<i32> {
            var selected = first;
            let holder: OuterHolder = OuterHolder {
                inner: SlotHolder { slot: &mut selected }
            };
            var copied: OuterHolder = OuterHolder {
                inner: SlotHolder { slot: &mut selected }
            };
            copied = holder;
            let replace = || { unsafe { *copied.inner.slot = second; } };
            replace();
            return selected;
        }
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            let forwarded = relay(&mut first, &mut second);
            second = 3;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot assign to 'second'"), std::string::npos) << out;
}

// Returning a pointer extracted from an aggregate argument retains its borrow.
TEST_F(MLATest, OwnershipAggregateArgumentReturnedPointerKeepsBorrowLive)
{
    std::string code = R"(
        fn extract(pointers: list<ptr<i32>>, index: i32) -> ptr<i32> {
            return pointers[index];
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let pointers: list<ptr<i32>> = [&value];
            let forwarded = extract(pointers, 0);
            value = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate 'pointers' containing a borrow of a local "
            "value because the callee may retain it"),
        std::string::npos)
        << out;
}

// A list borrow may be passed to a helper that only dereferences it to read.
TEST_F(MLATest, ReadOnlyListBorrowCanPassThroughScalarReader)
{
    std::string code = R"(
        fn read_first(pointers: list<ptr<i32>>) -> i32 {
            unsafe { return *pointers[0]; }
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let pointers: list<ptr<i32>> = [&value];
            let snapshot = read_first(pointers);
            value = 9;
            return snapshot + value - 16;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A list containing a borrow cannot be passed to a helper that mutates it.
TEST_F(MLATest, BorrowingListArgumentRejectsMutatingCallee)
{
    std::string code = R"(
        fn append(pointers: list<ptr<i32>>, extra: ptr<i32>) -> i32 {
            pointers.push(extra);
            return 0;
        }
        fn main() -> i32 {
            var value: i32 = 7;
            var extra: i32 = 8;
            let pointers: list<ptr<i32>> = [&value];
            return append(pointers, &extra);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass aggregate 'pointers' containing a borrow"),
        std::string::npos)
        << out;
}

// A map borrow may be passed to a helper that only reads through its pointer.
TEST_F(MLATest, ReadOnlyMapBorrowCanPassThroughScalarReader)
{
    std::string code = R"(
        fn read_value(values: map<str8, ptr<i32>>) -> i32 {
            unsafe { return *values["value"]; }
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let values: map<str8, ptr<i32>> = {"value": &value};
            let snapshot = read_value(values);
            value = 9;
            return snapshot + value - 16;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A map containing a borrow cannot be passed to a mutating helper.
TEST_F(MLATest, BorrowingMapArgumentRejectsElementMutation)
{
    std::string code = R"(
        fn replace(values: map<str8, ptr<i32>>, extra: ptr<i32>) -> i32 {
            values["value"] = extra;
            return 0;
        }
        fn main() -> i32 {
            var value: i32 = 7;
            var extra: i32 = 8;
            let values: map<str8, ptr<i32>> = {"value": &value};
            return replace(values, &extra);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass aggregate 'values' containing a borrow"),
        std::string::npos)
        << out;
}

// A borrowed map value cannot escape through a helper's pointer result.
TEST_F(MLATest, BorrowingMapArgumentRejectsPointerEscape)
{
    std::string code = R"(
        fn extract(values: map<str8, ptr<i32>>) -> ptr<i32> {
            return values["value"];
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let values: map<str8, ptr<i32>> = {"value": &value};
            let forwarded = extract(values);
            value = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass aggregate 'values' containing a borrow"),
        std::string::npos)
        << out;
}

// Pointer provenance must propagate through multiple visible forwarding calls.
TEST_F(MLATest, OwnershipNestedFunctionReturnedPointerKeepsBorrowLive)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let forwarded = relay_again(&value);
            value = 9;
            unsafe { return *forwarded; }
        }
        fn relay_again(pointer: ptr<i32>) -> ptr<i32> {
            return relay(pointer);
        }
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            let forwarded = pointer;
            return forwarded;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// Generic pointer-forwarding specializations must preserve caller borrow
// origins.
TEST_F(MLATest, OwnershipGenericFunctionReturnedPointerKeepsBorrowLive)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let forwarded = relay(&value);
            value = 9;
            unsafe { return *forwarded; }
        }
        generic<T> fn relay(pointer: ptr<T>) -> ptr<T> {
            return pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// Generic forwarding borrows expire once the returned pointer is last used.
TEST_F(MLATest, OwnershipGenericFunctionReturnedPointerBorrowEndsAfterLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let forwarded = relay(&value);
            var observed: i32 = 0;
            unsafe { observed = *forwarded; }
            value = 9;
            return observed + value - 16;
        }
        generic<T> fn relay(pointer: ptr<T>) -> ptr<T> {
            let local_alias = pointer;
            return local_alias;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Callable aliases of forwarding functions preserve returned pointer borrows.
TEST_F(MLATest, OwnershipCallableFunctionItemKeepsArgumentBorrowLive)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let forward: fn(ptr<i32>) -> ptr<i32> = relay;
            let copied_forward = forward;
            let forwarded = copied_forward(&value);
            value = 9;
            unsafe { return *forwarded; }
        }
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            return pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// A callable function-item alias releases its argument borrow after last use.
TEST_F(MLATest, OwnershipCallableFunctionItemBorrowEndsAfterLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let forward: fn(ptr<i32>) -> ptr<i32> = relay;
            let forwarded = forward(&value);
            var observed: i32 = 0;
            unsafe { observed = *forwarded; }
            value = 9;
            return observed + value - 16;
        }
        fn relay(pointer: ptr<i32>) -> ptr<i32> {
            return pointer;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// Straight-line reassignment of a mutable callable retains its known target.
TEST_F(MLATest, OwnershipMutableCallableReassignmentKeepsBorrowSummary)
{
    std::string code = R"(
        fn relay(pointer: ptr<i32>) -> ptr<i32> { return pointer; }
        fn main() -> i32 {
            var value: i32 = 7;
            var forward: fn(ptr<i32>) -> ptr<i32> = relay;
            forward = relay;
            let forwarded = forward(&value);
            value = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// A control-flow join keeps a callable target when every path agrees.
TEST_F(MLATest, OwnershipMutableCallableSameTargetAcrossBranchesKeepsBorrow)
{
    std::string code = R"(
        fn relay(pointer: ptr<i32>) -> ptr<i32> { return pointer; }
        fn main() -> i32 {
            var value: i32 = 7;
            var forward: fn(ptr<i32>) -> ptr<i32> = relay;
            if value > 0 {
                forward = relay;
            } else {
                forward = relay;
            }
            let forwarded = forward(&value);
            value = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// A control-flow join retains every possible target and merges their borrow
// summaries.
TEST_F(MLATest, OwnershipMutableCallableDifferentTargetsMergeBorrowSummaries)
{
    std::string code = R"(
        fn relay(pointer: ptr<i32>) -> ptr<i32> { return pointer; }
        fn relay_again(pointer: ptr<i32>) -> ptr<i32> { return pointer; }
        fn main() -> i32 {
            var value: i32 = 7;
            var forward: fn(ptr<i32>) -> ptr<i32> = relay;
            if value > 0 {
                forward = relay;
            } else {
                forward = relay_again;
            }
            let forwarded = forward(&value);
            value = 9;
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

// Any mutating target in a joined callable set rejects a shared borrow.
TEST_F(MLATest, OwnershipMutableCallableTargetSetRejectsSharedMutation)
{
    std::string code = R"(
        fn relay(pointer: ptr<i32>) -> ptr<i32> { return pointer; }
        fn mutate(pointer: ptr<i32>) -> ptr<i32> {
            unsafe { *pointer = 11; }
            return pointer;
        }
        fn main() -> i32 {
            var value: i32 = 7;
            var forward: fn(ptr<i32>) -> ptr<i32> = relay;
            if value > 0 {
                forward = relay;
            } else {
                forward = mutate;
            }
            let shared: ptr<i32> = &value;
            let forwarded = forward(shared);
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass a shared borrow to mutating callable "
            "pointer parameter"),
        std::string::npos)
        << out;
}

// A mutable callable assigned in a loop loses its single-target summary after
// the loop because the loop may execute zero or multiple times.
TEST_F(MLATest, OwnershipMutableCallableLoopInvalidatesTargetSummary)
{
    std::string code = R"(
        fn relay(pointer: ptr<i32>) -> ptr<i32> { return pointer; }
        fn main() -> i32 {
            var value: i32 = 7;
            var forward: fn(ptr<i32>) -> ptr<i32> = relay;
            var again: bool = true;
            while again {
                forward = relay;
                again = false;
            }
            let forwarded = forward(&value);
            unsafe { return *forwarded; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("opaque callable pointer parameter"), std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCannotStorePointerParameterBorrowGlobally)
{
    std::string code = R"(
        var saved: ptr<i32>;

        fn save(pointer: ptr<i32>) -> void {
            saved = pointer;
        }

        fn main() -> i32 {
            var value: i32 = 7;
            save(&value);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot store in global/static"), std::string::npos)
        << out;
}

// A conditional pointer expression must not store a local borrow globally.
TEST_F(MLATest, OwnershipCannotStoreTernaryPointerBorrowGlobally)
{
    std::string code = R"(
        var saved: ptr<i32>;
        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            saved = flag ? &local_value : &global_value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot store in global/static"), std::string::npos)
        << out;
}

// A joined pointer must retain local-borrow provenance from every branch.
TEST_F(MLATest, OwnershipCannotStoreJoinedGlobalOrLocalPointerGlobally)
{
    std::string code = R"(
        var saved: ptr<i32>;
        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            var selected: ptr<i32> = &global_value;
            if flag {
                selected = &local_value;
            } else {
                selected = &global_value;
            }
            saved = selected;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot store in global/static"), std::string::npos)
        << out;
}

// A shared local origin in a branch join must block mutation of its owner.
TEST_F(MLATest, JoinedSharedPointerKeepsLocalOwnerBorrowActive)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            var selected: ptr<i32> = &global_value;
            if flag {
                selected = &global_value;
            } else {
                selected = &local_value;
            }
            local_value = 8;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to 'local_value' while borrowed by 'selected'"),
        std::string::npos)
        << out;
}

// An exclusive local origin in a branch join must block direct owner writes.
TEST_F(MLATest, JoinedExclusivePointerKeepsLocalOwnerBorrowActive)
{
    std::string code = R"(
        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            var selected: ptr<i32> = &mut global_value;
            if flag {
                selected = &mut global_value;
            } else {
                selected = &mut local_value;
            }
            local_value = 8;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'local_value' directly while mutably borrowed"),
        std::string::npos)
        << out;
}

// A joined exclusive pointer must retain local origins when it escapes.
TEST_F(
    MLATest,
    OwnershipCannotStoreJoinedExclusivePointerWithLocalOriginGlobally)
{
    std::string code = R"(
        var saved: ptr<i32>;
        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            var flag: bool = true;
            var selected: ptr<i32> = &mut global_value;
            if flag {
                selected = &mut global_value;
            } else {
                selected = &mut local_value;
            }
            saved = selected;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_TRUE(
        out.find("cannot store in global/static") != std::string::npos ||
        out.find("cannot alias mutable borrow") != std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCannotReturnPointerLoadedThroughBorrowedPointer)
{
    std::string code = R"(
        fn relay(pointer: ptr<ptr<i32>>) -> ptr<i32> {
            return *pointer;
        }

        fn main() -> i32 {
            var value: i32 = 7;
            let inner = &value;
            let outer = &inner;
            let escaped = relay(outer);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return pointer that borrows local value"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCannotReturnAggregateContainingBorrowedPointer)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn wrap(pointer: ptr<i32>) -> PointerBox {
            return PointerBox { value: pointer };
        }

        fn main() -> i32 {
            var value: i32 = 7;
            let escaped = wrap(&value);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot return"), std::string::npos) << out;
}

// A match result must not hide a local pointer inside a returned aggregate.
TEST_F(MLATest, OwnershipCannotReturnMatchAggregateContainingBorrow)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        enum Choice : i32 { Local = 0, Global = 1 };
        var global_value: i32 = 11;
        fn choose(choice: Choice) -> PointerBox {
            var local_value: i32 = 7;
            return match choice {
                Choice::Local => PointerBox { value: &local_value },
                _ => PointerBox { value: &global_value }
            };
        }
        fn main() -> i32 {
            let boxed: PointerBox = choose(Choice::Local);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return aggregate containing a pointer"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCannotReturnLocalAggregateAliasContainingBorrow)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn wrap() -> PointerBox {
            var value: i32 = 7;
            let pointer = &value;
            let boxed = PointerBox { value: pointer };
            return boxed;
        }

        fn main() -> i32 {
            let escaped = wrap();
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return aggregate containing a pointer"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, AggregateBorrowKeepsOwnerBorrowedUntilLastUse)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var value: i32 = 7;
            let boxed = PointerBox { value: &value };
            value = 9;
            let pointer = boxed.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'boxed'"), std::string::npos) << out;
}

TEST_F(MLATest, MethodReturnedAggregateKeepsBorrowOriginAlive)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn duplicate(self: PointerBox) -> PointerBox {
                return self;
            }
        }

        fn main() -> i32 {
            var value: i32 = 7;
            let original = PointerBox { value: &value };
            let copied = original.duplicate();
            value = 9;
            unsafe { return *copied.value; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'copied'"), std::string::npos) << out;
}

TEST_F(MLATest, MethodReturnedAggregateFieldKeepsBorrowOriginAlive)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        impl PointerBox {
            fn duplicate(self: PointerBox) -> PointerBox {
                return self;
            }
        }

        fn main() -> i32 {
            var value: i32 = 7;
            let original = PointerBox { value: &value };
            let copied = original.duplicate();
            let pointer: ptr<i32> = copied.value;
            value = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateBorrowAllowsMutationOfDisjointOwnerField)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let boxed: PointerBox = PointerBox { value: &pair.left };
            pair.right = 9;
            unsafe { return *boxed.value + pair.right; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 10);
}

TEST_F(MLATest, AggregateBorrowStillRejectsMutationOfBorrowedOwnerField)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let boxed: PointerBox = PointerBox { value: &pair.left };
            pair.left = 9;
            unsafe { return *boxed.value; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'boxed'"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateBorrowPlaceRestoresAfterShadowedScope)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let boxed: PointerBox = PointerBox { value: &pair.left };
            {
                var other: i32 = 3;
                let boxed: PointerBox = PointerBox { value: &other };
                unsafe { if *boxed.value != 3 { return 1; } }
            }
            pair.right = 9;
            return pair.right;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, AggregateBorrowsMultipleDisjointFieldsPrecisely)
{
    std::string code = R"(
        struct Pair { var left: i32; var middle: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };

        fn set_value(value: ptr<i32>) -> void {
            unsafe { *value = 9; }
            return;
        }

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, middle: 2, right: 3 };
            let refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            let refs_copy: PointerPair = refs;
            set_value(&mut pair.middle);
            unsafe { return *refs_copy.first + pair.middle + *refs_copy.second; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 13);
}

TEST_F(MLATest, AggregateBorrowsMultipleFieldsStillRejectOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var middle: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, middle: 2, right: 3 };
            let refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            pair.left = 9;
            unsafe { return *refs.first + *refs.second; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'refs'"), std::string::npos) << out;
}

TEST_F(MLATest, ExtractedPointerRetainsItsAggregateFieldBorrowPath)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };
        struct PointerPairWrapper { var inner: PointerPair; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            let refs_copy: PointerPair = refs;
            let wrapped = PointerPairWrapper { inner: refs_copy };
            let first = wrapped.inner.first;
            pair.right = 9;
            unsafe { return *first + pair.right; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 10);
}

TEST_F(MLATest, ExtractedPointerStillProtectsItsBorrowedAggregateField)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };
        struct PointerPairWrapper { var inner: PointerPair; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            let refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            let refs_copy: PointerPair = refs;
            let wrapped = PointerPairWrapper { inner: refs_copy };
            let first = wrapped.inner.first;
            pair.left = 9;
            unsafe { return *first; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'first'"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateBorrowPlaceSetsMergeAcrossBranches)
{
    std::string code = R"(
        struct Pair { var left: i32; var middle: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, middle: 2, right: 3 };
            var refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            var choose: bool = true;
            if choose {
                refs = PointerPair {
                    first: &pair.middle,
                    second: &pair.right
                };
            }
            pair.left = 9;
            unsafe { return *refs.first; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'refs'"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateFieldAssignmentTracksBorrowOrigin)
{
    std::string code = R"(
        struct Inner { var value: ptr<i32>; };
        struct Outer { var inner: Inner; };

        fn main() -> i32 {
            var value: i32 = 7;
            var outer: Outer = Outer { inner: Inner {} };
            outer.inner = Inner { value: &value };
            value = 9;
            let pointer = outer.inner.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'outer'"), std::string::npos) << out;
}

// Replacing one pointer field releases its previous borrow of that field's
// owner.
TEST_F(MLATest, AggregateFieldReplacementReleasesOldBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            var other: i32 = 3;
            var refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            refs.first = &other;
            pair.left = 9;
            unsafe { return *refs.first + pair.left + *refs.second; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 14);
}

// Replacing one field must retain a sibling field's borrow of another place.
TEST_F(MLATest, AggregateFieldReplacementKeepsSiblingBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            var other: i32 = 3;
            var refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            refs.first = &other;
            pair.right = 9;
            unsafe { return *refs.second; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'pair' while borrowed by 'refs'"),
        std::string::npos)
        << out;
}

// Copying a sibling pointer into a field transfers its tracked source path.
TEST_F(MLATest, AggregateFieldReplacementFromSiblingTransfersBorrowPlace)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerPair { var first: ptr<i32>; var second: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            var refs: PointerPair = PointerPair {
                first: &pair.left,
                second: &pair.right
            };
            refs.first = refs.second;
            pair.left = 9;
            unsafe { return *refs.first + pair.left + *refs.second; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 13);
}

// Replacing a nested pointer field releases its old borrow and keeps siblings.
TEST_F(MLATest, NestedAggregateFieldReplacementUpdatesBorrowPlaces)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct Inner { var value: ptr<i32>; };
        struct Outer { var inner: Inner; var sibling: ptr<i32>; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            var other: i32 = 3;
            var outer: Outer = Outer {
                inner: Inner { value: &pair.left },
                sibling: &pair.right
            };
            outer.inner.value = &other;
            pair.left = 9;
            unsafe { return *outer.inner.value + pair.left + *outer.sibling; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 14);
}

// Replacing a list element updates only that element's retained borrow place.
TEST_F(MLATest, ListElementReplacementUpdatesBorrowPlaces)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            var other: i32 = 3;
            var refs: list<ptr<i32>> = [&pair.left, &pair.right];
            refs[0] = &other;
            pair.left = 9;
            unsafe { return *refs[0] + pair.left + *refs[1]; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 14);
}

// Replacing a list element must not release a sibling element's borrow.
TEST_F(MLATest, ListElementReplacementKeepsSiblingBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            var other: i32 = 3;
            var refs: list<ptr<i32>> = [&pair.left, &pair.right];
            refs[0] = &other;
            pair.right = 9;
            unsafe { return *refs[1]; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'refs'"), std::string::npos) << out;
}

// A dynamic list index may replace only one slot, so other borrows must remain.
TEST_F(MLATest, DynamicListElementReplacementKeepsPossibleSiblingBorrows)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };

        fn main() -> i32 {
            var pair: Pair = Pair { left: 1, right: 2 };
            var other: i32 = 3;
            var index: i32 = 0;
            var refs: list<ptr<i32>> = [&pair.left, &pair.right];
            refs[index] = &other;
            pair.right = 9;
            unsafe { return *refs[1]; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'refs'"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateFieldMovePreservesBorrowOrigin)
{
    std::string code = R"(
        struct Inner { var value: ptr<i32>; };
        struct Outer { var inner: Inner; };

        fn main() -> i32 {
            var value: i32 = 7;
            let inner: Inner = Inner { value: &value };
            var outer: Outer = Outer { inner: Inner {} };
            outer.inner = inner;
            value = 9;
            let pointer = outer.inner.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'outer'"), std::string::npos) << out;
}

TEST_F(MLATest, ListPushTracksBorrowOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&value);
            value = 9;
            let pointer = pointers[0];
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'pointers'"), std::string::npos)
        << out;
}

TEST_F(MLATest, ListPushRejectsSharedBorrowWhileOwnerIsMutablyBorrowed)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let exclusive: ptr<i32> = &mut value;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&value);
            unsafe { return *exclusive; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("while mutably borrowed by 'exclusive'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListPushRejectsStoringMutableBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let exclusive: ptr<i32> = &mut value;
            var pointers: list<ptr<i32>> = [];
            pointers.push(exclusive);
            unsafe { return *exclusive; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("use of moved value: 'exclusive'"), std::string::npos)
        << out;
}

TEST_F(MLATest, ListRetainsExclusiveBorrowUntilItsLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let exclusive: ptr<i32> = &mut value;
            var pointers: list<ptr<i32>> = [];
            pointers.push(exclusive);
            value = 9;
            let count: i64 = pointers.len();
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("mutably borrowed by 'pointers'"), std::string::npos)
        << out;
}

TEST_F(MLATest, ListPushTracksInlineExclusiveBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut value);
            value = 9;
            let count: i64 = pointers.len();
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("while mutably borrowed by 'pointers'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, StructRetainsExclusiveBorrowUntilItsLastUse)
{
    std::string code = R"(
        struct PointerBox {
            var value: ptr<i32>;
        };

        fn main() -> i32 {
            var value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &mut value };
            value = 9;
            unsafe { return *boxed.value; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while mutably borrowed by 'boxed'"), std::string::npos)
        << out;
}

TEST_F(MLATest, ListCanRetainDisjointExclusiveBorrows)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut first);
            pointers.push(&mut second);
            return pointers.len() == 2 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListCannotRetainOverlappingExclusiveBorrows)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut value);
            pointers.push(&mut value);
            return pointers.len() == 2 ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot retain overlapping mutable borrows of 'value'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListTreatsDynamicExclusiveBorrowIndexesAsOverlapping)
{
    std::string code = R"(
        fn main() -> i32 {
            var values: array<i32, 2> = { 4, 8 };
            var pointers: list<ptr<i32>> = [];
            let first: i64 = 0;
            let second: i64 = 1;
            pointers.push(&mut values[first]);
            pointers.push(&mut values[second]);
            return pointers.len() == 2 ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot retain overlapping mutable borrows"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListCanRetainDisjointBorrowsOfOneStruct)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; var spare: i32; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 8, spare: 3 };
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut pair.left);
            pointers.push(&mut pair.right);
            pair.spare = 9;
            return pointers.len() == 2 ? pair.spare - 9 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ListRetainsEachDisjointExclusiveBorrow)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 8 };
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut pair.left);
            pointers.push(&mut pair.right);
            pair.left = 9;
            return pointers.len() == 2 ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'pointers'"), std::string::npos)
        << out;
}

TEST_F(MLATest, StructBorrowsProtectOnlyBorrowedFields)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; var spare: i32; };
        struct PairPointers { var left: ptr<i32>; var right: ptr<i32>; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 8, spare: 3 };
            let refs: PairPointers = PairPointers {
                left: &mut pair.left,
                right: &mut pair.right
            };
            pair.spare = 9;
            unsafe { return *refs.left + *refs.right + pair.spare; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 21);
}

// Verifies in-place mutation through an exclusive pointer stored in a struct.
TEST_F(MLATest, CanUseExclusiveBorrowInPlaceInsideStruct)
{
    std::string code = R"(
        struct MutableSlot { var value: ptr<i32>; };
        fn main() -> i32 {
            var value: i32 = 4;
            let slot: MutableSlot = MutableSlot { value: &mut value };
            unsafe {
                *slot.value = 12;
                return *slot.value;
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 12);
}

TEST_F(MLATest, StructCannotMutateBorrowedExclusiveField)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PairPointers { var left: ptr<i32>; var right: ptr<i32>; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 8 };
            let refs: PairPointers = PairPointers {
                left: &mut pair.left,
                right: &mut pair.right
            };
            pair.left = 9;
            unsafe { return *refs.left + *refs.right; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'refs'"), std::string::npos) << out;
}

TEST_F(MLATest, StructRejectsOverlappingExclusiveFieldBorrows)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PairPointers { var first: ptr<i32>; var second: ptr<i32>; };
        fn main() -> i32 {
            var pair: Pair = Pair { left: 4, right: 8 };
            let refs: PairPointers = PairPointers {
                first: &mut pair.left,
                second: &mut pair.left
            };
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot borrow 'pair' as mutable more than once"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListCannotOutliveItsExclusiveBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var pointers: list<ptr<i32>> = [];
            {
                var value: i32 = 7;
                pointers.push(&mut value);
            }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot store mutable borrow of 'value' in "
            "longer-lived aggregate 'pointers'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListCannotCopyOutExclusiveBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut value);
            let extracted: ptr<i32> = pointers[0];
            unsafe { return *extracted; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot extract a mutable borrow from aggregate "
            "'pointers'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ListCannotPopExclusiveBorrowOut)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut value);
            let extracted: ptr<i32> = pointers.pop();
            unsafe { return *extracted; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot extract a mutable borrow from aggregate "
            "'pointers'"),
        std::string::npos)
        << out;
}

// A shared borrow returned by pop() remains live through the extracted pointer.
TEST_F(MLATest, ListPopTransfersSharedBorrowToExtractedPointer)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&value);
            let extracted: ptr<i32> = pointers.pop();
            value = 9;
            unsafe { return *extracted; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'extracted'"), std::string::npos)
        << out;
}

TEST_F(MLATest, CannotPassExclusiveBorrowOutOfAggregateToFunction)
{
    std::string code = R"(
        fn consume(pointer: ptr<i32>) -> i32 {
            unsafe { return *pointer; }
        }

        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut value);
            return consume(pointers[0]);
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass a mutable borrow out of aggregate "
            "'pointers'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, CannotPassAggregateContainingExclusiveBorrowToFunction)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn inspect(box: PointerBox) -> ptr<i32> {
            let pointer = box.value;
            return pointer;
        }

        fn main() -> i32 {
            var value: i32 = 7;
            let box = PointerBox { value: &mut value };
            inspect(box);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate 'box' containing a borrow of a "
            "local value"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, CannotPassTemporaryAggregateContainingExclusiveBorrow)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn inspect(box: PointerBox) -> void {
            return;
        }

        fn main() -> i32 {
            var value: i32 = 7;
            inspect(PointerBox { value: &mut value });
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate containing a borrow of a local "
            "value"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, CannotPassAggregateContainingSharedBorrowToFunction)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn inspect(box: PointerBox) -> ptr<i32> {
            return box.value;
        }

        fn main() -> i32 {
            let value: i32 = 7;
            let box = PointerBox { value: &value };
            inspect(box);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate 'box' containing a borrow of a "
            "local value"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, CannotPassTemporaryAggregateContainingSharedBorrow)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn inspect(box: PointerBox) -> ptr<i32> {
            return box.value;
        }

        fn main() -> i32 {
            let value: i32 = 7;
            inspect(PointerBox { value: &value });
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate containing a borrow of a local "
            "value"),
        std::string::npos)
        << out;
}

// A visible function may read a shared borrow from an aggregate synchronously.
TEST_F(MLATest, SharedBorrowingAggregateCanPassToReadOnlyFunction)
{
    std::string code = R"(
        struct PointerBox {
            var value: ptr<i32>;
            var count: i32;
        };
        fn inspect(box: PointerBox) -> i32 {
            let saved = box.value;
            unsafe { return *saved + box.count; }
        }
        fn main() -> i32 {
            let value: i32 = 17;
            let box = PointerBox { value: &value, count: 2 };
            if inspect(box) != 19 { return 1; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A read-only callee may dereference a shared pointer nested in an aggregate
// field.
TEST_F(MLATest, SharedBorrowingAggregateCanReadNestedPointerField)
{
    std::string code = R"(
        struct InnerPointer {
            var value: ptr<i32>;
        };
        struct PointerBox {
            var inner: InnerPointer;
            var count: i32;
        };
        fn inspect(box: PointerBox) -> i32 {
            let saved = box.inner.value;
            unsafe { return *saved + box.count; }
        }
        fn main() -> i32 {
            let value: i32 = 17;
            let inner = InnerPointer { value: &value };
            let box = PointerBox { inner: inner, count: 2 };
            if inspect(box) != 19 { return 1; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A callee that writes through an aggregate pointer field cannot accept its
// shared borrow.
TEST_F(MLATest, SharedBorrowingAggregateRejectsMutatingFunction)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        fn write(box: PointerBox) -> void {
            unsafe { *box.value = 9; }
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let box = PointerBox { value: &value };
            write(box);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate 'box' containing a borrow of a "
            "local value"),
        std::string::npos)
        << out;
}

// A nested pointer write must not pass the read-only aggregate-call proof.
TEST_F(MLATest, SharedBorrowingAggregateRejectsNestedPointerMutation)
{
    std::string code = R"(
        struct InnerPointer { var value: ptr<i32>; };
        struct PointerBox { var inner: InnerPointer; };
        fn write(box: PointerBox) -> void {
            unsafe { *box.inner.value = 9; }
        }
        fn main() -> i32 {
            var value: i32 = 7;
            let inner = InnerPointer { value: &value };
            let box = PointerBox { inner: inner };
            write(box);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate 'box' containing a borrow of a "
            "local value"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, CannotHideSharedBorrowingAggregateInCopyCallArgument)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn inspect(box: PointerBox) -> ptr<i32> {
            return box.value;
        }

        fn main() -> i32 {
            let value: i32 = 7;
            let box = PointerBox { value: &value };
            inspect(std::copy(box));
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot pass aggregate containing a borrow of a local "
            "value"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, AggregateBorrowOfGlobalCanPassToFunction)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        var global_value: i32 = 7;

        fn inspect(box: PointerBox) -> void {
            return;
        }

        fn main() -> i32 {
            inspect(PointerBox { value: &global_value });
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CannotCopyAggregateContainingExclusiveBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&mut value);
            let copied: list<ptr<i32>> = std::copy(pointers);
            return copied.len();
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("std::copy cannot duplicate a mutable borrow"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, ExclusiveBorrowInListExpiresAfterContainerLastUse)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let exclusive: ptr<i32> = &mut value;
            var pointers: list<ptr<i32>> = [];
            pointers.push(exclusive);
            let count: i64 = pointers.len();
            value = 9;
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

TEST_F(MLATest, PointerExtractedFromListRetainsBorrowOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&value);
            let pointer = pointers[0];
            value = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, PointerExtractionTracksAllAggregateOrigins)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&first);
            pointers.push(&second);
            let pointer = pointers[0];
            first = 3;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, PointerExtractionConservativelyBorrowsEveryPossibleOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&first);
            pointers.push(&second);
            let pointer = pointers[0];
            second = 3;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, PointerAliasPreservesAllAggregateBorrowOrigins)
{
    std::string code = R"(
        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&first);
            pointers.push(&second);
            let pointer = pointers[0];
            let copied = pointer;
            second = 3;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateCopyPreservesAllPointerBorrowOrigins)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&first);
            pointers.push(&second);
            let pointer = pointers[0];
            let boxed = PointerBox { value: pointer };
            second = 3;
            unsafe { return *boxed.value; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos) << out;
}

TEST_F(MLATest, StructFieldListPushTracksBorrowOrigin)
{
    std::string code = R"(
        struct PointerList { var items: list<ptr<i32>>; };

        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: PointerList = PointerList { items: [] };
            pointers.items.push(&value);
            value = 9;
            let pointer = pointers.items[0];
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'pointers'"), std::string::npos)
        << out;
}

TEST_F(MLATest, ListExtendTracksBorrowOrigins)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let source: list<ptr<i32>> = [&value];
            var pointers: list<ptr<i32>> = [];
            pointers.extend(source);
            value = 9;
            let pointer = pointers[0];
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'pointers'"), std::string::npos)
        << out;
}

TEST_F(MLATest, FixedArrayFillTracksBorrowOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var other: i32 = 3;
            var pointers: array<ptr<i32>, 1> = {&other};
            pointers.fill(&value);
            value = 9;
            let pointer = pointers[0];
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'pointers'"), std::string::npos)
        << out;
}

TEST_F(MLATest, FixedArrayFillReleasesReplacedBorrowOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var other: i32 = 3;
            var pointers: array<ptr<i32>, 1> = {&other};
            pointers.fill(&value);
            pointers.fill(&other);
            value = 9;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, ClearingListReleasesStoredBorrowOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            var pointers: list<ptr<i32>> = [];
            pointers.push(&value);
            pointers.clear();
            value = 9;
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// A pointer stored in a map keeps its owner borrowed for the map's lifetime.
TEST_F(MLATest, MapPointerValueTracksBorrowOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let pointers: map<str8, ptr<i32>> = {"value": &value};
            value = 9;
            let pointer: ptr<i32> = pointers["value"];
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'pointers'"), std::string::npos)
        << out;
}

// Copying a map preserves pointer borrow origins in the copied aggregate.
TEST_F(MLATest, MapCopyPreservesPointerBorrowOrigin)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let original: map<str8, ptr<i32>> = {"value": &value};
            let copied: map<str8, ptr<i32>> = original;
            value = 9;
            let pointer: ptr<i32> = copied["value"];
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'copied'"), std::string::npos) << out;
}

// A map literal holding an exclusive pointer must prevent owner mutation.
TEST_F(MLATest, MapPointerValueRetainsExclusiveBorrow)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            let pointers: map<str8, ptr<i32>> = {"value": &mut value};
            value = 9;
            let pointer: ptr<i32> = pointers["value"];
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("while mutably borrowed by 'pointers'"),
        std::string::npos)
        << out;
}

// A constant-key map lookup preserves the precise field borrowed by its value.
TEST_F(MLATest, MapConstantKeyPointerAllowsDisjointFieldMutation)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn main() -> i32 {
            var pair = Pair { left: 11, right: 22 };
            let pointers: map<i32, ptr<i32>> = {
                0: &pair.left,
                1: &pair.right
            };
            let left: ptr<i32> = pointers[0];
            pair.right = 23;
            unsafe { return *left; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 11);
}

// A runtime map key keeps all possible entry borrow origins conservative.
TEST_F(MLATest, MapDynamicKeyPointerBlocksPotentiallyOverlappingMutation)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn main() -> i32 {
            var pair = Pair { left: 11, right: 22 };
            let pointers: map<i32, ptr<i32>> = {
                0: &pair.left,
                1: &pair.right
            };
            var key: i32 = 0;
            let selected: ptr<i32> = pointers[key];
            pair.right = 23;
            unsafe { return *selected; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// A literal string key preserves the selected map value's borrow place.
TEST_F(MLATest, MapStringKeyPointerAllowsDisjointFieldMutation)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn main() -> i32 {
            var pair = Pair { left: 11, right: 22 };
            let pointers: map<str8, ptr<i32>> = {
                "left": &pair.left,
                "right": &pair.right
            };
            let left: ptr<i32> = pointers["left"];
            pair.right = 23;
            unsafe { return *left; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 11);
}

// A constexpr map key preserves the same precise borrow path as its literal.
TEST_F(MLATest, MapCexprKeyPointerAllowsDisjointFieldMutation)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn main() -> i32 {
            cexpr LEFT_KEY: i32 = 0;
            var pair = Pair { left: 11, right: 22 };
            let pointers: map<i32, ptr<i32>> = {
                LEFT_KEY: &pair.left,
                1: &pair.right
            };
            let left: ptr<i32> = pointers[LEFT_KEY];
            pair.right = 23;
            unsafe { return *left; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 11);
}

// A compile-time arithmetic map key keeps construction and lookup paths equal.
TEST_F(MLATest, MapConstantExpressionKeyAllowsDisjointFieldMutation)
{
    std::string code = R"(
        struct Pair {
            var left: i32;
            var right: i32;
        };

        fn main() -> i32 {
            var pair = Pair { left: 11, right: 22 };
            let pointers: map<i32, ptr<i32>> = {
                1 + 1: &pair.left,
                3: &pair.right
            };
            let left: ptr<i32> = pointers[1 + 1];
            pair.right = 23;
            unsafe { return *left; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 11);
}

// A closure selected across branches must retain every possible captured owner.
TEST_F(MLATest, RuntimeClosureBranchMergeRetainsAllCaptureBorrows)
{
    std::string code = R"(
        fn run(choose_first: bool) -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var selected = || {};
            if choose_first {
                let capture_first = || { first += 1; };
                selected = capture_first;
            } else {
                let capture_second = || { second += 1; };
                selected = capture_second;
            }
            first = 3;
            selected();
            return first + second;
        }

        fn main() -> i32 { return run(true); }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// Branch-merged closure captures release every owner after the closure's last
// use.
TEST_F(MLATest, RuntimeClosureBranchMergeReleasesAllCaptureBorrows)
{
    std::string code = R"(
        fn run(choose_first: bool) -> i32 {
            var first: i32 = 1;
            var second: i32 = 2;
            var selected = || {};
            if choose_first {
                let capture_first = || { first += 1; };
                selected = capture_first;
            } else {
                let capture_second = || { second += 1; };
                selected = capture_second;
            }
            selected();
            first = 3;
            second = 4;
            return first + second;
        }

        fn main() -> i32 { return run(true); }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

// A branch-local closure transferred before break releases its borrow after
// its last use.
TEST_F(MLATest, RuntimeClosureCaptureBorrowReleasesAfterLoopBreak)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            var selected = || {};
            var enter: bool = true;
            while enter {
                if enter {
                    let capture_value = || { value += 1; };
                    selected = capture_value;
                    break;
                }
                enter = false;
            }
            selected();
            value = 3;
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 3);
}

// A closure carried out by break keeps its captured owner borrowed until
// called.
TEST_F(MLATest, RuntimeClosureCaptureBorrowRemainsLiveAfterLoopBreak)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            var selected = || {};
            var enter: bool = true;
            while enter {
                if enter {
                    let capture_value = || { value += 1; };
                    selected = capture_value;
                    break;
                }
                enter = false;
            }
            value = 3;
            selected();
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

// A range-loop break preserves an outer closure's capture until its last use.
TEST_F(MLATest, RuntimeClosureCaptureBorrowReleasesAfterRangeBreak)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            var selected = || {};
            for item in 0..1 {
                if item == 0 {
                    let capture_value = || { value += 1; };
                    selected = capture_value;
                    break;
                }
            }
            selected();
            value = 3;
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 3);
}

// A range-loop break keeps an outer closure's capture borrowed until called.
TEST_F(MLATest, RuntimeClosureCaptureBorrowRemainsLiveAfterRangeBreak)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 1;
            var selected = || {};
            for item in 0..1 {
                if item == 0 {
                    let capture_value = || { value += 1; };
                    selected = capture_value;
                    break;
                }
            }
            value = 3;
            selected();
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'selected'"), std::string::npos)
        << out;
}

TEST_F(MLATest, OwnershipCannotStoreBorrowingAggregateInGlobal)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            static var saved: PointerBox;
            var value: i32 = 7;
            let boxed = PointerBox { value: &value };
            saved = boxed;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot store in global/static"), std::string::npos)
        << out;
}

TEST_F(MLATest, AggregateBorrowOriginsMergeAcrossIfBranches)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var value: i32 = 7;
            var boxed: PointerBox = PointerBox { value: null };
            let flag: bool = true;
            if flag {
                boxed = PointerBox { value: &value };
            }
            value = 9;
            let pointer = boxed.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'boxed'"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateBorrowOriginSurvivesTernaryAlias)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn wrap(flag: bool) -> PointerBox {
            var value: i32 = 7;
            let boxed: PointerBox = flag
                ? PointerBox { value: &value }
                : PointerBox {};
            return boxed;
        }

        fn main() -> i32 {
            let escaped = wrap(true);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot return aggregate containing a pointer"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, AggregateBorrowOriginsRestoreAfterShadowedScope)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var value: i32 = 7;
            let boxed: PointerBox = PointerBox { value: &value };
            {
                let boxed: PointerBox = PointerBox {};
                let ignored = boxed.value;
            }
            value = 9;
            let pointer = boxed.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'boxed'"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateBorrowOriginsMergeAcrossWhileLoop)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var value: i32 = 7;
            var boxed: PointerBox = PointerBox {};
            var flag: bool = true;
            while flag {
                boxed = PointerBox { value: &value };
                flag = false;
            }
            value = 9;
            let pointer = boxed.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'boxed'"), std::string::npos) << out;
}

// A pointer reassigned inside a possibly executed loop must retain the new
// iteration's borrow origin after the loop join.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossWhileLoop)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            while flag {
                pointer = &mut value;
                flag = false;
            }
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A loop join must keep a mutable pointer's field path on its possible owner.
TEST_F(MLATest, LoopJoinedFieldBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        var outside: i32 = 0;
        fn main() -> i32 {
            var value: Pair = Pair { left: 4, right: 5 };
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            while flag {
                pointer = &mut value.left;
                flag = false;
            }
            value.right = 8;
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A break exit must preserve the same field-sensitive mutable-borrow path.
TEST_F(MLATest, LoopBreakJoinedFieldBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        var outside: i32 = 0;
        fn main() -> i32 {
            var value: Pair = Pair { left: 4, right: 5 };
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            while flag {
                pointer = &mut value.left;
                break;
            }
            value.right = 8;
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A loop break must also keep writes to the borrowed field forbidden.
TEST_F(MLATest, LoopBreakJoinedFieldBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        var outside: i32 = 0;
        fn main() -> i32 {
            var value: Pair = Pair { left: 4, right: 5 };
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            while flag {
                pointer = &mut value.left;
                break;
            }
            value.left = 8;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'value' while borrowed"),
        std::string::npos)
        << out;
}

// A range-loop join must retain field precision for a mutable pointer.
TEST_F(MLATest, RangeLoopJoinedFieldBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        var outside: i32 = 0;
        fn main() -> i32 {
            var value: Pair = Pair { left: 4, right: 5 };
            var pointer: ptr<i32> = &mut outside;
            for i in 0..1 {
                pointer = &mut value.left;
            }
            value.right = 8;
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A range-loop join must not allow writes through the borrowed field path.
TEST_F(MLATest, RangeLoopJoinedFieldBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        var outside: i32 = 0;
        fn main() -> i32 {
            var value: Pair = Pair { left: 4, right: 5 };
            var pointer: ptr<i32> = &mut outside;
            for i in 0..1 {
                pointer = &mut value.left;
            }
            value.left = 8;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'value' while borrowed"),
        std::string::npos)
        << out;
}

// A list-iteration exit must preserve precise paths for mutable pointer
// borrows.
TEST_F(MLATest, ListLoopJoinedFieldBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        var outside: i32 = 0;
        fn main() -> i32 {
            var value: Pair = Pair { left: 4, right: 5 };
            var pointer: ptr<i32> = &mut outside;
            let items: list<i32> = [1];
            for item in items {
                pointer = &mut value.left;
            }
            value.right = 8;
            unsafe { return *pointer; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A child reborrow of a joined pointer must preserve owner-specific field
// paths.
TEST_F(MLATest, JoinedFieldReborrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &mut first.left;
            let flag: bool = true;
            if flag {
                pointer = &mut first.left;
            } else {
                pointer = &mut second.left;
            }
            let child: ptr<i32> = &mut *pointer;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A child reborrow of a joined pointer must block every possible borrowed
// field.
TEST_F(MLATest, JoinedFieldReborrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &mut first.left;
            let flag: bool = true;
            if flag {
                pointer = &mut first.left;
            } else {
                pointer = &mut second.left;
            }
            let child: ptr<i32> = &mut *pointer;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A shared child reborrow must block writes to every possible joined owner.
TEST_F(MLATest, JoinedSharedFieldReborrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &mut first.left;
            let flag: bool = true;
            if flag {
                pointer = &mut first.left;
            } else {
                pointer = &mut second.left;
            }
            let child: ptr<i32> = &*pointer;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A shared reborrow of a joined pointer may leave sibling fields mutable.
TEST_F(MLATest, JoinedSharedFieldReborrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &mut first.left;
            let flag: bool = true;
            if flag {
                pointer = &mut first.left;
            } else {
                pointer = &mut second.left;
            }
            let child: ptr<i32> = &*pointer;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A shared joined reborrow must also suspend writes through its parent pointer.
TEST_F(MLATest, JoinedSharedFieldReborrowBlocksParentWrite)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &mut first.left;
            let flag: bool = true;
            if flag {
                pointer = &mut first.left;
            } else {
                pointer = &mut second.left;
            }
            let child: ptr<i32> = &*pointer;
            unsafe { *pointer = 8; }
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'child'"), std::string::npos) << out;
}

// A nested shared reborrow must retain every owner of a joined shared pointer.
TEST_F(MLATest, NestedSharedJoinedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                pointer = &first.left;
            } else {
                pointer = &second.left;
            }
            let child: ptr<i32> = &*pointer;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A nested shared reborrow still leaves sibling fields available for writes.
TEST_F(MLATest, NestedSharedJoinedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var pointer: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                pointer = &first.left;
            } else {
                pointer = &second.left;
            }
            let child: ptr<i32> = &*pointer;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A nested shared reborrow must retain local origins for opaque-call checks.
TEST_F(MLATest, NestedSharedJoinedBorrowCannotEscapeLocalOrigin)
{
    std::string code = R"(
        extern fn retain_pointer(pointer: ptr<i32>) -> void;

        var global_value: i32 = 11;
        fn main() -> i32 {
            var local_value: i32 = 7;
            let flag: bool = true;
            var parent: ptr<i32> = &global_value;
            if flag {
                parent = &global_value;
            } else {
                parent = &local_value;
            }
            let child: ptr<i32> = &*parent;
            retain_pointer(child);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot pass a borrow of local storage"),
        std::string::npos)
        << out;
}

// A ternary shared join must preserve field precision through reborrowing.
TEST_F(MLATest, NestedTernarySharedJoinedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let flag: bool = true;
            let parent: ptr<i32> = flag ? &first.left : &second.left;
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A ternary shared join must reject writes to any possible borrowed field.
TEST_F(MLATest, NestedTernarySharedJoinedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let flag: bool = true;
            let parent: ptr<i32> = flag ? &first.left : &second.left;
            let child: ptr<i32> = &*parent;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A loop-joined shared pointer must retain field precision when reborrowed.
TEST_F(MLATest, NestedLoopSharedJoinedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var again: bool = true;
            var parent: ptr<i32> = &first.left;
            while again {
                parent = &second.left;
                again = false;
            }
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// A loop-joined shared pointer must reject writes to every possible target.
TEST_F(MLATest, NestedLoopSharedJoinedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var again: bool = true;
            var parent: ptr<i32> = &first.left;
            while again {
                parent = &second.left;
                again = false;
            }
            let child: ptr<i32> = &*parent;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A break-carried shared pointer must preserve its disjoint field path.
TEST_F(MLATest, NestedLoopBreakSharedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let again: bool = true;
            var parent: ptr<i32> = &first.left;
            while again {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// A break-carried shared pointer must still reject overlapping writes.
TEST_F(MLATest, NestedLoopBreakSharedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let again: bool = true;
            var parent: ptr<i32> = &first.left;
            while again {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A range-loop break must keep precise shared-pointer owner paths.
TEST_F(MLATest, NestedRangeBreakSharedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for i in 0..2 {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// A range-loop join must retain precise paths for its zero-iteration owner.
TEST_F(MLATest, NestedRangeBreakSharedBorrowPreservesIncomingOwnerSibling)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for i in 0..2 {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            first.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// The zero-iteration path must retain the incoming owner's overlapping loan.
TEST_F(MLATest, NestedRangeBreakSharedBorrowRejectsIncomingOwnerOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for i in 0..2 {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            first.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'first' while borrowed"),
        std::string::npos)
        << out;
}

// A range-loop break must still reject writes to the borrowed field.
TEST_F(MLATest, NestedRangeBreakSharedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for i in 0..2 {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A match-joined shared pointer must preserve precise paths when reborrowed.
TEST_F(MLATest, NestedMatchSharedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let choice: i32 = 1;
            let parent: ptr<i32> = match choice {
                1 => &first.left,
                _ => &second.left
            };
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A match-joined shared pointer must also reject overlap on each alternative.
TEST_F(MLATest, NestedMatchSharedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            let choice: i32 = 1;
            let parent: ptr<i32> = match choice {
                1 => &first.left,
                _ => &second.left
            };
            let child: ptr<i32> = &*parent;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A list-loop break must preserve field precision for a nested shared borrow.
TEST_F(MLATest, NestedListBreakSharedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var values: list<i32> = [1];
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for item in values {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// The list-loop exit join must preserve the incoming owner's sibling path.
TEST_F(MLATest, NestedListBreakSharedBorrowPreservesIncomingOwnerSibling)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var values: list<i32> = [1];
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for item in values {
                parent = &second.left;
                break;
            }
            let child: ptr<i32> = &*parent;
            first.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// A range-loop continue must preserve shared-pointer field precision too.
TEST_F(MLATest, NestedRangeContinueSharedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for i in 0..2 {
                parent = &second.left;
                continue;
            }
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// A range-loop continue must not lose overlap checks on its pointer target.
TEST_F(MLATest, NestedRangeContinueSharedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for i in 0..2 {
                parent = &second.left;
                continue;
            }
            let child: ptr<i32> = &*parent;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A list-loop continue must preserve shared-pointer field precision.
TEST_F(MLATest, NestedListContinueSharedBorrowPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var values: list<i32> = [1];
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for item in values {
                parent = &second.left;
                continue;
            }
            let child: ptr<i32> = &*parent;
            second.right = 8;
            unsafe { return *child; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 6);
}

// A list-loop continue must keep rejecting writes to the borrowed place.
TEST_F(MLATest, NestedListContinueSharedBorrowRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var values: list<i32> = [1];
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            for item in values {
                parent = &second.left;
                continue;
            }
            let child: ptr<i32> = &*parent;
            second.left = 8;
            unsafe { return *child; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// Copying a joined shared pointer must retain every possible owner.
TEST_F(MLATest, AliasOfJoinedSharedPointerRejectsOverlappingAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let copied: ptr<i32> = parent;
            second.left = 8;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A copied joined shared pointer should still allow disjoint field access.
TEST_F(MLATest, AliasOfJoinedSharedPointerPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let copied: ptr<i32> = parent;
            second.right = 8;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// Copying a joined pointer to multiple fields must retain every field path.
TEST_F(MLATest, AliasOfSameOwnerJoinedSharedPointerRejectsEitherField)
{
    std::string code = R"(
        struct Triple { var first: i32; var second: i32; var third: i32; };
        fn main() -> i32 {
            var value: Triple = Triple { first: 1, second: 2, third: 3 };
            let flag: bool = true;
            var parent: ptr<i32> = &value.first;
            if flag {
                parent = &value.first;
            } else {
                parent = &value.second;
            }
            let copied: ptr<i32> = parent;
            value.second = 9;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'value' while borrowed"),
        std::string::npos)
        << out;
}

// Multiple possible fields in one owner must not block a disjoint sibling.
TEST_F(MLATest, AliasOfSameOwnerJoinedSharedPointerPreservesDisjointField)
{
    std::string code = R"(
        struct Triple { var first: i32; var second: i32; var third: i32; };
        fn main() -> i32 {
            var value: Triple = Triple { first: 1, second: 2, third: 3 };
            let flag: bool = true;
            var parent: ptr<i32> = &value.first;
            if flag {
                parent = &value.first;
            } else {
                parent = &value.second;
            }
            let copied: ptr<i32> = parent;
            value.third = 9;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 1);
}

// A pointer field must preserve every owner of a joined shared pointer.
TEST_F(MLATest, AggregateFieldOfJoinedSharedPointerRejectsOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerBox { var value: ptr<i32>; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let boxed: PointerBox = PointerBox { value: parent };
            let copied: ptr<i32> = boxed.value;
            second.left = 8;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A stored joined pointer should keep disjoint fields writable after reading.
TEST_F(MLATest, AggregateFieldOfJoinedSharedPointerPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct PointerBox { var value: ptr<i32>; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let boxed: PointerBox = PointerBox { value: parent };
            let copied: ptr<i32> = boxed.value;
            second.right = 8;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// Aggregate fields must retain multiple possible places in one owner.
TEST_F(MLATest, AggregateFieldOfSameOwnerJoinedPointerRejectsEitherField)
{
    std::string code = R"(
        struct Triple { var first: i32; var second: i32; var third: i32; };
        struct PointerBox { var value: ptr<i32>; };
        fn main() -> i32 {
            var value: Triple = Triple { first: 1, second: 2, third: 3 };
            let flag: bool = true;
            var parent: ptr<i32> = &value.first;
            if flag {
                parent = &value.first;
            } else {
                parent = &value.second;
            }
            let boxed: PointerBox = PointerBox { value: parent };
            let copied: ptr<i32> = boxed.value;
            value.second = 9;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'value' while borrowed"),
        std::string::npos)
        << out;
}

// Aggregate paths should allow a third field outside every possible borrow.
TEST_F(MLATest, AggregateFieldOfSameOwnerJoinedPointerPreservesDisjointField)
{
    std::string code = R"(
        struct Triple { var first: i32; var second: i32; var third: i32; };
        struct PointerBox { var value: ptr<i32>; };
        fn main() -> i32 {
            var value: Triple = Triple { first: 1, second: 2, third: 3 };
            let flag: bool = true;
            var parent: ptr<i32> = &value.first;
            if flag {
                parent = &value.first;
            } else {
                parent = &value.second;
            }
            let boxed: PointerBox = PointerBox { value: parent };
            let copied: ptr<i32> = boxed.value;
            value.third = 9;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 1);
}

// Nested aggregate fields must carry every owner of a joined shared pointer.
TEST_F(MLATest, NestedAggregateFieldOfJoinedSharedPointerRejectsOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct InnerBox { var value: ptr<i32>; };
        struct OuterBox { var inner: InnerBox; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let boxed: OuterBox = OuterBox {
                inner: InnerBox { value: parent }
            };
            let copied: ptr<i32> = boxed.inner.value;
            second.left = 8;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// Nested aggregate paths still allow siblings outside the joined pointer.
TEST_F(MLATest, NestedAggregateFieldOfJoinedSharedPointerPreservesDisjoint)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        struct InnerBox { var value: ptr<i32>; };
        struct OuterBox { var inner: InnerBox; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let boxed: OuterBox = OuterBox {
                inner: InnerBox { value: parent }
            };
            let copied: ptr<i32> = boxed.inner.value;
            second.right = 8;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A list element must retain all owners of a stored joined shared pointer.
TEST_F(MLATest, ListElementOfJoinedSharedPointerRejectsOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            var pointers: list<ptr<i32>> = [];
            pointers.push(parent);
            let copied: ptr<i32> = pointers[0];
            second.left = 8;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// Extracting a stored joined pointer should preserve sibling-field access.
TEST_F(MLATest, ListElementOfJoinedSharedPointerPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            var pointers: list<ptr<i32>> = [];
            pointers.push(parent);
            let copied: ptr<i32> = pointers[0];
            second.right = 8;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A map value must retain every owner of a joined shared pointer.
TEST_F(MLATest, MapValueOfJoinedSharedPointerRejectsOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let pointers: map<str8, ptr<i32>> = {"value": parent};
            let copied: ptr<i32> = pointers["value"];
            second.left = 8;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A map-held joined pointer should leave disjoint sibling fields writable.
TEST_F(MLATest, MapValueOfJoinedSharedPointerPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let pointers: map<str8, ptr<i32>> = {"value": parent};
            let copied: ptr<i32> = pointers["value"];
            second.right = 8;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A tuple element must retain every owner of a joined shared pointer.
TEST_F(MLATest, TupleElementOfJoinedSharedPointerRejectsOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let pointers: tuple<ptr<i32>, i32> = (parent, 0);
            let copied: ptr<i32> = pointers.0;
            second.left = 8;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A tuple-held joined pointer must allow writes to disjoint sibling fields.
TEST_F(MLATest, TupleElementOfJoinedSharedPointerPreservesDisjointAccess)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let pointers: tuple<ptr<i32>, i32> = (parent, 0);
            let copied: ptr<i32> = pointers.0;
            second.right = 8;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// A fixed-array element must retain all owners of a joined shared pointer.
TEST_F(MLATest, FixedArrayElementOfJoinedSharedPointerRejectsOverlap)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let pointers: array<ptr<i32>, 1> = {parent};
            let copied: ptr<i32> = pointers[0];
            parent = &first.right;
            second.left = 8;
            unsafe { return *copied; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cannot assign to field of 'second' while borrowed"),
        std::string::npos)
        << out;
}

// A fixed-array joined pointer should leave a disjoint sibling writable.
TEST_F(MLATest, FixedArrayElementOfJoinedSharedPointerPreservesDisjoint)
{
    std::string code = R"(
        struct Pair { var left: i32; var right: i32; };
        fn main() -> i32 {
            var first: Pair = Pair { left: 4, right: 5 };
            var second: Pair = Pair { left: 6, right: 7 };
            var parent: ptr<i32> = &first.left;
            let flag: bool = true;
            if flag {
                parent = &first.left;
            } else {
                parent = &second.left;
            }
            let pointers: array<ptr<i32>, 1> = {parent};
            let copied: ptr<i32> = pointers[0];
            parent = &first.right;
            second.right = 8;
            unsafe { return *copied; }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 4);
}

// An if-join must retain either possible owner of a reassigned mutable pointer.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossDifferentIfOwners)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var other: i32 = 5;
            var pointer: ptr<i32> = &mut outside;
            let flag: bool = true;
            if flag {
                pointer = &mut value;
            } else {
                pointer = &mut other;
            }
            value = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// Every possible owner in a mutable pointer join must remain protected.
TEST_F(MLATest, PointerBorrowOriginsProtectSecondIfOwnerAfterJoin)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var other: i32 = 5;
            var pointer: ptr<i32> = &mut outside;
            let flag: bool = true;
            if flag {
                pointer = &mut value;
            } else {
                pointer = &mut other;
            }
            other = 9;
            unsafe { return *pointer; }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'other' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// Borrow state from a loop path ending in break must be joined at loop exit.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossWhileBreak)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            while true {
                pointer = &mut value;
                break;
            }
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A pointer local to the loop body must stop borrowing when break exits scope.
TEST_F(MLATest, LoopBodyPointerBorrowEndsOnBreak)
{
    std::string code = R"(
        fn main() -> i32 {
            var value: i32 = 7;
            while true {
                let pointer: ptr<i32> = &mut value;
                break;
            }
            value = 9;
            return value;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 9);
}

// A borrow created before a conditional break remains possible after the join.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossConditionalBreak)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            while true {
                if flag {
                    pointer = &mut value;
                    break;
                } else {
                    break;
                }
            }
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A range-loop conditional break must retain borrows from its exiting arm.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossRangeLoopBreak)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            for i in 0..1 {
                if flag {
                    pointer = &mut value;
                    break;
                } else {
                    break;
                }
            }
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A list-loop conditional break must retain borrows from its exiting arm.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossListLoopBreak)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            var flag: bool = true;
            let values: list<i32> = [1];
            for item in values {
                if flag {
                    pointer = &mut value;
                    break;
                } else {
                    break;
                }
            }
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A while-loop continue path must preserve a borrow that can reach loop exit.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossWhileContinue)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            while true {
                pointer = &mut value;
                continue;
            }
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

// A range-loop continue must preserve borrows on the eventual loop exit.
TEST_F(MLATest, PointerBorrowOriginsMergeAcrossRangeLoopContinue)
{
    std::string code = R"(
        var outside: i32 = 3;

        fn main() -> i32 {
            var value: i32 = 7;
            var pointer: ptr<i32> = &mut outside;
            for i in 0..1 {
                pointer = &mut value;
                continue;
            }
            value = 9;
            return *pointer;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "cannot assign to 'value' directly while mutably "
            "borrowed by 'pointer'"),
        std::string::npos)
        << out;
}

TEST_F(MLATest, AggregateBorrowOriginsMergeAcrossRangeFor)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };

        fn main() -> i32 {
            var value: i32 = 7;
            var boxed: PointerBox = PointerBox {};
            for i in 0..1 {
                boxed = PointerBox { value: &value };
            }
            value = 9;
            let pointer = boxed.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'boxed'"), std::string::npos) << out;
}

TEST_F(MLATest, AggregateBorrowOriginsTrackMatchResult)
{
    std::string code = R"(
        struct PointerBox { var value: ptr<i32>; };
        enum Choice : i32 { Borrow = 1, Skip = 2 };

        fn main() -> i32 {
            var value: i32 = 7;
            let choice: Choice = Choice::Borrow;
            let boxed: PointerBox = match choice {
                Choice::Borrow => PointerBox { value: &value },
                _ => PointerBox {}
            };
            value = 9;
            let pointer = boxed.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed by 'boxed'"), std::string::npos) << out;
}

TEST_F(MLATest, OwnershipCannotStorePointerBorrowingLocalFieldInGlobalPointer)
{
    std::string code = R"(
        struct Box {
            var value: i32;
        };
        var GP: ptr<i32>;

        fn main() -> i32 {
            let b: Box = Box { value: 7 };
            GP = &b.value;
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot store in global/static"), std::string::npos);
}

// ============================================================================
// Enum Backing Type Tests
// ============================================================================

TEST_F(MLATest, EnumWithI64BackingCompilesAndMatches)
{
    std::string code = R"(
        enum Big : i64 {
            Min = -42,
            Max = 9223372036854775807
        };

        fn as_i32(v: Big) -> i32 {
            return match v {
                Big::Min => -1,
                Big::Max => 1,
                _ => 0
            };
        }

        fn main() -> i32 {
            let a: Big = Big::Max;
            let b: Big = Big::Min;
            if as_i32(a) != 1 { return 1; }
            if as_i32(b) != -1 { return 2; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, RuntimeClosurePreservesEnumParameterIdentity)
{
    std::string code = R"(
        enum Status : u8 {
            Idle = 1,
            Ready = 2
        };
        fn main() -> i32 {
            let describe: fn(Status) -> i32 = |status| {
                println!("{}", status);
                return match status {
                    Status::Idle => 3,
                    Status::Ready => 7,
                    _ => 0
                };
            };
            return describe(Status::Ready) - 7;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Status::Ready\n");
}

TEST_F(MLATest, EnumWithU8BackingCompiles)
{
    std::string code = R"(
        enum Small : u8 {
            Zero = 0,
            Max = 255
        };

        fn main() -> i32 {
            let s: Small = Small::Max;
            let z: Small = Small::Zero;
            let ok: i32 = match s {
                Small::Max => 1,
                _ => 0
            };
            let ok2: i32 = match z {
                Small::Zero => 1,
                _ => 0
            };
            return 2 - (ok + ok2);
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, EnumExplicitValueOverflowFails)
{
    std::string code = R"(
        enum TooBig : u8 {
            A = 256
        };

        fn main() -> i32 {
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("does not fit backing type 'u8'"), std::string::npos);
}

TEST_F(MLATest, EnumImplicitValueOverflowFails)
{
    std::string code = R"(
        enum TooBig : u8 {
            A = 255,
            B
        };

        fn main() -> i32 {
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("implicit value overflows backing type 'u8'"),
        std::string::npos);
}

TEST_F(MLATest, EnumCanReferenceCompatibleOtherEnumValue)
{
    std::string code = R"(
        enum U8Enum : u8 {
            Invalid = 1,
            Success = 2
        };

        enum WideEnum : u32 {
            Invalid = U8Enum::Invalid,
            Success = U8Enum::Success,
            Next
        };

        fn main() -> i32 {
            let a: WideEnum = WideEnum::Invalid;
            let b: WideEnum = WideEnum::Success;
            let c: WideEnum = WideEnum::Next;
            let s: i32 = match a {
                WideEnum::Invalid => 1,
                WideEnum::Success => 2,
                WideEnum::Next => 3,
                _ => 0
            };
            let s2: i32 = match b {
                WideEnum::Invalid => 1,
                WideEnum::Success => 2,
                WideEnum::Next => 3,
                _ => 0
            };
            let s3: i32 = match c {
                WideEnum::Invalid => 1,
                WideEnum::Success => 2,
                WideEnum::Next => 3,
                _ => 0
            };
            if s != 1 { return 1; }
            if s2 != 2 { return 2; }
            if s3 != 3 { return 3; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, EnumReferenceValueMustFitTargetBackingType)
{
    std::string code = R"(
        enum Big : u32 {
            Huge = 300
        };

        enum Small : u8 {
            Bad = Big::Huge
        };

        fn main() -> i32 {
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("enum types/values are not compatible"),
        std::string::npos);
    EXPECT_NE(
        out.find("cannot fit in enum 'Small' backing type 'u8'"),
        std::string::npos);
}

TEST_F(MLATest, EnumReferenceSignedToUnsignedIncompatibilityHasClearError)
{
    std::string code = R"(
        enum Signed : i8 {
            Neg = -1
        };

        enum Unsigned : u8 {
            Bad = Signed::Neg
        };

        fn main() -> i32 {
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("enum types/values are not compatible"),
        std::string::npos);
    EXPECT_NE(out.find("Signed::Neg"), std::string::npos);
    EXPECT_NE(out.find("backing type 'u8'"), std::string::npos);
}

TEST_F(MLATest, EnumPrintsAsStringInPrintln)
{
    std::string code = R"(
        enum Status : u8 {
            Invalid = 1,
            Success = 2
        };

        fn main() -> i32 {
            println!("{}", Status::Invalid);
            let s: Status = Status::Success;
            println!("{}", s);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Status::Invalid\nStatus::Success\n");
}

TEST_F(MLATest, EnumPrintUnknownValueFallback)
{
    std::string code = R"(
        enum Status : u8 {
            Invalid = 1,
            Success = 2
        };

        fn main() -> i32 {
            let raw: i32 = 77;
            let s: Status = raw;
            println!("{}", s);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "<Status:unknown>\n");
}

TEST_F(MLATest, EnumForLoopIteratesInDeclarationOrder)
{
    std::string code = R"(
        enum Status : u8 {
            Idle = 1,
            Busy = 2,
            Done = 3
        };

        fn main() -> i32 {
            for (i, s) in Status.enumerate() {
                println!("{}:{}", i, s);
            }
            return 0;
        }
    )";
    EXPECT_EQ(
        compileAndRun(code),
        "0:Status::Idle\n1:Status::Busy\n2:Status::Done\n");
}

TEST_F(MLATest, EnumBackingValidExampleCompilesAndRuns)
{
    fs::path repoRoot = fs::path(__FILE__).parent_path().parent_path();
    fs::path src = repoRoot / "examples" / "enum_backing_valid.mla";
    int rc = 0;
    std::string out = compilePathCapture(src, rc);
    EXPECT_EQ(rc, 0) << out;
    EXPECT_EQ(runExitCode(), 0);
}

TEST_F(MLATest, EnumBackingExplicitOverflowExampleFails)
{
    fs::path repoRoot = fs::path(__FILE__).parent_path().parent_path();
    fs::path src = repoRoot / "examples" / "enum_backing_fail_u8_explicit.mla";
    int rc = 0;
    std::string out = compilePathCapture(src, rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("does not fit backing type 'u8'"), std::string::npos);
}

TEST_F(MLATest, EnumBackingImplicitOverflowExampleFails)
{
    fs::path repoRoot = fs::path(__FILE__).parent_path().parent_path();
    fs::path src = repoRoot / "examples" / "enum_backing_fail_u8_implicit.mla";
    int rc = 0;
    std::string out = compilePathCapture(src, rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("implicit value overflows backing type 'u8'"),
        std::string::npos);
}

TEST_F(MLATest, EnumPrintExampleCompilesAndRuns)
{
    fs::path repoRoot = fs::path(__FILE__).parent_path().parent_path();
    fs::path src = repoRoot / "examples" / "enum_print_demo.mla";
    int rc = 0;
    std::string out = compilePathCapture(src, rc);
    EXPECT_EQ(rc, 0) << out;
    EXPECT_EQ(
        run(),
        "literal: Status::Invalid\n"
        "variable: Status::Success\n"
        "iterate:\n"
        "  [0] Status::Invalid\n"
        "  [1] Status::Busy\n"
        "  [2] Status::Success\n"
        "unknown: <Status:unknown>\n");
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(MLATest, FizzBuzz)
{
    std::string code = R"(
        fn main() -> i32 {
            for i in 1..16 {
                var fizz: i32 = i / 3;
                var buzz: i32 = i / 5;
                fizz = fizz * 3;
                buzz = buzz * 5;

                if fizz == i: {
                    if buzz == i: {
                        println!("FizzBuzz");
                    } else: {
                        println!("Fizz");
                    }
                } else if buzz == i: {
                    println!("Buzz");
                } else: {
                    println!("{}", i);
                }
            }
            return 0;
        }
    )";
    std::string expected = "1\n2\nFizz\n4\nBuzz\nFizz\n7\n8\nFizz\nBuzz\n11\nFi"
                           "zz\n13\n14\nFizzBuzz\n";
    EXPECT_EQ(compileAndRun(code), expected);
}

TEST_F(MLATest, SumOfSquares)
{
    std::string code = R"(
        fn square(x: i32) -> i32 {
            return x * x;
        }

        fn main() -> i32 {
            var sum: i32 = 0;
            for i in 1..6 {
                sum = sum + square(i);
            }
            println!("{}", sum);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "55\n"); // 1 + 4 + 9 + 16 + 25
}

TEST_F(MLATest, IsPrime)
{
    std::string code = R"(
        fn is_prime(n: i32) -> i32 {
            if n < 2: {
                return 0;
            }
            var i: i32 = 2;
            for i in 2..n {
                var remainder: i32 = n / i;
                remainder = remainder * i;
                if remainder == n: {
                    return 0;
                }
            }
            return 1;
        }

        fn main() -> i32 {
            for n in 2..20 {
                if is_prime(n): {
                    println!("{}", n);
                }
            }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "2\n3\n5\n7\n11\n13\n17\n19\n");
}

TEST_F(MLATest, AssertRuntimePasses)
{
    std::string code = R"(
        fn main() -> i32 {
            assert!(1);
            assert!(2 + 2 == 4);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, AssertRuntimeFailurePrintsMessage)
{
    std::string code = R"(
        fn main() -> i32 {
            assert!(0);
            return 0;
        }
    )";
    writeSource(code);
    ASSERT_TRUE(compile(true));
    const std::string err = runStderr();
    EXPECT_NE(err.find("assert! failed"), std::string::npos);
}

TEST_F(MLATest, StaticAssertCompileTimeTruePasses)
{
    std::string code = R"(
        fn main() -> i32 {
            static_assert!(2 + 2 == 4);
            static_assert!(1 || 0);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StaticAssertCompileTimeFalseFails)
{
    std::string code = R"(
        fn main() -> i32 {
            static_assert!(2 + 2 == 5);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("static_assert! failed"), std::string::npos);
}

TEST_F(MLATest, StaticAssertNonConstExpressionFails)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 1;
            static_assert!(x > 0);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("static_assert! requires a compile-time boolean expression"),
        std::string::npos);
}

TEST_F(MLATest, CexprExpressionInitializesRuntimeValue)
{
    std::string code = R"(
        cexpr fn add(a: i64, b: i64) -> i64 {
            return a + b;
        }

        fn main() -> i32 {
            let value: i64 = cexpr(add(2, 5) * 3);
            return value == 21 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CexprFunctionTwiceI32EvaluatesAtCompileTime)
{
    std::string code = R"(
        cexpr fn twice(x: i32) -> i32 {
            return x * 2;
        }

        fn main() -> i32 {
            static_assert!(twice(21) == 42);
            let value: i32 = cexpr(twice(21));
            return value == 42 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CexprFunctionEvaluatesFloatArithmeticAtCompileTime)
{
    std::string code = R"(
        cexpr fn midpoint(a: f32, b: f32) -> f32 {
            return (a + b) / 2.0f;
        }

        cexpr fn wide_scale(x: f64) -> f64 {
            return x * 1.5;
        }

        fn main() -> i32 {
            static_assert!(midpoint(2.0f, 4.0f) == 3.0f);
            static_assert!(wide_scale(4.0) == 6.0);
            let value: f32 = cexpr(midpoint(8.0f, 10.0f));
            return value > 8.9f && value < 9.1f ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CexprDeclarationProvidesCompileTimeValue)
{
    std::string code = R"(
        cexpr N: i32 = 21;
        cexpr Scale: f32 = 1.5f;

        fn main() -> i32 {
            static_assert!(N * 2 == 42);
            static_assert!(Scale > 1.4f && Scale < 1.6f);
            let value: i32 = cexpr(N * 2);
            let runtime_value: i32 = N;
            return value == 42 && runtime_value == 21 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, LocalCexprDeclarationProvidesCompileTimeValue)
{
    std::string code = R"(
        fn main() -> i32 {
            cexpr Local: i32 = 7 * 6;
            static_assert!(Local == 42);
            let value: i32 = cexpr(Local);
            return value == 42 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CexprDeclarationRejectsRuntimeInitializer)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 1;
            cexpr Bad: i32 = x;
            return Bad;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("runtime variable is not available in cexpr: 'x'"),
        std::string::npos);
}

TEST_F(MLATest, CexprIfSelectsCompileTimeBranch)
{
    std::string code = R"(
        cexpr UseFast: bool = true;

        fn main() -> i32 {
            cexpr Value: i32 = 21;
            cexpr if UseFast {
                return cexpr(Value * 2);
            } else {
                return missing_runtime_symbol();
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 42);
}

TEST_F(MLATest, CexprIfSupportsElseBranch)
{
    std::string code = R"(
        cexpr UseFast: bool = false;

        fn main() -> i32 {
            cexpr if UseFast {
                return missing_runtime_symbol();
            } else {
                return 7;
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, CexprElseIfSelectsCompileTimeBranchAndIgnoresOtherReturnTypes)
{
    std::string code = R"(
        fn selected_value() {
            cexpr if false {
                return "wrong branch type";
            } else if true {
                return 42;
            } else {
                return "also wrong branch type";
            }
        }

        fn main() -> i32 {
            return selected_value() == 42 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericMutableReferenceInfersElementType)
{
    const std::string code = R"(
        generic<T>
        fn count_values(values: &mut list<T>) -> i64 {
            return values.len();
        }

        fn main() -> i32 {
            var values: list<i64> = [42];
            let count: i64 = count_values(&mut values);
            if count != 1 { return 1; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericCexprFunctionDispatchesOnTypeId)
{
    std::string code = R"(
        alias SomeType = i64;
        alias SomeOtherType = f64;
        alias SomeItemTypeY = i64;

        cexpr fn for_i64(item: i64) -> i64 {
            return item * 2;
        }

        cexpr fn for_f64(item: f64) -> i64 {
            return item > 0.0 ? 30 : 0;
        }

        cexpr fn for_y(item: i64) -> i64 {
            return item + 7;
        }

        cexpr fn fallback() -> i64 {
            return 5;
        }

        generic<T, Y>
        cexpr fn pick(item: T, item2: Y) {
            cexpr if type_id(T) == SomeType {
                return for_i64(item);
            } else if type_id(T) == SomeOtherType {
                return for_f64(item);
            } else if type_id(T) != SomeType && type_id(Y) == SomeItemTypeY {
                return for_y(item2);
            } else {
                return fallback();
            }
        }

        fn main() -> i32 {
            cexpr A: i64 = pick(21, 0);
            cexpr B: i64 = pick(1.0, 0);
            cexpr C: i64 = pick(1.0f, 8);
            cexpr D: i64 = pick(1.0f, 2.0f);
            return cexpr(A + B + C + D) == 92 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, GenericCexprFunctionDispatchesOnStructTypeId)
{
    std::string code = R"(
        struct Marker {
            var value: i32;
        };

        struct Other {
            var value: i32;
        };

        generic<T>
        cexpr fn classify(item: T) -> i64 {
            cexpr if type_id(T) == Marker {
                return 7;
            } else if type_id(T) == Other {
                return 9;
            } else {
                return 1;
            }
        }

        fn main() -> i32 {
            let marker: Marker = Marker { value: 3 };
            let other: Other = Other { value: 4 };
            cexpr A: i64 = classify(marker);
            cexpr B: i64 = classify(other);
            return cexpr(A + B) == 16 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CexprStructValueReportsUnsupportedValueEvaluation)
{
    std::string code = R"(
        struct Marker {
            var value: i32;
        };

        fn main() -> i32 {
            let marker: Marker = Marker { value: 3 };
            let value: Marker = cexpr(marker);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "struct values in cexpr are only supported for generic "
            "type dispatch"),
        std::string::npos);
}

TEST_F(MLATest, CexprIfRejectsRuntimeCondition)
{
    std::string code = R"(
        fn main() -> i32 {
            let runtime_flag: bool = true;
            cexpr if runtime_flag {
                return 1;
            }
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("runtime variable is not available in cexpr: 'runtime_flag'"),
        std::string::npos);
}

TEST_F(MLATest, StaticAssertAcceptsCexprFunctionCall)
{
    std::string code = R"(
        cexpr fn square(x: i64) -> i64 {
            return x * x;
        }

        fn main() -> i32 {
            static_assert!(square(6) == 36);
            static_assert!(cexpr(square(3) + 1) == 10);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, StaticAssertAcceptsPostfixCexprFunctionForm)
{
    std::string code = R"(
        fn square(x: i64) cexpr -> i64 {
            return x * x;
        }

        fn main() -> i32 {
            static_assert!(square(7) == 49);
            let value: i64 = cexpr(square(4) + 1);
            return value == 17 ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, CexprRejectsNonCexprFunctionCall)
{
    std::string code = R"(
        fn runtime_add(a: i64, b: i64) -> i64 {
            return a + b;
        }

        fn main() -> i32 {
            let value: i64 = cexpr(runtime_add(1, 2));
            return value == 3 ? 0 : 1;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("cexpr call requires a matching 'cexpr fn' overload"),
        std::string::npos);
}

TEST_F(MLATest, CexprReportsRuntimeVariableName)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 1;
            let value: i32 = cexpr(x);
            return value;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("runtime variable is not available in cexpr: 'x'"),
        std::string::npos);
}

TEST_F(MLATest, CexprReportsUnsupportedExpressionKind)
{
    std::string code = R"(
        fn main() -> i32 {
            let value: str8 = cexpr("hello");
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("unsupported expression in cexpr"), std::string::npos);
}

TEST_F(MLATest, RawPointerDereferenceOutsideUnsafeFails)
{
    std::string code = R"(
        extern fn raw_i32_ptr() -> ptr<i32>;

        fn main() -> i32 {
            let p: ptr<i32> = raw_i32_ptr();
            return *p;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("dereferencing raw pointer requires an unsafe block"),
        std::string::npos);
}

TEST_F(MLATest, RawPointerDereferenceInsideUnsafeCompiles)
{
    std::string code = R"(
        var G: i32 = 7;

        fn raw_i32_ptr() -> ptr<i32> {
            return &G;
        }

        fn main() -> i32 {
            let p: ptr<i32> = raw_i32_ptr();
            unsafe {
                return *p;
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, KnownNullPointerDereferenceFailsAtCompileTime)
{
    std::string code = R"(
        fn main() -> i32 {
            var p: ptr<i32>;
            unsafe {
                return *p;
            }
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot dereference null pointer"), std::string::npos);
}

TEST_F(MLATest, UnsafeRawPointerDereferenceChecksNullAtRuntime)
{
    std::string code = R"(
        fn maybe_null() -> ptr<i32> {
            var p: ptr<i32>;
            return p;
        }

        fn main() -> i32 {
            let p: ptr<i32> = maybe_null();
            unsafe {
                return *p;
            }
        }
    )";
    EXPECT_NE(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, BorrowedPointerDereferenceOutsideUnsafeStillAllowed)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 7;
            let p: ptr<i32> = &x;
            unsafe {
                return *p;
            }
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, FreeObjsReleasesHeterogeneousStructs)
{
    std::string code = R"(
        struct Session { var value: i32; };
        impl Session {
            fn release(self: Session) -> void {
                let ignored: i32 = self.value;
            }
        }

        struct Project { var value: i64; };
        impl Project {
            fn release(self: Project) -> void {
                let ignored: i64 = self.value;
            }
        }

        fn main() -> i32 {
            var session: Session = Session { value: 1 };
            var project: Project = Project { value: 2 };
            free_objs(session, project);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, VariadicGenericCommaFoldPushesArgumentsInOrder)
{
    std::string code = R"(
        generic<T, Args...>
        fn push_back_vec(values: &mut list<T>, args: Args...) -> void {
            static_assert!((is_constructible<T>(args) && ...));
            (values.push(args), ...);
        }

        fn main() -> i32 {
            var values: list<i32> = [];
            push_back_vec(&mut values, 4, 7, 9);
            if values.len() != 3 { return 1; }
            if values[0] != 4 { return 2; }
            if values[1] != 7 { return 3; }
            if values[2] != 9 { return 4; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, VariadicGenericConstructibilityFoldRejectsBadArgument)
{
    writeSource(R"(
        generic<T, Args...>
        fn push_back_vec(values: &mut list<T>, args: Args...) -> void {
            static_assert!((is_constructible<T>(args) && ...));
            (values.push(args), ...);
        }

        fn main() -> i32 {
            var values: list<i32> = [];
            push_back_vec(&mut values, 4, "not an integer");
            return 0;
        }
    )");
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("static_assert! failed"), std::string::npos);
}

TEST_F(MLATest, VariadicGenericCommaFoldSupportsHeterogeneousReceivers)
{
    std::string code = R"(
        struct Session { var value: i32; };
        impl Session {
            fn release(self: Session) -> void {
                let ignored: i32 = self.value;
            }
        }

        struct Project { var value: i64; };
        impl Project {
            fn release(self: Project) -> void {
                let ignored: i64 = self.value;
            }
        }

        generic<Args...>
        fn release_all(args: Args...) -> void {
            (args.release(), ...);
        }

        fn main() -> i32 {
            var session: Session = Session { value: 1 };
            var project: Project = Project { value: 2 };
            release_all(session, project);
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, FreeObjsRejectsObjectWithoutReleaseMethod)
{
    std::string code = R"(
        struct Session { var value: i32; };
        impl Session {
            fn release(self: Session) -> void {
                let ignored: i32 = self.value;
            }
        }

        struct Project { var value: i64; };

        fn main() -> i32 {
            var session: Session = Session { value: 1 };
            var project: Project = Project { value: 2 };
            free_objs(session, project);
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find(
            "argument 2 to free_objs must be an object with a "
            "zero-argument release() method"),
        std::string::npos);
}

TEST_F(MLATest, FreeObjsRejectsEmptyPack)
{
    writeSource(R"(
        fn main() -> i32 {
            free_objs();
            return 0;
        }
    )");
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(
        out.find("free_objs expects at least one releasable object"),
        std::string::npos);
}

TEST_F(MLATest, SwitchCasesCanReturnFromFunction)
{
    std::string code = R"(
        fn choose(x: i32) -> i32 {
            switch x {
                case 0: { return 9; }
                default: { return 1; }
            }
        }

        fn main() -> i32 {
            if choose(0) != 9 { return 1; }
            if choose(4) != 1 { return 1; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, NamespaceBlockQualifiesDeclarationsAndLocalTypes)
{
    std::string code = R"(
        namespace geometry::units {
            alias Distance = f32;

            struct Reading {
                let value: Distance = 0.0f;
            };

            fn average(a: Distance, b: Distance) -> Distance {
                return (a + b) / 2.0f;
            }
        }

        fn main() -> i32 {
            let a: geometry::units::Distance = 10.0f;
            let r: geometry::units::Reading =
                geometry::units::Reading {
                    value: geometry::units::average(a, 14.0f)
                };
            if r.value < 11.9f { return 1; }
            if r.value > 12.1f { return 1; }
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

TEST_F(MLATest, NamespaceAliasShortensQualifiedPaths)
{
    std::string code = R"(
        namespace geometry::units {
            alias Distance = f32;

            struct Reading {
                let value: Distance = 0.0f;
            };

            fn average(a: Distance, b: Distance) -> Distance {
                return (a + b) / 2.0f;
            }
        }

        namespace gu = geometry::units;
        alias ga = geometry::units;

        fn main() -> i32 {
            let a: gu::Distance = 10.0f;

            namespace fn_units = geometry::units;
            let direct: fn_units::Reading =
                fn_units::Reading { value: fn_units::average(6.0f, 8.0f) };
            if direct.value < 6.9f { return 1; }

            {
                namespace local = geometry::units;
                let r: local::Reading =
                    local::Reading { value: local::average(a, 14.0f) };
                if r.value < 11.9f { return 1; }
                if r.value > 12.1f { return 1; }
            }

            let b: gu::Reading =
                gu::Reading { value: gu::average(2.0f, 4.0f) };
            let c: ga::Reading =
                ga::Reading { value: ga::average(8.0f, 10.0f) };
            return b.value > 2.9f && c.value > 8.9f ? 0 : 1;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 0);
}

// ============================================================================
// Main
// ============================================================================

TEST_F(MLATest, DiagnosticsInImportedModuleNameModuleFile)
{
    // An empty block in an imported module is reported at the module's file,
    // not at the file given on the command line; the main file's own
    // warnings keep naming the main file.
    {
        std::ofstream helper(testDir + "/helper.mla");
        helper << "pub fn helper_value() -> i32 {\n"
                  "    let x: i32 = 1;\n"
                  "    if x > 0 { }\n"
                  "    return x;\n"
                  "}\n";
    }
    writeSource(
        "mod helper;\n"
        "use helper::helper_value;\n"
        "fn main() -> i32 {\n"
        "    let y: i32 = helper_value();\n"
        "    if y > 5 { }\n"
        "    return y - 1;\n"
        "}\n");
    int rc = 0;
    std::string out = compileCapture(rc);
    ASSERT_EQ(rc, 0) << out;
    std::istringstream lines(out);
    std::string line;
    int helperWarnings = 0;
    int mainWarnings = 0;
    while(std::getline(lines, line))
    {
        if(line.find("empty block") == std::string::npos)
            continue;
        if(line.find("helper.mla:") != std::string::npos)
            helperWarnings++;
        else if(line.find("test.mla:") != std::string::npos)
            mainWarnings++;
    }
    EXPECT_EQ(helperWarnings, 1) << out;
    EXPECT_EQ(mainWarnings, 1) << out;
}

// Source for the mlang -g tests; line numbers matter (see the checks).
static const char* kDebugInfoSource =
    "struct Point { var x: i32; var y: i32; };\n"  // 1
    "fn scale(value: i32, factor: i32) -> i32 {\n" // 2
    "    let result: i32 = value * factor;\n"      // 3
    "    return result;\n"                         // 4
    "}\n"                                          // 5
    "fn main() -> i32 {\n"                         // 6
    "    let p: Point = Point { x: 3, y: 4 };\n"   // 7
    "    var sum: i32 = 0;\n"                      // 8
    "    for i in 0..3 {\n"                        // 9
    "        sum = sum + scale(i32(i), p.x);\n"    // 10
    "    }\n"                                      // 11
    "    {\n"                                      // 12
    "        let inner: i32 = sum + p.y;\n"        // 13
    "        println!(\"{}\", inner);\n"           // 14
    "    }\n"                                      // 15
    "    return 0;\n"                              // 16
    "}\n";

static std::string readTextFile(const fs::path& path)
{
    std::ifstream input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

TEST_F(MLATest, DebugInfoDescribesFunctionsLinesAndVariables)
{
    writeSource(kDebugInfoSource);
    const fs::path irFile = fs::path(testDir) / "debug_info.ll";
    std::string cmd = compilerPath + " -O0 -g -emit-llvm -o " +
                      irFile.string() + " " + sourceFile + " 2>&1";
    ASSERT_EQ(system(cmd.c_str()), 0);
    const std::string ir = readTextFile(irFile);
    EXPECT_NE(ir.find("!DICompileUnit("), std::string::npos) << ir;
    EXPECT_NE(ir.find("\"Debug Info Version\""), std::string::npos);
    EXPECT_NE(ir.find("!DISubprogram(name: \"scale\""), std::string::npos);
    EXPECT_NE(ir.find("!DISubprogram(name: \"main\""), std::string::npos);
    // Parameters with their numbers, locals, the loop variable.
    EXPECT_NE(
        ir.find("!DILocalVariable(name: \"value\", arg: 1"),
        std::string::npos);
    EXPECT_NE(
        ir.find("!DILocalVariable(name: \"factor\", arg: 2"),
        std::string::npos);
    EXPECT_NE(ir.find("!DILocalVariable(name: \"result\""), std::string::npos);
    EXPECT_NE(ir.find("!DILocalVariable(name: \"sum\""), std::string::npos);
    EXPECT_NE(ir.find("!DILocalVariable(name: \"i\""), std::string::npos);
    EXPECT_NE(ir.find("!DILocalVariable(name: \"inner\""), std::string::npos);
    // Struct type with its fields.
    EXPECT_NE(ir.find("name: \"Point\""), std::string::npos);
    EXPECT_NE(
        ir.find("!DIDerivedType(tag: DW_TAG_member, name: \"x\""),
        std::string::npos);
    EXPECT_NE(
        ir.find("!DIDerivedType(tag: DW_TAG_member, name: \"y\""),
        std::string::npos);
    // Statements at their first token: `let` (after an empty rule in the
    // grammar), `return`, and the first statement of a nested block.
    EXPECT_NE(ir.find("!DILocation(line: 3, column: 5"), std::string::npos);
    EXPECT_NE(ir.find("!DILocation(line: 4, column: 5"), std::string::npos);
    EXPECT_NE(ir.find("!DILocation(line: 13, column: 9"), std::string::npos);
    EXPECT_NE(ir.find("!DILexicalBlock("), std::string::npos);
}

TEST_F(MLATest, DebugInfoOnlyWithFlag)
{
    writeSource(kDebugInfoSource);
    const fs::path irFile = fs::path(testDir) / "no_debug_info.ll";
    std::string cmd = compilerPath + " -O0 -emit-llvm -o " + irFile.string() +
                      " " + sourceFile + " 2>&1";
    ASSERT_EQ(system(cmd.c_str()), 0);
    const std::string ir = readTextFile(irFile);
    EXPECT_EQ(ir.find("!DICompileUnit("), std::string::npos);
    EXPECT_EQ(ir.find("!dbg"), std::string::npos);
}

TEST_F(MLATest, DebugInfoDescribesComplexContainerAndFieldTypes)
{
    const fs::path fixture =
        fs::path(__FILE__).parent_path() / "fixtures/mladbg_complex.mla";
    writeSource(readTextFile(fixture));
    const fs::path irFile = fs::path(testDir) / "complex_debug_info.ll";
    const std::string cmd = compilerPath + " -O0 -g -emit-llvm -o " +
                            irFile.string() + " " + sourceFile + " 2>&1";
    ASSERT_EQ(system(cmd.c_str()), 0);
    const std::string ir = readTextFile(irFile);
    // The emitted types must remain usable after module verification.
    EXPECT_NE(ir.find("!DICompileUnit("), std::string::npos) << ir;
    for(const char* name :
        {"Point",
         "Shape",
         "Box_Point",
         "Tagged",
         "list<Point>",
         "array<i32, 3>",
         "map<str8, Point>",
         "tuple<u32, str8>",
         "str16"})
        EXPECT_NE(
            ir.find(std::string("name: \"") + name + "\""),
            std::string::npos)
            << name << "\n"
            << ir;
    for(const char* name : {"len", "data", "keys", "values", "_0", "_1"})
        EXPECT_NE(
            ir.find(std::string("DW_TAG_member, name: \"") + name + "\""),
            std::string::npos)
            << name;
    // A u8 enum must retain its unsigned 8-bit backing type, rather than
    // acquiring the default signed i32 representation during type emission.
    const auto enumAt = ir.find("DW_TAG_enumeration_type, name: \"Color\"");
    ASSERT_NE(enumAt, std::string::npos) << ir;
    EXPECT_NE(ir.substr(enumAt, 200).find("size: 8"), std::string::npos);
    EXPECT_NE(
        ir.find("name: \"u8\", size: 8, encoding: DW_ATE_unsigned"),
        std::string::npos);
    EXPECT_NE(ir.find("DIFlagBitField"), std::string::npos);
    // Inferred map literals must carry their named struct element type too.
    EXPECT_EQ(ir.find("name: \"map<str8, struct>\""), std::string::npos);
    EXPECT_NE(
        ir.find("!DILocalVariable(name: \"inferred_mapping\""),
        std::string::npos);
}

TEST_F(MLATest, DebugInfoProgramRunsTheSame)
{
    writeSource(kDebugInfoSource);
    ASSERT_TRUE(compile(true, "-O0 -g"));
    EXPECT_EQ(run(), "13\n"); // 0 + 3 + 6, plus p.y
}

TEST_F(MLATest, DebugInfoNamesImportedModuleFile)
{
    {
        std::ofstream helper(testDir + "/helper.mla");
        helper << "pub fn helper_value(seed: i32) -> i32 {\n"
                  "    let doubled: i32 = seed * 2;\n"
                  "    return doubled;\n"
                  "}\n";
    }
    writeSource(
        "mod helper;\n"
        "use helper::helper_value;\n"
        "fn main() -> i32 {\n"
        "    return helper_value(0);\n"
        "}\n");
    const fs::path irFile = fs::path(testDir) / "module_debug_info.ll";
    std::string cmd = compilerPath + " -O0 -g -emit-llvm -o " +
                      irFile.string() + " " + sourceFile + " 2>&1";
    ASSERT_EQ(system(cmd.c_str()), 0);
    const std::string ir = readTextFile(irFile);
    EXPECT_NE(ir.find("!DIFile(filename: \"helper.mla\""), std::string::npos)
        << ir;
    EXPECT_NE(
        ir.find("!DISubprogram(name: \"helper_value\""),
        std::string::npos);
    EXPECT_NE(ir.find("!DILocalVariable(name: \"doubled\""), std::string::npos);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    // Allow setting compiler path via command line
    if(argc > 1)
    {
        MLATest::compilerPath = argv[1];
    }

    return RUN_ALL_TESTS();
}
