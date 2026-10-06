#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Keep the executable independent of a particular LLDB ABI. Its frontend uses
// the Python bindings distributed with the user's installed LLDB.
int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    fs::path executable;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if(_NSGetExecutablePath(path.data(), &size) == 0)
        executable = fs::weakly_canonical(path.data());
#else
    std::error_code error;
    executable = fs::read_symlink("/proc/self/exe", error);
#endif
    fs::path script = executable.parent_path() / "../" MLANG_DEBUGGER_DATA_DIR "/mladbg.py";
    if(!fs::is_regular_file(script))
        script = MLANG_DEBUGGER_SOURCE;
    if(!fs::is_regular_file(script))
    {
        std::cerr << "mladbg: frontend missing; reinstall mladbg\n";
        return 1;
    }
    const char* python = std::getenv("MLADBG_PYTHON");
    if(!python || !*python)
#ifdef __APPLE__
        python = "/usr/bin/python3";
#else
        python = "python3";
#endif
    std::string scriptPath = script.string();
    std::vector<char*> args{const_cast<char*>(python), scriptPath.data()};
    for(int i = 1; i < argc; ++i)
        args.push_back(argv[i]);
    args.push_back(nullptr);
    execvp(python, args.data());
    std::cerr << "mladbg: cannot start " << python << " (errno " << errno
              << "); set MLADBG_PYTHON to a Python with LLDB bindings\n";
    return 1;
}
