#pragma once

#include "incremental_compiler.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mlang::ide
{

struct IdentifierMatch
{
    std::string text;
    int startCharacter = 0;
    int endCharacter = 0;
};

bool isIdentifierChar(char c);

std::optional<IdentifierMatch> identifierAt(std::string_view lineText,
                                            int character);

std::optional<IdentifierMatch>
identifierAt(const std::vector<std::string>& lines, const Position& position);

std::string identifierPrefixAt(std::string_view lineText, int character);

std::vector<Range> findWholeWordMatches(const std::vector<std::string>& lines,
                                        std::string_view word);

} // namespace mlang::ide
