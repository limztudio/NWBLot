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

[[nodiscard]] bool StmtReferencesIdentifier(const Stmt* stmt, MStringView varName)noexcept;

[[nodiscard]] bool ExprReferencesIdentifier(const Expr* expr, const MStringView varName)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Identifier && expr->text == varName)
        return true;
    if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& cap : expr->lambdaCaptures){
            if(cap.name == varName)
                return true;
        }
        for(const Stmt* child : expr->lambdaBody){
            if(StmtReferencesIdentifier(child, varName))
                return true;
        }
    }
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
    if(stmt->initStmt && StmtReferencesIdentifier(stmt->initStmt, varName))
        return true;
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
    for(const NativeBinding& nb : stmt->nativeBindings){
        if(ExprReferencesIdentifier(nb.sourceExpr, varName))
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

[[nodiscard]] bool AreTypeRefsEquivalentWithSubst(
    const TypeRef* actual,
    const TypeRef* pattern,
    const MVector<TemplateParamDecl>* ifaceParams,
    const MVector<TypeArg>* ifaceArgs
)noexcept;

[[nodiscard]] bool AreTypeRefsEquivalent(const TypeRef* a, const TypeRef* b)noexcept{
    return AreTypeRefsEquivalentWithSubst(a, b, nullptr, nullptr);
}

[[nodiscard]] bool AreTypeRefsEquivalentWithSubst(
    const TypeRef* actual,
    const TypeRef* pattern,
    const MVector<TemplateParamDecl>* ifaceParams,
    const MVector<TypeArg>* ifaceArgs
)noexcept{
    if(actual == pattern)
        return true;
    if(!actual || !pattern)
        return IsVoidTypeRef(actual) && IsVoidTypeRef(pattern);

    if(ifaceParams && ifaceArgs && pattern->templateArgs.empty() && !pattern->isFuncType){
        const usize substCount = ifaceParams->size() < ifaceArgs->size() ? ifaceParams->size() : ifaceArgs->size();
        for(usize k = 0u; k < substCount; ++k){
            if(!(*ifaceParams)[k].constValueType && (*ifaceParams)[k].name == pattern->name && (*ifaceArgs)[k].type){
                const TypeRef* subst = (*ifaceArgs)[k].type;
                const ParamPassingMode::Enum expectedRefMode = (pattern->refMode != ParamPassingMode::None)
                    ? pattern->refMode
                    : subst->refMode;
                if(actual->refMode != expectedRefMode)
                    return false;
                return actual->name == subst->name
                    && actual->isAuto == subst->isAuto
                    && actual->isFuncType == subst->isFuncType
                    && actual->templateArgs.size() == subst->templateArgs.size()
                    && AreTypeRefsEquivalent(actual, subst);
            }
        }
    }

    if(actual->name != pattern->name
        || actual->refMode != pattern->refMode
        || actual->isAuto != pattern->isAuto
        || actual->isFuncType != pattern->isFuncType
        || actual->templateArgs.size() != pattern->templateArgs.size())
    {
        return false;
    }
    for(usize i = 0u; i < actual->templateArgs.size(); ++i){
        const TypeArg& ta = actual->templateArgs[i];
        const TypeArg& tb = pattern->templateArgs[i];
        if(ta.constantText != tb.constantText || !AreTypeRefsEquivalentWithSubst(ta.type, tb.type, ifaceParams, ifaceArgs))
            return false;
    }
    if(actual->isFuncType){
        if(!AreTypeRefsEquivalentWithSubst(actual->funcReturnType, pattern->funcReturnType, ifaceParams, ifaceArgs)
            || actual->funcParams.size() != pattern->funcParams.size())
        {
            return false;
        }
        for(usize i = 0u; i < actual->funcParams.size(); ++i){
            if(actual->funcParams[i].mode != pattern->funcParams[i].mode
                || !AreTypeRefsEquivalentWithSubst(actual->funcParams[i].type, pattern->funcParams[i].type, ifaceParams, ifaceArgs))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool TypeRefReferencesAlias(
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

[[nodiscard]] TypeRef* ExtractUnderlyingFuncType(TypeRef* type, const SymbolTable& symbols)noexcept{
    for(u32 depth = 0u; type && depth < 16u; ++depth){
        if(type->isFuncType)
            return type;
        if(const TypeAliasDecl* aliasDecl = symbols.findTypeAlias(type->name)){
            if(aliasDecl->aliasedType){
                type = aliasDecl->aliasedType;
                continue;
            }
        }
        if(type->name == "Optional" && !type->templateArgs.empty() && type->templateArgs[0].type){
            type = type->templateArgs[0].type;
            continue;
        }
        break;
    }
    return nullptr;
}

void PropagateFuncCaptureBytes(
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

[[nodiscard]] bool DoParamListsMatch(const MVector<ParamDecl>& a, const MVector<ParamDecl>& b)noexcept{
    if(a.size() != b.size())
        return false;
    for(usize i = 0u; i < a.size(); ++i){
        if(a[i].mode != b[i].mode || !AreTypeRefsEquivalent(a[i].type, b[i].type))
            return false;
    }
    return true;
}

[[nodiscard]] bool StatementsContainReturnOrNative(const MVector<Stmt*>& stmts)noexcept{
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

[[nodiscard]] bool HasConstValueTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, const MStringView name)noexcept{
    if(method){
        for(const TemplateParamDecl& tp : method->templateParams){
            if(tp.constValueType && tp.name == name)
                return true;
        }
    }
    if(ownerClass){
        for(const TemplateParamDecl& tp : ownerClass->templateParams){
            if(tp.constValueType && tp.name == name)
                return true;
        }
    }
    return false;
}

[[nodiscard]] bool NormalizeConstantTemplateArg(
    TypeArg& arg,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    const SymbolTable& symbols
)noexcept{
    if(!arg.constantText.empty())
        return true;
    if(!arg.type || arg.type->refMode != ParamPassingMode::None || !arg.type->templateArgs.empty() || arg.type->isFuncType)
        return false;
    const MStringView name = arg.type->name;
    if(name == "true" || name == "false"
        || symbols.findGlobalConstant(name) != nullptr
        || HasConstValueTemplateParam(enclosingClass, enclosingMethod, name))
    {
        arg.constantText = name;
        arg.type = nullptr;
        return true;
    }
    return false;
}

[[nodiscard]] bool IsSingleTypeParamBuiltinGeneric(const MStringView name)noexcept{
    return name == "Vector"
        || name == "ParallelVector"
        || name == "ForwardList"
        || name == "List"
        || name == "Deque"
        || name == "Set"
        || name == "HashSet"
        || name == "ParallelHashSet"
        || name == "Queue"
        || name == "ParallelQueue"
        || name == "Span"
        || name == "Optional"
        || name == "AssetRef"
        || name == "Atomic";
}

[[nodiscard]] bool IsTwoTypeParamBuiltinGeneric(const MStringView name)noexcept{
    return name == "Map"
        || name == "HashMap"
        || name == "ParallelHashMap"
        || name == "Expected";
}

[[nodiscard]] bool IsTypeAndCountBuiltinGeneric(const MStringView name)noexcept{
    return name == "Array"
        || name == "FixedVector"
        || name == "ParallelBlockQueue";
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
        if(aliasDecl->aliasedType){
            if(__hidden_adhd_type_checker::TypeRefReferencesAlias(aliasDecl->aliasedType, aliasDecl->name, m_symbols)){
                reportError(errors, aliasDecl->span, "cyclic 'type' alias declaration is prohibited");
                ok = false;
            }
            if(!checkTypeRef(*aliasDecl->aliasedType, nullptr, nullptr, errors, true))
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
            if(gc->varType->refMode != ParamPassingMode::None){
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
    }

    for(MethodDecl* fn : unit.freeFunctions){
        if(!checkTemplateParamBounds(fn->templateParams, errors))
            ok = false;
        if(fn->returnType){
            if(!checkTypeRef(*fn->returnType, nullptr, fn, errors, true))
                ok = false;
            if(fn->returnType->refMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(fn->returnType->name))){
                reportError(errors, fn->span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(usize pIdx = 0u; pIdx < fn->params.size(); ++pIdx){
            const ParamDecl& p = fn->params[pIdx];
            if(p.type && !checkTypeRef(*p.type, nullptr, fn, errors))
                ok = false;
            if(!fn->hasBody && !p.name.empty()){
                for(usize prevP = 0u; prevP < pIdx; ++prevP){
                    if(fn->params[prevP].name == p.name){
                        reportError(errors, p.span, "duplicate parameter name");
                        ok = false;
                        break;
                    }
                }
            }
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
                    && __hidden_adhd_type_checker::DoParamListsMatch(m->params, ool->params))
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

        if(!ool->templateParams.empty())
            matched->templateParams = ool->templateParams;
        matched->params = ool->params;
        matched->hasBody = true;
        matched->body = ool->body;
        if(ool->isInline)
            matched->isInline = true;
        if(ool->isNoInline)
            matched->isNoInline = true;
        if(ool->isReflect){
            matched->isReflect = true;
            cls->isReflect = true;
        }
        if(matched->isInline && matched->isNoInline){
            reportError(errors, ool->span, "callable cannot be marked both '[inline]' and '[noinline]'");
            ok = false;
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
            if(!__hidden_adhd_type_checker::DoParamListsMatch(prev->params, fn->params))
                continue;
            if(!__hidden_adhd_type_checker::AreTypeRefsEquivalent(prev->returnType, fn->returnType)){
                reportError(errors, fn->span, "conflicting return type for free function declaration");
                ok = false;
                continue;
            }
            if(prev->hasBody && fn->hasBody){
                reportError(errors, fn->span, "duplicate free function definition with identical signature");
                ok = false;
                continue;
            }
            if(!prev->hasBody && fn->hasBody){
                if(!prev->isImported){
                    prev->params = fn->params;
                    prev->hasBody = true;
                    prev->body = fn->body;
                    if(fn->isInline)
                        prev->isInline = true;
                    if(fn->isNoInline)
                        prev->isNoInline = true;
                    if(fn->isReflect)
                        prev->isReflect = true;
                    if(prev->isInline && prev->isNoInline){
                        reportError(errors, fn->span, "callable cannot be marked both '[inline]' and '[noinline]'");
                        ok = false;
                    }
                    fn->isImported = true;
                    fn->hasBody = false;
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

bool TypeChecker::checkTemplateParamBounds(const MVector<TemplateParamDecl>& params, MVector<ParseError>& errors){
    bool ok = true;
    for(usize i = 0u; i < params.size(); ++i){
        const TemplateParamDecl& param = params[i];
        for(usize j = 0u; j < i; ++j){
            if(params[j].name == param.name){
                reportError(errors, param.span, "duplicate template parameter name");
                ok = false;
                break;
            }
        }
        if(param.constValueType && !checkTypeRef(*param.constValueType, nullptr, nullptr, errors))
            ok = false;
        for(usize bIdx = 0u; bIdx < param.interfaceBounds.size(); ++bIdx){
            const MStringView& boundName = param.interfaceBounds[bIdx];
            for(usize prevB = 0u; prevB < bIdx; ++prevB){
                if(param.interfaceBounds[prevB] == boundName){
                    reportError(errors, param.span, "duplicate interface bound on template parameter");
                    ok = false;
                    break;
                }
            }
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

bool TypeChecker::interfaceInheritsFrom(const InterfaceDecl& iface, const MStringView requiredIface, const u32 depth)const noexcept{
    if(depth > 16u)
        return false;
    if(iface.name == requiredIface)
        return true;
    for(const TypeRef* baseType : iface.baseInterfaces){
        if(!baseType)
            continue;
        if(baseType->name == requiredIface)
            return true;
        if(const InterfaceDecl* baseDecl = m_symbols.findInterface(baseType->name)){
            if(interfaceInheritsFrom(*baseDecl, requiredIface, depth + 1u))
                return true;
        }
    }
    return false;
}

bool TypeChecker::classImplementsInterface(const ClassDecl& cls, const MStringView requiredIface)const noexcept{
    for(const TypeRef* implType : cls.implementedInterfaces){
        if(!implType)
            continue;
        if(implType->name == requiredIface)
            return true;
        if(const InterfaceDecl* ifaceDecl = m_symbols.findInterface(implType->name)){
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
            if(!__hidden_adhd_type_checker::NormalizeConstantTemplateArg(const_cast<TypeArg&>(arg), enclosingClass, enclosingMethod, m_symbols)){
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

        const MStringView argTypeName = m_symbols.resolveBaseTypeName(arg.type->name);
        bool isDependentParam = false;
        if(enclosingClass){
            for(const TemplateParamDecl& tp : enclosingClass->templateParams){
                if(tp.name == arg.type->name || tp.name == argTypeName){
                    isDependentParam = true;
                    break;
                }
            }
        }
        if(!isDependentParam && enclosingMethod){
            for(const TemplateParamDecl& tp : enclosingMethod->templateParams){
                if(tp.name == arg.type->name || tp.name == argTypeName){
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
    MVector<ParseError>& errors,
    const bool allowVoid
){
    bool ok = true;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&typeRef);
    const bool isVoid = __hidden_adhd_type_checker::IsVoidTypeRef(&typeRef) || __hidden_adhd_type_checker::IsVoidTypeRef(unwrapped);
    if(isVoid){
        if(typeRef.refMode != ParamPassingMode::None || (unwrapped && unwrapped->refMode != ParamPassingMode::None)){
            reportError(errors, typeRef.span, "'void' cannot be qualified with 'in' or 'mut'");
            ok = false;
        }
        else if(!allowVoid){
            reportError(errors, typeRef.span, "'void' is a return-only type and cannot be used for variables, fields, parameters, casts, or template arguments");
            ok = false;
        }
    }
    if(typeRef.isFuncType){
        if(typeRef.funcReturnType){
            if(!checkTypeRef(*typeRef.funcReturnType, enclosingClass, enclosingMethod, errors, true))
                ok = false;
            if(typeRef.funcReturnType->refMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(typeRef.funcReturnType->name))){
                reportError(errors, typeRef.span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(const FuncParamType& fp : typeRef.funcParams){
            if(fp.type && !checkTypeRef(*fp.type, enclosingClass, enclosingMethod, errors))
                ok = false;
        }
    }
    if(__hidden_adhd_type_checker::IsSingleTypeParamBuiltinGeneric(typeRef.name)){
        if(typeRef.templateArgs.size() != 1u || !typeRef.templateArgs[0].type){
            reportError(errors, typeRef.span, "built-in generic type requires exactly 1 type argument");
            ok = false;
        }
    }
    else if(__hidden_adhd_type_checker::IsTwoTypeParamBuiltinGeneric(typeRef.name)){
        if(typeRef.templateArgs.size() != 2u || !typeRef.templateArgs[0].type || !typeRef.templateArgs[1].type){
            reportError(errors, typeRef.span, "built-in generic type requires exactly 2 type arguments");
            ok = false;
        }
    }
    else if(__hidden_adhd_type_checker::IsTypeAndCountBuiltinGeneric(typeRef.name)){
        if(typeRef.templateArgs.size() != 2u
            || !typeRef.templateArgs[0].type
            || !__hidden_adhd_type_checker::NormalizeConstantTemplateArg(const_cast<TypeArg&>(typeRef.templateArgs[1]), enclosingClass, enclosingMethod, m_symbols))
        {
            reportError(errors, typeRef.span, "built-in bounded container requires 1 element type argument and 1 compile-time constant capacity argument");
            ok = false;
        }
    }
    else if(const ClassDecl* targetCls = m_symbols.findClass(typeRef.name)){
        if(!targetCls->templateParams.empty() || !typeRef.templateArgs.empty()){
            if(!checkTemplateArgBounds(targetCls->templateParams, typeRef.templateArgs, enclosingClass, enclosingMethod, typeRef.span, errors))
                ok = false;
        }
    }
    else if(const InterfaceDecl* targetIface = m_symbols.findInterface(typeRef.name)){
        if(!targetIface->templateParams.empty() || !typeRef.templateArgs.empty()){
            if(!checkTemplateArgBounds(targetIface->templateParams, typeRef.templateArgs, enclosingClass, enclosingMethod, typeRef.span, errors))
                ok = false;
        }
    }
    else if(const TypeAliasDecl* targetAlias = m_symbols.findTypeAlias(typeRef.name)){
        if(!targetAlias->templateParams.empty() || !typeRef.templateArgs.empty()){
            if(!checkTemplateArgBounds(targetAlias->templateParams, typeRef.templateArgs, enclosingClass, enclosingMethod, typeRef.span, errors))
                ok = false;
        }
    }
    else if(!typeRef.templateArgs.empty()){
        reportError(errors, typeRef.span, "type does not accept template arguments");
        ok = false;
    }

    for(const TypeArg& targ : typeRef.templateArgs){
        if(targ.type){
            if(!checkTypeRef(*targ.type, enclosingClass, enclosingMethod, errors))
                ok = false;
            const TypeRef* unwrappedArg = m_symbols.unwrapTypeAlias(targ.type);
            const ParamPassingMode::Enum argRefMode = targ.type->refMode != ParamPassingMode::None
                ? targ.type->refMode
                : (unwrappedArg ? unwrappedArg->refMode : ParamPassingMode::None);
            if(argRefMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(targ.type->name))){
                reportError(errors, targ.type->span.line > 0u ? targ.type->span : typeRef.span, "template type argument of 'interface' type must specify 'in' or 'mut'; interfaces cannot be stored or instantiated by value");
                ok = false;
            }
        }
    }
    return ok;
}

bool TypeChecker::checkInterfaceDecl(InterfaceDecl& iface, MVector<ParseError>& errors){
    bool ok = checkTemplateParamBounds(iface.templateParams, errors);
    for(usize i = 0u; i < iface.baseInterfaces.size(); ++i){
        const TypeRef* baseType = iface.baseInterfaces[i];
        if(!baseType)
            continue;
        const MStringView baseName = baseType->name;
        for(usize j = 0u; j < i; ++j){
            if(iface.baseInterfaces[j] && __hidden_adhd_type_checker::AreTypeRefsEquivalent(iface.baseInterfaces[j], baseType)){
                reportError(errors, iface.span, "duplicate base interface in interface declaration");
                ok = false;
                break;
            }
        }
        if(m_symbols.findClass(baseName)){
            reportError(errors, iface.span, "'interface' cannot inherit from a concrete 'class'");
            ok = false;
        }
        else if(const InterfaceDecl* baseDecl = m_symbols.findInterface(baseName)){
            if(interfaceInheritsFrom(*baseDecl, iface.name)){
                reportError(errors, iface.span, "cyclic interface inheritance is prohibited");
                ok = false;
            }
        }
        else{
            reportError(errors, iface.span, "unknown base interface name");
            ok = false;
        }
        if(!checkTypeRef(*baseType, nullptr, nullptr, errors))
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
                && __hidden_adhd_type_checker::DoParamListsMatch(prev->params, m->params))
            {
                reportError(errors, m->span, "duplicate interface method declaration with identical signature");
                ok = false;
                break;
            }
        }
        if(!checkTemplateParamBounds(m->templateParams, errors))
            ok = false;
        if(m->returnType){
            if(!checkTypeRef(*m->returnType, nullptr, m, errors, true))
                ok = false;
            if(m->returnType->refMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(m->returnType->name))){
                reportError(errors, m->span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(usize pIdx = 0u; pIdx < m->params.size(); ++pIdx){
            const ParamDecl& p = m->params[pIdx];
            if(p.type && !checkTypeRef(*p.type, nullptr, m, errors))
                ok = false;
            if(!p.name.empty()){
                for(usize prevP = 0u; prevP < pIdx; ++prevP){
                    if(m->params[prevP].name == p.name){
                        reportError(errors, p.span, "duplicate parameter name");
                        ok = false;
                        break;
                    }
                }
            }
        }
    }
    return ok;
}

bool TypeChecker::checkClassDecl(ClassDecl& classDecl, MVector<ParseError>& errors){
    bool ok = checkTemplateParamBounds(classDecl.templateParams, errors);

    for(usize i = 0u; i < classDecl.implementedInterfaces.size(); ++i){
        const TypeRef* ifaceType = classDecl.implementedInterfaces[i];
        if(!ifaceType)
            continue;
        for(usize j = 0u; j < i; ++j){
            if(classDecl.implementedInterfaces[j] && __hidden_adhd_type_checker::AreTypeRefsEquivalent(classDecl.implementedInterfaces[j], ifaceType)){
                reportError(errors, classDecl.span, "duplicate implemented interface in class declaration");
                ok = false;
                break;
            }
        }
        const MStringView ifaceName = ifaceType->name;
        if(m_symbols.findClass(ifaceName)){
            reportError(errors, classDecl.span, "concrete class inheritance is prohibited in adhd; classes are implicitly final and may only implement interfaces");
            ok = false;
            continue;
        }
        if(!checkTypeRef(*ifaceType, &classDecl, nullptr, errors))
            ok = false;
        MVector<MethodDecl*> requiredInterfaceMethods(m_arena);
        m_symbols.collectAllInterfaceMethods(ifaceName, requiredInterfaceMethods);
        const InterfaceDecl* directIfaceDecl = m_symbols.findInterface(ifaceName);
        const MVector<TemplateParamDecl>* ifaceParams = directIfaceDecl ? &directIfaceDecl->templateParams : nullptr;
        const MVector<TypeArg>* ifaceArgs = &ifaceType->templateArgs;

        for(MethodDecl* req : requiredInterfaceMethods){
            MethodDecl* impl = nullptr;
            for(MethodDecl* m : classDecl.methods){
                if(!m->isConstructor && !m->isDestructor && m->isOperator == req->isOperator && m->name == req->name && m->params.size() == req->params.size()){
                    bool exactParamsMatch = (m->isConst == req->isConst);
                    if(exactParamsMatch){
                        for(usize pIdx = 0u; pIdx < req->params.size(); ++pIdx){
                            if(m->params[pIdx].mode != req->params[pIdx].mode
                                || !__hidden_adhd_type_checker::AreTypeRefsEquivalentWithSubst(m->params[pIdx].type, req->params[pIdx].type, ifaceParams, ifaceArgs))
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
                if(!__hidden_adhd_type_checker::AreTypeRefsEquivalentWithSubst(impl->returnType, req->returnType, ifaceParams, ifaceArgs)){
                    reportError(errors, impl->span, "override method return type does not match interface contract");
                    ok = false;
                }
                for(usize pIdx = 0u; pIdx < req->params.size(); ++pIdx){
                    const ParamDecl& implP = impl->params[pIdx];
                    const ParamDecl& reqP = req->params[pIdx];
                    if(implP.mode != reqP.mode || !__hidden_adhd_type_checker::AreTypeRefsEquivalentWithSubst(implP.type, reqP.type, ifaceParams, ifaceArgs)){
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
                const MStringView ifaceName = ifaceType->name;
                if(!m_symbols.findInterface(ifaceName)){
                    matchesInterface = true;
                    break;
                }
                if(const MethodDecl* ifaceMatch = m_symbols.findInterfaceMethod(ifaceName, m->name, &methodArity)){
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
            if(field->type->refMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(field->type->name))){
                reportError(errors, field->span, "class member of 'interface' type must specify 'in' or 'mut'; interfaces cannot be stored by value");
                ok = false;
            }
        }
        const TypeRef* unwrappedFieldType = field->type ? m_symbols.unwrapTypeAlias(field->type) : nullptr;
        if(TypeRef* targetFuncType = __hidden_adhd_type_checker::ExtractUnderlyingFuncType(field->type, m_symbols); targetFuncType && field->defaultExpr && field->defaultExpr->kind == ExprKind::Lambda)
            targetFuncType->inferredCaptureBytes = field->defaultExpr->lambdaCaptures.empty() ? 0u : 64u;
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

        const bool isAutoDefaultField = field->type && field->type->refMode == ParamPassingMode::None && (
            m_symbols.isSelfDefaultedType(field->type->name)
            || findTemplateParam(&classDecl, nullptr, field->type->name) != nullptr
        );
        const bool hasInlineDefault = (field->defaultExpr != nullptr) || field->hasCtorInit || isAutoDefaultField;
        if(!hasInlineDefault && !hasConstructor){
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
                && __hidden_adhd_type_checker::DoParamListsMatch(prev->params, m->params))
            {
                reportError(errors, m->span, "duplicate class method declaration with identical signature");
                ok = false;
                break;
            }
        }
        if(!checkTemplateParamBounds(m->templateParams, errors))
            ok = false;
        if(m->returnType){
            if(!checkTypeRef(*m->returnType, &classDecl, m, errors, true))
                ok = false;
            if(m->returnType->refMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(m->returnType->name))){
                reportError(errors, m->span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(usize pIdx = 0u; pIdx < m->params.size(); ++pIdx){
            const ParamDecl& p = m->params[pIdx];
            if(p.type && !checkTypeRef(*p.type, &classDecl, m, errors))
                ok = false;
            if(!m->hasBody && !p.name.empty()){
                for(usize prevP = 0u; prevP < pIdx; ++prevP){
                    if(m->params[prevP].name == p.name){
                        reportError(errors, p.span, "duplicate parameter name");
                        ok = false;
                        break;
                    }
                }
            }
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
        const bool isAutoDefaultField = field->type && field->type->refMode == ParamPassingMode::None && (
            m_symbols.isSelfDefaultedType(field->type->name)
            || findTemplateParam(&classDecl, nullptr, field->type->name) != nullptr
        );
        const bool hasInlineDefault = (field->defaultExpr != nullptr) || field->hasCtorInit || isAutoDefaultField;
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
    if(!method.isConstructor && !method.isDestructor && !__hidden_adhd_type_checker::IsVoidTypeRef(m_symbols.unwrapTypeAlias(method.returnType))){
        if(!__hidden_adhd_type_checker::StatementsContainReturnOrNative(method.body)){
            reportError(errors, method.span, "non-void function or method must contain a 'return' statement");
            ok = false;
        }
    }
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

        const MStringView resolvedLocalType = !local.inferredTypeName.empty()
            ? local.inferredTypeName
            : (local.type ? m_symbols.resolveBaseTypeName(local.type->name) : MStringView{});
        if(SymbolTable::isSimdType(resolvedLocalType))
            continue;

        if(lastUseIndex < statements.size()){
            Stmt* lastStmt = statements[lastUseIndex];
            if(lastStmt->kind == StmtKind::ExprStmt && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call){
                usize argRefCount = 0u;
                if(!__hidden_adhd_type_checker::ExprReferencesIdentifier(lastStmt->expr->lhs, local.name)){
                    for(const Expr* arg : lastStmt->expr->arguments){
                        if(__hidden_adhd_type_checker::ExprReferencesIdentifier(arg, local.name))
                            ++argRefCount;
                    }
                }
                if(argRefCount == 1u){
                    for(Expr* arg : lastStmt->expr->arguments){
                        if(arg && !arg->isMutArg && arg->kind == ExprKind::Identifier && arg->text == local.name)
                            arg->moveAtLastUse = true;
                    }
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
            const MStringView baseVarTypeName = m_symbols.resolveBaseTypeName(stmt.varType->name);
            if(m_symbols.findInterface(baseVarTypeName)){
                if(stmt.varType->refMode == ParamPassingMode::None){
                    reportError(errors, stmt.span, "local variable of 'interface' type must specify 'in' or 'mut'; interfaces cannot be instantiated by value");
                    ok = false;
                }
                else if(stmt.expr){
                    const MStringView rhsType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
                    if(const ClassDecl* rhsCls = m_symbols.findClass(rhsType)){
                        if(!classImplementsInterface(*rhsCls, baseVarTypeName)){
                            reportError(errors, stmt.span, "cannot bind concrete class to interface reference: class does not implement the interface");
                            ok = false;
                        }
                    }
                }
            }
            if(!stmt.expr && !stmt.isCtorInit && (stmt.varType->refMode != ParamPassingMode::None || !m_symbols.isSelfDefaultedType(stmt.varType->name))){
                reportError(errors, stmt.span, "uninitialized variable declaration is prohibited in adhd; every local variable must be initialized at declaration");
                ok = false;
            }
            if(stmt.varType->refMode == ParamPassingMode::None && SymbolTable::isArenaBackedContainer(baseVarTypeName)){
                stmt.usesScratchArena = true;
                method.usesScratchArena = true;
            }
            __hidden_adhd_type_checker::PropagateFuncCaptureBytes(stmt.varType, stmt.expr, scope, m_symbols);
            if(baseVarTypeName == "Path"){
                if(stmt.expr){
                    const MStringView exprType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
                    if(stmt.expr->kind == ExprKind::StringLiteral || exprType == "String" || exprType == "StringView" || exprType == "CompactString")
                        stmt.expr->adaptToTChar = true;
                }
                if(stmt.isCtorInit){
                    for(Expr* arg : stmt.arguments){
                        if(!arg)
                            continue;
                        const MStringView argType = inferExprTypeName(ownerClass, &method, *arg, scope);
                        if(arg->kind == ExprKind::StringLiteral || argType == "String" || argType == "StringView" || argType == "CompactString")
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
        if(stmt.varType && stmt.varType->isAuto && stmt.expr && stmt.expr->kind != ExprKind::Lambda){
            info.inferredTypeName = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
            if(const TypeRef* exprTypeRef = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope))
                info.type = const_cast<TypeRef*>(exprTypeRef);
        }
        info.mode = stmt.varType ? stmt.varType->refMode : ParamPassingMode::None;
        info.isConst = stmt.isConst || (stmt.varType && stmt.varType->refMode == ParamPassingMode::In);
        info.isParam = false;
        if(stmt.expr && stmt.expr->kind == ExprKind::Lambda)
            info.lambdaExpr = stmt.expr;
        scope.push_back(info);
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
        if(m_parallelDepth > 0u && !m_inLambda){
            reportError(errors, stmt.span, "'return' cannot be used inside a 'parallel' or 'parallel_for' construct");
            ok = false;
        }
        const TypeRef* activeRetType = m_inLambda ? m_activeLambdaReturnType : method.returnType;
        const TypeRef* unwrappedRetType = m_symbols.unwrapTypeAlias(activeRetType);
        const ParamPassingMode::Enum retRefMode = activeRetType
            ? (activeRetType->refMode != ParamPassingMode::None ? activeRetType->refMode : (unwrappedRetType ? unwrappedRetType->refMode : ParamPassingMode::None))
            : ParamPassingMode::None;
        const bool isVoidLike = (!m_inLambda && (method.isConstructor || method.isDestructor)) || __hidden_adhd_type_checker::IsVoidTypeRef(unwrappedRetType);
        if(isVoidLike && stmt.expr){
            reportError(errors, stmt.span, "cannot return a value from a 'void' function, constructor, or destructor");
            ok = false;
        }
        else if(!isVoidLike && !stmt.expr){
            reportError(errors, stmt.span, "non-void function must return a value");
            ok = false;
        }
        if(activeRetType && stmt.expr){
            __hidden_adhd_type_checker::PropagateFuncCaptureBytes(const_cast<TypeRef*>(activeRetType), stmt.expr, scope, m_symbols);
            const MStringView baseRetTypeName = m_symbols.resolveBaseTypeName(activeRetType->name);
            if(baseRetTypeName == "Path"){
                const MStringView retExprType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
                if(stmt.expr->kind == ExprKind::StringLiteral || retExprType == "String" || retExprType == "StringView" || retExprType == "CompactString")
                    stmt.expr->adaptToTChar = true;
            }
            if(retRefMode != ParamPassingMode::None){
                stmt.wrapReturnNotNull = true;
                if(m_symbols.findInterface(baseRetTypeName)){
                    const MStringView retExprType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
                    if(const ClassDecl* retCls = m_symbols.findClass(retExprType)){
                        if(!classImplementsInterface(*retCls, baseRetTypeName)){
                            reportError(errors, stmt.span, "cannot return concrete class for interface reference return type: class does not implement the interface");
                            ok = false;
                        }
                    }
                }
            }
        }
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, retRefMode == ParamPassingMode::Mut, errors))
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
        if(!stmt.name.empty() && findScopedVar(scope, stmt.name)){
            reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
            ok = false;
        }
        if(stmt.expr && !checkExpr(ownerClass, method, *stmt.expr, scope, stmt.rangeMode == ParamPassingMode::Mut, errors))
            ok = false;
        const TypeRef* elemTypeRef = nullptr;
        MStringView resolvedElemName;
        if(stmt.varType){
            if(stmt.varType->isAuto && stmt.expr){
                const TypeRef* containerType = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
                if(containerType && !containerType->templateArgs.empty() && containerType->templateArgs[0].type){
                    elemTypeRef = m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type);
                    if(elemTypeRef){
                        resolvedElemName = m_symbols.resolveBaseTypeName(elemTypeRef->name);
                        if(elemTypeRef->templateArgs.empty() && !elemTypeRef->isFuncType && SymbolTable::isSimdType(resolvedElemName)){
                            stmt.varType->isAuto = false;
                            stmt.varType->name = resolvedElemName;
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
            if(isAfterCond){
                if(arg->kind == ExprKind::StringLiteral){
                    arg->adaptToTChar = true;
                }
                else{
                    const MStringView argType = inferExprTypeName(ownerClass, &method, *arg, scope);
                    if(argType == "String" || argType == "StringView" || argType == "CompactString")
                        arg->adaptToTChar = true;
                }
            }
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
                else if(m_parallelDepth > 0u && local->type && __hidden_adhd_type_checker::IsNonThreadSafeContainer(m_symbols.resolveBaseTypeName(local->type->name))){
                    reportError(errors, expr.span, "shared 'mut' access to non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                    ok = false;
                }
            }
        }
        else{
            bool reportedUnqualifiedMember = false;
            if(ownerClass && (findClassField(*ownerClass, expr.text) || findClassMethod(*ownerClass, expr.text))){
                reportError(errors, expr.span, "class member must be accessed explicitly via 'this.member' in adhd");
                ok = false;
                reportedUnqualifiedMember = true;
            }
            if(!reportedUnqualifiedMember && isMutationTarget){
                if(m_symbols.findGlobalConstant(expr.text) || __hidden_adhd_type_checker::HasConstValueTemplateParam(ownerClass, &method, expr.text))
                    reportError(errors, expr.span, "cannot mutate 'const' variable");
                else
                    reportError(errors, expr.span, "cannot mutate non-variable identifier");
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
        const ScopedVarInfo* lambdaThis = m_inLambda ? findScopedVar(scope, "this") : nullptr;
        if(m_inLambda && !lambdaThis){
            reportError(errors, expr.span, "accessing 'this' inside a lambda requires capturing 'in this' or 'mut this'");
            ok = false;
        }
        const bool isConstThis = method.isConst || (lambdaThis && lambdaThis->isConst);
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
            if(isConstThis){
                reportError(errors, expr.span, "cannot mutate 'this.member' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
            else if(!method.isConstructor && field && field->type && field->type->refMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' reference member");
                ok = false;
            }
            else if(m_parallelDepth > 0u && field && field->type && __hidden_adhd_type_checker::IsNonThreadSafeContainer(m_symbols.resolveBaseTypeName(field->type->name))){
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
                const MStringView localTypeName = !local->inferredTypeName.empty()
                    ? local->inferredTypeName
                    : (local->type ? m_symbols.resolveBaseTypeName(local->type->name) : MStringView{});
                if(!localTypeName.empty() && m_symbols.findEnum(localTypeName))
                    expr.scopeChain[0] = localTypeName;
            }
            else{
                const MStringView resolvedRoot = m_symbols.resolveBaseTypeName(expr.scopeChain[0]);
                if(!resolvedRoot.empty() && m_symbols.findEnum(resolvedRoot))
                    expr.scopeChain[0] = resolvedRoot;
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
            else{
                reportError(errors, expr.span, "invalid scope resolution '::'; only 'enum' types and 'enum' variables support '::' access in adhd");
                ok = false;
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
        const TypeRef* invokedFuncType = nullptr;
        const usize callArgCount = expr.arguments.size();
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
                targetCallable = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount);
                if(!targetCallable){
                    localCallable = findScopedVar(scope, expr.lhs->text);
                    if(localCallable && localCallable->type)
                        invokedFuncType = m_symbols.unwrapTypeAlias(localCallable->type);
                }
                if(expr.lhs->text == "wait"){
                    reportError(errors, expr.span, "'wait' can only be used as a standalone join statement");
                    ok = false;
                }
                if(baseCallee == "Path"){
                    for(Expr* arg : expr.arguments){
                        if(!arg)
                            continue;
                        const MStringView argType = inferExprTypeName(ownerClass, &method, *arg, scope);
                        if(arg->kind == ExprKind::StringLiteral || argType == "String" || argType == "StringView" || argType == "CompactString")
                            arg->adaptToTChar = true;
                    }
                }
            }
            else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
                targetCallable = findClassMethod(*ownerClass, expr.lhs->text, &callArgCount);
                if(!targetCallable){
                    if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text))
                        invokedFuncType = m_symbols.unwrapTypeAlias(field->type);
                }
                const ScopedVarInfo* lambdaThis = m_inLambda ? findScopedVar(scope, "this") : nullptr;
                const bool isConstThis = method.isConst || (lambdaThis && lambdaThis->isConst);
                if(isConstThis && targetCallable && !targetCallable->isConst){
                    reportError(errors, expr.span, "cannot call non-'const' method on 'this' inside a 'const' method or 'in this' lambda");
                    ok = false;
                }
            }
            else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
                const MStringView objType = inferExprTypeName(ownerClass, &method, *expr.lhs->lhs, scope);
                const MStringView mName = expr.lhs->text;
                const bool isMutatingContainerOp = (mName == "push_back" || mName == "emplace_back" || mName == "pop_back" || mName == "clear" || mName == "insert" || mName == "erase" || mName == "resize");
                if(const ClassDecl* targetCls = m_symbols.findClass(objType)){
                    targetCallable = findClassMethod(*targetCls, mName, &callArgCount);
                    if(!targetCallable){
                        if(const FieldDecl* field = findClassField(*targetCls, mName))
                            invokedFuncType = m_symbols.unwrapTypeAlias(field->type);
                    }
                }
                else if(m_symbols.findInterface(objType))
                    targetCallable = m_symbols.findInterfaceMethod(objType, mName, &callArgCount);
                else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, &method, objType)){
                    for(const MStringView& boundName : tp->interfaceBounds){
                        if(const MethodDecl* boundMethod = m_symbols.findInterfaceMethod(boundName, mName, &callArgCount)){
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

        const TypeRef* callReturnType = nullptr;
        if(targetCallable)
            callReturnType = targetCallable->returnType;
        else if(invokedFuncType && invokedFuncType->isFuncType)
            callReturnType = invokedFuncType->funcReturnType;
        else if(localCallable && localCallable->lambdaExpr)
            callReturnType = localCallable->lambdaExpr->lambdaReturnType;

        if(callReturnType && callReturnType->refMode != ParamPassingMode::None)
            expr.derefNotNull = true;

        if(isMutationTarget && callReturnType){
            if(callReturnType->refMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                ok = false;
            }
            else if(callReturnType->refMode == ParamPassingMode::None){
                reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
                ok = false;
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
                else if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(expr.lhs->text)){
                    if(!checkTemplateArgBounds(aliasDecl->templateParams, expr.callTemplateArgs, ownerClass, &method, expr.span, errors))
                        ok = false;
                }
            }
        }
        for(const TypeArg& targ : expr.callTemplateArgs){
            if(targ.type){
                if(!checkTypeRef(*targ.type, ownerClass, &method, errors))
                    ok = false;
                const TypeRef* unwrappedArg = m_symbols.unwrapTypeAlias(targ.type);
                const ParamPassingMode::Enum argRefMode = targ.type->refMode != ParamPassingMode::None
                    ? targ.type->refMode
                    : (unwrappedArg ? unwrappedArg->refMode : ParamPassingMode::None);
                if(argRefMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(targ.type->name))){
                    reportError(errors, targ.type->span.line > 0u ? targ.type->span : expr.span, "template type argument of 'interface' type must specify 'in' or 'mut'; interfaces cannot be stored or instantiated by value");
                    ok = false;
                }
            }
        }

        for(usize i = 0u; i < expr.arguments.size(); ++i){
            Expr* arg = expr.arguments[i];
            if(!arg)
                continue;
            bool paramIsMut = false;
            MStringView expectedParamType;
            if(targetCallable && i < targetCallable->params.size()){
                paramIsMut = (targetCallable->params[i].mode == ParamPassingMode::Mut);
                if(targetCallable->params[i].type){
                    expectedParamType = m_symbols.resolveBaseTypeName(targetCallable->params[i].type->name);
                    __hidden_adhd_type_checker::PropagateFuncCaptureBytes(targetCallable->params[i].type, arg, scope, m_symbols);
                }
            }
            else if(invokedFuncType && invokedFuncType->isFuncType && i < invokedFuncType->funcParams.size()){
                paramIsMut = (invokedFuncType->funcParams[i].mode == ParamPassingMode::Mut);
                if(invokedFuncType->funcParams[i].type){
                    expectedParamType = m_symbols.resolveBaseTypeName(invokedFuncType->funcParams[i].type->name);
                    __hidden_adhd_type_checker::PropagateFuncCaptureBytes(invokedFuncType->funcParams[i].type, arg, scope, m_symbols);
                }
            }
            else if(localCallable && localCallable->lambdaExpr && i < localCallable->lambdaExpr->lambdaParams.size()){
                paramIsMut = (localCallable->lambdaExpr->lambdaParams[i].mode == ParamPassingMode::Mut);
                if(localCallable->lambdaExpr->lambdaParams[i].type){
                    expectedParamType = m_symbols.resolveBaseTypeName(localCallable->lambdaExpr->lambdaParams[i].type->name);
                    __hidden_adhd_type_checker::PropagateFuncCaptureBytes(localCallable->lambdaExpr->lambdaParams[i].type, arg, scope, m_symbols);
                }
            }
            arg->isMutArg = paramIsMut;
            if(expectedParamType == "Path"){
                const MStringView argType = inferExprTypeName(ownerClass, &method, *arg, scope);
                if(arg->kind == ExprKind::StringLiteral || argType == "String" || argType == "StringView" || argType == "CompactString")
                    arg->adaptToTChar = true;
            }
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
        bool rhsInClassArenaCtx = false;
        if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text)){
                const MStringView baseFieldTypeName = field->type ? m_symbols.resolveBaseTypeName(field->type->name) : MStringView{};
                if(field->type && SymbolTable::isArenaBackedContainer(baseFieldTypeName))
                    rhsInClassArenaCtx = true;
                if(field->type && field->type->refMode != ParamPassingMode::None && (method.isConstructor || m_symbols.findInterface(baseFieldTypeName))){
                    expr.lhs->derefNotNull = false;
                    expr.isNotNullRebind = true;
                    if(expr.rhs && m_symbols.findInterface(baseFieldTypeName)){
                        const MStringView rhsType = inferExprTypeName(ownerClass, &method, *expr.rhs, scope);
                        if(const ClassDecl* rhsCls = m_symbols.findClass(rhsType)){
                            if(!classImplementsInterface(*rhsCls, baseFieldTypeName)){
                                reportError(errors, expr.span, "cannot bind interface reference member to concrete class that does not implement the interface");
                                ok = false;
                            }
                        }
                    }
                }
                __hidden_adhd_type_checker::PropagateFuncCaptureBytes(field->type, expr.rhs, scope, m_symbols);
            }
        }
        if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text);
            if(local && local->type){
                const MStringView baseLocalTypeName = m_symbols.resolveBaseTypeName(local->type->name);
                if(expr.lhs->derefNotNull && m_symbols.findInterface(baseLocalTypeName)){
                    expr.lhs->derefNotNull = false;
                    expr.isNotNullRebind = true;
                    if(expr.rhs){
                        const MStringView rhsType = inferExprTypeName(ownerClass, &method, *expr.rhs, scope);
                        if(const ClassDecl* rhsCls = m_symbols.findClass(rhsType)){
                            if(!classImplementsInterface(*rhsCls, baseLocalTypeName)){
                                reportError(errors, expr.span, "cannot rebind interface reference to concrete class that does not implement the interface");
                                ok = false;
                            }
                        }
                    }
                }
                __hidden_adhd_type_checker::PropagateFuncCaptureBytes(local->type, expr.rhs, scope, m_symbols);
            }
        }
        if(expr.rhs){
            const bool savedClassArenaCtx = m_inClassArenaContext;
            if(rhsInClassArenaCtx)
                m_inClassArenaContext = true;
            if(!checkExpr(ownerClass, method, *expr.rhs, scope, false, errors))
                ok = false;
            m_inClassArenaContext = savedClassArenaCtx;
        }
        if(expr.lhs && expr.rhs){
            const MStringView lhsType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
            if(lhsType == "Path"){
                const MStringView rhsType = inferExprTypeName(ownerClass, &method, *expr.rhs, scope);
                if(expr.rhs->kind == ExprKind::StringLiteral || rhsType == "String" || rhsType == "StringView" || rhsType == "CompactString")
                    expr.rhs->adaptToTChar = true;
            }
        }
        break;
    }
    case ExprKind::Cast:
    case ExprKind::BitCast:{
        if(expr.targetType){
            if(!checkTypeRef(*expr.targetType, ownerClass, &method, errors))
                ok = false;
            if(m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.targetType->name))){
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
            if(!checkTypeRef(*expr.lambdaReturnType, ownerClass, &method, errors, true))
                ok = false;
            if(expr.lambdaReturnType->refMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.lambdaReturnType->name))){
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
        if(expr.lambdaReturnType && !__hidden_adhd_type_checker::IsVoidTypeRef(m_symbols.unwrapTypeAlias(expr.lambdaReturnType))){
            if(!__hidden_adhd_type_checker::StatementsContainReturnOrNative(expr.lambdaBody)){
                reportError(errors, expr.span, "non-void lambda must contain a 'return' statement");
                ok = false;
            }
        }
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
            if(!local->inferredTypeName.empty())
                return local->inferredTypeName;
            if(local->type)
                return m_symbols.resolveBaseTypeName(local->type->name);
        }
        if(const Stmt* gc = m_symbols.findGlobalConstant(expr.text); gc && gc->varType)
            return m_symbols.resolveBaseTypeName(gc->varType->name);
    }
    else if(expr.kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = findClassField(*ownerClass, expr.text)){
            if(field->type)
                return m_symbols.resolveBaseTypeName(field->type->name);
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
                    return m_symbols.resolveBaseTypeName(field->type->name);
            }
        }
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        u32 simdLanes = 0u;
        if(SymbolTable::isSimdVectorType(lhsType, simdLanes))
            return lhsType.substr(0u, 3u);
        if(const TypeRef* indexedType = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(indexedType->name);
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
        const usize callArgCount = expr.arguments.size();
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(m_symbols.findClass(baseCallee) || SymbolTable::isSimdType(baseCallee)
                || SymbolTable::isArenaBackedContainer(baseCallee) || m_symbols.isSelfDefaultedType(baseCallee))
            {
                return baseCallee;
            }
            if(const MethodDecl* fn = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount)){
                if(fn->returnType)
                    return m_symbols.resolveBaseTypeName(fn->returnType->name);
            }
            if(const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(local->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.resolveBaseTypeName(ft->funcReturnType->name);
                if(local->lambdaExpr && local->lambdaExpr->lambdaReturnType)
                    return m_symbols.resolveBaseTypeName(local->lambdaExpr->lambdaReturnType->name);
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
            if(const MethodDecl* m = findClassMethod(*ownerClass, expr.lhs->text, &callArgCount)){
                if(m->returnType)
                    return m_symbols.resolveBaseTypeName(m->returnType->name);
            }
            if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(field->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.resolveBaseTypeName(ft->funcReturnType->name);
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs->lhs, scope);
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                if(const MethodDecl* m = findClassMethod(*cls, expr.lhs->text, &callArgCount)){
                    if(m->returnType)
                        return m_symbols.resolveBaseTypeName(m->returnType->name);
                }
                if(const FieldDecl* field = findClassField(*cls, expr.lhs->text)){
                    if(const TypeRef* ft = m_symbols.unwrapTypeAlias(field->type); ft && ft->isFuncType && ft->funcReturnType)
                        return m_symbols.resolveBaseTypeName(ft->funcReturnType->name);
                }
            }
            else if(m_symbols.findInterface(lhsType)){
                if(const MethodDecl* m = m_symbols.findInterfaceMethod(lhsType, expr.lhs->text, &callArgCount)){
                    if(m->returnType)
                        return m_symbols.resolveBaseTypeName(m->returnType->name);
                }
            }
            else if(const TemplateParamDecl* tp = findTemplateParam(ownerClass, method, lhsType)){
                for(const MStringView& boundName : tp->interfaceBounds){
                    if(const MethodDecl* m = m_symbols.findInterfaceMethod(boundName, expr.lhs->text, &callArgCount)){
                        if(m->returnType)
                            return m_symbols.resolveBaseTypeName(m->returnType->name);
                    }
                }
            }
        }
    }
    else if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.targetType){
        return m_symbols.resolveBaseTypeName(expr.targetType->name);
    }
    else if(expr.kind == ExprKind::ScopeAccess && !expr.scopeChain.empty()){
        const MStringView resolvedRoot = m_symbols.resolveBaseTypeName(expr.scopeChain[0]);
        if(m_symbols.findEnum(resolvedRoot))
            return resolvedRoot;
    }
    else if(expr.kind == ExprKind::StringLiteral){
        return "StringView";
    }
    return {};
}

const TypeRef* TypeChecker::inferExprTypeRef(
    ClassDecl* ownerClass,
    const MethodDecl* method,
    const Expr& expr,
    const MVector<ScopedVarInfo>& scope
)const noexcept{
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = findScopedVar(scope, expr.text))
            return m_symbols.unwrapTypeAlias(local->type);
        if(const Stmt* gc = m_symbols.findGlobalConstant(expr.text))
            return m_symbols.unwrapTypeAlias(gc->varType);
    }
    else if(expr.kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = findClassField(*ownerClass, expr.text))
            return m_symbols.unwrapTypeAlias(field->type);
    }
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            if(const FieldDecl* field = findClassField(*cls, expr.text))
                return m_symbols.unwrapTypeAlias(field->type);
        }
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        if(const TypeRef* containerType = inferExprTypeRef(ownerClass, method, *expr.lhs, scope)){
            const MStringView baseName = m_symbols.resolveBaseTypeName(containerType->name);
            if((baseName == "Map" || baseName == "HashMap" || baseName == "ParallelHashMap")
                && containerType->templateArgs.size() >= 2u && containerType->templateArgs[1].type)
            {
                return m_symbols.unwrapTypeAlias(containerType->templateArgs[1].type);
            }
            if((baseName == "Array" || baseName == "Vector" || baseName == "FixedVector" || baseName == "ParallelVector" || baseName == "Deque" || baseName == "Span")
                && !containerType->templateArgs.empty() && containerType->templateArgs[0].type)
            {
                return m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type);
            }
        }
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        const usize oneArg = 1u;
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            if(const MethodDecl* opIdx = findClassMethod(*cls, "[]", &oneArg))
                return m_symbols.unwrapTypeAlias(opIdx->returnType);
        }
        else if(m_symbols.findInterface(lhsType)){
            if(const MethodDecl* opIdx = m_symbols.findInterfaceMethod(lhsType, "[]", &oneArg))
                return m_symbols.unwrapTypeAlias(opIdx->returnType);
        }
    }
    else if(expr.kind == ExprKind::Call && expr.lhs){
        const usize callArgCount = expr.arguments.size();
        if(expr.lhs->kind == ExprKind::Identifier){
            if(const MethodDecl* fn = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount))
                return m_symbols.unwrapTypeAlias(fn->returnType);
            if(const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(local->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(ft->funcReturnType);
                if(local->lambdaExpr && local->lambdaExpr->lambdaReturnType)
                    return m_symbols.unwrapTypeAlias(local->lambdaExpr->lambdaReturnType);
            }
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            if(const MethodDecl* m = findClassMethod(*ownerClass, expr.lhs->text, &callArgCount))
                return m_symbols.unwrapTypeAlias(m->returnType);
            if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(field->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(ft->funcReturnType);
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs->lhs, scope);
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                if(const MethodDecl* m = findClassMethod(*cls, expr.lhs->text, &callArgCount))
                    return m_symbols.unwrapTypeAlias(m->returnType);
                if(const FieldDecl* field = findClassField(*cls, expr.lhs->text)){
                    if(const TypeRef* ft = m_symbols.unwrapTypeAlias(field->type); ft && ft->isFuncType && ft->funcReturnType)
                        return m_symbols.unwrapTypeAlias(ft->funcReturnType);
                }
            }
            else if(m_symbols.findInterface(lhsType)){
                if(const MethodDecl* m = m_symbols.findInterfaceMethod(lhsType, expr.lhs->text, &callArgCount))
                    return m_symbols.unwrapTypeAlias(m->returnType);
            }
        }
    }
    else if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.targetType){
        return m_symbols.unwrapTypeAlias(expr.targetType);
    }
    return nullptr;
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

const MethodDecl* TypeChecker::findClassMethod(const ClassDecl& classDecl, const MStringView name, const usize* preferredArgCount)noexcept{
    const MethodDecl* firstMatch = nullptr;
    for(const MethodDecl* m : classDecl.methods){
        if(m->name == name){
            if(!preferredArgCount || m->params.size() == *preferredArgCount)
                return m;
            if(!firstMatch)
                firstMatch = m;
        }
    }
    return firstMatch;
}

void TypeChecker::reportError(MVector<ParseError>& errors, const SourceSpan span, const MStringView message)const{
    ReportAdhdDiagnostic(m_arena, errors, span, message);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

