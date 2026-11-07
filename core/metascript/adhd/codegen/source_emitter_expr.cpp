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

[[nodiscard]] b8 IsFloatSimdRegisterVectorType(const MStringView simdType)noexcept{
    return simdType == "VectorSingle" || simdType == "VectorDouble";
}

[[nodiscard]] b8 TryMatchIsFiniteLaneTerm(const Expr* term, const b8 expectNegated, const Expr*& outBase, u32& outLane)noexcept{
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
    if(!TryMatchSingleLaneAccess(callExpr->arguments[0], outBase, outLane) || !outBase)
        return false;
    return IsFloatSimdRegisterVectorType(outBase->resolvedSimdType) && IsPureSimdAddressExpr(*outBase);
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

[[nodiscard]] b8 TryMatchSimdLaneComparisonTerm(
    const Expr* term,
    TokenType::Enum& outCmpOp,
    const Expr*& outLhsBase,
    const Expr*& outRhsBase,
    const Expr*& outRhsScalar,
    u32& outLane
)noexcept{
    if(!term || term->kind != ExprKind::Binary || !IsSimdComparisonOp(term->op) || !term->lhs || !term->rhs)
        return false;

    const Expr* lhsBase = nullptr;
    u32 lhsLane = 0u;
    const Expr* rhsBase = nullptr;
    u32 rhsLane = 0u;
    const b8 lhsIsLane = TryMatchSingleLaneAccess(term->lhs, lhsBase, lhsLane)
        && lhsBase
        && IsFloatSimdRegisterVectorType(lhsBase->resolvedSimdType)
        && IsPureSimdAddressExpr(*lhsBase);
    const b8 rhsIsLane = TryMatchSingleLaneAccess(term->rhs, rhsBase, rhsLane)
        && rhsBase
        && IsFloatSimdRegisterVectorType(rhsBase->resolvedSimdType)
        && IsPureSimdAddressExpr(*rhsBase);

    if(lhsIsLane && rhsIsLane){
        if(lhsLane != rhsLane || lhsBase->resolvedSimdType != rhsBase->resolvedSimdType)
            return false;
        outCmpOp = term->op;
        outLhsBase = lhsBase;
        outRhsBase = rhsBase;
        outRhsScalar = nullptr;
        outLane = lhsLane;
        return true;
    }
    if(lhsIsLane && term->rhs->resolvedSimdType.empty() && IsPureSimdAddressExpr(*term->rhs)){
        outCmpOp = term->op;
        outLhsBase = lhsBase;
        outRhsBase = nullptr;
        outRhsScalar = term->rhs;
        outLane = lhsLane;
        return true;
    }
    if(rhsIsLane && term->lhs->resolvedSimdType.empty() && IsPureSimdAddressExpr(*term->lhs)){
        outCmpOp = FlipComparisonOp(term->op);
        outLhsBase = rhsBase;
        outRhsBase = nullptr;
        outRhsScalar = term->lhs;
        outLane = rhsLane;
        return true;
    }
    return false;
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

    struct FusedSegment{
        u8 kind = 0u;
        usize start = 0u;
        usize end = 0u;
        u8 laneMask = 0u;
        TokenType::Enum cmpOp = TokenType::EndOfFile;
        const Expr* lhsBase = nullptr;
        const Expr* rhsBase = nullptr;
        const Expr* rhsScalar = nullptr;
    };

    MVector<FusedSegment> segments(m_arena);
    b8 anyFused = false;
    usize idx = 0u;
    while(idx < terms.size()){
        const Expr* finBase = nullptr;
        u32 finLane = 0u;
        if(TryMatchIsFiniteLaneTerm(terms[idx], isOrChain, finBase, finLane)){
            const b8 isDouble = IsDoubleSimdType(finBase->resolvedSimdType);
            u8 accMask = static_cast<u8>(1u << finLane);
            usize bestEnd = 0u;
            u8 bestMask = 0u;
            usize scan = idx + 1u;
            while(scan < terms.size()){
                const Expr* nextBase = nullptr;
                u32 nextLane = 0u;
                if(!TryMatchIsFiniteLaneTerm(terms[scan], isOrChain, nextBase, nextLane) || !AreSamePureSimdExpr(*finBase, *nextBase))
                    break;
                accMask |= static_cast<u8>(1u << nextLane);
                ++scan;
                if(!SimdComponentMaskConstant(accMask, isDouble).empty()){
                    bestEnd = scan;
                    bestMask = accMask;
                }
            }
            if(bestEnd >= idx + 2u){
                FusedSegment seg;
                seg.kind = 1u;
                seg.start = idx;
                seg.end = bestEnd;
                seg.laneMask = bestMask;
                seg.lhsBase = finBase;
                segments.push_back(seg);
                anyFused = true;
                idx = bestEnd;
                continue;
            }
        }

        TokenType::Enum cmpOp = TokenType::EndOfFile;
        const Expr* lhsBase = nullptr;
        const Expr* rhsBase = nullptr;
        const Expr* rhsScalar = nullptr;
        u32 cmpLane = 0u;
        if(TryMatchSimdLaneComparisonTerm(terms[idx], cmpOp, lhsBase, rhsBase, rhsScalar, cmpLane)){
            u8 accMask = static_cast<u8>(1u << cmpLane);
            usize scan = idx + 1u;
            while(scan < terms.size()){
                TokenType::Enum nextCmpOp = TokenType::EndOfFile;
                const Expr* nextLhsBase = nullptr;
                const Expr* nextRhsBase = nullptr;
                const Expr* nextRhsScalar = nullptr;
                u32 nextLane = 0u;
                if(!TryMatchSimdLaneComparisonTerm(terms[scan], nextCmpOp, nextLhsBase, nextRhsBase, nextRhsScalar, nextLane))
                    break;
                if(nextCmpOp != cmpOp || !AreSamePureSimdExpr(*lhsBase, *nextLhsBase))
                    break;
                if(rhsBase){
                    if(!nextRhsBase || !AreSamePureSimdExpr(*rhsBase, *nextRhsBase))
                        break;
                }
                else{
                    if(!nextRhsScalar || !AreSamePureSimdExpr(*rhsScalar, *nextRhsScalar))
                        break;
                }
                accMask |= static_cast<u8>(1u << nextLane);
                ++scan;
            }
            if(scan >= idx + 2u && CountMaskBits4(accMask) >= 2u){
                FusedSegment seg;
                seg.kind = 2u;
                seg.start = idx;
                seg.end = scan;
                seg.laneMask = accMask;
                seg.cmpOp = cmpOp;
                seg.lhsBase = lhsBase;
                seg.rhsBase = rhsBase;
                seg.rhsScalar = rhsScalar;
                segments.push_back(seg);
                anyFused = true;
                idx = scan;
                continue;
            }
        }

        FusedSegment rawSeg;
        rawSeg.kind = 0u;
        rawSeg.start = idx;
        rawSeg.end = idx + 1u;
        segments.push_back(rawSeg);
        ++idx;
    }

    if(!anyFused)
        return false;

    const auto emitCmpSecondArg = [&](const FusedSegment& seg, const MStringView simdType){
        if(seg.rhsBase)
            emitSimdRegExpr(out, *seg.rhsBase, simdType);
        else
            emitSimdRegExpr(out, *seg.rhsScalar, simdType);
    };

    const auto emitSegment = [&](const FusedSegment& seg){
        if(seg.kind == 0u){
            emitExpr(out, *terms[seg.start]);
            return;
        }
        const MStringView simdType = seg.lhsBase->resolvedSimdType;
        const b8 isDouble = IsDoubleSimdType(simdType);
        if(seg.kind == 1u){
            const MStringView maskConst = SimdComponentMaskConstant(seg.laneMask, isDouble);
            if(isOrChain)
                out.push_back('!');
            out.append(isDouble ? "::VectorDoubleIsFinite(" : "::VectorIsFinite(");
            emitSimdRegExpr(out, *seg.lhsBase, simdType);
            out.append(", ");
            out.append(maskConst.data(), maskConst.size());
            out.push_back(')');
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
                        emitCmpSecondArg(seg, simdType);
                        out.append(", ");
                        emitSimdRegExpr(out, *seg.lhsBase, simdType);
                    }
                    else{
                        emitSimdRegExpr(out, *seg.lhsBase, simdType);
                        out.append(", ");
                        emitCmpSecondArg(seg, simdType);
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
                    emitSimdRegExpr(out, *seg.lhsBase, simdType);
                    out.append(", ");
                    emitCmpSecondArg(seg, simdType);
                    out.push_back(')');
                    return;
                }
            }
            if(seg.laneMask == 0x3u && (seg.cmpOp == TokenType::GreaterEqual || seg.cmpOp == TokenType::LessEqual)){
                out.append(isDouble ? "::VectorDouble2GreaterOrEqual(" : "::Vector2GreaterOrEqual(");
                if(seg.cmpOp == TokenType::LessEqual){
                    emitCmpSecondArg(seg, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *seg.lhsBase, simdType);
                }
                else{
                    emitSimdRegExpr(out, *seg.lhsBase, simdType);
                    out.append(", ");
                    emitCmpSecondArg(seg, simdType);
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
            emitSimdRegExpr(out, *seg.lhsBase, simdType);
            out.append(", ");
            emitCmpSecondArg(seg, simdType);
            out.push_back(')');
            return;
        }

        const MStringView cmpPrefix = MapSimdVectorCompareIntrinsicPrefix(seg.cmpOp, isDouble);
        out.append(isDouble ? "((::VectorDoubleMoveMask(" : "((::VectorMoveMask(");
        out.append(cmpPrefix.data(), cmpPrefix.size());
        emitSimdRegExpr(out, *seg.lhsBase, simdType);
        out.append(", ");
        emitCmpSecondArg(seg, simdType);
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
                        const u32 effLanes = InferEffectiveSimdLanes(arg0);
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
                const u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0]);
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
        if((expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe) && tryEmitFusedSimdLogicChain(out, expr, omitOuterBinaryParens))
            break;
        if(!omitOuterBinaryParens)
            out.push_back('(');
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(' ');
        out.append(expr.text.data(), expr.text.size());
        out.push_back(' ');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
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
