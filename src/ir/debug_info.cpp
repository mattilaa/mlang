// DWARF debug info for `mlang -g`: a compile unit, a DIFile per source file
// (ASTNode::file), a DISubprogram per function and method, lexical blocks,
// statement line locations, and local variables and parameters with types
// built from their MLang types. lldb and gdb then show source lines, set
// file:line breakpoints and print variables by name.
//
// Code generation moves between functions (methods, generics instantiated on
// demand) without the builder's debug location following along, so
// debugInfoFinish() checks every function before the module is emitted:
// functions without a DISubprogram lose all debug info, and instructions of a
// function with one get a location in it.

#include "ir.h"
#include "llvm_compat.h"

#include <filesystem>
#include <llvm/Config/llvm-config.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace
{
// A node's source position: where it starts when the parser recorded that
// (startLine), else its diagnostic line.
unsigned nodeLine(const ASTNode* node)
{
    if(!node)
        return 0;
    int line = node->startLine > 0 ? node->startLine : node->line;
    return line > 0 ? static_cast<unsigned>(line) : 0;
}
unsigned nodeColumn(const ASTNode* node)
{
    if(!node)
        return 0;
    int col = node->startLine > 0 ? node->startCol : node->col;
    return col > 0 ? static_cast<unsigned>(col) : 0;
}
} // namespace

void CodeGenerator::debugInfoBegin()
{
    if(!debugInfoEnabled)
        return;
    diBuilder = std::make_unique<llvm::DIBuilder>(*module);
    diFiles.clear();
    diTypes.clear();
    diScopes.clear();
    llvm::DIFile* file = debugFile(sourceFileName.c_str());
    // MLang has no DWARF language code; C keeps lldb's expression evaluator
    // (p, expression) working on its C-like types.
    diCompileUnit = diBuilder->createCompileUnit(
        llvm::dwarf::DW_LANG_C11, file, std::string("mlang ") + MLANG_VERSION,
        debugInfoOptimized, "", 0);
    // Darwin's tools read DWARF 4 reliably; elsewhere LLVM's default is fine.
    if(llvm::Triple(module->getTargetTriple()).isOSDarwin())
        module->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
    module->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                          llvm::DEBUG_METADATA_VERSION);
}

llvm::DIFile* CodeGenerator::debugFile(const char* path)
{
    std::string name = (path && *path) ? path : sourceFileName;
    auto it = diFiles.find(name);
    if(it != diFiles.end())
        return it->second;
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(name, ec);
    if(ec)
        absolute = name;
    absolute = absolute.lexically_normal();
    llvm::DIFile* file = diBuilder->createFile(
        absolute.filename().string(), absolute.parent_path().string());
    diFiles[name] = file;
    return file;
}

CodeGenerator::DebugFunctionScope::DebugFunctionScope(
    CodeGenerator& g, llvm::Function* function, ASTNode* node,
    const std::string& displayName)
    : gen(g), savedScopes(g.diScopes),
      savedLocation(g.builder.getCurrentDebugLocation())
{
    gen.diScopes.clear();
    gen.builder.SetCurrentDebugLocation(llvm::DebugLoc());
    if(!gen.diBuilder || !function || !node)
        return;
    if(function->getSubprogram())
    {
        gen.diScopes.push_back(function->getSubprogram());
        return;
    }
    llvm::DIFile* file = gen.debugFile(node->file);
    unsigned line = nodeLine(node);
    // The function's type: return type first, then the parameters.
    llvm::SmallVector<llvm::Metadata*, 8> types;
    llvm::FunctionType* fnType = function->getFunctionType();
    types.push_back(fnType->getReturnType()->isVoidTy()
                        ? nullptr
                        : gen.debugTypeFromLLVM(fnType->getReturnType()));
    for(llvm::Type* param : fnType->params())
        types.push_back(gen.debugTypeFromLLVM(param));
    auto* subroutine =
        gen.diBuilder->createSubroutineType(gen.diBuilder->getOrCreateTypeArray(types));
    llvm::DISubprogram::DISPFlags flags = llvm::DISubprogram::SPFlagDefinition;
    if(gen.debugInfoOptimized)
        flags |= llvm::DISubprogram::SPFlagOptimized;
    if(function->hasLocalLinkage())
        flags |= llvm::DISubprogram::SPFlagLocalToUnit;
    llvm::DISubprogram* subprogram = gen.diBuilder->createFunction(
        file, displayName, function->getName(), file, line, subroutine, line,
        llvm::DINode::FlagPrototyped, flags);
    function->setSubprogram(subprogram);
    gen.diScopes.push_back(subprogram);
    gen.builder.SetCurrentDebugLocation(
        llvm::DILocation::get(gen.context, line, 0, subprogram));
}

CodeGenerator::DebugFunctionScope::~DebugFunctionScope()
{
    gen.diScopes = savedScopes;
    gen.builder.SetCurrentDebugLocation(savedLocation);
}

void CodeGenerator::debugSetLocation(ASTNode* node)
{
    if(!diBuilder || diScopes.empty() || nodeLine(node) == 0)
        return;
    builder.SetCurrentDebugLocation(llvm::DILocation::get(
        context, nodeLine(node), nodeColumn(node), diScopes.back()));
}

void CodeGenerator::debugPushBlock(ASTNode* node)
{
    if(!diBuilder || diScopes.empty())
        return;
    unsigned line = nodeLine(node);
    unsigned col = nodeColumn(node);
    llvm::DIFile* file =
        node && node->file ? debugFile(node->file) : diScopes.back()->getFile();
    diScopes.push_back(
        diBuilder->createLexicalBlock(diScopes.back(), file, line, col));
}

void CodeGenerator::debugPopBlock()
{
    if(diScopes.size() > 1)
        diScopes.pop_back();
}

void CodeGenerator::debugDeclareVariable(const std::string& name,
                                         llvm::Value* storage, TypeNode* type,
                                         ASTNode* at, unsigned argNo)
{
    if(!diBuilder || diScopes.empty() || name.empty())
        return;
    auto* slot = llvm::dyn_cast_or_null<llvm::AllocaInst>(storage);
    if(!slot)
        return;
    llvm::BasicBlock* block = builder.GetInsertBlock();
    llvm::Function* function = block ? block->getParent() : nullptr;
    // Only slots of the function being generated, described in its scope.
    if(!function || slot->getFunction() != function ||
       function->getSubprogram() != diScopes.front())
        return;
    llvm::DIScope* scope = argNo > 0 ? diScopes.front() : diScopes.back();
    llvm::DIFile* file =
        at && at->file ? debugFile(at->file) : scope->getFile();
    unsigned line = nodeLine(at);
    llvm::Type* allocated = slot->getAllocatedType();
    llvm::DIType* diType = nullptr;
    // Inferred declarations still have semantic element types recorded by
    // code generation. Opaque LLVM pointers alone cannot recover these.
    if(!type)
    {
        if(auto it = structVariableTypes.find(name); it != structVariableTypes.end())
            diType = debugStructType(it->second);
        else if(auto it = listElementTypes.find(name); it != listElementTypes.end())
        {
            GenericListTypeNode inferred(it->second);
            diType = debugType(&inferred, allocated);
        }
        else if(auto it = mapKeyValueTypes.find(name); it != mapKeyValueTypes.end())
        {
            MapTypeNode inferred(it->second.first, it->second.second);
            diType = debugType(&inferred, allocated);
        }
        else if(auto it = tupleElementTypes.find(name); it != tupleElementTypes.end())
        {
            TypeListNode elements;
            elements.types = it->second;
            TupleTypeNode inferred(&elements);
            diType = debugType(&inferred, allocated);
        }
        else if(auto it = pointerElementTypes.find(name); it != pointerElementTypes.end())
        {
            PointerTypeNode inferred(it->second);
            diType = debugType(&inferred, allocated);
        }
        else if(auto it = enumVariableTypes.find(name); it != enumVariableTypes.end())
        {
            StructTypeRefNode inferred(it->second);
            diType = debugType(&inferred, allocated);
        }
        else if(auto it = variableTypes.find(name); it != variableTypes.end())
        {
            TypeNode inferred(it->second);
            diType = debugType(&inferred, allocated);
        }
    }
    if(!diType)
        diType = debugType(type, allocated);
    if(!diType)
        return;
    llvm::DILocalVariable* variable =
        argNo > 0 ? diBuilder->createParameterVariable(scope, name, argNo, file,
                                                       line, diType, true)
                  : diBuilder->createAutoVariable(scope, name, file, line,
                                                  diType, true);
    auto* location = llvm::DILocation::get(context, line, 0, scope);
    if(llvm::Instruction* next = slot->getNextNode())
#if LLVM_VERSION_MAJOR >= 19
        diBuilder->insertDeclare(slot, variable, diBuilder->createExpression(),
                                 location, next->getIterator());
#else
        diBuilder->insertDeclare(slot, variable, diBuilder->createExpression(),
                                 location, next);
#endif
    else
        diBuilder->insertDeclare(slot, variable, diBuilder->createExpression(),
                                 location, slot->getParent());
}

namespace
{
llvm::DIType* basicType(llvm::DIBuilder& b, std::map<std::string, llvm::DIType*>& cache,
                        const std::string& name, uint64_t bits, unsigned encoding)
{
    auto it = cache.find(name);
    if(it != cache.end())
        return it->second;
    llvm::DIType* type = b.createBasicType(name, bits, encoding);
    cache[name] = type;
    return type;
}
} // namespace

llvm::DIType* CodeGenerator::debugType(TypeNode* type, llvm::Type* fallback)
{
    if(!type)
        return debugTypeFromLLVM(fallback);
    auto& b = *diBuilder;
    const auto pointerBits = module->getDataLayout().getPointerSizeInBits();
    using namespace llvm::dwarf;
    switch(type->kind)
    {
    case TypeNode::TYPE_BOOL:
    case TypeNode::TYPE_BIT:
        return basicType(b, diTypes, "bool", 8, DW_ATE_boolean);
    case TypeNode::TYPE_INT:
    case TypeNode::TYPE_I32:
        return basicType(b, diTypes, "i32", 32, DW_ATE_signed);
    case TypeNode::TYPE_I8:
        return basicType(b, diTypes, "i8", 8, DW_ATE_signed);
    case TypeNode::TYPE_I16:
        return basicType(b, diTypes, "i16", 16, DW_ATE_signed);
    case TypeNode::TYPE_I64:
        return basicType(b, diTypes, "i64", 64, DW_ATE_signed);
    case TypeNode::TYPE_U8:
        return basicType(b, diTypes, "u8", 8, DW_ATE_unsigned);
    case TypeNode::TYPE_U16:
        return basicType(b, diTypes, "u16", 16, DW_ATE_unsigned);
    case TypeNode::TYPE_U32:
        return basicType(b, diTypes, "u32", 32, DW_ATE_unsigned);
    case TypeNode::TYPE_U64:
        return basicType(b, diTypes, "u64", 64, DW_ATE_unsigned);
    case TypeNode::TYPE_FLOAT:
        return basicType(b, diTypes, "f32", 32, DW_ATE_float);
    case TypeNode::TYPE_DOUBLE:
        return basicType(b, diTypes, "f64", 64, DW_ATE_float);
    case TypeNode::TYPE_STRING:
    case TypeNode::TYPE_STR8:
    {
        // A C string: lldb and gdb print the text.
        auto it = diTypes.find("str8");
        if(it != diTypes.end())
            return it->second;
        llvm::DIType* chars = basicType(b, diTypes, "char", 8, DW_ATE_signed_char);
        llvm::DIType* str = b.createTypedef(b.createPointerType(chars, pointerBits), "str8",
                                            nullptr, 0, diCompileUnit);
        diTypes["str8"] = str;
        return str;
    }
    case TypeNode::TYPE_STR16:
        return b.createTypedef(
            b.createPointerType(basicType(b, diTypes, "char16_t", 16, DW_ATE_UTF), pointerBits),
            "str16", nullptr, 0, diCompileUnit);
    case TypeNode::TYPE_PTR:
        if(auto* ptr = dynamic_cast<PointerTypeNode*>(type))
            if(llvm::DIType* element = debugType(ptr->elementType, nullptr))
                return b.createPointerType(element, pointerBits);
        return debugTypeFromLLVM(fallback);
    case TypeNode::TYPE_REF:
    case TypeNode::TYPE_REF_MUT:
        if(auto* ref = dynamic_cast<ReferenceTypeNode*>(type))
        {
            // References use local value storage, including copy-in/copy-out
            // &mut parameters. A referenced pointer is not pointer-to-pointer.
            llvm::DIType* element = debugType(ref->elementType, fallback);
            if(element)
                return element;
        }
        return debugTypeFromLLVM(fallback);
    case TypeNode::TYPE_LIST:
        if(auto* list = dynamic_cast<GenericListTypeNode*>(type))
        {
            // { i64 len, ptr data } with data typed as the elements.
            std::string name = type->toString();
            llvm::DIType* element = debugType(list->elementType, nullptr);
            llvm::DIType* i64 = basicType(b, diTypes, "i64", 64, DW_ATE_signed);
            llvm::DIType* data = b.createPointerType(
                element ? element : basicType(b, diTypes, "u8", 8, DW_ATE_unsigned), pointerBits);
            return debugAggregateType(name, llvm::dyn_cast_or_null<llvm::StructType>(
                                          fallback ? fallback : getLLVMTypeFromNode(type)),
                                      {{"len", i64}, {"data", data}});
        }
        return debugTypeFromLLVM(fallback);
    case TypeNode::TYPE_MAP:
        if(auto* map = dynamic_cast<MapTypeNode*>(type))
            return debugAggregateType(type->toString(),
                llvm::dyn_cast_or_null<llvm::StructType>(
                    fallback ? fallback : getLLVMTypeFromNode(type)),
                {{"len", basicType(b, diTypes, "i64", 64, DW_ATE_signed)},
                 {"keys", b.createPointerType(debugType(map->keyType, nullptr), pointerBits)},
                 {"values", b.createPointerType(debugType(map->valueType, nullptr), pointerBits)}});
        return debugTypeFromLLVM(fallback);
    case TypeNode::TYPE_TUPLE:
        if(auto* tuple = dynamic_cast<TupleTypeNode*>(type))
        {
            std::vector<std::pair<std::string, llvm::DIType*>> fields;
            for(size_t i = 0; i < tuple->elementTypes->types.size(); ++i)
                fields.push_back({"_" + std::to_string(i),
                                  debugType(tuple->elementTypes->types[i], nullptr)});
            return debugAggregateType(type->toString(),
                llvm::dyn_cast_or_null<llvm::StructType>(
                    fallback ? fallback : getLLVMTypeFromNode(type)), fields);
        }
        return debugTypeFromLLVM(fallback);
    case TypeNode::TYPE_STRUCT:
    {
        std::string name;
        if(auto* ref = dynamic_cast<StructTypeRefNode*>(type))
            name = ref->structName;
        else if(auto* generic = dynamic_cast<GenericStructTypeRefNode*>(type))
            name = generic->getMangledName();
        if(!name.empty())
        {
            std::string enumName = resolveVisibleEnumName(name);
            if(!enumName.empty())
            {
                auto it = diTypes.find("enum " + enumName);
                if(it != diTypes.end())
                    return it->second;
                llvm::SmallVector<llvm::Metadata*, 8> enumerators;
                auto order = enumVariantOrder.find(enumName);
                if(order != enumVariantOrder.end())
                    for(const auto& [variant, value] : order->second)
                        enumerators.push_back(b.createEnumerator(variant, value));
                auto base = enumBaseTypes.find(enumName);
                auto kind = base != enumBaseTypes.end() ? base->second : TypeNode::TYPE_I32;
                if(kind == TypeNode::TYPE_STRING || kind == TypeNode::TYPE_STR8)
                {
                    TypeNode stringType(TypeNode::TYPE_STR8);
                    return debugType(&stringType, fallback);
                }
                llvm::Type* storageType = getLLVMType(kind);
                uint64_t bits = storageType && storageType->isIntegerTy()
                                    ? storageType->getIntegerBitWidth() : 32;
                const bool isUnsigned = isUnsignedType(kind);
                llvm::DIType* underlying =
                    basicType(b, diTypes, (isUnsigned ? "u" : "i") + std::to_string(bits),
                              bits, isUnsigned ? DW_ATE_unsigned : DW_ATE_signed);
                llvm::DIType* enumType = b.createEnumerationType(
                    diCompileUnit, enumName, nullptr, 0, bits, bits,
                    b.getOrCreateArray(enumerators), underlying);
                diTypes["enum " + enumName] = enumType;
                return enumType;
            }
            std::string structName = resolveVisibleStructName(name);
            if(llvm::DIType* st = debugStructType(structName.empty() ? name : structName))
                return st;
        }
        return debugTypeFromLLVM(fallback);
    }
    default:
        return debugTypeFromLLVM(fallback);
    }
}

llvm::DIType* CodeGenerator::debugAggregateType(
    const std::string& name, llvm::StructType* type,
    const std::vector<std::pair<std::string, llvm::DIType*>>& fields)
{
    if(!type || type->isOpaque() || fields.size() != type->getNumElements())
        return debugTypeFromLLVM(type);
    auto cached = diTypes.find("aggregate " + name);
    if(cached != diTypes.end())
        return cached->second;
    const auto& dl = module->getDataLayout();
    const auto* layout = dl.getStructLayout(type);
    llvm::SmallVector<llvm::Metadata*, 8> members;
    for(size_t i = 0; i < fields.size(); ++i)
    {
        if(!fields[i].second)
            continue;
        auto* storage = type->getElementType(i);
        members.push_back(diBuilder->createMemberType(
            diCompileUnit, fields[i].first, nullptr, 0, dl.getTypeSizeInBits(storage),
            dl.getABITypeAlign(storage).value() * 8, layout->getElementOffsetInBits(i),
            llvm::DINode::FlagZero, fields[i].second));
    }
    auto* result = diBuilder->createStructType(
        diCompileUnit, name, nullptr, 0, layout->getSizeInBits(),
        dl.getABITypeAlign(type).value() * 8, llvm::DINode::FlagZero, nullptr,
        diBuilder->getOrCreateArray(members));
    diTypes["aggregate " + name] = result;
    return result;
}

llvm::DIType* CodeGenerator::debugStructType(const std::string& name)
{
    auto cached = diTypes.find("struct " + name);
    if(cached != diTypes.end())
        return cached->second;
    auto typeIt = structTypes.find(name);
    auto* layoutType =
        typeIt != structTypes.end() ? llvm::dyn_cast<llvm::StructType>(typeIt->second) : nullptr;
    if(!layoutType || layoutType->isOpaque())
        return nullptr;
    const llvm::DataLayout& dl = module->getDataLayout();
    const llvm::StructLayout* layout = dl.getStructLayout(layoutType);
    // Created first and filled in after the members, so a struct can point
    // to itself.
    llvm::DICompositeType* composite = diBuilder->createReplaceableCompositeType(
        llvm::dwarf::DW_TAG_structure_type, name, diCompileUnit, nullptr, 0, 0,
        layout->getSizeInBits(), dl.getABITypeAlign(layoutType).value() * 8);
    diTypes["struct " + name] = composite;

    llvm::SmallVector<llvm::Metadata*, 16> members;
    auto membersIt = structMembers.find(name);
    auto layoutsIt = structFieldLayouts.find(name);
    std::set<unsigned> described;
    if(membersIt != structMembers.end())
    {
        const auto& fields = membersIt->second;
        for(size_t i = 0; i < fields.size(); ++i)
        {
            unsigned index = static_cast<unsigned>(i);
            bool packed = false;
            unsigned bitOffset = 0;
            if(layoutsIt != structFieldLayouts.end() && i < layoutsIt->second.size())
            {
                index = layoutsIt->second[i].storageIndex;
                packed = layoutsIt->second[i].packedBit;
                bitOffset = layoutsIt->second[i].bitOffset;
            }
            if(index >= layoutType->getNumElements())
                continue;
            llvm::Type* storage = layoutType->getElementType(index);
            uint64_t offset = layout->getElementOffsetInBits(index);
            llvm::DIType* fieldType = debugType(fields[i].second, storage);
            if(!fieldType)
                continue;
            if(packed)
                members.push_back(diBuilder->createBitFieldMemberType(
                    composite, fields[i].first, nullptr, 0, 1, offset + bitOffset,
                    offset, llvm::DINode::FlagZero, fieldType));
            else
                members.push_back(diBuilder->createMemberType(
                    composite, fields[i].first, nullptr, 0, dl.getTypeSizeInBits(storage),
                    dl.getABITypeAlign(storage).value() * 8, offset,
                    llvm::DINode::FlagZero, fieldType));
            described.insert(index);
        }
    }
    llvm::DICompositeType* complete = diBuilder->createStructType(
        diCompileUnit, name, nullptr, 0, layout->getSizeInBits(),
        dl.getABITypeAlign(layoutType).value() * 8, llvm::DINode::FlagZero,
        nullptr, diBuilder->getOrCreateArray(members));
    // Members (and self-references) pointed at the temporary forward type.
    composite->replaceAllUsesWith(complete);
    llvm::MDNode::deleteTemporary(composite);
    diTypes["struct " + name] = complete;
    return complete;
}

llvm::DIType* CodeGenerator::debugTypeFromLLVM(llvm::Type* type)
{
    if(!type || !diBuilder)
        return nullptr;
    auto& b = *diBuilder;
    using namespace llvm::dwarf;
    if(type->isIntegerTy(1))
        return basicType(b, diTypes, "bool", 8, DW_ATE_boolean);
    if(type->isIntegerTy())
    {
        unsigned bits = type->getIntegerBitWidth();
        return basicType(b, diTypes, "i" + std::to_string(bits), bits, DW_ATE_signed);
    }
    if(type->isFloatTy())
        return basicType(b, diTypes, "f32", 32, DW_ATE_float);
    if(type->isDoubleTy())
        return basicType(b, diTypes, "f64", 64, DW_ATE_float);
    if(type->isPointerTy())
    {
        auto it = diTypes.find("ptr");
        if(it != diTypes.end())
            return it->second;
        llvm::DIType* ptr = b.createPointerType(nullptr, module->getDataLayout().getPointerSizeInBits(),
                                               0, mlang::llvm_compat::NoValue, "ptr");
        diTypes["ptr"] = ptr;
        return ptr;
    }
    const llvm::DataLayout& dl = module->getDataLayout();
    if(auto* st = llvm::dyn_cast<llvm::StructType>(type))
    {
        if(st->isOpaque())
            return nullptr;
        if(st->hasName())
            if(llvm::DIType* named = debugStructType(st->getName().str()))
                return named;
        // An anonymous aggregate (tuples, maps, closures): numbered fields.
        const llvm::StructLayout* layout = dl.getStructLayout(st);
        llvm::SmallVector<llvm::Metadata*, 8> members;
        for(unsigned i = 0; i < st->getNumElements(); ++i)
        {
            llvm::Type* element = st->getElementType(i);
            llvm::DIType* elementType = debugTypeFromLLVM(element);
            if(!elementType)
                continue;
            members.push_back(b.createMemberType(
                diCompileUnit, "_" + std::to_string(i), nullptr, 0,
                dl.getTypeSizeInBits(element), dl.getABITypeAlign(element).value() * 8,
                layout->getElementOffsetInBits(i), llvm::DINode::FlagZero, elementType));
        }
        return b.createStructType(diCompileUnit, "", nullptr, 0, layout->getSizeInBits(),
                                  dl.getABITypeAlign(st).value() * 8,
                                  llvm::DINode::FlagZero, nullptr,
                                  b.getOrCreateArray(members));
    }
    if(auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
    {
        llvm::DIType* element = debugTypeFromLLVM(array->getElementType());
        if(!element)
            return nullptr;
        llvm::Metadata* range[] = {
            b.getOrCreateSubrange(0, static_cast<int64_t>(array->getNumElements()))};
        return b.createArrayType(dl.getTypeSizeInBits(array),
                                 dl.getABITypeAlign(array).value() * 8, element,
                                 b.getOrCreateArray(range));
    }
    return nullptr;
}

void CodeGenerator::debugInfoFinish()
{
    if(!diBuilder)
        return;
    for(llvm::Function& function : *module)
    {
        if(function.isDeclaration())
            continue;
        llvm::DISubprogram* subprogram = function.getSubprogram();
        if(!subprogram)
        {
            // Generated without debug info (wrappers, closures): nothing in it
            // may point at another function's scopes.
            llvm::stripDebugInfo(function);
            continue;
        }
        llvm::DILocation* fallback =
            llvm::DILocation::get(context, subprogram->getLine(), 0, subprogram);
        for(llvm::BasicBlock& block : function)
        {
            llvm::DILocation* previous = fallback;
            for(llvm::Instruction& inst : block)
            {
                llvm::DILocation* location = inst.getDebugLoc().get();
                bool foreign = location &&
                               location->getScope()->getSubprogram() != subprogram;
                if(!location || foreign)
                {
                    inst.setDebugLoc(previous);
                    continue;
                }
                previous = location;
            }
        }
    }
    diBuilder->finalize();
    std::string problems;
    llvm::raw_string_ostream out(problems);
    bool brokenDebugInfo = false;
    if(llvm::verifyModule(*module, &out, &brokenDebugInfo) || brokenDebugInfo)
    {
        // Keep the program correct even if its debug info is not.
        llvm::errs() << "warning: dropping invalid debug info: " << out.str() << "\n";
        llvm::StripDebugInfo(*module);
    }
    diBuilder.reset();
}
