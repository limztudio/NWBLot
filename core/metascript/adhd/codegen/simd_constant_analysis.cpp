// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_constant_analysis.h"
#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_simd_constant_analysis{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsIdentityMatrixRowCtorExpr(
    const Expr& expr,
    const u32 rowIndex,
    const SymbolTable& symbols,
    const Span<const Stmt* const> localVarDecls = {}
)noexcept{
    Span<const Expr* const> rowArgs;
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = symbols.resolveBaseTypeName(expr.lhs->text);
        if(!SymbolTable::IsSimdRegisterVectorType(baseCallee))
            return false;
        rowArgs = Span<const Expr* const>(expr.arguments.data(), expr.arguments.size());
    }
    else if(expr.kind == ExprKind::Identifier){
        if(const Stmt* localConst = SimdConstantAnalysis::FindLocalConstVarDecl(expr.text, localVarDecls))
            rowArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*localConst);
        else if(const Stmt* globalConst = symbols.findGlobalConstant(expr.text))
            rowArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*globalConst);
    }
    const usize argCount = rowArgs.size();
    if(argCount != 3u && argCount != 4u)
        return false;
    if(rowIndex >= argCount)
        return false;
    for(usize i = 0u; i < argCount; ++i){
        const Expr* arg = rowArgs[i];
        if(!arg)
            return false;
        if(i == rowIndex){
            if(!SimdConstantAnalysis::IsUnitFloatLiteral(*arg, &symbols))
                return false;
        }
        else if(!SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &symbols)){
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Span<const Expr* const> SimdConstantAnalysis::ExtractGlobalConstCtorArgs(const Stmt& globalConst)noexcept{
    if(globalConst.isCtorInit && !globalConst.arguments.empty())
        return Span<const Expr* const>(globalConst.arguments.data(), globalConst.arguments.size());
    if(globalConst.expr && globalConst.expr->kind == ExprKind::Call && !globalConst.expr->arguments.empty())
        return Span<const Expr* const>(globalConst.expr->arguments.data(), globalConst.expr->arguments.size());
    return {};
}

[[nodiscard]] const Stmt* SimdConstantAnalysis::FindLocalConstVarDecl(const MStringView name, const Span<const Stmt* const> localVarDecls)noexcept{
    for(usize i = localVarDecls.size(); i > 0u; --i){
        const Stmt* decl = localVarDecls[i - 1u];
        if(!decl)
            continue;
        if(decl->kind == StmtKind::ForIndex || decl->kind == StmtKind::ParallelForIndex){
            if(decl->initStmt && decl->initStmt->name == name)
                return nullptr;
            continue;
        }
        if(decl->name == name)
            return (decl->kind == StmtKind::VarDecl && decl->isConst) ? decl : nullptr;
    }
    return nullptr;
}

[[nodiscard]] u32 SimdConstantAnalysis::InferEffectiveSimdLanes(const Expr& expr, const SymbolTable* const symbols)noexcept{
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle && expr.text.size() >= 2u && expr.text.size() <= 4u)
        return static_cast<u32>(expr.text.size());
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = symbols ? symbols->resolveBaseTypeName(expr.lhs->text) : expr.lhs->text;
        if(SymbolTable::IsSimdRegisterVectorType(baseCallee) && !IsIntSimdType(baseCallee)){
            const u32 zeroedAbove = SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(expr, IsDoubleSimdType(baseCallee), symbols);
            if(zeroedAbove >= 2u && zeroedAbove <= 3u)
                return zeroedAbove;
        }
    }
    u32 lanes = 4u;
    if(SymbolTable::IsSimdVectorType(expr.resolvedSimdType, lanes))
        return lanes;
    return 4u;
}

[[nodiscard]] const Expr& SimdConstantAnalysis::UnwrapConstScalarExpr(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr* cur = &expr;
    if(!symbols)
        return *cur;
    for(u32 depth = 0u; depth < 4u; ++depth){
        if(cur->kind != ExprKind::Identifier || !cur->resolvedSimdType.empty() || !cur->resolvedUnalignedType.empty())
            break;
        const Stmt* globalConst = symbols->findGlobalConstant(cur->text);
        if(!globalConst)
            break;
        if(globalConst->expr){
            cur = globalConst->expr;
        }
        else if(globalConst->isCtorInit && globalConst->arguments.size() == 1u && globalConst->arguments[0]){
            cur = globalConst->arguments[0];
        }
        else{
            break;
        }
    }
    return *cur;
}

[[nodiscard]] b8 SimdConstantAnalysis::IsUnitFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = SimdConstantAnalysis::UnwrapConstScalarExpr(expr, symbols);
    return (unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral)
        && (unwrapped.text == "1.0f" || unwrapped.text == "1.0" || unwrapped.text == "1.f" || unwrapped.text == "1.0F" || unwrapped.text == "1.F");
}

[[nodiscard]] b8 SimdConstantAnalysis::IsNegativeUnitFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = SimdConstantAnalysis::UnwrapConstScalarExpr(expr, symbols);
    if(unwrapped.kind == ExprKind::Unary && !unwrapped.isPostfix && unwrapped.op == TokenType::Minus && unwrapped.lhs)
        return SimdConstantAnalysis::IsUnitFloatLiteral(*unwrapped.lhs, symbols);
    return (unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral)
        && (unwrapped.text == "-1.0f" || unwrapped.text == "-1.0" || unwrapped.text == "-1.f" || unwrapped.text == "-1.0F" || unwrapped.text == "-1.F");
}

[[nodiscard]] b8 SimdConstantAnalysis::IsHalfFloatLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = SimdConstantAnalysis::UnwrapConstScalarExpr(expr, symbols);
    return (unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral)
        && (unwrapped.text == "0.5f" || unwrapped.text == "0.5" || unwrapped.text == ".5f" || unwrapped.text == ".5" || unwrapped.text == "0.5F" || unwrapped.text == ".5F");
}

[[nodiscard]] MStringView SimdConstantAnalysis::TryMatchKnownSimdFloatVectorConstant(const Span<const Expr* const> arguments, const b8 isDouble, const SymbolTable* const symbols)noexcept{
    if(arguments.size() == 1u && arguments[0]){
        const Expr& a0 = *arguments[0];
        if(a0.resolvedSimdType.empty() && a0.resolvedUnalignedType.empty()){
            if(SimdConstantAnalysis::IsUnitFloatLiteral(a0, symbols))
                return isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne";
            if(SimdConstantAnalysis::IsNegativeUnitFloatLiteral(a0, symbols))
                return isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne";
            if(SimdConstantAnalysis::IsHalfFloatLiteral(a0, symbols))
                return isDouble ? "::s_DoubleSIMDOneHalf" : "::s_SIMDOneHalf";
            return {};
        }
        if(symbols && a0.kind == ExprKind::Identifier){
            if(const Stmt* globalConst = symbols->findGlobalConstant(a0.text)){
                const Span<const Expr* const> constArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*globalConst);
                if(!constArgs.empty()){
                    b8 allZero = true;
                    for(const Expr* cArg : constArgs){
                        if(!cArg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*cArg, symbols)){
                            allZero = false;
                            break;
                        }
                    }
                    if(allZero)
                        return isDouble ? "::VectorDoubleZero()" : "::VectorZero()";
                    return SimdConstantAnalysis::TryMatchKnownSimdFloatVectorConstant(constArgs, isDouble, symbols);
                }
            }
        }
        return {};
    }
    if(arguments.size() != 3u && arguments.size() != 4u)
        return {};
    for(const Expr* arg : arguments){
        if(!arg)
            return {};
    }
    const b8 hasW = (arguments.size() == 4u);
    const b8 u0 = SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[0], symbols);
    const b8 u1 = SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[1], symbols);
    const b8 u2 = SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[2], symbols);
    const b8 u3 = hasW && SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[3], symbols);
    const b8 z0 = SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[0], symbols);
    const b8 z1 = SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[1], symbols);
    const b8 z2 = SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[2], symbols);
    const b8 z3 = !hasW || SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[3], symbols);

    if(u0 && u1 && u2 && u3)
        return isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne";
    if(u0 && u1 && u2 && z3)
        return isDouble ? "::s_DoubleSIMDOne3" : "::s_SIMDOne3";
    if(u0 && z1 && z2 && z3)
        return isDouble ? "::s_DoubleSIMDIdentityR0" : "::s_SIMDIdentityR0";
    if(z0 && u1 && z2 && z3)
        return isDouble ? "::s_DoubleSIMDIdentityR1" : "::s_SIMDIdentityR1";
    if(z0 && z1 && u2 && z3)
        return isDouble ? "::s_DoubleSIMDIdentityR2" : "::s_SIMDIdentityR2";
    if(hasW && z0 && z1 && z2 && u3)
        return isDouble ? "::s_DoubleSIMDIdentityR3" : "::s_SIMDIdentityR3";

    if(
        hasW
        && SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*arguments[0], symbols)
        && SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*arguments[1], symbols)
        && SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*arguments[2], symbols)
        && SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*arguments[3], symbols)
    ){
        return isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne";
    }
    if(
        hasW
        && SimdConstantAnalysis::IsHalfFloatLiteral(*arguments[0], symbols)
        && SimdConstantAnalysis::IsHalfFloatLiteral(*arguments[1], symbols)
        && SimdConstantAnalysis::IsHalfFloatLiteral(*arguments[2], symbols)
        && SimdConstantAnalysis::IsHalfFloatLiteral(*arguments[3], symbols)
    ){
        return isDouble ? "::s_DoubleSIMDOneHalf" : "::s_SIMDOneHalf";
    }
    return {};
}

[[nodiscard]] b8 SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(const Expr& expr, const b8 isDouble, const SymbolTable* const symbols)noexcept{
    if(expr.resolvedSimdType.empty() && expr.resolvedUnalignedType.empty())
        return SimdExpressionAnalysis::IsZeroScalarLiteral(expr, symbols);
    if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs)
        return SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(*expr.lhs, isDouble, symbols);
    if(symbols){
        if(const Expr* splatScalar = SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(expr, *symbols))
            return SimdExpressionAnalysis::IsZeroScalarLiteral(*splatScalar, symbols);
    }
    const MStringView expectedSimd = isDouble ? "VectorDouble" : "VectorSingle";
    if(expr.resolvedSimdType != expectedSimd)
        return false;
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = symbols ? symbols->resolveBaseTypeName(expr.lhs->text) : expr.lhs->text;
        if(baseCallee == expectedSimd){
            if(expr.arguments.empty())
                return true;
            for(const Expr* arg : expr.arguments){
                if(!arg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, symbols))
                    return false;
            }
            return true;
        }
    }
    if(symbols && expr.kind == ExprKind::Identifier){
        if(const Stmt* globalConst = symbols->findGlobalConstant(expr.text)){
            const Span<const Expr* const> constArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*globalConst);
            if(!constArgs.empty()){
                for(const Expr* cArg : constArgs){
                    if(!cArg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*cArg, symbols))
                        return false;
                }
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] b8 SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(const Expr& expr, const b8 isDouble, const SymbolTable* const symbols)noexcept{
    if(expr.resolvedSimdType.empty() && expr.resolvedUnalignedType.empty())
        return SimdConstantAnalysis::IsUnitFloatLiteral(expr, symbols);
    if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs)
        return SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(*expr.lhs, isDouble, symbols);
    if(symbols){
        if(const Expr* splatScalar = SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(expr, *symbols))
            return SimdConstantAnalysis::IsUnitFloatLiteral(*splatScalar, symbols);
    }
    const MStringView expectedSimd = isDouble ? "VectorDouble" : "VectorSingle";
    if(expr.resolvedSimdType != expectedSimd)
        return false;
    const MStringView expectedOneConst = isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne";
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = symbols ? symbols->resolveBaseTypeName(expr.lhs->text) : expr.lhs->text;
        if(baseCallee == expectedSimd)
            return SimdConstantAnalysis::TryMatchKnownSimdFloatVectorConstant(expr.arguments, isDouble, symbols) == expectedOneConst;
    }
    if(symbols && expr.kind == ExprKind::Identifier){
        if(const Stmt* globalConst = symbols->findGlobalConstant(expr.text)){
            const Span<const Expr* const> constArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*globalConst);
            if(!constArgs.empty())
                return SimdConstantAnalysis::TryMatchKnownSimdFloatVectorConstant(constArgs, isDouble, symbols) == expectedOneConst;
        }
    }
    return false;
}

[[nodiscard]] b8 SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(const Expr& expr, const b8 isDouble, const SymbolTable* const symbols)noexcept{
    if(expr.resolvedSimdType.empty() && expr.resolvedUnalignedType.empty())
        return SimdConstantAnalysis::IsNegativeUnitFloatLiteral(expr, symbols);
    if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs)
        return SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*expr.lhs, isDouble, symbols);
    if(symbols){
        if(const Expr* splatScalar = SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(expr, *symbols))
            return SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*splatScalar, symbols);
    }
    const MStringView expectedSimd = isDouble ? "VectorDouble" : "VectorSingle";
    if(expr.resolvedSimdType != expectedSimd)
        return false;
    const MStringView expectedNegOneConst = isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne";
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = symbols ? symbols->resolveBaseTypeName(expr.lhs->text) : expr.lhs->text;
        if(baseCallee == expectedSimd)
            return SimdConstantAnalysis::TryMatchKnownSimdFloatVectorConstant(expr.arguments, isDouble, symbols) == expectedNegOneConst;
    }
    if(symbols && expr.kind == ExprKind::Identifier){
        if(const Stmt* globalConst = symbols->findGlobalConstant(expr.text)){
            const Span<const Expr* const> constArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*globalConst);
            if(!constArgs.empty())
                return SimdConstantAnalysis::TryMatchKnownSimdFloatVectorConstant(constArgs, isDouble, symbols) == expectedNegOneConst;
        }
    }
    return false;
}

[[nodiscard]] b8 SimdConstantAnalysis::IsIdentityMatrixCtorArgs(const Span<const Expr* const> arguments, const SymbolTable& symbols)noexcept{
    const usize count = arguments.size();
    if(count == 1u && arguments[0] && arguments[0]->kind == ExprKind::Identifier){
        if(const Stmt* globalConst = symbols.findGlobalConstant(arguments[0]->text)){
            const Span<const Expr* const> constArgs = SimdConstantAnalysis::ExtractGlobalConstCtorArgs(*globalConst);
            if(!constArgs.empty())
                return SimdConstantAnalysis::IsIdentityMatrixCtorArgs(constArgs, symbols);
        }
        return false;
    }
    if(count == 3u || count == 4u){
        for(usize r = 0u; r < count; ++r){
            if(!arguments[r] || !__hidden_simd_constant_analysis::IsIdentityMatrixRowCtorExpr(*arguments[r], static_cast<u32>(r), symbols))
                return false;
        }
        return true;
    }
    if(count == 9u || count == 12u || count == 16u){
        const usize stride = (count == 9u) ? 4u : 5u;
        for(usize i = 0u; i < count; ++i){
            if(!arguments[i])
                return false;
            const b8 isDiag = (i % stride == 0u);
            if(isDiag){
                if(!SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[i], &symbols))
                    return false;
            }
            else if(!SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &symbols)){
                return false;
            }
        }
        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

