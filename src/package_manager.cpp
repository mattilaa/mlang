#include "package_manager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SHA256.h>

#ifndef MLANG_VERSION
#define MLANG_VERSION "0.1.0"
#endif

namespace
{

struct PackageManifest;
struct BuildConfig;
struct TaskSpec;

static std::vector<std::string> split_toml_array(std::string_view input);
static std::string shell_quote(const std::string& s);
static std::string unquote(std::string_view v);
static std::string unquote_preserve(std::string_view v);
static std::string unescape_toml_basic_string(std::string_view v);
static void append_toml_command_value(const std::string& value,
                                      std::vector<std::string>& out);
static void append_toml_commands_value(const std::string& value,
                                       std::vector<std::string>& out);
static std::string format_task_elapsed(std::chrono::milliseconds elapsed);
static std::string shorten_progress_description(const std::string& description);
static std::string sanitize_progress_output(std::string text);
static std::string shorten_progress_output(const std::string& text);
static std::vector<std::string>
parse_workspace_members(const std::string& content);
static std::string current_host_name();
static void append_toml_string_list_value(const std::string& value,
                                          std::vector<std::string>& out);
static bool parse_toml_bool_value(const std::string& value);
static int run_task_for_manifest(const PackageManifest& pkg,
                                 const std::string& taskName);
static int run_task_for_manifest(const PackageManifest& pkg,
                                 const std::string& taskName,
                                 const BuildConfig& buildConfig);
static bool task_list_contains_name(const std::vector<TaskSpec>& tasks,
                                    const std::string& name);
static void
append_toml_string_list_value_preserve(const std::string& value,
                                       std::vector<std::string>& out);

enum class TaskTomlKey
{
    Name,
    Message,
    Print,
    SupportedHosts,
    UnsupportedMessage,
    Phase,
    Workdir,
    Language,
    Source,
    Output,
    Inputs,
    CompileOnly,
    Parallel,
    LogOutput,
    InlineOutput,
    DependsOn,
    PhaseDependsOn,
    JoinOn,
    PhaseJoinOn,
    Next,
    NextPhases,
    Command,
    Env,
    Script,
    Shell,
    Commands,
    Chmod,
    ChmodPath,
    ChmodPaths,
    OptLevel,
    TargetArch,
    PathEntries,
    CompilerFlags,
    LinkerFlags,
    LibPaths,
    Libs,
    StaticDeps,
    StaticCppRuntime,
    Sign,
};

enum class TargetArchAlias
{
    X86,
    I386,
    I686,
    X64,
    X86_64,
    Amd64,
    X86Dash64,
    Aarch64,
    Arm64,
};

enum class TargetArchValue
{
    X86,
    X64,
    Aarch64,
};

enum class OptLevelAlias
{
    O0,
    Og,
    O1,
    O2,
    O3,
    Os,
    Oz,
};

template <typename Enum, size_t N>
static std::optional<Enum>
find_enum_key(std::string_view key,
              const std::array<std::pair<Enum, std::string_view>, N>& mappings)
{
    const auto it =
        std::find_if(mappings.begin(), mappings.end(),
                     [&](const auto& entry) { return entry.second == key; });
    if(it == mappings.end())
        return std::nullopt;
    return it->first;
}

template <typename Enum, size_t N>
static std::optional<std::string_view>
find_enum_text(Enum key,
               const std::array<std::pair<Enum, std::string_view>, N>& mappings)
{
    const auto it =
        std::find_if(mappings.begin(), mappings.end(),
                     [&](const auto& entry) { return entry.first == key; });
    if(it == mappings.end())
        return std::nullopt;
    return it->second;
}

enum class TaskLanguageAlias
{
    Mlang,
    C,
    Cpp,
    Cxx,
    CPlusPlus,
    CC,
};

enum class TaskLanguage
{
    Mlang,
    C,
    Cpp,
};

static constexpr std::array<std::pair<TaskTomlKey, std::string_view>, 39>
    kTaskTomlKeys{{{TaskTomlKey::Name, "name"},
                   {TaskTomlKey::Message, "message"},
                   {TaskTomlKey::Print, "print"},
                   {TaskTomlKey::SupportedHosts, "supported_hosts"},
                   {TaskTomlKey::UnsupportedMessage, "unsupported_message"},
                   {TaskTomlKey::Phase, "phase"},
                   {TaskTomlKey::Workdir, "workdir"},
                   {TaskTomlKey::Language, "language"},
                   {TaskTomlKey::Source, "source"},
                   {TaskTomlKey::Output, "output"},
                   {TaskTomlKey::Inputs, "inputs"},
                   {TaskTomlKey::CompileOnly, "compile_only"},
                   {TaskTomlKey::Parallel, "parallel"},
                   {TaskTomlKey::LogOutput, "log_output"},
                   {TaskTomlKey::InlineOutput, "inline_output"},
                   {TaskTomlKey::DependsOn, "depends_on"},
                   {TaskTomlKey::PhaseDependsOn, "phase_depends_on"},
                   {TaskTomlKey::JoinOn, "join_on"},
                   {TaskTomlKey::PhaseJoinOn, "phase_join_on"},
                   {TaskTomlKey::Next, "next"},
                   {TaskTomlKey::NextPhases, "next_phases"},
                   {TaskTomlKey::Command, "command"},
                   {TaskTomlKey::Env, "env"},
                   {TaskTomlKey::Script, "script"},
                   {TaskTomlKey::Shell, "shell"},
                   {TaskTomlKey::Commands, "commands"},
                   {TaskTomlKey::Chmod, "chmod"},
                   {TaskTomlKey::ChmodPath, "chmod_path"},
                   {TaskTomlKey::ChmodPaths, "chmod_paths"},
                   {TaskTomlKey::OptLevel, "opt_level"},
                   {TaskTomlKey::TargetArch, "target_arch"},
                   {TaskTomlKey::PathEntries, "path_entries"},
                   {TaskTomlKey::CompilerFlags, "compiler_flags"},
                   {TaskTomlKey::LinkerFlags, "linker_flags"},
                   {TaskTomlKey::LibPaths, "lib_paths"},
                   {TaskTomlKey::Libs, "libs"},
                   {TaskTomlKey::StaticDeps, "static_deps"},
                   {TaskTomlKey::StaticCppRuntime, "static_cpp_runtime"},
                   {TaskTomlKey::Sign, "sign"}}};

static constexpr std::array<std::pair<TaskLanguageAlias, std::string_view>, 6>
    kTaskLanguageAliases{{{TaskLanguageAlias::Mlang, "mlang"},
                          {TaskLanguageAlias::C, "c"},
                          {TaskLanguageAlias::Cpp, "cpp"},
                          {TaskLanguageAlias::Cxx, "cxx"},
                          {TaskLanguageAlias::CPlusPlus, "c++"},
                          {TaskLanguageAlias::CC, "cc"}}};

static constexpr std::array<std::pair<TaskLanguage, std::string_view>, 3>
    kTaskLanguageNames{{{TaskLanguage::Mlang, "mlang"},
                        {TaskLanguage::C, "c"},
                        {TaskLanguage::Cpp, "c++"}}};

static constexpr std::array<std::pair<TargetArchAlias, std::string_view>, 9>
    kTargetArchAliases{{{TargetArchAlias::X86, "x86"},
                        {TargetArchAlias::I386, "i386"},
                        {TargetArchAlias::I686, "i686"},
                        {TargetArchAlias::X64, "x64"},
                        {TargetArchAlias::X86_64, "x86_64"},
                        {TargetArchAlias::Amd64, "amd64"},
                        {TargetArchAlias::X86Dash64, "x86-64"},
                        {TargetArchAlias::Aarch64, "aarch64"},
                        {TargetArchAlias::Arm64, "arm64"}}};

static constexpr std::array<std::pair<TargetArchValue, std::string_view>, 3>
    kTargetArchNames{{{TargetArchValue::X86, "x86"},
                      {TargetArchValue::X64, "x64"},
                      {TargetArchValue::Aarch64, "aarch64"}}};

static constexpr std::array<std::pair<OptLevelAlias, std::string_view>, 7>
    kOptLevelAliases{{{OptLevelAlias::O0, "-O0"},
                      {OptLevelAlias::Og, "-Og"},
                      {OptLevelAlias::O1, "-O1"},
                      {OptLevelAlias::O2, "-O2"},
                      {OptLevelAlias::O3, "-O3"},
                      {OptLevelAlias::Os, "-Os"},
                      {OptLevelAlias::Oz, "-Oz"}}};

static std::string trim(std::string_view s)
{
    size_t start = 0;
    while(start < s.size() &&
          std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while(end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return std::string(s.substr(start, end - start));
}

static std::string strip_toml_comment(std::string_view line)
{
    bool inDoubleQuotes = false;
    bool inSingleQuotes = false;
    bool escaped = false;
    for(size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if(inDoubleQuotes)
        {
            if(escaped)
            {
                escaped = false;
                continue;
            }
            if(c == '\\')
            {
                escaped = true;
                continue;
            }
            if(c == '"')
                inDoubleQuotes = false;
            continue;
        }
        if(inSingleQuotes)
        {
            if(c == '\'')
                inSingleQuotes = false;
            continue;
        }
        if(c == '"')
        {
            inDoubleQuotes = true;
            continue;
        }
        if(c == '\'')
        {
            inSingleQuotes = true;
            continue;
        }
        if(c == '#')
            return trim(line.substr(0, i));
    }
    return trim(line);
}

static bool is_section_line(const std::string& line, const std::string& section)
{
    std::string t = strip_toml_comment(line);
    return t == ("[" + section + "]");
}

static bool line_has_dep(const std::string& line, const std::string& name)
{
    std::string t = strip_toml_comment(line);
    if(t.empty() || t[0] == '#')
        return false;
    if(t.rfind(name, 0) != 0)
        return false;
    size_t pos = t.find('=');
    return pos != std::string::npos;
}

static bool add_dep_to_section(std::string& content, const std::string& section,
                               const std::string& name, const std::string& line)
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

static bool append_unique_quoted_list_entry(std::string& content,
                                            const std::string& section,
                                            const std::string& key,
                                            const std::string& value)
{
    std::istringstream in(content);
    std::vector<std::string> lines;
    std::string cur;
    while(std::getline(in, cur))
        lines.push_back(cur);

    int sectionStart = -1;
    int sectionEnd = static_cast<int>(lines.size());
    int keyLine = -1;
    for(size_t i = 0; i < lines.size(); ++i)
    {
        if(is_section_line(lines[i], section))
        {
            sectionStart = static_cast<int>(i);
            for(size_t j = i + 1; j < lines.size(); ++j)
            {
                std::string t = trim(lines[j]);
                if(!t.empty() && t[0] == '[')
                {
                    sectionEnd = static_cast<int>(j);
                    break;
                }
                size_t eq = t.find('=');
                if(eq == std::string::npos)
                    continue;
                if(trim(t.substr(0, eq)) == key)
                    keyLine = static_cast<int>(j);
            }
            break;
        }
    }

    std::vector<std::string> values;
    if(section == "workspace" && key == "members")
    {
        values = parse_workspace_members(content);
    }
    else if(keyLine >= 0)
    {
        size_t eq = lines[keyLine].find('=');
        if(eq != std::string::npos)
        {
            std::string raw = trim(lines[keyLine].substr(eq + 1));
            if(!raw.empty() && raw.front() == '[' && raw.back() == ']')
            {
                for(const auto& part :
                    split_toml_array(raw.substr(1, raw.size() - 2)))
                {
                    std::string item = unquote(part);
                    if(!item.empty())
                        values.push_back(item);
                }
            }
        }
    }

    if(std::find(values.begin(), values.end(), value) != values.end())
        return false;
    values.push_back(value);

    std::ostringstream arr;
    arr << key << " = [";
    for(size_t i = 0; i < values.size(); ++i)
    {
        if(i > 0)
            arr << ", ";
        arr << "\"" << values[i] << "\"";
    }
    arr << "]";

    if(sectionStart >= 0)
    {
        if(keyLine >= 0)
            lines[keyLine] = arr.str();
        else
            lines.insert(lines.begin() + sectionEnd, arr.str());
    }
    else
    {
        if(!lines.empty() && !lines.back().empty())
            lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(arr.str());
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

static std::optional<std::string> find_toml_string(const std::string& content,
                                                   const std::string& key)
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

static std::optional<std::string>
find_section_toml_string(const std::string& content, const std::string& section,
                         const std::string& key)
{
    std::istringstream in(content);
    std::string line;
    std::string currentSection;
    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t.empty())
            continue;
        if(t.front() == '[' && t.back() == ']')
        {
            currentSection = t.substr(1, t.size() - 2);
            continue;
        }
        if(currentSection != section)
            continue;
        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        if(trim(t.substr(0, eq)) != key)
            continue;
        return unquote(trim(t.substr(eq + 1)));
    }
    return std::nullopt;
}

static std::vector<std::string> split_toml_array(std::string_view input)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool inDoubleQuotes = false;
    bool inSingleQuotes = false;
    bool escaped = false;
    for(char c : input)
    {
        if(inDoubleQuotes)
        {
            cur.push_back(c);
            if(escaped)
            {
                escaped = false;
                continue;
            }
            if(c == '\\')
            {
                escaped = true;
                continue;
            }
            if(c == '"')
                inDoubleQuotes = false;
            continue;
        }
        if(inSingleQuotes)
        {
            cur.push_back(c);
            if(c == '\'')
                inSingleQuotes = false;
            continue;
        }
        if(c == '"')
        {
            inDoubleQuotes = true;
            cur.push_back(c);
            continue;
        }
        if(c == '\'')
        {
            inSingleQuotes = true;
            cur.push_back(c);
            continue;
        }
        if(c == '[')
        {
            ++depth;
            cur.push_back(c);
            continue;
        }
        if(c == ']')
        {
            if(depth > 0)
                --depth;
            cur.push_back(c);
            continue;
        }
        if(c == ',' && depth == 0 && !inDoubleQuotes && !inSingleQuotes)
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

static bool toml_array_is_complete(std::string_view input)
{
    int depth = 0;
    bool inDoubleQuotes = false;
    bool inSingleQuotes = false;
    bool escaped = false;
    for(char c : input)
    {
        if(inDoubleQuotes)
        {
            if(escaped)
            {
                escaped = false;
                continue;
            }
            if(c == '\\')
            {
                escaped = true;
                continue;
            }
            if(c == '"')
                inDoubleQuotes = false;
            continue;
        }
        if(inSingleQuotes)
        {
            if(c == '\'')
                inSingleQuotes = false;
            continue;
        }

        if(c == '"')
        {
            inDoubleQuotes = true;
            continue;
        }
        if(c == '\'')
        {
            inSingleQuotes = true;
            continue;
        }
        if(c == '[')
        {
            ++depth;
            continue;
        }
        if(c == ']')
        {
            if(depth > 0)
                --depth;
            continue;
        }
    }
    return depth == 0;
}

static bool toml_basic_string_is_complete(std::string_view input)
{
    std::string t = trim(input);
    if(t.empty() || t.front() != '"')
        return true;

    bool escaped = false;
    for(size_t i = 1; i < t.size(); ++i)
    {
        char c = t[i];
        if(escaped)
        {
            escaped = false;
            continue;
        }
        if(c == '\\')
        {
            escaped = true;
            continue;
        }
        if(c == '"')
            return i == t.size() - 1;
    }
    return false;
}

static bool toml_literal_string_is_complete(std::string_view input)
{
    std::string t = trim(input);
    if(t.empty() || t.front() != '\'')
        return true;
    return t.size() >= 2 && t.back() == '\'';
}

static bool toml_value_is_complete(std::string_view input)
{
    std::string t = trim(input);
    if(t.empty())
        return true;
    if(t.front() == '[')
        return toml_array_is_complete(t);
    if(t.front() == '"')
        return toml_basic_string_is_complete(t);
    if(t.front() == '\'')
        return toml_literal_string_is_complete(t);
    return true;
}

static std::string collect_multiline_toml_value(std::string value,
                                                std::istream& in)
{
    if(toml_value_is_complete(value))
    {
        return value;
    }

    std::string extra;
    while(std::getline(in, extra))
    {
        value += "\n";
        value += strip_toml_comment(extra);
        if(toml_array_is_complete(value))
            break;
    }
    return value;
}

struct ParsedTomlAssignment
{
    std::string key;
    std::string value;
    bool append = false;
};

static std::optional<ParsedTomlAssignment>
parse_toml_assignment(const std::string& line, std::istream& in)
{
    bool inDoubleQuotes = false;
    bool inSingleQuotes = false;
    bool escaped = false;
    for(size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if(inDoubleQuotes)
        {
            if(escaped)
            {
                escaped = false;
                continue;
            }
            if(c == '\\')
            {
                escaped = true;
                continue;
            }
            if(c == '"')
                inDoubleQuotes = false;
            continue;
        }
        if(inSingleQuotes)
        {
            if(c == '\'')
                inSingleQuotes = false;
            continue;
        }
        if(c == '"')
        {
            inDoubleQuotes = true;
            continue;
        }
        if(c == '\'')
        {
            inSingleQuotes = true;
            continue;
        }
        if(c != '=')
            continue;

        ParsedTomlAssignment assignment;
        assignment.append = i > 0 && line[i - 1] == '+';
        const size_t keyEnd = assignment.append ? i - 1 : i;
        assignment.key = trim(line.substr(0, keyEnd));
        assignment.value =
            collect_multiline_toml_value(trim(line.substr(i + 1)), in);
        if(assignment.key.empty())
            return std::nullopt;
        return assignment;
    }
    return std::nullopt;
}

static std::string normalize_target_arch_name(const std::string& arch)
{
    const auto archKey = find_enum_key(trim(arch), kTargetArchAliases);
    if(!archKey.has_value())
        return "";

    TargetArchValue normalizedKey;
    switch(*archKey)
    {
    case TargetArchAlias::X86:
    case TargetArchAlias::I386:
    case TargetArchAlias::I686:
        normalizedKey = TargetArchValue::X86;
        break;
    case TargetArchAlias::X64:
    case TargetArchAlias::X86_64:
    case TargetArchAlias::Amd64:
    case TargetArchAlias::X86Dash64:
        normalizedKey = TargetArchValue::X64;
        break;
    case TargetArchAlias::Aarch64:
    case TargetArchAlias::Arm64:
        normalizedKey = TargetArchValue::Aarch64;
        break;
    }

    if(const auto text = find_enum_text(normalizedKey, kTargetArchNames);
       text.has_value())
        return std::string(*text);
    return "";
}

static std::string normalize_opt_level(std::string opt)
{
    opt = trim(opt);
    if(opt.empty())
        return "";
    if(opt[0] != '-')
        opt = "-" + opt;
    if(const auto optKey = find_enum_key(opt, kOptLevelAliases);
       optKey.has_value())
    {
        if(const auto text = find_enum_text(*optKey, kOptLevelAliases);
           text.has_value())
            return std::string(*text);
    }
    return "";
}

static std::string normalize_task_language(std::string language)
{
    language = trim(language);
    if(language.empty())
        return "";
    std::transform(language.begin(), language.end(), language.begin(),
                   [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    const auto languageKey = find_enum_key(language, kTaskLanguageAliases);
    if(!languageKey.has_value())
        return "";

    TaskLanguage normalizedKey;
    switch(*languageKey)
    {
    case TaskLanguageAlias::Mlang:
        normalizedKey = TaskLanguage::Mlang;
        break;
    case TaskLanguageAlias::C:
    case TaskLanguageAlias::CC:
        normalizedKey = TaskLanguage::C;
        break;
    case TaskLanguageAlias::Cpp:
    case TaskLanguageAlias::Cxx:
    case TaskLanguageAlias::CPlusPlus:
        normalizedKey = TaskLanguage::Cpp;
        break;
    }

    if(const auto text = find_enum_text(normalizedKey, kTaskLanguageNames);
       text.has_value())
        return std::string(*text);
    return "";
}

struct BuildConfig
{
    std::string optLevel;
    std::string targetArch;
    std::string minMlangVersion;
    std::string compilerProgram;
    std::string makeProgram;
    std::string buildDir;
    std::string depsDir;
    std::string logDir;
    std::string stdoutLog;
    std::string stderrLog;
    std::string warnLog;
    std::vector<std::string> pathEntries;
    std::vector<std::string> compilerFlags;
    std::vector<std::string> linkerFlags;
    std::vector<std::string> libPaths;
    std::vector<std::string> libs;
    std::map<std::string, std::string> optionValues;
    std::optional<bool> useNinja;
    std::optional<bool> asan;
    std::optional<bool> staticDeps;
    std::optional<bool> staticCppRuntime;
    std::optional<bool> taskPrintToStdoutLog;
    bool enableLogs = false;
    // Non-empty only for a manifest selected by a root [[include]]. These
    // absolute paths keep the included package's artifacts isolated while
    // source and task paths continue to resolve from its own packageDir.
    std::string includedBuildDir;
    std::string includedDepsDir;
    // 0 = off (no codesign), 1 = sign (codesign --sign -),
    // 2 = force-sign (codesign --force --sign -). Macro-stable so it can be
    // copied through value semantics and merged with apply_cli_overrides.
    int signMode = 0;
};

struct ToolchainRequirement
{
    std::string name;
    std::string command;
    std::string minVersion;
    std::string versionArgs = "--version";
    std::string host;
    std::string install;
};

struct BuildTarget
{
    enum Kind
    {
        executable,
        dynamic_library,
        static_library,
    };

    std::string name;
    std::string entry;
    Kind kind = executable;
    std::string invalidLibraryType;
    bool hasInvalidLibraryType = false;
    std::vector<std::string> dependsOn;
    BuildConfig config;
};

struct TaskSpec
{
    std::string name;
    std::string message;
    std::string print;
    std::vector<std::string> supportedHosts;
    std::string unsupportedMessage;
    std::string phase;
    std::string workdir;
    std::string language;
    std::string source;
    std::string output;
    std::vector<std::string> inputs;
    std::optional<bool> compileOnly;
    std::optional<bool> parallel;
    std::optional<bool> logOutput;
    std::optional<bool> inlineOutput;
    std::string chmodMode;
    std::vector<std::string> chmodPaths;
    std::vector<std::string> dependsOn;
    std::vector<std::string> phaseDependsOn;
    std::vector<std::string> joinOn;
    std::vector<std::string> phaseJoinOn;
    std::vector<std::string> nextTasks;
    std::vector<std::string> nextPhases;
    std::vector<std::string> env;
    std::vector<std::string> shellLines;
    std::vector<std::string> commands;
    std::vector<std::string> signOutputs;
    BuildConfig buildConfig;
    struct HostOverride
    {
        std::string message;
        std::string print;
        std::vector<std::string> supportedHosts;
        std::string unsupportedMessage;
        std::string phase;
        std::string workdir;
        std::string language;
        std::string source;
        std::string output;
        std::vector<std::string> inputs;
        std::optional<bool> compileOnly;
        std::optional<bool> parallel;
        std::optional<bool> logOutput;
        std::optional<bool> inlineOutput;
        std::string chmodMode;
        std::vector<std::string> chmodPaths;
        std::vector<std::string> dependsOn;
        std::vector<std::string> phaseDependsOn;
        std::vector<std::string> joinOn;
        std::vector<std::string> phaseJoinOn;
        std::vector<std::string> nextTasks;
        std::vector<std::string> nextPhases;
        std::vector<std::string> env;
        std::vector<std::string> shellLines;
        std::vector<std::string> commands;
        std::vector<std::string> signOutputs;
        BuildConfig buildConfig;
    };
    std::map<std::string, HostOverride> hostOverrides;
};

static void parse_build_config_key_value(BuildConfig& cfg,
                                         const std::string& key,
                                         const std::string& value);
static void pkg_warn_line(const std::string& text);
static std::string expand_task_text(const std::string& text,
                                    const PackageManifest& pkg,
                                    const BuildConfig& buildConfig);
static BuildConfig
materialize_build_config_for_package(const PackageManifest& pkg,
                                     BuildConfig buildConfig);

static bool apply_task_build_config_key_value(BuildConfig& cfg,
                                              TaskTomlKey taskKeyKind,
                                              const std::string& value)
{
    switch(taskKeyKind)
    {
    case TaskTomlKey::OptLevel:
        cfg.optLevel = normalize_opt_level(unquote(value));
        return true;
    case TaskTomlKey::TargetArch:
        cfg.targetArch = normalize_target_arch_name(unquote(value));
        return true;
    case TaskTomlKey::PathEntries:
        append_toml_string_list_value(value, cfg.pathEntries);
        return true;
    case TaskTomlKey::CompilerFlags:
        append_toml_string_list_value(value, cfg.compilerFlags);
        return true;
    case TaskTomlKey::LinkerFlags:
        append_toml_string_list_value(value, cfg.linkerFlags);
        return true;
    case TaskTomlKey::LibPaths:
        append_toml_string_list_value(value, cfg.libPaths);
        return true;
    case TaskTomlKey::Libs:
        append_toml_string_list_value(value, cfg.libs);
        return true;
    case TaskTomlKey::StaticDeps:
        cfg.staticDeps = parse_toml_bool_value(value);
        return true;
    case TaskTomlKey::StaticCppRuntime:
        cfg.staticCppRuntime = parse_toml_bool_value(value);
        return true;
    default:
        return false;
    }
}

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

static bool parse_toml_bool_value(const std::string& value)
{
    std::string t = trim(value);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return t == "true" || t == "1" || t == "\"true\"";
}

static std::optional<unsigned int>
parse_octal_permission_bits(const std::string& value)
{
    const std::string text = trim(unquote(value));
    if(text.empty())
        return std::nullopt;

    unsigned int mode = 0;
    for(char c : text)
    {
        if(c < '0' || c > '7')
            return std::nullopt;
        mode = (mode * 8u) + static_cast<unsigned int>(c - '0');
    }
    return mode;
}

static bool is_opt_flag(std::string_view flag)
{
    return flag == "-O0" || flag == "-Og" || flag == "-O1" || flag == "-O2" ||
           flag == "-O3" || flag == "-Os" || flag == "-Oz";
}

static bool is_release_opt_flag(std::string_view flag)
{
    return flag == "-O1" || flag == "-O2" || flag == "-O3" || flag == "-Os" ||
           flag == "-Oz";
}

static bool build_config_asan_enabled(const BuildConfig& buildConfig)
{
    return buildConfig.asan.value_or(false);
}

static std::string build_config_cmake_build_type(const BuildConfig& buildConfig)
{
    if(build_config_asan_enabled(buildConfig))
        return "Debug";
    const std::string optLevel = trim(buildConfig.optLevel);
    if(optLevel == "-O0" || optLevel == "-Og")
        return "Debug";
    return "Release";
}

static std::string
build_config_asan_compile_flags(const BuildConfig& buildConfig)
{
    if(!build_config_asan_enabled(buildConfig))
        return "";
    return "-fsanitize=address -fno-omit-frame-pointer -g -O0";
}

static std::string build_config_asan_link_flags(const BuildConfig& buildConfig)
{
    if(!build_config_asan_enabled(buildConfig))
        return "";
    return "-fsanitize=address -fno-omit-frame-pointer";
}

static std::optional<std::string>
find_opt_flag_in_fragments(const std::vector<std::string>& flags)
{
    for(const auto& flag : flags)
    {
        const std::string trimmed = trim(flag);
        if(is_opt_flag(trimmed))
            return trimmed;
    }
    return std::nullopt;
}

static void remove_opt_flags(std::vector<std::string>& flags)
{
    flags.erase(std::remove_if(flags.begin(), flags.end(),
                               [](const std::string& flag)
                               { return is_opt_flag(trim(flag)); }),
                flags.end());
}

static void append_unique_flag(std::vector<std::string>& flags,
                               const std::string& flag)
{
    if(std::find(flags.begin(), flags.end(), flag) == flags.end())
        flags.push_back(flag);
}

static void apply_asan_overrides(
    BuildConfig& buildConfig, const std::string& contextLabel,
    const std::optional<std::string>& cliOptFlag = std::nullopt)
{
    if(!buildConfig.asan.value_or(false))
        return;

    const std::optional<std::string> existingCompilerOpt =
        find_opt_flag_in_fragments(buildConfig.compilerFlags);
    const std::string explicitOpt =
        cliOptFlag.has_value() && !cliOptFlag->empty() ? *cliOptFlag
                                                       : buildConfig.optLevel;
    std::string warnedOpt;
    if(is_release_opt_flag(explicitOpt))
        warnedOpt = explicitOpt;
    else if(existingCompilerOpt.has_value() &&
            is_release_opt_flag(*existingCompilerOpt))
        warnedOpt = *existingCompilerOpt;

    if(!warnedOpt.empty())
    {
        pkg_warn_line("warning: " + contextLabel +
                      " requested release optimization " + warnedOpt +
                      ", but --asan forces a debug-friendly build (-O0).");
    }

    buildConfig.optLevel = "-O0";
    remove_opt_flags(buildConfig.compilerFlags);
    append_unique_flag(buildConfig.compilerFlags, "-fsanitize=address");
    append_unique_flag(buildConfig.compilerFlags, "-fno-omit-frame-pointer");
    append_unique_flag(buildConfig.compilerFlags, "-g");
    append_unique_flag(buildConfig.linkerFlags, "-fsanitize=address");
    append_unique_flag(buildConfig.linkerFlags, "-fno-omit-frame-pointer");
}

static std::filesystem::perms directory_perms_from_file_mode(unsigned int mode)
{
    std::filesystem::perms perms = std::filesystem::perms::none;
    auto add_if = [&](bool enabled, std::filesystem::perms bit)
    {
        if(enabled)
            perms |= bit;
    };

    const bool ownerRead = (mode & 0400u) != 0u;
    const bool ownerWrite = (mode & 0200u) != 0u;
    const bool ownerExec = (mode & 0100u) != 0u;
    const bool groupRead = (mode & 0040u) != 0u;
    const bool groupWrite = (mode & 0020u) != 0u;
    const bool groupExec = (mode & 0010u) != 0u;
    const bool othersRead = (mode & 0004u) != 0u;
    const bool othersWrite = (mode & 0002u) != 0u;
    const bool othersExec = (mode & 0001u) != 0u;

    add_if(ownerRead, std::filesystem::perms::owner_read);
    add_if(ownerWrite, std::filesystem::perms::owner_write);
    add_if(ownerExec || ownerRead, std::filesystem::perms::owner_exec);
    add_if(groupRead, std::filesystem::perms::group_read);
    add_if(groupWrite, std::filesystem::perms::group_write);
    add_if(groupExec || groupRead, std::filesystem::perms::group_exec);
    add_if(othersRead, std::filesystem::perms::others_read);
    add_if(othersWrite, std::filesystem::perms::others_write);
    add_if(othersExec || othersRead, std::filesystem::perms::others_exec);
    return perms;
}

static std::filesystem::perms file_perms_from_mode(unsigned int mode)
{
    std::filesystem::perms perms = std::filesystem::perms::none;
    auto add_if = [&](bool enabled, std::filesystem::perms bit)
    {
        if(enabled)
            perms |= bit;
    };
    add_if((mode & 0400u) != 0u, std::filesystem::perms::owner_read);
    add_if((mode & 0200u) != 0u, std::filesystem::perms::owner_write);
    add_if((mode & 0100u) != 0u, std::filesystem::perms::owner_exec);
    add_if((mode & 0040u) != 0u, std::filesystem::perms::group_read);
    add_if((mode & 0020u) != 0u, std::filesystem::perms::group_write);
    add_if((mode & 0010u) != 0u, std::filesystem::perms::group_exec);
    add_if((mode & 0004u) != 0u, std::filesystem::perms::others_read);
    add_if((mode & 0002u) != 0u, std::filesystem::perms::others_write);
    add_if((mode & 0001u) != 0u, std::filesystem::perms::others_exec);
    return perms;
}

static bool apply_recursive_permissions(const std::filesystem::path& path,
                                        unsigned int mode, std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if(!fs::exists(path, ec))
    {
        error = "chmod target does not exist: " + path.string();
        return false;
    }

    const fs::perms filePerms = file_perms_from_mode(mode);
    const fs::perms dirPerms = directory_perms_from_file_mode(mode);
    auto apply_one = [&](const fs::path& current) -> bool
    {
        std::error_code statusEc;
        const fs::file_status status = fs::symlink_status(current, statusEc);
        if(statusEc)
        {
            error = "Failed to read status for chmod target " +
                    current.string() + ": " + statusEc.message();
            return false;
        }
        if(fs::is_symlink(status))
            return true;
        const fs::perms perms = fs::is_directory(status) ? dirPerms : filePerms;
        std::error_code permsEc;
        fs::permissions(current, perms, fs::perm_options::replace, permsEc);
        if(permsEc)
        {
            error = "Failed to chmod " + current.string() + ": " +
                    permsEc.message();
            return false;
        }
        return true;
    };

    if(!apply_one(path))
        return false;
    if(!fs::is_directory(path, ec))
        return true;

    for(fs::recursive_directory_iterator it(path, ec), end; it != end;
        it.increment(ec))
    {
        if(ec)
        {
            error = "Failed to walk chmod target " + path.string() + ": " +
                    ec.message();
            return false;
        }
        if(!apply_one(it->path()))
            return false;
    }
    return true;
}

static void append_unique_strings(std::vector<std::string>& dst,
                                  const std::vector<std::string>& src)
{
    for(const auto& value : src)
    {
        if(value.empty())
            continue;
        if(std::find(dst.begin(), dst.end(), value) == dst.end())
            dst.push_back(value);
    }
}

static std::vector<std::string>
task_names_for_phases(const std::vector<TaskSpec>& tasks,
                      const std::vector<std::string>& phases,
                      const std::string& hostName)
{
    std::vector<std::string> names;
    for(const auto& task : tasks)
    {
        std::string effectivePhase = task.phase;
        auto hostIt = task.hostOverrides.find(hostName);
        if(hostIt != task.hostOverrides.end() && !hostIt->second.phase.empty())
        {
            effectivePhase = hostIt->second.phase;
        }
        if(effectivePhase.empty())
            continue;
        if(std::find(phases.begin(), phases.end(), effectivePhase) ==
           phases.end())
        {
            continue;
        }
        if(std::find(names.begin(), names.end(), task.name) == names.end())
            names.push_back(task.name);
    }
    return names;
}

static std::vector<std::string>
task_roots_for_phase(const std::vector<TaskSpec>& tasks,
                     const std::string& hostName, const std::string& phase,
                     const std::vector<std::string>& fallbackNames = {})
{
    std::vector<std::string> roots =
        task_names_for_phases(tasks, std::vector<std::string>{phase}, hostName);
    if(roots.empty())
    {
        for(const auto& fallback : fallbackNames)
        {
            if(task_list_contains_name(tasks, fallback))
            {
                roots.push_back(fallback);
                break;
            }
        }
    }
    return roots;
}

static void print_pkg_usage(const std::string& programName)
{
    const std::string tool = programName.empty() ? "mlang" : programName;
    std::cerr
        << "Usage: " << tool
        << " pkg [--config FILE] <init|add|lock|verify|tree|why|fetch|build|run|clean> "
           "[options...]\n"
        << "       " << tool
        << " pkg --tests [--tasks] [--color] <manifest.toml>...\n"
        << "       " << tool << " pkg [--tasks] [--color] <manifest.toml>...\n"
        << "\nNote: --config, --tasks, --color and per-subcommand flags may\n"
        << "appear in any order. `--color` alone implies task-tree output.\n"
        << "\nCommands:\n"
        << "  " << tool << " pkg init\n"
        << "      Scaffold mlang.toml and src/main.mla in the current dir.\n"
        << "  " << tool
        << " pkg add <name> [--git URL] [--rev REV] [--tag TAG] "
           "[--submodules] [--version REQ]\n"
        << "  " << tool
        << " pkg add <name> --path DIR [--version REQ]\n"
        << "  " << tool
        << " pkg add <name> --url URL [--archive tar.gz] [--strip-components "
           "N] [--subdir DIR]\n"
        << "  " << tool << " pkg add <name> [--pkg-config NAME] [--system]\n"
        << "  " << tool
        << " pkg add <name> [--git URL|--url URL] --add-lib [--project-dir "
           "DIR]\n"
        << "      Append a dependency entry to the manifest.\n"
        << "  " << tool << " pkg lock [--offline]\n"
        << "      Resolve exact Git revisions and archive SHA-256 checksums.\n"
        << "  " << tool << " pkg verify\n"
        << "      Verify mlang.lock and fetched dependency sources.\n"
        << "  " << tool << " pkg tree\n"
        << "      Print the direct and transitive dependency graph.\n"
        << "  " << tool << " pkg why <package>\n"
        << "      Show every dependency path that reaches a package.\n"
        << "  " << tool
        << " pkg fetch [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR]\n"
        << "           [--stdout-log FILE] [--stderr-log FILE] [--warn-log "
           "FILE]\n"
        << "           [--task-print-to-stdout-log] [--locked] [--offline]\n"
        << "      Fetch dependencies without building.\n"
        << "  " << tool
        << " pkg build [-O0|-Og|-O1|-O2|-O3|-Os|-Oz] [--ninja] [--asan]\n"
        << "           [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR]\n"
        << "           [--stdout-log FILE] [--stderr-log FILE] [--warn-log "
           "FILE]\n"
        << "           [--task-print-to-stdout-log] [--locked] [--offline]\n"
        << "      Build all configured targets.\n"
        << "  " << tool << " pkg run <task> [--tasks] [--color] [--asan]\n"
        << "           [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR]\n"
        << "           [--stdout-log FILE] [--stderr-log FILE] [--warn-log "
           "FILE]\n"
        << "           [--task-print-to-stdout-log] [--option KEY=VALUE]\n"
        << "           [--locked] [--offline]\n"
        << "      Run a named task (fetches dependencies first if needed).\n"
        << "      With --tasks, print the dependency/execution tree instead.\n"
        << "  " << tool
        << " pkg --tests [--tasks] [--color] <manifest.toml>...\n"
        << "      Compile and execute phase=\"test\" tasks in the given\n"
        << "      manifests. With --tasks, print the test-task tree instead.\n"
        << "  " << tool << " pkg [--tasks] [--color] <manifest.toml>...\n"
        << "      Show runnable task entrypoints for one or more manifests\n"
        << "      (no commands are executed).\n"
        << "  " << tool
        << " pkg clean [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR]\n"
        << "           [--stdout-log FILE] [--stderr-log FILE] [--warn-log "
           "FILE]\n"
        << "           [--task-print-to-stdout-log] [--deps]\n"
        << "      Remove build artifacts (add --deps to also remove fetched "
           "deps).\n"
        << "\nWorkflows:\n"
        << "  Separate:   " << tool << " pkg fetch ; " << tool
        << " pkg build ; " << tool << " pkg run <task>\n"
        << "  One-shot:   " << tool
        << " pkg run <task>   # fetches if needed, then runs the task chain\n"
        << "\nExamples:\n"
        << "  cd examples/package_manager_vst3_coreaudio_synth && \\\n"
        << "      ../../build/mlang pkg run preview-square --tasks --color\n"
        << "  " << tool
        << " pkg examples/package_manager_vst3_coreaudio_synth/mlang.toml "
           "--tasks --color\n"
        << "  " << tool
        << " pkg --color tests/mla_tests.toml            # implies --tasks\n"
        << "  " << tool
        << " pkg fetch --config examples/foo/mlang.toml  # --config anywhere\n"
        << "  " << tool
        << " pkg --config bootstrap/mlang.toml run build-and-install --option "
           "install_prefix=$HOME/.local --option bin_dir=$HOME/.local/bin\n";
}

static BuildConfig merge_build_config(const BuildConfig& base,
                                      const BuildConfig& overrideCfg)
{
    BuildConfig out = base;
    if(!overrideCfg.compilerProgram.empty())
        out.compilerProgram = overrideCfg.compilerProgram;
    if(!overrideCfg.optLevel.empty())
        out.optLevel = overrideCfg.optLevel;
    if(!overrideCfg.targetArch.empty())
        out.targetArch = overrideCfg.targetArch;
    if(!overrideCfg.minMlangVersion.empty())
        out.minMlangVersion = overrideCfg.minMlangVersion;
    if(!overrideCfg.makeProgram.empty())
        out.makeProgram = overrideCfg.makeProgram;
    if(!overrideCfg.buildDir.empty())
        out.buildDir = overrideCfg.buildDir;
    if(!overrideCfg.depsDir.empty())
        out.depsDir = overrideCfg.depsDir;
    out.pathEntries.insert(out.pathEntries.end(),
                           overrideCfg.pathEntries.begin(),
                           overrideCfg.pathEntries.end());
    out.compilerFlags.insert(out.compilerFlags.end(),
                             overrideCfg.compilerFlags.begin(),
                             overrideCfg.compilerFlags.end());
    out.linkerFlags.insert(out.linkerFlags.end(),
                           overrideCfg.linkerFlags.begin(),
                           overrideCfg.linkerFlags.end());
    out.libPaths.insert(out.libPaths.end(), overrideCfg.libPaths.begin(),
                        overrideCfg.libPaths.end());
    out.libs.insert(out.libs.end(), overrideCfg.libs.begin(),
                    overrideCfg.libs.end());
    for(const auto& [key, value] : overrideCfg.optionValues)
        out.optionValues[key] = value;
    if(overrideCfg.useNinja.has_value())
        out.useNinja = overrideCfg.useNinja;
    if(overrideCfg.asan.has_value())
        out.asan = overrideCfg.asan;
    if(overrideCfg.staticDeps.has_value())
        out.staticDeps = overrideCfg.staticDeps;
    if(overrideCfg.staticCppRuntime.has_value())
        out.staticCppRuntime = overrideCfg.staticCppRuntime;
    return out;
}

static std::optional<std::vector<int>>
parse_semver_components(const std::string& versionText)
{
    std::string text = trim(versionText);
    if(text.empty())
        return std::nullopt;

    std::vector<int> parts;
    size_t start = 0;
    while(start < text.size())
    {
        size_t end = text.find('.', start);
        std::string part = end == std::string::npos
                               ? text.substr(start)
                               : text.substr(start, end - start);
        if(part.empty())
            return std::nullopt;

        size_t digits = 0;
        while(digits < part.size() &&
              std::isdigit(static_cast<unsigned char>(part[digits])))
        {
            ++digits;
        }
        if(digits == 0)
            return std::nullopt;
        try
        {
            parts.push_back(std::stoi(part.substr(0, digits)));
        }
        catch(...)
        {
            return std::nullopt;
        }

        if(digits != part.size())
        {
            if(part[digits] != '-')
                return std::nullopt;
            break;
        }
        if(end == std::string::npos)
            break;
        start = end + 1;
    }
    return parts;
}

static int compare_semver(const std::string& lhs, const std::string& rhs)
{
    auto lhsParts = parse_semver_components(lhs);
    auto rhsParts = parse_semver_components(rhs);
    if(!lhsParts.has_value() || !rhsParts.has_value())
        return 0;

    const size_t n = std::max(lhsParts->size(), rhsParts->size());
    for(size_t i = 0; i < n; ++i)
    {
        const int a = i < lhsParts->size() ? (*lhsParts)[i] : 0;
        const int b = i < rhsParts->size() ? (*rhsParts)[i] : 0;
        if(a < b)
            return -1;
        if(a > b)
            return 1;
    }
    return 0;
}

struct SemanticVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::vector<std::string> prerelease;
};

static std::optional<SemanticVersion> parse_semantic_version(std::string text)
{
    text = trim(text);
    if(!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.erase(text.begin());
    const size_t plus = text.find('+');
    if(plus != std::string::npos)
        text.resize(plus);
    std::string prerelease;
    const size_t dash = text.find('-');
    if(dash != std::string::npos)
    {
        prerelease = text.substr(dash + 1);
        text.resize(dash);
        if(prerelease.empty())
            return std::nullopt;
    }
    std::vector<int> numbers;
    size_t start = 0;
    while(start <= text.size())
    {
        const size_t end = text.find('.', start);
        const std::string part = text.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if(part.empty() ||
           !std::all_of(part.begin(), part.end(), [](unsigned char c)
                        { return std::isdigit(c); }))
            return std::nullopt;
        if(part.size() > 1 && part.front() == '0')
            return std::nullopt;
        try
        {
            numbers.push_back(std::stoi(part));
        }
        catch(...)
        {
            return std::nullopt;
        }
        if(end == std::string::npos)
            break;
        start = end + 1;
    }
    if(numbers.empty() || numbers.size() > 3)
        return std::nullopt;
    SemanticVersion result;
    result.major = numbers[0];
    if(numbers.size() > 1)
        result.minor = numbers[1];
    if(numbers.size() > 2)
        result.patch = numbers[2];
    if(!prerelease.empty())
    {
        start = 0;
        while(start <= prerelease.size())
        {
            const size_t end = prerelease.find('.', start);
            const std::string part = prerelease.substr(
                start,
                end == std::string::npos ? std::string::npos : end - start);
            if(part.empty())
                return std::nullopt;
            if(part.size() > 1 && part.front() == '0' &&
               std::all_of(part.begin(), part.end(), [](unsigned char c)
                           { return std::isdigit(c); }))
                return std::nullopt;
            result.prerelease.push_back(part);
            if(end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return result;
}

static int compare_semantic_versions(const SemanticVersion& lhs,
                                     const SemanticVersion& rhs)
{
    if(lhs.major != rhs.major)
        return lhs.major < rhs.major ? -1 : 1;
    if(lhs.minor != rhs.minor)
        return lhs.minor < rhs.minor ? -1 : 1;
    if(lhs.patch != rhs.patch)
        return lhs.patch < rhs.patch ? -1 : 1;
    if(lhs.prerelease.empty() != rhs.prerelease.empty())
        return lhs.prerelease.empty() ? 1 : -1;
    for(size_t i = 0; i < std::min(lhs.prerelease.size(),
                                  rhs.prerelease.size());
        ++i)
    {
        if(lhs.prerelease[i] == rhs.prerelease[i])
            continue;
        const bool lhsNumeric =
            std::all_of(lhs.prerelease[i].begin(), lhs.prerelease[i].end(),
                        [](unsigned char c) { return std::isdigit(c); });
        const bool rhsNumeric =
            std::all_of(rhs.prerelease[i].begin(), rhs.prerelease[i].end(),
                        [](unsigned char c) { return std::isdigit(c); });
        if(lhsNumeric && rhsNumeric)
        {
            const std::string& a = lhs.prerelease[i];
            const std::string& b = rhs.prerelease[i];
            const size_t aFirst = a.find_first_not_of('0');
            const size_t bFirst = b.find_first_not_of('0');
            const std::string aNormalized =
                aFirst == std::string::npos ? "0" : a.substr(aFirst);
            const std::string bNormalized =
                bFirst == std::string::npos ? "0" : b.substr(bFirst);
            if(aNormalized.size() != bNormalized.size())
                return aNormalized.size() < bNormalized.size() ? -1 : 1;
            return aNormalized < bNormalized ? -1 : 1;
        }
        if(lhsNumeric != rhsNumeric)
            return lhsNumeric ? -1 : 1;
        return lhs.prerelease[i] < rhs.prerelease[i] ? -1 : 1;
    }
    if(lhs.prerelease.size() == rhs.prerelease.size())
        return 0;
    return lhs.prerelease.size() < rhs.prerelease.size() ? -1 : 1;
}

static bool semantic_version_satisfies_one(const SemanticVersion& version,
                                           std::string requirement)
{
    requirement = trim(requirement);
    if(requirement.empty() || requirement == "*" || requirement == "x" ||
       requirement == "X")
        return version.prerelease.empty();
    const bool requirementAllowsPrerelease = requirement.find('-') !=
                                             std::string::npos;
    if(!version.prerelease.empty() && !requirementAllowsPrerelease)
        return false;
    std::string op;
    for(const char* candidate : {">=", "<=", ">", "<", "=", "^", "~"})
    {
        if(requirement.rfind(candidate, 0) == 0)
        {
            op = candidate;
            requirement = trim(requirement.substr(op.size()));
            break;
        }
    }
    const size_t wildcard = requirement.find_first_of("xX*");
    if(wildcard != std::string::npos)
    {
        const std::string prefix = requirement.substr(0, wildcard);
        std::ostringstream current;
        current << version.major << "." << version.minor << "."
                << version.patch;
        return current.str().rfind(prefix, 0) == 0;
    }
    const std::string normalizedRequirement = requirement;
    const auto wanted = parse_semantic_version(requirement);
    if(!wanted.has_value())
        return false;
    const int comparison = compare_semantic_versions(version, *wanted);
    if(op == ">=")
        return comparison >= 0;
    if(op == "<=")
        return comparison <= 0;
    if(op == ">")
        return comparison > 0;
    if(op == "<")
        return comparison < 0;
    if(op == "^")
    {
        SemanticVersion upper = *wanted;
        if(upper.major > 0)
        {
            ++upper.major;
            upper.minor = upper.patch = 0;
        }
        else if(upper.minor > 0)
        {
            ++upper.minor;
            upper.patch = 0;
        }
        else
        {
            ++upper.patch;
        }
        upper.prerelease.clear();
        return comparison >= 0 && compare_semantic_versions(version, upper) < 0;
    }
    if(op == "~")
    {
        SemanticVersion upper = *wanted;
        ++upper.minor;
        upper.patch = 0;
        upper.prerelease.clear();
        return comparison >= 0 && compare_semantic_versions(version, upper) < 0;
    }
    if(op.empty() && normalizedRequirement.find('-') == std::string::npos)
    {
        const size_t dots = static_cast<size_t>(std::count(
            normalizedRequirement.begin(), normalizedRequirement.end(), '.'));
        if(dots == 0)
            return version.major == wanted->major;
        if(dots == 1)
            return version.major == wanted->major &&
                   version.minor == wanted->minor;
    }
    return comparison == 0;
}

static bool semantic_version_satisfies(const std::string& versionText,
                                       const std::string& requirementText)
{
    const auto version = parse_semantic_version(versionText);
    if(!version.has_value())
        return false;
    size_t alternativeStart = 0;
    while(alternativeStart <= requirementText.size())
    {
        const size_t alternativeEnd =
            requirementText.find("||", alternativeStart);
        std::string alternative = trim(requirementText.substr(
            alternativeStart,
            alternativeEnd == std::string::npos
                ? std::string::npos
                : alternativeEnd - alternativeStart));
        std::replace(alternative.begin(), alternative.end(), ',', ' ');
        std::istringstream terms(alternative);
        std::string term;
        bool matches = true;
        bool sawTerm = false;
        while(terms >> term)
        {
            sawTerm = true;
            if(!semantic_version_satisfies_one(*version, term))
                matches = false;
        }
        if(matches && sawTerm)
            return true;
        if(alternativeEnd == std::string::npos)
            break;
        alternativeStart = alternativeEnd + 2;
    }
    return trim(requirementText).empty();
}

static bool is_complete_semantic_version(const std::string& text)
{
    std::string core = trim(text);
    if(!core.empty() && (core.front() == 'v' || core.front() == 'V'))
        core.erase(core.begin());
    const size_t suffix = core.find_first_of("-+");
    if(suffix != std::string::npos)
        core.resize(suffix);
    return std::count(core.begin(), core.end(), '.') == 2 &&
           parse_semantic_version(text).has_value();
}

static BuildConfig parse_build_config(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::string section;
    BuildConfig cfg;
    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t.empty())
            continue;
        if(t.front() == '[' && t.back() == ']')
        {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        const auto assignment = parse_toml_assignment(t, in);
        if(!assignment.has_value())
            continue;
        if(section == "tool.mlang")
        {
            parse_build_config_key_value(cfg, assignment->key,
                                         assignment->value);
        }
        else if(section == "tool.mlang.options")
        {
            cfg.optionValues[assignment->key] =
                unquote_preserve(assignment->value);
        }
    }
    return cfg;
}

static std::filesystem::path
resolve_package_path(const std::filesystem::path& packageDir,
                     const std::string& configured,
                     const std::filesystem::path& fallback)
{
    std::filesystem::path path =
        configured.empty() ? fallback : std::filesystem::path(configured);
    if(path.is_relative())
        path = packageDir / path;
    return path.lexically_normal();
}

static std::filesystem::path
package_build_dir(const std::filesystem::path& packageDir,
                  const BuildConfig& config)
{
    if(!config.includedBuildDir.empty())
        return std::filesystem::path(config.includedBuildDir)
            .lexically_normal();
    return resolve_package_path(packageDir, config.buildDir, "build");
}

static std::filesystem::path
package_deps_dir(const std::filesystem::path& packageDir,
                 const BuildConfig& config)
{
    if(!config.includedDepsDir.empty())
        return std::filesystem::path(config.includedDepsDir).lexically_normal();
    if(!config.depsDir.empty())
        return resolve_package_path(packageDir, config.depsDir, "build/deps");
    return (package_build_dir(packageDir, config) / "deps").lexically_normal();
}

struct PackageLogState
{
    std::optional<std::filesystem::path> stdoutLog;
    std::optional<std::filesystem::path> stderrLog;
    std::optional<std::filesystem::path> warnLog;
    bool taskPrintToStdoutLog = false;
    bool logsEnabled = false;
};

static PackageLogState& current_package_log_state()
{
    static PackageLogState state;
    return state;
}

static std::optional<std::filesystem::path>
resolve_log_path(const std::filesystem::path& packageDir,
                 const BuildConfig& config, const std::string& configured,
                 const char* defaultName = nullptr)
{
    if(configured.empty() && defaultName == nullptr)
        return std::nullopt;

    std::filesystem::path path = configured.empty()
                                     ? std::filesystem::path(defaultName)
                                     : std::filesystem::path(configured);
    if(path.is_absolute())
        return path.lexically_normal();

    if(!config.logDir.empty())
        return resolve_package_path(packageDir, config.logDir, ".") / path;
    return (packageDir / path).lexically_normal();
}

static PackageLogState
make_package_log_state(const std::filesystem::path& packageDir,
                       const BuildConfig& config)
{
    PackageLogState state;
    if(!config.enableLogs)
        return state;

    const char* defaultStdoutLog =
        config.logDir.empty() ? nullptr : "pkg.stdout.log";
    const char* defaultStderrLog =
        config.logDir.empty() ? nullptr : "pkg.stderr.log";
    const char* defaultWarnLog =
        config.logDir.empty() ? nullptr : "pkg.warn.log";
    state.stdoutLog = resolve_log_path(packageDir, config, config.stdoutLog,
                                       defaultStdoutLog);
    state.stderrLog = resolve_log_path(packageDir, config, config.stderrLog,
                                       defaultStderrLog);
    state.warnLog =
        resolve_log_path(packageDir, config, config.warnLog, defaultWarnLog);
    state.taskPrintToStdoutLog = config.taskPrintToStdoutLog.value_or(false);
    state.logsEnabled = true;
    return state;
}

static bool pkg_logs_active()
{
    return current_package_log_state().logsEnabled;
}

static void append_log_line(const std::optional<std::filesystem::path>& path,
                            const std::string& text)
{
    if(!path.has_value())
        return;
    std::error_code ec;
    if(path->has_parent_path())
        std::filesystem::create_directories(path->parent_path(), ec);
    std::ofstream out(*path, std::ios::app | std::ios::binary);
    if(!out)
        return;
    out << text << "\n";
}

class ScopedPackageLogState
{
public:
    explicit ScopedPackageLogState(PackageLogState next)
        : previous_(current_package_log_state())
    {
        current_package_log_state() = std::move(next);
    }

    ~ScopedPackageLogState()
    {
        current_package_log_state() = previous_;
    }

private:
    PackageLogState previous_;
};

static void pkg_info_line(const std::string& text, bool logToStdout = true)
{
    std::cout << text << std::endl;
    if(logToStdout)
        append_log_line(current_package_log_state().stdoutLog, text);
}

static void pkg_warn_line(const std::string& text)
{
    std::cerr << text << std::endl;
    append_log_line(current_package_log_state().warnLog, text);
}

static void pkg_error_line(const std::string& text)
{
    std::cerr << text << std::endl;
    append_log_line(current_package_log_state().stderrLog, text);
}

static void pkg_task_print_line(const std::string& text)
{
    std::cout << text << std::endl;
    if(current_package_log_state().taskPrintToStdoutLog)
        append_log_line(current_package_log_state().stdoutLog, text);
}

static std::vector<BuildTarget> parse_build_targets(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::vector<BuildTarget> targets;
    std::optional<BuildTarget> current;

    auto flush_current = [&]()
    {
        if(current.has_value())
            targets.push_back(*current);
        current.reset();
    };

    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t.empty())
            continue;
        if(t == "[[bin]]" || t == "[[lib]]")
        {
            flush_current();
            current = BuildTarget{};
            current->kind = t == "[[lib]]" ? BuildTarget::dynamic_library
                                            : BuildTarget::executable;
            continue;
        }
        if(t.front() == '[' && t.back() == ']')
        {
            flush_current();
            continue;
        }
        if(!current.has_value())
            continue;

        const auto assignment = parse_toml_assignment(t, in);
        if(!assignment.has_value())
            continue;
        const std::string& key = assignment->key;
        const std::string& value = assignment->value;
        if(key == "name")
        {
            current->name = unquote(value);
        }
        else if(key == "entry")
        {
            current->entry = unquote(value);
        }
        else if(key == "depends_on")
        {
            append_toml_string_list_value(value, current->dependsOn);
        }
        else if(key == "type" && current->kind != BuildTarget::executable)
        {
            const std::string libraryType = unquote(value);
            if(libraryType == "static")
                current->kind = BuildTarget::static_library;
            else if(libraryType == "dynamic" || libraryType == "shared")
                current->kind = BuildTarget::dynamic_library;
            else
            {
                current->invalidLibraryType = libraryType;
                current->hasInvalidLibraryType = true;
            }
        }
        else
        {
            parse_build_config_key_value(current->config, key, value);
        }
    }
    flush_current();
    return targets;
}

static std::optional<std::vector<BuildTarget>>
order_build_targets(const std::vector<BuildTarget>& targets,
                    const std::filesystem::path& manifestPath)
{
    std::map<std::string, size_t> byName;
    for(size_t i = 0; i < targets.size(); ++i)
    {
        if(targets[i].name.empty())
            continue;
        if(!byName.emplace(targets[i].name, i).second)
        {
            pkg_error_line("Duplicate package target name '" +
                           targets[i].name + "' in " + manifestPath.string());
            return std::nullopt;
        }
    }

    std::vector<int> state(targets.size(), 0);
    std::vector<BuildTarget> ordered;
    std::function<bool(size_t)> visit = [&](size_t index)
    {
        if(state[index] == 2)
            return true;
        if(state[index] == 1)
        {
            pkg_error_line("Package target dependency cycle involving '" +
                           targets[index].name + "' in " +
                           manifestPath.string());
            return false;
        }
        state[index] = 1;
        for(const auto& dependencyName : targets[index].dependsOn)
        {
            const auto dependency = byName.find(dependencyName);
            if(dependency == byName.end())
            {
                pkg_error_line("Unknown package target '" + dependencyName +
                               "' in depends_on for target '" +
                               targets[index].name + "'");
                return false;
            }
            if(targets[dependency->second].kind == BuildTarget::executable)
            {
                pkg_error_line("Package target '" + targets[index].name +
                               "' can only depend_on [[lib]] targets; '" +
                               dependencyName + "' is not a library");
                return false;
            }
            if(!visit(dependency->second))
                return false;
        }
        state[index] = 2;
        ordered.push_back(targets[index]);
        return true;
    };

    for(size_t i = 0; i < targets.size(); ++i)
    {
        if(!visit(i))
            return std::nullopt;
    }
    return ordered;
}

static std::string dynamic_library_filename(const std::string& name)
{
#if defined(__APPLE__)
    return "lib" + name + ".dylib";
#elif defined(_WIN32)
    return name + ".dll";
#else
    return "lib" + name + ".so";
#endif
}

static std::string static_library_filename(const std::string& name)
{
    return "lib" + name + ".a";
}

static void collect_target_link_dependencies(
    const BuildTarget& target,
    const std::map<std::string, const BuildTarget*>& targetsByName,
    std::vector<const BuildTarget*>& dependencies,
    std::unordered_set<std::string>& seen)
{
    for(const auto& dependencyName : target.dependsOn)
    {
        if(!seen.insert(dependencyName).second)
            continue;
        const auto dependency = targetsByName.find(dependencyName);
        if(dependency == targetsByName.end())
            continue;
        dependencies.push_back(dependency->second);
        collect_target_link_dependencies(*dependency->second, targetsByName,
                                         dependencies, seen);
    }
}

static std::string package_target_rpath_flag()
{
#if defined(__APPLE__)
    return "-Wl,-rpath,@loader_path";
#elif defined(_WIN32)
    return "";
#else
    return "-Wl,-rpath,$ORIGIN";
#endif
}

static std::vector<TaskSpec> parse_task_specs(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::vector<TaskSpec> tasks;
    std::optional<TaskSpec> current;
    std::string currentHostSection;

    auto flush_current = [&]()
    {
        if(current.has_value())
            tasks.push_back(*current);
        current.reset();
        currentHostSection.clear();
    };

    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t.empty())
            continue;
        if(t == "[[task]]")
        {
            flush_current();
            current = TaskSpec{};
            continue;
        }
        if(t.front() == '[' && t.back() == ']')
        {
            std::string section = t.substr(1, t.size() - 2);
            if(current.has_value() && section.rfind("task.host.", 0) == 0 &&
               section.size() > std::string("task.host.").size())
            {
                currentHostSection =
                    section.substr(std::string("task.host.").size());
                continue;
            }
            if(current.has_value() && section == "task")
            {
                currentHostSection.clear();
                continue;
            }
            flush_current();
            continue;
        }
        if(!current.has_value())
            continue;

        const auto assignment = parse_toml_assignment(t, in);
        if(!assignment.has_value())
            continue;
        const std::string& key = assignment->key;
        const std::string& value = assignment->value;

        auto apply_common_task_kv = [&](auto& target, TaskTomlKey taskKeyKind,
                                        const std::string& taskValue)
        {
            switch(taskKeyKind)
            {
            case TaskTomlKey::Message:
                target.message = unquote_preserve(taskValue);
                break;
            case TaskTomlKey::Print:
                target.print = unquote_preserve(taskValue);
                break;
            case TaskTomlKey::SupportedHosts:
                append_toml_string_list_value(taskValue, target.supportedHosts);
                break;
            case TaskTomlKey::UnsupportedMessage:
                target.unsupportedMessage = unquote_preserve(taskValue);
                break;
            case TaskTomlKey::Phase:
                target.phase = unquote(taskValue);
                break;
            case TaskTomlKey::Workdir:
                target.workdir = unquote(taskValue);
                break;
            case TaskTomlKey::Language:
                target.language = normalize_task_language(unquote(taskValue));
                break;
            case TaskTomlKey::Source:
                target.source = unquote(taskValue);
                break;
            case TaskTomlKey::Output:
                target.output = unquote(taskValue);
                break;
            case TaskTomlKey::Inputs:
                append_toml_string_list_value(taskValue, target.inputs);
                break;
            case TaskTomlKey::CompileOnly:
                target.compileOnly = parse_toml_bool_value(taskValue);
                break;
            case TaskTomlKey::Parallel:
                target.parallel = parse_toml_bool_value(taskValue);
                break;
            case TaskTomlKey::LogOutput:
                target.logOutput = parse_toml_bool_value(taskValue);
                break;
            case TaskTomlKey::InlineOutput:
                target.inlineOutput = parse_toml_bool_value(taskValue);
                break;
            case TaskTomlKey::DependsOn:
                append_toml_string_list_value(taskValue, target.dependsOn);
                break;
            case TaskTomlKey::PhaseDependsOn:
                append_toml_string_list_value(taskValue, target.phaseDependsOn);
                break;
            case TaskTomlKey::JoinOn:
                append_toml_string_list_value(taskValue, target.joinOn);
                break;
            case TaskTomlKey::PhaseJoinOn:
                append_toml_string_list_value(taskValue, target.phaseJoinOn);
                break;
            case TaskTomlKey::Next:
                append_toml_string_list_value(taskValue, target.nextTasks);
                break;
            case TaskTomlKey::NextPhases:
                append_toml_string_list_value(taskValue, target.nextPhases);
                break;
            case TaskTomlKey::Command:
            {
                append_toml_command_value(taskValue, target.commands);
                break;
            }
            case TaskTomlKey::Env:
                append_toml_string_list_value_preserve(taskValue, target.env);
                break;
            case TaskTomlKey::Script:
            case TaskTomlKey::Shell:
                append_toml_string_list_value_preserve(taskValue,
                                                       target.shellLines);
                break;
            case TaskTomlKey::Commands:
                append_toml_commands_value(taskValue, target.commands);
                break;
            case TaskTomlKey::Sign:
                append_toml_string_list_value(taskValue, target.signOutputs);
                break;
            case TaskTomlKey::Chmod:
                target.chmodMode = unquote(taskValue);
                break;
            case TaskTomlKey::ChmodPath:
            case TaskTomlKey::ChmodPaths:
                append_toml_string_list_value(taskValue, target.chmodPaths);
                break;
            case TaskTomlKey::OptLevel:
            case TaskTomlKey::TargetArch:
            case TaskTomlKey::PathEntries:
            case TaskTomlKey::CompilerFlags:
            case TaskTomlKey::LinkerFlags:
            case TaskTomlKey::LibPaths:
            case TaskTomlKey::Libs:
            case TaskTomlKey::StaticDeps:
            case TaskTomlKey::StaticCppRuntime:
                apply_task_build_config_key_value(target.buildConfig,
                                                  taskKeyKind, taskValue);
                break;
            case TaskTomlKey::Name:
                break;
            }
        };

        auto apply_task_kv = [&](TaskSpec& task, const std::string& taskKey,
                                 const std::string& taskValue)
        {
            const auto taskKeyKind = find_enum_key(taskKey, kTaskTomlKeys);
            if(!taskKeyKind.has_value())
                return;
            if(*taskKeyKind == TaskTomlKey::Name)
            {
                task.name = unquote(taskValue);
                return;
            }
            apply_common_task_kv(task, *taskKeyKind, taskValue);
        };

        auto apply_override_kv = [&](TaskSpec::HostOverride& ov,
                                     const std::string& taskKey,
                                     const std::string& taskValue)
        {
            const auto taskKeyKind = find_enum_key(taskKey, kTaskTomlKeys);
            if(!taskKeyKind.has_value() || *taskKeyKind == TaskTomlKey::Name)
                return;
            apply_common_task_kv(ov, *taskKeyKind, taskValue);
        };

        if(!currentHostSection.empty())
        {
            apply_override_kv(current->hostOverrides[currentHostSection], key,
                              value);
        }
        else
        {
            apply_task_kv(*current, key, value);
        }
    }
    flush_current();
    return tasks;
}

static void parse_build_config_key_value(BuildConfig& cfg,
                                         const std::string& key,
                                         const std::string& value)
{
    if(key == "opt_level")
    {
        cfg.optLevel = normalize_opt_level(unquote(value));
    }
    else if(key == "target_arch")
    {
        cfg.targetArch = normalize_target_arch_name(unquote(value));
    }
    else if(key == "min_mlang_version")
    {
        cfg.minMlangVersion = unquote(value);
    }
    else if(key == "make_program")
    {
        cfg.makeProgram = unquote(value);
    }
    else if(key == "build_dir")
    {
        cfg.buildDir = unquote(value);
    }
    else if(key == "deps_dir")
    {
        cfg.depsDir = unquote(value);
    }
    else if(key == "log_dir")
    {
        cfg.logDir = unquote(value);
    }
    else if(key == "stdout_log")
    {
        cfg.stdoutLog = unquote(value);
    }
    else if(key == "stderr_log")
    {
        cfg.stderrLog = unquote(value);
    }
    else if(key == "warn_log")
    {
        cfg.warnLog = unquote(value);
    }
    else if(key == "compiler_flags")
    {
        append_toml_string_list_value(value, cfg.compilerFlags);
    }
    else if(key == "path_entries" || key == "bin_paths")
    {
        append_toml_string_list_value(value, cfg.pathEntries);
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
    else if(key == "use_ninja" || key == "ninja")
    {
        cfg.useNinja = parse_toml_bool_value(value);
    }
    else if(key == "static_deps")
    {
        cfg.staticDeps = parse_toml_bool_value(value);
    }
    else if(key == "static_cpp_runtime")
    {
        cfg.staticCppRuntime = parse_toml_bool_value(value);
    }
    else if(key == "task_print_to_stdout_log")
    {
        cfg.taskPrintToStdoutLog = parse_toml_bool_value(value);
    }
}

struct DepSpec
{
    std::string name;
    std::string git;
    std::string url;
    std::string path;
    std::string versionRequirement;
    std::string archiveType;
    std::string rev;
    std::string tag;
    std::string build;
    std::string cmakeArgs;
    std::string subdir;
    int stripComponents = 1;
    bool spinner = true;
    bool submodules = false;
};

struct ManifestInclude
{
    std::string path;
    std::string target;
};

struct LinkFlags
{
    std::vector<std::string> libDirs;
    std::vector<std::string> libs;
    std::vector<std::string> staticArchives;
};

static std::filesystem::path
dep_source_dir(const PackageManifest& pkg,
               const std::filesystem::path& depsDir, const DepSpec& dep);

static std::vector<std::string> split_csv(std::string_view input)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool inDoubleQuotes = false;
    bool inSingleQuotes = false;
    bool escaped = false;
    for(char c : input)
    {
        if(inDoubleQuotes)
        {
            cur.push_back(c);
            if(escaped)
            {
                escaped = false;
                continue;
            }
            if(c == '\\')
            {
                escaped = true;
                continue;
            }
            if(c == '"')
                inDoubleQuotes = false;
            continue;
        }
        if(inSingleQuotes)
        {
            cur.push_back(c);
            if(c == '\'')
                inSingleQuotes = false;
            continue;
        }
        if(c == '"')
        {
            inDoubleQuotes = true;
            cur.push_back(c);
            continue;
        }
        if(c == '\'')
        {
            inSingleQuotes = true;
            cur.push_back(c);
            continue;
        }
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

static std::optional<unsigned int> parse_hex_codepoint(std::string_view text)
{
    if(text.empty())
        return std::nullopt;

    unsigned int value = 0;
    for(char c : text)
    {
        value <<= 4u;
        if(c >= '0' && c <= '9')
            value |= static_cast<unsigned int>(c - '0');
        else if(c >= 'a' && c <= 'f')
            value |= static_cast<unsigned int>(10 + (c - 'a'));
        else if(c >= 'A' && c <= 'F')
            value |= static_cast<unsigned int>(10 + (c - 'A'));
        else
            return std::nullopt;
    }
    return value;
}

static void append_utf8_codepoint(std::string& out, unsigned int codepoint)
{
    if(codepoint <= 0x7Fu)
    {
        out.push_back(static_cast<char>(codepoint));
        return;
    }
    if(codepoint <= 0x7FFu)
    {
        out.push_back(static_cast<char>(0xC0u | ((codepoint >> 6u) & 0x1Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        return;
    }
    if(codepoint <= 0xFFFFu)
    {
        out.push_back(static_cast<char>(0xE0u | ((codepoint >> 12u) & 0x0Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        return;
    }

    out.push_back(static_cast<char>(0xF0u | ((codepoint >> 18u) & 0x07u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
}

static std::string unescape_toml_basic_string(std::string_view v)
{
    std::string out;
    out.reserve(v.size());
    for(size_t i = 0; i < v.size(); ++i)
    {
        const char c = v[i];
        if(c != '\\' || i + 1 >= v.size())
        {
            out.push_back(c);
            continue;
        }

        const char escaped = v[++i];
        switch(escaped)
        {
        case 'b':
            out.push_back('\b');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case '"':
            out.push_back('"');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case 'u':
        case 'U':
        {
            const size_t hexDigits = escaped == 'u' ? 4u : 8u;
            if(i + hexDigits >= v.size())
            {
                out.push_back('\\');
                out.push_back(escaped);
                break;
            }

            const auto codepoint =
                parse_hex_codepoint(v.substr(i + 1, hexDigits));
            if(!codepoint.has_value())
            {
                out.push_back('\\');
                out.push_back(escaped);
                break;
            }

            append_utf8_codepoint(out, *codepoint);
            i += hexDigits;
            break;
        }
        default:
            out.push_back(escaped);
            break;
        }
    }
    return out;
}

static std::string unquote(std::string_view v)
{
    std::string t = trim(v);
    if(t.size() >= 2 && t.front() == '"' && t.back() == '"')
    {
        std::string inner =
            unescape_toml_basic_string(t.substr(1, t.size() - 2));
        std::string out;
        out.reserve(inner.size());
        bool previousWasSpace = false;
        for(char c : inner)
        {
            if(c == '\n' || c == '\r')
                c = ' ';
            if(c == ' ' || c == '\t')
            {
                if(previousWasSpace)
                    continue;
                previousWasSpace = true;
                out.push_back(' ');
                continue;
            }
            previousWasSpace = false;
            out.push_back(c);
        }
        return trim(out);
    }
    if(t.size() >= 2 && t.front() == '\'' && t.back() == '\'')
        return t.substr(1, t.size() - 2);
    return t;
}

static std::string unquote_preserve(std::string_view v)
{
    std::string t = trim(v);
    if(t.size() >= 2 && t.front() == '"' && t.back() == '"')
        return unescape_toml_basic_string(t.substr(1, t.size() - 2));
    if(t.size() >= 2 && t.front() == '\'' && t.back() == '\'')
        return t.substr(1, t.size() - 2);
    return t;
}

static std::optional<std::string>
parse_toml_command_tokens(const std::string& value)
{
    std::string t = trim(value);
    if(t.empty() || t.front() != '[' || t.back() != ']')
        return std::nullopt;

    const auto parts = split_toml_array(t.substr(1, t.size() - 2));
    if(parts.empty())
        return std::string();

    std::string command;
    for(const auto& part : parts)
    {
        std::string token = unquote_preserve(part);
        if(token.empty())
            continue;
        if(!command.empty())
            command += " ";
        command += shell_quote(token);
    }
    return command;
}

static void append_toml_command_value(const std::string& value,
                                      std::vector<std::string>& out)
{
    std::string t = trim(value);
    if(t.empty())
        return;

    if(auto tokenized = parse_toml_command_tokens(t); tokenized.has_value())
    {
        if(!tokenized->empty())
            out.push_back(*tokenized);
        return;
    }

    std::string v = unquote_preserve(t);
    if(!v.empty())
        out.push_back(v);
}

static void append_toml_commands_value(const std::string& value,
                                       std::vector<std::string>& out)
{
    std::string t = trim(value);
    if(t.empty())
        return;
    if(t.front() == '[' && t.back() == ']')
    {
        for(const auto& part : split_toml_array(t.substr(1, t.size() - 2)))
        {
            if(part.empty())
                continue;
            append_toml_command_value(part, out);
        }
        return;
    }

    append_toml_command_value(t, out);
}

static void
append_toml_string_list_value_preserve(const std::string& value,
                                       std::vector<std::string>& out)
{
    std::string t = trim(value);
    if(t.empty())
        return;
    if(t.front() == '[' && t.back() == ']')
    {
        for(const auto& part : split_toml_array(t.substr(1, t.size() - 2)))
        {
            std::string v = unquote_preserve(part);
            if(!v.empty())
                out.push_back(v);
        }
        return;
    }

    std::string v = unquote_preserve(t);
    if(!v.empty())
        out.push_back(v);
}

static std::map<std::string, std::string>
parse_inline_table(const std::string& line)
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

static std::vector<ToolchainRequirement>
parse_toolchain_requirements(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::string section;
    std::vector<ToolchainRequirement> requirements;
    while(std::getline(in, line))
    {
        const std::string text = strip_toml_comment(line);
        if(text.empty())
            continue;
        if(text.front() == '[' && text.back() == ']')
        {
            section = text.substr(1, text.size() - 2);
            continue;
        }
        if(section != "tool.mlang.toolchains")
            continue;

        const auto assignment = parse_toml_assignment(text, in);
        if(!assignment.has_value())
            continue;
        const auto values = parse_inline_table(assignment->value);
        ToolchainRequirement requirement;
        requirement.name = assignment->key;
        if(const auto it = values.find("name"); it != values.end())
            requirement.name = it->second;
        if(const auto it = values.find("command"); it != values.end())
            requirement.command = it->second;
        if(const auto it = values.find("min_version"); it != values.end())
            requirement.minVersion = it->second;
        if(const auto it = values.find("version_args"); it != values.end())
            requirement.versionArgs = it->second;
        if(const auto it = values.find("host"); it != values.end())
            requirement.host = it->second;
        if(const auto it = values.find("install"); it != values.end())
            requirement.install = it->second;
        requirements.push_back(std::move(requirement));
    }
    return requirements;
}

static int parse_int_or_default(const std::string& text, int fallback)
{
    if(text.empty())
        return fallback;
    try
    {
        return std::stoi(text);
    }
    catch(...)
    {
        return fallback;
    }
}

static std::vector<DepSpec> parse_source_deps(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::string section;
    std::vector<DepSpec> deps;
    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t.empty())
            continue;
        if(t.front() == '[' && t.back() == ']')
        {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if(section != "dependencies" && section != "c-dependencies")
            continue;
        const auto assignment = parse_toml_assignment(t, in);
        if(!assignment.has_value())
            continue;
        std::string name = assignment->key;
        if(name.empty())
            continue;
        if(t.find('{') == std::string::npos)
            continue;
        auto kv = parse_inline_table(t);
        auto gitIt = kv.find("git");
        auto urlIt = kv.find("url");
        auto pathIt = kv.find("path");
        const bool hasSource = gitIt != kv.end() || urlIt != kv.end() ||
                               pathIt != kv.end();
        if(!hasSource && section == "c-dependencies")
            continue;
        DepSpec dep;
        dep.name = name;
        if(gitIt != kv.end())
            dep.git = gitIt->second;
        if(urlIt != kv.end())
            dep.url = urlIt->second;
        if(pathIt != kv.end())
            dep.path = pathIt->second;
        if(auto it = kv.find("version"); it != kv.end())
            dep.versionRequirement = it->second;
        if(auto it = kv.find("archive"); it != kv.end())
            dep.archiveType = it->second;
        if(auto it = kv.find("rev"); it != kv.end())
            dep.rev = it->second;
        if(auto it = kv.find("tag"); it != kv.end())
            dep.tag = it->second;
        if(auto it = kv.find("build"); it != kv.end())
            dep.build = it->second;
        if(auto it = kv.find("cmake_args"); it != kv.end())
            dep.cmakeArgs = it->second;
        if(auto it = kv.find("subdir"); it != kv.end())
            dep.subdir = it->second;
        if(auto it = kv.find("strip_components"); it != kv.end())
            dep.stripComponents = parse_int_or_default(it->second, 1);
        if(auto it = kv.find("spinner"); it != kv.end())
            dep.spinner = parse_toml_bool_value(it->second);
        if(auto it = kv.find("submodules"); it != kv.end())
            dep.submodules = parse_toml_bool_value(it->second);
        if(dep.archiveType.empty() && !dep.url.empty())
        {
            if(dep.url.size() >= 7 &&
               dep.url.substr(dep.url.size() - 7) == ".tar.gz")
                dep.archiveType = "tar.gz";
            else if(dep.url.size() >= 4 &&
                    dep.url.substr(dep.url.size() - 4) == ".tgz")
                dep.archiveType = "tar.gz";
        }
        if(dep.build.empty())
            dep.build = dep.path.empty() ? "cmake" : "mlang";
        deps.push_back(dep);
    }
    return deps;
}

static std::vector<std::string>
parse_workspace_members(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::string section;
    std::vector<std::string> out;
    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t.empty())
            continue;
        if(t.front() == '[' && t.back() == ']')
        {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if(section != "workspace")
            continue;
        const auto assignment = parse_toml_assignment(t, in);
        if(!assignment.has_value())
            continue;
        std::string key = assignment->key;
        if(key != "members")
            continue;
        std::string value = assignment->value;
        if(value.empty())
            return out;
        if(value.front() == '[' && value.back() == ']')
        {
            for(const auto& part :
                split_toml_array(value.substr(1, value.size() - 2)))
            {
                std::string v = unquote(part);
                if(!v.empty())
                    out.push_back(v);
            }
            return out;
        }
        std::string single = unquote(value);
        if(!single.empty())
            out.push_back(single);
        return out;
    }
    return out;
}

static std::vector<ManifestInclude>
parse_manifest_includes(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::vector<ManifestInclude> includes;
    std::optional<ManifestInclude> current;

    auto flush_current = [&]()
    {
        if(current.has_value())
            includes.push_back(*current);
        current.reset();
    };

    while(std::getline(in, line))
    {
        const std::string text = strip_toml_comment(line);
        if(text.empty())
            continue;
        if(text == "[[include]]")
        {
            flush_current();
            current = ManifestInclude{};
            continue;
        }
        if(text.front() == '[' && text.back() == ']')
        {
            flush_current();
            continue;
        }
        if(!current.has_value())
            continue;

        const auto assignment = parse_toml_assignment(text, in);
        if(!assignment.has_value())
            continue;
        if(assignment->key == "path")
            current->path = unquote(assignment->value);
        else if(assignment->key == "target")
            current->target = unquote(assignment->value);
    }
    flush_current();
    return includes;
}

static bool has_section(const std::string& content, const std::string& wanted)
{
    std::istringstream in(content);
    std::string line;
    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t == ("[" + wanted + "]"))
            return true;
    }
    return false;
}

static std::string sanitize_package_name(std::string name)
{
    for(char& c : name)
    {
        if(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
            continue;
        c = '_';
    }
    if(name.empty())
        name = "pkg";
    return name;
}

static std::filesystem::path default_added_package_dir(const std::string& name)
{
    return std::filesystem::path("packages") / sanitize_package_name(name);
}

static bool write_text_file_if_missing(const std::filesystem::path& path,
                                       const std::string& content)
{
    if(std::filesystem::exists(path))
        return true;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if(!out)
        return false;
    out << content;
    return true;
}

static std::string make_dependency_manifest_line(
    const std::string& name, const std::string& gitUrl,
    const std::string& archiveUrl, const std::string& archiveType,
    const std::string& rev, const std::string& tag, bool gitSubmodules,
    const std::string& depSubdir, int stripComponents)
{
    std::string line;
    if(!archiveUrl.empty())
    {
        line = name + " = { url = \"" + archiveUrl + "\"";
        if(!archiveType.empty())
            line += ", archive = \"" + archiveType + "\"";
        if(stripComponents != 1)
            line += ", strip_components = \"" +
                    std::to_string(stripComponents) + "\"";
        if(!depSubdir.empty())
            line += ", subdir = \"" + depSubdir + "\"";
        line += " }";
    }
    else if(!gitUrl.empty())
    {
        line = name + " = { git = \"" + gitUrl + "\"";
        if(!rev.empty())
            line += ", rev = \"" + rev + "\"";
        if(!tag.empty())
            line += ", tag = \"" + tag + "\"";
        if(gitSubmodules)
            line += ", submodules = true";
        if(!depSubdir.empty())
            line += ", subdir = \"" + depSubdir + "\"";
        line += " }";
    }
    else
    {
        line = name + " = \"*\"";
    }
    return line;
}

static std::string relative_path_string(const std::filesystem::path& fromDir,
                                        const std::filesystem::path& toPath)
{
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(toPath, fromDir, ec);
    if(ec)
        return toPath.lexically_normal().string();
    return rel.lexically_normal().string();
}

static std::string package_stub_source(const std::string& depName)
{
    return "fn main() {\n"
           "    println!(\"" +
           depName +
           " subproject scaffold ready\");\n"
           "}\n";
}

static bool ensure_package_entry_stub(const std::filesystem::path& manifestPath,
                                      const std::string& manifestContent,
                                      const std::string& label,
                                      std::string& error)
{
    if(!has_section(manifestContent, "package"))
        return true;

    std::string entry = "src/main.mla";
    if(auto entryOpt =
           find_section_toml_string(manifestContent, "package", "entry");
       entryOpt.has_value() && !entryOpt->empty())
    {
        entry = *entryOpt;
    }

    const std::filesystem::path entryPath = manifestPath.parent_path() / entry;
    if(std::filesystem::exists(entryPath))
        return true;
    if(!write_text_file_if_missing(entryPath, package_stub_source(label)))
    {
        error = "Failed to write package entry source: " + entryPath.string();
        return false;
    }
    return true;
}

static bool manifest_declares_tasks(const std::string& manifestContent)
{
    return manifestContent.find("[[task]]") != std::string::npos;
}

static bool task_list_contains_name(const std::vector<TaskSpec>& tasks,
                                    const std::string& taskName)
{
    for(const auto& task : tasks)
    {
        if(task.name == taskName)
            return true;
    }
    return false;
}

static bool scaffold_added_package(
    const std::filesystem::path& rootManifestPath,
    const std::filesystem::path& packageDir, const std::string& packageName,
    const std::string& depLine, const std::string& depName, std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(packageDir / "src", ec);
    if(ec)
    {
        error = "Failed to create package directory: " + packageDir.string();
        return false;
    }

    fs::path packageManifest = packageDir / "mlang.toml";
    std::string packageContent;
    if(fs::exists(packageManifest))
    {
        std::ifstream in(packageManifest, std::ios::binary);
        packageContent.assign((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
        if(packageContent.empty())
        {
            error = "Failed to read existing subproject manifest: " +
                    packageManifest.string();
            return false;
        }
    }
    else
    {
        std::string entry =
            relative_path_string(packageDir, packageDir / "src" / "main.mla");
        packageContent = "[package]\n";
        packageContent += "name = \"" + packageName + "\"\n";
        packageContent += "version = \"" + std::string(MLANG_VERSION) + "\"\n";
        packageContent += "entry = \"" + entry + "\"\n\n";
        packageContent += "[dependencies]\n\n";
        packageContent += "[c-dependencies]\n";
    }

    if(!has_section(packageContent, "package"))
    {
        error = "Subproject manifest is missing [package]: " +
                packageManifest.string();
        return false;
    }
    add_dep_to_section(packageContent, "dependencies", depName, depLine);

    std::ofstream out(packageManifest, std::ios::binary | std::ios::trunc);
    if(!out)
    {
        error =
            "Failed to write subproject manifest: " + packageManifest.string();
        return false;
    }
    out << packageContent;

    if(!write_text_file_if_missing(packageDir / "src" / "main.mla",
                                   package_stub_source(depName)))
    {
        error = "Failed to write subproject entry source: " +
                (packageDir / "src" / "main.mla").string();
        return false;
    }
    return true;
}

static std::string shell_quote(const std::string& s)
{
    std::string out = "'";
    for(char c : s)
    {
        if(c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out += "'";
    return out;
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
                         std::unordered_set<std::string>& libs,
                         std::unordered_set<std::string>& staticArchives)
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
        if(entry.path().extension() == ".a")
            staticArchives.insert(entry.path().string());
    }
}

struct PackageManifest
{
    std::filesystem::path manifestPath;
    std::filesystem::path packageDir;
    std::string content;
    std::string includeTarget;
    std::filesystem::path includeRootDir;
};

static bool valid_include_target(const std::string& target)
{
    if(target.empty() || target == "." || target == "..")
        return false;
    return std::all_of(target.begin(), target.end(), [](unsigned char c)
                       { return std::isalnum(c) || c == '-' || c == '_'; });
}

static void apply_include_output_paths(const PackageManifest& pkg,
                                       BuildConfig& buildConfig)
{
    if(pkg.includeTarget.empty())
        return;
    const std::filesystem::path targetDir =
        (pkg.includeRootDir / "build" / pkg.includeTarget).lexically_normal();
    buildConfig.includedBuildDir = targetDir.string();
    buildConfig.includedDepsDir = (targetDir / "deps").string();
}

static BuildConfig parse_manifest_build_config(const PackageManifest& pkg)
{
    BuildConfig buildConfig = parse_build_config(pkg.content);
    apply_include_output_paths(pkg, buildConfig);
    return buildConfig;
}

struct DependencyLockEntry
{
    std::string manifest;
    std::string name;
    std::string source;
    std::string url;
    std::string path;
    std::string versionRequirement;
    std::string resolvedVersion;
    std::string requestedRev;
    std::string requestedTag;
    std::string revision;
    std::string checksum;
    std::string archiveType;
    std::string subdir;
    int stripComponents = 1;
    bool submodules = false;
};

struct DependencyLockContext
{
    std::filesystem::path rootManifest;
    std::filesystem::path lockPath;
    bool locked = false;
    bool offline = false;
    bool dirty = false;
    std::map<std::string, DependencyLockEntry> entries;
    std::unordered_set<std::string> observedLockKeys;
    std::unordered_set<std::string> fetchedManifests;
    std::unordered_set<std::string> activeFetchManifests;
    std::unordered_set<std::string> verifiedManifests;
    std::unordered_set<std::string> activeVerifyManifests;
    std::unordered_set<std::string> builtManifests;
    std::unordered_set<std::string> activeBuildManifests;
    std::map<std::string, std::string> packageSources;
};

static DependencyLockContext*& current_dependency_lock_context()
{
    static DependencyLockContext* context = nullptr;
    return context;
}

class ScopedDependencyLockContext
{
public:
    explicit ScopedDependencyLockContext(DependencyLockContext& context)
        : previous_(current_dependency_lock_context())
    {
        current_dependency_lock_context() = &context;
    }

    ~ScopedDependencyLockContext()
    {
        current_dependency_lock_context() = previous_;
    }

private:
    DependencyLockContext* previous_;
};

static std::string lock_manifest_name(const DependencyLockContext& context,
                                      const PackageManifest& pkg)
{
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(
        pkg.manifestPath, context.rootManifest.parent_path(), ec);
    if(ec)
        return pkg.manifestPath.lexically_normal().generic_string();
    return relative.lexically_normal().generic_string();
}

static std::string dependency_lock_key(const std::string& manifest,
                                       const std::string& name)
{
    return manifest + "\n" + name;
}

static DependencyLockEntry make_dependency_lock_entry(
    const DependencyLockContext& context, const PackageManifest& pkg,
    const DepSpec& dep)
{
    DependencyLockEntry entry;
    entry.manifest = lock_manifest_name(context, pkg);
    entry.name = dep.name;
    entry.source = !dep.path.empty()
                       ? "path"
                       : (!dep.git.empty() ? "git" : "archive");
    entry.url = !dep.git.empty() ? dep.git : dep.url;
    entry.path = dep.path;
    entry.versionRequirement = dep.versionRequirement;
    entry.requestedRev = dep.rev;
    entry.requestedTag = dep.tag;
    entry.archiveType = dep.archiveType;
    entry.subdir = dep.subdir;
    entry.stripComponents = dep.stripComponents;
    entry.submodules = dep.submodules;
    return entry;
}

static bool lock_entry_matches_dependency(const DependencyLockEntry& entry,
                                          const DependencyLockEntry& expected)
{
    return entry.manifest == expected.manifest && entry.name == expected.name &&
           entry.source == expected.source && entry.url == expected.url &&
           entry.path == expected.path &&
           entry.versionRequirement == expected.versionRequirement &&
           entry.requestedRev == expected.requestedRev &&
           entry.requestedTag == expected.requestedTag &&
           entry.archiveType == expected.archiveType &&
           entry.subdir == expected.subdir &&
           entry.stripComponents == expected.stripComponents &&
           entry.submodules == expected.submodules;
}

static std::optional<std::string>
sha256_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
        return std::nullopt;
    llvm::SHA256 sha;
    std::array<char, 64 * 1024> buffer{};
    while(in)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        if(count > 0)
            sha.update(llvm::StringRef(buffer.data(),
                                       static_cast<size_t>(count)));
    }
    if(in.bad())
        return std::nullopt;
    const auto digest = sha.final();
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for(uint8_t byte : digest)
    {
        out.push_back(digits[(byte >> 4) & 0x0f]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

static std::string lock_quote(const std::string& value)
{
    std::string out = "\"";
    for(char c : value)
    {
        if(c == '\\' || c == '"')
            out.push_back('\\');
        if(c == '\n')
            out += "\\n";
        else
            out.push_back(c);
    }
    out.push_back('"');
    return out;
}

static bool read_dependency_lock(const std::filesystem::path& path,
                                 DependencyLockContext& context,
                                 std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
    {
        error = "Failed to read lockfile: " + path.string();
        return false;
    }
    std::string line;
    std::optional<DependencyLockEntry> current;
    bool versionSeen = false;
    auto flush = [&]() -> bool
    {
        if(!current.has_value())
            return true;
        if(current->manifest.empty() || current->name.empty() ||
           current->source.empty() ||
           (current->source != "path" && current->url.empty()) ||
           (current->source == "path" && current->path.empty()))
        {
            error = "Invalid incomplete package entry in " + path.string();
            return false;
        }
        const std::string key =
            dependency_lock_key(current->manifest, current->name);
        if(!context.entries.emplace(key, *current).second)
        {
            error = "Duplicate dependency '" + current->name + "' for " +
                    current->manifest + " in " + path.string();
            return false;
        }
        current.reset();
        return true;
    };

    while(std::getline(in, line))
    {
        const std::string text = strip_toml_comment(line);
        if(text.empty())
            continue;
        if(text == "[[package]]")
        {
            if(!flush())
                return false;
            current = DependencyLockEntry{};
            continue;
        }
        if(text.front() == '[' && text.back() == ']')
        {
            error = "Unsupported section in " + path.string() + ": " + text;
            return false;
        }
        const auto assignment = parse_toml_assignment(text, in);
        if(!assignment.has_value())
            continue;
        if(!current.has_value())
        {
            if(assignment->key == "version")
            {
                if(unquote(assignment->value) != "1")
                {
                    error = "Unsupported mlang.lock version in " +
                            path.string();
                    return false;
                }
                versionSeen = true;
            }
            continue;
        }
        const std::string value = unquote(assignment->value);
        if(assignment->key == "manifest")
            current->manifest = value;
        else if(assignment->key == "name")
            current->name = value;
        else if(assignment->key == "source")
            current->source = value;
        else if(assignment->key == "url")
            current->url = value;
        else if(assignment->key == "path")
            current->path = value;
        else if(assignment->key == "requirement")
            current->versionRequirement = value;
        else if(assignment->key == "resolved_version")
            current->resolvedVersion = value;
        else if(assignment->key == "requested_rev")
            current->requestedRev = value;
        else if(assignment->key == "requested_tag")
            current->requestedTag = value;
        else if(assignment->key == "revision")
            current->revision = value;
        else if(assignment->key == "checksum")
            current->checksum = value.rfind("sha256:", 0) == 0
                                    ? value.substr(7)
                                    : value;
        else if(assignment->key == "archive")
            current->archiveType = value;
        else if(assignment->key == "subdir")
            current->subdir = value;
        else if(assignment->key == "strip_components")
            current->stripComponents = parse_int_or_default(value, 1);
        else if(assignment->key == "submodules")
            current->submodules = parse_toml_bool_value(value);
    }
    if(!flush())
        return false;
    if(!versionSeen)
    {
        error = "Missing lockfile version in " + path.string();
        return false;
    }
    return true;
}

static bool write_dependency_lock(const DependencyLockContext& context,
                                  std::string& error)
{
    const std::filesystem::path temporaryPath =
        context.lockPath.string() + ".tmp";
    std::ofstream out(temporaryPath, std::ios::binary | std::ios::trunc);
    if(!out)
    {
        error = "Failed to write temporary lockfile: " +
                temporaryPath.string();
        return false;
    }
    out << "# Generated by mlang pkg. Do not edit manually.\n";
    out << "version = 1\n";
    for(const auto& [key, entry] : context.entries)
    {
        (void)key;
        out << "\n[[package]]\n";
        out << "manifest = " << lock_quote(entry.manifest) << "\n";
        out << "name = " << lock_quote(entry.name) << "\n";
        out << "source = " << lock_quote(entry.source) << "\n";
        if(!entry.url.empty())
            out << "url = " << lock_quote(entry.url) << "\n";
        if(!entry.path.empty())
            out << "path = " << lock_quote(entry.path) << "\n";
        if(!entry.versionRequirement.empty())
            out << "requirement = " << lock_quote(entry.versionRequirement)
                << "\n";
        if(!entry.resolvedVersion.empty())
            out << "resolved_version = " << lock_quote(entry.resolvedVersion)
                << "\n";
        if(!entry.requestedRev.empty())
            out << "requested_rev = " << lock_quote(entry.requestedRev) << "\n";
        if(!entry.requestedTag.empty())
            out << "requested_tag = " << lock_quote(entry.requestedTag) << "\n";
        if(!entry.revision.empty())
            out << "revision = " << lock_quote(entry.revision) << "\n";
        if(!entry.checksum.empty())
            out << "checksum = " << lock_quote("sha256:" + entry.checksum)
                << "\n";
        if(!entry.archiveType.empty())
            out << "archive = " << lock_quote(entry.archiveType) << "\n";
        if(!entry.subdir.empty())
            out << "subdir = " << lock_quote(entry.subdir) << "\n";
        if(entry.stripComponents != 1)
            out << "strip_components = " << entry.stripComponents << "\n";
        if(entry.submodules)
            out << "submodules = true\n";
    }
    out.close();
    if(!out)
    {
        error = "Failed to finish writing lockfile: " + temporaryPath.string();
        return false;
    }
#if defined(_WIN32)
    {
        std::error_code ec;
        std::filesystem::remove(context.lockPath, ec);
    }
#endif
    if(std::rename(temporaryPath.string().c_str(),
                   context.lockPath.string().c_str()) != 0)
    {
        std::error_code ec;
        std::filesystem::remove(temporaryPath, ec);
        error = "Failed to replace lockfile atomically: " +
                context.lockPath.string();
        return false;
    }
    return true;
}

static std::vector<std::filesystem::path>
discover_workspace_manifests(const std::filesystem::path& manifestPath,
                             const std::string& content)
{
    namespace fs = std::filesystem;
    std::vector<std::filesystem::path> out;
    std::unordered_set<std::string> seen;

    const fs::path rootDir = manifestPath.parent_path();
    const auto members = parse_workspace_members(content);
    for(const auto& member : members)
    {
        fs::path base = rootDir / member;
        std::error_code ec;
        if(fs::is_regular_file(base, ec))
        {
            fs::path candidate =
                base.filename() == "mlang.toml" ? base : (base / "mlang.toml");
            if(fs::exists(candidate, ec))
            {
                std::string key = candidate.lexically_normal().string();
                if(seen.insert(key).second)
                    out.push_back(candidate);
            }
            continue;
        }
        if(!fs::is_directory(base, ec))
            continue;

        for(fs::recursive_directory_iterator it(base, ec), end; it != end;
            it.increment(ec))
        {
            if(ec)
                break;
            const fs::path p = it->path();
            if(it->is_directory(ec))
            {
                const std::string name = p.filename().string();
                if(name == ".git" || name == "build" || name == "docs")
                    it.disable_recursion_pending();
                continue;
            }
            if(!it->is_regular_file(ec) || p.filename() != "mlang.toml")
                continue;
            std::string key = p.lexically_normal().string();
            if(seen.insert(key).second)
                out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

static std::vector<PackageManifest>
collect_target_manifests(const std::filesystem::path& manifestPath)
{
    namespace fs = std::filesystem;
    std::vector<PackageManifest> out;
    fs::path manifestAbs = fs::absolute(manifestPath);
    std::ifstream in(manifestAbs, std::ios::binary);
    if(!in)
        return out;
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if(content.empty())
        return out;

    std::unordered_set<std::string> seen;
    seen.insert(manifestAbs.lexically_normal().string());

    if(has_section(content, "package"))
    {
        out.push_back(PackageManifest{
            manifestAbs, manifestAbs.parent_path(), content, "", {}});
    }

    std::unordered_set<std::string> includeTargets;
    for(const auto& include : parse_manifest_includes(content))
    {
        if(include.path.empty())
        {
            pkg_error_line("Missing path in [[include]] entry in " +
                           manifestAbs.string());
            return {};
        }
        if(!valid_include_target(include.target))
        {
            pkg_error_line("Invalid or missing target in [[include]] for '" +
                           include.path + "' in " + manifestAbs.string() +
                           "; use a unique name containing only letters, "
                           "digits, '-' or '_'");
            return {};
        }
        if(!includeTargets.insert(include.target).second)
        {
            pkg_error_line("Duplicate [[include]] target '" + include.target +
                           "' in " + manifestAbs.string());
            return {};
        }

        fs::path childPath = manifestAbs.parent_path() / include.path;
        std::error_code ec;
        if(fs::is_directory(childPath, ec))
            childPath /= "mlang.toml";
        childPath = fs::absolute(childPath).lexically_normal();
        if(!fs::is_regular_file(childPath, ec))
        {
            pkg_error_line("Included package manifest not found: " +
                           childPath.string());
            return {};
        }
        const std::string childKey = childPath.string();
        if(!seen.insert(childKey).second)
        {
            pkg_error_line("Package manifest included more than once: " +
                           childPath.string());
            return {};
        }

        std::ifstream childIn(childPath, std::ios::binary);
        std::string childContent((std::istreambuf_iterator<char>(childIn)),
                                 std::istreambuf_iterator<char>());
        if(childContent.empty() || !has_section(childContent, "package"))
        {
            pkg_error_line("Included manifest must declare [package]: " +
                           childPath.string());
            return {};
        }
        out.push_back(PackageManifest{childPath, childPath.parent_path(),
                                      childContent, include.target,
                                      manifestAbs.parent_path()});
    }
    for(const auto& child : discover_workspace_manifests(manifestAbs, content))
    {
        fs::path childAbs = fs::absolute(child);
        const std::string childKey = childAbs.lexically_normal().string();
        if(!seen.insert(childKey).second)
            continue;
        std::ifstream childIn(childAbs, std::ios::binary);
        if(!childIn)
            continue;
        std::string childContent((std::istreambuf_iterator<char>(childIn)),
                                 std::istreambuf_iterator<char>());
        if(childContent.empty() || !has_section(childContent, "package"))
            continue;
        out.push_back(PackageManifest{
            childAbs, childAbs.parent_path(), childContent, "", {}});
    }
    return out;
}

static bool prepare_dependency_lock_context(
    const std::filesystem::path& rootManifest,
    const std::vector<PackageManifest>& manifests, bool locked, bool offline,
    DependencyLockContext& context)
{
    context.rootManifest = std::filesystem::absolute(rootManifest);
    context.lockPath = context.rootManifest.parent_path() / "mlang.lock";
    context.locked = locked;
    context.offline = offline;

    const bool lockExists = std::filesystem::exists(context.lockPath);
    if(lockExists)
    {
        std::string error;
        if(!read_dependency_lock(context.lockPath, context, error))
        {
            pkg_error_line(error);
            return false;
        }
    }
    else if(locked || offline)
    {
        pkg_error_line("mlang.lock not found at " + context.lockPath.string() +
                       (locked ? "; --locked requires an existing lockfile"
                               : "; --offline requires an existing lockfile"));
        return false;
    }

    std::map<std::string, DependencyLockEntry> expected;
    for(const auto& pkg : manifests)
    {
        const auto declaredName =
            find_section_toml_string(pkg.content, "package", "name");
        const auto declaredVersion =
            find_section_toml_string(pkg.content, "package", "version");
        if(!declaredName.has_value() || declaredName->empty() ||
           !declaredVersion.has_value() ||
           !is_complete_semantic_version(*declaredVersion))
        {
            pkg_error_line("Package manifest requires a name and complete "
                           "semantic version: " + pkg.manifestPath.string());
            return false;
        }
        for(const auto& dep : parse_source_deps(pkg.content))
        {
            DependencyLockEntry entry =
                make_dependency_lock_entry(context, pkg, dep);
            expected.emplace(dependency_lock_key(entry.manifest, entry.name),
                             std::move(entry));
        }
    }

    bool stale = false;
    if(!stale)
    {
        for(const auto& [key, wanted] : expected)
        {
            const auto found = context.entries.find(key);
            if(found == context.entries.end() ||
               !lock_entry_matches_dependency(found->second, wanted) ||
               (wanted.source == "git" && found->second.revision.empty()) ||
               (wanted.source == "archive" && found->second.checksum.empty()) ||
               (wanted.source == "path" &&
                found->second.resolvedVersion.empty()))
            {
                stale = true;
                break;
            }
        }
    }

    if(stale)
    {
        if(locked || offline)
        {
            pkg_error_line("mlang.lock is out of date for " +
                           context.rootManifest.string() +
                           (locked ? "; rerun 'mlang pkg lock'"
                                   : "; update it before using --offline"));
            return false;
        }
        if(lockExists)
            pkg_info_line("Dependency declarations changed; regenerating " +
                          context.lockPath.string());
        context.entries.clear();
        context.dirty = true;
    }
    else if(!lockExists)
    {
        context.dirty = true;
    }
    return true;
}

static DependencyLockEntry* dependency_lock_entry(const PackageManifest& pkg,
                                                  const DepSpec& dep)
{
    DependencyLockContext* context = current_dependency_lock_context();
    if(context == nullptr)
        return nullptr;
    const std::string manifest = lock_manifest_name(*context, pkg);
    const std::string key = dependency_lock_key(manifest, dep.name);
    context->observedLockKeys.insert(key);
    const auto found = context->entries.find(key);
    return found == context->entries.end() ? nullptr : &found->second;
}

static void record_dependency_lock_entry(const PackageManifest& pkg,
                                         const DepSpec& dep,
                                         const std::string& revision,
                                         const std::string& checksum,
                                         const std::string& resolvedVersion = "")
{
    DependencyLockContext* context = current_dependency_lock_context();
    if(context == nullptr)
        return;
    DependencyLockEntry entry = make_dependency_lock_entry(*context, pkg, dep);
    entry.revision = revision;
    entry.checksum = checksum;
    entry.resolvedVersion = resolvedVersion;
    const std::string key = dependency_lock_key(entry.manifest, entry.name);
    context->observedLockKeys.insert(key);
    const auto found = context->entries.find(key);
    if(entry.resolvedVersion.empty() && found != context->entries.end())
        entry.resolvedVersion = found->second.resolvedVersion;
    if(found == context->entries.end() ||
       found->second.revision != entry.revision ||
       found->second.checksum != entry.checksum ||
       found->second.resolvedVersion != entry.resolvedVersion ||
       !lock_entry_matches_dependency(found->second, entry))
    {
        context->entries[key] = std::move(entry);
        context->dirty = true;
    }
}

static bool finish_dependency_lock_context(DependencyLockContext& context)
{
    std::vector<std::string> obsolete;
    for(const auto& [key, entry] : context.entries)
    {
        (void)entry;
        if(context.observedLockKeys.count(key) == 0)
            obsolete.push_back(key);
    }
    if(!obsolete.empty())
    {
        if(context.locked || context.offline)
        {
            pkg_error_line("mlang.lock contains dependencies no longer "
                           "reachable from " + context.rootManifest.string() +
                           "; rerun 'mlang pkg lock'");
            return false;
        }
        for(const auto& key : obsolete)
            context.entries.erase(key);
        context.dirty = true;
    }
    if(!context.dirty)
        return true;
    if(context.locked || context.offline)
    {
        pkg_error_line("Refusing to modify " + context.lockPath.string() +
                       " in locked/offline mode");
        return false;
    }
    std::string error;
    if(!write_dependency_lock(context, error))
    {
        pkg_error_line(error);
        return false;
    }
    pkg_info_line("Wrote reproducible dependency lockfile " +
                  context.lockPath.string());
    context.dirty = false;
    return true;
}

static std::string command_with_log_redirection(const std::string& cmd,
                                                bool logChildOutput)
{
    if(!logChildOutput)
        return cmd;

    std::string full = cmd;
    if(current_package_log_state().stdoutLog.has_value())
    {
        full += " >> " +
                shell_quote(current_package_log_state().stdoutLog->string());
    }
    if(current_package_log_state().stderrLog.has_value())
    {
        full += " 2>> " +
                shell_quote(current_package_log_state().stderrLog->string());
    }
    return full;
}

static int run_command(const std::string& cmd, bool logCommand = true,
                       bool logChildOutput = true)
{
    pkg_info_line(cmd, logCommand);
    return std::system(
        command_with_log_redirection(cmd, logChildOutput).c_str());
}

class ProgressSpinner
{
public:
    explicit ProgressSpinner(std::string label) : label_(std::move(label))
    {
        hideCursor_ = ::isatty(fileno(stderr)) != 0;
        if(hideCursor_)
            std::cerr << "\033[?25l" << std::flush;
        worker_ = std::thread(
            [this]()
            {
                static constexpr char frames[] = {'|', '/', '-', '\\'};
                size_t idx = 0;
                while(!done_.load())
                {
                    std::string detail;
                    {
                        std::lock_guard<std::mutex> lock(detailMutex_);
                        detail = detail_;
                    }
                    std::cerr << "\r\033[2K"
                              << live_line(frames[idx % 4], detail)
                              << std::flush;
                    idx++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                }
            });
    }

    ~ProgressSpinner()
    {
        stop();
    }

    void stop(const std::string& suffix = "")
    {
        if(stopped_)
            return;
        done_.store(true);
        if(worker_.joinable())
            worker_.join();
        std::cerr << "\r\033[2K" << label_;
        if(!suffix.empty())
            std::cerr << " " << suffix;
        if(hideCursor_)
            std::cerr << "\033[?25h";
        std::cerr << std::endl;
        stopped_ = true;
    }

    void set_detail(std::string detail)
    {
        std::lock_guard<std::mutex> lock(detailMutex_);
        detail_ = sanitize_progress_output(std::move(detail));
    }

private:
    std::string live_line(char frame, const std::string& detail) const
    {
        if(!label_.empty() && label_.front() == '[')
        {
            const size_t close = label_.find(']');
            if(close != std::string::npos)
            {
                std::string out = label_.substr(0, close + 1);
                out += " ";
                out += frame;
                const std::string rest = trim(label_.substr(close + 1));
                if(!rest.empty())
                    out += " " + rest;
                if(!detail.empty())
                    out += " " + shorten_progress_output(detail);
                return out;
            }
        }

        std::string out;
        out += frame;
        out += " ";
        out += label_;
        if(!detail.empty())
            out += " " + shorten_progress_output(detail);
        return out;
    }

    std::string label_;
    std::string detail_;
    std::mutex detailMutex_;
    std::atomic<bool> done_{false};
    std::thread worker_;
    bool stopped_ = false;
    bool hideCursor_ = false;
};

static int run_progress_command(const std::string& label,
                                const std::string& cmd, bool logCommand = true,
                                bool logChildOutput = true)
{
    const auto start = std::chrono::steady_clock::now();
    ProgressSpinner spinner(shorten_progress_description(label));
    int rc = run_command(cmd, logCommand, logChildOutput);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    std::string suffix;
    if(label.size() > 4 && label.front() == '[' &&
       label.find('/') != std::string::npos &&
       label.find(']') != std::string::npos)
    {
        if(rc == 0)
        {
            const size_t close = label.find(']');
            suffix = "Completed: " + trim(label.substr(close + 1)) + " - " +
                     format_task_elapsed(elapsed);
        }
        else
        {
            suffix = "[failed]";
        }
    }
    else
    {
        suffix = (rc == 0 ? "[ok]" : "[failed]");
    }
    spinner.stop(suffix);
    return rc;
}

static int run_status_command(const std::string& label, const std::string& cmd,
                              bool useSpinner, bool logCommand = true,
                              bool logChildOutput = true)
{
    if(useSpinner && !pkg_logs_active())
        return run_progress_command(label, cmd, logCommand, logChildOutput);
    std::cerr << shorten_progress_description(label) << std::endl;
    const auto start = std::chrono::steady_clock::now();
    int rc = run_command(cmd, logCommand, logChildOutput);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if(label.size() > 4 && label.front() == '[' &&
       label.find('/') != std::string::npos &&
       label.find(']') != std::string::npos && rc == 0)
    {
        const size_t close = label.find(']');
        std::cerr << label.substr(0, close + 1)
                  << " Completed: " << trim(label.substr(close + 1)) << " - "
                  << format_task_elapsed(elapsed) << std::endl;
    }
    else
    {
        std::cerr << label << (rc == 0 ? " [ok]" : " [failed]") << std::endl;
    }
    return rc;
}

static int stream_inline_output_command(ProgressSpinner& spinner,
                                        const std::string& cmd,
                                        bool logCommand = true,
                                        bool logChildOutput = true)
{
    pkg_info_line(cmd, logCommand);
    std::string fullCmd = cmd + " 2>&1";
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if(!pipe)
    {
        return 1;
    }

    std::vector<std::string> recentOutput;
    constexpr size_t maxRecentLines = 20;
    char buffer[512];
    while(fgets(buffer, sizeof(buffer), pipe))
    {
        const std::string line = sanitize_progress_output(buffer);
        if(line.empty())
            continue;
        spinner.set_detail(line);
        recentOutput.push_back(line);
        if(recentOutput.size() > maxRecentLines)
            recentOutput.erase(recentOutput.begin());
        if(logChildOutput)
            append_log_line(current_package_log_state().stdoutLog, line);
    }

    const int rc = pclose(pipe);
    if(rc != 0 && !recentOutput.empty())
    {
        spinner.stop("[failed]");
        pkg_error_line("Last command output:");
        for(const auto& line : recentOutput)
            pkg_error_line("  " + line);
    }
    return rc;
}

static std::string join_path_entries(const std::vector<std::string>& entries)
{
    std::string out;
    for(const auto& entry : entries)
    {
        if(entry.empty())
            continue;
        if(!out.empty())
            out += ":";
        out += entry;
    }
    return out;
}

static std::string
command_with_path_entries(const std::string& cmd,
                          const std::vector<std::string>& entries)
{
    const std::string joined = join_path_entries(entries);
    if(joined.empty())
        return cmd;

    std::string currentPath;
    if(const char* envPath = std::getenv("PATH"))
        currentPath = envPath;
    std::string fullPath = joined;
    if(!currentPath.empty())
    {
        if(!fullPath.empty())
            fullPath += ":";
        fullPath += currentPath;
    }
    return "env PATH=" + shell_quote(fullPath) + " " + cmd;
}

static int run_command_with_paths(const std::string& cmd,
                                  const std::vector<std::string>& entries)
{
    return run_command(command_with_path_entries(cmd, entries));
}

static int
run_progress_command_with_paths(const std::string& label,
                                const std::string& cmd,
                                const std::vector<std::string>& entries)
{
    return run_progress_command(label, command_with_path_entries(cmd, entries));
}

static int
run_status_command_with_paths(const std::string& label, const std::string& cmd,
                              const std::vector<std::string>& entries,
                              bool useSpinner, bool logCommand = true,
                              bool logChildOutput = true)
{
    return run_status_command(label, command_with_path_entries(cmd, entries),
                              useSpinner, logCommand, logChildOutput);
}

static int run_command_in_dir(const std::filesystem::path& dir,
                              const std::string& cmd)
{
    return run_command("cd " + shell_quote(dir.string()) + " && " + cmd);
}

static int run_command_in_dir_with_paths(
    const std::filesystem::path& dir, const std::string& cmd,
    const std::vector<std::string>& entries, bool logCommand = true,
    bool logChildOutput = true)
{
    return run_command("cd " + shell_quote(dir.string()) + " && " +
                           command_with_path_entries(cmd, entries),
                       logCommand, logChildOutput);
}

static int run_progress_command_in_dir_with_paths(
    const std::string& label, const std::filesystem::path& dir,
    const std::string& cmd, const std::vector<std::string>& entries)
{
    return run_progress_command(label,
                                "cd " + shell_quote(dir.string()) + " && " +
                                    command_with_path_entries(cmd, entries));
}

static int run_status_command_in_dir_with_paths(
    const std::string& label, const std::filesystem::path& dir,
    const std::string& cmd, const std::vector<std::string>& entries,
    bool useSpinner, bool logCommand = true, bool logChildOutput = true)
{
    return run_status_command(label,
                              "cd " + shell_quote(dir.string()) + " && " +
                                  command_with_path_entries(cmd, entries),
                              useSpinner, logCommand, logChildOutput);
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

static std::optional<std::string>
run_command_capture_with_stderr(const std::string& cmd)
{
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if(!pipe)
        return std::nullopt;
    std::string out;
    char buf[256];
    while(fgets(buf, sizeof(buf), pipe))
        out += buf;
    const int rc = pclose(pipe);
    if(rc != 0)
        return std::nullopt;
    return out;
}

static std::optional<std::string>
run_command_capture_with_paths(const std::string& cmd,
                               const std::vector<std::string>& entries)
{
    return run_command_capture(command_with_path_entries(cmd, entries));
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

static void append_shell_fragment(std::string& cmd, const std::string& fragment)
{
    const auto tokens = split_shell_tokens(fragment);
    if(tokens.empty())
    {
        cmd += " " + shell_quote(fragment);
        return;
    }
    for(const auto& token : tokens)
        cmd += " " + shell_quote(token);
}

static std::optional<std::string>
extract_toolchain_version(const std::string& output)
{
    for(size_t start = 0; start < output.size(); ++start)
    {
        if(!std::isdigit(static_cast<unsigned char>(output[start])))
            continue;
        size_t end = start;
        bool hasDot = false;
        while(end < output.size())
        {
            const char c = output[end];
            if(std::isdigit(static_cast<unsigned char>(c)))
            {
                ++end;
                continue;
            }
            if(c == '.')
            {
                hasDot = true;
                ++end;
                continue;
            }
            break;
        }
        while(end > start && output[end - 1] == '.')
            --end;
        if(!hasDot || end <= start)
            continue;
        const std::string candidate = output.substr(start, end - start);
        if(parse_semver_components(candidate).has_value())
            return candidate;
        start = end;
    }
    return std::nullopt;
}

static int validate_toolchain_requirements(const PackageManifest& pkg,
                                           const BuildConfig& buildConfig)
{
    const auto requirements = parse_toolchain_requirements(pkg.content);
    if(requirements.empty())
        return 0;

    const std::string hostName = current_host_name();
    bool failed = false;
    pkg_info_line("-- Checking dependency toolchains for " +
                  pkg.manifestPath.string());
    for(const auto& requirement : requirements)
    {
        if(!requirement.host.empty() && requirement.host != hostName)
            continue;

        const std::string displayName =
            requirement.name.empty() ? requirement.command : requirement.name;
        if(requirement.command.empty())
        {
            pkg_error_line("-- Invalid toolchain " + displayName +
                           ": missing command");
            failed = true;
            continue;
        }
        if(!requirement.minVersion.empty() &&
           !parse_semver_components(requirement.minVersion).has_value())
        {
            pkg_error_line("-- Invalid minimum version for " + displayName +
                           ": " + requirement.minVersion);
            failed = true;
            continue;
        }

        const auto executable = run_command_capture_with_paths(
            "command -v " + shell_quote(requirement.command),
            buildConfig.pathEntries);
        if(!executable.has_value() || trim(*executable).empty())
        {
            pkg_error_line("-- Missing " + displayName + ": command '" +
                           requirement.command + "' was not found in PATH");
            if(!requirement.install.empty())
                pkg_error_line("   Install: " + requirement.install);
            failed = true;
            continue;
        }

        const std::string executablePath = trim(*executable);
        if(requirement.minVersion.empty())
        {
            pkg_info_line("-- Found " + displayName + ": " + executablePath);
            continue;
        }

        std::string versionCommand = shell_quote(executablePath);
        append_shell_fragment(versionCommand, requirement.versionArgs);
        const auto versionOutput =
            run_command_capture_with_stderr(versionCommand);
        const auto version = versionOutput.has_value()
                                 ? extract_toolchain_version(*versionOutput)
                                 : std::nullopt;
        if(!version.has_value())
        {
            pkg_error_line("-- Found " + displayName + ": " + executablePath +
                           ", but its version could not be determined");
            if(!requirement.install.empty())
                pkg_error_line("   Install: " + requirement.install);
            failed = true;
            continue;
        }

        if(compare_semver(*version, requirement.minVersion) < 0)
        {
            pkg_error_line("-- Found " + displayName + ": " + executablePath +
                           " (version " + *version + ", requires >= " +
                           requirement.minVersion + ")");
            if(!requirement.install.empty())
                pkg_error_line("   Install or upgrade: " +
                               requirement.install);
            failed = true;
            continue;
        }
        pkg_info_line("-- Found " + displayName + ": " + executablePath +
                      " (version " + *version + ", requires >= " +
                      requirement.minVersion + ")");
    }

    if(failed)
    {
        pkg_error_line("Required dependency toolchains are missing or too old. "
                       "Install or upgrade them before continuing.");
        return 1;
    }
    pkg_info_line("-- Dependency toolchain check completed");
    return 0;
}

static std::filesystem::path
dep_checkout_dir(const std::filesystem::path& depsDir, const DepSpec& dep)
{
    return depsDir / dep.name;
}

static std::filesystem::path
dep_source_dir(const PackageManifest& pkg,
               const std::filesystem::path& depsDir, const DepSpec& dep)
{
    std::filesystem::path path;
    if(!dep.path.empty())
    {
        path = dep.path;
        if(path.is_relative())
            path = pkg.packageDir / path;
        path = std::filesystem::absolute(path).lexically_normal();
        if(path.filename() == "mlang.toml")
            path = path.parent_path();
    }
    else
    {
        path = dep_checkout_dir(depsDir, dep);
    }
    if(!dep.subdir.empty())
        path /= dep.subdir;
    return path;
}

static std::optional<PackageManifest>
load_dependency_manifest(const PackageManifest& owner, const DepSpec& dep,
                         const std::filesystem::path& depsDir,
                         bool required)
{
    namespace fs = std::filesystem;
    fs::path manifestPath = dep_source_dir(owner, depsDir, dep) / "mlang.toml";
    std::error_code ec;
    if(!fs::is_regular_file(manifestPath, ec))
    {
        if(required)
            pkg_error_line("MLang dependency '" + dep.name +
                           "' has no manifest at " + manifestPath.string());
        return std::nullopt;
    }
    manifestPath = fs::absolute(manifestPath).lexically_normal();
    std::ifstream in(manifestPath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if(content.empty() || !has_section(content, "package"))
    {
        pkg_error_line("Dependency manifest must declare [package]: " +
                       manifestPath.string());
        return std::nullopt;
    }
    const auto packageName =
        find_section_toml_string(content, "package", "name");
    const auto packageVersion =
        find_section_toml_string(content, "package", "version");
    if(!packageName.has_value() || packageName->empty() ||
       !packageVersion.has_value() || packageVersion->empty())
    {
        pkg_error_line("MLang dependency manifest requires package name and "
                       "version: " + manifestPath.string());
        return std::nullopt;
    }
    if(*packageName != dep.name)
    {
        pkg_error_line("Dependency key '" + dep.name +
                       "' does not match package name '" + *packageName +
                       "' in " + manifestPath.string());
        return std::nullopt;
    }
    if(!is_complete_semantic_version(*packageVersion))
    {
        pkg_error_line("Package '" + dep.name + "' has invalid semantic "
                       "version '" + *packageVersion + "'");
        return std::nullopt;
    }
    if(!dep.versionRequirement.empty() &&
       !semantic_version_satisfies(*packageVersion, dep.versionRequirement))
    {
        pkg_error_line("Package '" + dep.name + "' version " +
                       *packageVersion + " does not satisfy '" +
                       dep.versionRequirement + "' required by " +
                       owner.manifestPath.string());
        return std::nullopt;
    }
    return PackageManifest{manifestPath, manifestPath.parent_path(), content,
                           "", {}};
}

static void collect_transitive_link_flags_impl(
    const PackageManifest& pkg, const std::filesystem::path& depsDir,
    std::unordered_set<std::string>& visited,
    std::unordered_set<std::string>& libDirs,
    std::unordered_set<std::string>& libs,
    std::unordered_set<std::string>& staticArchives)
{
    for(const auto& dep : parse_source_deps(pkg.content))
    {
        const std::filesystem::path path = dep_source_dir(pkg, depsDir, dep);
        scan_lib_dir(path / "build" / "lib", libDirs, libs, staticArchives);
        scan_lib_dir(path / "build", libDirs, libs, staticArchives);
        scan_lib_dir(path / "lib", libDirs, libs, staticArchives);
        const auto child = load_dependency_manifest(pkg, dep, depsDir, false);
        if(!child.has_value())
            continue;
        const std::string key = child->manifestPath.string();
        if(!visited.insert(key).second)
            continue;
        const BuildConfig childConfig = parse_manifest_build_config(*child);
        collect_transitive_link_flags_impl(
            *child, package_deps_dir(child->packageDir, childConfig), visited,
            libDirs, libs, staticArchives);
    }
}

static LinkFlags collect_transitive_dep_link_flags(
    const PackageManifest& pkg, const std::filesystem::path& depsDir)
{
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> libDirs;
    std::unordered_set<std::string> libs;
    std::unordered_set<std::string> staticArchives;
    visited.insert(pkg.manifestPath.string());
    collect_transitive_link_flags_impl(pkg, depsDir, visited, libDirs, libs,
                                       staticArchives);
    LinkFlags flags;
    flags.libDirs.assign(libDirs.begin(), libDirs.end());
    flags.libs.assign(libs.begin(), libs.end());
    flags.staticArchives.assign(staticArchives.begin(), staticArchives.end());
    std::sort(flags.libDirs.begin(), flags.libDirs.end());
    std::sort(flags.libs.begin(), flags.libs.end());
    std::sort(flags.staticArchives.begin(), flags.staticArchives.end());
    return flags;
}

struct ExecutionProgressState
{
    size_t totalSteps = 0;
    std::atomic<size_t> nextStep{1};
    bool active = false;
};

static ExecutionProgressState& current_execution_progress_state()
{
    static ExecutionProgressState state;
    return state;
}

class ScopedExecutionProgressState
{
public:
    explicit ScopedExecutionProgressState(size_t totalSteps)
    {
        auto& current = current_execution_progress_state();
        previousTotalSteps_ = current.totalSteps;
        previousNextStep_ = current.nextStep.load();
        previousActive_ = current.active;
        current.totalSteps = totalSteps;
        current.nextStep.store(1);
        current.active = totalSteps > 0;
    }

    ~ScopedExecutionProgressState()
    {
        auto& current = current_execution_progress_state();
        current.totalSteps = previousTotalSteps_;
        current.nextStep.store(previousNextStep_);
        current.active = previousActive_;
    }

private:
    size_t previousTotalSteps_ = 0;
    size_t previousNextStep_ = 1;
    bool previousActive_ = false;
};

static std::string reserve_execution_step_prefix()
{
    auto& state = current_execution_progress_state();
    if(!state.active || state.totalSteps == 0)
        return "[pkg]";
    const size_t index = state.nextStep.fetch_add(1);
    std::ostringstream out;
    out << "[" << index << "/" << state.totalSteps << "]";
    return out.str();
}

static std::string execution_step_label(const std::string& description)
{
    return reserve_execution_step_prefix() + " " + description;
}

static std::string shorten_progress_description(const std::string& description)
{
    constexpr size_t maxLen = 72;
    if(description.size() <= maxLen)
        return description;
    return description.substr(0, maxLen - 3) + "...";
}

static std::string sanitize_progress_output(std::string text)
{
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
    for(char& c : text)
    {
        if(c == '\t')
            c = ' ';
    }
    return trim(text);
}

static std::string shorten_progress_output(const std::string& text)
{
    constexpr size_t maxLen = 56;
    if(text.size() <= maxLen)
        return text;
    return text.substr(0, maxLen - 3) + "...";
}

static size_t count_fetch_dep_steps(const DepSpec& dep,
                                    const std::filesystem::path& depsDir,
                                    bool updateExisting);
static size_t count_build_dep_steps(const PackageManifest& pkg,
                                    const DepSpec& dep,
                                    const std::filesystem::path& depsDir);
static size_t count_reachable_task_steps(const std::vector<TaskSpec>& tasks,
                                         const std::string& hostName,
                                         const std::vector<std::string>& roots);

static int fetch_git_dep(const PackageManifest& pkg, const DepSpec& dep,
                         const std::filesystem::path& depsDir,
                         bool updateExisting,
                         const std::vector<std::string>& pathEntries)
{
    DependencyLockContext* lockContext = current_dependency_lock_context();
    DependencyLockEntry* lockEntry = dependency_lock_entry(pkg, dep);
    const bool offline = lockContext != nullptr && lockContext->offline;
    std::filesystem::path path = dep_checkout_dir(depsDir, dep);
    if(!std::filesystem::exists(path))
    {
        if(offline)
        {
            pkg_error_line("Offline dependency missing from cache: " +
                           path.string());
            return 1;
        }
        std::string cloneCmd = "git clone " + shell_quote(dep.git) + " " +
                               shell_quote(path.string());
        if(run_status_command_with_paths(
               execution_step_label("Fetching git dependency '" + dep.name +
                                    "' from " + dep.git),
               cloneCmd, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
            return 1;
    }
    else if(lockEntry == nullptr && !offline &&
            (updateExisting || (lockContext != nullptr && lockContext->dirty)))
    {
        std::string fetchCmd =
            "git -C " + shell_quote(path.string()) + " fetch --all --tags";
        if(run_status_command_with_paths(
               execution_step_label("Updating git dependency '" + dep.name +
                                    "'"),
               fetchCmd, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
            return 1;
    }

    auto origin = run_command_capture_with_paths(
        "git -C " + shell_quote(path.string()) +
            " config --get remote.origin.url",
        pathEntries);
    if(!origin.has_value() || trim(*origin) != dep.git)
    {
        pkg_error_line("Git dependency '" + dep.name +
                       "' has a different origin URL in " + path.string() +
                       "; clean its dependency cache before continuing");
        return 1;
    }

    const std::string requestedRevision =
        lockEntry != nullptr ? lockEntry->revision : dep.rev;
    const std::string requestedTag =
        lockEntry != nullptr ? "" : dep.tag;
    if(!requestedRevision.empty())
    {
        std::string checkout = "git -C " + shell_quote(path.string()) +
                               " checkout --detach " +
                               shell_quote(requestedRevision);
        if(run_status_command_with_paths(
               execution_step_label("Checking out '" + dep.name +
                                    "' revision " + requestedRevision),
               checkout, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
            return 1;
    }
    else if(!requestedTag.empty())
    {
        std::string checkout = "git -C " + shell_quote(path.string()) +
                               " checkout --detach " +
                               shell_quote("tags/" + requestedTag);
        if(run_status_command_with_paths(
               execution_step_label("Checking out '" + dep.name + "' tag " +
                                    requestedTag),
               checkout, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
            return 1;
    }
    else if(lockEntry == nullptr)
    {
        std::string checkout = "git -C " + shell_quote(path.string()) +
                               " checkout --detach origin/HEAD";
        if(run_status_command_with_paths(
               execution_step_label("Pinning '" + dep.name +
                                    "' to the remote default revision"),
               checkout, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
            return 1;
    }

    if(dep.submodules)
    {
        std::string updateSubmodulesCmd =
            "git -C " + shell_quote(path.string()) +
            " submodule update --init --recursive";
        if(offline)
            updateSubmodulesCmd += " --no-fetch";
        if(run_status_command_with_paths(
               execution_step_label("Initializing submodules for '" + dep.name +
                                    "'"),
               updateSubmodulesCmd, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
        {
            return 1;
        }
    }

    auto resolved = run_command_capture_with_paths(
        "git -C " + shell_quote(path.string()) + " rev-parse HEAD",
        pathEntries);
    if(!resolved.has_value() || trim(*resolved).empty())
    {
        pkg_error_line("Failed to resolve exact Git revision for dependency '" +
                       dep.name + "'");
        return 1;
    }
    const std::string revision = trim(*resolved);
    if(lockEntry != nullptr && revision != lockEntry->revision)
    {
        pkg_error_line("Git dependency '" + dep.name + "' resolved to " +
                       revision + ", expected locked revision " +
                       lockEntry->revision);
        return 1;
    }
    record_dependency_lock_entry(pkg, dep, revision, "");
    return 0;
}

static int fetch_archive_dep(const PackageManifest& pkg, const DepSpec& dep,
                             const std::filesystem::path& depsDir,
                             const std::vector<std::string>& pathEntries)
{
    if(dep.url.empty())
        return 1;
    if(dep.archiveType != "tar.gz")
    {
        pkg_error_line("Unsupported archive type for dependency '" + dep.name +
                       "': " + dep.archiveType);
        return 1;
    }

    DependencyLockContext* lockContext = current_dependency_lock_context();
    DependencyLockEntry* lockEntry = dependency_lock_entry(pkg, dep);
    const bool offline = lockContext != nullptr && lockContext->offline;
    const std::string expectedChecksum =
        lockEntry == nullptr ? "" : lockEntry->checksum;
    std::filesystem::path checkoutDir = dep_checkout_dir(depsDir, dep);
    std::error_code ec;
    const std::filesystem::path archiveDir = depsDir / ".archives";
    const std::filesystem::path archivePath =
        archiveDir / (sanitize_package_name(dep.name) + ".tar.gz");
    std::optional<std::string> checksum;
    if(std::filesystem::exists(archivePath, ec))
        checksum = sha256_file(archivePath);
    const bool cacheMatches =
        checksum.has_value() &&
        !(lockEntry == nullptr && lockContext != nullptr &&
          lockContext->dirty) &&
        (expectedChecksum.empty() || *checksum == expectedChecksum);

    if(!cacheMatches)
    {
        if(offline)
        {
            pkg_error_line("Offline archive missing or checksum-mismatched for "
                           "dependency '" +
                           dep.name + "': " + archivePath.string());
            return 1;
        }
        std::filesystem::create_directories(archiveDir, ec);
        std::string downloadCmd = "curl -L --fail " + shell_quote(dep.url) +
                                  " -o " + shell_quote(archivePath.string());
        if(run_status_command_with_paths(
               execution_step_label("Downloading archive dependency '" +
                                    dep.name + "' from " + dep.url),
               downloadCmd, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
            return 1;
        checksum = sha256_file(archivePath);
    }
    if(!checksum.has_value())
    {
        pkg_error_line("Failed to compute SHA-256 for dependency '" + dep.name +
                       "'");
        return 1;
    }
    if(!expectedChecksum.empty() && *checksum != expectedChecksum)
    {
        pkg_error_line("Checksum mismatch for dependency '" + dep.name +
                       "': expected sha256:" + expectedChecksum +
                       ", got sha256:" + *checksum);
        return 1;
    }

    const std::filesystem::path markerPath =
        checkoutDir / ".mlang-source.sha256";
    std::string marker;
    if(std::filesystem::exists(markerPath, ec))
    {
        std::ifstream markerIn(markerPath);
        std::getline(markerIn, marker);
        marker = trim(marker);
    }
    if(std::filesystem::exists(checkoutDir, ec) && marker == *checksum)
    {
        record_dependency_lock_entry(pkg, dep, "", *checksum);
        return 0;
    }

    std::filesystem::path extractDir = depsDir / (dep.name + ".extracting");
    std::filesystem::remove_all(extractDir, ec);
    std::filesystem::remove_all(checkoutDir, ec);
    std::filesystem::create_directories(extractDir);

    std::string extractCmd = "tar -xzf " + shell_quote(archivePath.string()) +
                             " -C " + shell_quote(extractDir.string());
    if(dep.stripComponents > 0)
    {
        extractCmd +=
            " --strip-components=" + std::to_string(dep.stripComponents);
    }
    if(run_status_command_with_paths(
           execution_step_label("Unpacking " + archivePath.filename().string() +
                                " into " + checkoutDir.string()),
           extractCmd, pathEntries, true, true, true) != 0)
        return 1;
    std::filesystem::rename(extractDir, checkoutDir, ec);
    if(ec)
    {
        pkg_error_line("Failed to finalize archive checkout for '" + dep.name +
                       "': " + ec.message());
        return 1;
    }
    std::ofstream markerOut(checkoutDir / ".mlang-source.sha256",
                            std::ios::binary | std::ios::trunc);
    if(!markerOut)
    {
        pkg_error_line("Failed to write archive checksum marker for '" +
                       dep.name + "'");
        return 1;
    }
    markerOut << *checksum << "\n";
    record_dependency_lock_entry(pkg, dep, "", *checksum);
    return 0;
}

static int fetch_dep(const PackageManifest& pkg, const DepSpec& dep,
                     const std::filesystem::path& depsDir,
                     bool updateExisting,
                     const std::vector<std::string>& pathEntries)
{
    const int sourceCount = static_cast<int>(!dep.path.empty()) +
                            static_cast<int>(!dep.git.empty()) +
                            static_cast<int>(!dep.url.empty());
    if(sourceCount != 1)
    {
        pkg_error_line("Dependency '" + dep.name +
                       "' must select exactly one of path, git, or url");
        return 1;
    }
    if(dep.git.empty() && (!dep.rev.empty() || !dep.tag.empty()))
    {
        pkg_error_line("Dependency '" + dep.name +
                       "' uses rev/tag without a Git source");
        return 1;
    }
    if(!dep.rev.empty() && !dep.tag.empty())
    {
        pkg_error_line("Git dependency '" + dep.name +
                       "' cannot select both rev and tag");
        return 1;
    }
    if(auto* context = current_dependency_lock_context(); context != nullptr)
    {
        DependencyLockEntry* entry = dependency_lock_entry(pkg, dep);
        const DependencyLockEntry expected =
            make_dependency_lock_entry(*context, pkg, dep);
        if(entry == nullptr && (context->locked || context->offline))
        {
            pkg_error_line("Transitive dependency '" + dep.name +
                           "' is missing from mlang.lock; rerun 'mlang pkg "
                           "lock'");
            return 1;
        }
        if(entry != nullptr &&
           !lock_entry_matches_dependency(*entry, expected))
        {
            if(context->locked || context->offline)
            {
                pkg_error_line("mlang.lock is out of date for transitive "
                               "dependency '" + dep.name +
                               "'; rerun 'mlang pkg lock'");
                return 1;
            }
            const std::string key = dependency_lock_key(expected.manifest,
                                                        expected.name);
            context->entries.erase(key);
            context->dirty = true;
        }
    }
    if(!dep.path.empty())
    {
        const auto child = load_dependency_manifest(pkg, dep, depsDir, true);
        if(!child.has_value())
            return 1;
        const std::string resolvedVersion =
            *find_section_toml_string(child->content, "package", "version");
        if(auto* lockedEntry = dependency_lock_entry(pkg, dep);
           lockedEntry != nullptr && !lockedEntry->resolvedVersion.empty() &&
           lockedEntry->resolvedVersion != resolvedVersion)
        {
            DependencyLockContext* context =
                current_dependency_lock_context();
            if(context != nullptr && (context->locked || context->offline))
            {
                pkg_error_line("Path dependency '" + dep.name +
                               "' changed version: locked " +
                               lockedEntry->resolvedVersion + ", found " +
                               resolvedVersion);
                return 1;
            }
        }
        if(auto* context = current_dependency_lock_context())
        {
            const std::string source =
                "path:" + child->manifestPath.string();
            const auto found = context->packageSources.find(dep.name);
            if(found != context->packageSources.end() && found->second != source)
            {
                pkg_error_line("Dependency source conflict for package '" +
                               dep.name + "': " + found->second + " and " +
                               source);
                return 1;
            }
            context->packageSources[dep.name] = source;
        }
        record_dependency_lock_entry(pkg, dep, "", "", resolvedVersion);
        pkg_info_line("Using path dependency " + dep.name + " v" +
                      resolvedVersion + " from " + child->packageDir.string());
        return 0;
    }
    if(!dep.git.empty())
        return fetch_git_dep(pkg, dep, depsDir, updateExisting, pathEntries);
    if(!dep.url.empty())
        return fetch_archive_dep(pkg, dep, depsDir, pathEntries);
    pkg_error_line("Dependency '" + dep.name +
                   "' is missing a supported source (path/git/url)");
    return 1;
}

static int verify_dependency(const PackageManifest& pkg, const DepSpec& dep,
                             const std::filesystem::path& depsDir,
                             const std::vector<std::string>& pathEntries)
{
    DependencyLockEntry* entry = dependency_lock_entry(pkg, dep);
    if(entry == nullptr)
    {
        pkg_error_line("Dependency '" + dep.name +
                       "' is missing from mlang.lock");
        return 1;
    }
    if(auto* context = current_dependency_lock_context())
    {
        const DependencyLockEntry expected =
            make_dependency_lock_entry(*context, pkg, dep);
        if(!lock_entry_matches_dependency(*entry, expected))
        {
            pkg_error_line("mlang.lock is out of date for dependency '" +
                           dep.name + "'");
            return 1;
        }
    }
    if(entry->source == "path")
    {
        const auto child = load_dependency_manifest(pkg, dep, depsDir, true);
        if(!child.has_value())
            return 1;
        const std::string version =
            *find_section_toml_string(child->content, "package", "version");
        if(version != entry->resolvedVersion)
        {
            pkg_error_line("Path dependency '" + dep.name +
                           "' changed version: expected " +
                           entry->resolvedVersion + ", got " + version);
            return 1;
        }
        pkg_info_line("Verified path dependency " + dep.name + " v" +
                      version);
        return 0;
    }
    const std::filesystem::path checkoutDir = dep_checkout_dir(depsDir, dep);
    if(!std::filesystem::exists(checkoutDir))
    {
        pkg_error_line("Fetched dependency is missing: " +
                       checkoutDir.string());
        return 1;
    }

    if(entry->source == "git")
    {
        auto origin = run_command_capture_with_paths(
            "git -C " + shell_quote(checkoutDir.string()) +
                " config --get remote.origin.url",
            pathEntries);
        if(!origin.has_value() || trim(*origin) != entry->url)
        {
            pkg_error_line("Git verification failed for '" + dep.name +
                           "': origin URL does not match mlang.lock");
            return 1;
        }
        auto head = run_command_capture_with_paths(
            "git -C " + shell_quote(checkoutDir.string()) +
                " rev-parse HEAD",
            pathEntries);
        if(!head.has_value() || trim(*head) != entry->revision)
        {
            pkg_error_line("Git verification failed for '" + dep.name +
                           "': expected revision " + entry->revision +
                           (head.has_value() ? ", got " + trim(*head)
                                             : ", checkout is unreadable"));
            return 1;
        }
        if(entry->submodules)
        {
            auto status = run_command_capture_with_paths(
                "git -C " + shell_quote(checkoutDir.string()) +
                    " submodule status --recursive",
                pathEntries);
            if(!status.has_value())
            {
                pkg_error_line("Git submodule verification failed for '" +
                               dep.name + "'");
                return 1;
            }
            std::istringstream lines(*status);
            std::string line;
            while(std::getline(lines, line))
            {
                if(!line.empty() && line.front() != ' ')
                {
                    pkg_error_line("Git submodule is missing or does not match "
                                   "the locked tree for '" +
                                   dep.name + "': " + trim(line));
                    return 1;
                }
            }
        }
        pkg_info_line("Verified " + dep.name + " at Git revision " +
                      entry->revision);
        return 0;
    }

    const std::filesystem::path archivePath =
        depsDir / ".archives" /
        (sanitize_package_name(dep.name) + ".tar.gz");
    const auto checksum = sha256_file(archivePath);
    if(!checksum.has_value() || *checksum != entry->checksum)
    {
        pkg_error_line("Archive verification failed for '" + dep.name +
                       "': expected sha256:" + entry->checksum +
                       (checksum.has_value() ? ", got sha256:" + *checksum
                                             : ", cached archive is missing"));
        return 1;
    }
    std::ifstream markerIn(checkoutDir / ".mlang-source.sha256");
    std::string marker;
    std::getline(markerIn, marker);
    if(trim(marker) != entry->checksum)
    {
        pkg_error_line("Extracted archive verification marker is missing or "
                       "stale for '" +
                       dep.name + "'");
        return 1;
    }
    pkg_info_line("Verified " + dep.name + " archive sha256:" +
                  entry->checksum);
    return 0;
}

static int verify_manifest_dependencies(const PackageManifest& pkg,
                                        const BuildConfig& buildConfig)
{
    DependencyLockContext* traversal = current_dependency_lock_context();
    const std::string manifestKey =
        std::filesystem::absolute(pkg.manifestPath).lexically_normal().string();
    if(traversal != nullptr)
    {
        if(traversal->verifiedManifests.count(manifestKey) != 0)
            return 0;
        if(!traversal->activeVerifyManifests.insert(manifestKey).second)
        {
            pkg_error_line("Transitive dependency cycle reaches " +
                           pkg.manifestPath.string());
            return 1;
        }
    }
    const auto finishTraversal = [&](bool success)
    {
        if(traversal == nullptr)
            return;
        traversal->activeVerifyManifests.erase(manifestKey);
        if(success)
            traversal->verifiedManifests.insert(manifestKey);
    };
    const BuildConfig effectiveBuildConfig =
        materialize_build_config_for_package(pkg, buildConfig);
    const std::filesystem::path depsDir =
        package_deps_dir(pkg.packageDir, effectiveBuildConfig);
    for(const auto& dep : parse_source_deps(pkg.content))
    {
        if(verify_dependency(pkg, dep, depsDir,
                             effectiveBuildConfig.pathEntries) != 0)
        {
            finishTraversal(false);
            return 1;
        }
        const bool requiresManifest = !dep.path.empty() || dep.build == "mlang";
        const auto child =
            load_dependency_manifest(pkg, dep, depsDir, requiresManifest);
        if(!child.has_value())
        {
            if(requiresManifest)
            {
                finishTraversal(false);
                return 1;
            }
            continue;
        }
        const std::string version =
            *find_section_toml_string(child->content, "package", "version");
        DependencyLockEntry* entry = dependency_lock_entry(pkg, dep);
        if(entry == nullptr || entry->resolvedVersion != version)
        {
            pkg_error_line("Resolved version verification failed for '" +
                           dep.name + "': expected " +
                           (entry == nullptr ? std::string("<missing>")
                                             : entry->resolvedVersion) +
                           ", got " + version);
            finishTraversal(false);
            return 1;
        }
        BuildConfig childBuildConfig = parse_manifest_build_config(*child);
        childBuildConfig.compilerProgram = effectiveBuildConfig.compilerProgram;
        if(verify_manifest_dependencies(*child, childBuildConfig) != 0)
        {
            finishTraversal(false);
            return 1;
        }
    }
    finishTraversal(true);
    return 0;
}

static std::string package_name(const PackageManifest& pkg)
{
    return find_section_toml_string(pkg.content, "package", "name")
        .value_or(pkg.packageDir.filename().string());
}

static std::string package_version(const PackageManifest& pkg)
{
    return find_section_toml_string(pkg.content, "package", "version")
        .value_or("?");
}

static std::string dependency_source_label(const DepSpec& dep)
{
    if(!dep.path.empty())
        return "path " + dep.path;
    if(!dep.git.empty())
        return "git " + dep.git;
    return "archive " + dep.url;
}

static int print_dependency_tree_recursive(
    const PackageManifest& pkg, const std::string& prefix,
    std::unordered_set<std::string>& expanded,
    std::unordered_set<std::string>& active)
{
    const std::string key =
        std::filesystem::absolute(pkg.manifestPath).lexically_normal().string();
    active.insert(key);
    const auto deps = parse_source_deps(pkg.content);
    const BuildConfig config = parse_manifest_build_config(pkg);
    const std::filesystem::path depsDir =
        package_deps_dir(pkg.packageDir, config);
    for(size_t i = 0; i < deps.size(); ++i)
    {
        const DepSpec& dep = deps[i];
        const bool last = i + 1 == deps.size();
        std::cout << prefix << (last ? "`-- " : "|-- ") << dep.name;
        if(!dep.versionRequirement.empty())
            std::cout << " " << dep.versionRequirement;
        std::cout << " (" << dependency_source_label(dep) << ")";
        const auto child = load_dependency_manifest(
            pkg, dep, depsDir, !dep.path.empty() || dep.build == "mlang");
        if(!child.has_value())
        {
            if(!dep.path.empty() || dep.build == "mlang")
            {
                std::cout << " [invalid]\n";
                active.erase(key);
                return 1;
            }
            std::cout << " [not fetched]\n";
            continue;
        }
        std::cout << " => v" << package_version(*child);
        const std::string childKey = std::filesystem::absolute(
                                         child->manifestPath)
                                         .lexically_normal()
                                         .string();
        if(active.count(childKey) != 0)
        {
            std::cout << " [cycle]\n";
            active.erase(key);
            return 1;
        }
        if(expanded.count(childKey) != 0)
        {
            std::cout << " (*)\n";
            continue;
        }
        std::cout << "\n";
        expanded.insert(childKey);
        if(print_dependency_tree_recursive(
               *child, prefix + (last ? "    " : "|   "), expanded,
               active) != 0)
        {
            active.erase(key);
            return 1;
        }
    }
    active.erase(key);
    return 0;
}

static bool find_dependency_paths_recursive(
    const PackageManifest& pkg, const std::string& wanted,
    std::vector<std::string>& chain,
    std::unordered_set<std::string>& active,
    std::vector<std::vector<std::string>>& matches)
{
    const std::string key =
        std::filesystem::absolute(pkg.manifestPath).lexically_normal().string();
    if(!active.insert(key).second)
        return false;
    bool foundAny = false;
    const BuildConfig config = parse_manifest_build_config(pkg);
    const std::filesystem::path depsDir =
        package_deps_dir(pkg.packageDir, config);
    for(const auto& dep : parse_source_deps(pkg.content))
    {
        chain.push_back(dep.name);
        if(dep.name == wanted)
        {
            matches.push_back(chain);
            foundAny = true;
        }
        const auto child = load_dependency_manifest(
            pkg, dep, depsDir, !dep.path.empty() || dep.build == "mlang");
        if(child.has_value() &&
           find_dependency_paths_recursive(*child, wanted, chain, active,
                                           matches))
            foundAny = true;
        chain.pop_back();
    }
    active.erase(key);
    return foundAny;
}

static int print_dependency_tree(const std::vector<PackageManifest>& manifests)
{
    for(size_t i = 0; i < manifests.size(); ++i)
    {
        const auto& pkg = manifests[i];
        if(i != 0)
            std::cout << "\n";
        std::cout << package_name(pkg) << " v" << package_version(pkg) << "\n";
        std::unordered_set<std::string> expanded;
        std::unordered_set<std::string> active;
        expanded.insert(std::filesystem::absolute(pkg.manifestPath)
                            .lexically_normal()
                            .string());
        if(print_dependency_tree_recursive(pkg, "", expanded, active) != 0)
            return 1;
    }
    return 0;
}

static int print_dependency_why(const std::vector<PackageManifest>& manifests,
                                const std::string& wanted)
{
    bool found = false;
    for(const auto& pkg : manifests)
    {
        std::vector<std::string> chain{package_name(pkg)};
        std::unordered_set<std::string> active;
        std::vector<std::vector<std::string>> matches;
        find_dependency_paths_recursive(pkg, wanted, chain, active, matches);
        for(const auto& match : matches)
        {
            found = true;
            for(size_t i = 0; i < match.size(); ++i)
            {
                if(i != 0)
                    std::cout << " -> ";
                std::cout << match[i];
            }
            std::cout << "\n";
        }
    }
    if(!found)
    {
        pkg_error_line("Package '" + wanted +
                       "' is not reachable from the selected manifest");
        return 1;
    }
    return 0;
}

static int build_git_dep(const PackageManifest& pkg, const DepSpec& dep,
                         const std::filesystem::path& depsDir, bool useNinja,
                         const std::string& makeProgram,
                         const std::vector<std::string>& pathEntries)
{
    std::filesystem::path path = dep_source_dir(pkg, depsDir, dep);
    if(!std::filesystem::exists(path))
        return 1;

    if(dep.build == "none" || dep.build == "skip")
        return 0;
    if(dep.build == "mlang")
        return 0; // Built recursively by build_for_manifest.

    if(dep.build == "cmake")
    {
        std::filesystem::path buildDir = path / "build";
        std::string cfg = "cmake -S " + shell_quote(path.string()) + " -B " +
                          shell_quote(buildDir.string());
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
        if(run_status_command_with_paths(
               execution_step_label("Configuring CMake dependency '" +
                                    dep.name + "' in " + buildDir.string()),
               cfg, pathEntries, dep.spinner, dep.spinner, dep.spinner) != 0)
            return 1;
        std::string build = "cmake --build " + shell_quote(buildDir.string());
        return run_status_command_with_paths(
            execution_step_label("Building CMake dependency '" + dep.name +
                                 "'"),
            build, pathEntries, dep.spinner, dep.spinner, dep.spinner);
    }
    if(dep.build == "meson")
    {
        std::filesystem::path buildDir = path / "build";
        if(!std::filesystem::exists(buildDir))
        {
            std::string setup = "meson setup " +
                                shell_quote(buildDir.string()) + " " +
                                shell_quote(path.string());
            if(run_status_command_with_paths(
                   execution_step_label("Configuring Meson dependency '" +
                                        dep.name + "' in " + buildDir.string()),
                   setup, pathEntries, dep.spinner, dep.spinner,
                   dep.spinner) != 0)
                return 1;
        }
        std::string compile =
            "meson compile -C " + shell_quote(buildDir.string());
        return run_status_command_with_paths(
            execution_step_label("Building Meson dependency '" + dep.name +
                                 "'"),
            compile, pathEntries, dep.spinner, dep.spinner, dep.spinner);
    }
    if(dep.build == "make")
    {
        std::string effectiveMake = makeProgram.empty() ? "make" : makeProgram;
        std::string cmd =
            shell_quote(effectiveMake) + " -C " + shell_quote(path.string());
        return run_status_command_with_paths(
            execution_step_label("Building make dependency '" + dep.name + "'"),
            cmd, pathEntries, dep.spinner, dep.spinner, dep.spinner);
    }

    pkg_error_line("Unknown build system: " + dep.build);
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
        std::string t = strip_toml_comment(line);
        if(t.empty())
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
            else if(auto it = kv.find("system");
                    it != kv.end() &&
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
                                    std::vector<std::string>& outFlags,
                                    const std::vector<std::string>& pathEntries)
{
    if(!dep.usePkgConfig || dep.pkgConfig.empty())
        return true;

    auto pkgConfigPath = run_command_capture_with_paths(
        "command -v pkg-config", pathEntries);
    if(!pkgConfigPath.has_value() || trim(*pkgConfigPath).empty())
    {
        std::cerr << "pkg-config executable not found while resolving: "
                  << dep.pkgConfig << "\n"
                  << "Install pkg-config (Homebrew: brew install pkg-config) "
                     "and ensure it is available in PATH.\n";
        return false;
    }

    std::string cmd = "pkg-config --cflags --libs " + dep.pkgConfig;
    auto result = run_command_capture_with_paths(cmd, pathEntries);
    if(!result.has_value())
    {
        std::cerr << "pkg-config could not resolve: " << dep.pkgConfig << "\n"
                  << "Install the library's development package or add the "
                     "directory containing its .pc file to PKG_CONFIG_PATH.\n";
        return false;
    }
    for(const auto& token : split_shell_tokens(result.value()))
        outFlags.push_back(token);
    return true;
}

static bool ensure_ninja_available(const std::vector<std::string>& pathEntries)
{
    auto ninjaPath = run_command_capture_with_paths(
        "command -v ninja || command -v ninja-build", pathEntries);
    if(ninjaPath.has_value() && !trim(*ninjaPath).empty())
        return true;
    std::cerr << "Ninja build requested, but neither 'ninja' nor 'ninja-build'"
              << " was found in PATH.\n";
    return false;
}

static bool validate_declared_libraries(
    const std::filesystem::path& manifestPath, const std::string& targetName,
    const BuildConfig& buildConfig, const LinkFlags& linkFlags)
{
    if(buildConfig.libs.empty())
        return true;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tempDir = fs::temp_directory_path(ec);
    if(ec)
        tempDir = fs::current_path();
    const std::string uniqueId =
        std::to_string(static_cast<unsigned long long>(std::time(nullptr))) +
        "_" + std::to_string(static_cast<unsigned long long>(std::rand()));
    fs::path srcPath = tempDir / ("mlang_pkg_libcheck_" + uniqueId + ".cpp");
    fs::path binPath = tempDir / ("mlang_pkg_libcheck_" + uniqueId);

    {
        std::ofstream out(srcPath, std::ios::binary);
        if(!out)
        {
            std::cerr
                << "Failed to create temporary file for library validation"
                << " while checking " << manifestPath.string() << "\n";
            return false;
        }
        out << "int main(){return 0;}\n";
    }

    std::vector<std::string> searchDirs = buildConfig.libPaths;
    searchDirs.insert(searchDirs.end(), linkFlags.libDirs.begin(),
                      linkFlags.libDirs.end());

    for(const auto& lib : buildConfig.libs)
    {
        std::string cmd = "c++ " + shell_quote(srcPath.string()) + " -o " +
                          shell_quote(binPath.string());
        for(const auto& dir : searchDirs)
            cmd += " -L" + shell_quote(dir);
        cmd += " -l" + shell_quote(lib) + " >/dev/null 2>&1";
        cmd = command_with_path_entries(cmd, buildConfig.pathEntries);
        int rc = std::system(cmd.c_str());
        if(rc != 0)
        {
            std::cerr << "Declared library '-l" << lib
                      << "' could not be linked";
            if(!targetName.empty())
                std::cerr << " for target '" << targetName << "'";
            std::cerr << " in " << manifestPath.string();
            if(!searchDirs.empty())
            {
                std::cerr << " using search paths:";
                for(const auto& dir : searchDirs)
                    std::cerr << " " << dir;
            }
            std::cerr << "\n";
            std::filesystem::remove(srcPath, ec);
            std::filesystem::remove(binPath, ec);
            return false;
        }
    }

    std::filesystem::remove(srcPath, ec);
    std::filesystem::remove(binPath, ec);
    return true;
}

static int
validate_mlang_version_requirement(const std::filesystem::path& manifestPath,
                                   const BuildConfig& buildConfig,
                                   const std::string& targetName)
{
    if(buildConfig.minMlangVersion.empty())
        return 0;
    if(!parse_semver_components(buildConfig.minMlangVersion).has_value())
    {
        std::cerr << "Invalid [tool.mlang].min_mlang_version";
        if(!targetName.empty())
            std::cerr << " for [[bin]] target '" << targetName << "'";
        std::cerr << " in " << manifestPath.string() << ": "
                  << buildConfig.minMlangVersion << "\n";
        return 1;
    }
    if(!parse_semver_components(MLANG_VERSION).has_value())
    {
        std::cerr << "Current mlang version is not semver-compatible: "
                  << MLANG_VERSION << "\n";
        return 1;
    }
    if(compare_semver(MLANG_VERSION, buildConfig.minMlangVersion) < 0)
    {
        std::cerr << "Package " << manifestPath.string();
        if(!targetName.empty())
            std::cerr << " target '" << targetName << "'";
        std::cerr << " requires mlang >= " << buildConfig.minMlangVersion
                  << ", but current version is " << MLANG_VERSION << "\n";
        return 1;
    }
    return 0;
}

static int fetch_for_manifest(const PackageManifest& pkg,
                              const BuildConfig& buildConfig)
{
    DependencyLockContext* traversal = current_dependency_lock_context();
    const std::string manifestKey =
        std::filesystem::absolute(pkg.manifestPath).lexically_normal().string();
    if(traversal != nullptr)
    {
        if(traversal->fetchedManifests.count(manifestKey) != 0)
            return 0;
        if(!traversal->activeFetchManifests.insert(manifestKey).second)
        {
            pkg_error_line("Transitive dependency cycle reaches " +
                           pkg.manifestPath.string());
            return 1;
        }
    }
    const auto finishTraversal = [&](bool success)
    {
        if(traversal == nullptr)
            return;
        traversal->activeFetchManifests.erase(manifestKey);
        if(success)
            traversal->fetchedManifests.insert(manifestKey);
    };
    const BuildConfig effectiveBuildConfig =
        materialize_build_config_for_package(pkg, buildConfig);
    if(validate_toolchain_requirements(pkg, effectiveBuildConfig) != 0)
    {
        finishTraversal(false);
        return 1;
    }
    ScopedPackageLogState scopedLogs(
        make_package_log_state(pkg.packageDir, effectiveBuildConfig));
    auto deps = parse_source_deps(pkg.content);
    std::filesystem::path depsDir =
        package_deps_dir(pkg.packageDir, effectiveBuildConfig);
    std::filesystem::create_directories(depsDir);
    const bool ownProgress = !current_execution_progress_state().active;
    size_t totalSteps = 0;
    if(ownProgress)
    {
        for(const auto& dep : deps)
        {
            totalSteps +=
                count_fetch_dep_steps(dep, depsDir, /*updateExisting=*/true);
        }
    }
    std::optional<ScopedExecutionProgressState> scopedProgress;
    if(ownProgress)
        scopedProgress.emplace(totalSteps);
    for(const auto& dep : deps)
    {
        if(fetch_dep(pkg, dep, depsDir, /*updateExisting=*/true,
                     effectiveBuildConfig.pathEntries) != 0)
        {
            finishTraversal(false);
            return 1;
        }
        const bool requiresManifest = !dep.path.empty() || dep.build == "mlang";
        const auto child =
            load_dependency_manifest(pkg, dep, depsDir, requiresManifest);
        if(!child.has_value())
        {
            if(requiresManifest)
            {
                finishTraversal(false);
                return 1;
            }
            continue;
        }
        const std::string childVersion =
            *find_section_toml_string(child->content, "package", "version");
        if(auto* entry = dependency_lock_entry(pkg, dep))
        {
            if(!entry->resolvedVersion.empty() &&
               entry->resolvedVersion != childVersion)
            {
                DependencyLockContext* context =
                    current_dependency_lock_context();
                if(context != nullptr &&
                   (context->locked || context->offline))
                {
                    pkg_error_line("MLang dependency '" + dep.name +
                                   "' changed version: locked " +
                                   entry->resolvedVersion + ", found " +
                                   childVersion);
                    finishTraversal(false);
                    return 1;
                }
            }
            record_dependency_lock_entry(pkg, dep, entry->revision,
                                         entry->checksum, childVersion);
        }
        if(traversal != nullptr)
        {
            std::string sourceIdentity;
            if(!dep.path.empty())
                sourceIdentity = "path:" + child->manifestPath.string();
            else if(!dep.git.empty())
                sourceIdentity = "git:" + dep.git;
            else
                sourceIdentity = "archive:" + dep.url;
            const auto found = traversal->packageSources.find(dep.name);
            if(found != traversal->packageSources.end() &&
               found->second != sourceIdentity)
            {
                pkg_error_line("Dependency source conflict for package '" +
                               dep.name + "': " + found->second + " and " +
                               sourceIdentity);
                finishTraversal(false);
                return 1;
            }
            traversal->packageSources[dep.name] = sourceIdentity;
        }
        BuildConfig childBuildConfig = parse_manifest_build_config(*child);
        childBuildConfig.compilerProgram = effectiveBuildConfig.compilerProgram;
        if(fetch_for_manifest(*child, childBuildConfig) != 0)
        {
            finishTraversal(false);
            return 1;
        }
    }
    pkg_info_line("Fetch completed for " + pkg.manifestPath.string() + ".");
    finishTraversal(true);
    return 0;
}

static std::string
default_task_compiler_for_language(std::string_view language,
                                   const BuildConfig& buildConfig)
{
    if(language == "mlang")
    {
        if(!buildConfig.compilerProgram.empty())
            return buildConfig.compilerProgram;
        return "mlang";
    }
    if(language == "c")
        return "cc";
    if(language == "c++")
        return "c++";
    return "";
}

static std::optional<std::string> build_task_language_command(
    const PackageManifest& pkg, const std::string& taskName,
    const BuildConfig& taskBuildConfig, const std::string& language,
    const std::string& source, const std::string& output,
    const std::vector<std::string>& inputs, bool compileOnly,
    const LinkFlags& linkFlags, const std::vector<std::string>& pkgFlags,
    std::string& error)
{
    if(language.empty())
        return std::nullopt;
    if(output.empty())
    {
        error =
            "Task '" + taskName + "' in " + pkg.manifestPath.string() +
            " is missing required 'output' for language-driven task execution.";
        return std::nullopt;
    }
    if(source.empty() && inputs.empty())
    {
        error =
            "Task '" + taskName + "' in " + pkg.manifestPath.string() +
            " needs 'source' or 'inputs' for language-driven task execution.";
        return std::nullopt;
    }

    if(!compileOnly && !validate_declared_libraries(pkg.manifestPath, taskName,
                                                    taskBuildConfig, linkFlags))
    {
        error = "";
        return std::nullopt;
    }

    const std::string compiler =
        default_task_compiler_for_language(language, taskBuildConfig);
    if(compiler.empty())
    {
        error = "Task '" + taskName + "' in " + pkg.manifestPath.string() +
                " has unsupported language '" + language + "'.";
        return std::nullopt;
    }

    std::string cmd = shell_quote(compiler);
    if(language == "mlang")
    {
        if(!source.empty())
            cmd += " " + shell_quote(source);
        for(const auto& input : inputs)
            cmd += " " + shell_quote(input);
        cmd += " -o " + shell_quote(output);
        if(compileOnly)
            cmd += " -c";
        if(!taskBuildConfig.targetArch.empty())
            cmd += " --target-arch " + shell_quote(taskBuildConfig.targetArch);
        const std::string optFlag = taskBuildConfig.optLevel;
        if(!optFlag.empty())
            cmd += " " + optFlag;
        for(const auto& flag : taskBuildConfig.compilerFlags)
            append_shell_fragment(cmd, flag);
        if(!compileOnly)
        {
            for(const auto& dir : taskBuildConfig.libPaths)
                cmd += " -L" + shell_quote(dir);
            for(const auto& lib : taskBuildConfig.libs)
                cmd += " -l" + shell_quote(lib);
            if(taskBuildConfig.staticDeps.value_or(false))
            {
                for(const auto& archive : linkFlags.staticArchives)
                    cmd += " " + shell_quote(archive);
            }
            else
            {
                for(const auto& dir : linkFlags.libDirs)
                    cmd += " -L" + shell_quote(dir);
                for(const auto& lib : linkFlags.libs)
                    cmd += " -l" + shell_quote(lib);
                for(const auto& dir : linkFlags.libDirs)
                    cmd += " -Wl,-rpath," + shell_quote(dir);
            }
            if(taskBuildConfig.staticCppRuntime.value_or(false))
                cmd += " -static-libstdc++ -static-libgcc";
            for(const auto& flag : taskBuildConfig.linkerFlags)
                append_shell_fragment(cmd, flag);
            for(const auto& flag : pkgFlags)
                cmd += " " + shell_quote(flag);
        }
        return cmd;
    }

    if(!source.empty())
        cmd += " " + shell_quote(source);
    for(const auto& input : inputs)
        cmd += " " + shell_quote(input);
    for(const auto& flag : taskBuildConfig.compilerFlags)
        append_shell_fragment(cmd, flag);
    if(compileOnly)
        cmd += " -c";
    cmd += " -o " + shell_quote(output);
    if(!compileOnly)
    {
        for(const auto& dir : taskBuildConfig.libPaths)
            cmd += " -L" + shell_quote(dir);
        for(const auto& lib : taskBuildConfig.libs)
            cmd += " -l" + shell_quote(lib);
        if(taskBuildConfig.staticDeps.value_or(false))
        {
            for(const auto& archive : linkFlags.staticArchives)
                cmd += " " + shell_quote(archive);
        }
        else
        {
            for(const auto& dir : linkFlags.libDirs)
                cmd += " -L" + shell_quote(dir);
            for(const auto& lib : linkFlags.libs)
                cmd += " -l" + shell_quote(lib);
            for(const auto& dir : linkFlags.libDirs)
                cmd += " -Wl,-rpath," + shell_quote(dir);
        }
        if(language == "c++" &&
           taskBuildConfig.staticCppRuntime.value_or(false))
            cmd += " -static-libstdc++ -static-libgcc";
        for(const auto& flag : taskBuildConfig.linkerFlags)
            append_shell_fragment(cmd, flag);
        for(const auto& flag : pkgFlags)
            cmd += " " + shell_quote(flag);
    }
    return cmd;
}

static int build_for_manifest(const PackageManifest& pkg,
                              const std::string& argv0,
                              const std::string& optFlagOverride, bool useNinja,
                              const BuildConfig& packageBuildConfig)
{
    DependencyLockContext* buildTraversal =
        current_dependency_lock_context();
    const std::string buildManifestKey =
        std::filesystem::absolute(pkg.manifestPath).lexically_normal().string();
    if(buildTraversal != nullptr &&
       !buildTraversal->activeBuildManifests.insert(buildManifestKey).second)
    {
        pkg_error_line("Transitive dependency cycle reaches " +
                       pkg.manifestPath.string());
        return 1;
    }
    struct BuildTraversalGuard
    {
        DependencyLockContext* context;
        std::string key;
        ~BuildTraversalGuard()
        {
            if(context != nullptr)
                context->activeBuildManifests.erase(key);
        }
    } buildTraversalGuard{buildTraversal, buildManifestKey};
    const BuildConfig effectivePackageBuildConfig =
        materialize_build_config_for_package(pkg, packageBuildConfig);
    if(validate_toolchain_requirements(pkg, effectivePackageBuildConfig) != 0)
        return 1;
    ScopedPackageLogState scopedLogs(
        make_package_log_state(pkg.packageDir, effectivePackageBuildConfig));
    std::vector<BuildTarget> targets = parse_build_targets(pkg.content);
    const bool hasExplicitTargets = !targets.empty();
    const auto packageEntry =
        find_section_toml_string(pkg.content, "package", "entry");
    const bool hasExplicitPackageEntry =
        packageEntry.has_value() && !packageEntry->empty();
    const bool taskOnlyPackage =
        manifest_declares_tasks(pkg.content) && !hasExplicitPackageEntry;

    std::string packageLabel = "app";
    if(auto name = find_section_toml_string(pkg.content, "package", "name");
       name.has_value() && !name->empty())
    {
        packageLabel = *name;
    }
    std::string entryError;
    if(!taskOnlyPackage && !hasExplicitTargets &&
       !ensure_package_entry_stub(pkg.manifestPath, pkg.content, packageLabel,
                                  entryError))
    {
        pkg_error_line(entryError);
        return 1;
    }

    auto deps = parse_source_deps(pkg.content);
    auto cdeps = parse_c_deps(pkg.content);
    if(targets.empty() && !taskOnlyPackage)
    {
        BuildTarget defaultTarget;
        defaultTarget.name = "app";
        if(auto v = find_section_toml_string(pkg.content, "package", "name");
           v.has_value())
        {
            defaultTarget.name = v.value();
        }
        defaultTarget.entry = "src/main.mla";
        if(auto v = find_section_toml_string(pkg.content, "package", "entry");
           v.has_value())
        {
            defaultTarget.entry = v.value();
        }
        targets.push_back(defaultTarget);
    }
    for(const auto& target : targets)
    {
        const char* sectionName = target.kind != BuildTarget::executable
                                      ? "[[lib]]"
                                      : "[[bin]]";
        if(target.hasInvalidLibraryType)
        {
            pkg_error_line("Invalid [[lib]] type '" +
                           target.invalidLibraryType + "' for target '" +
                           target.name +
                           "' (expected dynamic, shared, or static)");
            return 1;
        }
        if(target.name.empty())
        {
            pkg_error_line("Missing name in " + std::string(sectionName) +
                           " target for " + pkg.manifestPath.string());
            return 1;
        }
        if(target.entry.empty())
        {
            pkg_error_line("Missing entry in " + std::string(sectionName) +
                           " target '" + target.name + "' for " +
                           pkg.manifestPath.string());
            return 1;
        }
    }
    if(auto ordered = order_build_targets(targets, pkg.manifestPath);
       ordered.has_value())
    {
        targets = std::move(*ordered);
    }
    else
    {
        return 1;
    }

    bool effectiveUseNinja =
        useNinja || packageBuildConfig.useNinja.value_or(false);
    const auto tasks = parse_task_specs(pkg.content);
    const std::string hostName = current_host_name();
    std::vector<std::string> buildTaskRoots;
    if(targets.empty())
    {
        buildTaskRoots = task_names_for_phases(
            tasks, std::vector<std::string>{"build"}, hostName);
        if(buildTaskRoots.empty() && task_list_contains_name(tasks, "build"))
            buildTaskRoots.push_back("build");
    }
    for(const auto& target : targets)
    {
        BuildConfig mergedConfig =
            merge_build_config(effectivePackageBuildConfig, target.config);
        apply_asan_overrides(mergedConfig,
                             "package target '" + target.name + "' in " +
                                 pkg.manifestPath.string(),
                             optFlagOverride.empty()
                                 ? std::nullopt
                                 : std::optional<std::string>(optFlagOverride));
        mergedConfig = materialize_build_config_for_package(pkg, mergedConfig);
        if(mergedConfig.useNinja.value_or(false))
            effectiveUseNinja = true;
        if(validate_mlang_version_requirement(pkg.manifestPath, mergedConfig,
                                              target.name) != 0)
        {
            return 1;
        }
    }
    if(effectiveUseNinja &&
       !ensure_ninja_available(effectivePackageBuildConfig.pathEntries))
        return 1;

    std::filesystem::path depsDir =
        package_deps_dir(pkg.packageDir, effectivePackageBuildConfig);
    std::filesystem::create_directories(depsDir);
    size_t totalSteps = 0;
    for(const auto& dep : deps)
        totalSteps +=
            count_fetch_dep_steps(dep, depsDir, /*updateExisting=*/false);
    for(const auto& dep : deps)
        totalSteps += count_build_dep_steps(pkg, dep, depsDir);
    if(!targets.empty())
        totalSteps += targets.size();
    else
        totalSteps +=
            count_reachable_task_steps(tasks, hostName, buildTaskRoots);
    ScopedExecutionProgressState scopedProgress(totalSteps);
    for(const auto& dep : deps)
    {
        if(fetch_dep(pkg, dep, depsDir, /*updateExisting=*/false,
                     effectivePackageBuildConfig.pathEntries) != 0)
            return 1;
        const bool requiresManifest = !dep.path.empty() || dep.build == "mlang";
        const auto child =
            load_dependency_manifest(pkg, dep, depsDir, requiresManifest);
        if(!child.has_value())
        {
            if(requiresManifest)
                return 1;
            continue;
        }
        const std::string version = package_version(*child);
        if(auto* entry = dependency_lock_entry(pkg, dep))
        {
            if(!entry->resolvedVersion.empty() &&
               entry->resolvedVersion != version)
            {
                DependencyLockContext* context =
                    current_dependency_lock_context();
                if(context != nullptr &&
                   (context->locked || context->offline))
                {
                    pkg_error_line("MLang dependency '" + dep.name +
                                   "' changed version: locked " +
                                   entry->resolvedVersion + ", found " +
                                   version);
                    return 1;
                }
            }
            record_dependency_lock_entry(pkg, dep, entry->revision,
                                         entry->checksum, version);
        }
    }
    for(const auto& dep : deps)
    {
        if(dep.build == "mlang")
        {
            const auto child =
                load_dependency_manifest(pkg, dep, depsDir, true);
            if(!child.has_value())
                return 1;
            const std::string childKey = std::filesystem::absolute(
                                             child->manifestPath)
                                             .lexically_normal()
                                             .string();
            DependencyLockContext* context =
                current_dependency_lock_context();
            if(context != nullptr &&
               context->builtManifests.count(childKey) != 0)
                continue;
            BuildConfig childConfig = parse_manifest_build_config(*child);
            childConfig.compilerProgram =
                effectivePackageBuildConfig.compilerProgram;
            if(build_for_manifest(*child, argv0, optFlagOverride, useNinja,
                                  childConfig) != 0)
                return 1;
            if(context != nullptr)
                context->builtManifests.insert(childKey);
            continue;
        }
        if(build_git_dep(pkg, dep, depsDir, effectiveUseNinja,
                         effectivePackageBuildConfig.makeProgram,
                         effectivePackageBuildConfig.pathEntries) != 0)
            return 1;
    }

    LinkFlags linkFlags = collect_transitive_dep_link_flags(pkg, depsDir);
    std::vector<std::string> pkgFlags;
    for(const auto& dep : cdeps)
    {
        if(!append_pkg_config_flags(dep, pkgFlags,
                                    effectivePackageBuildConfig.pathEntries))
            return 1;
    }

    if(targets.empty())
    {
        if(!buildTaskRoots.empty())
        {
            for(const auto& taskName : buildTaskRoots)
            {
                if(run_task_for_manifest(pkg, taskName,
                                         effectivePackageBuildConfig) != 0)
                    return 1;
            }
            return 0;
        }

        pkg_error_line("No package entry, [[bin]]/[[lib]] targets, or "
                       "phase=\"build\" "
                       "tasks found for " +
                       pkg.manifestPath.string());
        return 1;
    }

    const std::filesystem::path buildDir =
        package_build_dir(pkg.packageDir, effectivePackageBuildConfig);
    std::filesystem::create_directories(buildDir);
    std::map<std::string, const BuildTarget*> targetsByName;
    for(const auto& target : targets)
        targetsByName[target.name] = &target;
    std::string backend = argv0;
    if(argv0.find('/') != std::string::npos)
        backend = std::filesystem::absolute(argv0).string();

    for(const auto& target : targets)
    {
        BuildConfig buildConfig =
            merge_build_config(effectivePackageBuildConfig, target.config);
        apply_asan_overrides(buildConfig,
                             "package target '" + target.name + "' in " +
                                 pkg.manifestPath.string(),
                             optFlagOverride.empty()
                                 ? std::nullopt
                                 : std::optional<std::string>(optFlagOverride));
        buildConfig = materialize_build_config_for_package(pkg, buildConfig);
        if(!validate_declared_libraries(pkg.manifestPath, target.name,
                                        buildConfig, linkFlags))
            return 1;

        std::string optFlag = optFlagOverride;
        if(optFlag.empty())
            optFlag = buildConfig.optLevel;

        const std::filesystem::path outputPath =
            buildDir /
            (target.kind == BuildTarget::dynamic_library
                 ? dynamic_library_filename(target.name)
                 : target.kind == BuildTarget::static_library
                       ? static_library_filename(target.name)
                       : target.name);
        std::string output = outputPath.string();
        const std::filesystem::path compileWorkDir =
            pkg.includeTarget.empty() ? pkg.packageDir : buildDir;
        const std::filesystem::path entryPath =
            pkg.includeTarget.empty()
                ? std::filesystem::path(target.entry)
                : (pkg.packageDir / target.entry).lexically_normal();
        std::string cmd = shell_quote(backend) + " " +
                          shell_quote(entryPath.string()) + " -o " +
                          shell_quote(output);
        if(target.kind == BuildTarget::dynamic_library)
            cmd += " --shared";
        else if(target.kind == BuildTarget::static_library)
            cmd += " --static-library";
        if(!buildConfig.targetArch.empty())
            cmd += " --target-arch " + shell_quote(buildConfig.targetArch);
        if(!optFlag.empty())
            cmd += " " + optFlag;
        for(const auto& flag : buildConfig.compilerFlags)
            append_shell_fragment(cmd, flag);
        for(const auto& dir : buildConfig.libPaths)
            cmd += " -L" + shell_quote(dir);
        for(const auto& lib : buildConfig.libs)
            cmd += " -l" + shell_quote(lib);
        if(!target.dependsOn.empty() &&
           target.kind != BuildTarget::static_library)
        {
            std::vector<const BuildTarget*> targetDependencies;
            std::unordered_set<std::string> seenDependencies;
            collect_target_link_dependencies(target, targetsByName,
                                             targetDependencies,
                                             seenDependencies);
            bool hasDynamicDependency = false;
            for(const auto* dependency : targetDependencies)
            {
                if(dependency->kind == BuildTarget::static_library)
                {
                    cmd += " " + shell_quote(
                        (buildDir / static_library_filename(dependency->name))
                            .string());
                }
                else
                {
                    if(!hasDynamicDependency)
                        cmd += " -L" + shell_quote(buildDir.string());
                    cmd += " -l" + shell_quote(dependency->name);
                    hasDynamicDependency = true;
                }
            }
            if(hasDynamicDependency)
            {
                const std::string rpath = package_target_rpath_flag();
                if(!rpath.empty())
                    cmd += " " + shell_quote(rpath);
            }
        }
        if(buildConfig.staticDeps.value_or(false))
        {
            for(const auto& archive : linkFlags.staticArchives)
                cmd += " " + shell_quote(archive);
        }
        else
        {
            for(const auto& dir : linkFlags.libDirs)
                cmd += " -L" + shell_quote(dir);
            for(const auto& lib : linkFlags.libs)
                cmd += " -l" + shell_quote(lib);
            for(const auto& dir : linkFlags.libDirs)
                cmd += " -Wl,-rpath," + shell_quote(dir);
        }
        if(buildConfig.staticCppRuntime.value_or(false))
            cmd += " -static-libstdc++ -static-libgcc";
        for(const auto& flag : buildConfig.linkerFlags)
            append_shell_fragment(cmd, flag);
        for(const auto& flag : pkgFlags)
            cmd += " " + shell_quote(flag);

        int rc = run_status_command_in_dir_with_paths(
            execution_step_label(std::string(
                                     target.kind == BuildTarget::dynamic_library
                                         ? "Building dynamic library target '"
                                         : target.kind ==
                                                   BuildTarget::static_library
                                               ? "Building static library target '"
                                               : "Compiling target '") +
                                 target.name +
                                 "' from " + target.entry + " -> " + output),
            compileWorkDir, cmd, buildConfig.pathEntries, true);
        if(rc != 0)
        {
            std::string message =
                "Build failed for " + pkg.manifestPath.string();
            if(!target.name.empty())
                message += " target '" + target.name + "'";
            message += ".";
            pkg_error_line(message);
            return 1;
        }
    }
    return 0;
}

static int clean_for_manifest(const PackageManifest& pkg,
                              const BuildConfig& buildConfig)
{
    ScopedPackageLogState scopedLogs(
        make_package_log_state(pkg.packageDir, buildConfig));
    const std::filesystem::path buildDir =
        package_build_dir(pkg.packageDir, buildConfig);
    if(!std::filesystem::exists(buildDir))
    {
        pkg_warn_line("No artifacts to clean in " + buildDir.string());
        return 0;
    }
    std::error_code ec;
    std::filesystem::remove_all(buildDir, ec);
    if(ec)
    {
        pkg_error_line("Failed to clean " + buildDir.string() + ": " +
                       ec.message());
        return 1;
    }
    pkg_info_line("Cleaned " + buildDir.string());
    return 0;
}

static std::string replace_all(std::string text, const std::string& needle,
                               const std::string& value)
{
    if(needle.empty())
        return text;
    size_t pos = 0;
    while((pos = text.find(needle, pos)) != std::string::npos)
    {
        text.replace(pos, needle.size(), value);
        pos += value.size();
    }
    return text;
}

static std::string expand_task_text(const std::string& text,
                                    const PackageManifest& pkg,
                                    const BuildConfig& buildConfig)
{
    const std::filesystem::path buildDir =
        package_build_dir(pkg.packageDir, buildConfig);
    const std::filesystem::path depsDir =
        package_deps_dir(pkg.packageDir, buildConfig);
    std::string out = text;
    out = replace_all(out, "{{root}}", pkg.packageDir.string());
    out = replace_all(out, "{{manifest}}", pkg.manifestPath.string());
    out = replace_all(out, "{{build_dir}}", buildDir.string());
    out = replace_all(out, "{{deps_dir}}", depsDir.string());
    out = replace_all(
        out, "{{make}}",
        buildConfig.makeProgram.empty() ? "make" : buildConfig.makeProgram);
    out = replace_all(out, "{{cmake_build_type}}",
                      build_config_cmake_build_type(buildConfig));
    out = replace_all(out, "{{asan_enabled}}",
                      build_config_asan_enabled(buildConfig) ? "1" : "0");
    out = replace_all(out, "{{cmake_c_flags}}",
                      build_config_asan_compile_flags(buildConfig));
    out = replace_all(out, "{{cmake_cxx_flags}}",
                      build_config_asan_compile_flags(buildConfig));
    out = replace_all(out, "{{cmake_exe_linker_flags}}",
                      build_config_asan_link_flags(buildConfig));
    out = replace_all(out, "{{cmake_shared_linker_flags}}",
                      build_config_asan_link_flags(buildConfig));
    out = replace_all(out, "{{cmake_module_linker_flags}}",
                      build_config_asan_link_flags(buildConfig));
    for(const auto& [key, value] : buildConfig.optionValues)
        out = replace_all(out, "{{option." + key + "}}", value);
    return out;
}

static std::string expand_build_config_fragment(const PackageManifest& pkg,
                                                const BuildConfig& buildConfig,
                                                const std::string& text)
{
    return expand_task_text(text, pkg, buildConfig);
}

static std::string
resolve_build_config_path_entry(const PackageManifest& pkg,
                                const BuildConfig& buildConfig,
                                const std::string& text)
{
    const std::string expanded =
        expand_build_config_fragment(pkg, buildConfig, text);
    if(expanded.empty())
        return expanded;
    std::filesystem::path path(expanded);
    if(path.is_relative())
        path = (pkg.packageDir / path).lexically_normal();
    return path.string();
}

static BuildConfig
materialize_build_config_for_package(const PackageManifest& pkg,
                                     BuildConfig buildConfig)
{
    buildConfig.compilerProgram = expand_build_config_fragment(
        pkg, buildConfig, buildConfig.compilerProgram);
    buildConfig.makeProgram =
        expand_build_config_fragment(pkg, buildConfig, buildConfig.makeProgram);

    for(auto& entry : buildConfig.pathEntries)
        entry = resolve_build_config_path_entry(pkg, buildConfig, entry);
    for(auto& entry : buildConfig.libPaths)
        entry = resolve_build_config_path_entry(pkg, buildConfig, entry);
    for(auto& flag : buildConfig.compilerFlags)
        flag = expand_build_config_fragment(pkg, buildConfig, flag);
    for(auto& flag : buildConfig.linkerFlags)
        flag = expand_build_config_fragment(pkg, buildConfig, flag);

    return buildConfig;
}

static std::string current_host_name()
{
#if defined(__APPLE__)
    return "darwin";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#elif defined(__FreeBSD__)
    return "freebsd";
#else
    return "unknown";
#endif
}

struct TaskRunState
{
    enum Status
    {
        not_started,
        running,
        succeeded,
        failed
    };

    Status status = not_started;
};

class TaskRunStateGuard
{
  public:
    TaskRunStateGuard(std::map<std::string, TaskRunState>& states,
                      std::mutex& mutex, std::condition_variable& cv,
                      std::string taskName)
        : states_(states), mutex_(mutex), cv_(cv), taskName_(std::move(taskName))
    {
    }

    ~TaskRunStateGuard()
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto state = states_.find(taskName_);
            if(state != states_.end() &&
               state->second.status == TaskRunState::running)
            {
                state->second.status = TaskRunState::failed;
                changed = true;
            }
        }
        if(changed)
            cv_.notify_all();
    }

    TaskRunStateGuard(const TaskRunStateGuard&) = delete;
    TaskRunStateGuard& operator=(const TaskRunStateGuard&) = delete;

  private:
    std::map<std::string, TaskRunState>& states_;
    std::mutex& mutex_;
    std::condition_variable& cv_;
    std::string taskName_;
};

static std::string format_task_elapsed(std::chrono::milliseconds elapsed)
{
    const auto totalMs = elapsed.count();
    const long long hours = totalMs / (1000LL * 60LL * 60LL);
    const long long minutes = (totalMs / (1000LL * 60LL)) % 60LL;
    const long long seconds = (totalMs / 1000LL) % 60LL;
    const long long milliseconds = totalMs % 1000LL;
    std::ostringstream out;
    out << "[" << std::setw(2) << std::setfill('0') << hours << "/"
        << std::setw(2) << std::setfill('0') << minutes << "/" << std::setw(2)
        << std::setfill('0') << seconds << "/" << std::setw(3)
        << std::setfill('0') << milliseconds << "]";
    return out.str();
}

static std::string
format_task_elapsed_compact(std::chrono::milliseconds elapsed)
{
    const auto totalMs = elapsed.count();
    const long long hours = totalMs / (1000LL * 60LL * 60LL);
    const long long minutes = (totalMs / (1000LL * 60LL)) % 60LL;
    const long long seconds = (totalMs / 1000LL) % 60LL;
    const long long milliseconds = totalMs % 1000LL;
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << hours << ":" << std::setw(2)
        << std::setfill('0') << minutes << ":" << std::setw(2)
        << std::setfill('0') << seconds << ":" << std::setw(3)
        << std::setfill('0') << milliseconds;
    return out.str();
}

static std::optional<TaskSpec>
find_task_spec(const std::vector<TaskSpec>& tasks, const std::string& taskName)
{
    for(const auto& task : tasks)
    {
        if(task.name == taskName)
            return task;
    }
    return std::nullopt;
}

static bool host_supported_by_task(const TaskSpec& task,
                                   const std::string& hostName)
{
    if(task.supportedHosts.empty())
        return true;
    return std::find(task.supportedHosts.begin(), task.supportedHosts.end(),
                     hostName) != task.supportedHosts.end();
}

static std::string task_unsupported_host_message(const TaskSpec& task,
                                                 const std::string& hostName)
{
    if(!task.unsupportedMessage.empty())
        return task.unsupportedMessage;

    std::ostringstream out;
    out << "Task '" << task.name << "' does not support host '" << hostName
        << "'";
    if(!task.supportedHosts.empty())
    {
        out << " (supported:";
        for(size_t i = 0; i < task.supportedHosts.size(); ++i)
            out << (i == 0 ? " " : ", ") << task.supportedHosts[i];
        out << ")";
    }
    return out.str();
}

static std::optional<std::filesystem::path>
write_task_script(const PackageManifest& pkg, const std::string& taskName,
                  const std::vector<std::string>& shellLines,
                  const BuildConfig& buildConfig)
{
    if(shellLines.empty())
        return std::nullopt;

    std::filesystem::path scriptDir =
        package_build_dir(pkg.packageDir, buildConfig) / "task-scripts";
    std::error_code ec;
    std::filesystem::create_directories(scriptDir, ec);
    if(ec)
    {
        std::cerr << "Failed to create task script directory "
                  << scriptDir.string() << ": " << ec.message() << "\n";
        return std::nullopt;
    }

    std::string safeName = taskName;
    for(char& c : safeName)
    {
        if(!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
             c == '-'))
            c = '_';
    }

    std::filesystem::path scriptPath = scriptDir / (safeName + ".sh");
    std::ofstream out(scriptPath, std::ios::binary);
    if(!out)
    {
        std::cerr << "Failed to write task script " << scriptPath.string()
                  << "\n";
        return std::nullopt;
    }
    out << "#!/bin/sh\nset -eu\n";
    for(const auto& line : shellLines)
        out << expand_task_text(line, pkg, buildConfig) << "\n";
    out.close();

    std::filesystem::permissions(scriptPath,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write |
                                     std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_read |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::replace, ec);
    if(ec)
    {
        std::cerr << "Failed to make task script executable "
                  << scriptPath.string() << ": " << ec.message() << "\n";
        return std::nullopt;
    }
    return scriptPath;
}

static size_t count_fetch_dep_steps(const DepSpec& dep,
                                    const std::filesystem::path& depsDir,
                                    bool updateExisting)
{
    if(!dep.git.empty())
    {
        const std::filesystem::path path = dep_checkout_dir(depsDir, dep);
        size_t steps = 0;
        if(!std::filesystem::exists(path))
            steps += 1;
        else if(updateExisting)
            steps += 1;
        if(dep.submodules)
            steps += 1;
        steps += 1; // Exact revision, tag, or remote default checkout.
        return steps;
    }

    if(!dep.url.empty())
    {
        const std::filesystem::path checkoutDir =
            dep_checkout_dir(depsDir, dep);
        std::error_code ec;
        const std::filesystem::path archivePath =
            depsDir / ".archives" /
            (sanitize_package_name(dep.name) + ".tar.gz");
        const std::filesystem::path markerPath =
            checkoutDir / ".mlang-source.sha256";
        if(std::filesystem::exists(checkoutDir, ec) &&
           std::filesystem::exists(archivePath, ec) &&
           std::filesystem::exists(markerPath, ec))
        {
            bool hasEntries = false;
            for(std::filesystem::directory_iterator it(checkoutDir, ec), end;
                !ec && it != end; it.increment(ec))
            {
                hasEntries = true;
                break;
            }
            if(!ec && hasEntries)
                return 0;
        }
        return 2;
    }

    return 0;
}

static size_t count_build_dep_steps(const PackageManifest& pkg,
                                    const DepSpec& dep,
                                    const std::filesystem::path& depsDir)
{
    const std::filesystem::path path = dep_source_dir(pkg, depsDir, dep);
    if(!std::filesystem::exists(path))
        return 0;
    if(dep.build == "none" || dep.build == "skip")
        return 0;
    if(dep.build == "cmake")
        return 2;
    if(dep.build == "meson")
    {
        const std::filesystem::path buildDir = path / "build";
        return std::filesystem::exists(buildDir) ? 1 : 2;
    }
    if(dep.build == "make")
        return 1;
    return 0;
}

struct EffectiveTaskEdges
{
    std::vector<std::string> dependsOn;
    std::vector<std::string> next;
    bool parallel = false;
};

static EffectiveTaskEdges
resolve_effective_task_edges(const TaskSpec& task,
                             const std::vector<TaskSpec>& tasks,
                             const std::string& hostName)
{
    EffectiveTaskEdges edges;
    auto hostIt = task.hostOverrides.find(hostName);
    const TaskSpec::HostOverride* hostOverride =
        hostIt == task.hostOverrides.end() ? nullptr : &hostIt->second;

    edges.dependsOn = task.dependsOn;
    if(hostOverride)
    {
        edges.dependsOn.insert(edges.dependsOn.end(),
                               hostOverride->dependsOn.begin(),
                               hostOverride->dependsOn.end());
    }
    std::vector<std::string> effectivePhaseDependsOn = task.phaseDependsOn;
    if(hostOverride)
    {
        effectivePhaseDependsOn.insert(effectivePhaseDependsOn.end(),
                                       hostOverride->phaseDependsOn.begin(),
                                       hostOverride->phaseDependsOn.end());
    }
    append_unique_strings(
        edges.dependsOn,
        task_names_for_phases(tasks, effectivePhaseDependsOn, hostName));

    std::vector<std::string> effectiveJoinOn = task.joinOn;
    if(hostOverride)
    {
        effectiveJoinOn.insert(effectiveJoinOn.end(),
                               hostOverride->joinOn.begin(),
                               hostOverride->joinOn.end());
    }
    std::vector<std::string> effectivePhaseJoinOn = task.phaseJoinOn;
    if(hostOverride)
    {
        effectivePhaseJoinOn.insert(effectivePhaseJoinOn.end(),
                                    hostOverride->phaseJoinOn.begin(),
                                    hostOverride->phaseJoinOn.end());
    }
    append_unique_strings(
        effectiveJoinOn,
        task_names_for_phases(tasks, effectivePhaseJoinOn, hostName));
    append_unique_strings(edges.dependsOn, effectiveJoinOn);

    edges.next = task.nextTasks;
    if(hostOverride)
    {
        edges.next.insert(edges.next.end(), hostOverride->nextTasks.begin(),
                          hostOverride->nextTasks.end());
    }
    std::vector<std::string> effectiveNextPhases = task.nextPhases;
    if(hostOverride)
    {
        effectiveNextPhases.insert(effectiveNextPhases.end(),
                                   hostOverride->nextPhases.begin(),
                                   hostOverride->nextPhases.end());
    }
    append_unique_strings(
        edges.next,
        task_names_for_phases(tasks, effectiveNextPhases, hostName));

    edges.parallel = hostOverride && hostOverride->parallel.has_value()
                         ? hostOverride->parallel.value()
                         : task.parallel.value_or(false);
    return edges;
}

static bool append_task_execution_order(
    const std::vector<TaskSpec>& tasks, const std::string& hostName,
    const std::string& taskName, std::vector<std::string>& stack,
    std::unordered_set<std::string>& emitted, std::vector<std::string>& order,
    std::string& error)
{
    if(taskName.empty())
        return true;
    if(std::find(stack.begin(), stack.end(), taskName) != stack.end())
    {
        error = "Detected cyclic task dependency while expanding '" + taskName +
                "'";
        return false;
    }

    const auto taskOpt = find_task_spec(tasks, taskName);
    if(!taskOpt.has_value())
    {
        error = "Task not found: " + taskName;
        return false;
    }
    if(!host_supported_by_task(*taskOpt, hostName))
    {
        error = task_unsupported_host_message(*taskOpt, hostName);
        return false;
    }

    stack.push_back(taskName);
    const EffectiveTaskEdges edges =
        resolve_effective_task_edges(*taskOpt, tasks, hostName);
    for(const auto& dep : edges.dependsOn)
    {
        if(!append_task_execution_order(tasks, hostName, dep, stack, emitted,
                                        order, error))
        {
            stack.pop_back();
            return false;
        }
    }
    if(emitted.insert(taskName).second)
        order.push_back(taskName);
    for(const auto& nextTask : edges.next)
    {
        if(!append_task_execution_order(tasks, hostName, nextTask, stack,
                                        emitted, order, error))
        {
            stack.pop_back();
            return false;
        }
    }
    stack.pop_back();
    return true;
}

struct BranchColorInfo
{
    size_t position = 0;
    size_t total = 1;
    size_t shadePosition = 0;
    size_t shadeTotal = 1;
    bool useNeutralHue = false;
    double brightnessScale = 1.0;
};

static BranchColorInfo
make_child_branch_color(std::optional<BranchColorInfo> parentColor,
                        bool isParallelGroup, size_t childIndex,
                        size_t childCount)
{
    const BranchColorInfo parent =
        parentColor.value_or(BranchColorInfo{0, 1, 0, 1, true, 1.0});
    BranchColorInfo child = parent;
    child.brightnessScale = std::max(0.42, parent.brightnessScale * 0.75);

    if(isParallelGroup && childCount > 1)
    {
        child.position = childIndex;
        child.total = childCount;
        child.useNeutralHue = false;
    }

    child.shadePosition = childIndex;
    child.shadeTotal = std::max<size_t>(childCount, 1);
    return child;
}

static bool terminal_supports_truecolor()
{
    const char* colorTerm = std::getenv("COLORTERM");
    if(colorTerm == nullptr)
        return false;
    std::string value = colorTerm;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return value.find("truecolor") != std::string::npos ||
           value.find("24bit") != std::string::npos;
}

static std::string branch_color_code(const BranchColorInfo& colorInfo)
{
    const size_t total = std::max<size_t>(colorInfo.total, 1);
    const size_t last = total > 0 ? total - 1 : 0;
    const double t =
        last == 0 ? 0.0
                  : static_cast<double>(std::min(colorInfo.position, last)) /
                        static_cast<double>(last);
    const size_t shadeTotal = std::max<size_t>(colorInfo.shadeTotal, 1);
    const size_t shadeLast = shadeTotal > 0 ? shadeTotal - 1 : 0;
    const double shadeT =
        shadeLast == 0 ? 0.5
                       : static_cast<double>(
                             std::min(colorInfo.shadePosition, shadeLast)) /
                             static_cast<double>(shadeLast);
    const double shadeFactor = 0.92 + (shadeT * 0.08);
    const double brightness = colorInfo.brightnessScale * shadeFactor;

    double baseRed = colorInfo.useNeutralHue ? 0.0 : 255.0 * (1.0 - t);
    double baseGreen = colorInfo.useNeutralHue ? 200.0 : 255.0 * t;
    double baseBlue = colorInfo.useNeutralHue ? 200.0 : 0.0;
    const int red = static_cast<int>(
        std::clamp(std::lround(baseRed * brightness), 0L, 255L));
    const int green = static_cast<int>(
        std::clamp(std::lround(baseGreen * brightness), 0L, 255L));
    const int blue = static_cast<int>(
        std::clamp(std::lround(baseBlue * brightness), 0L, 255L));
    if(terminal_supports_truecolor())
    {
        return "\033[38;2;" + std::to_string(red) + ";" +
               std::to_string(green) + ";" + std::to_string(blue) + "m";
    }

    const auto toCube = [](int channel)
    {
        static const int kLevels[] = {0, 95, 135, 175, 215, 255};
        size_t bestIndex = 0;
        int bestDistance = std::abs(channel - kLevels[0]);
        for(size_t i = 1; i < sizeof(kLevels) / sizeof(kLevels[0]); ++i)
        {
            const int distance = std::abs(channel - kLevels[i]);
            if(distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = i;
            }
        }
        return bestIndex;
    };
    const size_t cubeRed = toCube(red);
    const size_t cubeGreen = toCube(green);
    const size_t cubeBlue = toCube(blue);
    const size_t paletteIndex =
        16 + (36 * cubeRed) + (6 * cubeGreen) + cubeBlue;
    return "\033[38;5;" + std::to_string(paletteIndex) + "m";
}

static std::string colorize_tree_text(const std::string& text, bool enableColor,
                                      std::optional<BranchColorInfo> colorInfo)
{
    if(!enableColor)
        return text;
    if(!colorInfo.has_value())
        return std::string("\033[36m") + text + "\033[0m";
    return branch_color_code(*colorInfo) + text + "\033[0m";
}

static std::string colorize_host_label(const std::string& hostName,
                                       bool enableColor)
{
    if(!enableColor)
        return hostName;
    if(terminal_supports_truecolor())
        return "\033[38;2;255;140;0m" + hostName + "\033[0m";
    return std::string("\033[38;5;208m") + hostName + "\033[0m";
}

static std::string
build_task_tree_prefix(const std::vector<bool>& ancestorHasMore, bool isLast,
                       bool isRoot, bool enableColor,
                       std::optional<BranchColorInfo> colorInfo)
{
    if(isRoot)
        return "";

    std::string out;
    for(bool hasMore : ancestorHasMore)
        out += hasMore ? "\xE2\x94\x82   " : "    ";
    out += isLast ? "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 "
                  : "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 ";
    return colorize_tree_text(out, enableColor, colorInfo);
}

static std::string format_task_tree_numbered_label(
    const std::string& taskName, const std::map<std::string, size_t>& orderMap,
    bool parallel, bool unpredictableParallelOrder, bool enableColor)
{
    std::ostringstream out;
    out << taskName;
    if(unpredictableParallelOrder)
    {
        if(enableColor)
            out << " \033[33m[*]\033[0m";
        else
            out << " [*]";
    }
    else
    {
        auto it = orderMap.find(taskName);
        if(it != orderMap.end())
            out << " [" << it->second << "]";
    }
    if(parallel)
        out << " [parallel]";
    return out.str();
}

static void print_task_tree_ascii_node(
    const std::vector<TaskSpec>& tasks, const std::string& hostName,
    const std::string& taskName, const std::map<std::string, size_t>& orderMap,
    const std::vector<bool>& ancestorHasMore, bool isLast, bool isRoot,
    bool enableColor, std::optional<BranchColorInfo> colorInfo,
    std::vector<std::string>& stack, bool printSelf = true,
    bool unpredictableParallelOrder = false)
{
    const auto taskOpt = find_task_spec(tasks, taskName);
    if(!taskOpt.has_value())
    {
        const std::string prefix = build_task_tree_prefix(
            ancestorHasMore, isLast, isRoot, enableColor, colorInfo);
        std::cout << prefix << taskName << " (missing)\n";
        return;
    }
    if(std::find(stack.begin(), stack.end(), taskName) != stack.end())
    {
        const std::string prefix = build_task_tree_prefix(
            ancestorHasMore, isLast, isRoot, enableColor, colorInfo);
        std::cout << prefix << taskName << " (cycle)\n";
        return;
    }

    const EffectiveTaskEdges edges =
        resolve_effective_task_edges(*taskOpt, tasks, hostName);
    if(printSelf)
    {
        const std::string prefix = build_task_tree_prefix(
            ancestorHasMore, isLast, isRoot, enableColor, colorInfo);
        std::cout << prefix
                  << format_task_tree_numbered_label(
                         taskName, orderMap, edges.parallel,
                         unpredictableParallelOrder, enableColor)
                  << "\n";
    }

    std::vector<std::pair<std::string, bool>> children;
    children.reserve(edges.dependsOn.size() + edges.next.size());
    const bool parallelDepends = edges.parallel && edges.dependsOn.size() > 1;
    for(const auto& dep : edges.dependsOn)
        children.push_back({dep, parallelDepends});
    const bool parallelNext = edges.parallel && edges.next.size() > 1;
    for(const auto& nextTask : edges.next)
        children.push_back({nextTask, parallelNext});

    stack.push_back(taskName);
    for(size_t i = 0; i < children.size(); ++i)
    {
        const bool childIsLast = i + 1 == children.size();
        std::vector<bool> childAncestors = ancestorHasMore;
        if(!isRoot)
            childAncestors.push_back(!isLast);
        std::optional<BranchColorInfo> childColorInfo = colorInfo;
        if(enableColor)
        {
            childColorInfo = make_child_branch_color(
                colorInfo, edges.parallel && children.size() > 1, i,
                children.size());
        }

        const std::string edgePrefix = build_task_tree_prefix(
            childAncestors, childIsLast, false, enableColor, childColorInfo);
        std::ostringstream edgeLine;
        edgeLine << edgePrefix;
        if(children[i].second)
        {
            if(enableColor)
                edgeLine << children[i].first << " \033[33m[*]\033[0m";
            else
                edgeLine << children[i].first << " [*]";
        }
        else
        {
            edgeLine << children[i].first;
            auto orderIt = orderMap.find(children[i].first);
            if(orderIt != orderMap.end())
                edgeLine << " [" << orderIt->second << "]";
        }
        std::cout << edgeLine.str() << "\n";

        std::vector<bool> nestedAncestors = childAncestors;
        nestedAncestors.push_back(!childIsLast);
        print_task_tree_ascii_node(tasks, hostName, children[i].first, orderMap,
                                   nestedAncestors, true, false, enableColor,
                                   childColorInfo, stack, false,
                                   children[i].second);
    }
    stack.pop_back();
}

static int print_task_plan_for_manifest(const PackageManifest& pkg,
                                        const std::string& taskName,
                                        const BuildConfig& buildConfig,
                                        bool enableColor)
{
    const auto tasks = parse_task_specs(pkg.content);
    const std::string hostName = current_host_name();
    const auto taskOpt = find_task_spec(tasks, taskName);
    if(!taskOpt.has_value())
        return -1;

    std::vector<std::string> order;
    std::unordered_set<std::string> emitted;
    std::vector<std::string> orderStack;
    std::string error;
    if(!append_task_execution_order(tasks, hostName, taskName, orderStack,
                                    emitted, order, error))
    {
        pkg_error_line(error + " in " + pkg.manifestPath.string());
        return 1;
    }

    std::map<std::string, size_t> orderMap;
    for(size_t i = 0; i < order.size(); ++i)
        orderMap[order[i]] = i + 1;

    ScopedPackageLogState scopedLogs(
        make_package_log_state(pkg.packageDir, buildConfig));
    pkg_info_line("Task tree for '" + taskName + "' (" +
                  colorize_host_label(hostName, enableColor) + "):");
    std::vector<std::string> treeStack;
    print_task_tree_ascii_node(tasks, hostName, taskName, orderMap, {}, true,
                               true, enableColor, std::nullopt, treeStack);

    pkg_info_line("");
    pkg_info_line("Execution order:");
    for(size_t i = 0; i < order.size(); ++i)
    {
        std::string line = "  " + std::to_string(i + 1) + ". " + order[i];
        if(enableColor)
        {
            line = colorize_tree_text(
                line, true,
                BranchColorInfo{i, std::max<size_t>(order.size(), 1), i,
                                std::max<size_t>(order.size(), 1), false, 1.0});
        }
        pkg_info_line(line);
    }
    return 0;
}

static std::vector<std::string>
find_manifest_task_entrypoints(const std::vector<TaskSpec>& tasks,
                               const std::string& hostName)
{
    std::vector<std::string> entrypoints;
    std::unordered_set<std::string> referenced;
    for(const auto& task : tasks)
    {
        if(!host_supported_by_task(task, hostName))
            continue;
        const EffectiveTaskEdges edges =
            resolve_effective_task_edges(task, tasks, hostName);
        for(const auto& dep : edges.dependsOn)
            referenced.insert(dep);
        for(const auto& nextTask : edges.next)
            referenced.insert(nextTask);
    }

    for(const auto& task : tasks)
    {
        if(!host_supported_by_task(task, hostName))
            continue;
        if(referenced.find(task.name) == referenced.end())
            entrypoints.push_back(task.name);
    }
    if(entrypoints.empty())
    {
        for(const auto& task : tasks)
        {
            if(host_supported_by_task(task, hostName))
                entrypoints.push_back(task.name);
        }
    }
    return entrypoints;
}

static int print_task_overview_for_manifest(const PackageManifest& pkg,
                                            const BuildConfig& buildConfig,
                                            bool enableColor)
{
    const auto tasks = parse_task_specs(pkg.content);
    const std::string hostName = current_host_name();
    const auto entrypoints = find_manifest_task_entrypoints(tasks, hostName);
    if(entrypoints.empty())
    {
        pkg_info_line("No runnable tasks for host '" +
                      colorize_host_label(hostName, enableColor) + "'.");
        return 0;
    }

    pkg_info_line("Runnable task entrypoints for '" +
                  pkg.manifestPath.string() + "' (" +
                  colorize_host_label(hostName, enableColor) + "):");
    for(size_t i = 0; i < entrypoints.size(); ++i)
    {
        if(i > 0)
            pkg_info_line("");
        const int rc = print_task_plan_for_manifest(pkg, entrypoints[i],
                                                    buildConfig, enableColor);
        if(rc != 0)
            return rc;
    }
    return 0;
}

static void collect_reachable_task_names(
    const std::vector<TaskSpec>& tasks, const std::string& hostName,
    const std::string& taskName, std::unordered_set<std::string>& visited)
{
    if(taskName.empty() || !visited.insert(taskName).second)
        return;

    const auto taskOpt = find_task_spec(tasks, taskName);
    if(!taskOpt.has_value())
        return;
    const EffectiveTaskEdges edges =
        resolve_effective_task_edges(*taskOpt, tasks, hostName);

    for(const auto& dep : edges.dependsOn)
        collect_reachable_task_names(tasks, hostName, dep, visited);
    for(const auto& nextTask : edges.next)
        collect_reachable_task_names(tasks, hostName, nextTask, visited);
}

static size_t count_reachable_task_steps(const std::vector<TaskSpec>& tasks,
                                         const std::string& hostName,
                                         const std::vector<std::string>& roots)
{
    std::unordered_set<std::string> visited;
    for(const auto& root : roots)
        collect_reachable_task_names(tasks, hostName, root, visited);
    return visited.size();
}

// Sign each path in `signOutputs` after a task succeeds, when the build
// config requested it (CLI: --sign / --force-sign). Templates such as
// {{build_dir}} are expanded via `expand_task_text`. `force` triggers
// `codesign --force --sign -` so an existing (and potentially tainted) adhoc
// signature is replaced; otherwise plain `codesign --sign -` is used. macOS
// only — silently skipped on other hosts since `codesign` does not exist.
static int codesign_task_outputs(const std::vector<std::string>& signOutputs,
                                 const PackageManifest& pkg,
                                 const BuildConfig& buildConfig,
                                 const std::string& taskName,
                                 const std::string& taskPrefix)
{
    if(buildConfig.signMode == 0 || signOutputs.empty())
        return 0;
#if defined(__APPLE__)
    const bool force = buildConfig.signMode == 2;
    for(const auto& raw : signOutputs)
    {
        const std::string expanded = expand_task_text(raw, pkg, buildConfig);
        if(expanded.empty())
            continue;
        std::error_code ec;
        if(!std::filesystem::exists(expanded, ec))
        {
            pkg_warn_line(taskPrefix + " " + taskName +
                          " sign: skipping missing output " + expanded);
            continue;
        }
        std::string cmd = "codesign ";
        if(force)
            cmd += "--force ";
        cmd += "--sign - " + shell_quote(expanded);
        pkg_task_print_line(taskPrefix + " " + taskName + " " +
                            (force ? "force-sign" : "sign") + " " + expanded);
        const int sysRc = std::system(cmd.c_str());
        int rc = 1;
        if(sysRc >= 0 && WIFEXITED(sysRc))
            rc = WEXITSTATUS(sysRc);
        if(rc != 0)
        {
            pkg_error_line(taskPrefix + " " + taskName +
                           " codesign failed (rc=" + std::to_string(rc) +
                           ") for " + expanded);
            return rc;
        }
    }
    return 0;
#else
    (void)pkg;
    (void)taskName;
    (void)taskPrefix;
    return 0;
#endif
}

static int run_task_for_manifest_impl(
    const PackageManifest& pkg, const std::vector<TaskSpec>& tasks,
    const BuildConfig& buildConfig, const std::string& hostName,
    std::map<std::string, TaskRunState>& taskStates, std::mutex& taskMutex,
    std::condition_variable& taskCv, const std::string& taskName,
    std::vector<std::string>& taskStack)
{
    if(std::find(taskStack.begin(), taskStack.end(), taskName) !=
       taskStack.end())
    {
        std::cerr << "Detected cyclic task dependency while running '"
                  << taskName << "' in " << pkg.manifestPath.string() << "\n";
        return 1;
    }

    {
        std::unique_lock<std::mutex> lock(taskMutex);
        TaskRunState& state = taskStates[taskName];
        while(state.status == TaskRunState::running)
            taskCv.wait(lock);
        if(state.status == TaskRunState::succeeded)
            return 0;
        if(state.status == TaskRunState::failed)
            return 1;
        state.status = TaskRunState::running;
    }
    TaskRunStateGuard taskStateGuard(taskStates, taskMutex, taskCv, taskName);

    for(const auto& task : tasks)
    {
        if(task.name != taskName)
            continue;
        if(!host_supported_by_task(task, hostName))
        {
            std::cerr << task_unsupported_host_message(task, hostName) << " in "
                      << pkg.manifestPath.string() << "\n";
            return 1;
        }
        auto hostIt = task.hostOverrides.find(hostName);
        const TaskSpec::HostOverride* hostOverride =
            hostIt == task.hostOverrides.end() ? nullptr : &hostIt->second;
        const EffectiveTaskEdges edges =
            resolve_effective_task_edges(task, tasks, hostName);

        std::vector<std::string> effectiveEnv = task.env;
        if(hostOverride)
        {
            effectiveEnv.insert(effectiveEnv.end(), hostOverride->env.begin(),
                                hostOverride->env.end());
        }

        std::vector<std::string> effectiveShell =
            (hostOverride && !hostOverride->shellLines.empty())
                ? hostOverride->shellLines
                : task.shellLines;

        const std::vector<std::string>& effectiveDependsOn = edges.dependsOn;
        const std::vector<std::string>& effectiveNext = edges.next;

        std::vector<std::string> effectiveCommands =
            (hostOverride && !hostOverride->commands.empty())
                ? hostOverride->commands
                : task.commands;
        const std::string effectiveMessage = [&]() -> std::string
        {
            if(hostOverride && !hostOverride->print.empty())
                return hostOverride->print;
            if(hostOverride && !hostOverride->message.empty())
                return hostOverride->message;
            if(!task.print.empty())
                return task.print;
            return task.message;
        }();

        std::string effectiveWorkdir =
            (hostOverride && !hostOverride->workdir.empty())
                ? hostOverride->workdir
                : task.workdir;
        const std::string effectiveLanguage =
            (hostOverride && !hostOverride->language.empty())
                ? hostOverride->language
                : task.language;
        const std::string effectiveSource =
            (hostOverride && !hostOverride->source.empty())
                ? hostOverride->source
                : task.source;
        const std::string effectiveOutput =
            (hostOverride && !hostOverride->output.empty())
                ? hostOverride->output
                : task.output;
        std::vector<std::string> effectiveInputs = task.inputs;
        if(hostOverride)
        {
            effectiveInputs.insert(effectiveInputs.end(),
                                   hostOverride->inputs.begin(),
                                   hostOverride->inputs.end());
        }
        bool effectiveCompileOnly =
            hostOverride && hostOverride->compileOnly.has_value()
                ? hostOverride->compileOnly.value()
                : task.compileOnly.value_or(false);
        bool effectiveParallel = edges.parallel;
        bool effectiveLogOutput =
            hostOverride && hostOverride->logOutput.has_value()
                ? hostOverride->logOutput.value()
                : task.logOutput.value_or(true);
        bool effectiveInlineOutput =
            hostOverride && hostOverride->inlineOutput.has_value()
                ? hostOverride->inlineOutput.value()
                : task.inlineOutput.value_or(false);
        const std::string effectiveChmodMode =
            (hostOverride && !hostOverride->chmodMode.empty())
                ? hostOverride->chmodMode
                : task.chmodMode;
        std::vector<std::string> effectiveChmodPaths = task.chmodPaths;
        if(hostOverride)
        {
            effectiveChmodPaths.insert(effectiveChmodPaths.end(),
                                       hostOverride->chmodPaths.begin(),
                                       hostOverride->chmodPaths.end());
        }
        BuildConfig effectiveTaskBuildConfig =
            merge_build_config(buildConfig, task.buildConfig);
        if(hostOverride)
        {
            effectiveTaskBuildConfig = merge_build_config(
                effectiveTaskBuildConfig, hostOverride->buildConfig);
        }
        apply_asan_overrides(effectiveTaskBuildConfig,
                             "task '" + taskName + "' in " +
                                 pkg.manifestPath.string());
        effectiveTaskBuildConfig =
            materialize_build_config_for_package(pkg, effectiveTaskBuildConfig);

        taskStack.push_back(taskName);
        if(effectiveParallel && effectiveDependsOn.size() > 1)
        {
            std::vector<std::future<int>> futures;
            futures.reserve(effectiveDependsOn.size());
            for(const auto& dep : effectiveDependsOn)
            {
                std::vector<std::string> childStack = taskStack;
                futures.push_back(std::async(
                    std::launch::async,
                    [&pkg, &tasks, &buildConfig, &hostName, &taskStates,
                     &taskMutex, &taskCv, dep, childStack]() mutable
                    {
                        return run_task_for_manifest_impl(
                            pkg, tasks, buildConfig, hostName, taskStates,
                            taskMutex, taskCv, dep, childStack);
                    }));
            }
            for(size_t i = 0; i < effectiveDependsOn.size(); ++i)
            {
                if(futures[i].get() != 0)
                {
                    std::cerr << "Task '" << taskName << "' dependency '"
                              << effectiveDependsOn[i] << "' failed for "
                              << pkg.manifestPath.string() << ".\n";
                    taskStack.pop_back();
                    return 1;
                }
            }
        }
        else
        {
            for(const auto& dep : effectiveDependsOn)
            {
                if(run_task_for_manifest_impl(pkg, tasks, buildConfig, hostName,
                                              taskStates, taskMutex, taskCv,
                                              dep, taskStack) != 0)
                {
                    std::cerr << "Task '" << taskName << "' dependency '" << dep
                              << "' failed for " << pkg.manifestPath.string()
                              << ".\n";
                    taskStack.pop_back();
                    return 1;
                }
            }
        }

        std::filesystem::path workdir =
            effectiveWorkdir.empty()
                ? pkg.packageDir
                : std::filesystem::path(expand_task_text(
                      effectiveWorkdir, pkg, effectiveTaskBuildConfig));
        if(!workdir.is_absolute())
            workdir = pkg.packageDir / workdir;

        const std::string taskDescription =
            effectiveMessage.empty()
                ? taskName
                : expand_task_text(effectiveMessage, pkg,
                                   effectiveTaskBuildConfig);
        const std::string taskPrefix = reserve_execution_step_prefix();
        pkg_task_print_line(taskPrefix + " " + taskDescription);
        const auto taskStart = std::chrono::steady_clock::now();
        std::unique_ptr<ProgressSpinner> inlineSpinner;
        if(effectiveInlineOutput && !pkg_logs_active())
        {
            inlineSpinner = std::make_unique<ProgressSpinner>(
                taskPrefix + " " + shorten_progress_description(taskName));
        }

        if(!effectiveShell.empty())
        {
            auto scriptPathOpt = write_task_script(
                pkg, taskName, effectiveShell, effectiveTaskBuildConfig);
            if(!scriptPathOpt.has_value())
            {
                taskStack.pop_back();
                return 1;
            }
            effectiveCommands.insert(effectiveCommands.begin(),
                                     "sh " +
                                         shell_quote(scriptPathOpt->string()));
        }

        if(!effectiveLanguage.empty())
        {
            const auto deps = parse_source_deps(pkg.content);
            const auto cdeps = parse_c_deps(pkg.content);
            const std::filesystem::path depsDir =
                package_deps_dir(pkg.packageDir, buildConfig);
            LinkFlags linkFlags = collect_transitive_dep_link_flags(pkg, depsDir);
            std::vector<std::string> pkgFlags;
            for(const auto& dep : cdeps)
            {
                if(!append_pkg_config_flags(
                       dep, pkgFlags, effectiveTaskBuildConfig.pathEntries))
                {
                    taskStack.pop_back();
                    return 1;
                }
            }
            std::string generationError;
            auto generatedCommand = build_task_language_command(
                pkg, taskName, effectiveTaskBuildConfig, effectiveLanguage,
                expand_task_text(effectiveSource, pkg,
                                 effectiveTaskBuildConfig),
                expand_task_text(effectiveOutput, pkg,
                                 effectiveTaskBuildConfig),
                [&]()
                {
                    std::vector<std::string> expandedInputs;
                    expandedInputs.reserve(effectiveInputs.size());
                    for(const auto& input : effectiveInputs)
                    {
                        expandedInputs.push_back(expand_task_text(
                            input, pkg, effectiveTaskBuildConfig));
                    }
                    return expandedInputs;
                }(),
                effectiveCompileOnly, linkFlags, pkgFlags, generationError);
            if(!generatedCommand.has_value())
            {
                if(!generationError.empty())
                    std::cerr << generationError << "\n";
                taskStack.pop_back();
                return 1;
            }
            effectiveCommands.insert(effectiveCommands.begin(),
                                     *generatedCommand);
        }

        if(effectiveCommands.empty() && effectiveShell.empty())
        {
            std::cerr << "Task '" << taskName << "' in "
                      << pkg.manifestPath.string()
                      << " has no commands, script, or language-driven build "
                         "configuration.\n";
            taskStack.pop_back();
            return 1;
        }

        for(const auto& command : effectiveCommands)
        {
            std::string envPrefix;
            for(const auto& entry : effectiveEnv)
            {
                const std::string expandedEnv =
                    expand_task_text(entry, pkg, effectiveTaskBuildConfig);
                if(expandedEnv.empty())
                    continue;
                envPrefix += " " + shell_quote(expandedEnv);
            }
            std::string expanded =
                expand_task_text(command, pkg, effectiveTaskBuildConfig);
            if(!envPrefix.empty())
                expanded = "env" + envPrefix + " " + expanded;
            const std::string fullCommand =
                "cd " + shell_quote(workdir.string()) + " && " +
                command_with_path_entries(expanded,
                                          effectiveTaskBuildConfig.pathEntries);
            const int rc =
                inlineSpinner
                    ? stream_inline_output_command(*inlineSpinner, fullCommand,
                                                   false, effectiveLogOutput)
                    : run_command(fullCommand, true, effectiveLogOutput);
            if(rc != 0)
            {
                if(inlineSpinner)
                    inlineSpinner->stop("[failed]");
                std::cerr << "Task '" << taskName << "' failed for "
                          << pkg.manifestPath.string() << ".\n";
                taskStack.pop_back();
                {
                    std::lock_guard<std::mutex> lock(taskMutex);
                    taskStates[taskName].status = TaskRunState::failed;
                }
                taskCv.notify_all();
                return 1;
            }
        }

        if(!effectiveChmodMode.empty())
        {
            const auto mode = parse_octal_permission_bits(effectiveChmodMode);
            if(!mode.has_value())
            {
                std::cerr << "Task '" << taskName << "' in "
                          << pkg.manifestPath.string()
                          << " has invalid chmod mode '" << effectiveChmodMode
                          << "'. Use an octal mode such as 644 or 755.\n";
                taskStack.pop_back();
                {
                    std::lock_guard<std::mutex> lock(taskMutex);
                    taskStates[taskName].status = TaskRunState::failed;
                }
                taskCv.notify_all();
                return 1;
            }
            if(effectiveChmodPaths.empty())
            {
                std::cerr << "Task '" << taskName << "' in "
                          << pkg.manifestPath.string()
                          << " sets chmod but no chmod_path or chmod_paths.\n";
                taskStack.pop_back();
                {
                    std::lock_guard<std::mutex> lock(taskMutex);
                    taskStates[taskName].status = TaskRunState::failed;
                }
                taskCv.notify_all();
                return 1;
            }
            for(const auto& chmodPathText : effectiveChmodPaths)
            {
                const std::filesystem::path chmodPath = expand_task_text(
                    chmodPathText, pkg, effectiveTaskBuildConfig);
                std::string chmodError;
                if(!apply_recursive_permissions(chmodPath, *mode, chmodError))
                {
                    std::cerr << chmodError << "\n";
                    std::cerr << "Task '" << taskName << "' failed for "
                              << pkg.manifestPath.string() << ".\n";
                    taskStack.pop_back();
                    {
                        std::lock_guard<std::mutex> lock(taskMutex);
                        taskStates[taskName].status = TaskRunState::failed;
                    }
                    taskCv.notify_all();
                    return 1;
                }
            }
        }

        if(effectiveParallel && effectiveNext.size() > 1)
        {
            std::vector<std::future<int>> futures;
            futures.reserve(effectiveNext.size());
            for(const auto& nextTask : effectiveNext)
            {
                std::vector<std::string> childStack = taskStack;
                futures.push_back(std::async(
                    std::launch::async,
                    [&pkg, &tasks, &buildConfig, &hostName, &taskStates,
                     &taskMutex, &taskCv, nextTask, childStack]() mutable
                    {
                        return run_task_for_manifest_impl(
                            pkg, tasks, buildConfig, hostName, taskStates,
                            taskMutex, taskCv, nextTask, childStack);
                    }));
            }
            for(size_t i = 0; i < effectiveNext.size(); ++i)
            {
                if(futures[i].get() != 0)
                {
                    std::cerr << "Task '" << taskName << "' next task '"
                              << effectiveNext[i] << "' failed for "
                              << pkg.manifestPath.string() << ".\n";
                    taskStack.pop_back();
                    {
                        std::lock_guard<std::mutex> lock(taskMutex);
                        taskStates[taskName].status = TaskRunState::failed;
                    }
                    taskCv.notify_all();
                    return 1;
                }
            }
        }
        else
        {
            for(const auto& nextTask : effectiveNext)
            {
                if(run_task_for_manifest_impl(pkg, tasks, buildConfig, hostName,
                                              taskStates, taskMutex, taskCv,
                                              nextTask, taskStack) != 0)
                {
                    std::cerr << "Task '" << taskName << "' next task '"
                              << nextTask << "' failed for "
                              << pkg.manifestPath.string() << ".\n";
                    taskStack.pop_back();
                    {
                        std::lock_guard<std::mutex> lock(taskMutex);
                        taskStates[taskName].status = TaskRunState::failed;
                    }
                    taskCv.notify_all();
                    return 1;
                }
            }
        }
        taskStack.pop_back();
        const auto taskElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - taskStart);
        if(inlineSpinner)
            inlineSpinner->stop("");
        if(const int signRc = codesign_task_outputs(
               task.signOutputs, pkg, buildConfig, taskName, taskPrefix);
           signRc != 0)
        {
            {
                std::lock_guard<std::mutex> lock(taskMutex);
                taskStates[taskName].status = TaskRunState::failed;
            }
            taskCv.notify_all();
            return signRc;
        }
        pkg_task_print_line(taskPrefix + " " + taskName + " Completed, time " +
                            format_task_elapsed_compact(taskElapsed) + " - " +
                            taskDescription);
        {
            std::lock_guard<std::mutex> lock(taskMutex);
            taskStates[taskName].status = TaskRunState::succeeded;
        }
        taskCv.notify_all();
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        taskStates[taskName].status = TaskRunState::failed;
    }
    taskCv.notify_all();
    return -1;
}

static int run_task_for_manifest(const PackageManifest& pkg,
                                 const std::string& taskName,
                                 const BuildConfig& buildConfig)
{
    const auto tasks = parse_task_specs(pkg.content);
    ScopedPackageLogState scopedLogs(
        make_package_log_state(pkg.packageDir, buildConfig));
    const std::string hostName = current_host_name();
    const bool ownProgress = !current_execution_progress_state().active;
    std::optional<ScopedExecutionProgressState> scopedProgress;
    if(ownProgress)
    {
        scopedProgress.emplace(count_reachable_task_steps(
            tasks, hostName, std::vector<std::string>{taskName}));
    }
    std::map<std::string, TaskRunState> taskStates;
    std::mutex taskMutex;
    std::condition_variable taskCv;
    std::vector<std::string> taskStack;
    return run_task_for_manifest_impl(pkg, tasks, buildConfig, hostName,
                                      taskStates, taskMutex, taskCv, taskName,
                                      taskStack);
}

static int run_task_for_manifest(const PackageManifest& pkg,
                                 const std::string& taskName)
{
    return run_task_for_manifest(pkg, taskName,
                                 parse_manifest_build_config(pkg));
}

struct PkgCliOverrides
{
    std::optional<std::string> buildDir;
    std::optional<std::string> depsDir;
    std::optional<std::string> logDir;
    std::optional<std::string> stdoutLog;
    std::optional<std::string> stderrLog;
    std::optional<std::string> warnLog;
    std::optional<bool> taskPrintToStdoutLog;
    std::optional<bool> asan;
    std::map<std::string, std::string> optionValues;
    std::optional<int> signMode;
    bool locked = false;
    bool offline = false;
};

static void apply_cli_overrides(BuildConfig& buildConfig,
                                const PkgCliOverrides& overrides)
{
    const bool enableLogs =
        overrides.logDir.has_value() || overrides.stdoutLog.has_value() ||
        overrides.stderrLog.has_value() || overrides.warnLog.has_value() ||
        overrides.taskPrintToStdoutLog.has_value();
    if(!buildConfig.includedBuildDir.empty())
    {
        namespace fs = std::filesystem;
        const fs::path currentTargetDir(buildConfig.includedBuildDir);
        const std::string target = currentTargetDir.filename().string();
        const fs::path includeRoot =
            currentTargetDir.parent_path().parent_path();
        if(overrides.buildDir.has_value())
        {
            fs::path base(*overrides.buildDir);
            if(base.is_relative())
                base = includeRoot / base;
            buildConfig.includedBuildDir =
                (base / target).lexically_normal().string();
        }
        buildConfig.includedDepsDir =
            (fs::path(buildConfig.includedBuildDir) / "deps").string();
        if(overrides.depsDir.has_value())
        {
            fs::path base(*overrides.depsDir);
            if(base.is_relative())
                base = includeRoot / base;
            buildConfig.includedDepsDir =
                (base / target).lexically_normal().string();
        }
    }
    else
    {
        if(overrides.buildDir.has_value())
            buildConfig.buildDir = *overrides.buildDir;
        if(overrides.depsDir.has_value())
            buildConfig.depsDir = *overrides.depsDir;
    }
    if(overrides.logDir.has_value())
        buildConfig.logDir = *overrides.logDir;
    if(overrides.stdoutLog.has_value())
        buildConfig.stdoutLog = *overrides.stdoutLog;
    if(overrides.stderrLog.has_value())
        buildConfig.stderrLog = *overrides.stderrLog;
    if(overrides.warnLog.has_value())
        buildConfig.warnLog = *overrides.warnLog;
    if(overrides.taskPrintToStdoutLog.has_value())
        buildConfig.taskPrintToStdoutLog = *overrides.taskPrintToStdoutLog;
    if(overrides.asan.has_value())
        buildConfig.asan = *overrides.asan;
    for(const auto& [key, value] : overrides.optionValues)
        buildConfig.optionValues[key] = value;
    if(overrides.signMode.has_value())
        buildConfig.signMode = *overrides.signMode;
    if(enableLogs)
        buildConfig.enableLogs = true;
}

static bool parse_pkg_option_argument(const std::string& text, std::string& key,
                                      std::string& value)
{
    const size_t eq = text.find('=');
    if(eq == std::string::npos)
        return false;
    key = trim(text.substr(0, eq));
    value = trim(text.substr(eq + 1));
    return !key.empty();
}

} // namespace

int PackageManager::run(int argc, char** argv)
{
    const std::string programName = argv[0] ? std::string(argv[0]) : "mlang";
    if(argc < 3)
    {
        print_pkg_usage(programName);
        return 1;
    }

    std::filesystem::path manifestPath = "mlang.toml";
    std::string compilerProgram = argv[0] ? std::string(argv[0]) : "mlang";
    if(!compilerProgram.empty() &&
       compilerProgram.find('/') != std::string::npos)
        compilerProgram = std::filesystem::absolute(compilerProgram).string();

    // Extract `--config FILE` / `--config=FILE` from any position in the pkg
    // arguments so flag ordering is flexible. Build a filtered argv that the
    // rest of the dispatcher and subcommand handlers can consume as usual.
    std::vector<std::string> argStorage;
    argStorage.reserve(static_cast<size_t>(argc));
    argStorage.emplace_back(argv[0] ? argv[0] : "mlang");
    argStorage.emplace_back(argv[1] ? argv[1] : "pkg");
    for(int i = 2; i < argc; ++i)
    {
        std::string arg = argv[i];
        if(arg == "--config")
        {
            if(i + 1 >= argc)
            {
                std::cerr << "--config requires a manifest path\n";
                return 1;
            }
            manifestPath = argv[i + 1];
            ++i;
            continue;
        }
        if(arg.rfind("--config=", 0) == 0)
        {
            std::string value = arg.substr(std::string("--config=").size());
            if(value.empty())
            {
                std::cerr << "--config requires a manifest path\n";
                return 1;
            }
            manifestPath = value;
            continue;
        }
        argStorage.push_back(std::move(arg));
    }

    std::vector<char*> argPtrs;
    argPtrs.reserve(argStorage.size());
    for(auto& s : argStorage)
        argPtrs.push_back(s.data());
    argc = static_cast<int>(argPtrs.size());
    argv = argPtrs.data();

    int subIndex = 2;
    if(subIndex >= argc)
    {
        print_pkg_usage(programName);
        return 1;
    }

    std::string sub = argv[subIndex];
    const std::string manifestLabel = manifestPath.string();

    const auto is_pkg_subcommand = [](const std::string& value)
    {
        return value == "--help" || value == "-h" || value == "help" ||
               value == "--tests" || value == "init" || value == "add" ||
               value == "lock" || value == "verify" || value == "tree" ||
               value == "why" || value == "fetch" || value == "build" ||
               value == "run" || value == "clean";
    };

    if(!is_pkg_subcommand(sub))
    {
        bool printTasks = false;
        bool colorTasks = false;
        bool shorthandArgsOk = true;
        std::vector<std::filesystem::path> shorthandManifests;
        for(int i = subIndex; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--tasks")
                printTasks = true;
            else if(arg == "--color")
                colorTasks = true;
            else if(arg.size() >= 5 && arg.substr(arg.size() - 5) == ".toml")
                shorthandManifests.push_back(arg);
            else
            {
                shorthandArgsOk = false;
                break;
            }
        }
        if(shorthandArgsOk && (printTasks || colorTasks) &&
           !shorthandManifests.empty())
        {
            for(const auto& shorthandManifest : shorthandManifests)
            {
                manifestPath = shorthandManifest;
                if(!std::filesystem::exists(manifestPath))
                {
                    std::cerr << manifestPath.string()
                              << " not found. Run 'mlang pkg init' first.\n";
                    return 1;
                }
                auto manifests = collect_target_manifests(manifestPath);
                if(manifests.empty())
                {
                    std::cerr
                        << "No package manifests found for task overview in "
                        << manifestPath.string() << ".\n";
                    return 1;
                }
                for(const auto& pkg : manifests)
                {
                    BuildConfig buildConfig = parse_manifest_build_config(pkg);
                    buildConfig.compilerProgram = compilerProgram;
                    if(print_task_overview_for_manifest(pkg, buildConfig,
                                                        colorTasks) != 0)
                    {
                        return 1;
                    }
                }
            }
            return 0;
        }
    }

    if(sub == "--help" || sub == "-h" || sub == "help")
    {
        print_pkg_usage(programName);
        return 0;
    }

    if(sub == "--tests")
    {
        if(subIndex + 1 >= argc)
        {
            std::cerr
                << "Usage: " << argv[0]
                << " pkg --tests [--tasks] [--color] <manifest.toml>...\n";
            return 1;
        }

        bool printTasks = false;
        bool colorTasks = false;
        std::vector<std::filesystem::path> testManifestPaths;
        for(int i = subIndex + 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--tasks")
                printTasks = true;
            else if(arg == "--color")
                colorTasks = true;
            else if(!arg.empty() && arg[0] == '-')
            {
                std::cerr
                    << "Unknown option for 'pkg --tests': " << arg << "\n"
                    << "Usage: " << argv[0]
                    << " pkg --tests [--tasks] [--color] <manifest.toml>...\n";
                return 1;
            }
            else
            {
                testManifestPaths.push_back(arg);
            }
        }

        if(testManifestPaths.empty())
        {
            std::cerr
                << "Usage: " << argv[0]
                << " pkg --tests [--tasks] [--color] <manifest.toml>...\n";
            return 1;
        }

        bool foundTests = false;
        for(const auto& testManifestPath : testManifestPaths)
        {
            if(!std::filesystem::exists(testManifestPath))
            {
                std::cerr << testManifestPath.string() << " not found.\n";
                return 1;
            }

            auto manifests = collect_target_manifests(testManifestPath);
            if(manifests.empty())
            {
                std::cerr << "No package manifests found for tests in "
                          << testManifestPath.string() << ".\n";
                return 1;
            }

            bool foundTestsInManifest = false;
            for(const auto& pkg : manifests)
            {
                BuildConfig buildConfig = parse_manifest_build_config(pkg);
                buildConfig.compilerProgram = compilerProgram;
                const auto tasks = parse_task_specs(pkg.content);
                const std::string hostName = current_host_name();
                const std::vector<std::string> testRoots = task_roots_for_phase(
                    tasks, hostName, "test",
                    std::vector<std::string>{"tests", "test"});
                if(testRoots.empty())
                    continue;

                foundTests = true;
                foundTestsInManifest = true;
                if(printTasks)
                {
                    for(const auto& taskName : testRoots)
                    {
                        const int rc = print_task_plan_for_manifest(
                            pkg, taskName, buildConfig, colorTasks);
                        if(rc != 0)
                            return 1;
                    }
                    continue;
                }

                int rc = 0;
                {
                    ScopedPackageLogState scopedLogs(
                        make_package_log_state(pkg.packageDir, buildConfig));
                    const auto deps = parse_source_deps(pkg.content);
                    size_t totalSteps = 0;
                    const std::filesystem::path depsDir =
                        package_deps_dir(pkg.packageDir, buildConfig);
                    for(const auto& dep : deps)
                    {
                        totalSteps += count_fetch_dep_steps(
                            dep, depsDir, /*updateExisting=*/true);
                    }
                    totalSteps +=
                        count_reachable_task_steps(tasks, hostName, testRoots);
                    ScopedExecutionProgressState scopedProgress(totalSteps);
                    if(fetch_for_manifest(pkg, buildConfig) != 0)
                        return 1;
                    std::map<std::string, TaskRunState> taskStates;
                    std::mutex taskMutex;
                    std::condition_variable taskCv;
                    std::vector<std::string> taskStack;
                    for(const auto& taskName : testRoots)
                    {
                        rc = run_task_for_manifest_impl(
                            pkg, tasks, buildConfig, hostName, taskStates,
                            taskMutex, taskCv, taskName, taskStack);
                        if(rc != 0)
                            break;
                    }
                }
                if(rc != 0)
                    return 1;
            }

            if(!foundTestsInManifest)
            {
                std::cerr << "No phase=\"test\" tasks found in "
                          << testManifestPath.string() << "\n";
                return 1;
            }
        }
        return 0;
    }

    if(sub == "init")
    {
        if(std::filesystem::exists(manifestPath))
        {
            std::cerr << manifestLabel << " already exists\n";
            return 1;
        }

        std::string name = std::filesystem::current_path().filename().string();
        if(name.empty())
            name = "app";

        std::ofstream out(manifestPath, std::ios::binary);
        if(!out)
        {
            std::cerr << "Failed to write " << manifestLabel << "\n";
            return 1;
        }
        out << "[package]\n"
            << "name = \"" << name << "\"\n"
            << "version = \"" << MLANG_VERSION << "\"\n"
            << "entry = \"src/main.mla\"\n\n"
            << "[dependencies]\n\n"
            << "[c-dependencies]\n";

        if(!write_text_file_if_missing(std::filesystem::path("src") /
                                           "main.mla",
                                       package_stub_source(name)))
        {
            std::cerr << "Failed to write src/main.mla\n";
            return 1;
        }
        return 0;
    }

    if(sub == "add")
    {
        if(subIndex + 1 >= argc)
        {
            std::cerr << "Usage: " << argv[0]
                      << " pkg [--config FILE] add <name> [--git URL] [--rev "
                         "REV] [--tag TAG] [--submodules] [--version REQ]\n"
                      << "       " << argv[0]
                      << " pkg [--config FILE] add <name> --path DIR "
                         "[--version REQ]\n"
                      << "       " << argv[0]
                      << " pkg [--config FILE] add <name> --url URL [--archive "
                         "tar.gz] [--strip-components N] [--subdir DIR]\n"
                      << "       " << argv[0]
                      << " pkg [--config FILE] add <name> [--pkg-config NAME] "
                         "[--system]\n"
                      << "       " << argv[0]
                      << " pkg [--config FILE] add <name> [--git URL|--url "
                         "URL] --add-lib [--project-dir DIR]\n";
            return 1;
        }

        std::string name = argv[subIndex + 1];
        std::string gitUrl;
        std::string archiveUrl;
        std::string archiveType;
        std::string dependencyPath;
        std::string versionRequirement;
        std::string rev;
        std::string tag;
        bool gitSubmodules = false;
        std::string depSubdir;
        std::string pkgConfig;
        std::string addLibProjectDir;
        bool systemDep = false;
        bool addLib = false;
        int stripComponents = 1;

        for(int i = subIndex + 2; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--git" && i + 1 < argc)
                gitUrl = argv[++i];
            else if(arg == "--url" && i + 1 < argc)
                archiveUrl = argv[++i];
            else if(arg == "--archive" && i + 1 < argc)
                archiveType = argv[++i];
            else if(arg == "--path" && i + 1 < argc)
                dependencyPath = argv[++i];
            else if(arg == "--version" && i + 1 < argc)
                versionRequirement = argv[++i];
            else if(arg == "--rev" && i + 1 < argc)
                rev = argv[++i];
            else if(arg == "--tag" && i + 1 < argc)
                tag = argv[++i];
            else if(arg == "--submodules")
                gitSubmodules = true;
            else if(arg == "--subdir" && i + 1 < argc)
                depSubdir = argv[++i];
            else if(arg == "--strip-components" && i + 1 < argc)
                stripComponents = parse_int_or_default(argv[++i], 1);
            else if(arg == "--pkg-config" && i + 1 < argc)
                pkgConfig = argv[++i];
            else if(arg == "--project-dir" && i + 1 < argc)
                addLibProjectDir = argv[++i];
            else if(arg == "--add-lib")
                addLib = true;
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
            std::cerr << manifestLabel
                      << " not found. Run 'mlang pkg init' first.\n";
            return 1;
        }

        std::ifstream in(manifestPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if(content.empty())
        {
            std::cerr << "Failed to read " << manifestLabel << "\n";
            return 1;
        }

        if(addLib && (systemDep || !pkgConfig.empty()))
        {
            std::cerr << "--add-lib currently supports Git or tarball source "
                         "dependencies only.\n";
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
        else
        {
            const int sourceCount = static_cast<int>(!dependencyPath.empty()) +
                                    static_cast<int>(!gitUrl.empty()) +
                                    static_cast<int>(!archiveUrl.empty());
            if(sourceCount > 1)
            {
                std::cerr << "Choose exactly one of --path, --git, or --url.\n";
                return 1;
            }
            if(!dependencyPath.empty())
            {
                line = name + " = { path = \"" + dependencyPath + "\"";
                if(!versionRequirement.empty())
                    line += ", version = \"" + versionRequirement + "\"";
                line += " }";
            }
            else
            {
                line = make_dependency_manifest_line(
                    name, gitUrl, archiveUrl, archiveType, rev, tag,
                    gitSubmodules, depSubdir, stripComponents);
                if(!versionRequirement.empty() && !line.empty() &&
                   line.back() == '}')
                    line.insert(line.size() - 1,
                                ", version = \"" + versionRequirement + "\"");
            }
        }

        if(addLib)
        {
            namespace fs = std::filesystem;
            fs::path rootDir = manifestPath.parent_path();
            fs::path projectDir = addLibProjectDir.empty()
                                      ? default_added_package_dir(name)
                                      : fs::path(addLibProjectDir);
            fs::path projectDirAbs = rootDir / projectDir;
            std::string packageName = sanitize_package_name(name) + "_demo";
            std::string error;
            if(!scaffold_added_package(manifestPath, projectDirAbs, packageName,
                                       line, name, error))
            {
                std::cerr << error << "\n";
                return 1;
            }
            if(!ensure_package_entry_stub(manifestPath, content,
                                          "workspace_root", error))
            {
                std::cerr << error << "\n";
                return 1;
            }

            const fs::path memberRoot = projectDir.begin() == projectDir.end()
                                            ? fs::path(".")
                                            : *projectDir.begin();
            append_unique_quoted_list_entry(content, "workspace", "members",
                                            memberRoot.generic_string());

            std::ofstream rootOut(manifestPath,
                                  std::ios::binary | std::ios::trunc);
            if(!rootOut)
            {
                std::cerr << "Failed to update root " << manifestLabel << "\n";
                return 1;
            }
            rootOut << content;

            std::cout << "Scaffolded subproject at "
                      << projectDir.lexically_normal().string() << "\n";
            return 0;
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
            std::cerr << "Failed to update " << manifestLabel << "\n";
            return 1;
        }
        out << content;
        return 0;
    }

    if(sub == "tree" || sub == "why")
    {
        if(sub == "tree" && subIndex + 1 != argc)
        {
            std::cerr << "Usage: " << argv[0]
                      << " pkg [--config FILE] tree\n";
            return 1;
        }
        if(sub == "why" && subIndex + 2 != argc)
        {
            std::cerr << "Usage: " << argv[0]
                      << " pkg [--config FILE] why <package>\n";
            return 1;
        }
        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << manifestLabel << " not found.\n";
            return 1;
        }
        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for " << sub << ".\n";
            return 1;
        }
        return sub == "tree"
                   ? print_dependency_tree(manifests)
                   : print_dependency_why(manifests, argv[subIndex + 1]);
    }

    if(sub == "lock" || sub == "verify")
    {
        PkgCliOverrides overrides;
        for(int i = subIndex + 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if(arg == "--build-dir" && i + 1 < argc)
                overrides.buildDir = argv[++i];
            else if(arg == "--deps-dir" && i + 1 < argc)
                overrides.depsDir = argv[++i];
            else if(arg == "--offline" && sub == "lock")
                overrides.offline = true;
            else
            {
                std::cerr << "Unknown option for 'pkg " << sub << "': " << arg
                          << "\nUsage: " << argv[0] << " pkg [--config FILE] "
                          << sub << " [--build-dir DIR] [--deps-dir DIR]";
                if(sub == "lock")
                    std::cerr << " [--offline]";
                std::cerr << "\n";
                return 1;
            }
        }
        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << manifestLabel
                      << " not found. Run 'mlang pkg init' first.\n";
            return 1;
        }
        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for " << sub << ".\n";
            return 1;
        }

        DependencyLockContext lockContext;
        const bool verifyOnly = sub == "verify";
        if(!prepare_dependency_lock_context(
               manifestPath, manifests, verifyOnly,
               verifyOnly || overrides.offline, lockContext))
            return 1;
        ScopedDependencyLockContext scopedLock(lockContext);
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_manifest_build_config(pkg);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
            if(verifyOnly)
            {
                if(verify_manifest_dependencies(pkg, buildConfig) != 0)
                    return 1;
            }
            else if(fetch_for_manifest(pkg, buildConfig) != 0)
            {
                return 1;
            }
        }
        if(!finish_dependency_lock_context(lockContext))
            return 1;
        pkg_info_line(verifyOnly ? "mlang.lock and fetched dependencies verified."
                                 : "mlang.lock is up to date.");
        return 0;
    }

    if(sub == "fetch")
    {
        PkgCliOverrides overrides;
        for(int i = subIndex + 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--build-dir" && i + 1 < argc)
            {
                overrides.buildDir = argv[++i];
            }
            else if(arg == "--deps-dir" && i + 1 < argc)
            {
                overrides.depsDir = argv[++i];
            }
            else if(arg == "--log-dir" && i + 1 < argc)
            {
                overrides.logDir = argv[++i];
            }
            else if(arg == "--stdout-log" && i + 1 < argc)
            {
                overrides.stdoutLog = argv[++i];
            }
            else if(arg == "--stderr-log" && i + 1 < argc)
            {
                overrides.stderrLog = argv[++i];
            }
            else if(arg == "--warn-log" && i + 1 < argc)
            {
                overrides.warnLog = argv[++i];
            }
            else if(arg == "--task-print-to-stdout-log")
            {
                overrides.taskPrintToStdoutLog = true;
            }
            else if(arg == "--locked")
            {
                overrides.locked = true;
            }
            else if(arg == "--offline")
            {
                overrides.offline = true;
            }
            else
            {
                std::cerr << "Unknown option for 'pkg fetch': " << arg << "\n"
                          << "Usage: " << argv[0]
                          << " pkg [--config FILE] fetch [--build-dir DIR] "
                             "[--deps-dir DIR]"
                          << " [--log-dir DIR] [--stdout-log FILE]"
                          << " [--stderr-log FILE] [--warn-log FILE]"
                          << " [--task-print-to-stdout-log] [--locked]"
                          << " [--offline]\n";
                return 1;
            }
        }
        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << manifestLabel
                      << " not found. Run 'mlang pkg init' first.\n";
            return 1;
        }
        std::ifstream in(manifestPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if(content.empty())
        {
            std::cerr << "Failed to read " << manifestLabel << "\n";
            return 1;
        }
        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for fetch.\n";
            return 1;
        }
        DependencyLockContext lockContext;
        if(!prepare_dependency_lock_context(manifestPath, manifests,
                                            overrides.locked,
                                            overrides.offline, lockContext))
            return 1;
        ScopedDependencyLockContext scopedLock(lockContext);
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_manifest_build_config(pkg);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
            if(fetch_for_manifest(pkg, buildConfig) != 0)
                return 1;
        }
        if(!finish_dependency_lock_context(lockContext))
            return 1;
        return 0;
    }

    if(sub == "build")
    {
        std::string optFlag;
        bool useNinja = false;
        PkgCliOverrides overrides;
        for(int i = subIndex + 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "-O0" || arg == "-Og" || arg == "-O1" || arg == "-O2" ||
               arg == "-O3" || arg == "-Os" || arg == "-Oz")
            {
                optFlag = arg;
            }
            else if(arg == "--ninja")
            {
                useNinja = true;
            }
            else if(arg == "--asan")
            {
                overrides.asan = true;
            }
            else if(arg == "--sign")
            {
                overrides.signMode = 1;
            }
            else if(arg == "--force-sign" || arg == "--forcesign")
            {
                overrides.signMode = 2;
            }
            else if(arg == "--no-sign")
            {
                overrides.signMode = 0;
            }
            else if(arg == "--build-dir" && i + 1 < argc)
            {
                overrides.buildDir = argv[++i];
            }
            else if(arg == "--deps-dir" && i + 1 < argc)
            {
                overrides.depsDir = argv[++i];
            }
            else if(arg == "--log-dir" && i + 1 < argc)
            {
                overrides.logDir = argv[++i];
            }
            else if(arg == "--stdout-log" && i + 1 < argc)
            {
                overrides.stdoutLog = argv[++i];
            }
            else if(arg == "--stderr-log" && i + 1 < argc)
            {
                overrides.stderrLog = argv[++i];
            }
            else if(arg == "--warn-log" && i + 1 < argc)
            {
                overrides.warnLog = argv[++i];
            }
            else if(arg == "--task-print-to-stdout-log")
            {
                overrides.taskPrintToStdoutLog = true;
            }
            else if(arg == "--locked")
            {
                overrides.locked = true;
            }
            else if(arg == "--offline")
            {
                overrides.offline = true;
            }
            else
            {
                std::cerr << "Unknown option for 'pkg build': " << arg << "\n"
                          << "Usage: " << argv[0]
                          << " pkg [--config FILE] build "
                             "[-O0|-Og|-O1|-O2|-O3|-Os|-Oz] [--ninja] [--asan]"
                          << " [--sign|--force-sign|--no-sign]"
                          << " [--build-dir DIR] [--deps-dir DIR]"
                          << " [--log-dir DIR] [--stdout-log FILE]"
                          << " [--stderr-log FILE] [--warn-log FILE]"
                          << " [--task-print-to-stdout-log] [--locked]"
                          << " [--offline]\n";
                return 1;
            }
        }

        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << manifestLabel
                      << " not found. Run 'mlang pkg init' first.\n";
            return 1;
        }

        std::ifstream in(manifestPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        if(content.empty())
        {
            std::cerr << "Failed to read " << manifestLabel << "\n";
            return 1;
        }

        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for build.\n";
            return 1;
        }
        DependencyLockContext lockContext;
        if(!prepare_dependency_lock_context(manifestPath, manifests,
                                            overrides.locked,
                                            overrides.offline, lockContext))
            return 1;
        ScopedDependencyLockContext scopedLock(lockContext);
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_manifest_build_config(pkg);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
            if(build_for_manifest(pkg, argv[0], optFlag, useNinja,
                                  buildConfig) != 0)
                return 1;
        }
        if(!finish_dependency_lock_context(lockContext))
            return 1;
        return 0;
    }

    if(sub == "run")
    {
        if(subIndex + 1 >= argc)
        {
            std::cerr << "Usage: " << argv[0]
                      << " pkg [--config FILE] run <task> [--tasks] [--color] "
                         "[--build-dir DIR] [--deps-dir DIR] [--log-dir DIR] "
                         "[--stdout-log FILE]"
                      << " [--stderr-log FILE] [--warn-log FILE]"
                      << " [--task-print-to-stdout-log] [--asan]"
                      << " [--sign|--force-sign|--no-sign]"
                      << " [--option KEY=VALUE]\n";
            return 1;
        }
        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << manifestLabel
                      << " not found. Run 'mlang pkg init' first.\n";
            return 1;
        }

        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for run.\n";
            return 1;
        }

        const std::string taskName = argv[subIndex + 1];
        PkgCliOverrides overrides;
        bool printTasks = false;
        bool colorTasks = false;
        for(int i = subIndex + 2; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--tasks")
                printTasks = true;
            else if(arg == "--color")
                colorTasks = true;
            else if(arg == "--log-dir" && i + 1 < argc)
                overrides.logDir = argv[++i];
            else if(arg == "--stdout-log" && i + 1 < argc)
                overrides.stdoutLog = argv[++i];
            else if(arg == "--stderr-log" && i + 1 < argc)
                overrides.stderrLog = argv[++i];
            else if(arg == "--warn-log" && i + 1 < argc)
                overrides.warnLog = argv[++i];
            else if(arg == "--task-print-to-stdout-log")
                overrides.taskPrintToStdoutLog = true;
            else if(arg == "--asan")
                overrides.asan = true;
            else if(arg == "--sign")
                overrides.signMode = 1;
            else if(arg == "--force-sign" || arg == "--forcesign")
                overrides.signMode = 2;
            else if(arg == "--no-sign")
                overrides.signMode = 0;
            else if(arg == "--locked")
                overrides.locked = true;
            else if(arg == "--offline")
                overrides.offline = true;
            else if(arg == "--build-dir" && i + 1 < argc)
                overrides.buildDir = argv[++i];
            else if(arg == "--deps-dir" && i + 1 < argc)
                overrides.depsDir = argv[++i];
            else if(arg == "--option" && i + 1 < argc)
            {
                std::string key;
                std::string value;
                if(!parse_pkg_option_argument(argv[++i], key, value))
                {
                    std::cerr << "--option requires KEY=VALUE\n";
                    return 1;
                }
                overrides.optionValues[key] = value;
            }
            else if(arg.rfind("--option=", 0) == 0)
            {
                std::string key;
                std::string value;
                if(!parse_pkg_option_argument(
                       arg.substr(std::string("--option=").size()), key, value))
                {
                    std::cerr << "--option requires KEY=VALUE\n";
                    return 1;
                }
                overrides.optionValues[key] = value;
            }
            else
            {
                std::cerr << "Unknown option for 'pkg run': " << arg << "\n"
                          << "Usage: " << argv[0]
                          << " pkg [--config FILE] run <task> [--tasks] "
                             "[--color] [--build-dir DIR] [--deps-dir DIR]"
                          << " [--log-dir DIR] [--stdout-log FILE]"
                          << " [--stderr-log FILE] [--warn-log FILE]"
                          << " [--task-print-to-stdout-log] [--asan]"
                          << " [--sign|--force-sign|--no-sign]"
                          << " [--option KEY=VALUE] [--locked] [--offline]\n";
                return 1;
            }
        }
        DependencyLockContext lockContext;
        std::optional<ScopedDependencyLockContext> scopedLock;
        if(!printTasks)
        {
            if(!prepare_dependency_lock_context(manifestPath, manifests,
                                                overrides.locked,
                                                overrides.offline,
                                                lockContext))
                return 1;
            scopedLock.emplace(lockContext);
        }
        bool found = false;
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_manifest_build_config(pkg);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
            if(printTasks)
            {
                const int rc = print_task_plan_for_manifest(
                    pkg, taskName, buildConfig, colorTasks);
                if(rc == -1)
                    continue;
                found = true;
                if(rc != 0)
                    return 1;
                continue;
            }
            int rc;
            {
                ScopedPackageLogState scopedLogs(
                    make_package_log_state(pkg.packageDir, buildConfig));
                const auto deps = parse_source_deps(pkg.content);
                const auto tasks = parse_task_specs(pkg.content);
                const std::string hostName = current_host_name();
                size_t totalSteps = 0;
                const std::filesystem::path depsDir =
                    package_deps_dir(pkg.packageDir, buildConfig);
                for(const auto& dep : deps)
                {
                    totalSteps += count_fetch_dep_steps(
                        dep, depsDir, /*updateExisting=*/true);
                }
                totalSteps += count_reachable_task_steps(
                    tasks, hostName, std::vector<std::string>{taskName});
                ScopedExecutionProgressState scopedProgress(totalSteps);
                if(fetch_for_manifest(pkg, buildConfig) != 0)
                    return 1;
                std::map<std::string, TaskRunState> taskStates;
                std::mutex taskMutex;
                std::condition_variable taskCv;
                std::vector<std::string> taskStack;
                rc = run_task_for_manifest_impl(pkg, tasks, buildConfig,
                                                hostName, taskStates, taskMutex,
                                                taskCv, taskName, taskStack);
            }
            if(rc == -1)
                continue;
            found = true;
            if(rc != 0)
                return 1;
        }
        if(!found)
        {
            std::cerr << "Task not found: " << taskName << "\n";
            return 1;
        }
        if(!printTasks && !finish_dependency_lock_context(lockContext))
            return 1;
        return 0;
    }

    if(sub == "clean")
    {
        PkgCliOverrides overrides;
        bool cleanDeps = false;
        for(int i = subIndex + 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--build-dir" && i + 1 < argc)
            {
                overrides.buildDir = argv[++i];
            }
            else if(arg == "--deps-dir" && i + 1 < argc)
            {
                overrides.depsDir = argv[++i];
            }
            else if(arg == "--log-dir" && i + 1 < argc)
            {
                overrides.logDir = argv[++i];
            }
            else if(arg == "--stdout-log" && i + 1 < argc)
            {
                overrides.stdoutLog = argv[++i];
            }
            else if(arg == "--stderr-log" && i + 1 < argc)
            {
                overrides.stderrLog = argv[++i];
            }
            else if(arg == "--warn-log" && i + 1 < argc)
            {
                overrides.warnLog = argv[++i];
            }
            else if(arg == "--task-print-to-stdout-log")
            {
                overrides.taskPrintToStdoutLog = true;
            }
            else if(arg == "--deps")
            {
                cleanDeps = true;
            }
            else
            {
                std::cerr << "Unknown option for 'pkg clean': " << arg << "\n"
                          << "Usage: " << argv[0]
                          << " pkg [--config FILE] clean [--build-dir DIR] "
                             "[--deps-dir DIR]"
                          << " [--log-dir DIR] [--stdout-log FILE]"
                          << " [--stderr-log FILE] [--warn-log FILE]"
                          << " [--task-print-to-stdout-log] [--deps]\n";
                return 1;
            }
        }
        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            BuildConfig buildConfig;
            apply_cli_overrides(buildConfig, overrides);
            ScopedPackageLogState scopedLogs(make_package_log_state(
                std::filesystem::current_path(), buildConfig));
            const std::filesystem::path buildDir =
                package_build_dir(std::filesystem::current_path(), buildConfig);
            if(!std::filesystem::exists(buildDir))
            {
                pkg_warn_line("No artifacts to clean in " + buildDir.string());
                return 0;
            }
            std::error_code ec;
            std::filesystem::remove_all(buildDir, ec);
            if(ec)
            {
                pkg_error_line("Failed to clean " + buildDir.string() + ": " +
                               ec.message());
                return 1;
            }
            pkg_info_line("Cleaned " + buildDir.string());
            return 0;
        }
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_manifest_build_config(pkg);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
            if(clean_for_manifest(pkg, buildConfig) != 0)
                return 1;
            if(cleanDeps)
            {
                const std::filesystem::path depsDir =
                    package_deps_dir(pkg.packageDir, buildConfig);
                const std::filesystem::path buildDir =
                    package_build_dir(pkg.packageDir, buildConfig);
                if(depsDir == buildDir || depsDir == buildDir / "deps")
                    continue;
                ScopedPackageLogState scopedLogs(
                    make_package_log_state(pkg.packageDir, buildConfig));
                if(!std::filesystem::exists(depsDir))
                {
                    pkg_warn_line("No fetched dependencies to clean in " +
                                  depsDir.string());
                    continue;
                }
                std::error_code ec;
                std::filesystem::remove_all(depsDir, ec);
                if(ec)
                {
                    pkg_error_line("Failed to clean " + depsDir.string() +
                                   ": " + ec.message());
                    return 1;
                }
                pkg_info_line("Cleaned " + depsDir.string());
            }
        }
        return 0;
    }

    std::cerr << "Unknown pkg subcommand: " << sub << "\n";
    return 1;
}
