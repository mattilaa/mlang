#include "ast.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

struct SemanticSymbol {
    std::string name;
    int kind = 0; // 1 fn, 2 var/let, 3 struct, 4 mod
    std::string stable_id;
    std::string uri;
    int line = 0;
    int column = 0;
    int depth = 0;
    std::string type_info;
    std::string signature;
};

struct UseDecl {
    std::string module;
    std::string item;
    bool wildcard = false;
};

struct DocumentSemantic {
    std::string uri;
    std::string text;
    std::vector<int> line_depths;
    std::vector<SemanticSymbol> symbols;
    std::vector<std::string> mods;
    std::vector<UseDecl> uses;
    bool ast_valid = false;
};

static std::vector<SyntaxDiagnostic> computeSyntaxDiagnostics(std::string_view text);

static std::string stableSymbolId(const SemanticSymbol& sym) {
    std::string key;
    key.reserve(sym.uri.size() + sym.name.size() + sym.signature.size() + sym.type_info.size() + 64);
    key += sym.uri;
    key += "|";
    key += std::to_string(sym.kind);
    key += "|";
    key += sym.name;
    key += "|";
    key += sym.signature;
    key += "|";
    key += sym.type_info;
    key += "|";
    key += std::to_string(sym.depth);
    key += "|";
    key += std::to_string(sym.line);
    key += "|";
    key += std::to_string(sym.column);

    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : key) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }

    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
    return "sym_" + std::string(hex);
}

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

struct TokenSpan {
    std::string token;
    size_t start = 0;
    size_t end = 0;
};

static std::optional<TokenSpan> tokenSpanAtOffset(std::string_view text, size_t offset) {
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
    TokenSpan out;
    out.token = std::string(text.substr(start, end - start + 1));
    out.start = start;
    out.end = end;
    return out;
}

static bool startsWith(std::string_view value, std::string_view prefix) {
#if __cplusplus >= 202002L
    return value.starts_with(prefix);
#else
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
#endif
}

static std::optional<std::string> parseFnNameInLine(std::string_view line) {
    const size_t fn_pos = line.find("fn ");
    if (fn_pos == std::string_view::npos) {
        return std::nullopt;
    }
    size_t pos = fn_pos + 3;
    while (pos < line.size() &&
           std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
        ++pos;
    }
    if (pos >= line.size() || !isIdentStart(line[pos])) {
        return std::nullopt;
    }
    size_t end = pos + 1;
    while (end < line.size() && isIdentContinue(line[end])) {
        ++end;
    }
    return std::string(line.substr(pos, end - pos));
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

static std::string trimWs(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

static std::string unquote(std::string_view v) {
    std::string t = trimWs(v);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

static std::vector<std::string> splitTomlArray(std::string_view input) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;
    for (char c : input) {
        if (c == '"') {
            in_quotes = !in_quotes;
            cur.push_back(c);
            continue;
        }
        if (c == ',' && !in_quotes) {
            out.push_back(trimWs(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) {
        out.push_back(trimWs(cur));
    }
    return out;
}

static std::vector<std::string> parseModulePathsFromToml(
    const std::filesystem::path& manifest_path) {
    std::ifstream in(manifest_path, std::ios::binary);
    if (!in) {
        return {};
    }

    std::vector<std::string> out;
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        std::string t = trimWs(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if (section != "package" && section != "tool.mlang") {
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trimWs(t.substr(0, eq));
        if (key != "module_paths") {
            continue;
        }
        const std::string value = trimWs(t.substr(eq + 1));
        if (value.empty()) {
            continue;
        }
        if (value.front() == '[' && value.back() == ']') {
            std::string inner = value.substr(1, value.size() - 2);
            for (const auto& part : splitTomlArray(inner)) {
                std::string v = unquote(part);
                if (!v.empty()) {
                    out.push_back(v);
                }
            }
        } else {
            std::string v = unquote(value);
            if (!v.empty()) {
                out.push_back(v);
            }
        }
    }
    return out;
}

static std::optional<std::filesystem::path> findManifestPath(
    std::filesystem::path start_dir) {
    std::error_code ec;
    start_dir = std::filesystem::absolute(start_dir, ec);
    if (ec) {
        return std::nullopt;
    }

    std::filesystem::path cur = start_dir;
    while (!cur.empty()) {
        const auto candidate = cur / "mlang.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        const auto parent = cur.parent_path();
        if (parent == cur) {
            break;
        }
        cur = parent;
    }
    return std::nullopt;
}

static std::vector<std::string> defaultStdlibPaths() {
    std::vector<std::string> paths;
    if (const char* env = std::getenv("MLANG_STDLIB_PATH")) {
        paths.emplace_back(env);
    }
#ifdef MLANG_STDLIB_SOURCE_DIR
    {
        std::error_code ec;
        if (std::filesystem::exists(MLANG_STDLIB_SOURCE_DIR, ec)) {
            paths.emplace_back(MLANG_STDLIB_SOURCE_DIR);
        }
    }
#endif
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        paths.emplace_back(std::string(xdg) + "/mlang/stdlib");
    }
    if (const char* home = std::getenv("HOME")) {
        paths.emplace_back(std::string(home) + "/.local/share/mlang/stdlib");
    }
#ifdef MLANG_STDLIB_INSTALL_DIR
    paths.emplace_back(MLANG_STDLIB_INSTALL_DIR);
#endif
    paths.emplace_back("/usr/local/share/mlang/stdlib");
    paths.emplace_back("/usr/share/mlang/stdlib");
    return paths;
}

static void appendStdlibPaths(std::vector<std::string>& module_paths) {
    std::unordered_set<std::string> seen(module_paths.begin(), module_paths.end());
    for (const auto& p : defaultStdlibPaths()) {
        if (!p.empty() && seen.insert(p).second) {
            module_paths.push_back(p);
        }
    }
}

static std::vector<std::string> moduleSearchPathsForUri(std::string_view uri) {
    namespace fs = std::filesystem;

    std::vector<std::string> out;
    std::error_code ec;
    fs::path p = fs::path(uriToPath(uri));
    fs::path base = p.parent_path();
    if (base.empty()) {
        base = ".";
    }
    fs::path base_abs = fs::absolute(base, ec);
    if (!ec) {
        out.push_back(base_abs.lexically_normal().string());
    } else {
        out.push_back(base.string());
    }

    if (const auto manifest = findManifestPath(base); manifest.has_value()) {
        std::vector<std::string> manifest_paths = parseModulePathsFromToml(*manifest);
        const fs::path manifest_dir = manifest->parent_path();
        for (auto& mp : manifest_paths) {
            fs::path path_mp = fs::path(mp);
            if (!path_mp.is_absolute()) {
                path_mp = manifest_dir / path_mp;
            }
            std::error_code path_ec;
            fs::path abs = fs::absolute(path_mp, path_ec);
            if (!path_ec) {
                out.push_back(abs.lexically_normal().string());
            }
        }
    }

    appendStdlibPaths(out);
    return out;
}

static std::optional<std::string> resolveModuleFilePath(std::string_view requester_uri,
                                                        std::string_view module_name) {
    namespace fs = std::filesystem;
    const std::vector<std::string> roots = moduleSearchPathsForUri(requester_uri);
    const std::string rel_file = modulePathFromName(module_name);

    std::string rel_dir = std::string(module_name);
    for (size_t i = 0; i + 1 < rel_dir.size();) {
        if (rel_dir[i] == ':' && rel_dir[i + 1] == ':') {
            rel_dir.replace(i, 2, "/");
            ++i;
            continue;
        }
        ++i;
    }

    for (const auto& root : roots) {
        std::error_code ec;
        fs::path p1 = fs::path(root) / rel_file;
        if (fs::exists(p1, ec)) {
            fs::path abs = fs::absolute(p1, ec);
            if (!ec) {
                return abs.lexically_normal().string();
            }
            return p1.string();
        }
        ec.clear();
        fs::path p2 = fs::path(root) / rel_dir / "mod.mla";
        if (fs::exists(p2, ec)) {
            fs::path abs = fs::absolute(p2, ec);
            if (!ec) {
                return abs.lexically_normal().string();
            }
            return p2.string();
        }
    }
    return std::nullopt;
}

static std::string fileUriFromPath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(path, ec);
    const std::string p = (!ec ? abs.lexically_normal().generic_string()
                               : std::filesystem::path(path).generic_string());
    if (!p.empty() && p[0] == '/') {
        return "file://" + p;
    }
    return "file:///" + p;
}

static std::vector<int> computeLineDepths(std::string_view text) {
    std::vector<int> depths;
    depths.push_back(0); // line 1 depth
    int depth = 0;
    bool in_string = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
            in_string = !in_string;
        } else if (!in_string) {
            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                depth = std::max(0, depth - 1);
            }
        }

        if (ch == '\n') {
            depths.push_back(depth);
        }
    }
    return depths;
}

static int findIdentifierColumn(std::string_view line, std::string_view name) {
    if (name.empty()) {
        return 0;
    }
    size_t pos = 0;
    while (true) {
        pos = line.find(name, pos);
        if (pos == std::string_view::npos) {
            return 0;
        }
        const bool left_ok =
            pos == 0 || !isIdentContinue(line[pos - 1]);
        const size_t end = pos + name.size();
        const bool right_ok =
            end >= line.size() || !isIdentContinue(line[end]);
        if (left_ok && right_ok) {
            return static_cast<int>(pos) + 1;
        }
        ++pos;
    }
}

static std::string typeToString(TypeNode* type) {
    if (!type) {
        return {};
    }
    return type->toString();
}

static std::string functionSignatureFromAst(FunctionDefNode* fn) {
    if (!fn) {
        return {};
    }
    std::string sig = "fn " + fn->name + "(";
    if (fn->parameters) {
        for (size_t i = 0; i < fn->parameters->parameters.size(); ++i) {
            if (i > 0) {
                sig += ", ";
            }
            ParameterNode* p = fn->parameters->parameters[i];
            sig += p ? p->name : "_";
            sig += ": ";
            sig += (p && p->type) ? p->type->toString() : "_";
        }
    }
    sig += ") -> ";
    sig += fn->returnType ? fn->returnType->toString() : "void";
    return sig;
}

static std::string methodSignatureFromAst(const std::string& owner,
                                          StructMethodNode* method) {
    if (!method) {
        return {};
    }
    std::string sig = "fn " + owner + "::" + method->name + "(";
    if (method->parameters) {
        for (size_t i = 0; i < method->parameters->parameters.size(); ++i) {
            if (i > 0) {
                sig += ", ";
            }
            ParameterNode* p = method->parameters->parameters[i];
            sig += p ? p->name : "_";
            sig += ": ";
            sig += (p && p->type) ? p->type->toString() : "_";
        }
    }
    sig += ") -> ";
    sig += method->returnType ? method->returnType->toString() : "void";
    return sig;
}

static ProgramNode* parseProgramFromText(std::string_view text,
                                         int* out_error_line = nullptr) {
    ASTNode* saved_root = programRoot;
    programRoot = nullptr;

    yylineno = 1;
    parseHadError = false;

    YY_BUFFER_STATE buffer =
        yy_scan_bytes(text.data(), static_cast<yy_size_t>(text.size()));
    const int result = yyparse();
    yy_delete_buffer(buffer);

    ProgramNode* parsed = nullptr;
    if (result == 0 && !parseHadError && programRoot) {
        parsed = dynamic_cast<ProgramNode*>(programRoot);
    } else if (out_error_line != nullptr) {
        *out_error_line = yylineno > 0 ? yylineno : 1;
    }

    programRoot = saved_root;
    return parsed;
}

static std::string closeOpenBracesForRecovery(std::string_view text) {
    int balance = 0;
    bool in_string = false;
    bool escape = false;
    bool line_comment = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (line_comment) {
            if (ch == '\n') {
                line_comment = false;
            }
            continue;
        }
        if (in_string) {
            if (escape) {
                escape = false;
                continue;
            }
            if (ch == '\\') {
                escape = true;
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            line_comment = true;
            ++i;
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            ++balance;
        } else if (ch == '}' && balance > 0) {
            --balance;
        }
    }

    std::string out(text);
    for (int i = 0; i < balance; ++i) {
        out.append("\n}");
    }
    out.push_back('\n');
    return out;
}

static void addSemanticSymbol(DocumentSemantic& out,
                              const std::vector<std::string_view>& lines,
                              std::string name,
                              int kind,
                              int line_no,
                              int depth,
                              std::string type_info,
                              std::string signature) {
    SemanticSymbol s;
    s.name = std::move(name);
    s.kind = kind;
    s.uri = out.uri;
    auto guess_line = [&]() -> int {
        if (s.name.empty()) {
            return 1;
        }
        int best_line = -1;
        int best_depth_delta = 1 << 30;
        int best_line_delta = 1 << 30;
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string_view line = lines[i];
            bool kind_match = false;
            switch (kind) {
            case 1:
                kind_match = line.find("fn " + s.name) != std::string_view::npos;
                break;
            case 2:
                kind_match = line.find("let " + s.name) != std::string_view::npos ||
                             line.find("var " + s.name) != std::string_view::npos;
                if (!kind_match) {
                    kind_match = findIdentifierColumn(line, s.name) > 0;
                }
                break;
            case 3:
                kind_match = line.find("struct " + s.name) != std::string_view::npos;
                break;
            case 4:
                kind_match = line.find("mod " + s.name) != std::string_view::npos;
                break;
            default:
                kind_match = line.find(s.name) != std::string_view::npos;
                break;
            }
            if (!kind_match) {
                continue;
            }
            int depth_here = 0;
            if (i < out.line_depths.size()) {
                depth_here = out.line_depths[i];
            }
            const int depth_delta = std::abs(depth_here - depth);
            const int line_here = static_cast<int>(i) + 1;
            const int line_delta = (line_no > 1) ? std::abs(line_here - line_no) : 0;
            if (best_line == -1 || depth_delta < best_depth_delta ||
                (depth_delta == best_depth_delta && line_delta < best_line_delta) ||
                (depth_delta == best_depth_delta && line_delta == best_line_delta &&
                 line_here < best_line)) {
                best_line = line_here;
                best_depth_delta = depth_delta;
                best_line_delta = line_delta;
            }
        }
        if (best_line > 0) {
            return best_line;
        }
        return line_no > 0 ? line_no : 1;
    };

    s.line = guess_line();
    s.depth = depth;
    s.type_info = std::move(type_info);
    s.signature = std::move(signature);

    if (s.line > 0 && static_cast<size_t>(s.line - 1) < lines.size()) {
        s.column = findIdentifierColumn(lines[static_cast<size_t>(s.line - 1)], s.name);
    } else {
        s.column = 0;
    }
    if (s.column <= 0) {
        s.column = 1;
    }
    s.stable_id = stableSymbolId(s);
    out.symbols.push_back(std::move(s));
}

static void addSemanticSymbolAtLine(DocumentSemantic& out,
                                    const std::vector<std::string_view>& lines,
                                    std::string name,
                                    int kind,
                                    int line_no,
                                    int depth,
                                    std::string type_info,
                                    std::string signature) {
    SemanticSymbol s;
    s.name = std::move(name);
    s.kind = kind;
    s.uri = out.uri;
    s.line = line_no > 0 ? line_no : 1;
    s.depth = depth;
    s.type_info = std::move(type_info);
    s.signature = std::move(signature);

    if (s.line > 0 && static_cast<size_t>(s.line - 1) < lines.size()) {
        s.column = findIdentifierColumn(lines[static_cast<size_t>(s.line - 1)], s.name);
    } else {
        s.column = 0;
    }
    if (s.column <= 0) {
        int best_line = -1;
        int best_depth_delta = 1 << 30;
        int best_line_delta = 1 << 30;
        int best_col = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            const int col_here = findIdentifierColumn(lines[i], s.name);
            if (col_here <= 0) {
                continue;
            }
            int depth_here = 0;
            if (i < out.line_depths.size()) {
                depth_here = out.line_depths[i];
            }
            const int depth_delta = std::abs(depth_here - depth);
            const int line_here = static_cast<int>(i) + 1;
            const int line_delta = (line_no > 1) ? std::abs(line_here - line_no) : 0;
            if (best_line == -1 || depth_delta < best_depth_delta ||
                (depth_delta == best_depth_delta && line_delta < best_line_delta) ||
                (depth_delta == best_depth_delta && line_delta == best_line_delta &&
                 line_here < best_line)) {
                best_line = line_here;
                best_depth_delta = depth_delta;
                best_line_delta = line_delta;
                best_col = col_here;
            }
        }
        if (best_line > 0) {
            s.line = best_line;
            s.column = best_col;
        } else {
            s.column = 1;
        }
    }
    s.stable_id = stableSymbolId(s);
    out.symbols.push_back(std::move(s));
}

static int findStructDeclLine(const std::vector<std::string_view>& lines,
                              std::string_view struct_name) {
    const std::string needle = "struct " + std::string(struct_name);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(needle) != std::string_view::npos) {
            return static_cast<int>(i) + 1;
        }
    }
    return 1;
}

static void collectStatementSymbols(DocumentSemantic& out,
                                    const std::vector<std::string_view>& lines,
                                    StatementListNode* list,
                                    int depth);

static void collectIfSymbols(DocumentSemantic& out,
                             const std::vector<std::string_view>& lines,
                             IfNode* node,
                             int depth) {
    if (!node) {
        return;
    }
    collectStatementSymbols(out, lines, node->thenBranch, depth + 1);
    if (node->elseIfBranch) {
        collectIfSymbols(out, lines, node->elseIfBranch, depth + 1);
    }
    if (node->elseBranch) {
        collectStatementSymbols(out, lines, node->elseBranch, depth + 1);
    }
}

static void collectStatementSymbols(DocumentSemantic& out,
                                    const std::vector<std::string_view>& lines,
                                    StatementListNode* list,
                                    int depth) {
    if (!list) {
        return;
    }
    for (StatementNode* stmt : list->statements) {
        if (!stmt) {
            continue;
        }
        const int line_no = stmt->line > 0 ? stmt->line : 1;
        if (auto* let_decl = dynamic_cast<LetDeclNode*>(stmt)) {
            addSemanticSymbol(out, lines, let_decl->name, 2, line_no, depth,
                              typeToString(let_decl->type), {});
            continue;
        }
        if (auto* var_decl = dynamic_cast<VarDeclNode*>(stmt)) {
            addSemanticSymbol(out, lines, var_decl->name, 2, line_no, depth,
                              typeToString(var_decl->type), {});
            continue;
        }
        if (auto* init = dynamic_cast<StructInitNode*>(stmt)) {
            addSemanticSymbol(out, lines, init->varName, 2, line_no, depth,
                              init->typeName, {});
            continue;
        }
        if (auto* for_node = dynamic_cast<ForNode*>(stmt)) {
            addSemanticSymbol(out, lines, for_node->varName, 2, line_no,
                              depth + 1, {}, {});
            collectStatementSymbols(out, lines, for_node->body, depth + 1);
            continue;
        }
        if (auto* if_node = dynamic_cast<IfNode*>(stmt)) {
            collectIfSymbols(out, lines, if_node, depth);
            continue;
        }
        if (auto* block = dynamic_cast<BlockStatementNode*>(stmt)) {
            collectStatementSymbols(out, lines, block->statements, depth + 1);
            continue;
        }
    }
}

static std::optional<DocumentSemantic>
buildDocumentSemanticFromAst(const DocumentState& doc) {
    int parse_error_line = 0;
    ProgramNode* program = parseProgramFromText(doc.text, &parse_error_line);
    std::string parsed_text;
    parsed_text = doc.text;
    if (!program && parse_error_line > 1) {
        // Error-tolerant fallback: parse document prefix before the first
        // syntax error so semantic queries above the error still work.
        const std::vector<std::string_view> all_lines = splitLines(doc.text);
        const int keep_lines = std::min(parse_error_line - 1, static_cast<int>(all_lines.size()));
        if (keep_lines > 0) {
            std::string prefix;
            for (int i = 0; i < keep_lines; ++i) {
                prefix.append(all_lines[static_cast<size_t>(i)]);
                if (i + 1 < keep_lines) {
                    prefix.push_back('\n');
                }
            }
            std::string recovered_prefix = closeOpenBracesForRecovery(prefix);
            program = parseProgramFromText(recovered_prefix);
            if (program) {
                parsed_text = std::move(recovered_prefix);
            }
        }
    }
    if (!program) {
        return std::nullopt;
    }

    DocumentSemantic out;
    out.uri = doc.uri;
    out.text = doc.text;
    out.line_depths = computeLineDepths(doc.text);
    const std::vector<std::string_view> lines = splitLines(parsed_text);

    if (program->modules.size() > 0) {
        for (ModDeclNode* mod : program->modules) {
            if (!mod) {
                continue;
            }
            out.mods.push_back(mod->moduleName);
            addSemanticSymbol(out, lines, mod->moduleName, 4,
                              mod->line > 0 ? mod->line : 1, 0, {}, {});
        }
    }

    if (program->imports.size() > 0) {
        for (UseDeclNode* use : program->imports) {
            if (!use) {
                continue;
            }
            UseDecl u;
            u.module = use->moduleName;
            u.item = use->itemName;
            u.wildcard = use->importAll;
            out.uses.push_back(std::move(u));
        }
    }

    if (program->structList) {
        for (StructDefNode* st : program->structList->structs) {
            if (!st) {
                continue;
            }
            addSemanticSymbol(out, lines, st->name, 3,
                              st->line > 0 ? st->line : 1, 0, {}, {});
            if (st->members) {
                for (StructMemberNode* member : st->members->members) {
                    if (!member) {
                        continue;
                    }
                    const int field_line = member->line > 0
                                               ? member->line
                                               : (st->line > 0 ? st->line
                                                               : findStructDeclLine(lines, st->name));
                    addSemanticSymbolAtLine(out, lines, member->name, 2, field_line, 1,
                                            typeToString(member->type),
                                            "field " + st->name + "::" + member->name);
                }
                for (StructMethodNode* method : st->members->methods) {
                    if (!method) {
                        continue;
                    }
                    const int method_line = method->line > 0 ? method->line
                                                             : (st->line > 0 ? st->line : 1);
                    const size_t method_sym_idx = out.symbols.size();
                    addSemanticSymbol(out, lines, method->name, 1, method_line, 1,
                                      typeToString(method->returnType),
                                      methodSignatureFromAst(st->name, method));
                    int method_decl_line = method_line;
                    if (out.symbols.size() > method_sym_idx) {
                        method_decl_line = out.symbols.back().line;
                    }
                    if (method->parameters) {
                        for (ParameterNode* p : method->parameters->parameters) {
                            if (!p) {
                                continue;
                            }
                            addSemanticSymbolAtLine(out, lines, p->name, 2, method_decl_line, 2,
                                                    typeToString(p->type), {});
                        }
                    }
                    collectStatementSymbols(out, lines, method->body, 2);
                }
            }
        }
    }

    if (program->implList) {
        for (ImplBlockNode* impl : program->implList->impls) {
            if (!impl) {
                continue;
            }
            for (StructMethodNode* method : impl->methods) {
                if (!method) {
                    continue;
                }
                const int method_line = method->line > 0 ? method->line
                                                         : (impl->line > 0 ? impl->line : 1);
                const size_t method_sym_idx = out.symbols.size();
                addSemanticSymbol(out, lines, method->name, 1, method_line, 1,
                                  typeToString(method->returnType),
                                  methodSignatureFromAst(impl->structName, method));
                int method_decl_line = method_line;
                if (out.symbols.size() > method_sym_idx) {
                    method_decl_line = out.symbols.back().line;
                }
                if (method->parameters) {
                    for (ParameterNode* p : method->parameters->parameters) {
                        if (!p) {
                            continue;
                        }
                        addSemanticSymbolAtLine(out, lines, p->name, 2, method_decl_line, 2,
                                                typeToString(p->type), {});
                    }
                }
                collectStatementSymbols(out, lines, method->body, 2);
            }
        }
    }

    if (program->functionList) {
        for (FunctionDefNode* fn : program->functionList->functions) {
            if (!fn || fn->isExtern) {
                continue;
            }
            const int fn_line = fn->line > 0 ? fn->line : 1;
            const size_t fn_sym_idx = out.symbols.size();
            addSemanticSymbol(out, lines, fn->name, 1, fn_line, 0,
                              typeToString(fn->returnType),
                              functionSignatureFromAst(fn));
            int fn_decl_line = fn_line;
            if (out.symbols.size() > fn_sym_idx) {
                fn_decl_line = out.symbols.back().line;
            }

            if (fn->parameters) {
                for (ParameterNode* p : fn->parameters->parameters) {
                    if (!p) {
                        continue;
                    }
                    addSemanticSymbolAtLine(out, lines, p->name, 2, fn_decl_line, 1,
                                            typeToString(p->type), {});
                }
            }
            collectStatementSymbols(out, lines, fn->body, 1);
        }
    }

    // Ensure method/function declarations are discoverable even when parser
    // shape does not expose nested method nodes through the expected AST lists.
    {
        std::unordered_set<std::string> seen_fn_line_name;
        for (const auto& sym : out.symbols) {
            if (sym.kind != 1) {
                continue;
            }
            seen_fn_line_name.insert(std::to_string(sym.line) + "|" + sym.name);
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            auto maybe_name = parseFnNameInLine(lines[i]);
            if (!maybe_name.has_value()) {
                continue;
            }
            const int line_no = static_cast<int>(i) + 1;
            const std::string key = std::to_string(line_no) + "|" + *maybe_name;
            if (seen_fn_line_name.find(key) != seen_fn_line_name.end()) {
                continue;
            }
            addSemanticSymbol(out, lines, *maybe_name, 1, line_no, 0, {}, {});
            seen_fn_line_name.insert(key);
        }
    }

    delete program;
    out.ast_valid = true;
    return out;
}

static DocumentSemantic buildDocumentSemantic(const DocumentState& doc) {
    if (const auto ast_sem = buildDocumentSemanticFromAst(doc); ast_sem.has_value()) {
        return *ast_sem;
    }

    DocumentSemantic out;
    out.uri = doc.uri;
    out.text = doc.text;
    out.line_depths = computeLineDepths(doc.text);
    return out;
}

static std::vector<DocumentSemantic>
buildSemanticSnapshot(const std::vector<DocumentState>& docs) {
    std::vector<DocumentSemantic> out;
    out.reserve(docs.size());
    for (const DocumentState& doc : docs) {
        out.push_back(buildDocumentSemantic(doc));
    }
    return out;
}

static const DocumentSemantic* findSemanticDoc(
    const std::vector<DocumentSemantic>& docs,
    std::string_view uri) {
    for (const DocumentSemantic& doc : docs) {
        if (doc.uri == uri) {
            return &doc;
        }
    }
    return nullptr;
}

static bool uriMatchesModule(std::string_view uri, std::string_view modName);

static bool hasModuleProvider(const std::vector<DocumentSemantic>& docs,
                              std::string_view module_name) {
    for (const auto& doc : docs) {
        if (uriMatchesModule(doc.uri, module_name)) {
            return true;
        }
    }
    return false;
}

static std::optional<DocumentSemantic> loadFilesystemSemanticDocument(
    const std::string& file_path) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    DocumentState doc;
    doc.uri = fileUriFromPath(file_path);
    doc.language_id = "mlang";
    doc.text = std::move(text);
    doc.version = 0;

    return buildDocumentSemanticFromAst(doc);
}

static void enqueueDocModules(const DocumentSemantic& doc,
                              std::deque<std::pair<std::string, std::string>>& q) {
    for (const auto& use : doc.uses) {
        if (!use.module.empty()) {
            q.push_back({doc.uri, use.module});
        }
    }
    for (const auto& mod : doc.mods) {
        if (!mod.empty()) {
            q.push_back({doc.uri, mod});
        }
    }
}

static void hydrateFilesystemModulesFor(std::string_view root_uri,
                                        std::vector<DocumentSemantic>& docs) {
    const DocumentSemantic* root = findSemanticDoc(docs, root_uri);
    if (!root || !root->ast_valid) {
        return;
    }

    std::deque<std::pair<std::string, std::string>> queue;
    enqueueDocModules(*root, queue);
    std::unordered_set<std::string> visited;

    while (!queue.empty()) {
        auto [requester_uri, module_name] = queue.front();
        queue.pop_front();
        if (module_name.empty()) {
            continue;
        }
        const std::string visit_key = requester_uri + "\n" + module_name;
        if (!visited.insert(visit_key).second) {
            continue;
        }
        if (hasModuleProvider(docs, module_name)) {
            continue;
        }

        const auto file_path = resolveModuleFilePath(requester_uri, module_name);
        if (!file_path.has_value()) {
            continue;
        }

        const std::string uri = fileUriFromPath(*file_path);
        if (findSemanticDoc(docs, uri)) {
            continue;
        }

        const auto loaded = loadFilesystemSemanticDocument(*file_path);
        if (!loaded.has_value() || !loaded->ast_valid) {
            continue;
        }
        docs.push_back(*loaded);
        enqueueDocModules(docs.back(), queue);
    }
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

static int lineDepthAt(const DocumentSemantic& doc, int line) {
    if (line <= 0 || doc.line_depths.empty()) {
        return 0;
    }
    const size_t idx = static_cast<size_t>(line - 1);
    if (idx < doc.line_depths.size()) {
        return doc.line_depths[idx];
    }
    return doc.line_depths.back();
}

static bool isLikelyVarDeclarationSymbol(const DocumentSemantic& current,
                                         const SemanticSymbol& sym) {
    if (sym.kind != 2 || sym.line <= 0) {
        return false;
    }
    const size_t idx = static_cast<size_t>(sym.line - 1);
    if (idx >= splitLines(current.text).size()) {
        return false;
    }
    const std::vector<std::string_view> lines = splitLines(current.text);
    const std::string_view line = lines[idx];
    if (line.find("let " + sym.name) != std::string_view::npos ||
        line.find("var " + sym.name) != std::string_view::npos ||
        line.find("for " + sym.name) != std::string_view::npos) {
        return true;
    }
    if (line.find("fn ") != std::string_view::npos) {
        const int col = findIdentifierColumn(line, sym.name);
        if (col > 0) {
            size_t pos = static_cast<size_t>(col - 1) + sym.name.size();
            while (pos < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
                ++pos;
            }
            if (pos < line.size() && line[pos] == ':') {
                return true;
            }
        }
    }
    return false;
}

static const SemanticSymbol* findVisibleVarByName(const DocumentSemantic& current,
                                                  std::string_view name,
                                                  int query_line,
                                                  int query_depth) {
    const SemanticSymbol* best_local_var = nullptr;
    const SemanticSymbol* best_fallback_var = nullptr;
    for (const SemanticSymbol& sym : current.symbols) {
        if (sym.kind != 2 || sym.name != name) {
            continue;
        }
        const int sym_depth = lineDepthAt(current, sym.line);
        if (sym.line > query_line || sym_depth > query_depth) {
            continue;
        }
        const bool is_decl = isLikelyVarDeclarationSymbol(current, sym);
        if (!is_decl) {
            const int best_fallback_depth = best_fallback_var
                                                ? lineDepthAt(current, best_fallback_var->line)
                                                : -1;
            if (!best_fallback_var ||
                sym_depth > best_fallback_depth ||
                (sym_depth == best_fallback_depth &&
                 sym.line > best_fallback_var->line)) {
                best_fallback_var = &sym;
            }
            continue;
        }
        const int best_depth =
            best_local_var ? lineDepthAt(current, best_local_var->line) : -1;
        if (!best_local_var ||
            sym_depth > best_depth ||
            (sym_depth == best_depth && sym.line > best_local_var->line)) {
            best_local_var = &sym;
        }
    }
    if (best_local_var) {
        return best_local_var;
    }
    return best_fallback_var;
}

static std::string ownerTypeFromMethodSignature(std::string_view signature) {
    static constexpr std::string_view kFnPrefix = "fn ";
    if (!startsWith(signature, kFnPrefix)) {
        return {};
    }
    const std::string_view tail = signature.substr(kFnPrefix.size());
    const size_t sep = tail.find("::");
    if (sep == std::string_view::npos || sep == 0) {
        return {};
    }
    return std::string(tail.substr(0, sep));
}

static std::string structContextAtLine(const DocumentSemantic& current,
                                       int query_line) {
    const SemanticSymbol* best_method = nullptr;
    for (const SemanticSymbol& sym : current.symbols) {
        if (sym.kind != 1 || sym.line <= 0 || sym.line > query_line) {
            continue;
        }
        if (ownerTypeFromMethodSignature(sym.signature).empty()) {
            continue;
        }
        if (!best_method || sym.line > best_method->line) {
            best_method = &sym;
        }
    }
    if (!best_method) {
        return {};
    }
    return ownerTypeFromMethodSignature(best_method->signature);
}

static std::string normalizeStructTypeName(std::string_view type_info) {
    size_t start = 0;
    while (start < type_info.size() &&
           std::isspace(static_cast<unsigned char>(type_info[start])) != 0) {
        ++start;
    }
    size_t end = type_info.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(type_info[end - 1])) != 0) {
        --end;
    }
    if (start >= end) {
        return {};
    }
    std::string core(type_info.substr(start, end - start));
    const size_t generic = core.find('<');
    if (generic != std::string::npos) {
        core = core.substr(0, generic);
    }
    const size_t mod_sep = core.rfind("::");
    if (mod_sep != std::string::npos && mod_sep + 2 < core.size()) {
        core = core.substr(mod_sep + 2);
    }
    if (core.empty() || !isIdentStart(core.front())) {
        return {};
    }
    for (char c : core) {
        if (!isIdentContinue(c)) {
            return {};
        }
    }
    return core;
}

static std::vector<SemanticSymbol> collectImportedSymbols(
    const DocumentSemantic& current,
    const std::vector<DocumentSemantic>& all_docs) {
    std::vector<SemanticSymbol> imported;

    auto append_from_module = [&](std::string_view module_name,
                                  std::string_view specific_name,
                                  bool wildcard) {
        for (const DocumentSemantic& doc : all_docs) {
            if (doc.uri == current.uri) {
                continue;
            }
            if (!uriMatchesModule(doc.uri, module_name)) {
                continue;
            }
            for (const SemanticSymbol& sym : doc.symbols) {
                if (lineDepthAt(doc, sym.line) != 0) {
                    continue;
                }
                if (sym.kind == 2) {
                    continue;
                }
                if (!wildcard && sym.name != specific_name) {
                    continue;
                }
                imported.push_back(sym);
            }
        }
    };

    for (const UseDecl& use : current.uses) {
        append_from_module(use.module, use.item, use.wildcard);
    }

    for (const std::string& mod : current.mods) {
        append_from_module(mod, "", true);
    }

    return imported;
}

struct ResolvedQuerySymbol {
    SemanticSymbol symbol;
    int overload_count = 1;
    bool from_current_document = false;
};

struct TextDefinitionFallback {
    int line = 0;
    int column = 0;
    std::string name;
};

static bool parseSelfTypeFromSignatureLine(std::string_view line,
                                           std::string& out_type) {
    const size_t fn_pos = line.find("fn ");
    if (fn_pos == std::string_view::npos) {
        return false;
    }
    const size_t lp = line.find('(', fn_pos + 3);
    const size_t rp = (lp == std::string_view::npos) ? std::string_view::npos
                                                      : line.find(')', lp + 1);
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp) {
        return false;
    }
    const std::string_view params = line.substr(lp + 1, rp - lp - 1);
    const size_t self_pos = params.find("self");
    if (self_pos == std::string_view::npos) {
        return false;
    }
    const size_t colon = params.find(':', self_pos + 4);
    if (colon == std::string_view::npos) {
        return false;
    }
    size_t start = colon + 1;
    while (start < params.size() &&
           std::isspace(static_cast<unsigned char>(params[start])) != 0) {
        ++start;
    }
    if (start >= params.size()) {
        return false;
    }
    size_t end = start;
    while (end < params.size() &&
           params[end] != ',' &&
           params[end] != ')' &&
           std::isspace(static_cast<unsigned char>(params[end])) == 0) {
        ++end;
    }
    std::string ty(params.substr(start, end - start));
    const size_t generic = ty.find('<');
    if (generic != std::string::npos) {
        ty = ty.substr(0, generic);
    }
    const size_t mod_sep = ty.rfind("::");
    if (mod_sep != std::string::npos && mod_sep + 2 < ty.size()) {
        ty = ty.substr(mod_sep + 2);
    }
    if (ty.empty()) {
        return false;
    }
    out_type = std::move(ty);
    return true;
}

static bool findStructFieldDeclaration(std::string_view text,
                                       std::string_view struct_name,
                                       std::string_view field_name,
                                       int& out_line,
                                       int& out_col) {
    const std::vector<std::string_view> lines = splitLines(text);
    bool in_target_struct = false;
    int brace_depth = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (!in_target_struct) {
            std::string needle = "struct " + std::string(struct_name);
            if (line.find(needle) == std::string_view::npos &&
                line.find("pub " + needle) == std::string_view::npos) {
                continue;
            }
            in_target_struct = true;
        }

        for (char ch : line) {
            if (ch == '{') ++brace_depth;
            else if (ch == '}') --brace_depth;
        }

        const std::string field_needle = "var " + std::string(field_name);
        const size_t pos = line.find(field_needle);
        if (pos != std::string_view::npos) {
            const size_t after = pos + field_needle.size();
            if (after < line.size() && line[after] == ':') {
                out_line = static_cast<int>(i) + 1;
                out_col = static_cast<int>(pos + 5);
                return true;
            }
        }

        if (in_target_struct && brace_depth <= 0) {
            in_target_struct = false;
        }
    }
    return false;
}

static bool findParameterInNearestFunction(std::string_view text,
                                           int query_line,
                                           std::string_view name,
                                           int& out_line,
                                           int& out_col) {
    const std::vector<std::string_view> lines = splitLines(text);
    for (int y = std::min(query_line - 1, static_cast<int>(lines.size()) - 1);
         y >= 0; --y) {
        const std::string_view line = lines[static_cast<size_t>(y)];
        const size_t fn_pos = line.find("fn ");
        const size_t lp = line.find('(');
        const size_t rp = line.find(')');
        if (fn_pos == std::string_view::npos || lp == std::string_view::npos ||
            rp == std::string_view::npos || rp <= lp) {
            continue;
        }
        const std::string_view params = line.substr(lp + 1, rp - lp - 1);
        const size_t pos = params.find(name);
        if (pos == std::string_view::npos) {
            continue;
        }
        const size_t global = lp + 1 + pos;
        const bool left_ok = (global == 0) || !isIdentContinue(line[global - 1]);
        const size_t end = global + name.size();
        const bool right_ok = (end >= line.size()) || !isIdentContinue(line[end]);
        if (!left_ok || !right_ok) {
            continue;
        }
        size_t p = end;
        while (p < line.size() &&
               std::isspace(static_cast<unsigned char>(line[p])) != 0) {
            ++p;
        }
        if (p < line.size() && line[p] == ':') {
            out_line = y + 1;
            out_col = static_cast<int>(global) + 1;
            return true;
        }
    }
    return false;
}

static bool fallbackDefinitionFromText(const mlang::compiler_api::DocumentSemantic& current,
                                       int line,
                                       int column,
                                       TextDefinitionFallback& out) {
    const std::optional<size_t> offset = offsetFromLineColumn(current.text, line, column);
    if (!offset.has_value()) {
        return false;
    }
    const std::optional<TokenSpan> token_span = tokenSpanAtOffset(current.text, *offset);
    if (!token_span.has_value()) {
        return false;
    }
    out.name = token_span->token;

    bool member_access = false;
    std::string member_object;
    if (token_span->start > 0) {
        size_t p = token_span->start;
        while (p > 0 &&
               std::isspace(static_cast<unsigned char>(current.text[p - 1])) != 0) {
            --p;
        }
        if (p > 0 && current.text[p - 1] == '.') {
            size_t q = p - 1;
            while (q > 0 &&
                   std::isspace(static_cast<unsigned char>(current.text[q - 1])) != 0) {
                --q;
            }
            const size_t end = q;
            while (q > 0 && isIdentContinue(current.text[q - 1])) {
                --q;
            }
            if (q < end && isIdentStart(current.text[q])) {
                member_object = std::string(current.text.substr(q, end - q));
                member_access = true;
            }
        }
    }

    if (member_access && member_object == "self") {
        const std::vector<std::string_view> lines = splitLines(current.text);
        std::string owner;
        for (int y = std::min(line - 1, static_cast<int>(lines.size()) - 1);
             y >= 0; --y) {
            if (parseSelfTypeFromSignatureLine(lines[static_cast<size_t>(y)], owner)) {
                break;
            }
        }
        if (!owner.empty()) {
            int def_line = 0;
            int def_col = 0;
            if (findStructFieldDeclaration(current.text, owner, out.name, def_line, def_col)) {
                out.line = def_line;
                out.column = def_col;
                return true;
            }
        }
    }

    int def_line = 0;
    int def_col = 0;
    if (findParameterInNearestFunction(current.text, line, out.name, def_line, def_col)) {
        out.line = def_line;
        out.column = def_col;
        return true;
    }
    return false;
}

static std::optional<ResolvedQuerySymbol> resolveSymbolAtPosition(
    const DocumentSemantic& current,
    const std::vector<DocumentSemantic>& all_docs,
    int line,
    int column) {
    const std::optional<size_t> offset =
        offsetFromLineColumn(current.text, line, column);
    if (!offset.has_value()) {
        return std::nullopt;
    }
    const std::optional<TokenSpan> token_span =
        tokenSpanAtOffset(current.text, *offset);
    if (!token_span.has_value()) {
        return std::nullopt;
    }
    const std::string& token = token_span->token;

    const int query_depth = lineDepthAt(current, line);

    // Member access support (self.field and typed_var.field).
    bool is_member_access = false;
    std::string member_object;
    if (token_span->start > 0) {
        size_t p = token_span->start;
        while (p > 0 &&
               std::isspace(static_cast<unsigned char>(current.text[p - 1])) != 0) {
            --p;
        }
        if (p > 0 && current.text[p - 1] == '.') {
            size_t q = p - 1;
            while (q > 0 &&
                   std::isspace(static_cast<unsigned char>(current.text[q - 1])) != 0) {
                --q;
            }
            const size_t end = q;
            while (q > 0 && isIdentContinue(current.text[q - 1])) {
                --q;
            }
            if (q < end && isIdentStart(current.text[q])) {
                member_object = std::string(current.text.substr(q, end - q));
                is_member_access = true;
            }
        }
    }
    if (is_member_access) {
        std::string owner_type;
        if (member_object == "self") {
            owner_type = structContextAtLine(current, line);
        } else {
            const SemanticSymbol* object_sym =
                findVisibleVarByName(current, member_object, line, query_depth);
            if (object_sym) {
                owner_type = normalizeStructTypeName(object_sym->type_info);
            }
        }

        if (!owner_type.empty()) {
            const std::string field_sig = "field " + owner_type + "::" + token;
            for (const DocumentSemantic& doc : all_docs) {
                for (const SemanticSymbol& sym : doc.symbols) {
                    if (sym.kind != 2 || sym.name != token) {
                        continue;
                    }
                    if (sym.signature != field_sig) {
                        continue;
                    }
                    return ResolvedQuerySymbol{sym, 1, sym.uri == current.uri};
                }
            }
        }
    }

    // Prefer closest visible local variable declaration.
    const SemanticSymbol* best_local_var =
        findVisibleVarByName(current, token, line, query_depth);
    if (best_local_var) {
        return ResolvedQuerySymbol{*best_local_var, 1, true};
    }

    // Then global-like declarations in current document.
    const SemanticSymbol* first_local = nullptr;
    int overload_count = 0;
    for (const SemanticSymbol& sym : current.symbols) {
        if (sym.name != token) {
            continue;
        }
        if (!first_local) {
            first_local = &sym;
        }
        if (sym.kind == 1) {
            ++overload_count;
        }
    }
    if (first_local) {
        if (overload_count == 0) {
            overload_count = 1;
        }
        return ResolvedQuerySymbol{*first_local, overload_count, true};
    }

    const std::vector<SemanticSymbol> imported =
        collectImportedSymbols(current, all_docs);
    const SemanticSymbol* first_imported = nullptr;
    int imported_overloads = 0;
    for (const SemanticSymbol& sym : imported) {
        if (sym.name != token) {
            continue;
        }
        if (!first_imported) {
            first_imported = &sym;
        }
        if (sym.kind == 1) {
            ++imported_overloads;
        }
    }
    if (first_imported) {
        if (imported_overloads == 0) {
            imported_overloads = 1;
        }
        return ResolvedQuerySymbol{*first_imported, imported_overloads, false};
    }

    return std::nullopt;
}

struct ReferenceLocation {
    std::string uri;
    int line = 0;
    int column = 0;
};

static std::vector<ReferenceLocation> collectReferencesForSymbol(
    const SemanticSymbol& target,
    const std::vector<DocumentSemantic>& all_docs) {
    std::vector<ReferenceLocation> refs;
    std::unordered_set<std::string> seen;

    for (const DocumentSemantic& doc : all_docs) {
        if (!doc.ast_valid) {
            continue;
        }
        int line = 1;
        int col = 1;
        for (size_t i = 0; i < doc.text.size(); ++i) {
            const char ch = doc.text[i];
            if (ch == '\n') {
                ++line;
                col = 1;
                continue;
            }
            if (!isIdentStart(ch) || (i > 0 && isIdentContinue(doc.text[i - 1]))) {
                ++col;
                continue;
            }

            size_t j = i + 1;
            while (j < doc.text.size() && isIdentContinue(doc.text[j])) {
                ++j;
            }
            const std::string_view token = std::string_view(doc.text).substr(i, j - i);
            if (token == target.name) {
                const auto resolved = resolveSymbolAtPosition(doc, all_docs, line, col);
                if (resolved.has_value() &&
                    resolved->symbol.stable_id == target.stable_id) {
                    const std::string key = doc.uri + ":" + std::to_string(line) + ":" +
                                            std::to_string(col);
                    if (seen.insert(key).second) {
                        refs.push_back({doc.uri, line, col});
                    }
                }
            }
            col += static_cast<int>(j - i);
            i = j - 1;
        }
    }

    std::sort(refs.begin(), refs.end(), [](const ReferenceLocation& a, const ReferenceLocation& b) {
        if (a.uri != b.uri) {
            return a.uri < b.uri;
        }
        if (a.line != b.line) {
            return a.line < b.line;
        }
        return a.column < b.column;
    });
    return refs;
}

static bool isRenameSafeForSymbol(const SemanticSymbol& target,
                                  std::string_view new_name,
                                  const std::vector<DocumentSemantic>& all_docs) {
    if (new_name.empty() || !isIdentStart(new_name.front())) {
        return false;
    }
    for (char c : new_name) {
        if (!isIdentContinue(c)) {
            return false;
        }
    }

    const std::vector<ReferenceLocation> refs = collectReferencesForSymbol(target, all_docs);
    for (const auto& ref : refs) {
        const DocumentSemantic* doc = findSemanticDoc(all_docs, ref.uri);
        if (!doc) {
            continue;
        }
        const int query_depth = lineDepthAt(*doc, ref.line);

        for (const auto& sym : doc->symbols) {
            if (sym.stable_id == target.stable_id || sym.name != new_name) {
                continue;
            }
            if (sym.kind == 2) {
                const int sym_depth = lineDepthAt(*doc, sym.line);
                if (sym.line <= ref.line && sym_depth <= query_depth) {
                    return false;
                }
            } else {
                return false;
            }
        }

        const std::vector<SemanticSymbol> imported = collectImportedSymbols(*doc, all_docs);
        for (const auto& sym : imported) {
            if (sym.stable_id != target.stable_id && sym.name == new_name) {
                return false;
            }
        }
    }
    return true;
}

static std::string completionPrefixAtOffset(std::string_view text, size_t offset);

static std::vector<std::string> computeSemanticCompletions(
    const DocumentSemantic& current,
    const std::vector<DocumentSemantic>& all_docs,
    int line,
    int column) {
    static constexpr std::string_view kKeywords[] = {
        "fn", "let", "var", "struct", "mod", "use", "if", "else", "while",
        "for", "return",
    };

    const std::optional<size_t> offset =
        offsetFromLineColumn(current.text, line, column);
    if (!offset.has_value()) {
        return {};
    }
    const std::string prefix = completionPrefixAtOffset(current.text, *offset);

    std::set<std::string> dedup;
    auto consider = [&](std::string_view candidate) {
        if (prefix.empty() || startsWith(candidate, prefix)) {
            dedup.insert(std::string(candidate));
        }
    };

    for (const std::string_view kw : kKeywords) {
        consider(kw);
    }

    const int query_depth = lineDepthAt(current, line);
    for (const SemanticSymbol& sym : current.symbols) {
        if (sym.kind == 2) {
            if (sym.line <= line && lineDepthAt(current, sym.line) <= query_depth) {
                consider(sym.name);
            }
            continue;
        }
        consider(sym.name);
    }

    const std::vector<SemanticSymbol> imported =
        collectImportedSymbols(current, all_docs);
    for (const SemanticSymbol& sym : imported) {
        consider(sym.name);
    }

    return std::vector<std::string>(dedup.begin(), dedup.end());
}

static const char* symbolKindName(int kind) {
    switch (kind) {
    case 1:
        return "fn";
    case 2:
        return "var";
    case 3:
        return "struct";
    case 4:
        return "mod";
    default:
        return "symbol";
    }
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
        invalidateSemanticCacheLocked();
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
        invalidateSemanticCacheLocked();
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
        invalidateSemanticCacheLocked();
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

    Status semanticSnapshotForUri(std::string_view uri,
                                  std::vector<DocumentSemantic>& out_docs,
                                  const DocumentSemantic** out_current) {
        if (uri.empty() || out_current == nullptr) {
            return Status::InvalidArgument;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (documents_.find(std::string(uri)) == documents_.end()) {
            return Status::DocumentNotFound;
        }

        ensureBaseSemanticCacheLocked();
        const std::string key(uri);
        auto it = hydrated_semantic_cache_.find(key);
        if (it == hydrated_semantic_cache_.end() ||
            it->second.generation != semantic_generation_) {
            HydratedSemanticSnapshot snap;
            snap.generation = semantic_generation_;
            snap.docs = base_semantic_docs_;
            hydrateFilesystemModulesFor(key, snap.docs);
            for (size_t i = 0; i < snap.docs.size(); ++i) {
                snap.uri_to_index[snap.docs[i].uri] = i;
            }
            it = hydrated_semantic_cache_.insert_or_assign(key, std::move(snap)).first;
        }

        out_docs = it->second.docs;
        const auto idx_it = it->second.uri_to_index.find(key);
        if (idx_it == it->second.uri_to_index.end()) {
            return Status::DocumentNotFound;
        }
        *out_current = &out_docs[idx_it->second];
        return Status::Ok;
    }

    Status clearSemanticCache() {
        std::lock_guard<std::mutex> lock(mutex_);
        base_semantic_generation_ = std::numeric_limits<std::uint64_t>::max();
        base_semantic_docs_.clear();
        hydrated_semantic_cache_.clear();
        return Status::Ok;
    }

    Status warmSemanticCacheForUri(std::string_view uri) {
        std::vector<DocumentSemantic> docs;
        const DocumentSemantic* current = nullptr;
        return semanticSnapshotForUri(uri, docs, &current);
    }

private:
    struct HydratedSemanticSnapshot {
        std::uint64_t generation = 0;
        std::vector<DocumentSemantic> docs;
        std::unordered_map<std::string, size_t> uri_to_index;
    };

    void invalidateSemanticCacheLocked() {
        ++semantic_generation_;
        base_semantic_generation_ = std::numeric_limits<std::uint64_t>::max();
        base_semantic_docs_.clear();
        hydrated_semantic_cache_.clear();
    }

    void ensureBaseSemanticCacheLocked() {
        if (base_semantic_generation_ == semantic_generation_) {
            return;
        }
        std::vector<DocumentState> snapshot;
        snapshot.reserve(documents_.size());
        for (const auto& kv : documents_) {
            snapshot.push_back(kv.second);
        }
        base_semantic_docs_ = buildSemanticSnapshot(snapshot);
        base_semantic_generation_ = semantic_generation_;
        hydrated_semantic_cache_.clear();
    }

    std::mutex mutex_;
    std::unordered_map<std::string, DocumentState> documents_;
    std::uint64_t semantic_generation_ = 1;
    std::uint64_t base_semantic_generation_ = std::numeric_limits<std::uint64_t>::max();
    std::vector<DocumentSemantic> base_semantic_docs_;
    std::unordered_map<std::string, HydratedSemanticSnapshot> hydrated_semantic_cache_;
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
int __mlang_compiler_document_definition_id(mlang_compiler_session* session,
                                            const char* uri,
                                            int line,
                                            int column,
                                            char* out_id,
                                            int out_id_capacity,
                                            int* out_id_length);
int __mlang_compiler_document_symbol_id_get(mlang_compiler_session* session,
                                            const char* uri,
                                            int index,
                                            char* out_id,
                                            int out_id_capacity,
                                            int* out_id_length);
int __mlang_compiler_document_reference_count(mlang_compiler_session* session,
                                              const char* uri,
                                              int line,
                                              int column,
                                              int* out_count);
int __mlang_compiler_document_reference_get(mlang_compiler_session* session,
                                            const char* uri,
                                            int line,
                                            int column,
                                            int index,
                                            char* out_ref_uri,
                                            int out_ref_uri_capacity,
                                            int* out_ref_uri_length,
                                            int* out_ref_line,
                                            int* out_ref_column);
int __mlang_compiler_document_rename_is_safe(mlang_compiler_session* session,
                                             const char* uri,
                                             int line,
                                             int column,
                                             const char* new_name,
                                             int* out_is_safe);
int __mlang_compiler_semantic_cache_warm(mlang_compiler_session* session,
                                         const char* uri);
int __mlang_compiler_semantic_cache_clear(mlang_compiler_session* session);

static int prepare_semantic_query(mlang_compiler_session* session,
                                  const char* uri,
                                  std::shared_ptr<mlang::compiler_api::SessionStore>& out_store,
                                  std::vector<mlang::compiler_api::DocumentSemantic>& out_docs,
                                  const mlang::compiler_api::DocumentSemantic** out_current) {
    if (session == nullptr || uri == nullptr || out_current == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }
    out_store = mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!out_store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }
    const mlang::compiler_api::Status st =
        out_store->semanticSnapshotForUri(uri, out_docs, out_current);
    if (st != mlang::compiler_api::Status::Ok) {
        return static_cast<int>(st);
    }
    if (*out_current == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::Unsupported);
    }
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

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

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const std::optional<mlang::compiler_api::ResolvedQuerySymbol> resolved =
        mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
    if (!resolved.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }

    std::string hover = "symbol: " + resolved->symbol.name;
    hover += " [" + std::string(mlang::compiler_api::symbolKindName(resolved->symbol.kind)) + "]";
    if (!resolved->symbol.type_info.empty()) {
        hover += " : " + resolved->symbol.type_info;
    }
    if (!resolved->symbol.signature.empty()) {
        hover += " | " + resolved->symbol.signature;
    }
    if (resolved->overload_count > 1) {
        hover += " | overloads=" + std::to_string(resolved->overload_count);
    }
    if (resolved->symbol.uri == uri) {
        hover += " (declared in document)";
    } else {
        hover += " (declared in " + resolved->symbol.uri + ")";
    }

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

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const auto completions =
        mlang::compiler_api::computeSemanticCompletions(*current, sem_docs, line, column);
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

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const auto completions =
        mlang::compiler_api::computeSemanticCompletions(*current, sem_docs, line, column);
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

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    *out_count = static_cast<int>(current->symbols.size());
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

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    if (index < 0 || static_cast<size_t>(index) >= current->symbols.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const mlang::compiler_api::SemanticSymbol& sym =
        current->symbols[static_cast<size_t>(index)];
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

int __mlang_compiler_document_symbol_id_get(mlang_compiler_session* session,
                                            const char* uri,
                                            int index,
                                            char* out_id,
                                            int out_id_capacity,
                                            int* out_id_length) {
    if (session == nullptr || uri == nullptr || out_id == nullptr || out_id_capacity <= 0 ||
        out_id_length == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    if (index < 0 || static_cast<size_t>(index) >= current->symbols.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const std::string& id = current->symbols[static_cast<size_t>(index)].stable_id;
    *out_id_length = static_cast<int>(id.size());
    const size_t copy_len = std::min(static_cast<size_t>(out_id_capacity - 1), id.size());
    if (copy_len > 0) {
        std::memcpy(out_id, id.data(), copy_len);
    }
    out_id[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_symbol_type_get(mlang_compiler_session* session,
                                              const char* uri,
                                              int index,
                                              char* out_type,
                                              int out_type_capacity,
                                              int* out_type_length) {
    if (session == nullptr || uri == nullptr || out_type == nullptr ||
        out_type_capacity <= 0 || out_type_length == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    if (index < 0 || static_cast<size_t>(index) >= current->symbols.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const std::string& type_info = current->symbols[static_cast<size_t>(index)].type_info;
    *out_type_length = static_cast<int>(type_info.size());
    const size_t copy_len =
        std::min(static_cast<size_t>(out_type_capacity - 1), type_info.size());
    if (copy_len > 0) {
        std::memcpy(out_type, type_info.data(), copy_len);
    }
    out_type[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_symbol_signature_get(mlang_compiler_session* session,
                                                   const char* uri,
                                                   int index,
                                                   char* out_signature,
                                                   int out_signature_capacity,
                                                   int* out_signature_length) {
    if (session == nullptr || uri == nullptr || out_signature == nullptr ||
        out_signature_capacity <= 0 || out_signature_length == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    if (index < 0 || static_cast<size_t>(index) >= current->symbols.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const std::string& signature = current->symbols[static_cast<size_t>(index)].signature;
    *out_signature_length = static_cast<int>(signature.size());
    const size_t copy_len =
        std::min(static_cast<size_t>(out_signature_capacity - 1), signature.size());
    if (copy_len > 0) {
        std::memcpy(out_signature, signature.data(), copy_len);
    }
    out_signature[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_resolve_symbol(mlang_compiler_session* session,
                                             const char* uri,
                                             int line,
                                             int column,
                                             char* out_name,
                                             int out_name_capacity,
                                             int* out_name_length,
                                             int* out_kind,
                                             int* out_line,
                                             int* out_column,
                                             char* out_uri,
                                             int out_uri_capacity,
                                             int* out_uri_length,
                                             char* out_id,
                                             int out_id_capacity,
                                             int* out_id_length,
                                             char* out_type,
                                             int out_type_capacity,
                                             int* out_type_length,
                                             char* out_signature,
                                             int out_signature_capacity,
                                             int* out_signature_length,
                                             int* out_overload_count,
                                             int* out_from_current_document) {
    if (session == nullptr || uri == nullptr || out_name == nullptr ||
        out_name_capacity <= 0 || out_name_length == nullptr || out_kind == nullptr ||
        out_line == nullptr || out_column == nullptr || out_uri == nullptr ||
        out_uri_capacity <= 0 || out_uri_length == nullptr || out_id == nullptr ||
        out_id_capacity <= 0 || out_id_length == nullptr || out_type == nullptr ||
        out_type_capacity <= 0 || out_type_length == nullptr ||
        out_signature == nullptr || out_signature_capacity <= 0 ||
        out_signature_length == nullptr || out_overload_count == nullptr ||
        out_from_current_document == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const auto resolved =
        mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
    if (!resolved.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }

    const auto& sym = resolved->symbol;
    *out_kind = sym.kind;
    *out_line = sym.line;
    *out_column = sym.column;
    *out_overload_count = resolved->overload_count;
    *out_from_current_document = resolved->from_current_document ? 1 : 0;

    *out_name_length = static_cast<int>(sym.name.size());
    const size_t name_copy_len =
        std::min(static_cast<size_t>(out_name_capacity - 1), sym.name.size());
    if (name_copy_len > 0) {
        std::memcpy(out_name, sym.name.data(), name_copy_len);
    }
    out_name[name_copy_len] = '\0';

    *out_uri_length = static_cast<int>(sym.uri.size());
    const size_t uri_copy_len =
        std::min(static_cast<size_t>(out_uri_capacity - 1), sym.uri.size());
    if (uri_copy_len > 0) {
        std::memcpy(out_uri, sym.uri.data(), uri_copy_len);
    }
    out_uri[uri_copy_len] = '\0';

    *out_id_length = static_cast<int>(sym.stable_id.size());
    const size_t id_copy_len =
        std::min(static_cast<size_t>(out_id_capacity - 1), sym.stable_id.size());
    if (id_copy_len > 0) {
        std::memcpy(out_id, sym.stable_id.data(), id_copy_len);
    }
    out_id[id_copy_len] = '\0';

    *out_type_length = static_cast<int>(sym.type_info.size());
    const size_t type_copy_len =
        std::min(static_cast<size_t>(out_type_capacity - 1), sym.type_info.size());
    if (type_copy_len > 0) {
        std::memcpy(out_type, sym.type_info.data(), type_copy_len);
    }
    out_type[type_copy_len] = '\0';

    *out_signature_length = static_cast<int>(sym.signature.size());
    const size_t sig_copy_len =
        std::min(static_cast<size_t>(out_signature_capacity - 1), sym.signature.size());
    if (sig_copy_len > 0) {
        std::memcpy(out_signature, sym.signature.data(), sig_copy_len);
    }
    out_signature[sig_copy_len] = '\0';

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

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const std::optional<mlang::compiler_api::ResolvedQuerySymbol> def =
        mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
    if (!def.has_value()) {
        mlang::compiler_api::TextDefinitionFallback fb;
        if (!mlang::compiler_api::fallbackDefinitionFromText(*current, line, column, fb)) {
            return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
        }
        *out_line = fb.line;
        *out_column = fb.column;
        *out_name_length = static_cast<int>(fb.name.size());
        const size_t copy_len =
            std::min(static_cast<size_t>(out_name_capacity - 1), fb.name.size());
        if (copy_len > 0) {
            std::memcpy(out_name, fb.name.data(), copy_len);
        }
        out_name[copy_len] = '\0';
        *out_uri_length = static_cast<int>(current->uri.size());
        const size_t uri_copy_len =
            std::min(static_cast<size_t>(out_uri_capacity - 1), current->uri.size());
        if (uri_copy_len > 0) {
            std::memcpy(out_uri, current->uri.data(), uri_copy_len);
        }
        out_uri[uri_copy_len] = '\0';
        return static_cast<int>(mlang::compiler_api::Status::Ok);
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

    *out_uri_length = static_cast<int>(def->symbol.uri.size());
    const size_t uri_copy_len =
        std::min(static_cast<size_t>(out_uri_capacity - 1), def->symbol.uri.size());
    if (uri_copy_len > 0) {
        std::memcpy(out_uri, def->symbol.uri.data(), uri_copy_len);
    }
    out_uri[uri_copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_definition_id(mlang_compiler_session* session,
                                            const char* uri,
                                            int line,
                                            int column,
                                            char* out_id,
                                            int out_id_capacity,
                                            int* out_id_length) {
    if (session == nullptr || uri == nullptr || out_id == nullptr || out_id_capacity <= 0 ||
        out_id_length == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const auto def = mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
    if (!def.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }

    const std::string& id = def->symbol.stable_id;
    *out_id_length = static_cast<int>(id.size());
    const size_t copy_len = std::min(static_cast<size_t>(out_id_capacity - 1), id.size());
    if (copy_len > 0) {
        std::memcpy(out_id, id.data(), copy_len);
    }
    out_id[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_reference_count(mlang_compiler_session* session,
                                              const char* uri,
                                              int line,
                                              int column,
                                              int* out_count) {
    if (session == nullptr || uri == nullptr || out_count == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const auto target = mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
    if (!target.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }

    const auto refs = mlang::compiler_api::collectReferencesForSymbol(target->symbol, sem_docs);
    *out_count = static_cast<int>(refs.size());
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_reference_get(mlang_compiler_session* session,
                                            const char* uri,
                                            int line,
                                            int column,
                                            int index,
                                            char* out_ref_uri,
                                            int out_ref_uri_capacity,
                                            int* out_ref_uri_length,
                                            int* out_ref_line,
                                            int* out_ref_column) {
    if (session == nullptr || uri == nullptr || out_ref_uri == nullptr ||
        out_ref_uri_capacity <= 0 || out_ref_uri_length == nullptr ||
        out_ref_line == nullptr || out_ref_column == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const auto target = mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
    if (!target.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }
    const auto refs = mlang::compiler_api::collectReferencesForSymbol(target->symbol, sem_docs);
    if (index < 0 || static_cast<size_t>(index) >= refs.size()) {
        return static_cast<int>(mlang::compiler_api::Status::OutOfRange);
    }

    const auto& ref = refs[static_cast<size_t>(index)];
    *out_ref_line = ref.line;
    *out_ref_column = ref.column;
    *out_ref_uri_length = static_cast<int>(ref.uri.size());
    const size_t copy_len = std::min(static_cast<size_t>(out_ref_uri_capacity - 1), ref.uri.size());
    if (copy_len > 0) {
        std::memcpy(out_ref_uri, ref.uri.data(), copy_len);
    }
    out_ref_uri[copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_document_rename_is_safe(mlang_compiler_session* session,
                                             const char* uri,
                                             int line,
                                             int column,
                                             const char* new_name,
                                             int* out_is_safe) {
    if (session == nullptr || uri == nullptr || new_name == nullptr || out_is_safe == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store;
    std::vector<mlang::compiler_api::DocumentSemantic> sem_docs;
    const mlang::compiler_api::DocumentSemantic* current = nullptr;
    const int prep = prepare_semantic_query(session, uri, store, sem_docs, &current);
    if (prep != static_cast<int>(mlang::compiler_api::Status::Ok)) {
        return prep;
    }

    const auto target = mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
    if (!target.has_value()) {
        return static_cast<int>(mlang::compiler_api::Status::SymbolNotFound);
    }

    *out_is_safe = mlang::compiler_api::isRenameSafeForSymbol(
                       target->symbol, std::string_view(new_name), sem_docs)
                       ? 1
                       : 0;
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

int __mlang_compiler_semantic_cache_warm(mlang_compiler_session* session,
                                         const char* uri) {
    if (session == nullptr || uri == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }
    const mlang::compiler_api::Status status = store->warmSemanticCacheForUri(uri);
    return static_cast<int>(status);
}

int __mlang_compiler_semantic_cache_clear(mlang_compiler_session* session) {
    if (session == nullptr) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidArgument);
    }

    std::shared_ptr<mlang::compiler_api::SessionStore> store =
        mlang::compiler_api::GlobalStore::instance().getSession(session->id);
    if (!store) {
        return static_cast<int>(mlang::compiler_api::Status::InvalidSession);
    }
    const mlang::compiler_api::Status status = store->clearSemanticCache();
    return static_cast<int>(status);
}

}  // extern "C"
