#include "ir.h"
#include "ir/backend_utils.h"
#include "llvm_compat.h"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <optional>

namespace
{

enum class OptLevelAlias
{
    O0,
    Og,
    O1,
    O2,
    O3,
    Os,
    Oz,
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

constexpr std::array<std::pair<OptLevelAlias, std::string_view>, 7>
    kOptLevelAliases{{{OptLevelAlias::O0, "-O0"},
                      {OptLevelAlias::Og, "-Og"},
                      {OptLevelAlias::O1, "-O1"},
                      {OptLevelAlias::O2, "-O2"},
                      {OptLevelAlias::O3, "-O3"},
                      {OptLevelAlias::Os, "-Os"},
                      {OptLevelAlias::Oz, "-Oz"}}};

static std::string shell_quote(const std::string& value)
{
    std::string quoted = "'";
    for(const char c : value)
    {
        if(c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
}

} // namespace

Backend::Backend(std::unique_ptr<llvm::Module>& m,
                 const std::string& archOverride,
                 bool initializeTargetMachine)
    : module(m), targetMachine(nullptr), targetArchOverride(archOverride)
{
    if(initializeTargetMachine)
        initializeTarget();
    else
        targetTriple = mlang::ir_detail::module_target_triple_string(module.get());
}

bool Backend::initializeTarget()
{
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    targetTriple = mlang::ir_detail::module_target_triple_string(module.get());
    if(targetTriple.empty())
        targetTriple = llvm::sys::getDefaultTargetTriple();
    if(!targetArchOverride.empty())
    {
        const std::string normalizedArch =
            mlang::ir_detail::normalize_target_arch_name(targetArchOverride);
        const std::string llvmArch =
            mlang::ir_detail::llvm_arch_name_for_target(normalizedArch);
        if(normalizedArch.empty() || llvmArch.empty())
        {
            std::cerr << "Error: Unsupported target arch '"
                      << targetArchOverride << "'" << std::endl;
            return false;
        }

        llvm::Triple triple(targetTriple);
        triple.setArchName(llvmArch);
        targetTriple = triple.str();
    }

#if defined(__APPLE__)
    {
        llvm::Triple triple(targetTriple);
        if(triple.isMacOSX())
        {
            triple.setOSName("macosx10.15.0");
            targetTriple = triple.str();
        }
    }
#endif

#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(llvm::Triple(targetTriple));
#else
    module->setTargetTriple(targetTriple);
#endif

    std::string error;
#if LLVM_VERSION_MAJOR >= 21
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(llvm::Triple(targetTriple), error);
#else
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(targetTriple, error);
#endif

    if(!target)
    {
        std::cerr << "Error looking up target: " << error << std::endl;
        return false;
    }

    std::string cpu = "generic";
    std::string features = "";

    llvm::TargetOptions opt;
    auto relocModel = mlang::llvm_compat::Optional<llvm::Reloc::Model>(
        llvm::Reloc::PIC_);
    using CodeGenOptLevel = mlang::llvm_compat::CodeGenOptLevel;
    CodeGenOptLevel codegenOpt = CodeGenOptLevel::Default;
    bool conservativeCodegen = false;
    if(const char* defaultOptEnv = std::getenv("MLANG_DEFAULT_OPT_LEVEL"))
    {
        if(defaultOptEnv[0] == '0' && defaultOptEnv[1] == '\0')
        {
            codegenOpt = CodeGenOptLevel::None;
            conservativeCodegen = true;
        }
    }
    if(conservativeCodegen)
    {
        opt.EnableFastISel = false;
        opt.EnableGlobalISel = false;
        opt.GlobalISelAbort = llvm::GlobalISelAbortMode::Disable;
    }

#if LLVM_VERSION_MAJOR >= 21
    llvm::Triple tripleObj(targetTriple);
    targetMachine = target->createTargetMachine(
        tripleObj, cpu, features, opt, relocModel,
        mlang::llvm_compat::NoValue, codegenOpt);
#else
    targetMachine = target->createTargetMachine(
        targetTriple, cpu, features, opt, relocModel,
        mlang::llvm_compat::NoValue, codegenOpt);
#endif

    if(!targetMachine)
    {
        std::cerr << "Error creating target machine" << std::endl;
        return false;
    }
    if(conservativeCodegen)
    {
        targetMachine->setFastISel(false);
        targetMachine->setO0WantsFastISel(false);
        targetMachine->setGlobalISel(false);
        targetMachine->setGlobalISelAbort(llvm::GlobalISelAbortMode::Disable);
    }

    module->setDataLayout(targetMachine->createDataLayout());
    return true;
}

bool Backend::emitObjectFile(const std::string& filename)
{
    if(!targetMachine)
    {
        std::cerr << "Target machine not initialized" << std::endl;
        return false;
    }

    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    if(llvm::verifyModule(*module, &verifyStream))
    {
        std::cerr << "LLVM module verification failed:\n" << verifyStream.str();
        return false;
    }

    mlang::ir_detail::ensure_artifact_parent_directory(filename);
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pass;
#if LLVM_VERSION_MAJOR >= 18
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CodeGenFileType::ObjectFile))
#else
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CGFT_ObjectFile))
#endif
    {
        std::cerr << "Target machine can't emit object file" << std::endl;
        return false;
    }

    pass.run(*module);
    dest.flush();

    std::cout << "Object file written to: " << filename << std::endl;
    return true;
}

bool Backend::emitAssemblyFile(const std::string& filename)
{
    if(!targetMachine)
    {
        std::cerr << "Target machine not initialized" << std::endl;
        return false;
    }

    mlang::ir_detail::ensure_artifact_parent_directory(filename);
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pass;
#if LLVM_VERSION_MAJOR >= 18
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CodeGenFileType::AssemblyFile))
#else
    if(targetMachine->addPassesToEmitFile(pass, dest, nullptr,
                                          llvm::CGFT_AssemblyFile))
#endif
    {
        std::cerr << "Target machine can't emit assembly file" << std::endl;
        return false;
    }

    pass.run(*module);
    dest.flush();

    std::cout << "Assembly file written to: " << filename << std::endl;
    return true;
}

bool Backend::emitLLVMIR(const std::string& filename)
{
    mlang::ir_detail::ensure_artifact_parent_directory(filename);
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    module->print(dest, nullptr);
    dest.flush();

    std::cout << "LLVM IR written to: " << filename << std::endl;
    return true;
}

bool Backend::emitBitcode(const std::string& filename)
{
    mlang::ir_detail::ensure_artifact_parent_directory(filename);
    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

    if(ec)
    {
        std::cerr << "Could not open file: " << ec.message() << std::endl;
        return false;
    }

    llvm::WriteBitcodeToFile(*module, dest);
    dest.flush();

    std::cout << "Bitcode written to: " << filename << std::endl;
    return true;
}

bool Backend::linkExecutable(const std::string& objectFile,
                             const std::string& outputFile,
                             const std::vector<std::string>& linkArgs)
{
    mlang::ir_detail::ensure_artifact_parent_directory(outputFile);
    std::string command = "c++ -o " + outputFile + " " + objectFile;
    if(const char* extraLinkFlags = std::getenv("MLANG_LINK_FLAGS"))
    {
        if(extraLinkFlags[0] != '\0')
        {
            command += " ";
            command += extraLinkFlags;
        }
    }
    for(const auto& arg : linkArgs)
    {
        command += " " + arg;
    }
    command += " 2>&1";
    std::cout << "Linking: " << command << std::endl;

    int result = system(command.c_str());
    if(result != 0)
    {
        std::cerr << "Linking failed with error code: " << result << std::endl;
        return false;
    }

    std::cout << "Executable created: " << outputFile << std::endl;
    return true;
}

bool Backend::compileToExecutable(const std::string& outputFile,
                                  const std::vector<std::string>& linkArgs)
{
    std::string objectFile =
        mlang::ir_detail::build_intermediate_object_path(outputFile);

    if(!emitObjectFile(objectFile))
    {
        return false;
    }

    return linkExecutable(objectFile, outputFile, linkArgs);
}

bool Backend::linkSharedLibrary(const std::string& objectFile,
                                const std::string& outputFile,
                                const std::vector<std::string>& linkArgs)
{
    mlang::ir_detail::ensure_artifact_parent_directory(outputFile);
    llvm::Triple triple(targetTriple);
    std::string command = "c++ ";
    if(triple.isOSDarwin())
    {
        const std::string libraryName =
            std::filesystem::path(outputFile).filename().string();
        command += "-dynamiclib -Wl,-install_name,@rpath/" + libraryName;
    }
    else if(triple.isOSWindows())
    {
        const std::filesystem::path outputPath(outputFile);
        const std::string importName =
            "lib" + outputPath.stem().string() + ".dll.a";
        const std::filesystem::path importPath =
            outputPath.parent_path() / importName;
        command += "-shared -Wl,--out-implib," + importPath.string();
    }
    else
    {
        command += "-shared";
    }
    command += " -o " + outputFile + " " + objectFile;
    if(const char* extraLinkFlags = std::getenv("MLANG_LINK_FLAGS"))
    {
        if(extraLinkFlags[0] != '\0')
        {
            command += " ";
            command += extraLinkFlags;
        }
    }
    for(const auto& arg : linkArgs)
        command += " " + arg;
    command += " 2>&1";
    std::cout << "Linking shared library: " << command << std::endl;

    const int result = system(command.c_str());
    if(result != 0)
    {
        std::cerr << "Shared-library linking failed with error code: " << result
                  << std::endl;
        return false;
    }

    std::cout << "Shared library created: " << outputFile << std::endl;
    return true;
}

bool Backend::compileToSharedLibrary(
    const std::string& outputFile, const std::vector<std::string>& linkArgs)
{
    const std::string objectFile =
        mlang::ir_detail::build_intermediate_object_path(outputFile);
    if(!emitObjectFile(objectFile))
        return false;
    return linkSharedLibrary(objectFile, outputFile, linkArgs);
}

bool Backend::compileToStaticLibrary(const std::string& outputFile)
{
    mlang::ir_detail::ensure_artifact_parent_directory(outputFile);
    const std::string objectFile =
        mlang::ir_detail::build_intermediate_object_path(outputFile);
    if(!emitObjectFile(objectFile))
        return false;

    const std::string command =
        "ar rcs " + shell_quote(outputFile) + " " + shell_quote(objectFile) +
        " 2>&1";
    std::cout << "Archiving static library: " << command << std::endl;
    const int result = system(command.c_str());
    if(result != 0)
    {
        std::cerr << "Static-library archiving failed with error code: "
                  << result << std::endl;
        return false;
    }

    std::cout << "Static library created: " << outputFile << std::endl;
    return true;
}

void Backend::optimize(const std::string& levelName)
{
    for(llvm::StructType* ty : module->getIdentifiedStructTypes())
    {
        if(!ty)
            continue;
        std::string name = ty->getName().str();
        if(name.rfind("trait.obj.", 0) == 0 ||
           name.rfind("trait.vtable.", 0) == 0)
        {
            return;
        }
    }

    std::string level = levelName;
    if(level.empty())
        level = std::string(kOptLevelAliases[3].second);

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::OptimizationLevel optLevel;
    bool runPipeline = true;
    std::string normalized = level;
    if(!normalized.empty() && normalized[0] != '-')
        normalized = "-" + normalized;
    const auto optKey = find_enum_key(normalized, kOptLevelAliases);
    if(!optKey.has_value())
    {
        normalized = std::string(kOptLevelAliases[3].second);
    }

    const auto normalizedText =
        find_enum_text(optKey.value_or(OptLevelAlias::O2), kOptLevelAliases)
            .value_or(kOptLevelAliases[3].second);
    normalized = std::string(normalizedText);

    switch(optKey.value_or(OptLevelAlias::O2))
    {
    case OptLevelAlias::Og:
        optLevel = llvm::OptimizationLevel::O1;
        break;
    case OptLevelAlias::O0:
        optLevel = llvm::OptimizationLevel::O0;
        runPipeline = false;
        break;
    case OptLevelAlias::O1:
        optLevel = llvm::OptimizationLevel::O1;
        break;
    case OptLevelAlias::O2:
        optLevel = llvm::OptimizationLevel::O2;
        break;
    case OptLevelAlias::O3:
        optLevel = llvm::OptimizationLevel::O3;
        break;
    case OptLevelAlias::Os:
        optLevel = llvm::OptimizationLevel::Os;
        break;
    case OptLevelAlias::Oz:
        optLevel = llvm::OptimizationLevel::Oz;
        break;
    }

    llvm::ModulePassManager MPM;
    if(runPipeline)
    {
        MPM = PB.buildPerModuleDefaultPipeline(optLevel);
    }

    MPM.run(*module, MAM);
    std::cout << "Optimization level " << normalized << " applied" << std::endl;
}
