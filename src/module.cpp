#include "module.h"
#include <filesystem>
#include <fstream>
#include <iostream>

static std::string type_mangle(TypeNode* typeNode)
{
    if(!typeNode)
        return "void";

    if(auto* ptrType = dynamic_cast<PointerTypeNode*>(typeNode))
        return "ptr_" + type_mangle(ptrType->elementType);

    if(auto* genList = dynamic_cast<GenericListTypeNode*>(typeNode))
        return "list_" + type_mangle(genList->elementType);

    if(auto* mapType = dynamic_cast<MapTypeNode*>(typeNode))
    {
        return "map_" + type_mangle(mapType->keyType) + "_" +
               type_mangle(mapType->valueType);
    }

    if(auto* tupleType = dynamic_cast<TupleTypeNode*>(typeNode))
    {
        std::string out = "tuple";
        if(tupleType->elementTypes)
        {
            for(auto* elem : tupleType->elementTypes->types)
            {
                out += "_" + type_mangle(elem);
            }
        }
        return out;
    }

    if(auto* genStruct = dynamic_cast<GenericStructTypeRefNode*>(typeNode))
    {
        std::string out = "struct_" + genStruct->structName;
        for(auto* arg : genStruct->typeArgs)
        {
            out += "_" + type_mangle(arg);
        }
        return out;
    }

    if(auto* structRef = dynamic_cast<StructTypeRefNode*>(typeNode))
        return "struct_" + structRef->structName;

    switch(typeNode->kind)
    {
    case TypeNode::TYPE_VOID:
        return "void";
    case TypeNode::TYPE_BOOL:
        return "bool";
    case TypeNode::TYPE_INT:
        return "int";
    case TypeNode::TYPE_FLOAT:
        return "float";
    case TypeNode::TYPE_DOUBLE:
        return "double";
    case TypeNode::TYPE_STRING:
        return "string";
    case TypeNode::TYPE_STR8:
        return "str8";
    case TypeNode::TYPE_STR16:
        return "str16";
    case TypeNode::TYPE_LIST:
        return "list";
    case TypeNode::TYPE_MAP:
        return "map";
    case TypeNode::TYPE_TUPLE:
        return "tuple";
    case TypeNode::TYPE_PTR:
        return "ptr";
    case TypeNode::TYPE_STRUCT:
        return "struct";
    case TypeNode::TYPE_I8:
        return "i8";
    case TypeNode::TYPE_I16:
        return "i16";
    case TypeNode::TYPE_I32:
        return "i32";
    case TypeNode::TYPE_I64:
        return "i64";
    case TypeNode::TYPE_U8:
        return "u8";
    case TypeNode::TYPE_U16:
        return "u16";
    case TypeNode::TYPE_U32:
        return "u32";
    case TypeNode::TYPE_U64:
        return "u64";
    default:
        return "unknown";
    }
}

static std::string function_signature_key(FunctionDefNode* node)
{
    std::string key = node->name + "(";
    if(node->parameters)
    {
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            if(i > 0)
                key += ",";
            key += type_mangle(node->parameters->parameters[i]->type);
        }
        if(node->parameters->isVarArg)
        {
            if(!node->parameters->parameters.empty())
                key += ",";
            key += "...";
        }
    }
    key += ")";
    return key;
}

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

    std::string relName = moduleName;
    size_t pos = 0;
    while((pos = relName.find("::", pos)) != std::string::npos)
    {
        relName.replace(pos, 2, "/");
        pos += 1;
    }

    for(const auto& root : searchPaths)
    {
        // Try moduleName.mla in the search path
        fs::path modulePath = fs::path(root) / (relName + ".mla");
        if(fs::exists(modulePath))
            return modulePath.string();

        // Try moduleName/mod.mla (directory module)
        fs::path dirModulePath = fs::path(root) / relName / "mod.mla";
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
                std::string sigKey = function_signature_key(func);
                bool alreadyAdded = false;
                for(auto* existing : program->functionList->functions)
                {
                    if(function_signature_key(existing) == sigKey)
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

        // Always import impl blocks so methods are available across modules.
        if(module->implList)
        {
            if(!program->implList)
            {
                program->implList = new ImplListNode();
            }
            for(auto* impl : module->implList->impls)
            {
                bool alreadyAdded = false;
                for(auto* existing : program->implList->impls)
                {
                    if(existing->structName == impl->structName &&
                       existing->methods.size() == impl->methods.size())
                    {
                        bool same = true;
                        for(size_t i = 0; i < impl->methods.size(); ++i)
                        {
                            if(existing->methods[i]->name != impl->methods[i]->name)
                            {
                                same = false;
                                break;
                            }
                        }
                        if(same)
                        {
                            alreadyAdded = true;
                            break;
                        }
                    }
                }
                if(!alreadyAdded)
                {
                    program->implList->impls.push_back(impl);
                }
            }
        }

        // For specific imports (not import all), verify the item is public
        if(!useDecl->importAll)
        {
            bool found = false;

            // Try to find as function
            bool hasFunction = false;
            bool hasPublicFunction = false;
            if(module->functionList)
            {
                for(auto* func : module->functionList->functions)
                {
                    if(func->name == useDecl->itemName)
                    {
                        hasFunction = true;
                        if(func->isPublic)
                            hasPublicFunction = true;
                    }
                }
            }
            if(hasFunction)
            {
                if(!hasPublicFunction)
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
