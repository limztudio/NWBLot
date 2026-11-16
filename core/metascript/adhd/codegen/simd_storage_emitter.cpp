// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "simd_storage_emitter.h"
#include "simd_emission_fragments.h"
#include "simd_optimization_analysis.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        const BuiltinScalarTypeInfo* laneInfo = BuiltinTypeSemantics::FindBuiltinScalarType(unalignedInfo->laneScalarType);
        isDouble = laneInfo && laneInfo->byteSize == 8u;
        isIntLane = laneInfo && laneInfo->isInteger;
        storeFn = CodegenSymbols::SimdStoreFunc(isDouble, isIntLane);
    }
    if(storeFn.empty())
        return false;

    const auto emitStoreVecWithReplacementW = [&](const Expr& baseVec, const u32 baseZeroedAbove, const Expr& wArg){
        const MStringView baseSimdType = SymbolTable::IsSimdRegisterVectorType(baseVec.resolvedSimdType)
            ? baseVec.resolvedSimdType
            : (isIntLane ? FindIntSimdStorageVectorTypeName(isDouble, true) : FindFloatSimdRegisterTypeName(isDouble));
        out.append(storeFn.data(), storeFn.size());
        out.push_back('(');
        SimdEmissionFragments::EmitSimdVecWithReplacedWLane(m_emitter, out, baseSimdType, baseVec, {}, baseZeroedAbove, wArg);
        out.append(", ");
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

