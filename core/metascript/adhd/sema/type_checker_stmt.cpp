// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeChecker::statementsContainReturnOrNative(const MVector<Stmt*>& stmts)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(stmt->kind == StmtKind::Return || stmt->kind == StmtKind::NativeBlock)
            return true;
        if(statementsContainReturnOrNative(stmt->statements) || statementsContainReturnOrNative(stmt->elseStatements))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(statementsContainReturnOrNative(sc.statements))
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
        if(!param.name.empty() && findScopedVar(scope, param.name)){
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
        if(!statementsContainReturnOrNative(method.body)){
            reportError(errors, method.span, "non-void function or method must contain a 'return' statement");
            ok = false;
        }
    }
    return ok;
}

b8 TypeChecker::checkStatementList(
    ClassDecl* ownerClass,
    MethodDecl& method,
    MVector<Stmt*>& statements,
    MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
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

b8 TypeChecker::checkVarDeclStatement(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Stmt& stmt,
    MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
    b8 ok = true;
    if(!stmt.name.empty() && findScopedVar(scope, stmt.name)){
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
        if(!stmt.expr && !stmt.isCtorInit && (varRefMode != ParamPassingMode::None || !m_symbols.isSelfDefaultedType(stmt.varType->name))){
            reportError(errors, stmt.span, "uninitialized variable declaration is prohibited in adhd; every local variable must be initialized at declaration");
            ok = false;
        }
        if(varRefMode == ParamPassingMode::None && SymbolTable::isArenaBackedContainer(baseVarTypeName)){
            stmt.usesScratchArena = true;
            method.usesScratchArena = true;
        }
        propagateFuncCaptureBytes(stmt.varType, stmt.expr, scope, m_symbols);
        if(baseVarTypeName == "Path"){
            if(stmt.expr)
                markStringToPathAdaptation(ownerClass, method, *stmt.expr, scope);
            if(stmt.isCtorInit){
                for(Expr* arg : stmt.arguments){
                    if(arg)
                        markStringToPathAdaptation(ownerClass, method, *arg, scope);
                }
            }
        }
    }

    if(stmt.expr){
        const b8 isMutRefBinding = (varRefMode == ParamPassingMode::Mut);
        if(!checkExpr(ownerClass, method, *stmt.expr, scope, isMutRefBinding, errors))
            ok = false;
        if(stmt.varType && stmt.varType->isAuto && !stmt.expr->resolvedSimdType.empty()){
            stmt.varType->isAuto = false;
            stmt.varType->name = stmt.expr->resolvedSimdType;
        }
    }

    const ClassDecl* ctorCls = nullptr;
    const MethodDecl* matchedCtor = nullptr;
    const TypeRef* resolvedCtorVarType = stmt.varType ? m_symbols.unwrapTypeAlias(stmt.varType) : nullptr;
    if(stmt.isCtorInit && stmt.varType){
        const MStringView baseVarTypeName = m_symbols.resolveBaseTypeName(stmt.varType->name);
        ctorCls = m_symbols.findClass(baseVarTypeName);
        if(ctorCls){
            matchedCtor = findClassConstructor(*ctorCls, stmt.arguments.size(), ownerClass != ctorCls);
            if(matchedCtor && ownerClass != ctorCls && matchedCtor->access != AccessSpecifier::Public){
                reportError(errors, stmt.span, "cannot call 'private' or 'protected' constructor outside of its declaring class");
                ok = false;
            }
        }
    }
    if(!checkCallableArguments(
        ownerClass,
        method,
        stmt.arguments,
        scope,
        matchedCtor,
        ctorCls,
        resolvedCtorVarType,
        nullptr,
        nullptr,
        nullptr,
        MStringView{},
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
            info.type = const_cast<TypeRef*>(exprTypeRef);
        }
    }
    info.mode = varRefMode;
    info.isConst = stmt.isConst || (varRefMode == ParamPassingMode::In);
    info.isParam = false;
    if(stmt.expr && stmt.expr->kind == ExprKind::Lambda)
        info.lambdaExpr = stmt.expr;
    scope.push_back(info);
    return ok;
}

b8 TypeChecker::checkReturnStatement(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Stmt& stmt,
    const MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
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
        propagateFuncCaptureBytes(const_cast<TypeRef*>(activeRetType), stmt.expr, scope, m_symbols);
        const MStringView baseRetTypeName = m_symbols.resolveBaseTypeName(activeRetType->name);
        if(baseRetTypeName == "Path")
            markStringToPathAdaptation(ownerClass, method, *stmt.expr, scope);
        if(retRefMode != ParamPassingMode::None){
            stmt.wrapReturnNotNull = true;
            if(m_symbols.findInterface(baseRetTypeName)
                && !verifyConcreteClassImplementsInterface(
                    ownerClass,
                    method,
                    *stmt.expr,
                    activeRetType,
                    scope,
                    stmt.span,
                    "cannot return concrete class for interface reference return type: class does not implement the interface",
                    errors
                ))
            {
                ok = false;
            }
        }
    }
    if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, retRefMode == ParamPassingMode::Mut, errors))
        ok = false;
    if(stmt.expr && isEscapingMutCaptureLambda(stmt.expr, scope)){
        reportError(errors, stmt.span, "cannot return lambda with 'mut' capture; mutable reference captures cannot escape their enclosing scope");
        ok = false;
    }
    return ok;
}

b8 TypeChecker::checkStatement(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Stmt& stmt,
    MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
    b8 ok = true;

    switch(stmt.kind){
    case StmtKind::Block:{
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::ParallelBlock:{
        method.usesTaskScope = true;
        const u32 savedLoop = m_loopDepth;
        const u32 savedSwitch = m_switchDepth;
        m_loopDepth = 0u;
        m_switchDepth = 0u;
        ++m_parallelDepth;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        --m_parallelDepth;
        m_loopDepth = savedLoop;
        m_switchDepth = savedSwitch;
        break;
    }
    case StmtKind::VarDecl:{
        if(!checkVarDeclStatement(ownerClass, method, stmt, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::ExprStmt:{
        if(stmt.expr && stmt.expr->kind == ExprKind::Call
            && stmt.expr->lhs && stmt.expr->lhs->kind == ExprKind::Identifier
            && stmt.expr->lhs->text == "wait")
        {
            method.usesTaskScope = true;
            if(m_parallelDepth > 0u){
                reportError(errors, stmt.span, "'wait' cannot be used inside a 'parallel' or 'parallel_for' construct");
                ok = false;
            }
            if(!stmt.expr->callTemplateArgs.empty()){
                reportError(errors, stmt.span, "'wait' does not accept template arguments");
                ok = false;
            }
            break;
        }
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, false, errors))
            ok = false;
        break;
    }
    case StmtKind::Return:{
        if(!checkReturnStatement(ownerClass, method, stmt, scope, errors))
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
        ++m_parallelDepth;
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, false, errors))
            ok = false;
        --m_parallelDepth;
        break;
    }
    case StmtKind::If:{
        if(stmt.condition && !checkExpr(ownerClass, method, *stmt.condition, scope, false, errors))
            ok = false;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        if(!stmt.elseStatements.empty() && !checkStatementList(ownerClass, method, stmt.elseStatements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::Switch:{
        if(stmt.expr){
            if(!checkExpr(ownerClass, method, *stmt.expr, scope, false, errors))
                ok = false;
            const MStringView subjectType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
            if(SymbolTable::isStringSwitchType(subjectType)){
                stmt.lowerSwitchToIfChain = true;
            }
            else if(const ClassDecl* switchCls = m_symbols.findClass(subjectType)){
                b8 hasEq = false;
                for(const MethodDecl* m : switchCls->methods){
                    if(m && m->isOperator && m->name == "=="){
                        hasEq = true;
                        break;
                    }
                }
                if(!hasEq){
                    reportError(errors, stmt.span, "switch on custom class requires 'operator==' to be defined on the class");
                    ok = false;
                }
                stmt.lowerSwitchToIfChain = true;
            }
        }
        ++m_switchDepth;
        for(SwitchCase& sc : stmt.switchCases){
            if(sc.matchExpr){
                if(!checkExpr(ownerClass, method, *sc.matchExpr, scope, false, errors))
                    ok = false;
                if(sc.matchExpr->kind == ExprKind::StringLiteral)
                    stmt.lowerSwitchToIfChain = true;
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
        if(!stmt.name.empty() && findScopedVar(scope, stmt.name)){
            reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
            ok = false;
        }
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, stmt.rangeMode == ParamPassingMode::Mut, errors))
            ok = false;
        const TypeRef* elemTypeRef = nullptr;
        MStringView resolvedElemName;
        if(stmt.varType){
            if(stmt.expr){
                const TypeRef* containerType = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
                if(containerType && !containerType->templateArgs.empty() && containerType->templateArgs[0].type){
                    elemTypeRef = m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type);
                    if(elemTypeRef){
                        resolvedElemName = m_symbols.resolveBaseTypeName(elemTypeRef->name);
                        if(stmt.varType->isAuto){
                            if(elemTypeRef->templateArgs.empty() && !elemTypeRef->isFuncType && SymbolTable::isSimdType(resolvedElemName)){
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
        const u32 savedLoop = m_loopDepth;
        const u32 savedSwitch = m_switchDepth;
        if(isPar){
            m_loopDepth = 0u;
            m_switchDepth = 0u;
            ++m_parallelDepth;
        }
        else{
            ++m_loopDepth;
        }
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        if(isPar)
            --m_parallelDepth;
        m_loopDepth = savedLoop;
        m_switchDepth = savedSwitch;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::ForIndex:
    case StmtKind::ParallelForIndex:{
        const b8 isPar = (stmt.kind == StmtKind::ParallelForIndex);
        if(isPar)
            method.usesTaskScope = true;
        const usize saved = scope.size();
        if(stmt.initStmt && !checkStatement(ownerClass, method, *stmt.initStmt, scope, errors))
            ok = false;
        if(stmt.condition && !checkExpr(ownerClass, method, *stmt.condition, scope, false, errors))
            ok = false;
        if(stmt.stepExpr && !checkExpr(ownerClass, method, *stmt.stepExpr, scope, false, errors))
            ok = false;
        const u32 savedLoop = m_loopDepth;
        const u32 savedSwitch = m_switchDepth;
        if(isPar){
            for(usize i = saved; i < scope.size(); ++i)
                scope[i].isConst = true;
            m_loopDepth = 0u;
            m_switchDepth = 0u;
            ++m_parallelDepth;
        }
        else{
            ++m_loopDepth;
        }
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        if(isPar)
            --m_parallelDepth;
        m_loopDepth = savedLoop;
        m_switchDepth = savedSwitch;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::While:
    case StmtKind::DoWhile:{
        if(stmt.condition && !checkExpr(ownerClass, method, *stmt.condition, scope, false, errors))
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
                if(!checkExpr(ownerClass, method, *binding.sourceExpr, scope, binding.mode == ParamPassingMode::Mut, errors))
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
            if(!checkExpr(ownerClass, method, *arg, scope, false, errors))
                ok = false;
            const b8 isAfterCond = (stmt.diagnosticKind != DiagnosticKind::Assert) || (i >= 1u);
            if(isAfterCond)
                markStringToPathAdaptation(ownerClass, method, *arg, scope);
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
