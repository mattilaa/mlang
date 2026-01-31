#include "module.h"
#include <filesystem>
#include <fstream>
#include <iostream>

// External parser functions
extern int yyparse();
extern FILE* yyin;
extern int yylineno;
extern "C"
{
    extern ASTNode* programRoot;
    extern bool parseHadError;
}

// Reset lexer state for new file
extern void yyrestart(FILE* input_file);

ModuleLoader::ModuleLoader(const std::string& basePath,
                           const std::vector<std::string>& extraPaths)
    : basePath(basePath)
{
    searchPaths.clear();
    if(!this->basePath.empty())
        searchPaths.push_back(this->basePath);
    for(const auto& p : extraPaths)
    {
        if(!p.empty())
            searchPaths.push_back(p);
    }
}

std::string ModuleLoader::resolveModulePath(const std::string& moduleName)
{
    namespace fs = std::filesystem;

    for(const auto& root : searchPaths)
    {
        // Try moduleName.mla in the search path
        fs::path modulePath = fs::path(root) / (moduleName + ".mla");
        if(fs::exists(modulePath))
            return modulePath.string();

        // Try moduleName/mod.mla (directory module)
        fs::path dirModulePath = fs::path(root) / moduleName / "mod.mla";
        if(fs::exists(dirModulePath))
            return dirModulePath.string();
    }

    return "";
}

ProgramNode* ModuleLoader::parseFile(const std::string& filePath)
{
    FILE* file = fopen(filePath.c_str(), "r");
    if(!file)
    {
        std::cerr << "Error: Cannot open module file: " << filePath
                  << std::endl;
        return nullptr;
    }

    // Save current parser state
    ASTNode* savedRoot = programRoot;
    programRoot = nullptr;

    // Reset lexer line number
    yylineno = 1;

    // Set up parser for new file
    yyrestart(file);
    yyin = file;

    // Parse the file
    parseHadError = false;
    int result = yyparse();
    fclose(file);

    ProgramNode* parsedProgram = nullptr;
    if(result == 0 && !parseHadError && programRoot)
    {
        parsedProgram = dynamic_cast<ProgramNode*>(programRoot);
    }

    // Restore previous parser state
    programRoot = savedRoot;

    return parsedProgram;
}

bool ModuleLoader::loadModule(const std::string& moduleName,
                              std::string& errorMsg)
{
    // Check if already loaded
    if(modules.find(moduleName) != modules.end() && modules[moduleName].loaded)
    {
        return true;
    }

    // Check for circular imports
    if(loadingStack.find(moduleName) != loadingStack.end())
    {
        errorMsg = "Circular import detected: " + moduleName;
        return false;
    }

    // Resolve the module path
    std::string modulePath = resolveModulePath(moduleName);
    if(modulePath.empty())
    {
        std::string paths;
        for(size_t i = 0; i < searchPaths.size(); ++i)
        {
            if(i > 0)
                paths += ", ";
            paths += searchPaths[i];
        }
        if(paths.empty())
            paths = basePath;
        errorMsg = "Cannot find module '" + moduleName + "' (looked for " +
                   moduleName + ".mla in " + paths + ")";
        return false;
    }

    // Add to loading stack
    loadingStack.insert(moduleName);

    // Parse the module file
    ProgramNode* moduleAst = parseFile(modulePath);
    if(!moduleAst)
    {
        errorMsg = "Failed to parse module: " + moduleName;
        loadingStack.erase(moduleName);
        return false;
    }

    // Process any mod declarations in this module (recursive loading)
    if(!processModDeclarations(moduleAst, errorMsg))
    {
        loadingStack.erase(moduleName);
        return false;
    }

    // Store the module
    ModuleInfo info;
    info.name = moduleName;
    info.filePath = modulePath;
    info.ast = moduleAst;
    info.loaded = true;
    modules[moduleName] = info;

    // Remove from loading stack
    loadingStack.erase(moduleName);

    return true;
}

ProgramNode* ModuleLoader::getModule(const std::string& moduleName)
{
    auto it = modules.find(moduleName);
    if(it != modules.end() && it->second.loaded)
    {
        return it->second.ast;
    }
    return nullptr;
}

std::vector<FunctionDefNode*>
ModuleLoader::getModuleFunctions(const std::string& moduleName)
{
    std::vector<FunctionDefNode*> functions;
    ProgramNode* module = getModule(moduleName);

    if(module && module->functionList)
    {
        functions = module->functionList->functions;
    }

    return functions;
}

FunctionDefNode* ModuleLoader::getFunction(const std::string& moduleName,
                                           const std::string& funcName)
{
    ProgramNode* module = getModule(moduleName);

    if(module && module->functionList)
    {
        for(auto* func : module->functionList->functions)
        {
            if(func->name == funcName)
            {
                return func;
            }
        }
    }

    return nullptr;
}

std::vector<StructDefNode*>
ModuleLoader::getModuleStructs(const std::string& moduleName)
{
    std::vector<StructDefNode*> structs;
    ProgramNode* module = getModule(moduleName);

    if(module && module->structList)
    {
        structs = module->structList->structs;
    }

    return structs;
}

bool ModuleLoader::processModDeclarations(ProgramNode* program,
                                          std::string& errorMsg)
{
    for(auto* modDecl : program->modules)
    {
        if(!loadModule(modDecl->moduleName, errorMsg))
        {
            return false;
        }
        // Store the resolved file path
        modDecl->filePath = modules[modDecl->moduleName].filePath;
    }
    return true;
}

bool ModuleLoader::processUseDeclarations(ProgramNode* program,
                                          std::string& errorMsg)
{
    for(auto* useDecl : program->imports)
    {
        // Check if the module is loaded
        ProgramNode* module = getModule(useDecl->moduleName);
        if(!module)
        {
            errorMsg = "Module '" + useDecl->moduleName +
                       "' not loaded. Add 'mod " + useDecl->moduleName +
                       ";' before using it.";
            return false;
        }

        // Always import ALL functions from the module (for internal calls)
        // but mark their source module so visibility can be checked at call
        // sites
        if(module->functionList)
        {
            if(!program->functionList)
            {
                program->functionList = new FunctionListNode();
            }
            for(auto* func : module->functionList->functions)
            {
                // Set source module for all functions
                func->sourceModule = useDecl->moduleName;

                // Check if function already added (avoid duplicates)
                bool alreadyAdded = false;
                for(auto* existing : program->functionList->functions)
                {
                    if(existing->name == func->name)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if(!alreadyAdded)
                {
                    program->functionList->functions.push_back(func);
                }
            }
        }

        // Always import ALL structs from the module (for internal use)
        if(module->structList)
        {
            if(!program->structList)
            {
                program->structList = new StructListNode();
            }
            for(auto* structDef : module->structList->structs)
            {
                // Set source module for all structs
                structDef->sourceModule = useDecl->moduleName;

                // Check if struct already added (avoid duplicates)
                bool alreadyAdded = false;
                for(auto* existing : program->structList->structs)
                {
                    if(existing->name == structDef->name)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if(!alreadyAdded)
                {
                    program->structList->addStruct(structDef);
                }
            }
        }

        // For specific imports (not import all), verify the item is public
        if(!useDecl->importAll)
        {
            bool found = false;

            // Try to find as function
            FunctionDefNode* func =
                getFunction(useDecl->moduleName, useDecl->itemName);
            if(func)
            {
                // Check if the function is public
                if(!func->isPublic)
                {
                    errorMsg = "function '" + useDecl->itemName +
                               "' is private in module '" +
                               useDecl->moduleName + "'";
                    return false;
                }
                found = true;
            }

            // Try to find as struct
            if(!found && module->structList)
            {
                for(auto* structDef : module->structList->structs)
                {
                    if(structDef->name == useDecl->itemName)
                    {
                        // Check if the struct is public
                        if(!structDef->isPublic)
                        {
                            errorMsg = "struct '" + useDecl->itemName +
                                       "' is private in module '" +
                                       useDecl->moduleName + "'";
                            return false;
                        }
                        found = true;
                        break;
                    }
                }
            }

            if(!found)
            {
                errorMsg = "'" + useDecl->itemName + "' not found in module '" +
                           useDecl->moduleName + "'";
                return false;
            }
        }
        else
        {
            // For "use module::*", verify at least one public item exists
            // (this is optional, just a warning might be nice)
        }
    }
    return true;
}

std::vector<std::string> ModuleLoader::getLoadedModules() const
{
    std::vector<std::string> names;
    for(const auto& pair : modules)
    {
        if(pair.second.loaded)
        {
            names.push_back(pair.first);
        }
    }
    return names;
}

std::vector<std::string> ModuleLoader::getLoadedModulePaths() const
{
    std::vector<std::string> paths;
    for(const auto& pair : modules)
    {
        if(pair.second.loaded && !pair.second.filePath.empty())
        {
            paths.push_back(pair.second.filePath);
        }
    }
    return paths;
}
