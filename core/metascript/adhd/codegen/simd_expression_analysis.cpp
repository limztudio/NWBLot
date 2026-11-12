// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_expression_analysis.h"

#include <core/metascript/adhd/ast/operator_semantics.h>

#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const Expr* SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(const Expr& expr, const SymbolTable& symbols)noexcept{
    if(expr.kind != ExprKind::Call || !expr.lhs || expr.lhs->kind != ExprKind::Identifier || expr.arguments.size() != 1u || !expr.arguments[0])
        return nullptr;
    const MStringView baseCallee = symbols.resolveBaseTypeName(expr.lhs->text);
    if(!SymbolTable::IsSimdRegisterVectorType(baseCallee))
        return nullptr;
    const Expr* arg0 = expr.arguments[0];
    if(!arg0->resolvedSimdType.empty() || !arg0->resolvedUnalignedType.empty())
        return nullptr;
    return arg0;
}

[[nodiscard]] b8 SimdExpressionAnalysis::IsZeroScalarLiteral(const Expr& expr, const SymbolTable* const symbols)noexcept{
    const Expr& unwrapped = SimdConstantAnalysis::UnwrapConstScalarExpr(expr, symbols);
    if(unwrapped.text.empty())
        return false;
    if(unwrapped.kind == ExprKind::IntegerLiteral){
        b8 sawZero = false;
        const b8 isHex = unwrapped.text.size() >= 2u && unwrapped.text[0] == '0' && (unwrapped.text[1] == 'x' || unwrapped.text[1] == 'X');
        for(usize i = isHex ? 2u : 0u; i < unwrapped.text.size(); ++i){
            const char c = unwrapped.text[i];
            if(c == '0'){
                sawZero = true;
                continue;
            }
            if(c == 'u' || c == 'U' || c == 'l' || c == 'L' || (!isHex && (c == 'b' || c == 'B')))
                continue;
            return false;
        }
        return sawZero;
    }
    if(unwrapped.kind == ExprKind::FloatLiteral || unwrapped.kind == ExprKind::DoubleLiteral){
        b8 sawZero = false;
        for(usize i = 0u; i < unwrapped.text.size(); ++i){
            const char c = unwrapped.text[i];
            if(c == 'e' || c == 'E')
                break;
            if(c == '0'){
                sawZero = true;
                continue;
            }
            if(c == '.' || c == 'f' || c == 'F')
                continue;
            return false;
        }
        return sawZero;
    }
    return false;
}

[[nodiscard]] b8 SimdExpressionAnalysis::IsPureSimdAddressExpr(const Expr& expr)noexcept{
    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:
    case ExprKind::ThisMember:
        return !expr.isMutArg;
    case ExprKind::ScopeAccess:
        return !expr.isMutArg && (!expr.lhs || SimdExpressionAnalysis::IsPureSimdAddressExpr(*expr.lhs));
    case ExprKind::MemberAccess:
        return !expr.isMutArg && expr.lhs && SimdExpressionAnalysis::IsPureSimdAddressExpr(*expr.lhs);
    case ExprKind::Index:
        return !expr.isMutArg && expr.lhs && expr.rhs && SimdExpressionAnalysis::IsPureSimdAddressExpr(*expr.lhs) && SimdExpressionAnalysis::IsPureSimdAddressExpr(*expr.rhs);
    case ExprKind::Unary:
        return !expr.isMutArg && !expr.isPostfix
            && (expr.op == TokenType::Minus || expr.op == TokenType::Plus || expr.op == TokenType::Tilde || expr.op == TokenType::Bang)
            && expr.lhs && SimdExpressionAnalysis::IsPureSimdAddressExpr(*expr.lhs);
    case ExprKind::Cast:
        return !expr.isMutArg && expr.targetType && expr.targetType->refMode == ParamPassingMode::None
            && expr.targetType->templateArgs.empty() && expr.lhs && SimdExpressionAnalysis::IsPureSimdAddressExpr(*expr.lhs);
    default:
        return false;
    }
}

[[nodiscard]] b8 SimdExpressionAnalysis::AreSamePureSimdExpr(const Expr& a, const Expr& b)noexcept{
    if(
        a.kind != b.kind
        || a.text != b.text
        || a.op != b.op
        || a.isPostfix != b.isPostfix
        || a.isSwizzle != b.isSwizzle
        || a.isPointerMemberAccess != b.isPointerMemberAccess
    ){
        return false;
    }
    if((a.targetType != nullptr) != (b.targetType != nullptr))
        return false;
    if(a.targetType && (a.targetType->name != b.targetType->name || a.targetType->refMode != b.targetType->refMode || a.targetType->templateArgs.size() != b.targetType->templateArgs.size()))
        return false;
    if(a.scopeChain.size() != b.scopeChain.size())
        return false;
    for(usize i = 0u; i < a.scopeChain.size(); ++i){
        if(a.scopeChain[i] != b.scopeChain[i])
            return false;
    }
    if((a.lhs != nullptr) != (b.lhs != nullptr) || (a.rhs != nullptr) != (b.rhs != nullptr))
        return false;
    if(a.lhs && !SimdExpressionAnalysis::AreSamePureSimdExpr(*a.lhs, *b.lhs))
        return false;
    if(a.rhs && !SimdExpressionAnalysis::AreSamePureSimdExpr(*a.rhs, *b.rhs))
        return false;
    return a.arguments.empty() && b.arguments.empty();
}

[[nodiscard]] b8 SimdExpressionAnalysis::TryMatchConstLaneIndex(const Expr& indexExpr, u32& outLane)noexcept{
    if(indexExpr.kind != ExprKind::IntegerLiteral || indexExpr.text.empty())
        return false;
    const char first = indexExpr.text[0];
    if(first < '0' || first > '3')
        return false;
    for(usize i = 1u; i < indexExpr.text.size(); ++i){
        const char c = indexExpr.text[i];
        if(c != 'u' && c != 'U' && c != 'l' && c != 'L')
            return false;
    }
    outLane = static_cast<u32>(first - '0');
    return true;
}

[[nodiscard]] b8 SimdExpressionAnalysis::TryMatchSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept{
    if(!expr || expr->isMutArg || !expr->lhs)
        return false;
    u32 lane = 0u;
    if(expr->kind == ExprKind::MemberAccess && !expr->isSwizzle){
        if(!IsSingleSwizzleLane(expr->text, lane))
            return false;
    }
    else if(
        expr->kind == ExprKind::Index
        && !expr->isMatrixRowIndex
        && (expr->isVectorRawIndex || SymbolTable::IsSimdRegisterVectorType(expr->lhs->resolvedSimdType))
        && expr->rhs
    ){
        if(!SimdExpressionAnalysis::TryMatchConstLaneIndex(*expr->rhs, lane))
            return false;
    }
    else{
        return false;
    }
    const Expr* base = expr->lhs;
    while(base && base->kind == ExprKind::MemberAccess && base->isSwizzle && !base->isMutArg && base->lhs){
        if(lane >= base->text.size() || !TryMapSwizzleCharToLane(base->text[lane], lane))
            return false;
        base = base->lhs;
    }
    if(!base || !SimdExpressionAnalysis::IsPureSimdAddressExpr(*base))
        return false;
    outBase = base;
    outLane = lane;
    return true;
}

[[nodiscard]] b8 SimdExpressionAnalysis::TryMatchNegatedSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept{
    if(!expr || expr->kind != ExprKind::Unary || expr->isPostfix || expr->op != TokenType::Minus || !expr->lhs)
        return false;
    return SimdExpressionAnalysis::TryMatchSingleLaneAccess(expr->lhs, outBase, outLane);
}

[[nodiscard]] b8 SimdExpressionAnalysis::TryMatchCastSingleLaneAccess(const Expr* expr, const MStringView expectedScalarType, const SymbolTable& symbols, const Expr*& outBase, u32& outLane)noexcept{
    if(!expr || expr->kind != ExprKind::Cast || !expr->targetType || !expr->lhs)
        return false;
    if(expr->targetType->refMode != ParamPassingMode::None || !expr->targetType->templateArgs.empty())
        return false;
    if(symbols.resolveBaseTypeName(expr->targetType->name) != expectedScalarType)
        return false;
    return SimdExpressionAnalysis::TryMatchSingleLaneAccess(expr->lhs, outBase, outLane);
}

[[nodiscard]] MStringView SimdExpressionAnalysis::ResolveLocalVarDeclSimdType(const Stmt& stmt, const SymbolTable& symbols)noexcept{
    if(stmt.usesScratchArena || (stmt.varType && stmt.varType->refMode != ParamPassingMode::None))
        return {};
    if(stmt.varType){
        const MStringView resolved = symbols.resolveBaseTypeName(stmt.varType->name);
        if(IsRegisterPromotableSimdType(resolved))
            return resolved;
    }
    if(stmt.expr && IsRegisterPromotableSimdType(stmt.expr->resolvedSimdType))
        return stmt.expr->resolvedSimdType;
    return {};
}

[[nodiscard]] b8 SimdExpressionAnalysis::ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept{
    if(!expr)
        return false;

    if(expr->kind == ExprKind::MemberAccess && expr->lhs && (!expr->lhs->resolvedSimdType.empty() || !expr->lhs->resolvedUnalignedType.empty())){
        u32 lane = 0u;
        if(expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane))
            return true;
    }

    if(expr->kind == ExprKind::Index && expr->lhs && (!expr->lhs->resolvedSimdType.empty() || !expr->lhs->resolvedUnalignedType.empty()))
        return true;

    if(expr->kind == ExprKind::Unary && (!expr->resolvedSimdType.empty() || !expr->resolvedUnalignedType.empty()))
        return true;

    if(expr->kind == ExprKind::Binary && (!expr->resolvedSimdType.empty() || !expr->resolvedUnalignedType.empty())){
        if(expr->op != TokenType::Equal)
            return true;
        if(ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }

    if(expr->kind == ExprKind::Binary && OperatorSemantics::IsCompoundOrSimpleAssignment(expr->op) && expr->lhs){
        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs && (!expr->lhs->lhs->resolvedSimdType.empty() || !expr->lhs->lhs->resolvedUnalignedType.empty()) && IsSingleSwizzleLane(expr->lhs->text, lane))
            return true;
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs && (!expr->lhs->lhs->resolvedSimdType.empty() || !expr->lhs->lhs->resolvedUnalignedType.empty()))
            return true;
    }

    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr->lhs->text;
        const MStringView baseCallee = symbols.resolveBaseTypeName(callee);
        if((SymbolTable::IsSimdType(baseCallee) || SymbolTable::IsUnalignedVectorType(baseCallee)) && (!expr->resolvedSimdType.empty() || !expr->resolvedUnalignedType.empty()))
            return true;
        if(expr->isBuiltinCall){
            if(!expr->resolvedSimdType.empty() || !expr->resolvedUnalignedType.empty())
                return true;
            const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(callee);
            if(IsSimdScalarReductionFunction(callee) || !MapSimdRegisterCall(callee).empty() || (fnInfo && fnInfo->rejectsSimdStorage)){
                for(const Expr* arg : expr->arguments){
                    if(arg && (!arg->resolvedSimdType.empty() || !arg->resolvedUnalignedType.empty()))
                        return true;
                }
            }
        }
    }

    if(ExprContainsSimdMath(expr->lhs, symbols) || ExprContainsSimdMath(expr->rhs, symbols))
        return true;

    for(const Expr* arg : expr->arguments){
        if(ExprContainsSimdMath(arg, symbols))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

