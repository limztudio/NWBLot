// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "local_variable_checker.h"
#include "constructor_argument_checker.h"
#include "borrow_lifetime_analyzer.h"
#include "lambda_capture_analyzer.h"
#include "type_layout_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LocalVariableChecker::check(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors){
    b8 ok = true;
    const b8 isAutoDeclaration = stmt.varType && stmt.varType->isAuto;
    if(!stmt.name.empty() && m_checker.FindScopedVar(scope, stmt.name)){
        m_checker.reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
        ok = false;
    }
    const ParamPassingMode::Enum varRefMode = m_checker.m_symbols.resolveRefMode(stmt.varType);
    if(stmt.varType){
        if(!m_checker.checkTypeRef(*stmt.varType, ownerClass, &method, errors))
            ok = false;
        const MStringView baseVarTypeName = m_checker.m_symbols.resolveBaseTypeName(stmt.varType->name);
        if(m_checker.m_symbols.isInterfaceOrAbstractType(baseVarTypeName)){
            if(varRefMode == ParamPassingMode::None){
                m_checker.reportError(errors, stmt.span, "local variable of 'interface' type must specify 'in' or 'mut'; interfaces cannot be instantiated by value");
                ok = false;
            }
            else if(stmt.expr && m_checker.m_symbols.findInterface(baseVarTypeName) && !m_checker.verifyConcreteClassImplementsInterface(
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
        b8 isSelfDefaultedVar = m_checker.m_symbols.isSelfDefaultedType(stmt.varType->name, ownerClass);
        if(isSelfDefaultedVar && !m_checker.m_symbols.hasTypeSymbol(baseVarTypeName) && IsFixedArrayContainerType(baseVarTypeName)){
            if(const TypeRef* unwrappedArr = m_checker.m_symbols.unwrapTypeAlias(stmt.varType); unwrappedArr && !unwrappedArr->templateArgs.empty() && unwrappedArr->templateArgs[0].type){
                const TypeRef* elemRef = unwrappedArr->templateArgs[0].type;
                const MStringView elemBase = m_checker.m_symbols.resolveBaseTypeName(elemRef->name);
                if(m_checker.m_symbols.resolveRefMode(elemRef) != ParamPassingMode::None || (m_checker.m_symbols.findClass(elemBase) && !m_checker.m_symbols.isSelfDefaultedType(elemBase, ownerClass)))
                    isSelfDefaultedVar = false;
            }
        }
        if(!stmt.expr && !stmt.isCtorInit && (varRefMode != ParamPassingMode::None || !isSelfDefaultedVar)){
            m_checker.reportError(errors, stmt.span, "uninitialized variable declaration is prohibited in adhd; every local variable must be initialized at declaration");
            ok = false;
        }
        if(varRefMode == ParamPassingMode::None){
            const TypeLayoutAnalyzer layoutAnalyzer(m_checker.m_symbols);
            const b8 isArenaContainer = !m_checker.m_symbols.hasTypeSymbol(baseVarTypeName) && SymbolTable::IsArenaBackedContainer(baseVarTypeName);
            const b8 isNonRootClass = !isArenaContainer && layoutAnalyzer.isNonRootMotherArenaType(*stmt.varType);
            const b8 isArenaDefaultArray = !stmt.expr && !stmt.isCtorInit && layoutAnalyzer.isFixedArrayNeedingArenaDefaultInit(*stmt.varType);
            const b8 isReboundValueInit = (stmt.expr != nullptr) && !stmt.varType->isAuto && layoutAnalyzer.requiresMotherArena(*stmt.varType);
            b8 callerPassedArena = false;
            if(stmt.isCtorInit && !stmt.arguments.empty()){
                auto isExplicitArenaArg = [](const Expr& argExpr, const MStringView inferredType)noexcept -> b8{
                    if(IsArenaObjectType(inferredType))
                        return true;
                    if(!inferredType.empty())
                        return false;
                    if((argExpr.kind == ExprKind::MemberAccess || argExpr.kind == ExprKind::ThisMember) && KeywordSemantics::IsUnresolvedArenaMemberName(argExpr.text))
                        return true;
                    if(argExpr.kind == ExprKind::Identifier && KeywordSemantics::IsUnresolvedArenaVarName(argExpr.text))
                        return true;
                    return false;
                };
                if(isNonRootClass){
                    const ClassDecl* ctorCls = m_checker.m_symbols.findClass(baseVarTypeName);
                    b8 hasUserCtor = false;
                    if(ctorCls){
                        for(const MethodDecl* m : ctorCls->methods){
                            if(m && m->isConstructor){
                                hasUserCtor = true;
                                break;
                            }
                        }
                    }
                    if(ctorCls && !hasUserCtor){
                        const MStringView arg0Type = (stmt.arguments.size() == 1u && stmt.arguments[0])
                            ? m_checker.inferExprBaseType(ownerClass, &method, *stmt.arguments[0], scope)
                            : MStringView{};
                        if(stmt.arguments.size() >= 2u || (stmt.arguments.size() == 1u && stmt.arguments[0] && TypeChecker::DoesSynthesizedClassCtorPassArena(*ctorCls, baseVarTypeName, stmt.arguments.size(), arg0Type)))
                            callerPassedArena = true;
                    }
                }
                else if(isArenaContainer && baseVarTypeName != BuiltinTypeSemantics::PathTypeName() && stmt.arguments.size() == 1u && stmt.arguments[0]){
                    const MStringView arg0Type = m_checker.inferExprBaseType(ownerClass, &method, *stmt.arguments[0], scope);
                    if(isExplicitArenaArg(*stmt.arguments[0], arg0Type))
                        callerPassedArena = true;
                }
            }
            if(!callerPassedArena && (isArenaContainer || isNonRootClass || isArenaDefaultArray || isReboundValueInit)){
                stmt.usesScratchArena = true;
                stmt.arenaFirstInit = (!m_checker.m_symbols.hasTypeSymbol(baseVarTypeName) && IsArenaFirstCtorType(baseVarTypeName)) || isNonRootClass;
                if(ownerClass && !ownerClass->assetTypeName.empty())
                    ownerClass->needsClassArena = true;
                else
                    m_checker.markScratchArenaUsed(method);
            }
        }
        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(stmt.varType, stmt.expr);
        if(stmt.isCtorInit){
            for(const Expr* arg : stmt.arguments){
                if(arg)
                    LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(stmt.varType, arg);
            }
        }
        m_checker.maybeAdaptInitToTChar(ownerClass, method, scope, baseVarTypeName, stmt.expr, stmt.isCtorInit ? Span<Expr* const>(stmt.arguments.data(), stmt.arguments.size()) : Span<Expr* const>{});
    }

    if(stmt.expr){
        const b8 isMutRefBinding = (varRefMode == ParamPassingMode::Mut);
        if(!m_checker.checkExpr(ownerClass, scope, isMutRefBinding, method, *stmt.expr, errors))
            ok = false;
        if(varRefMode == ParamPassingMode::In){
            if(m_checker.isRvalueTemporaryExpr(ownerClass, &method, *stmt.expr, scope)){
                m_checker.reportError(errors, stmt.span, "cannot bind 'in' reference variable to a literal or rvalue temporary");
                ok = false;
            }
            else{
                PromoteStringByteReadToConstRef(*stmt.expr);
            }
        }
        if(stmt.varType && stmt.varType->isAuto && !stmt.expr->resolvedSimdType.empty()){
            stmt.varType->isAuto = false;
            stmt.varType->name = stmt.expr->resolvedSimdType;
        }
        if(stmt.varType && !stmt.varType->isAuto && varRefMode == ParamPassingMode::None)
            m_checker.markSimdResultWrapperAdaptation(stmt.varType, *stmt.expr);
    }

    if(!ConstructorArgumentChecker(m_checker).check(
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
    if(stmt.isCtorInit && stmt.varType && varRefMode == ParamPassingMode::None && stmt.arguments.size() == 1u && stmt.arguments[0])
        m_checker.markSimdResultWrapperAdaptation(stmt.varType, *stmt.arguments[0]);

    ScopedVarInfo info;
    info.name = stmt.name;
    info.type = stmt.varType;
    if(stmt.varType && stmt.varType->isAuto && stmt.expr && stmt.expr->kind != ExprKind::Lambda){
        info.inferredTypeName = m_checker.inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
        const TypeRef* exprTypeRef = m_checker.inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
        const MStringView effectiveAutoName = exprTypeRef
            ? m_checker.m_symbols.resolveBaseTypeName(exprTypeRef->name)
            : info.inferredTypeName;
        if(varRefMode == ParamPassingMode::None && !effectiveAutoName.empty() && m_checker.m_symbols.isInterfaceOrAbstractType(effectiveAutoName)){
            m_checker.reportError(errors, stmt.span, "local variable of 'interface' type must specify 'in' or 'mut'; interfaces cannot be instantiated by value");
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
            if(TypeLayoutAnalyzer(m_checker.m_symbols).requiresMotherArena(*stmt.inferredAutoType)){
                stmt.usesScratchArena = true;
                m_checker.markScratchArenaUsed(method);
            }
        }
    }
    info.mode = varRefMode;
    info.isConst = stmt.isConst || (varRefMode == ParamPassingMode::In);
    info.isParam = false;
    info.declaredParallelDepth = m_checker.m_parallelDepth;
    const TypeRef* const declaredType = m_checker.m_symbols.unwrapTypeAlias(info.type);
    const b8 tracksCallable = isAutoDeclaration || (declaredType && declaredType->isFuncType);
    if(tracksCallable && stmt.expr && stmt.expr->kind == ExprKind::Lambda)
        info.lambdaExpr = stmt.expr;
    else if(tracksCallable && stmt.expr && stmt.expr->kind == ExprKind::Identifier){
        if(const ScopedVarInfo* source = m_checker.FindScopedVar(scope, stmt.expr->text)){
            info.lambdaExpr = source->lambdaExpr;
            info.callableFunction = source->callableFunction;
        }
        else if(!m_checker.m_symbols.findGlobalConstant(stmt.expr->text)){
            b8 ambiguous = false;
            info.callableFunction = m_checker.m_symbols.findUniqueFreeFunction(stmt.expr->text, ambiguous);
            if(info.callableFunction)
                stmt.expr->resolvedCppNamespace = info.callableFunction->cppNamespace;
        }
    }
    const MStringView effectiveVarBase = m_checker.resolveScopedVarBaseType(info);
    const Expr* stringViewInitExpr = stmt.expr
        ? stmt.expr
        : (stmt.isCtorInit && !stmt.arguments.empty() ? stmt.arguments[0] : nullptr);
    if(IsStringBorrowViewType(effectiveVarBase) && stringViewInitExpr){
        if(BorrowLifetimeAnalyzer(m_checker).borrowsTemporaryStringOwner(ownerClass, &method, *stringViewInitExpr, scope)){
            m_checker.reportError(errors, stmt.span, "cannot bind 'StringView' variable to a temporary owning string");
            ok = false;
        }
        else{
            MStringView borrowedOwnerName;
            if(BorrowLifetimeAnalyzer(m_checker).borrowsLocalStringOwner(ownerClass, &method, *stringViewInitExpr, scope, &borrowedOwnerName)){
                info.borrowsLocalStringOwner = true;
                info.borrowedLocalOwnerName = borrowedOwnerName;
            }
        }
    }
    scope.push_back(info);
    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

