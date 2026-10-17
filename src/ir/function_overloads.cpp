#include "ir.h"
#include "ir/common.h"

using mlang::ir_detail::common::Helpers;

#include <string>


std::string CodeGenerator::functionSignatureKey(FunctionDefNode* node) const
{
    std::string key = node->name + "(";
    if(node->parameters)
    {
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            if(i > 0)
                key += ",";
            key += typeMangle(node->parameters->parameters[i]->type);
        }
        if(node->parameters->isVarArg)
        {
            if(!node->parameters->parameters.empty())
                key += ",";
            key += "...";
        }
    }
    key += ")";
    if(!node->closureSpecializationKey.empty())
        key += "#closure=" + node->closureSpecializationKey;
    return key;
}

std::string CodeGenerator::functionSymbolName(FunctionDefNode* node) const
{
    if(node->isExtern)
        return node->name;
    if(node->name == "main")
        return "main";

    std::string suffix;
    if(node->parameters && !node->parameters->parameters.empty())
    {
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            if(i > 0)
                suffix += "_";
            suffix += typeMangle(node->parameters->parameters[i]->type);
        }
    }
    else
    {
        suffix = "void";
    }
    if(node->parameters && node->parameters->isVarArg)
    {
        if(!suffix.empty())
            suffix += "_";
        suffix += "vararg";
    }
    if(!node->closureSpecializationKey.empty())
        suffix += "_closure_" + node->closureSpecializationKey;
    return node->name + "__" + suffix;
}

void CodeGenerator::registerFunctionOverload(FunctionDefNode* node,
                                             llvm::Function* function)
{
    if(!node)
        return;

    std::string signatureKey = functionSignatureKey(node);
    auto& overloads = functionOverloads[node->name];
    bool alreadyRegistered = false;
    for(const auto& info : overloads)
    {
        if(info.signatureKey == signatureKey)
        {
            // Module import merging can surface the same declaration multiple
            // times. Treat identical signatures from the same source/symbol as
            // duplicates to skip, not hard errors.
            if(info.sourceModule == node->sourceModule ||
               (function && info.symbolName == function->getName().str()))
            {
                alreadyRegistered = true;
                break;
            }
            reportError(node->line,
                        "duplicate function overload: '" + signatureKey + "'");
            return;
        }
    }

    if(!alreadyRegistered)
    {
        FunctionOverloadInfo info{node,
                                  function,
                                  signatureKey,
                                  function ? function->getName().str() : "",
                                  node->parameters
                                      ? node->parameters->isVarArg
                                      : false,
                                  node->isPublic,
                                  node->sourceModule};
        overloads.push_back(info);
    }

    if(function && node->body && node->returnType &&
       node->returnType->kind == TypeNode::TYPE_PTR && node->parameters)
    {
        const std::string symbol = function->getName().str();
        functionPointerReturnSummaryComplete.erase(symbol);
        bool hasUnknownReturnOrigin = false;
        std::map<std::string, std::set<size_t>> pointerAliases;
        std::map<std::string, std::set<size_t>> pointerFieldAliases;
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            auto* parameter = node->parameters->parameters[i];
            if(parameter && dynamic_cast<PointerTypeNode*>(parameter->type))
                pointerAliases[parameter->name].insert(i);
        }
        std::function<bool(ExpressionNode*, std::string&)> getFieldPath =
            [&](ExpressionNode* current, std::string& path) {
            if(auto* identifier = dynamic_cast<IdentifierNode*>(current))
            {
                path = identifier->name;
                return true;
            }
            auto* field = dynamic_cast<FieldAccessNode*>(current);
            if(!field)
                return false;
            if(field->object)
            {
                if(!getFieldPath(field->object, path))
                    return false;
            }
            else
                path = field->structName;
            if(path.empty())
                return false;
            path += "." + field->fieldName;
            return true;
        };

        std::function<std::set<size_t>(ExpressionNode*)> origins =
            [&](ExpressionNode* expression) -> std::set<size_t> {
            if(!expression)
                return {};
            if(auto* identifier = dynamic_cast<IdentifierNode*>(expression))
            {
                auto alias = pointerAliases.find(identifier->name);
                return alias == pointerAliases.end()
                           ? std::set<size_t>{}
                           : alias->second;
            }
            if(auto* field = dynamic_cast<FieldAccessNode*>(expression))
            {
                std::string path;
                if(getFieldPath(field, path))
                {
                    auto alias = pointerFieldAliases.find(path);
                    if(alias != pointerFieldAliases.end())
                        return alias->second;
                }
                hasUnknownReturnOrigin = true;
                return {};
            }
            if(auto* cast = dynamic_cast<CastExpressionNode*>(expression))
                return origins(cast->expression);
            if(auto* attempt = dynamic_cast<TryExpressionNode*>(expression))
                return origins(attempt->expression);
            if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
            {
                auto result = origins(ternary->trueExpr);
                auto other = origins(ternary->falseExpr);
                result.insert(other.begin(), other.end());
                return result;
            }
            if(auto* match = dynamic_cast<MatchExpressionNode*>(expression))
            {
                std::set<size_t> result;
                for(auto* arm : match->arms)
                    if(arm)
                    {
                        auto other = origins(arm->expression);
                        result.insert(other.begin(), other.end());
                    }
                return result;
            }
            if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
                if(binary->op == BinaryOpNode::OP_COMMA)
                    return origins(binary->right);
            if(auto* call = dynamic_cast<FunctionCallNode*>(expression))
            {
                std::set<size_t> arguments;
                auto candidates = functionOverloads.find(call->name);
                if(candidates != functionOverloads.end())
                    for(const auto& candidate : candidates->second)
                    {
                        if(!candidate.node || !candidate.node->returnType ||
                           candidate.node->returnType->kind !=
                               TypeNode::TYPE_PTR)
                            continue;
                        auto summary = functionPointerReturnParameters.find(
                            candidate.symbolName);
                        if(!functionPointerReturnSummaryComplete.count(
                               candidate.symbolName))
                        {
                            hasUnknownReturnOrigin = true;
                            continue;
                        }
                        if(summary == functionPointerReturnParameters.end())
                            continue;
                        for(size_t index : summary->second)
                            if(index < call->arguments.size())
                            {
                                auto forwarded = origins(call->arguments[index]);
                                arguments.insert(forwarded.begin(),
                                                 forwarded.end());
                            }
                    }
                else
                    hasUnknownReturnOrigin = true;
                return arguments;
            }
            if(dynamic_cast<IndexExpressionNode*>(expression) ||
               dynamic_cast<TupleAccessNode*>(expression))
            {
                // A pointer loaded from aggregate storage is not yet tracked
                // by parameter path. Preserve safety at callers by making the
                // summary incomplete rather than claiming no borrow origin.
                hasUnknownReturnOrigin = true;
                return {};
            }
            if(auto* address = dynamic_cast<UnaryOpNode*>(expression))
            {
                if((address->op == UnaryOpNode::OP_ADDR ||
                    address->op == UnaryOpNode::OP_ADDR_MUT))
                    if(auto* dereference =
                           dynamic_cast<UnaryOpNode*>(address->operand))
                        if(dereference->op == UnaryOpNode::OP_DEREF)
                            return origins(dereference->operand);
                if(address->op == UnaryOpNode::OP_DEREF)
                    hasUnknownReturnOrigin = true;
            }
            if(dynamic_cast<CallableInvokeNode*>(expression) ||
               dynamic_cast<MethodCallNode*>(expression))
                hasUnknownReturnOrigin = true;
            return {};
        };

        auto clearPointerFieldAliasSubtree = [&](const std::string& path) {
            const std::string prefix = path + ".";
            for(auto alias = pointerFieldAliases.begin();
                alias != pointerFieldAliases.end();)
                if(alias->first == path ||
                   alias->first.compare(0, prefix.size(), prefix) == 0)
                    alias = pointerFieldAliases.erase(alias);
                else
                    ++alias;
        };
        auto copyPointerFieldAliases = [&](const std::string& source,
                                           const std::string& destination,
                                           auto& output) {
            const std::string sourcePrefix = source + ".";
            const std::string destinationPrefix = destination + ".";
            bool copiedAny = false;
            for(const auto& alias : pointerFieldAliases)
                if(alias.first.compare(0, sourcePrefix.size(),
                                       sourcePrefix) == 0)
                {
                    output[destinationPrefix +
                           alias.first.substr(sourcePrefix.size())] =
                        alias.second;
                    copiedAny = true;
                }
            return copiedAny;
        };
        std::function<void(const std::string&, ExpressionNode*,
                           std::map<std::string, std::set<size_t>>&)>
            collectPointerFieldAliases =
                [&](const std::string& path, ExpressionNode* expression,
                    std::map<std::string, std::set<size_t>>& output) {
                    if(auto* literal =
                           dynamic_cast<StructLiteralNode*>(expression))
                    {
                        for(const auto& field : literal->fields)
                            collectPointerFieldAliases(
                                path + "." + field.first, field.second,
                                output);
                        return;
                    }
                    std::string sourcePath;
                    if(getFieldPath(expression, sourcePath) &&
                       copyPointerFieldAliases(sourcePath, path, output))
                        return;
                    if(auto* identifier =
                           dynamic_cast<IdentifierNode*>(expression))
                        if(copyPointerFieldAliases(identifier->name, path,
                                                   output))
                            return;
                    output[path] = origins(expression);
                };
        auto replacePointerFieldAliases = [&](const std::string& path,
                                              ExpressionNode* expression) {
            std::map<std::string, std::set<size_t>> replacement;
            collectPointerFieldAliases(path, expression, replacement);
            clearPointerFieldAliasSubtree(path);
            pointerFieldAliases.insert(replacement.begin(), replacement.end());
        };
        auto hasPointerFieldAliases = [&](const std::string& path) {
            const std::string prefix = path + ".";
            return std::any_of(
                pointerFieldAliases.begin(), pointerFieldAliases.end(),
                [&](const auto& alias) {
                    return alias.first.compare(0, prefix.size(), prefix) == 0;
                });
        };
        auto shouldTrackAggregateAliases = [&](const std::string& path,
                                               ExpressionNode* expression) {
            if(dynamic_cast<StructLiteralNode*>(expression) ||
               hasPointerFieldAliases(path))
                return true;
            std::string sourcePath;
            if(getFieldPath(expression, sourcePath))
                return hasPointerFieldAliases(sourcePath);
            return false;
        };

        std::function<bool(StatementNode*)> scanStatement;
        std::function<bool(StatementListNode*)> scanList;
        auto mergeAliases = [](const std::vector<
                                   std::map<std::string, std::set<size_t>>>&
                                   branches) {
            std::map<std::string, std::set<size_t>> merged;
            for(const auto& branch : branches)
                for(const auto& alias : branch)
                    merged[alias.first].insert(alias.second.begin(),
                                               alias.second.end());
            return merged;
        };
        scanList = [&](StatementListNode* statements) {
            if(!statements)
                return true;
            std::map<std::string, std::set<size_t>> previousAliases;
            std::set<std::string> aliasesNotPreviouslyPresent;
            std::map<std::string,
                     std::map<std::string, std::set<size_t>>>
                previousFieldAliases;
            std::set<std::string> shadowedNames;
            auto rememberShadow = [&](const std::string& name) {
                if(!shadowedNames.insert(name).second)
                    return;
                auto previous = pointerAliases.find(name);
                if(previous == pointerAliases.end())
                    aliasesNotPreviouslyPresent.insert(name);
                else
                    previousAliases.emplace(name, previous->second);
                const std::string prefix = name + ".";
                auto& saved = previousFieldAliases[name];
                for(const auto& fieldAlias : pointerFieldAliases)
                    if(fieldAlias.first.compare(0, prefix.size(), prefix) == 0)
                        saved.insert(fieldAlias);
            };
            bool fallsThrough = true;
            for(auto* statement : statements->statements)
            {
                // Snapshot a shadow at its declaration point so earlier
                // assignments in this block still update the outer binding.
                if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
                {
                    rememberShadow(declaration->name);
                    const std::string prefix = declaration->name + ".";
                    for(auto alias = pointerFieldAliases.begin();
                        alias != pointerFieldAliases.end();)
                        if(alias->first.compare(0, prefix.size(), prefix) == 0)
                            alias = pointerFieldAliases.erase(alias);
                        else
                            ++alias;
                }
                else if(auto* declaration =
                            dynamic_cast<VarDeclNode*>(statement))
                {
                    rememberShadow(declaration->name);
                    const std::string prefix = declaration->name + ".";
                    for(auto alias = pointerFieldAliases.begin();
                        alias != pointerFieldAliases.end();)
                        if(alias->first.compare(0, prefix.size(), prefix) == 0)
                            alias = pointerFieldAliases.erase(alias);
                        else
                            ++alias;
                }
                if(!scanStatement(statement))
                {
                    fallsThrough = false;
                    break;
                }
            }
            for(const auto& name : aliasesNotPreviouslyPresent)
                pointerAliases.erase(name);
            for(auto& previous : previousAliases)
                pointerAliases[previous.first] = std::move(previous.second);
            for(const auto& name : shadowedNames)
            {
                const std::string prefix = name + ".";
                for(auto alias = pointerFieldAliases.begin();
                    alias != pointerFieldAliases.end();)
                    if(alias->first.compare(0, prefix.size(), prefix) == 0)
                        alias = pointerFieldAliases.erase(alias);
                    else
                        ++alias;
                const auto saved = previousFieldAliases.find(name);
                if(saved != previousFieldAliases.end())
                    pointerFieldAliases.insert(saved->second.begin(),
                                               saved->second.end());
            }
            return fallsThrough;
        };
        scanStatement = [&](StatementNode* statement) {
            if(!statement)
                return true;
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
            {
                auto returnedOrigins = origins(returned->expression);
                auto& summary = functionPointerReturnParameters[symbol];
                summary.insert(returnedOrigins.begin(), returnedOrigins.end());
                return false;
            }
            if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                auto values = origins(declaration->expression);
                if(!values.empty())
                    pointerAliases[declaration->name] = std::move(values);
                else
                    pointerAliases.erase(declaration->name);
                if(shouldTrackAggregateAliases(declaration->name,
                                               declaration->expression))
                    replacePointerFieldAliases(declaration->name,
                                               declaration->expression);
            }
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            {
                auto values = origins(declaration->initExpr);
                if(!values.empty())
                    pointerAliases[declaration->name] = std::move(values);
                else
                    pointerAliases.erase(declaration->name);
                if(shouldTrackAggregateAliases(declaration->name,
                                               declaration->initExpr))
                    replacePointerFieldAliases(declaration->name,
                                               declaration->initExpr);
            }
            else if(auto* assignment =
                        dynamic_cast<AssignmentNode*>(statement))
            {
                auto values = origins(assignment->expression);
                if(values.empty())
                    pointerAliases.erase(assignment->name);
                else
                    pointerAliases[assignment->name] = std::move(values);
                if(shouldTrackAggregateAliases(assignment->name,
                                              assignment->expression))
                    replacePointerFieldAliases(assignment->name,
                                               assignment->expression);
            }
            else if(auto* assignment =
                        dynamic_cast<FieldAssignmentNode*>(statement))
            {
                std::string path;
                bool hasPath = false;
                if(assignment->target)
                {
                    hasPath = getFieldPath(assignment->target, path);
                    if(hasPath)
                        path += "." + assignment->fieldName;
                }
                else if(!assignment->structName.empty() &&
                        !assignment->fieldName.empty())
                {
                    path = assignment->structName + "." +
                           assignment->fieldName;
                    hasPath = true;
                }
                if(hasPath)
                    replacePointerFieldAliases(path, assignment->expression);
                else
                    hasUnknownReturnOrigin = true;
            }
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                return scanList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                std::vector<std::map<std::string, std::set<size_t>>> branches;
                std::vector<std::map<std::string, std::set<size_t>>>
                    fieldBranches;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                if(scanList(branch->thenBranch))
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                const bool elseIfFallsThrough =
                    scanStatement(branch->elseIfBranch);
                if(branch->elseIfBranch && elseIfFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                }
                else if(!branch->elseBranch)
                {
                    branches.push_back(baseAliases);
                    fieldBranches.push_back(baseFieldAliases);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                const bool elseFallsThrough = scanList(branch->elseBranch);
                if(branch->elseBranch && elseFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                }
                if(branches.empty())
                    return false;
                pointerAliases = mergeAliases(branches);
                pointerFieldAliases = mergeAliases(fieldBranches);
                return true;
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                std::vector<std::map<std::string, std::set<size_t>>> branches;
                std::vector<std::map<std::string, std::set<size_t>>>
                    fieldBranches;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                if(scanList(branch->thenBranch))
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                const bool elseIfFallsThrough =
                    scanStatement(branch->elseIfBranch);
                if(branch->elseIfBranch && elseIfFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                }
                else if(!branch->elseBranch)
                {
                    branches.push_back(baseAliases);
                    fieldBranches.push_back(baseFieldAliases);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                const bool elseFallsThrough = scanList(branch->elseBranch);
                if(branch->elseBranch && elseFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                }
                if(branches.empty())
                    return false;
                pointerAliases = mergeAliases(branches);
                pointerFieldAliases = mergeAliases(fieldBranches);
                return true;
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                const bool bodyFallsThrough = scanList(loop->body);
                if(!bodyFallsThrough)
                {
                    pointerAliases = baseAliases;
                    pointerFieldAliases = baseFieldAliases;
                }
                pointerAliases = mergeAliases({baseAliases, pointerAliases});
                pointerFieldAliases = mergeAliases(
                    {baseFieldAliases, pointerFieldAliases});
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                const bool bodyFallsThrough = scanList(loop->body);
                if(!bodyFallsThrough)
                {
                    pointerAliases = baseAliases;
                    pointerFieldAliases = baseFieldAliases;
                }
                pointerAliases = mergeAliases({baseAliases, pointerAliases});
                pointerFieldAliases = mergeAliases(
                    {baseFieldAliases, pointerFieldAliases});
            }
            else if(dynamic_cast<TryCatchNode*>(statement))
            {
                // Exceptions can enter catch from any point in the try body;
                // until that exceptional flow is modeled, callers must retain
                // every pointer argument for this function's pointer result.
                hasUnknownReturnOrigin = true;
            }
            return true;
        };
        scanList(node->body);
        if(!hasUnknownReturnOrigin)
            functionPointerReturnSummaryComplete.insert(symbol);
    }

    functionVisibility[signatureKey] =
        std::make_pair(node->isPublic, node->sourceModule);
}

bool CodeGenerator::isOverloadVisible(const FunctionOverloadInfo& info) const
{
    if(info.sourceModule.empty())
        return true;
    if(Helpers::is_same_module_family(info.sourceModule, currentModule))
        return true;
    return info.isPublic;
}

bool CodeGenerator::canConvertType(llvm::Type* actualType,
                                   llvm::Type* expectedType, int& cost) const
{
    if(actualType == expectedType)
    {
        cost = 0;
        return true;
    }
    if(actualType->isPointerTy() && expectedType->isPointerTy())
    {
        cost = 0;
        return true;
    }
    if(actualType->isIntegerTy() && expectedType->isIntegerTy())
    {
        cost = 1;
        return true;
    }
    if(actualType->isFloatingPointTy() && expectedType->isFloatingPointTy())
    {
        cost = 1;
        return true;
    }
    if(actualType->isIntegerTy() && expectedType->isFloatingPointTy())
    {
        cost = 2;
        return true;
    }
    if(actualType->isFloatingPointTy() && expectedType->isIntegerTy())
    {
        cost = 2;
        return true;
    }
    return false;
}
