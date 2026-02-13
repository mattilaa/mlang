#pragma once

#include <array>

namespace mlang::constants {

struct AttributeTokenSpec
{
    const char* text;
    int keywordOffset;
    int keywordLength;
};

constexpr std::array<const char*, 12> kLspKeywords = {
    "fn",    "let",   "struct", "enum",  "if",     "else",
    "match", "return","Handle", "Thread","Mutex",  "Atomic64",
};

constexpr std::array<const char*, 4> kRuntimeBuiltinTypes = {
    "Handle",
    "Thread",
    "Mutex",
    "Atomic64",
};

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

constexpr std::array<const char*, 2> kAttributeKeywords = {
    "derive",
    "test",
};

constexpr std::array<AttributeTokenSpec, 2> kAttributeTokenSpecs = {
    AttributeTokenSpec{kAttrDeriveDebug, 2, 6}, // derive
    AttributeTokenSpec{kAttrTest, 2, 4},        // test
};

} // namespace mlang::constants
