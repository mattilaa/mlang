#include "package_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#ifndef MLANG_VERSION
#define MLANG_VERSION "0.1.0"
#endif

namespace {

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

static std::string normalize_target_arch_name(const std::string& arch)
{
    if(arch == "x86" || arch == "i386" || arch == "i686")
        return "x86";
    if(arch == "x64" || arch == "x86_64" || arch == "amd64" ||
       arch == "x86-64")
        return "x64";
    if(arch == "aarch64" || arch == "arm64")
        return "aarch64";
    return "";
}

static std::string normalize_opt_level(std::string opt)
{
    opt = trim(opt);
    if(opt.empty())
        return "";
    if(opt[0] != '-')
        opt = "-" + opt;
    if(opt == "-O0" || opt == "-O1" || opt == "-O2" || opt == "-O3")
        return opt;
    return "";
}

struct BuildConfig
{
    std::string optLevel;
    std::string targetArch;
    std::vector<std::string> compilerFlags;
    std::vector<std::string> linkerFlags;
    std::vector<std::string> libPaths;
    std::vector<std::string> libs;
};

static std::string unquote(std::string_view v);

static void append_toml_string_list_value(const std::string& value,
                                          std::vector<std::string>& out)
{
    std::string t = trim(value);
    if(t.empty())
        return;
    if(t.front() == '[' && t.back() == ']')
    {
        for(const auto& part : split_toml_array(t.substr(1, t.size() - 2)))
        {
            std::string v = unquote(part);
            if(!v.empty())
                out.push_back(v);
        }
        return;
    }

    std::string v = unquote(t);
    if(!v.empty())
        out.push_back(v);
}

static BuildConfig parse_build_config(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::string section;
    BuildConfig cfg;
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
        if(section != "tool.mlang")
            continue;

        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        std::string key = trim(t.substr(0, eq));
        std::string value = trim(t.substr(eq + 1));
        if(key == "opt_level")
        {
            cfg.optLevel = normalize_opt_level(unquote(value));
        }
        else if(key == "target_arch")
        {
            cfg.targetArch = normalize_target_arch_name(unquote(value));
        }
        else if(key == "compiler_flags")
        {
            append_toml_string_list_value(value, cfg.compilerFlags);
        }
        else if(key == "linker_flags")
        {
            append_toml_string_list_value(value, cfg.linkerFlags);
        }
        else if(key == "lib_paths")
        {
            append_toml_string_list_value(value, cfg.libPaths);
        }
        else if(key == "libs")
        {
            append_toml_string_list_value(value, cfg.libs);
        }
    }
    return cfg;
}

struct DepSpec
{
    std::string name;
    std::string git;
    std::string rev;
    std::string tag;
    std::string build;
    std::string cmakeArgs;
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

static std::vector<std::string> split_semicolon(std::string_view input)
{
    std::vector<std::string> out;
    std::string cur;
    for(char c : input)
    {
        if(c == ';')
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
        if(auto it = kv.find("cmake_args"); it != kv.end())
            dep.cmakeArgs = it->second;
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

static std::optional<std::string> run_command_capture(const std::string& cmd)
{
    std::string full = cmd + " 2>/dev/null";
    FILE* pipe = popen(full.c_str(), "r");
    if(!pipe)
        return std::nullopt;
    std::string out;
    char buf[256];
    while(fgets(buf, sizeof(buf), pipe))
        out += buf;
    int rc = pclose(pipe);
    if(rc != 0)
        return std::nullopt;
    return out;
}

static std::vector<std::string> split_shell_tokens(std::string_view input)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;
    char quote = 0;
    for(char c : input)
    {
        if(in_quotes)
        {
            if(c == quote)
            {
                in_quotes = false;
                continue;
            }
            cur.push_back(c);
            continue;
        }
        if(c == '"' || c == '\'')
        {
            in_quotes = true;
            quote = c;
            continue;
        }
        if(std::isspace(static_cast<unsigned char>(c)))
        {
            if(!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if(!cur.empty())
        out.push_back(cur);
    return out;
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
                         const std::filesystem::path& depsDir,
                         bool useNinja)
{
    std::filesystem::path path = depsDir / dep.name;
    if(!std::filesystem::exists(path))
        return 1;

    if(dep.build == "cmake")
    {
        std::filesystem::path buildDir = path / "build";
        std::string cfg = "cmake -S " + path.string() + " -B " +
                          buildDir.string();
        if(useNinja)
            cfg += " -G Ninja";
        if(!dep.cmakeArgs.empty())
        {
            for(const auto& arg : split_semicolon(dep.cmakeArgs))
            {
                if(arg.empty())
                    continue;
                if(arg.rfind("-", 0) == 0)
                    cfg += " " + arg;
                else
                    cfg += " -D" + arg;
            }
        }
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

struct CDepSpec
{
    std::string name;
    std::string pkgConfig;
    bool usePkgConfig = false;
};

static std::vector<CDepSpec> parse_c_deps(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::string section;
    std::vector<CDepSpec> deps;
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
        if(section != "c-dependencies")
            continue;
        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        std::string name = trim(t.substr(0, eq));
        if(name.empty())
            continue;

        CDepSpec dep;
        dep.name = name;
        if(t.find('{') != std::string::npos)
        {
            auto kv = parse_inline_table(t);
            if(auto it = kv.find("pkg_config"); it != kv.end())
            {
                dep.pkgConfig = it->second;
                dep.usePkgConfig = true;
            }
            else if(auto it = kv.find("system"); it != kv.end() &&
                    (it->second == "true" || it->second == "1"))
            {
                dep.pkgConfig = name;
                dep.usePkgConfig = true;
            }
        }
        else
        {
            dep.pkgConfig = name;
            dep.usePkgConfig = true;
        }

        if(dep.usePkgConfig)
            deps.push_back(dep);
    }
    return deps;
}

static bool append_pkg_config_flags(const CDepSpec& dep,
                                    std::vector<std::string>& outFlags)
{
    if(!dep.usePkgConfig || dep.pkgConfig.empty())
        return true;
    std::string cmd = "pkg-config --cflags --libs " + dep.pkgConfig;
    auto result = run_command_capture(cmd);
    if(!result.has_value())
    {
        std::cerr << "pkg-config failed for: " << dep.pkgConfig << "\n";
        return false;
    }
    for(const auto& token : split_shell_tokens(result.value()))
        outFlags.push_back(token);
    return true;
}

} // namespace

int PackageManager::run(int argc, char** argv)
{
    if(argc < 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " pkg <init|add|fetch|build|clean>\n";
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
            << "version = \"" << MLANG_VERSION << "\"\n"
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
        auto cdeps = parse_c_deps(content);
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
        std::string optFlag;
        bool useNinja = false;
        for(int i = 3; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "-O0" || arg == "-O1" || arg == "-O2" || arg == "-O3")
            {
                optFlag = arg;
            }
            else if(arg == "--ninja")
            {
                useNinja = true;
            }
            else
            {
                std::cerr << "Unknown option for 'pkg build': " << arg << "\n"
                          << "Usage: " << argv[0]
                          << " pkg build [-O0|-O1|-O2|-O3] [--ninja]\n";
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

        auto deps = parse_git_deps(content);
        auto cdeps = parse_c_deps(content);
        BuildConfig buildConfig = parse_build_config(content);
        std::filesystem::path depsDir = std::filesystem::path("build") / "deps";
        std::filesystem::create_directories(depsDir);
        for(const auto& dep : deps)
        {
            if(fetch_git_dep(dep, depsDir) != 0)
                return 1;
        }
        for(const auto& dep : deps)
        {
            if(build_git_dep(dep, depsDir, useNinja) != 0)
                return 1;
        }

        LinkFlags linkFlags = collect_dep_link_flags(deps, depsDir);
        std::vector<std::string> pkgFlags;
        for(const auto& dep : cdeps)
        {
            if(!append_pkg_config_flags(dep, pkgFlags))
                return 1;
        }

        std::string entry = "src/main.mla";
        if(auto v = find_toml_string(content, "entry"); v.has_value())
            entry = v.value();
        std::string name = "app";
        if(auto v = find_toml_string(content, "name"); v.has_value())
            name = v.value();
        if(optFlag.empty())
            optFlag = buildConfig.optLevel;

        std::filesystem::create_directories("build");
        std::string output = "build/" + name;

        std::string cmd =
            std::string(argv[0]) + " " + entry + " -o " + output;
        if(!buildConfig.targetArch.empty())
            cmd += " --target-arch " + buildConfig.targetArch;
        if(!optFlag.empty())
            cmd += " " + optFlag;
        for(const auto& flag : buildConfig.compilerFlags)
            cmd += " " + flag;
        for(const auto& dir : buildConfig.libPaths)
            cmd += " -L" + dir;
        for(const auto& lib : buildConfig.libs)
            cmd += " -l" + lib;
        for(const auto& dir : linkFlags.libDirs)
            cmd += " -L" + dir;
        for(const auto& lib : linkFlags.libs)
            cmd += " -l" + lib;
        for(const auto& dir : linkFlags.libDirs)
            cmd += " -Wl,-rpath," + dir;
        for(const auto& flag : buildConfig.linkerFlags)
            cmd += " " + flag;
        for(const auto& flag : pkgFlags)
            cmd += " " + flag;
        int rc = std::system(cmd.c_str());
        if(rc != 0)
        {
            std::cerr << "Build failed.\n";
            return 1;
        }
        return 0;
    }

    if(sub == "clean")
    {
        const std::filesystem::path buildDir = "build";
        if(!std::filesystem::exists(buildDir))
        {
            std::cout << "No artifacts to clean in " << buildDir.string()
                      << "\n";
            return 0;
        }
        std::error_code ec;
        std::filesystem::remove_all(buildDir, ec);
        if(ec)
        {
            std::cerr << "Failed to clean " << buildDir.string() << ": "
                      << ec.message() << "\n";
            return 1;
        }
        std::cout << "Cleaned " << buildDir.string() << "\n";
        return 0;
    }

    std::cerr << "Unknown pkg subcommand: " << sub << "\n";
    return 1;
}
