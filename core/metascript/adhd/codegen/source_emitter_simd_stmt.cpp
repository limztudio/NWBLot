// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_stmt{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TryGetStorageVectorLoadInfo(const Expr& srcExpr, const b8 targetIsDouble, MStringView& outLoadFn, u32& outLanes)noexcept{
    if(!srcExpr.resolvedSimdType.empty() && SymbolTable::IsSimdStorageVectorType(srcExpr.resolvedSimdType, outLanes)){
        const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(srcExpr.resolvedSimdType);
        if(!vecInfo || vecInfo->isDoubleReg != targetIsDouble)
            return false;
        if(vecInfo->isMixed){
            outLoadFn = vecInfo->isDoubleReg ? "::LoadDoubleInt" : "::LoadFloatInt";
            outLanes = 3u;
            return true;
        }
        outLoadFn = vecInfo->loadCppFunc;
        return !outLoadFn.empty();
    }
    if(!srcExpr.resolvedUnalignedType.empty()){
        const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(srcExpr.resolvedUnalignedType);
        if(!unalignedInfo)
            return false;
        outLanes = unalignedInfo->lanes;
        if(unalignedInfo->isHalfFloat){
            outLoadFn = targetIsDouble ? "::LoadDouble" : "::LoadHalf";
            return true;
        }
        const b8 srcIsDouble = (unalignedInfo->laneScalarType == "f64" || unalignedInfo->laneScalarType == "i64" || unalignedInfo->laneScalarType == "u64");
        if(srcIsDouble != targetIsDouble)
            return false;
        const b8 srcIsInt = IsIntegerScalarType(unalignedInfo->laneScalarType);
        outLoadFn = srcIsDouble ? (srcIsInt ? "::LoadDoubleInt" : "::LoadDouble") : (srcIsInt ? "::LoadInt" : "::LoadFloat");
        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitSimdVectorCtorReg(MString& out, const MStringView simdType, const Span<const Expr* const> arguments)const{
    using namespace __hidden_adhd_simd_stmt;

    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);

    if(SymbolTable::IsSimdMatrixType(simdType)){
        if(arguments.empty()){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        if(arguments.size() == 1u && arguments[0] && !arguments[0]->resolvedSimdType.empty()){
            if(SymbolTable::IsSimdStorageMatrixType(arguments[0]->resolvedSimdType)){
                const BuiltinSimdMatrixInfo* srcMatInfo = FindBuiltinSimdMatrixType(arguments[0]->resolvedSimdType);
                const MStringView loadFn = srcMatInfo ? srcMatInfo->loadCppFunc : (isDouble ? MStringView("::LoadDouble") : MStringView("::LoadFloat"));
                out.append(loadFn.data(), loadFn.size());
                out.push_back('(');
                emitExpr(out, *arguments[0]);
                out.push_back(')');
                return;
            }
            emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        if((arguments.size() == 3u || arguments.size() == 4u) && arguments[0] && SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType)){
            const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType);
            const MStringView rowSimdType = matInfo ? matInfo->rowVectorType : arguments[0]->resolvedSimdType;
            out.append(isDouble ? "::SIMDMatrixDouble{ .v = { " : "::SIMDMatrix{ .v = { ");
            for(usize i = 0u; i < arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(arguments[i])
                    emitSimdRegExpr(out, *arguments[i], rowSimdType);
            }
            if(arguments.size() == 3u)
                out.append(isDouble ? ", ::s_DoubleSIMDIdentityR3" : ", ::s_SIMDIdentityR3");
            out.append(" } }");
            return;
        }
        out.append(isDouble ? "::MatrixDoubleSet(" : "::MatrixSet(");
        const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType);
        const u32 elemCount = (arguments.size() == 9u || arguments.size() == 12u)
            ? static_cast<u32>(arguments.size())
            : (matInfo ? matInfo->elemCount : 16u);
        const b8 isSplat = (arguments.size() == 1u);
        if(elemCount == 9u){
            for(usize r = 0u; r < 3u; ++r){
                for(usize c = 0u; c < 3u; ++c){
                    if(r > 0u || c > 0u)
                        out.append(", ");
                    const usize argIdx = isSplat ? 0u : (r * 3u + c);
                    if(argIdx < arguments.size() && arguments[argIdx])
                        emitExpr(out, *arguments[argIdx]);
                    else
                        out.append(isDouble ? "0.0" : "0.0f");
                }
                out.append(isDouble ? ", 0.0" : ", 0.0f");
            }
            out.append(isDouble ? ", 0.0, 0.0, 0.0, 1.0)" : ", 0.0f, 0.0f, 0.0f, 1.0f)");
            return;
        }
        for(usize i = 0u; i < 16u; ++i){
            if(i > 0u)
                out.append(", ");
            const usize argIdx = isSplat ? 0u : i;
            if(i < elemCount && argIdx < arguments.size() && arguments[argIdx])
                emitExpr(out, *arguments[argIdx]);
            else if(i == 15u && elemCount == 12u)
                out.append(isDouble ? "1.0" : "1.0f");
            else
                out.append(isDouble ? "0.0" : "0.0f");
        }
        out.push_back(')');
        return;
    }

    b8 allZeroArgs = true;
    for(const Expr* arg : arguments){
        if(!arg || !IsZeroScalarLiteral(*arg)){
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
    if(arguments.size() == 1u && arguments[0]){
        MStringView loadFn;
        u32 srcLanes = 0u;
        if(TryGetStorageVectorLoadInfo(*arguments[0], isDouble, loadFn, srcLanes)){
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            emitExpr(out, *arguments[0]);
            out.push_back(')');
            return;
        }
        if(SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && arguments[0]->resolvedSimdType != simdType){
            out.append(isDouble ? "::ConvertVectorSingleToDouble(" : "::ConvertVectorDoubleToSingle(");
            emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
            out.push_back(')');
            return;
        }
        emitSimdRegExpr(out, *arguments[0], simdType);
        return;
    }
    if(arguments.size() == 2u && arguments[0] && arguments[1] && arguments[1]->resolvedSimdType.empty()){
        MStringView loadFn;
        u32 srcLanes = 0u;
        if(TryGetStorageVectorLoadInfo(*arguments[0], isDouble, loadFn, srcLanes)){
            if(srcLanes <= 3u && IsZeroScalarLiteral(*arguments[1])){
                out.append(loadFn.data(), loadFn.size());
                out.push_back('(');
                emitExpr(out, *arguments[0]);
                out.push_back(')');
                return;
            }
            out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            emitExpr(out, *arguments[0]);
            out.append("), ");
            emitExpr(out, *arguments[1]);
            out.push_back(')');
            return;
        }
        if(arguments[0]->resolvedSimdType == simdType){
            out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
            emitSimdRegExpr(out, *arguments[0], simdType);
            out.append(", ");
            emitExpr(out, *arguments[1]);
            out.push_back(')');
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
        && TryMatchSingleLaneAccess(arguments[0], storageBase, firstStorageLane)
        && firstStorageLane == 0u
        && storageBase
        && TryGetStorageVectorLoadInfo(*storageBase, isDouble, prefixLoadFn, prefixStorageLanes)
        && arguments.size() >= prefixStorageLanes
    ){
        b8 matchesStoragePrefix = true;
        for(u32 i = 1u; i < prefixStorageLanes; ++i){
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(!TryMatchSingleLaneAccess(arguments[i], laneBase, laneIdx) || laneIdx != i || !AreSamePureSimdExpr(*storageBase, *laneBase)){
                matchesStoragePrefix = false;
                break;
            }
        }
        if(matchesStoragePrefix){
            b8 trailingAllZero = true;
            for(usize i = prefixStorageLanes; i < arguments.size(); ++i){
                if(!arguments[i] || !IsZeroScalarLiteral(*arguments[i])){
                    trailingAllZero = false;
                    break;
                }
            }
            if(trailingAllZero){
                out.append(prefixLoadFn.data(), prefixLoadFn.size());
                out.push_back('(');
                emitExpr(out, *storageBase);
                out.push_back(')');
                return;
            }
            if(prefixStorageLanes == 3u && arguments.size() == 4u && arguments[3]){
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleSetIntW(" : "::VectorSetIntW(");
                else
                    out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
                out.append(prefixLoadFn.data(), prefixLoadFn.size());
                out.push_back('(');
                emitExpr(out, *storageBase);
                out.append("), ");
                if(isInt)
                    out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                emitExpr(out, *arguments[3]);
                if(isInt)
                    out.push_back(')');
                out.push_back(')');
                return;
            }
        }
    }

    Array<const Expr*, 4> laneBases{};
    Array<u32, 4> laneIndices{};
    for(usize i = 0u; i < 4u && i < arguments.size(); ++i){
        const Expr* base = nullptr;
        u32 idx = 0u;
        if(TryMatchSingleLaneAccess(arguments[i], base, idx) && base->resolvedSimdType == simdType){
            laneBases[i] = base;
            laneIndices[i] = idx;
        }
    }

    if(arguments.size() == 4u){
        for(u32 replaceLane = 0u; replaceLane < 4u; ++replaceLane){
            if(laneBases[replaceLane] != nullptr || !arguments[replaceLane])
                continue;
            const Expr* v0 = nullptr;
            b8 threeNaturalMatch = true;
            for(u32 i = 0u; i < 4u; ++i){
                if(i == replaceLane)
                    continue;
                if(!laneBases[i] || laneIndices[i] != i){
                    threeNaturalMatch = false;
                    break;
                }
                if(!v0)
                    v0 = laneBases[i];
                else if(!AreSamePureSimdExpr(*v0, *laneBases[i])){
                    threeNaturalMatch = false;
                    break;
                }
            }
            if(threeNaturalMatch && v0){
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
                else
                    out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
                const MStringView laneSuffix = SimdLaneCallSuffix(replaceLane);
                out.append(laneSuffix.data(), laneSuffix.size());
                emitSimdRegExpr(out, *v0, simdType);
                out.append(", ");
                if(isInt)
                    out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                emitExpr(out, *arguments[replaceLane]);
                if(isInt)
                    out.push_back(')');
                out.push_back(')');
                return;
            }
        }
    }

    {
        const Expr* v0 = nullptr;
        const Expr* v1 = nullptr;
        Array<u32, 4> permIdx{};
        b8 hasZeroLane = false;
        b8 canSwizzleOrPermute = true;
        for(u32 i = 0u; i < 4u; ++i){
            if(laneBases[i]){
                if(!v0){
                    v0 = laneBases[i];
                    permIdx[i] = laneIndices[i];
                }
                else if(AreSamePureSimdExpr(*v0, *laneBases[i])){
                    permIdx[i] = laneIndices[i];
                }
                else if(!v1 && !hasZeroLane){
                    v1 = laneBases[i];
                    permIdx[i] = 4u + laneIndices[i];
                }
                else if(v1 && AreSamePureSimdExpr(*v1, *laneBases[i])){
                    permIdx[i] = 4u + laneIndices[i];
                }
                else{
                    canSwizzleOrPermute = false;
                    break;
                }
            }
            else if(!v1 && (i >= arguments.size() || (arguments[i] && IsZeroScalarLiteral(*arguments[i])))){
                hasZeroLane = true;
                permIdx[i] = 4u + i;
            }
            else{
                canSwizzleOrPermute = false;
                break;
            }
        }
        if(canSwizzleOrPermute && v0){
            if(!v1 && !hasZeroLane){
                if(permIdx[0] == 0u && permIdx[1] == 1u && permIdx[2] == 2u && permIdx[3] == 3u){
                    emitSimdRegExpr(out, *v0, simdType);
                    return;
                }
                out.append(isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
                for(u32 i = 0u; i < 4u; ++i){
                    if(i > 0u)
                        out.append(", ");
                    AppendUnsignedDecimal(out, permIdx[i]);
                    out.push_back('u');
                }
                out.append(">(");
                emitSimdRegExpr(out, *v0, simdType);
                out.push_back(')');
                return;
            }
            out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
            for(u32 i = 0u; i < 4u; ++i){
                if(i > 0u)
                    out.append(", ");
                AppendUnsignedDecimal(out, permIdx[i]);
                out.push_back('u');
            }
            out.append(">(");
            emitSimdRegExpr(out, *v0, simdType);
            out.append(", ");
            if(v1){
                emitSimdRegExpr(out, *v1, simdType);
            }
            else if(isInt){
                out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
            }
            else{
                out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
            }
            out.push_back(')');
            return;
        }
    }

    if(
        arguments.size() == 4u
        && arguments[0] && arguments[1] && arguments[2] && arguments[3]
        && arguments[0]->resolvedSimdType.empty()
        && IsPureSimdAddressExpr(*arguments[0])
        && AreSamePureSimdExpr(*arguments[0], *arguments[1])
        && AreSamePureSimdExpr(*arguments[0], *arguments[2])
        && AreSamePureSimdExpr(*arguments[0], *arguments[3])
    ){
        emitSimdRegExpr(out, *arguments[0], simdType);
        return;
    }

    if(isInt){
        out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
        for(usize i = 0u; i < 4u; ++i){
            if(i > 0u)
                out.append(", ");
            out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
            if(i < arguments.size() && arguments[i])
                emitExpr(out, *arguments[i]);
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
            emitExpr(out, *arguments[i]);
        else
            out.append(isDouble ? "0.0" : "0.0f");
    }
    out.push_back(')');
}

b8 SourceEmitter::tryEmitStorageStoreFromSimdArgs(MString& out, const MStringView storageTypeName, const Span<const Expr* const> arguments, const MStringView dstExprText)const{
    if(arguments.empty())
        return false;

    if(SymbolTable::IsSimdStorageMatrixType(storageTypeName)){
        if(arguments.size() == 1u && arguments[0] && SymbolTable::IsSimdRegisterMatrixType(arguments[0]->resolvedSimdType)){
            const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(storageTypeName);
            if(matInfo && IsDoubleSimdType(arguments[0]->resolvedSimdType) == matInfo->isDoubleReg){
                out.append(matInfo->storeCppFunc.data(), matInfo->storeCppFunc.size());
                out.push_back('(');
                emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
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
            emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
            out.append(", ");
            emitExpr(out, *arguments[1]);
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
                if(!TryMatchSingleLaneAccess(arguments[i], laneBase, laneIdx) || laneIdx != i || !laneBase || !SymbolTable::IsSimdRegisterVectorType(laneBase->resolvedSimdType) || IsDoubleSimdType(laneBase->resolvedSimdType) != isDouble){
                    matchesXyz = false;
                    break;
                }
                if(!srcSimdVec)
                    srcSimdVec = laneBase;
                else if(!AreSamePureSimdExpr(*srcSimdVec, *laneBase)){
                    matchesXyz = false;
                    break;
                }
            }
            if(matchesXyz && srcSimdVec){
                out.append(isDouble ? "::StoreDoubleInt(" : "::StoreFloatInt(");
                emitSimdRegExpr(out, *srcSimdVec, srcSimdVec->resolvedSimdType);
                out.append(", ");
                emitExpr(out, *arguments[3]);
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
    MStringView storeFn;
    if(simdVecInfo && !SymbolTable::IsSimdRegisterVectorType(storageTypeName)){
        lanes = simdVecInfo->lanes;
        isDouble = simdVecInfo->isDoubleReg;
        storeFn = simdVecInfo->storeCppFunc;
    }
    else if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(storageTypeName); unalignedInfo && !unalignedInfo->isHalfFloat){
        lanes = unalignedInfo->lanes;
        isDouble = (unalignedInfo->laneScalarType == "f64" || unalignedInfo->laneScalarType == "i64" || unalignedInfo->laneScalarType == "u64");
        const b8 isIntLane = IsIntegerScalarType(unalignedInfo->laneScalarType);
        storeFn = isDouble ? (isIntLane ? "::StoreDoubleInt" : "::StoreDouble") : (isIntLane ? "::StoreInt" : "::StoreFloat");
    }
    if(storeFn.empty())
        return false;

    if(arguments.size() == 1u && arguments[0] && SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && IsDoubleSimdType(arguments[0]->resolvedSimdType) == isDouble){
        out.append(storeFn.data(), storeFn.size());
        out.push_back('(');
        emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
        out.append(", ");
        out.append(dstExprText.data(), dstExprText.size());
        out.push_back(')');
        return true;
    }
    if(lanes == 4u && arguments.size() == 2u && arguments[0] && arguments[1] && SymbolTable::IsSimdRegisterVectorType(arguments[0]->resolvedSimdType) && IsDoubleSimdType(arguments[0]->resolvedSimdType) == isDouble && arguments[1]->resolvedSimdType.empty()){
        out.append(storeFn.data(), storeFn.size());
        out.push_back('(');
        out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
        emitSimdRegExpr(out, *arguments[0], arguments[0]->resolvedSimdType);
        out.append(", ");
        emitExpr(out, *arguments[1]);
        out.append("), ");
        out.append(dstExprText.data(), dstExprText.size());
        out.push_back(')');
        return true;
    }
    if(arguments.size() == lanes && lanes >= 2u && lanes <= 4u){
        const Expr* srcSimdVec = nullptr;
        b8 matchesPrefix = true;
        const u32 checkCount = (lanes == 4u) ? 3u : lanes;
        for(u32 i = 0u; i < checkCount; ++i){
            const Expr* laneBase = nullptr;
            u32 laneIdx = 0u;
            if(!TryMatchSingleLaneAccess(arguments[i], laneBase, laneIdx) || laneIdx != i || !laneBase || !SymbolTable::IsSimdRegisterVectorType(laneBase->resolvedSimdType) || IsDoubleSimdType(laneBase->resolvedSimdType) != isDouble){
                matchesPrefix = false;
                break;
            }
            if(!srcSimdVec)
                srcSimdVec = laneBase;
            else if(!AreSamePureSimdExpr(*srcSimdVec, *laneBase)){
                matchesPrefix = false;
                break;
            }
        }
        if(matchesPrefix && srcSimdVec){
            if(lanes < 4u){
                out.append(storeFn.data(), storeFn.size());
                out.push_back('(');
                emitSimdRegExpr(out, *srcSimdVec, srcSimdVec->resolvedSimdType);
                out.append(", ");
                out.append(dstExprText.data(), dstExprText.size());
                out.push_back(')');
                return true;
            }
            const Expr* wLaneBase = nullptr;
            u32 wLaneIdx = 0u;
            if(TryMatchSingleLaneAccess(arguments[3], wLaneBase, wLaneIdx) && wLaneIdx == 3u && wLaneBase && AreSamePureSimdExpr(*srcSimdVec, *wLaneBase)){
                out.append(storeFn.data(), storeFn.size());
                out.push_back('(');
                emitSimdRegExpr(out, *srcSimdVec, srcSimdVec->resolvedSimdType);
                out.append(", ");
                out.append(dstExprText.data(), dstExprText.size());
                out.push_back(')');
                return true;
            }
            if(arguments[3] && arguments[3]->resolvedSimdType.empty()){
                out.append(storeFn.data(), storeFn.size());
                out.push_back('(');
                out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
                emitSimdRegExpr(out, *srcSimdVec, srcSimdVec->resolvedSimdType);
                out.append(", ");
                emitExpr(out, *arguments[3]);
                out.append("), ");
                out.append(dstExprText.data(), dstExprText.size());
                out.push_back(')');
                return true;
            }
        }
    }
    return false;
}

b8 SourceEmitter::isDirectSimdRegExpr(const Expr& expr)const noexcept{
    if(!IsRegisterPromotableSimdType(expr.resolvedSimdType))
        return false;
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle)
        return true;
    if(expr.kind == ExprKind::Unary)
        return true;
    if(expr.kind == ExprKind::Binary && expr.op != TokenType::Equal)
        return true;
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        if(expr.isBuiltinCall)
            return true;
        const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
        if(SymbolTable::IsSimdRegisterType(baseCallee))
            return true;
    }
    return false;
}

const SimdVarBinding* SourceEmitter::findSimdBinding(const b8 isThisMember, const MStringView name)const noexcept{
    if(!m_activeSimdBindings)
        return nullptr;
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isThisMember == isThisMember && binding.name == name)
            return &binding;
    }
    return nullptr;
}

const SimdVarBinding* SourceEmitter::findSimdBindingForExpr(const Expr* const expr)const noexcept{
    if(!expr)
        return nullptr;
    if(expr->kind == ExprKind::Identifier)
        return findSimdBinding(false, expr->text);
    if(expr->kind == ExprKind::ThisMember)
        return findSimdBinding(true, expr->text);
    return nullptr;
}

const SimdVarBinding* SourceEmitter::findSimdRootBindingForExpr(const Expr* const expr)const noexcept{
    if(!expr)
        return nullptr;
    if(const SimdVarBinding* direct = findSimdBindingForExpr(expr))
        return direct;
    if((expr->kind == ExprKind::MemberAccess || expr->kind == ExprKind::Index) && expr->lhs)
        return findSimdBindingForExpr(expr->lhs);
    return nullptr;
}

void SourceEmitter::emitSimdBindingInitialDecl(MString& out, const SimdVarBinding& binding, const b8 loadFromStorage, const b8 qualifyThisMember, const u32 indent)const{
    const MStringView regType = SimdRegisterCppType(binding.simdType);
    const MStringView loadFn = SimdLoadFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    if(!binding.written && !binding.needsStorageSync)
        out.append("const ");
    out.append(regType.data(), regType.size());
    out.push_back(' ');
    out.append(binding.regName.data(), binding.regName.size());
    if(loadFromStorage){
        out.append(" = ");
        out.append(loadFn.data(), loadFn.size());
        out.push_back('(');
        if(qualifyThisMember && binding.isThisMember)
            out.append("this->");
        out.append(binding.name.data(), binding.name.size());
        out.append(");\r\n");
    }
    else{
        out.append("{};\r\n");
    }
}

void SourceEmitter::emitSimdStoreForBinding(MString& out, const SimdVarBinding& binding, const u32 indent)const{
    const MStringView storeFn = SimdStoreFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    out.append(storeFn.data(), storeFn.size());
    out.push_back('(');
    out.append(binding.regName.data(), binding.regName.size());
    out.append(", ");
    if(binding.isThisMember)
        out.append("this->");
    out.append(binding.name.data(), binding.name.size());
    out.append(");\r\n");
}

void SourceEmitter::emitSimdLoadReloadForBinding(MString& out, const SimdVarBinding& binding, const u32 indent)const{
    const MStringView loadFn = SimdLoadFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    out.append(binding.regName.data(), binding.regName.size());
    out.append(" = ");
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    if(binding.isThisMember)
        out.append("this->");
    out.append(binding.name.data(), binding.name.size());
    out.append(");\r\n");
}

void SourceEmitter::emitSimdMutCallReloads(MString& out, const Expr& expr, const u32 indent)const{
    if(!m_activeSimdBindings)
        return;
    if(expr.lhs)
        emitSimdMutCallReloads(out, *expr.lhs, indent);
    if(expr.rhs)
        emitSimdMutCallReloads(out, *expr.rhs, indent);
    for(const Expr* arg : expr.arguments){
        if(arg)
            emitSimdMutCallReloads(out, *arg, indent);
    }
    if(expr.kind == ExprKind::Call){
        for(const Expr* arg : expr.arguments){
            if(!arg || !arg->isMutArg)
                continue;
            if(const SimdVarBinding* mutBinding = findSimdRootBindingForExpr(arg))
                emitSimdLoadReloadForBinding(out, *mutBinding, indent);
        }
        const b8 isThisMethodCall = (expr.lhs && expr.lhs->kind == ExprKind::ThisMember);
        for(const SimdVarBinding& binding : *m_activeSimdBindings){
            if(binding.capturedByMutLambda || (isThisMethodCall && binding.isThisMember))
                emitSimdLoadReloadForBinding(out, binding, indent);
        }
    }
}

void SourceEmitter::emitSimdBlockStores(MString& out, const u32 indent)const{
    if(!m_activeSimdBindings)
        return;
    b8 hasPendingStore = (m_activeSimdLoopBinding != nullptr);
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isLocalDecl || binding.isLoopElement || !binding.written)
            continue;
        hasPendingStore = true;
        break;
    }
    if(!hasPendingStore)
        return;
    if(m_activeScopeUsesTaskScope){
        emitIndent(out, indent);
        out.append("__adhd_task_scope.wait();\r\n");
    }
    if(m_activeSimdLoopBinding)
        emitSimdStoreForBinding(out, *m_activeSimdLoopBinding, indent);
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isLocalDecl || binding.isLoopElement || !binding.written)
            continue;
        emitSimdStoreForBinding(out, binding, indent);
    }
}

void SourceEmitter::emitScopedSimdBody(MString& out, const Span<const Stmt* const> statements, const u32 indent)const{
    MVector<SimdVarBinding> simdBindings(m_arena);
    CollectScopedSimdBindings(statements, m_symbols, m_arena, simdBindings);

    const MVector<SimdVarBinding>* prevBindings = m_activeSimdBindings;
    const SimdVarBinding* prevLoopBinding = m_activeSimdLoopBinding;
    const u32 prevInnerBreakDepth = m_activeSimdLoopInnerBreakDepth;
    const u32 prevInnerContinueDepth = m_activeSimdLoopInnerContinueDepth;
    const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
    m_activeSimdBindings = simdBindings.empty() ? nullptr : &simdBindings;
    m_activeSimdLoopBinding = nullptr;
    m_activeSimdLoopInnerBreakDepth = 0u;
    m_activeSimdLoopInnerContinueDepth = 0u;
    m_activeSwitchBreakLabelLine = 0u;

    for(const SimdVarBinding& binding : simdBindings){
        if(binding.isLocalDecl || binding.isLoopElement)
            continue;
        emitSimdBindingInitialDecl(out, binding, binding.readBeforeWrite || !binding.written, true, indent);
    }

    emitStatementList(out, statements, indent);

    const b8 endsWithTerminal = !statements.empty() && statements.back() && IsTerminalJumpStmtKind(statements.back()->kind);
    if(!endsWithTerminal)
        emitSimdBlockStores(out, indent);

    m_activeSimdBindings = prevBindings;
    m_activeSimdLoopBinding = prevLoopBinding;
    m_activeSimdLoopInnerBreakDepth = prevInnerBreakDepth;
    m_activeSimdLoopInnerContinueDepth = prevInnerContinueDepth;
    m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;
}

b8 SourceEmitter::tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.usesScratchArena || (stmt.varType && m_symbols.resolveRefMode(stmt.varType) != ParamPassingMode::None))
        return false;
    const MStringView varSimdType = ResolveLocalVarDeclSimdType(stmt, m_symbols);
    if(SymbolTable::IsSimdRegisterType(varSimdType)){
        const MStringView regType = SimdRegisterCppType(varSimdType);
        emitIndent(out, indent);
        if(stmt.isConst)
            out.append("const ");
        out.append(regType.data(), regType.size());
        out.push_back(' ');
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = ");
        if(stmt.expr && !stmt.isCtorInit)
            emitSimdRegExpr(out, *stmt.expr, varSimdType);
        else
            emitSimdVectorCtorReg(out, varSimdType, stmt.arguments);
        out.append(";\r\n");
        return true;
    }
    const MStringView baseVarType = stmt.varType ? m_symbols.resolveBaseTypeName(stmt.varType->name) : MStringView{};
    if(!baseVarType.empty() && (SymbolTable::IsSimdStorageType(baseVarType) || SymbolTable::IsUnalignedVectorType(baseVarType))){
        Span<const Expr* const> storeArgs;
        if(stmt.isCtorInit && !stmt.arguments.empty()){
            storeArgs = stmt.arguments;
        }
        else if(stmt.expr && stmt.expr->kind == ExprKind::Call && stmt.expr->lhs && stmt.expr->lhs->kind == ExprKind::Identifier && m_symbols.resolveBaseTypeName(stmt.expr->lhs->text) == baseVarType && !stmt.expr->arguments.empty()){
            storeArgs = stmt.expr->arguments;
        }
        if(!storeArgs.empty()){
            MString storeCallStr(m_arena);
            if(tryEmitStorageStoreFromSimdArgs(storeCallStr, baseVarType, storeArgs, stmt.name)){
                emitIndent(out, indent);
                m_typeEmitter.emitTypeCpp(out, *stmt.varType);
                out.push_back(' ');
                out.append(stmt.name.data(), stmt.name.size());
                out.append("{};\r\n");
                emitIndent(out, indent);
                out.append(storeCallStr.data(), storeCallStr.size());
                out.append(";\r\n");
                return true;
            }
        }
    }
    return false;
}

b8 SourceEmitter::tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.expr && stmt.expr->kind == ExprKind::Binary && IsCompoundOrSimpleAssignment(stmt.expr->op) && stmt.expr->lhs && stmt.expr->rhs){
        const Expr& lhs = *stmt.expr->lhs;
        const Expr& rhs = *stmt.expr->rhs;

        if(IsRegisterPromotableSimdType(lhs.resolvedSimdType)){
            const MStringView simdType = lhs.resolvedSimdType;
            emitIndent(out, indent);
            emitExpr(out, lhs);
            out.append(" = ");
            if(stmt.expr->op == TokenType::Equal || !tryEmitSimdBinaryOpReg(out, MapSimdAssignToBinaryOp(stmt.expr->op), lhs, rhs, simdType))
                emitSimdRegExpr(out, rhs, simdType);
            out.append(";\r\n");
            return true;
        }

        if(stmt.expr->op == TokenType::Equal && rhs.kind == ExprKind::Call && rhs.lhs && rhs.lhs->kind == ExprKind::Identifier && !rhs.arguments.empty()){
            const MStringView rhsBaseCallee = m_symbols.resolveBaseTypeName(rhs.lhs->text);
            if(SymbolTable::IsSimdStorageType(rhsBaseCallee) || SymbolTable::IsUnalignedVectorType(rhsBaseCallee)){
                MString lhsExprText(m_arena);
                emitExpr(lhsExprText, lhs);
                MString storeCallStr(m_arena);
                if(tryEmitStorageStoreFromSimdArgs(storeCallStr, rhsBaseCallee, rhs.arguments, MStringView(lhsExprText.data(), lhsExprText.size()))){
                    emitIndent(out, indent);
                    out.append(storeCallStr.data(), storeCallStr.size());
                    out.append(";\r\n");
                    return true;
                }
            }
        }

        u32 lane = 0u;
        const b8 isStaticLaneAssign = lhs.kind == ExprKind::MemberAccess && lhs.lhs
            && SymbolTable::IsSimdRegisterVectorType(lhs.lhs->resolvedSimdType)
            && IsSingleSwizzleLane(lhs.text, lane);
        const b8 isDynamicIndexAssign = lhs.kind == ExprKind::Index && lhs.lhs && lhs.rhs
            && SymbolTable::IsSimdRegisterVectorType(lhs.lhs->resolvedSimdType);
        if(isStaticLaneAssign || isDynamicIndexAssign){
            const MStringView simdType = lhs.lhs->resolvedSimdType;
            const b8 isDouble = IsDoubleSimdType(simdType);
            const b8 isInt = IsIntSimdType(simdType);

            emitIndent(out, indent);
            emitExpr(out, *lhs.lhs);
            out.append(" = ");
            if(isInt)
                out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
            else
                out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
            if(isDynamicIndexAssign){
                out.append("ByIndex(");
            }
            else{
                const MStringView laneSuffix = SimdLaneCallSuffix(lane);
                out.append(laneSuffix.data(), laneSuffix.size());
            }
            emitExpr(out, *lhs.lhs);
            out.append(", ");
            if(isInt)
                out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
            if(stmt.expr->op == TokenType::Equal){
                emitExpr(out, rhs);
            }
            else{
                const MStringView opSym = SimdBinaryScalarOpSymbol(MapSimdAssignToBinaryOp(stmt.expr->op));
                out.push_back('(');
                emitExpr(out, lhs);
                out.append(opSym.data(), opSym.size());
                out.push_back('(');
                emitExpr(out, rhs);
                out.append("))");
            }
            if(isInt)
                out.push_back(')');
            if(isDynamicIndexAssign){
                out.append(", static_cast<usize>(");
                emitExpr(out, *lhs.rhs);
                out.append("));\r\n");
            }
            else{
                out.append(");\r\n");
            }
            return true;
        }
    }
    return false;
}

b8 SourceEmitter::tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.wrapReturnNotNull)
        return false;
    if(stmt.expr && !stmt.expr->wrapInSimdResult && stmt.expr->kind == ExprKind::Call && stmt.expr->lhs && stmt.expr->lhs->kind == ExprKind::Identifier && !stmt.expr->arguments.empty()){
        const MStringView baseCallee = m_symbols.resolveBaseTypeName(stmt.expr->lhs->text);
        if(SymbolTable::IsSimdStorageType(baseCallee) || SymbolTable::IsUnalignedVectorType(baseCallee)){
            MString storeCallStr(m_arena);
            if(tryEmitStorageStoreFromSimdArgs(storeCallStr, baseCallee, stmt.expr->arguments, "__adhd_simd_ret")){
                TypeRef retType(m_arena);
                retType.name = stmt.expr->lhs->text;
                emitIndent(out, indent);
                m_typeEmitter.emitTypeCpp(out, retType);
                out.append(" __adhd_simd_ret{};\r\n");
                emitIndent(out, indent);
                out.append(storeCallStr.data(), storeCallStr.size());
                out.append(";\r\n");
                emitIndent(out, indent);
                out.append("return __adhd_simd_ret;\r\n");
                return true;
            }
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

