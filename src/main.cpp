#include "ast.h"
#include "diagnostics.h"
#include "ir.h"
#include "module.h"
#include "package_manager.h"
#include "source_filter.h"
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cstdlib>
#include <exception>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <optional>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <functional>
#ifdef _WIN32
#include <process.h>
#define popen  _popen
#define pclose _pclose
#ifndef WIFEXITED
#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#endif
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <llvm/TargetParser/Triple.h>

// Declare functions and globals from parser/lexer
extern int yyparse();
extern FILE* yyin;
extern const char* g_sourceFile;   // set before yyparse() for error messages
extern const char* g_targetArchForParse;
extern "C"
{
    extern ASTNode* programRoot;
    extern bool parseHadError;
}

typedef size_t yy_size_t;
struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_bytes(const char* bytes, int len);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

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
              << "  --target-arch <arch>  Set target arch: x86, x64, aarch64\n"
              << "  -O0           No optimization\n"
              << "  -Og           Debug-friendly optimization\n"
              << "  -O1           Basic optimization\n"
              << "  -O2           Standard optimization (default)\n"
              << "  -O3           Aggressive optimization\n"
              << "  -Os           Optimize for size\n"
              << "  -Oz           Optimize for minimum size\n"
              << "  --no-tests    Skip compiling #[test] functions\n"
              << "  --tests       Compile and run #[test] for input path/file\n"
              << "  --filter <name> Run only tests matching <name> (substring match)\n"
              << "  -Wno-colon-if Suppress warning for plain if/else-if with ':'\n"
              << "  -Wno-colon-while Suppress warning for plain while with ':'\n"
              << "  -Wno-unwrap Suppress warning for Result.unwrap() usage\n"
              << "  -L <dir>      Add a library search path for linking\n"
              << "  -l <name>     Link with library (e.g. -l m)\n"
              << "  -Wl,<args>    Pass raw linker arguments\n"
              << "  -v            Verbose output\n"
              << "  --debug       Enable debug-only logging\n"
              << "  --version     Show version and exit\n"
              << "  -h, --help    Show this help message\n"
              << "\nPackage manager (flag order is flexible; --config may\n"
              << "appear anywhere; --color alone implies --tasks output):\n"
              << "  " << programName << " pkg [--config FILE] init\n"
              << "  " << programName << " pkg [--config FILE] add <name> [--git URL] [--rev "
                 "REV] [--tag TAG] [--submodules]\n"
              << "  " << programName
              << " pkg [--config FILE] add <name> --url URL [--archive tar.gz] [--strip-components N] [--subdir DIR]\n"
              << "  " << programName
              << " pkg [--config FILE] add <name> [--pkg-config NAME] [--system]\n"
              << "  " << programName
              << " pkg [--config FILE] add <name> [--git URL|--url URL] --add-lib [--project-dir DIR]\n"
              << "  " << programName
              << " pkg [--config FILE] fetch [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR] [--stdout-log FILE] [--stderr-log FILE] [--warn-log FILE] [--task-print-to-stdout-log]\n"
              << "  " << programName
              << " pkg [--config FILE] build [-O0|-Og|-O1|-O2|-O3|-Os|-Oz] [--ninja] [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR] [--stdout-log FILE] [--stderr-log FILE] [--warn-log FILE] [--task-print-to-stdout-log]\n"
              << "  " << programName
              << " pkg --tests [--tasks] [--color] <manifest.toml>...\n"
              << "  " << programName
              << " pkg [--tasks] [--color] <manifest.toml>...\n"
              << "  " << programName
              << " pkg [--config FILE] run <task> [--tasks] [--color] [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR] [--stdout-log FILE] [--stderr-log FILE] [--warn-log FILE] [--task-print-to-stdout-log] [--option KEY=VALUE]\n"
              << "  " << programName
              << " pkg [--config FILE] clean [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR] [--stdout-log FILE] [--stderr-log FILE] [--warn-log FILE] [--task-print-to-stdout-log] [--deps]\n"
              << "  Separate steps: " << programName << " pkg fetch ; "
              << programName << " pkg build ; " << programName
              << " pkg run <task>\n"
              << "  One command: " << programName
              << " pkg run <task>    # fetches if needed, then runs the task chain\n"
              << "  Show task tree: " << programName
              << " pkg run <task> --tasks [--color]\n"
              << "  Test manifest: " << programName
              << " pkg --tests --tasks --color tests/mla_tests.toml\n"
              << "  Manifest overview: " << programName
              << " pkg --color tests/mla_tests.toml   # --tasks implied\n"
              << "\nTesting:\n"
              << "  " << programName << " --tests [path]\n"
              << "  " << programName << " test [path]\n"
              << "  " << programName << " test [path] --filter <name>\n"
              << "  " << programName << " run tests\n"
              << "\nBenchmarking:\n"
              << "  " << programName << " bench [path]\n"
              << "  " << programName
              << " bench [path] --bench-iters N --bench-warmup N\n"
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

static std::string normalize_target_arch_name(const std::string& arch)
{
    if(arch == "x86" || arch == "i386" || arch == "i686")
        return "x86";
    if(arch == "x64" || arch == "x86_64" || arch == "amd64")
        return "x64";
    if(arch == "aarch64" || arch == "arm64")
        return "aarch64";
    return "";
}

static std::string target_triple_for_arch_override(const std::string& arch)
{
    const std::string normalized = normalize_target_arch_name(arch);
    if(normalized.empty())
        return "";

    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    if(normalized == "x86")
        triple.setArchName("i386");
    else if(normalized == "x64")
        triple.setArchName("x86_64");
    else if(normalized == "aarch64")
        triple.setArchName("aarch64");
    return triple.str();
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
        "debug_json!",
        "format!",
        "assert!",
        "windows!",
        "posix!",
        "linux!",
        "macos!",
        "x64!",
        "aarch64!",
        "static_assert!",
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

static bool is_excluded_lsp_scan_dir(std::string_view name)
{
    return name == ".git" || name == "build" || name == "artifacts" ||
           name == ".cache" || name == ".venv";
}

static void append_workspace_mla_files(
    const std::filesystem::path& root,
    std::unordered_set<std::string>& unique,
    std::vector<std::string>& files)
{
    if(root.empty())
        return;

    std::error_code ec;
    std::filesystem::path abs_root = std::filesystem::absolute(root, ec);
    if(ec)
        abs_root = root;
    if(!std::filesystem::exists(abs_root, ec) || !std::filesystem::is_directory(abs_root, ec))
        return;

    std::filesystem::recursive_directory_iterator it(
        abs_root, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    if(ec)
        return;
    while(it != end)
    {
        const std::filesystem::directory_entry& entry = *it;
        const std::filesystem::path p = entry.path();
        std::error_code entry_ec;

        if(entry.is_directory(entry_ec))
        {
            const std::string base = p.filename().string();
            if(is_excluded_lsp_scan_dir(base))
                it.disable_recursion_pending();
            ++it;
            continue;
        }

        if(entry_ec)
        {
            ++it;
            continue;
        }

        if(!entry.is_regular_file(entry_ec))
        {
            ++it;
            continue;
        }
        if(entry_ec)
        {
            ++it;
            continue;
        }

        if(p.extension() == ".mla")
        {
            std::error_code abs_ec;
            std::filesystem::path abs = std::filesystem::absolute(p, abs_ec);
            const std::string norm =
                (abs_ec ? p : abs).lexically_normal().string();
            if(unique.insert(norm).second)
                files.push_back(norm);
        }
        std::error_code inc_ec;
        it.increment(inc_ec);
        if(inc_ec)
            break;
    }
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

static void append_unique_link_arg(std::vector<std::string>& linkArgs,
                                   const std::string& arg)
{
    if(arg.empty())
        return;
    for(const auto& existing : linkArgs)
    {
        if(existing == arg)
            return;
    }
    linkArgs.push_back(arg);
}

static void append_framework_link_args(std::vector<std::string>& linkArgs,
                                       const std::string& framework)
{
    if(framework.empty())
        return;
    for(std::size_t i = 0; i + 1 < linkArgs.size(); ++i)
    {
        if(linkArgs[i] == "-framework" && linkArgs[i + 1] == framework)
            return;
    }
    linkArgs.push_back("-framework");
    linkArgs.push_back(framework);
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

    std::string foundDir;
    if(!hasStdlib && !exePath.empty())
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
        if(hasStdlib)
            break;
        if(!foundDir.empty())
            break;
        if(!dir.empty() && stdlib_lib_exists(dir))
        {
            foundDir = dir;
            break;
        }
    }
    if(!hasStdlib && foundDir.empty())
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
    if(!hasStdlib && !hasDir)
        linkArgs.push_back(std::string("-L") + foundDir);
    if(!hasStdlib)
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

#ifdef MLANG_OPENSSL_SSL_DIR
    append_unique_link_arg(linkArgs,
                           std::string("-L") + MLANG_OPENSSL_SSL_DIR);
#endif
#ifdef MLANG_OPENSSL_CRYPTO_DIR
    append_unique_link_arg(linkArgs,
                           std::string("-L") + MLANG_OPENSSL_CRYPTO_DIR);
#endif
#ifdef MLANG_OPENSSL_SSL_LIBRARY
    append_unique_link_arg(linkArgs, MLANG_OPENSSL_SSL_LIBRARY);
#else
    append_unique_link_arg(linkArgs, "-lssl");
#endif
#ifdef MLANG_OPENSSL_CRYPTO_LIBRARY
    append_unique_link_arg(linkArgs, MLANG_OPENSSL_CRYPTO_LIBRARY);
#else
    append_unique_link_arg(linkArgs, "-lcrypto");
#endif

#ifdef __APPLE__
    append_framework_link_args(linkArgs, "CoreFoundation");
    append_framework_link_args(linkArgs, "CoreGraphics");
    append_framework_link_args(linkArgs, "ImageIO");
#endif
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

static std::string shell_quote(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('\'');
    for(char c : s)
    {
        if(c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static bool is_linker_input_path(std::string_view arg)
{
    auto has_suffix = [arg](std::string_view suffix) {
        return arg.size() >= suffix.size() &&
               arg.substr(arg.size() - suffix.size()) == suffix;
    };
    return arg.size() > 2 &&
           (has_suffix(".a") || has_suffix(".so") || has_suffix(".dylib") ||
            has_suffix(".o"));
}

static std::optional<std::filesystem::path>
find_mlang_pkg_frontend_source(const char* argv0)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    candidates.push_back(fs::current_path() / "tools" / "mlang-pkg-mla" /
                         "main.mla");

    fs::path exePath(argv0 ? argv0 : "mlang");
    fs::path exeDir = exePath.has_parent_path() ? exePath.parent_path()
                                                : fs::current_path();
    candidates.push_back(exeDir / ".." / "tools" / "mlang-pkg-mla" /
                         "main.mla");
    candidates.push_back(exeDir / "tools" / "mlang-pkg-mla" / "main.mla");

    for(const auto& c : candidates)
    {
        std::error_code ec;
        if(fs::exists(c, ec))
            return fs::weakly_canonical(c, ec);
    }
    return std::nullopt;
}

static std::optional<std::filesystem::path>
find_mlang_frontend_source(const char* argv0)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    candidates.push_back(fs::current_path() / "tools" / "mlang-frontend-mla" /
                         "main.mla");

    fs::path exePath(argv0 ? argv0 : "mlang");
    fs::path exeDir = exePath.has_parent_path() ? exePath.parent_path()
                                                : fs::current_path();
    candidates.push_back(exeDir / ".." / "tools" / "mlang-frontend-mla" /
                         "main.mla");
    candidates.push_back(exeDir / "tools" / "mlang-frontend-mla" / "main.mla");

    for(const auto& c : candidates)
    {
        std::error_code ec;
        if(fs::exists(c, ec))
            return fs::weakly_canonical(c, ec);
    }
    return std::nullopt;
}

static std::filesystem::path resolve_backend_compiler_path(const char* argv0)
{
    namespace fs = std::filesystem;
    fs::path exePath(argv0 ? argv0 : "mlang");
    std::error_code ec;
    if(fs::exists(exePath, ec))
        exePath = fs::weakly_canonical(exePath, ec);
    fs::path exeDir = exePath.has_parent_path() ? exePath.parent_path()
                                                : fs::current_path();
    const std::string exeName = exePath.filename().string();
    if(exeName == "mlang")
        return exePath;

    std::vector<fs::path> candidates;
    candidates.push_back(exeDir / "mlang");
    candidates.push_back(exeDir / ".." / "mlang");
    candidates.push_back(fs::current_path() / "build" / "mlang");
    candidates.push_back(fs::current_path() / "mlang");

    for(const auto& c : candidates)
    {
        std::error_code candidateEc;
        if(fs::exists(c, candidateEc))
            return fs::weakly_canonical(c, candidateEc);
    }
    return exePath;
}

static bool ensure_compiled_mla_tool(const char* argv0,
                                     const std::filesystem::path& src,
                                     const std::filesystem::path& compilerBin,
                                     std::string_view toolTag,
                                     std::filesystem::path& outBin,
                                     bool forceCppFrontendEnv)
{
    namespace fs = std::filesystem;
    std::hash<std::string> hasher;
    const auto h = static_cast<unsigned long long>(hasher(src.string()));
    outBin = fs::temp_directory_path() /
             (std::string(toolTag) + "-" + std::to_string(h));

    std::error_code ec;
    const bool binExists = fs::exists(outBin, ec);
    if(binExists)
    {
        auto srcTime = fs::last_write_time(src, ec);
        if(!ec)
        {
            auto binTime = fs::last_write_time(outBin, ec);
            if(!ec && binTime >= srcTime)
                return true;
        }
    }

    fs::path exePath = compilerBin.empty()
                           ? fs::path(argv0 ? argv0 : "mlang")
                           : compilerBin;
    fs::path exeDir = exePath.has_parent_path() ? exePath.parent_path()
                                                : fs::current_path();
    fs::path stdlibLibDir = exeDir;

    std::string compileCmd;
    if(forceCppFrontendEnv)
        compileCmd += "MLANG_FRONTEND_IMPL=cpp ";
    compileCmd += shell_quote(exePath.string()) + " " + shell_quote(src.string()) +
                  " -Wno-colon-if -Wno-colon-while -L " +
                  shell_quote(stdlibLibDir.string()) + " -lmlang_std -o " +
                  shell_quote(outBin.string());
    // The mlang compiler auto-adds CoreFoundation/CoreGraphics/ImageIO on
    // macOS when assembling default link args, so they do not need to be
    // forwarded here. Forwarding them as `-framework` CLI args would be
    // rejected as unknown options.

    int compileRc = std::system(compileCmd.c_str());
    std::error_code ecCheck;
    bool haveCompiledTool = fs::exists(outBin, ecCheck);
    if(compileRc != 0 && !haveCompiledTool)
        return false;
    return true;
}

static std::filesystem::path default_build_artifact_path(
    const std::string& inputFile, std::string_view ext)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path buildDir = fs::current_path(ec) / "build";
    if(ec)
        buildDir = fs::path("build");
    fs::create_directories(buildDir, ec);

    fs::path inputPath(inputFile);
    std::string stem = inputPath.stem().string();
    if(stem.empty())
        stem = "a.out";
    return buildDir / (stem + std::string(ext));
}

static std::optional<int> run_mlang_frontend(int argc, char** argv)
{
    namespace fs = std::filesystem;
    auto srcOpt = find_mlang_frontend_source(argv[0]);
    if(!srcOpt.has_value())
        return std::nullopt;

    fs::path src = *srcOpt;
    fs::path toolBin;
    fs::path backendBin = resolve_backend_compiler_path(argv[0]);
    if(!ensure_compiled_mla_tool(argv[0], src, backendBin, "mlang-frontend-mla", toolBin,
                                 /*forceCppFrontendEnv=*/true))
        return std::nullopt;

    std::string runCmd = "MLANG_FRONTEND_IMPL=cpp " + shell_quote(toolBin.string()) +
                         " --backend " + shell_quote(backendBin.string());
    for(int i = 1; i < argc; ++i)
        runCmd += " " + shell_quote(argv[i]);

    int runRc = std::system(runCmd.c_str());

    if(runRc < 0)
        return std::nullopt;
    if(WIFEXITED(runRc))
    {
        int code = WEXITSTATUS(runRc);
        if(code == 0)
            return 0;
        return std::nullopt;
    }
    return std::nullopt;
}

static bool manifest_requires_cpp_pkg_frontend()
{
    auto manifestOpt = find_manifest_path(std::filesystem::current_path());
    if(!manifestOpt.has_value())
        return false;

    std::ifstream in(*manifestOpt, std::ios::binary);
    if(!in)
        return false;
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if(content.empty())
        return false;

    if(content.find("[workspace]") != std::string::npos)
        return true;
    if(content.find("[[bin]]") != std::string::npos)
        return true;
    if(content.find("[[task]]") != std::string::npos)
        return true;
    if(content.find("opt_level") != std::string::npos)
        return true;
    if(content.find("target_arch") != std::string::npos)
        return true;
    if(content.find("path_entries") != std::string::npos)
        return true;
    if(content.find("bin_paths") != std::string::npos)
        return true;
    if(content.find("make_program") != std::string::npos)
        return true;
    if(content.find("compiler_flags") != std::string::npos)
        return true;
    if(content.find("linker_flags") != std::string::npos)
        return true;
    if(content.find("lib_paths") != std::string::npos)
        return true;
    if(content.find("libs") != std::string::npos)
        return true;
    if(content.find("use_ninja") != std::string::npos)
        return true;
    if(content.find("ninja") != std::string::npos)
        return true;
    if(content.find("min_mlang_version") != std::string::npos)
        return true;
    if(content.find("static_deps") != std::string::npos)
        return true;
    if(content.find("static_cpp_runtime") != std::string::npos)
        return true;
    if(content.find("build_dir") != std::string::npos)
        return true;
    if(content.find("deps_dir") != std::string::npos)
        return true;
    if(content.find("build = \"none\"") != std::string::npos)
        return true;
    return false;
}

static std::optional<int> run_mlang_pkg_frontend(int argc, char** argv)
{
    namespace fs = std::filesystem;
    if(argc < 2 || std::string(argv[1]) != "pkg")
        return std::nullopt;
    int subIndex = 2;
    while(subIndex < argc)
    {
        std::string arg = argv[subIndex];
        if(arg == "--config" || arg.rfind("--config=", 0) == 0)
            return std::nullopt;
        break;
    }
    if(argc >= subIndex + 1 &&
       (std::string(argv[subIndex]) == "--help" ||
        std::string(argv[subIndex]) == "-h" ||
        std::string(argv[subIndex]) == "help"))
    {
        return std::nullopt;
    }
    if(argc >= subIndex + 1 && std::string(argv[subIndex]) == "init")
        return std::nullopt;
    if(argc >= subIndex + 1 && std::string(argv[subIndex]) == "add")
    {
        for(int i = subIndex + 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--url" || arg == "--archive" ||
               arg == "--strip-components" || arg == "--subdir" ||
               arg == "--add-lib" || arg == "--project-dir")
            {
                return std::nullopt;
            }
        }
    }
    if(argc >= subIndex + 1 &&
       (std::string(argv[subIndex]) == "fetch" ||
        std::string(argv[subIndex]) == "build" ||
        std::string(argv[subIndex]) == "clean"))
    {
        for(int i = subIndex + 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--build-dir" || arg == "--deps-dir" || arg == "--deps")
                return std::nullopt;
        }
    }
    if(manifest_requires_cpp_pkg_frontend())
        return std::nullopt;

    auto srcOpt = find_mlang_pkg_frontend_source(argv[0]);
    if(!srcOpt.has_value())
        return std::nullopt;

    fs::path src = *srcOpt;
    fs::path toolBin;
    fs::path backendBin = resolve_backend_compiler_path(argv[0]);
    if(!ensure_compiled_mla_tool(argv[0], src, backendBin, "mlang-pkg-mla", toolBin,
                                 /*forceCppFrontendEnv=*/true))
        return std::nullopt;

    std::string runCmd = "MLANG_FRONTEND_IMPL=cpp " + shell_quote(toolBin.string()) + " --backend " +
                         shell_quote(backendBin.string());
    for(int i = 1; i < argc; ++i)
        runCmd += " " + shell_quote(argv[i]);

    int runRc = std::system(runCmd.c_str());

    if(runRc < 0)
        return std::nullopt;
    if(WIFEXITED(runRc))
    {
        int code = WEXITSTATUS(runRc);
        if(code == 0)
            return 0;
        return std::nullopt;
    }
    return std::nullopt;
}

static int decode_system_exit_code(int rc)
{
    if(rc < 0)
        return 1;
    if(WIFEXITED(rc))
        return WEXITSTATUS(rc);
    return 1;
}

static std::string log_date_prefix()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &t);
#else
    localtime_r(&t, &tmNow);
#endif
    std::ostringstream os;
    os << std::setfill('0') << std::setw(2) << (tmNow.tm_mon + 1) << "/"
       << std::setw(2) << tmNow.tm_mday << "/" << (tmNow.tm_year + 1900)
       << " " << std::setw(2) << tmNow.tm_hour << ":" << std::setw(2)
       << tmNow.tm_min << ":" << std::setw(2) << tmNow.tm_sec << ":"
       << std::setw(3) << ms.count() << " ";
    return os.str();
}

static bool has_log_date_prefix(const std::string& line)
{
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    size_t i = 0;
    if(!line.empty() && line[0] == '[')
    {
        size_t close = line.find(']');
        if(close == std::string::npos || close + 2 >= line.size() ||
           line[close + 1] != ' ')
        {
            return false;
        }
        i = close + 2;
    }
    if(i + 24 > line.size())
        return false;
    return is_digit(line[i + 0]) && is_digit(line[i + 1]) &&
           line[i + 2] == '/' && is_digit(line[i + 3]) &&
           is_digit(line[i + 4]) && line[i + 5] == '/' &&
           is_digit(line[i + 6]) && is_digit(line[i + 7]) &&
           is_digit(line[i + 8]) && is_digit(line[i + 9]) &&
           line[i + 10] == ' ' && is_digit(line[i + 11]) &&
           is_digit(line[i + 12]) && line[i + 13] == ':' &&
           is_digit(line[i + 14]) && is_digit(line[i + 15]) &&
           line[i + 16] == ':' && is_digit(line[i + 17]) &&
           is_digit(line[i + 18]) && line[i + 19] == ':' &&
           is_digit(line[i + 20]) && is_digit(line[i + 21]) &&
           is_digit(line[i + 22]) && line[i + 23] == ' ';
}

static int run_command_with_dated_output(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return 1;

    std::array<char, 4096> buf{};
    std::string pending;
    while(fgets(buf.data(), static_cast<int>(buf.size()), pipe))
    {
        pending.append(buf.data());
        size_t nl = std::string::npos;
        while((nl = pending.find('\n')) != std::string::npos)
        {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if(has_log_date_prefix(line))
                std::cout << line << std::endl;
            else
                std::cout << log_date_prefix() << line << std::endl;
        }
    }
    if(!pending.empty())
    {
        if(has_log_date_prefix(pending))
            std::cout << pending << std::endl;
        else
            std::cout << log_date_prefix() << pending << std::endl;
    }

    int rc = pclose(pipe);
    return decode_system_exit_code(rc);
}

/// \brief Scan a directory for test (or benchmark) files and execute each as
///        a separate subprocess.
///
/// Discovers \c *_tests.mla / \c test_*.mla files (or \c bench_*.mla in
/// benchmark mode), invokes \c mlang on each one, and reports per-suite
/// pass/fail results.  The optional \p testFilterStr is forwarded to each
/// subprocess via \c --filter so that individual tests can be selected even
/// in directory mode.
///
/// \param argv0           Path to the mlang binary (for re-invocation).
/// \param inPath          Directory to scan for test files.
/// \param benchmarkMode   If \c true, scan for benchmark files instead.
/// \param runTests        If \c false, compile but do not execute.
/// \param benchIterations Number of timed benchmark iterations.
/// \param benchWarmup     Number of warm-up iterations before timing.
/// \param warnPlainColonIf  Whether to warn on plain colon-if syntax.
/// \param warnPlainColonWhile Whether to warn on plain colon-while syntax.
/// \param linkArgs        Additional linker arguments forwarded to each run.
/// \param targetArch      Optional target architecture override.
/// \param testFilterStr   Substring filter for individual test selection.
///
/// \return Exit code: 0 on success, 1 if any suite failed, or \c std::nullopt
///         on internal error.
///
/// \see \ref test_sample.mla — unit test example
/// \see \ref bench_stdlib.mla — benchmark example
/// \see \ref mlang_attributes.mla — \c #[test] with \c #[derive(Debug)]
/// \see \ref testing_mock_example.mla — mock-based testing
static std::optional<int>
run_test_directory_mode(const char* argv0, const std::filesystem::path& inPath,
                        bool benchmarkMode, bool runTests, int benchIterations,
                        int benchWarmup, bool warnPlainColonIf,
                        bool warnPlainColonWhile,
                        const std::vector<std::string>& linkArgs,
                        const std::string& targetArch,
                        const std::string& testFilterStr)
{
    std::error_code tec;
    std::vector<std::filesystem::path> files;
    for(std::filesystem::directory_iterator it(inPath, tec), end; it != end;
        ++it)
    {
        if(!it->is_regular_file(tec))
            continue;
        auto p = it->path();
        if(p.extension() != ".mla")
            continue;
        if(p.filename() == "__mlang_test_root.mla")
            continue;
        std::string filename = p.filename().string();
        if(benchmarkMode)
        {
            if(filename.rfind("bench_", 0) != 0)
                continue;
        }
        else
        {
            bool isPrefixed = filename.rfind("test_", 0) == 0;
            bool isSuffixed = filename.size() >= 10 &&
                              filename.compare(filename.size() - 10, 10,
                                               "_tests.mla") == 0;
            if(!isPrefixed && !isSuffixed)
                continue;
        }
        if(!benchmarkMode)
        {
            std::ifstream in(p);
            if(!in)
                continue;
            std::string contents((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
            if(contents.find("#[test]") == std::string::npos)
                continue;
        }
        files.push_back(p);
    }

    if(files.empty())
    {
        std::cerr << "Error: No .mla " << (benchmarkMode ? "benchmark" : "test")
                  << " files found in " << inPath.string() << std::endl;
        return 1;
    }

    std::sort(files.begin(), files.end());
    int failedSuites = 0;
    for(const auto& file : files)
    {
        std::cout << log_date_prefix() << "[SUITE] " << file.filename().string()
                  << std::endl;
        std::string cmd;
        if(benchmarkMode)
        {
            cmd = shell_quote(argv0 ? argv0 : "mlang") + " bench " +
                  shell_quote(file.string());
            cmd += " --bench-iters " + std::to_string(benchIterations);
            cmd += " --bench-warmup " + std::to_string(benchWarmup);
        }
        else
        {
            cmd = shell_quote(argv0 ? argv0 : "mlang") + " --tests " +
                  shell_quote(file.string());
            if(!runTests)
                cmd += " --no-run";
            if(!warnPlainColonIf)
                cmd += " -Wno-colon-if";
            if(!warnPlainColonWhile)
                cmd += " -Wno-colon-while";
        }
        if(!testFilterStr.empty())
            cmd += " --filter " + shell_quote(testFilterStr);
        for(const auto& la : linkArgs)
            cmd += " " + shell_quote(la);
        if(!targetArch.empty())
            cmd += " --target-arch " + shell_quote(targetArch);

        int rc = benchmarkMode ? decode_system_exit_code(std::system(cmd.c_str()))
                               : run_command_with_dated_output(cmd);
        if(rc == 0)
            std::cout << log_date_prefix() << "[SUITE PASS] "
                      << file.filename().string() << std::endl;
        else
        {
            ++failedSuites;
            std::cout << log_date_prefix() << "[SUITE FAIL] "
                      << file.filename().string() << " rc=" << rc
                      << std::endl;
        }
    }
    return failedSuites == 0 ? 0 : 1;
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
    if(std::string(argv[1]) != "pkg")
    {
        const char* frontendEnv = std::getenv("MLANG_FRONTEND_IMPL");
        std::string frontendImpl = frontendEnv ? frontendEnv : "";
        if(frontendImpl == "mla")
        {
            if(auto rc = run_mlang_frontend(argc, argv); rc.has_value())
                return *rc;
        }
    }
    if(std::string(argv[1]) == "pkg")
    {
        const char* implEnv = std::getenv("MLANG_PKG_IMPL");
        std::string impl = implEnv ? implEnv : "";
        bool preferMla = impl.empty() || impl == "mla";
        bool forceCpp = impl == "cpp";
        // Scan every pkg arg (not just the leading ones) so the user can
        // place --config and the shorthand flags (--tasks, --color, --tests,
        // bare .toml manifests) in any position.
        bool shorthandManifestTasks = false;
        bool shorthandTests = false;
        for(int i = 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if(arg == "--config" && i + 1 < argc)
            {
                ++i;
                continue;
            }
            if(arg.rfind("--config=", 0) == 0)
                continue;
            if(arg == "--tasks" || arg == "--color" ||
               (arg.size() >= 5 && arg.substr(arg.size() - 5) == ".toml"))
                shorthandManifestTasks = true;
            if(arg == "--tests")
                shorthandTests = true;
        }

        if(preferMla && !forceCpp && !shorthandManifestTasks &&
           !shorthandTests)
        {
            if(auto rc = run_mlang_pkg_frontend(argc, argv); rc.has_value())
                return *rc;
        }

        PackageManager pkg;
        return pkg.run(argc, argv);
    }

    bool testMode = false;
    bool benchmarkMode = false;
    bool runTests = false;
    bool includeTests = true;
    int benchIterations = 100000;
    int benchWarmup = 10000;
    std::string testFilterStr;
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
    else if(std::string(argv[1]) == "bench")
    {
        testMode = true;
        benchmarkMode = true;
        runTests = true;
        argStart = 2;
    }
    else if(std::string(argv[1]) == "--tests")
    {
        testMode = true;
        runTests = true;
        argStart = 2;
    }

    // Parse command line arguments
    std::string inputFile;
    std::string outputFile = "a.out";
    bool outputPathExplicit = false;
    bool emitObjectOnly = false;
    bool emitAssembly = false;
    bool emitLLVMIR = false;
    bool emitBitcode = false;
    std::string optimizationLevel = "-O2";
    if(const char* defaultOptEnv = std::getenv("MLANG_DEFAULT_OPT_LEVEL"))
    {
        std::string opt = defaultOptEnv;
        if(!opt.empty() && opt[0] != '-')
            opt = "-" + opt;
        if(opt == "-O0" || opt == "-Og" || opt == "-O1" || opt == "-O2" ||
           opt == "-O3" || opt == "-Os" || opt == "-Oz")
            optimizationLevel = opt;
    }
    bool verbose = false;
    bool debugMode = false;
    bool warnPlainColonIf = true;
    bool warnPlainColonWhile = true;
    bool warnResultUnwrap = true;
    std::string targetArch;
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
            outputPathExplicit = true;
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
        else if(arg == "--target-arch" && i + 1 < argc)
        {
            targetArch = normalize_target_arch_name(argv[++i]);
            if(targetArch.empty())
            {
                std::cerr << "Invalid value for --target-arch: " << argv[i]
                          << " (expected x86, x64, or aarch64)" << std::endl;
                return 1;
            }
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
        else if(arg == "-O0" || arg == "-Og" || arg == "-O1" ||
                arg == "-O2" || arg == "-O3" || arg == "-Os" ||
                arg == "-Oz")
        {
            optimizationLevel = arg;
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
        else if(arg == "--tests")
        {
            testMode = true;
            runTests = true;
        }
        else if(arg == "-Wno-colon-if")
        {
            warnPlainColonIf = false;
        }
        else if(arg == "-Wno-colon-while")
        {
            warnPlainColonWhile = false;
        }
        else if(arg == "-Wno-unwrap")
        {
            warnResultUnwrap = false;
        }
        else if(arg == "--no-run" && testMode)
        {
            runTests = false;
        }
        else if(arg == "--filter" && i + 1 < argc && testMode)
        {
            testFilterStr = argv[++i];
        }
        else if(arg == "--bench-iters" && i + 1 < argc && testMode)
        {
            try
            {
                benchIterations = std::max(1, std::stoi(argv[++i]));
            }
            catch(const std::exception&)
            {
                std::cerr << "Invalid value for --bench-iters" << std::endl;
                return 1;
            }
        }
        else if(arg == "--bench-warmup" && i + 1 < argc && testMode)
        {
            try
            {
                benchWarmup = std::max(0, std::stoi(argv[++i]));
            }
            catch(const std::exception&)
            {
                std::cerr << "Invalid value for --bench-warmup" << std::endl;
                return 1;
            }
        }
        else if(arg[0] != '-')
        {
            if(inputFile.empty())
            {
                inputFile = arg;
            }
            else if(is_linker_input_path(arg))
            {
                linkArgs.push_back(arg);
            }
            else
            {
                inputFile = arg;
            }
        }
        else
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if(testMode)
    {
        // Tests intentionally use colon-form if/while in many fixtures.
        warnPlainColonIf = false;
        warnPlainColonWhile = false;
    }
    if(const char* noColonEnv = std::getenv("MLANG_NO_COLON_WARNINGS"))
    {
        if(noColonEnv[0] != '\0' && std::string(noColonEnv) != "0")
        {
            warnPlainColonIf = false;
            warnPlainColonWhile = false;
        }
    }
    if(const char* noUnwrapEnv = std::getenv("MLANG_NO_UNWRAP_WARNINGS"))
    {
        if(noUnwrapEnv[0] != '\0' && std::string(noUnwrapEnv) != "0")
            warnResultUnwrap = false;
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
    const char* artifactDirEnv = std::getenv("MLANG_ARTIFACT_DIR");
    std::string artifactDir = artifactDirEnv ? std::string(artifactDirEnv) : "";
    if(!artifactDir.empty())
    {
        std::error_code aec;
        std::filesystem::create_directories(std::filesystem::path(artifactDir), aec);
    }
    if(testMode)
    {
        std::error_code tec;
        std::filesystem::path inPath = inputFile;
        if(std::filesystem::is_directory(inPath, tec))
        {
            auto rc = run_test_directory_mode(argv[0], inPath, benchmarkMode,
                                              runTests, benchIterations,
                                              benchWarmup, warnPlainColonIf,
                                              warnPlainColonWhile, linkArgs,
                                              targetArch, testFilterStr);
            if(rc.has_value())
                return *rc;
            return 1;
        }
        else if(outputFile == "a.out")
        {
            std::string defaultName =
                benchmarkMode ? "mlang_bench_bin" : "mlang_test_bin";
            if(!outputPathExplicit && !artifactDir.empty())
            {
                outputFile =
                    (std::filesystem::path(artifactDir) / defaultName).string();
            }
            else
            {
                outputFile = defaultName;
            }
        }
    }

    {
        std::error_code oec;
        std::filesystem::path outPath(outputFile);
        if(!outputFile.empty() && std::filesystem::is_directory(outPath, oec))
        {
            std::string baseName;
            if(testMode)
            {
                baseName = benchmarkMode ? "mlang_bench_bin" : "mlang_test_bin";
            }
            else
            {
                std::filesystem::path inPath(inputFile);
                baseName = inPath.stem().string();
                if(baseName.empty())
                    baseName = "a.out";
            }
            outputFile = (outPath / baseName).string();
        }
    }

    std::ifstream input_stream(inputFile);
    if(!input_stream)
    {
        std::cerr << "Error opening file: " << inputFile << std::endl;
        return 1;
    }
    const std::string rawInput((std::istreambuf_iterator<char>(input_stream)),
                               std::istreambuf_iterator<char>());
    const std::string filteredInput =
        mlang::preprocess_conditional_regions(rawInput, targetArch);

    // Create LLVM context and module
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder(context);
    std::unique_ptr<llvm::Module> module =
        std::make_unique<llvm::Module>("MLang", context);
    if(!targetArch.empty())
    {
        const std::string targetTriple =
            target_triple_for_arch_override(targetArch);
#if LLVM_VERSION_MAJOR >= 21
        module->setTargetTriple(llvm::Triple(targetTriple));
#else
        module->setTargetTriple(targetTriple);
#endif
    }

    std::vector<std::string> modulePaths;
    std::vector<std::string> moduleSearchPaths;
    std::unique_ptr<ModuleLoader> moduleLoader;

    try
    {
        // Parse the input
        if(verbose)
        {
            std::cout << "Parsing " << inputFile << "..." << std::endl;
        }

        parseHadError = false;
        g_sourceFile = inputFile.c_str();
        g_targetArchForParse = targetArch.c_str();
        YY_BUFFER_STATE parseBuffer = yy_scan_bytes(
            filteredInput.data(), static_cast<int>(filteredInput.size()));
        const int parseResult = yyparse();
        yy_delete_buffer(parseBuffer);
        if(parseResult != 0 || parseHadError)
        {
            std::cerr << "Parsing failed. See previous diagnostics and "
                      << mlang::diag::docs_page() << "." << std::endl;
            return 1;
        }

        if(verbose)
        {
            std::cout << "Parsing completed successfully." << std::endl;
        }

        if(!programRoot)
        {
            std::cerr << "Error: No program root node created" << std::endl;
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
            moduleLoader =
                std::make_unique<ModuleLoader>(basePath, moduleSearchPaths);
            moduleLoader->setTargetArch(targetArch);

            // Process mod declarations (load modules)
            if(!program->modules.empty())
            {
                if(verbose)
                {
                    std::cout << "Loading modules..." << std::endl;
                }

                std::string errorMsg;
                if(!moduleLoader->processModDeclarations(program, errorMsg))
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                    delete programRoot;
                    return 1;
                }

                // Process use declarations (import symbols)
                if(!moduleLoader->processUseDeclarations(program, errorMsg))
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                    delete programRoot;
                    return 1;
                }

                if(verbose)
                {
                    std::cout << "Modules loaded: ";
                    for(const auto& mod : moduleLoader->getLoadedModules())
                    {
                        std::cout << mod << " ";
                    }
                    std::cout << std::endl;
                }
            }

            modulePaths = moduleLoader->getLoadedModulePaths();
        }

        // Initialize code generator
        CodeGenerator generator(context, builder, module, debugMode);
        generator.setSourceFile(inputFile);
        generator.setTestMode(testMode);
        generator.setBenchmarkMode(benchmarkMode);
        generator.setBenchmarkIterations(benchIterations);
        generator.setBenchmarkWarmupIterations(benchWarmup);
        generator.setWarnPlainColonIf(warnPlainColonIf);
        generator.setWarnPlainColonWhile(warnPlainColonWhile);
        generator.setWarnResultUnwrap(warnResultUnwrap);
        generator.setModuleLoader(moduleLoader.get());
        if(!testMode)
            generator.setIncludeTests(includeTests);
        if(!testFilterStr.empty())
            generator.setTestFilter(testFilterStr);

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
                std::cerr << "Compilation failed due to errors. See previous "
                             "diagnostics and "
                          << mlang::diag::docs_page() << "." << std::endl;
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
            Backend backend(module, targetArch);

            if(verbose)
            {
                std::cout << "Target: " << backend.getTargetTriple()
                          << std::endl;
            }

            // Apply optimizations
            if(optimizationLevel != "-O0")
            {
                if(verbose)
                {
                    std::cout << "Applying optimizations ("
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
                    llFile =
                        default_build_artifact_path(inputFile, ".ll").string();
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
                    bcFile =
                        default_build_artifact_path(inputFile, ".bc").string();
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
                        default_build_artifact_path(inputFile, ".s").string();
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
                        default_build_artifact_path(inputFile, ".o").string();
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
                delete programRoot;
                return 1;
            }
        }
        else
        {
            std::cerr << "Error: Invalid program root node type" << std::endl;
            return 1;
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error during compilation: " << e.what() << std::endl;
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
    append_workspace_mla_files(std::filesystem::current_path(ec), unique, files);
    for(const auto& root : moduleSearchPaths)
        append_workspace_mla_files(std::filesystem::path(root), unique, files);
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
    delete programRoot;

    if(verbose)
    {
        std::cout << "Compilation completed successfully." << std::endl;
    }

    if(testMode && runTests && !emitObjectOnly && !emitAssembly &&
       !emitLLVMIR && !emitBitcode)
    {
        std::string execPath = outputFile;
        if(execPath.find('/') == std::string::npos &&
           execPath.find('\\') == std::string::npos)
        {
            execPath = "./" + execPath;
        }
        int exitCode = run_command_with_dated_output(execPath);
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
