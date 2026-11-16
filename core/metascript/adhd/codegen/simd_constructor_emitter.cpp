// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_constructor_emitter.h"
#include "simd_emission_fragments.h"
#include "simd_optimization_analysis.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SimdConstructorEmitter::emit()const{
    MString& out = m_output;
    const MStringView simdType = m_simdType;
    const Span<const Expr* const> arguments = m_arguments;
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);

    if(SymbolTable::IsSimdMatrixType(simdType)){
        if(arguments.empty()){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        if(SimdConstantAnalysis::IsIdentityMatrixCtorArgs(arguments, m_emitter.m_symbols)){
            out.append(isDouble ? "::MatrixDoubleIdentity()" : "::MatrixIdentity()");
            return;
        }
        if(arguments.size() == 1u && arguments[0] && !arguments[0]->resolvedSimdType.empty()){
            if(SymbolTable::IsSimdStorageMatrixType(arguments[0]->resolvedSimdType)){
                const BuiltinSimdMatrixInfo* srcMatInfo = FindBuiltinSimdMatrixType(arguments[0]->resolvedSimdType);
                const MStringView loadFn = srcMatInfo ? srcMatInfo->loadCppFunc : (isDouble ? MStringView("::LoadDouble") : MStringView("::LoadFloat"));
                out.append(loadFn.data(), loadFn.size());
                out.push_back('(');
                m_emitter.emitExpr(out, *arguments[0]);
                out.push_back(')');
                return;
            }
            m_emitter.emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        u32 rowVecLanes = 0u;
        if((arguments.size() == 3u || arguments.size() == 4u) && arguments[0] && (SymbolTable::IsSimdVectorType(arguments[0]->resolvedSimdType, rowVecLanes) || !arguments[0]->resolvedUnalignedType.empty())){
            const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType);
            const MStringView rowSimdType = matInfo ? matInfo->rowVectorType : FindFloatSimdRegisterTypeName(isDouble);
            out.append(isDouble ? "::SIMDMatrixDouble{ .v = { " : "::SIMDMatrix{ .v = { ");
            for(usize i = 0u; i < arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(arguments[i])
                    m_emitter.emitSimdRegExpr(out, *arguments[i], rowSimdType);
            }
            if(arguments.size() == 3u)
                out.append(isDouble ? ", ::s_DoubleSIMDIdentityR3" : ", ::s_SIMDIdentityR3");
            out.append(" } }");
            return;
        }
        if(arguments.size() == 9u || arguments.size() == 12u || arguments.size() == 16u){
            if(arguments.size() == 16u){
                b8 allZero = true;
                for(const Expr* arg : arguments){
                    if(!arg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &m_emitter.m_symbols)){
                        allZero = false;
                        break;
                    }
                }
                if(allZero){
                    out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
                    return;
                }
            }
            const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType);
            const MStringView rowSimdType = matInfo ? matInfo->rowVectorType : FindFloatSimdRegisterTypeName(isDouble);
            const usize cols = (arguments.size() == 9u) ? 3u : 4u;
            const usize rows = (arguments.size() == 16u) ? 4u : 3u;
            out.append(isDouble ? "::SIMDMatrixDouble{ .v = { " : "::SIMDMatrix{ .v = { ");
            for(usize r = 0u; r < rows; ++r){
                if(r > 0u)
                    out.append(", ");
                SimdConstructorEmitter(m_emitter, out, rowSimdType, Span<const Expr* const>(arguments.data() + (r * cols), cols)).emit();
            }
            if(rows == 3u)
                out.append(isDouble ? ", ::s_DoubleSIMDIdentityR3" : ", ::s_SIMDIdentityR3");
            out.append(" } }");
            return;
        }
        if(arguments.size() == 1u && arguments[0] && SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[0], &m_emitter.m_symbols)){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        out.append(isDouble ? "::MatrixDoubleSet(" : "::MatrixSet(");
        for(usize i = 0u; i < 16u; ++i){
            if(i > 0u)
                out.append(", ");
            if(!arguments.empty() && arguments[0])
                m_emitter.emitExpr(out, *arguments[0]);
            else
                out.append(isDouble ? "0.0" : "0.0f");
        }
        out.push_back(')');
        return;
    }

    b8 allZeroArgs = true;
    for(const Expr* arg : arguments){
        if(!arg || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &m_emitter.m_symbols)){
            allZeroArgs = false;
            break;
        }
    }
    if(arguments.empty() || allZeroArgs){
        if(isInt)
            out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
        else
            out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
        return;
    }
    if(!isInt){
        const MStringView knownConst = SimdConstantAnalysis::TryMatchKnownSimdFloatVectorConstant(arguments, isDouble, &m_emitter.m_symbols);
        if(!knownConst.empty()){
            out.append(knownConst.data(), knownConst.size());
            return;
        }
    }
    if(arguments.size() == 1u && arguments[0]){
        MStringView loadFn;
        u32 srcLanes = 0u;
        if(SimdVectorSourceAnalysis::TryGetStorageVectorLoadInfo(*arguments[0], isDouble, loadFn, srcLanes)){
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            m_emitter.emitExpr(out, *arguments[0]);
            out.push_back(')');
            return;
        }
        if(SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && arguments[0]->resolvedSimdType != simdType){
            out.append(isDouble ? "::ConvertVectorSingleToDouble(" : "::ConvertVectorDoubleToSingle(");
            m_emitter.emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
            out.push_back(')');
            return;
        }
        m_emitter.emitSimdRegExpr(out, *arguments[0], simdType);
        return;
    }
    if(arguments.size() == 2u && arguments[0] && arguments[1] && arguments[1]->resolvedSimdType.empty()){
        MStringView loadFn;
        u32 srcLanes = 0u;
        if(SimdVectorSourceAnalysis::TryGetStorageVectorLoadInfo(*arguments[0], isDouble, loadFn, srcLanes)){
            SimdEmissionFragments::EmitSimdVecWithReplacedWLane(m_emitter, out, simdType, *arguments[0], loadFn, srcLanes, *arguments[1]);
            return;
        }
        if(arguments[0]->resolvedSimdType == simdType){
            SimdEmissionFragments::EmitSimdVecWithReplacedWLane(m_emitter, out, simdType, *arguments[0], {}, 4u, *arguments[1]);
            return;
        }
    }

    const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(simdType);
    const Expr* storageBase = nullptr;
    u32 firstStorageLane = 0u;
    MStringView prefixLoadFn;
    u32 prefixStorageLanes = 0u;
    if(
        simdVecInfo
        && !simdVecInfo->isMixed
        && SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[0], storageBase, firstStorageLane)
        && firstStorageLane == 0u
        && storageBase
        && SimdVectorSourceAnalysis::TryGetStorageVectorLoadInfo(*storageBase, isDouble, prefixLoadFn, prefixStorageLanes)
        && arguments.size() >= prefixStorageLanes
    ){
        b8 matchesStoragePrefix = true;
        for(u32 i = 1u; i < prefixStorageLanes; ++i){
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(!SimdExpressionAnalysis::TryMatchSingleLaneAccess(arguments[i], laneBase, laneIdx) || laneIdx != i || !SimdExpressionAnalysis::AreSamePureSimdExpr(*storageBase, *laneBase)){
                matchesStoragePrefix = false;
                break;
            }
        }
        if(matchesStoragePrefix){
            b8 trailingAllZero = true;
            for(usize i = prefixStorageLanes; i < arguments.size(); ++i){
                if(!arguments[i] || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[i], &m_emitter.m_symbols)){
                    trailingAllZero = false;
                    break;
                }
            }
            if(trailingAllZero){
                out.append(prefixLoadFn.data(), prefixLoadFn.size());
                out.push_back('(');
                m_emitter.emitExpr(out, *storageBase);
                out.push_back(')');
                return;
            }
            if(prefixStorageLanes == 3u && arguments.size() == 4u && arguments[3]){
                SimdEmissionFragments::EmitSimdVecWithReplacedWLane(m_emitter, out, simdType, *storageBase, prefixLoadFn, prefixStorageLanes, *arguments[3]);
                return;
            }
        }
    }

    if(!isInt && tryEmitCastConstructor(isDouble))
        return;
    if(tryEmitLaneOperation(isDouble, isInt))
        return;
    if(tryEmitLaneConstructor(isDouble, isInt))
        return;

    if(isInt){
        out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
        for(usize i = 0u; i < 4u; ++i){
            if(i > 0u)
                out.append(", ");
            out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
            if(i < arguments.size() && arguments[i])
                m_emitter.emitExpr(out, *arguments[i]);
            else
                out.append("0u");
            out.push_back(')');
        }
        out.push_back(')');
        return;
    }

    out.append(isDouble ? "::VectorDoubleSet(" : "::VectorSet(");
    for(usize i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        if(i < arguments.size() && arguments[i])
            m_emitter.emitExpr(out, *arguments[i]);
        else
            out.append(isDouble ? "0.0" : "0.0f");
    }
    out.push_back(')');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

