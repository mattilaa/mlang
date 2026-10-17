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
        for(size_t i = 0; i < node->parameters->parameters.size(); ++i)
        {
            auto* parameter = node->parameters->parameters[i];
            if(parameter && dynamic_cast<PointerTypeNode*>(parameter->type))
                pointerAliases[parameter->name].insert(i);
        }

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

        std::function<void(StatementNode*)> scanStatement;
        std::function<void(StatementListNode*)> scanList;
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
                return;
            std::map<std::string, std::set<size_t>> previousAliases;
            std::set<std::string> aliasesNotPreviouslyPresent;
            auto rememberShadow = [&](const std::string& name) {
                if(previousAliases.count(name) != 0 ||
                   aliasesNotPreviouslyPresent.count(name) != 0)
                    return;
                auto previous = pointerAliases.find(name);
                if(previous == pointerAliases.end())
                    aliasesNotPreviouslyPresent.insert(name);
                else
                    previousAliases.emplace(name, previous->second);
            };
            for(auto* statement : statements->statements)
            {
                if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
                    rememberShadow(declaration->name);
                else if(auto* declaration =
                            dynamic_cast<VarDeclNode*>(statement))
                    rememberShadow(declaration->name);
            }
            for(auto* statement : statements->statements)
                scanStatement(statement);
            for(const auto& name : aliasesNotPreviouslyPresent)
                pointerAliases.erase(name);
            for(auto& previous : previousAliases)
                pointerAliases[previous.first] = std::move(previous.second);
        };
        scanStatement = [&](StatementNode* statement) {
            if(auto* returned = dynamic_cast<ReturnNode*>(statement))
            {
                auto returnedOrigins = origins(returned->expression);
                auto& summary = functionPointerReturnParameters[symbol];
                summary.insert(returnedOrigins.begin(), returnedOrigins.end());
                return;
            }
            if(auto* declaration = dynamic_cast<LetDeclNode*>(statement))
            {
                auto values = origins(declaration->expression);
                if(!values.empty())
                    pointerAliases[declaration->name] = std::move(values);
                else
                    pointerAliases.erase(declaration->name);
            }
            else if(auto* declaration = dynamic_cast<VarDeclNode*>(statement))
            {
                auto values = origins(declaration->initExpr);
                if(!values.empty())
                    pointerAliases[declaration->name] = std::move(values);
                else
                    pointerAliases.erase(declaration->name);
            }
            else if(auto* assignment =
                        dynamic_cast<AssignmentNode*>(statement))
            {
                auto values = origins(assignment->expression);
                if(values.empty())
                    pointerAliases.erase(assignment->name);
                else
                    pointerAliases[assignment->name] = std::move(values);
            }
            if(auto* block = dynamic_cast<BlockStatementNode*>(statement))
                scanList(block->statements);
            else if(auto* branch = dynamic_cast<IfNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                std::vector<std::map<std::string, std::set<size_t>>> branches;
                pointerAliases = baseAliases;
                scanList(branch->thenBranch);
                branches.push_back(pointerAliases);
                pointerAliases = baseAliases;
                scanStatement(branch->elseIfBranch);
                if(branch->elseIfBranch)
                    branches.push_back(pointerAliases);
                else if(!branch->elseBranch)
                    branches.push_back(baseAliases);
                pointerAliases = baseAliases;
                scanList(branch->elseBranch);
                if(branch->elseBranch)
                    branches.push_back(pointerAliases);
                pointerAliases = mergeAliases(branches);
            }
            else if(auto* branch = dynamic_cast<CexprIfNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                std::vector<std::map<std::string, std::set<size_t>>> branches;
                pointerAliases = baseAliases;
                scanList(branch->thenBranch);
                branches.push_back(pointerAliases);
                pointerAliases = baseAliases;
                scanStatement(branch->elseIfBranch);
                if(branch->elseIfBranch)
                    branches.push_back(pointerAliases);
                else if(!branch->elseBranch)
                    branches.push_back(baseAliases);
                pointerAliases = baseAliases;
                scanList(branch->elseBranch);
                if(branch->elseBranch)
                    branches.push_back(pointerAliases);
                pointerAliases = mergeAliases(branches);
            }
            else if(auto* loop = dynamic_cast<WhileNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                pointerAliases = baseAliases;
                scanList(loop->body);
                pointerAliases = mergeAliases({baseAliases, pointerAliases});
            }
            else if(auto* loop = dynamic_cast<ForNode*>(statement))
            {
                const auto baseAliases = pointerAliases;
                pointerAliases = baseAliases;
                scanList(loop->body);
                pointerAliases = mergeAliases({baseAliases, pointerAliases});
            }
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
