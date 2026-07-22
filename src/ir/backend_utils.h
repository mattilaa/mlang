#ifndef MLANG_IR_BACKEND_UTILS_H
#define MLANG_IR_BACKEND_UTILS_H

#include <llvm/IR/Module.h>
#include <string>

namespace mlang::ir_detail
{

std::string normalize_target_arch_name(const std::string& arch);
std::string llvm_arch_name_for_target(const std::string& arch);
std::string module_target_triple_string(llvm::Module* module);
void ensure_artifact_parent_directory(const std::string& filename);
std::string build_intermediate_object_path(const std::string& outputFile);

} // namespace mlang::ir_detail

#endif // MLANG_IR_BACKEND_UTILS_H
