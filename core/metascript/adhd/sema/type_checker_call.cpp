// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TypeChecker::PropagateFuncCaptureBytes(TypeRef* targetType, const Expr* rhs, const Span<const ScopedVarInfo> scope, const SymbolTable& symbols)noexcept{
    TypeRef* funcType = ExtractUnderlyingFuncType(targetType, symbols);
    if(!funcType || !rhs)
        return;
    if(rhs->kind == ExprKind::Lambda){
        if(!rhs->lambdaCaptures.empty())
            funcType->inferredCaptureBytes = 64u;
        return;
    }
    if(rhs->kind == ExprKind::Identifier){
        if(const ScopedVarInfo* v = FindScopedVar(scope, rhs->text)){
            const TypeRef* rhsFunc = ExtractUnderlyingFuncType(v->type, symbols);
            if((v->lambdaExpr && !v->lambdaExpr->lambdaCaptures.empty()) || (rhsFunc && rhsFunc->inferredCaptureBytes > 0u))
                funcType->inferredCaptureBytes = 64u;
        }
        return;
    }
    if(rhs->kind == ExprKind::Call){
        for(const Expr* arg : rhs->arguments){
            if(arg)
                PropagateFuncCaptureBytes(targetType, arg, scope, symbols);
        }
    }
}

b8 TypeChecker::IsEscapingMutCaptureLambda(const Expr* rhs, const Span<const ScopedVarInfo> scope)noexcept{
    if(!rhs)
        return false;
    if(rhs->kind == ExprKind::Lambda)
        return rhs->hasMutLocalCapture;
    if(rhs->kind == ExprKind::Identifier){
        if(const ScopedVarInfo* v = FindScopedVar(scope, rhs->text))
            return v->lambdaExpr && v->lambdaExpr->hasMutLocalCapture;
    }
    return false;
}

b8 TypeChecker::checkCallableArguments(
    ClassDecl* ownerClass,
    MethodDecl& method,
    MVector<Expr*>& arguments,
    const Span<const ScopedVarInfo> scope,
    const MethodDecl* targetCallable,
    const ClassDecl* targetOwnerCls,
    const TypeRef* recvTypeRef,
    const Span<const TypeArg> callTemplateArgs,
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
                if(targetOwnerCls)
                    subParamType = m_symbols.substituteTypeParams(subParamType, targetOwnerCls->templateParams, recvTypeRef);
                if(!targetCallable->templateParams.empty() && !callTemplateArgs.empty())
                    subParamType = m_symbols.substituteTypeParams(*subParamType, targetCallable->templateParams, callTemplateArgs);
                expectedParamTypeRef = subParamType;
                expectedParamType = m_symbols.resolveBaseTypeName(subParamType->name);
                PropagateFuncCaptureBytes(targetCallable->params[i].type, arg, scope, m_symbols);
            }
        }
        else if(invokedFuncType && invokedFuncType->isFuncType && i < invokedFuncType->funcParams.size()){
            paramIsMut = (invokedFuncType->funcParams[i].mode == ParamPassingMode::Mut);
            if(invokedFuncType->funcParams[i].type){
                expectedParamTypeRef = invokedFuncType->funcParams[i].type;
                expectedParamType = m_symbols.resolveBaseTypeName(invokedFuncType->funcParams[i].type->name);
                PropagateFuncCaptureBytes(invokedFuncType->funcParams[i].type, arg, scope, m_symbols);
            }
        }
        else if(localCallable && localCallable->lambdaExpr && i < localCallable->lambdaExpr->lambdaParams.size()){
            paramIsMut = (localCallable->lambdaExpr->lambdaParams[i].mode == ParamPassingMode::Mut);
            if(localCallable->lambdaExpr->lambdaParams[i].type){
                expectedParamTypeRef = localCallable->lambdaExpr->lambdaParams[i].type;
                expectedParamType = m_symbols.resolveBaseTypeName(localCallable->lambdaExpr->lambdaParams[i].type->name);
                PropagateFuncCaptureBytes(localCallable->lambdaExpr->lambdaParams[i].type, arg, scope, m_symbols);
            }
        }
        else if(i == 0u && recvTypeRef && !recvTypeRef->templateArgs.empty() && recvTypeRef->templateArgs[0].type){
            if(const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(calledMemberName); memberInfo && memberInfo->adaptsFirstArgToElement){
                expectedParamTypeRef = recvTypeRef->templateArgs[0].type;
                expectedParamType = m_symbols.resolveBaseTypeName(expectedParamTypeRef->name);
                PropagateFuncCaptureBytes(expectedParamTypeRef, arg, scope, m_symbols);
            }
        }
        arg->isMutArg = paramIsMut || expectedParamType == "Span" || expectedParamType == "StringView";
        if(expectedParamType == "Path")
            markStringToPathAdaptation(ownerClass, &method, *arg, scope);
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
        const auto checkUnboundTemplateParams = [&](const Span<const TemplateParamDecl> tparams){
            for(const TemplateParamDecl& tp : tparams){
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
        };
        if(targetCallable && expectedParamTypeRef && callTemplateArgs.empty() && !targetCallable->templateParams.empty())
            checkUnboundTemplateParams(targetCallable->templateParams);
        if(targetOwnerCls && expectedParamTypeRef && (!recvTypeRef || recvTypeRef->templateArgs.empty()) && !targetOwnerCls->templateParams.empty())
            checkUnboundTemplateParams(targetOwnerCls->templateParams);
    }
    return ok;
}

b8 TypeChecker::checkCallExpr(ClassDecl* ownerClass, MethodDecl& method, Expr& expr, const Span<const ScopedVarInfo> scope, const b8 isMutationTarget, MVector<ParseError>& errors){
    b8 ok = true;

    if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, false, errors))
        ok = false;

    const MethodDecl* targetCallable = nullptr;
    const ClassDecl* targetOwnerCls = nullptr;
    const TypeRef* recvTypeRef = nullptr;
    MStringView calledMemberName;
    const ScopedVarInfo* localCallable = nullptr;
    ScopedVarInfo tempInlineLambdaVar;
    const TypeRef* invokedFuncType = nullptr;
    const usize callArgCount = expr.arguments.size();
    MVector<MStringView> argTypeHints(m_arena);
    MVector<u8> argIsMutHints(m_arena);
    buildCallArgHints(ownerClass, &method, expr.arguments, scope, argTypeHints, argIsMutHints);
    if(expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(m_symbols.findInterface(baseCallee)){
                reportError(errors, expr.span, "cannot instantiate an 'interface' type directly");
                ok = false;
            }
            if(SymbolTable::IsArenaBackedContainer(baseCallee)){
                if(m_inClassArenaContext && ownerClass){
                    expr.injectClassArena = true;
                    ownerClass->needsClassArena = true;
                }
                else{
                    expr.injectScratchArena = true;
                    method.usesScratchArena = true;
                    m_activeParallelBlockUsesScratch = true;
                }
            }
            targetCallable = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount, argTypeHints, argIsMutHints);
            if(!targetCallable){
                localCallable = FindScopedVar(scope, expr.lhs->text);
                if(localCallable && localCallable->type)
                    invokedFuncType = m_symbols.unwrapTypeAlias(localCallable->type);
            }
            if(!targetCallable && !localCallable){
                if(const ClassDecl* ctorCls = m_symbols.findClass(baseCallee)){
                    targetOwnerCls = ctorCls;
                    targetCallable = m_symbols.findClassConstructor(*ctorCls, callArgCount, ownerClass != ctorCls, argTypeHints, argIsMutHints);
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
                if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text); fnInfo && fnInfo->arity > 0u && callArgCount != fnInfo->arity){
                    reportError(errors, expr.span, "wrong number of arguments to built-in math/SIMD function");
                    ok = false;
                }
            }
            if(baseCallee == "Path"){
                for(Expr* arg : expr.arguments){
                    if(arg)
                        markStringToPathAdaptation(ownerClass, &method, *arg, scope);
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            const b8 isConstThis = isThisConstInScope(method, scope);
            targetOwnerCls = ownerClass;
            targetCallable = m_symbols.findClassMethod(*ownerClass, expr.lhs->text, &callArgCount, isConstThis, false, argTypeHints, argIsMutHints);
            if(!targetCallable){
                if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, expr.lhs->text)){
                    invokedFuncType = m_symbols.unwrapTypeAlias(field->type);
                    if(field->type && (!invokedFuncType || !invokedFuncType->isFuncType) && !FindTemplateParam(ownerClass, &method, field->type->name)){
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
            const MStringView objType = inferExprBaseType(ownerClass, &method, *expr.lhs->lhs, scope, &recvTypeRef);
            const MStringView mName = expr.lhs->text;
            calledMemberName = mName;
            const b8 recvIsConst = isReceiverConst(ownerClass, method, *expr.lhs->lhs, scope);
            const BuiltinMemberMethodInfo* builtinMemberInfo = FindBuiltinMemberMethod(mName);
            const b8 isMutatingContainerOp = builtinMemberInfo && builtinMemberInfo->isMutating;
            if(const ClassDecl* targetCls = m_symbols.findClass(objType)){
                targetOwnerCls = targetCls;
                targetCallable = m_symbols.findClassMethod(*targetCls, mName, &callArgCount, recvIsConst, ownerClass != targetCls, argTypeHints, argIsMutHints);
                if(targetCallable){
                    if(ownerClass != targetCls && targetCallable->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot call 'private' or 'protected' method outside of its declaring class");
                        ok = false;
                    }
                }
                else if(const FieldDecl* field = SymbolTable::FindClassField(*targetCls, mName)){
                    if(ownerClass != targetCls && field->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot access 'private' or 'protected' field outside of its declaring class");
                        ok = false;
                    }
                    const TypeRef* ft = m_symbols.substituteTypeParams(field->type, targetCls->templateParams, recvTypeRef);
                    invokedFuncType = m_symbols.unwrapTypeAlias(ft);
                    if(ft && (!invokedFuncType || !invokedFuncType->isFuncType) && !FindTemplateParam(ownerClass, &method, ft->name)){
                        reportError(errors, expr.span, "class member field is not a callable function");
                        ok = false;
                    }
                }
            }
            else if(recvTypeRef && m_symbols.findInterface(objType)){
                targetCallable = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints);
                if(!targetCallable){
                    reportError(errors, expr.span, "unknown method on interface");
                    ok = false;
                }
                else if(recvIsConst && !targetCallable->isConst){
                    reportError(errors, expr.span, "cannot call non-'const' method on 'in' or 'const' interface receiver");
                    ok = false;
                }
            }
            else if(const TemplateParamDecl* tp = FindTemplateParam(ownerClass, &method, objType)){
                for(const TypeRef* boundRef : tp->interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(const MethodDecl* boundMethod = m_symbols.findInterfaceMethod(*boundRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints)){
                        targetCallable = boundMethod;
                        break;
                    }
                }
                if(!targetCallable){
                    reportError(errors, expr.span, "method call on template parameter requires a matching interface bound ('<T : IInterface>')");
                    ok = false;
                }
                else if(recvIsConst && !targetCallable->isConst){
                    reportError(errors, expr.span, "cannot call non-'const' method on 'in' or 'const' bounded template parameter");
                    ok = false;
                }
            }
            else if(IsNonThreadSafeContainer(objType) && m_parallelDepth > 0u && isMutatingContainerOp){
                b8 isTaskLocalContainer = false;
                if(expr.lhs->lhs->kind == ExprKind::Identifier){
                    if(const ScopedVarInfo* recvLocal = FindScopedVar(scope, expr.lhs->lhs->text))
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
        else if(expr.lhs->kind == ExprKind::Lambda){
            tempInlineLambdaVar.lambdaExpr = expr.lhs;
            tempInlineLambdaVar.declaredParallelDepth = m_parallelDepth;
            localCallable = &tempInlineLambdaVar;
        }
        else{
            const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, &method, *expr.lhs, scope);
            invokedFuncType = m_symbols.unwrapTypeAlias(lhsTypeRef);
        }
    }
    if(m_parallelDepth > 0u && localCallable && localCallable->lambdaExpr && localCallable->lambdaExpr->hasMutContainerCapture && localCallable->declaredParallelDepth < m_parallelDepth){
        reportError(errors, expr.span, "calling lambda that captures outer non-thread-safe container as 'mut' inside 'parallel' or 'parallel_for' is prohibited");
        ok = false;
    }

    const TypeRef* callReturnType = nullptr;
    if(targetCallable && !targetCallable->isConstructor){
        TypeRef* mutableRetType = targetCallable->returnType;
        if(targetOwnerCls)
            mutableRetType = m_symbols.substituteTypeParams(mutableRetType, targetOwnerCls->templateParams, recvTypeRef);
        if(mutableRetType && !targetCallable->templateParams.empty() && !expr.callTemplateArgs.empty())
            mutableRetType = m_symbols.substituteTypeParams(*mutableRetType, targetCallable->templateParams, expr.callTemplateArgs);
        callReturnType = mutableRetType;
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
            || (!targetCallable && !localCallable && expr.lhs && expr.lhs->kind == ExprKind::Identifier
                && isConstructibleTypeName(expr.lhs->text, m_symbols.resolveBaseTypeName(expr.lhs->text)));
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
        expr.callTemplateArgs,
        invokedFuncType,
        localCallable,
        calledMemberName,
        errors
    )){
        ok = false;
    }

    return ok;
}

b8 TypeChecker::checkLambdaExpr(ClassDecl* ownerClass, MethodDecl& method, Expr& expr, const Span<const ScopedVarInfo> scope, MVector<ParseError>& errors){
    b8 ok = true;

    if(expr.lambdaReturnType && !checkReturnTypeRef(*expr.lambdaReturnType, ownerClass, &method, expr.span, errors))
        ok = false;
    MVector<ScopedVarInfo> lambdaScope(m_arena);
    for(LambdaCapture& cap : expr.lambdaCaptures){
        if(!cap.name.empty() && FindScopedVar(lambdaScope, cap.name)){
            reportError(errors, cap.span, "duplicate capture name in lambda");
            ok = false;
        }
        if(cap.name == "this"){
            if(!ownerClass){
                reportError(errors, cap.span, "cannot capture 'this' in a free function");
                ok = false;
            }
            else if(cap.mode == ParamPassingMode::Mut && isThisConstInScope(method, scope)){
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
        const ScopedVarInfo* outer = FindScopedVar(scope, cap.name);
        const Stmt* globalConst = outer ? nullptr : m_symbols.findGlobalConstant(cap.name);
        cap.isGlobalConstant = (globalConst != nullptr);
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
        if(outer && cap.mode == ParamPassingMode::Mut){
            expr.hasMutLocalCapture = true;
            if(IsNonThreadSafeContainer(resolveScopedVarBaseType(*outer))){
                expr.hasMutContainerCapture = true;
                if(m_parallelDepth > 0u && outer->declaredParallelDepth < m_parallelDepth){
                    reportError(errors, cap.span, "shared 'mut' capture of non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited");
                    ok = false;
                }
            }
        }
        ScopedVarInfo capVar;
        capVar.name = cap.name;
        capVar.type = outer ? outer->type : (globalConst ? globalConst->varType : nullptr);
        capVar.inferredTypeName = outer ? outer->inferredTypeName : MStringView{};
        capVar.mode = cap.mode;
        capVar.isConst = (cap.mode == ParamPassingMode::In) || (globalConst != nullptr);
        capVar.isParam = true;
        capVar.declaredParallelDepth = outer ? outer->declaredParallelDepth : 0u;
        lambdaScope.push_back(capVar);
    }
    for(const ParamDecl& p : expr.lambdaParams){
        if(!p.name.empty() && FindScopedVar(lambdaScope, p.name)){
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
    const b8 savedParScratch = m_activeParallelBlockUsesScratch;
    m_inLambda = true;
    m_activeLambdaReturnType = expr.lambdaReturnType;
    m_loopDepth = 0u;
    m_switchDepth = 0u;
    method.usesScratchArena = false;
    method.usesTaskScope = false;
    m_activeParallelBlockUsesScratch = false;
    if(!checkStatementList(ownerClass, method, expr.lambdaBody, lambdaScope, errors))
        ok = false;
    if(expr.lambdaReturnType && !IsVoidTypeRef(m_symbols.unwrapTypeAlias(expr.lambdaReturnType))){
        if(!StatementsContainReturnOrNative(expr.lambdaBody)){
            reportError(errors, expr.span, "non-void lambda must contain a 'return' statement");
            ok = false;
        }
    }
    expr.lambdaUsesScratchArena = method.usesScratchArena;
    expr.lambdaUsesTaskScope = method.usesTaskScope;
    method.usesScratchArena = savedMethodScratch;
    method.usesTaskScope = savedMethodTaskScope;
    m_activeParallelBlockUsesScratch = savedParScratch;
    m_inLambda = savedInLambda;
    m_activeLambdaReturnType = savedLambdaRet;
    m_loopDepth = savedLoop;
    m_switchDepth = savedSwitch;

    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

