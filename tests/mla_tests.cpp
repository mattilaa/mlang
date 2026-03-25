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
        std::string cmd =
            compilerPath + " -o " + outputExe + " " + sourceFile + " 2>&1";
        int result = system(cmd.c_str());
        if(expectSuccess)
        {
            return result == 0;
        }
        return result != 0;
    }

    std::string compileCapture(int& exitCode)
    {
        std::string cmd =
            compilerPath + " -o " + outputExe + " " + sourceFile + " 2>&1";
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
        std::string cmd = compilerPath + " -o " + outputExe + " " +
                          srcPath.string() + " 2>&1";
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
        return WEXITSTATUS(status);
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
            let s: string = "Hello, World!";
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

TEST_F(MLATest, StringConcatenationPlus)
{
    std::string code = R"(
        fn main() -> i32 {
            let a: string = "Hello, ";
            let b: string = "MLA!";
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
            let a: string = "x=";
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
        fn main() -> i32 {
            let a: string = "left";
            let b: str8 = "right";
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
            fn summarize(&self) -> string;
        }

        struct SocialPost {
            var username: string;
            var content: string;
            var reply: bool;
            var repost: bool;
        };

        impl Summary for SocialPost {
            fn summarize(&self) -> string {
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

TEST_F(MLATest, F32F64CastAliases)
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
            let s: string = "ok";
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
            let r: Result<i32, string> = Ok<i32, string>(123);
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
            let r: Result<i32, string> = Ok<i32, string>(1);
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "hello" };
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
            var content: string;
        };

        fn borrow_ptr(p: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "hello" };
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "hello" };
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "hello" };
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
        fn take_msg(s: string) -> i32 {
            return 5;
        }

        fn main() -> i32 {
            let flag: i32 = 0;
            let msg: string = "hello";
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 1;
        }

        fn main() -> i32 {
            let flag: i32 = 1;
            let msg: Post = Post { content: "hello" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "hello" };
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
            let s: string = String::from("hello");
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
            let s: string = String::from("hello");
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
        fn free(s: string) {
            String::free(s);
        }

        fn main() -> i32 {
            let s: string = String::from("hello");
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
            let b: Post = Post { content: "b" };
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
            let b: Post = Post { content: "b" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
            let b: Post = Post { content: "b" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
            let b: Post = Post { content: "b" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 7;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "x" };
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
            var content: string;
        };

        fn take_post(p: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let a: Post = Post { content: "x" };
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
            var content: string;
        };

        fn main() -> i32 {
            let a: Post = Post { content: "a" };
            let b: Post = Post { content: "b" };
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
            return *p;
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
            return *p;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
}

TEST_F(MLATest, OwnershipCannotBorrowSameOwnerTwiceInSingleCall)
{
    std::string code = R"(
        struct Post {
            var content: string;
        };

        fn inspect(a: ptr<Post>, b: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: "x" };
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
            var content: string;
        };

        fn inspect(a: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: "x" };
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
            return *a + *b;
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
            return *a + *b;
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
            fn summarize(&self) -> string;
        }

        struct SocialPost {
            var username: string;
            var content: string;
            var reply: bool;
            var repost: bool;
        };

        impl Summary for SocialPost {
            fn summarize(&self) -> string {
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
                return self.left + *x;
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
                return self.left + *x;
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
            var content: string;
        };

        fn consume_two(a: ptr<Post>, b: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: "x" };
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
            var content: string;
        };

        fn consume_two(a: Post, b: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: "x" };
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
            var content: string;
        };

        impl Post {
            fn consume_with(&self, other: Post) -> i32 {
                return 0;
            }
        }

        fn main() -> i32 {
            let p: Post = Post { content: "x" };
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
            var content: str8;
        };

        fn consume_two(a: ptr<Post>, b: Post) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: "x" };
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
            var content: str8;
        };

        fn inspect_two(a: ptr<Post>, b: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            let p: Post = Post { content: "x" };
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
            var content: str8;
        };

        fn inspect_one(a: ptr<Post>) -> i32 {
            return 0;
        }

        fn main() -> i32 {
            var p: Post = Post { content: "x" };
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
        extern fn raw_i32_ptr() -> ptr<i32>;

        fn main() -> i32 {
            let p: ptr<i32> = raw_i32_ptr();
            unsafe {
                return *p;
            }
        }
    )";
    writeSource(code);
    EXPECT_TRUE(compile(true));
}

TEST_F(MLATest, BorrowedPointerDereferenceOutsideUnsafeStillAllowed)
{
    std::string code = R"(
        fn main() -> i32 {
            let x: i32 = 7;
            let p: ptr<i32> = &x;
            return *p;
        }
    )";
    EXPECT_EQ(compileAndRunExitCode(code), 7);
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
