#pragma once

#include <stdint.h>

#if defined(__has_include)
#  if __has_include(<uchar.h>)
#    include <uchar.h>
typedef char16_t mlang_char16;
#  else
typedef uint16_t mlang_char16;
#  endif
#else
typedef uint16_t mlang_char16;
#endif

typedef float mlang_float;
typedef double mlang_double;
typedef const char* mlang_string;
typedef const char* mlang_str8;
typedef const mlang_char16* mlang_str16;
