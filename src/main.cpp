#include "ast.h"
#include "ir.h"
#include "module.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <unordered_set>

// Declare functions and globals from parser/lexer
extern int yyparse();
extern FILE* yyin;
extern "C"
{
    extern ASTNode* programRoot;
}

void printUsage(const char* programName)
{
    std::cerr << "Usage: " << programName << " [options] <input_file>\n"
              << "\nOptions:\n"
              << "  -o <file>     Output file name (default: a.out)\n"
              << "  -c            Compile to object file only (don't link)\n"
              << "  -S            Emit assembly file\n"
              << "  -emit-llvm    Emit LLVM IR file (.ll)\n"
              << "  -emit-bc      Emit LLVM bitcode file (.bc)\n"
              << "  -O0           No optimization\n"
              << "  -O1           Basic optimization\n"
              << "  -O2           Standard optimization (default)\n"
              << "  -O3           Aggressive optimization\n"
              << "  -v            Verbose output\n"
              << "  -h, --help    Show this help message\n"
              << "\nExamples:\n"
              << "  " << programName
              << " test.mla              # Compile to a.out\n"
              << "  " << programName
              << " -o myprogram test.mla # Compile to myprogram\n"
              << "  " << programName
              << " -S test.mla           # Emit assembly\n"
              << "  " << programName
              << " -emit-llvm test.mla   # Emit LLVM IR\n"
              << std::endl;
}

static std::string escape_json_string(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for(char c : s)
    {
        if(c == '\\' || c == '"')
        {
            out.push_back('\\');
            out.push_back(c);
        }
        else if(c == '\n')
        {
            out += "\\n";
        }
        else if(c == '\r')
        {
            out += "\\r";
        }
        else if(c == '\t')
        {
            out += "\\t";
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

static void write_mlang_commands_json(
    const std::vector<std::string>& files)
{
    std::ofstream out("mlang_commands.json", std::ios::binary);
    if(!out)
        return;
    out << "{ \"files\": [";
    for(size_t i = 0; i < files.size(); ++i)
    {
        if(i > 0)
            out << ", ";
        out << "\"" << escape_json_string(files[i]) << "\"";
    }
    out << "] }";
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    // Parse command line arguments
    std::string inputFile;
    std::string outputFile = "a.out";
    bool emitObjectOnly = false;
    bool emitAssembly = false;
    bool emitLLVMIR = false;
    bool emitBitcode = false;
    int optimizationLevel = 2;
    bool verbose = false;

    for(int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if(arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if(arg == "-o" && i + 1 < argc)
        {
            outputFile = argv[++i];
        }
        else if(arg == "-c")
        {
            emitObjectOnly = true;
        }
        else if(arg == "-S")
        {
            emitAssembly = true;
        }
        else if(arg == "-emit-llvm")
        {
            emitLLVMIR = true;
        }
        else if(arg == "-emit-bc")
        {
            emitBitcode = true;
        }
        else if(arg == "-O0")
        {
            optimizationLevel = 0;
        }
        else if(arg == "-O1")
        {
            optimizationLevel = 1;
        }
        else if(arg == "-O2")
        {
            optimizationLevel = 2;
        }
        else if(arg == "-O3")
        {
            optimizationLevel = 3;
        }
        else if(arg == "-v")
        {
            verbose = true;
        }
        else if(arg[0] != '-')
        {
            inputFile = arg;
        }
        else
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if(inputFile.empty())
    {
        std::cerr << "Error: No input file specified" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    FILE* input_file = fopen(inputFile.c_str(), "r");
    if(!input_file)
    {
        std::cerr << "Error opening file: " << inputFile << std::endl;
        return 1;
    }

    yyin = input_file;

    // Create LLVM context and module
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder(context);
    std::unique_ptr<llvm::Module> module =
        std::make_unique<llvm::Module>("MLang", context);

    std::vector<std::string> modulePaths;

    try
    {
        // Parse the input
        if(verbose)
        {
            std::cout << "Parsing " << inputFile << "..." << std::endl;
        }

        if(yyparse() != 0)
        {
            std::cerr << "Parsing failed." << std::endl;
            fclose(input_file);
            return 1;
        }

        if(verbose)
        {
            std::cout << "Parsing completed successfully." << std::endl;
        }

        if(!programRoot)
        {
            std::cerr << "Error: No program root node created" << std::endl;
            fclose(input_file);
            return 1;
        }

        // Process modules
        if(auto* program = dynamic_cast<ProgramNode*>(programRoot))
        {
            // Get the directory of the input file for module resolution
            std::filesystem::path inputPath(inputFile);
            std::string basePath = inputPath.parent_path().string();
            if(basePath.empty())
            {
                basePath = ".";
            }

            // Initialize module loader
            ModuleLoader moduleLoader(basePath);

            // Process mod declarations (load modules)
            if(!program->modules.empty())
            {
                if(verbose)
                {
                    std::cout << "Loading modules..." << std::endl;
                }

                std::string errorMsg;
                if(!moduleLoader.processModDeclarations(program, errorMsg))
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                    fclose(input_file);
                    delete programRoot;
                    return 1;
                }

                // Process use declarations (import symbols)
                if(!moduleLoader.processUseDeclarations(program, errorMsg))
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                    fclose(input_file);
                    delete programRoot;
                    return 1;
                }

                if(verbose)
                {
                    std::cout << "Modules loaded: ";
                    for(const auto& mod : moduleLoader.getLoadedModules())
                    {
                        std::cout << mod << " ";
                    }
                    std::cout << std::endl;
                }
            }

            modulePaths = moduleLoader.getLoadedModulePaths();
        }

        // Initialize code generator
        CodeGenerator generator(context, builder, module);

        // Generate LLVM IR
        if(auto* program = dynamic_cast<ProgramNode*>(programRoot))
        {
            if(verbose)
            {
                std::cout << "Generating LLVM IR..." << std::endl;
            }

            generator.generateCode(program);

            // Check for semantic errors
            if(generator.hadError())
            {
                std::cerr << "Compilation failed due to errors." << std::endl;
                fclose(input_file);
                delete programRoot;
                return 1;
            }

            // Print the AST if verbose
            if(verbose)
            {
                std::cout << "\n=== AST ===" << std::endl;
                std::cout << program->toString() << std::endl;
            }

            // Initialize backend
            Backend backend(module);

            if(verbose)
            {
                std::cout << "Target: " << backend.getTargetTriple()
                          << std::endl;
            }

            // Apply optimizations
            if(optimizationLevel > 0)
            {
                if(verbose)
                {
                    std::cout << "Applying optimizations (O"
                              << optimizationLevel << ")..." << std::endl;
                }
                backend.optimize(optimizationLevel);
            }

            // Determine output based on flags
            bool success = false;

            if(emitLLVMIR)
            {
                // Emit LLVM IR
                std::string llFile = outputFile;
                if(llFile == "a.out")
                {
                    llFile = inputFile.substr(0, inputFile.find_last_of('.')) +
                             ".ll";
                }
                success = backend.emitLLVMIR(llFile);

                // Also print to stdout if verbose
                if(verbose)
                {
                    std::cout << "\n=== LLVM IR ===" << std::endl;
                    module->print(llvm::outs(), nullptr);
                }
            }
            else if(emitBitcode)
            {
                // Emit LLVM bitcode
                std::string bcFile = outputFile;
                if(bcFile == "a.out")
                {
                    bcFile = inputFile.substr(0, inputFile.find_last_of('.')) +
                             ".bc";
                }
                success = backend.emitBitcode(bcFile);
            }
            else if(emitAssembly)
            {
                // Emit assembly
                std::string asmFile = outputFile;
                if(asmFile == "a.out")
                {
                    asmFile =
                        inputFile.substr(0, inputFile.find_last_of('.')) + ".s";
                }
                success = backend.emitAssemblyFile(asmFile);
            }
            else if(emitObjectOnly)
            {
                // Emit object file only
                std::string objFile = outputFile;
                if(objFile == "a.out")
                {
                    objFile =
                        inputFile.substr(0, inputFile.find_last_of('.')) + ".o";
                }
                success = backend.emitObjectFile(objFile);
            }
            else
            {
                // Compile to executable
                success = backend.compileToExecutable(outputFile);
            }

            if(!success)
            {
                std::cerr << "Compilation failed." << std::endl;
                fclose(input_file);
                delete programRoot;
                return 1;
            }
        }
        else
        {
            std::cerr << "Error: Invalid program root node type" << std::endl;
            fclose(input_file);
            return 1;
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error during compilation: " << e.what() << std::endl;
        fclose(input_file);
        return 1;
    }

    // Clean up
    fclose(input_file);
    delete programRoot;

    if(verbose)
    {
        std::cout << "Compilation completed successfully." << std::endl;
    }

    // Emit mlang_commands.json for LSP indexing.
    std::unordered_set<std::string> unique;
    std::vector<std::string> files;
    files.reserve(modulePaths.size() + 1);
    std::error_code ec;
    std::filesystem::path inputAbs =
        std::filesystem::absolute(std::filesystem::path(inputFile), ec);
    std::string inputNorm = ec ? inputFile : inputAbs.lexically_normal().string();
    unique.insert(inputNorm);
    files.push_back(inputNorm);
    for(const auto& path : modulePaths)
    {
        std::filesystem::path p = std::filesystem::path(path);
        std::filesystem::path abs = p.is_absolute()
                                        ? p
                                        : std::filesystem::absolute(p, ec);
        std::string norm = ec ? path : abs.lexically_normal().string();
        if(unique.insert(norm).second)
            files.push_back(norm);
    }
    write_mlang_commands_json(files);

    return 0;
}
