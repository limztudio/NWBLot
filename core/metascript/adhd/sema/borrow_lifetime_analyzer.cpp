// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/operator_semantics.h>

#include "borrow_lifetime_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BorrowLifetimeAnalyzer::isTemporaryOwnerExpr(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    const TypeRef* exprTypeRef = nullptr;
    const MStringView baseType = m_checker.inferExprBaseType(ownerClass, method, expr, scope, &exprTypeRef);
    if(m_checker.m_symbols.resolveRefMode(exprTypeRef) != ParamPassingMode::None)
        return false;
    if(IsBorrowViewType(baseType))
        return false;

    switch(expr.kind){
    case ExprKind::Identifier:
    case ExprKind::ThisMember:
        return false;
    case ExprKind::MemberAccess:
        return expr.lhs != nullptr && isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
    case ExprKind::Index:{
        if(!expr.lhs)
            return false;
        const TypeRef* containerTypeRef = nullptr;
        const MStringView lhsBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs, scope, &containerTypeRef);
        if(IsBorrowViewType(lhsBase))
            return false;
        const b8 recvIsConst = method ? m_checker.isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
        if(const MethodDecl* customOp = m_checker.resolveCustomOperator(ownerClass, method, containerTypeRef, lhsBase, "[]", 1u, recvIsConst))
            return m_checker.m_symbols.resolveRefMode(customOp->returnType) == ParamPassingMode::None;
        return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            const MStringView recvBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope);
            if(IsContainerOrWrapperValueAccessMethod(recvBase, mName, true)){
                return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs->lhs, scope);
            }
            if(IsBorrowViewType(recvBase)){
                const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(mName);
                if((memberInfo && !memberInfo->isMutating && memberInfo->returnRule == BuiltinMemberReturnRule::FirstTypeArg) || IsStringByteAccessorMethod(recvBase, mName))
                    return false;
            }
        }
        return true;
    }
    case ExprKind::Binary:
        if(OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op))
            return expr.lhs != nullptr && isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
        return true;
    case ExprKind::Unary:
        if(!expr.isPostfix && OperatorSemantics::IsIncrementOrDecrementOperatorToken(expr.op))
            return expr.lhs != nullptr && isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
        return true;
    default:
        return true;
    }
}

b8 BorrowLifetimeAnalyzer::borrowsTemporaryStringOwner(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    const MStringView baseType = m_checker.inferExprBaseType(ownerClass, method, expr, scope);
    if(IsOwningStringType(baseType))
        return isTemporaryOwnerExpr(ownerClass, method, expr, scope);
    if(baseType != "StringView")
        return false;

    if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_checker.m_symbols.resolveBaseTypeName(expr.lhs->text);
            if((IsBuiltinStringViewBorrowFunction(expr.lhs->text) || baseCallee == "StringView") && !expr.arguments.empty() && expr.arguments[0])
                return borrowsTemporaryStringOwner(ownerClass, method, *expr.arguments[0], scope);
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            const MStringView recvBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope);
            if(IsStringReceiverBorrowViewMethod(recvBase, mName)){
                if(borrowsTemporaryStringOwner(ownerClass, method, *expr.lhs->lhs, scope))
                    return true;
            }
        }
        for(const Expr* arg : expr.arguments){
            if(!arg)
                continue;
            const MStringView argBase = m_checker.inferExprBaseType(ownerClass, method, *arg, scope);
            if(IsStringLikeType(argBase) && borrowsTemporaryStringOwner(ownerClass, method, *arg, scope))
                return true;
        }
        return false;
    }
    if(expr.kind == ExprKind::Binary && OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op) && expr.rhs)
        return borrowsTemporaryStringOwner(ownerClass, method, *expr.rhs, scope);
    if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.lhs)
        return borrowsTemporaryStringOwner(ownerClass, method, *expr.lhs, scope);
    return false;
}

b8 BorrowLifetimeAnalyzer::borrowsLocalStringOwner(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope, MStringView* outOwnerName, usize* outOwnerScopeIndex)const noexcept{
    const auto resolveLocalIndex = [&](const MStringView name, usize& outIndex)noexcept -> const ScopedVarInfo*{
        for(usize i = scope.size(); i > 0u; --i){
            if(scope[i - 1u].name == name){
                outIndex = i - 1u;
                return &scope[i - 1u];
            }
        }
        return nullptr;
    };

    const auto checkRootLocalOwner = [&](const Expr& candidate)noexcept -> b8{
        const Expr* root = &candidate;
        while(root){
            if(root->kind == ExprKind::MemberAccess && root->lhs){
                const TypeRef* midType = m_checker.inferExprTypeRef(ownerClass, method, *root, scope);
                if(m_checker.m_symbols.resolveRefMode(midType) != ParamPassingMode::None)
                    return false;
                root = root->lhs;
                continue;
            }
            if(root->kind == ExprKind::Index && root->lhs){
                const TypeRef* midType = m_checker.inferExprTypeRef(ownerClass, method, *root, scope);
                if(m_checker.m_symbols.resolveRefMode(midType) != ParamPassingMode::None)
                    return false;
                const MStringView parentBase = m_checker.inferExprBaseType(ownerClass, method, *root->lhs, scope);
                if(IsBorrowViewType(parentBase))
                    return false;
                root = root->lhs;
                continue;
            }
            if(root->kind == ExprKind::Call && root->lhs && root->lhs->kind == ExprKind::MemberAccess && root->lhs->lhs){
                const MStringView mName = root->lhs->text;
                const MStringView recvBase = m_checker.inferExprBaseType(ownerClass, method, *root->lhs->lhs, scope);
                if(IsContainerOrWrapperValueAccessMethod(recvBase, mName, false)){
                    root = root->lhs->lhs;
                    continue;
                }
            }
            break;
        }
        if(!root || root->kind != ExprKind::Identifier)
            return false;
        usize localIdx = 0u;
        const ScopedVarInfo* local = resolveLocalIndex(root->text, localIdx);
        if(!local)
            return false;
        if(local->borrowsLocalStringOwner){
            usize ownerIdx = localIdx;
            if(!local->borrowedLocalOwnerName.empty())
                resolveLocalIndex(local->borrowedLocalOwnerName, ownerIdx);
            if(outOwnerName)
                *outOwnerName = local->borrowedLocalOwnerName;
            if(outOwnerScopeIndex)
                *outOwnerScopeIndex = ownerIdx;
            return true;
        }
        const MStringView candidateBase = m_checker.inferExprBaseType(ownerClass, method, candidate, scope);
        if(IsOwningStringType(candidateBase)
            && !local->isParam
            && local->mode == ParamPassingMode::None
            && m_checker.m_symbols.resolveRefMode(local->type) == ParamPassingMode::None
            && !IsBorrowViewType(m_checker.resolveScopedVarBaseType(*local))){
            if(outOwnerName)
                *outOwnerName = local->name;
            if(outOwnerScopeIndex)
                *outOwnerScopeIndex = localIdx;
            return true;
        }
        return false;
    };

    const MStringView baseType = m_checker.inferExprBaseType(ownerClass, method, expr, scope);
    if(IsOwningStringType(baseType))
        return checkRootLocalOwner(expr);
    if(checkRootLocalOwner(expr))
        return true;
    if(baseType != "StringView")
        return false;

    if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_checker.m_symbols.resolveBaseTypeName(expr.lhs->text);
            if((IsBuiltinStringViewBorrowFunction(expr.lhs->text) || baseCallee == "StringView") && !expr.arguments.empty() && expr.arguments[0])
                return borrowsLocalStringOwner(ownerClass, method, *expr.arguments[0], scope, outOwnerName, outOwnerScopeIndex);
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            const MStringView recvBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope);
            if(IsStringReceiverBorrowViewMethod(recvBase, mName)){
                if(borrowsLocalStringOwner(ownerClass, method, *expr.lhs->lhs, scope, outOwnerName, outOwnerScopeIndex))
                    return true;
            }
        }
        for(const Expr* arg : expr.arguments){
            if(!arg)
                continue;
            const MStringView argBase = m_checker.inferExprBaseType(ownerClass, method, *arg, scope);
            if(IsStringLikeType(argBase)
                && borrowsLocalStringOwner(ownerClass, method, *arg, scope, outOwnerName, outOwnerScopeIndex)){
                return true;
            }
        }
        return false;
    }
    if(expr.kind == ExprKind::Binary && OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op) && expr.rhs)
        return borrowsLocalStringOwner(ownerClass, method, *expr.rhs, scope, outOwnerName, outOwnerScopeIndex);
    if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.lhs)
        return borrowsLocalStringOwner(ownerClass, method, *expr.lhs, scope, outOwnerName, outOwnerScopeIndex);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

