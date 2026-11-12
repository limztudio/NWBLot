// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_optimization_analysis.h"
#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(const Expr& scalarExpr, const MStringView simdType, const SymbolTable& symbols)noexcept{
    if(!scalarExpr.resolvedSimdType.empty() || IsIntSimdType(simdType) || SymbolTable::IsSimdMatrixType(simdType))
        return false;
    if(scalarExpr.kind == ExprKind::Unary && !scalarExpr.isPostfix && scalarExpr.op == TokenType::Minus && scalarExpr.lhs)
        return SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*scalarExpr.lhs, simdType, symbols);
    const b8 isDouble = IsDoubleSimdType(simdType);
    const Expr* laneBase = nullptr;
    u32 laneIdx = 0u;
    if(SimdExpressionAnalysis::TryMatchSingleLaneAccess(&scalarExpr, laneBase, laneIdx) && laneBase && laneBase->resolvedSimdType == simdType)
        return true;
    if(scalarExpr.kind == ExprKind::Call && scalarExpr.isBuiltinCall && scalarExpr.lhs && scalarExpr.lhs->kind == ExprKind::Identifier){
        const MStringView callee = scalarExpr.lhs->text;
        if(!MapSimdScalarReductionSuffix(callee).empty() && !scalarExpr.arguments.empty() && scalarExpr.arguments[0]){
            const MStringView argSimd = scalarExpr.arguments[0]->resolvedSimdType;
            if(IsRegisterPromotableSimdType(argSimd) && IsDoubleSimdType(argSimd) == isDouble)
                return true;
        }
    }
    return false;
}

[[nodiscard]] const Expr* SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(const Expr& expr, const MStringView simdType, const SymbolTable& symbols)noexcept{
    if(IsIntSimdType(simdType) || SymbolTable::IsSimdMatrixType(simdType))
        return nullptr;
    if(expr.resolvedSimdType.empty() && expr.resolvedUnalignedType.empty()){
        if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs && !SimdExpressionAnalysis::IsZeroScalarLiteral(*expr.lhs, &symbols))
            return expr.lhs;
        return nullptr;
    }
    if(expr.resolvedSimdType != simdType)
        return nullptr;
    if(const Expr* splatArg = SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(expr, symbols)){
        if(splatArg->kind == ExprKind::Unary && !splatArg->isPostfix && splatArg->op == TokenType::Minus && splatArg->lhs && !SimdExpressionAnalysis::IsZeroScalarLiteral(*splatArg->lhs, &symbols))
            return splatArg->lhs;
    }
    if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs && expr.lhs->resolvedSimdType == simdType)
        return expr.lhs;
    if(
        expr.kind == ExprKind::Call
        && expr.lhs
        && expr.lhs->kind == ExprKind::Identifier
        && expr.arguments.size() == 4u
        && symbols.resolveBaseTypeName(expr.lhs->text) == simdType
    ){
        const Expr* negBase = nullptr;
        for(u32 i = 0u; i < 4u; ++i){
            const Expr* curBase = nullptr;
            u32 curLane = 0u;
            if(!SimdExpressionAnalysis::TryMatchNegatedSingleLaneAccess(expr.arguments[i], curBase, curLane) || !curBase || curBase->resolvedSimdType != simdType || curLane != i)
                return nullptr;
            if(!negBase)
                negBase = curBase;
            else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*negBase, *curBase))
                return nullptr;
        }
        return negBase;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

