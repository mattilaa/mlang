#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mlang::compiler_api {

enum class Status : int {
    Ok = 0,
    InvalidArgument = 1,
    InvalidSession = 2,
    Unsupported = 3,
    VersionConflict = 4,
    DocumentNotFound = 5,
    OutOfRange = 6,
    SymbolNotFound = 7,
};

struct DocumentState {
    std::string uri;
    std::string language_id;
    std::string text;
    int version = 0;
};

struct SyntaxDiagnostic {
    int line = 0;
    int column = 0;
    std::string message;
};

static bool isIdentStart(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalpha(uc) != 0 || c == '_';
}

static bool isIdentContinue(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_';
}

static std::optional<size_t> offsetFromLineColumn(std::string_view text, int line, int column) {
    if (line <= 0 || column <= 0) {
        return std::nullopt;
    }

    int cur_line = 1;
    int cur_col = 1;
    for (size_t i = 0; i < text.size(); ++i) {
        if (cur_line == line && cur_col == column) {
            return i;
        }
        if (text[i] == '\n') {
            ++cur_line;
            cur_col = 1;
        } else {
            ++cur_col;
        }
    }

    if (cur_line == line && cur_col == column) {
        return text.size();
    }
    return std::nullopt;
}

static std::optional<std::string> tokenAtOffset(std::string_view text, size_t offset) {
    if (text.empty()) {
        return std::nullopt;
    }
    if (offset >= text.size()) {
        if (offset == 0) {
            return std::nullopt;
        }
        offset = text.size() - 1;
    }

    size_t pos = offset;
    if (!isIdentContinue(text[pos])) {
        if (pos == 0 || !isIdentContinue(text[pos - 1])) {
            return std::nullopt;
        }
        --pos;
    }

    size_t start = pos;
    while (start > 0 && isIdentContinue(text[start - 1])) {
        --start;
    }
    size_t end = pos;
    while (end + 1 < text.size() && isIdentContinue(text[end + 1])) {
        ++end;
    }

    if (!isIdentStart(text[start])) {
        return std::nullopt;
    }
    return std::string(text.substr(start, end - start + 1));
}

static bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

static bool containsDeclForSymbol(std::string_view text, std::string_view symbol) {
    if (symbol.empty()) {
        return false;
    }

    const std::string patterns[] = {
        "fn " + std::string(symbol),
        "let " + std::string(symbol),
        "var " + std::string(symbol),
        "struct " + std::string(symbol),
        "mod " + std::string(symbol),
    };

    for (const std::string& p : patterns) {
        if (text.find(p) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

static std::vector<std::string> extractDeclaredSymbols(std::string_view text) {
    std::vector<std::string> symbols;

    const std::string_view decls[] = {"fn ", "let ", "var ", "struct ", "mod "};
    for (std::string_view decl : decls) {
        size_t pos = 0;
        while (true) {
            pos = text.find(decl, pos);
            if (pos == std::string_view::npos) {
                break;
            }
            const size_t name_start = pos + decl.size();
            if (name_start >= text.size()) {
                break;
            }
            if (!isIdentStart(text[name_start])) {
                pos = name_start;
                continue;
            }
            size_t end = name_start + 1;
            while (end < text.size() && isIdentContinue(text[end])) {
                ++end;
            }
            symbols.emplace_back(text.substr(name_start, end - name_start));
            pos = end;
        }
    }
    return symbols;
}

static std::string completionPrefixAtOffset(std::string_view text, size_t offset) {
    if (offset > text.size()) {
        return {};
    }
    size_t start = offset;
    while (start > 0 && isIdentContinue(text[start - 1])) {
        --start;
    }
    return std::string(text.substr(start, offset - start));
}

static std::vector<std::string> computeCompletions(std::string_view text, size_t offset) {
    static constexpr std::string_view kKeywords[] = {
        "fn", "let", "var", "struct", "mod", "if", "else", "while", "for", "return",
    };

    const std::string prefix = completionPrefixAtOffset(text, offset);
    std::set<std::string> dedup;

    for (std::string_view kw : kKeywords) {
        if (prefix.empty() || startsWith(kw, prefix)) {
            dedup.insert(std::string(kw));
        }
    }

    const std::vector<std::string> symbols = extractDeclaredSymbols(text);
    for (const std::string& sym : symbols) {
        if (prefix.empty() || startsWith(sym, prefix)) {
            dedup.insert(sym);
        }
    }

    std::vector<std::string> out;
    out.reserve(dedup.size());
    for (const std::string& item : dedup) {
        out.push_back(item);
    }
    return out;
}

static std::vector<SyntaxDiagnostic> computeSyntaxDiagnostics(std::string_view text) {
    std::vector<SyntaxDiagnostic> out;
    std::vector<char> delimiters;
    bool in_string = false;
    int line = 1;
    int column = 1;

    auto push_diag = [&out](int l, int c, std::string msg) {
        out.push_back(SyntaxDiagnostic{l, c, std::move(msg)});
    };

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\n') {
            ++line;
            column = 1;
            continue;
        }

        if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
            in_string = !in_string;
            ++column;
            continue;
        }

        if (!in_string) {
            if (ch == '(' || ch == '[' || ch == '{') {
                delimiters.push_back(ch);
            } else if (ch == ')' || ch == ']' || ch == '}') {
                if (delimiters.empty()) {
                    push_diag(line, column, "unmatched closing delimiter");
                } else {
                    const char open = delimiters.back();
                    const bool ok = (open == '(' && ch == ')') ||
                                    (open == '[' && ch == ']') ||
                                    (open == '{' && ch == '}');
                    if (!ok) {
                        push_diag(line, column, "mismatched closing delimiter");
                    } else {
                        delimiters.pop_back();
                    }
                }
            }
        }

        ++column;
    }

    if (in_string) {
        push_diag(line, column, "unterminated string literal");
    }
    if (!delimiters.empty()) {
        push_diag(line, column, "unclosed delimiter");
    }
    return out;
}

class SessionStore {
public:
    bool openDocument(std::string_view uri,
                      std::string_view language_id,
                      std::string_view text,
                      int version) {
        if (uri.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        DocumentState& doc = documents_[std::string(uri)];
        doc.uri.assign(uri.data(), uri.size());
        doc.language_id.assign(language_id.data(), language_id.size());
        doc.text.assign(text.data(), text.size());
        doc.version = version;
        return true;
    }

    Status changeDocument(std::string_view uri, std::string_view text, int version) {
        if (uri.empty()) {
            return Status::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = documents_.find(std::string(uri));
        if (it == documents_.end()) {
            return Status::DocumentNotFound;
        }

        DocumentState& doc = it->second;
        if (version <= doc.version) {
            return Status::VersionConflict;
        }

        doc.text.assign(text.data(), text.size());
        doc.version = version;
        return Status::Ok;
    }

    Status closeDocument(std::string_view uri) {
        if (uri.empty()) {
            return Status::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = documents_.find(std::string(uri));
        if (it == documents_.end()) {
            return Status::DocumentNotFound;
        }

        documents_.erase(it);
        return Status::Ok;
    }

    std::optional<std::string> documentText(std::string_view uri) {
        if (uri.empty()) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = documents_.find(std::string(uri));
        if (it == documents_.end()) {
            return std::nullopt;
        }
        return it->second.text;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, DocumentState> documents_;
};

class GlobalStore {
public:
    static GlobalStore& instance() {
        static GlobalStore store;
        return store;
    }

    std::uint64_t createSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t id = next_session_id_++;
        sessions_[id] = std::make_shared<SessionStore>();
        return id;
    }

    bool destroySession(std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.erase(id) > 0;
    }

    std::shared_ptr<SessionStore> getSession(std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return {};
        }
        return it->second;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<SessionStore>> sessions_;
    std::atomic<std::uint64_t> next_session_id_{1};
};

}  // namespace mlang::compiler_api

extern "C" {

struct mlang_compiler_session {
    std::uint64_t id;
};

int __mlang_compiler_session_create(mlang_compiler_session** out_session) {
    if (out_session == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    auto* session = new mlang_compiler_session{};
    session->id = mlang::compiler_api::GlobalStore::instance().createSession();
    *out_session = session;
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_session_destroy(mlang_compiler_session* session) {
    if (session == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    mlang::compiler_api::GlobalStore::instance().destroySession(session->id);
    delete session;
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_open(mlang_compiler_session* session,
                                   const char* uri,
                                   const char* language_id,
                                   const char* text,
                                   int version) {
    if (session == nullptr || uri == nullptr || language_id == nullptr || text == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const bool ok = store->openDocument(uri, language_id, text, version);
    return ok ? static_cast<int>(mlang::compiler_api::Status::Ok)
              : static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
}

int __mlang_compiler_document_change(mlang_compiler_session* session,
                                     const char* uri,
                                     const char* text,
                                     int version) {
    if (session == nullptr || uri == nullptr || text == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const mlang::compiler_api::Status status = store->changeDocument(uri, text, version);
    return static_cast<int>(status);
}

int __mlang_compiler_document_close(mlang_compiler_session* session, const char* uri) {
    if (session == nullptr || uri == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const mlang::compiler_api::Status status = store->closeDocument(uri);
    return static_cast<int>(status);
}

int __mlang_compiler_document_syntax_diagnostic_count(mlang_compiler_session* session,
                                                      const char* uri,
                                                      int* out_count) {
    if (session == nullptr || uri == nullptr || out_count == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const std::optional<std::string> text = store->documentText(uri);
    if (!text.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }

    const auto diags = mlang::compiler_api::computeSyntaxDiagnostics(*text);
    *out_count = static_cast<int>(diags.size());
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_syntax_diagnostic_get(mlang_compiler_session* session,
                                                    const char* uri,
                                                    int index,
                                                    int* out_line,
                                                    int* out_column,
                                                    char* out_message,
                                                    int out_message_capacity,
                                                    int* out_message_length) {
    if (session == nullptr || uri == nullptr || out_line == nullptr || out_column == nullptr ||
        out_message == nullptr || out_message_capacity <= 0 || out_message_length == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const std::optional<std::string> text = store->documentText(uri);
    if (!text.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }

    const auto diags = mlang::compiler_api::computeSyntaxDiagnostics(*text);
    if (index < 0 || static_cast<size_t>(index) >= diags.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const mlang::compiler_api::SyntaxDiagnostic& diag = diags[static_cast<size_t>(index)];
    *out_line = diag.line;
    *out_column = diag.column;
    *out_message_length = static_cast<int>(diag.message.size());

    const size_t copy_len = std::min(static_cast<size_t>(out_message_capacity - 1), diag.message.size());
    if (copy_len > 0) {
        std::memcpy(out_message, diag.message.data(), copy_len);
    }
    out_message[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_hover(mlang_compiler_session* session,
                                    const char* uri,
                                    int line,
                                    int column,
                                    char* out_message,
                                    int out_message_capacity,
                                    int* out_message_length) {
    if (session == nullptr || uri == nullptr || out_message == nullptr ||
        out_message_capacity <= 0 || out_message_length == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const std::optional<std::string> text = store->documentText(uri);
    if (!text.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }

    const std::optional<size_t> offset = mlang::compiler_api::offsetFromLineColumn(*text, line, column);
    if (!offset.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const std::optional<std::string> token = mlang::compiler_api::tokenAtOffset(*text, *offset);
    if (!token.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }

    const bool declared_here = mlang::compiler_api::containsDeclForSymbol(*text, *token);
    std::string hover = declared_here ? ("symbol: " + *token + " (declared in document)")
                                      : ("symbol: " + *token);

    *out_message_length = static_cast<int>(hover.size());
    const size_t copy_len = std::min(static_cast<size_t>(out_message_capacity - 1), hover.size());
    if (copy_len > 0) {
        std::memcpy(out_message, hover.data(), copy_len);
    }
    out_message[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_completion_count(mlang_compiler_session* session,
                                               const char* uri,
                                               int line,
                                               int column,
                                               int* out_count) {
    if (session == nullptr || uri == nullptr || out_count == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const std::optional<std::string> text = store->documentText(uri);
    if (!text.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }

    const std::optional<size_t> offset = mlang::compiler_api::offsetFromLineColumn(*text, line, column);
    if (!offset.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const auto completions = mlang::compiler_api::computeCompletions(*text, *offset);
    *out_count = static_cast<int>(completions.size());
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_completion_get(mlang_compiler_session* session,
                                             const char* uri,
                                             int line,
                                             int column,
                                             int index,
                                             char* out_item,
                                             int out_item_capacity,
                                             int* out_item_length) {
    if (session == nullptr || uri == nullptr || out_item == nullptr ||
        out_item_capacity <= 0 || out_item_length == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }

    const std::optional<std::string> text = store->documentText(uri);
    if (!text.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }

    const std::optional<size_t> offset = mlang::compiler_api::offsetFromLineColumn(*text, line, column);
    if (!offset.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const auto completions = mlang::compiler_api::computeCompletions(*text, *offset);
    if (index < 0 || static_cast<size_t>(index) >= completions.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const std::string& item = completions[static_cast<size_t>(index)];
    *out_item_length = static_cast<int>(item.size());
    const size_t copy_len = std::min(static_cast<size_t>(out_item_capacity - 1), item.size());
    if (copy_len > 0) {
        std::memcpy(out_item, item.data(), copy_len);
    }
    out_item[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

}  // extern "C"
