#include "workspace_graph.h"

#include <algorithm>
#include <deque>

namespace mlang::ide
{

void WorkspaceGraph::removeDocumentIndexes(const WorkspaceDocumentNode& doc)
{
    for(const auto& module : doc.providedModules)
    {
        auto it = moduleProviders_.find(module);
        if(it == moduleProviders_.end())
            continue;
        it->second.erase(doc.uri);
        if(it->second.empty())
            moduleProviders_.erase(it);
    }

    for(const auto& imp : doc.imports)
    {
        auto it = moduleImporters_.find(imp.moduleName);
        if(it == moduleImporters_.end())
            continue;
        it->second.erase(doc.uri);
        if(it->second.empty())
            moduleImporters_.erase(it);
    }
}

void WorkspaceGraph::upsertDocument(const WorkspaceDocumentNode& doc)
{
    if(auto it = documents_.find(doc.uri); it != documents_.end())
    {
        removeDocumentIndexes(it->second);
    }

    documents_[doc.uri] = doc;

    for(const auto& module : doc.providedModules)
    {
        if(module.empty())
            continue;
        moduleProviders_[module].insert(doc.uri);
    }

    for(const auto& imp : doc.imports)
    {
        if(imp.moduleName.empty())
            continue;
        moduleImporters_[imp.moduleName].insert(doc.uri);
    }
}

void WorkspaceGraph::removeDocument(std::string_view uri)
{
    auto it = documents_.find(std::string(uri));
    if(it == documents_.end())
        return;

    removeDocumentIndexes(it->second);
    documents_.erase(it);
}

bool WorkspaceGraph::hasDocument(std::string_view uri) const
{
    return documents_.find(std::string(uri)) != documents_.end();
}

size_t WorkspaceGraph::documentCount() const
{
    return documents_.size();
}

std::vector<std::string>
WorkspaceGraph::findProviderUris(std::string_view moduleName) const
{
    std::vector<std::string> out;
    auto it = moduleProviders_.find(std::string(moduleName));
    if(it == moduleProviders_.end())
        return out;

    out.reserve(it->second.size());
    for(const auto& uri : it->second)
        out.push_back(uri);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string>
WorkspaceGraph::directDependencyModules(std::string_view uri) const
{
    std::vector<std::string> out;
    auto it = documents_.find(std::string(uri));
    if(it == documents_.end())
        return out;

    std::unordered_set<std::string> seen;
    for(const auto& imp : it->second.imports)
    {
        if(imp.moduleName.empty() || !seen.insert(imp.moduleName).second)
            continue;
        out.push_back(imp.moduleName);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> WorkspaceGraph::directDependentUris(std::string_view uri) const
{
    std::vector<std::string> out;
    auto it = documents_.find(std::string(uri));
    if(it == documents_.end())
        return out;

    std::unordered_set<std::string> seen;
    for(const auto& provided : it->second.providedModules)
    {
        auto depIt = moduleImporters_.find(provided);
        if(depIt == moduleImporters_.end())
            continue;
        for(const auto& depUri : depIt->second)
        {
            if(depUri == it->second.uri)
                continue;
            if(seen.insert(depUri).second)
                out.push_back(depUri);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string>
WorkspaceGraph::transitiveDependencyModules(std::string_view uri) const
{
    std::vector<std::string> out;
    auto rootIt = documents_.find(std::string(uri));
    if(rootIt == documents_.end())
        return out;

    std::unordered_set<std::string> visitedModules;
    std::deque<std::string> queue;

    for(const auto& imp : rootIt->second.imports)
    {
        if(!imp.moduleName.empty() && visitedModules.insert(imp.moduleName).second)
            queue.push_back(imp.moduleName);
    }

    while(!queue.empty())
    {
        std::string module = std::move(queue.front());
        queue.pop_front();
        out.push_back(module);

        auto providerIt = moduleProviders_.find(module);
        if(providerIt == moduleProviders_.end())
            continue;

        for(const auto& providerUri : providerIt->second)
        {
            auto docIt = documents_.find(providerUri);
            if(docIt == documents_.end())
                continue;
            for(const auto& imp : docIt->second.imports)
            {
                if(imp.moduleName.empty())
                    continue;
                if(visitedModules.insert(imp.moduleName).second)
                    queue.push_back(imp.moduleName);
            }
        }
    }

    return out;
}

} // namespace mlang::ide
