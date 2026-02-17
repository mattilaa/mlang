#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mlang::ide
{

struct WorkspaceImport
{
    std::string moduleName;
    std::string itemName;
    bool importAll = false;
};

struct WorkspaceDocumentNode
{
    std::string uri;
    std::string path;
    std::vector<std::string> providedModules;
    std::vector<WorkspaceImport> imports;
};

class WorkspaceGraph
{
public:
    void upsertDocument(const WorkspaceDocumentNode& doc);
    void removeDocument(std::string_view uri);

    bool hasDocument(std::string_view uri) const;
    size_t documentCount() const;

    std::vector<std::string> findProviderUris(std::string_view moduleName) const;
    std::vector<std::string>
    directDependencyModules(std::string_view uri) const;
    std::vector<std::string> directDependentUris(std::string_view uri) const;
    std::vector<std::string>
    transitiveDependencyModules(std::string_view uri) const;

private:
    void removeDocumentIndexes(const WorkspaceDocumentNode& doc);

    std::unordered_map<std::string, WorkspaceDocumentNode> documents_;
    std::unordered_map<std::string, std::unordered_set<std::string>>
        moduleProviders_;
    std::unordered_map<std::string, std::unordered_set<std::string>>
        moduleImporters_;
};

} // namespace mlang::ide
