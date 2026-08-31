#include "ir.h"
#include "ir/ast_analysis.h"
#include "ir/backend_utils.h"
#include "ir/common.h"
#include "ir/expression_type_kind.h"
#include "llvm_compat.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/InlineAsm.h>

using mlang::ir_detail::ast_analysis::contains_unsupported_try_control_flow;
using mlang::ir_detail::common::Helpers;
using mlang::ir_detail::module_target_triple_string;
using mlang::ir_detail::normalize_target_arch_name;

llvm::Value* CodeGenerator::generateInlineAsm(InlineAsmNode* node)
{
    if(!node || !node->resultType)
    {
        reportError(node ? node->line : 0, "invalid inline asm expression");
        return nullptr;
    }

    if(!node->requiredArch.empty())
    {
        const std::string requiredArch =
            normalize_target_arch_name(node->requiredArch);
        if(requiredArch.empty())
        {
            reportError(node->line, "unsupported inline asm target arch '" +
                                        node->requiredArch +
                                        "'; expected x86, x64, or aarch64");
            return nullptr;
        }

        std::string effectiveTriple = module_target_triple_string(module.get());
        if(effectiveTriple.empty())
            effectiveTriple = llvm::sys::getDefaultTargetTriple();
        llvm::Triple triple(effectiveTriple);
        const std::string actualArch =
            normalize_target_arch_name(triple.getArchName().str());
        if(actualArch.empty())
        {
            reportError(node->line,
                        "unsupported compilation target arch '" +
                            triple.getArchName().str() +
                            "' for inline asm; expected x86, x64, or aarch64");
            return nullptr;
        }
        if(actualArch != requiredArch)
        {
            reportError(node->line,
                        "inline asm target arch '" + requiredArch +
                            "' does not match compilation target arch '" +
                            actualArch + "'");
            return nullptr;
        }
    }

    llvm::Type* resultType = getLLVMTypeFromNode(node->resultType);
    if(!resultType)
    {
        reportError(node->line, "inline asm result type could not be resolved");
        return nullptr;
    }

    auto supportsRegisterConstraint = [](llvm::Type* ty)
    { return ty && (ty->isIntegerTy() || ty->isPointerTy()); };

    std::vector<llvm::Value*> argValues;
    std::vector<llvm::Type*> argTypes;
    argValues.reserve(node->arguments.size());
    argTypes.reserve(node->arguments.size());
    for(auto* arg : node->arguments)
    {
        llvm::Value* value = generateExpression(arg);
        if(!value)
            return nullptr;
        if(!supportsRegisterConstraint(value->getType()))
        {
            reportError(node->line, "inline asm currently supports only "
                                    "integer and pointer operands");
            return nullptr;
        }
        argValues.push_back(value);
        argTypes.push_back(value->getType());
    }

    const bool returnsVoid = resultType->isVoidTy();
    if(!returnsVoid && !supportsRegisterConstraint(resultType))
    {
        reportError(node->line, "inline asm currently supports only integer, "
                                "pointer, or void result types");
        return nullptr;
    }
    if(!returnsVoid)
    {
        if(argValues.empty())
        {
            reportError(node->line,
                        "non-void inline asm requires at least one operand");
            return nullptr;
        }
        if(argTypes.front() != resultType)
        {
            reportError(node->line, "non-void inline asm requires its first "
                                    "operand to match the result type");
            return nullptr;
        }
    }

    std::string constraints;
    if(!returnsVoid)
    {
        constraints = "=r";
        for(size_t i = 0; i < argValues.size(); ++i)
        {
            constraints += ",";
            constraints += (i == 0) ? "0" : "r";
        }
    }
    else
    {
        for(size_t i = 0; i < argValues.size(); ++i)
        {
            if(i > 0)
                constraints += ",";
            constraints += "r";
        }
    }

    llvm::FunctionType* asmType =
        llvm::FunctionType::get(resultType, argTypes, false);
    llvm::InlineAsm* asmFn = llvm::InlineAsm::get(
        asmType, node->asmTemplate, constraints, node->isVolatile);

    if(returnsVoid)
        return builder.CreateCall(asmFn, argValues);
    return builder.CreateCall(asmFn, argValues, "asmtmp");
}

llvm::Value* CodeGenerator::generateBinaryOp(BinaryOpNode* node)
{
    llvm::Value* L = generateExpression(node->left);
    llvm::Value* R = generateExpression(node->right);

    if(!L || !R)
        return nullptr;

    auto generateTraitBinaryOp = [&](const std::string& traitName,
                                     const std::string& methodName,
                                     const std::string& opText) -> llvm::Value*
    {
        auto* lhsStructTy = llvm::dyn_cast<llvm::StructType>(L->getType());
        auto* rhsStructTy = llvm::dyn_cast<llvm::StructType>(R->getType());

        if(!lhsStructTy && !rhsStructTy)
            return nullptr;

        if(!lhsStructTy || !rhsStructTy || !lhsStructTy->hasName() ||
           !rhsStructTy->hasName() ||
           lhsStructTy->getName() != rhsStructTy->getName())
        {
            reportError(node->line,
                        "operator '" + opText +
                            "' on structs requires both operands to have the "
                            "same struct type");
            return nullptr;
        }

        const std::string structTypeName = lhsStructTy->getName().str();
        auto traitIt = structImplementedTraits.find(structTypeName);
        if(traitIt == structImplementedTraits.end() ||
           traitIt->second.find(traitName) == traitIt->second.end())
        {
            reportError(node->line, "struct '" + structTypeName +
                                        "' must implement trait '" + traitName +
                                        "' to use operator '" + opText + "'");
            return nullptr;
        }

        auto structIt = structMethods.find(structTypeName);
        if(structIt == structMethods.end())
        {
            reportError(node->line,
                        "struct '" + structTypeName + "' has no methods");
            return nullptr;
        }
        auto methodIt = structIt->second.find(methodName);
        if(methodIt == structIt->second.end() || !methodIt->second.second)
        {
            reportError(node->line, "trait '" + traitName + "' on struct '" +
                                        structTypeName + "' requires method '" +
                                        methodName + "'");
            return nullptr;
        }
        if(methodIt->second.second->isStatic)
        {
            reportError(node->line,
                        "trait method '" + methodName + "' must not be static");
            return nullptr;
        }

        std::string definingStruct = structTypeName;
        std::string searchStruct = structTypeName;
        while(!searchStruct.empty())
        {
            std::string candidate = searchStruct + "_" + methodName;
            if(module->getFunction(candidate))
            {
                definingStruct = searchStruct;
                break;
            }
            auto baseIt = structBases.find(searchStruct);
            if(baseIt != structBases.end())
                searchStruct = baseIt->second;
            else
                break;
        }

        const std::string mangledName = definingStruct + "_" + methodName;
        llvm::Function* callee = module->getFunction(mangledName);
        if(!callee)
        {
            reportError(node->line, "unknown trait method: " + methodName);
            return nullptr;
        }
        if(callee->empty() && monomorphizedTypes.count(definingStruct))
        {
            auto defStructIt = structMethods.find(definingStruct);
            if(defStructIt != structMethods.end())
            {
                auto defMethodIt = defStructIt->second.find(methodName);
                if(defMethodIt != defStructIt->second.end())
                {
                    StructMethodNode* methodDef = defMethodIt->second.second;
                    if(methodDef && methodDef->body)
                    {
                        llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
                        auto savedNamedValues = namedValues;
                        auto savedConstantVariables = constantVariables;
                        auto savedVariableTypes = variableTypes;
                        auto savedStructVariableTypes = structVariableTypes;
                        auto savedTraitObjectVariableTypes =
                            traitObjectVariableTypes;
                        auto savedEnumVariableTypes = enumVariableTypes;
                        auto savedListElementTypes = listElementTypes;
                        auto savedMapKeyValueTypes = mapKeyValueTypes;
                        auto savedTupleElementTypes = tupleElementTypes;
                        auto savedPointerElementTypes = pointerElementTypes;
                        auto savedPointerKnownNull = pointerKnownNull;
                        auto savedMovedVariables = movedVariables;
                        auto savedPointerBorrowTarget = pointerBorrowTarget;
                        auto savedActiveBorrowers = activeBorrowers;
                        auto savedActiveMutBorrower = activeMutBorrower;
                        auto savedVariableScopeDepth = variableScopeDepth;
                        auto savedCleanupScopes = cleanupScopes;
                        auto savedPointerBorrowScopes = pointerBorrowScopes;
                        auto savedVariableScopeDepthScopes =
                            variableScopeDepthScopes;

                        generateMethodDefinition(definingStruct, methodDef);

                        namedValues = savedNamedValues;
                        constantVariables = savedConstantVariables;
                        variableTypes = savedVariableTypes;
                        structVariableTypes = savedStructVariableTypes;
                        traitObjectVariableTypes =
                            savedTraitObjectVariableTypes;
                        enumVariableTypes = savedEnumVariableTypes;
                        listElementTypes = savedListElementTypes;
                        mapKeyValueTypes = savedMapKeyValueTypes;
                        tupleElementTypes = savedTupleElementTypes;
                        pointerElementTypes = savedPointerElementTypes;
                        pointerKnownNull = savedPointerKnownNull;
                        movedVariables = savedMovedVariables;
                        pointerBorrowTarget = savedPointerBorrowTarget;
                        activeBorrowers = savedActiveBorrowers;
                        activeMutBorrower = savedActiveMutBorrower;
                        variableScopeDepth = savedVariableScopeDepth;
                        cleanupScopes = savedCleanupScopes;
                        pointerBorrowScopes = savedPointerBorrowScopes;
                        variableScopeDepthScopes =
                            savedVariableScopeDepthScopes;
                        if(savedBlock)
                            builder.SetInsertPoint(savedBlock);
                    }
                }
            }
        }
        if(callee->arg_size() < 2)
        {
            reportError(node->line, "trait method '" + methodName +
                                        "' for operator '" + opText +
                                        "' requires one argument");
            return nullptr;
        }

        llvm::Value* lhsPtr = getLValuePointer(node->left, node->line);
        if(!lhsPtr)
        {
            lhsPtr =
                builder.CreateAlloca(L->getType(), nullptr, "traitop.lhs.tmp");
            builder.CreateStore(L, lhsPtr);
        }

        llvm::Value* rhsPtr = getLValuePointer(node->right, node->line);
        if(!rhsPtr)
        {
            rhsPtr =
                builder.CreateAlloca(R->getType(), nullptr, "traitop.rhs.tmp");
            builder.CreateStore(R, rhsPtr);
        }

        std::vector<llvm::Value*> args;
        args.push_back(lhsPtr);

        llvm::Value* arg1 = R;
        llvm::Type* expectedTy = callee->getArg(1)->getType();
        if(expectedTy->isPointerTy())
        {
            arg1 = rhsPtr;
            if(arg1->getType() != expectedTy && arg1->getType()->isPointerTy())
                arg1 = builder.CreateBitCast(arg1, expectedTy,
                                             "traitop.arg.ptrcast");
        }
        else if(arg1->getType() != expectedTy)
        {
            int convCost = 0;
            if(!canConvertType(arg1->getType(), expectedTy, convCost))
            {
                reportError(node->line,
                            "argument type mismatch for trait method '" +
                                methodName + "'");
                return nullptr;
            }
            if(arg1->getType()->isIntegerTy() && expectedTy->isIntegerTy())
                arg1 = builder.CreateIntCast(arg1, expectedTy, true,
                                             "traitop.arg.cast");
            else if(arg1->getType()->isIntegerTy() &&
                    expectedTy->isFloatingPointTy())
                arg1 = builder.CreateSIToFP(arg1, expectedTy,
                                            "traitop.arg.sitofp");
            else if(arg1->getType()->isFloatingPointTy() &&
                    expectedTy->isIntegerTy())
                arg1 = builder.CreateFPToSI(arg1, expectedTy,
                                            "traitop.arg.fptosi");
            else if(arg1->getType()->isFloatingPointTy() &&
                    expectedTy->isFloatingPointTy())
                arg1 = builder.CreateFPCast(arg1, expectedTy,
                                            "traitop.arg.fpcast");
            else if(arg1->getType()->isPointerTy() && expectedTy->isPointerTy())
                arg1 = builder.CreateBitCast(arg1, expectedTy,
                                             "traitop.arg.ptrcast");
        }
        args.push_back(arg1);

        llvm::Value* res = builder.CreateCall(callee, args, "traitop.call");
        if(res->getType() != L->getType())
        {
            reportError(node->line, "trait method '" + methodName +
                                        "' used by operator '" + opText +
                                        "' must return the receiver type");
            return nullptr;
        }
        return res;
    };

    if(node->op == BinaryOpNode::OP_SPACESHIP)
    {
        auto buildNumericSpaceship = [&](llvm::Value* lhs,
                                         llvm::Value* rhs) -> llvm::Value*
        {
            bool isFloat = lhs->getType()->isFloatingPointTy() ||
                           rhs->getType()->isFloatingPointTy();
            TypeNode::TypeKind lhsCmpKind =
                getExpressionTypeKind(node->left, variableTypes);
            TypeNode::TypeKind rhsCmpKind =
                getExpressionTypeKind(node->right, variableTypes);
            bool useUnsignedIntCmp =
                isUnsignedType(lhsCmpKind) || isUnsignedType(rhsCmpKind);

            bool lhsIsNumeric = lhs->getType()->isIntegerTy() ||
                                lhs->getType()->isFloatingPointTy();
            bool rhsIsNumeric = rhs->getType()->isIntegerTy() ||
                                rhs->getType()->isFloatingPointTy();
            if(!lhsIsNumeric || !rhsIsNumeric)
                return nullptr;

            if(!isFloat && lhs->getType()->isIntegerTy() &&
               rhs->getType()->isIntegerTy())
            {
                unsigned lhsBits = lhs->getType()->getIntegerBitWidth();
                unsigned rhsBits = rhs->getType()->getIntegerBitWidth();
                if(lhsBits != rhsBits)
                {
                    if(lhsBits > rhsBits)
                        rhs = builder.CreateIntCast(
                            rhs, lhs->getType(), !useUnsignedIntCmp,
                            useUnsignedIntCmp ? "spaceship.zext"
                                              : "spaceship.sext");
                    else
                        lhs = builder.CreateIntCast(
                            lhs, rhs->getType(), !useUnsignedIntCmp,
                            useUnsignedIntCmp ? "spaceship.zext"
                                              : "spaceship.sext");
                }
            }

            llvm::Value* isLt = nullptr;
            llvm::Value* isGt = nullptr;
            if(isFloat)
            {
                isLt = builder.CreateFCmpOLT(lhs, rhs, "spaceship.lt");
                isGt = builder.CreateFCmpOGT(lhs, rhs, "spaceship.gt");
            }
            else
            {
                isLt = useUnsignedIntCmp
                           ? builder.CreateICmpULT(lhs, rhs, "spaceship.lt")
                           : builder.CreateICmpSLT(lhs, rhs, "spaceship.lt");
                isGt = useUnsignedIntCmp
                           ? builder.CreateICmpUGT(lhs, rhs, "spaceship.gt")
                           : builder.CreateICmpSGT(lhs, rhs, "spaceship.gt");
            }

            llvm::Type* i32Ty = llvm::Type::getInt32Ty(context);
            llvm::Value* negOne = llvm::ConstantInt::getSigned(i32Ty, -1);
            llvm::Value* posOne = llvm::ConstantInt::getSigned(i32Ty, 1);
            llvm::Value* zero = llvm::ConstantInt::get(i32Ty, 0);
            llvm::Value* gtOrEq =
                builder.CreateSelect(isGt, posOne, zero, "spaceship.gtoreq");
            return builder.CreateSelect(isLt, negOne, gtOrEq, "spaceship.res");
        };

        auto* lhsStructTy = llvm::dyn_cast<llvm::StructType>(L->getType());
        auto* rhsStructTy = llvm::dyn_cast<llvm::StructType>(R->getType());

        if(lhsStructTy || rhsStructTy)
        {
            if(!lhsStructTy || !rhsStructTy || !lhsStructTy->hasName() ||
               !rhsStructTy->hasName() ||
               lhsStructTy->getName() != rhsStructTy->getName())
            {
                reportError(node->line,
                            "<=> on structs requires both operands to have the "
                            "same struct type");
                return nullptr;
            }

            const std::string structTypeName = lhsStructTy->getName().str();
            const std::string traitName = "Compare";
            const std::string methodName = "compare";

            auto traitIt = structImplementedTraits.find(structTypeName);
            if(traitIt == structImplementedTraits.end() ||
               traitIt->second.find(traitName) == traitIt->second.end())
            {
                reportError(node->line, "struct '" + structTypeName +
                                            "' must implement trait '" +
                                            traitName + "' to use <=>");
                return nullptr;
            }

            auto structIt = structMethods.find(structTypeName);
            if(structIt == structMethods.end())
            {
                reportError(node->line,
                            "struct '" + structTypeName + "' has no methods");
                return nullptr;
            }
            auto methodIt = structIt->second.find(methodName);
            if(methodIt == structIt->second.end() || !methodIt->second.second)
            {
                reportError(node->line, "trait '" + traitName +
                                            "' on struct '" + structTypeName +
                                            "' requires method '" + methodName +
                                            "'");
                return nullptr;
            }
            if(methodIt->second.second->isStatic)
            {
                reportError(node->line, "trait method '" + methodName +
                                            "' must not be static");
                return nullptr;
            }

            std::string definingStruct = structTypeName;
            std::string searchStruct = structTypeName;
            while(!searchStruct.empty())
            {
                std::string candidate = searchStruct + "_" + methodName;
                if(module->getFunction(candidate))
                {
                    definingStruct = searchStruct;
                    break;
                }
                auto baseIt = structBases.find(searchStruct);
                if(baseIt != structBases.end())
                    searchStruct = baseIt->second;
                else
                    break;
            }

            const std::string mangledName = definingStruct + "_" + methodName;
            llvm::Function* callee = module->getFunction(mangledName);
            if(!callee)
            {
                reportError(node->line, "unknown trait method: " + methodName);
                return nullptr;
            }
            if(callee->arg_size() < 2)
            {
                reportError(node->line, "trait method '" + methodName +
                                            "' for <=> requires one argument");
                return nullptr;
            }

            llvm::Value* lhsPtr = getLValuePointer(node->left, node->line);
            if(!lhsPtr)
            {
                lhsPtr = builder.CreateAlloca(L->getType(), nullptr,
                                              "spaceship.lhs.tmp");
                builder.CreateStore(L, lhsPtr);
            }

            llvm::Value* rhsPtr = getLValuePointer(node->right, node->line);
            if(!rhsPtr)
            {
                rhsPtr = builder.CreateAlloca(R->getType(), nullptr,
                                              "spaceship.rhs.tmp");
                builder.CreateStore(R, rhsPtr);
            }

            std::vector<llvm::Value*> args;
            args.push_back(lhsPtr);

            llvm::Value* arg1 = R;
            llvm::Type* expectedTy = callee->getArg(1)->getType();
            if(expectedTy->isPointerTy())
            {
                arg1 = rhsPtr;
                if(arg1->getType() != expectedTy &&
                   arg1->getType()->isPointerTy())
                    arg1 = builder.CreateBitCast(arg1, expectedTy,
                                                 "spaceship.arg.ptrcast");
            }
            else if(arg1->getType() != expectedTy)
            {
                int convCost = 0;
                if(!canConvertType(arg1->getType(), expectedTy, convCost))
                {
                    reportError(node->line,
                                "argument type mismatch for trait method '" +
                                    methodName + "'");
                    return nullptr;
                }
                if(arg1->getType()->isIntegerTy() && expectedTy->isIntegerTy())
                {
                    arg1 = builder.CreateIntCast(arg1, expectedTy, true,
                                                 "spaceship.arg.cast");
                }
                else if(arg1->getType()->isIntegerTy() &&
                        expectedTy->isFloatingPointTy())
                {
                    arg1 = builder.CreateSIToFP(arg1, expectedTy,
                                                "spaceship.arg.sitofp");
                }
                else if(arg1->getType()->isFloatingPointTy() &&
                        expectedTy->isIntegerTy())
                {
                    arg1 = builder.CreateFPToSI(arg1, expectedTy,
                                                "spaceship.arg.fptosi");
                }
                else if(arg1->getType()->isFloatingPointTy() &&
                        expectedTy->isFloatingPointTy())
                {
                    arg1 = builder.CreateFPCast(arg1, expectedTy,
                                                "spaceship.arg.fpcast");
                }
                else if(arg1->getType()->isPointerTy() &&
                        expectedTy->isPointerTy())
                {
                    arg1 = builder.CreateBitCast(arg1, expectedTy,
                                                 "spaceship.arg.ptrcast");
                }
            }
            args.push_back(arg1);

            llvm::Value* cmpVal =
                builder.CreateCall(callee, args, "spaceship.call");
            if(!cmpVal->getType()->isIntegerTy())
            {
                reportError(node->line,
                            "trait method '" + methodName +
                                "' used by <=> must return an integer");
                return nullptr;
            }
            llvm::Type* i32Ty = llvm::Type::getInt32Ty(context);
            if(cmpVal->getType() != i32Ty)
            {
                cmpVal = builder.CreateIntCast(cmpVal, i32Ty, true,
                                               "spaceship.ret.cast");
            }
            return cmpVal;
        }

        llvm::Value* numericCmp = buildNumericSpaceship(L, R);
        if(!numericCmp)
        {
            reportError(node->line,
                        "<=> requires numeric operands or structs implementing "
                        "trait 'Compare'");
            return nullptr;
        }
        return numericCmp;
    }

    if(node->op == BinaryOpNode::OP_PLUS ||
       node->op == BinaryOpNode::OP_MINUS ||
       node->op == BinaryOpNode::OP_MULTIPLY ||
       node->op == BinaryOpNode::OP_DIVIDE ||
       node->op == BinaryOpNode::OP_MODULO)
    {
        const char* opText = nullptr;
        const char* traitName = nullptr;
        const char* methodName = nullptr;
        switch(node->op)
        {
        case BinaryOpNode::OP_PLUS:
            opText = "+";
            traitName = "Add";
            methodName = "add";
            break;
        case BinaryOpNode::OP_MINUS:
            opText = "-";
            traitName = "Sub";
            methodName = "sub";
            break;
        case BinaryOpNode::OP_MULTIPLY:
            opText = "*";
            traitName = "Mul";
            methodName = "mul";
            break;
        case BinaryOpNode::OP_DIVIDE:
            opText = "/";
            traitName = "Div";
            methodName = "div";
            break;
        case BinaryOpNode::OP_MODULO:
            opText = "%";
            traitName = "Rem";
            methodName = "rem";
            break;
        default:
            break;
        }
        if(opText && traitName && methodName)
        {
            llvm::Value* traitResult =
                generateTraitBinaryOp(traitName, methodName, opText);
            if(traitResult)
                return traitResult;
            if(L->getType()->isStructTy() || R->getType()->isStructTy())
                return nullptr;
        }
    }

    // Check for struct types - cannot perform arithmetic on structs
    if(L->getType()->isStructTy())
    {
        std::string typeName = L->getType()->getStructName().str().empty()
                                   ? "struct"
                                   : L->getType()->getStructName().str();
        reportError(node->line,
                    "cannot perform binary operation on struct type '" +
                        typeName + "'");
        return nullptr;
    }
    if(R->getType()->isStructTy())
    {
        std::string typeName = R->getType()->getStructName().str().empty()
                                   ? "struct"
                                   : R->getType()->getStructName().str();
        reportError(node->line,
                    "cannot perform binary operation on struct type '" +
                        typeName + "'");
        return nullptr;
    }

    auto toBoolValue = [&](llvm::Value* v, const char* name) -> llvm::Value*
    {
        if(v->getType()->isIntegerTy(1))
            return v;
        if(v->getType()->isIntegerTy())
            return builder.CreateICmpNE(
                v, llvm::ConstantInt::get(v->getType(), 0), name);
        if(v->getType()->isFloatingPointTy())
            return builder.CreateFCmpONE(
                v, llvm::ConstantFP::get(v->getType(), 0.0), name);
        reportError(node->line,
                    "logical operations require numeric or boolean operands");
        return nullptr;
    };

    if(node->op == BinaryOpNode::OP_AND || node->op == BinaryOpNode::OP_OR)
    {
        llvm::Value* Lb = toBoolValue(L, "lbool");
        llvm::Value* Rb = toBoolValue(R, "rbool");
        if(!Lb || !Rb)
            return nullptr;
        return (node->op == BinaryOpNode::OP_AND)
                   ? builder.CreateAnd(Lb, Rb, "andtmp")
                   : builder.CreateOr(Lb, Rb, "ortmp");
    }

    auto resolveExprKind = [&](ExpressionNode* expr) -> TypeNode::TypeKind
    {
        std::string enumName = getEnumTypeName(expr, node->line);
        if(!enumName.empty())
        {
            std::string resolvedEnumName = resolveVisibleEnumName(enumName);
            auto baseIt = enumBaseTypes.find(resolvedEnumName);
            if(baseIt != enumBaseTypes.end())
                return baseIt->second;
        }
        if(TypeNode* exprType = getLValueType(expr, node->line))
            return exprType->kind;
        return getExpressionTypeKind(expr, variableTypes);
    };

    TypeNode::TypeKind lhsKind = resolveExprKind(node->left);
    TypeNode::TypeKind rhsKind = resolveExprKind(node->right);
    bool useUnsignedIntOps = isUnsignedType(lhsKind) || isUnsignedType(rhsKind);

    if(node->op == BinaryOpNode::OP_BITAND ||
       node->op == BinaryOpNode::OP_BITOR ||
       node->op == BinaryOpNode::OP_BITXOR ||
       node->op == BinaryOpNode::OP_SHL || node->op == BinaryOpNode::OP_SHR)
    {
        if(!L->getType()->isIntegerTy() || !R->getType()->isIntegerTy())
        {
            reportError(node->line,
                        "bitwise operations require integer operands");
            return nullptr;
        }
        unsigned LBits = L->getType()->getIntegerBitWidth();
        unsigned RBits = R->getType()->getIntegerBitWidth();
        if(node->op == BinaryOpNode::OP_SHL || node->op == BinaryOpNode::OP_SHR)
        {
            if(RBits != LBits)
                R = builder.CreateIntCast(R, L->getType(), false,
                                          "bitshift.cast");
        }
        else if(LBits != RBits)
        {
            if(LBits > RBits)
                R = builder.CreateIntCast(R, L->getType(), !useUnsignedIntOps,
                                          useUnsignedIntOps ? "bitand.zext"
                                                            : "bitand.sext");
            else
                L = builder.CreateIntCast(L, R->getType(), !useUnsignedIntOps,
                                          useUnsignedIntOps ? "bitand.zext"
                                                            : "bitand.sext");
        }
        switch(node->op)
        {
        case BinaryOpNode::OP_BITAND:
            return builder.CreateAnd(L, R, "bitandtmp");
        case BinaryOpNode::OP_BITOR:
            return builder.CreateOr(L, R, "bitortmp");
        case BinaryOpNode::OP_BITXOR:
            return builder.CreateXor(L, R, "bitxortmp");
        case BinaryOpNode::OP_SHL:
            return builder.CreateShl(L, R, "shltmp");
        case BinaryOpNode::OP_SHR:
            return useUnsignedIntOps ? builder.CreateLShr(L, R, "shrtmp")
                                     : builder.CreateAShr(L, R, "shrtmp");
        default:
            return nullptr;
        }
    }

    auto typeKindName = [](TypeNode::TypeKind kind) -> std::string
    {
        switch(kind)
        {
        case TypeNode::TYPE_STRING:
            return "str8";
        case TypeNode::TYPE_STR8:
            return "str8";
        case TypeNode::TYPE_STR16:
            return "str16";
        case TypeNode::TYPE_BOOL:
            return "bool";
        case TypeNode::TYPE_INT:
            return "i32";
        case TypeNode::TYPE_FLOAT:
            return "f32";
        case TypeNode::TYPE_DOUBLE:
            return "f64";
        case TypeNode::TYPE_I8:
            return "i8";
        case TypeNode::TYPE_I16:
            return "i16";
        case TypeNode::TYPE_I32:
            return "i32";
        case TypeNode::TYPE_I64:
            return "i64";
        case TypeNode::TYPE_U8:
            return "u8";
        case TypeNode::TYPE_U16:
            return "u16";
        case TypeNode::TYPE_U32:
            return "u32";
        case TypeNode::TYPE_U64:
            return "u64";
        default:
            return "unknown";
        }
    };

    bool lhsIsString = lhsKind == TypeNode::TYPE_STRING ||
                       lhsKind == TypeNode::TYPE_STR8 ||
                       lhsKind == TypeNode::TYPE_STR16;
    bool rhsIsString = rhsKind == TypeNode::TYPE_STRING ||
                       rhsKind == TypeNode::TYPE_STR8 ||
                       rhsKind == TypeNode::TYPE_STR16;
    bool lhsIsStr8Family =
        lhsKind == TypeNode::TYPE_STRING || lhsKind == TypeNode::TYPE_STR8;
    bool rhsIsStr8Family =
        rhsKind == TypeNode::TYPE_STRING || rhsKind == TypeNode::TYPE_STR8;
    bool sameStringRuntimeKind =
        lhsKind == rhsKind || (lhsIsStr8Family && rhsIsStr8Family);

    if(node->op == BinaryOpNode::OP_PLUS && (lhsIsString || rhsIsString))
    {
        if(!lhsIsString || !rhsIsString)
        {
            reportError(node->line,
                        "string concatenation requires both operands to be "
                        "string types");
            return nullptr;
        }
        if(!sameStringRuntimeKind)
        {
            reportError(node->line,
                        "string concatenation requires matching operand types "
                        "(got '" +
                            typeKindName(lhsKind) + "' and '" +
                            typeKindName(rhsKind) + "')");
            return nullptr;
        }
        initializeStdlibFunctions();
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::Value* lhsPtr = L;
        llvm::Value* rhsPtr = R;
        if(lhsPtr->getType() != ptrType && lhsPtr->getType()->isPointerTy())
            lhsPtr = builder.CreateBitCast(lhsPtr, ptrType, "strcat.lhs.cast");
        if(rhsPtr->getType() != ptrType && rhsPtr->getType()->isPointerTy())
            rhsPtr = builder.CreateBitCast(rhsPtr, ptrType, "strcat.rhs.cast");
        if(!lhsPtr->getType()->isPointerTy() ||
           !rhsPtr->getType()->isPointerTy())
        {
            reportError(node->line,
                        "invalid string operands for concatenation");
            return nullptr;
        }

        llvm::FunctionType* concatFnType =
            llvm::FunctionType::get(ptrType, {ptrType, ptrType}, false);
        const char* concatName = lhsKind == TypeNode::TYPE_STR16
                                     ? "__mlang_std_strbuf_concat16"
                                     : "__mlang_std_strbuf_concat";
        llvm::FunctionCallee concatFn =
            module->getOrInsertFunction(concatName, concatFnType);
        return builder.CreateCall(concatFn, {lhsPtr, rhsPtr}, "strcat");
    }
    if(lhsIsString || rhsIsString)
    {
        if(!lhsIsString || !rhsIsString)
        {
            reportError(node->line,
                        "string operations require both operands to be string "
                        "types");
            return nullptr;
        }
        if(!sameStringRuntimeKind)
        {
            reportError(
                node->line,
                "string operations require matching operand types (got '" +
                    typeKindName(lhsKind) + "' and '" + typeKindName(rhsKind) +
                    "')");
            return nullptr;
        }
        bool isStringCompareOp = node->op == BinaryOpNode::OP_EQ ||
                                 node->op == BinaryOpNode::OP_NE ||
                                 node->op == BinaryOpNode::OP_LT ||
                                 node->op == BinaryOpNode::OP_LE ||
                                 node->op == BinaryOpNode::OP_GT ||
                                 node->op == BinaryOpNode::OP_GE;
        if(!isStringCompareOp)
        {
            reportError(node->line,
                        "only '+', '==', '!=', '<', '<=', '>', '>=' are "
                        "supported for string operands");
            return nullptr;
        }

        initializeStdlibFunctions();
#if LLVM_VERSION_MAJOR >= 15
        llvm::Type* ptrType = llvm::PointerType::get(context, 0);
#else
        llvm::Type* ptrType =
            llvm::PointerType::get(llvm::Type::getInt8Ty(context), 0);
#endif
        llvm::Value* lhsPtr = L;
        llvm::Value* rhsPtr = R;
        if(lhsPtr->getType() != ptrType && lhsPtr->getType()->isPointerTy())
            lhsPtr = builder.CreateBitCast(lhsPtr, ptrType, "strcmp.lhs.cast");
        if(rhsPtr->getType() != ptrType && rhsPtr->getType()->isPointerTy())
            rhsPtr = builder.CreateBitCast(rhsPtr, ptrType, "strcmp.rhs.cast");
        if(!lhsPtr->getType()->isPointerTy() ||
           !rhsPtr->getType()->isPointerTy())
        {
            reportError(node->line, "invalid string operands for comparison");
            return nullptr;
        }

        if(node->op == BinaryOpNode::OP_EQ || node->op == BinaryOpNode::OP_NE)
        {
            llvm::FunctionType* eqFnType = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(context), {ptrType, ptrType}, false);
            const char* eqName = lhsKind == TypeNode::TYPE_STR16
                                     ? "__mlang_std_strbuf_eq16"
                                     : "__mlang_std_strbuf_eq";
            llvm::FunctionCallee eqFn =
                module->getOrInsertFunction(eqName, eqFnType);
            llvm::Value* eqVal =
                builder.CreateCall(eqFn, {lhsPtr, rhsPtr}, "streq");
            llvm::Value* eqBool = builder.CreateICmpNE(
                eqVal,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
                "streq.bool");
            if(node->op == BinaryOpNode::OP_EQ)
                return eqBool;
            return builder.CreateNot(eqBool, "strne.bool");
        }

        llvm::FunctionType* cmpFnType = llvm::FunctionType::get(
            llvm::Type::getInt64Ty(context), {ptrType, ptrType}, false);
        const char* cmpName = lhsKind == TypeNode::TYPE_STR16
                                  ? "__mlang_std_strbuf_compare16"
                                  : "__mlang_std_strbuf_compare";
        llvm::FunctionCallee cmpFn =
            module->getOrInsertFunction(cmpName, cmpFnType);
        llvm::Value* cmpVal =
            builder.CreateCall(cmpFn, {lhsPtr, rhsPtr}, "strcmp");
        llvm::Value* zero =
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 0);
        switch(node->op)
        {
        case BinaryOpNode::OP_LT:
            return builder.CreateICmpSLT(cmpVal, zero, "strlt");
        case BinaryOpNode::OP_LE:
            return builder.CreateICmpSLE(cmpVal, zero, "strle");
        case BinaryOpNode::OP_GT:
            return builder.CreateICmpSGT(cmpVal, zero, "strgt");
        case BinaryOpNode::OP_GE:
            return builder.CreateICmpSGE(cmpVal, zero, "strge");
        default:
            reportError(node->line, "unsupported string comparison");
            return nullptr;
        }
    }

    if(L->getType()->isIntegerTy(1) && R->getType()->isIntegerTy(1))
    {
        switch(node->op)
        {
        case BinaryOpNode::OP_EQ:
            return builder.CreateICmpEQ(L, R, "bool.eq");
        case BinaryOpNode::OP_NE:
            return builder.CreateICmpNE(L, R, "bool.ne");
        case BinaryOpNode::OP_LT:
            return builder.CreateICmpULT(L, R, "bool.lt");
        case BinaryOpNode::OP_LE:
            return builder.CreateICmpULE(L, R, "bool.le");
        case BinaryOpNode::OP_GT:
            return builder.CreateICmpUGT(L, R, "bool.gt");
        case BinaryOpNode::OP_GE:
            return builder.CreateICmpUGE(L, R, "bool.ge");
        default:
            break;
        }
    }

    // Check if we're dealing with floating point or integer types
    bool isFloat =
        L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy();

    // Verify operand types are numeric
    bool LIsNumeric =
        L->getType()->isIntegerTy() || L->getType()->isFloatingPointTy();
    bool RIsNumeric =
        R->getType()->isIntegerTy() || R->getType()->isFloatingPointTy();

    if(!LIsNumeric || !RIsNumeric)
    {
        reportError(node->line,
                    "binary operations require numeric operands (integer or "
                    "floating-point)");
        return nullptr;
    }

    // Handle type mismatches for integer operands
    if(!isFloat && L->getType()->isIntegerTy() && R->getType()->isIntegerTy())
    {
        unsigned LBits = L->getType()->getIntegerBitWidth();
        unsigned RBits = R->getType()->getIntegerBitWidth();

        if(LBits != RBits)
        {
            // Extend the smaller type to match the larger type
            if(LBits > RBits)
            {
                R = builder.CreateIntCast(R, L->getType(), !useUnsignedIntOps,
                                          useUnsignedIntOps ? "zext" : "sext");
            }
            else
            {
                L = builder.CreateIntCast(L, R->getType(), !useUnsignedIntOps,
                                          useUnsignedIntOps ? "zext" : "sext");
            }
        }
    }

    switch(node->op)
    {
    case BinaryOpNode::OP_PLUS:
        return isFloat ? builder.CreateFAdd(L, R, "addtmp")
                       : builder.CreateAdd(L, R, "addtmp");
    case BinaryOpNode::OP_MINUS:
        return isFloat ? builder.CreateFSub(L, R, "subtmp")
                       : builder.CreateSub(L, R, "subtmp");
    case BinaryOpNode::OP_MULTIPLY:
        return isFloat ? builder.CreateFMul(L, R, "multmp")
                       : builder.CreateMul(L, R, "multmp");
    case BinaryOpNode::OP_DIVIDE:
    {
        // Check for division by zero with constant divisor
        if(auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(R))
        {
            if(constInt->isZero())
            {
                reportError(node->line, "division by zero");
                return nullptr;
            }
        }
        if(auto* constFP = llvm::dyn_cast<llvm::ConstantFP>(R))
        {
            if(constFP->isZero())
            {
                reportError(node->line, "division by zero");
                return nullptr;
            }
        }
        return isFloat
                   ? builder.CreateFDiv(L, R, "divtmp")
                   : (useUnsignedIntOps ? builder.CreateUDiv(L, R, "divtmp")
                                        : builder.CreateSDiv(L, R, "divtmp"));
    }
    case BinaryOpNode::OP_MODULO:
    {
        // Check for modulo by zero with constant divisor
        if(auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(R))
        {
            if(constInt->isZero())
            {
                reportError(node->line, "modulo by zero");
                return nullptr;
            }
        }
        if(auto* constFP = llvm::dyn_cast<llvm::ConstantFP>(R))
        {
            if(constFP->isZero())
            {
                reportError(node->line, "modulo by zero");
                return nullptr;
            }
        }
        if(isFloat)
        {
            return builder.CreateFRem(L, R, "modtmp");
        }
        return useUnsignedIntOps ? builder.CreateURem(L, R, "modtmp")
                                 : builder.CreateSRem(L, R, "modtmp");
    }
    case BinaryOpNode::OP_LT:
        return isFloat ? builder.CreateFCmpOLT(L, R, "cmptmp")
                       : (useUnsignedIntOps
                              ? builder.CreateICmpULT(L, R, "cmptmp")
                              : builder.CreateICmpSLT(L, R, "cmptmp"));
    case BinaryOpNode::OP_GT:
        return isFloat ? builder.CreateFCmpOGT(L, R, "cmptmp")
                       : (useUnsignedIntOps
                              ? builder.CreateICmpUGT(L, R, "cmptmp")
                              : builder.CreateICmpSGT(L, R, "cmptmp"));
    case BinaryOpNode::OP_LE:
        return isFloat ? builder.CreateFCmpOLE(L, R, "cmptmp")
                       : (useUnsignedIntOps
                              ? builder.CreateICmpULE(L, R, "cmptmp")
                              : builder.CreateICmpSLE(L, R, "cmptmp"));
    case BinaryOpNode::OP_GE:
        return isFloat ? builder.CreateFCmpOGE(L, R, "cmptmp")
                       : (useUnsignedIntOps
                              ? builder.CreateICmpUGE(L, R, "cmptmp")
                              : builder.CreateICmpSGE(L, R, "cmptmp"));
    case BinaryOpNode::OP_EQ:
        return isFloat ? builder.CreateFCmpOEQ(L, R, "cmptmp")
                       : builder.CreateICmpEQ(L, R, "cmptmp");
    case BinaryOpNode::OP_NE:
        return isFloat ? builder.CreateFCmpONE(L, R, "cmptmp")
                       : builder.CreateICmpNE(L, R, "cmptmp");
    case BinaryOpNode::OP_BITAND:
    case BinaryOpNode::OP_BITOR:
    case BinaryOpNode::OP_BITXOR:
    case BinaryOpNode::OP_SHL:
    case BinaryOpNode::OP_SHR:
        // Handled in dedicated branch above.
        return nullptr;
    case BinaryOpNode::OP_AND:
    case BinaryOpNode::OP_OR:
        // Handled before numeric op checks.
        return nullptr;
    case BinaryOpNode::OP_SPACESHIP:
        // Handled in dedicated branch above (numeric + trait-based struct
        // path).
        return nullptr;
    }
    return nullptr;
}

llvm::Value* CodeGenerator::generateFoldExpression(FoldExpressionNode* node)
{
    auto* listId = dynamic_cast<IdentifierNode*>(node->packExpr);
    if(!listId)
    {
        reportError(node->line,
                    "fold expression currently requires a list variable");
        return nullptr;
    }

    auto kindIt = variableTypes.find(listId->name);
    if(kindIt == variableTypes.end() || kindIt->second != TypeNode::TYPE_LIST)
    {
        reportError(node->line, "fold expression requires list operand");
        return nullptr;
    }

    auto elemTypeIt = listElementTypes.find(listId->name);
    if(elemTypeIt == listElementTypes.end() || !elemTypeIt->second)
    {
        reportError(node->line,
                    "cannot infer element type for fold expression");
        return nullptr;
    }

    llvm::Type* elemType = getLLVMTypeFromNode(elemTypeIt->second);
    if(!elemType)
        return nullptr;

    if(node->op == BinaryOpNode::OP_PLUS ||
       node->op == BinaryOpNode::OP_MULTIPLY)
    {
        if(!(elemType->isIntegerTy() || elemType->isFloatingPointTy()))
        {
            reportError(node->line,
                        "fold '+'/'*' requires numeric list elements");
            return nullptr;
        }
    }

    llvm::Value* listVal = generateExpression(node->packExpr);
    if(!listVal || !listVal->getType()->isStructTy())
    {
        reportError(node->line, "invalid fold operand");
        return nullptr;
    }

    llvm::Type* i64Type = llvm::Type::getInt64Ty(context);
    llvm::Type* boolTy = llvm::Type::getInt1Ty(context);
#if LLVM_VERSION_MAJOR >= 15
    llvm::Type* elemPtrTy = llvm::PointerType::get(context, 0);
#else
    llvm::Type* elemPtrTy = llvm::PointerType::get(elemType, 0);
#endif

    llvm::Value* lenVal =
        builder.CreateExtractValue(listVal, {0}, listId->name + ".fold.len");
    llvm::Value* dataRaw =
        builder.CreateExtractValue(listVal, {1}, listId->name + ".fold.data");
#if LLVM_VERSION_MAJOR >= 15
    // Opaque pointers (LLVM 15+): pointer type carries no pointee.
    (void)elemType;
    elemPtrTy = llvm::PointerType::get(context, 0);
#endif
    llvm::Value* dataPtr =
        builder.CreateBitCast(dataRaw, elemPtrTy, listId->name + ".fold.ptr");

    llvm::Type* accType = elemType;
    if(node->op == BinaryOpNode::OP_AND || node->op == BinaryOpNode::OP_OR)
        accType = boolTy;

    auto makeIdentity = [&]() -> llvm::Constant*
    {
        switch(node->op)
        {
        case BinaryOpNode::OP_PLUS:
            if(elemType->isFloatingPointTy())
                return llvm::ConstantFP::get(elemType, 0.0);
            return llvm::ConstantInt::get(elemType, 0);
        case BinaryOpNode::OP_MULTIPLY:
            if(elemType->isFloatingPointTy())
                return llvm::ConstantFP::get(elemType, 1.0);
            return llvm::ConstantInt::get(elemType, 1);
        case BinaryOpNode::OP_AND:
            return llvm::ConstantInt::getTrue(context);
        case BinaryOpNode::OP_OR:
            return llvm::ConstantInt::getFalse(context);
        default:
            return nullptr;
        }
    };

    llvm::Constant* identity = makeIdentity();
    if(!identity)
    {
        reportError(node->line, "unsupported fold operator");
        return nullptr;
    }

    auto toBoolValue = [&](llvm::Value* v, const char* name) -> llvm::Value*
    {
        if(v->getType()->isIntegerTy(1))
            return v;
        if(v->getType()->isIntegerTy())
            return builder.CreateICmpNE(
                v, llvm::ConstantInt::get(v->getType(), 0), name);
        if(v->getType()->isFloatingPointTy())
            return builder.CreateFCmpONE(
                v, llvm::ConstantFP::get(v->getType(), 0.0), name);
        reportError(node->line,
                    "logical fold requires numeric or boolean list elements");
        return static_cast<llvm::Value*>(nullptr);
    };

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::AllocaInst* accAlloca =
        builder.CreateAlloca(accType, nullptr, "fold.acc");
    llvm::AllocaInst* idxAlloca =
        builder.CreateAlloca(i64Type, nullptr, "fold.idx");
    builder.CreateStore(identity, accAlloca);

    if(node->isRightFold)
        builder.CreateStore(lenVal, idxAlloca);
    else
        builder.CreateStore(llvm::ConstantInt::get(i64Type, 0), idxAlloca);

    llvm::BasicBlock* condBB =
        llvm::BasicBlock::Create(context, "fold.cond", function);
    llvm::BasicBlock* bodyBB =
        llvm::BasicBlock::Create(context, "fold.body", function);
    llvm::BasicBlock* endBB =
        llvm::BasicBlock::Create(context, "fold.end", function);
    builder.CreateBr(condBB);

    builder.SetInsertPoint(condBB);
    llvm::Value* idxVal =
        builder.CreateLoad(i64Type, idxAlloca, "fold.idx.cur");
    llvm::Value* cond = nullptr;
    if(node->isRightFold)
        cond = builder.CreateICmpSGT(idxVal, llvm::ConstantInt::get(i64Type, 0),
                                     "fold.cond.right");
    else
        cond = builder.CreateICmpSLT(idxVal, lenVal, "fold.cond.left");
    builder.CreateCondBr(cond, bodyBB, endBB);

    builder.SetInsertPoint(bodyBB);
    llvm::Value* elemIdx = idxVal;
    if(node->isRightFold)
    {
        elemIdx = builder.CreateSub(idxVal, llvm::ConstantInt::get(i64Type, 1),
                                    "fold.elem.idx");
    }
    llvm::Value* elemPtr =
        builder.CreateGEP(elemType, dataPtr, elemIdx, "fold.elem.ptr");
    llvm::Value* elemVal = builder.CreateLoad(elemType, elemPtr, "fold.elem");
    llvm::Value* accVal =
        builder.CreateLoad(accType, accAlloca, "fold.acc.cur");

    llvm::Value* nextAcc = nullptr;
    switch(node->op)
    {
    case BinaryOpNode::OP_PLUS:
        nextAcc = elemType->isFloatingPointTy()
                      ? builder.CreateFAdd(accVal, elemVal, "fold.add")
                      : builder.CreateAdd(accVal, elemVal, "fold.add");
        break;
    case BinaryOpNode::OP_MULTIPLY:
        nextAcc = elemType->isFloatingPointTy()
                      ? builder.CreateFMul(accVal, elemVal, "fold.mul")
                      : builder.CreateMul(accVal, elemVal, "fold.mul");
        break;
    case BinaryOpNode::OP_AND:
    {
        llvm::Value* b = toBoolValue(elemVal, "fold.and.bool");
        if(!b)
            return nullptr;
        nextAcc = builder.CreateAnd(accVal, b, "fold.and");
        break;
    }
    case BinaryOpNode::OP_OR:
    {
        llvm::Value* b = toBoolValue(elemVal, "fold.or.bool");
        if(!b)
            return nullptr;
        nextAcc = builder.CreateOr(accVal, b, "fold.or");
        break;
    }
    default:
        reportError(node->line, "unsupported fold operator");
        return nullptr;
    }

    builder.CreateStore(nextAcc, accAlloca);
    llvm::Value* nextIdx =
        node->isRightFold
            ? builder.CreateSub(idxVal, llvm::ConstantInt::get(i64Type, 1),
                                "fold.idx.next")
            : builder.CreateAdd(idxVal, llvm::ConstantInt::get(i64Type, 1),
                                "fold.idx.next");
    builder.CreateStore(nextIdx, idxAlloca);
    builder.CreateBr(condBB);

    builder.SetInsertPoint(endBB);
    return builder.CreateLoad(accType, accAlloca, "fold.result");
}

llvm::Value* CodeGenerator::generateUnaryOp(UnaryOpNode* node)
{
    switch(node->op)
    {
    case UnaryOpNode::OP_NEG:
    {
        llvm::Value* value = generateExpression(node->operand);
        if(!value)
            return nullptr;

        bool isFloat = value->getType()->isFloatingPointTy();
        bool isInt = value->getType()->isIntegerTy();
        auto* structTy = llvm::dyn_cast<llvm::StructType>(value->getType());

        if(structTy && structTy->hasName())
        {
            const std::string structTypeName = structTy->getName().str();
            const std::string traitName = "Neg";
            const std::string methodName = "neg";

            auto traitIt = structImplementedTraits.find(structTypeName);
            if(traitIt == structImplementedTraits.end() ||
               traitIt->second.find(traitName) == traitIt->second.end())
            {
                reportError(node->line, "struct '" + structTypeName +
                                            "' must implement trait '" +
                                            traitName + "' to use unary '-'");
                return nullptr;
            }

            auto structIt = structMethods.find(structTypeName);
            if(structIt == structMethods.end())
            {
                reportError(node->line,
                            "struct '" + structTypeName + "' has no methods");
                return nullptr;
            }
            auto methodIt = structIt->second.find(methodName);
            if(methodIt == structIt->second.end() || !methodIt->second.second)
            {
                reportError(node->line, "trait '" + traitName +
                                            "' on struct '" + structTypeName +
                                            "' requires method '" + methodName +
                                            "'");
                return nullptr;
            }
            if(methodIt->second.second->isStatic)
            {
                reportError(node->line, "trait method '" + methodName +
                                            "' must not be static");
                return nullptr;
            }

            std::string definingStruct = structTypeName;
            std::string searchStruct = structTypeName;
            while(!searchStruct.empty())
            {
                std::string candidate = searchStruct + "_" + methodName;
                if(module->getFunction(candidate))
                {
                    definingStruct = searchStruct;
                    break;
                }
                auto baseIt = structBases.find(searchStruct);
                if(baseIt != structBases.end())
                    searchStruct = baseIt->second;
                else
                    break;
            }

            const std::string mangledName = definingStruct + "_" + methodName;
            llvm::Function* callee = module->getFunction(mangledName);
            if(!callee)
            {
                reportError(node->line, "unknown trait method: " + methodName);
                return nullptr;
            }
            if(callee->empty() && monomorphizedTypes.count(definingStruct))
            {
                auto defStructIt = structMethods.find(definingStruct);
                if(defStructIt != structMethods.end())
                {
                    auto defMethodIt = defStructIt->second.find(methodName);
                    if(defMethodIt != defStructIt->second.end())
                    {
                        StructMethodNode* methodDef =
                            defMethodIt->second.second;
                        if(methodDef && methodDef->body)
                        {
                            llvm::BasicBlock* savedBlock =
                                builder.GetInsertBlock();
                            auto savedNamedValues = namedValues;
                            auto savedConstantVariables = constantVariables;
                            auto savedVariableTypes = variableTypes;
                            auto savedStructVariableTypes = structVariableTypes;
                            auto savedTraitObjectVariableTypes =
                                traitObjectVariableTypes;
                            auto savedEnumVariableTypes = enumVariableTypes;
                            auto savedListElementTypes = listElementTypes;
                            auto savedMapKeyValueTypes = mapKeyValueTypes;
                            auto savedTupleElementTypes = tupleElementTypes;
                            auto savedPointerElementTypes = pointerElementTypes;
                            auto savedMovedVariables = movedVariables;
                            auto savedPointerBorrowTarget = pointerBorrowTarget;
                            auto savedActiveBorrowers = activeBorrowers;
                            auto savedActiveMutBorrower = activeMutBorrower;
                            auto savedVariableScopeDepth = variableScopeDepth;
                            auto savedCleanupScopes = cleanupScopes;
                            auto savedPointerBorrowScopes = pointerBorrowScopes;
                            auto savedVariableScopeDepthScopes =
                                variableScopeDepthScopes;

                            generateMethodDefinition(definingStruct, methodDef);

                            namedValues = savedNamedValues;
                            constantVariables = savedConstantVariables;
                            variableTypes = savedVariableTypes;
                            structVariableTypes = savedStructVariableTypes;
                            traitObjectVariableTypes =
                                savedTraitObjectVariableTypes;
                            enumVariableTypes = savedEnumVariableTypes;
                            listElementTypes = savedListElementTypes;
                            mapKeyValueTypes = savedMapKeyValueTypes;
                            tupleElementTypes = savedTupleElementTypes;
                            pointerElementTypes = savedPointerElementTypes;
                            movedVariables = savedMovedVariables;
                            pointerBorrowTarget = savedPointerBorrowTarget;
                            activeBorrowers = savedActiveBorrowers;
                            activeMutBorrower = savedActiveMutBorrower;
                            variableScopeDepth = savedVariableScopeDepth;
                            cleanupScopes = savedCleanupScopes;
                            pointerBorrowScopes = savedPointerBorrowScopes;
                            variableScopeDepthScopes =
                                savedVariableScopeDepthScopes;
                            if(savedBlock)
                                builder.SetInsertPoint(savedBlock);
                        }
                    }
                }
            }
            if(callee->arg_size() != 1)
            {
                reportError(node->line,
                            "trait method '" + methodName +
                                "' for unary '-' must take no arguments");
                return nullptr;
            }

            llvm::Value* selfPtr = getLValuePointer(node->operand, node->line);
            if(!selfPtr)
            {
                selfPtr = builder.CreateAlloca(value->getType(), nullptr,
                                               "traitneg.self.tmp");
                builder.CreateStore(value, selfPtr);
            }

            llvm::Value* res =
                builder.CreateCall(callee, {selfPtr}, "traitneg.call");
            if(res->getType() != value->getType())
            {
                reportError(node->line,
                            "trait method '" + methodName +
                                "' used by unary '-' must return the receiver "
                                "type");
                return nullptr;
            }
            return res;
        }

        if(!isFloat && !isInt)
        {
            reportError(
                node->line,
                "unary '-' requires numeric operand (integer or float)");
            return nullptr;
        }
        return isFloat ? builder.CreateFNeg(value, "negtmp")
                       : builder.CreateNeg(value, "negtmp");
    }
    case UnaryOpNode::OP_NOT:
    {
        llvm::Value* value = generateExpression(node->operand);
        if(!value)
            return nullptr;

        if(value->getType()->isIntegerTy(1))
            return builder.CreateNot(value, "nottmp");
        if(value->getType()->isIntegerTy())
        {
            llvm::Value* asBool = builder.CreateICmpNE(
                value, llvm::ConstantInt::get(value->getType(), 0), "not.bool");
            return builder.CreateNot(asBool, "nottmp");
        }
        if(value->getType()->isFloatingPointTy())
        {
            llvm::Value* asBool = builder.CreateFCmpONE(
                value, llvm::ConstantFP::get(value->getType(), 0.0),
                "not.bool");
            return builder.CreateNot(asBool, "nottmp");
        }

        reportError(node->line,
                    "unary '!' requires boolean or numeric operand");
        return nullptr;
    }
    case UnaryOpNode::OP_BITNOT:
    {
        llvm::Value* value = generateExpression(node->operand);
        if(!value)
            return nullptr;
        if(!value->getType()->isIntegerTy())
        {
            reportError(node->line, "unary '~' requires integer operand");
            return nullptr;
        }
        return builder.CreateNot(value, "bitnottmp");
    }
    case UnaryOpNode::OP_ADDR:
    {
        llvm::Value* ptr = getLValuePointer(node->operand, node->line);
        if(!ptr)
            return nullptr;
        return ptr;
    }
    case UnaryOpNode::OP_ADDR_MUT:
    {
        // &mut x — validate the owner is mutable (var, not let)
        std::string ownerName = resolveBorrowOwnerFromLValue(node->operand);
        if(!ownerName.empty() && constantVariables.count(ownerName))
        {
            reportError(
                node->line,
                "cannot take mutable reference of immutable variable '" +
                    ownerName + "'");
            return nullptr;
        }
        llvm::Value* ptr = getLValuePointer(node->operand, node->line);
        if(!ptr)
            return nullptr;
        return ptr;
    }
    case UnaryOpNode::OP_DEREF:
    {
        if(!validatePointerDereference(node->operand, node->line))
            return nullptr;

        llvm::Value* ptrVal = generateExpression(node->operand);
        if(!ptrVal)
            return nullptr;
        if(!ptrVal->getType()->isPointerTy())
        {
            reportError(node->line, "dereference requires a pointer value");
            return nullptr;
        }
        if(!emitRuntimeNullPointerCheck(ptrVal, node->line))
            return nullptr;

        TypeNode* elemTypeNode =
            getPointerElementType(node->operand, node->line);
        if(!elemTypeNode)
            return nullptr;
        llvm::Type* elemType = getLLVMTypeFromNode(elemTypeNode);
        if(!elemType)
            return nullptr;

        return builder.CreateLoad(elemType, ptrVal, "deref");
    }
    }
    return nullptr;
}

llvm::Value* CodeGenerator::generateUpdateExpression(UpdateExpressionNode* node)
{
    llvm::Value* ptr = getLValuePointer(node->operand, node->line);
    if(!ptr)
    {
        reportError(node->line, "++/-- requires an assignable variable");
        return nullptr;
    }

    // Load the current value (also gives us the concrete LLVM type).
    llvm::Value* oldVal = generateExpression(node->operand);
    if(!oldVal)
        return nullptr;

    llvm::Type* ty = oldVal->getType();
    bool isInc = (node->kind == UpdateExpressionNode::KIND_INCREMENT);
    if(ty->isIntegerTy() || ty->isFloatingPointTy())
    {
        llvm::Value* one = ty->isFloatingPointTy()
                               ? llvm::ConstantFP::get(ty, 1.0)
                               : llvm::ConstantInt::get(ty, 1);
        llvm::Value* newVal =
            ty->isFloatingPointTy()
                ? (isInc ? builder.CreateFAdd(oldVal, one, "upd.new")
                         : builder.CreateFSub(oldVal, one, "upd.new"))
                : (isInc ? builder.CreateAdd(oldVal, one, "upd.new")
                         : builder.CreateSub(oldVal, one, "upd.new"));

        builder.CreateStore(newVal, ptr);

        // prefix: return new value; postfix: return old value
        return node->isPrefix ? newVal : oldVal;
    }

    // Trait-based ++/-- for user-defined structs:
    //   trait Increment { fn increment(&mut self) -> void; }   or
    //   trait Increment { fn increment(self) -> Self; }
    //   trait Decrement { fn decrement(&mut self) -> void; }   or
    //   trait Decrement { fn decrement(self) -> Self; }
    const std::string traitName = isInc ? "Increment" : "Decrement";
    const std::string methodName = isInc ? "increment" : "decrement";

    auto* structTy = llvm::dyn_cast<llvm::StructType>(ty);
    if(!structTy || !structTy->hasName())
    {
        reportError(node->line,
                    "++/-- requires a numeric operand or trait-based struct");
        return nullptr;
    }
    const std::string structTypeName = structTy->getName().str();

    auto traitIt = structImplementedTraits.find(structTypeName);
    if(traitIt == structImplementedTraits.end() ||
       traitIt->second.find(traitName) == traitIt->second.end())
    {
        reportError(node->line, "struct '" + structTypeName +
                                    "' must implement trait '" + traitName +
                                    "' to use " + (isInc ? "++" : "--"));
        return nullptr;
    }

    auto structIt = structMethods.find(structTypeName);
    if(structIt == structMethods.end())
    {
        reportError(node->line,
                    "struct '" + structTypeName + "' has no methods");
        return nullptr;
    }
    auto methodIt = structIt->second.find(methodName);
    if(methodIt == structIt->second.end())
    {
        reportError(node->line, "trait '" + traitName + "' on struct '" +
                                    structTypeName + "' requires method '" +
                                    methodName + "'");
        return nullptr;
    }

    StructMethodNode* methodNode = methodIt->second.second;
    if(!methodNode)
    {
        reportError(node->line, "invalid method metadata for '" +
                                    structTypeName + "::" + methodName + "'");
        return nullptr;
    }
    if(methodNode->isStatic)
    {
        reportError(node->line,
                    "trait method '" + methodName + "' must not be static");
        return nullptr;
    }

    // Find defining struct (method may come from a base struct).
    std::string definingStruct = structTypeName;
    std::string searchStruct = structTypeName;
    while(!searchStruct.empty())
    {
        std::string candidate = searchStruct + "_" + methodName;
        if(module->getFunction(candidate))
        {
            definingStruct = searchStruct;
            break;
        }
        auto baseIt = structBases.find(searchStruct);
        if(baseIt != structBases.end())
        {
            searchStruct = baseIt->second;
        }
        else
        {
            break;
        }
    }

    const std::string mangledName = definingStruct + "_" + methodName;
    llvm::Function* callee = module->getFunction(mangledName);
    if(!callee)
    {
        reportError(node->line, "unknown trait method: " + methodName);
        return nullptr;
    }

    if(callee->empty() && monomorphizedTypes.count(definingStruct))
    {
        auto defStructIt = structMethods.find(definingStruct);
        if(defStructIt != structMethods.end())
        {
            auto defMethodIt = defStructIt->second.find(methodName);
            if(defMethodIt != defStructIt->second.end())
            {
                StructMethodNode* methodDef = defMethodIt->second.second;
                if(methodDef && methodDef->body)
                {
                    llvm::BasicBlock* savedBlock = builder.GetInsertBlock();
                    auto savedNamedValues = namedValues;
                    auto savedConstantVariables = constantVariables;
                    auto savedVariableTypes = variableTypes;
                    auto savedStructVariableTypes = structVariableTypes;
                    auto savedTraitObjectVariableTypes =
                        traitObjectVariableTypes;
                    auto savedEnumVariableTypes = enumVariableTypes;
                    auto savedListElementTypes = listElementTypes;
                    auto savedMapKeyValueTypes = mapKeyValueTypes;
                    auto savedTupleElementTypes = tupleElementTypes;
                    auto savedPointerElementTypes = pointerElementTypes;
                    auto savedMovedVariables = movedVariables;
                    auto savedPointerBorrowTarget = pointerBorrowTarget;
                    auto savedActiveBorrowers = activeBorrowers;
                    auto savedActiveMutBorrower = activeMutBorrower;
                    auto savedVariableScopeDepth = variableScopeDepth;
                    auto savedCleanupScopes = cleanupScopes;
                    auto savedPointerBorrowScopes = pointerBorrowScopes;
                    auto savedVariableScopeDepthScopes =
                        variableScopeDepthScopes;

                    generateMethodDefinition(definingStruct, methodDef);

                    namedValues = savedNamedValues;
                    constantVariables = savedConstantVariables;
                    variableTypes = savedVariableTypes;
                    structVariableTypes = savedStructVariableTypes;
                    traitObjectVariableTypes = savedTraitObjectVariableTypes;
                    enumVariableTypes = savedEnumVariableTypes;
                    listElementTypes = savedListElementTypes;
                    mapKeyValueTypes = savedMapKeyValueTypes;
                    tupleElementTypes = savedTupleElementTypes;
                    pointerElementTypes = savedPointerElementTypes;
                    movedVariables = savedMovedVariables;
                    pointerBorrowTarget = savedPointerBorrowTarget;
                    activeBorrowers = savedActiveBorrowers;
                    activeMutBorrower = savedActiveMutBorrower;
                    variableScopeDepth = savedVariableScopeDepth;
                    cleanupScopes = savedCleanupScopes;
                    pointerBorrowScopes = savedPointerBorrowScopes;
                    variableScopeDepthScopes = savedVariableScopeDepthScopes;
                    if(savedBlock)
                    {
                        builder.SetInsertPoint(savedBlock);
                    }
                }
            }
        }
    }

    std::vector<llvm::Value*> args;
    args.push_back(ptr);
    llvm::Value* newVal = nullptr;
    if(callee->getReturnType()->isVoidTy())
    {
        builder.CreateCall(callee, args);
        newVal = builder.CreateLoad(ty, ptr, "upd.new");
    }
    else
    {
        llvm::Value* callVal = builder.CreateCall(callee, args, "upd.new");
        if(callVal->getType() != ty)
        {
            reportError(node->line,
                        "trait method '" + methodName +
                            "' must return void or the receiver type");
            return nullptr;
        }
        builder.CreateStore(callVal, ptr);
        newVal = callVal;
    }

    return node->isPrefix ? newVal : oldVal;
}

llvm::Value* CodeGenerator::generateTernaryExpression(TernaryNode* node)
{
    auto incomingMoved = movedVariables;
    auto incomingPointerBorrowTarget = pointerBorrowTarget;
    auto incomingActiveBorrowers = activeBorrowers;
    auto incomingActiveMutBorrower = activeMutBorrower;

    llvm::Value* condValue = generateExpression(node->condition);
    if(!condValue)
        return nullptr;

    llvm::Type* condType = condValue->getType();
    if(!condType->isIntegerTy() && !condType->isFloatingPointTy())
    {
        std::string typeStr;
        if(condType->isStructTy())
            typeStr = condType->getStructName().str().empty()
                          ? "struct"
                          : condType->getStructName().str();
        else if(condType->isPointerTy())
            typeStr = "pointer";
        else
            typeStr = "non-boolean";

        reportError(
            node->line,
            "ternary condition must be a boolean or numeric type, got '" +
                typeStr + "'");
        return nullptr;
    }

    if(!condValue->getType()->isIntegerTy(1))
    {
        if(condValue->getType()->isFloatingPointTy())
        {
            condValue = builder.CreateFCmpONE(
                condValue, llvm::ConstantFP::get(condValue->getType(), 0.0),
                "ternary.cond");
        }
        else
        {
            condValue = builder.CreateICmpNE(
                condValue, llvm::ConstantInt::get(condValue->getType(), 0),
                "ternary.cond");
        }
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* thenBB =
        llvm::BasicBlock::Create(context, "ternary.then", function);
    llvm::BasicBlock* elseBB =
        llvm::BasicBlock::Create(context, "ternary.else");
    llvm::BasicBlock* mergeBB =
        llvm::BasicBlock::Create(context, "ternary.end");

    builder.CreateCondBr(condValue, thenBB, elseBB);

    // Then block
    builder.SetInsertPoint(thenBB);
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    activeBorrowers = incomingActiveBorrowers;
    activeMutBorrower = incomingActiveMutBorrower;
    llvm::Value* thenVal = generateExpression(node->trueExpr);
    if(!thenVal)
        return nullptr;
    auto thenMoved = movedVariables;
    auto thenPointerBorrowTarget = pointerBorrowTarget;
    auto thenActiveBorrowers = activeBorrowers;
    auto thenActiveMutBorrower = activeMutBorrower;
    if(!builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(mergeBB);
    llvm::BasicBlock* thenEnd = builder.GetInsertBlock();

    // Else block
    elseBB->insertInto(function);
    builder.SetInsertPoint(elseBB);
    movedVariables = incomingMoved;
    pointerBorrowTarget = incomingPointerBorrowTarget;
    activeBorrowers = incomingActiveBorrowers;
    activeMutBorrower = incomingActiveMutBorrower;
    llvm::Value* elseVal = generateExpression(node->falseExpr);
    if(!elseVal)
        return nullptr;
    auto elseMoved = movedVariables;
    auto elsePointerBorrowTarget = pointerBorrowTarget;
    auto elseActiveBorrowers = activeBorrowers;
    auto elseActiveMutBorrower = activeMutBorrower;
    if(!builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(mergeBB);
    llvm::BasicBlock* elseEnd = builder.GetInsertBlock();

    std::vector<llvm::Type*> types = {thenVal->getType(), elseVal->getType()};
    auto commonTypeFrom =
        [&](const std::vector<llvm::Type*>& tps) -> llvm::Type*
    {
        if(tps.empty())
            return nullptr;
        llvm::Type* common = tps[0];
        bool anyFloat = common->isFloatingPointTy();
        bool anyInt = common->isIntegerTy();
        bool anyPtr = common->isPointerTy();
        bool anyStruct = common->isStructTy();

        unsigned maxIntBits = anyInt ? common->getIntegerBitWidth() : 0;
        bool anyDouble = common->isDoubleTy();

        for(size_t i = 1; i < tps.size(); ++i)
        {
            llvm::Type* t = tps[i];
            if(t == common)
                continue;
            if(t->isFloatingPointTy())
            {
                anyFloat = true;
                if(t->isDoubleTy())
                    anyDouble = true;
            }
            if(t->isIntegerTy())
            {
                anyInt = true;
                unsigned bits = t->getIntegerBitWidth();
                if(bits > maxIntBits)
                    maxIntBits = bits;
            }
            if(t->isPointerTy())
                anyPtr = true;
            if(t->isStructTy())
                anyStruct = true;
        }

        if(anyStruct)
        {
            for(auto* t : tps)
            {
                if(t != tps[0])
                    return nullptr;
            }
            return tps[0];
        }

        if(anyPtr && (anyFloat || anyInt || anyStruct))
            return nullptr;

        if(anyPtr && !(anyFloat || anyInt))
        {
            for(auto* t : tps)
            {
                if(!t->isPointerTy() || t != tps[0])
                    return nullptr;
            }
            return tps[0];
        }

        if(anyFloat)
            return anyDouble ? llvm::Type::getDoubleTy(context)
                             : llvm::Type::getFloatTy(context);
        if(anyInt)
            return llvm::Type::getIntNTy(context, maxIntBits);

        for(auto* t : tps)
        {
            if(t != tps[0])
                return nullptr;
        }
        return tps[0];
    };

    llvm::Type* commonType = commonTypeFrom(types);
    if(!commonType)
    {
        std::string t0 =
            thenVal->getType()->isStructTy()
                ? (thenVal->getType()->getStructName().str().empty()
                       ? "struct"
                       : thenVal->getType()->getStructName().str())
                : (thenVal->getType()->isPointerTy() ? "pointer" : "value");
        std::string t1 =
            elseVal->getType()->isStructTy()
                ? (elseVal->getType()->getStructName().str().empty()
                       ? "struct"
                       : elseVal->getType()->getStructName().str())
                : (elseVal->getType()->isPointerTy() ? "pointer" : "value");
        reportError(node->line,
                    "ternary branches must return the same type (got " + t0 +
                        " and " + t1 + ")");
        return nullptr;
    }
    if(commonType->isVoidTy())
    {
        reportError(node->line, "ternary branches must return a value");
        return nullptr;
    }

    if(thenVal->getType() != commonType)
    {
        llvm::IRBuilder<> castBuilder(thenEnd->getTerminator());
        llvm::Type* src = thenVal->getType();
        if(src->isIntegerTy() && commonType->isIntegerTy())
        {
            unsigned srcBits = src->getIntegerBitWidth();
            unsigned dstBits = commonType->getIntegerBitWidth();
            if(srcBits > dstBits)
                thenVal =
                    castBuilder.CreateTrunc(thenVal, commonType, "tern.trunc");
            else if(srcBits < dstBits)
                thenVal =
                    castBuilder.CreateSExt(thenVal, commonType, "tern.sext");
        }
        else if(src->isIntegerTy() && commonType->isFloatingPointTy())
        {
            thenVal =
                castBuilder.CreateSIToFP(thenVal, commonType, "tern.sitofp");
        }
        else if(src->isFloatingPointTy() && commonType->isFloatingPointTy())
        {
            thenVal =
                castBuilder.CreateFPCast(thenVal, commonType, "tern.fpcast");
        }
    }

    if(elseVal->getType() != commonType)
    {
        llvm::IRBuilder<> castBuilder(elseEnd->getTerminator());
        llvm::Type* src = elseVal->getType();
        if(src->isIntegerTy() && commonType->isIntegerTy())
        {
            unsigned srcBits = src->getIntegerBitWidth();
            unsigned dstBits = commonType->getIntegerBitWidth();
            if(srcBits > dstBits)
                elseVal =
                    castBuilder.CreateTrunc(elseVal, commonType, "tern.trunc");
            else if(srcBits < dstBits)
                elseVal =
                    castBuilder.CreateSExt(elseVal, commonType, "tern.sext");
        }
        else if(src->isIntegerTy() && commonType->isFloatingPointTy())
        {
            elseVal =
                castBuilder.CreateSIToFP(elseVal, commonType, "tern.sitofp");
        }
        else if(src->isFloatingPointTy() && commonType->isFloatingPointTy())
        {
            elseVal =
                castBuilder.CreateFPCast(elseVal, commonType, "tern.fpcast");
        }
    }

    mergeBB->insertInto(function);
    builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder.CreatePHI(commonType, 2, "ternary.result");
    phi->addIncoming(thenVal, thenEnd);
    phi->addIncoming(elseVal, elseEnd);

    std::set<std::string> mergedMoved = thenMoved;
    mergedMoved.insert(elseMoved.begin(), elseMoved.end());
    movedVariables = std::move(mergedMoved);

    std::map<std::string, std::string> mergedPointerBorrowTarget =
        thenPointerBorrowTarget;
    for(const auto& kv : elsePointerBorrowTarget)
    {
        if(mergedPointerBorrowTarget.find(kv.first) ==
           mergedPointerBorrowTarget.end())
        {
            mergedPointerBorrowTarget[kv.first] = kv.second;
        }
    }
    pointerBorrowTarget = std::move(mergedPointerBorrowTarget);

    std::map<std::string, std::set<std::string>> mergedActiveBorrowers =
        thenActiveBorrowers;
    for(const auto& kv : elseActiveBorrowers)
    {
        auto& dst = mergedActiveBorrowers[kv.first];
        dst.insert(kv.second.begin(), kv.second.end());
    }
    activeBorrowers = std::move(mergedActiveBorrowers);

    // Merge &mut borrowers: keep only those active in both branches
    std::map<std::string, std::string> mergedActiveMutBorrower;
    for(const auto& kv : thenActiveMutBorrower)
        if(elseActiveMutBorrower.count(kv.first))
            mergedActiveMutBorrower[kv.first] = kv.second;
    activeMutBorrower = std::move(mergedActiveMutBorrower);

    return phi;
}

llvm::Value* CodeGenerator::generateTryExpression(TryExpressionNode* node)
{
    if(!node || !node->expression)
        return nullptr;

    llvm::Value* resultValue = generateExpression(node->expression);
    if(!resultValue)
        return nullptr;

    if(!resultValue->getType()->isStructTy())
    {
        reportError(node->line, "operator '?' expects result<T, E> expression");
        return nullptr;
    }

    auto* resultStructType =
        llvm::cast<llvm::StructType>(resultValue->getType());
    std::string resultStructName = resultStructType->getName().str();
    auto resultMembersIt = structMembers.find(resultStructName);
    if(resultMembersIt == structMembers.end())
    {
        reportError(node->line, "operator '?' expects result<T, E> expression");
        return nullptr;
    }

    int resultIsOkIndex = -1;
    int resultOkIndex = -1;
    int resultErrIndex = -1;
    TypeNode* resultOkType = nullptr;
    TypeNode* resultErrType = nullptr;
    for(size_t i = 0; i < resultMembersIt->second.size(); ++i)
    {
        const auto& mem = resultMembersIt->second[i];
        if(mem.first == "is_ok")
            resultIsOkIndex = static_cast<int>(i);
        else if(mem.first == "ok")
        {
            resultOkIndex = static_cast<int>(i);
            resultOkType = mem.second;
        }
        else if(mem.first == "err")
        {
            resultErrIndex = static_cast<int>(i);
            resultErrType = mem.second;
        }
    }

    if(resultIsOkIndex < 0 || resultOkIndex < 0 || resultErrIndex < 0)
    {
        reportError(node->line, "operator '?' expects result<T, E> expression");
        return nullptr;
    }

    llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
    llvm::Type* expectedRetType = currentFunc->getReturnType();
    if(!expectedRetType->isStructTy())
    {
        reportError(node->line, "operator '?' can only be used in functions "
                                "returning result<_, _>");
        return nullptr;
    }

    auto* expectedStructType = llvm::cast<llvm::StructType>(expectedRetType);
    std::string expectedStructName = expectedStructType->getName().str();
    auto expectedMembersIt = structMembers.find(expectedStructName);
    if(expectedMembersIt == structMembers.end())
    {
        reportError(node->line, "operator '?' can only be used in functions "
                                "returning result<_, _>");
        return nullptr;
    }

    int expectedIsOkIndex = -1;
    int expectedOkIndex = -1;
    int expectedErrIndex = -1;
    TypeNode* expectedErrType = nullptr;
    for(size_t i = 0; i < expectedMembersIt->second.size(); ++i)
    {
        const auto& mem = expectedMembersIt->second[i];
        if(mem.first == "is_ok")
            expectedIsOkIndex = static_cast<int>(i);
        else if(mem.first == "ok")
            expectedOkIndex = static_cast<int>(i);
        else if(mem.first == "err")
        {
            expectedErrIndex = static_cast<int>(i);
            expectedErrType = mem.second;
        }
    }

    if(expectedIsOkIndex < 0 || expectedOkIndex < 0 || expectedErrIndex < 0 ||
       !expectedErrType)
    {
        reportError(node->line, "operator '?' can only be used in functions "
                                "returning result<_, _>");
        return nullptr;
    }

    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* okBB =
        llvm::BasicBlock::Create(context, "try.ok", function);
    llvm::BasicBlock* errBB = llvm::BasicBlock::Create(context, "try.err");
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(context, "try.cont");

    llvm::Value* isOkVal =
        builder.CreateExtractValue(resultValue, resultIsOkIndex, "try.is_ok");
    llvm::Type* okLlvmType = getLLVMTypeFromNode(resultOkType);
    if(!okLlvmType)
    {
        reportError(node->line,
                    "operator '?': failed to resolve Ok payload type");
        return nullptr;
    }
    llvm::AllocaInst* okSlot =
        builder.CreateAlloca(okLlvmType, nullptr, "try.ok.slot");

    builder.CreateCondBr(isOkVal, okBB, errBB);

    builder.SetInsertPoint(okBB);
    llvm::Value* okPayload = builder.CreateExtractValue(
        resultValue, resultOkIndex, "try.ok.payload");
    builder.CreateStore(okPayload, okSlot);
    builder.CreateBr(contBB);

    errBB->insertInto(function);
    builder.SetInsertPoint(errBB);
    llvm::Value* errPayload = builder.CreateExtractValue(
        resultValue, resultErrIndex, "try.err.payload");
    llvm::Type* expectedErrLlvmType = getLLVMTypeFromNode(expectedErrType);
    if(!expectedErrLlvmType)
    {
        reportError(node->line,
                    "operator '?': failed to resolve Err payload type");
        return nullptr;
    }

    if(errPayload->getType() != expectedErrLlvmType)
    {
        if(errPayload->getType()->isIntegerTy() &&
           expectedErrLlvmType->isIntegerTy())
        {
            errPayload = builder.CreateIntCast(errPayload, expectedErrLlvmType,
                                               true, "try.err.cast");
        }
        else if(errPayload->getType()->isIntegerTy() &&
                expectedErrLlvmType->isFloatingPointTy())
        {
            errPayload = builder.CreateSIToFP(errPayload, expectedErrLlvmType,
                                              "try.err.sitofp");
        }
        else if(errPayload->getType()->isFloatingPointTy() &&
                expectedErrLlvmType->isFloatingPointTy())
        {
            errPayload = builder.CreateFPCast(errPayload, expectedErrLlvmType,
                                              "try.err.fpcast");
        }
        else if(errPayload->getType()->isPointerTy() &&
                expectedErrLlvmType->isPointerTy())
        {
            errPayload = builder.CreateBitCast(errPayload, expectedErrLlvmType,
                                               "try.err.ptrcast");
        }
        else if(errPayload->getType()->isStructTy() &&
                expectedErrLlvmType->isStructTy() &&
                errPayload->getType() == expectedErrLlvmType)
        {
            // Same struct type, nothing to do.
        }
        else
        {
            reportError(
                node->line,
                "operator '?': Err type does not match function return type");
            return nullptr;
        }
    }

    llvm::Value* retResult = llvm::UndefValue::get(expectedRetType);
    // For result<_, string>, append call-site context on every `?` propagation.
    if(expectedErrType && (expectedErrType->kind == TypeNode::TYPE_STRING ||
                           expectedErrType->kind == TypeNode::TYPE_STR8))
    {
        llvm::Type* charPtrTy = getLLVMType(TypeNode::TYPE_STRING);
        llvm::FunctionCallee addContextFn = module->getOrInsertFunction(
            "__mlang_error_add_context",
            llvm::FunctionType::get(charPtrTy, {charPtrTy, charPtrTy}, false));
        std::string fnLabel = function->getName().str();
        size_t mangledPos = fnLabel.rfind("__");
        if(mangledPos != std::string::npos && mangledPos > 0)
            fnLabel = fnLabel.substr(0, mangledPos);
        if(fnLabel == "__mlang_user_main")
            fnLabel = "main";
        std::string tryContext = fnLabel + ":" + std::to_string(node->line);
        llvm::Value* ctxStr =
            Helpers::create_global_cstring(builder, tryContext, "try.ctx");
        errPayload = builder.CreateCall(addContextFn, {errPayload, ctxStr},
                                        "try.err.withctx");
    }

    retResult = builder.CreateInsertValue(
        retResult, llvm::ConstantInt::getFalse(context),
        static_cast<unsigned>(expectedIsOkIndex), "try.ret.is_ok");
    retResult = builder.CreateInsertValue(
        retResult,
        llvm::Constant::getNullValue(expectedRetType->getStructElementType(
            static_cast<unsigned>(expectedOkIndex))),
        static_cast<unsigned>(expectedOkIndex), "try.ret.ok");
    retResult = builder.CreateInsertValue(
        retResult, errPayload, static_cast<unsigned>(expectedErrIndex),
        "try.ret.err");
    emitAllActiveCleanups();
    builder.CreateRet(retResult);

    contBB->insertInto(function);
    builder.SetInsertPoint(contBB);
    return builder.CreateLoad(okLlvmType, okSlot, "try.ok");
}
