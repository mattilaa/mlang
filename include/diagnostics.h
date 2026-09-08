#ifndef MLANG_DIAGNOSTICS_H
#define MLANG_DIAGNOSTICS_H

#include <cctype>
#include <ostream>
#include <string>
#include <utility>

namespace mlang::diag {

struct ParserDiagnostic
{
    int line = 0;
    int column = 0;
    std::string message;
};

inline thread_local bool parser_diagnostic_capture_enabled = false;
inline thread_local ParserDiagnostic captured_parser_diagnostic;

inline void begin_parser_diagnostic_capture()
{
    parser_diagnostic_capture_enabled = true;
    captured_parser_diagnostic = {};
}

inline ParserDiagnostic end_parser_diagnostic_capture()
{
    parser_diagnostic_capture_enabled = false;
    ParserDiagnostic result = std::move(captured_parser_diagnostic);
    captured_parser_diagnostic = {};
    return result;
}

inline bool capture_parser_diagnostic(int line, int column,
                                      const std::string& message)
{
    if(!parser_diagnostic_capture_enabled)
        return false;
    if(captured_parser_diagnostic.message.empty())
        captured_parser_diagnostic = {line, column, message};
    return true;
}

inline std::string docs_page()
{
    return "compiler_diagnostics.html";
}

inline std::string docs_ref(const std::string& code)
{
    return "see docs: " + docs_page() + " (" + code + ")";
}

inline bool contains_text(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

inline std::string classify_warning_code(const std::string& message)
{
    if(contains_text(message, "plain if/else-if with ':'"))
        return "MLANG-W0001";
    if(contains_text(message, "plain while with ':'"))
        return "MLANG-W0002";
    if(contains_text(message, "result.unwrap() may panic"))
        return "MLANG-W0003";
    if(contains_text(message, "empty block"))
        return "MLANG-W0004";
    if(contains_text(message, "implicit zero-initialization"))
        return "MLANG-W0005";
    return "MLANG-W9999";
}

inline std::string classify_error_code(const std::string& message)
{
    if(contains_text(message, "unknown variable"))
        return "MLANG-E2001";
    if(contains_text(message, "type mismatch"))
        return "MLANG-E2002";
    if(contains_text(message, "unknown struct type"))
        return "MLANG-E2003";
    if(contains_text(message, "unknown method"))
        return "MLANG-E2004";
    if(contains_text(message, "division by zero") ||
       contains_text(message, "modulo by zero"))
        return "MLANG-E2005";
    if(contains_text(message, "moved value") ||
       contains_text(message, "cannot move") ||
       contains_text(message, "borrow"))
        return "MLANG-E2006";
    if(contains_text(message, "match "))
        return "MLANG-E2007";
    if(contains_text(message, "missing entry point"))
        return "MLANG-E2008";
    if(contains_text(message, "narrow_cast constant"))
        return "MLANG-E2009";
    if(contains_text(message, "internal error"))
        return "MLANG-E9000";
    return "MLANG-E2999";
}

inline std::string format_message_with_code(const std::string& code,
                                            const std::string& message)
{
    return "[" + code + "] " + message + " [" + docs_ref(code) + "]";
}

inline std::string format_error_message(const std::string& message)
{
    return format_message_with_code(classify_error_code(message), message);
}

inline std::string format_warning_message(const std::string& message)
{
    return format_message_with_code(classify_warning_code(message), message);
}

inline void print_diagnostic_location(std::ostream& os,
                                      const std::string& file,
                                      int line,
                                      int col,
                                      const char* level)
{
    if(line > 0 && col > 0)
        os << file << ":" << line << ":" << col << ": " << level << ": ";
    else if(line > 0)
        os << file << ":" << line << ": " << level << ": ";
    else
        os << file << ": " << level << ": ";
}

} // namespace mlang::diag

#endif
