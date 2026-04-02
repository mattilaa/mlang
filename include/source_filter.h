#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace mlang {

inline std::string normalize_region_arch_name(std::string_view arch)
{
    std::string s(arch);
    for(char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    if(s == "x86_64" || s == "amd64" || s == "x64" || s == "x86-64")
        return "x86-64";
    if(s == "aarch64" || s == "arm64")
        return "aarch64";
    if(s == "x86" || s == "i386" || s == "i686")
        return "x86";
    return s;
}

inline std::string host_platform_region_name()
{
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(__unix__) || defined(__unix)
    return "posix";
#else
    return "unknown";
#endif
}

inline std::string host_arch_region_name()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    return "x86-64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

inline bool host_platform_is_posix()
{
#if defined(__unix__) || defined(__unix) || defined(__APPLE__) || defined(__linux__)
    return true;
#else
    return false;
#endif
}

inline bool region_tag_matches(std::string_view tag, std::string_view targetArch)
{
    std::string arch = normalize_region_arch_name(targetArch);
    if(arch.empty())
        arch = host_arch_region_name();
    if(tag == "x86-64")
        return arch == "x86-64";
    if(tag == "aarch64")
        return arch == "aarch64";
    if(tag == "windows")
        return host_platform_region_name() == "windows";
    if(tag == "linux")
        return host_platform_region_name() == "linux";
    if(tag == "macos")
        return host_platform_region_name() == "macos";
    if(tag == "posix")
        return host_platform_is_posix();
    return false;
}

inline bool parse_region_tag(std::string_view trimmed, bool& isClosing,
                             std::string& tag)
{
    if(trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']')
        return false;

    std::string_view inner = trimmed.substr(1, trimmed.size() - 2);
    isClosing = false;
    if(!inner.empty() && inner.front() == '/')
    {
        isClosing = true;
        inner.remove_prefix(1);
    }

    if(inner.empty())
        return false;

    const std::string normalized = normalize_region_arch_name(inner);
    if(normalized == "x86-64" || normalized == "aarch64" ||
       normalized == "windows" || normalized == "posix" ||
       normalized == "linux" || normalized == "macos")
    {
        tag = normalized;
        return true;
    }

    return false;
}

inline std::string preprocess_conditional_regions(std::string_view text,
                                                  std::string_view targetArch)
{
    std::string output;
    output.reserve(text.size());

    std::vector<bool> activeStack;
    activeStack.push_back(true);

    size_t pos = 0;
    while(pos < text.size())
    {
        size_t lineEnd = text.find('\n', pos);
        bool hasNewline = lineEnd != std::string_view::npos;
        if(!hasNewline)
            lineEnd = text.size();

        std::string_view line = text.substr(pos, lineEnd - pos);
        size_t left = 0;
        size_t right = line.size();
        while(left < right &&
              std::isspace(static_cast<unsigned char>(line[left])) != 0)
            ++left;
        while(right > left &&
              std::isspace(static_cast<unsigned char>(line[right - 1])) != 0)
            --right;
        std::string_view trimmed = line.substr(left, right - left);

        bool isClosing = false;
        std::string tag;
        if(parse_region_tag(trimmed, isClosing, tag))
        {
            if(isClosing)
            {
                if(activeStack.size() > 1)
                    activeStack.pop_back();
            }
            else
            {
                const bool parentActive = activeStack.back();
                activeStack.push_back(parentActive &&
                                      region_tag_matches(tag, targetArch));
            }
            if(hasNewline)
                output.push_back('\n');
        }
        else if(activeStack.back())
        {
            output.append(line.data(), line.size());
            if(hasNewline)
                output.push_back('\n');
        }
        else if(hasNewline)
        {
            output.push_back('\n');
        }

        pos = hasNewline ? lineEnd + 1 : lineEnd;
    }

    return output;
}

} // namespace mlang
