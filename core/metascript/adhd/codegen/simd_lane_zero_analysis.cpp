// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_lane_zero_analysis.h"
#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_simd_lane_zero_analysis{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u32 InferSimdRegZeroedAboveLaneImpl(
    const Expr& expr,
    b8 isDouble,
    const SymbolTable* symbols,
    Span<const Stmt* const> localVarDecls,
    u32 depth
)noexcept;

[[nodiscard]] u32 InferCtorArgsZeroedAboveLane(
    const Span<const Expr* const> arguments,
    const b8 isDouble,
    const SymbolTable* const symbols,
    const Span<const Stmt* const> localVarDecls,
    const u32 depth
)noexcept{
    if(arguments.empty())
        return 2u;
    b8 allZero = true;
    for(const Expr* arg : arguments){
        if(!arg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, symbols)){
            allZero = false;
            break;
        }
    }
    if(allZero)
        return 2u;
    if(arguments.size() == 1u && arguments[0]){
        MStringView regType;
        MStringView loadFn;
        u32 zeroedAbove = 4u;
        if(SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*arguments[0], isDouble, regType, loadFn, zeroedAbove)){
            if(!regType.empty())
                return InferSimdRegZeroedAboveLaneImpl(*arguments[0], isDouble, symbols, localVarDecls, depth + 1u);
            if(zeroedAbove >= 2u && zeroedAbove <= 3u)
                return zeroedAbove;
        }
        if(SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && !IsIntSimdType(arguments[0]->resolvedSimdType))
            return InferSimdRegZeroedAboveLaneImpl(*arguments[0], IsDoubleSimdType(arguments[0]->resolvedSimdType), symbols, localVarDecls, depth + 1u);
        return 4u;
    }
    if(arguments.size() == 2u && arguments[0] && arguments[1] && arguments[1]->resolvedSimdType.empty() && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[1], symbols)){
        MStringView regType;
        MStringView loadFn;
        u32 subZeroed = 4u;
        if(SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*arguments[0], isDouble, regType, loadFn, subZeroed)){
            if(!regType.empty())
                subZeroed = InferSimdRegZeroedAboveLaneImpl(*arguments[0], isDouble, symbols, localVarDecls, depth + 1u);
            if(subZeroed == 2u)
                return 2u;
        }
        return 3u;
    }
    if(arguments.size() == 3u && arguments[0] && arguments[1] && arguments[2]){
        if(!arguments[0]->resolvedSimdType.empty() || !arguments[0]->resolvedUnalignedType.empty()){
            if(arguments[2]->resolvedSimdType.empty() && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[2], symbols))
                return (arguments[1]->resolvedSimdType.empty() && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[1], symbols)) ? 2u : 3u;
            return 4u;
        }
        if(arguments[2]->resolvedSimdType.empty() && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[2], symbols))
            return 2u;
        return 3u;
    }
    if(arguments.size() == 4u && arguments[0] && arguments[1] && arguments[2] && arguments[3]){
        if(arguments[3]->resolvedSimdType.empty() && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[3], symbols)){
            if(arguments[2]->resolvedSimdType.empty() && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[2], symbols))
                return 2u;
            return 3u;
        }
    }
    return 4u;
}

[[nodiscard]] b8 IsNonNegativeFiniteFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = SimdConstantAnalysis::UnwrapConstScalarExpr(expr, symbols);
    return (unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral)
        && !unwrapped.text.empty()
        && unwrapped.text.front() != '-';
}

[[nodiscard]] b8 IsPositiveFiniteFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    return IsNonNegativeFiniteFloatLiteral(expr, symbols) && !SimdExpressionAnalysis::IsZeroScalarLiteral(expr, symbols);
}

[[nodiscard]] b8 HasFloatConstantTailLanes(
    const Expr& expr,
    const b8 isDouble,
    const u32 fromLane,
    const b8 allowZero,
    const SymbolTable* const symbols,
    const Span<const Stmt* const> localVarDecls
)noexcept{
    const auto matchesScalar = [&](const Expr& scalarExpr)noexcept -> b8{
        return allowZero
            ? IsNonNegativeFiniteFloatLiteral(scalarExpr, symbols)
            : IsPositiveFiniteFloatLiteral(scalarExpr, symbols);
    };
    if(expr.resolvedSimdType.empty() && expr.resolvedUnalignedType.empty())
        return matchesScalar(expr);
    if(SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(expr, isDouble, symbols) || (allowZero && SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(expr, isDouble, symbols)))
        return true;
    if(symbols){
        if(const Expr* splatScalar = SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(expr, *symbols))
            return matchesScalar(*splatScalar);
    }
    const MStringView expectedSimd = isDouble ? "VectorDouble" : "VectorSingle";
    Span<const Expr* const> ctorArgs;
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = symbols ? symbols->resolveBaseTypeName(expr.lhs->text) : expr.lhs->text;
        if(baseCallee == expectedSimd)
            ctorArgs = Span<const Expr* const>(expr.arguments.data(), expr.arguments.size());
    }
    else if(expr.kind == ExprKind::Identifier){
        if(const Stmt* localConst = SimdConstantAnalysis::FindLocalConstVarDecl(expr.text, localVarDecls))
            ctorArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*localConst);
    }
    if(ctorArgs.size() == 4u || (allowZero && ctorArgs.size() == 3u)){
        for(u32 i = fromLane; i < static_cast<u32>(ctorArgs.size()); ++i){
            if(!ctorArgs[i] || !matchesScalar(*ctorArgs[i]))
                return false;
        }
        return true;
    }
    return false;
}

[[nodiscard]] u32 InferSimdRegZeroedAboveLaneImpl(
    const Expr& expr,
    const b8 isDouble,
    const SymbolTable* const symbols,
    const Span<const Stmt* const> localVarDecls,
    const u32 depth
)noexcept{
    if(depth > 6u)
        return 4u;
    const MStringView expectedSimd = isDouble ? "VectorDouble" : "VectorSingle";
    if(expr.resolvedSimdType != expectedSimd && !(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier && (symbols ? symbols->resolveBaseTypeName(expr.lhs->text) : expr.lhs->text) == expectedSimd))
        return 4u;

    if(expr.kind == ExprKind::Identifier){
        if(const Stmt* localConst = SimdConstantAnalysis::FindLocalConstVarDecl(expr.text, localVarDecls)){
            if(localConst->isCtorInit)
                return InferCtorArgsZeroedAboveLane(localConst->arguments, isDouble, symbols, localVarDecls, depth + 1u);
            if(localConst->expr)
                return InferSimdRegZeroedAboveLaneImpl(*localConst->expr, isDouble, symbols, localVarDecls, depth + 1u);
        }
        else if(symbols){
            if(const Stmt* globalConst = symbols->findGlobalConstant(expr.text)){
                const Span<const Expr* const> constArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*globalConst);
                if(!constArgs.empty())
                    return InferCtorArgsZeroedAboveLane(constArgs, isDouble, symbols, {}, depth + 1u);
            }
        }
        return 4u;
    }

    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr.lhs->text;
        const MStringView baseCallee = symbols ? symbols->resolveBaseTypeName(callee) : callee;
        if(baseCallee == expectedSimd)
            return InferCtorArgsZeroedAboveLane(expr.arguments, isDouble, symbols, localVarDecls, depth);
        if(expr.isBuiltinCall){
            if(callee == "Normalize" && expr.arguments.size() == 1u && expr.arguments[0]){
                const u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[0], symbols);
                if(effLanes >= 2u && effLanes <= 3u)
                    return effLanes;
                return 4u;
            }
            if(callee == "unfoldOctahedralAxis" && expr.arguments.size() == 1u && expr.arguments[0])
                return 3u;
            if(callee == "Cross" && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const u32 eff0 = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[0], symbols);
                const u32 eff1 = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[1], symbols);
                const u32 effLanes = eff0 > eff1 ? eff0 : eff1;
                if(effLanes >= 3u)
                    return 3u;
                return 4u;
            }
            if(
                (
                    callee == "Abs"
                    || callee == "Sqrt"
                    || callee == "Floor"
                    || callee == "Ceil"
                    || callee == "Round"
                    || callee == "trunc"
                    || callee == "frac"
                    || callee == "Saturate"
                    || callee == "Sin"
                    || callee == "Tan"
                    || callee == "ASin"
                    || callee == "ATan"
                    || callee == "FoldOctahedralAxis"
                )
                && expr.arguments.size() == 1u
                && expr.arguments[0]
            ){
                return InferSimdRegZeroedAboveLaneImpl(*expr.arguments[0], isDouble, symbols, localVarDecls, depth + 1u);
            }
            if((callee == "Min" || callee == "Max") && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const u32 z0 = InferSimdRegZeroedAboveLaneImpl(*expr.arguments[0], isDouble, symbols, localVarDecls, depth + 1u);
                const u32 z1 = InferSimdRegZeroedAboveLaneImpl(*expr.arguments[1], isDouble, symbols, localVarDecls, depth + 1u);
                return z0 > z1 ? z0 : z1;
            }
            if(callee == "Clamp" && expr.arguments.size() == 3u && expr.arguments[0] && expr.arguments[1] && expr.arguments[2]){
                if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(*expr.arguments[1], isDouble, symbols) && SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*expr.arguments[2], isDouble, symbols))
                    return InferSimdRegZeroedAboveLaneImpl(*expr.arguments[0], isDouble, symbols, localVarDecls, depth + 1u);
            }
        }
        return 4u;
    }

    if(expr.kind == ExprKind::Binary && expr.lhs && expr.rhs){
        if(expr.op == TokenType::Plus || expr.op == TokenType::Minus){
            const u32 z0 = InferSimdRegZeroedAboveLaneImpl(*expr.lhs, isDouble, symbols, localVarDecls, depth + 1u);
            const u32 z1 = InferSimdRegZeroedAboveLaneImpl(*expr.rhs, isDouble, symbols, localVarDecls, depth + 1u);
            return z0 > z1 ? z0 : z1;
        }
        if(expr.op == TokenType::Star){
            const u32 z0 = InferSimdRegZeroedAboveLaneImpl(*expr.lhs, isDouble, symbols, localVarDecls, depth + 1u);
            const u32 z1 = InferSimdRegZeroedAboveLaneImpl(*expr.rhs, isDouble, symbols, localVarDecls, depth + 1u);
            if(z0 <= 3u && z1 <= 3u)
                return z0 > z1 ? z0 : z1;
            if(z0 <= 3u && HasFloatConstantTailLanes(*expr.rhs, isDouble, z0, true, symbols, localVarDecls))
                return z0;
            if(z1 <= 3u && HasFloatConstantTailLanes(*expr.lhs, isDouble, z1, true, symbols, localVarDecls))
                return z1;
        }
        if(expr.op == TokenType::Slash){
            const u32 z0 = InferSimdRegZeroedAboveLaneImpl(*expr.lhs, isDouble, symbols, localVarDecls, depth + 1u);
            if(z0 <= 3u && HasFloatConstantTailLanes(*expr.rhs, isDouble, z0, false, symbols, localVarDecls))
                return z0;
        }
    }

    return 4u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u32 SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(
    const Expr& expr,
    const b8 isDouble,
    const SymbolTable* const symbols,
    const Span<const Stmt* const> localVarDecls
)noexcept{
    return __hidden_simd_lane_zero_analysis::InferSimdRegZeroedAboveLaneImpl(expr, isDouble, symbols, localVarDecls, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

