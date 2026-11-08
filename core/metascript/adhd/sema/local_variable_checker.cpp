// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "local_variable_checker.h"
#include "constructor_argument_checker.h"
#include "borrow_lifetime_analyzer.h"
#include "lambda_capture_analyzer.h"


NWB_ADHD_BEGIN


b8 LocalVariableChecker::check(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors){
    b8 ok = true;
    if(!stmt.name.empty() && m_checker.FindScopedVar(scope, stmt.name)){
        m_checker.reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
        ok = false;
    }
    const ParamPassingMode::Enum varRefMode = m_checker.m_symbols.resolveRefMode(stmt.varType);
    if(stmt.varType){
        if(!m_checker.checkTypeRef(*stmt.varType, ownerClass, &method, errors))
            ok = false;
        const MStringView baseVarTypeName = m_checker.m_symbols.resolveBaseTypeName(stmt.varType->name);
        if(m_checker.m_symbols.findInterface(baseVarTypeName)){
            if(varRefMode == ParamPassingMode::None){
                m_checker.reportError(errors, stmt.span, "local variable of 'interface' type must specify 'in' or 'mut'; interfaces cannot be instantiated by value");
                ok = false;
            }
            else if(stmt.expr && !m_checker.verifyConcreteClassImplementsInterface(
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
        if(isSelfDefaultedVar && IsFixedArrayContainerType(baseVarTypeName)){
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
        if(varRefMode == ParamPassingMode::None && SymbolTable::IsArenaBackedContainer(baseVarTypeName)){
            stmt.usesScratchArena = true;
            method.usesScratchArena = true;
            m_checker.m_activeParallelBlockUsesScratch = true;
        }
        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(stmt.varType, stmt.expr);
        if(stmt.isCtorInit){
            for(const Expr* arg : stmt.arguments){
                if(arg)
                    LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(stmt.varType, arg);
            }
        }
        if(RequiresTCharAdaptation(baseVarTypeName)){
            if(stmt.expr)
                m_checker.markStringToPathAdaptation(ownerClass, &method, scope, *stmt.expr);
            if(stmt.isCtorInit){
                for(Expr* arg : stmt.arguments){
                    if(arg)
                        m_checker.markStringToPathAdaptation(ownerClass, &method, scope, *arg);
                }
            }
        }
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
            else if(stmt.expr->isStringByteRead){
                stmt.expr->isStringByteRead = false;
                stmt.expr->isStringByteConstRef = true;
            }
        }
        if(stmt.varType && stmt.varType->isAuto && !stmt.expr->resolvedSimdType.empty()){
            stmt.varType->isAuto = false;
            stmt.varType->name = stmt.expr->resolvedSimdType;
        }
        if(stmt.varType && !stmt.varType->isAuto && varRefMode == ParamPassingMode::None){
            if(const TypeRef* unwrappedVar = m_checker.m_symbols.unwrapTypeAlias(stmt.varType); unwrappedVar && !unwrappedVar->templateArgs.empty() && unwrappedVar->templateArgs[0].type){
                if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(unwrappedVar->name); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional)){
                    const MStringView valBase = m_checker.m_symbols.resolveBaseTypeName(unwrappedVar->templateArgs[0].type->name);
                    if(SymbolTable::IsSimdRegisterVectorType(valBase) && stmt.expr->resolvedSimdType == valBase)
                        stmt.expr->wrapInSimdResult = true;
                }
            }
        }
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
    if(stmt.isCtorInit && stmt.varType && varRefMode == ParamPassingMode::None && stmt.arguments.size() == 1u && stmt.arguments[0]){
        if(const TypeRef* unwrappedVar = m_checker.m_symbols.unwrapTypeAlias(stmt.varType); unwrappedVar && !unwrappedVar->templateArgs.empty() && unwrappedVar->templateArgs[0].type){
            if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(unwrappedVar->name); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional)){
                const MStringView valBase = m_checker.m_symbols.resolveBaseTypeName(unwrappedVar->templateArgs[0].type->name);
                if(SymbolTable::IsSimdRegisterVectorType(valBase) && stmt.arguments[0]->resolvedSimdType == valBase)
                    stmt.arguments[0]->wrapInSimdResult = true;
            }
        }
    }

    ScopedVarInfo info;
    info.name = stmt.name;
    info.type = stmt.varType;
    if(stmt.varType && stmt.varType->isAuto && stmt.expr && stmt.expr->kind != ExprKind::Lambda){
        info.inferredTypeName = m_checker.inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
        const TypeRef* exprTypeRef = m_checker.inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
        const MStringView effectiveAutoName = exprTypeRef
            ? m_checker.m_symbols.resolveBaseTypeName(exprTypeRef->name)
            : info.inferredTypeName;
        if(varRefMode == ParamPassingMode::None && !effectiveAutoName.empty() && m_checker.m_symbols.findInterface(effectiveAutoName)){
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
        }
    }
    info.mode = varRefMode;
    info.isConst = stmt.isConst || (varRefMode == ParamPassingMode::In);
    info.isParam = false;
    info.declaredParallelDepth = m_checker.m_parallelDepth;
    if(stmt.expr && stmt.expr->kind == ExprKind::Lambda)
        info.lambdaExpr = stmt.expr;
    const MStringView effectiveVarBase = m_checker.resolveScopedVarBaseType(info);
    const Expr* stringViewInitExpr = stmt.expr
        ? stmt.expr
        : (stmt.isCtorInit && !stmt.arguments.empty() ? stmt.arguments[0] : nullptr);
    if(effectiveVarBase == "StringView" && stringViewInitExpr){
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


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
