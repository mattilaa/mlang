#include "concepts.h"
#include "ir.h"
#include "ir/common.h"
#include "module.h"
#include <functional>
#include <sstream>

using mlang::ir_detail::common::Helpers;

void CodeGenerator::buildConceptTable(ProgramNode* program)
{
    conceptProgram = program;
    conceptDefinitions.clear();
    conceptEvaluationStack.clear();
    conceptKnownTypes =
        {"void",
         "bool",
         "bit",
         "i8",
         "i16",
         "i32",
         "i64",
         "u8",
         "u16",
         "u32",
         "u64",
         "f32",
         "f64",
         "str8",
         "str16"};
    if(program->structList)
        for(auto* definition : program->structList->structs)
            if(definition)
                conceptKnownTypes.insert(definition->name);
    if(program->enumList)
        for(auto* definition : program->enumList->enums)
            if(definition)
                conceptKnownTypes.insert(definition->name);
    for(auto* alias : program->typeAliases)
        if(alias)
            conceptKnownTypes.insert(alias->name);
    for(auto* definition : program->conceptDefs)
    {
        if(!definition)
            continue;
        auto existing = conceptDefinitions.find(definition->name);
        if(existing != conceptDefinitions.end() &&
           existing->second != definition)
        {
            reportError(
                definition->line,
                "duplicate concept '" + definition->name + "'");
            continue;
        }
        if(conceptKnownTypes.count(definition->name) ||
           traitDefinitions.count(definition->name))
            reportError(
                definition->line,
                "concept '" + definition->name +
                    "' conflicts with an existing type or trait");
        std::set<std::string> parameters;
        for(const auto& parameter : definition->typeParams)
            if(!parameters.insert(parameter).second)
                reportError(
                    definition->line,
                    "duplicate concept type parameter '" + parameter + "'");
        conceptDefinitions[definition->name] = definition;
    }
    std::function<void(ExpressionNode*, const std::string&, const std::string&)>
        checkNames;
    checkNames = [&](ExpressionNode* expression,
                     const std::string& scope,
                     const std::string& source)
    {
        if(!expression)
            return;
        if(auto* application =
               dynamic_cast<ConceptApplicationNode*>(expression))
        {
            auto* definition = findConcept(application->name, scope);
            if(!definition)
                reportError(
                    application->line,
                    "unknown concept '" + application->name + "'");
            else if(
                definition->typeParams.size() !=
                application->typeArguments.size())
                reportError(
                    application->line,
                    "concept '" + application->name + "' expects " +
                        std::to_string(definition->typeParams.size()) +
                        " type argument(s)");
            else if(
                !definition->sourceModule.empty() && !definition->isPublic &&
                !Helpers::is_same_module_family(
                    definition->sourceModule,
                    source))
                reportError(
                    application->line,
                    "concept '" + definition->name +
                        "' is private in module '" + definition->sourceModule +
                        "'");
        }
        else if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
        {
            checkNames(binary->left, scope, source);
            checkNames(binary->right, scope, source);
        }
        else if(auto* unary = dynamic_cast<UnaryOpNode*>(expression))
            checkNames(unary->operand, scope, source);
        else if(auto* call = dynamic_cast<FunctionCallNode*>(expression))
            for(auto* argument : call->arguments)
                checkNames(argument, scope, source);
        else if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
        {
            checkNames(ternary->condition, scope, source);
            checkNames(ternary->trueExpr, scope, source);
            checkNames(ternary->falseExpr, scope, source);
        }
    };
    for(auto* definition : program->conceptDefs)
    {
        if(!definition)
            continue;
        const size_t separator = definition->name.rfind("::");
        checkNames(
            definition->predicate,
            separator == std::string::npos
                ? ""
                : definition->name.substr(0, separator),
            definition->sourceModule);
        if(dynamic_cast<IntLiteralNode*>(definition->predicate) ||
           dynamic_cast<FloatLiteralNode*>(definition->predicate) ||
           dynamic_cast<DoubleLiteralNode*>(definition->predicate) ||
           dynamic_cast<StringLiteralNode*>(definition->predicate))
            reportError(
                definition->line,
                "concept '" + definition->name + "' must evaluate to bool");
    }
    if(program->functionList)
        for(auto* function : program->functionList->functions)
        {
            if(!function || !function->requiresClause)
                continue;
            const size_t separator = function->name.rfind("::");
            checkNames(
                function->requiresClause,
                separator == std::string::npos
                    ? ""
                    : function->name.substr(0, separator),
                function->sourceModule);
        }
}

ConceptDefNode* CodeGenerator::findConcept(
    const std::string& name,
    const std::string& scope)
{
    std::string enclosing = scope;
    if(name.find("::") == std::string::npos)
        while(!enclosing.empty())
        {
            auto found = conceptDefinitions.find(enclosing + "::" + name);
            if(found != conceptDefinitions.end())
                return found->second;
            const size_t separator = enclosing.rfind("::");
            if(separator == std::string::npos)
                break;
            enclosing.resize(separator);
        }
    auto found = conceptDefinitions.find(name);
    if(found != conceptDefinitions.end())
        return found->second;
    // Qualified module lookup works without a wildcard import. Inside a
    // definition, private helper concepts remain available in its module.
    if(moduleLoader)
    {
        size_t separator = name.rfind("::");
        while(separator != std::string::npos)
        {
            if(auto* module =
                   moduleLoader->getModule(name.substr(0, separator)))
                for(auto* definition : module->conceptDefs)
                    if(definition &&
                       definition->name == name.substr(separator + 2))
                        return definition;
            if(separator == 0)
                break;
            separator = name.rfind("::", separator - 1);
        }
        if(!currentModule.empty())
            if(auto* module = moduleLoader->getModule(currentModule))
                for(auto* definition : module->conceptDefs)
                    if(definition && definition->name == name)
                        return definition;
    }
    return nullptr;
}

bool CodeGenerator::evalConcept(
    ConceptApplicationNode* application,
    ConstexprValue& out,
    std::string* error,
    ConstexprEnv* env,
    int depth)
{
    auto fail = [&](const std::string& reason)
    {
        if(error)
            *error = reason;
        return false;
    };
    std::string scope;
    bool explicitScope = false;
    if(env)
    {
        auto found = env->find("$concept_namespace");
        if(found == env->end())
            found = env->find("$literal_namespace");
        if(found != env->end())
        {
            scope = found->second.typeName;
            explicitScope = true;
        }
    }
    if(!explicitScope && builder.GetInsertBlock())
    {
        const std::string caller =
            builder.GetInsertBlock()->getParent()->getName().str();
        const size_t separator = caller.rfind("::");
        if(separator != std::string::npos)
            scope = caller.substr(0, separator);
    }
    ConceptDefNode* definition = findConcept(application->name, scope);
    if(!definition)
        return fail("unknown concept '" + application->name + "'");
    if(!definition->sourceModule.empty() && !definition->isPublic &&
       !Helpers::is_same_module_family(definition->sourceModule, currentModule))
        return fail(
            "concept '" + definition->name + "' is private in module '" +
            definition->sourceModule + "'");
    if(definition->typeParams.size() != application->typeArguments.size())
        return fail(
            "concept '" + definition->name + "' expects " +
            std::to_string(definition->typeParams.size()) +
            " type argument(s)");

    std::map<std::string, TypeNode*> bindings;
    if(!env)
    {
        bindings = activeFunctionTypeBindings;
        for(const auto& binding : activeTypeParamBindings)
            bindings[binding.first] = binding.second;
    }
    if(env)
        for(const auto& binding : *env)
            if(binding.first.compare(0, 7, "__type:") == 0 &&
               binding.second.kind == ConstexprValue::Kind::Type)
                bindings[binding.first.substr(7)] =
                    Helpers::type_from_text(binding.second.typeName);
    std::vector<std::string> names;
    std::vector<TypeNode*> types;
    for(const auto& binding : bindings)
    {
        names.push_back(binding.first);
        types.push_back(binding.second);
    }
    ConstexprEnv local;
    std::string key = definition->name + "<";
    for(size_t i = 0; i < application->typeArguments.size(); ++i)
    {
        TypeNode* type =
            substituteTypeParams(application->typeArguments[i], names, types);
        std::vector<std::string> aliases;
        type = resolveTypeAliasNode(type, {}, aliases);
        if(!isKnownRequirementType(type))
            return fail(
                "unknown or unbound type argument in concept '" +
                application->name + "'");
        ConstexprValue value;
        value.kind = ConstexprValue::Kind::Type;
        value.typeName = canonicalConstexprTypeName(type);
        local["__type:" + definition->typeParams[i]] = value;
        key += value.typeName + ";";
    }
    key += ">";
    if(conceptEvaluationStack.size() >= 128 ||
       std::find(
           conceptEvaluationStack.begin(),
           conceptEvaluationStack.end(),
           key) != conceptEvaluationStack.end())
        return fail("recursive concept evaluation: '" + key + "'");
    conceptEvaluationStack.push_back(key);
    const size_t separator = definition->name.rfind("::");
    ConstexprValue nameSpace;
    nameSpace.kind = ConstexprValue::Kind::Type;
    nameSpace.typeName = separator == std::string::npos
                             ? ""
                             : definition->name.substr(0, separator);
    local["$concept_namespace"] = nameSpace;
    const std::string callerModule = currentModule;
    currentModule = definition->sourceModule;
    ConstexprValue result;
    const bool evaluated = evalConstexprExpression(
        definition->predicate,
        result,
        error,
        &local,
        depth + 1);
    currentModule = callerModule;
    conceptEvaluationStack.pop_back();
    if(!evaluated)
        return false;
    if(result.kind != ConstexprValue::Kind::Bool &&
       result.typeKind != TypeNode::TYPE_BOOL)
        return fail("concept '" + definition->name + "' must evaluate to bool");
    out = result;
    out.kind = ConstexprValue::Kind::Bool;
    out.typeKind = TypeNode::TYPE_BOOL;
    out.intValue = out.boolValue ? 1 : 0;
    out.floatValue = out.boolValue ? 1.0 : 0.0;
    return true;
}

bool CodeGenerator::satisfiesFunctionConstraints(
    FunctionDefNode* function,
    const std::map<std::string, TypeNode*>& bindings,
    std::string* error,
    std::vector<std::set<std::string>>* normalized)
{
    if(function->genericOrigin)
        function = function->genericOrigin;

    struct ModuleRestore
    {
        std::string& slot;
        std::string previous;

        ~ModuleRestore()
        {
            slot = previous;
        }
    } restore{currentModule, currentModule};

    currentModule = function->sourceModule;
    auto fail = [&](const std::string& reason)
    {
        if(error)
            *error = reason;
        return false;
    };
    ConstexprEnv env;
    for(const auto& binding : bindings)
    {
        ConstexprValue value;
        value.kind = ConstexprValue::Kind::Type;
        value.typeName = canonicalConstexprTypeName(binding.second);
        env["__type:" + binding.first] = value;
    }
    std::string scope;
    const size_t separator = function->name.rfind("::");
    if(separator != std::string::npos)
        scope = function->name.substr(0, separator);
    ConstexprValue nameSpace;
    nameSpace.kind = ConstexprValue::Kind::Type;
    nameSpace.typeName = scope;
    env["$concept_namespace"] = nameSpace;

    using Terms = mlang::concepts::Terms;
    Terms terms{std::set<std::string>{}};
    auto conjunction = [&](const Terms& left, const Terms& right, Terms& result)
    {
        if(left.size() * right.size() > 256)
            return false;
        for(const auto& a : left)
            for(const auto& b : right)
            {
                auto term = a;
                term.insert(b.begin(), b.end());
                result.push_back(std::move(term));
            }
        return true;
    };
    std::function<bool(
        ExpressionNode*,
        const std::map<std::string, TypeNode*>&,
        const std::string&,
        Terms&,
        int)>
        normalize;
    normalize = [&](ExpressionNode* expression,
                    const std::map<std::string, TypeNode*>& concrete,
                    const std::string& currentScope,
                    Terms& result,
                    int depth)
    {
        if(depth > 128)
            return false;
        if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
            if(binary->op == BinaryOpNode::OP_AND ||
               binary->op == BinaryOpNode::OP_OR)
            {
                Terms left, right;
                if(!normalize(
                       binary->left,
                       concrete,
                       currentScope,
                       left,
                       depth + 1) ||
                   !normalize(
                       binary->right,
                       concrete,
                       currentScope,
                       right,
                       depth + 1))
                    return false;
                if(binary->op == BinaryOpNode::OP_AND)
                    return conjunction(left, right, result);
                result = std::move(left);
                result.insert(result.end(), right.begin(), right.end());
                return result.size() <= 256;
            }
        if(auto* application =
               dynamic_cast<ConceptApplicationNode*>(expression))
        {
            auto* definition = findConcept(application->name, currentScope);
            if(definition && definition->typeParams.size() ==
                                 application->typeArguments.size())
            {
                std::vector<std::string> names;
                std::vector<TypeNode*> arguments;
                for(const auto& binding : concrete)
                {
                    names.push_back(binding.first);
                    arguments.push_back(binding.second);
                }
                std::map<std::string, TypeNode*> local;
                for(size_t i = 0; i < definition->typeParams.size(); ++i)
                    local[definition->typeParams[i]] = substituteTypeParams(
                        application->typeArguments[i],
                        names,
                        arguments);
                const size_t scopeEnd = definition->name.rfind("::");
                return normalize(
                    definition->predicate,
                    local,
                    scopeEnd == std::string::npos
                        ? ""
                        : definition->name.substr(0, scopeEnd),
                    result,
                    depth + 1);
            }
        }
        std::string atom =
            std::to_string(reinterpret_cast<uintptr_t>(expression));
        for(const auto& binding : concrete)
            atom += ":" + binding.first + "=" +
                    canonicalConstexprTypeName(binding.second);
        result = Terms{{atom}};
        return true;
    };
    for(const auto& bound : function->typeParamTraitBounds)
    {
        auto concrete = bindings.find(bound.first);
        if(concrete == bindings.end())
            continue;
        std::istringstream parts(bound.second);
        std::string name;
        while(std::getline(parts, name, '+'))
        {
            if(auto* definition = findConcept(name, scope))
            {
                ConceptApplicationNode
                    application(definition->name, {concrete->second});
                ConstexprValue value;
                if(!evalConcept(&application, value, error, &env, 0))
                    return false;
                if(!value.boolValue)
                    return fail(
                        "concept '" + name + "' is not satisfied for " +
                        bound.first + " = " +
                        canonicalConstexprTypeName(concrete->second));
                Terms condition, combined;
                if(!normalize(&application, bindings, scope, condition, 0) ||
                   !conjunction(terms, condition, combined))
                    return fail(
                        "concept constraint normalization is too complex");
                terms = std::move(combined);
            }
            else
            {
                if(!validateTypeArgumentTraitBounds(
                       {bound.first},
                       {{bound.first, name}},
                       {concrete->second},
                       {},
                       function->line,
                       "function",
                       function->name,
                       false))
                    return fail(
                        "trait bound '" + name + "' is not satisfied for " +
                        bound.first);
                for(auto& term : terms)
                    term.insert(
                        "trait:" + name + ":" +
                        canonicalConstexprTypeName(concrete->second));
            }
        }
    }
    if(function->requiresClause)
    {
        ConstexprValue result;
        const std::string callerModule = currentModule;
        currentModule = function->sourceModule;
        const bool evaluated = evalConstexprExpression(
            function->requiresClause,
            result,
            error,
            &env,
            0);
        currentModule = callerModule;
        if(!evaluated)
            return false;
        if(result.kind != ConstexprValue::Kind::Bool &&
           result.typeKind != TypeNode::TYPE_BOOL)
            return fail("constraint clause must evaluate to bool");
        if(!result.boolValue)
        {
            std::string arguments;
            for(const auto& binding : bindings)
                arguments += (arguments.empty() ? "" : ", ") + binding.first +
                             " = " + canonicalConstexprTypeName(binding.second);
            return fail(
                "constraint clause '" + function->requiresClause->toString() +
                "' is not satisfied" +
                (arguments.empty() ? "" : " for " + arguments));
        }
        Terms condition, combined;
        if(!normalize(
               function->requiresClause,
               bindings,
               scope,
               condition,
               0) ||
           !conjunction(terms, condition, combined))
            return fail("concept constraint normalization is too complex");
        terms = std::move(combined);
    }
    if(normalized)
        *normalized = std::move(terms);
    return true;
}

bool CodeGenerator::isKnownRequirementType(TypeNode* type)
{
    if(!type)
        return false;
    if(auto* generic = dynamic_cast<GenericStructTypeRefNode*>(type))
    {
        if(!conceptKnownTypes.count(generic->structName))
            return false;
        for(auto* argument : generic->typeArgs)
            if(!isKnownRequirementType(argument))
                return false;
    }
    else if(auto* reference = dynamic_cast<StructTypeRefNode*>(type))
        return conceptKnownTypes.count(reference->structName) ||
               structTypes.count(reference->structName);
    else if(auto* pointer = dynamic_cast<PointerTypeNode*>(type))
        return isKnownRequirementType(pointer->elementType);
    else if(auto* reference = dynamic_cast<ReferenceTypeNode*>(type))
        return isKnownRequirementType(reference->elementType);
    else if(auto* list = dynamic_cast<GenericListTypeNode*>(type))
        return isKnownRequirementType(list->elementType);
    else if(auto* map = dynamic_cast<MapTypeNode*>(type))
        return isKnownRequirementType(map->keyType) &&
               isKnownRequirementType(map->valueType);
    else if(auto* function = dynamic_cast<FunctionTypeNode*>(type))
    {
        if(!isKnownRequirementType(function->returnType))
            return false;
        if(function->parameterTypes)
            for(auto* parameter : function->parameterTypes->types)
                if(!isKnownRequirementType(parameter))
                    return false;
    }
    else if(auto* tuple = dynamic_cast<TupleTypeNode*>(type))
        for(auto* item : tuple->elementTypes->types)
            if(!isKnownRequirementType(item))
                return false;
    return true;
}
