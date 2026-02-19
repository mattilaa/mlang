#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef MLANG_VERSION
#define MLANG_VERSION "0.1.0"
#endif

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
int __mlang_compiler_document_close(mlang_compiler_session* session, const char* uri);
int __mlang_compiler_document_syntax_diagnostic_count(mlang_compiler_session* session,
                                                      const char* uri,
                                                      int* out_count);
int __mlang_compiler_document_syntax_diagnostic_get(mlang_compiler_session* session,
                                                    const char* uri,
                                                    int index,
                                                    int* out_line,
                                                    int* out_column,
                                                    char* out_message,
                                                    int out_message_capacity,
                                                    int* out_message_length);
int __mlang_compiler_document_hover(mlang_compiler_session* session,
                                    const char* uri,
                                    int line,
                                    int column,
                                    char* out_message,
                                    int out_message_capacity,
                                    int* out_message_length);
int __mlang_compiler_document_completion_count(mlang_compiler_session* session,
                                               const char* uri,
                                               int line,
                                               int column,
                                               int* out_count);
int __mlang_compiler_document_completion_get(mlang_compiler_session* session,
                                             const char* uri,
                                             int line,
                                             int column,
                                             int index,
                                             char* out_item,
                                             int out_item_capacity,
                                             int* out_item_length);
int __mlang_compiler_document_symbol_count(mlang_compiler_session* session,
                                           const char* uri,
                                           int* out_count);
int __mlang_compiler_document_symbol_get(mlang_compiler_session* session,
                                         const char* uri,
                                         int index,
                                         char* out_name,
                                         int out_name_capacity,
                                         int* out_name_length,
                                         int* out_kind,
                                         int* out_line,
                                         int* out_column);
int __mlang_compiler_document_definition(mlang_compiler_session* session,
                                         const char* uri,
                                         int line,
                                         int column,
                                         int* out_line,
                                         int* out_column,
                                         char* out_name,
                                         int out_name_capacity,
                                         int* out_name_length);
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
int __mlang_compiler_document_symbol_type_get(mlang_compiler_session* session,
                                              const char* uri,
                                              int index,
                                              char* out_type,
                                              int out_type_capacity,
                                              int* out_type_length);
int __mlang_compiler_document_symbol_signature_get(mlang_compiler_session* session,
                                                   const char* uri,
                                                   int index,
                                                   char* out_signature,
                                                   int out_signature_capacity,
                                                   int* out_signature_length);
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
int __mlang_compiler_semantic_cache_warm(mlang_compiler_session* session,
                                         const char* uri);
int __mlang_compiler_semantic_cache_clear(mlang_compiler_session* session);
}

namespace {

enum class CompilerStatus : int {
    Ok = 0,
    InvalidArgument = 1,
    InvalidSession = 2,
    Unsupported = 3,
    VersionConflict = 4,
    DocumentNotFound = 5,
    OutOfRange = 6,
    SymbolNotFound = 7,
};

std::mutex g_mutex;
std::unordered_map<std::int64_t, mlang_compiler_session*> g_sessions;
std::int64_t g_next_handle = 1;
thread_local int g_last_status = static_cast<int>(CompilerStatus::Ok);
thread_local std::string g_last_error;

const char* status_name(int status)
{
    switch(static_cast<CompilerStatus>(status))
    {
    case CompilerStatus::Ok:
        return "Ok";
    case CompilerStatus::InvalidArgument:
        return "InvalidArgument";
    case CompilerStatus::InvalidSession:
        return "InvalidSession";
    case CompilerStatus::Unsupported:
        return "Unsupported";
    case CompilerStatus::VersionConflict:
        return "VersionConflict";
    case CompilerStatus::DocumentNotFound:
        return "DocumentNotFound";
    case CompilerStatus::OutOfRange:
        return "OutOfRange";
    case CompilerStatus::SymbolNotFound:
        return "SymbolNotFound";
    default:
        return "UnknownStatus";
    }
}

void set_last_status(int status)
{
    g_last_status = status;
    g_last_error = std::string("std::compiler status: ") + status_name(status);
}

char* dup_string(const std::string& s)
{
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if(!out)
        return nullptr;
    if(!s.empty())
        std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

mlang_compiler_session* session_from_handle(std::int64_t handle)
{
    if(handle <= 0)
        return nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_sessions.find(handle);
    if(it == g_sessions.end())
        return nullptr;
    return it->second;
}

constexpr int kBufferCap = 4096;

struct ResolvedSymbolFields
{
    int kind = 0;
    int line = 0;
    int column = 0;
    int overload_count = 0;
    int from_current_document = 0;
    std::string name;
    std::string uri;
    std::string id;
    std::string type_info;
    std::string signature;
};

int resolve_symbol_fields(mlang_compiler_session* session,
                          const char* uri,
                          int line,
                          int column,
                          ResolvedSymbolFields& out)
{
    int name_len = 0;
    int uri_len = 0;
    int id_len = 0;
    int type_len = 0;
    int signature_len = 0;
    char name_buf[kBufferCap];
    char uri_buf[kBufferCap];
    char id_buf[kBufferCap];
    char type_buf[kBufferCap];
    char signature_buf[kBufferCap];

    const int status = __mlang_compiler_document_resolve_symbol(
        session, uri, line, column, name_buf, kBufferCap, &name_len, &out.kind,
        &out.line, &out.column, uri_buf, kBufferCap, &uri_len, id_buf, kBufferCap,
        &id_len, type_buf, kBufferCap, &type_len, signature_buf, kBufferCap,
        &signature_len, &out.overload_count, &out.from_current_document);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return status;

    out.name.assign(name_buf);
    out.uri.assign(uri_buf);
    out.id.assign(id_buf);
    out.type_info.assign(type_buf);
    out.signature.assign(signature_buf);
    return status;
}

} // namespace

extern "C" {

char* __mlang_std_compiler_last_error()
{
    return dup_string(g_last_error);
}

int __mlang_std_compiler_last_status()
{
    return g_last_status;
}

char* __mlang_std_compiler_status_name(int status)
{
    return dup_string(status_name(status));
}

char* __mlang_std_compiler_version()
{
    return dup_string(MLANG_VERSION);
}

std::int64_t __mlang_std_compiler_session_create()
{
    mlang_compiler_session* session = nullptr;
    const int status = __mlang_compiler_session_create(&session);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok) || !session)
        return 0;

    std::lock_guard<std::mutex> lock(g_mutex);
    const std::int64_t handle = g_next_handle++;
    g_sessions[handle] = session;
    return handle;
}

int __mlang_std_compiler_session_destroy(std::int64_t handle)
{
    mlang_compiler_session* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sessions.find(handle);
        if(it == g_sessions.end())
        {
            set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
            return g_last_status;
        }
        session = it->second;
        g_sessions.erase(it);
    }

    const int status = __mlang_compiler_session_destroy(session);
    set_last_status(status);
    return status;
}

int __mlang_std_compiler_document_open(std::int64_t handle,
                                       const char* uri,
                                       const char* language_id,
                                       const char* text,
                                       int version)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return g_last_status;
    }
    const int status =
        __mlang_compiler_document_open(session, uri, language_id, text, version);
    set_last_status(status);
    return status;
}

int __mlang_std_compiler_document_change(std::int64_t handle,
                                         const char* uri,
                                         const char* text,
                                         int version)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return g_last_status;
    }
    const int status = __mlang_compiler_document_change(session, uri, text, version);
    set_last_status(status);
    return status;
}

int __mlang_std_compiler_document_close(std::int64_t handle, const char* uri)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return g_last_status;
    }
    const int status = __mlang_compiler_document_close(session, uri);
    set_last_status(status);
    return status;
}

int __mlang_std_compiler_document_syntax_diagnostic_count(std::int64_t handle,
                                                          const char* uri)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int count = 0;
    const int status =
        __mlang_compiler_document_syntax_diagnostic_count(session, uri, &count);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return count;
}

int __mlang_std_compiler_document_syntax_diagnostic_line(std::int64_t handle,
                                                         const char* uri,
                                                         int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int line = 0;
    int column = 0;
    int message_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_syntax_diagnostic_get(
        session, uri, index, &line, &column, buffer, kBufferCap, &message_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return line;
}

int __mlang_std_compiler_document_syntax_diagnostic_column(std::int64_t handle,
                                                           const char* uri,
                                                           int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int line = 0;
    int column = 0;
    int message_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_syntax_diagnostic_get(
        session, uri, index, &line, &column, buffer, kBufferCap, &message_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return column;
}

char* __mlang_std_compiler_document_syntax_diagnostic_message(std::int64_t handle,
                                                              const char* uri,
                                                              int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }

    int line = 0;
    int column = 0;
    int message_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_syntax_diagnostic_get(
        session, uri, index, &line, &column, buffer, kBufferCap, &message_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

char* __mlang_std_compiler_document_hover(std::int64_t handle,
                                          const char* uri,
                                          int line,
                                          int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }

    int message_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_hover(
        session, uri, line, column, buffer, kBufferCap, &message_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

int __mlang_std_compiler_document_completion_count(std::int64_t handle,
                                                   const char* uri,
                                                   int line,
                                                   int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int count = 0;
    const int status =
        __mlang_compiler_document_completion_count(session, uri, line, column, &count);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return count;
}

char* __mlang_std_compiler_document_completion_get(std::int64_t handle,
                                                   const char* uri,
                                                   int line,
                                                   int column,
                                                   int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }

    int item_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_completion_get(
        session, uri, line, column, index, buffer, kBufferCap, &item_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

int __mlang_std_compiler_document_symbol_count(std::int64_t handle,
                                               const char* uri)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int count = 0;
    const int status = __mlang_compiler_document_symbol_count(session, uri, &count);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return count;
}

char* __mlang_std_compiler_document_symbol_name(std::int64_t handle,
                                                const char* uri,
                                                int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }

    int name_len = 0;
    int kind = 0;
    int line = 0;
    int column = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_symbol_get(
        session, uri, index, buffer, kBufferCap, &name_len, &kind, &line, &column);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

int __mlang_std_compiler_document_symbol_kind(std::int64_t handle,
                                              const char* uri,
                                              int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int name_len = 0;
    int kind = 0;
    int line = 0;
    int column = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_symbol_get(
        session, uri, index, buffer, kBufferCap, &name_len, &kind, &line, &column);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return kind;
}

int __mlang_std_compiler_document_symbol_line(std::int64_t handle,
                                              const char* uri,
                                              int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int name_len = 0;
    int kind = 0;
    int line = 0;
    int column = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_symbol_get(
        session, uri, index, buffer, kBufferCap, &name_len, &kind, &line, &column);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return line;
}

int __mlang_std_compiler_document_symbol_column(std::int64_t handle,
                                                const char* uri,
                                                int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int name_len = 0;
    int kind = 0;
    int line = 0;
    int column = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_symbol_get(
        session, uri, index, buffer, kBufferCap, &name_len, &kind, &line, &column);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return column;
}

char* __mlang_std_compiler_document_symbol_id(std::int64_t handle,
                                              const char* uri,
                                              int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    int id_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_symbol_id_get(
        session, uri, index, buffer, kBufferCap, &id_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

char* __mlang_std_compiler_document_symbol_type(std::int64_t handle,
                                                const char* uri,
                                                int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    int type_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_symbol_type_get(
        session, uri, index, buffer, kBufferCap, &type_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

char* __mlang_std_compiler_document_symbol_signature(std::int64_t handle,
                                                     const char* uri,
                                                     int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    int signature_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_symbol_signature_get(
        session, uri, index, buffer, kBufferCap, &signature_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

int __mlang_std_compiler_document_definition_line(std::int64_t handle,
                                                  const char* uri,
                                                  int line,
                                                  int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int def_line = 0;
    int def_column = 0;
    int name_len = 0;
    int uri_len = 0;
    char buffer[kBufferCap];
    char uri_buffer[kBufferCap];
    const int status = __mlang_compiler_document_definition_ex(
        session, uri, line, column, &def_line, &def_column, buffer, kBufferCap,
        &name_len, uri_buffer, kBufferCap, &uri_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return def_line;
}

int __mlang_std_compiler_document_definition_column(std::int64_t handle,
                                                    const char* uri,
                                                    int line,
                                                    int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int def_line = 0;
    int def_column = 0;
    int name_len = 0;
    int uri_len = 0;
    char buffer[kBufferCap];
    char uri_buffer[kBufferCap];
    const int status = __mlang_compiler_document_definition_ex(
        session, uri, line, column, &def_line, &def_column, buffer, kBufferCap,
        &name_len, uri_buffer, kBufferCap, &uri_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return def_column;
}

char* __mlang_std_compiler_document_definition_name(std::int64_t handle,
                                                    const char* uri,
                                                    int line,
                                                    int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }

    int def_line = 0;
    int def_column = 0;
    int name_len = 0;
    int uri_len = 0;
    char buffer[kBufferCap];
    char uri_buffer[kBufferCap];
    const int status = __mlang_compiler_document_definition_ex(
        session, uri, line, column, &def_line, &def_column, buffer, kBufferCap,
        &name_len, uri_buffer, kBufferCap, &uri_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

char* __mlang_std_compiler_document_definition_uri(std::int64_t handle,
                                                   const char* uri,
                                                   int line,
                                                   int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }

    int def_line = 0;
    int def_column = 0;
    int name_len = 0;
    int uri_len = 0;
    char buffer[kBufferCap];
    char uri_buffer[kBufferCap];
    const int status = __mlang_compiler_document_definition_ex(
        session, uri, line, column, &def_line, &def_column, buffer, kBufferCap,
        &name_len, uri_buffer, kBufferCap, &uri_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(uri_buffer));
}

char* __mlang_std_compiler_document_definition_id(std::int64_t handle,
                                                  const char* uri,
                                                  int line,
                                                  int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    int id_len = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_definition_id(
        session, uri, line, column, buffer, kBufferCap, &id_len);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

char* __mlang_std_compiler_resolve_symbol_name(std::int64_t handle,
                                               const char* uri,
                                               int line,
                                               int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(fields.name);
}

int __mlang_std_compiler_resolve_symbol_kind(std::int64_t handle,
                                             const char* uri,
                                             int line,
                                             int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return fields.kind;
}

int __mlang_std_compiler_resolve_symbol_line(std::int64_t handle,
                                             const char* uri,
                                             int line,
                                             int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return fields.line;
}

int __mlang_std_compiler_resolve_symbol_column(std::int64_t handle,
                                               const char* uri,
                                               int line,
                                               int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return fields.column;
}

char* __mlang_std_compiler_resolve_symbol_uri(std::int64_t handle,
                                              const char* uri,
                                              int line,
                                              int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(fields.uri);
}

char* __mlang_std_compiler_resolve_symbol_id(std::int64_t handle,
                                             const char* uri,
                                             int line,
                                             int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(fields.id);
}

char* __mlang_std_compiler_resolve_symbol_type(std::int64_t handle,
                                               const char* uri,
                                               int line,
                                               int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(fields.type_info);
}

char* __mlang_std_compiler_resolve_symbol_signature(std::int64_t handle,
                                                    const char* uri,
                                                    int line,
                                                    int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(fields.signature);
}

int __mlang_std_compiler_resolve_symbol_overload_count(std::int64_t handle,
                                                       const char* uri,
                                                       int line,
                                                       int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return fields.overload_count;
}

int __mlang_std_compiler_resolve_symbol_from_current_document(std::int64_t handle,
                                                              const char* uri,
                                                              int line,
                                                              int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }
    ResolvedSymbolFields fields;
    const int status = resolve_symbol_fields(session, uri, line, column, fields);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return fields.from_current_document;
}

int __mlang_std_compiler_document_reference_count(std::int64_t handle,
                                                  const char* uri,
                                                  int line,
                                                  int column)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int count = 0;
    const int status =
        __mlang_compiler_document_reference_count(session, uri, line, column, &count);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return count;
}

char* __mlang_std_compiler_document_reference_uri(std::int64_t handle,
                                                  const char* uri,
                                                  int line,
                                                  int column,
                                                  int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return nullptr;
    }

    int ref_uri_len = 0;
    int ref_line = 0;
    int ref_col = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_reference_get(
        session, uri, line, column, index, buffer, kBufferCap, &ref_uri_len,
        &ref_line, &ref_col);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return nullptr;
    return dup_string(std::string(buffer));
}

int __mlang_std_compiler_document_reference_line(std::int64_t handle,
                                                 const char* uri,
                                                 int line,
                                                 int column,
                                                 int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int ref_uri_len = 0;
    int ref_line = 0;
    int ref_col = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_reference_get(
        session, uri, line, column, index, buffer, kBufferCap, &ref_uri_len,
        &ref_line, &ref_col);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return ref_line;
}

int __mlang_std_compiler_document_reference_column(std::int64_t handle,
                                                   const char* uri,
                                                   int line,
                                                   int column,
                                                   int index)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }

    int ref_uri_len = 0;
    int ref_line = 0;
    int ref_col = 0;
    char buffer[kBufferCap];
    const int status = __mlang_compiler_document_reference_get(
        session, uri, line, column, index, buffer, kBufferCap, &ref_uri_len,
        &ref_line, &ref_col);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return ref_col;
}

int __mlang_std_compiler_document_rename_is_safe(std::int64_t handle,
                                                 const char* uri,
                                                 int line,
                                                 int column,
                                                 const char* new_name)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return -1;
    }
    int safe = 0;
    const int status = __mlang_compiler_document_rename_is_safe(
        session, uri, line, column, new_name, &safe);
    set_last_status(status);
    if(status != static_cast<int>(CompilerStatus::Ok))
        return -1;
    return safe;
}

int __mlang_std_compiler_semantic_cache_warm(std::int64_t handle,
                                             const char* uri)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return g_last_status;
    }
    const int status = __mlang_compiler_semantic_cache_warm(session, uri);
    set_last_status(status);
    return status;
}

int __mlang_std_compiler_semantic_cache_clear(std::int64_t handle)
{
    mlang_compiler_session* session = session_from_handle(handle);
    if(!session)
    {
        set_last_status(static_cast<int>(CompilerStatus::InvalidSession));
        return g_last_status;
    }
    const int status = __mlang_compiler_semantic_cache_clear(session);
    set_last_status(status);
    return status;
}

} // extern "C"
