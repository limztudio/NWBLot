// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TypeChecker::checkInterfaceDecl(InterfaceDecl& iface, MVector<ParseError>& errors){
    MethodDecl ifaceScope(m_arena);
    ifaceScope.templateParams = iface.templateParams;
    bool ok = checkTemplateParamBounds(iface.templateParams, nullptr, &ifaceScope, errors);
    for(usize i = 0u; i < iface.baseInterfaces.size(); ++i){
        const TypeRef* baseType = iface.baseInterfaces[i];
        if(!baseType)
            continue;
        const MStringView baseName = m_symbols.resolveBaseTypeName(baseType->name);
        for(usize j = 0u; j < i; ++j){
            if(iface.baseInterfaces[j] && AreTypeRefsEquivalent(iface.baseInterfaces[j], baseType, m_symbols)){
                reportError(errors, iface.span, "duplicate base interface in interface declaration");
                ok = false;
                break;
            }
        }
        if(m_symbols.findClass(baseName)){
            reportError(errors, iface.span, "'interface' cannot inherit from a concrete 'class'");
            ok = false;
        }
        else if(m_symbols.findInterface(baseName)){
            TypeRef selfRef(m_arena);
            selfRef.name = iface.name;
            if(interfaceInheritsFrom(*baseType, selfRef)){
                reportError(errors, iface.span, "cyclic interface inheritance is prohibited");
                ok = false;
            }
        }
        else{
            reportError(errors, iface.span, "unknown base interface name");
            ok = false;
        }
        if(!checkTypeRef(*baseType, nullptr, &ifaceScope, errors))
            ok = false;
    }
    for(usize i = 0u; i < iface.methods.size(); ++i){
        MethodDecl* m = iface.methods[i];
        if(!m)
            continue;
        for(usize j = 0u; j < i; ++j){
            const MethodDecl* prev = iface.methods[j];
            if(prev
                && prev->isOperator == m->isOperator
                && prev->name == m->name
                && prev->isConst == m->isConst
                && prev->templateParams.size() == m->templateParams.size()
                && DoParamListsMatch(prev->params, m->params, m_symbols))
            {
                reportError(errors, m->span, "duplicate interface method declaration with identical signature");
                ok = false;
                break;
            }
        }
        if(!checkCallableSignature(nullptr, *m, errors))
            ok = false;
    }
    return ok;
}

bool TypeChecker::checkCallableSignature(const ClassDecl* enclosingClass, MethodDecl& method, MVector<ParseError>& errors){
    bool ok = checkTemplateParamBounds(method.templateParams, enclosingClass, &method, errors);
    if(method.returnType){
        if(!checkTypeRef(*method.returnType, enclosingClass, &method, errors, true))
            ok = false;
        if(m_symbols.resolveRefMode(method.returnType) == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(method.returnType->name))){
            reportError(errors, method.span, "cannot return an 'interface' type by value");
            ok = false;
        }
    }
    for(usize pIdx = 0u; pIdx < method.params.size(); ++pIdx){
        const ParamDecl& p = method.params[pIdx];
        if(p.type && !checkTypeRef(*p.type, enclosingClass, &method, errors))
            ok = false;
        if(!method.hasBody && !p.name.empty()){
            for(usize prevP = 0u; prevP < pIdx; ++prevP){
                if(method.params[prevP].name == p.name){
                    reportError(errors, p.span, "duplicate parameter name");
                    ok = false;
                    break;
                }
            }
        }
    }
    return ok;
}

bool TypeChecker::hasFieldInlineOrAutoDefault(const ClassDecl& classDecl, const FieldDecl& field)const noexcept{
    const bool isAutoDefaultField = field.type && m_symbols.resolveRefMode(field.type) == ParamPassingMode::None && (
        field.type->isFuncType
        || m_symbols.isSelfDefaultedType(field.type->name)
        || findTemplateParam(&classDecl, nullptr, field.type->name) != nullptr
    );
    return (field.defaultExpr != nullptr) || field.hasCtorInit || isAutoDefaultField;
}

bool TypeChecker::checkClassDecl(ClassDecl& classDecl, MVector<ParseError>& errors){
    bool ok = checkTemplateParamBounds(classDecl.templateParams, &classDecl, nullptr, errors);

    if(!classDecl.subsystemTypeName.empty()){
        if(classDecl.isSerialize || !classDecl.assetTypeName.empty() || !classDecl.componentAssetTypeName.empty()
            || !classDecl.templateParams.empty() || !classDecl.implementedInterfaces.empty() || !classDecl.methods.empty())
        {
            reportError(errors, classDecl.span, "'[subsystem(\"...\")]' class may only declare 'public' AssetRef<T> or Vector<AssetRef<T>> fields");
            ok = false;
        }
        if(classDecl.fields.empty()){
            reportError(errors, classDecl.span, "'[subsystem(\"...\")]' class must declare at least one asset field");
            ok = false;
        }
        for(const FieldDecl* field : classDecl.fields){
            if(!field)
                continue;
            if(field->access != AccessSpecifier::Public || field->defaultExpr || field->hasCtorInit || !field->type){
                reportError(errors, field->span, "'[subsystem(\"...\")]' field must be 'public' without an inline initializer");
                ok = false;
                continue;
            }
            const TypeRef* resolved = m_symbols.unwrapTypeAlias(field->type);
            bool validSubsystemField = false;
            if(resolved && resolved->refMode == ParamPassingMode::None){
                if(resolved->name == "AssetRef" && resolved->templateArgs.size() == 1u && resolved->templateArgs[0].type && !resolved->templateArgs[0].type->name.empty()){
                    validSubsystemField = true;
                }
                else if(resolved->name == "Vector" && resolved->templateArgs.size() == 1u && resolved->templateArgs[0].type){
                    const TypeRef* elemType = m_symbols.unwrapTypeAlias(resolved->templateArgs[0].type);
                    if(elemType && elemType->name == "AssetRef" && elemType->templateArgs.size() == 1u && elemType->templateArgs[0].type && !elemType->templateArgs[0].type->name.empty())
                        validSubsystemField = true;
                }
            }
            if(!validSubsystemField){
                reportError(errors, field->span, "'[subsystem(\"...\")]' field must have type 'AssetRef<TAsset>' or 'Vector<AssetRef<TAsset>>'");
                ok = false;
            }
        }
    }

    if(!classDecl.componentAssetTypeName.empty()){
        if(classDecl.isSerialize || !classDecl.assetTypeName.empty() || !classDecl.subsystemTypeName.empty()
            || !classDecl.templateParams.empty() || !classDecl.implementedInterfaces.empty() || !classDecl.methods.empty())
        {
            reportError(errors, classDecl.span, "'[component_asset(\"...\")]' class may only declare a single 'public' component field");
            ok = false;
        }
        if(classDecl.fields.size() != 1u || !classDecl.fields[0]
            || classDecl.fields[0]->access != AccessSpecifier::Public
            || classDecl.fields[0]->name != "component"
            || classDecl.fields[0]->defaultExpr || classDecl.fields[0]->hasCtorInit)
        {
            reportError(errors, classDecl.span, "'[component_asset(\"...\")]' class must declare exactly one 'public <ComponentType> component;' field");
            ok = false;
        }
    }

    for(usize i = 0u; i < classDecl.implementedInterfaces.size(); ++i){
        const TypeRef* ifaceType = classDecl.implementedInterfaces[i];
        if(!ifaceType)
            continue;
        for(usize j = 0u; j < i; ++j){
            if(classDecl.implementedInterfaces[j] && AreTypeRefsEquivalent(classDecl.implementedInterfaces[j], ifaceType, m_symbols)){
                reportError(errors, classDecl.span, "duplicate implemented interface in class declaration");
                ok = false;
                break;
            }
        }
        const MStringView ifaceName = m_symbols.resolveBaseTypeName(ifaceType->name);
        if(m_symbols.findClass(ifaceName)){
            reportError(errors, classDecl.span, "concrete class inheritance is prohibited in adhd; classes are implicitly final and may only implement interfaces");
            ok = false;
            continue;
        }
        if(!checkTypeRef(*ifaceType, &classDecl, nullptr, errors))
            ok = false;
        MVector<MethodDecl*> requiredInterfaceMethods(m_arena);
        m_symbols.collectAllInterfaceMethods(*ifaceType, requiredInterfaceMethods);
        const TypeRef* resolvedIfaceType = m_symbols.unwrapTypeAlias(ifaceType);
        const InterfaceDecl* directIfaceDecl = m_symbols.findInterface(ifaceName);
        const MVector<TemplateParamDecl>* ifaceParams = directIfaceDecl ? &directIfaceDecl->templateParams : nullptr;
        const MVector<TypeArg>* ifaceArgs = resolvedIfaceType ? &resolvedIfaceType->templateArgs : &ifaceType->templateArgs;

        for(MethodDecl* req : requiredInterfaceMethods){
            MethodDecl* impl = nullptr;
            for(MethodDecl* m : classDecl.methods){
                if(!m->isConstructor && !m->isDestructor && m->isOperator == req->isOperator && m->name == req->name && m->params.size() == req->params.size()){
                    bool exactParamsMatch = (m->isConst == req->isConst);
                    if(exactParamsMatch){
                        for(usize pIdx = 0u; pIdx < req->params.size(); ++pIdx){
                            if(m->params[pIdx].mode != req->params[pIdx].mode
                                || !AreTypeRefsEquivalentWithSubst(m->params[pIdx].type, req->params[pIdx].type, ifaceParams, ifaceArgs, m_symbols))
                            {
                                exactParamsMatch = false;
                                break;
                            }
                        }
                    }
                    if(exactParamsMatch){
                        impl = m;
                        break;
                    }
                    if(!impl || m->isConst == req->isConst)
                        impl = m;
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
                if(!AreTypeRefsEquivalentWithSubst(impl->returnType, req->returnType, ifaceParams, ifaceArgs, m_symbols)){
                    reportError(errors, impl->span, "override method return type does not match interface contract");
                    ok = false;
                }
                for(usize pIdx = 0u; pIdx < req->params.size(); ++pIdx){
                    const ParamDecl& implP = impl->params[pIdx];
                    const ParamDecl& reqP = req->params[pIdx];
                    if(implP.mode != reqP.mode || !AreTypeRefsEquivalentWithSubst(implP.type, reqP.type, ifaceParams, ifaceArgs, m_symbols)){
                        reportError(errors, implP.span, "override method parameter 'in'/'mut' mode or type does not match interface contract");
                        ok = false;
                        break;
                    }
                }
            }
        }
    }

    for(MethodDecl* m : classDecl.methods){
        if(m->isOverride){
            bool matchesInterface = false;
            const usize methodArity = m->params.size();
            for(const TypeRef* ifaceType : classDecl.implementedInterfaces){
                if(!ifaceType)
                    continue;
                const MStringView ifaceName = m_symbols.resolveBaseTypeName(ifaceType->name);
                if(!m_symbols.findInterface(ifaceName)){
                    matchesInterface = true;
                    break;
                }
                if(const MethodDecl* ifaceMatch = m_symbols.findInterfaceMethod(*ifaceType, m->name, &methodArity)){
                    if(ifaceMatch->isOperator == m->isOperator && ifaceMatch->params.size() == methodArity){
                        matchesInterface = true;
                        break;
                    }
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
            if(m_symbols.resolveRefMode(field->type) == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(field->type->name))){
                reportError(errors, field->span, "class member of 'interface' type must specify 'in' or 'mut'; interfaces cannot be stored by value");
                ok = false;
            }
        }
        const TypeRef* unwrappedFieldType = field->type ? m_symbols.unwrapTypeAlias(field->type) : nullptr;
        if(TypeRef* targetFuncType = ExtractUnderlyingFuncType(field->type, m_symbols); targetFuncType && field->defaultExpr && field->defaultExpr->kind == ExprKind::Lambda){
            if(!field->defaultExpr->lambdaCaptures.empty() && targetFuncType->inferredCaptureBytes < 64u)
                targetFuncType->inferredCaptureBytes = 64u;
        }
        if(unwrappedFieldType && unwrappedFieldType->name == "Path"){
            if(field->defaultExpr && field->defaultExpr->kind == ExprKind::StringLiteral)
                field->defaultExpr->adaptToTChar = true;
            for(Expr* arg : field->ctorArgs){
                if(arg && arg->kind == ExprKind::StringLiteral)
                    arg->adaptToTChar = true;
            }
        }
        if(field->defaultExpr || !field->ctorArgs.empty()){
            MethodDecl fieldInitDummy(m_arena);
            MVector<ScopedVarInfo> emptyScope(m_arena);
            const bool savedClassArenaCtx = m_inClassArenaContext;
            m_inClassArenaContext = true;
            if(field->defaultExpr && !checkExpr(&classDecl, fieldInitDummy, *field->defaultExpr, emptyScope, false, errors))
                ok = false;
            for(Expr* arg : field->ctorArgs){
                if(arg && !checkExpr(&classDecl, fieldInitDummy, *arg, emptyScope, false, errors))
                    ok = false;
            }
            m_inClassArenaContext = savedClassArenaCtx;
        }

        if(!hasFieldInlineOrAutoDefault(classDecl, *field) && !hasConstructor){
            reportError(errors, field->span, "class member has no inline default value and class defines no constructor to initialize it");
            ok = false;
        }
    }

    for(usize i = 0u; i < classDecl.methods.size(); ++i){
        MethodDecl* m = classDecl.methods[i];
        if(!m)
            continue;
        for(usize j = 0u; j < i; ++j){
            const MethodDecl* prev = classDecl.methods[j];
            if(prev
                && prev->isConstructor == m->isConstructor
                && prev->isDestructor == m->isDestructor
                && prev->isOperator == m->isOperator
                && prev->name == m->name
                && prev->isConst == m->isConst
                && prev->templateParams.size() == m->templateParams.size()
                && DoParamListsMatch(prev->params, m->params, m_symbols))
            {
                reportError(errors, m->span, "duplicate class method declaration with identical signature");
                ok = false;
                break;
            }
        }
        if(!checkCallableSignature(&classDecl, *m, errors))
            ok = false;
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
        if(hasFieldInlineOrAutoDefault(classDecl, *field))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

