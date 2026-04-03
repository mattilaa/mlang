#include "package_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <condition_variable>
#include <unordered_set>
#include <vector>

#ifndef MLANG_VERSION
#define MLANG_VERSION "0.1.0"
#endif

namespace {

static std::vector<std::string> split_toml_array(std::string_view input);
static std::string unquote(std::string_view v);
static std::vector<std::string> parse_workspace_members(
    const std::string& content);

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
        std::string t = trim(line);
        if(t.empty() || t[0] == '#')
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

static bool toml_array_is_complete(std::string_view input)
{
    int depth = 0;
    bool in_quotes = false;
    bool escaped = false;
    for(char c : input)
    {
        if(in_quotes)
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
                in_quotes = false;
            continue;
        }

        if(c == '"')
        {
            in_quotes = true;
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

static bool toml_value_is_complete(std::string_view input)
{
    std::string t = trim(input);
    if(t.empty())
        return true;
    if(t.front() == '[')
        return toml_array_is_complete(t);
    if(t.front() == '"')
        return toml_basic_string_is_complete(t);
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
        value += trim(extra);
        if(toml_array_is_complete(value))
            break;
    }
    return value;
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
    std::string minMlangVersion;
    std::string makeProgram = "make";
    std::vector<std::string> pathEntries;
    std::vector<std::string> compilerFlags;
    std::vector<std::string> linkerFlags;
    std::vector<std::string> libPaths;
    std::vector<std::string> libs;
    std::optional<bool> useNinja;
    std::optional<bool> staticDeps;
    std::optional<bool> staticCppRuntime;
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
    std::string phase;
    std::string workdir;
    std::optional<bool> parallel;
    std::vector<std::string> dependsOn;
    std::vector<std::string> phaseDependsOn;
    std::vector<std::string> joinOn;
    std::vector<std::string> phaseJoinOn;
    std::vector<std::string> nextTasks;
    std::vector<std::string> nextPhases;
    std::vector<std::string> env;
    std::vector<std::string> shellLines;
    std::vector<std::string> commands;
    struct HostOverride
    {
        std::string phase;
        std::string workdir;
        std::optional<bool> parallel;
        std::vector<std::string> dependsOn;
        std::vector<std::string> phaseDependsOn;
        std::vector<std::string> joinOn;
        std::vector<std::string> phaseJoinOn;
        std::vector<std::string> nextTasks;
        std::vector<std::string> nextPhases;
        std::vector<std::string> env;
        std::vector<std::string> shellLines;
        std::vector<std::string> commands;
    };
    std::map<std::string, HostOverride> hostOverrides;
};

static void parse_build_config_key_value(BuildConfig& cfg,
                                         const std::string& key,
                                         const std::string& value);

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
    if(!overrideCfg.optLevel.empty())
        out.optLevel = overrideCfg.optLevel;
    if(!overrideCfg.targetArch.empty())
        out.targetArch = overrideCfg.targetArch;
    if(!overrideCfg.minMlangVersion.empty())
        out.minMlangVersion = overrideCfg.minMlangVersion;
    if(!overrideCfg.makeProgram.empty())
        out.makeProgram = overrideCfg.makeProgram;
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
        std::string value = collect_multiline_toml_value(trim(t.substr(eq + 1)), in);
        parse_build_config_key_value(cfg, key, value);
    }
    return cfg;
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
        std::string t = trim(line);
        if(t.empty() || t[0] == '#')
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

        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        std::string key = trim(t.substr(0, eq));
        std::string value = collect_multiline_toml_value(trim(t.substr(eq + 1)), in);
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
        std::string t = trim(line);
        if(t.empty() || t[0] == '#')
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

        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        std::string key = trim(t.substr(0, eq));
        std::string value = collect_multiline_toml_value(trim(t.substr(eq + 1)), in);

        auto apply_task_kv = [&](TaskSpec& task, const std::string& taskKey,
                                 const std::string& taskValue) {
            if(taskKey == "name")
            {
                task.name = unquote(taskValue);
            }
            else if(taskKey == "phase")
            {
                task.phase = unquote(taskValue);
            }
            else if(taskKey == "workdir")
            {
                task.workdir = unquote(taskValue);
            }
            else if(taskKey == "parallel")
            {
                task.parallel = parse_toml_bool_value(taskValue);
            }
            else if(taskKey == "depends_on")
            {
                append_toml_string_list_value(taskValue, task.dependsOn);
            }
            else if(taskKey == "phase_depends_on")
            {
                append_toml_string_list_value(taskValue, task.phaseDependsOn);
            }
            else if(taskKey == "join_on")
            {
                append_toml_string_list_value(taskValue, task.joinOn);
            }
            else if(taskKey == "phase_join_on")
            {
                append_toml_string_list_value(taskValue, task.phaseJoinOn);
            }
            else if(taskKey == "next")
            {
                append_toml_string_list_value(taskValue, task.nextTasks);
            }
            else if(taskKey == "next_phases")
            {
                append_toml_string_list_value(taskValue, task.nextPhases);
            }
            else if(taskKey == "command")
            {
                std::string v = unquote(taskValue);
                if(!v.empty())
                    task.commands.push_back(v);
            }
            else if(taskKey == "env")
            {
                append_toml_string_list_value(taskValue, task.env);
            }
            else if(taskKey == "script" || taskKey == "shell")
            {
                append_toml_string_list_value(taskValue, task.shellLines);
            }
            else if(taskKey == "commands")
            {
                append_toml_string_list_value(taskValue, task.commands);
            }
        };

        auto apply_override_kv = [&](TaskSpec::HostOverride& ov,
                                     const std::string& taskKey,
                                     const std::string& taskValue) {
            if(taskKey == "phase")
            {
                ov.phase = unquote(taskValue);
            }
            else if(taskKey == "workdir")
            {
                ov.workdir = unquote(taskValue);
            }
            else if(taskKey == "parallel")
            {
                ov.parallel = parse_toml_bool_value(taskValue);
            }
            else if(taskKey == "depends_on")
            {
                append_toml_string_list_value(taskValue, ov.dependsOn);
            }
            else if(taskKey == "phase_depends_on")
            {
                append_toml_string_list_value(taskValue, ov.phaseDependsOn);
            }
            else if(taskKey == "join_on")
            {
                append_toml_string_list_value(taskValue, ov.joinOn);
            }
            else if(taskKey == "phase_join_on")
            {
                append_toml_string_list_value(taskValue, ov.phaseJoinOn);
            }
            else if(taskKey == "next")
            {
                append_toml_string_list_value(taskValue, ov.nextTasks);
            }
            else if(taskKey == "next_phases")
            {
                append_toml_string_list_value(taskValue, ov.nextPhases);
            }
            else if(taskKey == "command")
            {
                std::string v = unquote(taskValue);
                if(!v.empty())
                    ov.commands.push_back(v);
            }
            else if(taskKey == "env")
            {
                append_toml_string_list_value(taskValue, ov.env);
            }
            else if(taskKey == "script" || taskKey == "shell")
            {
                append_toml_string_list_value(taskValue, ov.shellLines);
            }
            else if(taskKey == "commands")
            {
                append_toml_string_list_value(taskValue, ov.commands);
            }
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
        std::string t = trim(line);
        if(t.empty() || t[0] == '#')
            continue;
        if(t.front() == '[' && t.back() == ']')
        {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if(section != "workspace")
            continue;
        size_t eq = t.find('=');
        if(eq == std::string::npos)
            continue;
        std::string key = trim(t.substr(0, eq));
        if(key != "members")
            continue;
        std::string value =
            collect_multiline_toml_value(trim(t.substr(eq + 1)), in);
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
        std::string t = trim(line);
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
    return "fn main() -> void {\n"
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

static int run_command(const std::string& cmd)
{
    std::cout << cmd << std::endl;
    return std::system(cmd.c_str());
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

static int run_command_in_dir(const std::filesystem::path& dir,
                              const std::string& cmd)
{
    return run_command("cd " + shell_quote(dir.string()) + " && " + cmd);
}

static int run_command_in_dir_with_paths(const std::filesystem::path& dir,
                                         const std::string& cmd,
                                         const std::vector<std::string>& entries)
{
    return run_command("cd " + shell_quote(dir.string()) + " && " +
                       command_with_path_entries(cmd, entries));
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
        if(run_command_with_paths(cloneCmd, pathEntries) != 0)
            return 1;
    }
    else if(updateExisting)
    {
        std::string fetchCmd = "git -C " + shell_quote(path.string()) +
                               " fetch --all --tags";
        if(run_command_with_paths(fetchCmd, pathEntries) != 0)
            return 1;
    }

    if(!dep.rev.empty())
    {
        std::string checkout = "git -C " + shell_quote(path.string()) +
                               " checkout " + shell_quote(dep.rev);
        if(run_command_with_paths(checkout, pathEntries) != 0)
            return 1;
    }
    else if(!dep.tag.empty())
    {
        std::string checkout = "git -C " + shell_quote(path.string()) +
                               " checkout " +
                               shell_quote("tags/" + dep.tag);
        if(run_command_with_paths(checkout, pathEntries) != 0)
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
        std::cerr << "Unsupported archive type for dependency '" << dep.name
                  << "': " << dep.archiveType << "\n";
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
            std::cerr << "Failed to reset partial archive checkout for '"
                      << dep.name << "': " << ec.message() << "\n";
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
    if(run_command_with_paths(downloadCmd, pathEntries) != 0)
        return 1;

    std::string extractCmd = "tar -xzf " + shell_quote(archivePath.string()) +
                             " -C " + shell_quote(extractDir.string());
    if(dep.stripComponents > 0)
    {
        extractCmd += " --strip-components=" +
                      std::to_string(dep.stripComponents);
    }
    if(run_command_with_paths(extractCmd, pathEntries) != 0)
        return 1;
    std::filesystem::rename(extractDir, checkoutDir, ec);
    if(ec)
    {
        std::cerr << "Failed to finalize archive checkout for '" << dep.name
                  << "': " << ec.message() << "\n";
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
    std::cerr << "Dependency '" << dep.name
              << "' is missing a supported source (git/url)\n";
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
        if(run_command_with_paths(cfg, pathEntries) != 0)
            return 1;
        std::string build =
            "cmake --build " + shell_quote(buildDir.string());
        return run_command_with_paths(build, pathEntries);
    }
    if(dep.build == "meson")
    {
        std::filesystem::path buildDir = path / "build";
        if(!std::filesystem::exists(buildDir))
        {
            std::string setup = "meson setup " +
                                shell_quote(buildDir.string()) + " " +
                                shell_quote(path.string());
            if(run_command_with_paths(setup, pathEntries) != 0)
                return 1;
        }
        std::string compile =
            "meson compile -C " + shell_quote(buildDir.string());
        return run_command_with_paths(compile, pathEntries);
    }
    if(dep.build == "make")
    {
        std::string effectiveMake = makeProgram.empty() ? "make" : makeProgram;
        std::string cmd = shell_quote(effectiveMake) + " -C " +
                          shell_quote(path.string());
        return run_command_with_paths(cmd, pathEntries);
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

static int fetch_for_manifest(const PackageManifest& pkg)
{
    auto deps = parse_source_deps(pkg.content);
    BuildConfig buildConfig = parse_build_config(pkg.content);
    std::filesystem::path depsDir = pkg.packageDir / "build" / "deps";
    std::filesystem::create_directories(depsDir);
    for(const auto& dep : deps)
    {
        if(fetch_dep(dep, depsDir, /*updateExisting=*/true,
                     buildConfig.pathEntries) != 0)
            return 1;
    }
    std::cout << "Fetch completed for " << pkg.manifestPath.string() << ".\n";
    return 0;
}

static int build_for_manifest(const PackageManifest& pkg, const std::string& argv0,
                              const std::string& optFlagOverride,
                              bool useNinja)
{
    auto deps = parse_source_deps(pkg.content);
    auto cdeps = parse_c_deps(pkg.content);
    BuildConfig packageBuildConfig = parse_build_config(pkg.content);
    std::vector<BuildTarget> targets = parse_bin_targets(pkg.content);
    if(targets.empty())
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

    std::filesystem::path depsDir = pkg.packageDir / "build" / "deps";
    std::filesystem::create_directories(depsDir);
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

    std::filesystem::create_directories(pkg.packageDir / "build");
    std::string backend = argv0;
    if(argv0.find('/') != std::string::npos)
        backend = std::filesystem::absolute(argv0).string();

    for(const auto& target : targets)
    {
        if(target.name.empty())
        {
            std::cerr << "Missing name in [[bin]] target for "
                      << pkg.manifestPath.string() << "\n";
            return 1;
        }
        if(target.entry.empty())
        {
            std::cerr << "Missing entry in [[bin]] target '" << target.name
                      << "' for " << pkg.manifestPath.string() << "\n";
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

        std::string output = "build/" + target.name;
        std::string cmd = shell_quote(backend) + " " +
                          shell_quote(target.entry) + " -o " +
                          shell_quote(output);
        if(!buildConfig.targetArch.empty())
            cmd += " --target-arch " + shell_quote(buildConfig.targetArch);
        if(!optFlag.empty())
            cmd += " " + optFlag;
        for(const auto& flag : buildConfig.compilerFlags)
            cmd += " " + shell_quote(flag);
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
            cmd += " " + shell_quote(flag);
        for(const auto& flag : pkgFlags)
            cmd += " " + shell_quote(flag);

        int rc = run_command_in_dir_with_paths(pkg.packageDir, cmd,
                                              buildConfig.pathEntries);
        if(rc != 0)
        {
            std::cerr << "Build failed for " << pkg.manifestPath.string();
            if(!target.name.empty())
                std::cerr << " target '" << target.name << "'";
            std::cerr << ".\n";
            return 1;
        }
    }
    return 0;
}

static int clean_for_manifest(const PackageManifest& pkg)
{
    const std::filesystem::path buildDir = pkg.packageDir / "build";
    if(!std::filesystem::exists(buildDir))
    {
        std::cout << "No artifacts to clean in " << buildDir.string() << "\n";
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
    const std::filesystem::path buildDir = pkg.packageDir / "build";
    const std::filesystem::path depsDir = buildDir / "deps";
    std::string out = text;
    out = replace_all(out, "{{root}}", pkg.packageDir.string());
    out = replace_all(out, "{{manifest}}", pkg.manifestPath.string());
    out = replace_all(out, "{{build_dir}}", buildDir.string());
    out = replace_all(out, "{{deps_dir}}", depsDir.string());
    out = replace_all(out, "{{make}}",
                      buildConfig.makeProgram.empty() ? "make"
                                                      : buildConfig.makeProgram);
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

static std::optional<std::filesystem::path>
write_task_script(const PackageManifest& pkg, const std::string& taskName,
                  const std::vector<std::string>& shellLines,
                  const BuildConfig& buildConfig)
{
    if(shellLines.empty())
        return std::nullopt;

    std::filesystem::path scriptDir = pkg.packageDir / "build" / "task-scripts";
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

        std::string effectiveWorkdir =
            (hostOverride && !hostOverride->workdir.empty())
                ? hostOverride->workdir
                : task.workdir;
        bool effectiveParallel =
            hostOverride && hostOverride->parallel.has_value()
                ? hostOverride->parallel.value()
                : task.parallel.value_or(false);

        if(effectiveCommands.empty() && effectiveShell.empty())
        {
            std::cerr << "Task '" << taskName << "' in "
                      << pkg.manifestPath.string()
                      << " has no commands or script.\n";
            return 1;
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
            if(run_command_in_dir_with_paths(workdir, expanded,
                                             buildConfig.pathEntries) != 0)
            {
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
                                 const std::string& taskName)
{
    const auto tasks = parse_task_specs(pkg.content);
    const BuildConfig buildConfig = parse_build_config(pkg.content);
    const std::string hostName = current_host_name();
    std::map<std::string, TaskRunState> taskStates;
    std::mutex taskMutex;
    std::condition_variable taskCv;
    std::vector<std::string> taskStack;
    return run_task_for_manifest_impl(pkg, tasks, buildConfig, hostName,
                                      taskStates, taskMutex, taskCv, taskName,
                                      taskStack);
}

} // namespace

int PackageManager::run(int argc, char** argv)
{
    if(argc < 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " pkg <init|add|fetch|build|run|clean>\n";
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
                      << " pkg add <name> --url URL [--archive tar.gz] [--strip-components N] [--subdir DIR]\n"
                      << "       " << argv[0]
                      << " pkg add <name> [--pkg-config NAME] [--system]\n"
                      << "       " << argv[0]
                      << " pkg add <name> [--git URL|--url URL] --add-lib [--project-dir DIR]\n";
            return 1;
        }

        std::string name = argv[3];
        std::string gitUrl;
        std::string archiveUrl;
        std::string archiveType;
        std::string rev;
        std::string tag;
        std::string depSubdir;
        std::string pkgConfig;
        std::string addLibProjectDir;
        bool systemDep = false;
        bool addLib = false;
        int stripComponents = 1;

        for(int i = 4; i < argc; ++i)
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
                std::cerr << "Failed to update root mlang.toml\n";
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
        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for fetch.\n";
            return 1;
        }
        for(const auto& pkg : manifests)
        {
            if(fetch_for_manifest(pkg) != 0)
                return 1;
        }
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

        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for build.\n";
            return 1;
        }
        for(const auto& pkg : manifests)
        {
            if(build_for_manifest(pkg, argv[0], optFlag, useNinja) != 0)
                return 1;
        }
        return 0;
    }

    if(sub == "run")
    {
        if(argc < 4)
        {
            std::cerr << "Usage: " << argv[0] << " pkg run <task>\n";
            return 1;
        }
        if(!std::filesystem::exists(manifestPath))
        {
            std::cerr << "mlang.toml not found. Run 'mlang pkg init' first.\n";
            return 1;
        }

        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
        {
            std::cerr << "No package manifests found for run.\n";
            return 1;
        }

        const std::string taskName = argv[3];
        bool found = false;
        for(const auto& pkg : manifests)
        {
            int rc = run_task_for_manifest(pkg, taskName);
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
        auto manifests = collect_target_manifests(manifestPath);
        if(manifests.empty())
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
        for(const auto& pkg : manifests)
        {
            if(clean_for_manifest(pkg) != 0)
                return 1;
        }
        return 0;
    }

    std::cerr << "Unknown pkg subcommand: " << sub << "\n";
    return 1;
}
