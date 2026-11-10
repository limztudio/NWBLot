// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_lane_operation_emitter.h"
#include "simd_binary_emitter.h"

NWB_ADHD_BEGIN


b8 SimdLaneOperationEmitter::tryEmit()const{
    MString& out = m_output;
    const MStringView simdType = m_simdType;
    const Span<const Expr* const> arguments = m_arguments;
    const b8 isDouble = m_isDouble;
    const b8 isInt = m_isInt;
    Expr synthCtorCallee(m_emitter.m_arena);
    synthCtorCallee.kind = ExprKind::Identifier;
    synthCtorCallee.text = simdType;
    Array<Expr, 3> synthColCtors{ Expr(m_emitter.m_arena), Expr(m_emitter.m_arena), Expr(m_emitter.m_arena) };
    Array<Expr, 4> synthNegScalars{ Expr(m_emitter.m_arena), Expr(m_emitter.m_arena), Expr(m_emitter.m_arena), Expr(m_emitter.m_arena) };

    const auto classifyActiveLaneColumn = [&](
        const Array<const Expr*, 4>& col,
        const u32 activeLanes,
        Expr& synthCtorSlot,
        const Expr*& outEmitExpr,
        b8& outIsIdentityVec,
        b8& outIsNonUniformTuple,
        u32& outZeroedAbove
    )noexcept -> b8{
        outEmitExpr = nullptr;
        outIsIdentityVec = false;
        outIsNonUniformTuple = false;
        outZeroedAbove = 4u;
        if(activeLanes < 2u || activeLanes > 4u)
            return false;

        const Expr* idVec = nullptr;
        MStringView idRegType;
        u32 idZeroedAbove = 4u;
        b8 allIdentityVec = true;
        for(u32 i = 0u; i < activeLanes; ++i){
            const Expr* curBase = nullptr;
            u32 curLane = 0u;
            MStringView curRegType;
            MStringView curLoadFn;
            u32 curZeroedAbove = 4u;
            if(
                col[i]
                && SimdExpressionAnalysis::TryMatchSingleLaneAccess(col[i], curBase, curLane)
                && curBase
                && curLane == i
                && SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*curBase, isDouble, curRegType, curLoadFn, curZeroedAbove)
                && i < curZeroedAbove
                && (!idVec || SimdExpressionAnalysis::AreSamePureSimdExpr(*idVec, *curBase))
            ){
                idVec = curBase;
                idRegType = curRegType;
                idZeroedAbove = curZeroedAbove;
            }
            else{
                allIdentityVec = false;
                break;
            }
        }
        if(allIdentityVec && idVec){
            outEmitExpr = idVec;
            outIsIdentityVec = true;
            outZeroedAbove = !idRegType.empty()
                ? SimdLaneZeroAnalysis::InferSimdRegZeroedAboveLane(*idVec, isDouble, &m_emitter.m_symbols, m_emitter.activeLocalVarDecls())
                : idZeroedAbove;
            return true;
        }

        for(u32 i = 0u; i < activeLanes; ++i){
            if(!col[i] || !col[i]->resolvedSimdType.empty() || !col[i]->resolvedUnalignedType.empty() || !SimdExpressionAnalysis::IsPureSimdAddressExpr(*col[i]))
                return false;
        }

        b8 allSameScalar = true;
        for(u32 i = 1u; i < activeLanes; ++i){
            if(!SimdExpressionAnalysis::AreSamePureSimdExpr(*col[0], *col[i])){
                allSameScalar = false;
                break;
            }
        }
        if(allSameScalar){
            outEmitExpr = col[0];
            outZeroedAbove = SimdExpressionAnalysis::IsZeroScalarLiteral(*col[0], &m_emitter.m_symbols) ? activeLanes : 4u;
            return true;
        }

        if(activeLanes < 3u)
            return false;

        synthCtorSlot.kind = ExprKind::Call;
        synthCtorSlot.lhs = &synthCtorCallee;
        synthCtorSlot.resolvedSimdType = simdType;
        synthCtorSlot.arguments.clear();
        for(u32 i = 0u; i < activeLanes; ++i)
            synthCtorSlot.arguments.push_back(const_cast<Expr*>(col[i]));
        for(u32 i = activeLanes; i < static_cast<u32>(arguments.size()); ++i)
            synthCtorSlot.arguments.push_back(const_cast<Expr*>(arguments[i]));
        outEmitExpr = &synthCtorSlot;
        outIsNonUniformTuple = true;
        outZeroedAbove = (activeLanes < 4u) ? activeLanes : 4u;
        return true;
    };

    if(!isInt && (arguments.size() == 3u || arguments.size() == 4u) && arguments[0] && arguments[0]->kind == ExprKind::Binary){
        const TokenType::Enum firstOp = arguments[0]->op;
        if(firstOp == TokenType::Plus || firstOp == TokenType::Minus || firstOp == TokenType::Star || firstOp == TokenType::Slash){
            const b8 isAddSubFamily = (firstOp == TokenType::Plus || firstOp == TokenType::Minus);
            u32 activeLanes = 0u;
            u8 plusMask = 0u;
            u8 minusMask = 0u;
            Array<const Expr*, 4> lhsCol{};
            Array<const Expr*, 4> rhsCol{};
            b8 binMatch = true;
            for(u32 i = 0u; i < static_cast<u32>(arguments.size()); ++i){
                const Expr* arg = arguments[i];
                if(!arg){
                    binMatch = false;
                    break;
                }
                if(SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &m_emitter.m_symbols)){
                    for(u32 j = i + 1u; j < static_cast<u32>(arguments.size()); ++j){
                        if(!arguments[j] || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[j], &m_emitter.m_symbols)){
                            binMatch = false;
                            break;
                        }
                    }
                    break;
                }
                const b8 opMatches = isAddSubFamily
                    ? (arg->op == TokenType::Plus || arg->op == TokenType::Minus)
                    : (arg->op == firstOp);
                if(arg->kind != ExprKind::Binary || !opMatches || !arg->lhs || !arg->rhs){
                    binMatch = false;
                    break;
                }
                if(arg->op == TokenType::Plus)
                    plusMask |= static_cast<u8>(1u << i);
                else if(arg->op == TokenType::Minus)
                    minusMask |= static_cast<u8>(1u << i);
                lhsCol[i] = arg->lhs;
                rhsCol[i] = arg->rhs;
                ++activeLanes;
            }
            if(binMatch && activeLanes >= 2u){
                const Expr* emitLhs = nullptr;
                b8 lhsIsIdVec = false;
                b8 lhsIsTuple = false;
                u32 lhsZeroedAbove = 4u;
                const Expr* emitRhs = nullptr;
                b8 rhsIsIdVec = false;
                b8 rhsIsTuple = false;
                u32 rhsZeroedAbove = 4u;
                if(
                    classifyActiveLaneColumn(lhsCol, activeLanes, synthColCtors[0], emitLhs, lhsIsIdVec, lhsIsTuple, lhsZeroedAbove)
                    && classifyActiveLaneColumn(rhsCol, activeLanes, synthColCtors[1], emitRhs, rhsIsIdVec, rhsIsTuple, rhsZeroedAbove)
                    && (lhsIsIdVec || rhsIsIdVec)
                ){
                    if(plusMask != 0u && minusMask != 0u){
                        if(rhsIsTuple){
                            Array<const Expr*, 4> signedRhsCol = rhsCol;
                            for(u32 i = 0u; i < activeLanes; ++i){
                                if((minusMask & static_cast<u8>(1u << i)) == 0u)
                                    continue;
                                const Expr* curRhs = signedRhsCol[i];
                                if(curRhs && curRhs->kind == ExprKind::Unary && !curRhs->isPostfix && curRhs->op == TokenType::Minus && curRhs->lhs){
                                    signedRhsCol[i] = curRhs->lhs;
                                }
                                else{
                                    synthNegScalars[i].kind = ExprKind::Unary;
                                    synthNegScalars[i].op = TokenType::Minus;
                                    synthNegScalars[i].lhs = const_cast<Expr*>(curRhs);
                                    signedRhsCol[i] = &synthNegScalars[i];
                                }
                            }
                            if(classifyActiveLaneColumn(signedRhsCol, activeLanes, synthColCtors[1], emitRhs, rhsIsIdVec, rhsIsTuple, rhsZeroedAbove)){
                                minusMask = 0u;
                            }
                        }
                        else if(activeLanes == 4u){
                            const MStringView flipConst = SimdInPlaceSignFlipMaskConstant(minusMask, isDouble);
                            if(!flipConst.empty()){
                                out.append(isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(");
                                m_emitter.emitSimdRegExpr(out, *emitLhs, simdType);
                                out.append(isDouble ? ", ::VectorDoubleXorInt(" : ", ::VectorXorInt(");
                                m_emitter.emitSimdRegExpr(out, *emitRhs, simdType);
                                out.append(", ");
                                out.append(flipConst.data(), flipConst.size());
                                out.append("))");
                                return true;
                            }
                            out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
                            for(u32 i = 0u; i < 4u; ++i){
                                if(i > 0u)
                                    out.append(", ");
                                AppendUnsignedDecimal(out, ((minusMask & static_cast<u8>(1u << i)) != 0u) ? (4u + i) : i);
                                out.push_back('u');
                            }
                            out.append(">(");
                            out.append(isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(");
                            m_emitter.emitSimdRegExpr(out, *emitLhs, simdType);
                            out.append(", ");
                            m_emitter.emitSimdRegExpr(out, *emitRhs, simdType);
                            out.append("), ");
                            out.append(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(");
                            m_emitter.emitSimdRegExpr(out, *emitLhs, simdType);
                            out.append(", ");
                            m_emitter.emitSimdRegExpr(out, *emitRhs, simdType);
                            out.append("))");
                            return true;
                        }
                    }

                    if(plusMask == 0u || minusMask == 0u){
                        const TokenType::Enum effBinOp = (minusMask != 0u) ? TokenType::Minus : (plusMask != 0u ? TokenType::Plus : firstOp);
                        if(effBinOp != TokenType::Slash || activeLanes == 4u || !rhsIsTuple){
                            const b8 alreadyZeroedAbove = (activeLanes == 4u)
                                || (
                                    (effBinOp == TokenType::Plus || effBinOp == TokenType::Minus || effBinOp == TokenType::Star)
                                    && lhsZeroedAbove <= activeLanes
                                    && rhsZeroedAbove <= activeLanes
                                );
                            const u8 activeMask = static_cast<u8>((1u << activeLanes) - 1u);
                            const MStringView maskConst = !alreadyZeroedAbove ? SimdInPlaceLaneMaskConstant(activeMask, isDouble) : MStringView{};
                            if(alreadyZeroedAbove || !maskConst.empty()){
                                if(!alreadyZeroedAbove)
                                    out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                                (void)SimdBinaryEmitter(m_emitter).tryEmit(out, effBinOp, *emitLhs, *emitRhs, simdType);
                                if(!alreadyZeroedAbove){
                                    out.append(", ");
                                    out.append(maskConst.data(), maskConst.size());
                                    out.push_back(')');
                                }
                                return true;
                            }
                        }
                    }
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
            && (!MapSimdUnaryFloatIntrinsic(callee, isDouble).empty() || (callee == "Lerp" && callArity == 3u));
        if(supportedBuiltin){
            const b8 isMinMaxFamily = (callArity == 2u && (callee == "Min" || callee == "Max"));
            u32 activeLanes = 0u;
            u8 minMask = 0u;
            u8 maxMask = 0u;
            Array<Array<const Expr*, 4>, 3> paramCols{};
            b8 callMatch = true;
            for(u32 i = 0u; i < static_cast<u32>(arguments.size()); ++i){
                const Expr* arg = arguments[i];
                if(!arg){
                    callMatch = false;
                    break;
                }
                if(SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &m_emitter.m_symbols)){
                    for(u32 j = i + 1u; j < static_cast<u32>(arguments.size()); ++j){
                        if(!arguments[j] || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[j], &m_emitter.m_symbols)){
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
                    || arg->arguments.size() != callArity
                ){
                    callMatch = false;
                    break;
                }
                const MStringView curCallee = arg->lhs->text;
                const b8 calleeMatches = isMinMaxFamily
                    ? (curCallee == "Min" || curCallee == "Max")
                    : (curCallee == callee);
                if(!calleeMatches){
                    callMatch = false;
                    break;
                }
                if(curCallee == "Min")
                    minMask |= static_cast<u8>(1u << i);
                else if(curCallee == "Max")
                    maxMask |= static_cast<u8>(1u << i);
                for(usize pIdx = 0u; pIdx < callArity; ++pIdx){
                    if(!arg->arguments[pIdx]){
                        callMatch = false;
                        break;
                    }
                    paramCols[pIdx][i] = arg->arguments[pIdx];
                }
                if(!callMatch)
                    break;
                ++activeLanes;
            }
            if(callMatch && activeLanes >= 2u){
                Array<const Expr*, 3> emitParams{};
                Array<b8, 3> paramIsIdVec{};
                Array<b8, 3> paramIsTuple{};
                Array<u32, 3> paramZeroedAbove{4u, 4u, 4u};
                b8 allColsValid = true;
                b8 hasAnyIdVecParam = false;
                for(usize pIdx = 0u; pIdx < callArity; ++pIdx){
                    if(!classifyActiveLaneColumn(paramCols[pIdx], activeLanes, synthColCtors[pIdx], emitParams[pIdx], paramIsIdVec[pIdx], paramIsTuple[pIdx], paramZeroedAbove[pIdx])){
                        allColsValid = false;
                        break;
                    }
                    if(paramIsIdVec[pIdx])
                        hasAnyIdVecParam = true;
                }
                if(allColsValid && hasAnyIdVecParam){
                    if(minMask != 0u && maxMask != 0u){
                        if(activeLanes == 4u && callArity == 2u){
                            out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
                            for(u32 i = 0u; i < 4u; ++i){
                                if(i > 0u)
                                    out.append(", ");
                                AppendUnsignedDecimal(out, ((maxMask & static_cast<u8>(1u << i)) != 0u) ? (4u + i) : i);
                                out.push_back('u');
                            }
                            out.append(">(");
                            out.append(isDouble ? "::VectorDoubleMin(" : "::VectorMin(");
                            m_emitter.emitSimdRegExpr(out, *emitParams[0], simdType);
                            out.append(", ");
                            m_emitter.emitSimdRegExpr(out, *emitParams[1], simdType);
                            out.append("), ");
                            out.append(isDouble ? "::VectorDoubleMax(" : "::VectorMax(");
                            m_emitter.emitSimdRegExpr(out, *emitParams[0], simdType);
                            out.append(", ");
                            m_emitter.emitSimdRegExpr(out, *emitParams[1], simdType);
                            out.append("))");
                            return true;
                        }
                    }
                    else{
                        b8 allParamsZeroedAtActive = true;
                        for(usize pIdx = 0u; pIdx < callArity; ++pIdx){
                            if(paramZeroedAbove[pIdx] > activeLanes){
                                allParamsZeroedAtActive = false;
                                break;
                            }
                        }
                        const b8 zeroPreservingFn = callee == "Abs"
                            || callee == "Sqrt"
                            || callee == "Floor"
                            || callee == "Ceil"
                            || callee == "Round"
                            || callee == "trunc"
                            || callee == "frac"
                            || callee == "Saturate"
                            || callee == "Sin"
                            || callee == "Tan"
                            || callee == "ASin"
                            || callee == "ATan"
                            || callee == "Min"
                            || callee == "Max"
                            || callee == "Clamp"
                            || callee == "Lerp";
                        const b8 alreadyZeroedAbove = (activeLanes == 4u) || (zeroPreservingFn && allParamsZeroedAtActive);
                        const u8 activeMask = static_cast<u8>((1u << activeLanes) - 1u);
                        const MStringView maskConst = !alreadyZeroedAbove ? SimdInPlaceLaneMaskConstant(activeMask, isDouble) : MStringView{};
                        if(alreadyZeroedAbove || !maskConst.empty()){
                            if(!alreadyZeroedAbove)
                                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                            Expr synthCall(m_emitter.m_arena);
                            synthCall.kind = ExprKind::Call;
                            synthCall.isBuiltinCall = true;
                            synthCall.lhs = arguments[0]->lhs;
                            synthCall.resolvedSimdType = simdType;
                            for(usize pIdx = 0u; pIdx < callArity; ++pIdx)
                                synthCall.arguments.push_back(const_cast<Expr*>(emitParams[pIdx]));
                            m_emitter.emitSimdRegExpr(out, synthCall, simdType);
                            if(!alreadyZeroedAbove){
                                out.append(", ");
                                out.append(maskConst.data(), maskConst.size());
                                out.push_back(')');
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

