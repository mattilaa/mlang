#pragma once

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
