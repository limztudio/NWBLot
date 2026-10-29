// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TypeRefReferencesAlias(
    const TypeRef* type,
    const MStringView targetAliasName,
    const SymbolTable& symbols,
    const u32 depth = 0u
)noexcept{
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
        const MStringView callee = expr->lhs->text;
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, astEnum, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
        if(callee == adhdKeyword) return true;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
    }
    if(ExprUsesOutOfLineConstruct(expr->lhs) || ExprUsesOutOfLineConstruct(expr->rhs))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprUsesOutOfLineConstruct(arg))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtListIsAutoInlineable(const MVector<Stmt*>& stmts, u32& inOutCount)noexcept{
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

[[nodiscard]] const MethodDecl* FindScoredClassCallable(
    const SymbolTable& symbols,
    const ClassDecl& classDecl,
    const MStringView name,
    const b8 isOperator,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || m->isConstructor || m->isDestructor || m->isOperator != isOperator || m->name != name)
            continue;
        i32 score = 0;
        if(!preferredArgCount || m->params.size() == *preferredArgCount)
            score += 64;
        if(!requirePublic || m->access == AccessSpecifier::Public)
            score += 32;
        score += ScoreCallableParams(symbols, m->params, argTypeNames, argIsMut);
        if(m->isConst == preferConst)
            score += 1;
        if(score > bestScore){
            bestScore = score;
            bestMatch = m;
        }
    }
    return bestMatch;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
                reportError(errors, entry.span, "use 'kCount' instead of 'Count' for enum count sentries");
                ok = false;
            }
            for(usize j = 0u; j < i; ++j){
                if(enumDecl->entries[j].name == entry.name){
                    reportError(errors, entry.span, "duplicate enumerator name in enum");
                    ok = false;
                    break;
                }
            }
            if(!enumDecl->isImported){
                if(entry.isFlag && !entry.valueExpr){
                    if(autoFlagBitIndex >= static_cast<u32>(enumDecl->underlyingSize) * 8u){
                        reportError(errors, entry.span, "auto-assigned 'flag' bit position exceeds underlying enum bit width");
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
                        if(entry.valueExpr->kind == ExprKind::FloatLiteral
                            || entry.valueExpr->kind == ExprKind::StringLiteral
                            || IsFloatScalarType(valType)
                            || SymbolTable::isStringSwitchType(valType))
                        {
                            reportError(errors, entry.span, "enum entry value must be an integral constant expression");
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
            if(!checkTypeRef(*aliasDecl->aliasedType, nullptr, &aliasScope, errors, true))
                ok = false;
        }
    }

    for(Stmt* gc : unit.globalConstants){
        if(!gc || gc->isImported)
            continue;
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
                if(gc->expr && gc->expr->kind == ExprKind::StringLiteral)
                    gc->expr->adaptToTChar = true;
                for(Expr* arg : gc->arguments){
                    if(arg && arg->kind == ExprKind::StringLiteral)
                        arg->adaptToTChar = true;
                }
            }
        }
        MethodDecl dummyGlobalCtx(m_arena);
        MVector<ScopedVarInfo> emptyScope(m_arena);
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
            if(m->isConstructor == ool->isConstructor
                && m->isDestructor == ool->isDestructor
                && m->isOperator == ool->isOperator
                && m->name == ool->name
                && m->params.size() == ool->params.size())
            {
                if(!matched || m->templateParams.size() == ool->templateParams.size())
                    matched = m;
                if(m->templateParams.size() == ool->templateParams.size()
                    && DoParamListsMatch(m->params, ool->params, m_symbols))
                {
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
            if((declTp.constValueType != nullptr) != (oolTp.constValueType != nullptr)
                || (declTp.constValueType && oolTp.constValueType && !AreTypeRefsEquivalent(declTp.constValueType, oolTp.constValueType, m_symbols))
                || declTp.interfaceBounds.size() != oolTp.interfaceBounds.size())
            {
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
                    if(fn->isReflect)
                        prev->isReflect = true;
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

void TypeChecker::buildCallArgHints(
    ClassDecl* ownerClass,
    const MethodDecl* method,
    const MVector<Expr*>& args,
    const MVector<ScopedVarInfo>& scope,
    MVector<MStringView>& outArgTypes,
    MVector<b8>& outArgIsMut
)const{
    outArgTypes.clear();
    outArgIsMut.clear();
    outArgTypes.reserve(args.size());
    outArgIsMut.reserve(args.size());
    for(const Expr* arg : args){
        b8 isMut = arg && arg->isMutArg;
        if(!isMut && arg && method){
            if(arg->kind == ExprKind::Identifier){
                if(findScopedVar(scope, arg->text) != nullptr && !isReceiverConst(ownerClass, *method, *arg, scope))
                    isMut = true;
            }
            else if(arg->kind == ExprKind::ThisMember || arg->kind == ExprKind::MemberAccess || arg->kind == ExprKind::Index){
                if(!isReceiverConst(ownerClass, *method, *arg, scope))
                    isMut = true;
            }
        }
        outArgIsMut.push_back(isMut);
        outArgTypes.push_back(arg ? inferExprTypeName(ownerClass, method, *arg, scope) : MStringView{});
    }
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

const MethodDecl* TypeChecker::findClassMethod(
    const ClassDecl& classDecl,
    const MStringView name,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)const noexcept{
    return __hidden_adhd_type_checker::FindScoredClassCallable(m_symbols, classDecl, name, false, preferredArgCount, preferConst, requirePublic, argTypeNames, argIsMut);
}

const MethodDecl* TypeChecker::findClassOperator(
    const ClassDecl& classDecl,
    const MStringView opName,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)const noexcept{
    return __hidden_adhd_type_checker::FindScoredClassCallable(m_symbols, classDecl, opName, true, preferredArgCount, preferConst, requirePublic, argTypeNames, argIsMut);
}

const MethodDecl* TypeChecker::findClassConstructor(
    const ClassDecl& classDecl,
    const usize argCount,
    const b8 requirePublic,
    const MVector<MStringView>* argTypeNames,
    const MVector<b8>* argIsMut
)const noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || !m->isConstructor || m->params.size() != argCount)
            continue;
        i32 score = (!requirePublic || m->access == AccessSpecifier::Public) ? 32 : 1;
        score += ScoreCallableParams(m_symbols, m->params, argTypeNames, argIsMut);
        if(score > bestScore){
            bestScore = score;
            bestMatch = m;
        }
    }
    return bestMatch;
}

void TypeChecker::reportError(MVector<ParseError>& errors, const SourceSpan span, const MStringView message)const{
    ReportAdhdDiagnostic(m_arena, errors, span, message);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
