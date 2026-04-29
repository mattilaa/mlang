#include "module.h"
#include "source_filter.h"
#include <filesystem>
#include <fstream>
#include <iostream>

static bool is_same_module_family(const std::string& a, const std::string& b)
{
    if(a.empty() || b.empty())
        return a == b;
    if(a == b)
        return true;
    auto is_nested = [](const std::string& parent, const std::string& child)
    {
        if(child.size() <= parent.size())
            return false;
        if(child.compare(0, parent.size(), parent) != 0)
            return false;
        return child.compare(parent.size(), 2, "::") == 0;
    };
    return is_nested(a, b) || is_nested(b, a);
}

static std::string type_mangle(TypeNode* typeNode)
{
    if(!typeNode)
        return "void";

    if(auto* refType = dynamic_cast<ReferenceTypeNode*>(typeNode))
    {
        return refType->isMutable
                   ? "ref_mut_" + type_mangle(refType->elementType)
                   : "ref_" + type_mangle(refType->elementType);
    }

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
        return "i32";
    case TypeNode::TYPE_FLOAT:
        return "f32";
    case TypeNode::TYPE_DOUBLE:
        return "f64";
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
    case TypeNode::TYPE_REF:
        return "ref";
    case TypeNode::TYPE_REF_MUT:
        return "ref_mut";
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
    if(!node)
        return "<null_fn>()";

    std::string key = node->name + "(";
    if(node->parameters)
    {
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            if(i > 0)
                key += ",";
            auto* param = node->parameters->parameters[i];
            if(!param)
            {
                key += "unknown";
                continue;
            }
            key += type_mangle(param->type);
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
extern const char* g_sourceFile;
extern const char* g_targetArchForParse;
extern "C"
{
    extern ASTNode* programRoot;
    extern bool parseHadError;
}

typedef size_t yy_size_t;
struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_bytes(const char* bytes, yy_size_t len);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

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
    std::ifstream file(filePath);
    if(!file)
    {
        std::cerr << "Error: Cannot open module file: " << filePath
                  << std::endl;
        return nullptr;
    }
    const std::string rawText((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    const std::string filteredText =
        mlang::preprocess_conditional_regions(rawText, targetArchOverride);

    // Save current parser state
    ASTNode* savedRoot = programRoot;
    programRoot = nullptr;

    // Reset lexer line number
    yylineno = 1;

    // Parse the file
    parseHadError = false;
    g_sourceFile = filePath.c_str();
    g_targetArchForParse = targetArchOverride.c_str();
    YY_BUFFER_STATE buffer = yy_scan_bytes(
        filteredText.data(), static_cast<yy_size_t>(filteredText.size()));
    int result = yyparse();
    yy_delete_buffer(buffer);

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

    // Mark symbols declared in this module with their source module name.
    if(moduleAst->functionList)
    {
        for(auto* fn : moduleAst->functionList->functions)
        {
            if(fn && fn->sourceModule.empty())
                fn->sourceModule = moduleName;
        }
    }
    if(moduleAst->structList)
    {
        for(auto* st : moduleAst->structList->structs)
        {
            if(st && st->sourceModule.empty())
                st->sourceModule = moduleName;
        }
    }
    if(moduleAst->enumList)
    {
        for(auto* en : moduleAst->enumList->enums)
        {
            if(en && en->sourceModule.empty())
                en->sourceModule = moduleName;
        }
    }
    for(auto* tr : moduleAst->traitDefs)
    {
        if(tr && tr->sourceModule.empty())
            tr->sourceModule = moduleName;
    }
    // Process any mod declarations in this module (recursive loading)
    if(!processModDeclarations(moduleAst, errorMsg))
    {
        loadingStack.erase(moduleName);
        return false;
    }

    // Process use declarations within the module itself so that symbols
    // imported from sub-modules (e.g. `use std::math::detail::*;`) are
    // merged into this module's function list before it is stored.
    if(!processUseDeclarations(moduleAst, errorMsg, moduleName))
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
                                          std::string& errorMsg,
                                          const std::string& currentModuleName)
{
    std::unordered_map<std::string, std::string> moduleAliases;
    auto resolveModuleAlias = [&](const std::string& rawModule) -> std::string
    {
        size_t sep = rawModule.find("::");
        std::string head =
            (sep == std::string::npos) ? rawModule : rawModule.substr(0, sep);
        auto it = moduleAliases.find(head);
        if(it == moduleAliases.end())
            return rawModule;
        if(sep == std::string::npos)
            return it->second;
        return it->second + rawModule.substr(sep);
    };

    for(auto* useDecl : program->imports)
    {
        std::string resolvedModuleName = resolveModuleAlias(useDecl->moduleName);
        bool skipSpecificImportCheck = false;

        auto bindModuleAlias = [&](const std::string& targetModuleName) -> bool
        {
            ProgramNode* aliasedModule = getModule(targetModuleName);
            if(!aliasedModule)
            {
                errorMsg = "Module '" + targetModuleName +
                           "' not loaded. Add 'mod " + targetModuleName +
                           ";' before using it.";
                return false;
            }
            moduleAliases[useDecl->aliasName] = targetModuleName;

            // Register alias-qualified functions so calls like alias::fn(...)
            // resolve directly without additional parser/runtime rewrites.
            if(aliasedModule->functionList)
            {
                if(!program->functionList)
                    program->functionList = new FunctionListNode();
                for(auto* func : aliasedModule->functionList->functions)
                {
                    if(!func || func->name.empty())
                        continue;
                    auto* aliasFn = new FunctionDefNode(*func);
                    aliasFn->name = useDecl->aliasName + "::" + func->name;
                    if(aliasFn->sourceModule.empty())
                        aliasFn->sourceModule = targetModuleName;

                    std::string sigKey = function_signature_key(aliasFn);
                    bool alreadyAdded = false;
                    for(auto* existing : program->functionList->functions)
                    {
                        if(existing && function_signature_key(existing) == sigKey)
                        {
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if(!alreadyAdded)
                        program->functionList->functions.push_back(aliasFn);
                }
            }
            return true;
        };

        if(useDecl->moduleAlias)
        {
            if(!bindModuleAlias(resolvedModuleName))
                return false;
            skipSpecificImportCheck = true;
        }

        // Ambiguous parse fallback:
        // `use a::b as x;` may be parsed as item alias from module `a`.
        // If `a` is not a loaded module but `a::b` is, treat it as module alias.
        if(!useDecl->importAll && !useDecl->aliasName.empty())
        {
            std::string combinedModuleName =
                resolveModuleAlias(useDecl->moduleName + "::" + useDecl->itemName);
            if(!getModule(resolvedModuleName) && getModule(combinedModuleName))
            {
                if(!bindModuleAlias(combinedModuleName))
                    return false;
                resolvedModuleName = combinedModuleName;
                skipSpecificImportCheck = true;
            }
        }

        // Check if the module is loaded
        ProgramNode* module = getModule(resolvedModuleName);
        if(!module)
        {
            errorMsg = "Module '" + resolvedModuleName +
                       "' not loaded. Add 'mod " + resolvedModuleName +
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
                if(!func)
                    continue;
                // Preserve already-tagged origin module (e.g. std::x::detail)
                // and only set when not yet known.
                if(func->sourceModule.empty())
                    func->sourceModule = resolvedModuleName;

                // Check if function already added (avoid duplicates)
                std::string sigKey = function_signature_key(func);
                bool alreadyAdded = false;
                for(size_t i = 0; i < program->functionList->functions.size(); ++i)
                {
                    auto* existing = program->functionList->functions[i];
                    if(!existing)
                        continue;
                    if(function_signature_key(existing) == sigKey)
                    {
                        // Prefer the more visible symbol when signatures collide
                        // across modules (e.g. private detail binding vs public
                        // facade declaration).
                        if(!existing->isPublic && func->isPublic)
                        {
                            program->functionList->functions[i] = func;
                        }
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
                structDef->sourceModule = resolvedModuleName;
                if(structDef->members)
                {
                    for(auto* method : structDef->members->methods)
                    {
                        if(method && method->sourceModule.empty())
                            method->sourceModule = resolvedModuleName;
                    }
                }

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
                for(auto* method : impl->methods)
                {
                    if(method && method->sourceModule.empty())
                        method->sourceModule = resolvedModuleName;
                }
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

        // Always import ALL enums from the module.
        if(module->enumList && !module->enumList->enums.empty())
        {
            if(!program->enumList)
                program->enumList = new EnumListNode();
            for(auto* enumDef : module->enumList->enums)
            {
                if(!enumDef)
                    continue;
                if(enumDef->sourceModule.empty())
                    enumDef->sourceModule = resolvedModuleName;

                bool alreadyAdded = false;
                for(auto* existing : program->enumList->enums)
                {
                    if(existing->name == enumDef->name)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if(!alreadyAdded)
                {
                    program->enumList->enums.push_back(enumDef);
                }
            }
        }

        // Import type aliases so they can be used in the current module.
        if(!module->typeAliases.empty())
        {
            for(auto* aliasDef : module->typeAliases)
            {
                bool alreadyAdded = false;
                for(auto* existing : program->typeAliases)
                {
                    if(existing->name == aliasDef->name)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if(!alreadyAdded)
                {
                    program->typeAliases.push_back(aliasDef);
                }
            }
        }

        // Import traits so trait names can be referenced in impl blocks.
        if(!module->traitDefs.empty())
        {
            for(auto* traitDef : module->traitDefs)
            {
                if(!traitDef)
                    continue;
                bool alreadyAdded = false;
                for(auto* existing : program->traitDefs)
                {
                    if(existing->name == traitDef->name)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if(!alreadyAdded)
                    program->traitDefs.push_back(traitDef);
            }
        }

        // For specific imports (not import all), verify the item is public
        if(!useDecl->importAll && !skipSpecificImportCheck)
        {
            bool found = false;

            // Try to find as function
            bool hasFunction = false;
            bool hasPublicFunction = false;
            if(module->functionList)
            {
                for(auto* func : module->functionList->functions)
                {
                    if(!func)
                        continue;
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
                    if(!is_same_module_family(currentModuleName,
                                              resolvedModuleName))
                    {
                        errorMsg = "function '" + useDecl->itemName +
                                   "' is private in module '" +
                                   resolvedModuleName + "'";
                        return false;
                    }
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
                            if(!is_same_module_family(currentModuleName,
                                                      resolvedModuleName))
                            {
                                errorMsg = "struct '" + useDecl->itemName +
                                           "' is private in module '" +
                                           resolvedModuleName + "'";
                                return false;
                            }
                        }
                        found = true;
                        break;
                    }
                }
            }

            // Try to find as type alias
            if(!found && !module->typeAliases.empty())
            {
                for(auto* aliasDef : module->typeAliases)
                {
                    if(aliasDef->name == useDecl->itemName)
                    {
                        found = true;
                        break;
                    }
                }
            }

            // Try to find as enum
            if(!found && module->enumList && !module->enumList->enums.empty())
            {
                for(auto* enumDef : module->enumList->enums)
                {
                    if(enumDef->name == useDecl->itemName)
                    {
                        if(!enumDef->isPublic)
                        {
                            if(!is_same_module_family(currentModuleName,
                                                      resolvedModuleName))
                            {
                                errorMsg = "enum '" + useDecl->itemName +
                                           "' is private in module '" +
                                           resolvedModuleName + "'";
                                return false;
                            }
                        }
                        found = true;
                        break;
                    }
                }
            }

            // Try to find as trait
            if(!found && !module->traitDefs.empty())
            {
                for(auto* traitDef : module->traitDefs)
                {
                    if(traitDef && traitDef->name == useDecl->itemName)
                    {
                        found = true;
                        break;
                    }
                }
            }

            if(!found)
            {
                errorMsg = "'" + useDecl->itemName + "' not found in module '" +
                           resolvedModuleName + "'";
                return false;
            }

            if(!useDecl->aliasName.empty())
            {
                bool aliasBound = false;

                if(module->functionList)
                {
                    if(!program->functionList)
                        program->functionList = new FunctionListNode();
                    for(auto* func : module->functionList->functions)
                    {
                        if(!func || func->name != useDecl->itemName)
                            continue;
                        auto* aliasFn = new FunctionDefNode(*func);
                        aliasFn->name = useDecl->aliasName;
                        if(aliasFn->sourceModule.empty())
                            aliasFn->sourceModule = resolvedModuleName;

                        std::string sigKey = function_signature_key(aliasFn);
                        bool alreadyAdded = false;
                        for(size_t i = 0; i < program->functionList->functions.size();
                            ++i)
                        {
                            auto* existing = program->functionList->functions[i];
                            if(!existing)
                                continue;
                            if(function_signature_key(existing) == sigKey)
                            {
                                alreadyAdded = true;
                                break;
                            }
                        }
                        if(!alreadyAdded)
                            program->functionList->functions.push_back(aliasFn);
                        aliasBound = true;
                    }
                }

                if(!aliasBound && module->structList)
                {
                    for(auto* structDef : module->structList->structs)
                    {
                        if(!structDef || structDef->name != useDecl->itemName)
                            continue;
                        bool exists = false;
                        for(auto* existing : program->typeAliases)
                        {
                            if(existing && existing->name == useDecl->aliasName)
                            {
                                exists = true;
                                break;
                            }
                        }
                        if(!exists)
                        {
                            auto* aliasType =
                                new TypeAliasNode(useDecl->aliasName,
                                                  new StructTypeRefNode(
                                                      useDecl->itemName));
                            program->typeAliases.push_back(aliasType);
                        }
                        aliasBound = true;
                        break;
                    }
                }

                if(!aliasBound && module->enumList && !module->enumList->enums.empty())
                {
                    for(auto* enumDef : module->enumList->enums)
                    {
                        if(!enumDef || enumDef->name != useDecl->itemName)
                            continue;
                        bool exists = false;
                        for(auto* existing : program->typeAliases)
                        {
                            if(existing && existing->name == useDecl->aliasName)
                            {
                                exists = true;
                                break;
                            }
                        }
                        if(!exists)
                        {
                            auto* aliasType =
                                new TypeAliasNode(useDecl->aliasName,
                                                  new StructTypeRefNode(
                                                      useDecl->itemName));
                            program->typeAliases.push_back(aliasType);
                        }
                        aliasBound = true;
                        break;
                    }
                }

                if(!aliasBound)
                {
                    errorMsg = "cannot alias '" + useDecl->itemName +
                               "' from module '" + resolvedModuleName + "'";
                    return false;
                }
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
