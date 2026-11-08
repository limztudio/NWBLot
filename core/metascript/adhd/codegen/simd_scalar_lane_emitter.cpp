// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_scalar_lane_emitter.h"
#include "simd_optimization_analysis.h"


NWB_ADHD_BEGIN


b8 SimdScalarLaneEmitter::tryEmit()const{
    MString& out = m_output;
    const MStringView simdType = m_simdType;
    const Span<const Expr* const> arguments = m_arguments;
    const b8 isDouble = m_isDouble;
    const b8 isInt = m_isInt;
    if(arguments.size() == 3u || arguments.size() == 4u){
        const Expr* repeatedScalar = nullptr;
        u8 nonZeroMask = 0u;
        u32 nonZeroCount = 0u;
        b8 sameOrZero = true;
        for(u32 i = 0u; i < 4u; ++i){
            if(i >= arguments.size() || (arguments[i] && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &m_emitter.m_symbols)))
                continue;
            if(
                arguments[i]
                && arguments[i]->resolvedSimdType.empty()
                && arguments[i]->resolvedUnalignedType.empty()
                && SimdExpressionAnalysis::IsPureSimdAddressExpr(*arguments[i])
            ){
                if(!repeatedScalar){
                    repeatedScalar = arguments[i];
                }
                else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*repeatedScalar, *arguments[i])){
                    sameOrZero = false;
                    break;
                }
                nonZeroMask |= static_cast<u8>(1u << i);
                ++nonZeroCount;
            }
            else{
                sameOrZero = false;
                break;
            }
        }
        if(sameOrZero && repeatedScalar){
            if(nonZeroMask == 0xFu){
                m_emitter.emitSimdRegExpr(out, *repeatedScalar, simdType);
                return true;
            }
            const b8 isKnownFloatConstScalar = !isInt && (
                SimdConstantAnalysis::IsUnitFloatLiteral(*repeatedScalar, &m_emitter.m_symbols)
                || SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*repeatedScalar, &m_emitter.m_symbols)
                || SimdConstantAnalysis::IsHalfFloatLiteral(*repeatedScalar, &m_emitter.m_symbols)
            );
            if(!isInt && (nonZeroCount >= 2u || isKnownFloatConstScalar || SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*repeatedScalar, simdType, m_emitter.m_symbols))){

                const MStringView maskConst = SimdInPlaceLaneMaskConstant(nonZeroMask, isDouble);
                if(!maskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                    m_emitter.emitSimdRegExpr(out, *repeatedScalar, simdType);
                    out.append(", ");
                    out.append(maskConst.data(), maskConst.size());
                    out.push_back(')');
                    return true;
                }
                const MStringView clearMaskConst = SimdInPlaceLaneMaskConstant(static_cast<u8>(0xFu ^ nonZeroMask), isDouble);
                if(!clearMaskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndCInt(" : "::VectorAndCInt(");
                    m_emitter.emitSimdRegExpr(out, *repeatedScalar, simdType);
                    out.append(", ");
                    out.append(clearMaskConst.data(), clearMaskConst.size());
                    out.push_back(')');
                    return true;
                }
                if(SimdConstantAnalysis::IsUnitFloatLiteral(*repeatedScalar, &m_emitter.m_symbols)){
                    if(nonZeroMask == 0x9u){
                        out.append(isDouble ? "::VectorDoubleOrInt(::s_DoubleSIMDIdentityR0, ::s_DoubleSIMDIdentityR3)" : "::VectorOrInt(::s_SIMDIdentityR0, ::s_SIMDIdentityR3)");
                        return true;
                    }
                    if(nonZeroMask == 0x6u){
                        out.append(isDouble ? "::VectorDoubleOrInt(::s_DoubleSIMDIdentityR1, ::s_DoubleSIMDIdentityR2)" : "::VectorOrInt(::s_SIMDIdentityR1, ::s_SIMDIdentityR2)");
                        return true;
                    }
                }
            }
        }
        if(!isInt && arguments.size() == 4u){
            const Expr* signScalar = nullptr;
            u8 posMask = 0u;
            u8 negMask = 0u;
            b8 allSignedScalar = true;
            for(u32 i = 0u; i < 4u; ++i){
                const Expr* arg = arguments[i];
                if(!arg){
                    allSignedScalar = false;
                    break;
                }
                const b8 isNeg = (arg->kind == ExprKind::Unary && !arg->isPostfix && arg->op == TokenType::Minus && arg->lhs);
                const Expr* baseScalar = isNeg ? arg->lhs : arg;
                if(!baseScalar || !baseScalar->resolvedSimdType.empty() || !baseScalar->resolvedUnalignedType.empty() || !SimdExpressionAnalysis::IsPureSimdAddressExpr(*baseScalar)){
                    allSignedScalar = false;
                    break;
                }
                if(!signScalar){
                    signScalar = baseScalar;
                }
                else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*signScalar, *baseScalar)){
                    allSignedScalar = false;
                    break;
                }
                if(isNeg)
                    negMask |= static_cast<u8>(1u << i);
                else
                    posMask |= static_cast<u8>(1u << i);
            }
            if(allSignedScalar && signScalar && posMask != 0u && negMask != 0u && (posMask | negMask) == 0xFu){
                const MStringView flipConst = SimdInPlaceSignFlipMaskConstant(negMask, isDouble);
                if(!flipConst.empty()){
                    out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                    m_emitter.emitSimdRegExpr(out, *signScalar, simdType);
                    out.append(", ");
                    out.append(flipConst.data(), flipConst.size());
                    out.push_back(')');
                    return true;
                }
            }
        }
        if(!isInt){
            u8 unitPosMask = 0u;
            u8 unitNegMask = 0u;
            u8 zeroMask = 0u;
            u32 firstPosLane = 0u;
            b8 allTrinaryUnitConst = true;
            for(u32 i = 0u; i < 4u; ++i){
                if(i >= arguments.size() || (arguments[i] && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &m_emitter.m_symbols))){
                    zeroMask |= static_cast<u8>(1u << i);
                }
                else if(arguments[i] && SimdConstantAnalysis::IsUnitFloatLiteral(*arguments[i], &m_emitter.m_symbols)){
                    if(unitPosMask == 0u)
                        firstPosLane = i;
                    unitPosMask |= static_cast<u8>(1u << i);
                }
                else if(arguments[i] && SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*arguments[i], &m_emitter.m_symbols)){
                    unitNegMask |= static_cast<u8>(1u << i);
                }
                else{
                    allTrinaryUnitConst = false;
                    break;
                }
            }
            if(allTrinaryUnitConst && unitPosMask != 0u && unitNegMask != 0u && zeroMask != 0u){
                out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
                for(u32 i = 0u; i < 4u; ++i){
                    if(i > 0u)
                        out.append(", ");
                    u32 idx = 0u;
                    if((unitPosMask & static_cast<u8>(1u << i)) != 0u)
                        idx = firstPosLane;
                    else if((unitNegMask & static_cast<u8>(1u << i)) != 0u)
                        idx = 4u + i;
                    else
                        idx = (i == firstPosLane) ? ((firstPosLane + 1u) & 3u) : i;
                    AppendUnsignedDecimal(out, idx);
                    out.push_back('u');
                }
                out.append(isDouble ? ">(::s_DoubleSIMDIdentityR" : ">(::s_SIMDIdentityR");
                AppendUnsignedDecimal(out, firstPosLane);
                out.append(isDouble ? ", ::s_DoubleSIMDNegativeOne)" : ", ::s_SIMDNegativeOne)");
                return true;
            }
        }
    }

    return false;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

