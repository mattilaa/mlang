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
    bool compile(bool expectSuccess = true)
    {
        std::string cmd = compilerPath + " -o " + outputExe + " " + sourceFile;
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

    std::string compileCapture(int& exitCode)
    {
        std::string cmd = compilerPath + " -o " + outputExe + " " + sourceFile;
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
    EXPECT_EQ(compileAndRun(code),
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
    EXPECT_NE(out.find("string concatenation requires both operands to be string types"),
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
    EXPECT_NE(out.find("string concatenation requires matching operand types"),
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
    EXPECT_NE(out.find("list initializer for type 'list<i32>' uses braces"),
              std::string::npos);
    EXPECT_NE(out.find("use '[' and ']' instead of '{' and '}'"),
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
    EXPECT_NE(out.find("array<list<i32>, 1> capacity is 1"),
              std::string::npos);
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
    EXPECT_NE(out.find("array initializer for field 'items' has 4 elements"),
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
    EXPECT_NE(out.find("array initializer for field 'items' has 4 elements"),
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
    EXPECT_NE(out.find("first() requires a non-empty array"),
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
    EXPECT_NE(out.find("map.extend() argument key/value types do not match "
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

TEST_F(MLATest, PointerAccess)
{
    std::string code = R"(
        struct Foo { var value: i32; };
        fn main() -> i32 {
            var foo: Foo = Foo { value: 10 };
            let p: ptr<Foo> = &foo;
            (*p).value = 22;
            println!("{}", foo.value);
            let pi: ptr<i32> = &foo.value;
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

TEST_F(MLATest, LegacyBuiltinTypeAliasesAreRejected)
{
    for(const char* typeName :
        {"int", "float", "double", "char", "string", "utf8", "utf16"})
    {
        std::string code = std::string(R"(
            fn main() -> i32 {
                let value: )") + typeName + R"( = 0;
                return 0;
            }
        )";
        writeSource(code);
        int rc = 0;
        std::string out = compileCapture(rc);
        EXPECT_NE(rc, 0) << typeName;
        EXPECT_NE(out.find(std::string("unknown struct type: ") + typeName),
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
    EXPECT_NE(out.find("implicit zero-initialization for typed var 'stamp'"),
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
    EXPECT_NE(out.find("implicit zero-initialization for typed var 'value'"),
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
            let decoded_r: Result<Leaf, str8> = Leaf::from_json(text);
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
            let parsed: Result<Packet, str8> =
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
    EXPECT_NE(out.find("ternary branches must return the same type"),
              std::string::npos);
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
            let r: Result<i32, str8> = Ok<i32, str8>(123);
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
            let r: Result<i32, str8> = Ok<i32, str8>(1);
            let v: i32 = r.unwrap();
            return v;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("Result.unwrap() may panic"), std::string::npos);
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
    EXPECT_NE(out.find("cannot infer return type for function 'bad'"),
              std::string::npos);
    EXPECT_NE(out.find("function mixes 'return;' and 'return value;'"),
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
    EXPECT_NE(out.find("cannot infer return type for function 'bad'"),
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
    EXPECT_NE(out.find("index expression does not allow pre/post ++/--"),
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

TEST_F(MLATest, OwnershipCannotMoveWhileBorrowedByPointer)
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
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
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
        fn main() -> i32 {
            let a = atomic_i64_new(1);
            atomic_i64_free(a);
            atomic_i64_free(a);
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
            return 0;
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

TEST_F(MLATest, OwnershipCannotAliasBorrowFromPointerVariable)
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
            return 0;
        }
    )";
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot alias exclusive borrow"), std::string::npos);
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

TEST_F(MLATest, OwnershipOuterPointerBorrowRestoredAfterShadowedPointerScope)
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
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
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

TEST_F(MLATest, OwnershipOuterPointerBorrowSurvivesLoopLocalPointer)
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
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot move"), std::string::npos);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
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
    EXPECT_NE(out.find("cannot return pointer that borrows local value"),
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

TEST_F(MLATest, OwnershipCannotBorrowCallArgWhenOwnerAlreadyBorrowed)
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
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while already borrowed"), std::string::npos);
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

TEST_F(MLATest, OwnershipCannotCallMethodWhileObjectBorrowed)
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
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("while borrowed"), std::string::npos);
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
    EXPECT_NE(out.find("cannot move 'p' while borrowed in call"),
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
    EXPECT_NE(out.find("cannot move 'p' while borrowed in call"),
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
    EXPECT_NE(out.find("cannot move 'p' while borrowed in call"),
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
    EXPECT_NE(out.find("cannot move 'p' while borrowed in call"),
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
    EXPECT_NE(out.find("cannot borrow 'p'"),
              std::string::npos);
}

TEST_F(MLATest, OwnershipCannotBorrowMutInCallWhenSharedBorrowActive)
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
    writeSource(code);
    int rc = 0;
    std::string out = compileCapture(rc);
    EXPECT_NE(rc, 0);
    EXPECT_NE(out.find("cannot borrow 'p' as mutable because it is already borrowed"),
              std::string::npos);
}

TEST_F(MLATest, OwnershipCannotAssignFieldWhileOwnerBorrowed)
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
    EXPECT_NE(out.find("cannot return pointer that borrows local value"),
              std::string::npos);
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
    EXPECT_NE(out.find("implicit value overflows backing type 'u8'"),
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
    EXPECT_NE(out.find("enum types/values are not compatible"),
              std::string::npos);
    EXPECT_NE(out.find("cannot fit in enum 'Small' backing type 'u8'"),
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
    EXPECT_NE(out.find("enum types/values are not compatible"),
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
    EXPECT_EQ(compileAndRun(code),
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
    EXPECT_NE(out.find("implicit value overflows backing type 'u8'"),
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
    EXPECT_NE(out.find("static_assert! requires a compile-time boolean expression"),
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
    EXPECT_NE(out.find("runtime variable is not available in cexpr: 'x'"),
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
    EXPECT_NE(out.find("struct values in cexpr are only supported for generic type dispatch"),
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
    EXPECT_NE(out.find("runtime variable is not available in cexpr: 'runtime_flag'"),
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
    EXPECT_NE(out.find("cexpr call requires a matching 'cexpr fn' overload"),
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
    EXPECT_NE(out.find("runtime variable is not available in cexpr: 'x'"),
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
    EXPECT_NE(out.find("unsupported expression in cexpr"),
              std::string::npos);
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
    EXPECT_NE(out.find("dereferencing raw pointer requires an unsafe block"),
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
