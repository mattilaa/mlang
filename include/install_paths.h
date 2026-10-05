#ifndef MLANG_INSTALL_PATHS_H
#define MLANG_INSTALL_PATHS_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace mlang
{

// The prefix the running tool is installed under: the parent of the directory
// holding its binary (symlinks resolved). An install moved or unpacked
// anywhere finds its own share/mlang and lib/mlang through it. Empty when the
// binary's location is unknown.
inline std::filesystem::path installed_prefix()
{
    static const std::filesystem::path prefix = [] {
        std::filesystem::path exe;
#ifdef __APPLE__
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if(size > 0 && _NSGetExecutablePath(buffer.data(), &size) == 0)
            exe = buffer.c_str();
#elif defined(__linux__)
        std::error_code linkEc;
        exe = std::filesystem::read_symlink("/proc/self/exe", linkEc);
#endif
        if(exe.empty())
            return std::filesystem::path();
        std::error_code ec;
        const auto real = std::filesystem::canonical(exe, ec);
        if(!ec)
            exe = real;
        return exe.parent_path().parent_path();
    }();
    return prefix;
}

// `relative` under the installed prefix, or empty when it does not exist.
inline std::string installed_path(const char* relative)
{
    const auto prefix = installed_prefix();
    if(prefix.empty())
        return std::string();
    std::error_code ec;
    const auto path = prefix / relative;
    return std::filesystem::is_directory(path, ec) ? path.string()
                                                   : std::string();
}

} // namespace mlang

#endif
