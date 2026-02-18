#include "ast.h"

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
        return 1;
    }
    size_t pos = 0;
    while (true) {
        pos = line.find(name, pos);
        if (pos == std::string_view::npos) {
            return 1;
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

static ProgramNode* parseProgramFromText(std::string_view text) {
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
    }

    programRoot = saved_root;
    return parsed;
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
        s.column = 1;
    }
    out.symbols.push_back(std::move(s));
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
    ProgramNode* program = parseProgramFromText(doc.text);
    if (!program) {
        return std::nullopt;
    }

    DocumentSemantic out;
    out.uri = doc.uri;
    out.text = doc.text;
    out.line_depths = computeLineDepths(doc.text);
    const std::vector<std::string_view> lines = splitLines(doc.text);

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
        }
    }

    if (program->functionList) {
        for (FunctionDefNode* fn : program->functionList->functions) {
            if (!fn || fn->isExtern) {
                continue;
            }
            const int fn_line = fn->line > 0 ? fn->line : 1;
            addSemanticSymbol(out, lines, fn->name, 1, fn_line, 0,
                              typeToString(fn->returnType),
                              functionSignatureFromAst(fn));

            if (fn->parameters) {
                for (ParameterNode* p : fn->parameters->parameters) {
                    if (!p) {
                        continue;
                    }
                    addSemanticSymbol(out, lines, p->name, 2, fn_line, 1,
                                      typeToString(p->type), {});
                }
            }
            collectStatementSymbols(out, lines, fn->body, 1);
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
    const std::optional<std::string> token =
        tokenAtOffset(current.text, *offset);
    if (!token.has_value()) {
        return std::nullopt;
    }

    const int query_depth = lineDepthAt(current, line);

    // Prefer closest visible local variable declaration.
    const SemanticSymbol* best_local_var = nullptr;
    for (const SemanticSymbol& sym : current.symbols) {
        if (sym.name != *token || sym.kind != 2) {
            continue;
        }
        const int sym_depth = lineDepthAt(current, sym.line);
        if (sym.line > line || sym_depth > query_depth) {
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
        return ResolvedQuerySymbol{*best_local_var, 1, true};
    }

    // Then global-like declarations in current document.
    const SemanticSymbol* first_local = nullptr;
    int overload_count = 0;
    for (const SemanticSymbol& sym : current.symbols) {
        if (sym.name != *token) {
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
        if (sym.name != *token) {
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

    const std::vector<mlang::compiler_api::DocumentState> docs = store->snapshotDocuments();
    const std::vector<mlang::compiler_api::DocumentSemantic> sem_docs =
        mlang::compiler_api::buildSemanticSnapshot(docs);
    const mlang::compiler_api::DocumentSemantic* current =
        mlang::compiler_api::findSemanticDoc(sem_docs, uri);
    if (!current) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }
    if (!current->ast_valid) {
        return static_cast<int>(mlang::compiler_api::Status::Unsupported);
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
    const std::vector<mlang::compiler_api::DocumentSemantic> sem_docs =
        mlang::compiler_api::buildSemanticSnapshot(docs);
    const mlang::compiler_api::DocumentSemantic* current =
        mlang::compiler_api::findSemanticDoc(sem_docs, uri);
    if (!current) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }
    if (!current->ast_valid) {
        return static_cast<int>(mlang::compiler_api::Status::Unsupported);
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
    const std::vector<mlang::compiler_api::DocumentSemantic> sem_docs =
        mlang::compiler_api::buildSemanticSnapshot(docs);
    const mlang::compiler_api::DocumentSemantic* current =
        mlang::compiler_api::findSemanticDoc(sem_docs, uri);
    if (!current) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }
    if (!current->ast_valid) {
        return static_cast<int>(mlang::compiler_api::Status::Unsupported);
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
    const std::vector<mlang::compiler_api::DocumentSemantic> sem_docs =
        mlang::compiler_api::buildSemanticSnapshot(docs);
    const mlang::compiler_api::DocumentSemantic* current =
        mlang::compiler_api::findSemanticDoc(sem_docs, uri);
    if (!current) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }
    if (!current->ast_valid) {
        return static_cast<int>(mlang::compiler_api::Status::Unsupported);
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
    const std::vector<mlang::compiler_api::DocumentSemantic> sem_docs =
        mlang::compiler_api::buildSemanticSnapshot(docs);
    const mlang::compiler_api::DocumentSemantic* current =
        mlang::compiler_api::findSemanticDoc(sem_docs, uri);
    if (!current) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }
    if (!current->ast_valid) {
        return static_cast<int>(mlang::compiler_api::Status::Unsupported);
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
    const std::vector<mlang::compiler_api::DocumentSemantic> sem_docs =
        mlang::compiler_api::buildSemanticSnapshot(docs);
    const mlang::compiler_api::DocumentSemantic* current =
        mlang::compiler_api::findSemanticDoc(sem_docs, uri);
    if (!current) {
        return static_cast<int>(mlang::compiler_api::Status::DocumentNotFound);
    }
    if (!current->ast_valid) {
        return static_cast<int>(mlang::compiler_api::Status::Unsupported);
    }

    const std::optional<mlang::compiler_api::ResolvedQuerySymbol> def =
        mlang::compiler_api::resolveSymbolAtPosition(*current, sem_docs, line, column);
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

    *out_uri_length = static_cast<int>(def->symbol.uri.size());
    const size_t uri_copy_len =
        std::min(static_cast<size_t>(out_uri_capacity - 1), def->symbol.uri.size());
    if (uri_copy_len > 0) {
        std::memcpy(out_uri, def->symbol.uri.data(), uri_copy_len);
    }
    out_uri[uri_copy_len] = '\0';
    return static_cast<int>(mlang::compiler_api::Status::Ok);
}

}  // extern "C"
