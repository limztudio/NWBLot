// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_codegen_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Array<MStringView, 4> s_LaneCallSuffixes = {
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex, cppLaneCallSuffix) \
    cppLaneCallSuffix,
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsRegisterPromotableSimdType(const MStringView simdType)noexcept{
    return SymbolTable::IsSimdRegisterType(simdType);
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
    const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(name);
    return fnInfo ? fnInfo->cppExportName : MStringView{};
}

[[nodiscard]] MStringView MapSimdRegisterCall(const MStringView callee)noexcept{
#define NWB_ADHD_EXPORT_SIMD_REGISTER_CALL(adhdName) \
    if(callee == adhdName) return MapBuiltinMathFunction(callee);
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_REGISTER_CALL
    return {};
}

[[nodiscard]] b8 IsSimdScalarReductionFunction(const MStringView callee)noexcept{
#define NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION(adhdName, laneIntrinsicSuffix) \
    if(callee == adhdName) return true;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION
    return false;
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
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case assignToken: return binaryToken;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return op;
    }
}

[[nodiscard]] MStringView SimdBinaryScalarOpSymbol(const TokenType::Enum binaryOp)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case binaryToken: return paddedBinarySymbol;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return {};
    }
}

[[nodiscard]] b8 IsSimdIntLaneFallbackBinaryOp(const TokenType::Enum binaryOp)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case binaryToken: return isIntLaneFallback;
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return false;
    }
}

[[nodiscard]] MStringView MapSimdBitwiseBinaryIntrinsic(const TokenType::Enum binaryOp, const b8 isDouble)noexcept{
    switch(binaryOp){
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble) \
    case binaryToken: return isDouble ? MStringView(bitwiseIntrinsicDouble) : MStringView(bitwiseIntrinsicSingle);
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
    default: return {};
    }
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

[[nodiscard]] b8 IsZeroScalarLiteral(const Expr& expr)noexcept{
    if(expr.text.empty())
        return false;
    if(expr.kind == ExprKind::IntegerLiteral){
        b8 sawZero = false;
        const b8 isHex = expr.text.size() >= 2u && expr.text[0] == '0' && (expr.text[1] == 'x' || expr.text[1] == 'X');
        for(usize i = isHex ? 2u : 0u; i < expr.text.size(); ++i){
            const char c = expr.text[i];
            if(c == '0'){
                sawZero = true;
                continue;
            }
            if(c == 'u' || c == 'U' || c == 'l' || c == 'L' || (!isHex && (c == 'b' || c == 'B')))
                continue;
            return false;
        }
        return sawZero;
    }
    if(expr.kind == ExprKind::FloatLiteral || expr.kind == ExprKind::DoubleLiteral){
        b8 sawZero = false;
        for(usize i = 0u; i < expr.text.size(); ++i){
            const char c = expr.text[i];
            if(c == 'e' || c == 'E')
                break;
            if(c == '0'){
                sawZero = true;
                continue;
            }
            if(c == '.' || c == 'f' || c == 'F')
                continue;
            return false;
        }
        return sawZero;
    }
    return false;
}

[[nodiscard]] b8 IsPureSimdAddressExpr(const Expr& expr)noexcept{
    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:
    case ExprKind::ThisMember:
        return !expr.isMutArg;
    case ExprKind::ScopeAccess:
        return !expr.isMutArg && (!expr.lhs || IsPureSimdAddressExpr(*expr.lhs));
    case ExprKind::MemberAccess:
        return !expr.isMutArg && expr.lhs && IsPureSimdAddressExpr(*expr.lhs);
    case ExprKind::Index:
        return !expr.isMutArg && expr.lhs && expr.rhs && IsPureSimdAddressExpr(*expr.lhs) && IsPureSimdAddressExpr(*expr.rhs);
    default:
        return false;
    }
}

[[nodiscard]] b8 AreSamePureSimdExpr(const Expr& a, const Expr& b)noexcept{
    if(a.kind != b.kind || a.text != b.text || a.op != b.op || a.isSwizzle != b.isSwizzle || a.isPointerMemberAccess != b.isPointerMemberAccess)
        return false;
    if(a.scopeChain.size() != b.scopeChain.size())
        return false;
    for(usize i = 0u; i < a.scopeChain.size(); ++i){
        if(a.scopeChain[i] != b.scopeChain[i])
            return false;
    }
    if((a.lhs != nullptr) != (b.lhs != nullptr) || (a.rhs != nullptr) != (b.rhs != nullptr))
        return false;
    if(a.lhs && !AreSamePureSimdExpr(*a.lhs, *b.lhs))
        return false;
    if(a.rhs && !AreSamePureSimdExpr(*a.rhs, *b.rhs))
        return false;
    return a.arguments.empty() && b.arguments.empty();
}

[[nodiscard]] b8 TryMatchSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept{
    if(!expr || expr->kind != ExprKind::MemberAccess || expr->isSwizzle || expr->isMutArg || !expr->lhs)
        return false;
    if(!IsSingleSwizzleLane(expr->text, outLane) || !IsPureSimdAddressExpr(*expr->lhs))
        return false;
    outBase = expr->lhs;
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
        if(expr->isBuiltinCall){
            if(IsRegisterPromotableSimdType(expr->resolvedSimdType))
                return true;
            if(IsSimdScalarReductionFunction(callee) || !MapSimdRegisterCall(callee).empty()){
                for(const Expr* arg : expr->arguments){
                    if(arg && IsRegisterPromotableSimdType(arg->resolvedSimdType))
                        return true;
                }
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

