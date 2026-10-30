// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TypeRefReferencesAlias(const TypeRef* type, const MStringView targetAliasName, const SymbolTable& symbols, const u32 depth = 0u)noexcept{
    if(!type || depth > 16u)
        return false;
    if(type->isFuncType){
        if(TypeRefReferencesAlias(type->funcReturnType, targetAliasName, symbols, depth + 1u))
            return true;
        for(const FuncParamType& fp : type->funcParams){
            if(TypeRefReferencesAlias(fp.type, targetAliasName, symbols, depth + 1u))
                return true;
        }
        return false;
    }
    if(type->name == targetAliasName)
        return true;
    for(const TypeArg& targ : type->templateArgs){
        if(TypeRefReferencesAlias(targ.type, targetAliasName, symbols, depth + 1u))
            return true;
    }
    if(const TypeAliasDecl* nextAlias = symbols.findTypeAlias(type->name)){
        if(TypeRefReferencesAlias(nextAlias->aliasedType, targetAliasName, symbols, depth + 1u))
            return true;
    }
    return false;
}

[[nodiscard]] b8 ExprUsesOutOfLineConstruct(const Expr* expr)noexcept{
    if(!expr)
        return false;
    if(!expr->resolvedSimdType.empty() || expr->kind == ExprKind::Lambda)
        return true;
    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        DiagnosticKind::Enum diagKind = DiagnosticKind::Assert;
        if(TryParseDiagnosticKind(expr->lhs->text, diagKind))
            return true;
    }
    if(ExprUsesOutOfLineConstruct(expr->lhs) || ExprUsesOutOfLineConstruct(expr->rhs))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprUsesOutOfLineConstruct(arg))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtListIsAutoInlineable(const Span<const Stmt* const> stmts, u32& inOutCount)noexcept{
    for(const Stmt* s : stmts){
        if(!s)
            continue;
        ++inOutCount;
        if(inOutCount > 16u)
            return false;
        switch(s->kind){
        case StmtKind::Return:
            if(ExprUsesOutOfLineConstruct(s->expr))
                return false;
            break;
        case StmtKind::ExprStmt:
            if(!s->expr || s->expr->kind != ExprKind::Binary || s->expr->op != TokenType::Equal)
                return false;
            if(!s->expr->lhs || s->expr->lhs->kind != ExprKind::ThisMember)
                return false;
            if(ExprUsesOutOfLineConstruct(s->expr->lhs) || ExprUsesOutOfLineConstruct(s->expr->rhs))
                return false;
            break;
        case StmtKind::If:
            if(ExprUsesOutOfLineConstruct(s->condition))
                return false;
            if(!StmtListIsAutoInlineable(s->statements, inOutCount) || !StmtListIsAutoInlineable(s->elseStatements, inOutCount))
                return false;
            break;
        case StmtKind::Switch:
            if(ExprUsesOutOfLineConstruct(s->expr))
                return false;
            for(const SwitchCase& sc : s->switchCases){
                if(ExprUsesOutOfLineConstruct(sc.matchExpr))
                    return false;
                if(!StmtListIsAutoInlineable(sc.statements, inOutCount))
                    return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

[[nodiscard]] b8 ShouldAutoInlineCallable(const MethodDecl& fn)noexcept{
    if(!fn.hasBody || fn.isNoInline)
        return false;
    if(fn.isForceInline)
        return true;
    if(fn.usesScratchArena || fn.usesTaskScope || fn.isOutOfLine)
        return false;
    if(!fn.templateParams.empty())
        return true;
    u32 stmtCount = 0u;
    return StmtListIsAutoInlineable(fn.body, stmtCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const ScopedVarInfo* TypeChecker::FindScopedVar(const Span<const ScopedVarInfo> scope, const MStringView name)noexcept{
    for(usize i = scope.size(); i > 0u; --i){
        if(scope[i - 1u].name == name)
            return &scope[i - 1u];
    }
    return nullptr;
}

b8 TypeChecker::checkTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    b8 ok = true;

    if(!m_symbols.build(unit, errors))
        ok = false;

    if(!linkOutOfLineMethods(unit, errors))
        ok = false;

    for(const EnumDecl* enumDecl : unit.enums){
        if(!enumDecl)
            continue;
        u32 autoFlagBitIndex = 0u;
        MethodDecl dummyEnumCtx(m_arena);
        MVector<ScopedVarInfo> enumScope(m_arena);
        TypeRef* enumUnderlyingRef = AllocateAstNode<TypeRef>(m_arena);
        enumUnderlyingRef->name = enumDecl->sizeTypeName;

        for(usize i = 0u; i < enumDecl->entries.size(); ++i){
            const EnumEntry& entry = enumDecl->entries[i];
            if(entry.name == "Count"){
                reportError(errors, entry.span, enumDecl->isFlag
                    ? "use 'kCount' instead of 'Count' for flag count sentries"
                    : "use 'kCount' instead of 'Count' for enum count sentries");
                ok = false;
            }
            for(usize j = 0u; j < i; ++j){
                if(enumDecl->entries[j].name == entry.name){
                    reportError(errors, entry.span, enumDecl->isFlag ? "duplicate enumerator name in flag" : "duplicate enumerator name in enum");
                    ok = false;
                    break;
                }
            }
            if(!enumDecl->isImported){
                if(enumDecl->isFlag && !entry.valueExpr){
                    if(autoFlagBitIndex >= static_cast<u32>(enumDecl->underlyingSize) * 8u){
                        reportError(errors, entry.span, "auto-assigned 'flag' bit position exceeds underlying flag bit width");
                        ok = false;
                    }
                    ++autoFlagBitIndex;
                }
                if(entry.valueExpr){
                    if(!checkExpr(nullptr, dummyEnumCtx, *entry.valueExpr, enumScope, false, errors)){
                        ok = false;
                    }
                    else{
                        const MStringView valType = inferExprTypeName(nullptr, &dummyEnumCtx, *entry.valueExpr, enumScope);
                        if(entry.valueExpr->kind == ExprKind::FloatLiteral || entry.valueExpr->kind == ExprKind::StringLiteral || IsFloatScalarType(valType) || SymbolTable::IsStringSwitchType(valType)){
                            reportError(errors, entry.span, enumDecl->isFlag
                                ? "flag entry value must be an integral constant expression"
                                : "enum entry value must be an integral constant expression");
                            ok = false;
                        }
                    }
                }
            }
            ScopedVarInfo entryVar;
            entryVar.name = entry.name;
            entryVar.inferredTypeName = enumDecl->sizeTypeName;
            entryVar.type = enumUnderlyingRef;
            entryVar.isConst = true;
            enumScope.push_back(entryVar);
        }
    }

    for(TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(!checkTemplateParamBounds(aliasDecl->templateParams, nullptr, nullptr, errors))
            ok = false;
        if(aliasDecl->aliasedType){
            if(__hidden_adhd_type_checker::TypeRefReferencesAlias(aliasDecl->aliasedType, aliasDecl->name, m_symbols)){
                reportError(errors, aliasDecl->span, "cyclic 'type' alias declaration is prohibited");
                ok = false;
            }
            MethodDecl aliasScope(m_arena);
            aliasScope.templateParams = aliasDecl->templateParams;
            if(!checkTypeRef(*aliasDecl->aliasedType, nullptr, &aliasScope, true, errors))
                ok = false;
        }
    }

    for(Stmt* gc : unit.globalConstants){
        if(!gc || gc->isImported)
            continue;
        MethodDecl dummyGlobalCtx(m_arena);
        MVector<ScopedVarInfo> emptyScope(m_arena);
        if(gc->varType){
            if(!checkTypeRef(*gc->varType, nullptr, nullptr, errors))
                ok = false;
            const MStringView baseGcType = m_symbols.resolveBaseTypeName(gc->varType->name);
            if(m_symbols.findInterface(baseGcType)){
                reportError(errors, gc->span, "file-level 'const' cannot have an 'interface' type");
                ok = false;
            }
            if(m_symbols.resolveRefMode(gc->varType) != ParamPassingMode::None){
                reportError(errors, gc->span, "file-level 'const' cannot declare an 'in' or 'mut' reference");
                ok = false;
            }
            if(baseGcType == "Path"){
                if(gc->expr)
                    markStringToPathAdaptation(nullptr, &dummyGlobalCtx, *gc->expr, emptyScope);
                for(Expr* arg : gc->arguments){
                    if(arg)
                        markStringToPathAdaptation(nullptr, &dummyGlobalCtx, *arg, emptyScope);
                }
            }
        }
        if(gc->expr){
            if(!checkExpr(nullptr, dummyGlobalCtx, *gc->expr, emptyScope, false, errors))
                ok = false;
            if(gc->varType && gc->varType->isAuto && !gc->expr->resolvedSimdType.empty()){
                gc->varType->isAuto = false;
                gc->varType->name = gc->expr->resolvedSimdType;
            }
        }
        for(Expr* arg : gc->arguments){
            if(arg && !checkExpr(nullptr, dummyGlobalCtx, *arg, emptyScope, false, errors))
                ok = false;
        }
    }

    for(InterfaceDecl* iface : unit.interfaces){
        if(!checkInterfaceDecl(*iface, errors))
            ok = false;
    }

    for(ClassDecl* classDecl : unit.classes){
        if(!checkClassDecl(*classDecl, errors))
            ok = false;
        for(MethodDecl* m : classDecl->methods){
            if(m)
                m->isInline = __hidden_adhd_type_checker::ShouldAutoInlineCallable(*m);
        }
    }

    for(MethodDecl* fn : unit.freeFunctions){
        if(!checkCallableSignature(nullptr, *fn, errors))
            ok = false;
        if(!fn->hasBody){
            if(!fn->isImported){
                reportError(errors, fn->span, "declared function has no implementation body");
                ok = false;
            }
            continue;
        }
        if(fn->isImported){
            fn->isInline = __hidden_adhd_type_checker::ShouldAutoInlineCallable(*fn);
            continue;
        }
        if(!m_parallelValidator.validateMethod(*fn, errors))
            ok = false;
        if(!checkMethodBody(nullptr, *fn, errors))
            ok = false;
        fn->isInline = __hidden_adhd_type_checker::ShouldAutoInlineCallable(*fn);
    }

    if(!m_layoutOptimizer.optimizeTranslationUnit(unit, errors))
        ok = false;

    return ok;
}

b8 TypeChecker::linkOutOfLineMethods(TranslationUnit& unit, MVector<ParseError>& errors){
    b8 ok = true;

    for(MethodDecl* ool : unit.outOfLineMethods){
        ClassDecl* cls = m_symbols.findClass(ool->ownerClassName);
        if(!cls){
            reportError(errors, ool->span, "out-of-line method references unknown class");
            ok = false;
            continue;
        }

        if(cls->templateParams.size() != ool->ownerTemplateParams.size()){
            reportError(errors, ool->span, "out-of-line method owner class template parameter count does not match class declaration");
            ok = false;
            continue;
        }
        b8 ownerTemplateNamesMatch = true;
        for(usize tIdx = 0u; tIdx < cls->templateParams.size(); ++tIdx){
            if(cls->templateParams[tIdx].name != ool->ownerTemplateParams[tIdx]){
                reportError(errors, ool->span, "out-of-line method owner class template parameter name does not match class declaration");
                ok = false;
                ownerTemplateNamesMatch = false;
                break;
            }
        }
        if(!ownerTemplateNamesMatch)
            continue;

        MethodDecl* matched = nullptr;
        MethodDecl* paramMatched = nullptr;
        for(MethodDecl* m : cls->methods){
            if(m->isConstructor == ool->isConstructor && m->isDestructor == ool->isDestructor && m->isOperator == ool->isOperator && m->name == ool->name && m->params.size() == ool->params.size()){
                if(!matched || m->templateParams.size() == ool->templateParams.size())
                    matched = m;
                if(m->templateParams.size() == ool->templateParams.size() && DoParamListsMatch(m->params, ool->params, m_symbols)){
                    paramMatched = m;
                    if(m->isConst == ool->isConst)
                        break;
                }
            }
        }
        if(paramMatched)
            matched = paramMatched;

        if(!matched){
            reportError(errors, ool->span, "out-of-line method is not declared in class");
            ok = false;
            continue;
        }

        if(matched->templateParams.size() != ool->templateParams.size()){
            reportError(errors, ool->span, "out-of-line method template parameter count does not match class declaration");
            ok = false;
            continue;
        }
        b8 methodTemplateParamsMatch = true;
        for(usize tIdx = 0u; tIdx < matched->templateParams.size(); ++tIdx){
            const TemplateParamDecl& declTp = matched->templateParams[tIdx];
            const TemplateParamDecl& oolTp = ool->templateParams[tIdx];
            if(declTp.name != oolTp.name){
                reportError(errors, ool->span, "out-of-line method template parameter name does not match class declaration");
                ok = false;
                methodTemplateParamsMatch = false;
                break;
            }
            if(
                (declTp.constValueType != nullptr) != (oolTp.constValueType != nullptr)
                || (declTp.constValueType && oolTp.constValueType && !AreTypeRefsEquivalent(declTp.constValueType, oolTp.constValueType, m_symbols))
                || declTp.interfaceBounds.size() != oolTp.interfaceBounds.size()
            ){
                reportError(errors, ool->span, "out-of-line method template parameter constraints do not match class declaration");
                ok = false;
                methodTemplateParamsMatch = false;
                break;
            }
            for(usize bIdx = 0u; bIdx < declTp.interfaceBounds.size(); ++bIdx){
                if(!AreTypeRefsEquivalent(declTp.interfaceBounds[bIdx], oolTp.interfaceBounds[bIdx], m_symbols)){
                    reportError(errors, ool->span, "out-of-line method template parameter constraints do not match class declaration");
                    ok = false;
                    methodTemplateParamsMatch = false;
                    break;
                }
            }
            if(!methodTemplateParamsMatch)
                break;
        }
        if(!methodTemplateParamsMatch)
            continue;

        if(matched->isConst != ool->isConst){
            reportError(errors, ool->span, "out-of-line method 'const' qualifier does not match class declaration");
            ok = false;
            continue;
        }

        if(ool->isOverride && !matched->isOverride){
            reportError(errors, ool->span, "out-of-line method cannot specify 'override' when class declaration is not marked 'override'");
            ok = false;
            continue;
        }

        if((matched->isForceInline && ool->isNoInline) || (matched->isNoInline && ool->isForceInline)){
            reportError(errors, ool->span, "conflicting 'inline' and 'noinline' attributes between method declaration and definition");
            ok = false;
            continue;
        }

        if(!matched->isConstructor && !matched->isDestructor){
            if(!AreTypeRefsEquivalent(matched->returnType, ool->returnType, m_symbols)){
                reportError(errors, ool->span, "out-of-line method return type does not match class declaration");
                ok = false;
                continue;
            }
        }

        b8 paramsMatch = true;
        for(usize pIdx = 0u; pIdx < matched->params.size(); ++pIdx){
            const ParamDecl& declP = matched->params[pIdx];
            const ParamDecl& oolP = ool->params[pIdx];
            if(declP.mode != oolP.mode || !AreTypeRefsEquivalent(declP.type, oolP.type, m_symbols)){
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

        if(!ool->templateParams.empty())
            matched->templateParams = ool->templateParams;
        matched->params = ool->params;
        matched->hasBody = true;
        matched->isOutOfLine = true;
        matched->body = ool->body;
        if(ool->isForceInline)
            matched->isForceInline = true;
        if(ool->isNoInline)
            matched->isNoInline = true;
        if(ool->isReflect){
            matched->isReflect = true;
            cls->isReflect = true;
        }
        if(!ool->isImported)
            matched->isImported = false;
    }

    for(usize i = 0u; i < unit.freeFunctions.size(); ++i){
        MethodDecl* fn = unit.freeFunctions[i];
        if(!fn)
            continue;
        for(usize j = 0u; j < i; ++j){
            MethodDecl* prev = unit.freeFunctions[j];
            if(!prev || prev->name != fn->name || prev->templateParams.size() != fn->templateParams.size())
                continue;
            if(!DoParamListsMatch(prev->params, fn->params, m_symbols))
                continue;
            if(!AreTypeRefsEquivalent(prev->returnType, fn->returnType, m_symbols)){
                reportError(errors, fn->span, "conflicting return type for free function declaration");
                ok = false;
                continue;
            }
            if((prev->isForceInline && fn->isNoInline) || (prev->isNoInline && fn->isForceInline)){
                reportError(errors, fn->span, "conflicting 'inline' and 'noinline' attributes between function declaration and definition");
                ok = false;
                continue;
            }
            if(prev->hasBody && fn->hasBody){
                reportError(errors, fn->span, "duplicate free function definition with identical signature");
                ok = false;
                continue;
            }
            if(prev->isForceInline || fn->isForceInline){
                prev->isForceInline = true;
                fn->isForceInline = true;
            }
            if(prev->isNoInline || fn->isNoInline){
                prev->isNoInline = true;
                fn->isNoInline = true;
            }
            if(!prev->hasBody && fn->hasBody){
                if(!prev->isImported){
                    prev->params = fn->params;
                    prev->hasBody = true;
                    prev->isOutOfLine = true;
                    prev->body = fn->body;
                    fn->isImported = true;
                    fn->hasBody = false;
                }
                else{
                    fn->isOutOfLine = true;
                    prev->hasBody = true;
                }
            }
            else if(prev->hasBody && !fn->hasBody){
                fn->isImported = true;
            }
            else if(!prev->hasBody && !fn->hasBody){
                fn->isImported = true;
            }
        }
    }

    return ok;
}

void TypeChecker::buildCallArgHints(ClassDecl* ownerClass, const MethodDecl* method, const Span<const Expr* const> args, const Span<const ScopedVarInfo> scope, MVector<MStringView>& outArgTypes, MVector<u8>& outArgIsMut)const{
    outArgTypes.clear();
    outArgIsMut.clear();
    outArgTypes.reserve(args.size());
    outArgIsMut.reserve(args.size());
    for(const Expr* arg : args){
        b8 isMut = arg && arg->isMutArg;
        if(!isMut && arg && method){
            if(arg->kind == ExprKind::Identifier){
                if(FindScopedVar(scope, arg->text) != nullptr && !isReceiverConst(ownerClass, *method, *arg, scope))
                    isMut = true;
            }
            else if(arg->kind == ExprKind::ThisMember || arg->kind == ExprKind::MemberAccess || arg->kind == ExprKind::Index){
                if(!isReceiverConst(ownerClass, *method, *arg, scope))
                    isMut = true;
            }
        }
        outArgIsMut.push_back(isMut ? 1u : 0u);
        outArgTypes.push_back(arg ? inferExprTypeName(ownerClass, method, *arg, scope) : MStringView{});
    }
}

MStringView TypeChecker::resolveScopedVarBaseType(const ScopedVarInfo& var)const noexcept{
    if(!var.inferredTypeName.empty())
        return var.inferredTypeName;
    if(var.type)
        return m_symbols.resolveBaseTypeName(var.type->name);
    return {};
}

b8 TypeChecker::isThisConstInScope(const MethodDecl& method, const Span<const ScopedVarInfo> scope)const noexcept{
    const ScopedVarInfo* lambdaThis = m_inLambda ? FindScopedVar(scope, "this") : nullptr;
    return method.isConst || (lambdaThis && lambdaThis->isConst);
}

b8 TypeChecker::validateThisAccess(const ClassDecl* ownerClass, const SourceSpan span, const Span<const ScopedVarInfo> scope, MVector<ParseError>& errors)const{
    if(!ownerClass){
        reportError(errors, span, "'this' cannot be used in a free function");
        return false;
    }
    if(m_inLambda && !FindScopedVar(scope, "this")){
        reportError(errors, span, "accessing 'this' inside a lambda requires capturing 'in this' or 'mut this'");
        return false;
    }
    return true;
}

b8 TypeChecker::isConstructibleTypeName(const MStringView calleeName, const MStringView baseCallee)const noexcept{
    return m_symbols.findClass(baseCallee) != nullptr
        || m_symbols.findTypeAlias(calleeName) != nullptr
        || SymbolTable::IsSimdType(baseCallee)
        || SymbolTable::IsArenaBackedContainer(baseCallee)
        || m_symbols.isSelfDefaultedType(baseCallee);
}

const MethodDecl* TypeChecker::resolveCustomOperator(
    ClassDecl* ownerClass,
    const MethodDecl* method,
    const TypeRef* lhsTypeRef,
    const MStringView lhsBaseType,
    const MStringView opSym,
    const usize arity,
    const b8 recvIsConst,
    const ClassDecl** outDeclaringClass
)const noexcept{
    if(outDeclaringClass)
        *outDeclaringClass = nullptr;
    if(opSym.empty())
        return nullptr;
    if(const ClassDecl* cls = m_symbols.findClass(lhsBaseType)){
        if(outDeclaringClass)
            *outDeclaringClass = cls;
        return m_symbols.findClassOperator(*cls, opSym, &arity, recvIsConst, ownerClass != cls);
    }
    if(lhsTypeRef && m_symbols.findInterface(lhsBaseType))
        return m_symbols.findInterfaceMethod(*lhsTypeRef, opSym, &arity);
    if(const TemplateParamDecl* tp = FindTemplateParam(ownerClass, method, lhsBaseType)){
        for(const TypeRef* boundRef : tp->interfaceBounds){
            if(!boundRef)
                continue;
            if(const MethodDecl* boundOp = m_symbols.findInterfaceMethod(*boundRef, opSym, &arity))
                return boundOp;
        }
    }
    return nullptr;
}

MStringView TypeChecker::resolveScopeAccessEnumTypeName(ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope, const ClassDecl** outAccessedClass, const FieldDecl** outAccessedField)const noexcept{
    if(outAccessedClass)
        *outAccessedClass = nullptr;
    if(outAccessedField)
        *outAccessedField = nullptr;
    if(expr.scopeChain.empty())
        return {};

    const MStringView rootName = expr.scopeChain[0];
    if(expr.text == "this"){
        if(ownerClass){
            if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, rootName); field && field->type){
                const MStringView fieldTypeName = m_symbols.resolveBaseTypeName(field->type->name);
                if(m_symbols.findEnum(fieldTypeName))
                    return fieldTypeName;
            }
        }
        return {};
    }
    if(expr.lhs){
        const TypeRef* lhsTypeRef = nullptr;
        const MStringView lhsBase = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
        if(const ClassDecl* targetCls = m_symbols.findClass(lhsBase)){
            if(const FieldDecl* field = SymbolTable::FindClassField(*targetCls, rootName); field && field->type){
                if(outAccessedClass)
                    *outAccessedClass = targetCls;
                if(outAccessedField)
                    *outAccessedField = field;
                const TypeRef* ft = m_symbols.substituteTypeParams(field->type, targetCls->templateParams, lhsTypeRef);
                const MStringView fieldTypeName = ft ? m_symbols.resolveBaseTypeName(ft->name) : MStringView{};
                if(!fieldTypeName.empty() && m_symbols.findEnum(fieldTypeName))
                    return fieldTypeName;
            }
        }
        return {};
    }
    if(const ScopedVarInfo* local = FindScopedVar(scope, rootName)){
        const MStringView localTypeName = resolveScopedVarBaseType(*local);
        if(!localTypeName.empty() && m_symbols.findEnum(localTypeName))
            return localTypeName;
        return {};
    }
    if(const Stmt* gc = m_symbols.findGlobalConstant(rootName); gc && gc->varType){
        const MStringView gcTypeName = m_symbols.resolveBaseTypeName(gc->varType->name);
        if(!gcTypeName.empty() && m_symbols.findEnum(gcTypeName))
            return gcTypeName;
        return {};
    }
    const MStringView resolvedRoot = m_symbols.resolveBaseTypeName(rootName);
    if(!resolvedRoot.empty() && m_symbols.findEnum(resolvedRoot))
        return resolvedRoot;
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

