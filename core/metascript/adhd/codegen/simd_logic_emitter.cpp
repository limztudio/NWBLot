// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_logic_emitter.h"
#include "simd_expression_analysis.h"


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
        || callExpr->lhs->text != "IsFinite"
        || callExpr->arguments.size() != 1u
        || !callExpr->arguments[0]
    ){
        return false;
    }
    if(!SimdExpressionAnalysis::TryMatchSingleLaneAccess(callExpr->arguments[0], outBase, outLane) || !outBase)
        return false;
    return IsFloatSimdRegisterVectorType(outBase->resolvedSimdType) && SimdExpressionAnalysis::IsPureSimdAddressExpr(*outBase);
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
    const b8 lhsIsLane = SimdExpressionAnalysis::TryMatchSingleLaneAccess(term->lhs, lhsBase, lhsLane)
        && lhsBase
        && IsFloatSimdRegisterVectorType(lhsBase->resolvedSimdType)
        && SimdExpressionAnalysis::IsPureSimdAddressExpr(*lhsBase);
    const b8 rhsIsLane = SimdExpressionAnalysis::TryMatchSingleLaneAccess(term->rhs, rhsBase, rhsLane)
        && rhsBase
        && IsFloatSimdRegisterVectorType(rhsBase->resolvedSimdType)
        && SimdExpressionAnalysis::IsPureSimdAddressExpr(*rhsBase);

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
    if(lhsIsLane && term->rhs->resolvedSimdType.empty() && SimdExpressionAnalysis::IsPureSimdAddressExpr(*term->rhs)){
        outCmpOp = term->op;
        outLhsBase = lhsBase;
        outRhsBase = nullptr;
        outRhsScalar = term->rhs;
        outLane = lhsLane;
        return true;
    }
    if(rhsIsLane && term->lhs->resolvedSimdType.empty() && SimdExpressionAnalysis::IsPureSimdAddressExpr(*term->lhs)){
        outCmpOp = FlipComparisonOp(term->op);
        outLhsBase = rhsBase;
        outRhsBase = nullptr;
        outRhsScalar = term->lhs;
        outLane = rhsLane;
        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SimdLogicEmitter::tryEmit(MString& out, const Expr& expr, const b8 omitOuterBinaryParens)const{
    using namespace __hidden_adhd_expr;

    if(expr.kind != ExprKind::Binary || (expr.op != TokenType::AmpersandAmpersand && expr.op != TokenType::PipePipe))
        return false;

    MVector<const Expr*> terms(m_emitter.m_arena);
    CollectLogicChainTerms(expr, expr.op, terms);
    b8 hasSimdLaneTerm = false;
    for(const Expr* term : terms){
        const Expr* base = nullptr;
        const Expr* lhs = nullptr;
        const Expr* rhs = nullptr;
        const Expr* scalar = nullptr;
        TokenType::Enum comparison = TokenType::EndOfFile;
        u32 lane = 0u;
        if(TryMatchIsFiniteLaneTerm(term, expr.op == TokenType::PipePipe, base, lane)
            || TryMatchSimdLaneComparisonTerm(term, comparison, lhs, rhs, scalar, lane)){
            hasSimdLaneTerm = true;
            break;
        }
    }
    if(!hasSimdLaneTerm)
        return false;

    if(!omitOuterBinaryParens)
        out.push_back('(');
    for(usize i = 0u; i < terms.size(); ++i){
        if(i > 0u)
            out.append(expr.op == TokenType::PipePipe ? " || " : " && ");
        m_emitter.emitExpr(out, *terms[i]);
    }
    if(!omitOuterBinaryParens)
        out.push_back(')');
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

