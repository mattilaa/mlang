#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mlang::runtime
{

struct LoadedFile
{
    std::string path;
    std::string content;
};

class RuntimeScheduler
{
public:
    explicit RuntimeScheduler(size_t workers = 0);

    size_t workerCount() const;

    std::vector<LoadedFile>
    loadFiles(const std::vector<std::string>& paths) const;

private:
    static size_t normalizeWorkers(size_t requested);

    size_t workers_ = 1;
};

} // namespace mlang::runtime
