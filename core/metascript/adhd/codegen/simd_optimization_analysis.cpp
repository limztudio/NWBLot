// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_optimization_analysis.h"
#include "simd_codegen_utils.h"


NWB_ADHD_BEGIN


[[nodiscard]] const Expr* SimdOptimizationAnalysis::TryMatchSimdLengthSqVecArg(const Expr* expr, const b8 isDouble, const SymbolTable& symbols)noexcept{
    if(!expr)
        return nullptr;
    const Expr* candidate = expr->resolvedSimdType.empty() ? expr : SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(*expr, symbols);
    if(!candidate)
        return nullptr;
    if(
        candidate->kind == ExprKind::Call
        && candidate->isBuiltinCall
        && candidate->lhs
        && candidate->lhs->kind == ExprKind::Identifier
        && candidate->lhs->text == "lengthSq"
        && candidate->arguments.size() == 1u
        && candidate->arguments[0]
        && IsRegisterPromotableSimdType(candidate->arguments[0]->resolvedSimdType)
        && IsDoubleSimdType(candidate->arguments[0]->resolvedSimdType) == isDouble
    ){
        return candidate->arguments[0];
    }
    return nullptr;
}

[[nodiscard]] const Expr* SimdOptimizationAnalysis::TryMatchSimdLengthVecArg(const Expr* expr, const b8 isDouble, const SymbolTable& symbols)noexcept{
    if(!expr)
        return nullptr;
    const Expr* candidate = expr->resolvedSimdType.empty() ? expr : SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(*expr, symbols);
    if(!candidate)
        return nullptr;
    if(
        candidate->kind == ExprKind::Call
        && candidate->isBuiltinCall
        && candidate->lhs
        && candidate->lhs->kind == ExprKind::Identifier
        && candidate->arguments.size() == 1u
        && candidate->arguments[0]
    ){
        if(
            candidate->lhs->text == "length"
            && IsRegisterPromotableSimdType(candidate->arguments[0]->resolvedSimdType)
            && IsDoubleSimdType(candidate->arguments[0]->resolvedSimdType) == isDouble
        ){
            return candidate->arguments[0];
        }
        if(candidate->lhs->text == "sqrt")
            return SimdOptimizationAnalysis::TryMatchSimdLengthSqVecArg(candidate->arguments[0], isDouble, symbols);
    }
    return nullptr;
}

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
        if(callee == "rsqrt" && scalarExpr.arguments.size() == 1u && SimdOptimizationAnalysis::TryMatchSimdLengthSqVecArg(scalarExpr.arguments[0], isDouble, symbols))
            return true;
        const Expr* maxBaseVec = nullptr;
        b8 maxIsDouble = false;
        u32 maxLanes = 0u;
        b8 maxIsAbs = false;
        if(callee == "max" && SimdExpressionAnalysis::TryMatchSimdMaxComponentReduction(scalarExpr, maxBaseVec, maxIsDouble, maxLanes, maxIsAbs) && maxIsDouble == isDouble)
            return true;
    }
    if(scalarExpr.kind == ExprKind::Binary && scalarExpr.op == TokenType::Slash && scalarExpr.lhs && scalarExpr.rhs){
        if(SimdConstantAnalysis::IsUnitFloatLiteral(*scalarExpr.lhs, &symbols) && SimdOptimizationAnalysis::TryMatchSimdLengthVecArg(scalarExpr.rhs, isDouble, symbols))
            return true;
    }
    return false;
}

[[nodiscard]] b8 SimdOptimizationAnalysis::WouldSimdMulOperandRequireReplicateInFma(const Expr& operand, const MStringView simdType, const SymbolTable& symbols)noexcept{
    if(IsIntSimdType(simdType) || SymbolTable::IsSimdMatrixType(simdType))
        return false;
    const Expr* scalarExpr = operand.resolvedSimdType.empty() ? &operand : SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(operand, symbols);
    if(!scalarExpr)
        return false;
    if(
        SimdExpressionAnalysis::IsZeroScalarLiteral(*scalarExpr, &symbols)
        || SimdConstantAnalysis::IsUnitFloatLiteral(*scalarExpr, &symbols)
        || SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*scalarExpr, &symbols)
        || SimdConstantAnalysis::IsHalfFloatLiteral(*scalarExpr, &symbols)
    ){
        return false;
    }
    return !SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*scalarExpr, simdType, symbols);
}

[[nodiscard]] b8 SimdOptimizationAnalysis::TryMatchSimdTransform3Arg(
    const Expr& vecExpr,
    const MStringView simdType,
    const SymbolTable& symbols,
    const Expr*& outBaseVec,
    MStringView& outBaseRegType,
    MStringView& outBaseLoadFn,
    u32& outTransformLanes,
    b8& outIsNormal
)noexcept{
    if(vecExpr.kind != ExprKind::Call || !vecExpr.lhs || vecExpr.lhs->kind != ExprKind::Identifier)
        return false;
    const MStringView baseCallee = symbols.resolveBaseTypeName(vecExpr.lhs->text);
    if(baseCallee != simdType)
        return false;
    const b8 isDouble = IsDoubleSimdType(simdType);
    if(vecExpr.arguments.size() == 2u && vecExpr.arguments[0] && vecExpr.arguments[1]){
        MStringView regType;
        MStringView loadFn;
        u32 zeroedAbove = 4u;
        if(
            SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*vecExpr.arguments[0], isDouble, regType, loadFn, zeroedAbove)
            && zeroedAbove >= 3u
            && vecExpr.arguments[1]->resolvedSimdType.empty()
            && vecExpr.arguments[1]->resolvedUnalignedType.empty()
        ){
            if(SimdConstantAnalysis::IsUnitFloatLiteral(*vecExpr.arguments[1], &symbols)){
                outBaseVec = vecExpr.arguments[0];
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = false;
                return true;
            }
            if(SimdExpressionAnalysis::IsZeroScalarLiteral(*vecExpr.arguments[1], &symbols)){
                outBaseVec = vecExpr.arguments[0];
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = true;
                return true;
            }
        }
        return false;
    }
    if(vecExpr.arguments.size() == 3u && vecExpr.arguments[0] && vecExpr.arguments[1] && vecExpr.arguments[2]){
        MStringView regType2;
        MStringView loadFn2;
        u32 zeroedAbove2 = 4u;
        if(
            SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*vecExpr.arguments[0], isDouble, regType2, loadFn2, zeroedAbove2)
            && zeroedAbove2 == 2u
            && vecExpr.arguments[1]->resolvedSimdType.empty()
            && vecExpr.arguments[1]->resolvedUnalignedType.empty()
            && vecExpr.arguments[2]->resolvedSimdType.empty()
            && vecExpr.arguments[2]->resolvedUnalignedType.empty()
            && SimdExpressionAnalysis::IsZeroScalarLiteral(*vecExpr.arguments[1], &symbols)
            && SimdConstantAnalysis::IsUnitFloatLiteral(*vecExpr.arguments[2], &symbols)
        ){
            outBaseVec = vecExpr.arguments[0];
            outBaseRegType = regType2;
            outBaseLoadFn = loadFn2;
            outTransformLanes = 2u;
            outIsNormal = false;
            return true;
        }
    }
    if((vecExpr.arguments.size() == 3u || vecExpr.arguments.size() == 4u) && vecExpr.arguments[0] && vecExpr.arguments[1] && vecExpr.arguments[2]){
        const Expr* baseVec = nullptr;
        for(u32 i = 0u; i < 2u; ++i){
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(!SimdExpressionAnalysis::TryMatchSingleLaneAccess(vecExpr.arguments[i], laneBase, laneIdx) || laneIdx != i || !laneBase)
                return false;
            if(!baseVec)
                baseVec = laneBase;
            else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*baseVec, *laneBase))
                return false;
        }
        MStringView regType;
        MStringView loadFn;
        u32 zeroedAbove = 4u;
        if(!baseVec || !SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*baseVec, isDouble, regType, loadFn, zeroedAbove))
            return false;
        if(
            vecExpr.arguments.size() == 4u
            && vecExpr.arguments[3]
            && zeroedAbove >= 2u
            && vecExpr.arguments[2]->resolvedSimdType.empty()
            && vecExpr.arguments[2]->resolvedUnalignedType.empty()
            && vecExpr.arguments[3]->resolvedSimdType.empty()
            && vecExpr.arguments[3]->resolvedUnalignedType.empty()
            && SimdExpressionAnalysis::IsZeroScalarLiteral(*vecExpr.arguments[2], &symbols)
            && SimdConstantAnalysis::IsUnitFloatLiteral(*vecExpr.arguments[3], &symbols)
        ){
            outBaseVec = baseVec;
            outBaseRegType = regType;
            outBaseLoadFn = loadFn;
            outTransformLanes = 2u;
            outIsNormal = false;
            return true;
        }
        const Expr* lane2Base = nullptr;
        u32 lane2Idx = 0u;
        if(
            zeroedAbove < 3u
            || !SimdExpressionAnalysis::TryMatchSingleLaneAccess(vecExpr.arguments[2], lane2Base, lane2Idx)
            || lane2Idx != 2u
            || !lane2Base
            || !SimdExpressionAnalysis::AreSamePureSimdExpr(*baseVec, *lane2Base)
        ){
            return false;
        }
        if(vecExpr.arguments.size() == 3u){
            outBaseVec = baseVec;
            outBaseRegType = regType;
            outBaseLoadFn = loadFn;
            outTransformLanes = 3u;
            outIsNormal = true;
            return true;
        }
        if(vecExpr.arguments[3] && vecExpr.arguments[3]->resolvedSimdType.empty() && vecExpr.arguments[3]->resolvedUnalignedType.empty()){
            if(SimdConstantAnalysis::IsUnitFloatLiteral(*vecExpr.arguments[3], &symbols)){
                outBaseVec = baseVec;
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = false;
                return true;
            }
            if(SimdExpressionAnalysis::IsZeroScalarLiteral(*vecExpr.arguments[3], &symbols)){
                outBaseVec = baseVec;
                outBaseRegType = regType;
                outBaseLoadFn = loadFn;
                outTransformLanes = 3u;
                outIsNormal = true;
                return true;
            }
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
    const b8 isDouble = IsDoubleSimdType(simdType);
    if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs && expr.lhs->resolvedSimdType == simdType)
        return expr.lhs;
    if(expr.kind == ExprKind::Binary && expr.lhs && expr.rhs){
        if(expr.op == TokenType::Minus && expr.rhs->resolvedSimdType == simdType && SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(*expr.lhs, isDouble, &symbols))
            return expr.rhs;
        if(expr.op == TokenType::Star){
            if(expr.lhs->resolvedSimdType == simdType && SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(*expr.rhs, isDouble, &symbols))
                return expr.lhs;
            if(expr.rhs->resolvedSimdType == simdType && SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(*expr.lhs, isDouble, &symbols))
                return expr.rhs;
        }
        else if(expr.op == TokenType::Slash && expr.lhs->resolvedSimdType == simdType && SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(*expr.rhs, isDouble, &symbols)){
            return expr.lhs;
        }
    }
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

[[nodiscard]] b8 SimdOptimizationAnalysis::TryMatchSimdNegatedMulExpr(
    const Expr* expr,
    const MStringView simdType,
    const Expr*& outA,
    const Expr*& outB,
    const SymbolTable& symbols
)noexcept{
    if(!expr)
        return false;
    if(const Expr* negInner = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*expr, simdType, symbols)){
        if(SimdExpressionAnalysis::IsSimdMulExpr(negInner, outA, outB, &symbols))
            return true;
    }
    const Expr* mulA = nullptr;
    const Expr* mulB = nullptr;
    if(!SimdExpressionAnalysis::IsSimdMulExpr(expr, mulA, mulB, &symbols) || !mulA || !mulB)
        return false;
    const Expr* negA = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulA, simdType, symbols);
    const Expr* negB = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulB, simdType, symbols);
    if(negA && !negB){
        outA = negA;
        outB = mulB;
        return true;
    }
    if(!negA && negB){
        outA = mulA;
        outB = negB;
        return true;
    }
    return false;
}

[[nodiscard]] const Expr* SimdOptimizationAnalysis::TryGetSimdScalarOperand(const Expr& operand, const SymbolTable& symbols)noexcept{
    if(operand.resolvedSimdType.empty() && operand.resolvedUnalignedType.empty())
        return &operand;
    return SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(operand, symbols);
}

[[nodiscard]] const Expr* SimdOptimizationAnalysis::TryMatchSimdSaturateCallArg(
    const Expr& callExpr,
    const MStringView simdType,
    const b8 isDouble,
    const SymbolTable& symbols
)noexcept{
    if(callExpr.kind != ExprKind::Call || !callExpr.isBuiltinCall || !callExpr.lhs || callExpr.lhs->kind != ExprKind::Identifier)
        return nullptr;
    const MStringView callee = callExpr.lhs->text;
    if(callee == "clamp" && callExpr.arguments.size() == 3u && callExpr.arguments[0] && callExpr.arguments[1] && callExpr.arguments[2]){
        if(
            SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(*callExpr.arguments[1], isDouble, &symbols)
            && SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*callExpr.arguments[2], isDouble, &symbols)
        ){
            return callExpr.arguments[0];
        }
        return nullptr;
    }
    const auto matchMinMaxConstArg = [&](const Expr& e, const MStringView expectedName, const b8 wantUnit)noexcept -> const Expr*{
        if(
            e.kind != ExprKind::Call
            || !e.isBuiltinCall
            || !e.lhs
            || e.lhs->kind != ExprKind::Identifier
            || e.lhs->text != expectedName
            || e.arguments.size() != 2u
            || !e.arguments[0]
            || !e.arguments[1]
        ){
            return nullptr;
        }
        const b8 arg1Matches = wantUnit
            ? SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*e.arguments[1], isDouble, &symbols)
            : SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(*e.arguments[1], isDouble, &symbols);
        if(arg1Matches)
            return e.arguments[0];
        const b8 arg0Matches = wantUnit
            ? SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*e.arguments[0], isDouble, &symbols)
            : SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(*e.arguments[0], isDouble, &symbols);
        if(arg0Matches)
            return e.arguments[1];
        return nullptr;
    };
    if(callee == "min"){
        if(const Expr* innerMax = matchMinMaxConstArg(callExpr, "min", true)){
            if(innerMax->resolvedSimdType == simdType)
                return matchMinMaxConstArg(*innerMax, "max", false);
        }
    }
    else if(callee == "max"){
        if(const Expr* innerMin = matchMinMaxConstArg(callExpr, "max", false)){
            if(innerMin->resolvedSimdType == simdType)
                return matchMinMaxConstArg(*innerMin, "min", true);
        }
    }
    return nullptr;
}

[[nodiscard]] const Expr* SimdOptimizationAnalysis::TryGetTransposeSourceMatrix(
    const Expr& matExpr,
    const Span<const Stmt* const> localVarDecls
)noexcept{
    const Expr* callExpr = &matExpr;
    if(matExpr.kind == ExprKind::Identifier){
        const Stmt* foundDecl = SimdConstantAnalysis::FindLocalConstVarDecl(matExpr.text, localVarDecls);
        if(!foundDecl)
            return nullptr;
        if(foundDecl->isCtorInit && foundDecl->arguments.size() == 1u && foundDecl->arguments[0])
            callExpr = foundDecl->arguments[0];
        else if(!foundDecl->isCtorInit && foundDecl->expr)
            callExpr = foundDecl->expr;
        else
            return nullptr;
    }
    if(
        callExpr->kind == ExprKind::Call
        && callExpr->isBuiltinCall
        && callExpr->lhs
        && callExpr->lhs->kind == ExprKind::Identifier
        && callExpr->lhs->text == "transpose"
        && callExpr->arguments.size() == 1u
        && callExpr->arguments[0]
    ){
        const Expr* srcMat = callExpr->arguments[0];
        if(
            SymbolTable::IsSimdRegisterMatrixType(srcMat->resolvedSimdType)
            && SimdExpressionAnalysis::IsPureSimdAddressExpr(*srcMat)
            && (srcMat->kind == ExprKind::Identifier || srcMat->kind == ExprKind::ThisMember || srcMat->kind == ExprKind::MemberAccess || srcMat->kind == ExprKind::Index)
        ){
            return srcMat;
        }
    }
    return nullptr;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

