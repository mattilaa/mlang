#include "ir/backend_utils.h"
#include "llvm_compat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <functional>
#include <llvm/Config/llvm-config.h>
#include <optional>

namespace mlang::ir_detail
{
namespace
{

enum class TargetArchAlias
{
    X86,
    I386,
    I686,
    X64,
    X86_64,
    Amd64,
    Aarch64,
    Arm64,
};

enum class CanonicalTargetArch
{
    X86,
    X64,
    Aarch64,
};

template <typename Enum, size_t N>
std::optional<Enum>
find_enum_key(std::string_view key,
              const std::array<std::pair<Enum, std::string_view>, N>& mappings)
{
    const auto it =
        std::find_if(mappings.begin(), mappings.end(),
                     [&](const auto& entry) { return entry.second == key; });
    if(it == mappings.end())
        return std::nullopt;
    return it->first;
}

template <typename Enum, size_t N>
std::optional<std::string_view>
find_enum_text(Enum key,
               const std::array<std::pair<Enum, std::string_view>, N>& mappings)
{
    const auto it =
        std::find_if(mappings.begin(), mappings.end(),
                     [&](const auto& entry) { return entry.first == key; });
    if(it == mappings.end())
        return std::nullopt;
    return it->second;
}

constexpr std::array<std::pair<TargetArchAlias, std::string_view>, 8>
    kTargetArchAliases{{{TargetArchAlias::X86, "x86"},
                        {TargetArchAlias::I386, "i386"},
                        {TargetArchAlias::I686, "i686"},
                        {TargetArchAlias::X64, "x64"},
                        {TargetArchAlias::X86_64, "x86_64"},
                        {TargetArchAlias::Amd64, "amd64"},
                        {TargetArchAlias::Aarch64, "aarch64"},
                        {TargetArchAlias::Arm64, "arm64"}}};

constexpr std::array<std::pair<CanonicalTargetArch, std::string_view>, 3>
    kCanonicalTargetArchNames{{{CanonicalTargetArch::X86, "x86"},
                               {CanonicalTargetArch::X64, "x64"},
                               {CanonicalTargetArch::Aarch64, "aarch64"}}};

constexpr std::array<std::pair<CanonicalTargetArch, std::string_view>, 3>
    kLlvmTargetArchNames{{{CanonicalTargetArch::X86, "i386"},
                          {CanonicalTargetArch::X64, "x86_64"},
                          {CanonicalTargetArch::Aarch64, "aarch64"}}};

} // namespace

std::string normalize_target_arch_name(const std::string& arch)
{
    const auto archKey = find_enum_key(arch, kTargetArchAliases);
    if(!archKey.has_value())
        return "";

    CanonicalTargetArch canonicalKey;
    switch(*archKey)
    {
    case TargetArchAlias::X86:
    case TargetArchAlias::I386:
    case TargetArchAlias::I686:
        canonicalKey = CanonicalTargetArch::X86;
        break;
    case TargetArchAlias::X64:
    case TargetArchAlias::X86_64:
    case TargetArchAlias::Amd64:
        canonicalKey = CanonicalTargetArch::X64;
        break;
    case TargetArchAlias::Aarch64:
    case TargetArchAlias::Arm64:
        canonicalKey = CanonicalTargetArch::Aarch64;
        break;
    }

    if(const auto text =
           find_enum_text(canonicalKey, kCanonicalTargetArchNames);
       text.has_value())
        return std::string(*text);
    return "";
}

std::string llvm_arch_name_for_target(const std::string& arch)
{
    const auto archKey = find_enum_key(arch, kCanonicalTargetArchNames);
    if(!archKey.has_value())
        return "";
    if(const auto text = find_enum_text(*archKey, kLlvmTargetArchNames);
       text.has_value())
        return std::string(*text);
    return "";
}

std::string module_target_triple_string(llvm::Module* module)
{
#if LLVM_VERSION_MAJOR >= 21
    return module ? module->getTargetTriple().str() : std::string();
#else
    return module ? module->getTargetTriple() : std::string();
#endif
}

void ensure_artifact_parent_directory(const std::string& filename)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path path(filename);
    if(path.has_parent_path())
        fs::create_directories(path.parent_path(), ec);
}

std::string build_intermediate_object_path(const std::string& outputFile)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path buildDir = fs::current_path(ec) / "build" / ".mlang";
    if(ec)
        buildDir = fs::path("build") / ".mlang";
    fs::create_directories(buildDir, ec);

    fs::path outPath(outputFile);
    std::string base = outPath.filename().string();
    if(base.empty())
        base = "a.out";

    std::hash<std::string> hasher;
    fs::path absOut = fs::absolute(outPath, ec);
    const std::string key =
        ec ? outputFile : absOut.lexically_normal().string();
    const auto hashValue = static_cast<unsigned long long>(hasher(key));
    return (buildDir / (base + "-" + std::to_string(hashValue) + ".o"))
        .string();
}

} // namespace mlang::ir_detail
