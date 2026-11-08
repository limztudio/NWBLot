// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "return_statement_checker.h"
#include "borrow_lifetime_analyzer.h"
#include "lambda_capture_analyzer.h"


NWB_ADHD_BEGIN


b8 ReturnStatementChecker::check(ClassDecl* ownerClass, const Span<const ScopedVarInfo> scope, MethodDecl& method, Stmt& stmt, MVector<ParseError>& errors){
    b8 ok = true;
    if(m_checker.m_parallelDepth > 0u && !m_checker.m_inLambda){
        m_checker.reportError(errors, stmt.span, "'return' cannot be used inside a 'parallel' or 'parallel_for' construct");
        ok = false;
    }
    const TypeRef* activeRetType = m_checker.m_inLambda ? m_checker.m_activeLambdaReturnType : method.returnType;
    const TypeRef* unwrappedRetType = m_checker.m_symbols.unwrapTypeAlias(activeRetType);
    const ParamPassingMode::Enum retRefMode = m_checker.m_symbols.resolveRefMode(activeRetType);
    const b8 isVoidLike = (!m_checker.m_inLambda && (method.isConstructor || method.isDestructor)) || IsVoidTypeRef(unwrappedRetType);
    if(isVoidLike && stmt.expr){
        m_checker.reportError(errors, stmt.span, "cannot return a value from a 'void' function, constructor, or destructor");
        ok = false;
    }
    else if(!isVoidLike && !stmt.expr){
        m_checker.reportError(errors, stmt.span, "non-void function must return a value");
        ok = false;
    }
    if(activeRetType && stmt.expr){
        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(const_cast<TypeRef*>(activeRetType), stmt.expr);
        const MStringView baseRetTypeName = m_checker.m_symbols.resolveBaseTypeName(activeRetType->name);
        if(RequiresTCharAdaptation(baseRetTypeName))
            m_checker.markStringToPathAdaptation(ownerClass, &method, scope, *stmt.expr);
        if(retRefMode != ParamPassingMode::None){
            stmt.wrapReturnNotNull = true;
            b8 isDanglingReturn = false;
            if(stmt.expr->kind == ExprKind::Identifier){
                const ScopedVarInfo* local = m_checker.FindScopedVar(scope, stmt.expr->text);
                if(local && !local->isParam && m_checker.m_symbols.resolveRefMode(local->type) == ParamPassingMode::None && local->mode == ParamPassingMode::None)
                    isDanglingReturn = true;
            }
            else if(m_checker.isRvalueTemporaryExpr(ownerClass, &method, *stmt.expr, scope)){
                isDanglingReturn = true;
            }
            else if(stmt.expr->kind == ExprKind::MemberAccess || stmt.expr->kind == ExprKind::Index){
                const TypeRef* subTypeRef = m_checker.inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
                if(m_checker.m_symbols.resolveRefMode(subTypeRef) == ParamPassingMode::None){
                    if(BorrowLifetimeAnalyzer(m_checker).isTemporaryOwnerExpr(ownerClass, &method, *stmt.expr, scope)){
                        isDanglingReturn = true;
                    }
                    const Expr* root = stmt.expr->lhs;
                    while(root && (root->kind == ExprKind::MemberAccess || root->kind == ExprKind::Index)){
                        const TypeRef* midType = m_checker.inferExprTypeRef(ownerClass, &method, *root, scope);
                        if(m_checker.m_symbols.resolveRefMode(midType) != ParamPassingMode::None || (midType && IsBorrowViewType(m_checker.m_symbols.resolveBaseTypeName(midType->name)))){
                            root = nullptr;
                            break;
                        }
                        root = root->lhs;
                    }
                    if(root && root->kind == ExprKind::Identifier){
                        if(const ScopedVarInfo* rootLocal = m_checker.FindScopedVar(scope, root->text)){
                            if(
                                !rootLocal->isParam
                                && rootLocal->mode == ParamPassingMode::None
                                && m_checker.m_symbols.resolveRefMode(rootLocal->type) == ParamPassingMode::None
                                && !IsBorrowViewType(m_checker.resolveScopedVarBaseType(*rootLocal))
                            ){
                                isDanglingReturn = true;
                            }
                        }
                    }
                }
            }
            if(isDanglingReturn){
                m_checker.reportError(errors, stmt.span, "cannot return a reference to a temporary or stack-local variable");
                ok = false;
            }
            if(
                m_checker.m_symbols.findInterface(baseRetTypeName)
                && !m_checker.verifyConcreteClassImplementsInterface(ownerClass, method, *stmt.expr, activeRetType, scope, stmt.span, "cannot return concrete class for interface reference return type: class does not implement the interface", errors)
            )
                ok = false;
        }
    }
    if(stmt.expr && !m_checker.checkExpr(ownerClass, scope, retRefMode == ParamPassingMode::Mut, method, *stmt.expr, errors))
        ok = false;
    if(unwrappedRetType && stmt.expr && retRefMode == ParamPassingMode::None && !unwrappedRetType->templateArgs.empty() && unwrappedRetType->templateArgs[0].type){
        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(unwrappedRetType->name); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional)){
            const MStringView valBase = m_checker.m_symbols.resolveBaseTypeName(unwrappedRetType->templateArgs[0].type->name);
            if(SymbolTable::IsSimdRegisterVectorType(valBase) && stmt.expr->resolvedSimdType == valBase)
                stmt.expr->wrapInSimdResult = true;
        }
    }
    if(stmt.expr && retRefMode == ParamPassingMode::In && stmt.expr->isStringByteRead){
        stmt.expr->isStringByteRead = false;
        stmt.expr->isStringByteConstRef = true;
    }
    if(activeRetType && stmt.expr && retRefMode == ParamPassingMode::None){
        const MStringView baseRetTypeName = m_checker.m_symbols.resolveBaseTypeName(activeRetType->name);
        if(baseRetTypeName == "StringView"){
            if(BorrowLifetimeAnalyzer(m_checker).borrowsTemporaryStringOwner(ownerClass, &method, *stmt.expr, scope)
                || BorrowLifetimeAnalyzer(m_checker).borrowsLocalStringOwner(ownerClass, &method, *stmt.expr, scope)){
                m_checker.reportError(errors, stmt.span, "cannot return 'StringView' borrowing a temporary or stack-local string");
                ok = false;
            }
        }
        else if(!IsOwningStringType(baseRetTypeName) && stmt.expr->kind == ExprKind::Identifier){
            if(const ScopedVarInfo* retLocal = m_checker.FindScopedVar(scope, stmt.expr->text); retLocal && retLocal->borrowsLocalStringOwner){
                m_checker.reportError(errors, stmt.span, "cannot return value containing 'StringView' borrowing a stack-local string");
                ok = false;
            }
        }
    }
    if(stmt.expr && LambdaCaptureAnalyzer(scope, m_checker.m_symbols).isEscapingMutCaptureLambda(stmt.expr)){
        m_checker.reportError(errors, stmt.span, "cannot return lambda with 'mut' capture; mutable reference captures cannot escape their enclosing scope");
        ok = false;
    }
    return ok;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
