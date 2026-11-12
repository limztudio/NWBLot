// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_cast_constructor_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SimdCastConstructorEmitter::tryEmit()const{
    MString& out = m_output;
    const Span<const Expr* const> arguments = m_arguments;
    const b8 isDouble = m_isDouble;
    const b8 isInt = false;
    if(!isInt && (arguments.size() == 3u || arguments.size() == 4u)){
        const MStringView targetScalarType = isDouble ? "f64" : "f32";
        const Expr* cV0 = nullptr;
        const Expr* cV1 = nullptr;
        MStringView cV0RegType;
        MStringView cV0LoadFn;
        u32 cV0ZeroedAbove = 4u;
        MStringView cV1RegType;
        MStringView cV1LoadFn;
        Array<u32, 4> cPermIdx{};
        u8 cInPlaceMask = 0u;
        u8 cV0UsedMask = 0u;
        b8 cAllInPlaceOrZero = true;
        b8 cHasZeroLane = false;
        b8 cValid = true;
        for(u32 i = 0u; i < 4u; ++i){
            const Expr* base = nullptr;
            u32 laneIdx = 0u;
            if(i < arguments.size() && SimdExpressionAnalysis::TryMatchCastSingleLaneAccess(arguments[i], targetScalarType, m_emitter.m_symbols, base, laneIdx) && base){
                MStringView baseRegType;
                MStringView baseLoadFn;
                u32 baseZeroedAbove = 4u;
                if(!SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*base, !isDouble, baseRegType, baseLoadFn, baseZeroedAbove)){
                    cValid = false;
                    break;
                }
                if(!cV0){
                    cV0 = base;
                    cV0RegType = baseRegType;
                    cV0LoadFn = baseLoadFn;
                    cV0ZeroedAbove = baseZeroedAbove;
                    cPermIdx[i] = laneIdx;
                    cV0UsedMask |= static_cast<u8>(1u << laneIdx);
                    if(laneIdx == i)
                        cInPlaceMask |= static_cast<u8>(1u << i);
                    else
                        cAllInPlaceOrZero = false;
                }
                else if(SimdExpressionAnalysis::AreSamePureSimdExpr(*cV0, *base)){
                    cPermIdx[i] = laneIdx;
                    cV0UsedMask |= static_cast<u8>(1u << laneIdx);
                    if(laneIdx == i && !cV1)
                        cInPlaceMask |= static_cast<u8>(1u << i);
                    else
                        cAllInPlaceOrZero = false;
                }
                else if(!cV1 && !cHasZeroLane){
                    cV1 = base;
                    cV1RegType = baseRegType;
                    cV1LoadFn = baseLoadFn;
                    cPermIdx[i] = 4u + laneIdx;
                    cAllInPlaceOrZero = false;
                }
                else if(cV1 && SimdExpressionAnalysis::AreSamePureSimdExpr(*cV1, *base)){
                    cPermIdx[i] = 4u + laneIdx;
                    cAllInPlaceOrZero = false;
                }
                else{
                    cValid = false;
                    break;
                }
            }
            else if(!cV1 && (i >= arguments.size() || (arguments[i] && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &m_emitter.m_symbols)))){
                cHasZeroLane = true;
                cPermIdx[i] = 4u + i;
            }
            else{
                cValid = false;
                break;
            }
        }
        if(cValid && cV0 && (cV1 || (cV0UsedMask & (cV0UsedMask - 1u)) != 0u)){
            const auto emitCastSourceVec = [&](const Expr& srcVec, const MStringView regType, const MStringView loadFn){
                if(!regType.empty()){
                    m_emitter.emitSimdRegExpr(out, srcVec, regType);
                }
                else{
                    out.append(loadFn.data(), loadFn.size());
                    out.push_back('(');
                    m_emitter.emitExpr(out, srcVec);
                    out.push_back(')');
                }
            };
            if(!cV1 && cAllInPlaceOrZero){
                const u32 effCV0ZeroedAbove = !cV0RegType.empty()
                    ? SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(*cV0, !isDouble, &m_emitter.m_symbols, m_emitter.activeLocalVarDecls())
                    : cV0ZeroedAbove;
                const u8 loadedZeroMask = static_cast<u8>((1u << effCV0ZeroedAbove) - 1u);
                if(cInPlaceMask == 0xFu || (cInPlaceMask & loadedZeroMask) == loadedZeroMask){
                    out.append(isDouble ? "::ConvertVectorSingleToDouble(" : "::ConvertVectorDoubleToSingle(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.push_back(')');
                    return true;
                }
                const MStringView maskConst = SimdInPlaceLaneMaskConstant(cInPlaceMask, !isDouble);
                if(!maskConst.empty()){
                    out.append(isDouble ? "::ConvertVectorSingleToDouble(::VectorAndInt(" : "::ConvertVectorDoubleToSingle(::VectorDoubleAndInt(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append(", ");
                    out.append(maskConst.data(), maskConst.size());
                    out.append("))");
                    return true;
                }
            }
            if(!cV1 && !cAllInPlaceOrZero && !cHasZeroLane){
                    out.append(isDouble ? "::ConvertVectorSingleToDouble(::VectorSwizzle<" : "::ConvertVectorDoubleToSingle(::VectorDoubleSwizzle<");
                    for(u32 i = 0u; i < 4u; ++i){
                        if(i > 0u)
                            out.append(", ");
                        AppendUnsignedDecimal(out, cPermIdx[i]);
                        out.push_back('u');
                    }
                    out.append(">(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append("))");
                return true;
            }
            if(cV1 && !cHasZeroLane){
                    out.append(isDouble ? "::ConvertVectorSingleToDouble(::VectorPermute<" : "::ConvertVectorDoubleToSingle(::VectorDoublePermute<");
                    for(u32 i = 0u; i < 4u; ++i){
                        if(i > 0u)
                            out.append(", ");
                        AppendUnsignedDecimal(out, cPermIdx[i]);
                        out.push_back('u');
                    }
                    out.append(">(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append(", ");
                    emitCastSourceVec(*cV1, cV1RegType, cV1LoadFn);
                    out.append("))");
                return true;
            }
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

