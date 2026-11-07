// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitSimdVectorCtorReg(MString& out, const MStringView simdType, const Span<const Expr* const> arguments)const{
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);

    if(SymbolTable::IsSimdMatrixType(simdType)){
        if(arguments.empty()){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        if(IsIdentityMatrixCtorArgs(arguments, m_symbols)){
            out.append(isDouble ? "::MatrixDoubleIdentity()" : "::MatrixIdentity()");
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
        u32 rowVecLanes = 0u;
        if((arguments.size() == 3u || arguments.size() == 4u) && arguments[0] && (SymbolTable::IsSimdVectorType(arguments[0]->resolvedSimdType, rowVecLanes) || !arguments[0]->resolvedUnalignedType.empty())){
            const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType);
            const MStringView rowSimdType = matInfo ? matInfo->rowVectorType : (isDouble ? MStringView("VectorDouble") : MStringView("VectorSingle"));
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
        if(arguments.size() == 9u || arguments.size() == 12u || arguments.size() == 16u){
            if(arguments.size() == 16u){
                b8 allZero = true;
                for(const Expr* arg : arguments){
                    if(!arg || !IsZeroScalarLiteral(*arg, &m_symbols)){
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
            const MStringView rowSimdType = matInfo ? matInfo->rowVectorType : (isDouble ? MStringView("VectorDouble") : MStringView("VectorSingle"));
            const usize cols = (arguments.size() == 9u) ? 3u : 4u;
            const usize rows = (arguments.size() == 16u) ? 4u : 3u;
            out.append(isDouble ? "::SIMDMatrixDouble{ .v = { " : "::SIMDMatrix{ .v = { ");
            for(usize r = 0u; r < rows; ++r){
                if(r > 0u)
                    out.append(", ");
                emitSimdVectorCtorReg(out, rowSimdType, Span<const Expr* const>(arguments.data() + (r * cols), cols));
            }
            if(rows == 3u)
                out.append(isDouble ? ", ::s_DoubleSIMDIdentityR3" : ", ::s_SIMDIdentityR3");
            out.append(" } }");
            return;
        }
        if(arguments.size() == 1u && arguments[0] && IsZeroScalarLiteral(*arguments[0], &m_symbols)){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        out.append(isDouble ? "::MatrixDoubleSet(" : "::MatrixSet(");
        for(usize i = 0u; i < 16u; ++i){
            if(i > 0u)
                out.append(", ");
            if(!arguments.empty() && arguments[0])
                emitExpr(out, *arguments[0]);
            else
                out.append(isDouble ? "0.0" : "0.0f");
        }
        out.push_back(')');
        return;
    }

    b8 allZeroArgs = true;
    for(const Expr* arg : arguments){
        if(!arg || !IsZeroScalarLiteral(*arg, &m_symbols)){
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
        const MStringView knownConst = TryMatchKnownSimdFloatVectorConstant(arguments, isDouble, &m_symbols);
        if(!knownConst.empty()){
            out.append(knownConst.data(), knownConst.size());
            return;
        }
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
        const Expr* wLaneBase = nullptr;
        u32 wLaneIdx = 0u;
        const b8 wIsSimdLane = TryMatchSingleLaneAccess(arguments[1], wLaneBase, wLaneIdx)
            && wLaneBase
            && wLaneBase->resolvedSimdType == simdType;

        MStringView loadFn;
        u32 srcLanes = 0u;
        if(TryGetStorageVectorLoadInfo(*arguments[0], isDouble, loadFn, srcLanes)){
            if(srcLanes <= 3u && IsZeroScalarLiteral(*arguments[1], &m_symbols)){
                out.append(loadFn.data(), loadFn.size());
                out.push_back('(');
                emitExpr(out, *arguments[0]);
                out.push_back(')');
                return;
            }
            if(!isInt && IsZeroScalarLiteral(*arguments[1], &m_symbols)){
                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                out.append(loadFn.data(), loadFn.size());
                out.push_back('(');
                emitExpr(out, *arguments[0]);
                out.append(isDouble ? "), ::s_DoubleSIMDMask3)" : "), ::s_SIMDMask3)");
                return;
            }
            if(wIsSimdLane){
                out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, " : "::VectorPermute<0u, 1u, 2u, ");
                AppendUnsignedDecimal(out, 4u + wLaneIdx);
                out.append("u>(");
                out.append(loadFn.data(), loadFn.size());
                out.push_back('(');
                emitExpr(out, *arguments[0]);
                out.append("), ");
                emitSimdRegExpr(out, *wLaneBase, simdType);
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
            if(!isInt && IsZeroScalarLiteral(*arguments[1], &m_symbols)){
                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                emitSimdRegExpr(out, *arguments[0], simdType);
                out.append(isDouble ? ", ::s_DoubleSIMDMask3)" : ", ::s_SIMDMask3)");
                return;
            }
            if(wIsSimdLane){
                if(AreSamePureSimdExpr(*arguments[0], *wLaneBase)){
                    if(wLaneIdx == 3u){
                        emitSimdRegExpr(out, *arguments[0], simdType);
                        return;
                    }
                    out.append(isDouble ? "::VectorDoubleSwizzle<0u, 1u, 2u, " : "::VectorSwizzle<0u, 1u, 2u, ");
                    AppendUnsignedDecimal(out, wLaneIdx);
                    out.append("u>(");
                    emitSimdRegExpr(out, *arguments[0], simdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, " : "::VectorPermute<0u, 1u, 2u, ");
                AppendUnsignedDecimal(out, 4u + wLaneIdx);
                out.append("u>(");
                emitSimdRegExpr(out, *arguments[0], simdType);
                out.append(", ");
                emitSimdRegExpr(out, *wLaneBase, simdType);
                out.push_back(')');
                return;
            }
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
                if(!arguments[i] || !IsZeroScalarLiteral(*arguments[i], &m_symbols)){
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
                const Expr* wLaneBase = nullptr;
                u32 wLaneIdx = 0u;
                if(TryMatchSingleLaneAccess(arguments[3], wLaneBase, wLaneIdx) && wLaneBase && wLaneBase->resolvedSimdType == simdType){
                    out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, " : "::VectorPermute<0u, 1u, 2u, ");
                    AppendUnsignedDecimal(out, 4u + wLaneIdx);
                    out.append("u>(");
                    out.append(prefixLoadFn.data(), prefixLoadFn.size());
                    out.push_back('(');
                    emitExpr(out, *storageBase);
                    out.append("), ");
                    emitSimdRegExpr(out, *wLaneBase, simdType);
                    out.push_back(')');
                    return;
                }
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
            if(i < arguments.size() && TryMatchCastSingleLaneAccess(arguments[i], targetScalarType, m_symbols, base, laneIdx) && base){
                MStringView baseRegType;
                MStringView baseLoadFn;
                u32 baseZeroedAbove = 4u;
                if(!TryGetFloatVectorSourceInfo(*base, !isDouble, baseRegType, baseLoadFn, baseZeroedAbove)){
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
                else if(AreSamePureSimdExpr(*cV0, *base)){
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
                else if(cV1 && AreSamePureSimdExpr(*cV1, *base)){
                    cPermIdx[i] = 4u + laneIdx;
                    cAllInPlaceOrZero = false;
                }
                else{
                    cValid = false;
                    break;
                }
            }
            else if(!cV1 && (i >= arguments.size() || (arguments[i] && IsZeroScalarLiteral(*arguments[i], &m_symbols)))){
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
                    emitSimdRegExpr(out, srcVec, regType);
                }
                else{
                    out.append(loadFn.data(), loadFn.size());
                    out.push_back('(');
                    emitExpr(out, srcVec);
                    out.push_back(')');
                }
            };
            if(!cV1 && cAllInPlaceOrZero){
                const u8 loadedZeroMask = static_cast<u8>((1u << cV0ZeroedAbove) - 1u);
                if(cInPlaceMask == 0xFu || cInPlaceMask == loadedZeroMask){
                    out.append(isDouble ? "::ConvertVectorSingleToDouble(" : "::ConvertVectorDoubleToSingle(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.push_back(')');
                    return;
                }
                const MStringView maskConst = SimdInPlaceLaneMaskConstant(cInPlaceMask, isDouble);
                if(!maskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndInt(::ConvertVectorSingleToDouble(" : "::VectorAndInt(::ConvertVectorDoubleToSingle(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append("), ");
                    out.append(maskConst.data(), maskConst.size());
                    out.push_back(')');
                    return;
                }
                const MStringView clearMaskConst = SimdInPlaceLaneMaskConstant(static_cast<u8>(0xFu ^ cInPlaceMask), isDouble);
                if(!clearMaskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndCInt(::ConvertVectorSingleToDouble(" : "::VectorAndCInt(::ConvertVectorDoubleToSingle(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append("), ");
                    out.append(clearMaskConst.data(), clearMaskConst.size());
                    out.push_back(')');
                    return;
                }
            }
            if(!cV1 && !cAllInPlaceOrZero && !cHasZeroLane){
                if(isDouble){
                    out.append("::ConvertVectorSingleToDouble(::VectorSwizzle<");
                    for(u32 i = 0u; i < 4u; ++i){
                        if(i > 0u)
                            out.append(", ");
                        AppendUnsignedDecimal(out, cPermIdx[i]);
                        out.push_back('u');
                    }
                    out.append(">(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append("))");
                }
                else{
                    out.append("::VectorSwizzle<");
                    for(u32 i = 0u; i < 4u; ++i){
                        if(i > 0u)
                            out.append(", ");
                        AppendUnsignedDecimal(out, cPermIdx[i]);
                        out.push_back('u');
                    }
                    out.append(">(::ConvertVectorDoubleToSingle(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append("))");
                }
                return;
            }
            if(cV1 && !cHasZeroLane){
                if(isDouble){
                    out.append("::ConvertVectorSingleToDouble(::VectorPermute<");
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
                }
                else{
                    out.append("::VectorPermute<");
                    for(u32 i = 0u; i < 4u; ++i){
                        if(i > 0u)
                            out.append(", ");
                        AppendUnsignedDecimal(out, cPermIdx[i]);
                        out.push_back('u');
                    }
                    out.append(">(::ConvertVectorDoubleToSingle(");
                    emitCastSourceVec(*cV0, cV0RegType, cV0LoadFn);
                    out.append("), ::ConvertVectorDoubleToSingle(");
                    emitCastSourceVec(*cV1, cV1RegType, cV1LoadFn);
                    out.append("))");
                }
                return;
            }
        }
    }

    if(!isInt && (arguments.size() == 3u || arguments.size() == 4u) && arguments[0] && arguments[0]->kind == ExprKind::Binary){
        const TokenType::Enum binOp = arguments[0]->op;
        if(binOp == TokenType::Plus || binOp == TokenType::Minus || binOp == TokenType::Star || binOp == TokenType::Slash){
            u32 activeLanes = 0u;
            const Expr* lhsVec = nullptr;
            const Expr* lhsScalar = nullptr;
            u32 lhsZeroedAbove = 4u;
            const Expr* rhsVec = nullptr;
            const Expr* rhsScalar = nullptr;
            u32 rhsZeroedAbove = 4u;
            b8 binMatch = true;
            for(u32 i = 0u; i < static_cast<u32>(arguments.size()); ++i){
                const Expr* arg = arguments[i];
                if(!arg){
                    binMatch = false;
                    break;
                }
                if(IsZeroScalarLiteral(*arg, &m_symbols)){
                    for(u32 j = i + 1u; j < static_cast<u32>(arguments.size()); ++j){
                        if(!arguments[j] || !IsZeroScalarLiteral(*arguments[j], &m_symbols)){
                            binMatch = false;
                            break;
                        }
                    }
                    break;
                }
                if(arg->kind != ExprKind::Binary || arg->op != binOp || !arg->lhs || !arg->rhs){
                    binMatch = false;
                    break;
                }
                const Expr* curLhsBase = nullptr;
                u32 curLhsLane = 0u;
                MStringView curLhsRegType;
                MStringView curLhsLoadFn;
                u32 curLhsZeroedAbove = 4u;
                if(
                    TryMatchSingleLaneAccess(arg->lhs, curLhsBase, curLhsLane)
                    && curLhsBase
                    && curLhsLane == i
                    && TryGetFloatVectorSourceInfo(*curLhsBase, isDouble, curLhsRegType, curLhsLoadFn, curLhsZeroedAbove)
                    && i < curLhsZeroedAbove
                ){
                    if(lhsScalar || (lhsVec && !AreSamePureSimdExpr(*lhsVec, *curLhsBase))){
                        binMatch = false;
                        break;
                    }
                    lhsVec = curLhsBase;
                    lhsZeroedAbove = curLhsZeroedAbove;
                }
                else if(!lhsVec && arg->lhs->resolvedSimdType.empty() && arg->lhs->resolvedUnalignedType.empty() && IsPureSimdAddressExpr(*arg->lhs)){
                    if(lhsScalar && !AreSamePureSimdExpr(*lhsScalar, *arg->lhs)){
                        binMatch = false;
                        break;
                    }
                    lhsScalar = arg->lhs;
                }
                else{
                    binMatch = false;
                    break;
                }

                const Expr* curRhsBase = nullptr;
                u32 curRhsLane = 0u;
                MStringView curRhsRegType;
                MStringView curRhsLoadFn;
                u32 curRhsZeroedAbove = 4u;
                if(
                    TryMatchSingleLaneAccess(arg->rhs, curRhsBase, curRhsLane)
                    && curRhsBase
                    && curRhsLane == i
                    && TryGetFloatVectorSourceInfo(*curRhsBase, isDouble, curRhsRegType, curRhsLoadFn, curRhsZeroedAbove)
                    && i < curRhsZeroedAbove
                ){
                    if(rhsScalar || (rhsVec && !AreSamePureSimdExpr(*rhsVec, *curRhsBase))){
                        binMatch = false;
                        break;
                    }
                    rhsVec = curRhsBase;
                    rhsZeroedAbove = curRhsZeroedAbove;
                }
                else if(!rhsVec && arg->rhs->resolvedSimdType.empty() && arg->rhs->resolvedUnalignedType.empty() && IsPureSimdAddressExpr(*arg->rhs)){
                    if(rhsScalar && !AreSamePureSimdExpr(*rhsScalar, *arg->rhs)){
                        binMatch = false;
                        break;
                    }
                    rhsScalar = arg->rhs;
                }
                else{
                    binMatch = false;
                    break;
                }
                ++activeLanes;
            }
            if(binMatch && activeLanes >= 2u && (lhsVec || rhsVec)){
                const b8 alreadyZeroedAbove = (activeLanes == 4u)
                    || (
                        (binOp == TokenType::Plus || binOp == TokenType::Minus || binOp == TokenType::Star)
                        && lhsVec
                        && rhsVec
                        && lhsZeroedAbove == activeLanes
                        && rhsZeroedAbove == activeLanes
                    );
                const u8 activeMask = static_cast<u8>((1u << activeLanes) - 1u);
                const MStringView maskConst = !alreadyZeroedAbove ? SimdInPlaceLaneMaskConstant(activeMask, isDouble) : MStringView{};
                if(alreadyZeroedAbove || !maskConst.empty()){
                    if(!alreadyZeroedAbove)
                        out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                    const Expr& emitLhs = lhsVec ? *lhsVec : *lhsScalar;
                    const Expr& emitRhs = rhsVec ? *rhsVec : *rhsScalar;
                    (void)tryEmitSimdBinaryOpReg(out, binOp, emitLhs, emitRhs, simdType);
                    if(!alreadyZeroedAbove){
                        out.append(", ");
                        out.append(maskConst.data(), maskConst.size());
                        out.push_back(')');
                    }
                    return;
                }
            }
        }
    }

    if(
        !isInt
        && (arguments.size() == 3u || arguments.size() == 4u)
        && arguments[0]
        && arguments[0]->kind == ExprKind::Call
        && arguments[0]->isBuiltinCall
        && arguments[0]->lhs
        && arguments[0]->lhs->kind == ExprKind::Identifier
    ){
        const MStringView callee = arguments[0]->lhs->text;
        const usize callArity = arguments[0]->arguments.size();
        const b8 supportedBuiltin = (callArity >= 1u && callArity <= 3u)
            && (!MapSimdUnaryFloatIntrinsic(callee, isDouble).empty() || (callee == "lerp" && callArity == 3u));
        if(supportedBuiltin){
            u32 activeLanes = 0u;
            Array<const Expr*, 3> paramVec{};
            Array<const Expr*, 3> paramScalar{};
            Array<u32, 3> paramZeroedAbove{4u, 4u, 4u};
            b8 callMatch = true;
            for(u32 i = 0u; i < static_cast<u32>(arguments.size()); ++i){
                const Expr* arg = arguments[i];
                if(!arg){
                    callMatch = false;
                    break;
                }
                if(IsZeroScalarLiteral(*arg, &m_symbols)){
                    for(u32 j = i + 1u; j < static_cast<u32>(arguments.size()); ++j){
                        if(!arguments[j] || !IsZeroScalarLiteral(*arguments[j], &m_symbols)){
                            callMatch = false;
                            break;
                        }
                    }
                    break;
                }
                if(
                    arg->kind != ExprKind::Call
                    || !arg->isBuiltinCall
                    || !arg->lhs
                    || arg->lhs->kind != ExprKind::Identifier
                    || arg->lhs->text != callee
                    || arg->arguments.size() != callArity
                ){
                    callMatch = false;
                    break;
                }
                for(usize pIdx = 0u; pIdx < callArity; ++pIdx){
                    const Expr* subArg = arg->arguments[pIdx];
                    if(!subArg){
                        callMatch = false;
                        break;
                    }
                    const Expr* curBase = nullptr;
                    u32 curLane = 0u;
                    MStringView curRegType;
                    MStringView curLoadFn;
                    u32 curZeroedAbove = 4u;
                    if(
                        TryMatchSingleLaneAccess(subArg, curBase, curLane)
                        && curBase
                        && curLane == i
                        && TryGetFloatVectorSourceInfo(*curBase, isDouble, curRegType, curLoadFn, curZeroedAbove)
                        && i < curZeroedAbove
                    ){
                        if(paramScalar[pIdx] || (paramVec[pIdx] && !AreSamePureSimdExpr(*paramVec[pIdx], *curBase))){
                            callMatch = false;
                            break;
                        }
                        paramVec[pIdx] = curBase;
                        paramZeroedAbove[pIdx] = curZeroedAbove;
                    }
                    else if(!paramVec[pIdx] && subArg->resolvedSimdType.empty() && subArg->resolvedUnalignedType.empty() && IsPureSimdAddressExpr(*subArg)){
                        if(paramScalar[pIdx] && !AreSamePureSimdExpr(*paramScalar[pIdx], *subArg)){
                            callMatch = false;
                            break;
                        }
                        paramScalar[pIdx] = subArg;
                    }
                    else{
                        callMatch = false;
                        break;
                    }
                }
                if(!callMatch)
                    break;
                ++activeLanes;
            }
            const b8 hasAnyVecParam = paramVec[0] != nullptr || paramVec[1] != nullptr || paramVec[2] != nullptr;
            if(callMatch && activeLanes >= 2u && hasAnyVecParam){
                b8 allParamsZeroedAtActive = true;
                for(usize pIdx = 0u; pIdx < callArity; ++pIdx){
                    if(!paramVec[pIdx] || paramZeroedAbove[pIdx] != activeLanes){
                        allParamsZeroedAtActive = false;
                        break;
                    }
                }
                const b8 zeroPreservingFn = callee == "abs"
                    || callee == "sqrt"
                    || callee == "floor"
                    || callee == "ceil"
                    || callee == "round"
                    || callee == "trunc"
                    || callee == "frac"
                    || callee == "saturate"
                    || callee == "sin"
                    || callee == "tan"
                    || callee == "asin"
                    || callee == "atan"
                    || callee == "min"
                    || callee == "max"
                    || callee == "clamp"
                    || callee == "lerp";
                const b8 alreadyZeroedAbove = (activeLanes == 4u) || (zeroPreservingFn && allParamsZeroedAtActive);
                const u8 activeMask = static_cast<u8>((1u << activeLanes) - 1u);
                const MStringView maskConst = !alreadyZeroedAbove ? SimdInPlaceLaneMaskConstant(activeMask, isDouble) : MStringView{};
                if(alreadyZeroedAbove || !maskConst.empty()){
                    if(!alreadyZeroedAbove)
                        out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                    Expr synthCall(m_arena);
                    synthCall.kind = ExprKind::Call;
                    synthCall.isBuiltinCall = true;
                    synthCall.lhs = arguments[0]->lhs;
                    synthCall.resolvedSimdType = simdType;
                    for(usize pIdx = 0u; pIdx < callArity; ++pIdx){
                        const Expr* chosen = paramVec[pIdx] ? paramVec[pIdx] : paramScalar[pIdx];
                        synthCall.arguments.push_back(const_cast<Expr*>(chosen));
                    }
                    emitSimdRegExpr(out, synthCall, simdType);
                    if(!alreadyZeroedAbove){
                        out.append(", ");
                        out.append(maskConst.data(), maskConst.size());
                        out.push_back(')');
                    }
                    return;
                }
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
            if(i < arguments.size() && TryMatchSingleLaneAccess(arguments[i], base, idx) && base){
                MStringView baseRegType;
                MStringView baseLoadFn;
                u32 baseZeroedAbove = 4u;
                if(idx != i || !TryGetFloatVectorSourceInfo(*base, isDouble, baseRegType, baseLoadFn, baseZeroedAbove) || idx >= baseZeroedAbove){
                    allInPlaceOrZero = false;
                    break;
                }
                if(!inPlaceBase){
                    inPlaceBase = base;
                    inPlaceRegType = baseRegType;
                    inPlaceLoadFn = baseLoadFn;
                    inPlaceZeroedAbove = baseZeroedAbove;
                }
                else if(!AreSamePureSimdExpr(*inPlaceBase, *base)){
                    allInPlaceOrZero = false;
                    break;
                }
                inPlaceMask |= static_cast<u8>(1u << i);
            }
            else if(i < arguments.size() && (!arguments[i] || !IsZeroScalarLiteral(*arguments[i], &m_symbols))){
                allInPlaceOrZero = false;
                break;
            }
        }
        if(allInPlaceOrZero && inPlaceBase && (!inPlaceRegType.empty() || (inPlaceMask & (inPlaceMask - 1u)) != 0u)){
            const u8 naturalLoadMask = static_cast<u8>((1u << inPlaceZeroedAbove) - 1u);
            if(inPlaceMask == 0xFu || inPlaceMask == naturalLoadMask){
                if(!inPlaceRegType.empty()){
                    emitSimdRegExpr(out, *inPlaceBase, simdType);
                }
                else{
                    out.append(inPlaceLoadFn.data(), inPlaceLoadFn.size());
                    out.push_back('(');
                    emitExpr(out, *inPlaceBase);
                    out.push_back(')');
                }
                return;
            }
            const MStringView maskConst = SimdInPlaceLaneMaskConstant(inPlaceMask, isDouble);
            if(!maskConst.empty()){
                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                if(!inPlaceRegType.empty()){
                    emitSimdRegExpr(out, *inPlaceBase, simdType);
                }
                else{
                    out.append(inPlaceLoadFn.data(), inPlaceLoadFn.size());
                    out.push_back('(');
                    emitExpr(out, *inPlaceBase);
                    out.push_back(')');
                }
                out.append(", ");
                out.append(maskConst.data(), maskConst.size());
                out.push_back(')');
                return;
            }
            const MStringView clearMaskConst = SimdInPlaceLaneMaskConstant(static_cast<u8>(0xFu ^ inPlaceMask), isDouble);
            if(!clearMaskConst.empty()){
                out.append(isDouble ? "::VectorDoubleAndCInt(" : "::VectorAndCInt(");
                if(!inPlaceRegType.empty()){
                    emitSimdRegExpr(out, *inPlaceBase, simdType);
                }
                else{
                    out.append(inPlaceLoadFn.data(), inPlaceLoadFn.size());
                    out.push_back('(');
                    emitExpr(out, *inPlaceBase);
                    out.push_back(')');
                }
                out.append(", ");
                out.append(clearMaskConst.data(), clearMaskConst.size());
                out.push_back(')');
                return;
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
                && TryMatchNegatedSingleLaneAccess(arguments[i], base, idx)
                && base
                && idx == i
                && TryGetFloatVectorSourceInfo(*base, isDouble, baseRegType, baseLoadFn, baseZeroedAbove)
                && idx < baseZeroedAbove
            ){
                if(!negBase){
                    negBase = base;
                    negRegType = baseRegType;
                    negLoadFn = baseLoadFn;
                    negZeroedAbove = baseZeroedAbove;
                }
                else if(!AreSamePureSimdExpr(*negBase, *base)){
                    allNegInPlaceOrZero = false;
                    break;
                }
                negMask |= static_cast<u8>(1u << i);
            }
            else if(
                i < arguments.size()
                && TryMatchSingleLaneAccess(arguments[i], base, idx)
                && base
                && idx == i
                && TryGetFloatVectorSourceInfo(*base, isDouble, baseRegType, baseLoadFn, baseZeroedAbove)
                && idx < baseZeroedAbove
            ){
                if(!negBase){
                    negBase = base;
                    negRegType = baseRegType;
                    negLoadFn = baseLoadFn;
                    negZeroedAbove = baseZeroedAbove;
                }
                else if(!AreSamePureSimdExpr(*negBase, *base)){
                    allNegInPlaceOrZero = false;
                    break;
                }
                posMask |= static_cast<u8>(1u << i);
            }
            else if(i < arguments.size() && (!arguments[i] || !IsZeroScalarLiteral(*arguments[i], &m_symbols))){
                allNegInPlaceOrZero = false;
                break;
            }
        }
        const u8 negUsedMask = static_cast<u8>(negMask | posMask);
        if(allNegInPlaceOrZero && negBase && negMask != 0u && (!negRegType.empty() || (negUsedMask & (negUsedMask - 1u)) != 0u)){
            const u8 naturalZeroLaneMask = static_cast<u8>(~((1u << negZeroedAbove) - 1u) & 0xFu & ~negMask);
            const u8 effectivePosMask = static_cast<u8>(posMask | naturalZeroLaneMask);
            if(negMask == 0xFu && posMask == 0u){
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                emitSimdRegExpr(out, *negBase, simdType);
                out.push_back(')');
                return;
            }
            if((negMask | effectivePosMask) == 0xFu){
                const MStringView flipConst = SimdInPlaceSignFlipMaskConstant(negMask, isDouble);
                if(!flipConst.empty()){
                    out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                    emitSimdRegExpr(out, *negBase, simdType);
                    out.append(", ");
                    out.append(flipConst.data(), flipConst.size());
                    out.push_back(')');
                    return;
                }
            }
            if(posMask == 0u){
                const MStringView maskConst = SimdInPlaceLaneMaskConstant(negMask, isDouble);
                if(!maskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndInt(::VectorDoubleNegate(" : "::VectorAndInt(::VectorNegate(");
                    emitSimdRegExpr(out, *negBase, simdType);
                    out.append("), ");
                    out.append(maskConst.data(), maskConst.size());
                    out.push_back(')');
                    return;
                }
                const MStringView clearMaskConst = SimdInPlaceLaneMaskConstant(static_cast<u8>(0xFu ^ negMask), isDouble);
                if(!clearMaskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndCInt(::VectorDoubleNegate(" : "::VectorAndCInt(::VectorNegate(");
                    emitSimdRegExpr(out, *negBase, simdType);
                    out.append("), ");
                    out.append(clearMaskConst.data(), clearMaskConst.size());
                    out.push_back(')');
                    return;
                }
            }
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
                const Expr* curBase = laneBases[i];
                u32 curIdx = laneIndices[i];
                if(!curBase && !isInt && arguments[i]){
                    const Expr* candBase = nullptr;
                    u32 candIdx = 0u;
                    MStringView candRegType;
                    MStringView candLoadFn;
                    u32 candZeroedAbove = 4u;
                    if(
                        TryMatchSingleLaneAccess(arguments[i], candBase, candIdx)
                        && candBase
                        && TryGetFloatVectorSourceInfo(*candBase, isDouble, candRegType, candLoadFn, candZeroedAbove)
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
                else if(!AreSamePureSimdExpr(*v0, *curBase)){
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
                    TryMatchSingleLaneAccess(arguments[i], candBase, candIdx)
                    && candBase
                    && TryGetFloatVectorSourceInfo(*candBase, isDouble, candRegType, candLoadFn, candZeroedAbove)
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
                else if(AreSamePureSimdExpr(*v0, *curBase)){
                    v0UsedLaneMask |= static_cast<u8>(1u << curIdx);
                    permIdx[i] = curIdx;
                }
                else if(!v1 && zeroLaneMask == 0u && unitLaneMask == 0u){
                    v1 = curBase;
                    v1UsedLaneMask |= static_cast<u8>(1u << curIdx);
                    permIdx[i] = 4u + curIdx;
                }
                else if(v1 && AreSamePureSimdExpr(*v1, *curBase)){
                    v1UsedLaneMask |= static_cast<u8>(1u << curIdx);
                    permIdx[i] = 4u + curIdx;
                }
                else{
                    canSwizzleOrPermute = false;
                    break;
                }
            }
            else if(!v1 && (i >= arguments.size() || (arguments[i] && IsZeroScalarLiteral(*arguments[i], &m_symbols)))){
                if(isInt && unitLaneMask != 0u){
                    canSwizzleOrPermute = false;
                    break;
                }
                zeroLaneMask |= static_cast<u8>(1u << i);
                permIdx[i] = 4u + i;
            }
            else if(!isInt && !v1 && i < arguments.size() && arguments[i] && IsUnitFloatLiteral(*arguments[i], &m_symbols)){
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
                    emitSimdRegExpr(out, *v0, simdType);
                    return;
                }
                if(!isInt && permIdx[0] == permIdx[1] && permIdx[1] == permIdx[2] && permIdx[2] == permIdx[3]){
                    const MStringView splatPrefix = SimdSplatIntrinsic(permIdx[0], isDouble);
                    out.append(splatPrefix.data(), splatPrefix.size());
                    emitSimdRegExpr(out, *v0, simdType);
                    out.push_back(')');
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
            if(v1)
                emitSimdRegExpr(out, *v1, simdType);
            else
                out.append(constSecondVec.data(), constSecondVec.size());
            out.push_back(')');
            return;
        }
    }

    if(arguments.size() == 3u || arguments.size() == 4u){
        const Expr* repeatedScalar = nullptr;
        u8 nonZeroMask = 0u;
        u32 nonZeroCount = 0u;
        b8 sameOrZero = true;
        for(u32 i = 0u; i < 4u; ++i){
            if(i >= arguments.size() || (arguments[i] && IsZeroScalarLiteral(*arguments[i], &m_symbols)))
                continue;
            if(
                arguments[i]
                && arguments[i]->resolvedSimdType.empty()
                && arguments[i]->resolvedUnalignedType.empty()
                && IsPureSimdAddressExpr(*arguments[i])
            ){
                if(!repeatedScalar){
                    repeatedScalar = arguments[i];
                }
                else if(!AreSamePureSimdExpr(*repeatedScalar, *arguments[i])){
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
                emitSimdRegExpr(out, *repeatedScalar, simdType);
                return;
            }
            if(!isInt && nonZeroCount >= 2u){
                const MStringView maskConst = SimdInPlaceLaneMaskConstant(nonZeroMask, isDouble);
                if(!maskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                    emitSimdRegExpr(out, *repeatedScalar, simdType);
                    out.append(", ");
                    out.append(maskConst.data(), maskConst.size());
                    out.push_back(')');
                    return;
                }
                const MStringView clearMaskConst = SimdInPlaceLaneMaskConstant(static_cast<u8>(0xFu ^ nonZeroMask), isDouble);
                if(!clearMaskConst.empty()){
                    out.append(isDouble ? "::VectorDoubleAndCInt(" : "::VectorAndCInt(");
                    emitSimdRegExpr(out, *repeatedScalar, simdType);
                    out.append(", ");
                    out.append(clearMaskConst.data(), clearMaskConst.size());
                    out.push_back(')');
                    return;
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
                if(!baseScalar || !baseScalar->resolvedSimdType.empty() || !baseScalar->resolvedUnalignedType.empty() || !IsPureSimdAddressExpr(*baseScalar)){
                    allSignedScalar = false;
                    break;
                }
                if(!signScalar){
                    signScalar = baseScalar;
                }
                else if(!AreSamePureSimdExpr(*signScalar, *baseScalar)){
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
                    emitSimdRegExpr(out, *signScalar, simdType);
                    out.append(", ");
                    out.append(flipConst.data(), flipConst.size());
                    out.push_back(')');
                    return;
                }
            }
        }
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

    const auto emitStoreVecWithReplacementW = [&](const Expr& baseVec, const Expr& wArg){
        const MStringView baseSimdType = baseVec.resolvedSimdType;
        out.append(storeFn.data(), storeFn.size());
        out.push_back('(');
        const Expr* wLaneBase = nullptr;
        u32 wLaneIdx = 0u;
        if(TryMatchSingleLaneAccess(&wArg, wLaneBase, wLaneIdx) && wLaneBase && wLaneBase->resolvedSimdType == baseSimdType){
            if(AreSamePureSimdExpr(baseVec, *wLaneBase)){
                if(wLaneIdx == 3u){
                    emitSimdRegExpr(out, baseVec, baseSimdType);
                }
                else{
                    out.append(isDouble ? "::VectorDoubleSwizzle<0u, 1u, 2u, " : "::VectorSwizzle<0u, 1u, 2u, ");
                    AppendUnsignedDecimal(out, wLaneIdx);
                    out.append("u>(");
                    emitSimdRegExpr(out, baseVec, baseSimdType);
                    out.push_back(')');
                }
            }
            else{
                out.append(isDouble ? "::VectorDoublePermute<0u, 1u, 2u, " : "::VectorPermute<0u, 1u, 2u, ");
                AppendUnsignedDecimal(out, 4u + wLaneIdx);
                out.append("u>(");
                emitSimdRegExpr(out, baseVec, baseSimdType);
                out.append(", ");
                emitSimdRegExpr(out, *wLaneBase, baseSimdType);
                out.push_back(')');
            }
            out.append(", ");
        }
        else if(!IsIntSimdType(baseSimdType) && IsZeroScalarLiteral(wArg, &m_symbols)){
            out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
            emitSimdRegExpr(out, baseVec, baseSimdType);
            out.append(isDouble ? ", ::s_DoubleSIMDMask3), " : ", ::s_SIMDMask3), ");
        }
        else{
            out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
            emitSimdRegExpr(out, baseVec, baseSimdType);
            out.append(", ");
            emitExpr(out, wArg);
            out.append("), ");
        }
        out.append(dstExprText.data(), dstExprText.size());
        out.push_back(')');
    };

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
        emitStoreVecWithReplacementW(*arguments[0], *arguments[1]);
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
            if(arguments[3] && arguments[3]->resolvedSimdType.empty()){
                emitStoreVecWithReplacementW(*srcSimdVec, *arguments[3]);
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
