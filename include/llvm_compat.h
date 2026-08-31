#pragma once

#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CodeGen.h>
#include <optional>

#if LLVM_VERSION_MAJOR < 16
#include <llvm/ADT/Optional.h>
#endif

// LLVM moved these headers from Support/ADT to TargetParser in newer releases.
// Keep the compiler buildable with the LLVM 15 toolchain used by release CI.
#if __has_include(<llvm/TargetParser/Host.h>)
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif

#if __has_include(<llvm/TargetParser/Triple.h>)
#include <llvm/TargetParser/Triple.h>
#else
#include <llvm/ADT/Triple.h>
#endif

namespace mlang::llvm_compat
{
#if LLVM_VERSION_MAJOR >= 18
using CodeGenOptLevel = llvm::CodeGenOptLevel;
#else
using CodeGenOptLevel = llvm::CodeGenOpt::Level;
#endif

#if LLVM_VERSION_MAJOR >= 16
template<typename T> using Optional = std::optional<T>;
inline constexpr auto NoValue = std::nullopt;
#else
template<typename T> using Optional = llvm::Optional<T>;
inline constexpr auto NoValue = llvm::None;
#endif
}
