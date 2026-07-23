#include "ir.h"

#include <string>

namespace
{

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

} // namespace

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
    return node->name + "__" + suffix;
}

void CodeGenerator::registerFunctionOverload(FunctionDefNode* node,
                                             llvm::Function* function)
{
    if(!node)
        return;

    std::string signatureKey = functionSignatureKey(node);
    auto& overloads = functionOverloads[node->name];
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
                return;
            }
            reportError(node->line,
                        "duplicate function overload: '" + signatureKey + "'");
            return;
        }
    }

    FunctionOverloadInfo info{node,
                              function,
                              signatureKey,
                              function ? function->getName().str() : "",
                              node->parameters ? node->parameters->isVarArg
                                               : false,
                              node->isPublic,
                              node->sourceModule};
    overloads.push_back(info);

    functionVisibility[signatureKey] =
        std::make_pair(node->isPublic, node->sourceModule);
}

bool CodeGenerator::isOverloadVisible(const FunctionOverloadInfo& info) const
{
    if(info.sourceModule.empty())
        return true;
    if(is_same_module_family(info.sourceModule, currentModule))
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
