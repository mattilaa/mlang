#include "incremental_compiler.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

extern int yyparse();
extern int yylineno;
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

namespace
{

char closing_for(char opener)
{
    switch(opener)
    {
    case '{':
        return '}';
    case '[':
        return ']';
    case '(':
        return ')';
    default:
        return '\0';
    }
}

bool matches_pair(char opener, char closer)
{
    return closing_for(opener) == closer;
}

// Build a minimal suffix that can repair common in-progress edits.
std::string recovery_suffix_for(std::string_view text)
{
    enum class ScanState
    {
        Code,
        String,
        LineComment,
        BlockComment
    };

    ScanState state = ScanState::Code;
    std::vector<char> openers;

    for(size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if(state == ScanState::Code)
        {
            if(c == '"')
            {
                state = ScanState::String;
                continue;
            }
            if(c == '/' && i + 1 < text.size() && text[i + 1] == '/')
            {
                state = ScanState::LineComment;
                ++i;
                continue;
            }
            if(c == '/' && i + 1 < text.size() && text[i + 1] == '*')
            {
                state = ScanState::BlockComment;
                ++i;
                continue;
            }

            if(c == '{' || c == '[' || c == '(')
            {
                openers.push_back(c);
            }
            else if(c == '}' || c == ']' || c == ')')
            {
                if(!openers.empty() && matches_pair(openers.back(), c))
                    openers.pop_back();
            }
            continue;
        }

        if(state == ScanState::String)
        {
            if(c == '\\' && i + 1 < text.size())
            {
                ++i;
                continue;
            }
            if(c == '"')
                state = ScanState::Code;
            continue;
        }

        if(state == ScanState::LineComment)
        {
            if(c == '\n')
                state = ScanState::Code;
            continue;
        }

        if(state == ScanState::BlockComment)
        {
            if(c == '*' && i + 1 < text.size() && text[i + 1] == '/')
            {
                state = ScanState::Code;
                ++i;
            }
        }
    }

    std::string suffix;
    if(state == ScanState::String)
        suffix.push_back('"');
    else if(state == ScanState::BlockComment)
        suffix += "*/";

    while(!openers.empty())
    {
        char closer = closing_for(openers.back());
        if(closer != '\0')
            suffix.push_back(closer);
        openers.pop_back();
    }

    return suffix;
}

} // namespace

namespace mlang
{

bool IncrementalCompiler::openDocument(const std::string& uri,
                                       const std::string& path,
                                       const std::string& text, int version)
{
    IncrementalDocument doc;
    doc.uri = uri;
    doc.path = path;
    doc.text = text;
    doc.version = version;
    documents_[uri] = std::move(doc);
    return parseDocument(uri) != nullptr;
}

bool IncrementalCompiler::setDocumentText(const std::string& uri,
                                          const std::string& text,
                                          int version)
{
    auto it = documents_.find(uri);
    if(it == documents_.end())
        return false;

    it->second.text = text;
    if(version >= 0)
        it->second.version = version;
    return parseDocument(uri) != nullptr;
}

bool IncrementalCompiler::applyChanges(
    const std::string& uri, const std::vector<IncrementalTextChange>& changes,
    int version)
{
    auto it = documents_.find(uri);
    if(it == documents_.end())
        return false;

    for(const auto& change : changes)
    {
        if(!applyChangeToText(it->second.text, change))
            return false;
    }

    if(version >= 0)
        it->second.version = version;

    return parseDocument(uri) != nullptr;
}

void IncrementalCompiler::closeDocument(const std::string& uri)
{
    documents_.erase(uri);
}

IncrementalDocument* IncrementalCompiler::getDocument(const std::string& uri)
{
    auto it = documents_.find(uri);
    if(it == documents_.end())
        return nullptr;
    return &it->second;
}

const IncrementalDocument*
IncrementalCompiler::getDocument(const std::string& uri) const
{
    auto it = documents_.find(uri);
    if(it == documents_.end())
        return nullptr;
    return &it->second;
}

ProgramNode* IncrementalCompiler::parseDocument(const std::string& uri)
{
    auto it = documents_.find(uri);
    if(it == documents_.end())
        return nullptr;

    IncrementalDocument& doc = it->second;
    doc.ast = nullptr;
    doc.parseSuccess = false;
    doc.usedRecovery = false;
    doc.recoveryKind = ParseRecoveryKind::None;

    ProgramNode* parsed = parseText(doc.text);
    if(parsed)
    {
        doc.ast = parsed;
        doc.lastGoodAst = parsed;
        doc.parseSuccess = true;
        return parsed;
    }

    const std::string suffix = recovery_suffix_for(doc.text);
    if(!suffix.empty())
    {
        std::string recoveredText = doc.text;
        recoveredText += suffix;
        parsed = parseText(recoveredText);
        if(parsed)
        {
            doc.ast = parsed;
            doc.lastGoodAst = parsed;
            doc.parseSuccess = true;
            doc.usedRecovery = true;
            doc.recoveryKind = ParseRecoveryKind::DelimiterClosure;
            return parsed;
        }
    }

    if(doc.lastGoodAst)
    {
        doc.ast = doc.lastGoodAst;
        doc.usedRecovery = true;
        doc.recoveryKind = ParseRecoveryKind::LastGoodAst;
        return doc.ast;
    }

    return nullptr;
}

bool IncrementalCompiler::applyChangeToText(std::string& text,
                                            const IncrementalTextChange& change)
{
    if(change.isFullDocumentReplace())
    {
        text = change.text;
        return true;
    }

    const Range& range = *change.range;
    std::optional<size_t> start = lspPositionToOffset(text, range.start);
    std::optional<size_t> end = lspPositionToOffset(text, range.end);
    if(!start || !end || *start > *end || *end > text.size())
        return false;

    text.replace(*start, *end - *start, change.text);
    return true;
}

std::optional<size_t>
IncrementalCompiler::lspPositionToOffset(std::string_view text,
                                         const Position& position)
{
    if(position.line < 0 || position.character < 0)
        return std::nullopt;

    size_t offset = 0;
    int currentLine = 0;
    while(currentLine < position.line)
    {
        size_t nl = text.find('\n', offset);
        if(nl == std::string_view::npos)
            return std::nullopt;
        offset = nl + 1;
        ++currentLine;
    }

    size_t lineEnd = text.find('\n', offset);
    if(lineEnd == std::string_view::npos)
        lineEnd = text.size();

    size_t lineLen = lineEnd - offset;
    if(static_cast<size_t>(position.character) > lineLen)
        return std::nullopt;

    return offset + static_cast<size_t>(position.character);
}

ProgramNode* IncrementalCompiler::parseText(std::string_view text)
{
    ASTNode* savedRoot = programRoot;
    programRoot = nullptr;

    yylineno = 1;
    parseHadError = false;

    YY_BUFFER_STATE buffer =
        yy_scan_bytes(text.data(), static_cast<int>(text.size()));
    int result = yyparse();
    yy_delete_buffer(buffer);

    ProgramNode* parsedProgram = nullptr;
    if(result == 0 && !parseHadError && programRoot)
        parsedProgram = dynamic_cast<ProgramNode*>(programRoot);

    programRoot = savedRoot;
    return parsedProgram;
}

} // namespace mlang
