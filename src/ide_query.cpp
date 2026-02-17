#include "ide_query.h"

#include <cctype>

namespace mlang::ide
{

bool isIdentifierChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::optional<IdentifierMatch> identifierAt(std::string_view lineText,
                                            int character)
{
    int idx = character;
    if(idx < 0)
        return std::nullopt;
    if(idx > static_cast<int>(lineText.size()))
        idx = static_cast<int>(lineText.size());

    int left = idx;
    while(left > 0 && isIdentifierChar(lineText[left - 1]))
        --left;

    int right = idx;
    while(right < static_cast<int>(lineText.size()) &&
          isIdentifierChar(lineText[right]))
    {
        ++right;
    }

    if(right <= left)
        return std::nullopt;

    IdentifierMatch match;
    match.text = std::string(lineText.substr(left, right - left));
    match.startCharacter = left;
    match.endCharacter = right;
    return match;
}

std::optional<IdentifierMatch>
identifierAt(const std::vector<std::string>& lines, const Position& position)
{
    if(position.line < 0 || position.line >= static_cast<int>(lines.size()))
        return std::nullopt;
    return identifierAt(lines[static_cast<size_t>(position.line)],
                        position.character);
}

std::string identifierPrefixAt(std::string_view lineText, int character)
{
    int idx = character;
    if(idx < 0)
        return {};
    if(idx > static_cast<int>(lineText.size()))
        idx = static_cast<int>(lineText.size());

    int start = idx;
    while(start > 0 && isIdentifierChar(lineText[start - 1]))
        --start;
    return std::string(lineText.substr(start, idx - start));
}

std::vector<Range> findWholeWordMatches(const std::vector<std::string>& lines,
                                        std::string_view word)
{
    std::vector<Range> out;
    if(word.empty())
        return out;

    for(int lineNo = 0; lineNo < static_cast<int>(lines.size()); ++lineNo)
    {
        const std::string& line = lines[static_cast<size_t>(lineNo)];
        size_t searchPos = 0;
        while(true)
        {
            size_t found = line.find(word, searchPos);
            if(found == std::string::npos)
                break;

            const int start = static_cast<int>(found);
            const int end = start + static_cast<int>(word.size());
            const bool leftOk =
                start == 0 || !isIdentifierChar(line[static_cast<size_t>(start - 1)]);
            const bool rightOk =
                end >= static_cast<int>(line.size()) ||
                !isIdentifierChar(line[static_cast<size_t>(end)]);

            if(leftOk && rightOk)
            {
                Range range;
                range.start.line = lineNo;
                range.start.character = start;
                range.end.line = lineNo;
                range.end.character = end;
                out.push_back(range);
            }

            searchPos = found + 1;
        }
    }

    return out;
}

} // namespace mlang::ide
