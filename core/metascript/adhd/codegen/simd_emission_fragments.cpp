// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_emission_fragments.h"
#include "simd_codegen_utils.h"
#include "simd_optimization_analysis.h"
#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SimdEmissionFragments::EmitSimdMatrixBinaryRowInitSuffix(MString& out, const MStringView matRegType, const MStringView fnPrefix){
    out.append("; return ");
    out.append(matRegType.data(), matRegType.size());
    out.append("{ .v = { ");
    for(u32 i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        out.append(fnPrefix.data(), fnPrefix.size());
        out.append("__a.v[");
        AppendUnsignedDecimal(out, i);
        out.append("], __b.v[");
        AppendUnsignedDecimal(out, i);
        out.append("])");
    }
    out.append(" } }; }())");
}

void SimdEmissionFragments::EmitSimdMatrixUnaryRowInitSuffix(MString& out, const MStringView matRegType, const MStringView fnPrefix, const MStringView fnSuffix){
    out.append("; return ");
    out.append(matRegType.data(), matRegType.size());
    out.append("{ .v = { ");
    for(u32 i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        out.append(fnPrefix.data(), fnPrefix.size());
        out.append("__m.v[");
        AppendUnsignedDecimal(out, i);
        out.push_back(']');
        out.append(fnSuffix.data(), fnSuffix.size());
    }
    out.append(" } }; }())");
}

void SimdEmissionFragments::EmitSimdIntTwosComplementNegatePrefix(MString& out, const b8 isDouble){
    out.append(isDouble ? "::VectorDoubleAddInt(::VectorDoubleXorInt(" : "::VectorAddInt(::VectorXorInt(");
}

void SimdEmissionFragments::EmitSimdIntTwosComplementNegateSuffix(MString& out, const b8 isDouble){
    out.append(isDouble ? ", ::VectorDoubleTrueInt()), ::VectorDoubleReplicateInt(1ull))" : ", ::VectorTrueInt()), ::VectorReplicateInt(1u))");
}

void SimdEmissionFragments::EmitSimdVecWithReplacedWLane(
    const SourceEmitter& emitter,
    MString& out,
    const MStringView simdType,
    const Expr& baseVec,
    const MStringView explicitLoadFn,
    const u32 baseZeroedAbove,
    const Expr& wArg
){
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);
    const auto emitBaseVec = [&](){
        if(!explicitLoadFn.empty()){
            out.append(explicitLoadFn.data(), explicitLoadFn.size());
            out.push_back('(');
            emitter.emitExpr(out, baseVec);
            out.push_back(')');
        }
        else{
            emitter.emitSimdRegExpr(out, baseVec, simdType);
        }
    };

    const Expr* wLaneBase = nullptr;
    u32 wLaneIdx = 0u;
    const b8 isZeroW = SimdExpressionAnalysis::IsZeroScalarLiteral(wArg, &emitter.m_symbols);
    if(isZeroW && ((!explicitLoadFn.empty() && baseZeroedAbove <= 3u) || !isInt)){
        const u32 effZeroedAbove = (explicitLoadFn.empty() && SymbolTable::IsSimdRegisterVectorType(baseVec.resolvedSimdType))
            ? SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(baseVec, isDouble, &emitter.m_symbols, emitter.activeLocalVarDecls())
            : baseZeroedAbove;
        if(effZeroedAbove <= 3u){
            emitBaseVec();
            return;
        }
        if(explicitLoadFn.empty()){
            if(const Expr* negInner = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(baseVec, simdType, emitter.m_symbols);
                negInner && negInner->resolvedSimdType == simdType && SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(*negInner, isDouble, &emitter.m_symbols, emitter.activeLocalVarDecls()) <= 3u
            ){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                emitter.emitSimdRegExpr(out, *negInner, simdType);
                out.append(isDouble ? ", ::s_DoubleSIMDNegate3)" : ", ::s_SIMDNegate3)");
                return;
            }
        }
        out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
        emitBaseVec();
        out.append(isDouble ? ", ::s_DoubleSIMDMask3)" : ", ::s_SIMDMask3)");
        return;
    }
    if(SimdExpressionAnalysis::TryMatchSingleLaneAccess(&wArg, wLaneBase, wLaneIdx) && wLaneBase && wLaneBase->resolvedSimdType == simdType){
        if(explicitLoadFn.empty() && SimdExpressionAnalysis::AreSamePureSimdExpr(baseVec, *wLaneBase)){
            if(wLaneIdx == 3u){
                emitBaseVec();
                return;
            }
            const MStringView swizzlePrefix = isDouble ? CodegenSymbols::s_SimdSwizzleWPrefixDouble : CodegenSymbols::s_SimdSwizzleWPrefixSingle;
            out.append(swizzlePrefix.data(), swizzlePrefix.size());
            AppendUnsignedDecimal(out, wLaneIdx);
            out.append("u>(");
            emitBaseVec();
            out.push_back(')');
            return;
        }
        const MStringView permutePrefix = isDouble ? CodegenSymbols::s_SimdPermuteWPrefixDouble : CodegenSymbols::s_SimdPermuteWPrefixSingle;
        out.append(permutePrefix.data(), permutePrefix.size());
        AppendUnsignedDecimal(out, 4u + wLaneIdx);
        out.append("u>(");
        emitBaseVec();
        out.append(", ");
        emitter.emitSimdRegExpr(out, *wLaneBase, simdType);
        out.push_back(')');
        return;
    }
    if(!isInt && SimdExpressionAnalysis::TryMatchNegatedSingleLaneAccess(&wArg, wLaneBase, wLaneIdx) && wLaneBase && wLaneBase->resolvedSimdType == simdType){
        const MStringView permutePrefix = isDouble ? CodegenSymbols::s_SimdPermuteWPrefixDouble : CodegenSymbols::s_SimdPermuteWPrefixSingle;
        out.append(permutePrefix.data(), permutePrefix.size());
        AppendUnsignedDecimal(out, 4u + wLaneIdx);
        out.append("u>(");
        emitBaseVec();
        const MStringView negateFn = isDouble ? CodegenSymbols::s_SimdNegateDouble : CodegenSymbols::s_SimdNegateSingle;
        out.append(", ");
        out.append(negateFn.data(), negateFn.size());
        emitter.emitSimdRegExpr(out, *wLaneBase, simdType);
        out.append("))");
        return;
    }
    if(!isInt && SimdConstantAnalysis::IsUnitFloatLiteral(wArg, &emitter.m_symbols)){
        const MStringView permutePrefix = isDouble ? CodegenSymbols::s_SimdPermuteWPrefixDouble : CodegenSymbols::s_SimdPermuteWPrefixSingle;
        out.append(permutePrefix.data(), permutePrefix.size());
        out.append("7u>(");
        emitBaseVec();
        out.append(isDouble ? ", ::s_DoubleSIMDIdentityR3)" : ", ::s_SIMDIdentityR3)");
        return;
    }
    if(!isInt && SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(wArg, simdType, emitter.m_symbols)){
        const MStringView permutePrefix = isDouble ? CodegenSymbols::s_SimdPermuteWPrefixDouble : CodegenSymbols::s_SimdPermuteWPrefixSingle;
        out.append(permutePrefix.data(), permutePrefix.size());
        out.append("7u>(");
        emitBaseVec();
        out.append(", ");
        emitter.emitSimdRegExpr(out, wArg, simdType);
        out.push_back(')');
        return;
    }
    if(isInt)
        out.append(isDouble ? "::VectorDoubleSetIntW(" : "::VectorSetIntW(");
    else
        out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
    emitBaseVec();
    out.append(", ");
    if(isInt)
        out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
    emitter.emitExpr(out, wArg);
    if(isInt)
        out.push_back(')');
    out.push_back(')');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

