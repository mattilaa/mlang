#include "ast.h"
#include "module.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

extern int yyparse();
extern FILE* yyin;
extern int yylineno;
extern void yyrestart(FILE* input_file);
extern "C" {
    extern ASTNode* programRoot;
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
    if(out.rfind(prefix, 0) == 0)
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
    bool cHeadersLoaded = false;
    bool cHeaderDebug = false;
    std::string cHeaderDebugLog;

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
            if(line.rfind("-I", 0) == 0)
            {
                std::string dir = trim(line.substr(2));
                if(!dir.empty())
                    cIncludeDirs.push_back(dir);
                continue;
            }
            const std::string includePrefix = "include_dir:";
            const std::string headerPrefix = "header:";
            const std::string typeMapPrefix = "type_map:";
            if(line.rfind(includePrefix, 0) == 0)
            {
                std::string dir = trim(line.substr(includePrefix.size()));
                if(!dir.empty())
                    cIncludeDirs.push_back(dir);
                continue;
            }
            if(line.rfind(headerPrefix, 0) == 0)
            {
                std::string hdr = trim(line.substr(headerPrefix.size()));
                if(!hdr.empty())
                    cHeaderNames.push_back(hdr);
                continue;
            }
            if(line.rfind(typeMapPrefix, 0) == 0)
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

    void log_c_headers() const
    {
        std::string msg = "[mlang-lsp] c headers: ";
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
        debug_log("[mlang-lsp] c symbol not found: " + name);
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
        debug_log("[mlang-lsp] c typedef not found: " + name);
        return std::nullopt;
    }

    std::optional<Location> find_c_type_location(const std::string& typeName)
    {
        if(!cHeadersLoaded)
            load_c_header_config();
        auto it = cTypeMap.find(typeName);
        if(it == cTypeMap.end())
        {
            debug_log("[mlang-lsp] c type not mapped: " + typeName);
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
            if(line.rfind("Content-Length:", 0) == 0)
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

        for(const auto& item : *arr)
        {
            if(auto s = item.getAsString())
            {
                std::string filePath = s->str();
                if(!filePath.empty())
                {
                    std::string abs = filePath;
                    if(!std::filesystem::path(filePath).is_absolute() &&
                       !rootPath.empty())
                    {
                        abs =
                            (std::filesystem::path(rootPath) / filePath).string();
                    }
                    if(std::filesystem::path(abs).extension() != ".mla")
                        continue;
                    std::string content = read_file(abs);
                    if(!content.empty())
                        index_document(path_to_uri(abs), content);
                }
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
            if(std::filesystem::path(abs).extension() != ".mla")
                continue;
            std::string content = read_file(abs);
            if(!content.empty())
                index_document(path_to_uri(abs), content);
        }
    }

    std::string resolve_module_path(const std::string& baseDir,
                                    const std::string& moduleName) const
    {
        std::filesystem::path base(baseDir);
        std::filesystem::path direct = base / (moduleName + ".mla");
        std::error_code ec;
        if(std::filesystem::exists(direct, ec) && !ec)
            return direct.string();
        std::filesystem::path dirMod = base / moduleName / "mod.mla";
        if(std::filesystem::exists(dirMod, ec) && !ec)
            return dirMod.string();
        return {};
    }

    std::string resolve_module_path_for_file(const FileInfo& info,
                                             const std::string& moduleName) const
    {
        std::filesystem::path base =
            std::filesystem::path(info.path).parent_path();
        std::string path = resolve_module_path(base.string(), moduleName);
        if(!path.empty())
            return path;
        if(!rootPath.empty())
            return resolve_module_path(rootPath, moduleName);
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
        for(auto& [sname, st] : info.structs)
        {
            auto itf = st.fields.find(name);
            if(itf != st.fields.end())
                return itf->second.loc;
            auto itm = st.methods.find(name);
            if(itm != st.methods.end())
                return itm->second.loc;
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
    };

    static constexpr const char* kMlangKeywords[] = {
        "fn",
        "let",
        "struct",
        "if",
        "else",
        "return",
    };

    void add_completion(std::vector<CompletionCandidate>& out,
                        std::unordered_set<std::string>& seen,
                        const std::string& label, int kind,
                        const std::string& prefix)
    {
        if(!starts_with(label, prefix))
            return;
        if(seen.insert(label).second)
            out.push_back({label, kind});
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
            caps["referencesProvider"] = true;
            llvm::json::Array triggers;
            triggers.push_back(".");
            triggers.push_back(":");
            llvm::json::Object completion;
            completion["triggerCharacters"] = llvm::json::Value(std::move(triggers));
            caps["completionProvider"] = llvm::json::Value(std::move(completion));
            caps["documentSymbolProvider"] = true;
            caps["workspaceSymbolProvider"] = true;
            caps["documentFormattingProvider"] = true;
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
        else if(method == "textDocument/references")
        {
            send_response(id, handle_references(params));
        }
        else if(method == "textDocument/completion")
        {
            send_response(id, handle_completion(params));
        }
        else if(method == "textDocument/documentSymbol")
        {
            send_response(id, handle_document_symbols(params));
        }
        else if(method == "textDocument/formatting")
        {
            send_response(id, handle_formatting(params));
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
            if(uri && text)
                index_document(uri->str(), text->str());
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
            auto* last = changes->back().getAsObject();
            if(!last)
                return;
            auto text = last->getString("text");
            if(text)
                index_document(uri->str(), text->str());
        }
        else if(method == "textDocument/didSave")
        {
            auto* textDoc = params ? params->getObject("textDocument") : nullptr;
            if(!textDoc)
                return;
            auto uri = textDoc->getString("uri");
            if(!uri)
                return;
            std::string path = uri_to_path(uri->str());
            std::string text = read_file(path);
            if(!text.empty())
                index_document(uri->str(), text);
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
        scan_workspace(rootPath);
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
            if(it->path().extension() != ".mla")
                continue;
            std::string path = it->path().string();
            std::string text = read_file(path);
            if(text.empty())
                continue;
            index_document(path_to_uri(path), text);
        }
    }

    ProgramNode* parse_file(const std::string& path)
    {
        FILE* f = fopen(path.c_str(), "r");
        if(!f)
            return nullptr;

        ASTNode* savedRoot = programRoot;
        programRoot = nullptr;
        yylineno = 1;
        yyrestart(f);
        yyin = f;
        int result = yyparse();
        fclose(f);
        ProgramNode* parsed = nullptr;
        if(result == 0 && programRoot)
            parsed = dynamic_cast<ProgramNode*>(programRoot);
        programRoot = savedRoot;
        return parsed;
    }

    void index_document(const std::string& uri, const std::string& text)
    {
        std::string path = uri_to_path(uri);
        FileInfo info;
        info.path = path;
        info.uri = uri;
        info.text = text;
        info.lines = split_lines(text);
        info.ast = parse_file(path);
        collect_symbols(info);
        files[uri] = std::move(info);
    }

    void collect_symbols(FileInfo& info)
    {
        functionReturns.clear();
        if(!info.ast)
            return;
        collect_modules_imports(info, info.ast);
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
        int left = idx;
        while(left > 0 && (std::isalnum((unsigned char)lineText[left - 1]) ||
                           lineText[left - 1] == '_'))
            --left;
        int right = idx;
        while(right < (int)lineText.size() &&
              (std::isalnum((unsigned char)lineText[right]) ||
               lineText[right] == '_'))
            ++right;
        std::string word = lineText.substr(left, right - left);
        if(word.empty())
            return nullptr;

        if(auto typeLoc = find_c_type_location(word))
            return location_to_json(*typeLoc);

        auto find_module_prefix = [&](int wordStart) -> std::string {
            int j = wordStart - 1;
            while(j >= 0 && std::isspace((unsigned char)lineText[j]))
                --j;
            if(j < 1 || lineText[j] != ':' || lineText[j - 1] != ':')
                return {};
            j -= 2;
            while(j >= 0 && std::isspace((unsigned char)lineText[j]))
                --j;
            int end = j;
            while(j >= 0 && (std::isalnum((unsigned char)lineText[j]) ||
                             lineText[j] == '_'))
                --j;
            if(end < j + 1)
                return {};
            return lineText.substr(j + 1, end - j);
        };

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

        std::string modulePrefix = find_module_prefix(left);
        if(!modulePrefix.empty())
        {
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
        for(auto& [sname, st] : info.structs)
        {
            auto itf = st.fields.find(word);
            if(itf != st.fields.end())
                return location_to_json(itf->second.loc);
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

        return nullptr;
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

        int start = idx;
        while(start > 0 &&
              (std::isalnum((unsigned char)lineText[start - 1]) ||
               lineText[start - 1] == '_'))
            --start;
        std::string prefix = lineText.substr(start, idx - start);

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

                for(const auto* kw : kMlangKeywords)
                    add_completion(candidates, seen, kw, 14, prefix);

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
            obj["insertText"] = item.label;
            if(item.kind > 0)
                obj["kind"] = item.kind;
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
        int left = idx;
        while(left > 0 && (std::isalnum((unsigned char)lineText[left - 1]) ||
                           lineText[left - 1] == '_'))
            --left;
        int right = idx;
        while(right < (int)lineText.size() &&
              (std::isalnum((unsigned char)lineText[right]) ||
               lineText[right] == '_'))
            ++right;
        std::string word = lineText.substr(left, right - left);
        if(word.empty())
            return llvm::json::Array{};

        llvm::json::Array out;
        std::regex rx("\\b" + word + "\\b");
        for(auto& [furi, finfo] : files)
        {
            for(int i = 0; i < (int)finfo.lines.size(); ++i)
            {
                auto& ln = finfo.lines[i];
                auto begin = std::sregex_iterator(ln.begin(), ln.end(), rx);
                auto end = std::sregex_iterator();
                for(auto it2 = begin; it2 != end; ++it2)
                {
                    int col = (int)it2->position();
                    llvm::json::Object loc;
                    loc["uri"] = finfo.uri;
                    loc["range"] =
                        make_range_value(i, col, col + (int)word.size());
                    out.push_back(llvm::json::Value(std::move(loc)));
                }
            }
        }
        return llvm::json::Value(std::move(out));
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

        std::filesystem::path script =
            std::filesystem::path(rootPath) / "tools" / "mlang_format" /
            "mlang_format.py";
        if(!std::filesystem::exists(script))
        {
            std::filesystem::remove(tmpPath);
            return {};
        }

        std::string cmd = "python3 \"" + script.string() +
                          "\" --in-place \"" + tmpPath.string() + "\"";
        int result = std::system(cmd.c_str());
        if(result != 0)
        {
            std::filesystem::remove(tmpPath);
            return {};
        }

        std::string formatted = read_file(tmpPath.string());
        std::filesystem::remove(tmpPath);
        return formatted;
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
