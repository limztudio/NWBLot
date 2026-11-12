// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lambda_capture_analyzer.h"
#include "type_resolver.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LambdaCaptureAnalyzer::propagateFuncCaptureBytes(TypeRef* targetType, const Expr* rhs)const noexcept{
    TypeRef* funcType = TypeResolver(m_symbols).extractUnderlyingFuncType(targetType);
    if(!funcType || !rhs)
        return;
    if(rhs->kind == ExprKind::Lambda){
        if(!rhs->lambdaCaptures.empty())
            funcType->inferredCaptureBytes = s_InferredLambdaCaptureBytes;
        const_cast<Expr*>(rhs)->wrapInFuncType = funcType;
        return;
    }
    if(rhs->kind == ExprKind::Identifier){
        if(const ScopedVarInfo* v = TypeChecker::FindScopedVar(m_scope, rhs->text)){
            const TypeRef* rhsFunc = TypeResolver(m_symbols).extractUnderlyingFuncType(v->type);
            if((v->lambdaExpr && !v->lambdaExpr->lambdaCaptures.empty()) || (rhsFunc && rhsFunc->inferredCaptureBytes > 0u))
                funcType->inferredCaptureBytes = s_InferredLambdaCaptureBytes;
        }
        return;
    }
    if(rhs->kind == ExprKind::Call){
        for(const Expr* arg : rhs->arguments){
            if(arg)
                propagateFuncCaptureBytes(targetType, arg);
        }
    }
}

b8 LambdaCaptureAnalyzer::isEscapingMutCaptureLambda(const Expr* rhs)const noexcept{
    if(!rhs)
        return false;
    if(rhs->kind == ExprKind::Lambda)
        return rhs->hasMutLocalCapture;
    if(rhs->kind == ExprKind::Identifier){
        if(const ScopedVarInfo* v = TypeChecker::FindScopedVar(m_scope, rhs->text))
            return v->lambdaExpr && v->lambdaExpr->hasMutLocalCapture;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

