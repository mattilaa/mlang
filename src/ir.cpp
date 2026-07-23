#include "ir.h"
#include "diagnostics.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"
#include "ir/expression_type_kind.h"
#include "ir/return_inference.h"
#include "module.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <llvm/IR/Verifier.h>
#include <pthread.h>
#include <unordered_map>
#include <unordered_set>

namespace
{

using mlang::ir_detail::ast_analysis::collect_used_idents;
using mlang::ir_detail::ast_analysis::contains_exception_control_flow;
using mlang::ir_detail::ast_analysis::contains_update_expression;
using mlang::ir_detail::common::Helpers;
using mlang::ir_detail::return_inference::infer_function_return_type;

} // namespace

#include <llvm/Config/llvm-config.h>
#include <llvm/Bitcode/BitcodeWriter.h>

void CodeGenerator::generateCode(ProgramNode* program)
{
    globalNamedValues.clear();
    globalConstantVariables.clear();
    globalVariableTypes.clear();
    globalStructVariableTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    constexprValues.clear();
    deferredModuleFunctionDefs.clear();

    ensureHandleBuiltin(program);
    ensureThreadBuiltin(program);
    ensureMutexBuiltin(program);
    ensureAtomic64Builtin(program);
    ensureOptionBuiltin(program);
    ensureResultBuiltin(program);

    traitDefinitions.clear();
    structImplementedTraits.clear();
    for(auto* traitDef : program->traitDefs)
    {
        if(!traitDef || traitDef->name.empty())
            continue;
        traitDefinitions[traitDef->name] = traitDef;
    }
    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl || impl->traitName.empty())
                continue;
            if(!impl->typeParams.empty())
                continue;
            structImplementedTraits[impl->structName].insert(impl->traitName);
        }
    }

    resolveTypeAliasesInProgram(program);

    for(auto* cexprDecl : program->cexprDecls)
    {
        if(cexprDecl)
            generateCexprDeclaration(cexprDecl, false);
    }

    enum class MainArgMode
    {
        None,
        ArgsList,
        ArgsListWithCount
    };

    FunctionDefNode* mainDef = nullptr;
    FunctionDefNode* firstUserFunction = nullptr;
    MainArgMode mainArgMode = MainArgMode::None;
    GenericListTypeNode* mainArgsListType = nullptr;
    TypeNode::TypeKind mainArgcKind = TypeNode::TYPE_VOID;
    std::vector<FunctionDefNode*> testFunctions;

    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(fn && !fn->isExtern && !firstUserFunction)
                firstUserFunction = fn;
            if(fn && fn->name == "main" && !fn->isExtern)
            {
                mainDef = fn;
            }
            if(fn && fn->isTest && !fn->isExtern)
                testFunctions.push_back(fn);
        }
    }

    if(program->functionList)
    {
        std::unordered_map<std::string, TypeNode::TypeKind> fnReturnKinds;
        for(auto* fn : program->functionList->functions)
        {
            if(!fn || fn->isExtern || !fn->returnType)
                continue;
            auto it = fnReturnKinds.find(fn->name);
            if(it == fnReturnKinds.end())
            {
                fnReturnKinds[fn->name] =
                    Helpers::normalizeInferredKind(fn->returnType->kind);
            }
            else if(it->second != Helpers::normalizeInferredKind(fn->returnType->kind))
            {
                // Ambiguous overload returns for name-only inference; drop
                // entry.
                fnReturnKinds.erase(it);
            }
        }

        bool progress = true;
        while(progress)
        {
            progress = false;
            for(auto* fn : program->functionList->functions)
            {
                if(!fn || fn->isExtern || fn->returnType)
                    continue;
                if(!fn->typeParams.empty() && fn->isCexpr)
                    continue;
                if(fn->name == "main")
                {
                    fn->returnType = new TypeNode(TypeNode::TYPE_I32);
                    fnReturnKinds[fn->name] = TypeNode::TYPE_I32;
                    progress = true;
                    continue;
                }

                TypeNode::TypeKind inferredKind = TypeNode::TYPE_VOID;
                std::string reason;
                if(!infer_function_return_type(fn, fnReturnKinds, inferredKind,
                                               reason))
                    continue;

                fn->returnType = new TypeNode(inferredKind);
                fnReturnKinds[fn->name] = inferredKind;
                progress = true;
            }
        }

        for(auto* fn : program->functionList->functions)
        {
            if(!fn || fn->isExtern || fn->returnType)
                continue;
            if(!fn->typeParams.empty() && fn->isCexpr)
                continue;
            TypeNode::TypeKind inferredKind = TypeNode::TYPE_VOID;
            std::string reason;
            if(!infer_function_return_type(fn, fnReturnKinds, inferredKind,
                                           reason))
            {
                reportError(fn->line,
                            "cannot infer return type for function '" +
                                fn->name + "': " + reason);
                fn->returnType = new TypeNode(TypeNode::TYPE_VOID);
            }
            else
            {
                fn->returnType = new TypeNode(inferredKind);
            }
        }
    }

    if(testMode && mainDef)
    {
        reportError(mainDef->line,
                    "main is not allowed in test mode; use #[test] functions");
    }
    else if(requireMain && !mainDef)
    {
        std::string msg =
            "missing entry point: executable builds require 'fn main() -> i32'";
        if(firstUserFunction)
        {
            msg += "; found function '" + firstUserFunction->name +
                   "' instead";
            if(firstUserFunction->name != "main")
                msg += " (did you mean 'main'?)";
        }
        reportError(firstUserFunction ? firstUserFunction->line : 1, msg);
    }

    if(mainDef)
    {
        size_t paramCount = mainDef->parameters->parameters.size();
        bool returnOk = mainDef->returnType &&
                        (mainDef->returnType->kind == TypeNode::TYPE_INT ||
                         mainDef->returnType->kind == TypeNode::TYPE_I32);
        if(!returnOk)
        {
            reportError(
                mainDef->line,
                "invalid signature for 'main': return type must be i32");
        }

        if(paramCount == 0)
        {
            mainArgMode = MainArgMode::None;
        }
        else if(paramCount == 1 || paramCount == 2)
        {
            size_t listIndex = paramCount - 1;
            if(paramCount == 2)
            {
                mainArgcKind = mainDef->parameters->parameters[0]->type->kind;
                if(!(mainArgcKind == TypeNode::TYPE_INT ||
                     mainArgcKind == TypeNode::TYPE_I32 ||
                     mainArgcKind == TypeNode::TYPE_I64))
                {
                    reportError(
                        mainDef->line,
                        "invalid signature for 'main': argc must be i32/i64");
                }
            }

            auto* listType = dynamic_cast<GenericListTypeNode*>(
                mainDef->parameters->parameters[listIndex]->type);
            if(!listType)
            {
                reportError(
                    mainDef->line,
                    "invalid signature for 'main': argv must be list<str8>");
            }
            else
            {
                auto elemKind = listType->elementType->kind;
                if(!(elemKind == TypeNode::TYPE_STR8 ||
                     elemKind == TypeNode::TYPE_STRING))
                {
                    reportError(mainDef->line, "invalid signature for 'main': "
                                               "argv must be list<str8>");
                }
                else
                {
                    mainArgsListType = listType;
                }
            }

            mainArgMode = (paramCount == 1) ? MainArgMode::ArgsList
                                            : MainArgMode::ArgsListWithCount;
            mainDef->name = "__mlang_user_main";
        }
        else
        {
            reportError(mainDef->line,
                        "invalid signature for 'main': expected no args, "
                        "list<str8>, or (i32, list<str8>)");
        }
    }

    // Reserved type keywords and type/name conflicts.
    const std::unordered_set<std::string> reservedTypeNames = {
        "void", "bool", "f32", "f64", "str8", "str16", "list", "map", "tuple",
        "i8",   "i16",  "i32", "i64", "u8",   "u16",   "u32",  "u64"};

    std::map<std::string, std::pair<std::string, int>> typeDefs;
    if(!program->typeAliases.empty())
    {
        std::set<std::string> seenAliasNames;
        for(auto* aliasDef : program->typeAliases)
        {
            if(!aliasDef)
                continue;
            if(seenAliasNames.count(aliasDef->name))
                continue;
            seenAliasNames.insert(aliasDef->name);
            if(reservedTypeNames.count(aliasDef->name))
            {
                reportError(aliasDef->line, aliasDef->col,
                            "type name '" + aliasDef->name +
                                "' is a reserved keyword");
            }
            auto it = typeDefs.find(aliasDef->name);
            if(it != typeDefs.end())
            {
                reportError(aliasDef->line, aliasDef->col,
                            "type name '" + aliasDef->name +
                                "' conflicts with earlier " + it->second.first +
                                " defined at line " +
                                std::to_string(it->second.second));
            }
            else
            {
                typeDefs[aliasDef->name] = {"type alias", aliasDef->line};
            }
        }
    }
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(!st)
                continue;
            if(reservedTypeNames.count(st->name))
            {
                reportError(st->line, "type name '" + st->name +
                                          "' is a reserved keyword");
            }
            auto it = typeDefs.find(st->name);
            if(it != typeDefs.end())
            {
                reportError(st->line, "type name '" + st->name +
                                          "' conflicts with earlier " +
                                          it->second.first +
                                          " defined at line " +
                                          std::to_string(it->second.second));
            }
            else
            {
                typeDefs[st->name] = {"struct", st->line};
            }

            if(st->members)
            {
                for(auto* nestedEnum : st->members->enums)
                {
                    if(!nestedEnum)
                        continue;
                    if(reservedTypeNames.count(nestedEnum->name))
                    {
                        reportError(nestedEnum->line,
                                    "type name '" + nestedEnum->name +
                                        "' is a reserved keyword");
                    }
                    auto eit = typeDefs.find(nestedEnum->name);
                    if(eit != typeDefs.end())
                    {
                        reportError(nestedEnum->line,
                                    "type name '" + nestedEnum->name +
                                        "' conflicts with earlier " +
                                        eit->second.first +
                                        " defined at line " +
                                        std::to_string(eit->second.second));
                    }
                    else
                    {
                        typeDefs[nestedEnum->name] = {"enum", nestedEnum->line};
                    }
                }
            }
        }
    }

    if(!program->globalVars.empty())
    {
        for(auto* gv : program->globalVars)
        {
            if(!gv)
                continue;
            generateGlobalVarDeclaration(gv);
        }
    }

    if(program->enumList)
    {
        for(auto* en : program->enumList->enums)
        {
            if(!en)
                continue;
            if(reservedTypeNames.count(en->name))
            {
                reportError(en->line, "type name '" + en->name +
                                          "' is a reserved keyword");
            }
            auto it = typeDefs.find(en->name);
            if(it != typeDefs.end())
            {
                reportError(en->line, "type name '" + en->name +
                                          "' conflicts with earlier " +
                                          it->second.first +
                                          " defined at line " +
                                          std::to_string(it->second.second));
            }
            else
            {
                typeDefs[en->name] = {"enum", en->line};
            }
        }
    }

    if(program->functionList)
    {
        for(auto* fn : program->functionList->functions)
        {
            if(!fn)
                continue;
            auto it = typeDefs.find(fn->name);
            if(it != typeDefs.end())
            {
                reportError(fn->line, "function name '" + fn->name +
                                          "' conflicts with type '" +
                                          it->first + "'");
            }
        }
    }

    if(program->enumList)
    {
        for(auto enumDef : program->enumList->enums)
        {
            generateEnumDefinition(enumDef);
        }
    }
    if(program->structList)
    {
        for(auto* st : program->structList->structs)
        {
            if(!st || !st->members)
                continue;
            for(auto* nestedEnum : st->members->enums)
            {
                generateEnumDefinition(nestedEnum);
            }
        }
    }

    // First, collect generic struct templates and impl blocks
    // These are NOT generated immediately - they're instantiated on demand
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(structDef->isGeneric())
            {
                // Store as template for later instantiation
                genericStructTemplates[structDef->name] = structDef;
            }
        }
    }

    traitDefinitions.clear();
    structImplementedTraits.clear();
    for(auto* traitDef : program->traitDefs)
    {
        if(!traitDef || traitDef->name.empty())
            continue;
        traitDefinitions[traitDef->name] = traitDef;
    }

    auto typeNodesEquivalent = [&](TypeNode* lhs, TypeNode* rhs,
                                   const std::vector<std::string>& typeParams,
                                   const std::string& selfTypeName) -> bool
    {
        std::vector<std::string> substParams = typeParams;
        substParams.push_back("Self");
        std::vector<TypeNode*> substArgs;
        substArgs.reserve(typeParams.size() + 1);
        for(const auto& typeParam : typeParams)
            substArgs.push_back(new StructTypeRefNode(typeParam));
        substArgs.push_back(new StructTypeRefNode(selfTypeName));

        TypeNode* lhsResolved =
            substituteTypeParams(lhs, substParams, substArgs);
        TypeNode* rhsResolved =
            substituteTypeParams(rhs, substParams, substArgs);
        return Helpers::type_name_for_error(lhsResolved) ==
               Helpers::type_name_for_error(rhsResolved);
    };

    auto validateTraitImplBlock = [&](ImplBlockNode* impl)
    {
        if(!impl || impl->traitName.empty())
            return;

        auto traitIt = traitDefinitions.find(impl->traitName);
        if(traitIt == traitDefinitions.end() || !traitIt->second)
        {
            reportError(impl->line, "unknown trait '" + impl->traitName +
                                        "' in impl for '" + impl->structName +
                                        "'");
            return;
        }

        TraitDefNode* traitDef = traitIt->second;
        for(auto* traitMethod : traitDef->methods)
        {
            if(!traitMethod)
                continue;

            StructMethodNode* implMethod = nullptr;
            for(auto* candidate : impl->methods)
            {
                if(candidate && candidate->name == traitMethod->name)
                {
                    implMethod = candidate;
                    break;
                }
            }

            if(!implMethod)
            {
                // Trait method has a default body — synthesize a method on
                // the impl that delegates to it. We deep-copy the parameter
                // list so we can rebind `self: Self` to the concrete type
                // without mutating the trait definition. The body itself is
                // shared (read-only AST during codegen).
                if(traitMethod->body)
                {
                    auto* newParams = traitMethod->parameters
                                          ? new ParameterListNode()
                                          : nullptr;
                    if(newParams && traitMethod->parameters)
                    {
                        for(auto* p : traitMethod->parameters->parameters)
                        {
                            if(!p)
                            {
                                newParams->parameters.push_back(nullptr);
                                continue;
                            }
                            TypeNode* paramType = p->type;
                            if(p->name == "self")
                            {
                                if(auto* selfRef =
                                       dynamic_cast<StructTypeRefNode*>(
                                           p->type))
                                {
                                    if(selfRef->structName == "Self")
                                        paramType = new StructTypeRefNode(
                                            impl->structName);
                                }
                            }
                            auto* cloned =
                                new ParameterNode(paramType, p->name);
                            cloned->line = p->line;
                            newParams->parameters.push_back(cloned);
                        }
                    }
                    auto* defaulted = new StructMethodNode(
                        traitMethod->returnType, traitMethod->name, newParams,
                        traitMethod->body, traitMethod->isPublic,
                        traitMethod->isStatic);
                    // The defaulted method should resolve under the trait's
                    // module (same module-context rules as a normal trait
                    // method) so visibility behaves predictably.
                    defaulted->sourceModule = traitDef->sourceModule;
                    defaulted->line = traitMethod->line;
                    impl->methods.push_back(defaulted);
                    continue;
                }

                reportError(impl->line,
                            "trait '" + impl->traitName + "' for struct '" +
                                impl->structName + "' requires method '" +
                                traitMethod->name + "'");
                continue;
            }

            if(implMethod->isStatic != traitMethod->isStatic)
            {
                reportError(
                    implMethod->line,
                    "method '" + impl->structName + "::" + implMethod->name +
                        "' does not match trait '" + impl->traitName +
                        "': expected " +
                        std::string(traitMethod->isStatic ? "static"
                                                          : "instance") +
                        " method");
                continue;
            }

            size_t traitParamCount =
                traitMethod->parameters
                    ? traitMethod->parameters->parameters.size()
                    : 0;
            size_t implParamCount =
                implMethod->parameters
                    ? implMethod->parameters->parameters.size()
                    : 0;
            if(traitParamCount != implParamCount)
            {
                reportError(
                    implMethod->line,
                    "method '" + impl->structName + "::" + implMethod->name +
                        "' does not match trait '" + impl->traitName +
                        "': expected " + std::to_string(traitParamCount) +
                        " parameter(s), got " + std::to_string(implParamCount));
                continue;
            }

            bool mismatch = false;
            for(size_t i = 0; i < traitParamCount; ++i)
            {
                auto* expectedParam = traitMethod->parameters->parameters[i];
                auto* actualParam = implMethod->parameters->parameters[i];
                if(!expectedParam || !actualParam)
                    continue;
                if(expectedParam->name != actualParam->name)
                {
                    reportError(implMethod->line,
                                "method '" + impl->structName +
                                    "::" + implMethod->name +
                                    "' does not match trait '" +
                                    impl->traitName + "': parameter " +
                                    std::to_string(i + 1) + " must be named '" +
                                    expectedParam->name + "'");
                    mismatch = true;
                    break;
                }
                if(!typeNodesEquivalent(expectedParam->type, actualParam->type,
                                        impl->typeParams, impl->structName))
                {
                    reportError(implMethod->line,
                                "method '" + impl->structName +
                                    "::" + implMethod->name +
                                    "' does not match trait '" +
                                    impl->traitName + "': parameter '" +
                                    actualParam->name + "' has type '" +
                                    Helpers::type_name_for_error(actualParam->type) +
                                    "', expected '" +
                                    Helpers::type_name_for_error(expectedParam->type) +
                                    "'");
                    mismatch = true;
                    break;
                }
            }
            if(mismatch)
                continue;

            if(!typeNodesEquivalent(traitMethod->returnType,
                                    implMethod->returnType, impl->typeParams,
                                    impl->structName))
            {
                reportError(
                    implMethod->line,
                    "method '" + impl->structName + "::" + implMethod->name +
                        "' does not match trait '" + impl->traitName +
                        "': return type '" +
                        Helpers::type_name_for_error(implMethod->returnType) +
                        "' does not match expected '" +
                        Helpers::type_name_for_error(traitMethod->returnType) + "'");
            }
        }

        // Super-trait check: `trait Foo: Bar` requires every implementer of
        // Foo to also implement Bar. Generic impls (`impl<T> Foo for X`) are
        // skipped here because their concrete type isn't fixed yet.
        if(!traitDef->superTraits.empty() && impl->typeParams.empty())
        {
            auto& concreteImpls = structImplementedTraits[impl->structName];
            for(const auto& superTrait : traitDef->superTraits)
            {
                if(superTrait.empty())
                    continue;
                if(concreteImpls.find(superTrait) == concreteImpls.end())
                {
                    reportError(impl->line, "trait '" + impl->traitName +
                                                "' for struct '" +
                                                impl->structName +
                                                "' requires struct to also "
                                                "implement super-trait '" +
                                                superTrait + "'");
                }
            }
        }
    };

    // Repopulate structImplementedTraits after the cleanup above, so the
    // super-trait validation can see all concrete impls in the program.
    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl || impl->traitName.empty())
                continue;
            if(!impl->typeParams.empty())
                continue;
            structImplementedTraits[impl->structName].insert(impl->traitName);
        }
    }

    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
            validateTraitImplBlock(impl);
    }

    // Collect generic impl blocks
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(!impl->typeParams.empty())
            {
                // This is a generic impl block
                genericImplBlocks[impl->structName].push_back(impl);
            }
        }
    }

    // Generate all NON-GENERIC struct definitions
    // We need to process base structs before derived structs
    if(program->structList)
    {
        // Build a map of struct names to their definitions
        std::map<std::string, StructDefNode*> structMap;
        for(auto structDef : program->structList->structs)
        {
            structMap[structDef->name] = structDef;
        }

        // Process structs in dependency order (bases before derived)
        std::set<std::string> processed;
        std::function<void(StructDefNode*)> processStruct =
            [&](StructDefNode* structDef)
        {
            if(processed.count(structDef->name))
                return;

            // Skip generic structs - they're instantiated on demand
            if(structDef->isGeneric())
            {
                processed.insert(structDef->name);
                return;
            }

            // Process base first if it exists
            if(!structDef->baseName.empty())
            {
                auto baseIt = structMap.find(structDef->baseName);
                if(baseIt != structMap.end())
                {
                    processStruct(baseIt->second);
                }
            }

            std::function<void(TypeNode*)> processMemberType =
                [&](TypeNode* type)
            {
                if(!type)
                    return;
                if(auto* refType = dynamic_cast<ReferenceTypeNode*>(type))
                {
                    processMemberType(refType->elementType);
                    return;
                }
                if(auto* ptrType = dynamic_cast<PointerTypeNode*>(type))
                {
                    processMemberType(ptrType->elementType);
                    return;
                }
                if(auto* tupleType = dynamic_cast<TupleTypeNode*>(type))
                {
                    if(tupleType->elementTypes)
                    {
                        for(auto* elem : tupleType->elementTypes->types)
                            processMemberType(elem);
                    }
                    return;
                }
                if(auto* listType = dynamic_cast<GenericListTypeNode*>(type))
                {
                    processMemberType(listType->elementType);
                    return;
                }
                if(auto* mapType = dynamic_cast<MapTypeNode*>(type))
                {
                    processMemberType(mapType->keyType);
                    processMemberType(mapType->valueType);
                    return;
                }
                if(auto* genStructRef =
                       dynamic_cast<GenericStructTypeRefNode*>(type))
                {
                    for(auto* arg : genStructRef->typeArgs)
                        processMemberType(arg);
                    return;
                }
                if(auto* structRef = dynamic_cast<StructTypeRefNode*>(type))
                {
                    auto depIt = structMap.find(structRef->structName);
                    if(depIt != structMap.end() && depIt->second != structDef)
                        processStruct(depIt->second);
                }
            };

            if(structDef->members)
            {
                for(auto* member : structDef->members->members)
                {
                    if(member)
                        processMemberType(member->type);
                }
            }

            // Track struct visibility
            structVisibility[structDef->name] =
                std::make_pair(structDef->isPublic, structDef->sourceModule);
            generateStructDefinition(structDef);
            processed.insert(structDef->name);
        };

        for(auto structDef : program->structList->structs)
        {
            processStruct(structDef);
        }
    }

    // Generate forward declarations for all functions first
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(funcDef->isTest && !includeTests)
                continue;
            if(!funcDef->typeParams.empty() && funcDef->isCexpr)
            {
                registerFunctionOverload(funcDef, nullptr);
                continue;
            }
            llvm::Function* decl = generateFunctionDeclaration(funcDef);
            registerFunctionOverload(funcDef, decl);
        }
    }

    // Generate NON-GENERIC struct method declarations and track visibility
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric())
            {
                generateStructMethods(structDef);
            }
        }
    }

    // Process non-generic impl blocks (add methods to existing structs)
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                if(!impl->traitName.empty())
                {
                    structImplementedTraits[impl->structName].insert(
                        impl->traitName);
                }
                // Non-generic impl block - process immediately
                for(auto method : impl->methods)
                {
                    if(method && method->sourceModule.empty())
                        method->sourceModule = currentModule;
                    // Register the method with the struct
                    structMethods[impl->structName][method->name] =
                        std::make_pair(method->isPublic, method);

                    // Generate method declaration
                    generateMethodDeclaration(impl->structName, method);
                }
            }
        }
    }

    // Then generate all function bodies
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(funcDef->isTest && !includeTests)
                continue;
            if(!funcDef->typeParams.empty() && funcDef->isCexpr)
                continue;
            generateFunctionDefinition(funcDef);
        }
    }

    // Emit definitions for module functions that were loaded via `mod` and
    // referenced through fully-qualified calls (e.g. std::x::foo()) even when
    // they were not pulled in by `use`.
    if(!deferredModuleFunctionDefs.empty())
    {
        for(auto* fn : deferredModuleFunctionDefs)
        {
            if(!fn)
                continue;
            if(fn->isTest && !includeTests)
                continue;
            generateFunctionDefinition(fn);
        }
    }

    // Collect fixture-test methods from #[fixture] impl blocks. Each #[test]
    // method inside a #[fixture] impl gets a fresh stack-allocated, zero-
    // initialized instance via its &mut Self parameter.
    std::vector<FixtureTestEntry> fixtureTests;
    if(program->implList)
    {
        for(auto* impl : program->implList->impls)
        {
            if(!impl || !impl->isFixture)
                continue;
            if(!impl->traitName.empty())
            {
                reportError(impl->line,
                            "#[fixture] is only valid on inherent impl blocks");
                continue;
            }
            for(auto* method : impl->methods)
            {
                if(!method || !method->isTest)
                    continue;
                if(method->isStatic)
                {
                    reportError(method->line,
                                "fixture #[test] methods must take 'self: "
                                "&mut Self' (got static method)");
                    continue;
                }
                bool selfOnly = true;
                for(auto* p : method->parameters->parameters)
                {
                    if(p && p->name != "self")
                    {
                        selfOnly = false;
                        break;
                    }
                }
                if(!selfOnly)
                {
                    reportError(method->line,
                                "fixture #[test] methods must take only "
                                "'self: &mut Self' as parameter");
                    continue;
                }
                if(method->returnType &&
                   !(method->returnType->kind == TypeNode::TYPE_VOID ||
                     method->returnType->kind == TypeNode::TYPE_INT ||
                     method->returnType->kind == TypeNode::TYPE_I32))
                {
                    reportError(method->line,
                                "fixture #[test] methods must return void or "
                                "i32");
                    continue;
                }
                fixtureTests.push_back({impl, method});
            }
        }
    }

    if(testMode && (!testFunctions.empty() || !fixtureTests.empty()))
    {
        for(auto* testFn : testFunctions)
        {
            if(!testFn || !testFn->parameters)
                continue;
            if(!testFn->parameters->parameters.empty())
            {
                reportError(testFn->line,
                            "test functions must have no parameters");
                continue;
            }
            if(testFn->returnType &&
               !(testFn->returnType->kind == TypeNode::TYPE_VOID ||
                 testFn->returnType->kind == TypeNode::TYPE_INT ||
                 testFn->returnType->kind == TypeNode::TYPE_I32))
            {
                reportError(testFn->line,
                            "test functions must return void or i32");
                continue;
            }
        }
        if(benchmarkMode)
            generateBenchmarkMain(testFunctions);
        else
            generateTestMain(testFunctions, fixtureTests);
    }

    // Generate a C-compatible main wrapper if needed.
    if(!testMode && mainDef && mainArgMode != MainArgMode::None &&
       mainArgsListType)
    {
        llvm::Type* i32Type = llvm::Type::getInt32Ty(context);
        llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
        llvm::Type* argvPtrType = ptrType;
#else
        llvm::Type* i8Type = llvm::Type::getInt8Ty(context);
        llvm::Type* i8PtrType = llvm::PointerType::get(i8Type, 0);
        llvm::Type* argvPtrType = llvm::PointerType::get(i8PtrType, 0);
        llvm::Type* ptrType = llvm::PointerType::get(
            getLLVMTypeFromNode(mainArgsListType->elementType), 0);
#endif

        std::vector<llvm::Type*> wrapperParams = {i32Type, argvPtrType};
        llvm::FunctionType* wrapperType =
            llvm::FunctionType::get(i32Type, wrapperParams, false);
        llvm::Function* wrapper = llvm::Function::Create(
            wrapperType, llvm::Function::ExternalLinkage, "main", module.get());

        auto argIt = wrapper->arg_begin();
        llvm::Value* argcArg = &*argIt++;
        llvm::Value* argvArg = &*argIt++;
        argcArg->setName("argc");
        argvArg->setName("argv");

        llvm::BasicBlock* entry =
            llvm::BasicBlock::Create(context, "entry", wrapper);
        builder.SetInsertPoint(entry);

        llvm::Value* argc64 = builder.CreateSExt(argcArg, i64Type, "argc64");

        // Build list struct { size: i64, data: ptr }
        llvm::Type* listStructType = getLLVMTypeFromNode(mainArgsListType);
        llvm::Value* listStruct = llvm::UndefValue::get(listStructType);
        listStruct =
            builder.CreateInsertValue(listStruct, argc64, 0, "args.size");

#if LLVM_VERSION_MAJOR >= 15
        llvm::Value* dataPtr = argvArg;
#else
        llvm::Value* dataPtr = argvArg;
        if(argvArg->getType() != ptrType)
            dataPtr = builder.CreateBitCast(argvArg, ptrType, "args.data");
#endif

        listStruct =
            builder.CreateInsertValue(listStruct, dataPtr, 1, "args.data");

        llvm::Function* userMain =
            module->getFunction(functionSymbolName(mainDef));
        if(!userMain)
            userMain = module->getFunction("__mlang_user_main");
        if(!userMain)
        {
            reportError(mainDef->line,
                        "failed to generate main wrapper: missing user main");
            builder.CreateRet(llvm::ConstantInt::get(i32Type, 1));
        }
        else
        {
            std::vector<llvm::Value*> callArgs;
            if(mainArgMode == MainArgMode::ArgsListWithCount)
            {
                llvm::Value* argcValue = argcArg;
                if(mainArgcKind == TypeNode::TYPE_I64)
                    argcValue = argc64;
                callArgs.push_back(argcValue);
            }
            callArgs.push_back(listStruct);
            llvm::Value* rc = builder.CreateCall(userMain, callArgs, "mainrc");
            builder.CreateRet(rc);
        }
    }

    // Generate NON-GENERIC struct method bodies
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef->isGeneric() && structDef->members)
            {
                for(auto method : structDef->members->methods)
                {
                    generateMethodDefinition(structDef->name, method);
                }
            }
        }
    }

    // Generate non-generic impl block method bodies
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(impl->typeParams.empty())
            {
                for(auto method : impl->methods)
                {
                    generateMethodDefinition(impl->structName, method);
                }
            }
        }
    }
}
llvm::Function*
CodeGenerator::generateFunctionDeclaration(FunctionDefNode* node)
{
    std::string symbolName = functionSymbolName(node);
    std::vector<llvm::Type*> paramTypes;
    for(auto param : node->parameters->parameters)
    {
        llvm::Type* paramType = getLLVMTypeFromNode(param->type);
        if(!paramType)
        {
            reportError(param->line,
                        "unknown type: " + Helpers::type_name_for_error(param->type));
            paramType = llvm::Type::getInt32Ty(context); // fallback
        }
        paramTypes.push_back(paramType);
    }

    llvm::Type* returnType = getLLVMTypeFromNode(node->returnType);
    if(!returnType)
    {
        reportError(node->line,
                    "unknown type: " + Helpers::type_name_for_error(node->returnType));
        returnType = llvm::Type::getVoidTy(context); // fallback
    }

    // Check if function already declared
    if(llvm::Function* existing = module->getFunction(symbolName))
    {
        if(existing->arg_size() != paramTypes.size() ||
           existing->getReturnType() != returnType ||
           existing->isVarArg() != node->parameters->isVarArg)
        {
            reportError(node->line, "conflicting declaration for function '" +
                                        node->name + "'");
        }
        return existing;
    }

    llvm::FunctionType* funcType = llvm::FunctionType::get(
        returnType, paramTypes, node->parameters->isVarArg);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, symbolName, module.get());

    // Set parameter names
    unsigned idx = 0;
    for(auto& arg : function->args())
    {
        arg.setName(node->parameters->parameters[idx++]->name);
    }

    return function;
}

void CodeGenerator::seedFunctionScopeWithGlobals()
{
    for(const auto& it : globalNamedValues)
        namedValues[it.first] = it.second;
    for(const auto& it : globalVariableTypes)
        variableTypes[it.first] = it.second;
    for(const auto& it : globalStructVariableTypes)
        structVariableTypes[it.first] = it.second;
    for(const auto& n : globalConstantVariables)
        constantVariables.insert(n);
}

void CodeGenerator::generateGlobalVarDeclaration(VarDeclNode* node)
{
    if(!node)
        return;
    if(globalNamedValues.find(node->name) != globalNamedValues.end())
    {
        reportError(node->line,
                    "duplicate global variable: '" + node->name + "'");
        return;
    }

    TypeNode::TypeKind kind = TypeNode::TYPE_INT;
    if(node->type)
        kind = node->type->kind;
    else if(node->initExpr)
    {
        if(dynamic_cast<BoolLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_BOOL;
        else if(dynamic_cast<FloatLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_FLOAT;
        else if(dynamic_cast<DoubleLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_DOUBLE;
        else if(dynamic_cast<StringLiteralNode*>(node->initExpr))
            kind = TypeNode::TYPE_STRING;
    }

    llvm::Type* llvmTy =
        node->type ? getLLVMTypeFromNode(node->type) : getLLVMType(kind);
    if(!llvmTy)
    {
        reportError(node->line,
                    "invalid global variable type for '" + node->name + "'");
        return;
    }

    llvm::Constant* init = llvm::Constant::getNullValue(llvmTy);
    if(node->initExpr)
    {
        if(auto* i = dynamic_cast<IntLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isIntegerTy())
            {
                reportError(node->line,
                            "global integer initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantInt::get(llvmTy, i->value, true);
        }
        else if(auto* b = dynamic_cast<BoolLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isIntegerTy(1))
            {
                reportError(node->line,
                            "global bool initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantInt::get(llvmTy, b->value ? 1 : 0, false);
        }
        else if(auto* f = dynamic_cast<FloatLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isFloatTy())
            {
                reportError(node->line,
                            "global float initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantFP::get(llvmTy, f->value);
        }
        else if(auto* d = dynamic_cast<DoubleLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isDoubleTy())
            {
                reportError(node->line,
                            "global double initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
            init = llvm::ConstantFP::get(llvmTy, d->value);
        }
        else if(auto* s = dynamic_cast<StringLiteralNode*>(node->initExpr))
        {
            if(!llvmTy->isPointerTy())
            {
                reportError(node->line,
                            "global string initializer type mismatch for '" +
                                node->name + "'");
                return;
            }
#if LLVM_VERSION_MAJOR >= 21
            auto* gstr =
                builder.CreateGlobalString(s->value, node->name + ".gstr");
            init = llvm::dyn_cast<llvm::Constant>(gstr);
#else
            auto* gstr =
                builder.CreateGlobalStringPtr(s->value, node->name + ".gstr");
            init = llvm::dyn_cast<llvm::Constant>(gstr);
#endif
            if(!init)
                init = llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(llvmTy));
        }
        else
        {
            reportError(node->line,
                        "global initializer must be a literal constant");
            return;
        }
    }

    auto* gv = new llvm::GlobalVariable(*module, llvmTy, false,
                                        llvm::GlobalValue::InternalLinkage,
                                        init, "__mlang_global_" + node->name);
    globalNamedValues[node->name] = gv;
    globalVariableTypes[node->name] = kind;
}

llvm::Function* CodeGenerator::generateFunctionDefinition(FunctionDefNode* node)
{
    std::string symbolName = functionSymbolName(node);
    // Get the function (should already be declared)
    llvm::Function* function = module->getFunction(symbolName);
    if(!function)
    {
        // If not declared yet, declare it now
        function = generateFunctionDeclaration(node);
    }

    if(node->isExtern || !node->body)
    {
        return function;
    }

    // Check if function already has a body (was already defined)
    if(!function->empty())
    {
        // Function already defined, skip
        return function;
    }

    // Apply inline attributes
    if(node->isInlineAlways)
        function->addFnAttr(llvm::Attribute::AlwaysInline);
    else if(node->isInlineNever)
        function->addFnAttr(llvm::Attribute::NoInline);
    else if(node->isInline)
        function->addFnAttr(llvm::Attribute::InlineHint);

    if(contains_exception_control_flow(node->body))
    {
        function->addFnAttr(llvm::Attribute::OptimizeNone);
        function->addFnAttr(llvm::Attribute::NoInline);
    }

    // Track which module this function is from (for visibility checks)
    std::string savedModule = currentModule;
    currentModule = node->sourceModule;
    auto savedIP = builder.saveIP();
    auto savedNamedValues = namedValues;
    auto savedConstantVariables = constantVariables;
    auto savedConstexprValues = constexprValues;
    auto savedMovedVariables = movedVariables;
    auto savedPointerBorrowTarget = pointerBorrowTarget;
    auto savedActiveBorrowers = activeBorrowers;
    auto savedActiveMutBorrower = activeMutBorrower;
    auto savedVariableScopeDepth = variableScopeDepth;
    auto savedVariableTypes = variableTypes;
    auto savedStructVariableTypes = structVariableTypes;
    auto savedTraitObjectVariableTypes = traitObjectVariableTypes;
    auto savedEnumVariableTypes = enumVariableTypes;
    auto savedListElementTypes = listElementTypes;
    auto savedMapKeyValueTypes = mapKeyValueTypes;
    auto savedTupleElementTypes = tupleElementTypes;
    auto savedPointerElementTypes = pointerElementTypes;
    auto savedPointerKnownNull = pointerKnownNull;
    auto savedCleanupScopes = cleanupScopes;
    auto savedPointerBorrowScopes = pointerBorrowScopes;
    auto savedVariableScopeDepthScopes = variableScopeDepthScopes;
    auto savedClosureVariables = closureVariables;
    auto savedActiveInlineClosures = activeInlineClosures;
    auto savedCurrentFunctionExceptionFrame = currentFunctionExceptionFrame;
    auto savedSemanticReturnType = currentSemanticReturnType;
    int savedUnsafeDepth = unsafeDepth;
    currentSemanticReturnType = node->returnType;

    // Create a new basic block for the function
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(bb);

    // Clear the named values map and constant tracking for new function scope
    namedValues.clear();
    constantVariables.clear();
    movedVariables.clear();
    closureVariables.clear();
    activeInlineClosures.clear();
    pointerBorrowTarget.clear();
    pointerKnownNull.clear();
    activeBorrowers.clear();
    activeMutBorrower.clear();
    variableScopeDepth.clear();
    variableTypes.clear();
    structVariableTypes.clear();
    enumVariableTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    unsafeDepth = 0;
    cleanupScopes.clear();
    pointerBorrowScopes.clear();
    variableScopeDepthScopes.clear();
    currentFunctionExceptionFrame = nullptr;
    seedFunctionScopeWithGlobals();
    enterCleanupScope();

    initializeStdlibFunctions();
    llvm::BasicBlock* functionBodyBB =
        llvm::BasicBlock::Create(context, "fn.body", function);
    llvm::BasicBlock* functionExceptionBB =
        llvm::BasicBlock::Create(context, "fn.exc", function);
    currentFunctionExceptionFrame =
        builder.CreateCall(exceptionsPushFrameFunc, {}, "fn.exc.frame");
    llvm::Value* functionExceptionEnv = builder.CreateCall(
        exceptionsFrameEnvFunc, {currentFunctionExceptionFrame}, "fn.exc.env");
    auto* functionSetjmpCall = builder.CreateCall(
        exceptionsSetjmpFunc, {functionExceptionEnv}, "fn.exc.state");
    functionSetjmpCall->setCanReturnTwice();
    llvm::Value* functionExceptionState = functionSetjmpCall;
    llvm::Value* enteredNormally = builder.CreateICmpEQ(
        functionExceptionState,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
        "fn.exc.ok");
    builder.CreateCondBr(enteredNormally, functionBodyBB, functionExceptionBB);
    builder.SetInsertPoint(functionBodyBB);

    // Set up parameters
    unsigned paramIdx = 0;
    for(auto& arg : function->args())
    {
        // Allocate space for parameters so they can be modified
        llvm::AllocaInst* alloca = builder.CreateAlloca(
            arg.getType(), nullptr, std::string(arg.getName()) + ".addr");
        llvm::Value* paramValue = &arg;
        if(arg.getType()->isStructTy())
            paramValue = applyStructCopySemantics(paramValue);
        builder.CreateStore(paramValue, alloca);
        namedValues[std::string(arg.getName())] = alloca;
        recordVariableScopeDepth(std::string(arg.getName()));

        // Track parameter types
        if(paramIdx < node->parameters->parameters.size())
        {
            auto* paramNode = node->parameters->parameters[paramIdx];
            if(auto* refType =
                   dynamic_cast<ReferenceTypeNode*>(paramNode->type))
            {
                // Store inner element kind so the borrow checker treats the
                // param as the inner type (e.g. &str8 -> TYPE_STRING).
                variableTypes[std::string(arg.getName())] =
                    refType->elementType->kind;
                // Preserve container element typing for borrowed params so
                // indexing and methods work on &list<T> / &map<K,V>.
                if(auto* genListInner =
                       dynamic_cast<GenericListTypeNode*>(refType->elementType))
                {
                    listElementTypes[std::string(arg.getName())] =
                        genListInner->elementType;
                }
                if(auto* mapInner =
                       dynamic_cast<MapTypeNode*>(refType->elementType))
                {
                    mapKeyValueTypes[std::string(arg.getName())] =
                        std::make_pair(mapInner->keyType, mapInner->valueType);
                }
                if(auto* structInner =
                       dynamic_cast<StructTypeRefNode*>(refType->elementType))
                {
                    std::string resolvedEnumName =
                        resolveVisibleEnumName(structInner->structName);
                    if(!resolvedEnumName.empty())
                    {
                        variableTypes[std::string(arg.getName())] =
                            TypeNode::TYPE_INT;
                        enumVariableTypes[std::string(arg.getName())] =
                            resolvedEnumName;
                    }
                    else
                    {
                        structVariableTypes[std::string(arg.getName())] =
                            structInner->structName;
                    }
                }
                if(auto* genStructInner =
                       dynamic_cast<GenericStructTypeRefNode*>(
                           refType->elementType))
                {
                    std::string mangled = getOrCreateMonomorphizedStruct(
                        genStructInner->structName, genStructInner->typeArgs);
                    structVariableTypes[std::string(arg.getName())] = mangled;
                }
                // Immutable reference: param may not be mutated inside body.
                if(!refType->isMutable)
                    constantVariables.insert(std::string(arg.getName()));
                // Mutable reference: leave out of constantVariables.
            }
            else
            {
                variableTypes[std::string(arg.getName())] =
                    paramNode->type->kind;
            }
            if(auto* structType =
                   dynamic_cast<StructTypeRefNode*>(paramNode->type))
            {
                std::string resolvedEnumName =
                    resolveVisibleEnumName(structType->structName);
                if(!resolvedEnumName.empty())
                {
                    variableTypes[std::string(arg.getName())] =
                        TypeNode::TYPE_INT;
                    enumVariableTypes[std::string(arg.getName())] =
                        resolvedEnumName;
                }
                else
                {
                    structVariableTypes[std::string(arg.getName())] =
                        structType->structName;
                }
            }
            if(auto* genStructType =
                   dynamic_cast<GenericStructTypeRefNode*>(paramNode->type))
            {
                std::string mangled = getOrCreateMonomorphizedStruct(
                    genStructType->structName, genStructType->typeArgs);
                structVariableTypes[std::string(arg.getName())] = mangled;
            }
            if(auto* genListType =
                   dynamic_cast<GenericListTypeNode*>(paramNode->type))
            {
                listElementTypes[std::string(arg.getName())] =
                    genListType->elementType;
            }
            if(auto* mapType = dynamic_cast<MapTypeNode*>(paramNode->type))
            {
                mapKeyValueTypes[std::string(arg.getName())] =
                    std::make_pair(mapType->keyType, mapType->valueType);
            }
            if(auto* ptrType = dynamic_cast<PointerTypeNode*>(paramNode->type))
            {
                pointerElementTypes[std::string(arg.getName())] =
                    ptrType->elementType;
            }
            if(auto* traitObjType =
                   dynamic_cast<TraitObjectTypeNode*>(paramNode->type))
            {
                variableTypes[std::string(arg.getName())] =
                    TypeNode::TYPE_TRAIT_OBJECT;
                traitObjectVariableTypes[std::string(arg.getName())] =
                    traitObjType->traitName;
            }
        }
        paramIdx++;
    }

    // Generate the function body
    if(node->body)
    {
        const auto& bodyStmts = node->body->statements;
        for(size_t si = 0; si < bodyStmts.size(); si++)
        {
            generateStatement(bodyStmts[si]);
            // NLL: expire borrow variables not referenced in remaining stmts
            if(!pointerBorrowTarget.empty())
            {
                std::set<std::string> futureIdents;
                for(size_t sj = si + 1; sj < bodyStmts.size(); sj++)
                    collect_used_idents(bodyStmts[sj], futureIdents);
                std::vector<std::string> toClear;
                for(const auto& kv : pointerBorrowTarget)
                {
                    if(futureIdents.count(kv.first))
                        continue;
                    // Don't NLL-expire exclusive struct borrows (ptr<T> where T
                    // is a struct). They remain active until scope exit or
                    // explicit reassignment so a second borrow of the same
                    // owner is rejected.
                    auto peit = pointerElementTypes.find(kv.first);
                    if(peit != pointerElementTypes.end() && peit->second &&
                       peit->second->kind == TypeNode::TYPE_STRUCT)
                        continue;
                    toClear.push_back(kv.first);
                }
                for(const auto& ptr : toClear)
                    clearPointerBorrow(ptr);
            }
        }
    }

    auto exceptionNamedValues = namedValues;
    auto exceptionStructVariableTypes = structVariableTypes;
    auto exceptionCleanupScopes = cleanupScopes;
    auto exceptionMovedVariables = movedVariables;

    // Run scope-exit destructors for locals at normal function fallthrough.
    exitCleanupScope();

    // If the function is void and doesn't have a return, add one
    llvm::Type* returnType = function->getReturnType();
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();
    if(!currentBlock->getTerminator())
    {
        if(currentFunctionExceptionFrame)
            builder.CreateCall(exceptionsPopFrameFunc,
                               {currentFunctionExceptionFrame});
        if(returnType->isVoidTy())
        {
            builder.CreateRetVoid();
        }
        else
        {
            if(node->name == "main" || node->name == "__mlang_user_main")
            {
                // Default main return to 0 when no explicit return is present.
                builder.CreateRet(llvm::ConstantInt::get(returnType, 0, true));
            }
            else
            {
                // For non-void functions without a return, add unreachable
                // This indicates a bug in the source code but prevents LLVM
                // crashes
                builder.CreateUnreachable();
            }
        }
    }

    builder.SetInsertPoint(functionExceptionBB);
    if(currentFunctionExceptionFrame)
        builder.CreateCall(exceptionsPopFrameFunc,
                           {currentFunctionExceptionFrame});
    namedValues = exceptionNamedValues;
    structVariableTypes = exceptionStructVariableTypes;
    cleanupScopes = exceptionCleanupScopes;
    movedVariables = exceptionMovedVariables;
    emitAllActiveCleanups();
    builder.CreateCall(exceptionsRethrowFunc, {});
    builder.CreateUnreachable();

    namedValues = std::move(savedNamedValues);
    constantVariables = std::move(savedConstantVariables);
    movedVariables = std::move(savedMovedVariables);
    pointerBorrowTarget = std::move(savedPointerBorrowTarget);
    activeBorrowers = std::move(savedActiveBorrowers);
    activeMutBorrower = std::move(savedActiveMutBorrower);
    variableScopeDepth = std::move(savedVariableScopeDepth);
    variableTypes = std::move(savedVariableTypes);
    structVariableTypes = std::move(savedStructVariableTypes);
    traitObjectVariableTypes = std::move(savedTraitObjectVariableTypes);
    enumVariableTypes = std::move(savedEnumVariableTypes);
    listElementTypes = std::move(savedListElementTypes);
    mapKeyValueTypes = std::move(savedMapKeyValueTypes);
    tupleElementTypes = std::move(savedTupleElementTypes);
    pointerElementTypes = std::move(savedPointerElementTypes);
    pointerKnownNull = std::move(savedPointerKnownNull);
    cleanupScopes = std::move(savedCleanupScopes);
    pointerBorrowScopes = std::move(savedPointerBorrowScopes);
    variableScopeDepthScopes = std::move(savedVariableScopeDepthScopes);
    closureVariables = std::move(savedClosureVariables);
    activeInlineClosures = std::move(savedActiveInlineClosures);
    currentFunctionExceptionFrame = savedCurrentFunctionExceptionFrame;
    currentSemanticReturnType = savedSemanticReturnType;
    unsafeDepth = savedUnsafeDepth;
    currentModule = savedModule;
    constexprValues = std::move(savedConstexprValues);
    builder.restoreIP(savedIP);

    // Verify the function
    llvm::verifyFunction(*function);
    return function;
}

