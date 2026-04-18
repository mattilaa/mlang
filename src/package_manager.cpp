#include "package_manager.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <condition_variable>
#include <sys/wait.h>
#include <unordered_set>
#include <vector>

#ifndef MLANG_VERSION
#define MLANG_VERSION "0.1.0"
#endif

namespace {

struct PackageManifest;
struct BuildConfig;

static std::vector<std::string> split_toml_array(std::string_view input);
static std::string shell_quote(const std::string& s);
static std::string unquote(std::string_view v);
static std::string unquote_preserve(std::string_view v);
static void append_toml_command_value(const std::string& value,
                                      std::vector<std::string>& out);
static void append_toml_commands_value(const std::string& value,
                                       std::vector<std::string>& out);
static std::string format_task_elapsed(std::chrono::milliseconds elapsed);
static std::string shorten_progress_description(const std::string& description);
static std::string sanitize_progress_output(std::string text);
static std::string shorten_progress_output(const std::string& text);
static std::vector<std::string> parse_workspace_members(
    const std::string& content);
static std::string current_host_name();
static void append_toml_string_list_value(const std::string& value,
                                          std::vector<std::string>& out);
static bool parse_toml_bool_value(const std::string& value);
static int run_task_for_manifest(const PackageManifest& pkg,
                                 const std::string& taskName);
static int run_task_for_manifest(const PackageManifest& pkg,
                                 const std::string& taskName,
                                 const BuildConfig& buildConfig);
static void append_toml_string_list_value_preserve(const std::string& value,
                                                   std::vector<std::string>& out);

enum class TaskTomlKey
{
    Name,
    Message,
    Print,
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
static std::optional<std::string_view> find_enum_text(
    Enum key, const std::array<std::pair<Enum, std::string_view>, N>& mappings)
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

static constexpr std::array<std::pair<TaskTomlKey, std::string_view>, 36>
    kTaskTomlKeys {{{ TaskTomlKey::Name, "name" },
                    { TaskTomlKey::Message, "message" },
                    { TaskTomlKey::Print, "print" },
                    { TaskTomlKey::Phase, "phase" },
                    { TaskTomlKey::Workdir, "workdir" },
                    { TaskTomlKey::Language, "language" },
                    { TaskTomlKey::Source, "source" },
                    { TaskTomlKey::Output, "output" },
                    { TaskTomlKey::Inputs, "inputs" },
                    { TaskTomlKey::CompileOnly, "compile_only" },
                    { TaskTomlKey::Parallel, "parallel" },
                    { TaskTomlKey::LogOutput, "log_output" },
                    { TaskTomlKey::InlineOutput, "inline_output" },
                    { TaskTomlKey::DependsOn, "depends_on" },
                    { TaskTomlKey::PhaseDependsOn, "phase_depends_on" },
                    { TaskTomlKey::JoinOn, "join_on" },
                    { TaskTomlKey::PhaseJoinOn, "phase_join_on" },
                    { TaskTomlKey::Next, "next" },
                    { TaskTomlKey::NextPhases, "next_phases" },
                    { TaskTomlKey::Command, "command" },
                    { TaskTomlKey::Env, "env" },
                    { TaskTomlKey::Script, "script" },
                    { TaskTomlKey::Shell, "shell" },
                    { TaskTomlKey::Commands, "commands" },
                    { TaskTomlKey::Chmod, "chmod" },
                    { TaskTomlKey::ChmodPath, "chmod_path" },
                    { TaskTomlKey::ChmodPaths, "chmod_paths" },
                    { TaskTomlKey::OptLevel, "opt_level" },
                    { TaskTomlKey::TargetArch, "target_arch" },
                    { TaskTomlKey::PathEntries, "path_entries" },
                    { TaskTomlKey::CompilerFlags, "compiler_flags" },
                    { TaskTomlKey::LinkerFlags, "linker_flags" },
                    { TaskTomlKey::LibPaths, "lib_paths" },
                    { TaskTomlKey::Libs, "libs" },
                    { TaskTomlKey::StaticDeps, "static_deps" },
                    { TaskTomlKey::StaticCppRuntime, "static_cpp_runtime" } }};

static constexpr std::array<std::pair<TaskLanguageAlias, std::string_view>, 6>
    kTaskLanguageAliases {{{ TaskLanguageAlias::Mlang, "mlang" },
                            { TaskLanguageAlias::C, "c" },
                            { TaskLanguageAlias::Cpp, "cpp" },
                            { TaskLanguageAlias::Cxx, "cxx" },
                            { TaskLanguageAlias::CPlusPlus, "c++" },
                            { TaskLanguageAlias::CC, "cc" } }};

static constexpr std::array<std::pair<TaskLanguage, std::string_view>, 3>
    kTaskLanguageNames {{{ TaskLanguage::Mlang, "mlang" },
                          { TaskLanguage::C, "c" },
                          { TaskLanguage::Cpp, "c++" } }};

static constexpr std::array<std::pair<TargetArchAlias, std::string_view>, 9>
    kTargetArchAliases {{{ TargetArchAlias::X86, "x86" },
                          { TargetArchAlias::I386, "i386" },
                          { TargetArchAlias::I686, "i686" },
                          { TargetArchAlias::X64, "x64" },
                          { TargetArchAlias::X86_64, "x86_64" },
                          { TargetArchAlias::Amd64, "amd64" },
                          { TargetArchAlias::X86Dash64, "x86-64" },
                          { TargetArchAlias::Aarch64, "aarch64" },
                          { TargetArchAlias::Arm64, "arm64" } }};

static constexpr std::array<std::pair<TargetArchValue, std::string_view>, 3>
    kTargetArchNames {{{ TargetArchValue::X86, "x86" },
                        { TargetArchValue::X64, "x64" },
                        { TargetArchValue::Aarch64, "aarch64" } }};

static constexpr std::array<std::pair<OptLevelAlias, std::string_view>, 7>
    kOptLevelAliases {{{ OptLevelAlias::O0, "-O0" },
                        { OptLevelAlias::Og, "-Og" },
                        { OptLevelAlias::O1, "-O1" },
                        { OptLevelAlias::O2, "-O2" },
                        { OptLevelAlias::O3, "-O3" },
                        { OptLevelAlias::Os, "-Os" },
                        { OptLevelAlias::Oz, "-Oz" } }};

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

static bool is_section_line(const std::string& line,
                            const std::string& section)
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

static std::optional<std::string> find_section_toml_string(
    const std::string& content, const std::string& section,
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

static std::optional<ParsedTomlAssignment> parse_toml_assignment(
    const std::string& line, std::istream& in)
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
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
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
    std::string makeProgram = "make";
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
    std::optional<bool> staticDeps;
    std::optional<bool> staticCppRuntime;
    std::optional<bool> taskPrintToStdoutLog;
    bool enableLogs = false;
};

struct BuildTarget
{
    std::string name;
    std::string entry;
    BuildConfig config;
};

struct TaskSpec
{
    std::string name;
    std::string message;
    std::string print;
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
    BuildConfig buildConfig;
    struct HostOverride
    {
        std::string message;
        std::string print;
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
        BuildConfig buildConfig;
    };
    std::map<std::string, HostOverride> hostOverrides;
};

static void parse_build_config_key_value(BuildConfig& cfg,
                                         const std::string& key,
                                         const std::string& value);

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
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
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

static std::filesystem::perms
directory_perms_from_file_mode(unsigned int mode)
{
    std::filesystem::perms perms = std::filesystem::perms::none;
    auto add_if = [&](bool enabled, std::filesystem::perms bit) {
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
    auto add_if = [&](bool enabled, std::filesystem::perms bit) {
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

static bool apply_recursive_permissions(
    const std::filesystem::path& path, unsigned int mode, std::string& error)
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
    auto apply_one = [&](const fs::path& current) -> bool {
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
        const fs::perms perms =
            fs::is_directory(status) ? dirPerms : filePerms;
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
        if(hostIt != task.hostOverrides.end() &&
           !hostIt->second.phase.empty())
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
    out.pathEntries.insert(out.pathEntries.end(), overrideCfg.pathEntries.begin(),
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

static std::filesystem::path resolve_package_path(
    const std::filesystem::path& packageDir, const std::string& configured,
    const std::filesystem::path& fallback)
{
    std::filesystem::path path =
        configured.empty() ? fallback : std::filesystem::path(configured);
    if(path.is_relative())
        path = packageDir / path;
    return path.lexically_normal();
}

static std::filesystem::path package_build_dir(const std::filesystem::path& packageDir,
                                               const BuildConfig& config)
{
    return resolve_package_path(packageDir, config.buildDir, "build");
}

static std::filesystem::path package_deps_dir(const std::filesystem::path& packageDir,
                                              const BuildConfig& config)
{
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

static std::optional<std::filesystem::path> resolve_log_path(
    const std::filesystem::path& packageDir, const BuildConfig& config,
    const std::string& configured, const char* defaultName = nullptr)
{
    if(configured.empty() && defaultName == nullptr)
        return std::nullopt;

    std::filesystem::path path =
        configured.empty() ? std::filesystem::path(defaultName)
                           : std::filesystem::path(configured);
    if(path.is_absolute())
        return path.lexically_normal();

    if(!config.logDir.empty())
        return resolve_package_path(packageDir, config.logDir, ".") / path;
    return (packageDir / path).lexically_normal();
}

static PackageLogState make_package_log_state(const std::filesystem::path& packageDir,
                                              const BuildConfig& config)
{
    PackageLogState state;
    if(!config.enableLogs)
        return state;

    const char* defaultStdoutLog = config.logDir.empty() ? nullptr : "pkg.stdout.log";
    const char* defaultStderrLog = config.logDir.empty() ? nullptr : "pkg.stderr.log";
    const char* defaultWarnLog = config.logDir.empty() ? nullptr : "pkg.warn.log";
    state.stdoutLog = resolve_log_path(packageDir, config, config.stdoutLog,
                                       defaultStdoutLog);
    state.stderrLog = resolve_log_path(packageDir, config, config.stderrLog,
                                       defaultStderrLog);
    state.warnLog = resolve_log_path(packageDir, config, config.warnLog,
                                     defaultWarnLog);
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
    explicit ScopedPackageLogState(PackageLogState next) : previous_(current_package_log_state())
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

static std::vector<BuildTarget> parse_bin_targets(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::vector<BuildTarget> targets;
    std::optional<BuildTarget> current;

    auto flush_current = [&]() {
        if(current.has_value())
            targets.push_back(*current);
        current.reset();
    };

    while(std::getline(in, line))
    {
        std::string t = strip_toml_comment(line);
        if(t.empty())
            continue;
        if(t == "[[bin]]")
        {
            flush_current();
            current = BuildTarget {};
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
        else
        {
            parse_build_config_key_value(current->config, key, value);
        }
    }
    flush_current();
    return targets;
}

static std::vector<TaskSpec> parse_task_specs(const std::string& content)
{
    std::istringstream in(content);
    std::string line;
    std::vector<TaskSpec> tasks;
    std::optional<TaskSpec> current;
    std::string currentHostSection;

    auto flush_current = [&]() {
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
            current = TaskSpec {};
            continue;
        }
        if(t.front() == '[' && t.back() == ']')
        {
            std::string section = t.substr(1, t.size() - 2);
            if(current.has_value() &&
               section.rfind("task.host.", 0) == 0 &&
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
                                        const std::string& taskValue) {
            switch(taskKeyKind)
            {
            case TaskTomlKey::Message:
                target.message = unquote_preserve(taskValue);
                break;
            case TaskTomlKey::Print:
                target.print = unquote_preserve(taskValue);
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
                append_toml_string_list_value(taskValue,
                                              target.phaseDependsOn);
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
            case TaskTomlKey::Command: {
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
                                 const std::string& taskValue) {
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
                                     const std::string& taskValue) {
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

struct LinkFlags
{
    std::vector<std::string> libDirs;
    std::vector<std::string> libs;
    std::vector<std::string> staticArchives;
};

static std::filesystem::path dep_source_dir(const std::filesystem::path& depsDir,
                                            const DepSpec& dep);

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

static std::string unquote(std::string_view v)
{
    std::string t = trim(v);
    if(t.size() >= 2 && t.front() == '"' && t.back() == '"')
    {
        std::string inner = t.substr(1, t.size() - 2);
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
        return t.substr(1, t.size() - 2);
    if(t.size() >= 2 && t.front() == '\'' && t.back() == '\'')
        return t.substr(1, t.size() - 2);
    return t;
}

static std::optional<std::string> parse_toml_command_tokens(
    const std::string& value)
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

static void append_toml_string_list_value_preserve(const std::string& value,
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
        if(gitIt == kv.end() && urlIt == kv.end())
            continue;
        DepSpec dep;
        dep.name = name;
        if(gitIt != kv.end())
            dep.git = gitIt->second;
        if(urlIt != kv.end())
            dep.url = urlIt->second;
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
            dep.build = "cmake";
        deps.push_back(dep);
    }
    return deps;
}

static std::vector<std::string> parse_workspace_members(
    const std::string& content)
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
            for(const auto& part : split_toml_array(
                    value.substr(1, value.size() - 2)))
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

static std::string make_dependency_manifest_line(const std::string& name,
                                                 const std::string& gitUrl,
                                                 const std::string& archiveUrl,
                                                 const std::string& archiveType,
                                                 const std::string& rev,
                                                 const std::string& tag,
                                                 bool gitSubmodules,
                                                 const std::string& depSubdir,
                                                 int stripComponents)
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
           "    println!(\"" + depName +
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
    if(auto entryOpt = find_section_toml_string(manifestContent, "package",
                                                "entry");
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

static bool scaffold_added_package(const std::filesystem::path& rootManifestPath,
                                   const std::filesystem::path& packageDir,
                                   const std::string& packageName,
                                   const std::string& depLine,
                                   const std::string& depName,
                                   std::string& error)
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
        error = "Failed to write subproject manifest: " +
                packageManifest.string();
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

static LinkFlags collect_dep_link_flags(
    const std::vector<DepSpec>& deps,
    const std::filesystem::path& depsDir)
{
    std::unordered_set<std::string> libDirs;
    std::unordered_set<std::string> libs;
    std::unordered_set<std::string> staticArchives;
    for(const auto& dep : deps)
    {
        std::filesystem::path path = dep_source_dir(depsDir, dep);
        scan_lib_dir(path / "build" / "lib", libDirs, libs, staticArchives);
        scan_lib_dir(path / "build", libDirs, libs, staticArchives);
        scan_lib_dir(path / "lib", libDirs, libs, staticArchives);
    }
    LinkFlags flags;
    flags.libDirs.assign(libDirs.begin(), libDirs.end());
    flags.libs.assign(libs.begin(), libs.end());
    flags.staticArchives.assign(staticArchives.begin(), staticArchives.end());
    std::sort(flags.libDirs.begin(), flags.libDirs.end());
    std::sort(flags.libs.begin(), flags.libs.end());
    std::sort(flags.staticArchives.begin(), flags.staticArchives.end());
    return flags;
}

struct PackageManifest
{
    std::filesystem::path manifestPath;
    std::filesystem::path packageDir;
    std::string content;
};

static std::vector<std::filesystem::path> discover_workspace_manifests(
    const std::filesystem::path& manifestPath,
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
            fs::path candidate = base.filename() == "mlang.toml"
                                     ? base
                                     : (base / "mlang.toml");
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

static std::vector<PackageManifest> collect_target_manifests(
    const std::filesystem::path& manifestPath)
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

    if(has_section(content, "package"))
    {
        out.push_back(PackageManifest { manifestAbs, manifestAbs.parent_path(),
                                        content });
    }
    for(const auto& child : discover_workspace_manifests(manifestAbs, content))
    {
        fs::path childAbs = fs::absolute(child);
        std::ifstream childIn(childAbs, std::ios::binary);
        if(!childIn)
            continue;
        std::string childContent((std::istreambuf_iterator<char>(childIn)),
                                 std::istreambuf_iterator<char>());
        if(childContent.empty() || !has_section(childContent, "package"))
            continue;
        out.push_back(PackageManifest { childAbs, childAbs.parent_path(),
                                        childContent });
    }
    return out;
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
    return std::system(command_with_log_redirection(cmd, logChildOutput).c_str());
}

class ProgressSpinner
{
  public:
    explicit ProgressSpinner(std::string label) : label_(std::move(label))
    {
        hideCursor_ = ::isatty(fileno(stderr)) != 0;
        if(hideCursor_)
            std::cerr << "\033[?25l" << std::flush;
        worker_ = std::thread([this]() {
            static constexpr char frames[] = { '|', '/', '-', '\\' };
            size_t idx = 0;
            while(!done_.load())
            {
                std::string detail;
                {
                    std::lock_guard<std::mutex> lock(detailMutex_);
                    detail = detail_;
                }
                std::cerr << "\r\033[2K"
                          << live_line(frames[idx % 4], detail) << std::flush;
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
    std::atomic<bool> done_ { false };
    std::thread worker_;
    bool stopped_ = false;
    bool hideCursor_ = false;
};

static int run_progress_command(const std::string& label,
                                const std::string& cmd,
                                bool logCommand = true,
                                bool logChildOutput = true)
{
    const auto start = std::chrono::steady_clock::now();
    ProgressSpinner spinner(shorten_progress_description(label));
    int rc = run_command(cmd, logCommand, logChildOutput);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    std::string suffix;
    if(label.size() > 4 && label.front() == '[' && label.find('/') != std::string::npos &&
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
                              bool useSpinner,
                              bool logCommand = true,
                              bool logChildOutput = true)
{
    if(useSpinner && !pkg_logs_active())
        return run_progress_command(label, cmd, logCommand, logChildOutput);
    std::cerr << shorten_progress_description(label) << std::endl;
    const auto start = std::chrono::steady_clock::now();
    int rc = run_command(cmd, logCommand, logChildOutput);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    if(label.size() > 4 && label.front() == '[' && label.find('/') != std::string::npos &&
       label.find(']') != std::string::npos && rc == 0)
    {
        const size_t close = label.find(']');
        std::cerr << label.substr(0, close + 1) << " Completed: "
                  << trim(label.substr(close + 1)) << " - "
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

    char buffer[512];
    while(fgets(buffer, sizeof(buffer), pipe))
    {
        const std::string line = sanitize_progress_output(buffer);
        if(line.empty())
            continue;
        spinner.set_detail(line);
        if(logChildOutput)
            append_log_line(current_package_log_state().stdoutLog, line);
    }

    return pclose(pipe);
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

static std::string command_with_path_entries(const std::string& cmd,
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

static int run_progress_command_with_paths(const std::string& label,
                                           const std::string& cmd,
                                           const std::vector<std::string>& entries)
{
    return run_progress_command(label, command_with_path_entries(cmd, entries));
}

static int run_status_command_with_paths(const std::string& label,
                                         const std::string& cmd,
                                         const std::vector<std::string>& entries,
                                         bool useSpinner,
                                         bool logCommand = true,
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

static int run_command_in_dir_with_paths(const std::filesystem::path& dir,
                                         const std::string& cmd,
                                         const std::vector<std::string>& entries,
                                         bool logCommand = true,
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
    return run_progress_command(label, "cd " + shell_quote(dir.string()) +
                                           " && " +
                                           command_with_path_entries(cmd, entries));
}

static int run_status_command_in_dir_with_paths(
    const std::string& label, const std::filesystem::path& dir,
    const std::string& cmd, const std::vector<std::string>& entries,
    bool useSpinner, bool logCommand = true,
    bool logChildOutput = true)
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

static void append_shell_fragment(std::string& cmd,
                                  const std::string& fragment)
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

static std::filesystem::path dep_checkout_dir(
    const std::filesystem::path& depsDir, const DepSpec& dep)
{
    return depsDir / dep.name;
}

static std::filesystem::path dep_source_dir(const std::filesystem::path& depsDir,
                                            const DepSpec& dep)
{
    std::filesystem::path path = dep_checkout_dir(depsDir, dep);
    if(!dep.subdir.empty())
        path /= dep.subdir;
    return path;
}

struct ExecutionProgressState
{
    size_t totalSteps = 0;
    std::atomic<size_t> nextStep { 1 };
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
static size_t count_build_dep_steps(const DepSpec& dep,
                                    const std::filesystem::path& depsDir);
static size_t count_reachable_task_steps(const std::vector<TaskSpec>& tasks,
                                         const std::string& hostName,
                                         const std::vector<std::string>& roots);

static int fetch_git_dep(const DepSpec& dep,
                         const std::filesystem::path& depsDir,
                         bool updateExisting,
                         const std::vector<std::string>& pathEntries)
{
    std::filesystem::path path = dep_checkout_dir(depsDir, dep);
    if(!std::filesystem::exists(path))
    {
        std::string cloneCmd = "git clone " + shell_quote(dep.git) + " " +
                               shell_quote(path.string());
        if(run_status_command_with_paths(
               execution_step_label("Fetching git dependency '" + dep.name +
                                    "' from " + dep.git),
                                         cloneCmd, pathEntries, dep.spinner,
                                         dep.spinner, dep.spinner) != 0)
            return 1;
    }
    else if(updateExisting)
    {
        std::string fetchCmd = "git -C " + shell_quote(path.string()) +
                               " fetch --all --tags";
        if(run_status_command_with_paths(
               execution_step_label("Updating git dependency '" + dep.name + "'"),
                                         fetchCmd, pathEntries, dep.spinner,
                                         dep.spinner, dep.spinner) != 0)
            return 1;
    }

    if(dep.submodules)
    {
        std::string updateSubmodulesCmd =
            "git -C " + shell_quote(path.string()) +
            " submodule update --init --recursive";
        if(run_status_command_with_paths(
               execution_step_label("Initializing submodules for '" + dep.name +
                                    "'"),
               updateSubmodulesCmd, pathEntries, dep.spinner, dep.spinner,
               dep.spinner) != 0)
        {
            return 1;
        }
    }

    if(!dep.rev.empty())
    {
        std::string checkout = "git -C " + shell_quote(path.string()) +
                               " checkout " + shell_quote(dep.rev);
        if(run_status_command_with_paths(
               execution_step_label("Checking out '" + dep.name +
                                    "' revision " + dep.rev),
                                         checkout, pathEntries, dep.spinner,
                                         dep.spinner, dep.spinner) != 0)
            return 1;
    }
    else if(!dep.tag.empty())
    {
        std::string checkout = "git -C " + shell_quote(path.string()) +
                               " checkout " +
                               shell_quote("tags/" + dep.tag);
        if(run_status_command_with_paths(
               execution_step_label("Checking out '" + dep.name + "' tag " +
                                    dep.tag),
                                         checkout, pathEntries, dep.spinner,
                                         dep.spinner, dep.spinner) != 0)
            return 1;
    }
    return 0;
}

static int fetch_archive_dep(const DepSpec& dep,
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

    std::filesystem::path checkoutDir = dep_checkout_dir(depsDir, dep);
    std::error_code ec;
    if(std::filesystem::exists(checkoutDir, ec))
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
        std::filesystem::remove_all(checkoutDir, ec);
        if(ec)
        {
            pkg_error_line("Failed to reset partial archive checkout for '" +
                           dep.name + "': " + ec.message());
            return 1;
        }
    }

    std::filesystem::create_directories(depsDir);
    std::filesystem::path archivePath = depsDir / (dep.name + ".tar.gz");
    std::filesystem::path extractDir = depsDir / (dep.name + ".extracting");
    std::filesystem::remove_all(extractDir, ec);
    std::filesystem::create_directories(extractDir);

    std::string downloadCmd = "curl -L --fail " + shell_quote(dep.url) +
                              " -o " + shell_quote(archivePath.string());
    if(run_status_command_with_paths(
           execution_step_label("Downloading archive dependency '" + dep.name +
                                "' from " + dep.url),
                                     downloadCmd,
                                     pathEntries, dep.spinner, dep.spinner,
                                     dep.spinner) != 0)
        return 1;

    std::string extractCmd = "tar -xzf " + shell_quote(archivePath.string()) +
                             " -C " + shell_quote(extractDir.string());
    if(dep.stripComponents > 0)
    {
        extractCmd += " --strip-components=" +
                      std::to_string(dep.stripComponents);
    }
    if(run_status_command_with_paths(
           execution_step_label("Unpacking " + archivePath.filename().string() +
                                " into " + checkoutDir.string()),
                                     extractCmd,
                                     pathEntries, true, true, true) != 0)
        return 1;
    std::filesystem::rename(extractDir, checkoutDir, ec);
    if(ec)
    {
        pkg_error_line("Failed to finalize archive checkout for '" + dep.name +
                       "': " + ec.message());
        return 1;
    }
    std::filesystem::remove(archivePath, ec);
    return 0;
}

static int fetch_dep(const DepSpec& dep, const std::filesystem::path& depsDir,
                     bool updateExisting,
                     const std::vector<std::string>& pathEntries)
{
    if(!dep.git.empty())
        return fetch_git_dep(dep, depsDir, updateExisting, pathEntries);
    if(!dep.url.empty())
        return fetch_archive_dep(dep, depsDir, pathEntries);
    pkg_error_line("Dependency '" + dep.name +
                   "' is missing a supported source (git/url)");
    return 1;
}

static int build_git_dep(const DepSpec& dep,
                         const std::filesystem::path& depsDir,
                         bool useNinja,
                         const std::string& makeProgram,
                         const std::vector<std::string>& pathEntries)
{
    std::filesystem::path path = dep_source_dir(depsDir, dep);
    if(!std::filesystem::exists(path))
        return 1;

    if(dep.build == "none" || dep.build == "skip")
        return 0;

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
               execution_step_label("Configuring CMake dependency '" + dep.name +
                                    "' in " + buildDir.string()),
                                         cfg, pathEntries, dep.spinner,
                                         dep.spinner, dep.spinner) != 0)
            return 1;
        std::string build =
            "cmake --build " + shell_quote(buildDir.string());
        return run_status_command_with_paths(
            execution_step_label("Building CMake dependency '" + dep.name + "'"),
                                             build, pathEntries, dep.spinner,
                                             dep.spinner, dep.spinner);
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
                                        dep.name + "' in " +
                                        buildDir.string()),
                                             setup, pathEntries, dep.spinner,
                                             dep.spinner, dep.spinner) != 0)
                return 1;
        }
        std::string compile =
            "meson compile -C " + shell_quote(buildDir.string());
        return run_status_command_with_paths(
            execution_step_label("Building Meson dependency '" + dep.name + "'"),
                                             compile, pathEntries, dep.spinner,
                                             dep.spinner, dep.spinner);
    }
    if(dep.build == "make")
    {
        std::string effectiveMake = makeProgram.empty() ? "make" : makeProgram;
        std::string cmd = shell_quote(effectiveMake) + " -C " +
                          shell_quote(path.string());
        return run_status_command_with_paths(
            execution_step_label("Building make dependency '" + dep.name + "'"),
                                             cmd,
                                             pathEntries, dep.spinner,
                                             dep.spinner, dep.spinner);
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
                                    std::vector<std::string>& outFlags,
                                    const std::vector<std::string>& pathEntries)
{
    if(!dep.usePkgConfig || dep.pkgConfig.empty())
        return true;
    std::string cmd = "pkg-config --cflags --libs " + dep.pkgConfig;
    auto result = run_command_capture_with_paths(cmd, pathEntries);
    if(!result.has_value())
    {
        std::cerr << "pkg-config failed for: " << dep.pkgConfig << "\n";
        return false;
    }
    for(const auto& token : split_shell_tokens(result.value()))
        outFlags.push_back(token);
    return true;
}

static bool ensure_ninja_available(const std::vector<std::string>& pathEntries)
{
    auto ninjaPath =
        run_command_capture_with_paths("command -v ninja || command -v ninja-build",
                                       pathEntries);
    if(ninjaPath.has_value() && !trim(*ninjaPath).empty())
        return true;
    std::cerr << "Ninja build requested, but neither 'ninja' nor 'ninja-build'"
              << " was found in PATH.\n";
    return false;
}

static bool validate_declared_libraries(const std::filesystem::path& manifestPath,
                                        const std::string& targetName,
                                        const BuildConfig& buildConfig,
                                        const LinkFlags& linkFlags)
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
            std::cerr << "Failed to create temporary file for library validation"
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

static int validate_mlang_version_requirement(const std::filesystem::path& manifestPath,
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
    ScopedPackageLogState scopedLogs(
        make_package_log_state(pkg.packageDir, buildConfig));
    auto deps = parse_source_deps(pkg.content);
    std::filesystem::path depsDir = package_deps_dir(pkg.packageDir, buildConfig);
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
        if(fetch_dep(dep, depsDir, /*updateExisting=*/true,
                     buildConfig.pathEntries) != 0)
            return 1;
    }
    pkg_info_line("Fetch completed for " + pkg.manifestPath.string() + ".");
    return 0;
}

static std::string default_task_compiler_for_language(
    std::string_view language, const BuildConfig& buildConfig)
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
        error = "Task '" + taskName + "' in " + pkg.manifestPath.string() +
                " is missing required 'output' for language-driven task execution.";
        return std::nullopt;
    }
    if(source.empty() && inputs.empty())
    {
        error = "Task '" + taskName + "' in " + pkg.manifestPath.string() +
                " needs 'source' or 'inputs' for language-driven task execution.";
        return std::nullopt;
    }

    if(!compileOnly &&
       !validate_declared_libraries(pkg.manifestPath, taskName, taskBuildConfig,
                                    linkFlags))
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
        if(language == "c++" && taskBuildConfig.staticCppRuntime.value_or(false))
            cmd += " -static-libstdc++ -static-libgcc";
        for(const auto& flag : taskBuildConfig.linkerFlags)
            append_shell_fragment(cmd, flag);
        for(const auto& flag : pkgFlags)
            cmd += " " + shell_quote(flag);
    }
    return cmd;
}

static int build_for_manifest(const PackageManifest& pkg, const std::string& argv0,
                              const std::string& optFlagOverride,
                              bool useNinja,
                              const BuildConfig& packageBuildConfig)
{
    ScopedPackageLogState scopedLogs(
        make_package_log_state(pkg.packageDir, packageBuildConfig));
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
    if(!taskOnlyPackage &&
       !ensure_package_entry_stub(pkg.manifestPath, pkg.content, packageLabel,
                                  entryError))
    {
        pkg_error_line(entryError);
        return 1;
    }

    auto deps = parse_source_deps(pkg.content);
    auto cdeps = parse_c_deps(pkg.content);
    std::vector<BuildTarget> targets = parse_bin_targets(pkg.content);
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

    bool effectiveUseNinja = useNinja || packageBuildConfig.useNinja.value_or(false);
    const auto tasks = parse_task_specs(pkg.content);
    const std::string hostName = current_host_name();
    std::vector<std::string> buildTaskRoots;
    if(targets.empty())
    {
        buildTaskRoots =
            task_names_for_phases(tasks, std::vector<std::string> { "build" },
                                  hostName);
        if(buildTaskRoots.empty() && task_list_contains_name(tasks, "build"))
            buildTaskRoots.push_back("build");
    }
    for(const auto& target : targets)
    {
        BuildConfig mergedConfig =
            merge_build_config(packageBuildConfig, target.config);
        if(mergedConfig.useNinja.value_or(false))
            effectiveUseNinja = true;
        if(validate_mlang_version_requirement(pkg.manifestPath, mergedConfig,
                                              target.name) != 0)
        {
            return 1;
        }
    }
    if(effectiveUseNinja &&
       !ensure_ninja_available(packageBuildConfig.pathEntries))
        return 1;

    std::filesystem::path depsDir = package_deps_dir(pkg.packageDir, packageBuildConfig);
    std::filesystem::create_directories(depsDir);
    size_t totalSteps = 0;
    for(const auto& dep : deps)
        totalSteps += count_fetch_dep_steps(dep, depsDir, /*updateExisting=*/false);
    for(const auto& dep : deps)
        totalSteps += count_build_dep_steps(dep, depsDir);
    if(!targets.empty())
        totalSteps += targets.size();
    else
        totalSteps += count_reachable_task_steps(tasks, hostName, buildTaskRoots);
    ScopedExecutionProgressState scopedProgress(totalSteps);
    for(const auto& dep : deps)
    {
        if(fetch_dep(dep, depsDir, /*updateExisting=*/false,
                     packageBuildConfig.pathEntries) != 0)
            return 1;
    }
    for(const auto& dep : deps)
    {
        if(build_git_dep(dep, depsDir, effectiveUseNinja,
                         packageBuildConfig.makeProgram,
                         packageBuildConfig.pathEntries) != 0)
            return 1;
    }

    LinkFlags linkFlags = collect_dep_link_flags(deps, depsDir);
    std::vector<std::string> pkgFlags;
    for(const auto& dep : cdeps)
    {
        if(!append_pkg_config_flags(dep, pkgFlags,
                                    packageBuildConfig.pathEntries))
            return 1;
    }

    if(targets.empty())
    {
        if(!buildTaskRoots.empty())
        {
            for(const auto& taskName : buildTaskRoots)
            {
                if(run_task_for_manifest(pkg, taskName, packageBuildConfig) != 0)
                    return 1;
            }
            return 0;
        }

        pkg_error_line("No package entry, [[bin]] targets, or phase=\"build\" "
                       "tasks found for " + pkg.manifestPath.string());
        return 1;
    }

    const std::filesystem::path buildDir =
        package_build_dir(pkg.packageDir, packageBuildConfig);
    std::filesystem::create_directories(buildDir);
    std::string backend = argv0;
    if(argv0.find('/') != std::string::npos)
        backend = std::filesystem::absolute(argv0).string();

    for(const auto& target : targets)
    {
        if(target.name.empty())
        {
            pkg_error_line("Missing name in [[bin]] target for " +
                           pkg.manifestPath.string());
            return 1;
        }
        if(target.entry.empty())
        {
            pkg_error_line("Missing entry in [[bin]] target '" + target.name +
                           "' for " + pkg.manifestPath.string());
            return 1;
        }

        BuildConfig buildConfig =
            merge_build_config(packageBuildConfig, target.config);
        if(!validate_declared_libraries(pkg.manifestPath, target.name,
                                        buildConfig, linkFlags))
            return 1;

        std::string optFlag = optFlagOverride;
        if(optFlag.empty())
            optFlag = buildConfig.optLevel;

        const std::filesystem::path outputPath = buildDir / target.name;
        std::string output = outputPath.string();
        std::string cmd = shell_quote(backend) + " " +
                          shell_quote(target.entry) + " -o " +
                          shell_quote(output);
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
            execution_step_label("Compiling target '" + target.name + "' from " +
                                 target.entry + " -> " + output),
            pkg.packageDir, cmd,
            buildConfig.pathEntries, true);
        if(rc != 0)
        {
            std::string message = "Build failed for " + pkg.manifestPath.string();
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
    out = replace_all(out, "{{make}}",
                      buildConfig.makeProgram.empty() ? "make"
                                                      : buildConfig.makeProgram);
    for(const auto& [key, value] : buildConfig.optionValues)
        out = replace_all(out, "{{option." + key + "}}", value);
    return out;
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

static std::string format_task_elapsed(
    std::chrono::milliseconds elapsed)
{
    const auto totalMs = elapsed.count();
    const long long hours = totalMs / (1000LL * 60LL * 60LL);
    const long long minutes = (totalMs / (1000LL * 60LL)) % 60LL;
    const long long seconds = (totalMs / 1000LL) % 60LL;
    const long long milliseconds = totalMs % 1000LL;
    std::ostringstream out;
    out << "[" << std::setw(2) << std::setfill('0') << hours << "/"
        << std::setw(2) << std::setfill('0') << minutes << "/"
        << std::setw(2) << std::setfill('0') << seconds << "/"
        << std::setw(3) << std::setfill('0') << milliseconds << "]";
    return out.str();
}

static std::string format_task_elapsed_compact(
    std::chrono::milliseconds elapsed)
{
    const auto totalMs = elapsed.count();
    const long long hours = totalMs / (1000LL * 60LL * 60LL);
    const long long minutes = (totalMs / (1000LL * 60LL)) % 60LL;
    const long long seconds = (totalMs / 1000LL) % 60LL;
    const long long milliseconds = totalMs % 1000LL;
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << hours << ":"
        << std::setw(2) << std::setfill('0') << minutes << ":"
        << std::setw(2) << std::setfill('0') << seconds << ":"
        << std::setw(3) << std::setfill('0') << milliseconds;
    return out.str();
}

static std::optional<TaskSpec> find_task_spec(const std::vector<TaskSpec>& tasks,
                                              const std::string& taskName)
{
    for(const auto& task : tasks)
    {
        if(task.name == taskName)
            return task;
    }
    return std::nullopt;
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
        if(!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
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

    std::filesystem::permissions(
        scriptPath,
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
        if(!dep.rev.empty() || !dep.tag.empty())
            steps += 1;
        return steps;
    }

    if(!dep.url.empty())
    {
        const std::filesystem::path checkoutDir = dep_checkout_dir(depsDir, dep);
        std::error_code ec;
        if(std::filesystem::exists(checkoutDir, ec))
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

static size_t count_build_dep_steps(const DepSpec& dep,
                                    const std::filesystem::path& depsDir)
{
    const std::filesystem::path path = dep_source_dir(depsDir, dep);
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

static void collect_reachable_task_names(
    const std::vector<TaskSpec>& tasks, const std::string& hostName,
    const std::string& taskName, std::unordered_set<std::string>& visited)
{
    if(taskName.empty() || !visited.insert(taskName).second)
        return;

    const auto taskOpt = find_task_spec(tasks, taskName);
    if(!taskOpt.has_value())
        return;
    const auto& task = *taskOpt;
    auto hostIt = task.hostOverrides.find(hostName);
    const TaskSpec::HostOverride* hostOverride =
        hostIt == task.hostOverrides.end() ? nullptr : &hostIt->second;

    std::vector<std::string> effectiveDependsOn = task.dependsOn;
    if(hostOverride)
    {
        effectiveDependsOn.insert(effectiveDependsOn.end(),
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
        effectiveDependsOn,
        task_names_for_phases(tasks, effectivePhaseDependsOn, hostName));

    std::vector<std::string> effectiveJoinOn = task.joinOn;
    if(hostOverride)
    {
        effectiveJoinOn.insert(effectiveJoinOn.end(), hostOverride->joinOn.begin(),
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
    append_unique_strings(effectiveDependsOn, effectiveJoinOn);

    std::vector<std::string> effectiveNext = task.nextTasks;
    if(hostOverride)
    {
        effectiveNext.insert(effectiveNext.end(), hostOverride->nextTasks.begin(),
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
        effectiveNext, task_names_for_phases(tasks, effectiveNextPhases, hostName));

    for(const auto& dep : effectiveDependsOn)
        collect_reachable_task_names(tasks, hostName, dep, visited);
    for(const auto& nextTask : effectiveNext)
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

    for(const auto& task : tasks)
    {
        if(task.name != taskName)
            continue;
        auto hostIt = task.hostOverrides.find(hostName);
        const TaskSpec::HostOverride* hostOverride =
            hostIt == task.hostOverrides.end() ? nullptr : &hostIt->second;

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

        std::vector<std::string> effectiveDependsOn = task.dependsOn;
        if(hostOverride)
        {
            effectiveDependsOn.insert(effectiveDependsOn.end(),
                                      hostOverride->dependsOn.begin(),
                                      hostOverride->dependsOn.end());
        }
        std::vector<std::string> effectivePhaseDependsOn = task.phaseDependsOn;
        if(hostOverride)
        {
            effectivePhaseDependsOn.insert(
                effectivePhaseDependsOn.end(),
                hostOverride->phaseDependsOn.begin(),
                hostOverride->phaseDependsOn.end());
        }
        append_unique_strings(
            effectiveDependsOn,
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
            effectivePhaseJoinOn.insert(
                effectivePhaseJoinOn.end(),
                hostOverride->phaseJoinOn.begin(),
                hostOverride->phaseJoinOn.end());
        }
        append_unique_strings(
            effectiveJoinOn,
            task_names_for_phases(tasks, effectivePhaseJoinOn, hostName));
        effectiveDependsOn.insert(effectiveDependsOn.end(),
                                  effectiveJoinOn.begin(),
                                  effectiveJoinOn.end());

        std::vector<std::string> effectiveNext = task.nextTasks;
        if(hostOverride)
        {
            effectiveNext.insert(effectiveNext.end(),
                                 hostOverride->nextTasks.begin(),
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
            effectiveNext,
            task_names_for_phases(tasks, effectiveNextPhases, hostName));

        std::vector<std::string> effectiveCommands =
            (hostOverride && !hostOverride->commands.empty())
                ? hostOverride->commands
                : task.commands;
        const std::string effectiveMessage = [&]() -> std::string {
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
        bool effectiveParallel =
            hostOverride && hostOverride->parallel.has_value()
                ? hostOverride->parallel.value()
                : task.parallel.value_or(false);
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
                     &taskMutex, &taskCv, dep, childStack]() mutable {
                        return run_task_for_manifest_impl(
                            pkg, tasks, buildConfig, hostName, taskStates,
                            taskMutex, taskCv, dep, childStack);
                    }));
            }
            for(size_t i = 0; i < effectiveDependsOn.size(); ++i)
            {
                if(futures[i].get() != 0)
                {
                    std::cerr << "Task '" << taskName
                              << "' dependency '" << effectiveDependsOn[i]
                              << "' failed for "
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
                    std::cerr << "Task '" << taskName
                              << "' dependency '" << dep
                              << "' failed for "
                              << pkg.manifestPath.string() << ".\n";
                    taskStack.pop_back();
                    return 1;
                }
            }
        }

        std::filesystem::path workdir =
            effectiveWorkdir.empty() ? pkg.packageDir
                                 : std::filesystem::path(
                                       expand_task_text(effectiveWorkdir, pkg,
                                                        buildConfig));
        if(!workdir.is_absolute())
            workdir = pkg.packageDir / workdir;

        const std::string taskDescription =
            effectiveMessage.empty()
                ? taskName
                : expand_task_text(effectiveMessage, pkg, buildConfig);
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
            auto scriptPathOpt =
                write_task_script(pkg, taskName, effectiveShell, buildConfig);
            if(!scriptPathOpt.has_value())
            {
                taskStack.pop_back();
                return 1;
            }
            effectiveCommands.insert(effectiveCommands.begin(),
                                     "sh " + shell_quote(scriptPathOpt->string()));
        }

        if(!effectiveLanguage.empty())
        {
            const auto deps = parse_source_deps(pkg.content);
            const auto cdeps = parse_c_deps(pkg.content);
            const std::filesystem::path depsDir =
                package_deps_dir(pkg.packageDir, buildConfig);
            LinkFlags linkFlags = collect_dep_link_flags(deps, depsDir);
            std::vector<std::string> pkgFlags;
            for(const auto& dep : cdeps)
            {
                if(!append_pkg_config_flags(dep, pkgFlags,
                                            effectiveTaskBuildConfig.pathEntries))
                {
                    taskStack.pop_back();
                    return 1;
                }
            }
            std::string generationError;
            auto generatedCommand = build_task_language_command(
                pkg, taskName, effectiveTaskBuildConfig, effectiveLanguage,
                expand_task_text(effectiveSource, pkg, buildConfig),
                expand_task_text(effectiveOutput, pkg, buildConfig),
                [&]() {
                    std::vector<std::string> expandedInputs;
                    expandedInputs.reserve(effectiveInputs.size());
                    for(const auto& input : effectiveInputs)
                    {
                        expandedInputs.push_back(
                            expand_task_text(input, pkg, buildConfig));
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
                      << " has no commands, script, or language-driven build configuration.\n";
            taskStack.pop_back();
            return 1;
        }

        for(const auto& command : effectiveCommands)
        {
            std::string envPrefix;
            for(const auto& entry : effectiveEnv)
            {
                const std::string expandedEnv =
                    expand_task_text(entry, pkg, buildConfig);
                if(expandedEnv.empty())
                    continue;
                envPrefix += " " + shell_quote(expandedEnv);
            }
            std::string expanded = expand_task_text(command, pkg, buildConfig);
            if(!envPrefix.empty())
                expanded = "env" + envPrefix + " " + expanded;
            const std::string fullCommand =
                "cd " + shell_quote(workdir.string()) + " && " +
                command_with_path_entries(expanded,
                                          effectiveTaskBuildConfig.pathEntries);
            const int rc = inlineSpinner
                               ? stream_inline_output_command(
                                     *inlineSpinner, fullCommand, false,
                                     effectiveLogOutput)
                               : run_command(fullCommand, true,
                                             effectiveLogOutput);
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
                const std::filesystem::path chmodPath =
                    expand_task_text(chmodPathText, pkg, buildConfig);
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
                     &taskMutex, &taskCv, nextTask, childStack]() mutable {
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
        const auto taskElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - taskStart);
        if(inlineSpinner)
            inlineSpinner->stop("");
        pkg_task_print_line(taskPrefix + " " + taskName +
                            " Completed, time " +
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
            tasks, hostName, std::vector<std::string> { taskName }));
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
    return run_task_for_manifest(pkg, taskName, parse_build_config(pkg.content));
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
    std::map<std::string, std::string> optionValues;
};

static void apply_cli_overrides(BuildConfig& buildConfig,
                                const PkgCliOverrides& overrides)
{
    const bool enableLogs = overrides.logDir.has_value() ||
                            overrides.stdoutLog.has_value() ||
                            overrides.stderrLog.has_value() ||
                            overrides.warnLog.has_value() ||
                            overrides.taskPrintToStdoutLog.has_value();
    if(overrides.buildDir.has_value())
        buildConfig.buildDir = *overrides.buildDir;
    if(overrides.depsDir.has_value())
        buildConfig.depsDir = *overrides.depsDir;
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
    for(const auto& [key, value] : overrides.optionValues)
        buildConfig.optionValues[key] = value;
    if(enableLogs)
        buildConfig.enableLogs = true;
}

static bool parse_pkg_option_argument(const std::string& text,
                                      std::string& key,
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
    if(argc < 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " pkg [--config FILE] <init|add|fetch|build|run|clean>\n";
        return 1;
    }

    std::filesystem::path manifestPath = "mlang.toml";
    std::string compilerProgram = argv[0] ? std::string(argv[0]) : "mlang";
    if(!compilerProgram.empty() && compilerProgram.find('/') != std::string::npos)
        compilerProgram = std::filesystem::absolute(compilerProgram).string();
    int subIndex = 2;
    while(subIndex < argc)
    {
        std::string arg = argv[subIndex];
        if(arg == "--config")
        {
            if(subIndex + 1 >= argc)
            {
                std::cerr << "--config requires a manifest path\n";
                return 1;
            }
            manifestPath = argv[subIndex + 1];
            subIndex += 2;
            continue;
        }
        if(arg.rfind("--config=", 0) == 0)
        {
            manifestPath = arg.substr(std::string("--config=").size());
            if(manifestPath.empty())
            {
                std::cerr << "--config requires a manifest path\n";
                return 1;
            }
            ++subIndex;
            continue;
        }
        break;
    }

    if(subIndex >= argc)
    {
        std::cerr << "Usage: " << argv[0]
                  << " pkg [--config FILE] <init|add|fetch|build|run|clean>\n";
        return 1;
    }

    std::string sub = argv[subIndex];
    const std::string manifestLabel = manifestPath.string();

    if(sub == "init")
    {
        if(std::filesystem::exists(manifestPath))
        {
            std::cerr << manifestLabel << " already exists\n";
            return 1;
        }

        std::string name =
            std::filesystem::current_path().filename().string();
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

        if(!write_text_file_if_missing(std::filesystem::path("src") / "main.mla",
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
                      << " pkg [--config FILE] add <name> [--git URL] [--rev REV] [--tag TAG] [--submodules]\n"
                      << "       " << argv[0]
                      << " pkg [--config FILE] add <name> --url URL [--archive tar.gz] [--strip-components N] [--subdir DIR]\n"
                      << "       " << argv[0]
                      << " pkg [--config FILE] add <name> [--pkg-config NAME] [--system]\n"
                      << "       " << argv[0]
                      << " pkg [--config FILE] add <name> [--git URL|--url URL] --add-lib [--project-dir DIR]\n";
            return 1;
        }

        std::string name = argv[subIndex + 1];
        std::string gitUrl;
        std::string archiveUrl;
        std::string archiveType;
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
            line = make_dependency_manifest_line(name, gitUrl, archiveUrl,
                                                archiveType, rev, tag,
                                                 gitSubmodules,
                                                 depSubdir, stripComponents);
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
            if(!ensure_package_entry_stub(manifestPath, content, "workspace_root",
                                          error))
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
                std::cerr << "Failed to update root " << manifestLabel
                          << "\n";
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
            else
            {
                std::cerr << "Unknown option for 'pkg fetch': " << arg << "\n"
                          << "Usage: " << argv[0]
                          << " pkg [--config FILE] fetch [--build-dir DIR] [--deps-dir DIR]"
                          << " [--log-dir DIR] [--stdout-log FILE]"
                          << " [--stderr-log FILE] [--warn-log FILE]"
                          << " [--task-print-to-stdout-log]\n";
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
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_build_config(pkg.content);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
            if(fetch_for_manifest(pkg, buildConfig) != 0)
                return 1;
        }
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
            if(arg == "-O0" || arg == "-Og" || arg == "-O1" ||
               arg == "-O2" || arg == "-O3" || arg == "-Os" ||
               arg == "-Oz")
            {
                optFlag = arg;
            }
            else if(arg == "--ninja")
            {
                useNinja = true;
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
            else
            {
                std::cerr << "Unknown option for 'pkg build': " << arg << "\n"
                          << "Usage: " << argv[0]
                          << " pkg [--config FILE] build [-O0|-Og|-O1|-O2|-O3|-Os|-Oz] [--ninja]"
                          << " [--build-dir DIR] [--deps-dir DIR]"
                          << " [--log-dir DIR] [--stdout-log FILE]"
                          << " [--stderr-log FILE] [--warn-log FILE]"
                          << " [--task-print-to-stdout-log]\n";
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
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_build_config(pkg.content);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
            if(build_for_manifest(pkg, argv[0], optFlag, useNinja,
                                  buildConfig) != 0)
                return 1;
        }
        return 0;
    }

    if(sub == "run")
    {
        if(subIndex + 1 >= argc)
        {
            std::cerr << "Usage: " << argv[0]
                      << " pkg [--config FILE] run <task> [--build-dir DIR] [--deps-dir DIR] [--log-dir DIR] [--stdout-log FILE]"
                      << " [--stderr-log FILE] [--warn-log FILE]"
                      << " [--task-print-to-stdout-log]"
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
        for(int i = subIndex + 2; i < argc; ++i)
        {
            std::string arg = argv[i];
            if(arg == "--log-dir" && i + 1 < argc)
                overrides.logDir = argv[++i];
            else if(arg == "--stdout-log" && i + 1 < argc)
                overrides.stdoutLog = argv[++i];
            else if(arg == "--stderr-log" && i + 1 < argc)
                overrides.stderrLog = argv[++i];
            else if(arg == "--warn-log" && i + 1 < argc)
                overrides.warnLog = argv[++i];
            else if(arg == "--task-print-to-stdout-log")
                overrides.taskPrintToStdoutLog = true;
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
                          << " pkg [--config FILE] run <task> [--build-dir DIR] [--deps-dir DIR]"
                          << " [--log-dir DIR] [--stdout-log FILE]"
                          << " [--stderr-log FILE] [--warn-log FILE]"
                          << " [--task-print-to-stdout-log]"
                          << " [--option KEY=VALUE]\n";
                return 1;
            }
        }
        bool found = false;
        for(const auto& pkg : manifests)
        {
            BuildConfig buildConfig = parse_build_config(pkg.content);
            buildConfig.compilerProgram = compilerProgram;
            apply_cli_overrides(buildConfig, overrides);
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
                    totalSteps +=
                        count_fetch_dep_steps(dep, depsDir, /*updateExisting=*/true);
                }
                totalSteps += count_reachable_task_steps(
                    tasks, hostName, std::vector<std::string> { taskName });
                ScopedExecutionProgressState scopedProgress(totalSteps);
                if(fetch_for_manifest(pkg, buildConfig) != 0)
                    return 1;
                std::map<std::string, TaskRunState> taskStates;
                std::mutex taskMutex;
                std::condition_variable taskCv;
                std::vector<std::string> taskStack;
                rc = run_task_for_manifest_impl(pkg, tasks, buildConfig, hostName,
                                                taskStates, taskMutex, taskCv,
                                                taskName, taskStack);
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
                          << " pkg [--config FILE] clean [--build-dir DIR] [--deps-dir DIR]"
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
            ScopedPackageLogState scopedLogs(
                make_package_log_state(std::filesystem::current_path(),
                                       buildConfig));
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
            BuildConfig buildConfig = parse_build_config(pkg.content);
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
