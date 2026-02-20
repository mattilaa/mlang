#include "ast.h"
#include "mlang_constants.h"
#include "module.h"
#include "incremental_compiler.h"
#include "ide_query.h"
#include "workspace_graph.h"
#include "runtime_concurrency.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

extern "C" {
struct mlang_compiler_session;
int __mlang_compiler_session_create(mlang_compiler_session** out_session);
int __mlang_compiler_session_destroy(mlang_compiler_session* session);
int __mlang_compiler_document_open(mlang_compiler_session* session,
                                   const char* uri,
                                   const char* language_id,
                                   const char* text,
                                   int version);
int __mlang_compiler_document_change(mlang_compiler_session* session,
                                     const char* uri,
                                     const char* text,
                                     int version);
int __mlang_compiler_document_close(mlang_compiler_session* session,
                                    const char* uri);
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
                                             int* out_from_current_document);
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
int __mlang_compiler_semantic_cache_clear(mlang_compiler_session* session);
}

struct Location
{
    std::string uri;
    int line = 0;
    int character = 0;
};

struct FieldInfo
{
    std::string name;
    std::string typeName;
    Location loc;
};

struct MethodInfo
{
    std::string name;
    std::string returnType;
    Location loc;
};

struct StructInfo
{
    std::string name;
    std::string baseName;
    std::unordered_map<std::string, FieldInfo> fields;
    std::unordered_map<std::string, MethodInfo> methods;
    Location loc;
    int startLine = -1;
    int endLine = -1;
};

struct EnumVariantInfo
{
    std::string name;
    Location loc;
};

struct EnumInfo
{
    std::string name;
    std::unordered_map<std::string, EnumVariantInfo> variants;
    Location loc;
    int startLine = -1;
    int endLine = -1;
};

struct FunctionInfo
{
    std::string name;
    std::string returnType;
    Location loc;
    int startLine = -1;
    int endLine = -1;
    bool isExtern = false;
    std::unordered_map<std::string, std::string> varTypes;
    std::unordered_map<std::string, Location> varDecls;
    std::unordered_map<std::string, Location> paramDecls;
    std::string ownerStruct;
};

struct UseImport
{
    std::string moduleName;
    std::string itemName;
    bool importAll = false;
};

struct FileInfo
{
    std::string path;
    std::string uri;
    std::string text;
    std::vector<std::string> lines;
    ProgramNode* ast = nullptr;
    std::unordered_map<std::string, StructInfo> structs;
    std::unordered_map<std::string, EnumInfo> enums;
    std::unordered_map<std::string, FunctionInfo> functions;
    std::vector<FunctionInfo*> functionSpans;
    std::vector<std::string> moduleDecls;
    std::vector<UseImport> imports;
};

static std::string read_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
        return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static std::string shell_quote(const std::string& input)
{
    std::string out = "'";
    for(char c : input)
    {
        if(c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

static bool is_mlang_source_path(const std::filesystem::path& path)
{
    auto ext = path.extension();
    return ext == ".mla" || ext == ".mlastub";
}

static std::string path_to_uri(const std::string& path)
{
    std::string out = "file://";
    for(char c : path)
    {
        if(c == ' ')
            out += "%20";
        else
            out.push_back(c);
    }
    return out;
}

static std::string uri_to_path(const std::string& uri)
{
    std::string out = uri;
    const std::string prefix = "file://";
    if(out.starts_with(prefix))
        out = out.substr(prefix.size());
    std::string decoded;
    decoded.reserve(out.size());
    for(size_t i = 0; i < out.size(); ++i)
    {
        if(out[i] == '%' && i + 2 < out.size())
        {
            auto hex = [](char c) -> int {
                if(c >= '0' && c <= '9')
                    return c - '0';
                if(c >= 'a' && c <= 'f')
                    return 10 + (c - 'a');
                if(c >= 'A' && c <= 'F')
                    return 10 + (c - 'A');
                return -1;
            };
            int hi = hex(out[i + 1]);
            int lo = hex(out[i + 2]);
            if(hi >= 0 && lo >= 0)
            {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(out[i]);
    }
    return decoded;
}

static std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string cur;
    for(char c : text)
    {
        if(c == '\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else if(c != '\r')
        {
            cur.push_back(c);
        }
    }
    lines.push_back(cur);
    return lines;
}

static std::string strip_comments_strings(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    enum class State
    {
        Code,
        String,
        LineComment,
        BlockComment
    };
    State state = State::Code;
    for(size_t i = 0; i < text.size();)
    {
        char c = text[i];
        if(state == State::Code)
        {
            if(c == '"')
            {
                out.push_back(' ');
                state = State::String;
                ++i;
            }
            else if(c == '/' && i + 1 < text.size() && text[i + 1] == '/')
            {
                out.push_back(' ');
                out.push_back(' ');
                i += 2;
                state = State::LineComment;
            }
            else if(c == '/' && i + 1 < text.size() && text[i + 1] == '*')
            {
                out.push_back(' ');
                out.push_back(' ');
                i += 2;
                state = State::BlockComment;
            }
            else
            {
                out.push_back(c);
                ++i;
            }
        }
        else if(state == State::String)
        {
            if(c == '\\' && i + 1 < text.size())
            {
                out.push_back(' ');
                out.push_back(' ');
                i += 2;
            }
            else if(c == '"')
            {
                out.push_back(' ');
                ++i;
                state = State::Code;
            }
            else
            {
                out.push_back(c == '\n' ? '\n' : ' ');
                ++i;
            }
        }
        else if(state == State::LineComment)
        {
            if(c == '\n')
            {
                out.push_back('\n');
                ++i;
                state = State::Code;
            }
            else
            {
                out.push_back(' ');
                ++i;
            }
        }
        else if(state == State::BlockComment)
        {
            if(c == '*' && i + 1 < text.size() && text[i + 1] == '/')
            {
                out.push_back(' ');
                out.push_back(' ');
                i += 2;
                state = State::Code;
            }
            else
            {
                out.push_back(c == '\n' ? '\n' : ' ');
                ++i;
            }
        }
    }
    return out;
}

struct Span
{
    std::string name;
    int startLine = -1;
    int endLine = -1;
};

static std::vector<Span> find_spans(const std::vector<std::string>& lines,
                                   const std::regex& startRegex)
{
    std::vector<Span> spans;
    for(int i = 0; i < (int)lines.size(); ++i)
    {
        std::smatch m;
        if(!std::regex_search(lines[i], m, startRegex))
            continue;
        Span span;
        span.name = m[1].str();
        span.startLine = i;
        int depth = 0;
        bool sawOpen = false;
        for(int l = i; l < (int)lines.size(); ++l)
        {
            for(char c : lines[l])
            {
                if(c == '{')
                {
                    depth++;
                    sawOpen = true;
                }
                else if(c == '}')
                {
                    if(depth > 0)
                        depth--;
                    if(sawOpen && depth == 0)
                    {
                        span.endLine = l;
                        break;
                    }
                }
            }
            if(span.endLine != -1)
                break;
        }
        spans.push_back(span);
    }
    return spans;
}

static std::optional<Location> find_definition_location(
    const std::vector<std::string>& lines, int start, int end,
    const std::regex& regex)
{
    int lstart = std::max(0, start);
    int lend = end < 0 ? (int)lines.size() - 1 : end;
    for(int i = lstart; i <= lend && i < (int)lines.size(); ++i)
    {
        std::smatch m;
        if(std::regex_search(lines[i], m, regex))
        {
            Location loc;
            loc.line = i;
            loc.character = (int)m.position(1);
            return loc;
        }
    }
    return std::nullopt;
}

static std::string type_name(TypeNode* node)
{
    if(!node)
        return {};
    if(node->kind == TypeNode::TYPE_STRUCT)
    {
        if(auto* s = dynamic_cast<StructTypeRefNode*>(node))
            return s->structName;
        if(auto* g = dynamic_cast<GenericStructTypeRefNode*>(node))
            return g->structName;
    }
    return {};
}

class LspServer
{
public:
    ~LspServer()
    {
        if(compilerSession)
            __mlang_compiler_session_destroy(compilerSession);
        compilerSession = nullptr;
    }

    void run()
    {
        for(;;)
        {
            auto payload = read_message();
            if(!payload)
                break;
            auto json = llvm::json::parse(*payload);
            if(!json)
                continue;
            auto* obj = json->getAsObject();
            if(!obj)
                continue;
            if(auto method = obj->getString("method"))
            {
                if(obj->get("id"))
                    handle_request(*method, *obj);
                else
                    handle_notification(*method, *obj);
            }
        }
    }

private:
    std::unordered_map<std::string, FileInfo> files;
    mlang::IncrementalCompiler incrementalCompiler;
    mlang::ide::WorkspaceGraph workspaceGraph;
    mlang::runtime::RuntimeScheduler runtimeScheduler;
    std::unordered_map<std::string, std::string> functionReturns;
    std::string rootPath;
    std::string mlangCommandsPath;
    std::filesystem::file_time_type mlangCommandsMtime{};
    bool mlangCommandsLoaded = false;
    std::vector<std::string> cHeaderNames;
    std::vector<std::string> cIncludeDirs;
    std::vector<std::string> cHeaderPaths;
    std::unordered_map<std::string, std::string> cTypeMap;
    std::unordered_map<std::string, Location> cSymbolCache;
    std::unordered_map<std::string, std::string> diagnosticResultIds;
    std::unordered_map<std::string, int> compilerDocVersions;
    std::unordered_set<std::string> openDocumentUris;
    mlang_compiler_session* compilerSession = nullptr;
    bool cHeadersLoaded = false;
    bool cHeaderDebug = false;
    std::string cHeaderDebugLog;
    int compilerMutationCount = 0;
    int compilerCacheClearInterval = 256;
    std::uint64_t telemetryCacheClears = 0;
    std::uint64_t telemetryEvictions = 0;

    llvm::json::Value make_position_value(int line, int character)
    {
        llvm::json::Object pos;
        pos["line"] = line;
        pos["character"] = character;
        return llvm::json::Value(std::move(pos));
    }

    llvm::json::Value make_range_value(int line, int character, int endChar)
    {
        llvm::json::Object range;
        range["start"] = make_position_value(line, character);
        range["end"] = make_position_value(line, endChar);
        return llvm::json::Value(std::move(range));
    }

    llvm::json::Value make_range_value(int startLine, int startChar,
                                       int endLine, int endChar)
    {
        llvm::json::Object range;
        range["start"] = make_position_value(startLine, startChar);
        range["end"] = make_position_value(endLine, endChar);
        return llvm::json::Value(std::move(range));
    }

    static std::string trim(std::string s)
    {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        size_t start = 0;
        while(start < s.size() && is_space((unsigned char)s[start]))
            ++start;
        size_t end = s.size();
        while(end > start && is_space((unsigned char)s[end - 1]))
            --end;
        return s.substr(start, end - start);
    }

    bool ensure_compiler_session()
    {
        if(compilerSession)
            return true;
        return __mlang_compiler_session_create(&compilerSession) == 0 &&
               compilerSession != nullptr;
    }

    static bool is_file_uri(std::string_view uri)
    {
        return uri.rfind("file://", 0) == 0;
    }

    bool is_path_under_root(const std::string& path) const
    {
        if(rootPath.empty() || path.empty())
            return false;
        std::error_code ec;
        std::filesystem::path rp = std::filesystem::weakly_canonical(rootPath, ec);
        if(ec)
            return false;
        std::filesystem::path p = std::filesystem::weakly_canonical(path, ec);
        if(ec)
            return false;

        const auto rootItEnd = rp.end();
        auto rootIt = rp.begin();
        auto pathIt = p.begin();
        for(; rootIt != rootItEnd; ++rootIt, ++pathIt)
        {
            if(pathIt == p.end() || *pathIt != *rootIt)
                return false;
        }
        return true;
    }

    void maybe_clear_compiler_semantic_cache()
    {
        if(!compilerSession)
            return;
        if(compilerCacheClearInterval <= 0)
            return;
        if((compilerMutationCount % compilerCacheClearInterval) != 0)
            return;
        if(__mlang_compiler_semantic_cache_clear(compilerSession) == 0)
        {
            ++telemetryCacheClears;
            log_runtime_telemetry("cache_clear");
        }
    }

    void mark_compiler_mutation()
    {
        ++compilerMutationCount;
        maybe_clear_compiler_semantic_cache();
    }

    void prune_closed_document_indexes(const std::string& uri)
    {
        auto it = files.find(uri);
        if(it == files.end())
            return;

        const std::string path = uri_to_path(uri);
        bool keepIndexed = false;
        if(is_file_uri(uri))
        {
            std::error_code ec;
            if(!path.empty() && std::filesystem::exists(path, ec) && !ec &&
               is_path_under_root(path))
            {
                keepIndexed = true;
            }
        }

        if(keepIndexed)
            return;

        workspaceGraph.removeDocument(uri);
        files.erase(it);
        ++telemetryEvictions;
        log_runtime_telemetry("evict_closed_doc", uri);
    }

    void compiler_open_or_update(const std::string& uri,
                                 const std::string& text,
                                 std::optional<int> explicitVersion)
    {
        if(!ensure_compiler_session())
            return;

        int version = explicitVersion.has_value() ? *explicitVersion : 1;
        auto it = compilerDocVersions.find(uri);
        if(it == compilerDocVersions.end())
        {
            if(version <= 0)
                version = 1;
            int status = __mlang_compiler_document_open(
                compilerSession, uri.c_str(), "mlang", text.c_str(), version);
            if(status == 0)
            {
                compilerDocVersions[uri] = version;
                mark_compiler_mutation();
            }
            return;
        }

        if(version <= it->second)
            version = it->second + 1;
        int status = __mlang_compiler_document_change(
            compilerSession, uri.c_str(), text.c_str(), version);
        if(status == 0)
        {
            compilerDocVersions[uri] = version;
            mark_compiler_mutation();
        }
    }

    void compiler_close(const std::string& uri)
    {
        if(!compilerSession)
            return;
        __mlang_compiler_document_close(compilerSession, uri.c_str());
        compilerDocVersions.erase(uri);
        mark_compiler_mutation();
    }

    struct ResolvedSemanticSymbol
    {
        std::string name;
        std::string uri;
        std::string id;
        std::string typeInfo;
        std::string signature;
        int kind = 0;
        int line0 = 0;
        int column0 = 0;
        int overloadCount = 1;
        bool fromCurrentDocument = false;
    };

    static const char* semantic_kind_name(int kind)
    {
        switch(kind)
        {
        case 1:
            return "Function";
        case 2:
            return "Variable";
        case 3:
            return "Struct";
        case 4:
            return "Module";
        default:
            return "Symbol";
        }
    }

    std::optional<ResolvedSemanticSymbol> resolve_semantic_symbol(
        const std::string& uri, int line0, int column0)
    {
        if(!compilerSession)
            return std::nullopt;

        char nameBuf[4096];
        char uriBuf[4096];
        char idBuf[4096];
        char typeBuf[4096];
        char sigBuf[4096];
        int nameLen = 0;
        int uriLen = 0;
        int idLen = 0;
        int typeLen = 0;
        int sigLen = 0;
        int kind = 0;
        int line1 = 0;
        int col1 = 0;
        int overloadCount = 1;
        int fromCurrentDoc = 0;

        int st = __mlang_compiler_document_resolve_symbol(
            compilerSession, uri.c_str(), line0 + 1, column0 + 1, nameBuf,
            sizeof(nameBuf), &nameLen, &kind, &line1, &col1, uriBuf,
            sizeof(uriBuf), &uriLen, idBuf, sizeof(idBuf), &idLen, typeBuf,
            sizeof(typeBuf), &typeLen, sigBuf, sizeof(sigBuf), &sigLen,
            &overloadCount, &fromCurrentDoc);
        if(st != 0)
            return std::nullopt;

        ResolvedSemanticSymbol out;
        out.name = nameBuf;
        out.uri = uriBuf;
        out.id = idBuf;
        out.typeInfo = typeBuf;
        out.signature = sigBuf;
        out.kind = kind;
        out.line0 = std::max(0, line1 - 1);
        out.column0 = std::max(0, col1 - 1);
        out.overloadCount = std::max(1, overloadCount);
        out.fromCurrentDocument = fromCurrentDoc != 0;
        return out;
    }

    void load_c_header_config()
    {
        cHeaderNames.clear();
        cIncludeDirs.clear();
        cHeaderPaths.clear();
        cTypeMap.clear();
        cSymbolCache.clear();
        cHeadersLoaded = true;

        std::filesystem::path configPath =
            std::filesystem::path(rootPath) / ".mlang-c-headers";
        if(!std::filesystem::exists(configPath))
        {
            cHeaderNames = {"stdio.h", "stdlib.h", "stdint.h", "stdbool.h",
                            "math.h"};
            cIncludeDirs = {rootPath, "/usr/include", "/usr/local/include",
                            "/opt/homebrew/include"};
            cTypeMap = {
                {"i8", "int8_t"},
                {"i16", "int16_t"},
                {"i32", "int32_t"},
                {"i64", "int64_t"},
                {"u8", "uint8_t"},
                {"u16", "uint16_t"},
                {"u32", "uint32_t"},
                {"u64", "uint64_t"},
                {"int", "int32_t"},
                {"bool", "bool"},
                {"float", "mlang_float"},
                {"double", "mlang_double"},
                {"string", "mlang_string"},
                {"str8", "mlang_str8"},
                {"str16", "mlang_str16"},
            };
            add_sdk_include_dirs();
            resolve_c_headers();
            if(cHeaderDebug)
                log_c_headers();
            return;
        }

        std::ifstream f(configPath);
        if(!f)
            return;
        std::string line;
        while(std::getline(f, line))
        {
            line = trim(line);
            if(line.empty() || line[0] == '#')
                continue;
            if(line.starts_with("-I"))
            {
                std::string dir = trim(line.substr(2));
                if(!dir.empty())
                    cIncludeDirs.push_back(dir);
                continue;
            }
            const std::string includePrefix = "include_dir:";
            const std::string headerPrefix = "header:";
            const std::string typeMapPrefix = "type_map:";
            if(line.starts_with(includePrefix))
            {
                std::string dir = trim(line.substr(includePrefix.size()));
                if(!dir.empty())
                    cIncludeDirs.push_back(dir);
                continue;
            }
            if(line.starts_with(headerPrefix))
            {
                std::string hdr = trim(line.substr(headerPrefix.size()));
                if(!hdr.empty())
                    cHeaderNames.push_back(hdr);
                continue;
            }
            if(line.starts_with(typeMapPrefix))
            {
                std::string maps = trim(line.substr(typeMapPrefix.size()));
                size_t start = 0;
                while(start < maps.size())
                {
                    size_t comma = maps.find(',', start);
                    std::string entry = trim(maps.substr(
                        start, comma == std::string::npos ? std::string::npos
                                                          : comma - start));
                    if(!entry.empty())
                    {
                        size_t eq = entry.find('=');
                        if(eq != std::string::npos)
                        {
                            std::string lhs = trim(entry.substr(0, eq));
                            std::string rhs = trim(entry.substr(eq + 1));
                            if(!lhs.empty() && !rhs.empty())
                                cTypeMap[lhs] = rhs;
                        }
                    }
                    if(comma == std::string::npos)
                        break;
                    start = comma + 1;
                }
                continue;
            }
            cHeaderNames.push_back(line);
        }

        if(cIncludeDirs.empty())
        {
            cIncludeDirs = {rootPath, "/usr/include", "/usr/local/include",
                            "/opt/homebrew/include"};
        }
        if(cTypeMap.empty())
        {
            cTypeMap = {
                {"i8", "int8_t"},
                {"i16", "int16_t"},
                {"i32", "int32_t"},
                {"i64", "int64_t"},
                {"u8", "uint8_t"},
                {"u16", "uint16_t"},
                {"u32", "uint32_t"},
                {"u64", "uint64_t"},
                {"int", "int32_t"},
                {"bool", "bool"},
                {"float", "mlang_float"},
                {"double", "mlang_double"},
                {"string", "mlang_string"},
                {"str8", "mlang_str8"},
                {"str16", "mlang_str16"},
            };
        }
        add_sdk_include_dirs();
        resolve_c_headers();
        if(cHeaderDebug)
            log_c_headers();
    }

    void resolve_c_headers()
    {
        cHeaderPaths.clear();
        for(const auto& header : cHeaderNames)
        {
            std::filesystem::path p(header);
            if(p.is_absolute())
            {
                if(std::filesystem::exists(p))
                    cHeaderPaths.push_back(p.string());
                continue;
            }
            for(const auto& dir : cIncludeDirs)
            {
                std::filesystem::path base(dir);
                if(!base.is_absolute())
                    base = std::filesystem::path(rootPath) / base;
                std::filesystem::path cand = base / header;
                if(std::filesystem::exists(cand))
                {
                    cHeaderPaths.push_back(cand.string());
                    break;
                }
            }
        }
    }

    void add_sdk_include_dirs()
    {
        auto add_dir = [&](const std::string& dir) {
            if(dir.empty())
                return;
            std::error_code ec;
            if(!std::filesystem::exists(dir, ec))
                return;
            for(const auto& existing : cIncludeDirs)
            {
                if(existing == dir)
                    return;
            }
            cIncludeDirs.push_back(dir);
        };

        if(const char* sdkRoot = std::getenv("SDKROOT"))
        {
            add_dir(std::string(sdkRoot) + "/usr/include");
        }
        add_dir("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include");
        add_dir("/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include");
    }

    static bool is_c_source_path(const std::filesystem::path& path)
    {
        auto ext = path.extension().string();
        return ext == ".h" || ext == ".hpp" || ext == ".hh" ||
               ext == ".c" || ext == ".cc" || ext == ".cxx" ||
               ext == ".cpp";
    }

    std::optional<Location> find_c_symbol_in_workspace(const std::string& name)
    {
        if(rootPath.empty())
            return std::nullopt;

        // Prefer likely definition forms before generic declaration matches.
        std::regex defRx("\\b" + name + "\\s*\\([^;]*\\)\\s*\\{");
        std::regex declRx("\\b" + name + "\\s*\\(");

        auto search = [&](const std::regex& rx,
                          const std::filesystem::path& path)
            -> std::optional<Location> {
            std::ifstream f(path);
            if(!f)
                return std::nullopt;
            std::string line;
            int lineNo = 0;
            while(std::getline(f, line))
            {
                std::smatch match;
                if(std::regex_search(line, match, rx))
                {
                    Location loc;
                    loc.uri = path_to_uri(path.string());
                    loc.line = lineNo;
                    loc.character = (int)match.position();
                    return loc;
                }
                ++lineNo;
            }
            return std::nullopt;
        };

        auto scan = [&](const std::regex& rx) -> std::optional<Location> {
            std::error_code ec;
            for(auto it = std::filesystem::recursive_directory_iterator(
                    rootPath,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec);
                it != std::filesystem::recursive_directory_iterator(); ++it)
            {
                if(it->is_directory(ec))
                {
                    auto dirName = it->path().filename().string();
                    if(dirName == ".git" || dirName == "build" ||
                       dirName == "out" || dirName == "dist")
                    {
                        it.disable_recursion_pending();
                    }
                    continue;
                }

                if(!it->is_regular_file(ec))
                    continue;
                if(!is_c_source_path(it->path()))
                    continue;

                if(auto loc = search(rx, it->path()))
                    return loc;
            }
            return std::nullopt;
        };

        if(auto loc = scan(defRx))
            return loc;
        return scan(declRx);
    }

    void log_c_headers() const
    {
        std::string msg = "[mlangd] c headers: ";
        for(size_t i = 0; i < cHeaderPaths.size(); ++i)
        {
            if(i)
                msg += ", ";
            msg += cHeaderPaths[i];
        }
        debug_log(msg);
    }

    void debug_log(const std::string& msg) const
    {
        if(!cHeaderDebug)
            return;
        std::string path =
            cHeaderDebugLog.empty() ? "/tmp/mlang_lsp_debug.log"
                                    : cHeaderDebugLog;
        std::ofstream f(path, std::ios::app);
        if(f)
            f << msg << "\n";
    }

    void log_runtime_telemetry(const char* event,
                               const std::string& uri = std::string()) const
    {
        if(!cHeaderDebug)
            return;
        std::string msg = "[mlangd-telemetry] event=";
        msg += event ? event : "unknown";
        msg += " active_docs=" + std::to_string(openDocumentUris.size());
        msg += " indexed_files=" + std::to_string(files.size());
        msg += " compiler_docs=" + std::to_string(compilerDocVersions.size());
        msg += " cache_clears=" + std::to_string(telemetryCacheClears);
        msg += " evictions=" + std::to_string(telemetryEvictions);
        if(!uri.empty())
            msg += " uri=" + uri;
        debug_log(msg);
    }

    std::optional<Location> find_c_symbol_location(const std::string& name)
    {
        if(!cHeadersLoaded)
            load_c_header_config();
        if(auto it = cSymbolCache.find(name); it != cSymbolCache.end())
            return it->second;

        std::regex rx("\\b" + name + "\\s*\\(");
        for(const auto& path : cHeaderPaths)
        {
            std::ifstream f(path);
            if(!f)
                continue;
            std::string line;
            int lineNo = 0;
            while(std::getline(f, line))
            {
                std::smatch match;
                if(std::regex_search(line, match, rx))
                {
                    Location loc;
                    loc.uri = path_to_uri(path);
                    loc.line = lineNo;
                    loc.character = (int)match.position();
                    cSymbolCache[name] = loc;
                    return loc;
                }
                ++lineNo;
            }
        }
        if(auto loc = find_c_symbol_in_workspace(name))
        {
            cSymbolCache[name] = *loc;
            return loc;
        }

        debug_log("[mlangd] c symbol not found: " + name);
        return std::nullopt;
    }

    std::optional<Location> find_c_typedef_location(const std::string& name)
    {
        if(!cHeadersLoaded)
            load_c_header_config();

        std::regex typedefRx("\\btypedef\\b[^;]*\\b" + name + "\\b");
        std::regex usingRx("\\busing\\s+" + name + "\\s*=");
        for(const auto& path : cHeaderPaths)
        {
            std::ifstream f(path);
            if(!f)
                continue;
            std::string line;
            int lineNo = 0;
            while(std::getline(f, line))
            {
                std::smatch match;
                if(std::regex_search(line, match, typedefRx) ||
                   std::regex_search(line, match, usingRx))
                {
                    Location loc;
                    loc.uri = path_to_uri(path);
                    loc.line = lineNo;
                    loc.character = (int)match.position();
                    cSymbolCache[name] = loc;
                    return loc;
                }
                ++lineNo;
            }
        }
        debug_log("[mlangd] c typedef not found: " + name);
        return std::nullopt;
    }

    std::optional<Location> find_c_type_location(const std::string& typeName)
    {
        if(!cHeadersLoaded)
            load_c_header_config();
        auto it = cTypeMap.find(typeName);
        if(it == cTypeMap.end())
        {
            debug_log("[mlangd] c type not mapped: " + typeName);
            return std::nullopt;
        }
        if(auto loc = find_c_typedef_location(it->second))
            return loc;
        return find_c_symbol_location(it->second);
    }

    std::optional<std::string> read_message()
    {
        std::string line;
        size_t contentLength = 0;
        while(std::getline(std::cin, line))
        {
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            if(line.empty())
                break;
            if(line.starts_with("Content-Length:"))
            {
                contentLength = std::stoul(line.substr(15));
            }
        }
        if(contentLength == 0)
            return std::nullopt;
        std::string payload(contentLength, '\0');
        std::cin.read(payload.data(), (std::streamsize)contentLength);
        if(!std::cin)
            return std::nullopt;
        return payload;
    }

    std::optional<std::string> find_mlang_commands_path() const
    {
        if(rootPath.empty())
            return std::nullopt;
        std::filesystem::path root(rootPath);
        std::filesystem::path direct = root / "mlang_commands.json";
        std::error_code ec;
        if(std::filesystem::exists(direct, ec) && !ec)
            return direct.string();
        std::filesystem::path build = root / "build" / "mlang_commands.json";
        if(std::filesystem::exists(build, ec) && !ec)
            return build.string();
        return std::nullopt;
    }

    void refresh_mlang_commands_if_needed()
    {
        if(mlangCommandsPath.empty())
            return;
        std::error_code ec;
        if(!std::filesystem::exists(mlangCommandsPath, ec) || ec)
            return;
        auto mtime = std::filesystem::last_write_time(mlangCommandsPath, ec);
        if(ec)
            return;
        if(mlangCommandsLoaded && mtime == mlangCommandsMtime)
            return;
        load_mlang_commands(mlangCommandsPath);
        mlangCommandsMtime = mtime;
        mlangCommandsLoaded = true;
    }

    void load_mlang_commands(const std::string& path)
    {
        std::string text = read_file(path);
        if(text.empty())
            return;
        auto parsed = llvm::json::parse(text);
        if(!parsed)
            return;
        auto* obj = parsed->getAsObject();
        auto* arr = parsed->getAsArray();
        if(obj && !arr)
            arr = obj->getArray("files");
        if(!arr)
            return;

        std::vector<std::string> paths;
        std::unordered_set<std::string> seen;

        for(const auto& item : *arr)
        {
            if(auto s = item.getAsString())
            {
                std::string filePath = s->str();
                if(filePath.empty())
                    continue;

                std::string abs = filePath;
                if(!std::filesystem::path(filePath).is_absolute() &&
                   !rootPath.empty())
                {
                    abs = (std::filesystem::path(rootPath) / filePath).string();
                }
                if(!is_mlang_source_path(std::filesystem::path(abs)))
                    continue;
                if(seen.insert(abs).second)
                    paths.push_back(abs);
                continue;
            }

            auto* objItem = item.getAsObject();
            if(!objItem)
                continue;
            auto fileVal = objItem->getString("file");
            if(!fileVal)
                continue;

            std::string filePath = fileVal->str();
            auto dirVal = objItem->getString("directory");
            std::string abs = filePath;
            if(!std::filesystem::path(filePath).is_absolute())
            {
                if(dirVal)
                    abs = (std::filesystem::path(dirVal->str()) / filePath).string();
                else if(!rootPath.empty())
                    abs = (std::filesystem::path(rootPath) / filePath).string();
            }
            if(!is_mlang_source_path(std::filesystem::path(abs)))
                continue;
            if(seen.insert(abs).second)
                paths.push_back(abs);
        }

        auto loaded = runtimeScheduler.loadFiles(paths);
        for(const auto& file : loaded)
        {
            if(file.content.empty())
                continue;
            index_document(path_to_uri(file.path), file.content);
        }
    }

    std::vector<std::string>
    infer_provided_modules(const FileInfo& info) const
    {
        std::unordered_set<std::string> modules;
        for(const auto& mod : info.moduleDecls)
        {
            if(!mod.empty())
                modules.insert(mod);
        }

        std::filesystem::path path(info.path);
        if(path.filename() == "mod.mla")
        {
            std::string parent = path.parent_path().filename().string();
            if(!parent.empty())
                modules.insert(parent);
        }
        else
        {
            std::string stem = path.stem().string();
            if(!stem.empty())
                modules.insert(stem);
        }

        std::vector<std::string> out;
        out.reserve(modules.size());
        for(const auto& mod : modules)
            out.push_back(mod);
        std::sort(out.begin(), out.end());
        return out;
    }

    static std::string trim_ws(std::string_view s)
    {
        size_t start = 0;
        while(start < s.size() &&
              std::isspace(static_cast<unsigned char>(s[start])) != 0)
            ++start;
        size_t end = s.size();
        while(end > start &&
              std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)
            --end;
        return std::string(s.substr(start, end - start));
    }

    static std::string unquote(std::string_view v)
    {
        std::string t = trim_ws(v);
        if(t.size() >= 2 && t.front() == '"' && t.back() == '"')
            return t.substr(1, t.size() - 2);
        return t;
    }

    static std::vector<std::string> split_toml_array(std::string_view input)
    {
        std::vector<std::string> out;
        std::string cur;
        bool inQuotes = false;
        for(char c : input)
        {
            if(c == '"')
            {
                inQuotes = !inQuotes;
                cur.push_back(c);
                continue;
            }
            if(c == ',' && !inQuotes)
            {
                out.push_back(trim_ws(cur));
                cur.clear();
                continue;
            }
            cur.push_back(c);
        }
        if(!cur.empty())
            out.push_back(trim_ws(cur));
        return out;
    }

    static std::optional<std::filesystem::path>
    find_manifest_path(std::filesystem::path startDir)
    {
        std::error_code ec;
        startDir = std::filesystem::absolute(startDir, ec);
        if(ec)
            return std::nullopt;

        std::filesystem::path cur = startDir;
        while(!cur.empty())
        {
            const auto candidate = cur / "mlang.toml";
            if(std::filesystem::exists(candidate))
                return candidate;
            const auto parent = cur.parent_path();
            if(parent == cur)
                break;
            cur = parent;
        }
        return std::nullopt;
    }

    static std::vector<std::string>
    parse_module_paths_from_toml(const std::filesystem::path& manifestPath)
    {
        std::ifstream in(manifestPath, std::ios::binary);
        if(!in)
            return {};

        std::vector<std::string> out;
        std::string line;
        std::string section;
        while(std::getline(in, line))
        {
            std::string t = trim_ws(line);
            if(t.empty() || t[0] == '#')
                continue;
            if(t.front() == '[' && t.back() == ']')
            {
                section = t.substr(1, t.size() - 2);
                continue;
            }
            if(section != "package" && section != "tool.mlang")
                continue;
            const size_t eq = t.find('=');
            if(eq == std::string::npos)
                continue;
            const std::string key = trim_ws(t.substr(0, eq));
            if(key != "module_paths")
                continue;
            const std::string value = trim_ws(t.substr(eq + 1));
            if(value.empty())
                continue;
            if(value.front() == '[' && value.back() == ']')
            {
                std::string inner = value.substr(1, value.size() - 2);
                for(const auto& part : split_toml_array(inner))
                {
                    std::string v = unquote(part);
                    if(!v.empty())
                        out.push_back(v);
                }
            }
            else
            {
                std::string v = unquote(value);
                if(!v.empty())
                    out.push_back(v);
            }
        }
        return out;
    }

    std::vector<std::string> default_stdlib_paths() const
    {
        std::vector<std::string> paths;
        if(const char* env = std::getenv("MLANG_STDLIB_PATH"))
            paths.emplace_back(env);
        if(!rootPath.empty())
            paths.emplace_back((std::filesystem::path(rootPath) / "stdlib").string());
        if(const char* xdg = std::getenv("XDG_DATA_HOME"))
            paths.emplace_back(std::string(xdg) + "/mlang/stdlib");
        if(const char* home = std::getenv("HOME"))
            paths.emplace_back(std::string(home) + "/.local/share/mlang/stdlib");
        paths.emplace_back("/usr/local/share/mlang/stdlib");
        paths.emplace_back("/usr/share/mlang/stdlib");
        return paths;
    }

    std::vector<std::string> module_search_paths_for_file(const FileInfo& info) const
    {
        namespace fs = std::filesystem;
        std::vector<std::string> out;
        std::unordered_set<std::string> seen;

        std::error_code ec;
        fs::path base = fs::path(info.path).parent_path();
        if(base.empty())
            base = ".";
        fs::path baseAbs = fs::absolute(base, ec);
        std::string baseStr = (!ec ? baseAbs.lexically_normal().string() : base.string());
        if(seen.insert(baseStr).second)
            out.push_back(baseStr);

        if(const auto manifest = find_manifest_path(base); manifest.has_value())
        {
            std::vector<std::string> mps = parse_module_paths_from_toml(*manifest);
            const fs::path manifestDir = manifest->parent_path();
            for(auto& mp : mps)
            {
                fs::path p = fs::path(mp);
                if(!p.is_absolute())
                    p = manifestDir / p;
                std::error_code pec;
                fs::path abs = fs::absolute(p, pec);
                std::string v = (!pec ? abs.lexically_normal().string() : p.string());
                if(seen.insert(v).second)
                    out.push_back(v);
            }
        }

        for(const auto& p : default_stdlib_paths())
        {
            if(!p.empty() && seen.insert(p).second)
                out.push_back(p);
        }

        if(!rootPath.empty())
        {
            std::string rp = fs::path(rootPath).lexically_normal().string();
            if(seen.insert(rp).second)
                out.push_back(rp);
        }
        return out;
    }

    std::vector<std::filesystem::path>
    search_base_paths_for_lookup(const FileInfo* contextFile) const
    {
        std::vector<std::filesystem::path> out;
        std::unordered_set<std::string> seen;

        auto add_path = [&](std::filesystem::path p) {
            if(p.empty())
                return;
            std::error_code ec;
            p = std::filesystem::absolute(p, ec);
            if(ec)
                return;
            p = p.lexically_normal();
            std::string key = p.string();
            if(seen.insert(key).second)
                out.push_back(p);
        };

        if(contextFile && !contextFile->path.empty())
        {
            for(const auto& root : module_search_paths_for_file(*contextFile))
                add_path(root);
        }

        for(const auto& [_, info] : files)
        {
            if(info.path.empty())
                continue;
            for(const auto& root : module_search_paths_for_file(info))
                add_path(root);
        }

        if(!rootPath.empty())
            add_path(rootPath);
        add_path(std::filesystem::current_path());
        return out;
    }

    static std::string module_name_to_rel_dir(const std::string& moduleName)
    {
        std::string rel;
        rel.reserve(moduleName.size() + 2);
        for(size_t i = 0; i < moduleName.size();)
        {
            if(i + 1 < moduleName.size() && moduleName[i] == ':' &&
               moduleName[i + 1] == ':')
            {
                rel.push_back('/');
                i += 2;
                continue;
            }
            rel.push_back(moduleName[i]);
            ++i;
        }
        return rel;
    }

    void update_workspace_graph_for_file(const FileInfo& info)
    {
        mlang::ide::WorkspaceDocumentNode node;
        node.uri = info.uri;
        node.path = info.path;
        node.providedModules = infer_provided_modules(info);
        node.imports.reserve(info.imports.size());
        for(const auto& imp : info.imports)
        {
            mlang::ide::WorkspaceImport wi;
            wi.moduleName = imp.moduleName;
            wi.itemName = imp.itemName;
            wi.importAll = imp.importAll;
            node.imports.push_back(std::move(wi));
        }
        workspaceGraph.upsertDocument(node);
    }

    std::string resolve_module_path(const std::string& baseDir,
                                    const std::string& moduleName) const
    {
        std::filesystem::path base(baseDir);
        std::string relDir = module_name_to_rel_dir(moduleName);
        std::filesystem::path direct = base / (relDir + ".mla");
        std::error_code ec;
        if(std::filesystem::exists(direct, ec) && !ec)
            return direct.string();
        std::filesystem::path dirMod = base / relDir / "mod.mla";
        if(std::filesystem::exists(dirMod, ec) && !ec)
            return dirMod.string();
        // Backward compatible single-segment fallback.
        std::filesystem::path legacyDirect = base / (moduleName + ".mla");
        if(std::filesystem::exists(legacyDirect, ec) && !ec)
            return legacyDirect.string();
        std::filesystem::path legacyDirMod = base / moduleName / "mod.mla";
        if(std::filesystem::exists(legacyDirMod, ec) && !ec)
            return legacyDirMod.string();
        return {};
    }

    std::string resolve_module_path_for_file(const FileInfo& info,
                                             const std::string& moduleName) const
    {
        auto providers = workspaceGraph.findProviderUris(moduleName);
        for(const auto& providerUri : providers)
        {
            auto it = files.find(providerUri);
            if(it != files.end() && !it->second.path.empty())
                return it->second.path;

            std::string providerPath = uri_to_path(providerUri);
            if(!providerPath.empty())
                return providerPath;
        }
        for(const auto& root : module_search_paths_for_file(info))
        {
            std::string path = resolve_module_path(root, moduleName);
            if(!path.empty())
                return path;
        }
        return {};
    }

    FileInfo* get_or_index_file(const std::string& path)
    {
        std::string uri = path_to_uri(path);
        auto it = files.find(uri);
        if(it != files.end())
            return &it->second;
        std::string text = read_file(path);
        if(text.empty())
            return nullptr;
        index_document(uri, text);
        auto it2 = files.find(uri);
        if(it2 != files.end())
            return &it2->second;
        return nullptr;
    }

    std::optional<Location> find_symbol_in_file(FileInfo& info,
                                                const std::string& name)
    {
        if(auto fit = info.functions.find(name); fit != info.functions.end())
            return fit->second.loc;
        if(auto sit = info.structs.find(name); sit != info.structs.end())
            return sit->second.loc;
        if(auto eit = info.enums.find(name); eit != info.enums.end())
            return eit->second.loc;
        for(auto& [sname, st] : info.structs)
        {
            auto itf = st.fields.find(name);
            if(itf != st.fields.end())
                return itf->second.loc;
            auto itm = st.methods.find(name);
            if(itm != st.methods.end())
                return itm->second.loc;
        }
        for(auto& [ename, en] : info.enums)
        {
            auto itv = en.variants.find(name);
            if(itv != en.variants.end())
                return itv->second.loc;
        }
        return std::nullopt;
    }

    std::optional<Location> find_symbol_in_workspace(const std::string& name)
    {
        for(auto& [uri, info] : files)
        {
            if(auto loc = find_symbol_in_file(info, name))
                return loc;
        }
        return std::nullopt;
    }

    std::optional<std::filesystem::path>
    resolve_runtime_builtins_stub_path(const FileInfo* contextFile) const
    {
        auto try_from_base = [](const std::filesystem::path& base)
            -> std::optional<std::filesystem::path> {
            if(base.empty())
                return std::nullopt;
            std::error_code ec;
            std::filesystem::path cand = base / "docs" / "runtime_builtins.mlastub";
            if(std::filesystem::exists(cand, ec) && !ec)
                return cand;
            return std::nullopt;
        };

        for(const auto& base : search_base_paths_for_lookup(contextFile))
        {
            if(auto p = try_from_base(base))
                return p;
        }

        return std::nullopt;
    }

    std::optional<Location>
    find_runtime_builtin_location(const std::string& name,
                                  const FileInfo* contextFile = nullptr)
    {
        auto docsPathOpt = resolve_runtime_builtins_stub_path(contextFile);
        if(!docsPathOpt)
            return std::nullopt;

        const std::filesystem::path docsPath = *docsPathOpt;
        std::string text = read_file(docsPath.string());
        if(text.empty())
            return std::nullopt;
        auto lines = split_lines(text);

        auto find_struct = [&](const std::string& typeName)
            -> std::optional<Location>
        {
            return find_definition_location(
                lines, 0, -1,
                std::regex("(?:pub\\s+)?struct\\s+(" + typeName + ")\\b"));
        };

        auto find_extern_fn = [&](const std::string& fnName)
            -> std::optional<Location>
        {
            return find_definition_location(
                lines, 0, -1,
                std::regex("\\bextern\\s+fn\\s+(" + fnName + ")\\b"));
        };

        if(std::find(mlang::constants::kRuntimeBuiltinTypes.begin(),
                     mlang::constants::kRuntimeBuiltinTypes.end(),
                     name) != mlang::constants::kRuntimeBuiltinTypes.end())
        {
            if(auto loc = find_struct(name))
            {
                loc->uri = path_to_uri(docsPath.string());
                return loc;
            }
        }

        if(std::find(mlang::constants::kRuntimeBuiltinFunctions.begin(),
                     mlang::constants::kRuntimeBuiltinFunctions.end(),
                     name) !=
           mlang::constants::kRuntimeBuiltinFunctions.end())
        {
            if(auto loc = find_extern_fn(name))
            {
                loc->uri = path_to_uri(docsPath.string());
                return loc;
            }
        }

        return std::nullopt;
    }

    std::optional<std::filesystem::path>
    resolve_std_strbuf_module_path(const FileInfo* contextFile) const
    {
        auto try_from_base = [&](const std::filesystem::path& base)
            -> std::optional<std::filesystem::path> {
            if(base.empty())
                return std::nullopt;
            std::error_code ec;
            std::filesystem::path cand1 = base / "stdlib" / "std" / "strbuf.mla";
            if(std::filesystem::exists(cand1, ec) && !ec)
                return cand1;
            std::filesystem::path cand2 = base / "std" / "strbuf.mla";
            if(std::filesystem::exists(cand2, ec) && !ec)
                return cand2;
            std::string resolved = resolve_module_path(base.string(), "std::strbuf");
            if(!resolved.empty())
            {
                std::filesystem::path cand3 = resolved;
                if(std::filesystem::exists(cand3, ec) && !ec)
                    return cand3;
            }
            return std::nullopt;
        };

        for(const auto& base : search_base_paths_for_lookup(contextFile))
        {
            if(auto p = try_from_base(base))
                return p;
        }

        return std::nullopt;
    }

    std::optional<Location>
    find_string_intrinsic_location(const std::string& memberName,
                                   const FileInfo* contextFile = nullptr)
    {
        std::string targetName;
        if(memberName == "new")
            targetName = "new";
        else if(memberName == "with_capacity")
            targetName = "with_capacity";
        else if(memberName == "free")
            targetName = "free";
        else
            return std::nullopt;

        auto modPathOpt = resolve_std_strbuf_module_path(contextFile);
        if(!modPathOpt)
            return std::nullopt;

        const std::filesystem::path modPath = *modPathOpt;
        std::string text = read_file(modPath.string());
        if(text.empty())
            return std::nullopt;
        auto lines = split_lines(text);

        auto loc = find_definition_location(
            lines, 0, -1,
            std::regex("\\bpub\\s+fn\\s+(" + targetName + ")\\b"));
        if(!loc)
            return std::nullopt;
        loc->uri = path_to_uri(modPath.string());
        return loc;
    }

    std::optional<Location>
    find_runtime_attribute_location(const std::string& attrText)
    {
        if(rootPath.empty())
            return std::nullopt;

        std::vector<std::filesystem::path> candidates = {
            std::filesystem::path(rootPath) / "stdlib" / "attributes.mla",
            std::filesystem::path(rootPath) / "docs" / "language_attributes.mlastub",
            std::filesystem::path(rootPath) / "docs" / "runtime_builtins.mlastub",
        };

        for(const auto& docsPath : candidates)
        {
            if(!std::filesystem::exists(docsPath))
                continue;
            std::string text = read_file(docsPath.string());
            if(text.empty())
                continue;
            auto lines = split_lines(text);

            std::string attrName;
            if(attrText == mlang::constants::kAttrTest)
                attrName = "test";
            else if(attrText == mlang::constants::kAttrDeriveDebug)
                attrName = "derive";

            if(!attrName.empty())
            {
                const std::string headerTag =
                    "@builtin_attribute " + attrName;
                for(int i = 0; i < (int)lines.size(); ++i)
                {
                    const auto& line = lines[(size_t)i];
                    size_t pos = line.find(headerTag);
                    if(pos != std::string::npos)
                    {
                        Location loc;
                        loc.uri = path_to_uri(docsPath.string());
                        loc.line = i;
                        loc.character = (int)pos;
                        return loc;
                    }
                }
            }

            for(int i = 0; i < (int)lines.size(); ++i)
            {
                const auto& line = lines[(size_t)i];
                const auto trimmed = line.find_first_not_of(" \t");
                if(trimmed != std::string::npos && trimmed + 1 < line.size() &&
                   line[trimmed] == '/' && line[trimmed + 1] == '/')
                {
                    continue;
                }
                size_t pos = line.find(attrText);
                if(pos != std::string::npos)
                {
                    Location loc;
                    loc.uri = path_to_uri(docsPath.string());
                    loc.line = i;
                    loc.character = (int)pos;
                    return loc;
                }
            }
        }
        return std::nullopt;
    }

    static bool starts_with(const std::string& value,
                            const std::string& prefix)
    {
        if(prefix.empty())
            return true;
        if(value.size() < prefix.size())
            return false;
        return value.compare(0, prefix.size(), prefix) == 0;
    }

    struct CompletionCandidate
    {
        std::string label;
        int kind = 0;
        std::string insertText;
        bool isSnippet = false;
    };

    void add_completion(std::vector<CompletionCandidate>& out,
                        std::unordered_set<std::string>& seen,
                        const std::string& label, int kind,
                        const std::string& prefix,
                        const std::string& insertText = {},
                        bool isSnippet = false)
    {
        if(!starts_with(label, prefix))
            return;
        if(seen.insert(label).second)
        {
            CompletionCandidate cand;
            cand.label = label;
            cand.kind = kind;
            cand.insertText = insertText;
            cand.isSnippet = isSnippet;
            out.push_back(std::move(cand));
        }
    }

    void add_completion_snippet(std::vector<CompletionCandidate>& out,
                                std::unordered_set<std::string>& seen,
                                const std::string& label, int kind,
                                const std::string& prefix,
                                const std::string& snippet)
    {
        add_completion(out, seen, label, kind, prefix, snippet, true);
    }

    void collect_file_completions(FileInfo& info, const std::string& prefix,
                                  std::vector<CompletionCandidate>& out,
                                  std::unordered_set<std::string>& seen)
    {
        for(const auto& [name, fn] : info.functions)
            add_completion(out, seen, name, 3, prefix);
        for(const auto& [name, st] : info.structs)
            add_completion(out, seen, name, 7, prefix);
        for(const auto& [sname, st] : info.structs)
        {
            for(const auto& [fname, field] : st.fields)
                add_completion(out, seen, fname, 5, prefix);
            for(const auto& [mname, method] : st.methods)
                add_completion(out, seen, mname, 2, prefix);
        }
    }

    StructInfo* find_struct_in_workspace(const std::string& name,
                                         FileInfo** outFile)
    {
        for(auto& [uri, info] : files)
        {
            auto it = info.structs.find(name);
            if(it != info.structs.end())
            {
                if(outFile)
                    *outFile = &info;
                return &it->second;
            }
        }
        return nullptr;
    }

    static std::string find_module_prefix(std::string_view lineText,
                                          int wordStart)
    {
        int j = wordStart - 1;
        while(j >= 0 && std::isspace((unsigned char)lineText[j]))
            --j;
        if(j < 1 || lineText[j] != ':' || lineText[j - 1] != ':')
            return {};
        j -= 2;
        while(j >= 0 && std::isspace((unsigned char)lineText[j]))
            --j;
        int end = j;
        while(j >= 0 &&
              (std::isalnum((unsigned char)lineText[j]) || lineText[j] == '_'))
            --j;
        if(end < j + 1)
            return {};
        return std::string(lineText.substr(j + 1, end - j));
    }

    static std::string find_member_base(std::string_view lineText,
                                        int wordStart)
    {
        int j = wordStart - 1;
        while(j >= 0 && std::isspace((unsigned char)lineText[j]))
            --j;
        if(j < 0 || lineText[j] != '.')
            return {};
        --j;
        while(j >= 0 && std::isspace((unsigned char)lineText[j]))
            --j;
        int end = j;
        while(j >= 0 &&
              (std::isalnum((unsigned char)lineText[j]) || lineText[j] == '_'))
            --j;
        if(end < j + 1)
            return {};
        return std::string(lineText.substr(j + 1, end - j));
    }

    void send_json(llvm::json::Value v)
    {
        std::string out;
        llvm::raw_string_ostream os(out);
        llvm::json::OStream json(os);
        json.value(v);
        os.flush();
        std::cout << "Content-Length: " << out.size() << "\r\n\r\n" << out;
        std::cout.flush();
    }

    void send_response(const llvm::json::Value& id, llvm::json::Value result)
    {
        llvm::json::Object obj;
        obj["jsonrpc"] = "2.0";
        obj["id"] = id;
        obj["result"] = std::move(result);
        send_json(llvm::json::Value(std::move(obj)));
    }

    void handle_request(std::string_view method, llvm::json::Object& obj)
    {
        llvm::json::Value id = *obj.get("id");
        llvm::json::Object* params = obj.getObject("params");
        refresh_mlang_commands_if_needed();
        if(method == "initialize")
        {
            handle_initialize(params);
            llvm::json::Object sync;
            sync["openClose"] = true;
            sync["change"] = 1;
            llvm::json::Object caps;
            caps["textDocumentSync"] = llvm::json::Value(std::move(sync));
            caps["definitionProvider"] = true;
            caps["implementationProvider"] = true;
            caps["referencesProvider"] = true;
            caps["hoverProvider"] = true;
            caps["documentHighlightProvider"] = true;
            llvm::json::Array triggers;
            triggers.push_back(".");
            triggers.push_back(":");
            llvm::json::Object completion;
            completion["triggerCharacters"] = llvm::json::Value(std::move(triggers));
            caps["completionProvider"] = llvm::json::Value(std::move(completion));
            llvm::json::Object signatureHelp;
            llvm::json::Array sigTriggers;
            sigTriggers.push_back("(");
            sigTriggers.push_back(",");
            signatureHelp["triggerCharacters"] = llvm::json::Value(std::move(sigTriggers));
            caps["signatureHelpProvider"] = llvm::json::Value(std::move(signatureHelp));
            llvm::json::Object renameProvider;
            renameProvider["prepareProvider"] = true;
            caps["renameProvider"] = llvm::json::Value(std::move(renameProvider));
            caps["documentSymbolProvider"] = true;
            caps["workspaceSymbolProvider"] = true;
            caps["documentFormattingProvider"] = true;
            caps["documentRangeFormattingProvider"] = true;
            caps["codeActionProvider"] = true;
            llvm::json::Object diagnosticProvider;
            diagnosticProvider["interFileDependencies"] = false;
            diagnosticProvider["workspaceDiagnostics"] = false;
            caps["diagnosticProvider"] = llvm::json::Value(std::move(diagnosticProvider));
            llvm::json::Object semanticLegend;
            llvm::json::Array semanticTokenTypes;
            semanticTokenTypes.push_back("keyword");
            semanticTokenTypes.push_back("macro");
            semanticTokenTypes.push_back("function");
            semanticTokenTypes.push_back("parameter");
            semanticTokenTypes.push_back("variable");
            semanticTokenTypes.push_back("type");
            semanticTokenTypes.push_back("enumMember");
            semanticLegend["tokenTypes"] =
                llvm::json::Value(std::move(semanticTokenTypes));
            semanticLegend["tokenModifiers"] = llvm::json::Array{};
            llvm::json::Object semanticProvider;
            semanticProvider["legend"] = llvm::json::Value(std::move(semanticLegend));
            semanticProvider["full"] = true;
            caps["semanticTokensProvider"] =
                llvm::json::Value(std::move(semanticProvider));
            llvm::json::Object result;
            result["capabilities"] = llvm::json::Value(std::move(caps));
            send_response(id, llvm::json::Value(std::move(result)));
        }
        else if(method == "shutdown")
        {
            send_response(id, nullptr);
        }
        else if(method == "textDocument/definition")
        {
            send_response(id, handle_definition(params));
        }
        else if(method == "textDocument/implementation")
        {
            send_response(id, handle_implementation(params));
        }
        else if(method == "textDocument/references")
        {
            send_response(id, handle_references(params));
        }
        else if(method == "textDocument/hover")
        {
            send_response(id, handle_hover(params));
        }
        else if(method == "textDocument/documentHighlight")
        {
            send_response(id, handle_document_highlight(params));
        }
        else if(method == "textDocument/completion")
        {
            send_response(id, handle_completion(params));
        }
        else if(method == "textDocument/signatureHelp")
        {
            send_response(id, handle_signature_help(params));
        }
        else if(method == "textDocument/prepareRename")
        {
            send_response(id, handle_prepare_rename(params));
        }
        else if(method == "textDocument/rename")
        {
            send_response(id, handle_rename(params));
        }
        else if(method == "textDocument/documentSymbol")
        {
            send_response(id, handle_document_symbols(params));
        }
        else if(method == "textDocument/formatting")
        {
            send_response(id, handle_formatting(params));
        }
        else if(method == "textDocument/rangeFormatting")
        {
            send_response(id, handle_formatting(params));
        }
        else if(method == "textDocument/codeAction")
        {
            send_response(id, handle_code_action(params));
        }
        else if(method == "textDocument/diagnostic")
        {
            send_response(id, handle_document_diagnostic(params));
        }
        else if(method == "textDocument/semanticTokens/full")
        {
            send_response(id, handle_semantic_tokens(params));
        }
        else if(method == "workspace/symbol")
        {
            send_response(id, handle_workspace_symbols(params));
        }
        else
        {
            send_response(id, nullptr);
        }
    }

    void handle_notification(std::string_view method, llvm::json::Object& obj)
    {
        llvm::json::Object* params = obj.getObject("params");
        if(method == "initialized" || method == "exit")
            return;
        if(method == "textDocument/didOpen")
        {
            auto* textDoc = params ? params->getObject("textDocument") : nullptr;
            if(!textDoc)
                return;
            auto uri = textDoc->getString("uri");
            auto text = textDoc->getString("text");
            if(!uri || !text)
                return;

            std::string uriStr = uri->str();
            std::string path = uri_to_path(uriStr);
            int version = -1;
            if(auto v = textDoc->getInteger("version"))
                version = static_cast<int>(*v);
            openDocumentUris.insert(uriStr);
            incrementalCompiler.openDocument(uriStr, path, text->str(), version);
            compiler_open_or_update(
                uriStr, text->str(),
                version >= 0 ? std::optional<int>(version + 1) : std::nullopt);
            reindex_incremental_document(uriStr);
            log_runtime_telemetry("didOpen", uriStr);
        }
        else if(method == "textDocument/didChange")
        {
            auto* textDoc = params ? params->getObject("textDocument") : nullptr;
            auto* changes = params ? params->getArray("contentChanges") : nullptr;
            if(!textDoc || !changes || changes->empty())
                return;
            auto uri = textDoc->getString("uri");
            if(!uri)
                return;

            std::string uriStr = uri->str();
            openDocumentUris.insert(uriStr);
            if(!incrementalCompiler.getDocument(uriStr))
            {
                std::string path = uri_to_path(uriStr);
                std::string currentText = read_file(path);
                incrementalCompiler.openDocument(uriStr, path, currentText);
            }

            std::vector<mlang::IncrementalTextChange> incrementalChanges;
            incrementalChanges.reserve(changes->size());
            for(const auto& item : *changes)
            {
                auto* changeObj = item.getAsObject();
                if(!changeObj)
                    continue;
                auto text = changeObj->getString("text");
                if(!text)
                    continue;

                mlang::IncrementalTextChange change;
                change.text = text->str();
                if(auto* range = changeObj->getObject("range"))
                {
                    auto* start = range->getObject("start");
                    auto* end = range->getObject("end");
                    if(start && end)
                    {
                        auto sl = start->getInteger("line");
                        auto sc = start->getInteger("character");
                        auto el = end->getInteger("line");
                        auto ec = end->getInteger("character");
                        if(sl && sc && el && ec)
                        {
                            mlang::Range range;
                            range.start.line = static_cast<int>(*sl);
                            range.start.character = static_cast<int>(*sc);
                            range.end.line = static_cast<int>(*el);
                            range.end.character = static_cast<int>(*ec);
                            change.range = range;
                        }
                    }
                }
                incrementalChanges.push_back(std::move(change));
            }

            int version = -1;
            if(auto v = textDoc->getInteger("version"))
                version = static_cast<int>(*v);

            if(incrementalCompiler.applyChanges(uriStr, incrementalChanges,
                                                version))
            {
                if(auto doc = incrementalCompiler.getDocument(uriStr))
                {
                    compiler_open_or_update(
                        uriStr, doc->text,
                        version >= 0 ? std::optional<int>(version + 1)
                                     : std::nullopt);
                }
                reindex_incremental_document(uriStr);
                log_runtime_telemetry("didChange", uriStr);
            }
            else
            {
                auto* last = changes->back().getAsObject();
                if(last)
                {
                    if(auto fullText = last->getString("text"))
                    {
                        compiler_open_or_update(
                            uriStr, fullText->str(),
                            version >= 0 ? std::optional<int>(version + 1)
                                         : std::nullopt);
                        index_document(uriStr, fullText->str());
                        log_runtime_telemetry("didChange_fallback_full_reindex",
                                              uriStr);
                    }
                }
            }
        }
        else if(method == "textDocument/didSave")
        {
            auto* textDoc = params ? params->getObject("textDocument") : nullptr;
            if(!textDoc)
                return;
            auto uri = textDoc->getString("uri");
            if(!uri)
                return;
            std::string uriStr = uri->str();
            std::string path = uri_to_path(uriStr);
            std::string text = read_file(path);
            if(text.empty())
                return;

            if(incrementalCompiler.getDocument(uriStr))
                incrementalCompiler.setDocumentText(uriStr, text);
            else
                incrementalCompiler.openDocument(uriStr, path, text);
            compiler_open_or_update(uriStr, text, std::nullopt);
            reindex_incremental_document(uriStr);
            log_runtime_telemetry("didSave", uriStr);
        }
        else if(method == "textDocument/didClose")
        {
            auto* textDoc = params ? params->getObject("textDocument") : nullptr;
            if(!textDoc)
                return;
            auto uri = textDoc->getString("uri");
            if(!uri)
                return;
            std::string uriStr = uri->str();
            openDocumentUris.erase(uriStr);
            incrementalCompiler.closeDocument(uriStr);
            compiler_close(uriStr);
            prune_closed_document_indexes(uriStr);
            log_runtime_telemetry("didClose", uriStr);
        }
    }

    void handle_initialize(llvm::json::Object* params)
    {
        std::string root;
        if(params)
        {
            if(auto rootUri = params->getString("rootUri"))
                root = uri_to_path(rootUri->str());
            else if(auto rootPathVal = params->getString("rootPath"))
                root = rootPathVal->str();
        }
        if(root.empty())
            root = std::filesystem::current_path().string();
        rootPath = root;
        if(const char* env = std::getenv("MLANGD_COMPILER_CACHE_CLEAR_INTERVAL"))
        {
            int v = std::atoi(env);
            if(v > 0)
                compilerCacheClearInterval = v;
        }
        ensure_compiler_session();
        scan_workspace(rootPath);

        std::filesystem::path docsPath =
            std::filesystem::path(rootPath) / "docs" /
            "runtime_builtins.mlastub";
        if(std::filesystem::exists(docsPath))
        {
            std::string text = read_file(docsPath.string());
            if(!text.empty())
                index_document(path_to_uri(docsPath.string()), text);
        }
        cHeadersLoaded = false;
        cHeaderDebug = std::getenv("MLANG_LSP_DEBUG") != nullptr;
        if(const char* logPath = std::getenv("MLANG_LSP_DEBUG_LOG"))
            cHeaderDebugLog = logPath;
        if(auto path = find_mlang_commands_path())
        {
            mlangCommandsPath = *path;
            refresh_mlang_commands_if_needed();
        }
    }

    void scan_workspace(const std::string& root)
    {
        std::vector<std::string> paths;
        std::error_code ec;
        for(auto it = std::filesystem::recursive_directory_iterator(
                root, std::filesystem::directory_options::skip_permission_denied,
                ec);
            it != std::filesystem::recursive_directory_iterator(); ++it)
        {
            if(it->is_directory(ec))
            {
                auto name = it->path().filename().string();
                if(name == ".git" || name == "build" || name == "out" ||
                   name == "dist")
                {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if(!it->is_regular_file(ec))
                continue;
            if(!is_mlang_source_path(it->path()))
                continue;
            paths.push_back(it->path().string());
        }

        auto loaded = runtimeScheduler.loadFiles(paths);
        for(const auto& file : loaded)
        {
            if(file.content.empty())
                continue;
            index_document(path_to_uri(file.path), file.content);
        }
    }

    void reindex_incremental_document(const std::string& uri)
    {
        auto* doc = incrementalCompiler.getDocument(uri);
        if(!doc)
            return;

        FileInfo info;
        info.path = doc->path;
        info.uri = uri;
        info.text = doc->text;
        info.lines = split_lines(doc->text);
        info.ast = doc->ast;
        collect_symbols(info);
        files[uri] = std::move(info);
        update_workspace_graph_for_file(files[uri]);
    }

    void index_document(const std::string& uri, const std::string& text)
    {
        std::string path = uri_to_path(uri);
        incrementalCompiler.openDocument(uri, path, text);
        compiler_open_or_update(uri, text, std::nullopt);
        reindex_incremental_document(uri);
    }

    void collect_symbols(FileInfo& info)
    {
        functionReturns.clear();
        if(!info.ast)
            return;
        collect_modules_imports(info, info.ast);
        collect_enums(info, info.ast);
        collect_structs(info, info.ast);
        collect_functions(info, info.ast);
        index_locations(info);
    }

    void collect_modules_imports(FileInfo& info, ProgramNode* program)
    {
        info.moduleDecls.clear();
        info.imports.clear();
        for(auto* modDecl : program->modules)
        {
            if(modDecl)
                info.moduleDecls.push_back(modDecl->moduleName);
        }
        for(auto* useDecl : program->imports)
        {
            if(!useDecl)
                continue;
            UseImport ui;
            ui.moduleName = useDecl->moduleName;
            ui.itemName = useDecl->itemName;
            ui.importAll = useDecl->importAll;
            info.imports.push_back(ui);
        }
    }

    void collect_structs(FileInfo& info, ProgramNode* program)
    {
        if(program->structList)
        {
            for(auto* s : program->structList->structs)
            {
                StructInfo st;
                st.name = s->name;
                st.baseName = s->baseName;
                if(s->members)
                {
                    for(auto* member : s->members->members)
                    {
                        FieldInfo field;
                        field.name = member->name;
                        field.typeName = type_name(member->type);
                        st.fields[field.name] = field;
                    }
                    for(auto* method : s->members->methods)
                    {
                        MethodInfo mi;
                        mi.name = method->name;
                        mi.returnType = type_name(method->returnType);
                        st.methods[mi.name] = mi;
                    }
                }
                info.structs[st.name] = st;
            }
        }
        if(program->implList)
        {
            for(auto* impl : program->implList->impls)
            {
                auto it = info.structs.find(impl->structName);
                if(it == info.structs.end())
                    continue;
                for(auto* method : impl->methods)
                {
                    MethodInfo mi;
                    mi.name = method->name;
                    mi.returnType = type_name(method->returnType);
                    it->second.methods[mi.name] = mi;
                }
            }
        }
    }

    void collect_enums(FileInfo& info, ProgramNode* program)
    {
        info.enums.clear();
        if(program->enumList)
        {
            for(auto* e : program->enumList->enums)
            {
                EnumInfo en;
                en.name = e->name;
                if(e->variants)
                {
                    for(auto* v : e->variants->variants)
                    {
                        EnumVariantInfo vi;
                        vi.name = v->name;
                        en.variants[vi.name] = vi;
                    }
                }
                info.enums[en.name] = en;
            }
        }
    }

    void collect_functions(FileInfo& info, ProgramNode* program)
    {
        if(program->functionList)
        {
            for(auto* fn : program->functionList->functions)
            {
                FunctionInfo fi;
                fi.name = fn->name;
                fi.returnType = type_name(fn->returnType);
                fi.isExtern = fn->isExtern;
                collect_param_types(fi, fn->parameters);
                collect_var_types(fi, fn->body, info);
                info.functions[fi.name] = fi;
                if(!fi.returnType.empty())
                    functionReturns[fi.name] = fi.returnType;
            }
        }
        // Struct methods as functions scoped to struct
        for(auto& [name, st] : info.structs)
        {
            for(auto& [methodName, mi] : st.methods)
            {
                FunctionInfo fi;
                fi.name = methodName;
                fi.returnType = mi.returnType;
                fi.ownerStruct = st.name;
                fi.varTypes["self"] = st.name;
                info.functions[st.name + "::" + methodName] = fi;
                if(!fi.returnType.empty())
                    functionReturns[methodName] = fi.returnType;
            }
        }
    }

    void collect_param_types(FunctionInfo& fn, ParameterListNode* params)
    {
        if(!params)
            return;
        for(auto* p : params->parameters)
        {
            std::string t = type_name(p->type);
            if(!t.empty())
                fn.varTypes[p->name] = t;
        }
    }

    void collect_var_types(FunctionInfo& fn, StatementListNode* body,
                           FileInfo& info)
    {
        if(!body)
            return;
        for(auto* stmt : body->statements)
        {
            collect_var_types_stmt(fn, stmt, info);
        }
    }

    void collect_var_types_stmt(FunctionInfo& fn, StatementNode* stmt,
                                FileInfo& info)
    {
        if(!stmt)
            return;
        if(auto* letDecl = dynamic_cast<LetDeclNode*>(stmt))
        {
            std::string t = type_name(letDecl->type);
            if(!t.empty())
                fn.varTypes[letDecl->name] = t;
            return;
        }
        if(auto* varDecl = dynamic_cast<VarDeclNode*>(stmt))
        {
            std::string t = type_name(varDecl->type);
            if(!t.empty())
                fn.varTypes[varDecl->name] = t;
            if(varDecl->initExpr)
            {
                std::string inferred = resolve_expr_type(varDecl->initExpr, fn, info);
                if(!inferred.empty())
                    fn.varTypes[varDecl->name] = inferred;
            }
            return;
        }
        if(auto* assign = dynamic_cast<AssignmentNode*>(stmt))
        {
            std::string inferred = resolve_expr_type(assign->expression, fn, info);
            if(!inferred.empty())
                fn.varTypes[assign->name] = inferred;
            return;
        }
        if(auto* init = dynamic_cast<StructInitNode*>(stmt))
        {
            if(!init->typeName.empty())
                fn.varTypes[init->varName] = init->typeName;
            return;
        }
        if(auto* block = dynamic_cast<BlockStatementNode*>(stmt))
        {
            if(block->statements)
            {
                for(auto* s : block->statements->statements)
                    collect_var_types_stmt(fn, s, info);
            }
            return;
        }
        if(auto* exprStmt = dynamic_cast<ExpressionStatementNode*>(stmt))
        {
            (void)resolve_expr_type(exprStmt->expression, fn, info);
            return;
        }
    }

    std::string resolve_expr_type(ExpressionNode* expr, FunctionInfo& fn,
                                  FileInfo& info)
    {
        if(!expr)
            return {};
        if(auto* ident = dynamic_cast<IdentifierNode*>(expr))
        {
            auto it = fn.varTypes.find(ident->name);
            if(it != fn.varTypes.end())
                return it->second;
            return {};
        }
        if(auto* lit = dynamic_cast<StructLiteralNode*>(expr))
        {
            return lit->structName;
        }
        if(auto* call = dynamic_cast<FunctionCallNode*>(expr))
        {
            auto it = functionReturns.find(call->name);
            if(it != functionReturns.end())
                return it->second;
            return {};
        }
        if(auto* method = dynamic_cast<MethodCallNode*>(expr))
        {
            std::string objType = resolve_expr_type(method->object, fn, info);
            if(objType.empty())
                return {};
            auto it = info.structs.find(objType);
            if(it != info.structs.end())
            {
                auto* mi =
                    find_method_in_struct(it->second, method->methodName, info);
                if(mi)
                    return mi->returnType;
            }
            return {};
        }
        if(auto* field = dynamic_cast<FieldAccessNode*>(expr))
        {
            std::string baseType;
            if(field->object)
                baseType = resolve_expr_type(field->object, fn, info);
            else
            {
                auto it = fn.varTypes.find(field->structName);
                if(it != fn.varTypes.end())
                    baseType = it->second;
            }
            if(baseType.empty())
                return {};
            auto fit = info.structs.find(baseType);
            if(fit == info.structs.end())
                return {};
            auto fieldInfo = find_field_in_struct(fit->second, field->fieldName, info);
            if(fieldInfo)
                return fieldInfo->typeName;
            return {};
        }
        return {};
    }

    FieldInfo* find_field_in_struct(StructInfo& st, const std::string& name,
                                    FileInfo& info)
    {
        auto it = st.fields.find(name);
        if(it != st.fields.end())
            return &it->second;
        if(!st.baseName.empty())
        {
            auto baseIt = info.structs.find(st.baseName);
            if(baseIt != info.structs.end())
                return find_field_in_struct(baseIt->second, name, info);
        }
        return nullptr;
    }

    MethodInfo* find_method_in_struct(StructInfo& st, const std::string& name,
                                      FileInfo& info)
    {
        auto it = st.methods.find(name);
        if(it != st.methods.end())
            return &it->second;
        if(!st.baseName.empty())
        {
            auto baseIt = info.structs.find(st.baseName);
            if(baseIt != info.structs.end())
                return find_method_in_struct(baseIt->second, name, info);
        }
        return nullptr;
    }

    void index_locations(FileInfo& info)
    {
        std::string cleaned = strip_comments_strings(info.text);
        auto cleanedLines = split_lines(cleaned);

        auto structSpans = find_spans(cleanedLines,
                                     std::regex("\\bstruct\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        auto implSpans = find_spans(cleanedLines,
                                   std::regex("\\bimpl(?:\\s*<[^>]+>)?\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        auto enumSpans = find_spans(cleanedLines,
                                    std::regex("\\benum\\s+([A-Za-z_][A-Za-z0-9_]*)"));

        for(auto& span : structSpans)
        {
            auto it = info.structs.find(span.name);
            if(it == info.structs.end())
                continue;
            it->second.startLine = span.startLine;
            it->second.endLine = span.endLine;
            auto loc = find_definition_location(
                info.lines, span.startLine, span.startLine,
                std::regex("(?:pub\\s+)?struct\\s+(" + span.name + ")\\b"));
            if(loc)
            {
                it->second.loc.uri = info.uri;
                it->second.loc.line = loc->line;
                it->second.loc.character = loc->character;
            }
        }

        for(auto& span : enumSpans)
        {
            auto it = info.enums.find(span.name);
            if(it == info.enums.end())
                continue;
            it->second.startLine = span.startLine;
            it->second.endLine = span.endLine;
            auto loc = find_definition_location(
                info.lines, span.startLine, span.startLine,
                std::regex("(?:pub\\s+)?enum\\s+(" + span.name + ")\\b"));
            if(loc)
            {
                it->second.loc.uri = info.uri;
                it->second.loc.line = loc->line;
                it->second.loc.character = loc->character;
            }
        }

        for(auto& [name, st] : info.structs)
        {
            if(st.startLine < 0)
                continue;
            for(auto& [fname, field] : st.fields)
            {
                auto loc = find_definition_location(
                    info.lines, st.startLine, st.endLine,
                    std::regex("\\bvar\\s+(" + fname + ")\\b"));
                if(loc)
                {
                    field.loc.uri = info.uri;
                    field.loc.line = loc->line;
                    field.loc.character = loc->character;
                }
            }
            for(auto& [mname, method] : st.methods)
            {
                auto loc = find_definition_location(
                    info.lines, st.startLine, st.endLine,
                    std::regex("\\bfn\\s+(" + mname + ")\\b"));
                if(loc)
                {
                    method.loc.uri = info.uri;
                    method.loc.line = loc->line;
                    method.loc.character = loc->character;
                }
            }
        }

        for(auto& [name, en] : info.enums)
        {
            if(en.startLine < 0)
                continue;
            for(auto& [vname, variant] : en.variants)
            {
                auto loc = find_definition_location(
                    info.lines, en.startLine, en.endLine,
                    std::regex("\\b(" + vname + ")\\b"));
                if(loc)
                {
                    variant.loc.uri = info.uri;
                    variant.loc.line = loc->line;
                    variant.loc.character = loc->character;
                }
            }
        }

        for(auto& span : implSpans)
        {
            auto it = info.structs.find(span.name);
            if(it == info.structs.end())
                continue;
            for(auto& [mname, method] : it->second.methods)
            {
                if(method.loc.uri.empty())
                {
                    auto loc = find_definition_location(
                        info.lines, span.startLine, span.endLine,
                        std::regex("\\bfn\\s+(" + mname + ")\\b"));
                    if(loc)
                    {
                        method.loc.uri = info.uri;
                        method.loc.line = loc->line;
                        method.loc.character = loc->character;
                    }
                }
            }
        }

        for(auto& [fname, fn] : info.functions)
        {
            if(!fn.ownerStruct.empty())
                continue;
            auto loc = find_definition_location(
                info.lines, 0, -1,
                std::regex("\\bfn\\s+(" + fname + ")\\b"));
            if(loc)
            {
                fn.loc.uri = info.uri;
                fn.loc.line = loc->line;
                fn.loc.character = loc->character;
                fn.startLine = loc->line;
            }
        }

        // Build function spans by scanning for fn declarations
        auto fnSpans = find_spans(cleanedLines,
                                  std::regex("\\bfn\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        for(auto& span : fnSpans)
        {
            FunctionInfo* fn = nullptr;
            if(auto it = info.functions.find(span.name); it != info.functions.end())
            {
                fn = &it->second;
            }
            else
            {
                for(auto& [sname, st] : info.structs)
                {
                    auto mit = st.methods.find(span.name);
                    if(mit != st.methods.end())
                    {
                        auto it2 = info.functions.find(sname + "::" + span.name);
                        if(it2 != info.functions.end())
                        {
                            fn = &it2->second;
                            break;
                        }
                    }
                }
            }
            if(fn)
            {
                fn->startLine = span.startLine;
                fn->endLine = span.endLine;
                info.functionSpans.push_back(fn);

                // Capture parameter decls from the fn signature line.
                if(span.startLine >= 0 &&
                   span.startLine < (int)cleanedLines.size())
                {
                    const std::string& sig = cleanedLines[span.startLine];
                    std::regex paramRx(
                        "([A-Za-z_][A-Za-z0-9_]*)\\s*:\\s*[^,\\)]+");
                    for(auto itp = std::sregex_iterator(sig.begin(), sig.end(),
                                                        paramRx);
                        itp != std::sregex_iterator(); ++itp)
                    {
                        std::string pname = (*itp)[1].str();
                        Location loc;
                        loc.uri = info.uri;
                        loc.line = span.startLine;
                        loc.character = (int)itp->position(1);
                        fn->paramDecls[pname] = loc;
                    }
                }

                // Capture var/let decls within function span.
                std::regex varRx(
                    "\\b(let|var)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b");
                for(int l = span.startLine; l <= span.endLine &&
                                             l < (int)cleanedLines.size();
                    ++l)
                {
                    const std::string& ln = cleanedLines[l];
                    for(auto itv = std::sregex_iterator(ln.begin(), ln.end(),
                                                        varRx);
                        itv != std::sregex_iterator(); ++itv)
                    {
                        std::string vname = (*itv)[2].str();
                        Location loc;
                        loc.uri = info.uri;
                        loc.line = l;
                        loc.character = (int)itv->position(2);
                        fn->varDecls[vname] = loc;
                    }
                }
            }
        }
    }

    struct HoverEntry
    {
        std::string kind;
        std::string name;
        std::string typeName;
        std::string signature;
        std::string module;
        std::string docs;
    };

    std::string module_name_for_file(const FileInfo& info) const
    {
        if(!info.moduleDecls.empty() && !info.moduleDecls.front().empty())
            return info.moduleDecls.front();

        std::filesystem::path p(info.path);
        if(p.filename() == "mod.mla")
            return p.parent_path().filename().string();
        return p.stem().string();
    }

    std::string docs_before_line(const FileInfo& info, int line) const
    {
        if(line <= 0 || line > (int)info.lines.size())
            return {};

        std::vector<std::string> chunks;
        for(int l = line - 1; l >= 0; --l)
        {
            std::string t = trim(info.lines[(size_t)l]);
            if(t.empty())
            {
                if(chunks.empty())
                    continue;
                break;
            }
            if(!t.starts_with("//"))
                break;
            chunks.push_back(trim(t.substr(2)));
        }
        if(chunks.empty())
            return {};

        std::reverse(chunks.begin(), chunks.end());
        std::ostringstream os;
        for(size_t i = 0; i < chunks.size(); ++i)
        {
            if(i)
                os << "\n";
            os << chunks[i];
        }
        return os.str();
    }

    std::string signature_from_line(const FileInfo& info, int line,
                                    const std::string& fallback) const
    {
        if(line < 0 || line >= (int)info.lines.size())
            return fallback;
        std::string sig = trim(info.lines[(size_t)line]);
        if(sig.empty())
            return fallback;
        size_t brace = sig.find('{');
        if(brace != std::string::npos)
            sig = trim(sig.substr(0, brace));
        if(sig.empty())
            return fallback;
        return sig;
    }

    std::optional<HoverEntry>
    hover_entry_for_symbol(FileInfo& info, const std::string& word,
                           int lineHint = -1)
    {
        if(word.empty())
            return std::nullopt;

        HoverEntry h;
        h.name = word;
        h.module = module_name_for_file(info);

        if(lineHint >= 0)
        {
            if(auto* fn = find_enclosing_function(info, lineHint))
            {
                auto vit = fn->varTypes.find(word);
                if(vit != fn->varTypes.end())
                {
                    h.kind = fn->paramDecls.count(word) ? "Parameter" : "Variable";
                    h.typeName = vit->second;
                    int docLine = lineHint;
                    if(auto dit = fn->varDecls.find(word); dit != fn->varDecls.end())
                        docLine = dit->second.line;
                    if(auto pit = fn->paramDecls.find(word); pit != fn->paramDecls.end())
                        docLine = pit->second.line;
                    h.docs = docs_before_line(info, docLine);
                    return h;
                }
            }
        }

        if(auto fit = info.functions.find(word); fit != info.functions.end())
        {
            h.kind = "Function";
            h.typeName = fit->second.returnType;
            std::string fallback = "fn " + word + "(...)";
            if(!h.typeName.empty())
                fallback += " -> " + h.typeName;
            h.signature = signature_from_line(info, fit->second.startLine, fallback);
            h.docs = docs_before_line(info, fit->second.loc.line);
            return h;
        }

        if(auto sit = info.structs.find(word); sit != info.structs.end())
        {
            h.kind = "Struct";
            h.signature = "struct " + word;
            h.docs = docs_before_line(info, sit->second.loc.line);
            return h;
        }

        if(auto eit = info.enums.find(word); eit != info.enums.end())
        {
            h.kind = "Enum";
            h.signature = "enum " + word;
            h.docs = docs_before_line(info, eit->second.loc.line);
            return h;
        }

        for(auto& [sname, st] : info.structs)
        {
            if(auto itf = st.fields.find(word); itf != st.fields.end())
            {
                h.kind = "Field";
                h.typeName = itf->second.typeName;
                h.signature = sname + "." + word;
                h.docs = docs_before_line(info, itf->second.loc.line);
                return h;
            }
            if(auto itm = st.methods.find(word); itm != st.methods.end())
            {
                h.kind = "Method";
                h.typeName = itm->second.returnType;
                std::string fallback = "fn " + sname + "::" + word + "(...)";
                if(!h.typeName.empty())
                    fallback += " -> " + h.typeName;
                h.signature = signature_from_line(info, itm->second.loc.line, fallback);
                h.docs = docs_before_line(info, itm->second.loc.line);
                return h;
            }
        }

        for(auto& [ename, en] : info.enums)
        {
            if(auto itv = en.variants.find(word); itv != en.variants.end())
            {
                h.kind = "Enum Variant";
                h.typeName = ename;
                h.signature = ename + "::" + word;
                h.docs = docs_before_line(info, itv->second.loc.line);
                return h;
            }
        }

        return std::nullopt;
    }

    llvm::json::Value hover_entry_to_json(const HoverEntry& h,
                                          int line,
                                          int startChar,
                                          int endChar)
    {
        std::string md;
        md += "**" + h.kind + "** `" + h.name + "`";
        if(!h.typeName.empty())
            md += "\n\nType: `" + h.typeName + "`";
        if(!h.module.empty())
            md += "\n\nModule: `" + h.module + "`";
        if(!h.signature.empty())
            md += "\n\n```mlang\n" + h.signature + "\n```";
        if(!h.docs.empty())
            md += "\n\n" + h.docs;

        llvm::json::Object contents;
        contents["kind"] = "markdown";
        contents["value"] = md;

        llvm::json::Object out;
        out["contents"] = llvm::json::Value(std::move(contents));
        out["range"] = make_range_value(line, startChar, line, endChar);
        return llvm::json::Value(std::move(out));
    }

    llvm::json::Value handle_hover(llvm::json::Object* params)
    {
        if(!params)
            return nullptr;
        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return nullptr;
        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return nullptr;

        auto it = files.find(uri->str());
        if(it == files.end())
            return nullptr;
        auto& info = it->second;
        if(*line < 0 || *line >= (int)info.lines.size())
            return nullptr;

        mlang::Position queryPos;
        queryPos.line = static_cast<int>(*line);
        queryPos.character = static_cast<int>(*character);
        auto ident = mlang::ide::identifierAt(info.lines, queryPos);
        if(!ident)
            return nullptr;

        if(auto sem = resolve_semantic_symbol(uri->str(), static_cast<int>(*line),
                                              static_cast<int>(*character)))
        {
            std::string md;
            md += "**";
            md += semantic_kind_name(sem->kind);
            md += "** `";
            md += sem->name;
            md += "`";
            if(!sem->typeInfo.empty())
                md += "\n\nType: `" + sem->typeInfo + "`";
            if(!sem->signature.empty())
                md += "\n\n```mlang\n" + sem->signature + "\n```";
            if(sem->overloadCount > 1)
                md += "\n\nOverloads: `" + std::to_string(sem->overloadCount) + "`";
            md += "\n\nStable ID: `" + sem->id + "`";
            md += "\n\nDefined at: `" + sem->uri + ":" +
                  std::to_string(sem->line0 + 1) + ":" +
                  std::to_string(sem->column0 + 1) + "`";

            llvm::json::Object contents;
            contents["kind"] = "markdown";
            contents["value"] = md;

            llvm::json::Object out;
            out["contents"] = llvm::json::Value(std::move(contents));
            out["range"] = make_range_value(static_cast<int>(*line),
                                            ident->startCharacter,
                                            static_cast<int>(*line),
                                            ident->endCharacter);
            return llvm::json::Value(std::move(out));
        }

        if(auto local = hover_entry_for_symbol(info, ident->text,
                                               static_cast<int>(*line)))
        {
            return hover_entry_to_json(*local, static_cast<int>(*line),
                                       ident->startCharacter,
                                       ident->endCharacter);
        }

        std::string modulePrefix =
            find_module_prefix(info.lines[(size_t)*line], ident->startCharacter);
        if(!modulePrefix.empty())
        {
            std::string modPath = resolve_module_path_for_file(info, modulePrefix);
            if(!modPath.empty())
            {
                if(auto* modInfo = get_or_index_file(modPath))
                {
                    if(auto h = hover_entry_for_symbol(*modInfo, ident->text))
                        return hover_entry_to_json(*h, static_cast<int>(*line),
                                                   ident->startCharacter,
                                                   ident->endCharacter);
                }
            }
        }

        for(const auto& imp : info.imports)
        {
            if(!imp.importAll && imp.itemName != ident->text)
                continue;
            std::string modPath = resolve_module_path_for_file(info, imp.moduleName);
            if(modPath.empty())
                continue;
            if(auto* modInfo = get_or_index_file(modPath))
            {
                if(auto h = hover_entry_for_symbol(*modInfo, ident->text))
                    return hover_entry_to_json(*h, static_cast<int>(*line),
                                               ident->startCharacter,
                                               ident->endCharacter);
            }
        }

        for(auto& [_, finfo] : files)
        {
            if(auto h = hover_entry_for_symbol(finfo, ident->text))
                return hover_entry_to_json(*h, static_cast<int>(*line),
                                           ident->startCharacter,
                                           ident->endCharacter);
        }

        return nullptr;
    }

    llvm::json::Value handle_definition(llvm::json::Object* params)
    {
        if(!params)
            return nullptr;
        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return nullptr;
        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return nullptr;

        auto it = files.find(uri->str());
        if(it == files.end())
            return nullptr;
        auto& info = it->second;
        if(*line < 0 || *line >= (int)info.lines.size())
            return nullptr;
        const std::string& lineText = info.lines[*line];
        int idx = (int)*character;
        if(idx > (int)lineText.size())
            idx = (int)lineText.size();

        auto ident = mlang::ide::identifierAt(lineText, idx);
        int left = ident ? ident->startCharacter : idx;
        int right = ident ? ident->endCharacter : idx;
        std::string word = ident ? ident->text : std::string{};

        auto cursor_in_attribute = [&](const char* attrText) -> bool {
            size_t attrLen = std::strlen(attrText);
            size_t pos = 0;
            while(true)
            {
                pos = lineText.find(attrText, pos);
                if(pos == std::string::npos)
                    return false;
                int start = (int)pos;
                int end = start + (int)attrLen;
                if(idx >= start && idx <= end)
                    return true;
                pos += attrLen;
            }
        };

        bool isAttributeWord =
            std::find(mlang::constants::kAttributeKeywords.begin(),
                      mlang::constants::kAttributeKeywords.end(),
                      word) != mlang::constants::kAttributeKeywords.end();

        if((word.empty() || isAttributeWord) &&
           cursor_in_attribute(mlang::constants::kAttrTest))
        {
            if(auto loc = find_runtime_attribute_location(
                   mlang::constants::kAttrTest))
                return location_to_json(*loc);
        }
        if((word.empty() || isAttributeWord) &&
           cursor_in_attribute(mlang::constants::kAttrDeriveDebug))
        {
            if(auto loc = find_runtime_attribute_location(
                   mlang::constants::kAttrDeriveDebug))
                return location_to_json(*loc);
        }

        if(word.empty())
            return nullptr;

        std::string modulePrefix = find_module_prefix(lineText, left);

        if(modulePrefix.empty())
        {
            if(auto typeLoc = find_c_type_location(word))
                return location_to_json(*typeLoc);
        }

        // Resolve String::new/with_capacity/free before other fallbacks.
        if(modulePrefix == "String")
        {
            if(auto loc = find_string_intrinsic_location(word, &info))
                return location_to_json(*loc);
        }

        if(auto loc = find_runtime_builtin_location(word, &info))
            return location_to_json(*loc);

        // Jump from module declarations/usages first.
        static const std::regex modRx(
            "\\bmod\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*;");
        static const std::regex useRx(
            "\\buse\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*::\\s*([A-Za-z_][A-Za-z0-9_]*|\\*)\\s*;");
        std::smatch match;
        if(std::regex_search(lineText, match, modRx))
        {
            std::string modName = match[1].str();
            int pos = (int)match.position(1);
            int len = (int)match.length(1);
            if(idx >= pos && idx < pos + len)
            {
                std::string modPath =
                    resolve_module_path_for_file(info, modName);
                if(!modPath.empty())
                {
                    Location loc;
                    loc.uri = path_to_uri(modPath);
                    loc.line = 0;
                    loc.character = 0;
                    return location_to_json(loc);
                }
            }
        }
        if(std::regex_search(lineText, match, useRx))
        {
            std::string modName = match[1].str();
            std::string itemName = match[2].str();
            int modPos = (int)match.position(1);
            int modLen = (int)match.length(1);
            int itemPos = (int)match.position(2);
            int itemLen = (int)match.length(2);
            std::string modPath = resolve_module_path_for_file(info, modName);
            if(idx >= modPos && idx < modPos + modLen)
            {
                if(!modPath.empty())
                {
                    Location loc;
                    loc.uri = path_to_uri(modPath);
                    loc.line = 0;
                    loc.character = 0;
                    return location_to_json(loc);
                }
            }
            if(idx >= itemPos && idx < itemPos + itemLen)
            {
                if(itemName != "*" && !modPath.empty())
                {
                    if(auto* modInfo = get_or_index_file(modPath))
                    {
                        if(auto loc = find_symbol_in_file(*modInfo, itemName))
                            return location_to_json(*loc);
                    }
                }
                if(!modPath.empty())
                {
                    Location loc;
                    loc.uri = path_to_uri(modPath);
                    loc.line = 0;
                    loc.character = 0;
                    return location_to_json(loc);
                }
            }
        }

        if(auto sem = resolve_semantic_symbol(uri->str(), static_cast<int>(*line),
                                              static_cast<int>(*character)))
        {
            Location loc;
            loc.uri = sem->uri;
            loc.line = sem->line0;
            loc.character = sem->column0;
            return location_to_json(loc);
        }

        if(!modulePrefix.empty())
        {
            auto enumIt = info.enums.find(modulePrefix);
            if(enumIt != info.enums.end())
            {
                auto varIt = enumIt->second.variants.find(word);
                if(varIt != enumIt->second.variants.end())
                    return location_to_json(varIt->second.loc);
            }

            std::string modPath =
                resolve_module_path_for_file(info, modulePrefix);
            if(!modPath.empty())
            {
                if(auto* modInfo = get_or_index_file(modPath))
                {
                    if(auto loc = find_symbol_in_file(*modInfo, word))
                        return location_to_json(*loc);
                }
            }
        }

        bool isMethodCall = false;
        for(int i = right; i < (int)lineText.size(); ++i)
        {
            if(std::isspace((unsigned char)lineText[i]))
                continue;
            if(lineText[i] == '(')
                isMethodCall = true;
            break;
        }

        std::vector<std::string> chain;
        chain.push_back(word);
        int i = left - 1;
        while(i >= 0)
        {
            while(i >= 0 && std::isspace((unsigned char)lineText[i]))
                --i;
            if(i < 0 || lineText[i] != '.')
                break;
            --i;
            while(i >= 0 && std::isspace((unsigned char)lineText[i]))
                --i;
            int end = i;
            while(i >= 0 && (std::isalnum((unsigned char)lineText[i]) ||
                             lineText[i] == '_'))
                --i;
            std::string base = lineText.substr(i + 1, end - i);
            if(base.empty())
                break;
            chain.insert(chain.begin(), base);
        }

        if(chain.size() >= 2)
        {
            auto fn = find_enclosing_function(info, *line);
            std::string baseType;
            if(chain[0] == "self" && fn && !fn->ownerStruct.empty())
                baseType = fn->ownerStruct;
            else if(fn)
            {
                auto itv = fn->varTypes.find(chain[0]);
                if(itv != fn->varTypes.end())
                    baseType = itv->second;
            }
            if(!baseType.empty())
            {
                for(size_t idxChain = 1; idxChain < chain.size(); ++idxChain)
                {
                    auto sit = info.structs.find(baseType);
                    if(sit == info.structs.end())
                        break;
                    if(idxChain == chain.size() - 1 && isMethodCall)
                    {
                        auto* mi = find_method_in_struct(sit->second,
                                                         chain[idxChain], info);
                        if(mi)
                            return location_to_json(mi->loc);
                    }
                    auto field = find_field_in_struct(sit->second, chain[idxChain], info);
                    if(!field)
                        break;
                    if(idxChain == chain.size() - 1)
                        return location_to_json(field->loc);
                    baseType = field->typeName;
                }
            }
        }

        if(auto fn = find_enclosing_function(info, *line))
        {
            auto itv = fn->varDecls.find(word);
            if(itv != fn->varDecls.end())
                return location_to_json(itv->second);
            auto itp = fn->paramDecls.find(word);
            if(itp != fn->paramDecls.end())
                return location_to_json(itp->second);
        }

        if(auto fit = info.functions.find(word); fit != info.functions.end())
        {
            if(fit->second.isExtern)
            {
                if(auto cLoc = find_c_symbol_location(word))
                    return location_to_json(*cLoc);
            }
            return location_to_json(fit->second.loc);
        }
        if(auto sit = info.structs.find(word); sit != info.structs.end())
            return location_to_json(sit->second.loc);
        if(auto eit = info.enums.find(word); eit != info.enums.end())
            return location_to_json(eit->second.loc);
        for(auto& [sname, st] : info.structs)
        {
            auto itf = st.fields.find(word);
            if(itf != st.fields.end())
                return location_to_json(itf->second.loc);
        }
        for(auto& [ename, en] : info.enums)
        {
            auto itv = en.variants.find(word);
            if(itv != en.variants.end())
                return location_to_json(itv->second.loc);
        }

        for(const auto& imp : info.imports)
        {
            if(!imp.importAll && imp.itemName != word)
                continue;
            std::string modPath =
                resolve_module_path_for_file(info, imp.moduleName);
            if(modPath.empty())
                continue;
            if(auto* modInfo = get_or_index_file(modPath))
            {
                if(auto loc = find_symbol_in_file(*modInfo, word))
                    return location_to_json(*loc);
            }
        }

        if(auto loc = find_runtime_builtin_location(word, &info))
            return location_to_json(*loc);

        if(auto loc = find_symbol_in_workspace(word))
            return location_to_json(*loc);

        return nullptr;
    }

    llvm::json::Value handle_implementation(llvm::json::Object* params)
    {
        if(!params)
            return llvm::json::Array{};
        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return llvm::json::Array{};
        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return llvm::json::Array{};

        auto it = files.find(uri->str());
        if(it == files.end())
            return llvm::json::Array{};
        auto& info = it->second;
        if(*line < 0 || *line >= (int)info.lines.size())
            return llvm::json::Array{};

        mlang::Position p;
        p.line = static_cast<int>(*line);
        p.character = static_cast<int>(*character);
        auto ident = mlang::ide::identifierAt(info.lines, p);
        if(!ident)
            return llvm::json::Array{};

        std::unordered_set<std::string> bases;
        std::string symbol = ident->text;
        bool queryLooksLikeMethodDecl = false;
        std::string explicitOwner;

        if(info.structs.find(symbol) != info.structs.end())
            bases.insert(symbol);

        if(auto fn = find_enclosing_function(info, static_cast<int>(*line)))
        {
            if(!fn->ownerStruct.empty())
                bases.insert(fn->ownerStruct);
        }

        // Prefer direct owner inference at the query site for methods.
        // This avoids ambiguous owner mapping when multiple structs define
        // methods with the same name (e.g. Base::run and Derived::run).
        if(*line >= 0 && *line < (int)info.lines.size())
        {
            const std::string& lineText = info.lines[(size_t)*line];
            std::smatch sigMatch;
            if(std::regex_search(
                   lineText, sigMatch,
                   std::regex("\\bfn\\s+" + symbol +
                              "\\s*\\([^\\)]*\\bself\\s*:\\s*([A-Za-z_][A-Za-z0-9_:]*)")))
            {
                std::string owner = sigMatch[1].str();
                if(auto pos = owner.rfind("::"); pos != std::string::npos)
                    owner = owner.substr(pos + 2);
                if(!owner.empty())
                {
                    bases.insert(owner);
                    explicitOwner = owner;
                }
            }
            if(std::regex_search(
                   lineText,
                   std::regex("\\bfn\\s+" + symbol + "\\s*\\(")))
            {
                queryLooksLikeMethodDecl = true;
            }
        }

        for(const auto& [sname, st] : info.structs)
        {
            if(st.startLine < 0 || st.endLine < 0)
                continue;
            if(*line < st.startLine || *line > st.endLine)
                continue;
            auto mit = st.methods.find(symbol);
            if(mit == st.methods.end())
                continue;
            if(mit->second.loc.line == *line)
            {
                bases.insert(sname);
                if(explicitOwner.empty())
                    explicitOwner = sname;
            }
        }

        std::vector<Location> out_impls;
        auto gather_impl_for_base = [&](const std::string& baseName) {
            for(const auto& [_, finfo] : files)
            {
                for(const auto& [sname, st] : finfo.structs)
                {
                    if(sname == baseName)
                        continue;
                    std::string cur = st.baseName;
                    bool derives = false;
                    while(!cur.empty())
                    {
                        if(cur == baseName)
                        {
                            derives = true;
                            break;
                        }
                        auto itCur = finfo.structs.find(cur);
                        if(itCur == finfo.structs.end())
                            break;
                        cur = itCur->second.baseName;
                    }
                    if(!derives)
                        continue;

                    auto mit = st.methods.find(symbol);
                    if(mit == st.methods.end() || mit->second.loc.uri.empty())
                        continue;
                    out_impls.push_back(mit->second.loc);
                }
            }
        };

        std::unordered_set<std::string> seen;
        for(const auto& base : bases)
            gather_impl_for_base(base);

        if(out_impls.empty() && queryLooksLikeMethodDecl)
        {
            // Fallback: method declaration site but derivation chain was not
            // resolved. When owner is known, keep this constrained to derived
            // structs only; otherwise fall back to same-name methods.
            for(const auto& [_, finfo] : files)
            {
                for(const auto& [sname, st] : finfo.structs)
                {
                    if(!explicitOwner.empty())
                    {
                        std::string cur = st.baseName;
                        bool derives = false;
                        while(!cur.empty())
                        {
                            if(cur == explicitOwner)
                            {
                                derives = true;
                                break;
                            }
                            auto itCur = finfo.structs.find(cur);
                            if(itCur == finfo.structs.end())
                                break;
                            cur = itCur->second.baseName;
                        }
                        if(!derives)
                            continue;
                    }
                    auto mit = st.methods.find(symbol);
                    if(mit == st.methods.end() || mit->second.loc.uri.empty())
                        continue;
                    const auto& loc = mit->second.loc;
                    if(loc.uri == info.uri && loc.line == *line)
                        continue;
                    out_impls.push_back(loc);
                }
            }
        }

        if(out_impls.empty())
            return handle_definition(params);

        llvm::json::Array out;
        for(const auto& loc : out_impls)
        {
            std::string key =
                loc.uri + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.character);
            if(!seen.insert(key).second)
                continue;
            llvm::json::Object obj;
            obj["uri"] = loc.uri;
            obj["range"] = make_range_value(loc.line, loc.character,
                                            loc.character + 1);
            out.push_back(llvm::json::Value(std::move(obj)));
        }
        if(out.empty())
            return handle_definition(params);
        return llvm::json::Value(std::move(out));
    }

    struct SignatureHelpContext
    {
        std::string callee;
        int activeParameter = 0;
        int calleeStartCharacter = 0;
        int calleeEndCharacter = 0;
    };

    std::optional<SignatureHelpContext>
    parse_signature_help_context(std::string_view lineText, int character) const
    {
        int idx = character;
        if(idx < 0)
            return std::nullopt;
        if(idx > (int)lineText.size())
            idx = (int)lineText.size();

        int parenDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        int openParen = -1;

        for(int i = idx - 1; i >= 0; --i)
        {
            char c = lineText[(size_t)i];
            if(c == ')')
            {
                ++parenDepth;
                continue;
            }
            if(c == ']')
            {
                ++bracketDepth;
                continue;
            }
            if(c == '}')
            {
                ++braceDepth;
                continue;
            }
            if(c == '(')
            {
                if(parenDepth > 0)
                {
                    --parenDepth;
                    continue;
                }
                if(bracketDepth == 0 && braceDepth == 0)
                {
                    openParen = i;
                    break;
                }
                continue;
            }
            if(c == '[' && bracketDepth > 0)
            {
                --bracketDepth;
                continue;
            }
            if(c == '{' && braceDepth > 0)
            {
                --braceDepth;
                continue;
            }
        }

        if(openParen < 0)
            return std::nullopt;

        int j = openParen - 1;
        while(j >= 0 && std::isspace((unsigned char)lineText[(size_t)j]))
            --j;
        if(j < 0)
            return std::nullopt;

        int end = j;
        while(j >= 0)
        {
            char c = lineText[(size_t)j];
            if(std::isalnum((unsigned char)c) || c == '_' || c == ':' || c == '.')
                --j;
            else
                break;
        }

        std::string callee = std::string(lineText.substr((size_t)(j + 1),
                                                         (size_t)(end - j)));
        if(callee.empty())
            return std::nullopt;

        int activeParam = 0;
        int nestedParen = 0;
        int nestedBracket = 0;
        int nestedBrace = 0;
        for(int i = openParen + 1; i < idx; ++i)
        {
            char c = lineText[(size_t)i];
            if(c == '(')
                ++nestedParen;
            else if(c == ')' && nestedParen > 0)
                --nestedParen;
            else if(c == '[')
                ++nestedBracket;
            else if(c == ']' && nestedBracket > 0)
                --nestedBracket;
            else if(c == '{')
                ++nestedBrace;
            else if(c == '}' && nestedBrace > 0)
                --nestedBrace;
            else if(c == ',' && nestedParen == 0 && nestedBracket == 0 &&
                    nestedBrace == 0)
                ++activeParam;
        }

        SignatureHelpContext ctx;
        ctx.callee = callee;
        ctx.activeParameter = activeParam;
        ctx.calleeStartCharacter = j + 1;
        ctx.calleeEndCharacter = end + 1;
        return ctx;
    }

    std::string unqualified_callee_name(const std::string& callee) const
    {
        size_t pos = callee.rfind("::");
        if(pos != std::string::npos)
            return callee.substr(pos + 2);
        pos = callee.rfind('.');
        if(pos != std::string::npos)
            return callee.substr(pos + 1);
        return callee;
    }

    std::vector<std::string> signature_parameter_labels(const std::string& signature) const
    {
        std::vector<std::string> out;
        size_t open = signature.find('(');
        if(open == std::string::npos)
            return out;

        int depth = 0;
        size_t close = std::string::npos;
        for(size_t i = open; i < signature.size(); ++i)
        {
            char c = signature[i];
            if(c == '(')
                ++depth;
            else if(c == ')')
            {
                --depth;
                if(depth == 0)
                {
                    close = i;
                    break;
                }
            }
        }
        if(close == std::string::npos || close <= open + 1)
            return out;

        std::string inside = signature.substr(open + 1, close - open - 1);
        int angle = 0;
        int paren = 0;
        int bracket = 0;
        int brace = 0;
        size_t segStart = 0;

        for(size_t i = 0; i <= inside.size(); ++i)
        {
            char c = i < inside.size() ? inside[i] : ',';
            if(i < inside.size())
            {
                if(c == '<')
                    ++angle;
                else if(c == '>' && angle > 0)
                    --angle;
                else if(c == '(')
                    ++paren;
                else if(c == ')' && paren > 0)
                    --paren;
                else if(c == '[')
                    ++bracket;
                else if(c == ']' && bracket > 0)
                    --bracket;
                else if(c == '{')
                    ++brace;
                else if(c == '}' && brace > 0)
                    --brace;
            }

            bool split = (c == ',' && angle == 0 && paren == 0 && bracket == 0 &&
                          brace == 0) || (i == inside.size());
            if(!split)
                continue;

            std::string part = trim(inside.substr(segStart, i - segStart));
            if(!part.empty())
                out.push_back(part);
            segStart = i + 1;
        }

        return out;
    }

    llvm::json::Value make_signature_help_response(
        const std::vector<llvm::json::Object>& signatures,
        int activeSignature, int activeParameter)
    {
        if(signatures.empty())
            return nullptr;

        llvm::json::Array sigArray;
        for(auto sig : signatures)
            sigArray.push_back(llvm::json::Value(std::move(sig)));

        llvm::json::Object out;
        out["signatures"] = llvm::json::Value(std::move(sigArray));
        out["activeSignature"] = std::max(0, activeSignature);
        out["activeParameter"] = std::max(0, activeParameter);
        return llvm::json::Value(std::move(out));
    }

    void add_signature_candidate(std::vector<llvm::json::Object>& out,
                                 std::unordered_set<std::string>& seen,
                                 FileInfo& info, FunctionInfo& fn,
                                 const std::string& calleeName,
                                 int activeParameter)
    {
        std::string fallback = "fn " + calleeName + "(...)";
        if(!fn.returnType.empty())
            fallback += " -> " + fn.returnType;

        int sigLine = fn.startLine >= 0 ? fn.startLine : fn.loc.line;
        std::string label = signature_from_line(info, sigLine, fallback);
        std::string key = info.uri + ":" + std::to_string(fn.loc.line) + ":" + label;
        if(!seen.insert(key).second)
            return;

        llvm::json::Object sig;
        sig["label"] = label;

        std::string docs = docs_before_line(info, fn.loc.line);
        if(!docs.empty())
        {
            llvm::json::Object doc;
            doc["kind"] = "markdown";
            doc["value"] = docs;
            sig["documentation"] = llvm::json::Value(std::move(doc));
        }

        auto params = signature_parameter_labels(label);
        if(!params.empty())
        {
            llvm::json::Array p;
            for(const auto& item : params)
            {
                llvm::json::Object pi;
                pi["label"] = item;
                p.push_back(llvm::json::Value(std::move(pi)));
            }
            sig["parameters"] = llvm::json::Value(std::move(p));

            int maxParam = (int)params.size() - 1;
            if(activeParameter > maxParam && maxParam >= 0)
                activeParameter = maxParam;
        }

        out.push_back(std::move(sig));
    }

    llvm::json::Value handle_signature_help(llvm::json::Object* params)
    {
        if(!params)
            return nullptr;

        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return nullptr;

        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return nullptr;

        auto it = files.find(uri->str());
        if(it == files.end())
            return nullptr;

        FileInfo& info = it->second;
        if(*line < 0 || *line >= (int)info.lines.size())
            return nullptr;

        const std::string& lineText = info.lines[(size_t)*line];
        auto ctx = parse_signature_help_context(lineText,
                                                static_cast<int>(*character));
        if(!ctx)
            return nullptr;

        std::string calleeName = unqualified_callee_name(ctx->callee);
        if(calleeName.empty())
            return nullptr;

        if(auto sem = resolve_semantic_symbol(uri->str(), static_cast<int>(*line),
                                              ctx->calleeStartCharacter))
        {
            std::string label = sem->signature;
            if(label.empty())
            {
                label = "fn " + sem->name + "(...)";
                if(!sem->typeInfo.empty())
                    label += " -> " + sem->typeInfo;
            }

            llvm::json::Object sig;
            sig["label"] = label;
            auto paramsLabels = signature_parameter_labels(label);
            if(!paramsLabels.empty())
            {
                llvm::json::Array p;
                for(const auto& item : paramsLabels)
                {
                    llvm::json::Object pi;
                    pi["label"] = item;
                    p.push_back(llvm::json::Value(std::move(pi)));
                }
                sig["parameters"] = llvm::json::Value(std::move(p));
            }

            std::string doc = "kind: ";
            doc += semantic_kind_name(sem->kind);
            if(!sem->typeInfo.empty())
                doc += "\nreturn/type: " + sem->typeInfo;
            if(sem->overloadCount > 1)
                doc += "\noverloads: " + std::to_string(sem->overloadCount);
            llvm::json::Object docObj;
            docObj["kind"] = "markdown";
            docObj["value"] = doc;
            sig["documentation"] = llvm::json::Value(std::move(docObj));

            llvm::json::Array sigArray;
            sigArray.push_back(llvm::json::Value(std::move(sig)));

            int activeParam = std::max(0, ctx->activeParameter);
            if(!paramsLabels.empty())
                activeParam = std::min(activeParam, (int)paramsLabels.size() - 1);

            llvm::json::Object out;
            out["signatures"] = llvm::json::Value(std::move(sigArray));
            out["activeSignature"] = 0;
            out["activeParameter"] = activeParam;
            return llvm::json::Value(std::move(out));
        }

        std::vector<llvm::json::Object> signatures;
        std::unordered_set<std::string> seen;

        for(auto& [key, fn] : info.functions)
        {
            if(fn.name == calleeName || key == calleeName ||
               (key.size() > calleeName.size() + 2 &&
                key.compare(key.size() - calleeName.size(), calleeName.size(),
                            calleeName) == 0 &&
                key[key.size() - calleeName.size() - 2] == ':' &&
                key[key.size() - calleeName.size() - 1] == ':'))
            {
                add_signature_candidate(signatures, seen, info, fn, calleeName,
                                        ctx->activeParameter);
            }
        }

        for(const auto& imp : info.imports)
        {
            std::string modPath = resolve_module_path_for_file(info, imp.moduleName);
            if(modPath.empty())
                continue;
            FileInfo* modInfo = get_or_index_file(modPath);
            if(!modInfo)
                continue;

            for(auto& [key, fn] : modInfo->functions)
            {
                if(fn.name != calleeName && key != calleeName)
                    continue;
                add_signature_candidate(signatures, seen, *modInfo, fn,
                                        calleeName, ctx->activeParameter);
            }
        }

        for(auto& [_, finfo] : files)
        {
            for(auto& [key, fn] : finfo.functions)
            {
                if(fn.name != calleeName && key != calleeName)
                    continue;
                add_signature_candidate(signatures, seen, finfo, fn,
                                        calleeName, ctx->activeParameter);
            }
        }

        return make_signature_help_response(signatures, 0, ctx->activeParameter);
    }

    llvm::json::Value handle_completion(llvm::json::Object* params)
    {
        if(!params)
            return llvm::json::Array{};
        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return llvm::json::Array{};
        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return llvm::json::Array{};

        auto it = files.find(uri->str());
        if(it == files.end())
            return llvm::json::Array{};
        auto& info = it->second;
        if(*line < 0 || *line >= (int)info.lines.size())
            return llvm::json::Array{};
        const std::string& lineText = info.lines[*line];
        int idx = (int)*character;
        if(idx > (int)lineText.size())
            idx = (int)lineText.size();

        std::string prefix = mlang::ide::identifierPrefixAt(lineText, idx);
        int start = idx - static_cast<int>(prefix.size());

        std::vector<CompletionCandidate> candidates;
        std::unordered_set<std::string> seen;

        std::string modulePrefix = find_module_prefix(lineText, start);
        if(!modulePrefix.empty())
        {
            std::string modPath =
                resolve_module_path_for_file(info, modulePrefix);
            if(!modPath.empty())
            {
                if(auto* modInfo = get_or_index_file(modPath))
                {
                    collect_file_completions(*modInfo, prefix, candidates, seen);
                }
            }
        }
        else
        {
            std::string memberBase = find_member_base(lineText, start);
            if(!memberBase.empty())
            {
                if(auto fn = find_enclosing_function(info, *line))
                {
                    std::string baseType;
                    if(memberBase == "self" && !fn->ownerStruct.empty())
                        baseType = fn->ownerStruct;
                    else
                    {
                        auto itv = fn->varTypes.find(memberBase);
                        if(itv != fn->varTypes.end())
                            baseType = itv->second;
                    }
                    if(!baseType.empty())
                    {
                        FileInfo* stFile = nullptr;
                        StructInfo* st = find_struct_in_workspace(baseType,
                                                                  &stFile);
                        if(st && stFile)
                        {
                            for(const auto& [fname, field] : st->fields)
                                add_completion(candidates, seen, fname, 5,
                                               prefix);
                            for(const auto& [mname, method] : st->methods)
                                add_completion(candidates, seen, mname, 2,
                                               prefix);
                        }
                    }
                }
            }
            else
            {
                if(auto fn = find_enclosing_function(info, *line))
                {
                    for(const auto& [name, loc] : fn->varDecls)
                        add_completion(candidates, seen, name, 6, prefix);
                    for(const auto& [name, loc] : fn->paramDecls)
                        add_completion(candidates, seen, name, 6, prefix);
                }

                for(const auto* kw : mlang::constants::kLspKeywords)
                    add_completion(candidates, seen, kw, 14, prefix);
                add_completion_snippet(
                    candidates, seen, "match", 14, prefix,
                    "match ${1:expr} { Ok(${2:value}) => ${3:value}, "
                    "Err(${4:err}) => ${5:value} }");
                add_completion_snippet(candidates, seen, "Ok", 3, prefix,
                                       "Ok(${1:value})");
                add_completion_snippet(candidates, seen, "Err", 3, prefix,
                                       "Err(${1:error})");

                collect_file_completions(info, prefix, candidates, seen);

                for(const auto& mod : info.moduleDecls)
                    add_completion(candidates, seen, mod, 9, prefix);

                for(const auto& imp : info.imports)
                {
                    std::string modPath =
                        resolve_module_path_for_file(info, imp.moduleName);
                    if(modPath.empty())
                        continue;
                    if(imp.importAll)
                    {
                        if(auto* modInfo = get_or_index_file(modPath))
                        {
                            collect_file_completions(*modInfo, prefix,
                                                     candidates, seen);
                        }
                        continue;
                    }
                    if(!imp.itemName.empty() &&
                       starts_with(imp.itemName, prefix))
                    {
                        if(auto* modInfo = get_or_index_file(modPath))
                        {
                            if(auto loc =
                                   find_symbol_in_file(*modInfo, imp.itemName))
                            {
                                add_completion(candidates, seen, imp.itemName,
                                               0, prefix);
                                continue;
                            }
                        }
                        add_completion(candidates, seen, imp.itemName, 0,
                                       prefix);
                    }
                }
            }
        }

        llvm::json::Array out;
        for(const auto& item : candidates)
        {
            llvm::json::Object obj;
            obj["label"] = item.label;
            obj["insertText"] =
                item.insertText.empty() ? item.label : item.insertText;
            if(item.kind > 0)
                obj["kind"] = item.kind;
            if(item.isSnippet)
                obj["insertTextFormat"] = 2;
            out.push_back(llvm::json::Value(std::move(obj)));
        }
        return llvm::json::Value(std::move(out));
    }

    FunctionInfo* find_enclosing_function(FileInfo& info, int line)
    {
        for(auto* fn : info.functionSpans)
        {
            if(fn->startLine <= line && fn->endLine >= line)
                return fn;
        }
        return nullptr;
    }

    llvm::json::Value location_to_json(const Location& loc)
    {
        if(loc.uri.empty())
            return nullptr;
        llvm::json::Object obj;
        obj["uri"] = loc.uri;
        obj["range"] = make_range_value(loc.line, loc.character,
                                        loc.character + 1);
        llvm::json::Array arr;
        arr.push_back(llvm::json::Value(std::move(obj)));
        return llvm::json::Value(std::move(arr));
    }

    llvm::json::Value handle_document_highlight(llvm::json::Object* params)
    {
        llvm::json::Array out;
        if(!params)
            return llvm::json::Value(std::move(out));

        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return llvm::json::Value(std::move(out));

        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return llvm::json::Value(std::move(out));

        auto it = files.find(uri->str());
        if(it == files.end())
            return llvm::json::Value(std::move(out));

        FileInfo& info = it->second;
        mlang::Position queryPos;
        queryPos.line = static_cast<int>(*line);
        queryPos.character = static_cast<int>(*character);
        auto ident = mlang::ide::identifierAt(info.lines, queryPos);
        if(!ident)
            return llvm::json::Value(std::move(out));

        std::unordered_set<std::string> writeKeys;
        for(auto* fn : info.functionSpans)
        {
            for(const auto& [name, loc] : fn->paramDecls)
            {
                if(name != ident->text)
                    continue;
                writeKeys.insert(std::to_string(loc.line) + ":" +
                                 std::to_string(loc.character));
            }
            for(const auto& [name, loc] : fn->varDecls)
            {
                if(name != ident->text)
                    continue;
                writeKeys.insert(std::to_string(loc.line) + ":" +
                                 std::to_string(loc.character));
            }
        }

        for(int ln = 0; ln < (int)info.lines.size(); ++ln)
        {
            auto matches = mlang::ide::findWholeWordMatches(
                std::vector<std::string>{info.lines[(size_t)ln]}, ident->text);
            if(matches.empty())
                continue;

            for(const auto& m : matches)
            {
                int start = m.start.character;
                int end = m.end.character;
                int kind = 2; // read

                std::string key = std::to_string(ln) + ":" + std::to_string(start);
                if(writeKeys.find(key) != writeKeys.end())
                {
                    kind = 3; // write
                }
                else
                {
                    const std::string& lineText = info.lines[(size_t)ln];
                    size_t idx = (size_t)end;
                    while(idx < lineText.size() &&
                          std::isspace((unsigned char)lineText[idx]))
                        ++idx;
                    if(idx < lineText.size() && lineText[idx] == '=')
                        kind = 3;
                }

                llvm::json::Object obj;
                obj["range"] = make_range_value(ln, start, ln, end);
                obj["kind"] = kind;
                out.push_back(llvm::json::Value(std::move(obj)));
            }
        }

        return llvm::json::Value(std::move(out));
    }

    llvm::json::Value handle_references(llvm::json::Object* params)
    {
        if(!params)
            return llvm::json::Array{};
        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return llvm::json::Array{};
        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return llvm::json::Array{};

        if(compilerSession)
        {
            if(auto sem = resolve_semantic_symbol(uri->str(), static_cast<int>(*line),
                                                  static_cast<int>(*character)))
            {
                int count = 0;
                int st = __mlang_compiler_document_reference_count(
                    compilerSession, sem->uri.c_str(), sem->line0 + 1,
                    sem->column0 + 1, &count);
                if(st == 0)
                {
                    llvm::json::Array out;
                    for(int i = 0; i < count; ++i)
                    {
                        char refUriBuf[4096];
                        int refUriLen = 0;
                        int refLine1 = 0;
                        int refCol1 = 0;
                        int gst = __mlang_compiler_document_reference_get(
                            compilerSession, sem->uri.c_str(), sem->line0 + 1,
                            sem->column0 + 1, i, refUriBuf, sizeof(refUriBuf),
                            &refUriLen, &refLine1, &refCol1);
                        if(gst != 0)
                            continue;

                        int l0 = std::max(0, refLine1 - 1);
                        int c0 = std::max(0, refCol1 - 1);
                        llvm::json::Object loc;
                        loc["uri"] = std::string(refUriBuf);
                        loc["range"] = make_range_value(l0, c0, l0, c0 + 1);
                        out.push_back(llvm::json::Value(std::move(loc)));
                    }
                    return llvm::json::Value(std::move(out));
                }
            }
        }

        auto it = files.find(uri->str());
        if(it == files.end())
            return llvm::json::Array{};
        auto& info = it->second;
        if(*line < 0 || *line >= (int)info.lines.size())
            return llvm::json::Array{};
        mlang::Position queryPos;
        queryPos.line = static_cast<int>(*line);
        queryPos.character = static_cast<int>(*character);
        auto ident = mlang::ide::identifierAt(info.lines, queryPos);
        if(!ident)
            return llvm::json::Array{};

        llvm::json::Array out;
        for(auto& [furi, finfo] : files)
        {
            auto matches =
                mlang::ide::findWholeWordMatches(finfo.lines, ident->text);
            for(const auto& range : matches)
            {
                llvm::json::Object loc;
                loc["uri"] = finfo.uri;
                loc["range"] = make_range_value(
                    range.start.line, range.start.character,
                    range.end.line, range.end.character);
                out.push_back(llvm::json::Value(std::move(loc)));
            }
        }
        return llvm::json::Value(std::move(out));
    }

    static bool is_valid_identifier_name(const std::string& name)
    {
        if(name.empty())
            return false;
        unsigned char first = static_cast<unsigned char>(name[0]);
        if(!(std::isalpha(first) || name[0] == '_'))
            return false;
        for(size_t i = 1; i < name.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(name[i]);
            if(!(std::isalnum(c) || name[i] == '_'))
                return false;
        }
        return true;
    }

    bool is_rename_blocked_word(const std::string& word) const
    {
        if(std::find(mlang::constants::kLspKeywords.begin(),
                      mlang::constants::kLspKeywords.end(),
                      word) != mlang::constants::kLspKeywords.end())
            return true;
        if(std::find(mlang::constants::kRuntimeBuiltinFunctions.begin(),
                      mlang::constants::kRuntimeBuiltinFunctions.end(),
                      word) != mlang::constants::kRuntimeBuiltinFunctions.end())
            return true;
        if(std::find(mlang::constants::kRuntimeBuiltinTypes.begin(),
                      mlang::constants::kRuntimeBuiltinTypes.end(),
                      word) != mlang::constants::kRuntimeBuiltinTypes.end())
            return true;
        if(std::find(mlang::constants::kAttributeKeywords.begin(),
                      mlang::constants::kAttributeKeywords.end(),
                      word) != mlang::constants::kAttributeKeywords.end())
            return true;
        return false;
    }

    std::optional<mlang::ide::IdentifierMatch>
    rename_target_at(FileInfo& info, int line, int character)
    {
        if(line < 0 || line >= (int)info.lines.size())
            return std::nullopt;

        mlang::Position pos;
        pos.line = line;
        pos.character = character;
        auto ident = mlang::ide::identifierAt(info.lines, pos);
        if(!ident)
            return std::nullopt;
        if(is_rename_blocked_word(ident->text))
            return std::nullopt;

        bool found = false;
        if(auto fn = find_enclosing_function(info, line))
        {
            if(fn->varDecls.find(ident->text) != fn->varDecls.end() ||
               fn->paramDecls.find(ident->text) != fn->paramDecls.end() ||
               fn->varTypes.find(ident->text) != fn->varTypes.end())
            {
                found = true;
            }
        }

        if(!found)
        {
            if(auto fit = info.functions.find(ident->text); fit != info.functions.end())
                found = true;
            if(auto sit = info.structs.find(ident->text); sit != info.structs.end())
                found = true;
            if(auto eit = info.enums.find(ident->text); eit != info.enums.end())
                found = true;
            if(find_symbol_in_workspace(ident->text).has_value())
                found = true;
        }

        if(!found)
            return std::nullopt;
        return ident;
    }

    llvm::json::Value handle_prepare_rename(llvm::json::Object* params)
    {
        if(!params)
            return nullptr;
        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        if(!textDoc || !pos)
            return nullptr;

        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return nullptr;

        auto it = files.find(uri->str());
        if(it == files.end())
            return nullptr;

        auto ident = rename_target_at(it->second, (int)*line, (int)*character);
        if(!ident)
            return nullptr;

        llvm::json::Object out;
        out["range"] = make_range_value((int)*line, ident->startCharacter,
                                        (int)*line, ident->endCharacter);
        out["placeholder"] = ident->text;
        return llvm::json::Value(std::move(out));
    }

    llvm::json::Value handle_rename(llvm::json::Object* params)
    {
        if(!params)
            return nullptr;
        auto* textDoc = params->getObject("textDocument");
        auto* pos = params->getObject("position");
        auto newName = params->getString("newName");
        if(!textDoc || !pos || !newName)
            return nullptr;

        std::string newNameStr = newName->str();
        if(!is_valid_identifier_name(newNameStr) ||
           is_rename_blocked_word(newNameStr))
        {
            return nullptr;
        }

        auto uri = textDoc->getString("uri");
        auto line = pos->getInteger("line");
        auto character = pos->getInteger("character");
        if(!uri || !line || !character)
            return nullptr;

        auto it = files.find(uri->str());
        if(it == files.end())
            return nullptr;

        auto ident = rename_target_at(it->second, (int)*line, (int)*character);
        if(!ident)
            return nullptr;

        if(compilerSession)
        {
            int isSafe = 0;
            int st = __mlang_compiler_document_rename_is_safe(
                compilerSession, uri->str().c_str(), (int)*line + 1,
                (int)*character + 1, newNameStr.c_str(), &isSafe);
            if(st != 0 || isSafe == 0)
                return nullptr;
        }

        llvm::json::Object changes;
        for(auto& [furi, finfo] : files)
        {
            auto matches = mlang::ide::findWholeWordMatches(finfo.lines, ident->text);
            if(matches.empty())
                continue;

            llvm::json::Array edits;
            for(const auto& m : matches)
            {
                llvm::json::Object edit;
                edit["range"] = make_range_value(m.start.line, m.start.character,
                                                  m.end.line, m.end.character);
                edit["newText"] = newNameStr;
                edits.push_back(llvm::json::Value(std::move(edit)));
            }
            changes[furi] = llvm::json::Value(std::move(edits));
        }

        llvm::json::Object out;
        out["changes"] = llvm::json::Value(std::move(changes));
        return llvm::json::Value(std::move(out));
    }

    static int edit_distance_limited(const std::string& a,
                                     const std::string& b,
                                     int maxDistance = 3)
    {
        if(a == b)
            return 0;
        if(a.empty() || b.empty())
            return (int)std::max(a.size(), b.size());

        if(std::abs((int)a.size() - (int)b.size()) > maxDistance)
            return maxDistance + 1;

        std::vector<int> prev(b.size() + 1);
        std::vector<int> cur(b.size() + 1);
        for(size_t j = 0; j <= b.size(); ++j)
            prev[j] = (int)j;

        for(size_t i = 1; i <= a.size(); ++i)
        {
            cur[0] = (int)i;
            int rowMin = cur[0];
            for(size_t j = 1; j <= b.size(); ++j)
            {
                int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                cur[j] = std::min({
                    prev[j] + 1,
                    cur[j - 1] + 1,
                    prev[j - 1] + cost,
                });
                rowMin = std::min(rowMin, cur[j]);
            }
            if(rowMin > maxDistance)
                return maxDistance + 1;
            prev.swap(cur);
        }

        return prev[b.size()];
    }

    std::string extract_unknown_symbol_from_message(const std::string& msg) const
    {
        static const std::regex rx1("unknown\\s+(?:function|symbol|type)\\s*:\\s*'([A-Za-z_][A-Za-z0-9_]*)'",
                                    std::regex::icase);
        static const std::regex rx2("undeclared\\s+identifier\\s*:\\s*'([A-Za-z_][A-Za-z0-9_]*)'",
                                    std::regex::icase);
        std::smatch m;
        if(std::regex_search(msg, m, rx1) && m.size() >= 2)
            return m[1].str();
        if(std::regex_search(msg, m, rx2) && m.size() >= 2)
            return m[1].str();
        return {};
    }

    std::vector<std::string> collect_workspace_symbol_names() const
    {
        std::unordered_set<std::string> names;
        for(const auto& [_, info] : files)
        {
            for(const auto& [name, _] : info.functions)
                names.insert(name);
            for(const auto& [name, st] : info.structs)
            {
                names.insert(name);
                for(const auto& [field, __] : st.fields)
                    names.insert(field);
                for(const auto& [method, __] : st.methods)
                    names.insert(method);
            }
            for(const auto& [name, en] : info.enums)
            {
                names.insert(name);
                for(const auto& [variant, __] : en.variants)
                    names.insert(variant);
            }
        }

        std::vector<std::string> out;
        out.reserve(names.size());
        for(const auto& n : names)
            out.push_back(n);
        return out;
    }

    std::optional<std::string>
    best_symbol_correction(const std::string& unknown) const
    {
        auto names = collect_workspace_symbol_names();
        int bestDist = 4;
        std::string best;

        for(const auto& name : names)
        {
            std::string probe = name;
            size_t pos = probe.rfind("::");
            if(pos != std::string::npos)
                probe = probe.substr(pos + 2);

            int dist = edit_distance_limited(unknown, probe, 3);
            if(dist < bestDist)
            {
                bestDist = dist;
                best = probe;
            }
        }

        if(best.empty() || bestDist > 2)
            return std::nullopt;
        return best;
    }

    int preferred_import_insertion_line(const FileInfo& info) const
    {
        int line = 0;
        for(; line < (int)info.lines.size(); ++line)
        {
            std::string t = trim(info.lines[(size_t)line]);
            if(t.empty())
                continue;
            if(t.starts_with("//"))
                continue;
            if(t.starts_with("mod ") || t.starts_with("use "))
                continue;
            break;
        }
        return line;
    }

    std::vector<std::pair<std::string, std::string>>
    find_import_candidates(const FileInfo& current,
                           const std::string& symbol) const
    {
        std::unordered_set<std::string> existing;
        for(const auto& imp : current.imports)
        {
            if(!imp.moduleName.empty() && !imp.importAll && !imp.itemName.empty())
                existing.insert(imp.moduleName + "::" + imp.itemName);
        }

        std::vector<std::pair<std::string, std::string>> out;
        std::unordered_set<std::string> seen;

        for(const auto& [_, info] : files)
        {
            std::string module = module_name_for_file(info);
            if(module.empty())
                continue;

            bool hasSymbol = false;
            if(info.functions.find(symbol) != info.functions.end())
                hasSymbol = true;
            if(info.structs.find(symbol) != info.structs.end())
                hasSymbol = true;
            if(info.enums.find(symbol) != info.enums.end())
                hasSymbol = true;

            if(!hasSymbol)
                continue;

            std::string qualified = module + "::" + symbol;
            if(existing.find(qualified) != existing.end())
                continue;
            if(seen.insert(qualified).second)
                out.emplace_back(module, symbol);
        }

        return out;
    }

    llvm::json::Value handle_code_action(llvm::json::Object* params)
    {
        llvm::json::Array out;
        if(!params)
            return llvm::json::Value(std::move(out));

        auto* textDoc = params->getObject("textDocument");
        auto* range = params->getObject("range");
        auto* context = params->getObject("context");
        if(!textDoc || !range || !context)
            return llvm::json::Value(std::move(out));

        auto uri = textDoc->getString("uri");
        auto* diagnostics = context->getArray("diagnostics");
        if(!uri || !diagnostics)
            return llvm::json::Value(std::move(out));

        auto fit = files.find(uri->str());
        if(fit == files.end())
            return llvm::json::Value(std::move(out));
        FileInfo& current = fit->second;

        bool wantQuickFix = true;
        bool wantOrganizeImports = true;
        if(auto* only = context->getArray("only"))
        {
            wantQuickFix = false;
            wantOrganizeImports = false;
            for(const auto& entry : *only)
            {
                auto kind = entry.getAsString();
                if(!kind)
                    continue;
                if(kind->str() == "quickfix")
                    wantQuickFix = true;
                if(kind->str() == "source.organizeImports")
                    wantOrganizeImports = true;
            }
        }

        if(wantOrganizeImports)
        {
            int firstUse = -1;
            int lastUse = -1;
            std::vector<std::string> imports;
            for(int i = 0; i < (int)current.lines.size(); ++i)
            {
                std::string t = trim(current.lines[(size_t)i]);
                if(!t.starts_with("use "))
                    continue;
                if(firstUse < 0)
                    firstUse = i;
                lastUse = i;
                imports.push_back(t);
            }
            if(firstUse >= 0 && lastUse >= firstUse)
            {
                std::sort(imports.begin(), imports.end());
                imports.erase(std::unique(imports.begin(), imports.end()),
                              imports.end());
                std::string newText;
                for(const auto& imp : imports)
                    newText += imp + "\n";

                std::string oldText;
                for(int i = firstUse; i <= lastUse; ++i)
                    oldText += current.lines[(size_t)i] + "\n";

                if(newText != oldText)
                {
                    llvm::json::Object action;
                    action["title"] = "Organize imports";
                    action["kind"] = "source.organizeImports";
                    llvm::json::Object editObj;
                    llvm::json::Object changes;
                    llvm::json::Array edits;
                    llvm::json::Object edit;
                    edit["range"] = make_range_value(firstUse, 0, lastUse + 1, 0);
                    edit["newText"] = newText;
                    edits.push_back(llvm::json::Value(std::move(edit)));
                    changes[uri->str()] = llvm::json::Value(std::move(edits));
                    editObj["changes"] = llvm::json::Value(std::move(changes));
                    action["edit"] = llvm::json::Value(std::move(editObj));
                    out.push_back(llvm::json::Value(std::move(action)));
                }
            }
        }

        if(!wantQuickFix)
            return llvm::json::Value(std::move(out));

        for(const auto& d : *diagnostics)
        {
            auto* dobj = d.getAsObject();
            if(!dobj)
                continue;

            auto msg = dobj->getString("message");
            auto* drange = dobj->getObject("range");
            if(!msg || !drange)
                continue;

            int dStartLine = -1;
            int dStartChar = -1;
            int dEndLine = -1;
            int dEndChar = -1;
            if(auto* start = drange->getObject("start"))
            {
                if(auto* end = drange->getObject("end"))
                {
                    auto sl = start->getInteger("line");
                    auto sc = start->getInteger("character");
                    auto el = end->getInteger("line");
                    auto ec = end->getInteger("character");
                    if(sl && sc && el && ec)
                    {
                        dStartLine = (int)*sl;
                        dStartChar = (int)*sc;
                        dEndLine = (int)*el;
                        dEndChar = (int)*ec;
                    }
                }
            }

            std::string unknown = extract_unknown_symbol_from_message(msg->str());
            if(unknown.empty() && dStartLine >= 0 && dStartLine == dEndLine)
            {
                int lineNo = dStartLine;
                int startCh = dStartChar;
                int endCh = dEndChar;
                if(lineNo >= 0 && lineNo < (int)current.lines.size() &&
                   startCh >= 0 && endCh >= startCh &&
                   endCh <= (int)current.lines[(size_t)lineNo].size())
                {
                    unknown = current.lines[(size_t)lineNo].substr(
                        (size_t)startCh,
                        (size_t)(endCh - startCh));
                }
            }

            if(unknown.empty())
                continue;

            if(auto replacement = best_symbol_correction(unknown))
            {
                llvm::json::Object action;
                action["title"] = "Replace with '" + *replacement + "'";
                action["kind"] = "quickfix";
                llvm::json::Array diagArray;
                diagArray.push_back(d);
                action["diagnostics"] = llvm::json::Value(std::move(diagArray));

                llvm::json::Object editObj;
                llvm::json::Object changes;
                llvm::json::Array edits;
                llvm::json::Object edit;
                if(dStartLine < 0 || dStartChar < 0 || dEndLine < 0 || dEndChar < 0)
                    continue;
                edit["range"] = make_range_value(dStartLine, dStartChar, dEndLine, dEndChar);
                edit["newText"] = *replacement;
                edits.push_back(llvm::json::Value(std::move(edit)));
                changes[uri->str()] = llvm::json::Value(std::move(edits));
                editObj["changes"] = llvm::json::Value(std::move(changes));
                action["edit"] = llvm::json::Value(std::move(editObj));
                out.push_back(llvm::json::Value(std::move(action)));
            }

            auto imports = find_import_candidates(current, unknown);
            for(const auto& [module, symbol] : imports)
            {
                int insertLine = preferred_import_insertion_line(current);
                llvm::json::Object action;
                action["title"] = "Add import: use " + module + "::" + symbol + ";";
                action["kind"] = "quickfix";
                llvm::json::Array diagArray;
                diagArray.push_back(d);
                action["diagnostics"] = llvm::json::Value(std::move(diagArray));

                llvm::json::Object editObj;
                llvm::json::Object changes;
                llvm::json::Array edits;
                llvm::json::Object edit;
                edit["range"] = make_range_value(insertLine, 0, insertLine, 0);
                edit["newText"] = "use " + module + "::" + symbol + ";\n";
                edits.push_back(llvm::json::Value(std::move(edit)));
                changes[uri->str()] = llvm::json::Value(std::move(edits));
                editObj["changes"] = llvm::json::Value(std::move(changes));
                action["edit"] = llvm::json::Value(std::move(editObj));
                out.push_back(llvm::json::Value(std::move(action)));
            }
        }

        return llvm::json::Value(std::move(out));
    }

    std::optional<Location>
    find_token_location(const FileInfo& info, const std::string& token) const
    {
        if(token.empty())
            return std::nullopt;
        for(int l = 0; l < (int)info.lines.size(); ++l)
        {
            const std::string& line = info.lines[(size_t)l];
            size_t pos = line.find(token);
            if(pos == std::string::npos)
                continue;
            Location loc;
            loc.uri = info.uri;
            loc.line = l;
            loc.character = (int)pos;
            return loc;
        }
        return std::nullopt;
    }

    static std::string stable_diag_id(const std::string& uri, int line,
                                      int character,
                                      const std::string& code,
                                      const std::string& message)
    {
        std::string key = uri + "|" + std::to_string(line) + ":" +
                          std::to_string(character) + "|" + code + "|" +
                          message;
        size_t h = std::hash<std::string>{}(key);
        std::ostringstream os;
        os << std::hex << h;
        return os.str();
    }

    static llvm::json::Object make_diag_item(const std::string& uri,
                                             int line,
                                             int startChar,
                                             int endChar,
                                             int severity,
                                             const std::string& code,
                                             const std::string& message)
    {
        llvm::json::Object d;
        llvm::json::Object range;
        llvm::json::Object start;
        start["line"] = line;
        start["character"] = startChar;
        llvm::json::Object end;
        end["line"] = line;
        end["character"] = endChar;
        range["start"] = llvm::json::Value(std::move(start));
        range["end"] = llvm::json::Value(std::move(end));

        d["range"] = llvm::json::Value(std::move(range));
        d["severity"] = severity;
        d["source"] = "mlangd";
        d["code"] = code;
        d["message"] = message;

        llvm::json::Object data;
        data["id"] = stable_diag_id(uri, line, startChar, code, message);
        d["data"] = llvm::json::Value(std::move(data));
        return d;
    }

    static std::string compute_diagnostic_result_id(const llvm::json::Array& items)
    {
        std::string key;
        key.reserve(items.size() * 64);
        for(const auto& item : items)
        {
            const auto* obj = item.getAsObject();
            if(!obj)
                continue;
            if(auto msg = obj->getString("message"))
                key += msg->str();
            if(auto code = obj->getString("code"))
                key += code->str();
            if(auto* data = obj->getObject("data"))
            {
                if(auto id = data->getString("id"))
                    key += id->str();
            }
            key.push_back('\n');
        }
        size_t h = std::hash<std::string>{}(key);
        std::ostringstream os;
        os << std::hex << h;
        return os.str();
    }

    llvm::json::Array collect_pull_diagnostics(const std::string& uri,
                                               FileInfo& info)
    {
        llvm::json::Array out;

        auto* doc = incrementalCompiler.getDocument(uri);
        if(doc)
        {
            if(!doc->ast)
            {
                out.push_back(llvm::json::Value(make_diag_item(
                    uri, 0, 0, 1, 1, "parser/failed",
                    "Failed to parse document.")));
                return out;
            }

            if(doc->usedRecovery)
            {
                if(doc->recoveryKind == mlang::ParseRecoveryKind::DelimiterClosure)
                {
                    out.push_back(llvm::json::Value(make_diag_item(
                        uri, 0, 0, 1, 2, "parser/recovered-delimiters",
                        "Parser recovered by auto-closing delimiters.")));
                }
                else if(doc->recoveryKind == mlang::ParseRecoveryKind::LastGoodAst)
                {
                    out.push_back(llvm::json::Value(make_diag_item(
                        uri, 0, 0, 1, 2, "parser/using-last-good-ast",
                        "Using last successful AST because current text has parse errors.")));
                }
            }
        }

        for(const auto& imp : info.imports)
        {
            if(imp.moduleName.empty())
                continue;

            std::string modPath = resolve_module_path_for_file(info, imp.moduleName);
            auto loc = find_token_location(info, imp.moduleName);
            int line = loc ? loc->line : 0;
            int ch = loc ? loc->character : 0;
            int endCh = ch + (int)imp.moduleName.size();

            if(modPath.empty())
            {
                out.push_back(llvm::json::Value(make_diag_item(
                    uri, line, ch, endCh, 1, "import/module-not-found",
                    "Cannot resolve module '" + imp.moduleName + "'.")));
                continue;
            }

            if(imp.importAll || imp.itemName.empty())
                continue;

            FileInfo* modInfo = get_or_index_file(modPath);
            if(!modInfo)
                continue;
            if(find_symbol_in_file(*modInfo, imp.itemName).has_value())
                continue;

            auto itemLoc = find_token_location(info, imp.itemName);
            int il = itemLoc ? itemLoc->line : line;
            int ic = itemLoc ? itemLoc->character : ch;
            int ie = ic + (int)imp.itemName.size();

            out.push_back(llvm::json::Value(make_diag_item(
                uri, il, ic, ie, 1, "import/symbol-not-found",
                "Module '" + imp.moduleName + "' has no symbol '" + imp.itemName + "'.")));
        }

        return out;
    }

    llvm::json::Value handle_document_diagnostic(llvm::json::Object* params)
    {
        if(!params)
            return nullptr;

        auto* textDoc = params->getObject("textDocument");
        if(!textDoc)
            return nullptr;

        auto uri = textDoc->getString("uri");
        if(!uri)
            return nullptr;

        std::string uriStr = uri->str();
        auto it = files.find(uriStr);
        if(it == files.end())
        {
            std::string path = uri_to_path(uriStr);
            std::string content = read_file(path);
            if(!content.empty())
                index_document(uriStr, content);
            it = files.find(uriStr);
            if(it == files.end())
            {
                llvm::json::Object full;
                full["kind"] = "full";
                full["items"] = llvm::json::Array{};
                full["resultId"] = "0";
                return llvm::json::Value(std::move(full));
            }
        }

        llvm::json::Array items = collect_pull_diagnostics(uriStr, it->second);
        std::string resultId = compute_diagnostic_result_id(items);

        std::string previous;
        if(auto prev = params->getString("previousResultId"))
            previous = prev->str();

        if(!previous.empty() && previous == resultId)
        {
            llvm::json::Object unchanged;
            unchanged["kind"] = "unchanged";
            unchanged["resultId"] = resultId;
            diagnosticResultIds[uriStr] = resultId;
            return llvm::json::Value(std::move(unchanged));
        }

        llvm::json::Object full;
        full["kind"] = "full";
        full["resultId"] = resultId;
        full["items"] = llvm::json::Value(std::move(items));
        diagnosticResultIds[uriStr] = resultId;
        return llvm::json::Value(std::move(full));
    }

    llvm::json::Value handle_document_symbols(llvm::json::Object* params)
    {
        if(!params)
            return llvm::json::Array{};
        auto* textDoc = params->getObject("textDocument");
        if(!textDoc)
            return llvm::json::Array{};
        auto uri = textDoc->getString("uri");
        if(!uri)
            return llvm::json::Array{};
        auto it = files.find(uri->str());
        if(it == files.end())
            return llvm::json::Array{};
        auto& info = it->second;
        llvm::json::Array out;
        for(auto& [name, st] : info.structs)
        {
            out.push_back(symbol_to_json(name, 23, st.loc));
        }
        for(auto& [name, fn] : info.functions)
        {
            if(!fn.ownerStruct.empty())
                continue;
            out.push_back(symbol_to_json(name, 12, fn.loc));
        }
        return llvm::json::Value(std::move(out));
    }

    llvm::json::Value handle_formatting(llvm::json::Object* params)
    {
        if(!params)
            return llvm::json::Array{};
        auto* textDoc = params->getObject("textDocument");
        if(!textDoc)
            return llvm::json::Array{};
        auto uri = textDoc->getString("uri");
        if(!uri)
            return llvm::json::Array{};
        auto it = files.find(uri->str());
        if(it == files.end())
            return llvm::json::Array{};

        const auto& info = it->second;
        std::string formatted = format_mlang_text(info);
        if(formatted.empty() || formatted == info.text)
            return llvm::json::Array{};

        auto lines = split_lines(info.text);
        int endLine = (int)lines.size() - 1;
        int endChar = endLine >= 0 ? (int)lines[endLine].size() : 0;

        llvm::json::Object edit;
        edit["range"] = make_range_value(0, 0, endLine, endChar);
        edit["newText"] = formatted;
        llvm::json::Array out;
        out.push_back(llvm::json::Value(std::move(edit)));
        return llvm::json::Value(std::move(out));
    }

    llvm::json::Value handle_semantic_tokens(llvm::json::Object* params)
    {
        llvm::json::Object empty;
        empty["data"] = llvm::json::Array{};
        if(!params)
            return llvm::json::Value(std::move(empty));
        auto* textDoc = params->getObject("textDocument");
        if(!textDoc)
            return llvm::json::Value(std::move(empty));
        auto uri = textDoc->getString("uri");
        if(!uri)
            return llvm::json::Value(std::move(empty));

        auto it = files.find(uri->str());
        if(it == files.end())
            return llvm::json::Value(std::move(empty));

        struct SemanticTok
        {
            int line = 0;
            int start = 0;
            int length = 0;
            int type = 0;
            int modifiers = 0;
        };

        std::vector<SemanticTok> tokens;
        std::unordered_set<std::string> seen;

        auto push_tok = [&](int line, int start, int length, int type) {
            if(line < 0 || start < 0 || length <= 0)
                return;
            std::string key = std::to_string(line) + ":" + std::to_string(start) +
                              ":" + std::to_string(length) + ":" +
                              std::to_string(type);
            if(!seen.insert(key).second)
                return;
            SemanticTok tok;
            tok.line = line;
            tok.start = start;
            tok.length = length;
            tok.type = type;
            tok.modifiers = 0;
            tokens.push_back(tok);
        };

        const auto& info = it->second;
        for(int lineNo = 0; lineNo < (int)info.lines.size(); ++lineNo)
        {
            const std::string& line = info.lines[lineNo];
            size_t scanLimit = line.size();
            if(auto commentPos = line.find("//"); commentPos != std::string::npos)
                scanLimit = commentPos;

            auto push_whole_word = [&](std::string_view word, int type) {
                if(word.empty())
                    return;
                size_t pos = 0;
                while(true)
                {
                    pos = line.find(word, pos);
                    if(pos == std::string::npos)
                        break;
                    size_t end = pos + word.size();
                    if(end > scanLimit)
                        break;
                    bool leftOk =
                        pos == 0 ||
                        !(std::isalnum((unsigned char)line[pos - 1]) || line[pos - 1] == '_');
                    bool rightOk =
                        end >= line.size() ||
                        !(std::isalnum((unsigned char)line[end]) || line[end] == '_');
                    if(leftOk && rightOk)
                        push_tok(lineNo, (int)pos, (int)word.size(), type);
                    pos = end;
                }
            };

            for(const auto& kw : mlang::constants::kLspKeywords)
                push_whole_word(kw, 0);
            for(const auto& ty : mlang::constants::kRuntimeBuiltinTypes)
                push_whole_word(ty, 5);

            auto add_attr_keyword =
                [&](const char* attrText, int keywordOffset, int keywordLen) {
                    size_t attrLen = std::strlen(attrText);
                    size_t pos = 0;
                    while(true)
                    {
                        pos = line.find(attrText, pos);
                        if(pos == std::string::npos)
                            break;
                        if(pos + attrLen <= scanLimit)
                            push_tok(lineNo, (int)pos + keywordOffset,
                                     keywordLen, 0); // keyword
                        pos += attrLen;
                    }
                };

            for(const auto& attrSpec : mlang::constants::kAttributeTokenSpecs)
            {
                add_attr_keyword(attrSpec.text, attrSpec.keywordOffset,
                                 attrSpec.keywordLength);
            }

            auto add_doc_tag_token = [&](const char* tagText) {
                size_t tagLen = std::strlen(tagText);
                size_t pos = 0;
                while(true)
                {
                    pos = line.find(tagText, pos);
                    if(pos == std::string::npos)
                        break;
                    push_tok(lineNo, (int)pos, (int)tagLen, 1); // macro
                    pos += tagLen;
                }
            };

            add_doc_tag_token("@builtin_macro");
            add_doc_tag_token("@builtin_attribute");
        }

        // Symbol-based semantic tokens.
        for(const auto& [name, fn] : info.functions)
        {
            if(fn.loc.uri.empty())
                continue;
            if(name.empty())
                continue;
            std::string display = fn.name.empty() ? name : fn.name;
            push_tok(fn.loc.line, fn.loc.character, (int)display.size(), 2); // function
        }
        for(auto* fn : info.functionSpans)
        {
            for(const auto& [name, loc] : fn->paramDecls)
                push_tok(loc.line, loc.character, (int)name.size(), 3); // parameter
            for(const auto& [name, loc] : fn->varDecls)
                push_tok(loc.line, loc.character, (int)name.size(), 4); // variable
        }
        for(const auto& [name, st] : info.structs)
        {
            if(!st.loc.uri.empty())
                push_tok(st.loc.line, st.loc.character, (int)name.size(), 5); // type
            for(const auto& [fname, field] : st.fields)
            {
                if(!field.loc.uri.empty())
                    push_tok(field.loc.line, field.loc.character, (int)fname.size(), 4);
            }
            for(const auto& [mname, method] : st.methods)
            {
                if(!method.loc.uri.empty())
                    push_tok(method.loc.line, method.loc.character, (int)mname.size(), 2);
            }
        }
        for(const auto& [name, en] : info.enums)
        {
            if(!en.loc.uri.empty())
                push_tok(en.loc.line, en.loc.character, (int)name.size(), 5); // type
            for(const auto& [vname, variant] : en.variants)
            {
                if(!variant.loc.uri.empty())
                    push_tok(variant.loc.line, variant.loc.character,
                             (int)vname.size(), 6); // enumMember
            }
        }

        std::sort(tokens.begin(), tokens.end(),
                  [](const SemanticTok& a, const SemanticTok& b) {
                      if(a.line != b.line)
                          return a.line < b.line;
                      if(a.start != b.start)
                          return a.start < b.start;
                      return a.type < b.type;
                  });

        llvm::json::Array data;
        int prevLine = 0;
        int prevStart = 0;
        bool first = true;
        for(const auto& tok : tokens)
        {
            int deltaLine = first ? tok.line : (tok.line - prevLine);
            int deltaStart = first || deltaLine != 0 ? tok.start
                                                     : (tok.start - prevStart);
            data.push_back(deltaLine);
            data.push_back(deltaStart);
            data.push_back(tok.length);
            data.push_back(tok.type);
            data.push_back(tok.modifiers);
            prevLine = tok.line;
            prevStart = tok.start;
            first = false;
        }

        llvm::json::Object out;
        out["data"] = llvm::json::Value(std::move(data));
        return llvm::json::Value(std::move(out));
    }


    llvm::json::Value handle_workspace_symbols(llvm::json::Object* params)
    {
        std::string query;
        if(params)
        {
            if(auto q = params->getString("query"))
                query = q->str();
        }
        llvm::json::Array out;
        for(auto& [uri, info] : files)
        {
            for(auto& [name, st] : info.structs)
            {
                if(!query.empty() && name.find(query) == std::string::npos)
                    continue;
                out.push_back(workspace_symbol_to_json(name, 23, st.loc));
            }
            for(auto& [name, fn] : info.functions)
            {
                if(!fn.ownerStruct.empty())
                    continue;
                if(!query.empty() && name.find(query) == std::string::npos)
                    continue;
                out.push_back(workspace_symbol_to_json(name, 12, fn.loc));
            }
        }
        return llvm::json::Value(std::move(out));
    }

    llvm::json::Value symbol_to_json(const std::string& name, int kind,
                                     const Location& loc)
    {
        llvm::json::Object out;
        out["name"] = name;
        out["kind"] = kind;
        out["range"] =
            make_range_value(loc.line, loc.character, loc.character + 1);
        out["selectionRange"] =
            make_range_value(loc.line, loc.character, loc.character + 1);
        return llvm::json::Value(std::move(out));
    }

    llvm::json::Value workspace_symbol_to_json(const std::string& name,
                                               int kind, const Location& loc)
    {
        llvm::json::Object location;
        location["uri"] = loc.uri;
        location["range"] =
            make_range_value(loc.line, loc.character, loc.character + 1);
        llvm::json::Object out;
        out["name"] = name;
        out["kind"] = kind;
        out["location"] = llvm::json::Value(std::move(location));
        return llvm::json::Value(std::move(out));
    }

    std::string format_mlang_text(const FileInfo& info)
    {
        if(info.path.empty())
            return {};

        std::filesystem::path sourcePath(info.path);
        std::filesystem::path dir = sourcePath.parent_path();
        if(dir.empty())
            dir = std::filesystem::current_path();

        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::string tmpName = sourcePath.filename().string() +
                              ".mlang_format_tmp_" + std::to_string(now);
        std::filesystem::path tmpPath = dir / tmpName;

        std::ofstream tmpFile(tmpPath, std::ios::binary);
        if(!tmpFile)
            return {};
        tmpFile << info.text;
        tmpFile.close();

        auto run_formatter = [&](const std::string& cmd) -> std::optional<std::string> {
            int result = std::system(cmd.c_str());
            if(result != 0)
                return std::nullopt;
            std::string formatted = read_file(tmpPath.string());
            if(formatted.empty())
                return std::nullopt;
            return formatted;
        };

        if(const char* formatterHook = std::getenv("MLANGD_FORMATTER"))
        {
            std::string cmd = formatterHook;
            const std::string placeholder = "{file}";
            if(auto pos = cmd.find(placeholder); pos != std::string::npos)
            {
                cmd.replace(pos, placeholder.size(),
                            shell_quote(tmpPath.string()));
            }
            else
            {
                cmd += " " + shell_quote(tmpPath.string());
            }
            if(auto formatted = run_formatter(cmd))
            {
                std::filesystem::remove(tmpPath);
                return *formatted;
            }
        }

        // Preferred path: use installed mlang-format from PATH.
        std::string mlangFormatCmd = "mlang-format -i --style=file";
        mlangFormatCmd += " --assume-filename " + shell_quote(sourcePath.string());
        if(!rootPath.empty())
            mlangFormatCmd += " --root " + shell_quote(rootPath);
        mlangFormatCmd += " " + shell_quote(tmpPath.string());
        if(auto formatted = run_formatter(mlangFormatCmd))
        {
            std::filesystem::remove(tmpPath);
            return *formatted;
        }

        std::filesystem::remove(tmpPath);
        return {};
    }
};

int main(int argc, char** argv)
{
    bool stdio = false;
    for(int i = 1; i < argc; ++i)
    {
        if(std::string_view(argv[i]) == "--stdio")
            stdio = true;
    }
    if(!stdio)
    {
        std::cerr << "Only --stdio is supported.\n";
        return 2;
    }
    LspServer server;
    server.run();
    return 0;
}
