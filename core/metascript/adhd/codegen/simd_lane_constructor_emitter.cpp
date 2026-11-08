// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_lane_constructor_emitter.h"
#include "simd_scalar_lane_emitter.h"
#include "simd_optimization_analysis.h"

NWB_ADHD_BEGIN


b8 SimdLaneConstructorEmitter::tryEmit()const{
    MString& out = m_output;
    const MStringView simdType = m_simdType;
    const Span<const Expr* const> arguments = m_arguments;
    const b8 isDouble = m_isDouble;
    const b8 isInt = m_isInt;
    Array<const Expr*, 4> laneBases{};
    Array<u32, 4> laneIndices{};
    for(usize i = 0u; i < 4u && i < arguments.size(); ++i){
        const Expr* base = nullptr;
        u32 idx = 0u;
        if(SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], base, idx) && base->resolvedSimdType == simdType){
            laneBases[i] = base;
            laneIndices[i] = idx;
        }
    }

    if(!isInt){
        const Expr* inPlaceBase = nullptr;
        MStringView inPlaceRegType;
        MStringView inPlaceLoadFn;
        u32 inPlaceZeroedAbove = 4u;
        u8 inPlaceMask = 0u;
        b8 allInPlaceOrZero = true;
        for(u32 i = 0u; i < 4u; ++i){
            const Expr* base = nullptr;
            u32 idx = 0u;
            if(i < arguments.size() && SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], base, idx) && base){
                MStringView baseRegType;
                MStringView baseLoadFn;
                u32 baseZeroedAbove = 4u;
                if(idx != i || !SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*base, isDouble, baseRegType, baseLoadFn, baseZeroedAbove) || idx >= baseZeroedAbove){
                    allInPlaceOrZero = false;
                    break;
                }
                if(!inPlaceBase){
                    inPlaceBase = base;
                    inPlaceRegType = baseRegType;
                    inPlaceLoadFn = baseLoadFn;
                    inPlaceZeroedAbove = baseZeroedAbove;
                }
                else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*inPlaceBase, *base)){
                    allInPlaceOrZero = false;
                    break;
                }
                inPlaceMask |= static_cast<u8>(1u << i);
            }
            else if(i < arguments.size() && (!arguments[i] || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &m_emitter.m_symbols))){
                allInPlaceOrZero = false;
                break;
            }
        }
        if(allInPlaceOrZero && inPlaceBase && (!inPlaceRegType.empty() || (inPlaceMask & (inPlaceMask - 1u)) != 0u)){
            const u32 effInPlaceZeroedAbove = !inPlaceRegType.empty()
                ? SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(*inPlaceBase, isDouble, &m_emitter.m_symbols, m_emitter.activeLocalVarDecls())
                : inPlaceZeroedAbove;
            const u8 naturalLoadMask = static_cast<u8>((1u << effInPlaceZeroedAbove) - 1u);
            if(inPlaceMask == 0xFu || (inPlaceMask & naturalLoadMask) == naturalLoadMask){
                if(!inPlaceRegType.empty()){
                    m_emitter.emitSimdRegExpr(out, *inPlaceBase, simdType);
                }
                else{
                    out.append(inPlaceLoadFn.data(), inPlaceLoadFn.size());
                    out.push_back('(');
                    m_emitter.emitExpr(out, *inPlaceBase);
                    out.push_back(')');
                }
                return true;
            }
            const MStringView maskConst = SimdInPlaceLaneMaskConstant(inPlaceMask, isDouble);
            if(!maskConst.empty()){
                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                if(!inPlaceRegType.empty()){
                    m_emitter.emitSimdRegExpr(out, *inPlaceBase, simdType);
                }
                else{
                    out.append(inPlaceLoadFn.data(), inPlaceLoadFn.size());
                    out.push_back('(');
                    m_emitter.emitExpr(out, *inPlaceBase);
                    out.push_back(')');
                }
                out.append(", ");
                out.append(maskConst.data(), maskConst.size());
                out.push_back(')');
                return true;
            }
            const MStringView clearMaskConst = SimdInPlaceLaneMaskConstant(static_cast<u8>(0xFu ^ inPlaceMask), isDouble);
            if(!clearMaskConst.empty()){
                out.append(isDouble ? "::VectorDoubleAndCInt(" : "::VectorAndCInt(");
                if(!inPlaceRegType.empty()){
                    m_emitter.emitSimdRegExpr(out, *inPlaceBase, simdType);
                }
                else{
                    out.append(inPlaceLoadFn.data(), inPlaceLoadFn.size());
                    out.push_back('(');
                    m_emitter.emitExpr(out, *inPlaceBase);
                    out.push_back(')');
                }
                out.append(", ");
                out.append(clearMaskConst.data(), clearMaskConst.size());
                out.push_back(')');
                return true;
            }
        }

        const Expr* negBase = nullptr;
        MStringView negRegType;
        MStringView negLoadFn;
        u32 negZeroedAbove = 4u;
        u8 negMask = 0u;
        u8 posMask = 0u;
        b8 allNegInPlaceOrZero = true;
        for(u32 i = 0u; i < 4u; ++i){
            const Expr* base = nullptr;
            u32 idx = 0u;
            MStringView baseRegType;
            MStringView baseLoadFn;
            u32 baseZeroedAbove = 4u;
            if(
                i < arguments.size()
                && SimdExpressionAnalysis::TryMatchNegatedSingleLaneAccess(arguments[i], base, idx)
                && base
                && idx == i
                && SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*base, isDouble, baseRegType, baseLoadFn, baseZeroedAbove)
                && idx < baseZeroedAbove
            ){
                if(!negBase){
                    negBase = base;
                    negRegType = baseRegType;
                    negLoadFn = baseLoadFn;
                    negZeroedAbove = baseZeroedAbove;
                }
                else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*negBase, *base)){
                    allNegInPlaceOrZero = false;
                    break;
                }
                negMask |= static_cast<u8>(1u << i);
            }
            else if(
                i < arguments.size()
                && SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], base, idx)
                && base
                && idx == i
                && SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*base, isDouble, baseRegType, baseLoadFn, baseZeroedAbove)
                && idx < baseZeroedAbove
            ){
                if(!negBase){
                    negBase = base;
                    negRegType = baseRegType;
                    negLoadFn = baseLoadFn;
                    negZeroedAbove = baseZeroedAbove;
                }
                else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*negBase, *base)){
                    allNegInPlaceOrZero = false;
                    break;
                }
                posMask |= static_cast<u8>(1u << i);
            }
            else if(i < arguments.size() && (!arguments[i] || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &m_emitter.m_symbols))){
                allNegInPlaceOrZero = false;
                break;
            }
        }
        const u8 negUsedMask = static_cast<u8>(negMask | posMask);
        if(allNegInPlaceOrZero && negBase && negMask != 0u && (!negRegType.empty() || (negUsedMask & (negUsedMask - 1u)) != 0u)){
            const u32 effNegZeroedAbove = !negRegType.empty()
                ? SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(*negBase, isDouble, &m_emitter.m_symbols, m_emitter.activeLocalVarDecls())
                : negZeroedAbove;
            const u8 naturalZeroLaneMask = static_cast<u8>(~((1u << effNegZeroedAbove) - 1u) & 0xFu & ~negMask);
            const u8 effectivePosMask = static_cast<u8>(posMask | naturalZeroLaneMask);
            if(negMask == 0xFu && posMask == 0u){
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                m_emitter.emitSimdRegExpr(out, *negBase, simdType);
                out.push_back(')');
                return true;
            }
            if((negMask | effectivePosMask) == 0xFu){
                const MStringView flipConst = SimdInPlaceSignFlipMaskConstant(negMask, isDouble);
                if(!flipConst.empty()){
                    out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                    m_emitter.emitSimdRegExpr(out, *negBase, simdType);
                    out.append(", ");
                    out.append(flipConst.data(), flipConst.size());
                    out.push_back(')');
                    return true;
                }
            }
            if(posMask == 0u){
                const MStringView maskConst = SimdInPlaceLaneMaskConstant(negMask, isDouble);
                if(!maskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndInt(::VectorDoubleNegate(" : "::VectorAndInt(::VectorNegate(");
                    m_emitter.emitSimdRegExpr(out, *negBase, simdType);
                    out.append("), ");
                    out.append(maskConst.data(), maskConst.size());
                    out.push_back(')');
                    return true;
                }
                const MStringView clearMaskConst = SimdInPlaceLaneMaskConstant(static_cast<u8>(0xFu ^ negMask), isDouble);
                if(!clearMaskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndCInt(::VectorDoubleNegate(" : "::VectorAndCInt(::VectorNegate(");
                    m_emitter.emitSimdRegExpr(out, *negBase, simdType);
                    out.append("), ");
                    out.append(clearMaskConst.data(), clearMaskConst.size());
                    out.push_back(')');
                    return true;
                }
            }
        }
    }

    if(arguments.size() == 4u){
        for(u32 replaceLane = 0u; replaceLane < 4u; ++replaceLane){
            if(
                laneBases[replaceLane] != nullptr
                || !arguments[replaceLane]
                || SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[replaceLane], &m_emitter.m_symbols)
                || (!isInt && SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[replaceLane], &m_emitter.m_symbols))
            ){
                continue;
            }
            const Expr* v0 = nullptr;
            b8 threeNaturalMatch = true;
            for(u32 i = 0u; i < 4u; ++i){
                if(i == replaceLane)
                    continue;
                const Expr* curBase = laneBases[i];
                u32 curIdx = laneIndices[i];
                if(!curBase && !isInt && arguments[i]){
                    const Expr* candBase = nullptr;
                    u32 candIdx = 0u;
                    MStringView candRegType;
                    MStringView candLoadFn;
                    u32 candZeroedAbove = 4u;
                    if(
                        SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], candBase, candIdx)
                        && candBase
                        && SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*candBase, isDouble, candRegType, candLoadFn, candZeroedAbove)
                        && candZeroedAbove == 4u
                    ){
                        curBase = candBase;
                        curIdx = candIdx;
                    }
                }
                if(!curBase || curIdx != i){
                    threeNaturalMatch = false;
                    break;
                }
                if(!v0)
                    v0 = curBase;
                else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*v0, *curBase)){
                    threeNaturalMatch = false;
                    break;
                }
            }
            if(threeNaturalMatch && v0){
                if(!isInt){
                    const Expr* negLaneBase = nullptr;
                    u32 negLaneIdx = 0u;
                    if(SimdExpressionAnalysis::TryMatchNegatedSingleLaneAccess(arguments[replaceLane], negLaneBase, negLaneIdx) && negLaneBase && negLaneBase->resolvedSimdType == simdType){
                        out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
                        for(u32 i = 0u; i < 4u; ++i){
                            if(i > 0u)
                                out.append(", ");
                            AppendUnsignedDecimal(out, (i == replaceLane) ? (4u + negLaneIdx) : i);
                            out.push_back('u');
                        }
                        out.append(">(");
                        m_emitter.emitSimdRegExpr(out, *v0, simdType);
                        out.append(isDouble ? ", ::VectorDoubleNegate(" : ", ::VectorNegate(");
                        m_emitter.emitSimdRegExpr(out, *negLaneBase, simdType);
                        out.append("))");
                        return true;
                    }
                    if(SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*arguments[replaceLane], simdType, m_emitter.m_symbols)){
                        out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
                        for(u32 i = 0u; i < 4u; ++i){
                            if(i > 0u)
                                out.append(", ");
                            AppendUnsignedDecimal(out, (i == replaceLane) ? (4u + i) : i);
                            out.push_back('u');
                        }
                        out.append(">(");
                        m_emitter.emitSimdRegExpr(out, *v0, simdType);
                        out.append(", ");
                        m_emitter.emitSimdRegExpr(out, *arguments[replaceLane], simdType);
                        out.push_back(')');
                        return true;
                    }
                }
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
                else
                    out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
                const MStringView laneSuffix = SimdLaneCallSuffix(replaceLane);
                out.append(laneSuffix.data(), laneSuffix.size());
                m_emitter.emitSimdRegExpr(out, *v0, simdType);
                out.append(", ");
                if(isInt)
                    out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                m_emitter.emitExpr(out, *arguments[replaceLane]);
                if(isInt)
                    out.push_back(')');
                out.push_back(')');
                return true;
            }
        }
    }

    {
        const Expr* v0 = nullptr;
        const Expr* v1 = nullptr;
        u32 v0ZeroedAbove = 4u;
        u8 v0UsedLaneMask = 0u;
        u8 v1UsedLaneMask = 0u;
        Array<u32, 4> permIdx{};
        u8 zeroLaneMask = 0u;
        u8 unitLaneMask = 0u;
        b8 canSwizzleOrPermute = true;
        for(u32 i = 0u; i < 4u; ++i){
            const Expr* curBase = laneBases[i];
            u32 curIdx = laneIndices[i];
            u32 curZeroedAbove = 4u;
            if(!curBase && !isInt && i < arguments.size()){
                const Expr* candBase = nullptr;
                u32 candIdx = 0u;
                MStringView candRegType;
                MStringView candLoadFn;
                u32 candZeroedAbove = 4u;
                if(
                    SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], candBase, candIdx)
                    && candBase
                    && SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*candBase, isDouble, candRegType, candLoadFn, candZeroedAbove)
                    && candIdx < candZeroedAbove
                ){
                    curBase = candBase;
                    curIdx = candIdx;
                    curZeroedAbove = candZeroedAbove;
                }
            }
            if(curBase){
                if(!v0){
                    v0 = curBase;
                    v0ZeroedAbove = curZeroedAbove;
                    v0UsedLaneMask |= static_cast<u8>(1u << curIdx);
                    permIdx[i] = curIdx;
                }
                else if(SimdExpressionAnalysis::AreSamePureSimdExpr(*v0, *curBase)){
                    v0UsedLaneMask |= static_cast<u8>(1u << curIdx);
                    permIdx[i] = curIdx;
                }
                else if(!v1 && zeroLaneMask == 0u && unitLaneMask == 0u){
                    v1 = curBase;
                    v1UsedLaneMask |= static_cast<u8>(1u << curIdx);
                    permIdx[i] = 4u + curIdx;
                }
                else if(v1 && SimdExpressionAnalysis::AreSamePureSimdExpr(*v1, *curBase)){
                    v1UsedLaneMask |= static_cast<u8>(1u << curIdx);
                    permIdx[i] = 4u + curIdx;
                }
                else{
                    canSwizzleOrPermute = false;
                    break;
                }
            }
            else if(!v1 && (i >= arguments.size() || (arguments[i] && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &m_emitter.m_symbols)))){
                if(isInt && unitLaneMask != 0u){
                    canSwizzleOrPermute = false;
                    break;
                }
                zeroLaneMask |= static_cast<u8>(1u << i);
                permIdx[i] = 4u + i;
            }
            else if(!isInt && !v1 && i < arguments.size() && arguments[i] && SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[i], &m_emitter.m_symbols)){
                unitLaneMask |= static_cast<u8>(1u << i);
                permIdx[i] = 4u + i;
            }
            else{
                canSwizzleOrPermute = false;
                break;
            }
        }
        if(v0 && v0->resolvedSimdType != simdType && (v0UsedLaneMask & (v0UsedLaneMask - 1u)) == 0u)
            canSwizzleOrPermute = false;
        if(v1 && v1->resolvedSimdType != simdType && (v1UsedLaneMask & (v1UsedLaneMask - 1u)) == 0u)
            canSwizzleOrPermute = false;
        if(canSwizzleOrPermute && v0 && !v1 && unitLaneMask == 0u && zeroLaneMask != 0u && !isInt && v0ZeroedAbove == 4u && v0->resolvedSimdType == simdType)
            v0ZeroedAbove = SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(*v0, isDouble, &m_emitter.m_symbols, m_emitter.activeLocalVarDecls());
        if(canSwizzleOrPermute && v0 && !v1 && unitLaneMask == 0u && zeroLaneMask != 0u && v0ZeroedAbove < 4u){
            for(u32 i = 0u; i < 4u; ++i){
                if((zeroLaneMask & static_cast<u8>(1u << i)) != 0u)
                    permIdx[i] = v0ZeroedAbove;
            }
            zeroLaneMask = 0u;
        }
        MStringView constSecondVec;
        if(canSwizzleOrPermute && v0 && !v1 && (zeroLaneMask != 0u || unitLaneMask != 0u)){
            if(unitLaneMask == 0u){
                constSecondVec = isInt
                    ? (isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()")
                    : (isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
            }
            else if(zeroLaneMask == 0u){
                constSecondVec = isDouble ? "::s_DoubleSIMDOne" : "::s_SIMDOne";
            }
            else if(unitLaneMask == 0x1u){
                constSecondVec = isDouble ? "::s_DoubleSIMDIdentityR0" : "::s_SIMDIdentityR0";
            }
            else if(unitLaneMask == 0x2u){
                constSecondVec = isDouble ? "::s_DoubleSIMDIdentityR1" : "::s_SIMDIdentityR1";
            }
            else if(unitLaneMask == 0x4u){
                constSecondVec = isDouble ? "::s_DoubleSIMDIdentityR2" : "::s_SIMDIdentityR2";
            }
            else if(unitLaneMask == 0x8u){
                constSecondVec = isDouble ? "::s_DoubleSIMDIdentityR3" : "::s_SIMDIdentityR3";
            }
            else if(zeroLaneMask == 0x8u){
                constSecondVec = isDouble ? "::s_DoubleSIMDOne3" : "::s_SIMDOne3";
            }
            else{
                constSecondVec = isDouble ? "::s_DoubleSIMDIdentityR0" : "::s_SIMDIdentityR0";
                for(u32 i = 0u; i < 4u; ++i){
                    if((unitLaneMask & static_cast<u8>(1u << i)) != 0u)
                        permIdx[i] = 4u;
                    else if((zeroLaneMask & static_cast<u8>(1u << i)) != 0u)
                        permIdx[i] = (i == 0u) ? 5u : (4u + i);
                }
            }
        }
        if(canSwizzleOrPermute && v0){
            if(!v1 && zeroLaneMask == 0u && unitLaneMask == 0u){
                if(permIdx[0] == 0u && permIdx[1] == 1u && permIdx[2] == 2u && permIdx[3] == 3u){
                    m_emitter.emitSimdRegExpr(out, *v0, simdType);
                    return true;
                }
                if(!isInt && permIdx[0] == permIdx[1] && permIdx[1] == permIdx[2] && permIdx[2] == permIdx[3]){
                    const MStringView splatPrefix = SimdSplatIntrinsic(permIdx[0], isDouble);
                    out.append(splatPrefix.data(), splatPrefix.size());
                    m_emitter.emitSimdRegExpr(out, *v0, simdType);
                    out.push_back(')');
                    return true;
                }
                out.append(isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
                for(u32 i = 0u; i < 4u; ++i){
                    if(i > 0u)
                        out.append(", ");
                    AppendUnsignedDecimal(out, permIdx[i]);
                    out.push_back('u');
                }
                out.append(">(");
                m_emitter.emitSimdRegExpr(out, *v0, simdType);
                out.push_back(')');
                return true;
            }
            out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
            for(u32 i = 0u; i < 4u; ++i){
                if(i > 0u)
                    out.append(", ");
                AppendUnsignedDecimal(out, permIdx[i]);
                out.push_back('u');
            }
            out.append(">(");
            m_emitter.emitSimdRegExpr(out, *v0, simdType);
            out.append(", ");
            if(v1)
                m_emitter.emitSimdRegExpr(out, *v1, simdType);
            else
                out.append(constSecondVec.data(), constSecondVec.size());
            out.push_back(')');
            return true;
        }
    }

    return SimdScalarLaneEmitter(m_emitter, out, simdType, arguments, isDouble, isInt).tryEmit();
}

NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

