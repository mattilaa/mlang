#include "ast.h"
#include "ir.h"
#include "module.h"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <sstream>
#include <algorithm>
#include <unordered_set>

// Declare functions and globals from parser/lexer
extern int yyparse();
extern FILE* yyin;
extern "C"
{
    extern ASTNode* programRoot;
}

void printUsage(const char* programName)
{
    std::cerr << "Usage: " << programName << " [options] <input_file>\n"
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
              << "  -L <dir>      Add a library search path for linking\n"
              << "  -l <name>     Link with library (e.g. -l m)\n"
              << "  -v            Verbose output\n"
              << "  -h, --help    Show this help message\n"
              << "\nPackage manager:\n"
              << "  " << programName << " pkg init\n"
              << "  " << programName << " pkg add <name> [--git URL] [--rev "
                 "REV] [--tag TAG]\n"
              << "  " << programName
              << " pkg add <name> [--pkg-config NAME] [--system]\n"
              << "  " << programName << " pkg fetch\n"
              << "  " << programName << " pkg build\n"
              << "\nExamples:\n"
              << "  " << programName
              << " test.mla              # Compile to a.out\n"
              << "  " << programName
              << " -o myprogram test.mla # Compile to myprogram\n"
              << "  " << programName
              << " -S test.mla           # Emit assembly\n"
              << "  " << programName
              << " -emit-llvm test.mla   # Emit LLVM IR\n"
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
    const std::vector<std::string>& files)
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
    out << "] }";
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

static bool is_section_line(const std::string& line,
                            const std::string& section)
{
    std::string t = trim(line);
    return t == ("[" + section + "]");
}

static bool line_has_dep(const std::string& line, const std::string& name)
{
    std::string t = trim(line);
    if(t.empty() || t[0] == '#')
        return false;
    if(t.rfind(name, 0) != 0)
        return false;
    size_t pos = t.find('=');
    return pos != std::string::npos;
}

static bool add_dep_to_section(std::string& content,
                               const std::string& section,
                               const std::string& name,
                               const std::string& line)
{
    std::istringstream in(content);
    std::vector<std::string> lines;
    std::string cur;
    while(std::getline(in, cur))
        lines.push_back(cur);

    int section_start = -1;
    int section_end = (int)lines.size();
    for(size_t i = 0; i < lines.size(); ++i)
    {
        if(is_section_line(lines[i], section))
        {
            section_start = (int)i;
            for(size_t j = i + 1; j < lines.size(); ++j)
            {
                std::string t = trim(lines[j]);
                if(!t.empty() && t[0] == '[')
                {
                    section_end = (int)j;
                    break;
                }
            }
            break;
        }
    }

    if(section_start >= 0)
    {
        for(int i = section_start + 1; i < section_end; ++i)
        {
            if(line_has_dep(lines[i], name))
                return false;
        }
        lines.insert(lines.begin() + section_end, line);
    }
    else
    {
        if(!lines.empty() && !lines.back().empty())
            lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(line);
    }

    std::ostringstream out;
    for(size_t i = 0; i < lines.size(); ++i)
    {
        out << lines[i];
        if(i + 1 < lines.size())
            out << "\n";
    }
    content = out.str();
    return true;
}

static std::optional<std::string> find_toml_string(
    const std::string& content, const std::string& key)
{
    std::string needle = key + " = \"";
    size_t pos = content.find(needle);
    if(pos == std::string::npos)
        return std::nullopt;
    pos += needle.size();
    size_t end = content.find('"', pos);
    if(end == std::string::npos)
        return std::nullopt;
    return content.substr(pos, end - pos);
}

struct DepSpec
{
    std::string name;
    std::string git;
    std::string rev;
    std::string tag;
    std::string build;
};

struct LinkFlags
{
    std::vector<std::string> libDirs;
    std::vector<std::string> libs;
};

static std::vector<std::string> split_csv(std::string_view input)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for(char c : input)
    {
        if(c == '{')
            ++depth;
        else if(c == '}')
            --depth;
        if(c == ',' && depth == 0)
        {
            out.push_back(trim(cur));
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    if(!cur.empty())
        out.push_back(trim(cur));
    return out;
}

static std::string unquote(std::string_view v)
{
    std::string t = trim(v);
    if(t.size() >= 2 && t.front() == '"' && t.back() == '"')
        return t.substr(1, t.size() - 2);
    return t;
}

static std::map<std::string, std::string> parse_inline_table(
    const std::string& line)
{
    std::map<std::string, std::string> kv;
    size_t l = line.find('{');
    size_t r = line.rfind('}');
    if(l == std::string::npos || r == std::string::npos || r <= l)
        return kv;
    std::string inner = line.substr(l + 1, r - l - 1);
    for(const auto& part : split_csv(inner))
    {
        if(part.empty())
            continue;
        size_t eq = part.find('=');
        if(eq == std::string::npos)
            continue;
        std::string key = trim(part.substr(0, eq));
        std::string val = unquote(part.substr(eq + 1));
        kv[key] = val;
    }
    return kv;
}

static std::vector<DepSpec> parse_git_deps(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::string section;
    std::vector<DepSpec> deps;
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
        if(section != "dependencies" && section != "c-dependencies")
            continue;
        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        std::string name = trim(t.substr(0, eq));
        if(name.empty())
            continue;
        if(t.find('{') == std::string::npos)
            continue;
        auto kv = parse_inline_table(t);
        auto gitIt = kv.find("git");
        if(gitIt == kv.end())
            continue;
        DepSpec dep;
        dep.name = name;
        dep.git = gitIt->second;
        if(auto it = kv.find("rev"); it != kv.end())
            dep.rev = it->second;
        if(auto it = kv.find("tag"); it != kv.end())
            dep.tag = it->second;
        if(auto it = kv.find("build"); it != kv.end())
            dep.build = it->second;
        if(dep.build.empty())
            dep.build = "cmake";
        deps.push_back(dep);
    }
    return deps;
}

static bool extract_lib_name(const std::filesystem::path& path,
                             std::string& out)
{
    std::string name = path.filename().string();
    if(name.rfind("lib", 0) != 0)
        return false;
    if(path.extension() == ".a" || path.extension() == ".dylib" ||
       path.extension() == ".so")
    {
        std::string stem = path.stem().string();
        if(stem.rfind("lib", 0) != 0 || stem.size() <= 3)
            return false;
        out = stem.substr(3);
        return true;
    }
    size_t soPos = name.find(".so.");
    if(soPos != std::string::npos && soPos > 3)
    {
        out = name.substr(3, soPos - 3);
        return true;
    }
    return false;
}

static void scan_lib_dir(const std::filesystem::path& dir,
                         std::unordered_set<std::string>& libDirs,
                         std::unordered_set<std::string>& libs)
{
    if(!std::filesystem::exists(dir))
        return;
    libDirs.insert(dir.string());
    for(const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if(!entry.is_regular_file())
            continue;
        std::string libName;
        if(extract_lib_name(entry.path(), libName))
            libs.insert(libName);
    }
}

static LinkFlags collect_dep_link_flags(
    const std::vector<DepSpec>& deps,
    const std::filesystem::path& depsDir)
{
    std::unordered_set<std::string> libDirs;
    std::unordered_set<std::string> libs;
    for(const auto& dep : deps)
    {
        std::filesystem::path path = depsDir / dep.name;
        scan_lib_dir(path / "build" / "lib", libDirs, libs);
        scan_lib_dir(path / "build", libDirs, libs);
        scan_lib_dir(path / "lib", libDirs, libs);
    }
    LinkFlags flags;
    flags.libDirs.assign(libDirs.begin(), libDirs.end());
    flags.libs.assign(libs.begin(), libs.end());
    std::sort(flags.libDirs.begin(), flags.libDirs.end());
    std::sort(flags.libs.begin(), flags.libs.end());
    return flags;
}

static int run_command(const std::string& cmd)
{
    std::cout << cmd << std::endl;
    return std::system(cmd.c_str());
}

static int fetch_git_dep(const DepSpec& dep,
                         const std::filesystem::path& depsDir)
{
    std::filesystem::path path = depsDir / dep.name;
    if(!std::filesystem::exists(path))
    {
        std::string cloneCmd = "git clone " + dep.git + " " + path.string();
        if(run_command(cloneCmd) != 0)
            return 1;
    }
    else
    {
        std::string fetchCmd = "git -C " + path.string() + " fetch --all --tags";
        if(run_command(fetchCmd) != 0)
            return 1;
    }

    if(!dep.rev.empty())
    {
        std::string checkout = "git -C " + path.string() + " checkout " + dep.rev;
        if(run_command(checkout) != 0)
            return 1;
    }
    else if(!dep.tag.empty())
    {
        std::string checkout =
            "git -C " + path.string() + " checkout tags/" + dep.tag;
        if(run_command(checkout) != 0)
            return 1;
    }
    return 0;
}

static int build_git_dep(const DepSpec& dep,
                         const std::filesystem::path& depsDir)
{
    std::filesystem::path path = depsDir / dep.name;
    if(!std::filesystem::exists(path))
        return 1;

    if(dep.build == "cmake")
    {
        std::filesystem::path buildDir = path / "build";
        std::string cfg = "cmake -S " + path.string() + " -B " +
                          buildDir.string();
        if(run_command(cfg) != 0)
            return 1;
        std::string build = "cmake --build " + buildDir.string();
        return run_command(build);
    }
    if(dep.build == "meson")
    {
        std::filesystem::path buildDir = path / "build";
        if(!std::filesystem::exists(buildDir))
        {
            std::string setup =
                "meson setup " + buildDir.string() + " " + path.string();
            if(run_command(setup) != 0)
                return 1;
        }
        std::string compile = "meson compile -C " + buildDir.string();
        return run_command(compile);
    }
    if(dep.build == "make")
    {
        std::string cmd = "make -C " + path.string();
        return run_command(cmd);
    }

    std::cerr << "Unknown build system: " << dep.build << "\n";
    return 1;
}

static int handle_pkg_command(int argc, char** argv)
{
    if(argc < 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " pkg <init|add|fetch|build>\n";
        return 1;
    }

    std::string sub = argv[2];
    std::filesystem::path manifestPath = "mlang.toml";

    if(sub == "init")
    {
        if(std::filesystem::exists(manifestPath))
        {
            std::cerr << "mlang.toml already exists\n";
            return 1;
        }

        std::string name =
            std::filesystem::current_path().filename().string();
        if(name.empty())
            name = "app";

        std::ofstream out(manifestPath, std::ios::binary);
        if(!out)
        {
            std::cerr << "Failed to write mlang.toml\n";
            return 1;
        }
        out << "[package]\n"
            << "name = \"" << name << "\"\n"
            << "version = \"0.1.0\"\n"
            << "entry = \"src/main.mla\"\n\n"
            << "[dependencies]\n\n"
            << "[c-dependencies]\n";
        return 0;
    }

    if(sub == "add")
    {
        if(argc < 4)
        {
            std::cerr << "Usage: " << argv[0]
                      << " pkg add <name> [--git URL] [--rev REV] [--tag TAG]\n"
                      << "       " << argv[0]
                      << " pkg add <name> [--pkg-config NAME] [--system]\n";
            return 1;
        }

        std::string name = argv[3];
        std::string gitUrl;
        std::string rev;
        std::string tag;
        std::string pkgConfig;
        bool systemDep = false;

        for(int i = 4; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--git" && i + 1 < argc)
                gitUrl = argv[++i];
            else if(arg == "--rev" && i + 1 < argc)
                rev = argv[++i];
            else if(arg == "--tag" && i + 1 < argc)
                tag = argv[++i];
            else if(arg == "--pkg-config" && i + 1 < argc)
                pkgConfig = argv[++i];
            else if(arg == "--system")
                systemDep = true;
            else
            {
                std::cerr << "Unknown option: " << arg << "\n";
                return 1;
            }
        }

        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << "mlang.toml not found. Run 'mlang pkg init' first.\n";
            return 1;
        }

        std::ifstream in(manifestPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if(content.empty())
        {
            std::cerr << "Failed to read mlang.toml\n";
            return 1;
        }

        std::string section = "dependencies";
        std::string line;
        if(systemDep || !pkgConfig.empty())
        {
            section = "c-dependencies";
            if(systemDep)
                line = name + " = { system = true }";
            else
                line = name + " = { pkg_config = \"" + pkgConfig + "\" }";
        }
        else if(!gitUrl.empty())
        {
            line = name + " = { git = \"" + gitUrl + "\"";
            if(!rev.empty())
                line += ", rev = \"" + rev + "\"";
            if(!tag.empty())
                line += ", tag = \"" + tag + "\"";
            line += " }";
        }
        else
        {
            line = name + " = \"*\"";
        }

        bool added = add_dep_to_section(content, section, name, line);
        if(!added)
        {
            std::cerr << "Dependency already exists: " << name << "\n";
            return 1;
        }

        std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
        if(!out)
        {
            std::cerr << "Failed to update mlang.toml\n";
            return 1;
        }
        out << content;
        return 0;
    }

    if(sub == "fetch")
    {
        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << "mlang.toml not found. Run 'mlang pkg init' first.\n";
            return 1;
        }
        std::ifstream in(manifestPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if(content.empty())
        {
            std::cerr << "Failed to read mlang.toml\n";
            return 1;
        }
        auto deps = parse_git_deps(content);
        std::filesystem::path depsDir = std::filesystem::path("build") / "deps";
        std::filesystem::create_directories(depsDir);
        for(const auto& dep : deps)
        {
            if(fetch_git_dep(dep, depsDir) != 0)
                return 1;
        }
        std::cout << "Fetch completed.\n";
        return 0;
    }

    if(sub == "build")
    {
        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << "mlang.toml not found. Run 'mlang pkg init' first.\n";
            return 1;
        }

        std::ifstream in(manifestPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if(content.empty())
        {
            std::cerr << "Failed to read mlang.toml\n";
            return 1;
        }

        auto deps = parse_git_deps(content);
        std::filesystem::path depsDir = std::filesystem::path("build") / "deps";
        std::filesystem::create_directories(depsDir);
        for(const auto& dep : deps)
        {
            if(fetch_git_dep(dep, depsDir) != 0)
                return 1;
        }
        for(const auto& dep : deps)
        {
            if(build_git_dep(dep, depsDir) != 0)
                return 1;
        }

        LinkFlags linkFlags = collect_dep_link_flags(deps, depsDir);

        std::string entry = "src/main.mla";
        if(auto v = find_toml_string(content, "entry"); v.has_value())
            entry = v.value();
        std::string name = "app";
        if(auto v = find_toml_string(content, "name"); v.has_value())
            name = v.value();

        std::filesystem::create_directories("build");
        std::string output = "build/" + name;

        std::string cmd =
            std::string(argv[0]) + " " + entry + " -o " + output;
        for(const auto& dir : linkFlags.libDirs)
            cmd += " -L" + dir;
        for(const auto& lib : linkFlags.libs)
            cmd += " -l" + lib;
        int rc = std::system(cmd.c_str());
        if(rc != 0)
        {
            std::cerr << "Build failed.\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "Unknown pkg subcommand: " << sub << "\n";
    return 1;
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }
    if(std::string(argv[1]) == "pkg")
    {
        return handle_pkg_command(argc, argv);
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
    std::vector<std::string> linkArgs;

    for(int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if(arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
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
        std::cerr << "Error: No input file specified" << std::endl;
        printUsage(argv[0]);
        return 1;
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

    try
    {
        // Parse the input
        if(verbose)
        {
            std::cout << "Parsing " << inputFile << "..." << std::endl;
        }

        if(yyparse() != 0)
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

            // Initialize module loader
            ModuleLoader moduleLoader(basePath);

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
        CodeGenerator generator(context, builder, module);

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

    // Clean up
    fclose(input_file);
    delete programRoot;

    if(verbose)
    {
        std::cout << "Compilation completed successfully." << std::endl;
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
    write_mlang_commands_json(files);

    return 0;
}
