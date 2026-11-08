// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "simd_storage_emitter.h"
#include "simd_optimization_analysis.h"

NWB_ADHD_BEGIN

b8 SimdStorageEmitter::tryEmit(MString& out, const MStringView storageTypeName, const Span<const Expr* const> arguments, const MStringView dstExprText)const{
    if(arguments.empty())
        return false;

    if(SymbolTable::IsSimdStorageMatrixType(storageTypeName)){
        if(arguments.size() == 1u && arguments[0] && SymbolTable::IsSimdRegisterMatrixType(arguments[0]->resolvedSimdType)){
            const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(storageTypeName);
            if(matInfo && IsDoubleSimdType(arguments[0]->resolvedSimdType) == matInfo->isDoubleReg){
                out.append(matInfo->storeCppFunc.data(), matInfo->storeCppFunc.size());
                out.push_back('(');
                m_emitter.emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
                out.append(", ");
                out.append(dstExprText.data(), dstExprText.size());
                out.push_back(')');
                return true;
            }
        }
        return false;
    }

    const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(storageTypeName);
    if(simdVecInfo && simdVecInfo->isMixed){
        const b8 isDouble = simdVecInfo->isDoubleReg;
        if(arguments.size() == 2u && arguments[0] && arguments[1] && SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && IsDoubleSimdType(arguments[0]->resolvedSimdType) == isDouble){
            out.append(isDouble ? "::StoreDoubleInt(" : "::StoreFloatInt(");
            m_emitter.emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
            out.append(", ");
            m_emitter.emitExpr(out, *arguments[1]);
            out.append(", ");
            out.append(dstExprText.data(), dstExprText.size());
            out.push_back(')');
            return true;
        }
        if(arguments.size() == 4u && arguments[0] && arguments[1] && arguments[2] && arguments[3]){
            const Expr* srcSimdVec = nullptr;
            b8 matchesXyz = true;
            for(u32 i = 0u; i < 3u; ++i){
                const Expr* laneBase = nullptr;
                u32 laneIdx = 0u;
                if(!SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], laneBase, laneIdx) || laneIdx != i || !laneBase || !SymbolTable::IsSimdRegisterVectorType(laneBase->resolvedSimdType) || IsDoubleSimdType(laneBase->resolvedSimdType) != isDouble){
                    matchesXyz = false;
                    break;
                }
                if(!srcSimdVec)
                    srcSimdVec = laneBase;
                else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*srcSimdVec, *laneBase)){
                    matchesXyz = false;
                    break;
                }
            }
            if(matchesXyz && srcSimdVec){
                out.append(isDouble ? "::StoreDoubleInt(" : "::StoreFloatInt(");
                m_emitter.emitSimdRegExpr(out, *srcSimdVec, srcSimdVec->resolvedSimdType);
                out.append(", ");
                m_emitter.emitExpr(out, *arguments[3]);
                out.append(", ");
                out.append(dstExprText.data(), dstExprText.size());
                out.push_back(')');
                return true;
            }
        }
        return false;
    }

    u32 lanes = 0u;
    b8 isDouble = false;
    b8 isIntLane = false;
    MStringView storeFn;
    if(simdVecInfo && !SymbolTable::IsSimdRegisterVectorType(storageTypeName)){
        lanes = simdVecInfo->lanes;
        isDouble = simdVecInfo->isDoubleReg;
        isIntLane = simdVecInfo->isIntReg;
        storeFn = simdVecInfo->storeCppFunc;
    }
    else if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(storageTypeName); unalignedInfo && !unalignedInfo->isHalfFloat){
        lanes = unalignedInfo->lanes;
        isDouble = (unalignedInfo->laneScalarType == "f64" || unalignedInfo->laneScalarType == "i64" || unalignedInfo->laneScalarType == "u64");
        isIntLane = BuiltinTypeSemantics::IsIntegerScalarType(unalignedInfo->laneScalarType);
        storeFn = isDouble ? (isIntLane ? "::StoreDoubleInt" : "::StoreDouble") : (isIntLane ? "::StoreInt" : "::StoreFloat");
    }
    if(storeFn.empty())
        return false;

    const auto emitStoreVecWithReplacementW = [&](const Expr& baseVec, const u32 baseZeroedAbove, const Expr& wArg){
        const MStringView baseSimdType = SymbolTable::IsSimdRegisterVectorType(baseVec.resolvedSimdType)
            ? baseVec.resolvedSimdType
            : (isDouble ? MStringView(isIntLane ? "VectorInt2" : "VectorDouble") : MStringView(isIntLane ? "VectorInt" : "VectorSingle"));
        out.append(storeFn.data(), storeFn.size());
        out.push_back('(');
        const Expr* wLaneBase = nullptr;
        u32 wLaneIdx = 0u;
        if(SimdExpressionAnalysis::TryMatchSingleLaneAccess(&wArg, wLaneBase, wLaneIdx) && wLaneBase && wLaneBase->resolvedSimdType == baseSimdType){
            if(SimdExpressionAnalysis::AreSamePureSimdExpr(baseVec, *wLaneBase)){
                if(wLaneIdx == 3u){
                    m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
                }
                else{
                    out.append(isDouble ? "::VectorDoubleSwizzle<0u, 1u, 2u, " : "::VectorSwizzle<0u, 1u, 2u, ");
                    AppendUnsignedDecimal(out, wLaneIdx);
                    out.append("u>(");
                    m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
                    out.push_back(')');
                }
            }
            else{
                out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, " : "::VectorPermute<0u, 1u, 2u, ");
                AppendUnsignedDecimal(out, 4u + wLaneIdx);
                out.append("u>(");
                m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
                out.append(", ");
                m_emitter.emitSimdRegExpr(out, *wLaneBase, baseSimdType);
                out.push_back(')');
            }
            out.append(", ");
        }
        else if(!IsIntSimdType(baseSimdType) && SimdExpressionAnalysis::TryMatchNegatedSingleLaneAccess(&wArg, wLaneBase, wLaneIdx) && wLaneBase && wLaneBase->resolvedSimdType == baseSimdType){
            out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, " : "::VectorPermute<0u, 1u, 2u, ");
            AppendUnsignedDecimal(out, 4u + wLaneIdx);
            out.append("u>(");
            m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
            out.append(isDouble ? ", ::VectorDoubleNegate(" : ", ::VectorNegate(");
            m_emitter.emitSimdRegExpr(out, *wLaneBase, baseSimdType);
            out.append(")), ");
        }
        else if(!IsIntSimdType(baseSimdType) && SimdConstantAnalysis::IsUnitFloatLiteral(wArg, &m_emitter.m_symbols)){
            out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, 7u>(" : "::VectorPermute<0u, 1u, 2u, 7u>(");
            m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
            out.append(isDouble ? ", ::s_DoubleSIMDIdentityR3), " : ", ::s_SIMDIdentityR3), ");
        }
        else if(!IsIntSimdType(baseSimdType) && SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(wArg, baseSimdType, m_emitter.m_symbols)){
            out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, 7u>(" : "::VectorPermute<0u, 1u, 2u, 7u>(");
            m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
            out.append(", ");
            m_emitter.emitSimdRegExpr(out, wArg, baseSimdType);
            out.append("), ");
        }
        else if(!IsIntSimdType(baseSimdType) && SimdExpressionAnalysis::IsZeroScalarLiteral(wArg, &m_emitter.m_symbols)){
            const u32 effZeroedAbove = SymbolTable::IsSimdRegisterVectorType(baseVec.resolvedSimdType)
                ? SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(baseVec, isDouble, &m_emitter.m_symbols, m_emitter.activeLocalVarDecls())
                : baseZeroedAbove;
            if(effZeroedAbove <= 3u){
                m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
                out.append(", ");
            }
            else if(const Expr* negInner = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(baseVec, baseSimdType, m_emitter.m_symbols); negInner && negInner->resolvedSimdType == baseSimdType && SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(*negInner, isDouble, &m_emitter.m_symbols, m_emitter.activeLocalVarDecls()) <= 3u){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                m_emitter.emitSimdRegExpr(out, *negInner, baseSimdType);
                out.append(isDouble ? ", ::s_DoubleSIMDNegate3), " : ", ::s_SIMDNegate3), ");
            }
            else{
                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
                out.append(isDouble ? ", ::s_DoubleSIMDMask3), " : ", ::s_SIMDMask3), ");
            }
        }
        else{
            out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
            m_emitter.emitSimdRegExpr(out, baseVec, baseSimdType);
            out.append(", ");
            m_emitter.emitExpr(out, wArg);
            out.append("), ");
        }
        out.append(dstExprText.data(), dstExprText.size());
        out.push_back(')');
    };

    if(arguments.size() == 1u && arguments[0] && SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && IsDoubleSimdType(arguments[0]->resolvedSimdType) == isDouble){
        out.append(storeFn.data(), storeFn.size());
        out.push_back('(');
        m_emitter.emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
        out.append(", ");
        out.append(dstExprText.data(), dstExprText.size());
        out.push_back(')');
        return true;
    }
    if(lanes == 4u && arguments.size() == 2u && arguments[0] && arguments[1] && arguments[1]->resolvedSimdType.empty()){
        if(SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && IsDoubleSimdType(arguments[0]->resolvedSimdType) == isDouble){
            emitStoreVecWithReplacementW(*arguments[0], 4u, *arguments[1]);
            return true;
        }
        if(!isIntLane){
            MStringView srcRegType;
            MStringView srcLoadFn;
            u32 srcZeroedAbove = 4u;
            if(SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*arguments[0], isDouble, srcRegType, srcLoadFn, srcZeroedAbove) && srcZeroedAbove >= 3u){
                emitStoreVecWithReplacementW(*arguments[0], srcZeroedAbove, *arguments[1]);
                return true;
            }
        }
    }
    if(arguments.size() == lanes && lanes >= 2u && lanes <= 4u){
        const Expr* srcSimdVec = nullptr;
        u32 srcZeroedAbove = 4u;
        b8 matchesPrefix = true;
        const u32 checkCount = (lanes == 4u) ? 3u : lanes;
        for(u32 i = 0u; i < checkCount; ++i){
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(!SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], laneBase, laneIdx) || laneIdx != i || !laneBase){
                matchesPrefix = false;
                break;
            }
            u32 candZeroedAbove = 4u;
            if(SymbolTable::IsSimdRegisterVectorType(laneBase->resolvedSimdType) && IsDoubleSimdType(laneBase->resolvedSimdType) == isDouble){
                candZeroedAbove = 4u;
            }
            else if(lanes == 4u && !isIntLane){
                MStringView candRegType;
                MStringView candLoadFn;
                if(!SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*laneBase, isDouble, candRegType, candLoadFn, candZeroedAbove) || candZeroedAbove < checkCount){
                    matchesPrefix = false;
                    break;
                }
            }
            else{
                matchesPrefix = false;
                break;
            }
            if(!srcSimdVec){
                srcSimdVec = laneBase;
                srcZeroedAbove = candZeroedAbove;
            }
            else if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*srcSimdVec, *laneBase)){
                matchesPrefix = false;
                break;
            }
        }
        if(matchesPrefix && srcSimdVec){
            if(lanes < 4u){
                out.append(storeFn.data(), storeFn.size());
                out.push_back('(');
                m_emitter.emitSimdRegExpr(out, *srcSimdVec, srcSimdVec->resolvedSimdType);
                out.append(", ");
                out.append(dstExprText.data(), dstExprText.size());
                out.push_back(')');
                return true;
            }
            if(arguments[3] && arguments[3]->resolvedSimdType.empty()){
                emitStoreVecWithReplacementW(*srcSimdVec, srcZeroedAbove, *arguments[3]);
                return true;
            }
        }
    }
    return false;
}

NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

