// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"
#include "simd_binary_emitter.h"
#include "simd_call_emitter.h"
#include "simd_emission_fragments.h"
#include "simd_optimization_analysis.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitSimdLaneGetExpr(MString& out, const Expr& vecExpr, const u32 lane, const Expr* dynamicIndexExpr)const{
    const Expr* curVec = &vecExpr;
    u32 resolvedLane = lane;
    if(!dynamicIndexExpr){
        while(
            curVec->kind == ExprKind::MemberAccess
            && curVec->isSwizzle
            && curVec->lhs
            && curVec->lhs->resolvedSimdType == curVec->resolvedSimdType
            && resolvedLane < curVec->text.size()
        ){
            resolvedLane = MapSwizzleCharToLane(curVec->text[resolvedLane]);
            curVec = curVec->lhs;
        }
    }
    const MStringView lhsSimd = curVec->resolvedSimdType;
    const b8 isDouble = IsDoubleSimdType(lhsSimd);
    const b8 isInt = IsIntSimdType(lhsSimd);
    const b8 isSignedInt = IsSignedIntSimdType(lhsSimd);

    if(isSignedInt)
        out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
    if(isInt)
        out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
    else
        out.append(isDouble ? "::VectorDoubleGet" : "::VectorGet");
    if(dynamicIndexExpr){
        out.append("ByIndex(");
        emitSimdRegExpr(out, *curVec, lhsSimd);
        out.append(", static_cast<usize>(");
        emitExpr(out, *dynamicIndexExpr);
        out.append("))");
    }
    else{
        const MStringView laneSuffix = SimdLaneCallSuffix(resolvedLane);
        out.append(laneSuffix.data(), laneSuffix.size());
        emitSimdRegExpr(out, *curVec, lhsSimd);
        out.push_back(')');
    }
    if(isSignedInt)
        out.push_back(')');
}


void SourceEmitter::emitSimdRegExpr(MString& out, const Expr& expr, const MStringView expectedSimdType)const{

    const MStringView simdType = !expr.resolvedSimdType.empty() ? expr.resolvedSimdType : expectedSimdType;
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);
    const b8 isMatrix = SymbolTable::IsSimdMatrixType(simdType);

    if(expr.resolvedSimdType.empty() && !expectedSimdType.empty()){
        MStringView storageLoadFn;
        u32 storageLanes = 0u;
        if(SimdVectorSourceAnalysis::TryGetStorageVectorLoadInfo(expr, isDouble, storageLoadFn, storageLanes)){
            out.append(storageLoadFn.data(), storageLoadFn.size());
            out.push_back('(');
            emitExpr(out, expr);
            out.push_back(')');
            return;
        }
        if(!isInt && !isMatrix){
            if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs && SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*expr.lhs, expectedSimdType, m_symbols)){
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                emitSimdRegExpr(out, *expr.lhs, expectedSimdType);
                out.push_back(')');
                return;
            }
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(SimdExpressionAnalysis::TryMatchSingleLaneAccess(&expr, laneBase, laneIdx) && laneBase && laneBase->resolvedSimdType == expectedSimdType){
                const MStringView splatPrefix = SimdSplatIntrinsic(laneIdx, isDouble);
                out.append(splatPrefix.data(), splatPrefix.size());
                emitSimdRegExpr(out, *laneBase, expectedSimdType);
                out.push_back(')');
                return;
            }
            if(expr.kind == ExprKind::Call && expr.isBuiltinCall && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
                const MStringView callee = expr.lhs->text;
                const MStringView scalarReductionSuffix = MapSimdScalarReductionSuffix(callee);
                if(!scalarReductionSuffix.empty() && !expr.arguments.empty() && expr.arguments[0] && IsRegisterPromotableSimdType(expr.arguments[0]->resolvedSimdType)){
                    const MStringView argSimd = expr.arguments[0]->resolvedSimdType;
                    if(IsDoubleSimdType(argSimd) == isDouble){
                        u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[0], &m_symbols);
                        if(expr.arguments.size() == 2u && expr.arguments[1]){
                            const u32 effLanes1 = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[1], &m_symbols);
                            if(effLanes1 < effLanes)
                                effLanes = effLanes1;
                        }
                        if(callee == "Dot" && effLanes == 4u && !IsIntSimdType(argSimd) && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                            const Expr* sumArg = SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*expr.arguments[1], isDouble, &m_symbols)
                                ? expr.arguments[0]
                                : (SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*expr.arguments[0], isDouble, &m_symbols) ? expr.arguments[1] : nullptr);
                            if(sumArg){
                                out.append(isDouble ? "::VectorDoubleSum(" : "::VectorSum(");
                                emitSimdRegExpr(out, *sumArg, argSimd);
                                out.push_back(')');
                                return;
                            }
                        }
                        AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, scalarReductionSuffix);
                        for(usize i = 0u; i < expr.arguments.size(); ++i){
                            if(i > 0u)
                                out.append(", ");
                            if(expr.arguments[i])
                                emitSimdRegExpr(out, *expr.arguments[i], argSimd);
                        }
                        out.push_back(')');
                        return;
                    }
                }
                if(callee == "Rsqrt" && expr.arguments.size() == 1u){
                    if(const Expr* lenSqVec = SimdOptimizationAnalysis::TryMatchSimdLengthSqVecArg(expr.arguments[0], isDouble, m_symbols)){
                        const u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(*lenSqVec, &m_symbols);
                        AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "ReciprocalLength");
                        emitSimdRegExpr(out, *lenSqVec, lenSqVec->resolvedSimdType);
                        out.push_back(')');
                        return;
                    }
                }
                const Expr* maxBaseVec = nullptr;
                b8 maxIsDouble = false;
                u32 maxLanes = 0u;
                b8 maxIsAbs = false;
                if(callee == "Max" && SimdExpressionAnalysis::TryMatchSimdMaxComponentReduction(expr, maxBaseVec, maxIsDouble, maxLanes, maxIsAbs) && maxIsDouble == isDouble && maxBaseVec){
                    AppendSimdLaneCountIntrinsicPrefix(out, isDouble, maxLanes, "MaxComponent");
                    if(maxIsAbs)
                        out.append(isDouble ? "::VectorDoubleAbs(" : "::VectorAbs(");
                    emitSimdRegExpr(out, *maxBaseVec, isDouble ? "VectorDouble" : "VectorSingle");
                    if(maxIsAbs)
                        out.push_back(')');
                    out.push_back(')');
                    return;
                }
            }
            if(expr.kind == ExprKind::Binary && expr.op == TokenType::Slash && expr.lhs && expr.rhs && SimdConstantAnalysis::IsUnitFloatLiteral(*expr.lhs, &m_symbols)){
                if(const Expr* lenVec = SimdOptimizationAnalysis::TryMatchSimdLengthVecArg(expr.rhs, isDouble, m_symbols)){
                    const u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(*lenVec, &m_symbols);
                    AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "ReciprocalLength");
                    emitSimdRegExpr(out, *lenVec, lenVec->resolvedSimdType);
                    out.push_back(')');
                    return;
                }
            }
            if(SimdExpressionAnalysis::IsZeroScalarLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
                return;
            }
            if(SimdConstantAnalysis::IsUnitFloatLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne");
                return;
            }
            if(SimdConstantAnalysis::IsNegativeUnitFloatLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne");
                return;
            }
            if(SimdConstantAnalysis::IsHalfFloatLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::s_DoubleSIMDOneHalf" : "::s_SIMDOneHalf");
                return;
            }
        }
        if(isInt){
            if(SimdExpressionAnalysis::IsZeroScalarLiteral(expr, &m_symbols)){
                out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
                return;
            }
            out.append(isDouble ? "::VectorDoubleReplicateInt(static_cast<u64>(" : "::VectorReplicateInt(static_cast<u32>(");
            emitExpr(out, expr);
            out.append("))");
        }
        else{
            out.append(isDouble ? "::VectorDoubleReplicate(" : "::VectorReplicate(");
            emitExpr(out, expr);
            out.push_back(')');
        }
        return;
    }

    switch(expr.kind){
    case ExprKind::MemberAccess:{
        if(expr.isSwizzle && expr.lhs){
            const u32 len = static_cast<u32>(expr.text.size());
            Array<u32, 4> lanes{
                len > 0u ? MapSwizzleCharToLane(expr.text[0]) : 0u,
                len > 1u ? MapSwizzleCharToLane(expr.text[1]) : 1u,
                len > 2u ? MapSwizzleCharToLane(expr.text[2]) : 2u,
                len > 3u ? MapSwizzleCharToLane(expr.text[3]) : 3u,
            };
            const Expr* curBase = expr.lhs;
            while(curBase && curBase->kind == ExprKind::MemberAccess && curBase->isSwizzle && curBase->lhs && curBase->lhs->resolvedSimdType == simdType){
                const u32 parentLen = static_cast<u32>(curBase->text.size());
                const Array<u32, 4> parentLanes{
                    parentLen > 0u ? MapSwizzleCharToLane(curBase->text[0]) : 0u,
                    parentLen > 1u ? MapSwizzleCharToLane(curBase->text[1]) : 1u,
                    parentLen > 2u ? MapSwizzleCharToLane(curBase->text[2]) : 2u,
                    parentLen > 3u ? MapSwizzleCharToLane(curBase->text[3]) : 3u,
                };
                for(u32 i = 0u; i < 4u; ++i)
                    lanes[i] = parentLanes[lanes[i] < 4u ? lanes[i] : 3u];
                curBase = curBase->lhs;
            }
            if(lanes[0] == 0u && lanes[1] == 1u && lanes[2] == 2u && lanes[3] == 3u){
                emitSimdRegExpr(out, *curBase, simdType);
                return;
            }
            if(!isInt && lanes[0] == lanes[1] && lanes[1] == lanes[2] && lanes[2] == lanes[3]){
                const MStringView splatPrefix = SimdSplatIntrinsic(lanes[0], isDouble);
                out.append(splatPrefix.data(), splatPrefix.size());
                emitSimdRegExpr(out, *curBase, curBase->resolvedSimdType);
                out.push_back(')');
                return;
            }
            out.append(isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
            AppendUnsignedDecimal(out, lanes[0]);
            out.append("u, ");
            AppendUnsignedDecimal(out, lanes[1]);
            out.append("u, ");
            AppendUnsignedDecimal(out, lanes[2]);
            out.append("u, ");
            AppendUnsignedDecimal(out, lanes[3]);
            out.append("u>(");
            emitSimdRegExpr(out, *curBase, curBase->resolvedSimdType);
            out.push_back(')');
            return;
        }
        break;
    }
    case ExprKind::Unary:{
        if(expr.lhs){
            if(expr.op == TokenType::Minus){
                if(isMatrix){
                    const MStringView matRegType = SimdRegisterCppType(simdType);
                    out.append("([&]()noexcept{ const ");
                    out.append(matRegType.data(), matRegType.size());
                    out.append(" __m = ");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    SimdEmissionFragments::EmitSimdMatrixUnaryRowInitSuffix(out, matRegType, isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(", ")");
                    return;
                }
                if(isInt){
                    SimdEmissionFragments::EmitSimdIntTwosComplementNegatePrefix(out, isDouble);
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    SimdEmissionFragments::EmitSimdIntTwosComplementNegateSuffix(out, isDouble);
                    return;
                }
                if(const Expr* innerVec = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*expr.lhs, simdType, m_symbols)){
                    emitSimdRegExpr(out, *innerVec, simdType);
                    return;
                }
                if(const Expr* splatScalar = SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(*expr.lhs, m_symbols)){
                    if(SimdExpressionAnalysis::IsZeroScalarLiteral(*splatScalar, &m_symbols)){
                        out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
                        return;
                    }
                    if(SimdConstantAnalysis::IsUnitFloatLiteral(*splatScalar, &m_symbols)){
                        out.append(isDouble ? "::s_DoubleSIMDNegativeOne" : "::s_SIMDNegativeOne");
                        return;
                    }
                    if(SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*splatScalar, &m_symbols)){
                        out.append(isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne");
                        return;
                    }
                    if(!SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*splatScalar, simdType, m_symbols)){
                        out.append(isDouble ? "::VectorDoubleReplicate(-(" : "::VectorReplicate(-(");
                        emitExpr(out, *splatScalar);
                        out.append("))");
                        return;
                    }
                }
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Tilde){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(isDouble ? ", ::VectorDoubleTrueInt())" : ", ::VectorTrueInt())");
                return;
            }
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.lhs && expr.rhs && SimdBinaryEmitter(*this).tryEmit(out, expr.op, *expr.lhs, *expr.rhs, simdType))
            return;
        break;
    }
    case ExprKind::Call:{
        if(SimdCallEmitter(*this).tryEmit(out, expr, simdType))
            return;
        break;
    }
    default:
        break;
    }

    if(SymbolTable::IsSimdRegisterType(expr.resolvedSimdType)){
        emitExpr(out, expr);
        return;
    }

    const MStringView loadFn = SimdLoadFunctionCpp(simdType);
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    emitExpr(out, expr);
    out.push_back(')');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

