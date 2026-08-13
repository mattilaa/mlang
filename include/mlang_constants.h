#pragma once

#include <array>

namespace mlang::constants {

struct AttributeTokenSpec
{
    const char* text;
    int keywordOffset;
    int keywordLength;
};

constexpr std::array<const char*, 10> kLspKeywords = {
    "fn", "let", "struct", "enum", "if", "else", "match", "return",
    "result", "option",
};

constexpr std::array<const char*, 0> kRuntimeBuiltinTypes = {};

constexpr std::array<const char*, 11> kRuntimeBuiltinFunctions = {
    "thread_spawn",
    "thread_join",
    "mutex_create",
    "mutex_lock",
    "mutex_unlock",
    "mutex_destroy",
    "atomic_i64_new",
    "atomic_i64_load",
    "atomic_i64_store",
    "atomic_i64_add",
    "atomic_i64_free",
};

constexpr const char* kAttrDeriveDebug = "#[derive(Debug)]";
constexpr const char* kAttrTest = "#[test]";
constexpr const char* kAttrInline = "#[inline]";
constexpr const char* kAttrInlineAlways = "#[inline(always)]";
constexpr const char* kAttrInlineNever = "#[inline(never)]";

constexpr std::array<const char*, 4> kAttributeKeywords = {
    "derive",
    "test",
    "inline",
    "inline(always)",
};

constexpr std::array<AttributeTokenSpec, 5> kAttributeTokenSpecs = {
    AttributeTokenSpec{kAttrDeriveDebug,  2, 6},  // derive
    AttributeTokenSpec{kAttrTest,         2, 4},   // test
    AttributeTokenSpec{kAttrInline,       2, 6},   // inline
    AttributeTokenSpec{kAttrInlineAlways, 2, 13},  // inline(always)
    AttributeTokenSpec{kAttrInlineNever,  2, 12},  // inline(never)
};

} // namespace mlang::constants
