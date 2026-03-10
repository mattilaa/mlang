#pragma once

#include "ast.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Forward declarations
struct ModuleInfo
{
    std::string name;
    std::string filePath;
    ProgramNode* ast;
    bool loaded;

    ModuleInfo() : ast(nullptr), loaded(false) {}
};

class ModuleLoader
{
private:
    std::string basePath; // Base directory for module resolution
    std::vector<std::string> searchPaths;
    std::map<std::string, ModuleInfo> modules;
    std::set<std::string> loadingStack; // For detecting circular imports

    // Parse a single file and return its AST
    ProgramNode* parseFile(const std::string& filePath);

    // Resolve module name to file path
    std::string resolveModulePath(const std::string& moduleName);

public:
    explicit ModuleLoader(const std::string& basePath = ".",
                          const std::vector<std::string>& extraPaths = {});

    // Load a module by name
    bool loadModule(const std::string& moduleName, std::string& errorMsg);

    // Get a loaded module's AST
    ProgramNode* getModule(const std::string& moduleName);

    // Get all functions from a module
    std::vector<FunctionDefNode*>
    getModuleFunctions(const std::string& moduleName);

    // Get a specific function from a module
    FunctionDefNode* getFunction(const std::string& moduleName,
                                 const std::string& funcName);

    // Get all structs from a module
    std::vector<StructDefNode*> getModuleStructs(const std::string& moduleName);

    // Process all mod declarations in a program
    bool processModDeclarations(ProgramNode* program, std::string& errorMsg);

    // Merge imported symbols into the main program
    bool processUseDeclarations(ProgramNode* program, std::string& errorMsg,
                                const std::string& currentModuleName = "");

    // Set the base path for module resolution
    void setBasePath(const std::string& path)
    {
        basePath = path;
        if(searchPaths.empty())
            searchPaths.push_back(basePath);
        else
            searchPaths[0] = basePath;
    }

    // Get list of all loaded module names
    std::vector<std::string> getLoadedModules() const;

    // Get list of all loaded module file paths
    std::vector<std::string> getLoadedModulePaths() const;
};
