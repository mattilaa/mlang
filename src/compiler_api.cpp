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

struct DocumentSymbol {
    std::string name;
    int kind = 0;
    int line = 0;
    int column = 0;
};

struct DefinitionResult {
    std::string uri;
    DocumentSymbol symbol;
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
#if __cplusplus >= 202002L
    return value.starts_with(prefix);
#else
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
#endif
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

static std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

static std::optional<DocumentSymbol> parseDeclarationLine(std::string_view lineText, int lineNo) {
    size_t i = 0;
    while (i < lineText.size() && (lineText[i] == ' ' || lineText[i] == '\t')) {
        ++i;
    }
    if (i >= lineText.size()) {
        return std::nullopt;
    }

    struct PrefixKind {
        std::string_view prefix;
        int kind;
    };
    static constexpr PrefixKind kDecls[] = {
        {"fn ", 1},
        {"let ", 2},
        {"var ", 2},
        {"struct ", 3},
        {"mod ", 4},
    };

    int kind = 0;
    size_t nameStart = 0;
    for (const PrefixKind& d : kDecls) {
        if (startsWith(lineText.substr(i), d.prefix)) {
            kind = d.kind;
            nameStart = i + d.prefix.size();
            break;
        }
    }
    if (kind == 0 || nameStart >= lineText.size() || !isIdentStart(lineText[nameStart])) {
        return std::nullopt;
    }

    size_t nameEnd = nameStart + 1;
    while (nameEnd < lineText.size() && isIdentContinue(lineText[nameEnd])) {
        ++nameEnd;
    }

    DocumentSymbol sym;
    sym.name = std::string(lineText.substr(nameStart, nameEnd - nameStart));
    sym.kind = kind;
    sym.line = lineNo;
    sym.column = static_cast<int>(nameStart) + 1;
    return sym;
}

static std::vector<DocumentSymbol> computeDocumentSymbols(std::string_view text) {
    std::vector<DocumentSymbol> out;
    const std::vector<std::string_view> lines = splitLines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::optional<DocumentSymbol> decl =
            parseDeclarationLine(lines[i], static_cast<int>(i) + 1);
        if (decl.has_value()) {
            out.push_back(*decl);
        }
    }
    return out;
}

static std::optional<DocumentSymbol> findDefinition(std::string_view text, int line, int column) {
    const std::optional<size_t> offset = offsetFromLineColumn(text, line, column);
    if (!offset.has_value()) {
        return std::nullopt;
    }
    const std::optional<std::string> token = tokenAtOffset(text, *offset);
    if (!token.has_value()) {
        return std::nullopt;
    }

    const std::vector<DocumentSymbol> symbols = computeDocumentSymbols(text);
    for (const DocumentSymbol& sym : symbols) {
        if (sym.name == *token) {
            return sym;
        }
    }
    return std::nullopt;
}

static std::string uriToPath(std::string_view uri) {
    const size_t pos = uri.find("://");
    if (pos == std::string_view::npos) {
        return std::string(uri);
    }
    return std::string(uri.substr(pos + 3));
}

static bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
}

static std::string modulePathFromName(std::string_view modName) {
    std::string out;
    out.reserve(modName.size() + 4);
    for (size_t i = 0; i < modName.size();) {
        if (i + 1 < modName.size() && modName[i] == ':' && modName[i + 1] == ':') {
            out.push_back('/');
            i += 2;
            continue;
        }
        out.push_back(modName[i]);
        ++i;
    }
    out += ".mla";
    return out;
}

static std::string moduleLeafFromName(std::string_view modName) {
    const size_t pos = modName.rfind("::");
    if (pos == std::string_view::npos) {
        return std::string(modName);
    }
    return std::string(modName.substr(pos + 2));
}

static std::vector<std::string> parseModDeclarations(std::string_view text) {
    std::vector<std::string> mods;
    const auto lines = splitLines(text);
    for (const std::string_view line : lines) {
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i + 4 > line.size() || line.substr(i, 4) != "mod ") {
            continue;
        }
        i += 4;
        size_t start = i;
        while (i < line.size()) {
            const char ch = line[i];
            const bool ident = isIdentContinue(ch);
            const bool sep = (ch == ':' && i + 1 < line.size() && line[i + 1] == ':');
            if (ident) {
                ++i;
                continue;
            }
            if (sep) {
                i += 2;
                continue;
            }
            break;
        }
        if (i > start) {
            mods.emplace_back(line.substr(start, i - start));
        }
    }
    return mods;
}

static bool uriMatchesModule(std::string_view uri, std::string_view modName) {
    const std::string path = uriToPath(uri);
    const std::string modPath = modulePathFromName(modName);
    if (endsWith(path, "/" + modPath) || endsWith(path, modPath)) {
        return true;
    }
    const std::string leaf = moduleLeafFromName(modName);
    return endsWith(path, "/" + leaf + ".mla") || endsWith(path, leaf + ".mla");
}

static std::optional<DefinitionResult> findDefinitionAcrossDocuments(
    std::string_view current_uri,
    std::string_view current_text,
    const std::vector<DocumentState>& all_docs,
    int line,
    int column) {
    const std::optional<size_t> offset = offsetFromLineColumn(current_text, line, column);
    if (!offset.has_value()) {
        return std::nullopt;
    }
    const std::optional<std::string> token = tokenAtOffset(current_text, *offset);
    if (!token.has_value()) {
        return std::nullopt;
    }

    const std::vector<DocumentSymbol> local = computeDocumentSymbols(current_text);
    for (const DocumentSymbol& sym : local) {
        if (sym.name == *token) {
            return DefinitionResult{std::string(current_uri), sym};
        }
    }

    const std::vector<std::string> mods = parseModDeclarations(current_text);
    if (!mods.empty()) {
        for (const std::string& mod : mods) {
            for (const DocumentState& doc : all_docs) {
                if (doc.uri == current_uri) {
                    continue;
                }
                if (!uriMatchesModule(doc.uri, mod)) {
                    continue;
                }
                const std::vector<DocumentSymbol> syms =
                    computeDocumentSymbols(doc.text);
                for (const DocumentSymbol& sym : syms) {
                    if (sym.name == *token) {
                        return DefinitionResult{doc.uri, sym};
                    }
                }
            }
        }
    }

    return std::nullopt;
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

    std::vector<DocumentState> snapshotDocuments() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DocumentState> out;
        out.reserve(documents_.size());
        for (const auto& kv : documents_) {
            out.push_back(kv.second);
        }
        return out;
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

int __mlang_compiler_document_definition_ex(mlang_compiler_session* session,
                                            const char* uri,
                                            int line,
                                            int column,
                                            int* out_line,
                                            int* out_column,
                                            char* out_name,
                                            int out_name_capacity,
                                            int* out_name_length,
                                            char* out_uri,
                                            int out_uri_capacity,
                                            int* out_uri_length);

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

int __mlang_compiler_document_symbol_count(mlang_compiler_session* session,
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

    const auto symbols = mlang::compiler_api::computeDocumentSymbols(*text);
    *out_count = static_cast<int>(symbols.size());
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_symbol_get(mlang_compiler_session* session,
                                         const char* uri,
                                         int index,
                                         char* out_name,
                                         int out_name_capacity,
                                         int* out_name_length,
                                         int* out_kind,
                                         int* out_line,
                                         int* out_column) {
    if (session == nullptr || uri == nullptr || out_name == nullptr || out_name_capacity <= 0 ||
        out_name_length == nullptr || out_kind == nullptr || out_line == nullptr ||
        out_column == nullptr) {
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

    const auto symbols = mlang::compiler_api::computeDocumentSymbols(*text);
    if (index < 0 || static_cast<size_t>(index) >= symbols.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const mlang::compiler_api::DocumentSymbol& sym =
        symbols[static_cast<size_t>(index)];
    *out_kind = sym.kind;
    *out_line = sym.line;
    *out_column = sym.column;
    *out_name_length = static_cast<int>(sym.name.size());

    const size_t copy_len =
        std::min(static_cast<size_t>(out_name_capacity - 1), sym.name.size());
    if (copy_len > 0) {
        std::memcpy(out_name, sym.name.data(), copy_len);
    }
    out_name[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_definition(mlang_compiler_session* session,
                                         const char* uri,
                                         int line,
                                         int column,
                                         int* out_line,
                                         int* out_column,
                                         char* out_name,
                                         int out_name_capacity,
                                         int* out_name_length) {
    char out_uri_dummy[2] = {0, 0};
    int out_uri_length_dummy = 0;
    return __mlang_compiler_document_definition_ex(
        session, uri, line, column, out_line, out_column, out_name, out_name_capacity,
        out_name_length, out_uri_dummy, 2, &out_uri_length_dummy);
}

int __mlang_compiler_document_definition_ex(mlang_compiler_session* session,
                                            const char* uri,
                                            int line,
                                            int column,
                                            int* out_line,
                                            int* out_column,
                                            char* out_name,
                                            int out_name_capacity,
                                            int* out_name_length,
                                            char* out_uri,
                                            int out_uri_capacity,
                                            int* out_uri_length) {
    if (session == nullptr || uri == nullptr || out_line == nullptr ||
        out_column == nullptr || out_name == nullptr || out_name_capacity <= 0 ||
        out_name_length == nullptr || out_uri == nullptr || out_uri_capacity <= 0 ||
        out_uri_length == nullptr) {
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

    const std::vector<mlang::compiler_api::DocumentState> docs = store->snapshotDocuments();
    const std::optional<mlang::compiler_api::DefinitionResult> def =
        mlang::compiler_api::findDefinitionAcrossDocuments(uri, *text, docs, line, column);
    if (!def.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }

    *out_line = def->symbol.line;
    *out_column = def->symbol.column;
    *out_name_length = static_cast<int>(def->symbol.name.size());
    const size_t copy_len =
        std::min(static_cast<size_t>(out_name_capacity - 1), def->symbol.name.size());
    if (copy_len > 0) {
        std::memcpy(out_name, def->symbol.name.data(), copy_len);
    }
    out_name[copy_len] = '\0';

    *out_uri_length = static_cast<int>(def->uri.size());
    const size_t uri_copy_len =
        std::min(static_cast<size_t>(out_uri_capacity - 1), def->uri.size());
    if (uri_copy_len > 0) {
        std::memcpy(out_uri, def->uri.data(), uri_copy_len);
    }
    out_uri[uri_copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

}  // extern "C"
