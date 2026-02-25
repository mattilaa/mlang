#include "ast.h"
#include "ir.h"
#include "package_manager.h"
#include "module.h"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <optional>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <sys/wait.h>

// Declare functions and globals from parser/lexer
extern int yyparse();
extern FILE* yyin;
extern const char* g_sourceFile;   // set before yyparse() for error messages
extern "C"
{
    extern ASTNode* programRoot;
    extern bool parseHadError;
}

void printUsage(const char* programName)
{
    std::cerr << "Usage: " << programName << " [options] <input_file>\n"
              << "Version: " << MLANG_VERSION << "\n"
              << "\nOptions:\n"
              << "  -o <file>     Output file name (default: a.out)\n"
              << "  -c            Compile to object file only (don't link)\n"
              << "  -S            Emit assembly file\n"
              << "  -emit-llvm    Emit LLVM IR file (.ll)\n"
              << "  -emit-bc      Emit LLVM bitcode file (.bc)\n"
              << "  -O0           No optimization\n"
              << "  -O1           Basic optimization\n"
              << "  -O2           Standard optimization (default)\n"
              << "  -O3           Aggressive optimization\n"
              << "  --no-tests    Skip compiling #[test] functions\n"
              << "  -Wno-colon-if Suppress warning for plain if/else-if with ':'\n"
              << "  -Wno-colon-while Suppress warning for plain while with ':'\n"
              << "  -L <dir>      Add a library search path for linking\n"
              << "  -l <name>     Link with library (e.g. -l m)\n"
              << "  -Wl,<args>    Pass raw linker arguments\n"
              << "  -v            Verbose output\n"
              << "  --debug       Enable debug-only logging\n"
              << "  --version     Show version and exit\n"
              << "  -h, --help    Show this help message\n"
              << "\nPackage manager:\n"
              << "  " << programName << " pkg init\n"
              << "  " << programName << " pkg add <name> [--git URL] [--rev "
                 "REV] [--tag TAG]\n"
              << "  " << programName
              << " pkg add <name> [--pkg-config NAME] [--system]\n"
              << "  " << programName << " pkg fetch\n"
              << "  " << programName
              << " pkg build [-O0|-O1|-O2|-O3]\n"
              << "\nTesting:\n"
              << "  " << programName << " test [path]\n"
              << "  " << programName << " run tests\n"
              << "\nExamples:\n"
              << "  " << programName
              << " test.mla              # Compile to a.out\n"
              << "  " << programName
              << " -o myprogram test.mla # Compile to myprogram\n"
              << "  " << programName
              << " -S test.mla           # Emit assembly\n"
              << "  " << programName
              << " -emit-llvm test.mla   # Emit LLVM IR\n"
              << "\nStdlib linking:\n"
              << "  " << programName << " main.mla -L ~/.local/lib -lmlang_std\n"
              << "  (or set MLANG_STDLIB_LIB_PATH=~/.local/lib)\n"
              << std::endl;
}

static std::string escape_json_string(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for(char c : s)
    {
        if(c == '\\' || c == '"')
        {
            out.push_back('\\');
            out.push_back(c);
        }
        else if(c == '\n')
        {
            out += "\\n";
        }
        else if(c == '\r')
        {
            out += "\\r";
        }
        else if(c == '\t')
        {
            out += "\\t";
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

static void write_mlang_commands_json(
    const std::vector<std::string>& files,
    const std::vector<std::string>& modulePaths,
    const std::vector<std::string>& typeTokens,
    const std::vector<std::tuple<std::string, std::string, int>>& builtinTypes,
    const std::vector<std::tuple<std::string, std::string, int>>& builtinMacros,
    const std::vector<std::tuple<std::string, std::string, int>>& builtinFunctions)
{
    std::ofstream out("mlang_commands.json", std::ios::binary);
    if(!out)
        return;
    out << "{ \"files\": [";
    for(size_t i = 0; i < files.size(); ++i)
    {
        if(i > 0)
            out << ", ";
        out << "\"" << escape_json_string(files[i]) << "\"";
    }
    out << "], \"module_paths\": [";
    for(size_t i = 0; i < modulePaths.size(); ++i)
    {
        if(i > 0)
            out << ", ";
        out << "\"" << escape_json_string(modulePaths[i]) << "\"";
    }
    out << "], \"builtins\": [";
    const char* builtins[] = {
        "println!",
        "print!",
        "eprintln!",
        "eprint!",
        "debug!",
        "format!",
        "assert_eq!"
    };
    for(size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i)
    {
        if(i > 0)
            out << ", ";
        out << "\"" << builtins[i] << "\"";
    }
    out << "], \"tokens\": [";
    if(!typeTokens.empty())
    {
        out << "{ \"type\": \"type\", \"items\": [";
        for(size_t i = 0; i < typeTokens.size(); ++i)
        {
            if(i > 0)
                out << ", ";
            out << "\"" << escape_json_string(typeTokens[i]) << "\"";
        }
        out << "] }";
    }
    out << "], \"builtin_types\": [";
    for(size_t i = 0; i < builtinTypes.size(); ++i)
    {
        const auto& [name, path, line] = builtinTypes[i];
        if(i > 0)
            out << ", ";
        out << "{ \"name\": \"" << escape_json_string(name) << "\", "
            << "\"path\": \"" << escape_json_string(path) << "\", "
            << "\"line\": " << line << " }";
    }
    out << "], \"builtin_macros\": [";
    for(size_t i = 0; i < builtinMacros.size(); ++i)
    {
        const auto& [name, path, line] = builtinMacros[i];
        if(i > 0)
            out << ", ";
        out << "{ \"name\": \"" << escape_json_string(name) << "\", "
            << "\"path\": \"" << escape_json_string(path) << "\", "
            << "\"line\": " << line << " }";
    }
    out << "], \"builtin_functions\": [";
    for(size_t i = 0; i < builtinFunctions.size(); ++i)
    {
        const auto& [name, path, line] = builtinFunctions[i];
        if(i > 0)
            out << ", ";
        out << "{ \"name\": \"" << escape_json_string(name) << "\", "
            << "\"path\": \"" << escape_json_string(path) << "\", "
            << "\"line\": " << line << " }";
    }
    out << "] }";
}

static std::vector<std::string> default_stdlib_paths()
{
    std::vector<std::string> paths;
    if(const char* env = std::getenv("MLANG_STDLIB_PATH"))
        paths.emplace_back(env);
#ifdef MLANG_STDLIB_SOURCE_DIR
    {
        std::error_code ec;
        if(std::filesystem::exists(MLANG_STDLIB_SOURCE_DIR, ec))
            paths.emplace_back(MLANG_STDLIB_SOURCE_DIR);
    }
#endif
    if(const char* xdg = std::getenv("XDG_DATA_HOME"))
        paths.emplace_back(std::string(xdg) + "/mlang/stdlib");
    if(const char* home = std::getenv("HOME"))
        paths.emplace_back(std::string(home) + "/.local/share/mlang/stdlib");
#ifdef MLANG_STDLIB_INSTALL_DIR
    paths.emplace_back(MLANG_STDLIB_INSTALL_DIR);
#endif
    paths.emplace_back("/usr/local/share/mlang/stdlib");
    paths.emplace_back("/usr/share/mlang/stdlib");
    return paths;
}

static void append_stdlib_paths(std::vector<std::string>& modulePaths)
{
    std::unordered_set<std::string> seen;
    for(const auto& p : modulePaths)
        seen.insert(p);
    for(const auto& p : default_stdlib_paths())
    {
        if(!p.empty() && seen.insert(p).second)
            modulePaths.push_back(p);
    }
}

static std::vector<std::string> split_env_paths(const char* env)
{
    std::vector<std::string> out;
    if(!env)
        return out;
    std::string cur;
    for(const char* p = env; *p; ++p)
    {
        if(*p == ':')
        {
            if(!cur.empty())
                out.push_back(cur);
            cur.clear();
        }
        else
        {
            cur.push_back(*p);
        }
    }
    if(!cur.empty())
        out.push_back(cur);
    return out;
}

static std::vector<std::string> default_stdlib_lib_paths()
{
    std::vector<std::string> paths;
    if(const char* env = std::getenv("MLANG_STDLIB_LIB_PATH"))
    {
        for(const auto& p : split_env_paths(env))
            paths.emplace_back(p);
    }
    if(const char* home = std::getenv("HOME"))
    {
        paths.emplace_back(std::string(home) + "/.local/lib");
        paths.emplace_back(std::string(home) + "/.local/lib/mlang");
    }
#ifdef MLANG_STDLIB_LIB_INSTALL_DIR
    paths.emplace_back(MLANG_STDLIB_LIB_INSTALL_DIR);
#endif
    paths.emplace_back("/usr/local/lib");
    paths.emplace_back("/usr/local/lib/mlang");
    paths.emplace_back("/usr/lib");
    paths.emplace_back("/usr/lib/mlang");
    return paths;
}


static bool stdlib_lib_exists(const std::string& dir)
{
    std::error_code ec;
    std::filesystem::path base(dir);
    const char* names[] = {"libmlang_std.a", "libmlang_std.so", "libmlang_std.dylib"};
    for(const char* name : names)
    {
        if(std::filesystem::exists(base / name, ec))
            return true;
    }
    return false;
}

static void append_stdlib_link_args(std::vector<std::string>& linkArgs,
                                    std::string_view exePath)
{
    bool hasStdlib = false;
    for(const auto& arg : linkArgs)
    {
        if(arg == "-lmlang_std" || arg == "-lmlang_stdlib")
        {
            hasStdlib = true;
            break;
        }
    }
    if(hasStdlib)
        return;

    std::string foundDir;
    if(!exePath.empty())
    {
        std::error_code ec;
        std::filesystem::path exe = std::filesystem::absolute(
            std::filesystem::path(std::string(exePath)), ec);
        if(!ec)
        {
            std::string exeDir = exe.parent_path().string();
            if(!exeDir.empty() && stdlib_lib_exists(exeDir))
                foundDir = exeDir;
        }
    }

    for(const auto& dir : default_stdlib_lib_paths())
    {
        if(!foundDir.empty())
            break;
        if(!dir.empty() && stdlib_lib_exists(dir))
        {
            foundDir = dir;
            break;
        }
    }
    if(foundDir.empty())
        return;

    bool hasDir = false;
    for(const auto& arg : linkArgs)
    {
        if(arg == std::string("-L") + foundDir)
        {
            hasDir = true;
            break;
        }
    }
    if(!hasDir)
        linkArgs.push_back(std::string("-L") + foundDir);
    linkArgs.push_back("-lmlang_std");

    bool hasLibm = false;
    for(const auto& arg : linkArgs)
    {
        if(arg == "-lm")
        {
            hasLibm = true;
            break;
        }
    }
    if(!hasLibm)
        linkArgs.push_back("-lm");
}

static std::vector<std::tuple<std::string, std::string, int>>
collect_builtin_type_defs(const std::vector<std::string>& modulePaths)
{
    std::vector<std::tuple<std::string, std::string, int>> out;
    for(const auto& root : modulePaths)
    {
        std::filesystem::path p = std::filesystem::path(root) / "types.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        std::ifstream in(p);
        if(!in)
            continue;
        std::string line;
        int lineNo = 0;
        while(std::getline(in, line))
        {
            ++lineNo;
            const std::string marker = "// @builtin ";
            if(line.rfind(marker, 0) != 0)
                continue;
            std::string name = line.substr(marker.size());
            if(name.empty())
                continue;
            out.emplace_back(name, p.string(), lineNo);
        }
        if(!out.empty())
            break;
    }
    return out;
}

static std::vector<std::tuple<std::string, std::string, int>>
collect_builtin_macro_defs(const std::vector<std::string>& modulePaths)
{
    std::vector<std::tuple<std::string, std::string, int>> out;
    for(const auto& root : modulePaths)
    {
        std::filesystem::path p = std::filesystem::path(root) / "macros.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        std::ifstream in(p);
        if(!in)
            continue;
        std::string line;
        int lineNo = 0;
        while(std::getline(in, line))
        {
            ++lineNo;
            const std::string marker = "// @builtin_macro ";
            if(line.rfind(marker, 0) != 0)
                continue;
            std::string name = line.substr(marker.size());
            if(name.empty())
                continue;
            out.emplace_back(name, p.string(), lineNo);
        }
        if(!out.empty())
            break;
    }
    return out;
}

static std::vector<std::string> collect_user_type_tokens(ProgramNode* program)
{
    std::vector<std::string> out;
    if(!program)
        return out;

    std::unordered_set<std::string> seen;
    if(program->structList)
    {
        for(auto* s : program->structList->structs)
        {
            if(!s)
                continue;
            if(seen.insert(s->name).second)
                out.push_back(s->name);
        }
    }
    if(program->enumList)
    {
        for(auto* e : program->enumList->enums)
        {
            if(!e)
                continue;
            if(seen.insert(e->name).second)
                out.push_back(e->name);
        }
    }
    return out;
}

static std::vector<std::tuple<std::string, std::string, int>>
collect_builtin_function_defs(const std::vector<std::string>& modulePaths)
{
    std::vector<std::tuple<std::string, std::string, int>> out;
    for(const auto& root : modulePaths)
    {
        std::filesystem::path p = std::filesystem::path(root) / "test.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        std::ifstream in(p);
        if(!in)
            continue;
        std::string line;
        int lineNo = 0;
        while(std::getline(in, line))
        {
            ++lineNo;
            const std::string marker = "// @builtin_fn ";
            if(line.rfind(marker, 0) != 0)
                continue;
            std::string name = line.substr(marker.size());
            if(name.empty())
                continue;
            out.emplace_back(name, p.string(), lineNo);
        }
        if(!out.empty())
            break;
    }
    return out;
}

static std::string trim(std::string_view s)
{
    size_t start = 0;
    while(start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while(end > start &&
          std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return std::string(s.substr(start, end - start));
}

static std::string unquote(std::string_view v)
{
    std::string t = trim(v);
    if(t.size() >= 2 && t.front() == '"' && t.back() == '"')
        return t.substr(1, t.size() - 2);
    return t;
}

static std::vector<std::string> split_toml_array(std::string_view input)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;
    for(char c : input)
    {
        if(c == '"')
        {
            in_quotes = !in_quotes;
            cur.push_back(c);
            continue;
        }
        if(c == ',' && !in_quotes)
        {
            out.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if(!cur.empty())
        out.push_back(trim(cur));
    return out;
}

static std::vector<std::string> parse_module_paths_from_toml(
    const std::filesystem::path& manifestPath)
{
    std::ifstream in(manifestPath, std::ios::binary);
    if(!in)
        return {};

    std::vector<std::string> out;
    std::string line;
    std::string section;
    while(std::getline(in, line))
    {
        std::string t = trim(line);
        if(t.empty() || t[0] == '#')
            continue;
        if(t.front() == '[' && t.back() == ']')
        {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if(section != "package" && section != "tool.mlang")
            continue;

        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        std::string key = trim(t.substr(0, eq));
        if(key != "module_paths")
            continue;
        std::string value = trim(t.substr(eq + 1));
        if(value.empty())
            continue;
        if(value.front() == '[' && value.back() == ']')
        {
            std::string inner = value.substr(1, value.size() - 2);
            for(const auto& part : split_toml_array(inner))
            {
                std::string v = unquote(part);
                if(!v.empty())
                    out.push_back(v);
            }
        }
        else
        {
            std::string v = unquote(value);
            if(!v.empty())
                out.push_back(v);
        }
    }
    return out;
}

static std::optional<std::filesystem::path> find_manifest_path(
    std::filesystem::path startDir)
{
    std::error_code ec;
    startDir = std::filesystem::absolute(startDir, ec);
    if(ec)
        return std::nullopt;

    std::filesystem::path cur = startDir;
    while(!cur.empty())
    {
        auto candidate = cur / "mlang.toml";
        if(std::filesystem::exists(candidate))
            return candidate;
        auto parent = cur.parent_path();
        if(parent == cur)
            break;
        cur = parent;
    }
    return std::nullopt;
}

int main(int argc, char** argv)
{
#ifndef MLANG_VERSION
#define MLANG_VERSION "0.1.0"
#endif

    if(argc >= 2 && std::string(argv[1]) == "--version")
    {
        std::cout << "mlang " << MLANG_VERSION << "\n";
        return 0;
    }
    if(argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }
    if(std::string(argv[1]) == "pkg")
    {
        PackageManager pkg;
        return pkg.run(argc, argv);
    }

    bool testMode = false;
    bool runTests = false;
    bool includeTests = true;
    int argStart = 1;

    if(std::string(argv[1]) == "test")
    {
        testMode = true;
        runTests = true;
        argStart = 2;
    }
    else if(argc >= 3 && std::string(argv[1]) == "run" &&
            std::string(argv[2]) == "tests")
    {
        testMode = true;
        runTests = true;
        argStart = 3;
    }

    // Parse command line arguments
    std::string inputFile;
    std::string outputFile = "a.out";
    bool emitObjectOnly = false;
    bool emitAssembly = false;
    bool emitLLVMIR = false;
    bool emitBitcode = false;
    int optimizationLevel = 2;
    bool verbose = false;
    bool debugMode = false;
    bool warnPlainColonIf = true;
    bool warnPlainColonWhile = true;
    std::vector<std::string> linkArgs;

    for(int i = argStart; i < argc; ++i)
    {
        std::string arg = argv[i];

        if(arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if(arg == "--version")
        {
            std::cout << "mlang " << MLANG_VERSION << "\n";
            return 0;
        }
        else if(arg == "-o" && i + 1 < argc)
        {
            outputFile = argv[++i];
        }
        else if(arg == "-c")
        {
            emitObjectOnly = true;
        }
        else if(arg == "-S")
        {
            emitAssembly = true;
        }
        else if(arg == "-emit-llvm")
        {
            emitLLVMIR = true;
        }
        else if(arg == "-emit-bc")
        {
            emitBitcode = true;
        }
        else if(arg == "-L" && i + 1 < argc)
        {
            linkArgs.push_back(std::string("-L") + argv[++i]);
        }
        else if(arg.rfind("-L", 0) == 0 && arg.size() > 2)
        {
            linkArgs.push_back(arg);
        }
        else if(arg == "-l" && i + 1 < argc)
        {
            linkArgs.push_back(std::string("-l") + argv[++i]);
        }
        else if(arg.rfind("-l", 0) == 0 && arg.size() > 2)
        {
            linkArgs.push_back(arg);
        }
        else if(arg.rfind("-Wl,", 0) == 0 && arg.size() > 4)
        {
            linkArgs.push_back(arg);
        }
        else if(arg == "-O0")
        {
            optimizationLevel = 0;
        }
        else if(arg == "-O1")
        {
            optimizationLevel = 1;
        }
        else if(arg == "-O2")
        {
            optimizationLevel = 2;
        }
        else if(arg == "-O3")
        {
            optimizationLevel = 3;
        }
        else if(arg == "-v")
        {
            verbose = true;
        }
        else if(arg == "--debug")
        {
            debugMode = true;
        }
        else if(arg == "--no-tests")
        {
            includeTests = false;
        }
        else if(arg == "-Wno-colon-if")
        {
            warnPlainColonIf = false;
        }
        else if(arg == "-Wno-colon-while")
        {
            warnPlainColonWhile = false;
        }
        else if(arg == "--no-run" && testMode)
        {
            runTests = false;
        }
        else if(arg[0] != '-')
        {
            inputFile = arg;
        }
        else
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if(inputFile.empty())
    {
        if(testMode)
        {
            inputFile = "tests";
        }
        else
        {
            std::cerr << "Error: No input file specified" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    std::string generatedTestRoot;
    if(testMode)
    {
        std::error_code tec;
        std::filesystem::path inPath = inputFile;
        if(std::filesystem::is_directory(inPath, tec))
        {
            std::vector<std::filesystem::path> files;
            for(std::filesystem::directory_iterator it(inPath, tec), end;
                it != end; ++it)
            {
                if(!it->is_regular_file(tec))
                    continue;
                auto p = it->path();
                if(p.extension() != ".mla")
                    continue;
                if(p.filename() == "__mlang_test_root.mla")
                    continue;
                std::string filename = p.filename().string();
                if(filename.rfind("test_", 0) != 0)
                    continue;
                files.push_back(p);
            }
            if(files.empty())
            {
                std::cerr << "Error: No .mla test files found in " << inputFile
                          << std::endl;
                return 1;
            }

            std::filesystem::path rootFile =
                inPath / "__mlang_test_root.mla";
            std::ofstream out(rootFile);
            if(!out)
            {
                std::cerr << "Error: Failed to create test root file: "
                          << rootFile << std::endl;
                return 1;
            }
            for(const auto& f : files)
            {
                std::string modName = f.stem().string();
                out << "mod " << modName << ";\n";
                out << "use " << modName << "::*;\n";
            }
            out.close();

            generatedTestRoot = rootFile.string();
            inputFile = generatedTestRoot;
            if(outputFile == "a.out")
            {
                outputFile = (inPath / "__mlang_test_bin").string();
            }
        }
        else if(outputFile == "a.out")
        {
            outputFile = "mlang_test_bin";
        }
    }

    FILE* input_file = fopen(inputFile.c_str(), "r");
    if(!input_file)
    {
        std::cerr << "Error opening file: " << inputFile << std::endl;
        return 1;
    }

    yyin = input_file;

    // Create LLVM context and module
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder(context);
    std::unique_ptr<llvm::Module> module =
        std::make_unique<llvm::Module>("MLang", context);

    std::vector<std::string> modulePaths;
    std::vector<std::string> moduleSearchPaths;

    try
    {
        // Parse the input
        if(verbose)
        {
            std::cout << "Parsing " << inputFile << "..." << std::endl;
        }

        parseHadError = false;
        g_sourceFile = inputFile.c_str();
        if(yyparse() != 0 || parseHadError)
        {
            std::cerr << "Parsing failed." << std::endl;
            fclose(input_file);
            return 1;
        }

        if(verbose)
        {
            std::cout << "Parsing completed successfully." << std::endl;
        }

        if(!programRoot)
        {
            std::cerr << "Error: No program root node created" << std::endl;
            fclose(input_file);
            return 1;
        }

        // Process modules
        if(auto* program = dynamic_cast<ProgramNode*>(programRoot))
        {
            // Get the directory of the input file for module resolution
            std::filesystem::path inputPath(inputFile);
            std::string basePath = inputPath.parent_path().string();
            if(basePath.empty())
            {
                basePath = ".";
            }

            // Load module search paths from mlang.toml (if present)
            std::filesystem::path inputDir = inputPath.parent_path();
            if(inputDir.empty())
                inputDir = ".";
            auto manifestPath = find_manifest_path(inputDir);
            if(manifestPath.has_value())
            {
                moduleSearchPaths =
                    parse_module_paths_from_toml(manifestPath.value());
                std::filesystem::path manifestDir =
                    manifestPath.value().parent_path();
                std::error_code pathEc;
                for(auto& p : moduleSearchPaths)
                {
                    std::filesystem::path mp = std::filesystem::path(p);
                    if(!mp.is_absolute())
                        mp = manifestDir / mp;
                    std::filesystem::path abs =
                        std::filesystem::absolute(mp, pathEc);
                    if(!pathEc)
                        p = abs.lexically_normal().string();
                }
            }

            append_stdlib_paths(moduleSearchPaths);
            append_stdlib_link_args(linkArgs, argv[0]);

            // Initialize module loader
            ModuleLoader moduleLoader(basePath, moduleSearchPaths);

            // Process mod declarations (load modules)
            if(!program->modules.empty())
            {
                if(verbose)
                {
                    std::cout << "Loading modules..." << std::endl;
                }

                std::string errorMsg;
                if(!moduleLoader.processModDeclarations(program, errorMsg))
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                    fclose(input_file);
                    delete programRoot;
                    return 1;
                }

                // Process use declarations (import symbols)
                if(!moduleLoader.processUseDeclarations(program, errorMsg))
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                    fclose(input_file);
                    delete programRoot;
                    return 1;
                }

                if(verbose)
                {
                    std::cout << "Modules loaded: ";
                    for(const auto& mod : moduleLoader.getLoadedModules())
                    {
                        std::cout << mod << " ";
                    }
                    std::cout << std::endl;
                }
            }

            modulePaths = moduleLoader.getLoadedModulePaths();
        }

        // Initialize code generator
        CodeGenerator generator(context, builder, module, debugMode);
        generator.setSourceFile(inputFile);
        generator.setTestMode(testMode);
        generator.setWarnPlainColonIf(warnPlainColonIf);
        generator.setWarnPlainColonWhile(warnPlainColonWhile);
        if(!testMode)
            generator.setIncludeTests(includeTests);

        // Generate LLVM IR
        if(auto* program = dynamic_cast<ProgramNode*>(programRoot))
        {
            if(verbose)
            {
                std::cout << "Generating LLVM IR..." << std::endl;
            }

            generator.generateCode(program);

            // Check for semantic errors
            if(generator.hadError())
            {
                std::cerr << "Compilation failed due to errors." << std::endl;
                fclose(input_file);
                delete programRoot;
                return 1;
            }

            // Print the AST if verbose
            if(verbose)
            {
                std::cout << "\n=== AST ===" << std::endl;
                std::cout << program->toString() << std::endl;
            }

            // Initialize backend
            Backend backend(module);

            if(verbose)
            {
                std::cout << "Target: " << backend.getTargetTriple()
                          << std::endl;
            }

            // Apply optimizations
            if(optimizationLevel > 0)
            {
                if(verbose)
                {
                    std::cout << "Applying optimizations (O"
                              << optimizationLevel << ")..." << std::endl;
                }
                backend.optimize(optimizationLevel);
            }

            // Determine output based on flags
            bool success = false;

            if(emitLLVMIR)
            {
                // Emit LLVM IR
                std::string llFile = outputFile;
                if(llFile == "a.out")
                {
                    llFile = inputFile.substr(0, inputFile.find_last_of('.')) +
                             ".ll";
                }
                success = backend.emitLLVMIR(llFile);

                // Also print to stdout if verbose
                if(verbose)
                {
                    std::cout << "\n=== LLVM IR ===" << std::endl;
                    module->print(llvm::outs(), nullptr);
                }
            }
            else if(emitBitcode)
            {
                // Emit LLVM bitcode
                std::string bcFile = outputFile;
                if(bcFile == "a.out")
                {
                    bcFile = inputFile.substr(0, inputFile.find_last_of('.')) +
                             ".bc";
                }
                success = backend.emitBitcode(bcFile);
            }
            else if(emitAssembly)
            {
                // Emit assembly
                std::string asmFile = outputFile;
                if(asmFile == "a.out")
                {
                    asmFile =
                        inputFile.substr(0, inputFile.find_last_of('.')) + ".s";
                }
                success = backend.emitAssemblyFile(asmFile);
            }
            else if(emitObjectOnly)
            {
                // Emit object file only
                std::string objFile = outputFile;
                if(objFile == "a.out")
                {
                    objFile =
                        inputFile.substr(0, inputFile.find_last_of('.')) + ".o";
                }
                success = backend.emitObjectFile(objFile);
            }
            else
            {
                // Compile to executable
                success = backend.compileToExecutable(outputFile, linkArgs);
            }

            if(!success)
            {
                std::cerr << "Compilation failed." << std::endl;
                fclose(input_file);
                delete programRoot;
                return 1;
            }
        }
        else
        {
            std::cerr << "Error: Invalid program root node type" << std::endl;
            fclose(input_file);
            return 1;
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error during compilation: " << e.what() << std::endl;
        fclose(input_file);
        return 1;
    }

    // Emit mlang_commands.json for LSP indexing.
    std::unordered_set<std::string> unique;
    std::vector<std::string> files;
    files.reserve(modulePaths.size() + 1);
    std::error_code ec;
    std::filesystem::path inputAbs =
        std::filesystem::absolute(std::filesystem::path(inputFile), ec);
    std::string inputNorm = ec ? inputFile : inputAbs.lexically_normal().string();
    unique.insert(inputNorm);
    files.push_back(inputNorm);
    for(const auto& path : modulePaths)
    {
        std::filesystem::path p = std::filesystem::path(path);
        std::filesystem::path abs = p.is_absolute()
                                        ? p
                                        : std::filesystem::absolute(p, ec);
        std::string norm = ec ? path : abs.lexically_normal().string();
        if(unique.insert(norm).second)
            files.push_back(norm);
    }
    std::vector<std::string> searchPaths;
    std::unordered_set<std::string> searchUnique;
    for(const auto& path : moduleSearchPaths)
    {
        std::filesystem::path p = std::filesystem::path(path);
        std::filesystem::path abs = p.is_absolute()
                                        ? p
                                        : std::filesystem::absolute(p, ec);
        std::string norm = ec ? path : abs.lexically_normal().string();
        if(searchUnique.insert(norm).second)
            searchPaths.push_back(norm);
    }

    auto builtinTypes = collect_builtin_type_defs(searchPaths);
    auto builtinMacros = collect_builtin_macro_defs(searchPaths);
    auto builtinFunctions = collect_builtin_function_defs(searchPaths);
    auto* programNode = dynamic_cast<ProgramNode*>(programRoot);
    auto typeTokens = collect_user_type_tokens(programNode);
    write_mlang_commands_json(files, searchPaths, typeTokens, builtinTypes,
                              builtinMacros, builtinFunctions);

    // Clean up
    fclose(input_file);
    delete programRoot;

    if(verbose)
    {
        std::cout << "Compilation completed successfully." << std::endl;
    }

    if(testMode && runTests && !emitObjectOnly && !emitAssembly &&
       !emitLLVMIR && !emitBitcode)
    {
        int rc = std::system(outputFile.c_str());
        int exitCode = 1;
        if(rc != -1 && WIFEXITED(rc))
            exitCode = WEXITSTATUS(rc);
        if(!generatedTestRoot.empty())
        {
            std::error_code rmec;
            std::filesystem::remove(generatedTestRoot, rmec);
        }
        return exitCode;
    }

    if(!generatedTestRoot.empty())
    {
        std::error_code rmec;
        std::filesystem::remove(generatedTestRoot, rmec);
    }

    return 0;
}
