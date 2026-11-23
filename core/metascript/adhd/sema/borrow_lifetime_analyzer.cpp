// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/operator_semantics.h>

#include "borrow_lifetime_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BorrowLifetimeAnalyzer::isTemporaryOwnerExpr(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope, const b8 requireObject)const noexcept{
    const TypeRef* exprTypeRef = nullptr;
    const MStringView baseType = m_checker.inferExprBaseType(ownerClass, method, expr, scope, &exprTypeRef);
    if(m_checker.m_symbols.resolveRefMode(exprTypeRef) != ParamPassingMode::None){
        if(requireObject && expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const TypeRef* receiverType = nullptr;
            const MStringView receiver = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope, &receiverType);
            const b8 unwrapsBorrow = m_checker.wrappedReferenceType(receiverType) && FindSpecialMethodSemantics(expr.lhs->text) == SpecialMethodKind::UnwrapValue;
            if(!unwrapsBorrow && !IsBorrowViewType(receiver))
                return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs->lhs, scope, true);
        }
        return false;
    }
    if(!requireObject && IsBorrowViewType(baseType))
        return false;

    switch(expr.kind){
    case ExprKind::Identifier:
    case ExprKind::ThisMember:
        return false;
    case ExprKind::MemberAccess:
        return expr.lhs != nullptr && isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope, requireObject);
    case ExprKind::Index:{
        if(!expr.lhs)
            return false;
        const TypeRef* containerTypeRef = nullptr;
        const MStringView lhsBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs, scope, &containerTypeRef);
        if(IsBorrowViewType(lhsBase))
            return false;
        const b8 recvIsConst = method ? m_checker.isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
        if(const MethodDecl* customOp = m_checker.resolveCustomOperator(ownerClass, method, containerTypeRef, lhsBase, OperatorSemantics::s_SubscriptSymbol, 1u, recvIsConst))
            return m_checker.m_symbols.resolveRefMode(customOp->returnType) == ParamPassingMode::None;
        return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text); fnInfo && fnInfo->returnsReference)
                return false;
        }
        if(expr.lhs && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            const MStringView recvBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope);
            if(IsContainerOrWrapperValueAccessMethod(recvBase, mName, true)){
                return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs->lhs, scope);
            }
            if(!m_checker.m_symbols.hasTypeSymbol(recvBase)){
                if(const BuiltinReceiverMethodInfo* recvMethod = FindBuiltinReceiverMethod(recvBase, mName)){
                    if(recvMethod->returnsReference)
                        return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs->lhs, scope);
                    if(recvMethod->synthesizeTypeRef && recvMethod->returnTypeName.empty())
                        return false;
                }
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

const ScopedVarInfo* BorrowLifetimeAnalyzer::localReferenceOwner(const ClassDecl* ownerClass, const MethodDecl& method, const Expr& expr, const Span<const ScopedVarInfo> scope, const b8 takingAddress)const noexcept{
    const b8 needsStorage = takingAddress || expr.wrapInNotNull;
    if(expr.kind == ExprKind::Identifier){
        const ScopedVarInfo* local = TypeChecker::FindScopedVar(scope, expr.text);
        if(!local)
            return nullptr;
        if(!local->referenceOwnerName.empty())
            return TypeChecker::FindScopedVar(scope, local->referenceOwnerName);
        if(!needsStorage)
            return nullptr;
        if(local->isParam)
            return local->mode == ParamPassingMode::In && !local->retainsBorrow ? local : nullptr;
        return m_checker.m_symbols.resolveRefMode(local->type) == ParamPassingMode::None && local->mode == ParamPassingMode::None ? local : nullptr;
    }
    if((expr.kind == ExprKind::MemberAccess || expr.kind == ExprKind::Index) && expr.lhs){
        if(expr.kind == ExprKind::Index){
            const MStringView container = m_checker.inferExprBaseType(ownerClass, &method, *expr.lhs, scope);
            if(IsBorrowViewType(container))
                return localReferenceOwner(ownerClass, method, *expr.lhs, scope);
        }
        return localReferenceOwner(ownerClass, method, *expr.lhs, scope, needsStorage);
    }
    if(expr.kind != ExprKind::Call)
        return nullptr;
    const TypeRef* const result = m_checker.inferExprTypeRef(ownerClass, &method, expr, scope);
    const b8 borrowedResult = m_checker.wrappedReferenceType(result) != nullptr || m_checker.m_symbols.resolveRefMode(result) != ParamPassingMode::None;
    const b8 createsView = expr.lhs && expr.lhs->kind == ExprKind::Identifier && IsBorrowViewType(m_checker.m_symbols.resolveBaseTypeName(expr.lhs->text));
    if(expr.lhs && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs && (needsStorage || borrowedResult)){
        const Expr& receiver = *expr.lhs->lhs;
        const TypeRef* const receiverType = m_checker.inferExprTypeRef(ownerClass, &method, receiver, scope);
        const b8 readsStoredBorrow = m_checker.wrappedReferenceType(receiverType) != nullptr && FindSpecialMethodSemantics(expr.lhs->text) == SpecialMethodKind::UnwrapValue;
        if(const auto* owner = localReferenceOwner(ownerClass, method, receiver, scope, !readsStoredBorrow))
            return owner;
    }
    for(const Expr* arg : expr.arguments){
        if(!arg)
            continue;
        if(const auto* owner = localReferenceOwner(ownerClass, method, *arg, scope, createsView || (borrowedResult && (arg->isRetainedArg || arg->isMutArg))))
            return owner;
    }
    return nullptr;
}

b8 BorrowLifetimeAnalyzer::borrowsTemporaryStringOwner(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    const MStringView baseType = m_checker.inferExprBaseType(ownerClass, method, expr, scope);
    if(IsOwningStringType(baseType))
        return isTemporaryOwnerExpr(ownerClass, method, expr, scope);
    if(!IsStringBorrowViewType(baseType))
        return false;

    if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_checker.m_symbols.resolveBaseTypeName(expr.lhs->text);
            if((IsBuiltinStringViewBorrowFunction(expr.lhs->text) || baseCallee == BuiltinTypeSemantics::StringViewTypeName()) && !expr.arguments.empty() && expr.arguments[0])
                return borrowsTemporaryStringOwner(ownerClass, method, *expr.arguments[0], scope);
        }
        else if(expr.lhs->kind == ExprKind::ScopeAccess && expr.lhs->scopeChain.size() == 2u){
            if(const BuiltinFunctionInfo* scopedFn = FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1]); scopedFn && scopedFn->returnsBorrowedView && !expr.arguments.empty() && expr.arguments[0])
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
        const ScopedVarInfo* local = TypeChecker::FindScopedVar(scope, root->text, &localIdx);
        if(!local)
            return false;
        if(local->borrowsLocalStringOwner){
            usize ownerIdx = localIdx;
            if(!local->borrowedLocalOwnerName.empty() && !TypeChecker::FindScopedVar(scope, local->borrowedLocalOwnerName, &ownerIdx))
                ownerIdx = localIdx;
            if(outOwnerName)
                *outOwnerName = local->borrowedLocalOwnerName;
            if(outOwnerScopeIndex)
                *outOwnerScopeIndex = ownerIdx;
            return true;
        }
        const MStringView candidateBase = m_checker.inferExprBaseType(ownerClass, method, candidate, scope);
        if(IsOwningStringType(candidateBase) && m_checker.isOwnedStackLocalVar(*local)){
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
    if(!IsStringBorrowViewType(baseType))
        return false;

    if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_checker.m_symbols.resolveBaseTypeName(expr.lhs->text);
            if((IsBuiltinStringViewBorrowFunction(expr.lhs->text) || baseCallee == BuiltinTypeSemantics::StringViewTypeName()) && !expr.arguments.empty() && expr.arguments[0])
                return borrowsLocalStringOwner(ownerClass, method, *expr.arguments[0], scope, outOwnerName, outOwnerScopeIndex);
        }
        else if(expr.lhs->kind == ExprKind::ScopeAccess && expr.lhs->scopeChain.size() == 2u){
            if(const BuiltinFunctionInfo* scopedFn = FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1]); scopedFn && scopedFn->returnsBorrowedView && !expr.arguments.empty() && expr.arguments[0])
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

