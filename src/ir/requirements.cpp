#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/common.h"
#include "module.h"
#include <algorithm>
#include <functional>

using mlang::ir_detail::common::Helpers;

// Every expression is checked in a disposable module. Reusing the ordinary
// expression checker keeps operator, overload, visibility and borrow rules in
// sync without mutating the caller's locals, overloads, or emitted functions.
bool CodeGenerator::probeRequirement(
    ExpressionNode* expression,
    ParameterListNode* parameters,
    ConstexprEnv* env,
    TypeNode*& result,
    bool& noThrow,
    TypeNode* requiredType)
{
    if(structuralProbeDepth >= 64)
        return false;
    auto scratch =
        std::make_unique<llvm::Module>("requires-expression", context);
    scratch->setDataLayout(module->getDataLayout());
    scratch->setTargetTriple(module->getTargetTriple());
    llvm::IRBuilder<> scratchBuilder(context);
    CodeGenerator probe(context, scratchBuilder, scratch);
    probe.structuralRequirementProbe = true;
    probe.structuralProbeDepth = structuralProbeDepth + 1;
    probe.structTypes = structTypes;
    probe.structMembers = structMembers;
    probe.structFieldLayouts = structFieldLayouts;
    probe.structFieldAccessInfo = structFieldAccessInfo;
    probe.structMemberDefaults = structMemberDefaults;
    probe.structBases = structBases;
    probe.structVisibility = structVisibility;
    probe.structMethods = structMethods;
    probe.traitDefinitions = traitDefinitions;
    probe.structImplementedTraits = structImplementedTraits;
    probe.traitObjectTypes = traitObjectTypes;
    probe.traitVTableTypes = traitVTableTypes;
    probe.enumValues = enumValues;
    probe.enumStringValues = enumStringValues;
    probe.enumVariantOrder = enumVariantOrder;
    probe.enumStringVariantOrder = enumStringVariantOrder;
    probe.enumBaseTypes = enumBaseTypes;
    probe.typeAliases = typeAliases;
    probe.genericStructTemplates = genericStructTemplates;
    probe.genericImplBlocks = genericImplBlocks;
    probe.monomorphizedTypes = monomorphizedTypes;
    probe.mangledToGenericName = mangledToGenericName;
    probe.monomorphizedTypeArgs = monomorphizedTypeArgs;
    probe.functionVisibility = functionVisibility;
    probe.variadicGenericFunctionTemplates = variadicGenericFunctionTemplates;
    probe.conceptDefinitions = conceptDefinitions;
    probe.conceptKnownTypes = conceptKnownTypes;
    probe.conceptEvaluationStack = conceptEvaluationStack;
    probe.conceptProgram = conceptProgram;
    probe.moduleLoader = moduleLoader;
    probe.currentModule = currentModule;
    probe.constexprValues = constexprValues;
    if(env)
        for(const auto& binding : *env)
            probe.constexprValues[binding.first] = binding.second;
    std::vector<std::string> names;
    std::vector<TypeNode*> arguments;
    std::map<std::string, TypeNode*> bindings = activeFunctionTypeBindings;
    for(const auto& binding : activeTypeParamBindings)
        bindings[binding.first] = binding.second;
    if(env)
        for(const auto& binding : *env)
            if(binding.first.compare(0, 7, "__type:") == 0 &&
               binding.second.kind == ConstexprValue::Kind::Type)
                bindings[binding.first.substr(7)] =
                    Helpers::type_from_text(binding.second.typeName);
    for(const auto& binding : bindings)
    {
        names.push_back(binding.first);
        arguments.push_back(binding.second);
    }
    probe.activeFunctionTypeBindings = bindings;
    // Global compile-time queries can run before the main generator has built
    // struct layouts and function declarations. Populate that semantic context
    // in the scratch module as well.
    if(conceptProgram)
    {
        if(conceptProgram->enumList)
            for(auto* definition : conceptProgram->enumList->enums)
                if(!probe.enumValues.count(definition->name) &&
                   !probe.enumStringValues.count(definition->name))
                    probe.generateEnumDefinition(definition);
        if(conceptProgram->structList)
            for(auto* definition : conceptProgram->structList->structs)
                if(definition->isGeneric())
                    probe.genericStructTemplates[definition->name] = definition;
        if(conceptProgram->implList)
            for(auto* impl : conceptProgram->implList->impls)
                if(!impl->typeParams.empty())
                {
                    auto& blocks = probe.genericImplBlocks[impl->structName];
                    if(std::find(blocks.begin(), blocks.end(), impl) ==
                       blocks.end())
                        blocks.push_back(impl);
                }
        if(conceptProgram->structList)
            for(auto* definition : conceptProgram->structList->structs)
                if(!definition->isGeneric() &&
                   !probe.structTypes.count(definition->name))
                {
                    probe.structVisibility[definition->name] =
                        {definition->isPublic, definition->sourceModule};
                    probe.generateStructDefinition(definition);
                    if(probe.hasError)
                        return false;
                    probe.generateStructMethods(definition);
                }
    }
    if(conceptProgram && conceptProgram->implList)
        for(auto* impl : conceptProgram->implList->impls)
            if(impl->typeParams.empty())
                for(auto* method : impl->methods)
                    if(method && !probe.structMethods[impl->structName].count(
                                     method->name))
                    {
                        probe.structMethods[impl->structName][method->name] =
                            {method->isPublic, method};
                        probe.generateMethodDeclaration(
                            impl->structName,
                            method);
                    }
    // Preserve symbols' signatures, never their bodies or module ownership.
    for(auto& function : *module)
    {
        auto* declaration = scratch->getFunction(function.getName());
        if(!declaration)
            declaration = llvm::Function::Create(
                function.getFunctionType(),
                llvm::Function::ExternalLinkage,
                function.getName(),
                scratch.get());
        declaration->setAttributes(function.getAttributes());
    }
    for(auto& global : module->globals())
    {
        auto* declaration = new llvm::GlobalVariable(
            *scratch,
            global.getValueType(),
            global.isConstant(),
            llvm::GlobalValue::ExternalLinkage,
            nullptr,
            global.getName());
        for(const auto& binding : globalNamedValues)
            if(binding.second == &global)
                probe.globalNamedValues[binding.first] = declaration;
    }
    probe.globalVariableTypes = globalVariableTypes;
    probe.globalStructVariableTypes = globalStructVariableTypes;
    probe.globalConstantVariables = globalConstantVariables;
    probe.functionOverloads = functionOverloads;
    for(auto& overloads : probe.functionOverloads)
        for(auto& overload : overloads.second)
            if(overload.function)
                overload.function =
                    scratch->getFunction(overload.function->getName());
    if(conceptProgram && conceptProgram->functionList)
        for(auto* function : conceptProgram->functionList->functions)
            if(function && !probe.functionOverloads.count(function->name))
            {
                // Register all overloads of this name below, including generic
                // signatures whose bodies are deliberately left uninstantiated.
                for(auto* candidate : conceptProgram->functionList->functions)
                    if(candidate && candidate->name == function->name)
                    {
                        auto* declaration =
                            candidate->typeParams.empty()
                                ? probe.generateFunctionDeclaration(candidate)
                                : nullptr;
                        probe.registerFunctionOverload(candidate, declaration);
                    }
            }
    if(moduleLoader && !currentModule.empty())
        if(auto* definition = moduleLoader->getModule(currentModule))
            if(definition->functionList)
                for(auto* function : definition->functionList->functions)
                {
                    auto& overloads = probe.functionOverloads[function->name];
                    const bool present = std::any_of(
                        overloads.begin(),
                        overloads.end(),
                        [&](const FunctionOverloadInfo& info)
                        { return info.node == function; });
                    if(!present)
                    {
                        auto* declaration =
                            function->typeParams.empty()
                                ? probe.generateFunctionDeclaration(function)
                                : nullptr;
                        probe.registerFunctionOverload(function, declaration);
                    }
                }
    auto* concreteParameters = new ParameterListNode();
    std::set<std::string> localNames;
    if(parameters)
        for(auto* parameter : parameters->parameters)
        {
            if(parameter->isPack || !localNames.insert(parameter->name).second)
                return false;
            TypeNode* type =
                substituteTypeParams(parameter->type, names, arguments);
            std::vector<std::string> aliases;
            type = probe.resolveTypeAliasNode(type, {}, aliases);
            if(!probe.isKnownRequirementType(type) ||
               type->kind == TypeNode::TYPE_VOID)
                return false;
            probe.constexprValues.erase(parameter->name);
            concreteParameters->parameters.push_back(
                new ParameterNode(type, parameter->name));
        }
    if(requiredType)
    {
        TypeNode* type = substituteTypeParams(requiredType, names, arguments);
        std::vector<std::string> aliases;
        type = probe.resolveTypeAliasNode(type, {}, aliases);
        if(!probe.isKnownRequirementType(type))
            return false;
        // Lowering checks generic arity, bounds, and type visibility.
        bool valid = type->kind == TypeNode::TYPE_VOID ||
                     probe.getLLVMTypeFromNode(type);
        return valid && !probe.hasError;
    }
    // Local parameters of enclosing requires-expressions have types, but no
    // values for evaluating nested predicates.
    if(env)
        for(const auto& binding : *env)
            if(binding.first.compare(0, 17, "__requires_local:") == 0 &&
               localNames.insert(binding.first.substr(17)).second)
                concreteParameters->parameters.push_back(new ParameterNode(
                    Helpers::type_from_text(binding.second.typeName),
                    binding.first.substr(17)));
    // A requires-expression may also refer to locals in its lexical scope.
    std::set<std::string> referenced;
    mlang::ir_detail::ast_analysis::collect_used_idents(expression, referenced);
    for(const auto& local : variableTypes)
        if(env && env->count("$requires_capture_locals") &&
           !localNames.count(local.first) &&
           !globalVariableTypes.count(local.first) &&
           referenced.count(local.first) && !isVariableMoved(local.first) &&
           isVariableCurrentlyVisible(local.first))
        {
            IdentifierNode identifier(local.first);
            TypeNode* type = getLValueType(&identifier, expression->line);
            if(type)
            {
                if(constantVariables.count(local.first))
                    type = new ReferenceTypeNode(type, false);
                probe.constexprValues.erase(local.first);
                concreteParameters->parameters.push_back(
                    new ParameterNode(type, local.first));
            }
        }
    auto* body = new StatementListNode();
    body->statements.push_back(new ExpressionStatementNode(expression));
    FunctionDefNode function(
        new TypeNode(TypeNode::TYPE_VOID),
        (
            [&]()
            {
                auto scope = probe.constexprValues.find("$concept_namespace");
                return (scope != probe.constexprValues.end() &&
                                !scope->second.typeName.empty()
                            ? scope->second.typeName + "::"
                            : "") +
                       "__mlang_requires_probe";
            })(),
        concreteParameters,
        body);
    function.sourceModule = currentModule;
    function.concreteTypeBindings = bindings;
    probe.structuralProbeFunction = &function;
    probe.structuralProbeExpression = expression;
    probe.generateFunctionDefinition(&function);
    // Scratch-only monomorphized names must not escape into the caller. Return
    // source-level types so result concepts can compare generic instantiations.
    std::function<TypeNode*(TypeNode*)> restoreType =
        [&](TypeNode* type) -> TypeNode*
    {
        if(!type)
            return nullptr;
        if(auto* reference = dynamic_cast<StructTypeRefNode*>(type))
        {
            auto generic =
                probe.mangledToGenericName.find(reference->structName);
            auto arguments =
                probe.monomorphizedTypeArgs.find(reference->structName);
            if(generic != probe.mangledToGenericName.end() &&
               arguments != probe.monomorphizedTypeArgs.end())
            {
                auto* restored = new GenericStructTypeRefNode(generic->second);
                for(auto* argument : arguments->second)
                    restored->typeArgs.push_back(restoreType(argument));
                return restored;
            }
        }
        if(auto* reference = dynamic_cast<ReferenceTypeNode*>(type))
            return new ReferenceTypeNode(
                restoreType(reference->elementType),
                reference->isMutable);
        if(auto* pointer = dynamic_cast<PointerTypeNode*>(type))
            return new PointerTypeNode(restoreType(pointer->elementType));
        return cloneTypeNode(type);
    };
    result = restoreType(probe.structuralProbeResult);
    noThrow = probe.structuralProbeNoThrow;
    return !probe.hasError && result;
}

bool CodeGenerator::evalRequiresExpression(
    RequiresExpressionNode* expression,
    ConstexprValue& out,
    std::string* error,
    ConstexprEnv* env,
    int depth)
{
    if(depth > 128)
    {
        if(error)
            *error = "structural requirement evaluation is too deep";
        return false;
    }
    ConstexprEnv local = env ? *env : ConstexprEnv{};
    if(!env)
    {
        local["$requires_capture_locals"] = ConstexprValue{};
        auto bindings = activeFunctionTypeBindings;
        for(const auto& binding : activeTypeParamBindings)
            bindings[binding.first] = binding.second;
        for(const auto& binding : bindings)
        {
            ConstexprValue type;
            type.kind = ConstexprValue::Kind::Type;
            type.typeName = canonicalConstexprTypeName(binding.second);
            local["__type:" + binding.first] = type;
        }
    }
    std::vector<std::string> names;
    std::vector<TypeNode*> arguments;
    for(const auto& binding : local)
        if(binding.first.compare(0, 7, "__type:") == 0 &&
           binding.second.kind == ConstexprValue::Kind::Type)
        {
            names.push_back(binding.first.substr(7));
            arguments.push_back(
                Helpers::type_from_text(binding.second.typeName));
        }
    if(expression->parameters)
        for(auto* parameter : expression->parameters->parameters)
        {
            local.erase(parameter->name);
            ConstexprValue type;
            type.kind = ConstexprValue::Kind::Type;
            type.typeName = canonicalConstexprTypeName(
                substituteTypeParams(parameter->type, names, arguments));
            local["__requires_local:" + parameter->name] = type;
        }
    bool satisfied = true;
    if(expression->parameters && !expression->parameters->parameters.empty())
    {
        BoolLiteralNode validParameters(true);
        TypeNode* unused = nullptr;
        bool noThrow = false;
        satisfied = probeRequirement(
            &validParameters,
            expression->parameters,
            &local,
            unused,
            noThrow);
    }
    for(auto* requirement : expression->requirements)
    {
        if(!satisfied)
            break;
        if(requirement->kind == RequirementNode::Type)
        {
            TypeNode* unused = nullptr;
            bool noThrow = false;
            satisfied = probeRequirement(
                nullptr,
                expression->parameters,
                &local,
                unused,
                noThrow,
                requirement->requiredType);
        }
        else if(requirement->kind == RequirementNode::Nested)
        {
            ConstexprValue value;
            std::string reason;
            satisfied = evalConstexprExpression(
                            requirement->expression,
                            value,
                            &reason,
                            &local,
                            depth + 1) &&
                        (value.kind == ConstexprValue::Kind::Bool ||
                         value.typeKind == TypeNode::TYPE_BOOL) &&
                        value.boolValue;
        }
        else
        {
            TypeNode* result = nullptr;
            bool noThrow = false;
            satisfied = probeRequirement(
                requirement->expression,
                expression->parameters,
                &local,
                result,
                noThrow);
            if(satisfied && requirement->noThrow)
                satisfied = noThrow;
            if(satisfied && !requirement->resultConcept.empty())
            {
                std::vector<TypeNode*> types{result};
                for(auto* argument : requirement->resultArguments)
                    types.push_back(
                        substituteTypeParams(argument, names, arguments));
                ConceptApplicationNode
                    application(requirement->resultConcept, types);
                ConstexprValue value;
                std::string reason;
                satisfied = evalConcept(
                                &application,
                                value,
                                &reason,
                                &local,
                                depth + 1) &&
                            value.boolValue;
            }
        }
        if(!satisfied)
            break;
    }
    out.kind = ConstexprValue::Kind::Bool;
    out.typeKind = TypeNode::TYPE_BOOL;
    out.boolValue = satisfied;
    out.intValue = satisfied ? 1 : 0;
    out.floatValue = satisfied ? 1.0 : 0.0;
    return true;
}
