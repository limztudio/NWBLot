// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeChecker::StatementsContainReturnOrNative(const Span<const Stmt* const> stmts)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(stmt->kind == StmtKind::Return || stmt->kind == StmtKind::NativeBlock)
            return true;
        if(StatementsContainReturnOrNative(stmt->statements) || StatementsContainReturnOrNative(stmt->elseStatements))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(StatementsContainReturnOrNative(sc.statements))
                return true;
        }
    }
    return false;
}

b8 TypeChecker::checkMethodBody(ClassDecl* ownerClass, MethodDecl& method, MVector<ParseError>& errors){
    if(!method.hasBody)
        return true;

    b8 ok = true;
    MVector<ScopedVarInfo> scope(m_arena);
    for(const ParamDecl& param : method.params){
        if(!param.name.empty() && FindScopedVar(scope, param.name)){
            reportError(errors, param.span, "duplicate parameter name");
            ok = false;
        }
        ScopedVarInfo info;
        info.name = param.name;
        info.type = param.type;
        info.mode = param.mode;
        info.isConst = (param.mode == ParamPassingMode::In);
        info.isParam = true;
        scope.push_back(info);
    }

    if(!checkStatementList(ownerClass, method, method.body, scope, errors))
        ok = false;
    if(!method.isConstructor && !method.isDestructor && !IsVoidTypeRef(m_symbols.unwrapTypeAlias(method.returnType))){
        if(!StatementsContainReturnOrNative(method.body)){
            reportError(errors, method.span, "non-void function or method must contain a 'return' statement");
            ok = false;
        }
    }
    return ok;
}

b8 TypeChecker::checkStatementList(ClassDecl* ownerClass, MethodDecl& method, MVector<Stmt*>& statements, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors){
    const usize savedScopeSize = scope.size();
    b8 ok = true;

    for(Stmt* stmt : statements){
        if(!stmt)
            continue;
        if(!checkStatement(ownerClass, method, *stmt, scope, errors))
            ok = false;
    }

    analyzeBlockMoveAtLastUse(statements, scope, savedScopeSize);
    while(scope.size() > savedScopeSize)
        scope.pop_back();

    return ok;
}

b8 TypeChecker::checkConstructorInitArguments(ClassDecl* ownerClass, const TypeRef* targetType, const Span<const ScopedVarInfo> scope, const SourceSpan errorSpan, MethodDecl& method, MVector<Expr*>& arguments, MVector<ParseError>& errors){
    b8 ok = true;
    const TypeRef* resolvedTargetType = targetType ? m_symbols.unwrapTypeAlias(targetType) : nullptr;
    const MStringView baseTargetName = resolvedTargetType
        ? m_symbols.resolveBaseTypeName(resolvedTargetType->name)
        : MStringView{};
    const ClassDecl* ctorCls = !baseTargetName.empty()
        ? m_symbols.findClass(baseTargetName)
        : nullptr;
    const MethodDecl* matchedCtor = nullptr;
    if(ctorCls){
        MVector<MStringView> argTypeHints(m_arena);
        MVector<u8> argIsMutHints(m_arena);
        buildCallArgHints(ownerClass, &method, arguments, scope, argTypeHints, argIsMutHints);
        matchedCtor = m_symbols.findClassConstructor(*ctorCls, arguments.size(), ownerClass != ctorCls, argTypeHints, argIsMutHints);
        if(matchedCtor && ownerClass != ctorCls && matchedCtor->access != AccessSpecifier::Public){
            reportError(errors, errorSpan, "cannot call 'private' or 'protected' constructor outside of its declaring class");
            ok = false;
        }
    }
    else if(resolvedTargetType && !baseTargetName.empty()){
        if(!IsValidBuiltinVectorOrMatrixCtorArity(baseTargetName, arguments.size())){
            reportError(errors, errorSpan, "wrong number of arguments to built-in vector/matrix constructor");
            ok = false;
        }
        else if(
            arguments.size() > 1u
            && (
                IsScalarType(baseTargetName)
                || m_symbols.findEnum(baseTargetName) != nullptr
                || IsSingleValueCtorBuiltinType(baseTargetName)
            )
        ){
            reportError(errors, errorSpan, "scalar, enum, or single-value constructor accepts at most 1 argument");
            ok = false;
        }
        if(const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(baseTargetName); simdVecInfo && simdVecInfo->isMixed && arguments.size() == 1u && arguments[0]){
            const MStringView arg0Type = inferExprTypeName(ownerClass, &method, *arguments[0], scope);
            if(arg0Type != baseTargetName){
                reportError(errors, errorSpan, "mixed float/integer SIMD vector type does not support single-scalar splat construction; provide all 4 lane arguments");
                ok = false;
            }
        }
    }
    if(!checkCallableArguments(
        ownerClass,
        scope,
        matchedCtor,
        ctorCls,
        resolvedTargetType,
        {},
        nullptr,
        nullptr,
        MStringView{},
        method,
        arguments,
        errors
    )){
        ok = false;
    }
    return ok;
}

b8 TypeChecker::checkVarDeclStatement(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors){
    b8 ok = true;
    if(!stmt.name.empty() && FindScopedVar(scope, stmt.name)){
        reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
        ok = false;
    }
    const ParamPassingMode::Enum varRefMode = m_symbols.resolveRefMode(stmt.varType);
    if(stmt.varType){
        if(!checkTypeRef(*stmt.varType, ownerClass, &method, errors))
            ok = false;
        const MStringView baseVarTypeName = m_symbols.resolveBaseTypeName(stmt.varType->name);
        if(m_symbols.findInterface(baseVarTypeName)){
            if(varRefMode == ParamPassingMode::None){
                reportError(errors, stmt.span, "local variable of 'interface' type must specify 'in' or 'mut'; interfaces cannot be instantiated by value");
                ok = false;
            }
            else if(stmt.expr && !verifyConcreteClassImplementsInterface(
                ownerClass,
                method,
                *stmt.expr,
                stmt.varType,
                scope,
                stmt.span,
                "cannot bind concrete class to interface reference: class does not implement the interface",
                errors
            )){
                ok = false;
            }
        }
        b8 isSelfDefaultedVar = m_symbols.isSelfDefaultedType(stmt.varType->name, ownerClass);
        if(isSelfDefaultedVar && IsFixedArrayContainerType(baseVarTypeName)){
            if(const TypeRef* unwrappedArr = m_symbols.unwrapTypeAlias(stmt.varType); unwrappedArr && !unwrappedArr->templateArgs.empty() && unwrappedArr->templateArgs[0].type){
                const TypeRef* elemRef = unwrappedArr->templateArgs[0].type;
                const MStringView elemBase = m_symbols.resolveBaseTypeName(elemRef->name);
                if(m_symbols.resolveRefMode(elemRef) != ParamPassingMode::None || (m_symbols.findClass(elemBase) && !m_symbols.isSelfDefaultedType(elemBase, ownerClass)))
                    isSelfDefaultedVar = false;
            }
        }
        if(!stmt.expr && !stmt.isCtorInit && (varRefMode != ParamPassingMode::None || !isSelfDefaultedVar)){
            reportError(errors, stmt.span, "uninitialized variable declaration is prohibited in adhd; every local variable must be initialized at declaration");
            ok = false;
        }
        if(varRefMode == ParamPassingMode::None && SymbolTable::IsArenaBackedContainer(baseVarTypeName)){
            stmt.usesScratchArena = true;
            method.usesScratchArena = true;
            m_activeParallelBlockUsesScratch = true;
        }
        PropagateFuncCaptureBytes(stmt.varType, stmt.expr, scope, m_symbols);
        if(stmt.isCtorInit){
            for(const Expr* arg : stmt.arguments){
                if(arg)
                    PropagateFuncCaptureBytes(stmt.varType, arg, scope, m_symbols);
            }
        }
        if(RequiresTCharAdaptation(baseVarTypeName)){
            if(stmt.expr)
                markStringToPathAdaptation(ownerClass, &method, scope, *stmt.expr);
            if(stmt.isCtorInit){
                for(Expr* arg : stmt.arguments){
                    if(arg)
                        markStringToPathAdaptation(ownerClass, &method, scope, *arg);
                }
            }
        }
    }

    if(stmt.expr){
        const b8 isMutRefBinding = (varRefMode == ParamPassingMode::Mut);
        if(!checkExpr(ownerClass, scope, isMutRefBinding, method, *stmt.expr, errors))
            ok = false;
        if(varRefMode == ParamPassingMode::In && isRvalueTemporaryExpr(ownerClass, &method, *stmt.expr, scope)){
            reportError(errors, stmt.span, "cannot bind 'in' reference variable to a literal or rvalue temporary");
            ok = false;
        }
        if(stmt.varType && stmt.varType->isAuto && !stmt.expr->resolvedSimdType.empty()){
            stmt.varType->isAuto = false;
            stmt.varType->name = stmt.expr->resolvedSimdType;
        }
    }

    if(!checkConstructorInitArguments(
        ownerClass,
        stmt.isCtorInit ? stmt.varType : nullptr,
        scope,
        stmt.span,
        method,
        stmt.arguments,
        errors
    )){
        ok = false;
    }

    ScopedVarInfo info;
    info.name = stmt.name;
    info.type = stmt.varType;
    if(stmt.varType && stmt.varType->isAuto && stmt.expr && stmt.expr->kind != ExprKind::Lambda){
        info.inferredTypeName = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
        const TypeRef* exprTypeRef = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
        const MStringView effectiveAutoName = exprTypeRef
            ? m_symbols.resolveBaseTypeName(exprTypeRef->name)
            : info.inferredTypeName;
        if(varRefMode == ParamPassingMode::None && !effectiveAutoName.empty() && m_symbols.findInterface(effectiveAutoName)){
            reportError(errors, stmt.span, "local variable of 'interface' type must specify 'in' or 'mut'; interfaces cannot be instantiated by value");
            ok = false;
        }
        if(varRefMode != ParamPassingMode::None){
            if(exprTypeRef){
                const ParamPassingMode::Enum savedRefMode = stmt.varType->refMode;
                const SourceSpan savedSpan = stmt.varType->span;
                *stmt.varType = *exprTypeRef;
                stmt.varType->refMode = savedRefMode;
                stmt.varType->span = savedSpan;
                stmt.varType->isAuto = false;
            }
            else if(!effectiveAutoName.empty()){
                stmt.varType->isAuto = false;
                stmt.varType->name = effectiveAutoName;
            }
            info.type = stmt.varType;
        }
        else if(exprTypeRef){
            stmt.inferredAutoType = const_cast<TypeRef*>(exprTypeRef);
            info.type = stmt.inferredAutoType;
        }
    }
    info.mode = varRefMode;
    info.isConst = stmt.isConst || (varRefMode == ParamPassingMode::In);
    info.isParam = false;
    info.declaredParallelDepth = m_parallelDepth;
    if(stmt.expr && stmt.expr->kind == ExprKind::Lambda)
        info.lambdaExpr = stmt.expr;
    scope.push_back(info);
    return ok;
}

b8 TypeChecker::checkReturnStatement(ClassDecl* ownerClass, const Span<const ScopedVarInfo> scope, MethodDecl& method, Stmt& stmt, MVector<ParseError>& errors){
    b8 ok = true;
    if(m_parallelDepth > 0u && !m_inLambda){
        reportError(errors, stmt.span, "'return' cannot be used inside a 'parallel' or 'parallel_for' construct");
        ok = false;
    }
    const TypeRef* activeRetType = m_inLambda ? m_activeLambdaReturnType : method.returnType;
    const TypeRef* unwrappedRetType = m_symbols.unwrapTypeAlias(activeRetType);
    const ParamPassingMode::Enum retRefMode = m_symbols.resolveRefMode(activeRetType);
    const b8 isVoidLike = (!m_inLambda && (method.isConstructor || method.isDestructor)) || IsVoidTypeRef(unwrappedRetType);
    if(isVoidLike && stmt.expr){
        reportError(errors, stmt.span, "cannot return a value from a 'void' function, constructor, or destructor");
        ok = false;
    }
    else if(!isVoidLike && !stmt.expr){
        reportError(errors, stmt.span, "non-void function must return a value");
        ok = false;
    }
    if(activeRetType && stmt.expr){
        PropagateFuncCaptureBytes(const_cast<TypeRef*>(activeRetType), stmt.expr, scope, m_symbols);
        const MStringView baseRetTypeName = m_symbols.resolveBaseTypeName(activeRetType->name);
        if(RequiresTCharAdaptation(baseRetTypeName))
            markStringToPathAdaptation(ownerClass, &method, scope, *stmt.expr);
        if(retRefMode != ParamPassingMode::None){
            stmt.wrapReturnNotNull = true;
            b8 isDanglingReturn = false;
            if(stmt.expr->kind == ExprKind::Identifier){
                const ScopedVarInfo* local = FindScopedVar(scope, stmt.expr->text);
                if(local && !local->isParam && m_symbols.resolveRefMode(local->type) == ParamPassingMode::None && local->mode == ParamPassingMode::None)
                    isDanglingReturn = true;
            }
            else if(isRvalueTemporaryExpr(ownerClass, &method, *stmt.expr, scope)){
                isDanglingReturn = true;
            }
            else if(stmt.expr->kind == ExprKind::MemberAccess || stmt.expr->kind == ExprKind::Index){
                const TypeRef* subTypeRef = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
                if(m_symbols.resolveRefMode(subTypeRef) == ParamPassingMode::None){
                    const Expr* root = stmt.expr->lhs;
                    while(root && (root->kind == ExprKind::MemberAccess || root->kind == ExprKind::Index)){
                        const TypeRef* midType = inferExprTypeRef(ownerClass, &method, *root, scope);
                        if(m_symbols.resolveRefMode(midType) != ParamPassingMode::None || (midType && IsBorrowViewType(m_symbols.resolveBaseTypeName(midType->name)))){
                            root = nullptr;
                            break;
                        }
                        root = root->lhs;
                    }
                    if(root && root->kind == ExprKind::Identifier){
                        if(const ScopedVarInfo* rootLocal = FindScopedVar(scope, root->text)){
                            if(
                                !rootLocal->isParam
                                && rootLocal->mode == ParamPassingMode::None
                                && m_symbols.resolveRefMode(rootLocal->type) == ParamPassingMode::None
                                && !IsBorrowViewType(resolveScopedVarBaseType(*rootLocal))
                            ){
                                isDanglingReturn = true;
                            }
                        }
                    }
                }
            }
            if(isDanglingReturn){
                reportError(errors, stmt.span, "cannot return a reference to a temporary or stack-local variable");
                ok = false;
            }
            if(
                m_symbols.findInterface(baseRetTypeName)
                && !verifyConcreteClassImplementsInterface(ownerClass, method, *stmt.expr, activeRetType, scope, stmt.span, "cannot return concrete class for interface reference return type: class does not implement the interface", errors)
            )
                ok = false;
        }
    }
    if(stmt.expr && !checkExpr(ownerClass, scope, retRefMode == ParamPassingMode::Mut, method, *stmt.expr, errors))
        ok = false;
    if(stmt.expr && IsEscapingMutCaptureLambda(stmt.expr, scope)){
        reportError(errors, stmt.span, "cannot return lambda with 'mut' capture; mutable reference captures cannot escape their enclosing scope");
        ok = false;
    }
    return ok;
}

b8 TypeChecker::checkStatement(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors){
    b8 ok = true;

    auto checkLoopOrParallelBody = [&](const b8 isPar) -> b8{
        const u32 savedLoop = m_loopDepth;
        const u32 savedSwitch = m_switchDepth;
        const b8 savedParScratch = m_activeParallelBlockUsesScratch;
        if(isPar){
            m_loopDepth = 0u;
            m_switchDepth = 0u;
            m_activeParallelBlockUsesScratch = false;
            ++m_parallelDepth;
        }
        else{
            ++m_loopDepth;
        }
        const b8 bodyOk = checkStatementList(ownerClass, method, stmt.statements, scope, errors);
        if(isPar){
            --m_parallelDepth;
            stmt.usesScratchArena = m_activeParallelBlockUsesScratch;
            m_activeParallelBlockUsesScratch = savedParScratch;
        }
        m_loopDepth = savedLoop;
        m_switchDepth = savedSwitch;
        return bodyOk;
    };

    switch(stmt.kind){
    case StmtKind::Block:{
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::ParallelBlock:{
        method.usesTaskScope = true;
        if(!checkLoopOrParallelBody(true))
            ok = false;
        break;
    }
    case StmtKind::VarDecl:{
        if(!checkVarDeclStatement(ownerClass, method, stmt, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::ExprStmt:{
        if(IsParallelWaitCallExpr(stmt.expr)){
            method.usesTaskScope = true;
            if(m_parallelDepth > 0u){
                reportError(errors, stmt.span, "'wait' cannot be used inside a 'parallel' or 'parallel_for' construct");
                ok = false;
            }
            if(!stmt.expr->callTemplateArgs.empty()){
                reportError(errors, stmt.span, "'wait' does not accept template arguments");
                ok = false;
            }
            if(stmt.expr->arguments.empty()){
                reportError(errors, stmt.span, "'wait' requires at least one 'parallel' task name argument");
                ok = false;
            }
            break;
        }
        if(stmt.expr && !checkExpr(ownerClass, scope, false, method, *stmt.expr, errors))
            ok = false;
        break;
    }
    case StmtKind::Return:{
        if(!checkReturnStatement(ownerClass, scope, method, stmt, errors))
            ok = false;
        break;
    }
    case StmtKind::Break:{
        if(m_loopDepth == 0u && m_switchDepth == 0u){
            reportError(errors, stmt.span, "'break' cannot be used outside of a sequential loop or 'switch'");
            ok = false;
        }
        break;
    }
    case StmtKind::Continue:{
        if(m_loopDepth == 0u){
            reportError(errors, stmt.span, "'continue' cannot be used outside of a sequential loop");
            ok = false;
        }
        break;
    }
    case StmtKind::ParallelCall:{
        method.usesTaskScope = true;
        const b8 savedParScratch = m_activeParallelBlockUsesScratch;
        m_activeParallelBlockUsesScratch = false;
        ++m_parallelDepth;
        if(stmt.expr && !checkExpr(ownerClass, scope, false, method, *stmt.expr, errors))
            ok = false;
        --m_parallelDepth;
        stmt.usesScratchArena = m_activeParallelBlockUsesScratch;
        m_activeParallelBlockUsesScratch = savedParScratch || stmt.usesScratchArena;
        break;
    }
    case StmtKind::If:{
        if(stmt.condition && !checkExpr(ownerClass, scope, false, method, *stmt.condition, errors))
            ok = false;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        if(!stmt.elseStatements.empty() && !checkStatementList(ownerClass, method, stmt.elseStatements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::Switch:{
        if(stmt.expr){
            if(!checkExpr(ownerClass, scope, false, method, *stmt.expr, errors))
                ok = false;
            const TypeRef* subjTypeRef = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
            const MStringView subjectType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
            const b8 isNonSwitchableBuiltin = IsFloatScalarType(subjectType) || subjectType == "void" || SymbolTable::IsSimdType(subjectType) || SymbolTable::IsUnalignedVectorType(subjectType) || FindBuiltinGenericType(subjectType) != nullptr;
            if(!subjectType.empty() && (isNonSwitchableBuiltin || (subjTypeRef && subjTypeRef->isFuncType))){
                reportError(errors, stmt.span, "invalid 'switch' subject type; floating-point, SIMD, container, callable, and 'void' types cannot be switched on");
                ok = false;
            }
            if(SymbolTable::IsStringSwitchType(subjectType)){
                stmt.lowerSwitchToIfChain = true;
            }
            else{
                const ClassDecl* switchCls = nullptr;
                const MethodDecl* eqOp = resolveCustomOperator(ownerClass, &method, subjTypeRef, subjectType, "==", 1u, true, &switchCls);
                if(switchCls){
                    if(!eqOp){
                        reportError(errors, stmt.span, "switch on custom class requires 'operator==' to be defined on the class");
                        ok = false;
                    }
                    stmt.lowerSwitchToIfChain = true;
                }
                else if(subjTypeRef && m_symbols.findInterface(subjectType)){
                    if(!eqOp){
                        reportError(errors, stmt.span, "switch on interface requires 'operator==' to be declared on the interface");
                        ok = false;
                    }
                    stmt.lowerSwitchToIfChain = true;
                }
                else if(FindTemplateParam(ownerClass, &method, subjectType)){
                    if(!eqOp){
                        reportError(errors, stmt.span, "switch on template parameter requires 'operator==' in its interface bounds");
                        ok = false;
                    }
                    stmt.lowerSwitchToIfChain = true;
                }
                else if(eqOp){
                    stmt.lowerSwitchToIfChain = true;
                }
            }
        }
        ++m_switchDepth;
        for(usize i = 0u; i < stmt.switchCases.size(); ++i){
            SwitchCase& sc = stmt.switchCases[i];
            if(sc.matchExpr){
                if(!checkExpr(ownerClass, scope, false, method, *sc.matchExpr, errors))
                    ok = false;
                if(sc.matchExpr->kind == ExprKind::StringLiteral)
                    stmt.lowerSwitchToIfChain = true;
                for(usize j = 0u; j < i; ++j){
                    const SwitchCase& prevSc = stmt.switchCases[j];
                    if(prevSc.isDefault || !prevSc.matchExpr)
                        continue;
                    b8 isDup = false;
                    if(prevSc.matchExpr->kind == sc.matchExpr->kind){
                        if(IsLiteralExprKind(sc.matchExpr->kind) || sc.matchExpr->kind == ExprKind::Identifier){
                            isDup = (prevSc.matchExpr->text == sc.matchExpr->text);
                        }
                        else if(sc.matchExpr->kind == ExprKind::ScopeAccess && prevSc.matchExpr->scopeChain.size() == 2u && sc.matchExpr->scopeChain.size() == 2u){
                            isDup = (prevSc.matchExpr->scopeChain[0] == sc.matchExpr->scopeChain[0]
                                && prevSc.matchExpr->scopeChain[1] == sc.matchExpr->scopeChain[1]);
                        }
                    }
                    if(!isDup && !stmt.lowerSwitchToIfChain && sc.matchExpr->kind != ExprKind::StringLiteral && prevSc.matchExpr->kind != ExprKind::StringLiteral){
                        i64 prevConst = 0;
                        i64 curConst = 0;
                        if(TryEvaluateExprConstant(prevSc.matchExpr, m_symbols, 0u, nullptr, prevConst) && TryEvaluateExprConstant(sc.matchExpr, m_symbols, 0u, nullptr, curConst)){
                            const b8 prevIsBool = (prevSc.matchExpr->kind == ExprKind::BoolLiteral);
                            const b8 curIsBool = (sc.matchExpr->kind == ExprKind::BoolLiteral);
                            if(prevIsBool == curIsBool && prevConst == curConst)
                                isDup = true;
                        }
                    }
                    if(isDup){
                        reportError(errors, sc.matchExpr->span, "duplicate 'case' value in 'switch' statement");
                        ok = false;
                        break;
                    }
                }
            }
            if(!checkStatementList(ownerClass, method, sc.statements, scope, errors))
                ok = false;
        }
        --m_switchDepth;
        break;
    }
    case StmtKind::ForRange:
    case StmtKind::ParallelForRange:{
        const b8 isPar = (stmt.kind == StmtKind::ParallelForRange);
        if(isPar)
            method.usesTaskScope = true;
        if(!stmt.name.empty() && FindScopedVar(scope, stmt.name)){
            reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
            ok = false;
        }
        if(stmt.expr && !checkExpr(ownerClass, scope, stmt.rangeMode == ParamPassingMode::Mut, method, *stmt.expr, errors))
            ok = false;
        const TypeRef* elemTypeRef = nullptr;
        MStringView resolvedElemName;
        if(stmt.varType){
            if(stmt.expr){
                const TypeRef* containerType = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
                if(isPar && containerType){
                    const MStringView containerBaseName = m_symbols.resolveBaseTypeName(containerType->name);
                    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(containerBaseName); genericInfo && genericInfo->isNonRandomAccessContainer){
                        reportError(errors, stmt.span, "range-based 'parallel_for' requires a random-access indexable container (such as 'Vector', 'Array', or 'Span')");
                        ok = false;
                    }
                }
                if(containerType && !containerType->templateArgs.empty() && containerType->templateArgs[0].type){
                    elemTypeRef = m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type);
                    if(elemTypeRef){
                        if(stmt.rangeMode == ParamPassingMode::Mut && m_symbols.resolveRefMode(elemTypeRef) == ParamPassingMode::In){
                            reportError(errors, stmt.span, "cannot iterate with 'mut' over a container of read-only 'in' references");
                            ok = false;
                        }
                        resolvedElemName = m_symbols.resolveBaseTypeName(elemTypeRef->name);
                        if(stmt.varType->isAuto){
                            if(elemTypeRef->templateArgs.empty() && !elemTypeRef->isFuncType && SymbolTable::IsSimdType(resolvedElemName)){
                                stmt.varType->isAuto = false;
                                stmt.varType->name = resolvedElemName;
                            }
                        }
                        else if(elemTypeRef->refMode != ParamPassingMode::None && stmt.varType->refMode == ParamPassingMode::None){
                            stmt.varType->refMode = elemTypeRef->refMode;
                        }
                    }
                }
            }
            if(!checkTypeRef(*stmt.varType, ownerClass, &method, errors))
                ok = false;
        }
        const usize saved = scope.size();
        ScopedVarInfo loopVar;
        loopVar.name = stmt.name;
        loopVar.type = elemTypeRef ? const_cast<TypeRef*>(elemTypeRef) : stmt.varType;
        loopVar.inferredTypeName = resolvedElemName;
        loopVar.mode = stmt.rangeMode;
        loopVar.isConst = (stmt.rangeMode == ParamPassingMode::In);
        loopVar.isParam = true;
        scope.push_back(loopVar);
        if(!checkLoopOrParallelBody(isPar))
            ok = false;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::ForIndex:
    case StmtKind::ParallelForIndex:{
        const b8 isPar = (stmt.kind == StmtKind::ParallelForIndex);
        if(isPar){
            method.usesTaskScope = true;
            const b8 validInit = stmt.initStmt && stmt.initStmt->kind == StmtKind::VarDecl && (stmt.initStmt->expr != nullptr || stmt.initStmt->isCtorInit);
            const b8 validCond = stmt.condition && stmt.condition->kind == ExprKind::Binary
                && (stmt.condition->op == TokenType::Less || stmt.condition->op == TokenType::LessEqual)
                && stmt.condition->lhs != nullptr
                && stmt.condition->lhs->kind == ExprKind::Identifier
                && stmt.initStmt != nullptr
                && stmt.condition->lhs->text == stmt.initStmt->name
                && stmt.condition->rhs != nullptr;
            b8 validStep = false;
            if(stmt.stepExpr && stmt.initStmt){
                const MStringView idxName = stmt.initStmt->name;
                if(stmt.stepExpr->kind == ExprKind::Unary && stmt.stepExpr->op == TokenType::PlusPlus && stmt.stepExpr->lhs && stmt.stepExpr->lhs->kind == ExprKind::Identifier && stmt.stepExpr->lhs->text == idxName){
                    validStep = true;
                }
                else if(stmt.stepExpr->kind == ExprKind::Binary && stmt.stepExpr->lhs && stmt.stepExpr->lhs->kind == ExprKind::Identifier && stmt.stepExpr->lhs->text == idxName && stmt.stepExpr->rhs){
                    const Expr* stepAmountExpr = nullptr;
                    if(stmt.stepExpr->op == TokenType::PlusEqual){
                        stepAmountExpr = stmt.stepExpr->rhs;
                    }
                    else if(stmt.stepExpr->op == TokenType::Equal && stmt.stepExpr->rhs->kind == ExprKind::Binary && stmt.stepExpr->rhs->op == TokenType::Plus && stmt.stepExpr->rhs->lhs && stmt.stepExpr->rhs->rhs){
                        if(stmt.stepExpr->rhs->lhs->kind == ExprKind::Identifier && stmt.stepExpr->rhs->lhs->text == idxName)
                            stepAmountExpr = stmt.stepExpr->rhs->rhs;
                        else if(stmt.stepExpr->rhs->rhs->kind == ExprKind::Identifier && stmt.stepExpr->rhs->rhs->text == idxName)
                            stepAmountExpr = stmt.stepExpr->rhs->lhs;
                    }
                    if(stepAmountExpr){
                        i64 constStep = 1;
                        validStep = !TryEvaluateExprConstant(stepAmountExpr, m_symbols, 0u, nullptr, constStep) || constStep > 0;
                    }
                }
            }
            if(!validInit || !validCond || !validStep){
                reportError(errors, stmt.span, "indexed 'parallel_for' requires canonical bounds ('parallel_for(usize i = start; i < end; ++i)')");
                ok = false;
            }
        }
        const usize saved = scope.size();
        if(stmt.initStmt && !checkStatement(ownerClass, method, *stmt.initStmt, scope, errors))
            ok = false;
        if(isPar && saved < scope.size()){
            const MStringView idxType = resolveScopedVarBaseType(scope[saved]);
            if(!idxType.empty() && !IsIntegerScalarType(idxType) && !FindTemplateParam(ownerClass, &method, idxType)){
                reportError(errors, stmt.span, "indexed 'parallel_for' requires an integer loop variable type");
                ok = false;
            }
        }
        if(isPar)
            ++m_parallelDepth;
        if(stmt.condition && !checkExpr(ownerClass, scope, false, method, *stmt.condition, errors))
            ok = false;
        if(stmt.stepExpr && !checkExpr(ownerClass, scope, false, method, *stmt.stepExpr, errors))
            ok = false;
        if(isPar)
            --m_parallelDepth;
        if(isPar){
            for(usize i = saved; i < scope.size(); ++i)
                scope[i].isConst = true;
        }
        if(!checkLoopOrParallelBody(isPar))
            ok = false;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::While:
    case StmtKind::DoWhile:{
        if(stmt.condition && !checkExpr(ownerClass, scope, false, method, *stmt.condition, errors))
            ok = false;
        ++m_loopDepth;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        --m_loopDepth;
        break;
    }
    case StmtKind::NativeBlock:{
        for(usize i = 0u; i < stmt.nativeBindings.size(); ++i){
            const NativeBinding& binding = stmt.nativeBindings[i];
            for(usize j = 0u; j < i; ++j){
                if(!binding.aliasName.empty() && stmt.nativeBindings[j].aliasName == binding.aliasName){
                    reportError(errors, binding.span, "duplicate binding name in 'native(...)' capture list");
                    ok = false;
                    break;
                }
            }
            if(binding.sourceExpr){
                if(binding.mode == ParamPassingMode::Mut)
                    binding.sourceExpr->isMutArg = true;
                if(!checkExpr(ownerClass, scope, binding.mode == ParamPassingMode::Mut, method, *binding.sourceExpr, errors))
                    ok = false;
            }
        }
        break;
    }
    case StmtKind::Diagnostic:{
        for(usize i = 0u; i < stmt.arguments.size(); ++i){
            Expr* arg = stmt.arguments[i];
            if(!arg)
                continue;
            if(!checkExpr(ownerClass, scope, false, method, *arg, errors))
                ok = false;
            const b8 isAfterCond = (stmt.diagnosticKind != DiagnosticKind::Assert) || (i >= 1u);
            if(isAfterCond)
                markStringToPathAdaptation(ownerClass, &method, scope, *arg);
        }
        break;
    }
    default:
        break;
    }

    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

