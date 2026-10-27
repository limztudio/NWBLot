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

[[nodiscard]] bool IsVoidTypeRef(const TypeRef* ref)noexcept{
    if(!ref)
        return true;
    return ref->name == "void"
        && ref->refMode == ParamPassingMode::None
        && !ref->isAuto
        && !ref->isFuncType
        && ref->templateArgs.empty();
}

[[nodiscard]] bool AreTypeRefsEquivalent(const TypeRef* a, const TypeRef* b)noexcept{
    if(a == b)
        return true;
    if(!a || !b)
        return IsVoidTypeRef(a) && IsVoidTypeRef(b);
    if(a->name != b->name
        || a->refMode != b->refMode
        || a->isAuto != b->isAuto
        || a->isFuncType != b->isFuncType
        || a->templateArgs.size() != b->templateArgs.size())
    {
        return false;
    }
    for(usize i = 0u; i < a->templateArgs.size(); ++i){
        const TypeArg& ta = a->templateArgs[i];
        const TypeArg& tb = b->templateArgs[i];
        if(ta.constantText != tb.constantText || !AreTypeRefsEquivalent(ta.type, tb.type))
            return false;
    }
    if(a->isFuncType){
        if(!AreTypeRefsEquivalent(a->funcReturnType, b->funcReturnType)
            || a->funcParams.size() != b->funcParams.size())
        {
            return false;
        }
        for(usize i = 0u; i < a->funcParams.size(); ++i){
            if(a->funcParams[i].mode != b->funcParams[i].mode
                || !AreTypeRefsEquivalent(a->funcParams[i].type, b->funcParams[i].type))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool DoParamListsMatch(const MVector<ParamDecl>& a, const MVector<ParamDecl>& b)noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].mode != b[i].mode || !AreTypeRefsEquivalent(a[i].type, b[i].type))
            return false;
    }
    return true;
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

    for(const EnumDecl* enumDecl : unit.enums){
        if(!enumDecl)
            continue;
        for(usize i = 0u; i < enumDecl->entries.size(); ++i){
            for(usize j = 0u; j < i; ++j){
                if(enumDecl->entries[j].name == enumDecl->entries[i].name){
                    reportError(errors, enumDecl->entries[i].span, "duplicate enumerator name in enum");
                    ok = false;
                    break;
                }
            }
        }
    }

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
        if(fn->returnType){
            if(!checkTypeRef(*fn->returnType, nullptr, fn, errors))
                ok = false;
            if(fn->returnType->refMode == ParamPassingMode::None && m_symbols.findInterface(fn->returnType->name)){
                reportError(errors, fn->span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(const ParamDecl& p : fn->params){
            if(p.type && !checkTypeRef(*p.type, nullptr, fn, errors))
                ok = false;
        }
        if(!fn->hasBody){
            if(!fn->isImported){
                reportError(errors, fn->span, "declared function has no implementation body");
                ok = false;
            }
            continue;
        }
        if(fn->isImported)
            continue;
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
                if(__hidden_adhd_type_checker::DoParamListsMatch(m->params, ool->params))
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
            if(!__hidden_adhd_type_checker::AreTypeRefsEquivalent(matched->returnType, ool->returnType)){
                reportError(errors, ool->span, "out-of-line method return type does not match class declaration");
                ok = false;
                continue;
            }
        }

        bool paramsMatch = true;
        for(usize pIdx = 0u; pIdx < matched->params.size(); ++pIdx){
            const ParamDecl& declP = matched->params[pIdx];
            const ParamDecl& oolP = ool->params[pIdx];
            if(declP.mode != oolP.mode || !__hidden_adhd_type_checker::AreTypeRefsEquivalent(declP.type, oolP.type)){
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

        matched->params = ool->params;
        matched->hasBody = true;
        matched->body = ool->body;
        if(!ool->isImported)
            matched->isImported = false;
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
    if(args.size() != params.size()){
        reportError(errors, span, "template argument count does not match template parameter list");
        ok = false;
    }
    const usize count = params.size() < args.size() ? params.size() : args.size();
    for(usize i = 0u; i < count; ++i){
        const TemplateParamDecl& param = params[i];
        const TypeArg& arg = args[i];
        if(param.constValueType){
            if(arg.type){
                reportError(errors, span, "expected constant value argument for template value parameter");
                ok = false;
            }
            continue;
        }
        if(!arg.type){
            reportError(errors, span, "expected type argument for template type parameter");
            ok = false;
            continue;
        }
        if(param.interfaceBounds.empty())
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
    if(typeRef.isFuncType){
        if(typeRef.funcReturnType){
            if(!checkTypeRef(*typeRef.funcReturnType, enclosingClass, enclosingMethod, errors))
                ok = false;
            if(typeRef.funcReturnType->refMode == ParamPassingMode::None && m_symbols.findInterface(typeRef.funcReturnType->name)){
                reportError(errors, typeRef.span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(const FuncParamType& fp : typeRef.funcParams){
            if(fp.type && !checkTypeRef(*fp.type, enclosingClass, enclosingMethod, errors))
                ok = false;
        }
    }
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
    for(MethodDecl* m : iface.methods){
        if(!m)
            continue;
        if(!checkTemplateParamBounds(m->templateParams, errors))
            ok = false;
        if(m->returnType){
            if(!checkTypeRef(*m->returnType, nullptr, m, errors))
                ok = false;
            if(m->returnType->refMode == ParamPassingMode::None && m_symbols.findInterface(m->returnType->name)){
                reportError(errors, m->span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(const ParamDecl& p : m->params){
            if(p.type && !checkTypeRef(*p.type, nullptr, m, errors))
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
        else{
            if(!impl->isOverride){
                reportError(errors, impl->span, "method implementing an interface contract must be explicitly marked 'override'");
                ok = false;
            }
            if(impl->access != AccessSpecifier::Public){
                reportError(errors, impl->span, "method implementing an interface contract must be 'public'");
                ok = false;
            }
            if(impl->isConst != req->isConst){
                reportError(errors, impl->span, "override method 'const' qualifier does not match interface contract");
                ok = false;
            }
            if(!__hidden_adhd_type_checker::AreTypeRefsEquivalent(impl->returnType, req->returnType)){
                reportError(errors, impl->span, "override method return type does not match interface contract");
                ok = false;
            }
            for(usize pIdx = 0u; pIdx < req->params.size(); ++pIdx){
                const ParamDecl& implP = impl->params[pIdx];
                const ParamDecl& reqP = req->params[pIdx];
                if(implP.mode != reqP.mode || !__hidden_adhd_type_checker::AreTypeRefsEquivalent(implP.type, reqP.type)){
                    reportError(errors, implP.span, "override method parameter 'in'/'mut' mode or type does not match interface contract");
                    ok = false;
                    break;
                }
            }
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
            if(m->hasBody && !m->isImported && !checkConstructorInitialization(classDecl, *m, errors))
                ok = false;
        }
    }

    for(usize i = 0u; i < classDecl.fields.size(); ++i){
        FieldDecl* field = classDecl.fields[i];
        if(!field)
            continue;
        for(usize j = 0u; j < i; ++j){
            if(classDecl.fields[j] && classDecl.fields[j]->name == field->name){
                reportError(errors, field->span, "duplicate class member field name");
                ok = false;
                break;
            }
        }
        if(field->type){
            if(!checkTypeRef(*field->type, &classDecl, nullptr, errors))
                ok = false;
            if(field->type->refMode == ParamPassingMode::None && m_symbols.findInterface(field->type->name)){
                reportError(errors, field->span, "class member of 'interface' type must specify 'in' or 'mut'; interfaces cannot be stored by value");
                ok = false;
            }
        }
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
        if(m->returnType){
            if(!checkTypeRef(*m->returnType, &classDecl, m, errors))
                ok = false;
            if(m->returnType->refMode == ParamPassingMode::None && m_symbols.findInterface(m->returnType->name)){
                reportError(errors, m->span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(const ParamDecl& p : m->params){
            if(p.type && !checkTypeRef(*p.type, &classDecl, m, errors))
                ok = false;
        }
        if(!m->hasBody){
            if(!classDecl.isImported){
                reportError(errors, m->span, "declared class method has no implementation body");
                ok = false;
            }
            continue;
        }
        if(m->isImported)
            continue;
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

    bool ok = true;
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
    return ok;
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
        if(!stmt.name.empty() && findScopedVar(scope, stmt.name)){
            reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
            ok = false;
        }
        if(stmt.varType){
            if(!checkTypeRef(*stmt.varType, ownerClass, &method, errors))
                ok = false;
            if(m_symbols.findInterface(stmt.varType->name)){
                if(stmt.varType->refMode == ParamPassingMode::None){
                    reportError(errors, stmt.span, "local variable of 'interface' type must specify 'in' or 'mut'; interfaces cannot be instantiated by value");
                    ok = false;
                }
                else if(stmt.expr){
                    const MStringView rhsType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
                    if(const ClassDecl* rhsCls = m_symbols.findClass(rhsType)){
                        if(!classImplementsInterface(*rhsCls, stmt.varType->name)){
                            reportError(errors, stmt.span, "cannot bind concrete class to interface reference: class does not implement the interface");
                            ok = false;
                        }
                    }
                }
            }
            if(SymbolTable::isArenaBackedContainer(stmt.varType->name)){
                stmt.usesScratchArena = true;
                method.usesScratchArena = true;
            }
            if(stmt.varType->isFuncType && stmt.expr){
                if(stmt.expr->kind == ExprKind::Lambda){
                    stmt.varType->inferredCaptureBytes = stmt.expr->lambdaCaptures.empty() ? 0u : 64u;
                }
                else if(stmt.expr->kind == ExprKind::Identifier){
                    if(const ScopedVarInfo* rhsVar = findScopedVar(scope, stmt.expr->text)){
                        if(rhsVar->lambdaCaptureBytes > 0u || (rhsVar->type && rhsVar->type->inferredCaptureBytes > 0u))
                            stmt.varType->inferredCaptureBytes = 64u;
                    }
                }
            }
            if(stmt.varType->name == "Path"){
                if(stmt.expr && stmt.expr->kind == ExprKind::StringLiteral)
                    stmt.expr->adaptToTChar = true;
                if(stmt.isCtorInit){
                    for(Expr* arg : stmt.arguments){
                        if(arg && arg->kind == ExprKind::StringLiteral)
                            arg->adaptToTChar = true;
                    }
                }
            }
        }

        if(stmt.expr){
            const bool isMutRefBinding = stmt.varType && stmt.varType->refMode == ParamPassingMode::Mut;
            if(!checkExpr(ownerClass, method, *stmt.expr, scope, isMutRefBinding, errors))
                ok = false;
            if(stmt.varType && stmt.varType->isAuto && !stmt.expr->resolvedSimdType.empty()){
                stmt.varType->isAuto = false;
                stmt.varType->name = stmt.expr->resolvedSimdType;
            }
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
        if(stmt.expr && stmt.expr->kind == ExprKind::Lambda){
            info.lambdaExpr = stmt.expr;
            info.lambdaCaptureBytes = stmt.expr->lambdaCaptures.empty() ? 0u : 64u;
        }
        else if(stmt.varType && stmt.varType->isFuncType)
            info.lambdaCaptureBytes = stmt.varType->inferredCaptureBytes;
        scope.push_back(info);
        break;
    }
    case StmtKind::ExprStmt:{
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, false, errors))
            ok = false;
        break;
    }
    case StmtKind::Return:{
        if(m_parallelDepth > 0u && !m_inLambda){
            reportError(errors, stmt.span, "'return' cannot be used inside a 'parallel' or 'parallel_for' construct");
            ok = false;
        }
        const TypeRef* activeRetType = m_inLambda ? m_activeLambdaReturnType : method.returnType;
        const bool isVoidLike = (!m_inLambda && (method.isConstructor || method.isDestructor)) || !activeRetType || activeRetType->name == "void";
        if(isVoidLike && stmt.expr){
            reportError(errors, stmt.span, "cannot return a value from a 'void' function, constructor, or destructor");
            ok = false;
        }
        else if(!isVoidLike && !stmt.expr){
            reportError(errors, stmt.span, "non-void function must return a value");
            ok = false;
        }
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, false, errors))
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
                bool hasEq = false;
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
        const bool isPar = (stmt.kind == StmtKind::ParallelForRange);
        if(isPar)
            method.usesTaskScope = true;
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, stmt.rangeMode == ParamPassingMode::Mut, errors))
            ok = false;
        if(stmt.varType){
            if(stmt.varType->isAuto && stmt.expr){
                const TypeRef* containerType = nullptr;
                if(stmt.expr->kind == ExprKind::Identifier){
                    if(const ScopedVarInfo* local = findScopedVar(scope, stmt.expr->text))
                        containerType = local->type;
                }
                else if(stmt.expr->kind == ExprKind::ThisMember && ownerClass){
                    if(const FieldDecl* field = findClassField(*ownerClass, stmt.expr->text))
                        containerType = field->type;
                }
                if(containerType && !containerType->templateArgs.empty() && containerType->templateArgs[0].type){
                    const TypeRef& elemType = *containerType->templateArgs[0].type;
                    if(elemType.templateArgs.empty() && !elemType.isFuncType && SymbolTable::isSimdType(elemType.name)){
                        stmt.varType->isAuto = false;
                        stmt.varType->name = elemType.name;
                    }
                }
            }
            if(!checkTypeRef(*stmt.varType, ownerClass, &method, errors))
                ok = false;
        }
        const usize saved = scope.size();
        ScopedVarInfo loopVar;
        loopVar.name = stmt.name;
        loopVar.type = stmt.varType;
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
            const bool isAfterCond = (stmt.diagnosticKind != DiagnosticKind::Assert) || (i >= 1u);
            if(isAfterCond && arg->kind == ExprKind::StringLiteral)
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
        const FieldDecl* field = findClassField(*ownerClass, expr.text);
        const MethodDecl* memberMethod = findClassMethod(*ownerClass, expr.text);
        if(!field && !memberMethod){
            reportError(errors, expr.span, "unknown member on 'this'");
            ok = false;
            break;
        }
        if(field && field->type && field->type->refMode != ParamPassingMode::None)
            expr.derefNotNull = true;

        if(isMutationTarget){
            if(method.isConst){
                reportError(errors, expr.span, "cannot mutate 'this.member' inside a 'const' method");
                ok = false;
            }
            else if(!method.isConstructor && field && field->type && field->type->refMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' reference member");
                ok = false;
            }
            else if(m_parallelDepth > 0u && field && field->type && __hidden_adhd_type_checker::IsNonThreadSafeContainer(field->type->name)){
                reportError(errors, expr.span, "shared 'mut' access to non-thread-safe member container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::ScopeAccess:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot mutate or pass scope-qualified constant/enumerator to a 'mut' target");
            ok = false;
        }
        if(!expr.scopeChain.empty()){
            if(const ScopedVarInfo* local = findScopedVar(scope, expr.scopeChain[0])){
                if(local->type && m_symbols.findEnum(local->type->name))
                    expr.scopeChain[0] = local->type->name;
            }
            if(const EnumDecl* enumDecl = m_symbols.findEnum(expr.scopeChain[0])){
                if(expr.scopeChain.size() < 2u || expr.scopeChain.size() > 3u){
                    reportError(errors, expr.span, "invalid enum scope access; expected 'EnumName::Entry', 'EnumName::Enum::Entry', or 'EnumName::Flag::Entry'");
                    ok = false;
                }
                else{
                    const MStringView entryName = expr.scopeChain.back();
                    const EnumEntry* matchedEntry = nullptr;
                    for(const EnumEntry& entry : enumDecl->entries){
                        if(entry.name == entryName){
                            matchedEntry = &entry;
                            break;
                        }
                    }
                    if(!matchedEntry){
                        reportError(errors, expr.span, "unknown enumerator in enum scope access");
                        ok = false;
                    }
                    else if(expr.scopeChain.size() == 3u){
                        const MStringView qualifier = expr.scopeChain[1];
                        if(qualifier != "Enum" && qualifier != "Flag"){
                            reportError(errors, expr.span, "invalid enum scope qualifier; expected '::Enum::' or '::Flag::'");
                            ok = false;
                        }
                        else if(qualifier == "Enum" && matchedEntry->isFlag){
                            reportError(errors, expr.span, "enumerator declared with 'flag' must be accessed via '::Flag::', not '::Enum::'");
                            ok = false;
                        }
                        else if(qualifier == "Flag" && !matchedEntry->isFlag){
                            reportError(errors, expr.span, "enumerator declared with 'enum' must be accessed via '::Enum::', not '::Flag::'");
                            ok = false;
                        }
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
            const MStringView baseType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
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
        const ScopedVarInfo* localCallable = nullptr;
        if(expr.lhs){
            if(expr.lhs->kind == ExprKind::Identifier){
                if(m_symbols.findInterface(expr.lhs->text)){
                    reportError(errors, expr.span, "cannot instantiate an 'interface' type directly");
                    ok = false;
                }
                targetCallable = m_symbols.findFreeFunction(expr.lhs->text);
                if(!targetCallable)
                    localCallable = findScopedVar(scope, expr.lhs->text);
                if(expr.lhs->text == "Path"){
                    for(Expr* arg : expr.arguments){
                        if(arg && arg->kind == ExprKind::StringLiteral)
                            arg->adaptToTChar = true;
                    }
                }
            }
            else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
                targetCallable = findClassMethod(*ownerClass, expr.lhs->text);
                if(method.isConst && targetCallable && !targetCallable->isConst){
                    reportError(errors, expr.span, "cannot call non-'const' method on 'this' inside a 'const' method");
                    ok = false;
                }
            }
            else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
                const MStringView objType = inferExprTypeName(ownerClass, &method, *expr.lhs->lhs, scope);
                const MStringView mName = expr.lhs->text;
                const bool isMutatingContainerOp = (mName == "push_back" || mName == "emplace_back" || mName == "pop_back" || mName == "clear" || mName == "insert" || mName == "erase" || mName == "resize");
                if(const ClassDecl* targetCls = m_symbols.findClass(objType))
                    targetCallable = findClassMethod(*targetCls, mName);
                else if(m_symbols.findInterface(objType))
                    targetCallable = m_symbols.findInterfaceMethod(objType, mName);
                else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, &method, objType)){
                    for(const MStringView& boundName : tp->interfaceBounds){
                        if(const MethodDecl* boundMethod = m_symbols.findInterfaceMethod(boundName, mName)){
                            targetCallable = boundMethod;
                            break;
                        }
                    }
                    if(!targetCallable){
                        reportError(errors, expr.span, "method call on template parameter requires a matching interface bound ('<T : IInterface>')");
                        ok = false;
                    }
                }
                else if(__hidden_adhd_type_checker::IsNonThreadSafeContainer(objType) && m_parallelDepth > 0u && isMutatingContainerOp){
                    reportError(errors, expr.span, "mutating non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers");
                    ok = false;
                }

                if((targetCallable && !targetCallable->isConst) || isMutatingContainerOp){
                    const bool savedDeref = expr.lhs->lhs->derefNotNull;
                    if(!checkExpr(ownerClass, method, *expr.lhs->lhs, scope, true, errors))
                        ok = false;
                    expr.lhs->lhs->derefNotNull = savedDeref;
                }
            }
        }

        if(!expr.callTemplateArgs.empty()){
            if(targetCallable){
                if(!checkTemplateArgBounds(targetCallable->templateParams, expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
                    ok = false;
            }
            else if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
                if(const ClassDecl* targetCls = m_symbols.findClass(expr.lhs->text)){
                    if(!checkTemplateArgBounds(targetCls->templateParams, expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
                        ok = false;
                }
            }
        }
        for(const TypeArg& targ : expr.callTemplateArgs){
            if(targ.type && !checkTypeRef(*targ.type, ownerClass, &method, errors))
                ok = false;
        }

        for(usize i = 0u; i < expr.arguments.size(); ++i){
            Expr* arg = expr.arguments[i];
            if(!arg)
                continue;
            bool paramIsMut = false;
            MStringView expectedParamType;
            if(targetCallable && i < targetCallable->params.size()){
                paramIsMut = (targetCallable->params[i].mode == ParamPassingMode::Mut);
                if(targetCallable->params[i].type)
                    expectedParamType = targetCallable->params[i].type->name;
            }
            else if(localCallable){
                if(localCallable->type && localCallable->type->isFuncType && i < localCallable->type->funcParams.size()){
                    paramIsMut = (localCallable->type->funcParams[i].mode == ParamPassingMode::Mut);
                    if(localCallable->type->funcParams[i].type)
                        expectedParamType = localCallable->type->funcParams[i].type->name;
                }
                else if(localCallable->lambdaExpr && i < localCallable->lambdaExpr->lambdaParams.size()){
                    paramIsMut = (localCallable->lambdaExpr->lambdaParams[i].mode == ParamPassingMode::Mut);
                    if(localCallable->lambdaExpr->lambdaParams[i].type)
                        expectedParamType = localCallable->lambdaExpr->lambdaParams[i].type->name;
                }
            }
            arg->isMutArg = paramIsMut;
            if(!checkExpr(ownerClass, method, *arg, scope, paramIsMut, errors))
                ok = false;
            if(!expectedParamType.empty() && m_symbols.findInterface(expectedParamType)){
                const MStringView argType = inferExprTypeName(ownerClass, &method, *arg, scope);
                if(const ClassDecl* argCls = m_symbols.findClass(argType)){
                    if(!classImplementsInterface(*argCls, expectedParamType)){
                        reportError(errors, arg->span, "cannot pass concrete class to interface parameter: class does not implement the interface");
                        ok = false;
                    }
                }
            }
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
        if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text)){
                if(field->type && field->type->refMode != ParamPassingMode::None && (method.isConstructor || m_symbols.findInterface(field->type->name))){
                    expr.lhs->derefNotNull = false;
                    expr.isNotNullRebind = true;
                    if(expr.rhs && m_symbols.findInterface(field->type->name)){
                        const MStringView rhsType = inferExprTypeName(ownerClass, &method, *expr.rhs, scope);
                        if(const ClassDecl* rhsCls = m_symbols.findClass(rhsType)){
                            if(!classImplementsInterface(*rhsCls, field->type->name)){
                                reportError(errors, expr.span, "cannot bind interface reference member to concrete class that does not implement the interface");
                                ok = false;
                            }
                        }
                    }
                }
            }
        }
        if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text);
            if(local && local->type){
                if(expr.lhs->derefNotNull && m_symbols.findInterface(local->type->name)){
                    expr.lhs->derefNotNull = false;
                    expr.isNotNullRebind = true;
                    if(expr.rhs){
                        const MStringView rhsType = inferExprTypeName(ownerClass, &method, *expr.rhs, scope);
                        if(const ClassDecl* rhsCls = m_symbols.findClass(rhsType)){
                            if(!classImplementsInterface(*rhsCls, local->type->name)){
                                reportError(errors, expr.span, "cannot rebind interface reference to concrete class that does not implement the interface");
                                ok = false;
                            }
                        }
                    }
                }
                if(local->type->isFuncType && expr.rhs){
                    if(expr.rhs->kind == ExprKind::Lambda && !expr.rhs->lambdaCaptures.empty()){
                        local->type->inferredCaptureBytes = 64u;
                    }
                    else if(expr.rhs->kind == ExprKind::Identifier){
                        if(const ScopedVarInfo* rhsVar = findScopedVar(scope, expr.rhs->text)){
                            if(rhsVar->lambdaCaptureBytes > 0u || (rhsVar->type && rhsVar->type->inferredCaptureBytes > 0u))
                                local->type->inferredCaptureBytes = 64u;
                        }
                    }
                }
            }
        }
        if(expr.rhs && !checkExpr(ownerClass, method, *expr.rhs, scope, false, errors))
            ok = false;
        break;
    }
    case ExprKind::Cast:
    case ExprKind::BitCast:{
        if(expr.targetType){
            if(!checkTypeRef(*expr.targetType, ownerClass, &method, errors))
                ok = false;
            if(m_symbols.findInterface(expr.targetType->name)){
                reportError(errors, expr.span, "casting to an 'interface' type is prohibited in adhd");
                ok = false;
            }
        }
        if(expr.lhs){
            if(!checkExpr(ownerClass, method, *expr.lhs, scope, false, errors))
                ok = false;
            const MStringView srcType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
            if(!srcType.empty() && m_symbols.findInterface(srcType)){
                reportError(errors, expr.span, "downcasting from an 'interface' type is prohibited in adhd");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::Lambda:{
        if(expr.lambdaReturnType){
            if(!checkTypeRef(*expr.lambdaReturnType, ownerClass, &method, errors))
                ok = false;
            if(expr.lambdaReturnType->refMode == ParamPassingMode::None && m_symbols.findInterface(expr.lambdaReturnType->name)){
                reportError(errors, expr.span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        MVector<ScopedVarInfo> lambdaScope(m_arena);
        for(const LambdaCapture& cap : expr.lambdaCaptures){
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
        for(const ParamDecl& p : expr.lambdaParams){
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
        const bool savedInLambda = m_inLambda;
        const TypeRef* savedLambdaRet = m_activeLambdaReturnType;
        const u32 savedLoop = m_loopDepth;
        const u32 savedSwitch = m_switchDepth;
        m_inLambda = true;
        m_activeLambdaReturnType = expr.lambdaReturnType;
        m_loopDepth = 0u;
        m_switchDepth = 0u;
        if(!checkStatementList(ownerClass, method, expr.lambdaBody, lambdaScope, errors))
            ok = false;
        m_inLambda = savedInLambda;
        m_activeLambdaReturnType = savedLambdaRet;
        m_loopDepth = savedLoop;
        m_switchDepth = savedSwitch;
        break;
    }
    default:
        break;
    }

    const MStringView inferredType = inferExprTypeName(ownerClass, &method, expr, scope);
    if(SymbolTable::isSimdType(inferredType))
        expr.resolvedSimdType = inferredType;

    return ok;
}

MStringView TypeChecker::inferExprTypeName(
    ClassDecl* ownerClass,
    const MethodDecl* method,
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
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        u32 simdLanes = 0u;
        if(SymbolTable::isSimdVectorType(lhsType, simdLanes) && expr.text.size() == 1u){
            const MChar c = expr.text[0];
            if((c == 'w' || c == 'a') && (lhsType == "f32x3_i32" || lhsType == "f32x3_u32" || lhsType == "f64x3_i64" || lhsType == "f64x3_u64"))
                return lhsType.substr(6u);
            return lhsType.substr(0u, 3u);
        }
        if(expr.isSwizzle)
            return lhsType;
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            if(const FieldDecl* field = findClassField(*cls, expr.text)){
                if(field->type)
                    return field->type->name;
            }
        }
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        u32 simdLanes = 0u;
        if(SymbolTable::isSimdVectorType(lhsType, simdLanes))
            return lhsType.substr(0u, 3u);
    }
    else if(expr.kind == ExprKind::Unary && expr.lhs){
        if(expr.op == TokenType::Bang)
            return "bool";
        return inferExprTypeName(ownerClass, method, *expr.lhs, scope);
    }
    else if(expr.kind == ExprKind::Binary){
        if(expr.op == TokenType::EqualEqual || expr.op == TokenType::BangEqual
            || expr.op == TokenType::Less || expr.op == TokenType::LessEqual
            || expr.op == TokenType::Greater || expr.op == TokenType::GreaterEqual
            || expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe)
        {
            return "bool";
        }
        const MStringView lhsT = expr.lhs ? inferExprTypeName(ownerClass, method, *expr.lhs, scope) : MStringView{};
        const MStringView rhsT = expr.rhs ? inferExprTypeName(ownerClass, method, *expr.rhs, scope) : MStringView{};
        if(SymbolTable::isSimdType(lhsT))
            return lhsT;
        if(SymbolTable::isSimdType(rhsT))
            return rhsT;
        return !lhsT.empty() ? lhsT : rhsT;
    }
    else if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            if(m_symbols.findClass(expr.lhs->text) || SymbolTable::isSimdType(expr.lhs->text))
                return expr.lhs->text;
            if(const MethodDecl* fn = m_symbols.findFreeFunction(expr.lhs->text)){
                if(fn->returnType)
                    return fn->returnType->name;
            }
            if(!expr.arguments.empty() && expr.arguments[0]){
                const MStringView fnName = expr.lhs->text;
                if(fnName == "dot" || fnName == "length" || fnName == "lengthSq"){
                    const MStringView argType = inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                    u32 simdLanes = 0u;
                    if(SymbolTable::isSimdVectorType(argType, simdLanes))
                        return argType.substr(0u, 3u);
                    return argType;
                }
                if(fnName == "mul"){
                    const MStringView lhsT = inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                    if(expr.arguments.size() == 2u && expr.arguments[1]){
                        const MStringView rhsT = inferExprTypeName(ownerClass, method, *expr.arguments[1], scope);
                        u32 rhsLanes = 0u;
                        if(SymbolTable::isSimdMatrixType(lhsT) && SymbolTable::isSimdVectorType(rhsT, rhsLanes))
                            return rhsT;
                    }
                    return lhsT;
                }
                if(fnName == "min" || fnName == "max" || fnName == "abs" || fnName == "clamp"
                    || fnName == "lerp" || fnName == "sqrt" || fnName == "rsqrt"
                    || fnName == "floor" || fnName == "ceil" || fnName == "round"
                    || fnName == "sin" || fnName == "cos" || fnName == "tan"
                    || fnName == "cross" || fnName == "normalize"
                    || fnName == "transpose" || fnName == "inverse")
                {
                    for(const Expr* arg : expr.arguments){
                        if(!arg)
                            continue;
                        const MStringView argT = inferExprTypeName(ownerClass, method, *arg, scope);
                        if(SymbolTable::isSimdType(argT))
                            return argT;
                    }
                    return inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            if(const MethodDecl* m = findClassMethod(*ownerClass, expr.lhs->text)){
                if(m->returnType)
                    return m->returnType->name;
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs->lhs, scope);
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                if(const MethodDecl* m = findClassMethod(*cls, expr.lhs->text)){
                    if(m->returnType)
                        return m->returnType->name;
                }
            }
            else if(m_symbols.findInterface(lhsType)){
                if(const MethodDecl* m = m_symbols.findInterfaceMethod(lhsType, expr.lhs->text)){
                    if(m->returnType)
                        return m->returnType->name;
                }
            }
            else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, method, lhsType)){
                for(const MStringView& boundName : tp->interfaceBounds){
                    if(const MethodDecl* m = m_symbols.findInterfaceMethod(boundName, expr.lhs->text)){
                        if(m->returnType)
                            return m->returnType->name;
                    }
                }
            }
        }
    }
    else if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.targetType){
        return expr.targetType->name;
    }
    else if(expr.kind == ExprKind::ScopeAccess && !expr.scopeChain.empty()){
        if(m_symbols.findEnum(expr.scopeChain[0]))
            return expr.scopeChain[0];
    }
    else if(expr.kind == ExprKind::StringLiteral){
        return "StringView";
    }
    return {};
}

const TemplateParamDecl* TypeChecker::findTemplateParam(
    const ClassDecl* ownerClass,
    const MethodDecl* method,
    const MStringView name
)noexcept{
    if(method){
        for(const TemplateParamDecl& tp : method->templateParams){
            if(!tp.constValueType && tp.name == name)
                return &tp;
        }
    }
    if(ownerClass){
        for(const TemplateParamDecl& tp : ownerClass->templateParams){
            if(!tp.constValueType && tp.name == name)
                return &tp;
        }
    }
    return nullptr;
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
    ReportAdhdDiagnostic(m_arena, errors, span, message);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

