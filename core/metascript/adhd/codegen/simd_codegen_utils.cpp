// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_codegen_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr MStringView s_LaneCallSuffixes[4] = {
    "X(",
    "Y(",
    "Z(",
    "W("
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsRegisterPromotableSimdType(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType))
        return vecInfo->isFloatReg || vecInfo->isIntReg;
    return FindBuiltinSimdMatrixType(simdType) != nullptr;
}

[[nodiscard]] b8 IsDoubleSimdType(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType))
        return vecInfo->isDoubleReg && (vecInfo->isFloatReg || vecInfo->isIntReg);
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->isDoubleReg;
    return false;
}

[[nodiscard]] b8 IsIntSimdType(const MStringView simdType)noexcept{
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType);
    return vecInfo && vecInfo->isIntReg;
}

[[nodiscard]] b8 IsSignedIntSimdType(const MStringView simdType)noexcept{
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType);
    return vecInfo && vecInfo->isIntReg && vecInfo->isSignedInt;
}

[[nodiscard]] MStringView IntSimdLaneType(const MStringView simdType)noexcept{
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType);
    if(vecInfo && vecInfo->isIntReg)
        return vecInfo->xyzScalarType;
    return "u64";
}

[[nodiscard]] MStringView IntSimdRawWordType(const b8 isDouble)noexcept{
    return isDouble ? "u64" : "u32";
}

[[nodiscard]] MStringView SimdLaneCallSuffix(const u32 lane)noexcept{
    return __hidden_adhd_simd_codegen_utils::s_LaneCallSuffixes[lane < 4u ? lane : 3u];
}

void AppendIntSimdLaneExtract(MString& out, const MStringView varName, const u32 lane, const b8 isDouble, const MStringView laneType){
    const MStringView laneSuffix = SimdLaneCallSuffix(lane);
    out.append("static_cast<");
    out.append(laneType.data(), laneType.size());
    out.append(">(");
    out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
    out.append(laneSuffix.data(), laneSuffix.size());
    out.append(varName.data(), varName.size());
    out.append("))");
}

[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept{
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle && expr.text.size() >= 2u && expr.text.size() <= 4u)
        return static_cast<u32>(expr.text.size());
    u32 lanes = 4u;
    if(SymbolTable::IsSimdVectorType(expr.resolvedSimdType, lanes))
        return lanes;
    return 4u;
}

[[nodiscard]] b8 IsUnitFloatLiteral(const Expr& expr)noexcept{
    if(expr.kind != ExprKind::FloatLiteral && expr.kind != ExprKind::DoubleLiteral)
        return false;
    return expr.text == "1.0f" || expr.text == "1.0" || expr.text == "1.f";
}

[[nodiscard]] MStringView SimdRegisterCppType(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->regCppType.empty())
        return vecInfo->regCppType;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->regCppType;
    return "::SIMDVectorSingle";
}

[[nodiscard]] MStringView SimdLoadFunctionCpp(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->loadCppFunc.empty())
        return vecInfo->loadCppFunc;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->loadCppFunc;
    return "::LoadFloat";
}

[[nodiscard]] MStringView SimdStoreFunctionCpp(const MStringView simdType)noexcept{
    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(simdType); vecInfo && !vecInfo->storeCppFunc.empty())
        return vecInfo->storeCppFunc;
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType))
        return matInfo->storeCppFunc;
    return "::StoreFloat";
}

[[nodiscard]] MStringView MapBuiltinMathFunction(const MStringView name)noexcept{
    if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(name))
        return fnInfo->cppExportName;
    return {};
}

[[nodiscard]] MStringView MapSimdUnaryFloatIntrinsic(const MStringView callee, const b8 isDouble)noexcept{
#define NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC(adhdName, floatIntrinsicPrefix, doubleIntrinsicPrefix) \
    if(callee == adhdName) return isDouble ? doubleIntrinsicPrefix : floatIntrinsicPrefix;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_FLOAT_INTRINSIC
    return {};
}

void AppendSimdLaneCountIntrinsicPrefix(MString& out, const b8 isDouble, const u32 effLanes, const MStringView suffix){
    out.append(isDouble ? "::VectorDouble" : "::Vector");
    out.push_back(effLanes == 2u ? '2' : (effLanes == 3u ? '3' : '4'));
    out.append(suffix.data(), suffix.size());
    out.push_back('(');
}

[[nodiscard]] TokenType::Enum MapSimdAssignToBinaryOp(const TokenType::Enum op)noexcept{
    switch(op){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol) \
    case assignToken: return binaryToken;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return op;
    }
}

[[nodiscard]] MStringView SimdBinaryScalarOpSymbol(const TokenType::Enum binaryOp)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol) \
    case binaryToken: return paddedBinarySymbol;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return {};
    }
}

[[nodiscard]] b8 IsSingleSwizzleLane(const MStringView text, u32& outLane)noexcept{
    return text.size() == 1u && TryMapSwizzleCharToLane(text[0], outLane);
}

[[nodiscard]] u32 MapSwizzleCharToLane(const char c)noexcept{
    u32 lane = 3u;
    if(!TryMapSwizzleCharToLane(c, lane))
        return 3u;
    return lane;
}

[[nodiscard]] b8 IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept{
    if(!expr || expr->kind != ExprKind::Binary || expr->op != TokenType::Star || !expr->lhs || !expr->rhs)
        return false;
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(expr->resolvedSimdType);
    if(!vecInfo || !vecInfo->isFloatReg)
        return false;
    outMulA = expr->lhs;
    outMulB = expr->rhs;
    return true;
}

[[nodiscard]] MStringView ResolveLocalVarDeclSimdType(const Stmt& stmt, const SymbolTable& symbols)noexcept{
    if(stmt.usesScratchArena || (stmt.varType && stmt.varType->refMode != ParamPassingMode::None))
        return {};
    if(stmt.varType){
        const MStringView resolved = symbols.resolveBaseTypeName(stmt.varType->name);
        if(IsRegisterPromotableSimdType(resolved))
            return resolved;
    }
    if(stmt.expr && IsRegisterPromotableSimdType(stmt.expr->resolvedSimdType))
        return stmt.expr->resolvedSimdType;
    return {};
}

[[nodiscard]] b8 ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept{
    if(!expr)
        return false;

    if(expr->kind == ExprKind::MemberAccess && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
        u32 lane = 0u;
        if(expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane))
            return true;
    }

    if(expr->kind == ExprKind::Index && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Unary && IsRegisterPromotableSimdType(expr->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Binary && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        if(expr->op != TokenType::Equal)
            return true;
        if(ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }

    if(expr->kind == ExprKind::Binary && IsCompoundOrSimpleAssignment(expr->op) && expr->lhs){
        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType) && IsSingleSwizzleLane(expr->lhs->text, lane))
            return true;
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType))
            return true;
    }

    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr->lhs->text;
        const MStringView baseCallee = symbols.resolveBaseTypeName(callee);
        if(SymbolTable::IsSimdType(baseCallee) && IsRegisterPromotableSimdType(expr->resolvedSimdType))
            return true;
        if(!MapBuiltinMathFunction(callee).empty()){
            if(IsRegisterPromotableSimdType(expr->resolvedSimdType))
                return true;
            for(const Expr* arg : expr->arguments){
                if(arg && IsRegisterPromotableSimdType(arg->resolvedSimdType))
                    return true;
            }
        }
    }

    if(ExprContainsSimdMath(expr->lhs, symbols) || ExprContainsSimdMath(expr->rhs, symbols))
        return true;

    for(const Expr* arg : expr->arguments){
        if(ExprContainsSimdMath(arg, symbols))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

