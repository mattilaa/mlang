#include <stddef.h>
#include <stdlib.h>
#include <string.h>

char* __mlang_error_add_context(const char* err, const char* context)
{
    const char* e = err ? err : "";
    const char* c = context ? context : "";
    const size_t e_len = strlen(e);
    const size_t c_len = strlen(c);
    const char* sep = ": ";
    const size_t sep_len = 2;

    char* out = (char*)malloc(c_len + sep_len + e_len + 1);
    if(!out)
        return NULL;

    if(c_len > 0)
        memcpy(out, c, c_len);
    memcpy(out + c_len, sep, sep_len);
    if(e_len > 0)
        memcpy(out + c_len + sep_len, e, e_len);
    out[c_len + sep_len + e_len] = '\0';
    return out;
}
