#include <stdint.h>
#include "mlang_platform.h"

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(_MSC_VER)
#define MLANG_ASCII_TLS _Thread_local
#elif defined(_MSC_VER)
#define MLANG_ASCII_TLS __declspec(thread)
#else
#define MLANG_ASCII_TLS
#endif

static MLANG_ASCII_TLS char g_mlang_ascii_braille_bufs[8][5];
static MLANG_ASCII_TLS int g_mlang_ascii_braille_idx = 0;

static char* next_braille_buf(void)
{
    g_mlang_ascii_braille_idx = (g_mlang_ascii_braille_idx + 1) & 7;
    return g_mlang_ascii_braille_bufs[g_mlang_ascii_braille_idx];
}

const char* __mlang_std_ascii_braille_mask(int32_t mask)
{
    if(mask < 0)
        mask = 0;
    if(mask > 255)
        mask = 255;

    uint32_t cp = 0x2800u + (uint32_t)mask;
    char* out = next_braille_buf();

    out[0] = (char)(0xE0u | ((cp >> 12) & 0x0Fu));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    out[3] = '\0';
    return out;
}
