// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_expr{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CollectLogicChainTerms(const Expr& expr, const TokenType::Enum op, MVector<const Expr*>& outTerms){
    if(expr.kind == ExprKind::Binary && expr.op == op && !expr.isNotNullRebind && !expr.wrapInSimdResult && expr.lhs && expr.rhs){
        CollectLogicChainTerms(*expr.lhs, op, outTerms);
        CollectLogicChainTerms(*expr.rhs, op, outTerms);
        return;
    }
    outTerms.push_back(&expr);
}

[[nodiscard]] b8 TryMatchIsFiniteLaneTerm(
    const Expr* term,
    const b8 expectNegated,
    TokenType::Enum& outBinOp,
    const Expr*& outLhsBase,
    const Expr*& outRhsBase,
    const Expr*& outRhsScalar,
    b8& outIsDouble,
    u32& outLane,
    u32& outRhsLane
)noexcept{
    outBinOp = TokenType::EndOfFile;
    outLhsBase = nullptr;
    outRhsBase = nullptr;
    outRhsScalar = nullptr;
    outIsDouble = false;
    outLane = 0u;
    outRhsLane = 0u;
    if(!term)
        return false;
    const Expr* callExpr = term;
    if(expectNegated){
        if(term->kind != ExprKind::Unary || term->isPostfix || term->op != TokenType::Bang || !term->lhs)
            return false;
        callExpr = term->lhs;
    }
    if(
        callExpr->kind != ExprKind::Call
        || !callExpr->isBuiltinCall
        || !callExpr->lhs
        || callExpr->lhs->kind != ExprKind::Identifier
        || callExpr->lhs->text != "isFinite"
        || callExpr->arguments.size() != 1u
        || !callExpr->arguments[0]
    ){
        return false;
    }
    const Expr* arg0 = callExpr->arguments[0];
    if(TryMatchSingleLaneAccess(arg0, outLhsBase, outLane) && outLhsBase){
        MStringView regType;
        MStringView loadFn;
        u32 zeroedAbove = 0u;
        return TryGetAnyFloatVectorSourceInfo(*outLhsBase, outIsDouble, regType, loadFn, zeroedAbove)
            && outLane < zeroedAbove
            && IsPureSimdAddressExpr(*outLhsBase);
    }
    if(
        arg0->kind == ExprKind::Binary
        && (arg0->op == TokenType::Plus || arg0->op == TokenType::Minus || arg0->op == TokenType::Star || arg0->op == TokenType::Slash)
        && arg0->lhs
        && arg0->rhs
    ){
        const Expr* lhsBase = nullptr;
        b8 lhsIsDouble = false;
        u32 lhsLane = 0u;
        MStringView lhsRegType;
        MStringView lhsLoadFn;
        u32 lhsZeroedAbove = 0u;
        const b8 lhsIsLane = TryMatchSingleLaneAccess(arg0->lhs, lhsBase, lhsLane)
            && lhsBase
            && TryGetAnyFloatVectorSourceInfo(*lhsBase, lhsIsDouble, lhsRegType, lhsLoadFn, lhsZeroedAbove)
            && lhsLane < lhsZeroedAbove
            && IsPureSimdAddressExpr(*lhsBase);

        const Expr* rhsBase = nullptr;
        b8 rhsIsDouble = false;
        u32 rhsLane = 0u;
        MStringView rhsRegType;
        MStringView rhsLoadFn;
        u32 rhsZeroedAbove = 0u;
        const b8 rhsIsLane = TryMatchSingleLaneAccess(arg0->rhs, rhsBase, rhsLane)
            && rhsBase
            && TryGetAnyFloatVectorSourceInfo(*rhsBase, rhsIsDouble, rhsRegType, rhsLoadFn, rhsZeroedAbove)
            && rhsLane < rhsZeroedAbove
            && IsPureSimdAddressExpr(*rhsBase);

        if(lhsIsLane && rhsIsLane){
            if(lhsIsDouble != rhsIsDouble)
                return false;
            outBinOp = arg0->op;
            outIsDouble = lhsIsDouble;
            if((arg0->op == TokenType::Plus || arg0->op == TokenType::Star) && lhsLane > rhsLane && AreSamePureSimdExpr(*lhsBase, *rhsBase)){
                outLhsBase = rhsBase;
                outRhsBase = lhsBase;
                outLane = rhsLane;
                outRhsLane = lhsLane;
                return true;
            }
            outLhsBase = lhsBase;
            outRhsBase = rhsBase;
            outLane = lhsLane;
            outRhsLane = rhsLane;
            return true;
        }
        if(lhsIsLane && arg0->rhs->resolvedSimdType.empty() && arg0->rhs->resolvedUnalignedType.empty() && IsPureSimdAddressExpr(*arg0->rhs)){
            outBinOp = arg0->op;
            outLhsBase = lhsBase;
            outRhsScalar = arg0->rhs;
            outIsDouble = lhsIsDouble;
            outLane = lhsLane;
            return true;
        }
        if(rhsIsLane && (arg0->op == TokenType::Plus || arg0->op == TokenType::Star) && arg0->lhs->resolvedSimdType.empty() && arg0->lhs->resolvedUnalignedType.empty() && IsPureSimdAddressExpr(*arg0->lhs)){
            outBinOp = arg0->op;
            outLhsBase = rhsBase;
            outRhsScalar = arg0->lhs;
            outIsDouble = rhsIsDouble;
            outLane = rhsLane;
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr b8 IsSimdComparisonOp(const TokenType::Enum op)noexcept{
    return op == TokenType::EqualEqual
        || op == TokenType::BangEqual
        || op == TokenType::Less
        || op == TokenType::LessEqual
        || op == TokenType::Greater
        || op == TokenType::GreaterEqual;
}

[[nodiscard]] constexpr TokenType::Enum FlipComparisonOp(const TokenType::Enum op)noexcept{
    switch(op){
    case TokenType::Less: return TokenType::Greater;
    case TokenType::LessEqual: return TokenType::GreaterEqual;
    case TokenType::Greater: return TokenType::Less;
    case TokenType::GreaterEqual: return TokenType::LessEqual;
    default: return op;
    }
}

[[nodiscard]] b8 TryMatchSimdLaneOrAbsAccess(
    const Expr* expr,
    const Expr*& outBase,
    b8& outIsDouble,
    u32& outLane,
    b8& outIsAbs
)noexcept{
    outIsAbs = false;
    const Expr* laneExpr = expr;
    if(
        expr
        && expr->kind == ExprKind::Call
        && expr->isBuiltinCall
        && expr->lhs
        && expr->lhs->kind == ExprKind::Identifier
        && expr->lhs->text == "abs"
        && expr->arguments.size() == 1u
        && expr->arguments[0]
    ){
        laneExpr = expr->arguments[0];
        outIsAbs = true;
    }
    if(!TryMatchSingleLaneAccess(laneExpr, outBase, outLane) || !outBase)
        return false;
    MStringView regType;
    MStringView loadFn;
    u32 zeroedAbove = 0u;
    return TryGetAnyFloatVectorSourceInfo(*outBase, outIsDouble, regType, loadFn, zeroedAbove)
        && outLane < zeroedAbove
        && IsPureSimdAddressExpr(*outBase);
}

[[nodiscard]] b8 TryMatchSimdLaneComparisonTerm(
    const Expr* term,
    TokenType::Enum& outCmpOp,
    const Expr*& outLhsBase,
    const Expr*& outRhsBase,
    const Expr*& outRhsScalar,
    b8& outIsDouble,
    b8& outLhsAbs,
    b8& outRhsAbs,
    u32& outLane,
    u32& outRhsLane
)noexcept{
    outRhsLane = 0u;
    if(!term || term->kind != ExprKind::Binary || !IsSimdComparisonOp(term->op) || !term->lhs || !term->rhs)
        return false;

    const Expr* lhsBase = nullptr;
    b8 lhsIsDouble = false;
    u32 lhsLane = 0u;
    b8 lhsAbs = false;
    const b8 lhsIsLane = TryMatchSimdLaneOrAbsAccess(term->lhs, lhsBase, lhsIsDouble, lhsLane, lhsAbs);

    const Expr* rhsBase = nullptr;
    b8 rhsIsDouble = false;
    u32 rhsLane = 0u;
    b8 rhsAbs = false;
    const b8 rhsIsLane = TryMatchSimdLaneOrAbsAccess(term->rhs, rhsBase, rhsIsDouble, rhsLane, rhsAbs);

    if(lhsIsLane && rhsIsLane){
        if(lhsIsDouble != rhsIsDouble)
            return false;
        if(lhsLane > rhsLane && lhsAbs == rhsAbs && AreSamePureSimdExpr(*lhsBase, *rhsBase)){
            outCmpOp = FlipComparisonOp(term->op);
            outLhsBase = rhsBase;
            outRhsBase = lhsBase;
            outRhsScalar = nullptr;
            outIsDouble = lhsIsDouble;
            outLhsAbs = rhsAbs;
            outRhsAbs = lhsAbs;
            outLane = rhsLane;
            outRhsLane = lhsLane;
            return true;
        }
        outCmpOp = term->op;
        outLhsBase = lhsBase;
        outRhsBase = rhsBase;
        outRhsScalar = nullptr;
        outIsDouble = lhsIsDouble;
        outLhsAbs = lhsAbs;
        outRhsAbs = rhsAbs;
        outLane = lhsLane;
        outRhsLane = rhsLane;
        return true;
    }
    if(lhsIsLane && term->rhs->resolvedSimdType.empty() && term->rhs->resolvedUnalignedType.empty() && IsPureSimdAddressExpr(*term->rhs)){
        outCmpOp = term->op;
        outLhsBase = lhsBase;
        outRhsBase = nullptr;
        outRhsScalar = term->rhs;
        outIsDouble = lhsIsDouble;
        outLhsAbs = lhsAbs;
        outRhsAbs = false;
        outLane = lhsLane;
        return true;
    }
    if(rhsIsLane && term->lhs->resolvedSimdType.empty() && term->lhs->resolvedUnalignedType.empty() && IsPureSimdAddressExpr(*term->lhs)){
        outCmpOp = FlipComparisonOp(term->op);
        outLhsBase = rhsBase;
        outRhsBase = nullptr;
        outRhsScalar = term->lhs;
        outIsDouble = rhsIsDouble;
        outLhsAbs = rhsAbs;
        outRhsAbs = false;
        outLane = rhsLane;
        return true;
    }
    return false;
}

[[nodiscard]] b8 IsPureSimdLaneLogicTerm(const Expr* term, const b8 isOrChain)noexcept{
    TokenType::Enum cmpOp = TokenType::EndOfFile;
    const Expr* base0 = nullptr;
    const Expr* rhsBase = nullptr;
    const Expr* rhsScalar = nullptr;
    b8 isDouble = false;
    u32 lane0 = 0u;
    u32 rhsLane = 0u;
    if(TryMatchIsFiniteLaneTerm(term, isOrChain, cmpOp, base0, rhsBase, rhsScalar, isDouble, lane0, rhsLane))
        return true;
    b8 lhsAbs = false;
    b8 rhsAbs = false;
    return TryMatchSimdLaneComparisonTerm(term, cmpOp, base0, rhsBase, rhsScalar, isDouble, lhsAbs, rhsAbs, lane0, rhsLane);
}

[[nodiscard]] constexpr u32 CountMaskBits4(const u8 mask)noexcept{
    return (mask & 1u) + ((mask >> 1u) & 1u) + ((mask >> 2u) & 1u) + ((mask >> 3u) & 1u);
}

[[nodiscard]] MStringView MapSimdVectorCompareIntrinsicPrefix(const TokenType::Enum cmpOp, const b8 isDouble)noexcept{
    switch(cmpOp){
    case TokenType::EqualEqual: return isDouble ? "::VectorDoubleEqual(" : "::VectorEqual(";
    case TokenType::BangEqual: return isDouble ? "::VectorDoubleNotEqual(" : "::VectorNotEqual(";
    case TokenType::Less: return isDouble ? "::VectorDoubleLess(" : "::VectorLess(";
    case TokenType::LessEqual: return isDouble ? "::VectorDoubleLessOrEqual(" : "::VectorLessOrEqual(";
    case TokenType::Greater: return isDouble ? "::VectorDoubleGreater(" : "::VectorGreater(";
    case TokenType::GreaterEqual: return isDouble ? "::VectorDoubleGreaterOrEqual(" : "::VectorGreaterOrEqual(";
    default: return {};
    }
}

void AppendSimdMoveMaskHexOrConstant(MString& out, const u8 mask, const b8 isDouble){
    const MStringView maskConst = SimdComponentMaskConstant(mask, isDouble);
    if(!maskConst.empty()){
        out.append(maskConst.data(), maskConst.size());
        return;
    }
    static constexpr char s_HexDigits[] = "0123456789ABCDEF";
    out.append("0x");
    out.push_back(s_HexDigits[mask & 0xFu]);
    out.push_back('u');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SourceEmitter::tryEmitFusedSimdLogicChain(MString& out, const Expr& expr, const b8 omitOuterBinaryParens)const{
    using namespace __hidden_adhd_expr;

    if(expr.kind != ExprKind::Binary || (expr.op != TokenType::AmpersandAmpersand && expr.op != TokenType::PipePipe))
        return false;

    const b8 isOrChain = (expr.op == TokenType::PipePipe);
    MVector<const Expr*> terms(m_arena);
    CollectLogicChainTerms(expr, expr.op, terms);
    if(terms.size() < 2u)
        return false;
    usize simdTermCount = 0u;
    for(const Expr* term : terms){
        if(IsPureSimdLaneLogicTerm(term, isOrChain))
            ++simdTermCount;
    }
    if(simdTermCount < 2u)
        return false;

    struct FusedSegment{
        u8 kind = 0u;
        usize start = 0u;
        u8 laneMask = 0u;
        u8 compLaneMask = 0u;
        b8 isDouble = false;
        b8 lhsAbs = false;
        b8 rhsAbs = false;
        b8 hasUniformRhsScalar = true;
        TokenType::Enum cmpOp = TokenType::EndOfFile;
        TokenType::Enum compCmpOp = TokenType::EndOfFile;
        const Expr* lhsBase = nullptr;
        const Expr* rhsBase = nullptr;
        const Expr* rhsScalar = nullptr;
        Array<const Expr*, 4> rhsScalars{nullptr, nullptr, nullptr, nullptr};
        Array<u32, 4> rhsSwizzle{0u, 1u, 2u, 3u};
    };

    MVector<FusedSegment> segments(m_arena);
    MVector<b8> consumed(terms.size(), false, m_arena);
    b8 anyFused = false;
    for(usize idx = 0u; idx < terms.size(); ++idx){
        if(consumed[idx])
            continue;

        TokenType::Enum finBinOp = TokenType::EndOfFile;
        const Expr* finBase = nullptr;
        const Expr* finRhsBase = nullptr;
        const Expr* finRhsScalar = nullptr;
        b8 finIsDouble = false;
        u32 finLane = 0u;
        u32 finRhsLane = 0u;
        if(TryMatchIsFiniteLaneTerm(terms[idx], isOrChain, finBinOp, finBase, finRhsBase, finRhsScalar, finIsDouble, finLane, finRhsLane)){
            u8 accMask = static_cast<u8>(1u << finLane);
            Array<u32, 4> rhsSwizzle{0u, 1u, 2u, 3u};
            rhsSwizzle[finLane] = finRhsLane;
            Array<u32, 4> bestSwizzle = rhsSwizzle;
            MVector<usize> matchedIndices(m_arena);
            matchedIndices.push_back(idx);
            usize bestCount = 0u;
            u8 bestMask = 0u;
            for(usize scan = idx + 1u; scan < terms.size(); ++scan){
                if(consumed[scan])
                    continue;
                TokenType::Enum nextBinOp = TokenType::EndOfFile;
                const Expr* nextBase = nullptr;
                const Expr* nextRhsBase = nullptr;
                const Expr* nextRhsScalar = nullptr;
                b8 nextIsDouble = false;
                u32 nextLane = 0u;
                u32 nextRhsLane = 0u;
                if(TryMatchIsFiniteLaneTerm(terms[scan], isOrChain, nextBinOp, nextBase, nextRhsBase, nextRhsScalar, nextIsDouble, nextLane, nextRhsLane)){
                    const b8 sameRhs = (finBinOp == TokenType::EndOfFile)
                        || (finRhsBase ? (nextRhsBase && AreSamePureSimdExpr(*finRhsBase, *nextRhsBase)) : (nextRhsScalar && AreSamePureSimdExpr(*finRhsScalar, *nextRhsScalar)));
                    if(
                        nextBinOp == finBinOp
                        && nextIsDouble == finIsDouble
                        && AreSamePureSimdExpr(*finBase, *nextBase)
                        && sameRhs
                        && (accMask & static_cast<u8>(1u << nextLane)) == 0u
                    ){
                        accMask |= static_cast<u8>(1u << nextLane);
                        rhsSwizzle[nextLane] = nextRhsLane;
                        matchedIndices.push_back(scan);
                        if(!SimdComponentMaskConstant(accMask, finIsDouble).empty()){
                            bestCount = matchedIndices.size();
                            bestMask = accMask;
                            bestSwizzle = rhsSwizzle;
                        }
                        continue;
                    }
                }
                if(!IsPureSimdLaneLogicTerm(terms[scan], isOrChain))
                    break;
            }
            if(bestCount >= 2u){
                for(usize k = 0u; k < bestCount; ++k)
                    consumed[matchedIndices[k]] = true;
                FusedSegment seg;
                seg.kind = 1u;
                seg.start = idx;
                seg.laneMask = bestMask;
                seg.isDouble = finIsDouble;
                seg.cmpOp = finBinOp;
                seg.lhsBase = finBase;
                seg.rhsBase = finRhsBase;
                seg.rhsScalar = finRhsScalar;
                seg.rhsSwizzle = bestSwizzle;
                segments.push_back(seg);
                anyFused = true;
                continue;
            }
        }

        TokenType::Enum cmpOp = TokenType::EndOfFile;
        const Expr* lhsBase = nullptr;
        const Expr* rhsBase = nullptr;
        const Expr* rhsScalar = nullptr;
        b8 cmpIsDouble = false;
        b8 lhsAbs = false;
        b8 rhsAbs = false;
        u32 cmpLane = 0u;
        u32 cmpRhsLane = 0u;
        if(TryMatchSimdLaneComparisonTerm(terms[idx], cmpOp, lhsBase, rhsBase, rhsScalar, cmpIsDouble, lhsAbs, rhsAbs, cmpLane, cmpRhsLane)){
            u8 accMask = static_cast<u8>(1u << cmpLane);
            u8 uniformAccMask = accMask;
            Array<u32, 4> rhsSwizzle{0u, 1u, 2u, 3u};
            rhsSwizzle[cmpLane] = cmpRhsLane;
            Array<u32, 4> uniformSwizzle = rhsSwizzle;
            Array<const Expr*, 4> rhsScalars{nullptr, nullptr, nullptr, nullptr};
            rhsScalars[cmpLane] = rhsScalar;
            b8 allSameScalar = true;
            MVector<usize> matchedIndices(m_arena);
            matchedIndices.push_back(idx);
            MVector<usize> uniformMatchedIndices(m_arena);
            uniformMatchedIndices.push_back(idx);
            for(usize scan = idx + 1u; scan < terms.size(); ++scan){
                if(consumed[scan])
                    continue;
                TokenType::Enum nextCmpOp = TokenType::EndOfFile;
                const Expr* nextLhsBase = nullptr;
                const Expr* nextRhsBase = nullptr;
                const Expr* nextRhsScalar = nullptr;
                b8 nextIsDouble = false;
                b8 nextLhsAbs = false;
                b8 nextRhsAbs = false;
                u32 nextLane = 0u;
                u32 nextRhsLane = 0u;
                if(TryMatchSimdLaneComparisonTerm(terms[scan], nextCmpOp, nextLhsBase, nextRhsBase, nextRhsScalar, nextIsDouble, nextLhsAbs, nextRhsAbs, nextLane, nextRhsLane)){
                    const b8 compatibleRhs = rhsBase
                        ? (nextRhsBase && AreSamePureSimdExpr(*rhsBase, *nextRhsBase))
                        : (nextRhsScalar != nullptr);
                    if(
                        nextCmpOp == cmpOp
                        && nextIsDouble == cmpIsDouble
                        && nextLhsAbs == lhsAbs
                        && nextRhsAbs == rhsAbs
                        && AreSamePureSimdExpr(*lhsBase, *nextLhsBase)
                        && compatibleRhs
                    ){
                        const b8 sameScalar = !rhsBase && AreSamePureSimdExpr(*rhsScalar, *nextRhsScalar);
                        if((rhsBase || sameScalar) && (uniformAccMask & static_cast<u8>(1u << nextLane)) == 0u){
                            uniformAccMask |= static_cast<u8>(1u << nextLane);
                            uniformSwizzle[nextLane] = nextRhsLane;
                            uniformMatchedIndices.push_back(scan);
                        }
                        if((accMask & static_cast<u8>(1u << nextLane)) == 0u){
                            accMask |= static_cast<u8>(1u << nextLane);
                            rhsSwizzle[nextLane] = nextRhsLane;
                            rhsScalars[nextLane] = nextRhsScalar;
                            if(!rhsBase && !sameScalar)
                                allSameScalar = false;
                            matchedIndices.push_back(scan);
                        }
                    }
                    continue;
                }
                if(!IsPureSimdLaneLogicTerm(terms[scan], isOrChain))
                    break;
            }
            if(accMask != 0xFu){
                u8 compMask = 0u;
                TokenType::Enum compCmpOp = TokenType::EndOfFile;
                Array<u32, 4> compSwizzle = rhsSwizzle;
                Array<const Expr*, 4> compScalars = rhsScalars;
                b8 compSameScalar = allSameScalar;
                MVector<usize> compIndices(m_arena);
                for(usize scan = idx + 1u; scan < terms.size(); ++scan){
                    if(consumed[scan])
                        continue;
                    b8 alreadyInPrimary = false;
                    for(const usize mIdx : matchedIndices){
                        if(mIdx == scan){
                            alreadyInPrimary = true;
                            break;
                        }
                    }
                    if(alreadyInPrimary)
                        continue;
                    TokenType::Enum nextCmpOp = TokenType::EndOfFile;
                    const Expr* nextLhsBase = nullptr;
                    const Expr* nextRhsBase = nullptr;
                    const Expr* nextRhsScalar = nullptr;
                    b8 nextIsDouble = false;
                    b8 nextLhsAbs = false;
                    b8 nextRhsAbs = false;
                    u32 nextLane = 0u;
                    u32 nextRhsLane = 0u;
                    if(TryMatchSimdLaneComparisonTerm(terms[scan], nextCmpOp, nextLhsBase, nextRhsBase, nextRhsScalar, nextIsDouble, nextLhsAbs, nextRhsAbs, nextLane, nextRhsLane)){
                        const b8 compatibleRhs = rhsBase
                            ? (nextRhsBase && AreSamePureSimdExpr(*rhsBase, *nextRhsBase))
                            : (nextRhsScalar != nullptr);
                        if(
                            (compCmpOp == TokenType::EndOfFile || nextCmpOp == compCmpOp)
                            && nextIsDouble == cmpIsDouble
                            && nextLhsAbs == lhsAbs
                            && nextRhsAbs == rhsAbs
                            && ((accMask | compMask) & static_cast<u8>(1u << nextLane)) == 0u
                            && AreSamePureSimdExpr(*lhsBase, *nextLhsBase)
                            && compatibleRhs
                        ){
                            compCmpOp = nextCmpOp;
                            compMask |= static_cast<u8>(1u << nextLane);
                            compSwizzle[nextLane] = nextRhsLane;
                            compScalars[nextLane] = nextRhsScalar;
                            if(!rhsBase && !AreSamePureSimdExpr(*rhsScalar, *nextRhsScalar))
                                compSameScalar = false;
                            compIndices.push_back(scan);
                        }
                        continue;
                    }
                    if(!IsPureSimdLaneLogicTerm(terms[scan], isOrChain))
                        break;
                }
                if((accMask | compMask) == 0xFu && compMask != 0u){
                    for(const usize matchedIdx : matchedIndices)
                        consumed[matchedIdx] = true;
                    for(const usize compIdx : compIndices)
                        consumed[compIdx] = true;
                    FusedSegment seg;
                    seg.kind = 3u;
                    seg.start = idx;
                    seg.laneMask = accMask;
                    seg.compLaneMask = compMask;
                    seg.isDouble = cmpIsDouble;
                    seg.lhsAbs = lhsAbs;
                    seg.rhsAbs = rhsAbs;
                    seg.hasUniformRhsScalar = compSameScalar;
                    seg.cmpOp = cmpOp;
                    seg.compCmpOp = compCmpOp;
                    seg.lhsBase = lhsBase;
                    seg.rhsBase = rhsBase;
                    seg.rhsScalar = rhsScalar;
                    seg.rhsScalars = compScalars;
                    seg.rhsSwizzle = compSwizzle;
                    segments.push_back(seg);
                    anyFused = true;
                    continue;
                }
            }
            const b8 canUseNonUniformScalarMask = (accMask == 0x7u || accMask == 0xFu);
            if(matchedIndices.size() >= 2u && CountMaskBits4(accMask) >= 2u && (rhsBase != nullptr || allSameScalar || canUseNonUniformScalarMask)){
                for(const usize matchedIdx : matchedIndices)
                    consumed[matchedIdx] = true;
                FusedSegment seg;
                seg.kind = 2u;
                seg.start = idx;
                seg.laneMask = accMask;
                seg.isDouble = cmpIsDouble;
                seg.lhsAbs = lhsAbs;
                seg.rhsAbs = rhsAbs;
                seg.hasUniformRhsScalar = allSameScalar;
                seg.cmpOp = cmpOp;
                seg.lhsBase = lhsBase;
                seg.rhsBase = rhsBase;
                seg.rhsScalar = rhsScalar;
                seg.rhsScalars = rhsScalars;
                seg.rhsSwizzle = rhsSwizzle;
                segments.push_back(seg);
                anyFused = true;
                continue;
            }
            if(uniformMatchedIndices.size() >= 2u && CountMaskBits4(uniformAccMask) >= 2u){
                for(const usize matchedIdx : uniformMatchedIndices)
                    consumed[matchedIdx] = true;
                FusedSegment seg;
                seg.kind = 2u;
                seg.start = idx;
                seg.laneMask = uniformAccMask;
                seg.isDouble = cmpIsDouble;
                seg.lhsAbs = lhsAbs;
                seg.rhsAbs = rhsAbs;
                seg.hasUniformRhsScalar = true;
                seg.cmpOp = cmpOp;
                seg.lhsBase = lhsBase;
                seg.rhsBase = rhsBase;
                seg.rhsScalar = rhsScalar;
                seg.rhsSwizzle = uniformSwizzle;
                segments.push_back(seg);
                anyFused = true;
                continue;
            }
        }

        consumed[idx] = true;
        FusedSegment rawSeg;
        rawSeg.kind = 0u;
        rawSeg.start = idx;
        segments.push_back(rawSeg);
    }

    if(!anyFused)
        return false;

    const auto emitFusedVecOperand = [&](const Expr& baseExpr, const b8 wrapAbs){
        b8 vecIsDouble = false;
        MStringView regType;
        MStringView loadFn;
        u32 zeroedAbove = 4u;
        (void)TryGetAnyFloatVectorSourceInfo(baseExpr, vecIsDouble, regType, loadFn, zeroedAbove);
        if(wrapAbs)
            out.append(vecIsDouble ? "::VectorDoubleAbs(" : "::VectorAbs(");
        if(!regType.empty()){
            emitSimdRegExpr(out, baseExpr, regType);
        }
        else{
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            emitExpr(out, baseExpr);
            out.push_back(')');
        }
        if(wrapAbs)
            out.push_back(')');
    };

    const auto emitCmpSecondArg = [&](const FusedSegment& seg){
        const u8 activeMask = (seg.kind == 3u) ? 0xFu : seg.laneMask;
        if(seg.rhsBase){
            b8 needsSwizzle = false;
            u32 firstRhsLane = 0u;
            b8 allSameRhsLane = true;
            b8 sawActive = false;
            for(u32 i = 0u; i < 4u; ++i){
                if((activeMask & static_cast<u8>(1u << i)) != 0u){
                    if(seg.rhsSwizzle[i] != i)
                        needsSwizzle = true;
                    if(!sawActive){
                        firstRhsLane = seg.rhsSwizzle[i];
                        sawActive = true;
                    }
                    else if(seg.rhsSwizzle[i] != firstRhsLane){
                        allSameRhsLane = false;
                    }
                }
            }
            if(!needsSwizzle){
                emitFusedVecOperand(*seg.rhsBase, seg.rhsAbs);
                return;
            }
            if(allSameRhsLane){
                const MStringView splatPrefix = SimdSplatIntrinsic(firstRhsLane, seg.isDouble);
                out.append(splatPrefix.data(), splatPrefix.size());
                emitFusedVecOperand(*seg.rhsBase, seg.rhsAbs);
                out.push_back(')');
                return;
            }
            Array<u32, 4> swiz = seg.rhsSwizzle;
            if(activeMask == 0x3u){
                swiz[2] = swiz[0];
                swiz[3] = swiz[1];
            }
            else if(activeMask == 0xCu){
                swiz[0] = swiz[2];
                swiz[1] = swiz[3];
            }
            out.append(seg.isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
            for(u32 i = 0u; i < 4u; ++i){
                if(i > 0u)
                    out.append(", ");
                AppendUnsignedDecimal(out, swiz[i]);
                out.push_back('u');
            }
            out.append(">(");
            emitFusedVecOperand(*seg.rhsBase, seg.rhsAbs);
            out.push_back(')');
        }
        else if(seg.hasUniformRhsScalar){
            emitSimdRegExpr(out, *seg.rhsScalar, seg.isDouble ? "VectorDouble" : "VectorSingle");
        }
        else{
            const usize argCount = (activeMask == 0x7u) ? 3u : 4u;
            emitSimdVectorCtorReg(out, seg.isDouble ? "VectorDouble" : "VectorSingle", Span<const Expr* const>(seg.rhsScalars.data(), argCount));
        }
    };

    const auto emitSegment = [&](const FusedSegment& seg){
        if(seg.kind == 0u){
            emitExpr(out, *terms[seg.start]);
            return;
        }
        const b8 isDouble = seg.isDouble;
        if(seg.kind == 1u){
            const MStringView maskConst = SimdComponentMaskConstant(seg.laneMask, isDouble);
            if(isOrChain)
                out.push_back('!');
            out.append(isDouble ? "::VectorDoubleIsFinite(" : "::VectorIsFinite(");
            if(seg.cmpOp != TokenType::EndOfFile){
                MStringView binPrefix;
                switch(seg.cmpOp){
                case TokenType::Plus: binPrefix = isDouble ? "::VectorDoubleAdd(" : "::VectorAdd("; break;
                case TokenType::Minus: binPrefix = isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract("; break;
                case TokenType::Star: binPrefix = isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply("; break;
                case TokenType::Slash: binPrefix = isDouble ? "::VectorDoubleDivide(" : "::VectorDivide("; break;
                default: break;
                }
                out.append(binPrefix.data(), binPrefix.size());
                emitFusedVecOperand(*seg.lhsBase, false);
                out.append(", ");
                emitCmpSecondArg(seg);
                out.push_back(')');
            }
            else{
                emitFusedVecOperand(*seg.lhsBase, false);
            }
            out.append(", ");
            out.append(maskConst.data(), maskConst.size());
            out.push_back(')');
            return;
        }
        if(seg.kind == 3u){
            const MStringView cmpPrefix0 = MapSimdVectorCompareIntrinsicPrefix(seg.cmpOp, isDouble);
            const MStringView cmpPrefix1 = MapSimdVectorCompareIntrinsicPrefix(seg.compCmpOp, isDouble);
            out.append(isDouble ? "((::VectorDoubleMoveMask(::VectorDoublePermute<" : "((::VectorMoveMask(::VectorPermute<");
            for(u32 i = 0u; i < 4u; ++i){
                if(i > 0u)
                    out.append(", ");
                const u32 permLane = ((seg.compLaneMask & static_cast<u8>(1u << i)) != 0u) ? (4u + i) : i;
                AppendUnsignedDecimal(out, permLane);
                out.push_back('u');
            }
            out.append(">(");
            out.append(cmpPrefix0.data(), cmpPrefix0.size());
            emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
            out.append(", ");
            emitCmpSecondArg(seg);
            out.append("), ");
            out.append(cmpPrefix1.data(), cmpPrefix1.size());
            emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
            out.append(", ");
            emitCmpSecondArg(seg);
            out.append("))) & ");
            AppendSimdMoveMaskHexOrConstant(out, 0xFu, isDouble);
            if(isOrChain){
                out.append(") != 0u)");
            }
            else{
                out.append(") == ");
                AppendSimdMoveMaskHexOrConstant(out, 0xFu, isDouble);
                out.push_back(')');
            }
            return;
        }

        if(!isOrChain){
            if(seg.laneMask == 0x7u){
                MStringView fnPrefix;
                b8 swapOperands = false;
                switch(seg.cmpOp){
                case TokenType::EqualEqual: fnPrefix = isDouble ? "::VectorDouble3Equal(" : "::Vector3Equal("; break;
                case TokenType::Greater: fnPrefix = isDouble ? "::VectorDouble3Greater(" : "::Vector3Greater("; break;
                case TokenType::GreaterEqual: fnPrefix = isDouble ? "::VectorDouble3GreaterOrEqual(" : "::Vector3GreaterOrEqual("; break;
                case TokenType::LessEqual: fnPrefix = isDouble ? "::VectorDouble3LessOrEqual(" : "::Vector3LessOrEqual("; break;
                case TokenType::Less:
                    fnPrefix = isDouble ? "::VectorDouble3Greater(" : "::Vector3Greater(";
                    swapOperands = true;
                    break;
                default:
                    break;
                }
                if(!fnPrefix.empty()){
                    out.append(fnPrefix.data(), fnPrefix.size());
                    if(swapOperands){
                        emitCmpSecondArg(seg);
                        out.append(", ");
                        emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
                    }
                    else{
                        emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
                        out.append(", ");
                        emitCmpSecondArg(seg);
                    }
                    out.push_back(')');
                    return;
                }
            }
            if(seg.laneMask == 0xFu){
                MStringView fnPrefix;
                switch(seg.cmpOp){
                case TokenType::EqualEqual: fnPrefix = isDouble ? "::VectorDouble4Equal(" : "::Vector4Equal("; break;
                case TokenType::Greater: fnPrefix = isDouble ? "::VectorDouble4Greater(" : "::Vector4Greater("; break;
                case TokenType::GreaterEqual: fnPrefix = isDouble ? "::VectorDouble4GreaterOrEqual(" : "::Vector4GreaterOrEqual("; break;
                case TokenType::Less: fnPrefix = isDouble ? "::VectorDouble4Less(" : "::Vector4Less("; break;
                case TokenType::LessEqual: fnPrefix = isDouble ? "::VectorDouble4LessOrEqual(" : "::Vector4LessOrEqual("; break;
                default:
                    break;
                }
                if(!fnPrefix.empty()){
                    out.append(fnPrefix.data(), fnPrefix.size());
                    emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
                    out.append(", ");
                    emitCmpSecondArg(seg);
                    out.push_back(')');
                    return;
                }
            }
            if(seg.laneMask == 0x3u && (seg.cmpOp == TokenType::GreaterEqual || seg.cmpOp == TokenType::LessEqual)){
                if(!isDouble && seg.cmpOp == TokenType::LessEqual){
                    out.append("::Vector2LessOrEqual(");
                    emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
                    out.append(", ");
                    emitCmpSecondArg(seg);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDouble2GreaterOrEqual(" : "::Vector2GreaterOrEqual(");
                if(seg.cmpOp == TokenType::LessEqual){
                    emitCmpSecondArg(seg);
                    out.append(", ");
                    emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
                }
                else{
                    emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
                    out.append(", ");
                    emitCmpSecondArg(seg);
                }
                out.push_back(')');
                return;
            }
        }
        else if(seg.cmpOp == TokenType::BangEqual && (seg.laneMask == 0x7u || seg.laneMask == 0xFu)){
            if(seg.laneMask == 0x7u)
                out.append(isDouble ? "!::VectorDouble3Equal(" : "!::Vector3Equal(");
            else
                out.append(isDouble ? "!::VectorDouble4Equal(" : "!::Vector4Equal(");
            emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
            out.append(", ");
            emitCmpSecondArg(seg);
            out.push_back(')');
            return;
        }

        const MStringView cmpPrefix = MapSimdVectorCompareIntrinsicPrefix(seg.cmpOp, isDouble);
        out.append(isDouble ? "((::VectorDoubleMoveMask(" : "((::VectorMoveMask(");
        out.append(cmpPrefix.data(), cmpPrefix.size());
        emitFusedVecOperand(*seg.lhsBase, seg.lhsAbs);
        out.append(", ");
        emitCmpSecondArg(seg);
        out.append(")) & ");
        AppendSimdMoveMaskHexOrConstant(out, seg.laneMask, isDouble);
        if(isOrChain){
            out.append(") != 0u)");
        }
        else{
            out.append(") == ");
            AppendSimdMoveMaskHexOrConstant(out, seg.laneMask, isDouble);
            out.push_back(')');
        }
    };

    const b8 needOuterParens = !omitOuterBinaryParens && segments.size() > 1u;
    if(needOuterParens)
        out.push_back('(');
    for(usize i = 0u; i < segments.size(); ++i){
        if(i > 0u)
            out.append(isOrChain ? " || " : " && ");
        emitSegment(segments[i]);
    }
    if(needOuterParens)
        out.push_back(')');
    return true;
}

void SourceEmitter::emitExpr(MString& out, const Expr& expr, const b8 omitOuterBinaryParens)const{
    const TokenType::Enum prevUnfusedLogicOp = m_unfusedLogicOp;
    if(expr.kind != ExprKind::Binary || expr.op != prevUnfusedLogicOp)
        m_unfusedLogicOp = TokenType::EndOfFile;

    if(expr.wrapInSimdResult){
        out.append(IsDoubleSimdType(expr.resolvedSimdType) ? "::SIMDVectorDoubleResult{ " : "::SIMDVectorSingleResult{ ");
        Expr unwrapped = expr;
        unwrapped.wrapInSimdResult = false;
        emitExpr(out, unwrapped);
        out.append(" }");
        return;
    }

    if(expr.adaptToTChar && expr.kind != ExprKind::StringLiteral){
        out.append("::ConvertString(");
        Expr unwrapped = expr;
        unwrapped.adaptToTChar = false;
        emitExpr(out, unwrapped);
        out.push_back(')');
        return;
    }

    if(expr.moveAtLastUse){
        out.append("::Move(");
        out.append(expr.text.data(), expr.text.size());
        out.push_back(')');
        return;
    }

    if(isDirectSimdRegExpr(expr)){
        emitSimdRegExpr(out, expr, expr.resolvedSimdType);
        return;
    }

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:{
        if(expr.derefNotNull){
            out.append("(*");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            if(const BuiltinGlobalConstantInfo* globalConst = expr.isBuiltinConstant ? FindBuiltinGlobalConstant(expr.text) : nullptr)
                out.append(globalConst->cppExportName.data(), globalConst->cppExportName.size());
            else
                out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::StringLiteral:{
        if(expr.adaptToTChar){
            out.append("NWB_TEXT(\"");
            out.append(expr.text.data(), expr.text.size());
            out.append("\")");
        }
        else{
            out.push_back('"');
            out.append(expr.text.data(), expr.text.size());
            out.push_back('"');
        }
        break;
    }
    case ExprKind::ThisMember:{
        if(expr.derefNotNull){
            out.append("(*this->");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            out.append("this->");
            out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier && expr.lhs->callTemplateArgs.empty() && !expr.derefNotNull && !expr.isPointerMemberAccess && !m_symbols.findGlobalConstant(expr.lhs->text)){
            const MStringView resolvedBaseType = m_symbols.resolveBaseTypeName(expr.lhs->text);
            MStringView limitSuffix;
            if(IsNumericScalarType(resolvedBaseType) && TryMapScalarLimitMember(expr.text, &limitSuffix)){
                out.append("::Limit<::");
                out.append(resolvedBaseType.data(), resolvedBaseType.size());
                out.append(limitSuffix.data(), limitSuffix.size());
                break;
            }
        }
        u32 lane = 0u;
        if(!expr.isMutArg && expr.lhs && SymbolTable::IsSimdRegisterVectorType(expr.lhs->resolvedSimdType) && IsSingleSwizzleLane(expr.text, lane)){
            emitSimdLaneGetExpr(out, *expr.lhs, lane, nullptr);
            break;
        }
        if(expr.derefNotNull)
            out.append("(*(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isPointerMemberAccess)
            out.append("->");
        else
            out.push_back('.');
        out.append(expr.text.data(), expr.text.size());
        if(expr.derefNotNull)
            out.append("))");
        break;
    }
    case ExprKind::ScopeAccess:{
        for(usize i = 0u; i < expr.scopeChain.size(); ++i){
            if(i > 0u)
                out.append("::");
            out.append(expr.scopeChain[i].data(), expr.scopeChain[i].size());
        }
        break;
    }
    case ExprKind::Index:{
        if(!expr.isMutArg && expr.lhs && expr.rhs && SymbolTable::IsSimdRegisterVectorType(expr.lhs->resolvedSimdType)){
            emitSimdLaneGetExpr(out, *expr.lhs, 0u, expr.rhs);
            break;
        }
        if(expr.isStringByteMutRef)
            out.append("reinterpret_cast<::u8&>(");
        else if(expr.isStringByteConstRef)
            out.append("reinterpret_cast<const ::u8&>(");
        else if(expr.isStringByteRead)
            out.append("static_cast<::u8>(");
        if(expr.derefNotNull)
            out.append("(*(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isMatrixRowIndex)
            out.append((expr.lhs && SymbolTable::IsSimdRegisterMatrixType(expr.lhs->resolvedSimdType)) ? ".v[" : ".rows[");
        else if(expr.isVectorRawIndex)
            out.append(".raw[");
        else
            out.push_back('[');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(']');
        if(expr.derefNotNull)
            out.append("))");
        if(expr.isStringByteMutRef || expr.isStringByteConstRef || expr.isStringByteRead)
            out.push_back(')');
        break;
    }
    case ExprKind::Call:{
        if(expr.isBuiltinCall && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr.lhs->text;
            const MStringView registerCall = MapSimdRegisterCall(callee);
            if(!registerCall.empty()){
                out.append(registerCall.data(), registerCall.size());
                out.push_back('(');
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(expr.arguments[i]){
                        if(IsRegisterPromotableSimdType(expr.arguments[i]->resolvedSimdType))
                            emitSimdRegExpr(out, *expr.arguments[i], expr.arguments[i]->resolvedSimdType);
                        else
                            emitExpr(out, *expr.arguments[i]);
                    }
                }
                out.push_back(')');
                break;
            }
            if(callee == "isFinite" && expr.arguments.size() == 1u && expr.arguments[0] && (expr.arguments[0]->resolvedSimdType == "VectorSingle" || expr.arguments[0]->resolvedSimdType == "VectorDouble")){
                const Expr& arg0 = *expr.arguments[0];
                const b8 isDouble = IsDoubleSimdType(arg0.resolvedSimdType);
                u8 activeMask = 0xFu;
                const Expr* vecBase = &arg0;
                if(arg0.kind == ExprKind::MemberAccess && arg0.isSwizzle && arg0.lhs && arg0.lhs->resolvedSimdType == arg0.resolvedSimdType){
                    u8 swizMask = 0u;
                    for(usize i = 0u; i < arg0.text.size(); ++i)
                        swizMask |= static_cast<u8>(1u << MapSwizzleCharToLane(arg0.text[i]));
                    if(!SimdComponentMaskConstant(swizMask, isDouble).empty()){
                        activeMask = swizMask;
                        vecBase = arg0.lhs;
                    }
                    else{
                        const u32 effLanes = InferEffectiveSimdLanes(arg0, &m_symbols);
                        activeMask = effLanes == 2u ? 0x3u : (effLanes == 3u ? 0x7u : 0xFu);
                    }
                }
                const MStringView maskConst = SimdComponentMaskConstant(activeMask, isDouble);
                out.append(isDouble ? "::VectorDoubleIsFinite(" : "::VectorIsFinite(");
                emitSimdRegExpr(out, *vecBase, arg0.resolvedSimdType);
                out.append(", ");
                out.append(maskConst.data(), maskConst.size());
                out.push_back(')');
                break;
            }
            const MStringView scalarReductionSuffix = MapSimdScalarReductionSuffix(callee);
            if(!scalarReductionSuffix.empty() && !expr.arguments.empty() && expr.arguments[0] && IsRegisterPromotableSimdType(expr.arguments[0]->resolvedSimdType)){
                const MStringView argSimd = expr.arguments[0]->resolvedSimdType;
                const b8 isDouble = IsDoubleSimdType(argSimd);
                u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0], &m_symbols);
                if(expr.arguments.size() == 2u && expr.arguments[1]){
                    const u32 effLanes1 = InferEffectiveSimdLanes(*expr.arguments[1], &m_symbols);
                    if(effLanes1 < effLanes)
                        effLanes = effLanes1;
                }
                if(callee == "dot" && effLanes == 4u && !IsIntSimdType(argSimd) && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                    const Expr* sumArg = IsSimdUnitFloatVectorExpr(*expr.arguments[1], isDouble, &m_symbols)
                        ? expr.arguments[0]
                        : (IsSimdUnitFloatVectorExpr(*expr.arguments[0], isDouble, &m_symbols) ? expr.arguments[1] : nullptr);
                    if(sumArg){
                        out.append(isDouble ? "::VectorDoubleGetX(::VectorDoubleSum(" : "::VectorGetX(::VectorSum(");
                        emitSimdRegExpr(out, *sumArg, argSimd);
                        out.append("))");
                        break;
                    }
                }
                out.append(isDouble ? "::VectorDoubleGetX(" : "::VectorGetX(");
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, scalarReductionSuffix);
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(expr.arguments[i])
                        emitSimdRegExpr(out, *expr.arguments[i], argSimd);
                }
                out.append("))");
                break;
            }
            if(callee == "lerp" && expr.arguments.size() == 3u && expr.arguments[0] && expr.arguments[1] && expr.arguments[2] && expr.resolvedSimdType.empty()){
                out.append("([](auto&& __adhd_lerp_a, auto&& __adhd_lerp_b, auto&& __adhd_lerp_t) -> decltype(auto) { return (::Forward<decltype(__adhd_lerp_a)>(__adhd_lerp_a) + ((::Forward<decltype(__adhd_lerp_b)>(__adhd_lerp_b) - __adhd_lerp_a) * ::Forward<decltype(__adhd_lerp_t)>(__adhd_lerp_t))); }(");
                emitExpr(out, *expr.arguments[0]);
                out.append(", ");
                emitExpr(out, *expr.arguments[1]);
                out.append(", ");
                emitExpr(out, *expr.arguments[2]);
                out.append("))");
                break;
            }
            if(callee == "rsqrt" && expr.arguments.size() == 1u && expr.arguments[0] && expr.resolvedSimdType.empty()){
                out.append("(static_cast<decltype(");
                emitExpr(out, *expr.arguments[0]);
                out.append(")>(1) / ::Sqrt(");
                emitExpr(out, *expr.arguments[0]);
                out.append("))");
                break;
            }
            const Expr* maxBaseVec = nullptr;
            b8 maxIsDouble = false;
            u32 maxLanes = 0u;
            b8 maxIsAbs = false;
            if(callee == "max" && TryMatchSimdMaxComponentReduction(expr, maxBaseVec, maxIsDouble, maxLanes, maxIsAbs) && maxBaseVec){
                out.append(maxIsDouble ? "::VectorDoubleGetX(" : "::VectorGetX(");
                AppendSimdLaneCountIntrinsicPrefix(out, maxIsDouble, maxLanes, "MaxComponent");
                if(maxIsAbs)
                    out.append(maxIsDouble ? "::VectorDoubleAbs(" : "::VectorAbs(");
                emitSimdRegExpr(out, *maxBaseVec, maxIsDouble ? "VectorDouble" : "VectorSingle");
                if(maxIsAbs)
                    out.push_back(')');
                out.append("))");
                break;
            }
        }

        if(expr.isStringSubstr && expr.lhs && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            out.append("([](const auto& __adhd_str, const ::usize __adhd_pos = 0u, const ::usize __adhd_count = ::AStringView::npos)noexcept{ const ::AStringView __adhd_view(__adhd_str.data(), __adhd_str.size()); const ::usize __adhd_clamped = __adhd_pos < __adhd_view.size() ? __adhd_pos : __adhd_view.size(); const ::AStringView __adhd_sub = __adhd_view.substr(__adhd_clamped, __adhd_count); return ::RemoveCVRef_T<decltype(__adhd_str)>(__adhd_sub.data(), __adhd_sub.size(), __adhd_str.get_allocator()); }(");
            emitExpr(out, *expr.lhs->lhs);
            for(const Expr* arg : expr.arguments){
                if(!arg)
                    continue;
                out.append(", ");
                emitExpr(out, *arg);
            }
            out.append("))");
            break;
        }
        const MStringView baseCallee = (expr.lhs && expr.lhs->kind == ExprKind::Identifier)
            ? m_symbols.resolveBaseTypeName(expr.lhs->text)
            : MStringView{};
        if((SymbolTable::IsSimdStorageType(baseCallee) || SymbolTable::IsUnalignedVectorType(baseCallee)) && !expr.arguments.empty()){
            MString storeCallStr(m_arena);
            if(tryEmitStorageStoreFromSimdArgs(storeCallStr, baseCallee, expr.arguments, "__adhd_tmp")){
                TypeRef tmpType(m_arena);
                tmpType.name = expr.lhs->text;
                out.append("([&]()noexcept{ ");
                m_typeEmitter.emitTypeCpp(out, tmpType);
                out.append(" __adhd_tmp{}; ");
                out.append(storeCallStr.data(), storeCallStr.size());
                out.append("; return __adhd_tmp; }())");
                break;
            }
        }
        if(expr.isStringByteMutRef)
            out.append("reinterpret_cast<::u8&>(");
        else if(expr.isStringByteConstRef)
            out.append("reinterpret_cast<const ::u8&>(");
        else if(expr.isStringByteRead)
            out.append("static_cast<::u8>(");
        if(expr.derefNotNull)
            out.append("(*");
        emitCallTargetExpr(out, expr.lhs, expr.isBuiltinCall);
        if(!expr.callTemplateArgs.empty())
            m_typeEmitter.emitTypeArgListCpp(out, expr.callTemplateArgs);
        const MStringView injectedArena = expr.injectClassArena
            ? MStringView("s_ClassArena")
            : (expr.injectScratchArena ? MStringView("__adhd_scratch") : MStringView{});
        emitCtorOrCallArgs(out, baseCallee, expr.arguments, injectedArena, false, false);
        if(expr.unwrapSimdResultValue)
            out.append(".value");
        if(expr.derefNotNull)
            out.push_back(')');
        if(expr.isStringByteMutRef || expr.isStringByteConstRef || expr.isStringByteRead)
            out.push_back(')');
        break;
    }
    case ExprKind::Unary:{
        if(expr.isPostfix){
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(expr.text.data(), expr.text.size());
        }
        else{
            out.append(expr.text.data(), expr.text.size());
            if(expr.lhs){
                const b8 wrapPrefixChild = (expr.lhs->kind == ExprKind::Unary && !expr.lhs->isPostfix);
                if(wrapPrefixChild)
                    out.push_back('(');
                emitExpr(out, *expr.lhs);
                if(wrapPrefixChild)
                    out.push_back(')');
            }
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.isNotNullRebind){
            if(!omitOuterBinaryParens)
                out.push_back('(');
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(" = ::MakeNotNull(&(");
            if(expr.rhs)
                emitExpr(out, *expr.rhs);
            out.append(omitOuterBinaryParens ? "))" : ")))");
            break;
        }
        if(expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe){
            if(prevUnfusedLogicOp != expr.op && tryEmitFusedSimdLogicChain(out, expr, omitOuterBinaryParens))
                break;
            m_unfusedLogicOp = expr.op;
        }
        if(!omitOuterBinaryParens)
            out.push_back('(');
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(' ');
        out.append(expr.text.data(), expr.text.size());
        out.push_back(' ');
        if(expr.rhs){
            m_unfusedLogicOp = (expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe) ? expr.op : TokenType::EndOfFile;
            emitExpr(out, *expr.rhs);
        }
        m_unfusedLogicOp = TokenType::EndOfFile;
        if(!omitOuterBinaryParens)
            out.push_back(')');
        break;
    }
    case ExprKind::Cast:{
        out.append("static_cast<");
        if(expr.targetType)
            m_typeEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::BitCast:{
        if(
            expr.targetType
            && expr.targetType->refMode == ParamPassingMode::None
            && expr.targetType->templateArgs.empty()
            && expr.lhs
            && !expr.lhs->isMutArg
        ){
            const Expr* vecBase = nullptr;
            u32 staticLane = 0u;
            const Expr* dynamicIdx = nullptr;
            if(
                expr.lhs->kind == ExprKind::MemberAccess
                && !expr.lhs->isSwizzle
                && expr.lhs->lhs
                && SymbolTable::IsSimdRegisterVectorType(expr.lhs->lhs->resolvedSimdType)
                && IsSingleSwizzleLane(expr.lhs->text, staticLane)
            ){
                vecBase = expr.lhs->lhs;
                while(
                    vecBase->kind == ExprKind::MemberAccess
                    && vecBase->isSwizzle
                    && vecBase->lhs
                    && vecBase->lhs->resolvedSimdType == vecBase->resolvedSimdType
                    && staticLane < vecBase->text.size()
                ){
                    staticLane = MapSwizzleCharToLane(vecBase->text[staticLane]);
                    vecBase = vecBase->lhs;
                }
            }
            else if(
                expr.lhs->kind == ExprKind::Index
                && expr.lhs->lhs
                && expr.lhs->rhs
                && SymbolTable::IsSimdRegisterVectorType(expr.lhs->lhs->resolvedSimdType)
            ){
                vecBase = expr.lhs->lhs;
                dynamicIdx = expr.lhs->rhs;
            }
            if(vecBase){
                const MStringView vecSimd = vecBase->resolvedSimdType;
                const b8 isDouble = IsDoubleSimdType(vecSimd);
                const MStringView targetScalar = m_symbols.resolveBaseTypeName(expr.targetType->name);
                const b8 isUnsignedIntTarget = isDouble ? (targetScalar == "u64") : (targetScalar == "u32");
                const b8 isSignedIntTarget = isDouble ? (targetScalar == "i64") : (targetScalar == "i32");
                const b8 isFloatTarget = isDouble ? (targetScalar == "f64") : (targetScalar == "f32");
                if(isUnsignedIntTarget || isSignedIntTarget || isFloatTarget){
                    if(isSignedIntTarget)
                        out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
                    if(isFloatTarget)
                        out.append(isDouble ? "::VectorDoubleGet" : "::VectorGet");
                    else
                        out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
                    if(dynamicIdx){
                        out.append("ByIndex(");
                        emitSimdRegExpr(out, *vecBase, vecSimd);
                        out.append(", static_cast<usize>(");
                        emitExpr(out, *dynamicIdx);
                        out.append("))");
                    }
                    else{
                        const MStringView laneSuffix = SimdLaneCallSuffix(staticLane);
                        out.append(laneSuffix.data(), laneSuffix.size());
                        emitSimdRegExpr(out, *vecBase, vecSimd);
                        out.push_back(')');
                    }
                    if(isSignedIntTarget)
                        out.push_back(')');
                    break;
                }
            }
        }
        out.append("::BitCast<");
        if(expr.targetType)
            m_typeEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Lambda:{
        out.push_back('[');
        b8 firstEmittedCap = true;
        for(usize i = 0u; i < expr.lambdaCaptures.size(); ++i){
            const LambdaCapture& cap = expr.lambdaCaptures[i];
            if(cap.isGlobalConstant)
                continue;
            if(!firstEmittedCap)
                out.append(", ");
            firstEmittedCap = false;
            if(cap.name == "this"){
                out.append("this");
            }
            else if(cap.mode == ParamPassingMode::Mut){
                out.push_back('&');
                out.append(cap.name.data(), cap.name.size());
            }
            else{
                out.append(cap.name.data(), cap.name.size());
            }
        }
        out.push_back(']');
        m_typeEmitter.emitParamListCpp(out, expr.lambdaParams);
        out.append("noexcept -> ");
        if(expr.lambdaReturnType)
            m_typeEmitter.emitTypeCpp(out, *expr.lambdaReturnType);
        else
            out.append("void");
        out.append("{\r\n");
        if(expr.lambdaUsesScratchArena)
            emitScratchArenaPrologue(out, 2u, "lambda");
        if(expr.lambdaUsesTaskScope)
            emitTaskScopePrologue(out, 2u);
        const u32 prevSwitchBreak = m_activeSwitchBreakLabelLine;
        m_activeSwitchBreakLabelLine = 0u;
        emitStatementList(out, expr.lambdaBody, 2u);
        m_activeSwitchBreakLabelLine = prevSwitchBreak;
        out.append("    }");
        break;
    }
    }
}

void SourceEmitter::emitCallTargetExpr(MString& out, const Expr* lhs, const b8 isBuiltinCall)const{
    if(!lhs)
        return;
    if(lhs->kind == ExprKind::Identifier){
        const MStringView mappedMath = isBuiltinCall ? MapBuiltinMathFunction(lhs->text) : MStringView{};
        if(!mappedMath.empty()){
            out.append(mappedMath.data(), mappedMath.size());
        }
        else{
            TypeRef maybeType(m_arena);
            maybeType.name = lhs->text;
            m_typeEmitter.emitTypeCpp(out, maybeType);
        }
    }
    else{
        emitExpr(out, *lhs);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
