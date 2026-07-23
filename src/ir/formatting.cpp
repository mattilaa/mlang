#include "ir.h"

#include <algorithm>
#include <cctype>

std::string CodeGenerator::convertFormatString(
    const std::string& mlaFormat, const std::vector<ExpressionNode*>& args,
    const std::vector<std::pair<std::string, ExpressionNode*>>& namedArgs,
    std::vector<llvm::Value*>& argValues, int line)
{
    std::string cFormat;
    size_t argIndex = 0;

    auto trim = [](std::string s)
    {
        size_t start = 0;
        while(start < s.size() &&
              std::isspace(static_cast<unsigned char>(s[start])))
            ++start;
        size_t end = s.size();
        while(end > start &&
              std::isspace(static_cast<unsigned char>(s[end - 1])))
            --end;
        return s.substr(start, end - start);
    };

    auto findNamedArgument = [&](const std::string& name) -> ExpressionNode*
    {
        for(const auto& namedArg : namedArgs)
        {
            if(namedArg.first == name)
                return namedArg.second;
        }
        return nullptr;
    };

    for(size_t i = 0; i < mlaFormat.size(); ++i)
    {
        if(mlaFormat[i] == '{' && i + 1 < mlaFormat.size() &&
           mlaFormat[i + 1] == '{')
        {
            // Escaped {{ -> {
            cFormat += '{';
            i++;
            continue;
        }
        if(mlaFormat[i] == '}' && i + 1 < mlaFormat.size() &&
           mlaFormat[i + 1] == '}')
        {
            // Escaped }} -> }
            cFormat += '}';
            i++;
            continue;
        }
        if(mlaFormat[i] == '{')
        {
            size_t close = mlaFormat.find('}', i + 1);
            if(close == std::string::npos)
            {
                cFormat += '{';
                continue;
            }

            std::string inside = trim(mlaFormat.substr(i + 1, close - i - 1));
            std::string name;
            std::string spec;

            if(!inside.empty())
            {
                size_t colon = inside.find(':');
                if(colon == std::string::npos)
                {
                    if(inside == "?" || inside == "#?")
                        spec = inside;
                    else
                        name = inside;
                }
                else
                {
                    name = trim(inside.substr(0, colon));
                    spec = trim(inside.substr(colon + 1));
                }
            }

            bool debug = false;
            bool pretty = false;
            bool json = false;
            char align = '\0';
            ExpressionNode* widthExpr = nullptr;
            if(!spec.empty())
            {
                if(spec == "?")
                    debug = true;
                else if(spec == "#?")
                {
                    debug = true;
                    pretty = true;
                }
                else if(spec == "json")
                {
                    json = true;
                }
                else if(spec == "#json")
                {
                    json = true;
                    pretty = true;
                }
                else
                {
                    std::string widthSpec = spec;
                    if(widthSpec[0] == '<' || widthSpec[0] == '>' ||
                       widthSpec[0] == '^')
                    {
                        align = widthSpec[0];
                        widthSpec = trim(widthSpec.substr(1));
                        if(widthSpec.empty())
                        {
                            reportError(line,
                                        "alignment specifier requires a width");
                        }
                        else if(widthSpec.back() == '$')
                        {
                            widthSpec.pop_back();
                            widthSpec = trim(widthSpec);
                            if(widthSpec.empty())
                            {
                                reportError(
                                    line,
                                    "dynamic alignment width name is empty");
                            }
                            else if(std::all_of(widthSpec.begin(),
                                                widthSpec.end(),
                                                [](unsigned char ch)
                                                {
                                                    return std::isalnum(ch) ||
                                                           ch == '_';
                                                }))
                            {
                                widthExpr = findNamedArgument(widthSpec);
                                if(!widthExpr)
                                    widthExpr = new IdentifierNode(widthSpec);
                            }
                            else
                            {
                                reportError(
                                    line,
                                    "unsupported dynamic width specifier: {" +
                                        spec + "}");
                            }
                        }
                        else if(std::all_of(widthSpec.begin(), widthSpec.end(),
                                            [](unsigned char ch)
                                            { return std::isdigit(ch); }))
                        {
                            widthExpr =
                                new IntLiteralNode(std::stoll(widthSpec));
                        }
                        else
                        {
                            reportError(line,
                                        "unsupported format specifier: {" +
                                            spec + "}");
                        }
                    }
                    else
                    {
                        reportError(line, "unsupported format specifier: {" +
                                              spec + "}");
                    }
                }
            }

            ExpressionNode* argExpr = nullptr;
            if(!name.empty())
            {
                argExpr = findNamedArgument(name);
                if(!argExpr)
                    argExpr = new IdentifierNode(name);
            }
            else if(argIndex < args.size())
            {
                argExpr = args[argIndex++];
            }
            else
            {
                cFormat += "{" + inside + "}";
                i = close;
                continue;
            }

            llvm::Value* argVal = generateExpression(argExpr);
            if(argVal)
            {
                if(align != '\0')
                {
                    if(!widthExpr)
                    {
                        reportError(
                            line,
                            "alignment format specifier requires a width");
                    }
                    else
                    {
                        llvm::Value* widthVal = generateExpression(widthExpr);
                        llvm::Value* aligned =
                            buildAlignedString(argVal, widthVal, align, line);
                        cFormat += "%s";
                        argValues.push_back(aligned);
                    }
                }
                else
                {
                    appendFormatValue(argExpr, argVal, debug, pretty, json,
                                      cFormat, argValues, line);
                }
            }

            i = close;
            continue;
        }

        cFormat += mlaFormat[i];
    }

    return cFormat;
}

std::string CodeGenerator::convertFormatString(
    const std::string& mlaFormat, const std::vector<ExpressionNode*>& args,
    std::vector<llvm::Value*>& argValues, int line)
{
    static const std::vector<std::pair<std::string, ExpressionNode*>>
        kNoNamedArgs;
    return convertFormatString(mlaFormat, args, kNoNamedArgs, argValues, line);
}
