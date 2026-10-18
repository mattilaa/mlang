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

    if(function && node->body && node->parameters)
    {
        const std::string symbol = function->getName().str();
        auto& mutatingParameters =
            functionPointerSlotMutationParameters[symbol];
        mutatingParameters.clear();
        std::map<std::string, std::set<size_t>> pointerSlotAliases;
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            auto* parameter = node->parameters->parameters[i];
            auto* pointer = parameter
                                ? dynamic_cast<PointerTypeNode*>(parameter->type)
                                : nullptr;
            if(pointer && dynamic_cast<PointerTypeNode*>(pointer->elementType))
                pointerSlotAliases[parameter->name].insert(i);
        }
        auto collectPointerSlotAliases =
            [](ExpressionNode* expression,
               const std::map<std::string, std::set<size_t>>& aliases) {
                std::set<size_t> result;
                std::function<void(ExpressionNode*)> collect =
                    [&](ExpressionNode* value) {
                        if(auto* identifier =
                               dynamic_cast<IdentifierNode*>(value))
                        {
                            auto found = aliases.find(identifier->name);
                            if(found != aliases.end())
                                result.insert(found->second.begin(),
                                              found->second.end());
                        }
                        else if(auto* ternary =
                                    dynamic_cast<TernaryNode*>(value))
                        {
                            collect(ternary->trueExpr);
                            collect(ternary->falseExpr);
                        }
                        else if(auto* match =
                                    dynamic_cast<MatchExpressionNode*>(value))
                        {
                            for(auto* arm : match->arms)
                                if(arm)
                                    collect(arm->expression);
                        }
                        else if(auto* cast =
                                    dynamic_cast<CastExpressionNode*>(value))
                            collect(cast->expression);
                        else if(auto* attempt =
                                    dynamic_cast<TryExpressionNode*>(value))
                            collect(attempt->expression);
                        else if(auto* binary =
                                    dynamic_cast<BinaryOpNode*>(value))
                            if(binary->op == BinaryOpNode::OP_COMMA)
                                collect(binary->right);
                    };
                collect(expression);
                return result;
            };
        std::function<void(StatementListNode*)> scanPointerSlotWrites;
        std::function<void(StatementNode*)> scanPointerSlotStatement;
        scanPointerSlotWrites = [&](StatementListNode* statements) {
            if(statements)
                for(auto* statement : statements->statements)
                    scanPointerSlotStatement(statement);
        };
        scanPointerSlotStatement = [&](StatementNode* statement) {
            if(!statement)
                return;
            if(auto* assignment =
                   dynamic_cast<DerefAssignmentNode*>(statement))
            {
                if(auto* identifier = dynamic_cast<IdentifierNode*>(
                       assignment->pointerExpr))
                {
                    auto aliases = pointerSlotAliases.find(identifier->name);
                    if(aliases != pointerSlotAliases.end())
                        mutatingParameters.insert(aliases->second.begin(),
                                                  aliases->second.end());
                }
            }
            else if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                pointerSlotAliases[declaration->name] =
                    collectPointerSlotAliases(declaration->expression,
                                              pointerSlotAliases);
            }
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            {
                pointerSlotAliases[declaration->name] =
                    collectPointerSlotAliases(declaration->initExpr,
                                              pointerSlotAliases);
            }
            else if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
            {
                // Kill stale slot provenance before examining the new value;
                // otherwise writes through a reassigned local alias can be
                // attributed to the caller's old slot.
                auto aliases = collectPointerSlotAliases(
                    assignment->expression, pointerSlotAliases);
                pointerSlotAliases.erase(assignment->name);
                if(!aliases.empty())
                    pointerSlotAliases[assignment->name] = std::move(aliases);
            }
            else if(auto* expressionStatement =
                        dynamic_cast<ExpressionStatementNode*>(statement))
            {
                auto* call = dynamic_cast<FunctionCallNode*>(
                    expressionStatement->expression);
                if(call)
                    for(const auto& calledName :
                        getFunctionItemTargetsForName(call->name))
                    {
                        auto overloads = functionOverloads.find(calledName);
                        if(overloads == functionOverloads.end())
                            continue;
                        for(const auto& overload : overloads->second)
                        {
                            auto summary =
                                functionPointerSlotMutationParameters.find(
                                    overload.symbolName);
                            if(summary ==
                               functionPointerSlotMutationParameters.end())
                                continue;
                            for(size_t parameterIndex : summary->second)
                            {
                                if(parameterIndex >= call->arguments.size())
                                    continue;
                                ExpressionNode* argument =
                                    call->arguments[parameterIndex];
                                if(auto* address =
                                       dynamic_cast<UnaryOpNode*>(argument);
                                   address &&
                                   address->op == UnaryOpNode::OP_ADDR_MUT)
                                    argument = address->operand;
                                auto aliases = collectPointerSlotAliases(
                                    argument, pointerSlotAliases);
                                mutatingParameters.insert(aliases.begin(),
                                                         aliases.end());
                            }
                        }
                    }
            }
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                scanPointerSlotWrites(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                scanPointerSlotWrites(branch->thenBranch);
                scanPointerSlotStatement(branch->elseIfBranch);
                scanPointerSlotWrites(branch->elseBranch);
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                scanPointerSlotWrites(branch->thenBranch);
                scanPointerSlotStatement(branch->elseIfBranch);
                scanPointerSlotWrites(branch->elseBranch);
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
                scanPointerSlotWrites(loop->body);
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
                scanPointerSlotWrites(loop->body);
            else if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
            {
                scanPointerSlotWrites(attempt->tryBlock
                                          ? attempt->tryBlock->statements
                                          : nullptr);
                scanPointerSlotWrites(attempt->catchBlock
                                          ? attempt->catchBlock->statements
                                          : nullptr);
            }
        };
        scanPointerSlotWrites(node->body);

        auto& definitelyMutatingParameters =
            functionPointerSlotDefiniteMutationParameters[symbol];
        definitelyMutatingParameters.clear();
        std::map<std::string, std::set<size_t>> definiteSlotAliases;
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            auto* parameter = node->parameters->parameters[i];
            auto* pointer = parameter
                                ? dynamic_cast<PointerTypeNode*>(parameter->type)
                                : nullptr;
            if(pointer && dynamic_cast<PointerTypeNode*>(pointer->elementType))
                definiteSlotAliases[parameter->name].insert(i);
        }
        bool conditionalControlFlow = false;
        bool wroteSlot = false;
        std::function<void(StatementListNode*)> scanDefiniteSlotWrites;
        std::function<void(StatementNode*)> scanDefiniteSlotStatement;
        scanDefiniteSlotWrites = [&](StatementListNode* statements) {
            if(statements)
                for(auto* statement : statements->statements)
                    scanDefiniteSlotStatement(statement);
        };
        scanDefiniteSlotStatement = [&](StatementNode* statement) {
            if(!statement)
                return;
            if(auto* assignment =
                   dynamic_cast<DerefAssignmentNode*>(statement))
            {
                auto* identifier =
                    dynamic_cast<IdentifierNode*>(assignment->pointerExpr);
                if(!identifier)
                    return;
                auto aliases = definiteSlotAliases.find(identifier->name);
                if(aliases == definiteSlotAliases.end())
                    return;
                wroteSlot = true;
                if(aliases->second.size() == 1)
                    definitelyMutatingParameters.insert(
                        aliases->second.begin(), aliases->second.end());
                return;
            }
            if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                definiteSlotAliases[declaration->name] =
                    collectPointerSlotAliases(declaration->expression,
                                              definiteSlotAliases);
            }
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            {
                definiteSlotAliases[declaration->name] =
                    collectPointerSlotAliases(declaration->initExpr,
                                              definiteSlotAliases);
            }
            else if(auto* assignment =
                        dynamic_cast<AssignmentNode*>(statement))
            {
                auto aliases = collectPointerSlotAliases(
                    assignment->expression, definiteSlotAliases);
                definiteSlotAliases.erase(assignment->name);
                if(!aliases.empty())
                    definiteSlotAliases[assignment->name] =
                        std::move(aliases);
            }
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
            {
                scanDefiniteSlotWrites(block->statements);
                return;
            }
            if(dynamic_cast<IfNode*>(statement) ||
               dynamic_cast<CexprIfNode*>(statement) ||
               dynamic_cast<WhileNode*>(statement) ||
               dynamic_cast<ForNode*>(statement) ||
               dynamic_cast<TryCatchNode*>(statement))
            {
                conditionalControlFlow = true;
                return;
            }
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
            {
                if(!wroteSlot)
                    conditionalControlFlow = true;
                (void)returned;
                return;
            }
            if(auto* expression =
                   dynamic_cast<ExpressionStatementNode*>(statement))
            {
                auto* call =
                    dynamic_cast<FunctionCallNode*>(expression->expression);
                if(!call)
                    return;
                std::set<size_t> commonWrites;
                bool foundCandidate = false;
                for(const auto& calledName :
                    getFunctionItemTargetsForName(call->name))
                {
                    auto overloads = functionOverloads.find(calledName);
                    if(overloads == functionOverloads.end())
                        continue;
                    for(const auto& overload : overloads->second)
                    {
                        auto summary =
                            functionPointerSlotDefiniteMutationParameters.find(
                                overload.symbolName);
                        const std::set<size_t> candidateWrites =
                            summary ==
                                    functionPointerSlotDefiniteMutationParameters.end()
                                ? std::set<size_t>{}
                                : summary->second;
                        if(!foundCandidate)
                        {
                            commonWrites = candidateWrites;
                            foundCandidate = true;
                        }
                        else
                        {
                            std::set<size_t> intersection;
                            std::set_intersection(
                                commonWrites.begin(), commonWrites.end(),
                                candidateWrites.begin(), candidateWrites.end(),
                                std::inserter(intersection,
                                              intersection.begin()));
                            commonWrites = std::move(intersection);
                        }
                    }
                }
                for(size_t parameterIndex : commonWrites)
                {
                    if(parameterIndex >= call->arguments.size())
                        continue;
                    ExpressionNode* argument = call->arguments[parameterIndex];
                    if(auto* address = dynamic_cast<UnaryOpNode*>(argument);
                       address && address->op == UnaryOpNode::OP_ADDR_MUT)
                        argument = address->operand;
                    auto aliases = collectPointerSlotAliases(
                        argument, definiteSlotAliases);
                    if(aliases.size() != 1)
                        continue;
                    wroteSlot = true;
                    definitelyMutatingParameters.insert(aliases.begin(),
                                                         aliases.end());
                }
            }
        };
        scanDefiniteSlotWrites(node->body);
        if(conditionalControlFlow)
            definitelyMutatingParameters.clear();
    }

    if(function && node->body && node->returnType &&
       node->returnType->kind == TypeNode::TYPE_PTR && node->parameters)
    {
        const std::string symbol = function->getName().str();
        functionPointerReturnSummaryComplete.erase(symbol);
        bool hasUnknownReturnOrigin = false;
        std::map<std::string, std::set<size_t>> pointerAliases;
        std::map<std::string, std::set<std::string>> pointerLocalSlots;
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
            if(auto* index = dynamic_cast<IndexExpressionNode*>(current))
            {
                if(!getFieldPath(index->base, path))
                    return false;
                int64_t constantIndex = 0;
                if(!evaluateCompileTimeInt(index->index, constantIndex))
                    return false;
                path += "[" + std::to_string(constantIndex) + "]";
                return true;
            }
            if(auto* tuple = dynamic_cast<TupleAccessNode*>(current))
            {
                if(!getFieldPath(tuple->tuple, path))
                    return false;
                path += "." + std::to_string(tuple->index);
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
            if(dynamic_cast<FieldAccessNode*>(expression) ||
               dynamic_cast<IndexExpressionNode*>(expression) ||
               dynamic_cast<TupleAccessNode*>(expression))
            {
                std::string path;
                if(getFieldPath(expression, path))
                {
                    auto alias = pointerFieldAliases.find(path);
                    if(alias != pointerFieldAliases.end())
                        return alias->second;
                }
                if(auto* index =
                       dynamic_cast<IndexExpressionNode*>(expression))
                {
                    std::string basePath;
                    if(getFieldPath(index->base, basePath))
                    {
                        const std::string elementPrefix = basePath + "[";
                        std::set<size_t> possibleOrigins;
                        for(const auto& alias : pointerFieldAliases)
                        {
                            if(alias.first.compare(0, elementPrefix.size(),
                                                   elementPrefix) != 0)
                                continue;
                            const auto close = alias.first.find(
                                ']', elementPrefix.size());
                            if(close == std::string::npos ||
                               close + 1 != alias.first.size())
                                continue;
                            possibleOrigins.insert(alias.second.begin(),
                                                   alias.second.end());
                        }
                        if(!possibleOrigins.empty())
                            return possibleOrigins;
                    }
                }
                // Untracked aggregate paths may carry any pointer argument
                // and must stay conservative.
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

        auto isPointerFieldAliasWithin = [](const std::string& path,
                                            const std::string& candidate) {
            return candidate.size() > path.size() &&
                   candidate.compare(0, path.size(), path) == 0 &&
                   (candidate[path.size()] == '.' ||
                    candidate[path.size()] == '[');
        };
        auto clearPointerFieldAliasSubtree = [&](const std::string& path) {
            for(auto alias = pointerFieldAliases.begin();
                alias != pointerFieldAliases.end();)
                if(alias->first == path ||
                   isPointerFieldAliasWithin(path, alias->first))
                    alias = pointerFieldAliases.erase(alias);
                else
                    ++alias;
        };
        auto copyPointerFieldAliases = [&](const std::string& source,
                                           const std::string& destination,
                                           auto& output) {
            bool copiedAny = false;
            for(const auto& alias : pointerFieldAliases)
                if(isPointerFieldAliasWithin(source, alias.first))
                {
                    output[destination + alias.first.substr(source.size())] =
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
                    if(auto* literal =
                           dynamic_cast<ListLiteralNode*>(expression))
                    {
                        if(literal->elements)
                            for(size_t i = 0;
                                i < literal->elements->elements.size(); ++i)
                                collectPointerFieldAliases(
                                    path + "[" + std::to_string(i) + "]",
                                    literal->elements->elements[i], output);
                        return;
                    }
                    if(auto* literal =
                           dynamic_cast<TupleLiteralNode*>(expression))
                    {
                        if(literal->elements)
                            for(size_t i = 0;
                                i < literal->elements->elements.size(); ++i)
                                collectPointerFieldAliases(
                                    path + "." + std::to_string(i),
                                    literal->elements->elements[i], output);
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
            return std::any_of(
                pointerFieldAliases.begin(), pointerFieldAliases.end(),
                [&](const auto& alias) {
                    return isPointerFieldAliasWithin(path, alias.first);
                });
        };
        auto shouldTrackAggregateAliases = [&](const std::string& path,
                                               ExpressionNode* expression) {
            if(dynamic_cast<StructLiteralNode*>(expression) ||
               dynamic_cast<ListLiteralNode*>(expression) ||
               dynamic_cast<TupleLiteralNode*>(expression) ||
               hasPointerFieldAliases(path))
                return true;
            std::string sourcePath;
            if(getFieldPath(expression, sourcePath))
                return hasPointerFieldAliases(sourcePath);
            return false;
        };
        auto hasTrackedPointerAggregateAtOrBelow =
            [&](const std::string& path) {
                return std::any_of(
                    pointerFieldAliases.begin(), pointerFieldAliases.end(),
                    [&](const auto& alias) {
                        return alias.first == path ||
                               isPointerFieldAliasWithin(path, alias.first);
                    });
            };
        auto recordPointerLocalSlot = [&](const std::string& name,
                                          ExpressionNode* expression,
                                          bool declaration) {
            if(auto* address = dynamic_cast<UnaryOpNode*>(expression))
            {
                if(address->op != UnaryOpNode::OP_ADDR_MUT)
                {
                    if(!declaration && pointerLocalSlots.count(name))
                        pointerLocalSlots[name] = {""};
                    return;
                }
                std::string targetPath;
                if(!getFieldPath(address->operand, targetPath))
                    targetPath.clear();
                pointerLocalSlots[name] = {std::move(targetPath)};
                return;
            }
            if(auto* identifier = dynamic_cast<IdentifierNode*>(expression))
            {
                auto source = pointerLocalSlots.find(identifier->name);
                if(source != pointerLocalSlots.end())
                {
                    pointerLocalSlots[name] = source->second;
                    return;
                }
            }
            if(!declaration && pointerLocalSlots.count(name))
                pointerLocalSlots[name] = {""};
        };
        auto recordPointerSlotsInAggregate =
            [&](const std::string& path, ExpressionNode* expression) {
                std::map<std::string, std::set<std::string>> replacement;
                std::function<void(const std::string&, ExpressionNode*)>
                    collect;
                collect = [&](const std::string& target,
                              ExpressionNode* value) {
                    if(!value)
                        return;
                    if(auto* address = dynamic_cast<UnaryOpNode*>(value))
                    {
                        if(address->op != UnaryOpNode::OP_ADDR_MUT)
                            return;
                        std::string borrowedPath;
                        if(!getFieldPath(address->operand, borrowedPath))
                            borrowedPath.clear();
                        replacement[target] = {std::move(borrowedPath)};
                    }
                    else if(auto* literal =
                                dynamic_cast<StructLiteralNode*>(value))
                    {
                        for(const auto& field : literal->fields)
                            collect(target + "." + field.first, field.second);
                    }
                    else if(auto* literal =
                                dynamic_cast<TupleLiteralNode*>(value))
                    {
                        if(literal->elements)
                            for(size_t i = 0;
                                i < literal->elements->elements.size(); ++i)
                                collect(target + "." + std::to_string(i),
                                        literal->elements->elements[i]);
                    }
                    else if(auto* literal =
                                dynamic_cast<ListLiteralNode*>(value))
                    {
                        if(literal->elements)
                            for(size_t i = 0;
                                i < literal->elements->elements.size(); ++i)
                                collect(target + "[" + std::to_string(i) + "]",
                                        literal->elements->elements[i]);
                    }
                    else if(auto* identifier =
                                dynamic_cast<IdentifierNode*>(value))
                    {
                        const std::string source = identifier->name;
                        for(const auto& slot : pointerLocalSlots)
                            if(slot.first == source ||
                               isPointerFieldAliasWithin(source, slot.first))
                            {
                                const std::string suffix =
                                    slot.first.substr(source.size());
                                replacement[target + suffix] = slot.second;
                            }
                    }
                };
                collect(path, expression);
                for(auto slot = pointerLocalSlots.begin();
                    slot != pointerLocalSlots.end();)
                    if(isPointerFieldAliasWithin(path, slot->first))
                        slot = pointerLocalSlots.erase(slot);
                    else
                        ++slot;
                pointerLocalSlots.insert(replacement.begin(),
                                         replacement.end());
            };
        std::map<std::string, bool> closureMayMutatePointerAggregate;
        std::function<void(ExpressionNode*)> scanCallEffects;
        std::function<void(StatementNode*)> scanClosureEffects;
        std::function<void(StatementListNode*)> scanClosureList;
        scanClosureList = [&](StatementListNode* statements) {
            if(statements)
                for(auto* statement : statements->statements)
                    scanClosureEffects(statement);
        };
        scanClosureEffects = [&](StatementNode* statement) {
            if(!statement)
                return;
            if(auto* assignment = dynamic_cast<AssignmentNode*>(statement))
            {
                if(pointerAliases.count(assignment->name) ||
                   hasTrackedPointerAggregateAtOrBelow(assignment->name))
                    hasUnknownReturnOrigin = true;
                scanCallEffects(assignment->expression);
            }
            else if(auto* assignment =
                        dynamic_cast<FieldAssignmentNode*>(statement))
            {
                std::string path;
                bool hasPath = false;
                if(assignment->target)
                    hasPath = getFieldPath(assignment->target, path);
                else if(!assignment->structName.empty() &&
                        !assignment->fieldName.empty())
                {
                    path = assignment->structName + "." +
                           assignment->fieldName;
                    hasPath = true;
                }
                if(hasPath && hasTrackedPointerAggregateAtOrBelow(path))
                    hasUnknownReturnOrigin = true;
                else if(!hasPath)
                    hasUnknownReturnOrigin = true;
                scanCallEffects(assignment->expression);
            }
            else if(auto* returned = dynamic_cast<ReturnNode*>(statement))
                scanCallEffects(returned->expression);
            else if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
                scanCallEffects(declaration->expression);
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
                scanCallEffects(declaration->initExpr);
            else if(auto* expression =
                        dynamic_cast<ExpressionStatementNode*>(statement))
                scanCallEffects(expression->expression);
            else if(auto* assignment =
                        dynamic_cast<DerefAssignmentNode*>(statement))
            {
                std::set<std::string> targetPaths;
                if(auto* address = dynamic_cast<UnaryOpNode*>(
                       assignment->pointerExpr);
                   address && address->op == UnaryOpNode::OP_ADDR_MUT)
                {
                    std::string path;
                    if(getFieldPath(address->operand, path))
                        targetPaths.insert(path);
                    else
                        targetPaths.insert("");
                }
                else if(auto* identifier = dynamic_cast<IdentifierNode*>(
                            assignment->pointerExpr))
                {
                    auto slot = pointerLocalSlots.find(identifier->name);
                    if(slot != pointerLocalSlots.end())
                        targetPaths = slot->second;
                }
                else
                {
                    std::string slotPath;
                    if(getFieldPath(assignment->pointerExpr, slotPath))
                    {
                        auto slot = pointerLocalSlots.find(slotPath);
                        if(slot != pointerLocalSlots.end())
                            targetPaths = slot->second;
                    }
                }
                if(targetPaths.empty() &&
                   !dynamic_cast<IdentifierNode*>(assignment->pointerExpr))
                    hasUnknownReturnOrigin = true;
                for(const auto& path : targetPaths)
                    if(path.empty() || pointerAliases.count(path) ||
                       pointerFieldAliases.count(path) ||
                       hasTrackedPointerAggregateAtOrBelow(path))
                        hasUnknownReturnOrigin = true;
                scanCallEffects(assignment->pointerExpr);
                scanCallEffects(assignment->value);
            }
            else if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                scanClosureList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                scanCallEffects(branch->condition);
                scanClosureList(branch->thenBranch);
                scanClosureEffects(branch->elseIfBranch);
                scanClosureList(branch->elseBranch);
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                scanCallEffects(branch->condition);
                scanClosureList(branch->thenBranch);
                scanClosureEffects(branch->elseIfBranch);
                scanClosureList(branch->elseBranch);
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
            {
                scanCallEffects(loop->condition);
                scanClosureList(loop->body);
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
            {
                scanCallEffects(loop->iterable);
                scanClosureList(loop->body);
            }
            else if(auto* attempt = dynamic_cast<TryCatchNode*>(statement))
            {
                scanClosureEffects(attempt->tryBlock);
                scanClosureEffects(attempt->catchBlock);
            }
        };
        auto closureMutatesPointerAggregate = [&](ClosureNode* closure) {
            const bool previousUnknown = hasUnknownReturnOrigin;
            hasUnknownReturnOrigin = false;
            scanClosureList(closure ? closure->body : nullptr);
            const bool mutates = hasUnknownReturnOrigin;
            hasUnknownReturnOrigin = previousUnknown;
            return mutates;
        };
        auto recordClosureEffect = [&](const std::string& name,
                                       ExpressionNode* expression,
                                       bool replace) {
            bool knownClosure = false;
            bool mutatesAggregate = false;
            if(auto* closure = dynamic_cast<ClosureNode*>(expression))
            {
                knownClosure = true;
                mutatesAggregate =
                    closureMutatesPointerAggregate(closure);
            }
            else if(auto* identifier =
                        dynamic_cast<IdentifierNode*>(expression))
            {
                auto source = closureMayMutatePointerAggregate.find(
                    identifier->name);
                if(source != closureMayMutatePointerAggregate.end())
                {
                    knownClosure = true;
                    mutatesAggregate = source->second;
                }
            }
            if(!knownClosure)
                return;
            auto existing = closureMayMutatePointerAggregate.find(name);
            if(replace || existing == closureMayMutatePointerAggregate.end())
                closureMayMutatePointerAggregate[name] = mutatesAggregate;
            else
                existing->second = existing->second || mutatesAggregate;
        };
        auto argumentMayInvokeMutatingClosure =
            [&](ExpressionNode* argument) {
                if(auto* identifier =
                       dynamic_cast<IdentifierNode*>(argument))
                {
                    auto effect = closureMayMutatePointerAggregate.find(
                        identifier->name);
                    return effect !=
                               closureMayMutatePointerAggregate.end() &&
                           effect->second;
                }
                if(auto* closure = dynamic_cast<ClosureNode*>(argument))
                    return closureMutatesPointerAggregate(closure);
                return false;
            };
        scanCallEffects = [&](ExpressionNode* expression) {
            if(!expression)
                return;
            if(dynamic_cast<ClosureNode*>(expression))
                return;
            auto invalidateMutableAggregateArgument =
                [&](ExpressionNode* argument) {
                    auto* address = dynamic_cast<UnaryOpNode*>(argument);
                    if(!address || address->op != UnaryOpNode::OP_ADDR_MUT)
                        return;
                    std::string path;
                    if(getFieldPath(address->operand, path) &&
                       hasTrackedPointerAggregateAtOrBelow(path))
                        hasUnknownReturnOrigin = true;
                };
            auto callMayMutatePointerSlot = [&](const std::string& name,
                                               size_t argumentIndex) {
                auto named = functionOverloads.find(name);
                if(named == functionOverloads.end())
                    return false;
                for(const auto& overload : named->second)
                {
                    auto summary = functionPointerSlotMutationParameters.find(
                        overload.symbolName);
                    if(summary != functionPointerSlotMutationParameters.end() &&
                       summary->second.count(argumentIndex))
                        return true;
                }
                return false;
            };
            auto invalidateIndirectPointerArgument =
                [&](ExpressionNode* argument, bool mayMutate) {
                    if(!mayMutate)
                        return;
                    bool pointsAtTrackedPointer = false;
                    if(auto* identifier =
                           dynamic_cast<IdentifierNode*>(argument))
                    {
                        pointsAtTrackedPointer =
                            pointerLocalSlots.count(identifier->name) != 0;
                    }
                    else if(auto* address =
                                dynamic_cast<UnaryOpNode*>(argument);
                            address &&
                            address->op == UnaryOpNode::OP_ADDR_MUT)
                    {
                        std::string targetPath;
                        if(!getFieldPath(address->operand, targetPath))
                            pointsAtTrackedPointer = true;
                        else
                            pointsAtTrackedPointer =
                                pointerAliases.count(targetPath) != 0 ||
                                hasTrackedPointerAggregateAtOrBelow(targetPath);
                    }
                    if(!pointsAtTrackedPointer)
                        return;
                    // A callee receiving a pointer-to-pointer may replace a
                    // local alias. Until interprocedural write effects are
                    // summarized, retain the conservative return-origin
                    // fallback rather than returning a stale alias summary.
                    hasUnknownReturnOrigin = true;
                };
            if(auto* call = dynamic_cast<FunctionCallNode*>(expression))
            {
                auto closureEffect =
                    closureMayMutatePointerAggregate.find(call->name);
                if(closureEffect != closureMayMutatePointerAggregate.end() &&
                   closureEffect->second)
                    hasUnknownReturnOrigin = true;
                for(size_t i = 0; i < call->arguments.size(); ++i)
                {
                    auto* argument = call->arguments[i];
                    invalidateMutableAggregateArgument(argument);
                    invalidateIndirectPointerArgument(
                        argument, callMayMutatePointerSlot(call->name, i));
                    if(argumentMayInvokeMutatingClosure(argument))
                        hasUnknownReturnOrigin = true;
                    scanCallEffects(argument);
                }
                return;
            }
            if(auto* method = dynamic_cast<MethodCallNode*>(expression))
            {
                std::string receiverPath;
                if(getFieldPath(method->object, receiverPath) &&
                   hasTrackedPointerAggregateAtOrBelow(receiverPath))
                    hasUnknownReturnOrigin = true;
                scanCallEffects(method->object);
                for(size_t i = 0; i < method->arguments.size(); ++i)
                {
                    auto* argument = method->arguments[i];
                    invalidateIndirectPointerArgument(
                        argument,
                        callMayMutatePointerSlot(method->methodName, i));
                    if(argumentMayInvokeMutatingClosure(argument))
                        hasUnknownReturnOrigin = true;
                    scanCallEffects(argument);
                }
                return;
            }
            if(auto* invoke = dynamic_cast<CallableInvokeNode*>(expression))
            {
                if(auto* callable =
                       dynamic_cast<IdentifierNode*>(invoke->callable))
                {
                    auto closureEffect =
                        closureMayMutatePointerAggregate.find(
                            callable->name);
                    if(closureEffect !=
                           closureMayMutatePointerAggregate.end() &&
                       closureEffect->second)
                        hasUnknownReturnOrigin = true;
                }
                for(size_t i = 0; i < invoke->arguments.size(); ++i)
                {
                    auto* argument = invoke->arguments[i];
                    invalidateMutableAggregateArgument(argument);
                    // A callable value does not yet carry parameter effect
                    // metadata, so pointer-slot arguments remain unknown.
                    invalidateIndirectPointerArgument(argument, true);
                    if(argumentMayInvokeMutatingClosure(argument))
                        hasUnknownReturnOrigin = true;
                    scanCallEffects(argument);
                }
                scanCallEffects(invoke->callable);
                return;
            }
            if(auto* unary = dynamic_cast<UnaryOpNode*>(expression))
                scanCallEffects(unary->operand);
            else if(auto* binary = dynamic_cast<BinaryOpNode*>(expression))
            {
                scanCallEffects(binary->left);
                scanCallEffects(binary->right);
            }
            else if(auto* ternary = dynamic_cast<TernaryNode*>(expression))
            {
                scanCallEffects(ternary->condition);
                scanCallEffects(ternary->trueExpr);
                scanCallEffects(ternary->falseExpr);
            }
            else if(auto* field = dynamic_cast<FieldAccessNode*>(expression))
                scanCallEffects(field->object);
            else if(auto* index = dynamic_cast<IndexExpressionNode*>(expression))
            {
                scanCallEffects(index->base);
                scanCallEffects(index->index);
            }
            else if(auto* tuple = dynamic_cast<TupleAccessNode*>(expression))
                scanCallEffects(tuple->tuple);
            else if(auto* match =
                        dynamic_cast<MatchExpressionNode*>(expression))
            {
                scanCallEffects(match->target);
                for(auto* arm : match->arms)
                    if(arm)
                    {
                        if(arm->pattern)
                            scanCallEffects(arm->pattern->literal);
                        scanCallEffects(arm->expression);
                    }
            }
            else if(auto* cast = dynamic_cast<CastExpressionNode*>(expression))
                scanCallEffects(cast->expression);
            else if(auto* attempt = dynamic_cast<TryExpressionNode*>(expression))
                scanCallEffects(attempt->expression);
            else if(auto* cexpr = dynamic_cast<CexprExpressionNode*>(expression))
                scanCallEffects(cexpr->expression);
            else if(auto* fold = dynamic_cast<FoldExpressionNode*>(expression))
                scanCallEffects(fold->packExpr);
            else if(auto* update =
                        dynamic_cast<UpdateExpressionNode*>(expression))
                scanCallEffects(update->operand);
            else if(auto* range = dynamic_cast<RangeExpressionNode*>(expression))
            {
                scanCallEffects(range->start);
                scanCallEffects(range->end);
            }
            else if(auto* structure =
                        dynamic_cast<StructLiteralNode*>(expression))
                for(const auto& field : structure->fields)
                    scanCallEffects(field.second);
            else if(auto* list = dynamic_cast<ListLiteralNode*>(expression))
            {
                if(list->elements)
                    for(auto* element : list->elements->elements)
                        scanCallEffects(element);
            }
            else if(auto* tuple = dynamic_cast<TupleLiteralNode*>(expression))
            {
                if(tuple->elements)
                    for(auto* element : tuple->elements->elements)
                        scanCallEffects(element);
            }
            else if(auto* map = dynamic_cast<MapLiteralNode*>(expression))
            {
                if(map->entries)
                    for(auto* entry : map->entries->entries)
                        if(entry)
                        {
                            scanCallEffects(entry->key);
                            scanCallEffects(entry->value);
                        }
            }
            else if(auto* array = dynamic_cast<ArrayFillNode*>(expression))
            {
                scanCallEffects(array->value);
                scanCallEffects(array->count);
            }
            else if(auto* iterator = dynamic_cast<MapIteratorNode*>(expression))
                scanCallEffects(iterator->mapExpr);
            else if(auto* assembly = dynamic_cast<InlineAsmNode*>(expression))
                for(auto* argument : assembly->arguments)
                {
                    invalidateMutableAggregateArgument(argument);
                    scanCallEffects(argument);
                }
            else if(auto* format = dynamic_cast<FormatNode*>(expression))
            {
                for(auto* argument : format->arguments)
                    scanCallEffects(argument);
                for(const auto& argument : format->namedArguments)
                    scanCallEffects(argument.second);
            }
            else if(auto* size =
                        dynamic_cast<SizeofExpressionNode*>(expression))
                scanCallEffects(size->expressionTarget);
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
        auto mergePointerSlots = [](const std::vector<std::map<
                                        std::string,
                                        std::set<std::string>>>& branches) {
            std::map<std::string, std::set<std::string>> merged;
            for(const auto& branch : branches)
                for(const auto& slot : branch)
                    merged[slot.first].insert(slot.second.begin(),
                                              slot.second.end());
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
            std::map<std::string, bool> previousClosureEffects;
            std::set<std::string> closureNamesNotPreviouslyPresent;
            std::map<std::string,
                     std::map<std::string, std::set<std::string>>>
                previousPointerLocalSlots;
            std::set<std::string> pointerSlotNamesNotPreviouslyPresent;
            std::set<std::string> shadowedNames;
            auto rememberShadow = [&](const std::string& name) {
                if(!shadowedNames.insert(name).second)
                    return;
                auto previous = pointerAliases.find(name);
                if(previous == pointerAliases.end())
                    aliasesNotPreviouslyPresent.insert(name);
                else
                    previousAliases.emplace(name, previous->second);
                auto& saved = previousFieldAliases[name];
                for(const auto& fieldAlias : pointerFieldAliases)
                    if(isPointerFieldAliasWithin(name, fieldAlias.first))
                        saved.insert(fieldAlias);
                auto previousClosure =
                    closureMayMutatePointerAggregate.find(name);
                if(previousClosure ==
                   closureMayMutatePointerAggregate.end())
                    closureNamesNotPreviouslyPresent.insert(name);
                else
                    previousClosureEffects.emplace(name,
                                                   previousClosure->second);
                auto& savedSlots = previousPointerLocalSlots[name];
                for(const auto& slot : pointerLocalSlots)
                    if(isPointerFieldAliasWithin(name, slot.first))
                        savedSlots.insert(slot);
                if(savedSlots.empty())
                    pointerSlotNamesNotPreviouslyPresent.insert(name);
            };
            bool fallsThrough = true;
            for(auto* statement : statements->statements)
            {
                // Snapshot a shadow at its declaration point so earlier
                // assignments in this block still update the outer binding.
                if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
                {
                    rememberShadow(declaration->name);
                    for(auto alias = pointerFieldAliases.begin();
                        alias != pointerFieldAliases.end();)
                        if(isPointerFieldAliasWithin(declaration->name,
                                                     alias->first))
                            alias = pointerFieldAliases.erase(alias);
                        else
                            ++alias;
                    for(auto slot = pointerLocalSlots.begin();
                        slot != pointerLocalSlots.end();)
                        if(isPointerFieldAliasWithin(declaration->name,
                                                     slot->first))
                            slot = pointerLocalSlots.erase(slot);
                        else
                            ++slot;
                }
                else if(auto* declaration =
                            dynamic_cast<VarDeclNode*>(statement))
                {
                    rememberShadow(declaration->name);
                    for(auto alias = pointerFieldAliases.begin();
                        alias != pointerFieldAliases.end();)
                        if(isPointerFieldAliasWithin(declaration->name,
                                                     alias->first))
                            alias = pointerFieldAliases.erase(alias);
                        else
                            ++alias;
                    for(auto slot = pointerLocalSlots.begin();
                        slot != pointerLocalSlots.end();)
                        if(isPointerFieldAliasWithin(declaration->name,
                                                     slot->first))
                            slot = pointerLocalSlots.erase(slot);
                        else
                            ++slot;
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
            for(const auto& name : closureNamesNotPreviouslyPresent)
                closureMayMutatePointerAggregate.erase(name);
            for(const auto& previous : previousClosureEffects)
                closureMayMutatePointerAggregate[previous.first] =
                    previous.second;
            for(const auto& name : pointerSlotNamesNotPreviouslyPresent)
                for(auto slot = pointerLocalSlots.begin();
                    slot != pointerLocalSlots.end();)
                    if(isPointerFieldAliasWithin(name, slot->first))
                        slot = pointerLocalSlots.erase(slot);
                    else
                        ++slot;
            for(const auto& previous : previousPointerLocalSlots)
            {
                for(auto slot = pointerLocalSlots.begin();
                    slot != pointerLocalSlots.end();)
                    if(isPointerFieldAliasWithin(previous.first, slot->first))
                        slot = pointerLocalSlots.erase(slot);
                    else
                        ++slot;
                pointerLocalSlots.insert(previous.second.begin(),
                                         previous.second.end());
            }
            for(const auto& name : shadowedNames)
            {
                for(auto alias = pointerFieldAliases.begin();
                    alias != pointerFieldAliases.end();)
                    if(isPointerFieldAliasWithin(name, alias->first))
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
                scanCallEffects(returned->expression);
            else if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
                scanCallEffects(declaration->expression);
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
                scanCallEffects(declaration->initExpr);
            else if(auto* assignment =
                        dynamic_cast<AssignmentNode*>(statement))
                scanCallEffects(assignment->expression);
            else if(auto* assignment =
                        dynamic_cast<FieldAssignmentNode*>(statement))
                scanCallEffects(assignment->expression);
            else if(auto* assignment =
                        dynamic_cast<DerefAssignmentNode*>(statement))
            {
                scanCallEffects(assignment->pointerExpr);
                scanCallEffects(assignment->value);
            }
            else if(auto* expressionStatement =
                        dynamic_cast<ExpressionStatementNode*>(statement))
                scanCallEffects(expressionStatement->expression);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
                scanCallEffects(branch->condition);
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
                scanCallEffects(branch->condition);
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
                scanCallEffects(loop->condition);
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
                scanCallEffects(loop->iterable);
            else if(auto* print = dynamic_cast<PrintNode*>(statement))
            {
                for(auto* argument : print->arguments)
                    scanCallEffects(argument);
                for(const auto& argument : print->namedArguments)
                    scanCallEffects(argument.second);
            }
            else if(auto* assertion = dynamic_cast<AssertNode*>(statement))
                scanCallEffects(assertion->condition);
            else if(auto* assertion = dynamic_cast<AssertEqNode*>(statement))
            {
                scanCallEffects(assertion->left);
                scanCallEffects(assertion->right);
            }
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
            {
                auto returnedOrigins = origins(returned->expression);
                auto& summary = functionPointerReturnParameters[symbol];
                summary.insert(returnedOrigins.begin(), returnedOrigins.end());
                return false;
            }
            if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                recordPointerLocalSlot(declaration->name,
                                       declaration->expression, true);
                recordPointerSlotsInAggregate(declaration->name,
                                              declaration->expression);
                recordClosureEffect(declaration->name,
                                    declaration->expression, true);
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
                recordPointerLocalSlot(declaration->name,
                                       declaration->initExpr, true);
                recordPointerSlotsInAggregate(declaration->name,
                                              declaration->initExpr);
                recordClosureEffect(declaration->name, declaration->initExpr,
                                    true);
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
                recordPointerLocalSlot(assignment->name,
                                       assignment->expression, false);
                recordPointerSlotsInAggregate(assignment->name,
                                              assignment->expression);
                recordClosureEffect(assignment->name, assignment->expression,
                                    false);
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
                    hasPath = getFieldPath(assignment->target, path);
                else if(!assignment->structName.empty() &&
                        !assignment->fieldName.empty())
                {
                    path = assignment->structName + "." +
                           assignment->fieldName;
                    hasPath = true;
                }
                if(hasPath)
                {
                    recordPointerSlotsInAggregate(path,
                                                  assignment->expression);
                    replacePointerFieldAliases(path, assignment->expression);
                }
                else
                    hasUnknownReturnOrigin = true;
            }
            else if(auto* assignment =
                        dynamic_cast<DerefAssignmentNode*>(statement))
            {
                std::set<std::string> targetPaths;
                if(auto* address = dynamic_cast<UnaryOpNode*>(
                       assignment->pointerExpr);
                   address && address->op == UnaryOpNode::OP_ADDR_MUT)
                {
                    std::string path;
                    if(getFieldPath(address->operand, path))
                        targetPaths.insert(path);
                    else
                        targetPaths.insert("");
                }
                else if(auto* identifier =
                            dynamic_cast<IdentifierNode*>(
                                assignment->pointerExpr))
                {
                    auto slot = pointerLocalSlots.find(identifier->name);
                    if(slot != pointerLocalSlots.end())
                        targetPaths = slot->second;
                }
                if(targetPaths.empty())
                    hasUnknownReturnOrigin = true;
                else if(targetPaths.size() != 1 ||
                        targetPaths.count("") != 0)
                    hasUnknownReturnOrigin = true;
                else
                {
                    const auto& targetPath = *targetPaths.begin();
                    auto values = origins(assignment->value);
                    if(pointerAliases.count(targetPath))
                    {
                        if(values.empty())
                            pointerAliases.erase(targetPath);
                        else
                            pointerAliases[targetPath] = std::move(values);
                    }
                    else if(pointerFieldAliases.count(targetPath) ||
                            hasTrackedPointerAggregateAtOrBelow(targetPath))
                        replacePointerFieldAliases(targetPath,
                                                   assignment->value);
                }
            }
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                return scanList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                const auto basePointerSlots = pointerLocalSlots;
                std::vector<std::map<std::string, std::set<size_t>>> branches;
                std::vector<std::map<std::string, std::set<size_t>>>
                    fieldBranches;
                std::vector<std::map<std::string, std::set<std::string>>>
                    pointerSlotBranches;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                if(scanList(branch->thenBranch))
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                    pointerSlotBranches.push_back(pointerLocalSlots);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                const bool elseIfFallsThrough =
                    scanStatement(branch->elseIfBranch);
                if(branch->elseIfBranch && elseIfFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                    pointerSlotBranches.push_back(pointerLocalSlots);
                }
                else if(!branch->elseBranch)
                {
                    branches.push_back(baseAliases);
                    fieldBranches.push_back(baseFieldAliases);
                    pointerSlotBranches.push_back(basePointerSlots);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                const bool elseFallsThrough = scanList(branch->elseBranch);
                if(branch->elseBranch && elseFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                    pointerSlotBranches.push_back(pointerLocalSlots);
                }
                if(branches.empty())
                    return false;
                pointerAliases = mergeAliases(branches);
                pointerFieldAliases = mergeAliases(fieldBranches);
                pointerLocalSlots = mergePointerSlots(pointerSlotBranches);
                return true;
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                const auto basePointerSlots = pointerLocalSlots;
                std::vector<std::map<std::string, std::set<size_t>>> branches;
                std::vector<std::map<std::string, std::set<size_t>>>
                    fieldBranches;
                std::vector<std::map<std::string, std::set<std::string>>>
                    pointerSlotBranches;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                if(scanList(branch->thenBranch))
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                    pointerSlotBranches.push_back(pointerLocalSlots);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                const bool elseIfFallsThrough =
                    scanStatement(branch->elseIfBranch);
                if(branch->elseIfBranch && elseIfFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                    pointerSlotBranches.push_back(pointerLocalSlots);
                }
                else if(!branch->elseBranch)
                {
                    branches.push_back(baseAliases);
                    fieldBranches.push_back(baseFieldAliases);
                    pointerSlotBranches.push_back(basePointerSlots);
                }
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                const bool elseFallsThrough = scanList(branch->elseBranch);
                if(branch->elseBranch && elseFallsThrough)
                {
                    branches.push_back(pointerAliases);
                    fieldBranches.push_back(pointerFieldAliases);
                    pointerSlotBranches.push_back(pointerLocalSlots);
                }
                if(branches.empty())
                    return false;
                pointerAliases = mergeAliases(branches);
                pointerFieldAliases = mergeAliases(fieldBranches);
                pointerLocalSlots = mergePointerSlots(pointerSlotBranches);
                return true;
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                const auto basePointerSlots = pointerLocalSlots;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                const bool bodyFallsThrough = scanList(loop->body);
                if(!bodyFallsThrough)
                {
                    pointerAliases = baseAliases;
                    pointerFieldAliases = baseFieldAliases;
                    pointerLocalSlots = basePointerSlots;
                }
                pointerAliases = mergeAliases({baseAliases, pointerAliases});
                pointerFieldAliases = mergeAliases(
                    {baseFieldAliases, pointerFieldAliases});
                pointerLocalSlots = mergePointerSlots(
                    {basePointerSlots, pointerLocalSlots});
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                const auto baseFieldAliases = pointerFieldAliases;
                const auto basePointerSlots = pointerLocalSlots;
                pointerAliases = baseAliases;
                pointerFieldAliases = baseFieldAliases;
                pointerLocalSlots = basePointerSlots;
                const bool bodyFallsThrough = scanList(loop->body);
                if(!bodyFallsThrough)
                {
                    pointerAliases = baseAliases;
                    pointerFieldAliases = baseFieldAliases;
                    pointerLocalSlots = basePointerSlots;
                }
                pointerAliases = mergeAliases({baseAliases, pointerAliases});
                pointerFieldAliases = mergeAliases(
                    {baseFieldAliases, pointerFieldAliases});
                pointerLocalSlots = mergePointerSlots(
                    {basePointerSlots, pointerLocalSlots});
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
