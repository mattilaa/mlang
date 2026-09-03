#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/backend_utils.h"
#include "ir/common.h"
#include "ir/return_inference.h"
#include "llvm_compat.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <llvm/Config/llvm-config.h>
#include <unordered_map>
#include <unordered_set>

using mlang::ir_detail::ast_analysis::contains_update_expression;
using mlang::ir_detail::module_target_triple_string;
using mlang::ir_detail::normalize_target_arch_name;
using mlang::ir_detail::common::Helpers;
using mlang::ir_detail::return_inference::infer_function_return_type;

namespace
{
void trace_generation_phase(const std::string& phase)
{
    if(std::getenv("MLANG_TRACE_PHASES") != nullptr)
        std::cerr << "mlang generation: " << phase << std::endl;
}
} // namespace

void CodeGenerator::generateCode(ProgramNode* program)
{
    trace_generation_phase("initialize");
    globalNamedValues.clear();
    globalConstantVariables.clear();
    globalVariableTypes.clear();
    globalStructVariableTypes.clear();
    arrayCapacities.clear();
    arrayKnownLengths.clear();
    constexprValues.clear();
    deferredModuleFunctionDefs.clear();

    for(auto* moduleAsm : program->moduleAsms)
    {
        if(!moduleAsm)
            continue;

        const std::string requiredArch =
            normalize_target_arch_name(moduleAsm->requiredArch);
        if(requiredArch.empty())
        {
            reportError(moduleAsm->line,
                        "unsupported module asm target arch '" +
                            moduleAsm->requiredArch +
                            "'; expected x86, x64, or aarch64");
            continue;
        }

        std::string effectiveTriple = module_target_triple_string(module.get());
        if(effectiveTriple.empty())
            effectiveTriple = llvm::sys::getDefaultTargetTriple();
        llvm::Triple triple(effectiveTriple);
        const std::string actualArch =
            normalize_target_arch_name(triple.getArchName().str());
        if(actualArch != requiredArch)
        {
            reportError(moduleAsm->line,
                        "module asm target arch '" + requiredArch +
                            "' does not match compilation target arch '" +
                            (actualArch.empty() ? triple.getArchName().str()
                                                : actualArch) +
                            "'");
            continue;
        }

        module->appendModuleInlineAsm(moduleAsm->asmTemplate);
    }

    trace_generation_phase("builtins");
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

    trace_generation_phase("type-aliases");
    resolveTypeAliasesInProgram(program);

    trace_generation_phase("constexpr-declarations");
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

    trace_generation_phase("return-inference");
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

    trace_generation_phase("type-validation");
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

    trace_generation_phase("enum-definitions");
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
            if(!structDef)
                continue;
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
        if(typeParams.empty())
        {
            substArgs.push_back(new StructTypeRefNode(selfTypeName));
        }
        else
        {
            auto* selfType = new GenericStructTypeRefNode(selfTypeName);
            for(const auto& typeParam : typeParams)
                selfType->typeArgs.push_back(new StructTypeRefNode(typeParam));
            substArgs.push_back(selfType);
        }

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
    trace_generation_phase("trait-impl-validation");
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
            if(!impl)
                continue;
            if(!impl->typeParams.empty())
            {
                // This is a generic impl block
                genericImplBlocks[impl->structName].push_back(impl);
            }
        }
    }

    // Generate all NON-GENERIC struct definitions
    // We need to process base structs before derived structs
    trace_generation_phase("struct-definitions");
    if(program->structList)
    {
        // Build a map of struct names to their definitions
        std::map<std::string, StructDefNode*> structMap;
        for(auto structDef : program->structList->structs)
        {
            if(!structDef)
                continue;
            structMap[structDef->name] = structDef;
            if(!structDef->isGeneric() && !getStructType(structDef->name))
            {
                // Create all named types before resolving member layouts.
                // This permits pointers/references to structs declared later
                // without attempting to materialize either body recursively.
                structTypes[structDef->name] =
                    llvm::StructType::create(context, structDef->name);
            }
        }

        // Process structs in dependency order (bases before derived)
        std::set<std::string> processed;
        std::set<std::string> processing;
        std::function<void(StructDefNode*)> processStruct =
            [&](StructDefNode* structDef)
        {
            if(!structDef)
                return;
            if(processed.count(structDef->name))
                return;

            // A pair of mutually-referential structs is legal when the
            // relationship is indirect (for example through a pointer), but
            // the dependency walk used to recurse forever before either
            // struct was marked as processed. Apart from overflowing the
            // stack, that failure presents as a jump into a stack address on
            // LLVM's signal trace. Named LLVM structs can be forward
            // referenced, so stop the ordering walk at the cycle and let the
            // outer invocation emit the definition.
            if(!processing.insert(structDef->name).second)
                return;

            trace_generation_phase("struct-definition:" + structDef->name);

            // Skip generic structs - they're instantiated on demand
            if(structDef->isGeneric())
            {
                processing.erase(structDef->name);
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
                if(dynamic_cast<ReferenceTypeNode*>(type))
                {
                    // References are indirect and do not require the pointee
                    // body to be complete.
                    return;
                }
                if(dynamic_cast<PointerTypeNode*>(type))
                {
                    // Pointers are indirect and do not require the pointee
                    // body to be complete.
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
                    if(depIt == structMap.end())
                    {
                        size_t scopePos = structRef->structName.rfind("::");
                        if(scopePos != std::string::npos)
                        {
                            depIt = structMap.find(
                                structRef->structName.substr(scopePos + 2));
                        }
                    }
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
            processing.erase(structDef->name);
            processed.insert(structDef->name);
        };

        for(auto structDef : program->structList->structs)
        {
            processStruct(structDef);
        }
    }

    // Generate forward declarations for all functions first
    trace_generation_phase("function-declarations");
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(!funcDef)
                continue;
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
    trace_generation_phase("struct-method-declarations");
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef)
                continue;
            if(!structDef->isGeneric())
            {
                generateStructMethods(structDef);
            }
        }
    }

    // Process non-generic impl blocks (add methods to existing structs)
    trace_generation_phase("impl-method-declarations");
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(!impl)
                continue;
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
                    if(!method)
                        continue;
                    if(method->sourceModule.empty())
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
    trace_generation_phase("function-bodies");
    if(program->functionList)
    {
        for(auto funcDef : program->functionList->functions)
        {
            if(!funcDef)
                continue;
            if(funcDef->isTest && !includeTests)
                continue;
            if(!funcDef->typeParams.empty() && funcDef->isCexpr)
                continue;
            trace_generation_phase("function-body:" + funcDef->name);
            generateFunctionDefinition(funcDef);
        }
    }

    // Emit definitions for module functions that were loaded via `mod` and
    // referenced through fully-qualified calls (e.g. std::x::foo()) even when
    // they were not pulled in by `use`. Generating one definition may discover
    // and append more deferred definitions, so treat the vector as a work queue.
    // A range-for iterator is invalidated when push_back reallocates the vector
    // and caused Linux bootstrap builds of mlangd-mla to segfault here.
    trace_generation_phase("deferred-function-bodies");
    if(!deferredModuleFunctionDefs.empty())
    {
        for(std::size_t i = 0; i < deferredModuleFunctionDefs.size(); ++i)
        {
            auto* fn = deferredModuleFunctionDefs[i];
            if(!fn)
                continue;
            if(fn->isTest && !includeTests)
                continue;
            trace_generation_phase("deferred-function-body:" + fn->name);
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
    trace_generation_phase("struct-method-bodies");
    if(program->structList)
    {
        for(auto structDef : program->structList->structs)
        {
            if(!structDef)
                continue;
            if(!structDef->isGeneric() && structDef->members)
            {
                for(auto method : structDef->members->methods)
                {
                    if(!method)
                        continue;
                    trace_generation_phase("struct-method-body:" +
                                           structDef->name + "::" +
                                           method->name);
                    generateMethodDefinition(structDef->name, method);
                }
            }
        }
    }

    // Generate non-generic impl block method bodies
    trace_generation_phase("impl-method-bodies");
    if(program->implList)
    {
        for(auto impl : program->implList->impls)
        {
            if(!impl)
                continue;
            if(impl->typeParams.empty())
            {
                for(auto method : impl->methods)
                {
                    if(!method)
                        continue;
                    trace_generation_phase("impl-method-body:" +
                                           impl->structName + "::" +
                                           method->name);
                    generateMethodDefinition(impl->structName, method);
                }
            }
        }
    }
    trace_generation_phase("complete");
}
