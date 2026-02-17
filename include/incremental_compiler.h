#pragma once

#include "ast.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mlang
{

struct Position
{
    int line = 0;
    int character = 0;
};

struct Range
{
    Position start;
    Position end;
};

enum class ParseRecoveryKind
{
    None,
    DelimiterClosure,
    LastGoodAst
};

struct IncrementalTextChange
{
    std::string text;
    std::optional<Range> range;

    bool isFullDocumentReplace() const
    {
        return !range.has_value();
    }
};

struct IncrementalDocument
{
    std::string uri;
    std::string path;
    std::string text;
    int version = -1;
    ProgramNode* ast = nullptr;
    ProgramNode* lastGoodAst = nullptr;
    bool parseSuccess = false;
    bool usedRecovery = false;
    ParseRecoveryKind recoveryKind = ParseRecoveryKind::None;
};

class IncrementalCompiler
{
public:
    bool openDocument(const std::string& uri, const std::string& path,
                      const std::string& text, int version = -1);

    bool setDocumentText(const std::string& uri, const std::string& text,
                         int version = -1);

    bool applyChanges(const std::string& uri,
                      const std::vector<IncrementalTextChange>& changes,
                      int version = -1);

    void closeDocument(const std::string& uri);

    IncrementalDocument* getDocument(const std::string& uri);
    const IncrementalDocument* getDocument(const std::string& uri) const;

    ProgramNode* parseDocument(const std::string& uri);

    static bool applyChangeToText(std::string& text,
                                  const IncrementalTextChange& change);

private:
    static std::optional<size_t> lspPositionToOffset(std::string_view text,
                                                     const Position& position);

    static ProgramNode* parseText(std::string_view text);

    std::unordered_map<std::string, IncrementalDocument> documents_;
};

} // namespace mlang
