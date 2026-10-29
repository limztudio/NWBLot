// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TypeChecker::propagateFuncCaptureBytes(
    TypeRef* targetType,
    const Expr* rhs,
    const MVector<ScopedVarInfo>& scope,
    const SymbolTable& symbols
)noexcept{
    TypeRef* funcType = ExtractUnderlyingFuncType(targetType, symbols);
    if(!funcType || !rhs)
        return;
    if(rhs->kind == ExprKind::Lambda){
        if(!rhs->lambdaCaptures.empty())
            funcType->inferredCaptureBytes = 64u;
        return;
    }
    if(rhs->kind == ExprKind::Identifier){
        for(usize i = scope.size(); i > 0u; --i){
            const ScopedVarInfo& v = scope[i - 1u];
            if(v.name == rhs->text){
                const TypeRef* rhsFunc = ExtractUnderlyingFuncType(v.type, symbols);
                if((v.lambdaExpr && !v.lambdaExpr->lambdaCaptures.empty()) || (rhsFunc && rhsFunc->inferredCaptureBytes > 0u))
                    funcType->inferredCaptureBytes = 64u;
                break;
            }
        }
    }
}

b8 TypeChecker::isEscapingMutCaptureLambda(const Expr* rhs, const MVector<ScopedVarInfo>& scope)noexcept{
    if(!rhs)
        return false;
    if(rhs->kind == ExprKind::Lambda)
        return rhs->hasMutLocalCapture;
    if(rhs->kind == ExprKind::Identifier){
        for(usize i = scope.size(); i > 0u; --i){
            const ScopedVarInfo& v = scope[i - 1u];
            if(v.name == rhs->text)
                return v.lambdaExpr && v.lambdaExpr->hasMutLocalCapture;
        }
    }
    return false;
}

b8 TypeChecker::checkCallableArguments(
    ClassDecl* ownerClass,
    MethodDecl& method,
    MVector<Expr*>& arguments,
    const MVector<ScopedVarInfo>& scope,
    const MethodDecl* targetCallable,
    const ClassDecl* targetOwnerCls,
    const TypeRef* recvTypeRef,
    const MVector<TypeArg>* callTemplateArgs,
    const TypeRef* invokedFuncType,
    const ScopedVarInfo* localCallable,
    const MStringView calledMemberName,
    MVector<ParseError>& errors
){
    b8 ok = true;
    const SourceSpan errSpan = !arguments.empty() && arguments[0] ? arguments[0]->span : method.span;
    if(targetCallable && targetCallable->params.size() != arguments.size()){
        reportError(errors, errSpan, "wrong number of arguments in call");
        ok = false;
    }
    else if(invokedFuncType && invokedFuncType->isFuncType && invokedFuncType->funcParams.size() != arguments.size()){
        reportError(errors, errSpan, "wrong number of arguments in Func call");
        ok = false;
    }
    else if(localCallable && localCallable->lambdaExpr && localCallable->lambdaExpr->lambdaParams.size() != arguments.size()){
        reportError(errors, errSpan, "wrong number of arguments in lambda call");
        ok = false;
    }
    for(usize i = 0u; i < arguments.size(); ++i){
        Expr* arg = arguments[i];
        if(!arg)
            continue;
        b8 paramIsMut = false;
        TypeRef* expectedParamTypeRef = nullptr;
        MStringView expectedParamType;
        if(targetCallable && i < targetCallable->params.size()){
            paramIsMut = (targetCallable->params[i].mode == ParamPassingMode::Mut);
            if(targetCallable->params[i].type){
                TypeRef* subParamType = targetCallable->params[i].type;
                if(targetOwnerCls && recvTypeRef && !targetOwnerCls->templateParams.empty() && !recvTypeRef->templateArgs.empty())
                    subParamType = m_symbols.substituteTypeParams(*subParamType, targetOwnerCls->templateParams, recvTypeRef->templateArgs);
                if(callTemplateArgs && !targetCallable->templateParams.empty() && !callTemplateArgs->empty())
                    subParamType = m_symbols.substituteTypeParams(*subParamType, targetCallable->templateParams, *callTemplateArgs);
                expectedParamTypeRef = subParamType;
                expectedParamType = m_symbols.resolveBaseTypeName(subParamType->name);
                propagateFuncCaptureBytes(targetCallable->params[i].type, arg, scope, m_symbols);
            }
        }
        else if(invokedFuncType && invokedFuncType->isFuncType && i < invokedFuncType->funcParams.size()){
            paramIsMut = (invokedFuncType->funcParams[i].mode == ParamPassingMode::Mut);
            if(invokedFuncType->funcParams[i].type){
                expectedParamTypeRef = invokedFuncType->funcParams[i].type;
                expectedParamType = m_symbols.resolveBaseTypeName(invokedFuncType->funcParams[i].type->name);
                propagateFuncCaptureBytes(invokedFuncType->funcParams[i].type, arg, scope, m_symbols);
            }
        }
        else if(localCallable && localCallable->lambdaExpr && i < localCallable->lambdaExpr->lambdaParams.size()){
            paramIsMut = (localCallable->lambdaExpr->lambdaParams[i].mode == ParamPassingMode::Mut);
            if(localCallable->lambdaExpr->lambdaParams[i].type){
                expectedParamTypeRef = localCallable->lambdaExpr->lambdaParams[i].type;
                expectedParamType = m_symbols.resolveBaseTypeName(localCallable->lambdaExpr->lambdaParams[i].type->name);
                propagateFuncCaptureBytes(localCallable->lambdaExpr->lambdaParams[i].type, arg, scope, m_symbols);
            }
        }
        else if(i == 0u && recvTypeRef && !recvTypeRef->templateArgs.empty() && recvTypeRef->templateArgs[0].type){
            b8 adaptsFirstArg = false;
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, adaptsFirstArgToElement, returnRule) \
            if constexpr(adaptsFirstArgToElement){ \
                if(calledMemberName == methodName) adaptsFirstArg = true; \
            }
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
            if(adaptsFirstArg){
                expectedParamTypeRef = recvTypeRef->templateArgs[0].type;
                expectedParamType = m_symbols.resolveBaseTypeName(expectedParamTypeRef->name);
            }
        }
        arg->isMutArg = paramIsMut;
        if(expectedParamType == "Path")
            markStringToPathAdaptation(ownerClass, method, *arg, scope);
        if(!checkExpr(ownerClass, method, *arg, scope, paramIsMut, errors))
            ok = false;
        if(!expectedParamType.empty() && m_symbols.findInterface(expectedParamType)){
            if(!verifyConcreteClassImplementsInterface(
                ownerClass,
                method,
                *arg,
                expectedParamTypeRef,
                scope,
                arg->span,
                "cannot pass concrete class to interface parameter: class does not implement the interface",
                errors
            )){
                ok = false;
            }
        }
        if(targetCallable && expectedParamTypeRef && (!callTemplateArgs || callTemplateArgs->empty()) && !targetCallable->templateParams.empty()){
            for(const TemplateParamDecl& tp : targetCallable->templateParams){
                if(tp.name != expectedParamTypeRef->name || tp.interfaceBounds.empty())
                    continue;
                for(const TypeRef* boundRef : tp.interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(!verifyConcreteClassImplementsInterface(
                        ownerClass,
                        method,
                        *arg,
                        boundRef,
                        scope,
                        arg->span,
                        "template type argument does not implement required interface bound",
                        errors
                    )){
                        ok = false;
                    }
                }
            }
        }
        if(targetOwnerCls && expectedParamTypeRef && (!recvTypeRef || recvTypeRef->templateArgs.empty()) && !targetOwnerCls->templateParams.empty()){
            for(const TemplateParamDecl& tp : targetOwnerCls->templateParams){
                if(tp.name != expectedParamTypeRef->name || tp.interfaceBounds.empty())
                    continue;
                for(const TypeRef* boundRef : tp.interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(!verifyConcreteClassImplementsInterface(
                        ownerClass,
                        method,
                        *arg,
                        boundRef,
                        scope,
                        arg->span,
                        "template type argument does not implement required interface bound",
                        errors
                    )){
                        ok = false;
                    }
                }
            }
        }
    }
    return ok;
}

b8 TypeChecker::checkCallExpr(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Expr& expr,
    const MVector<ScopedVarInfo>& scope,
    const b8 isMutationTarget,
    MVector<ParseError>& errors
){
    b8 ok = true;

    if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, false, errors))
        ok = false;

    const MethodDecl* targetCallable = nullptr;
    const ClassDecl* targetOwnerCls = nullptr;
    const TypeRef* recvTypeRef = nullptr;
    MStringView calledMemberName;
    const ScopedVarInfo* localCallable = nullptr;
    const TypeRef* invokedFuncType = nullptr;
    const usize callArgCount = expr.arguments.size();
    MVector<MStringView> argTypeHints(m_arena);
    MVector<b8> argIsMutHints(m_arena);
    buildCallArgHints(ownerClass, &method, expr.arguments, scope, argTypeHints, argIsMutHints);
    if(expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(m_symbols.findInterface(baseCallee)){
                reportError(errors, expr.span, "cannot instantiate an 'interface' type directly");
                ok = false;
            }
            if(SymbolTable::isArenaBackedContainer(baseCallee)){
                if(m_inClassArenaContext && ownerClass){
                    expr.injectClassArena = true;
                    ownerClass->needsClassArena = true;
                }
                else{
                    expr.injectScratchArena = true;
                    method.usesScratchArena = true;
                }
            }
            targetCallable = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount, &argTypeHints, &argIsMutHints);
            if(!targetCallable){
                localCallable = findScopedVar(scope, expr.lhs->text);
                if(localCallable && localCallable->type)
                    invokedFuncType = m_symbols.unwrapTypeAlias(localCallable->type);
            }
            if(!targetCallable && !localCallable){
                if(const ClassDecl* ctorCls = m_symbols.findClass(baseCallee)){
                    targetOwnerCls = ctorCls;
                    targetCallable = findClassConstructor(*ctorCls, callArgCount, ownerClass != ctorCls, &argTypeHints, &argIsMutHints);
                    if(targetCallable && ownerClass != ctorCls && targetCallable->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot call 'private' or 'protected' constructor outside of its declaring class");
                        ok = false;
                    }
                    if(!ctorCls->templateParams.empty() && !expr.callTemplateArgs.empty()){
                        TypeRef* tempClsRef = AllocateAstNode<TypeRef>(m_arena);
                        tempClsRef->name = baseCallee;
                        tempClsRef->templateArgs = expr.callTemplateArgs;
                        recvTypeRef = tempClsRef;
                    }
                }
            }
            if(expr.lhs->text == "wait"){
                reportError(errors, expr.span, "'wait' can only be used as a standalone join statement");
                ok = false;
            }
            if(!targetCallable && !localCallable){
                const MStringView fnName = expr.lhs->text;
                usize expectedBuiltinArity = 0u;
#define NWB_ADHD_EXPORT_FUNCTION(adhdName, cppExportName, arity, returnRule) \
                if(fnName == adhdName) expectedBuiltinArity = arity;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_FUNCTION
                if(expectedBuiltinArity > 0u && callArgCount != expectedBuiltinArity){
                    reportError(errors, expr.span, "wrong number of arguments to built-in math/SIMD function");
                    ok = false;
                }
            }
            if(baseCallee == "Path"){
                for(Expr* arg : expr.arguments){
                    if(arg)
                        markStringToPathAdaptation(ownerClass, method, *arg, scope);
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            const ScopedVarInfo* lambdaThis = m_inLambda ? findScopedVar(scope, "this") : nullptr;
            const b8 isConstThis = method.isConst || (lambdaThis && lambdaThis->isConst);
            targetOwnerCls = ownerClass;
            targetCallable = findClassMethod(*ownerClass, expr.lhs->text, &callArgCount, isConstThis, false, &argTypeHints, &argIsMutHints);
            if(!targetCallable){
                if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text)){
                    invokedFuncType = m_symbols.unwrapTypeAlias(field->type);
                    if(field->type && (!invokedFuncType || !invokedFuncType->isFuncType) && !findTemplateParam(ownerClass, &method, field->type->name)){
                        reportError(errors, expr.span, "class member field is not a callable function");
                        ok = false;
                    }
                }
            }
            if(isConstThis && targetCallable && !targetCallable->isConst){
                reportError(errors, expr.span, "cannot call non-'const' method on 'this' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            recvTypeRef = inferExprTypeRef(ownerClass, &method, *expr.lhs->lhs, scope);
            const MStringView objType = recvTypeRef
                ? m_symbols.resolveBaseTypeName(recvTypeRef->name)
                : inferExprTypeName(ownerClass, &method, *expr.lhs->lhs, scope);
            const MStringView mName = expr.lhs->text;
            calledMemberName = mName;
            const b8 recvIsConst = isReceiverConst(ownerClass, method, *expr.lhs->lhs, scope);
            b8 isMutatingContainerOp = false;
#define NWB_ADHD_EXPORT_MEMBER_METHOD(methodName, isMutating, adaptsFirstArgToElement, returnRule) \
            if constexpr(isMutating){ \
                if(mName == methodName) isMutatingContainerOp = true; \
            }
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_MEMBER_METHOD
            if(const ClassDecl* targetCls = m_symbols.findClass(objType)){
                targetOwnerCls = targetCls;
                targetCallable = findClassMethod(*targetCls, mName, &callArgCount, recvIsConst, ownerClass != targetCls, &argTypeHints, &argIsMutHints);
                if(targetCallable){
                    if(ownerClass != targetCls && targetCallable->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot call 'private' or 'protected' method outside of its declaring class");
                        ok = false;
                    }
                }
                else if(const FieldDecl* field = findClassField(*targetCls, mName)){
                    if(ownerClass != targetCls && field->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot access 'private' or 'protected' field outside of its declaring class");
                        ok = false;
                    }
                    const TypeRef* ft = (recvTypeRef && !targetCls->templateParams.empty() && !recvTypeRef->templateArgs.empty() && field->type)
                        ? m_symbols.substituteTypeParams(*field->type, targetCls->templateParams, recvTypeRef->templateArgs)
                        : field->type;
                    invokedFuncType = m_symbols.unwrapTypeAlias(ft);
                    if(ft && (!invokedFuncType || !invokedFuncType->isFuncType) && !findTemplateParam(ownerClass, &method, ft->name)){
                        reportError(errors, expr.span, "class member field is not a callable function");
                        ok = false;
                    }
                }
            }
            else if(recvTypeRef && m_symbols.findInterface(objType)){
                targetCallable = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &callArgCount, 0u, &argTypeHints, &argIsMutHints);
                if(!targetCallable){
                    reportError(errors, expr.span, "unknown method on interface");
                    ok = false;
                }
            }
            else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, &method, objType)){
                for(const TypeRef* boundRef : tp->interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(const MethodDecl* boundMethod = m_symbols.findInterfaceMethod(*boundRef, mName, &callArgCount, 0u, &argTypeHints, &argIsMutHints)){
                        targetCallable = boundMethod;
                        break;
                    }
                }
                if(!targetCallable){
                    reportError(errors, expr.span, "method call on template parameter requires a matching interface bound ('<T : IInterface>')");
                    ok = false;
                }
            }
            else if(IsNonThreadSafeContainer(objType) && m_parallelDepth > 0u && isMutatingContainerOp){
                b8 isTaskLocalContainer = false;
                if(expr.lhs->lhs->kind == ExprKind::Identifier){
                    if(const ScopedVarInfo* recvLocal = findScopedVar(scope, expr.lhs->lhs->text))
                        isTaskLocalContainer = (recvLocal->declaredParallelDepth == m_parallelDepth);
                }
                if(!isTaskLocalContainer){
                    reportError(errors, expr.span, "mutating non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers");
                    ok = false;
                }
            }

            if((targetCallable && !targetCallable->isConst) || isMutatingContainerOp){
                const b8 savedDeref = expr.lhs->lhs->derefNotNull;
                if(!checkExpr(ownerClass, method, *expr.lhs->lhs, scope, true, errors))
                    ok = false;
                expr.lhs->lhs->derefNotNull = savedDeref;
            }
        }
    }

    const TypeRef* callReturnType = nullptr;
    if(targetCallable && !targetCallable->isConstructor){
        callReturnType = targetCallable->returnType;
        if(callReturnType && targetOwnerCls && recvTypeRef && !targetOwnerCls->templateParams.empty() && !recvTypeRef->templateArgs.empty())
            callReturnType = m_symbols.substituteTypeParams(*callReturnType, targetOwnerCls->templateParams, recvTypeRef->templateArgs);
        if(callReturnType && !targetCallable->templateParams.empty() && !expr.callTemplateArgs.empty())
            callReturnType = m_symbols.substituteTypeParams(*callReturnType, targetCallable->templateParams, expr.callTemplateArgs);
    }
    else if(invokedFuncType && invokedFuncType->isFuncType)
        callReturnType = invokedFuncType->funcReturnType;
    else if(localCallable && localCallable->lambdaExpr)
        callReturnType = localCallable->lambdaExpr->lambdaReturnType;

    const ParamPassingMode::Enum callRetRefMode = m_symbols.resolveRefMode(callReturnType);
    if(callRetRefMode != ParamPassingMode::None)
        expr.derefNotNull = true;

    if(isMutationTarget){
        const b8 isCtorOrTypeConstruction = (targetCallable && targetCallable->isConstructor)
            || (!targetCallable && !localCallable && expr.lhs && expr.lhs->kind == ExprKind::Identifier && (
                m_symbols.findClass(m_symbols.resolveBaseTypeName(expr.lhs->text)) != nullptr
                || SymbolTable::isSimdType(m_symbols.resolveBaseTypeName(expr.lhs->text))
                || SymbolTable::isArenaBackedContainer(m_symbols.resolveBaseTypeName(expr.lhs->text))
                || m_symbols.isSelfDefaultedType(m_symbols.resolveBaseTypeName(expr.lhs->text))
            ));
        if(isCtorOrTypeConstruction){
            reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
            ok = false;
        }
        else if(callReturnType){
            if(callRetRefMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                ok = false;
            }
            else if(callRetRefMode == ParamPassingMode::None){
                reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
                ok = false;
            }
        }
    }

    if(!expr.callTemplateArgs.empty()){
        if(targetCallable && !targetCallable->isConstructor){
            if(!checkTemplateArgBounds(targetCallable->templateParams, expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
                ok = false;
        }
        else if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            if(const ClassDecl* targetCls = m_symbols.findClass(expr.lhs->text)){
                if(!checkTemplateArgBounds(targetCls->templateParams, expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
                    ok = false;
            }
            else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(expr.lhs->text)){
                if(!checkTemplateArgBounds(aliasDecl->templateParams, expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
                    ok = false;
            }
        }
    }
    if(!checkTemplateArgTypes(expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
        ok = false;

    if(!checkCallableArguments(
        ownerClass,
        method,
        expr.arguments,
        scope,
        targetCallable,
        targetOwnerCls,
        recvTypeRef,
        &expr.callTemplateArgs,
        invokedFuncType,
        localCallable,
        calledMemberName,
        errors
    )){
        ok = false;
    }

    return ok;
}

b8 TypeChecker::checkLambdaExpr(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Expr& expr,
    const MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
    b8 ok = true;

    if(expr.lambdaReturnType){
        if(!checkTypeRef(*expr.lambdaReturnType, ownerClass, &method, errors, true))
            ok = false;
        if(m_symbols.resolveRefMode(expr.lambdaReturnType) == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.lambdaReturnType->name))){
            reportError(errors, expr.span, "cannot return an 'interface' type by value");
            ok = false;
        }
    }
    MVector<ScopedVarInfo> lambdaScope(m_arena);
    for(const LambdaCapture& cap : expr.lambdaCaptures){
        if(!cap.name.empty() && findScopedVar(lambdaScope, cap.name)){
            reportError(errors, cap.span, "duplicate capture name in lambda");
            ok = false;
        }
        if(cap.name == "this"){
            const ScopedVarInfo* outerThis = findScopedVar(scope, "this");
            if(!ownerClass){
                reportError(errors, cap.span, "cannot capture 'this' in a free function");
                ok = false;
            }
            else if(cap.mode == ParamPassingMode::Mut && (method.isConst || (outerThis && outerThis->isConst))){
                reportError(errors, cap.span, "cannot capture 'this' as 'mut' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
            ScopedVarInfo thisCapVar;
            thisCapVar.name = cap.name;
            thisCapVar.mode = cap.mode;
            thisCapVar.isConst = (cap.mode == ParamPassingMode::In);
            thisCapVar.isParam = true;
            lambdaScope.push_back(thisCapVar);
            continue;
        }
        const ScopedVarInfo* outer = findScopedVar(scope, cap.name);
        const Stmt* globalConst = outer ? nullptr : m_symbols.findGlobalConstant(cap.name);
        if(!outer && !globalConst){
            reportError(errors, cap.span, "unknown variable in lambda capture list");
            ok = false;
        }
        if(outer && cap.mode == ParamPassingMode::Mut && (outer->mode == ParamPassingMode::In || outer->isConst)){
            reportError(errors, cap.span, "cannot capture 'in' or 'const' variable as 'mut' in lambda");
            ok = false;
        }
        else if(globalConst && cap.mode == ParamPassingMode::Mut){
            reportError(errors, cap.span, "cannot capture 'const' global variable as 'mut' in lambda");
            ok = false;
        }
        if(outer && cap.mode == ParamPassingMode::Mut)
            expr.hasMutLocalCapture = true;
        ScopedVarInfo capVar;
        capVar.name = cap.name;
        capVar.type = outer ? outer->type : (globalConst ? globalConst->varType : nullptr);
        capVar.inferredTypeName = outer ? outer->inferredTypeName : MStringView{};
        capVar.mode = cap.mode;
        capVar.isConst = (cap.mode == ParamPassingMode::In) || (globalConst != nullptr);
        capVar.isParam = true;
        lambdaScope.push_back(capVar);
    }
    for(const ParamDecl& p : expr.lambdaParams){
        if(!p.name.empty() && findScopedVar(lambdaScope, p.name)){
            reportError(errors, p.span, "duplicate parameter or capture name in lambda");
            ok = false;
        }
        if(p.type && !checkTypeRef(*p.type, ownerClass, &method, errors))
            ok = false;
        ScopedVarInfo paramVar;
        paramVar.name = p.name;
        paramVar.type = p.type;
        paramVar.mode = p.mode;
        paramVar.isConst = (p.mode == ParamPassingMode::In);
        paramVar.isParam = true;
        lambdaScope.push_back(paramVar);
    }
    const b8 savedInLambda = m_inLambda;
    const TypeRef* savedLambdaRet = m_activeLambdaReturnType;
    const u32 savedLoop = m_loopDepth;
    const u32 savedSwitch = m_switchDepth;
    const b8 savedMethodScratch = method.usesScratchArena;
    const b8 savedMethodTaskScope = method.usesTaskScope;
    m_inLambda = true;
    m_activeLambdaReturnType = expr.lambdaReturnType;
    m_loopDepth = 0u;
    m_switchDepth = 0u;
    method.usesScratchArena = false;
    method.usesTaskScope = false;
    if(!checkStatementList(ownerClass, method, expr.lambdaBody, lambdaScope, errors))
        ok = false;
    if(expr.lambdaReturnType && !IsVoidTypeRef(m_symbols.unwrapTypeAlias(expr.lambdaReturnType))){
        if(!statementsContainReturnOrNative(expr.lambdaBody)){
            reportError(errors, expr.span, "non-void lambda must contain a 'return' statement");
            ok = false;
        }
    }
    expr.lambdaUsesScratchArena = method.usesScratchArena;
    expr.lambdaUsesTaskScope = method.usesTaskScope;
    method.usesScratchArena = savedMethodScratch;
    method.usesTaskScope = savedMethodTaskScope;
    m_inLambda = savedInLambda;
    m_activeLambdaReturnType = savedLambdaRet;
    m_loopDepth = savedLoop;
    m_switchDepth = savedSwitch;

    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
