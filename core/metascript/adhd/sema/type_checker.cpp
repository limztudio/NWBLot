// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsCompoundOrSimpleAssignment(const TokenType::Enum op)noexcept{
    return op == TokenType::Equal
        || op == TokenType::PlusEqual
        || op == TokenType::MinusEqual
        || op == TokenType::StarEqual
        || op == TokenType::SlashEqual
        || op == TokenType::PercentEqual
        || op == TokenType::AmpersandEqual
        || op == TokenType::PipeEqual
        || op == TokenType::CaretEqual
        || op == TokenType::LessLessEqual
        || op == TokenType::GreaterGreaterEqual;
}

[[nodiscard]] bool IsNonThreadSafeContainer(const MStringView typeName)noexcept{
    return typeName == "Vector"
        || typeName == "FixedVector"
        || typeName == "ForwardList"
        || typeName == "List"
        || typeName == "Deque"
        || typeName == "Set"
        || typeName == "HashSet"
        || typeName == "Map"
        || typeName == "HashMap"
        || typeName == "Queue"
        || typeName == "String";
}

[[nodiscard]] bool CheckSwizzlePattern(const MStringView text, const u32 maxLanes, bool& outIsPattern, bool& outValidLanes)noexcept{
    outIsPattern = false;
    outValidLanes = true;
    if(text.empty() || text.size() > 4u)
        return false;

    bool usesXYZW = false;
    bool usesRGBA = false;
    u32 highestLane = 0u;

    for(usize i = 0u; i < text.size(); ++i){
        const MChar c = text[i];
        u32 lane = 0u;
        if(c == 'x' || c == 'y' || c == 'z' || c == 'w'){
            usesXYZW = true;
            lane = (c == 'x') ? 0u : (c == 'y') ? 1u : (c == 'z') ? 2u : 3u;
        }
        else if(c == 'r' || c == 'g' || c == 'b' || c == 'a'){
            usesRGBA = true;
            lane = (c == 'r') ? 0u : (c == 'g') ? 1u : (c == 'b') ? 2u : 3u;
        }
        else{
            return false;
        }

        if(lane >= highestLane)
            highestLane = lane + 1u;
    }

    if(usesXYZW && usesRGBA){
        outIsPattern = true;
        outValidLanes = false;
        return true;
    }

    outIsPattern = true;
    outValidLanes = (highestLane <= maxLanes);
    return true;
}

void CollectAssignedThisMembers(const MVector<Stmt*>& stmts, MVector<MStringView>& outMembers, MVector<SourceSpan>& outSpans){
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(stmt->kind == StmtKind::ExprStmt && stmt->expr && stmt->expr->kind == ExprKind::Binary && stmt->expr->op == TokenType::Equal){
            if(stmt->expr->lhs && stmt->expr->lhs->kind == ExprKind::ThisMember){
                outMembers.push_back(stmt->expr->lhs->text);
                outSpans.push_back(stmt->expr->lhs->span);
            }
        }
        if(!stmt->statements.empty())
            CollectAssignedThisMembers(stmt->statements, outMembers, outSpans);
        if(!stmt->elseStatements.empty())
            CollectAssignedThisMembers(stmt->elseStatements, outMembers, outSpans);
        for(const SwitchCase& sc : stmt->switchCases)
            CollectAssignedThisMembers(sc.statements, outMembers, outSpans);
    }
}

[[nodiscard]] bool ExprReferencesIdentifier(const Expr* expr, const MStringView varName)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Identifier && expr->text == varName)
        return true;
    if(ExprReferencesIdentifier(expr->lhs, varName) || ExprReferencesIdentifier(expr->rhs, varName))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprReferencesIdentifier(arg, varName))
            return true;
    }
    return false;
}

[[nodiscard]] bool StmtReferencesIdentifier(const Stmt* stmt, const MStringView varName)noexcept{
    if(!stmt)
        return false;
    if(ExprReferencesIdentifier(stmt->expr, varName)
        || ExprReferencesIdentifier(stmt->condition, varName)
        || ExprReferencesIdentifier(stmt->stepExpr, varName))
    {
        return true;
    }
    for(const Expr* arg : stmt->arguments){
        if(ExprReferencesIdentifier(arg, varName))
            return true;
    }
    for(const Stmt* child : stmt->statements){
        if(StmtReferencesIdentifier(child, varName))
            return true;
    }
    for(const Stmt* child : stmt->elseStatements){
        if(StmtReferencesIdentifier(child, varName))
            return true;
    }
    for(const SwitchCase& sc : stmt->switchCases){
        if(ExprReferencesIdentifier(sc.matchExpr, varName))
            return true;
        for(const Stmt* child : sc.statements){
            if(StmtReferencesIdentifier(child, varName))
                return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TypeChecker::checkTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    bool ok = true;

    if(!m_symbols.build(unit, errors))
        ok = false;

    if(!linkOutOfLineMethods(unit, errors))
        ok = false;

    for(TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(!checkTemplateParamBounds(aliasDecl->templateParams, errors))
            ok = false;
        if(aliasDecl->aliasedType && !checkTypeRef(*aliasDecl->aliasedType, nullptr, nullptr, errors))
            ok = false;
    }

    for(InterfaceDecl* iface : unit.interfaces){
        if(!checkInterfaceDecl(*iface, errors))
            ok = false;
    }

    for(ClassDecl* classDecl : unit.classes){
        if(!checkClassDecl(*classDecl, errors))
            ok = false;
    }

    for(MethodDecl* fn : unit.freeFunctions){
        if(!checkTemplateParamBounds(fn->templateParams, errors))
            ok = false;
        if(fn->returnType && !checkTypeRef(*fn->returnType, nullptr, fn, errors))
            ok = false;
        for(const ParamDecl& p : fn->params){
            if(p.type && !checkTypeRef(*p.type, nullptr, fn, errors))
                ok = false;
        }
        if(!fn->hasBody){
            reportError(errors, fn->span, "declared function has no implementation body");
            ok = false;
            continue;
        }
        if(!m_parallelValidator.validateMethod(*fn, errors))
            ok = false;
        if(!checkMethodBody(nullptr, *fn, errors))
            ok = false;
    }

    if(!m_layoutOptimizer.optimizeTranslationUnit(unit, errors))
        ok = false;

    return ok;
}

bool TypeChecker::linkOutOfLineMethods(TranslationUnit& unit, MVector<ParseError>& errors){
    bool ok = true;

    for(MethodDecl* ool : unit.outOfLineMethods){
        ClassDecl* cls = m_symbols.findClass(ool->ownerClassName);
        if(!cls){
            reportError(errors, ool->span, "out-of-line method references unknown class");
            ok = false;
            continue;
        }

        MethodDecl* matched = nullptr;
        for(MethodDecl* m : cls->methods){
            if(m->isConstructor == ool->isConstructor
                && m->isDestructor == ool->isDestructor
                && m->isOperator == ool->isOperator
                && m->name == ool->name
                && m->params.size() == ool->params.size())
            {
                matched = m;
                break;
            }
        }

        if(!matched){
            reportError(errors, ool->span, "out-of-line method is not declared in class");
            ok = false;
            continue;
        }

        if(matched->isConst != ool->isConst){
            reportError(errors, ool->span, "out-of-line method 'const' qualifier does not match class declaration");
            ok = false;
            continue;
        }

        if(!matched->isConstructor && !matched->isDestructor){
            const MStringView declRet = matched->returnType ? matched->returnType->name : MStringView("void");
            const MStringView oolRet = ool->returnType ? ool->returnType->name : MStringView("void");
            if(declRet != oolRet){
                reportError(errors, ool->span, "out-of-line method return type does not match class declaration");
                ok = false;
                continue;
            }
        }

        bool paramsMatch = true;
        for(usize pIdx = 0u; pIdx < matched->params.size(); ++pIdx){
            const ParamDecl& declP = matched->params[pIdx];
            const ParamDecl& oolP = ool->params[pIdx];
            if(declP.mode != oolP.mode || (declP.type && oolP.type && declP.type->name != oolP.type->name)){
                reportError(errors, oolP.span, "out-of-line parameter 'in'/'mut' mode or type does not match class declaration");
                ok = false;
                paramsMatch = false;
                break;
            }
        }
        if(!paramsMatch)
            continue;

        if(matched->hasBody){
            reportError(errors, ool->span, "method already has an inline body in class declaration");
            ok = false;
            continue;
        }

        matched->hasBody = true;
        matched->body = ool->body;
        ool->access = matched->access;
        ool->isOverride = matched->isOverride;
    }

    return ok;
}

bool TypeChecker::checkTemplateParamBounds(const MVector<TemplateParamDecl>& params, MVector<ParseError>& errors){
    bool ok = true;
    for(const TemplateParamDecl& param : params){
        for(const MStringView& boundName : param.interfaceBounds){
            if(m_symbols.findClass(boundName)){
                reportError(errors, param.span, "template parameter bound must be an 'interface', not a concrete 'class'");
                ok = false;
            }
            else if(!m_symbols.findInterface(boundName)){
                reportError(errors, param.span, "unknown interface bound on template parameter");
                ok = false;
            }
        }
    }
    return ok;
}

bool TypeChecker::interfaceInheritsFrom(const InterfaceDecl& iface, const MStringView requiredIface)const noexcept{
    if(iface.name == requiredIface)
        return true;
    for(const MStringView& baseName : iface.baseInterfaces){
        if(baseName == requiredIface)
            return true;
        if(const InterfaceDecl* baseDecl = m_symbols.findInterface(baseName)){
            if(interfaceInheritsFrom(*baseDecl, requiredIface))
                return true;
        }
    }
    return false;
}

bool TypeChecker::classImplementsInterface(const ClassDecl& cls, const MStringView requiredIface)const noexcept{
    for(const MStringView& implName : cls.implementedInterfaces){
        if(implName == requiredIface)
            return true;
        if(const InterfaceDecl* ifaceDecl = m_symbols.findInterface(implName)){
            if(interfaceInheritsFrom(*ifaceDecl, requiredIface))
                return true;
        }
    }
    return false;
}

bool TypeChecker::checkTemplateArgBounds(
    const MVector<TemplateParamDecl>& params,
    const MVector<TypeArg>& args,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    const SourceSpan span,
    MVector<ParseError>& errors
){
    bool ok = true;
    const usize count = params.size() < args.size() ? params.size() : args.size();
    for(usize i = 0u; i < count; ++i){
        const TemplateParamDecl& param = params[i];
        const TypeArg& arg = args[i];
        if(param.interfaceBounds.empty() || !arg.type)
            continue;

        const MStringView argTypeName = arg.type->name;
        bool isDependentParam = false;
        if(enclosingClass){
            for(const TemplateParamDecl& tp : enclosingClass->templateParams){
                if(tp.name == argTypeName){
                    isDependentParam = true;
                    break;
                }
            }
        }
        if(!isDependentParam && enclosingMethod){
            for(const TemplateParamDecl& tp : enclosingMethod->templateParams){
                if(tp.name == argTypeName){
                    isDependentParam = true;
                    break;
                }
            }
        }
        if(isDependentParam)
            continue;

        const ClassDecl* argClass = m_symbols.findClass(argTypeName);
        for(const MStringView& boundName : param.interfaceBounds){
            if(!argClass || !classImplementsInterface(*argClass, boundName)){
                reportError(errors, span, "template type argument does not implement required interface bound");
                ok = false;
            }
        }
    }
    return ok;
}

bool TypeChecker::checkTypeRef(
    const TypeRef& typeRef,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    MVector<ParseError>& errors
){
    bool ok = true;
    if(!typeRef.templateArgs.empty()){
        if(const ClassDecl* targetCls = m_symbols.findClass(typeRef.name)){
            if(!checkTemplateArgBounds(targetCls->templateParams, typeRef.templateArgs, enclosingClass, enclosingMethod, typeRef.span, errors))
                ok = false;
        }
        else if(const InterfaceDecl* targetIface = m_symbols.findInterface(typeRef.name)){
            if(!checkTemplateArgBounds(targetIface->templateParams, typeRef.templateArgs, enclosingClass, enclosingMethod, typeRef.span, errors))
                ok = false;
        }
        else if(const TypeAliasDecl* targetAlias = m_symbols.findTypeAlias(typeRef.name)){
            if(!checkTemplateArgBounds(targetAlias->templateParams, typeRef.templateArgs, enclosingClass, enclosingMethod, typeRef.span, errors))
                ok = false;
        }

        for(const TypeArg& targ : typeRef.templateArgs){
            if(targ.type && !checkTypeRef(*targ.type, enclosingClass, enclosingMethod, errors))
                ok = false;
        }
    }
    return ok;
}

bool TypeChecker::checkInterfaceDecl(InterfaceDecl& iface, MVector<ParseError>& errors){
    bool ok = checkTemplateParamBounds(iface.templateParams, errors);
    for(const MStringView& baseName : iface.baseInterfaces){
        if(m_symbols.findClass(baseName)){
            reportError(errors, iface.span, "'interface' cannot inherit from a concrete 'class'");
            ok = false;
        }
        else if(!m_symbols.findInterface(baseName)){
            reportError(errors, iface.span, "unknown base interface name");
            ok = false;
        }
    }
    return ok;
}

bool TypeChecker::checkClassDecl(ClassDecl& classDecl, MVector<ParseError>& errors){
    bool ok = checkTemplateParamBounds(classDecl.templateParams, errors);

    MVector<MethodDecl*> requiredInterfaceMethods(m_arena);
    for(const MStringView& ifaceName : classDecl.implementedInterfaces){
        if(m_symbols.findClass(ifaceName)){
            reportError(errors, classDecl.span, "concrete class inheritance is prohibited in adhd; classes are implicitly final and may only implement interfaces");
            ok = false;
            continue;
        }
        m_symbols.collectAllInterfaceMethods(ifaceName, requiredInterfaceMethods);
    }

    for(MethodDecl* req : requiredInterfaceMethods){
        MethodDecl* impl = nullptr;
        for(MethodDecl* m : classDecl.methods){
            if(!m->isConstructor && !m->isDestructor && m->name == req->name && m->params.size() == req->params.size()){
                impl = m;
                break;
            }
        }
        if(!impl){
            reportError(errors, classDecl.span, "class does not implement required interface method");
            ok = false;
        }
        else if(!impl->isOverride){
            reportError(errors, impl->span, "method implementing an interface contract must be explicitly marked 'override'");
            ok = false;
        }
    }

    for(MethodDecl* m : classDecl.methods){
        if(m->isOverride){
            bool matchesInterface = false;
            for(const MStringView& ifaceName : classDecl.implementedInterfaces){
                if(!m_symbols.findInterface(ifaceName)){
                    matchesInterface = true;
                    break;
                }
                if(m_symbols.findInterfaceMethod(ifaceName, m->name)){
                    matchesInterface = true;
                    break;
                }
            }
            if(!matchesInterface){
                reportError(errors, m->span, "method marked 'override' does not match any method in an implemented interface");
                ok = false;
            }
        }
    }

    bool hasConstructor = false;
    for(MethodDecl* m : classDecl.methods){
        if(m->isConstructor){
            hasConstructor = true;
            if(m->hasBody && !checkConstructorInitialization(classDecl, *m, errors))
                ok = false;
        }
    }

    for(FieldDecl* field : classDecl.fields){
        if(field->type && !checkTypeRef(*field->type, &classDecl, nullptr, errors))
            ok = false;
        if(field->type && field->type->isFuncType && field->defaultExpr && field->defaultExpr->kind == ExprKind::Lambda)
            field->type->inferredCaptureBytes = field->defaultExpr->lambdaCaptures.empty() ? 0u : 64u;

        const bool isAutoArenaField = field->type && SymbolTable::isArenaBackedContainer(field->type->name);
        const bool hasInlineDefault = (field->defaultExpr != nullptr) || field->hasCtorInit || isAutoArenaField;
        if(!hasInlineDefault && !hasConstructor){
            reportError(errors, field->span, "class member has no inline default value and class defines no constructor to initialize it");
            ok = false;
        }
    }

    for(MethodDecl* m : classDecl.methods){
        if(!checkTemplateParamBounds(m->templateParams, errors))
            ok = false;
        if(m->returnType && !checkTypeRef(*m->returnType, &classDecl, m, errors))
            ok = false;
        for(const ParamDecl& p : m->params){
            if(p.type && !checkTypeRef(*p.type, &classDecl, m, errors))
                ok = false;
        }
        if(!m->hasBody){
            reportError(errors, m->span, "declared class method has no implementation body");
            ok = false;
            continue;
        }
        if(!m_parallelValidator.validateMethod(*m, errors))
            ok = false;
        if(!checkMethodBody(&classDecl, *m, errors))
            ok = false;
    }

    return ok;
}

bool TypeChecker::checkConstructorInitialization(ClassDecl& classDecl, MethodDecl& ctor, MVector<ParseError>& errors){
    bool ok = true;
    MVector<MStringView> assignedMembers(m_arena);
    MVector<SourceSpan> assignedSpans(m_arena);
    __hidden_adhd_type_checker::CollectAssignedThisMembers(ctor.body, assignedMembers, assignedSpans);

    for(usize i = 0u; i < assignedMembers.size(); ++i){
        const FieldDecl* field = findClassField(classDecl, assignedMembers[i]);
        if(!field)
            continue;
        if(field->defaultExpr != nullptr || field->hasCtorInit){
            reportError(errors, assignedSpans[i], "double-initialization is prohibited in adhd; member already has an inline default value");
            ok = false;
        }
    }

    for(const FieldDecl* field : classDecl.fields){
        const bool isAutoArenaField = field->type && SymbolTable::isArenaBackedContainer(field->type->name);
        const bool hasInlineDefault = (field->defaultExpr != nullptr) || field->hasCtorInit || isAutoArenaField;
        if(hasInlineDefault)
            continue;

        bool initializedInCtor = false;
        for(const MStringView& assigned : assignedMembers){
            if(assigned == field->name){
                initializedInCtor = true;
                break;
            }
        }
        if(!initializedInCtor){
            reportError(errors, ctor.span, "constructor must initialize all class members that do not have an inline default value");
            ok = false;
        }
    }

    return ok;
}

bool TypeChecker::checkMethodBody(ClassDecl* ownerClass, MethodDecl& method, MVector<ParseError>& errors){
    if(!method.hasBody)
        return true;

    MVector<ScopedVarInfo> scope(m_arena);
    for(const ParamDecl& param : method.params){
        ScopedVarInfo info;
        info.name = param.name;
        info.type = param.type;
        info.mode = param.mode;
        info.isConst = (param.mode == ParamPassingMode::In);
        info.isParam = true;
        scope.push_back(info);
    }

    return checkStatementList(ownerClass, method, method.body, scope, errors);
}

bool TypeChecker::checkStatementList(
    ClassDecl* ownerClass,
    MethodDecl& method,
    MVector<Stmt*>& statements,
    MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
    const usize savedScopeSize = scope.size();
    bool ok = true;

    for(Stmt* stmt : statements){
        if(!stmt)
            continue;
        if(!checkStatement(ownerClass, method, *stmt, scope, errors))
            ok = false;
    }

    for(usize i = savedScopeSize; i < scope.size(); ++i){
        const ScopedVarInfo& local = scope[i];
        if(local.isParam || local.isConst || (local.type && local.type->refMode != ParamPassingMode::None))
            continue;

        usize lastUseIndex = statements.size();
        for(usize s = 0u; s < statements.size(); ++s){
            if(__hidden_adhd_type_checker::StmtReferencesIdentifier(statements[s], local.name))
                lastUseIndex = s;
        }

        if(lastUseIndex < statements.size()){
            Stmt* lastStmt = statements[lastUseIndex];
            if(lastStmt->kind == StmtKind::ExprStmt && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call){
                for(Expr* arg : lastStmt->expr->arguments){
                    if(arg && arg->kind == ExprKind::Identifier && arg->text == local.name)
                        arg->moveAtLastUse = true;
                }
            }
        }
    }

    while(scope.size() > savedScopeSize)
        scope.pop_back();

    return ok;
}

bool TypeChecker::checkStatement(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Stmt& stmt,
    MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
    bool ok = true;

    switch(stmt.kind){
    case StmtKind::Block:{
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::ParallelBlock:{
        method.usesTaskScope = true;
        ++m_parallelDepth;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        --m_parallelDepth;
        break;
    }
    case StmtKind::VarDecl:{
        if(stmt.varType){
            if(!checkTypeRef(*stmt.varType, ownerClass, &method, errors))
                ok = false;
            if(SymbolTable::isArenaBackedContainer(stmt.varType->name)){
                stmt.usesScratchArena = true;
                method.usesScratchArena = true;
            }
            if(stmt.varType->isFuncType && stmt.expr && stmt.expr->kind == ExprKind::Lambda)
                stmt.varType->inferredCaptureBytes = stmt.expr->lambdaCaptures.empty() ? 0u : 64u;
            if(stmt.varType->name == "Path" && stmt.isCtorInit){
                for(Expr* arg : stmt.arguments){
                    if(arg && arg->kind == ExprKind::StringLiteral)
                        arg->adaptToTChar = true;
                }
            }
        }

        if(stmt.expr){
            const bool isMutRefBinding = stmt.varType && stmt.varType->refMode == ParamPassingMode::Mut;
            if(!checkExpr(ownerClass, method, *stmt.expr, scope, isMutRefBinding, errors))
                ok = false;
        }
        for(Expr* arg : stmt.arguments){
            if(arg && !checkExpr(ownerClass, method, *arg, scope, false, errors))
                ok = false;
        }

        ScopedVarInfo info;
        info.name = stmt.name;
        info.type = stmt.varType;
        info.mode = stmt.varType ? stmt.varType->refMode : ParamPassingMode::None;
        info.isConst = stmt.isConst || (stmt.varType && stmt.varType->refMode == ParamPassingMode::In);
        info.isParam = false;
        scope.push_back(info);
        break;
    }
    case StmtKind::ExprStmt:
    case StmtKind::Return:{
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, false, errors))
            ok = false;
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
            const MStringView subjectType = inferExprTypeName(ownerClass, *stmt.expr, scope);
            if(SymbolTable::isStringSwitchType(subjectType))
                stmt.lowerSwitchToIfChain = true;
        }
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
        break;
    }
    case StmtKind::ForRange:
    case StmtKind::ParallelForRange:{
        const bool isPar = (stmt.kind == StmtKind::ParallelForRange);
        if(isPar)
            method.usesTaskScope = true;
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, stmt.rangeMode == ParamPassingMode::Mut, errors))
            ok = false;
        const usize saved = scope.size();
        ScopedVarInfo loopVar;
        loopVar.name = stmt.name;
        loopVar.type = stmt.varType;
        loopVar.mode = stmt.rangeMode;
        loopVar.isConst = (stmt.rangeMode == ParamPassingMode::In);
        loopVar.isParam = true;
        scope.push_back(loopVar);
        if(isPar)
            ++m_parallelDepth;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        if(isPar)
            --m_parallelDepth;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::ForIndex:
    case StmtKind::ParallelForIndex:{
        const bool isPar = (stmt.kind == StmtKind::ParallelForIndex);
        if(isPar)
            method.usesTaskScope = true;
        const usize saved = scope.size();
        if(stmt.initStmt && !checkStatement(ownerClass, method, *stmt.initStmt, scope, errors))
            ok = false;
        if(stmt.condition && !checkExpr(ownerClass, method, *stmt.condition, scope, false, errors))
            ok = false;
        if(stmt.stepExpr && !checkExpr(ownerClass, method, *stmt.stepExpr, scope, false, errors))
            ok = false;
        if(isPar)
            ++m_parallelDepth;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        if(isPar)
            --m_parallelDepth;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::While:
    case StmtKind::DoWhile:{
        if(stmt.condition && !checkExpr(ownerClass, method, *stmt.condition, scope, false, errors))
            ok = false;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::NativeBlock:{
        for(const NativeBinding& binding : stmt.nativeBindings){
            if(binding.sourceExpr && !checkExpr(ownerClass, method, *binding.sourceExpr, scope, binding.mode == ParamPassingMode::Mut, errors))
                ok = false;
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
            const bool isMessageArg = (stmt.diagnosticKind == DiagnosticKind::Assert) ? (i == 1u) : (i == 0u);
            if(isMessageArg && arg->kind == ExprKind::StringLiteral)
                arg->adaptToTChar = true;
        }
        break;
    }
    default:
        break;
    }

    return ok;
}

bool TypeChecker::checkExpr(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Expr& expr,
    const MVector<ScopedVarInfo>& scope,
    const bool isMutationTarget,
    MVector<ParseError>& errors
){
    bool ok = true;

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::StringLiteral:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot pass literal or temporary to a 'mut' target");
            ok = false;
        }
        break;
    }
    case ExprKind::Identifier:{
        const ScopedVarInfo* local = findScopedVar(scope, expr.text);
        if(local){
            if(!local->isParam && local->mode != ParamPassingMode::None)
                expr.derefNotNull = true;

            if(isMutationTarget){
                if(local->mode == ParamPassingMode::In){
                    reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                    ok = false;
                }
                else if(local->isConst){
                    reportError(errors, expr.span, "cannot mutate 'const' variable");
                    ok = false;
                }
                else if(m_parallelDepth > 0u && local->type && __hidden_adhd_type_checker::IsNonThreadSafeContainer(local->type->name)){
                    reportError(errors, expr.span, "shared 'mut' access to non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                    ok = false;
                }
            }
        }
        else if(ownerClass){
            if(findClassField(*ownerClass, expr.text) || findClassMethod(*ownerClass, expr.text)){
                reportError(errors, expr.span, "class member must be accessed explicitly via 'this.member' in adhd");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::ThisMember:{
        if(!ownerClass){
            reportError(errors, expr.span, "'this' cannot be used in a free function");
            ok = false;
            break;
        }
        if(isMutationTarget){
            if(method.isConst){
                reportError(errors, expr.span, "cannot mutate 'this.member' inside a 'const' method");
                ok = false;
            }
            else if(m_parallelDepth > 0u){
                if(const FieldDecl* field = findClassField(*ownerClass, expr.text)){
                    if(field->type && __hidden_adhd_type_checker::IsNonThreadSafeContainer(field->type->name)){
                        reportError(errors, expr.span, "shared 'mut' access to non-thread-safe member container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                        ok = false;
                    }
                }
            }
        }
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, isMutationTarget, errors))
            ok = false;

        if(expr.lhs){
            if(expr.lhs->derefNotNull){
                expr.lhs->derefNotNull = false;
                expr.isPointerMemberAccess = true;
            }
            const MStringView baseType = inferExprTypeName(ownerClass, *expr.lhs, scope);
            u32 simdLanes = 4u;
            if(SymbolTable::isSimdVectorType(baseType, simdLanes)){
                bool isSwizzlePattern = false;
                bool validLanes = true;
                if(__hidden_adhd_type_checker::CheckSwizzlePattern(expr.text, simdLanes, isSwizzlePattern, validLanes) && isSwizzlePattern){
                    if(!validLanes){
                        reportError(errors, expr.span, "invalid SIMD swizzle component for vector width or mixed xyzw/rgba set");
                        ok = false;
                    }
                    else{
                        expr.isSwizzle = true;
                    }
                }
            }
        }
        break;
    }
    case ExprKind::Index:{
        const u32 savedParallelDepth = m_parallelDepth;
        m_parallelDepth = 0u;
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, isMutationTarget, errors))
            ok = false;
        m_parallelDepth = savedParallelDepth;
        if(expr.rhs && !checkExpr(ownerClass, method, *expr.rhs, scope, false, errors))
            ok = false;
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, false, errors))
            ok = false;

        const MethodDecl* targetCallable = nullptr;
        if(expr.lhs){
            if(expr.lhs->kind == ExprKind::Identifier)
                targetCallable = m_symbols.findFreeFunction(expr.lhs->text);
            else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass)
                targetCallable = findClassMethod(*ownerClass, expr.lhs->text);
            else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
                const MStringView objType = inferExprTypeName(ownerClass, *expr.lhs->lhs, scope);
                if(const ClassDecl* targetCls = m_symbols.findClass(objType))
                    targetCallable = findClassMethod(*targetCls, expr.lhs->text);
                else if(m_symbols.findInterface(objType))
                    targetCallable = m_symbols.findInterfaceMethod(objType, expr.lhs->text);
                else if(__hidden_adhd_type_checker::IsNonThreadSafeContainer(objType) && m_parallelDepth > 0u){
                    const MStringView mName = expr.lhs->text;
                    if(mName == "push_back" || mName == "emplace_back" || mName == "pop_back" || mName == "clear" || mName == "insert" || mName == "erase" || mName == "resize"){
                        reportError(errors, expr.span, "mutating non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers");
                        ok = false;
                    }
                }
            }
        }

        if(targetCallable && !expr.callTemplateArgs.empty()){
            if(!checkTemplateArgBounds(targetCallable->templateParams, expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
                ok = false;
        }
        for(const TypeArg& targ : expr.callTemplateArgs){
            if(targ.type && !checkTypeRef(*targ.type, ownerClass, &method, errors))
                ok = false;
        }

        for(usize i = 0u; i < expr.arguments.size(); ++i){
            Expr* arg = expr.arguments[i];
            if(!arg)
                continue;
            const bool paramIsMut = targetCallable
                && i < targetCallable->params.size()
                && targetCallable->params[i].mode == ParamPassingMode::Mut;
            if(!checkExpr(ownerClass, method, *arg, scope, paramIsMut, errors))
                ok = false;
        }
        break;
    }
    case ExprKind::Unary:{
        const bool mutates = (expr.op == TokenType::PlusPlus || expr.op == TokenType::MinusMinus);
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, mutates, errors))
            ok = false;
        break;
    }
    case ExprKind::Binary:{
        const bool isAssign = __hidden_adhd_type_checker::IsCompoundOrSimpleAssignment(expr.op);
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, isAssign, errors))
            ok = false;
        if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::Identifier && expr.lhs->derefNotNull){
            const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text);
            if(local && local->type && m_symbols.findInterface(local->type->name)){
                expr.lhs->derefNotNull = false;
                expr.isNotNullRebind = true;
            }
        }
        if(expr.rhs && !checkExpr(ownerClass, method, *expr.rhs, scope, false, errors))
            ok = false;
        break;
    }
    case ExprKind::Cast:
    case ExprKind::BitCast:{
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, false, errors))
            ok = false;
        break;
    }
    case ExprKind::Lambda:{
        MVector<ScopedVarInfo> lambdaScope(m_arena);
        for(const LambdaCapture& cap : expr.lambdaCaptures){
            if(!cap.isThis){
                const ScopedVarInfo* outer = findScopedVar(scope, cap.name);
                if(outer && cap.mode == ParamPassingMode::Mut && (outer->mode == ParamPassingMode::In || outer->isConst)){
                    reportError(errors, cap.span, "cannot capture 'in' or 'const' variable as 'mut' in lambda");
                    ok = false;
                }
                ScopedVarInfo capVar;
                capVar.name = cap.name;
                capVar.type = outer ? outer->type : nullptr;
                capVar.mode = cap.mode;
                capVar.isConst = (cap.mode == ParamPassingMode::In);
                capVar.isParam = true;
                lambdaScope.push_back(capVar);
            }
        }
        for(const ParamDecl& p : expr.lambdaParams){
            ScopedVarInfo paramVar;
            paramVar.name = p.name;
            paramVar.type = p.type;
            paramVar.mode = p.mode;
            paramVar.isConst = (p.mode == ParamPassingMode::In);
            paramVar.isParam = true;
            lambdaScope.push_back(paramVar);
        }
        if(!checkStatementList(ownerClass, method, expr.lambdaBody, lambdaScope, errors))
            ok = false;
        break;
    }
    default:
        break;
    }

    return ok;
}

MStringView TypeChecker::inferExprTypeName(
    ClassDecl* ownerClass,
    const Expr& expr,
    const MVector<ScopedVarInfo>& scope
)const noexcept{
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = findScopedVar(scope, expr.text)){
            if(local->type)
                return local->type->name;
        }
    }
    else if(expr.kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = findClassField(*ownerClass, expr.text)){
            if(field->type)
                return field->type->name;
        }
    }
    else if(expr.kind == ExprKind::StringLiteral){
        return "StringView";
    }
    return {};
}

const ScopedVarInfo* TypeChecker::findScopedVar(const MVector<ScopedVarInfo>& scope, const MStringView name)noexcept{
    for(usize i = scope.size(); i > 0u; --i){
        if(scope[i - 1u].name == name)
            return &scope[i - 1u];
    }
    return nullptr;
}

const FieldDecl* TypeChecker::findClassField(const ClassDecl& classDecl, const MStringView name)noexcept{
    for(const FieldDecl* field : classDecl.fields){
        if(field->name == name)
            return field;
    }
    return nullptr;
}

const MethodDecl* TypeChecker::findClassMethod(const ClassDecl& classDecl, const MStringView name)noexcept{
    for(const MethodDecl* m : classDecl.methods){
        if(m->name == name)
            return m;
    }
    return nullptr;
}

void TypeChecker::reportError(MVector<ParseError>& errors, const SourceSpan span, const MStringView message)const{
    errors.push_back(ParseError{span.line, span.column, MString(message.data(), message.size(), m_arena)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

