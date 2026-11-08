// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/operator_semantics.h>

#include "simd_expression_analysis.h"
#include "simd_codegen_utils.h"


NWB_ADHD_BEGIN


[[nodiscard]] b8 SimdExpressionAnalysis::IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB, const SymbolTable* const symbols)noexcept{
    if(!expr || expr->kind != ExprKind::Binary || expr->op != TokenType::Star || !expr->lhs || !expr->rhs)
        return false;
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(expr->resolvedSimdType);
    if(!vecInfo || !vecInfo->isFloatReg)
        return false;
    const auto isUnitOrNegUnitOperand = [&](const Expr& op)noexcept -> b8{
        return SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(op, vecInfo->isDoubleReg, symbols)
            || SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(op, vecInfo->isDoubleReg, symbols)
            || SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(op, vecInfo->isDoubleReg, symbols);
    };
    if(isUnitOrNegUnitOperand(*expr->lhs) || isUnitOrNegUnitOperand(*expr->rhs))
        return false;
    outMulA = expr->lhs;
    outMulB = expr->rhs;
    return true;
}

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

[[nodiscard]] b8 SimdExpressionAnalysis::TryMatchSimdMaxComponentReduction(
    const Expr& expr,
    const Expr*& outBaseVec,
    b8& outIsDouble,
    u32& outLanes,
    b8& outIsAbs
)noexcept{
    if(
        expr.kind != ExprKind::Call
        || !expr.isBuiltinCall
        || !expr.lhs
        || expr.lhs->kind != ExprKind::Identifier
        || expr.lhs->text != "max"
        || expr.arguments.size() != 2u
        || !expr.arguments[0]
        || !expr.arguments[1]
        || !expr.resolvedSimdType.empty()
        || !expr.resolvedUnalignedType.empty()
    ){
        return false;
    }

    const Expr* firstBase = nullptr;
    b8 baseIsDouble = false;
    u32 baseZeroedAbove = 4u;
    b8 firstIsAbs = false;
    u8 laneMask = 0u;

    const auto visitNode = [&](auto& self, const Expr* node)noexcept -> b8{
        if(!node || !node->resolvedSimdType.empty() || !node->resolvedUnalignedType.empty())
            return false;
        if(
            node->kind == ExprKind::Call
            && node->isBuiltinCall
            && node->lhs
            && node->lhs->kind == ExprKind::Identifier
            && node->lhs->text == "max"
            && node->arguments.size() == 2u
            && node->arguments[0]
            && node->arguments[1]
        ){
            return self(self, node->arguments[0]) && self(self, node->arguments[1]);
        }

        const Expr* leafBase = nullptr;
        u32 leafLane = 0u;
        b8 leafIsAbs = false;
        if(
            node->kind == ExprKind::Call
            && node->isBuiltinCall
            && node->lhs
            && node->lhs->kind == ExprKind::Identifier
            && node->lhs->text == "abs"
            && node->arguments.size() == 1u
            && node->arguments[0]
        ){
            if(!SimdExpressionAnalysis::TryMatchSingleLaneAccess(node->arguments[0], leafBase, leafLane) || !leafBase)
                return false;
            leafIsAbs = true;
        }
        else if(!SimdExpressionAnalysis::TryMatchSingleLaneAccess(node, leafBase, leafLane) || !leafBase){
            return false;
        }

        if(!firstBase){
            MStringView regType;
            MStringView loadFn;
            if(!SimdVectorSourceAnalysis::TryGetAnyFloatVectorSourceInfo(*leafBase, baseIsDouble, regType, loadFn, baseZeroedAbove))
                return false;
            firstBase = leafBase;
            firstIsAbs = leafIsAbs;
        }
        else if(leafIsAbs != firstIsAbs || !SimdExpressionAnalysis::AreSamePureSimdExpr(*firstBase, *leafBase)){
            return false;
        }

        if(leafLane >= baseZeroedAbove || leafLane >= 4u)
            return false;
        laneMask |= static_cast<u8>(1u << leafLane);
        return true;
    };

    if(!visitNode(visitNode, &expr) || !firstBase || (laneMask != 0x7u && laneMask != 0xFu))
        return false;

    outBaseVec = firstBase;
    outIsDouble = baseIsDouble;
    outLanes = (laneMask == 0x7u) ? 3u : 4u;
    outIsAbs = firstIsAbs;
    return true;
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

    if(expr->kind == ExprKind::MemberAccess && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
        u32 lane = 0u;
        if(expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane))
            return true;
    }

    if(expr->kind == ExprKind::Index && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Unary && IsRegisterPromotableSimdType(expr->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Binary && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        if(expr->op != TokenType::Equal)
            return true;
        if(ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }

    if(expr->kind == ExprKind::Binary && OperatorSemantics::IsCompoundOrSimpleAssignment(expr->op) && expr->lhs){
        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType) && IsSingleSwizzleLane(expr->lhs->text, lane))
            return true;
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType))
            return true;
    }

    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr->lhs->text;
        const MStringView baseCallee = symbols.resolveBaseTypeName(callee);
        if(SymbolTable::IsSimdType(baseCallee) && IsRegisterPromotableSimdType(expr->resolvedSimdType))
            return true;
        if(expr->isBuiltinCall){
            if(IsRegisterPromotableSimdType(expr->resolvedSimdType))
                return true;
            if(IsSimdScalarReductionFunction(callee) || !MapSimdRegisterCall(callee).empty() || callee == "isFinite"){
                for(const Expr* arg : expr->arguments){
                    if(arg && IsRegisterPromotableSimdType(arg->resolvedSimdType))
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


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

