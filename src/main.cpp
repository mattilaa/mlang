#include "ast.h"
#include "ir.h"
#include <iostream>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

// Declare functions and globals from parser/lexer
extern int yyparse();
extern FILE* yyin;
extern "C"
{
    extern ASTNode* programRoot;
}

int main(int argc, char** argv)
{
    if(argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
        return 1;
    }

    FILE* input_file = fopen(argv[1], "r");
    if(!input_file)
    {
        std::cerr << "Error opening file: " << argv[1] << std::endl;
        return 1;
    }

    yyin = input_file;

    // Create LLVM context and module
    llvm::LLVMContext context;
    llvm::IRBuilder<> builder(context);
    std::unique_ptr<llvm::Module> module =
        std::make_unique<llvm::Module>("MyLanguage", context);

    try
    {
        // Parse the input
        if(yyparse() != 0)
        {
            std::cerr << "Parsing failed." << std::endl;
            return 1;
        }

        std::cout << "Parsing completed successfully." << std::endl;

        if(!programRoot)
        {
            std::cerr << "Error: No program root node created" << std::endl;
            return 1;
        }

        // Initialize code generator
        CodeGenerator generator(context, builder, module);

        // Generate LLVM IR
        if(auto* program = dynamic_cast<ProgramNode*>(programRoot))
        {
            generator.generateCode(program);

            // Print the generated LLVM IR to console
            module->print(llvm::outs(), nullptr);

            // Write LLVM IR to a file
            std::error_code EC;
            llvm::raw_fd_ostream irFile("output.ll", EC,
                                        llvm::sys::fs::OF_None);
            if(EC)
            {
                std::cerr << "Could not open file: " << EC.message()
                          << std::endl;
                return 1;
            }
            module->print(irFile, nullptr);
        }
        else
        {
            std::cerr << "Error: Invalid program root node type" << std::endl;
            return 1;
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error during compilation: " << e.what() << std::endl;
        return 1;
    }

    // Clean up
    fclose(input_file);
    delete programRoot;

    std::cout << "Compilation completed successfully." << std::endl;
    return 0;
}
