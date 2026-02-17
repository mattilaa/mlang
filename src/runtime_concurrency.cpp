#include "runtime_concurrency.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>

namespace
{

std::string read_file_binary(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
        return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

size_t parse_env_workers()
{
    const char* env = std::getenv("MLANG_INDEX_WORKERS");
    if(!env || *env == '\0')
        return 0;

    char* end = nullptr;
    unsigned long value = std::strtoul(env, &end, 10);
    if(end == env)
        return 0;
    return static_cast<size_t>(value);
}

} // namespace

namespace mlang::runtime
{

RuntimeScheduler::RuntimeScheduler(size_t workers)
    : workers_(normalizeWorkers(workers == 0 ? parse_env_workers() : workers))
{
}

size_t RuntimeScheduler::workerCount() const
{
    return workers_;
}

size_t RuntimeScheduler::normalizeWorkers(size_t requested)
{
    if(requested > 0)
        return requested;

    unsigned int hc = std::thread::hardware_concurrency();
    if(hc == 0)
        return 2;
    return std::max<size_t>(2, static_cast<size_t>(hc));
}

std::vector<LoadedFile>
RuntimeScheduler::loadFiles(const std::vector<std::string>& paths) const
{
    std::vector<LoadedFile> out(paths.size());
    if(paths.empty())
        return out;

    for(size_t i = 0; i < paths.size(); ++i)
        out[i].path = paths[i];

    const size_t workers = std::min(workers_, paths.size());
    if(workers <= 1)
    {
        for(auto& file : out)
            file.content = read_file_binary(file.path);
        return out;
    }

    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(workers);

    for(size_t w = 0; w < workers; ++w)
    {
        pool.emplace_back([&]() {
            while(true)
            {
                const size_t idx = next.fetch_add(1, std::memory_order_relaxed);
                if(idx >= out.size())
                    break;
                out[idx].content = read_file_binary(out[idx].path);
            }
        });
    }

    for(auto& t : pool)
        t.join();

    return out;
}

} // namespace mlang::runtime
