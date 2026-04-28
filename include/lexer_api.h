#pragma once

#include <cstddef>

struct yy_buffer_state;
using YY_BUFFER_STATE = yy_buffer_state*;

YY_BUFFER_STATE mlang_yy_scan_bytes(const char* bytes, std::size_t len);
void yy_delete_buffer(YY_BUFFER_STATE buffer);
