#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace {

struct JsonDocHandle {
    rapidjson::Document doc;
};

struct JsonValueHandle {
    JsonDocHandle* owner = nullptr;
    const rapidjson::Value* value = nullptr;
};

thread_local std::string g_last_error;

void set_error(const std::string& msg) {
    g_last_error = msg;
}

void set_error(const char* msg) {
    g_last_error = msg ? msg : "unknown json error";
}

char* dup_cstr(const char* s) {
    if (s == nullptr) {
        return nullptr;
    }
    const size_t n = std::strlen(s);
    char* out = static_cast<char*>(std::malloc(n + 1));
    if (out == nullptr) {
        return nullptr;
    }
    if (n > 0) {
        (void)std::memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

char* dup_string(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    if (!s.empty()) {
        (void)std::memcpy(out, s.data(), s.size());
    }
    out[s.size()] = '\0';
    return out;
}

JsonDocHandle* as_doc(std::int64_t handle) {
    return reinterpret_cast<JsonDocHandle*>(static_cast<std::intptr_t>(handle));
}

JsonValueHandle* as_value(std::int64_t handle) {
    return reinterpret_cast<JsonValueHandle*>(static_cast<std::intptr_t>(handle));
}

std::int64_t make_value_handle(JsonDocHandle* owner, const rapidjson::Value* value) {
    if (owner == nullptr || value == nullptr) {
        return 0;
    }
    JsonValueHandle* wrapped = new JsonValueHandle{};
    wrapped->owner = owner;
    wrapped->value = value;
    return static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(wrapped));
}

// Kind mapping:
// 0 invalid, 1 null, 2 bool, 3 number, 4 string, 5 array, 6 object
int kind_of(const rapidjson::Value* v) {
    if (v == nullptr) {
        return 0;
    }
    if (v->IsNull()) {
        return 1;
    }
    if (v->IsBool()) {
        return 2;
    }
    if (v->IsNumber()) {
        return 3;
    }
    if (v->IsString()) {
        return 4;
    }
    if (v->IsArray()) {
        return 5;
    }
    if (v->IsObject()) {
        return 6;
    }
    return 0;
}

}  // namespace

extern "C" {

std::int64_t __mlang_std_json_parse(const char* text) {
    if (text == nullptr) {
        set_error("std::json parse: null input");
        return 0;
    }

    JsonDocHandle* h = new JsonDocHandle{};
    h->doc.Parse(text);
    if (h->doc.HasParseError()) {
        const rapidjson::ParseErrorCode code = h->doc.GetParseError();
        const size_t off = h->doc.GetErrorOffset();
        set_error(std::string("std::json parse error at offset ") + std::to_string(off) +
                  ": " + rapidjson::GetParseError_En(code));
        delete h;
        return 0;
    }

    g_last_error.clear();
    return static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(h));
}

void __mlang_std_json_doc_free(std::int64_t doc_handle) {
    JsonDocHandle* h = as_doc(doc_handle);
    delete h;
}

char* __mlang_std_json_last_error(void) {
    if (g_last_error.empty()) {
        return dup_cstr("");
    }
    return dup_string(g_last_error);
}

std::int64_t __mlang_std_json_doc_root(std::int64_t doc_handle) {
    JsonDocHandle* h = as_doc(doc_handle);
    if (h == nullptr) {
        set_error("std::json root: invalid document handle");
        return 0;
    }
    g_last_error.clear();
    return make_value_handle(h, &h->doc);
}

void __mlang_std_json_value_free(std::int64_t value_handle) {
    JsonValueHandle* v = as_value(value_handle);
    delete v;
}

int __mlang_std_json_value_kind(std::int64_t value_handle) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        return 0;
    }
    return kind_of(v->value);
}

std::int64_t __mlang_std_json_value_size(std::int64_t value_handle) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        set_error("std::json size: invalid value handle");
        return -1;
    }

    if (v->value->IsArray()) {
        g_last_error.clear();
        return static_cast<std::int64_t>(v->value->Size());
    }
    if (v->value->IsObject()) {
        g_last_error.clear();
        return static_cast<std::int64_t>(v->value->MemberCount());
    }

    set_error("std::json size: value is not array or object");
    return -1;
}

std::int64_t __mlang_std_json_object_get(std::int64_t value_handle, const char* key) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr || key == nullptr) {
        set_error("std::json get: invalid value handle or key");
        return 0;
    }
    if (!v->value->IsObject()) {
        set_error("std::json get: value is not object");
        return 0;
    }

    auto it = v->value->FindMember(key);
    if (it == v->value->MemberEnd()) {
        set_error(std::string("std::json get: key not found: ") + key);
        return 0;
    }
    g_last_error.clear();
    return make_value_handle(v->owner, &it->value);
}

std::int64_t __mlang_std_json_array_get(std::int64_t value_handle, std::int64_t index) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        set_error("std::json index: invalid value handle");
        return 0;
    }
    if (!v->value->IsArray()) {
        set_error("std::json index: value is not array");
        return 0;
    }
    if (index < 0 || static_cast<rapidjson::SizeType>(index) >= v->value->Size()) {
        set_error("std::json index: out of bounds");
        return 0;
    }
    g_last_error.clear();
    return make_value_handle(v->owner, &(*v->value)[static_cast<rapidjson::SizeType>(index)]);
}

int __mlang_std_json_value_as_bool(std::int64_t value_handle) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        set_error("std::json as_bool: invalid value handle");
        return 0;
    }
    if (!v->value->IsBool()) {
        set_error("std::json as_bool: value is not bool");
        return 0;
    }
    g_last_error.clear();
    return v->value->GetBool() ? 1 : 0;
}

std::int64_t __mlang_std_json_value_as_i64(std::int64_t value_handle) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        set_error("std::json as_i64: invalid value handle");
        return 0;
    }
    if (!v->value->IsInt64()) {
        set_error("std::json as_i64: value is not int64");
        return 0;
    }
    g_last_error.clear();
    return static_cast<std::int64_t>(v->value->GetInt64());
}

double __mlang_std_json_value_as_f64(std::int64_t value_handle) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        set_error("std::json as_f64: invalid value handle");
        return 0.0;
    }
    if (!v->value->IsNumber()) {
        set_error("std::json as_f64: value is not number");
        return 0.0;
    }
    g_last_error.clear();
    return v->value->GetDouble();
}

char* __mlang_std_json_value_as_string(std::int64_t value_handle) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        set_error("std::json as_string: invalid value handle");
        return nullptr;
    }
    if (!v->value->IsString()) {
        set_error("std::json as_string: value is not string");
        return nullptr;
    }
    g_last_error.clear();
    return dup_cstr(v->value->GetString());
}

char* __mlang_std_json_stringify(std::int64_t doc_handle, int pretty) {
    JsonDocHandle* h = as_doc(doc_handle);
    if (h == nullptr) {
        set_error("std::json stringify: invalid document handle");
        return nullptr;
    }

    rapidjson::StringBuffer buffer;
    if (pretty != 0) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        if (!h->doc.Accept(writer)) {
            set_error("std::json stringify: writer failed");
            return nullptr;
        }
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        if (!h->doc.Accept(writer)) {
            set_error("std::json stringify: writer failed");
            return nullptr;
        }
    }

    g_last_error.clear();
    return dup_cstr(buffer.GetString());
}

char* __mlang_std_json_object_key_at(std::int64_t value_handle, std::int64_t index) {
    JsonValueHandle* v = as_value(value_handle);
    if (v == nullptr || v->value == nullptr) {
        set_error("std::json key_at: invalid value handle");
        return nullptr;
    }
    if (!v->value->IsObject()) {
        set_error("std::json key_at: value is not object");
        return nullptr;
    }
    if (index < 0 || static_cast<rapidjson::SizeType>(index) >= v->value->MemberCount()) {
        set_error("std::json key_at: out of bounds");
        return nullptr;
    }

    auto it = v->value->MemberBegin() + static_cast<rapidjson::SizeType>(index);
    g_last_error.clear();
    return dup_cstr(it->name.GetString());
}

}  // extern "C"
