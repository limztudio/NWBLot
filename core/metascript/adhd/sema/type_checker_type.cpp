// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeChecker::checkTemplateParamBounds(
    const MVector<TemplateParamDecl>& params,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    MVector<ParseError>& errors
){
    b8 ok = true;
    MethodDecl tempMethodScope(m_arena);
    if(!enclosingMethod)
        tempMethodScope.templateParams = params;
    const MethodDecl* effectiveMethod = enclosingMethod ? enclosingMethod : &tempMethodScope;

    for(usize i = 0u; i < params.size(); ++i){
        const TemplateParamDecl& param = params[i];
        for(usize j = 0u; j < i; ++j){
            if(params[j].name == param.name){
                reportError(errors, param.span, "duplicate template parameter name");
                ok = false;
                break;
            }
        }
        if(param.constValueType && !checkTypeRef(*param.constValueType, enclosingClass, effectiveMethod, errors))
            ok = false;
        for(usize bIdx = 0u; bIdx < param.interfaceBounds.size(); ++bIdx){
            const TypeRef* boundRef = param.interfaceBounds[bIdx];
            if(!boundRef)
                continue;
            const TypeRef* resolvedBound = m_symbols.unwrapTypeAlias(boundRef);
            const MStringView boundName = resolvedBound ? resolvedBound->name : boundRef->name;
            for(usize prevB = 0u; prevB < bIdx; ++prevB){
                if(param.interfaceBounds[prevB] && AreTypeRefsEquivalent(param.interfaceBounds[prevB], boundRef, m_symbols)){
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
            else if(!checkTypeRef(*boundRef, enclosingClass, effectiveMethod, errors)){
                ok = false;
            }
        }
    }
    return ok;
}

b8 TypeChecker::interfaceInheritsFrom(const TypeRef& ifaceRef, const TypeRef& requiredIface, const u32 depth)const noexcept{
    if(depth > 16u)
        return false;
    const TypeRef* unwrappedIface = m_symbols.unwrapTypeAlias(&ifaceRef);
    const TypeRef* unwrappedReq = m_symbols.unwrapTypeAlias(&requiredIface);
    if(!unwrappedIface || !unwrappedReq)
        return false;

    TypeRef cleanIface = *unwrappedIface;
    cleanIface.refMode = ParamPassingMode::None;
    TypeRef cleanReq = *unwrappedReq;
    cleanReq.refMode = ParamPassingMode::None;

    if(cleanIface.name == cleanReq.name){
        if(cleanReq.templateArgs.empty() || AreTypeRefsEquivalent(&cleanIface, &cleanReq, m_symbols))
            return true;
    }

    if(const InterfaceDecl* ifaceDecl = m_symbols.findInterface(cleanIface.name)){
        const b8 hasSubst = !ifaceDecl->templateParams.empty() && !cleanIface.templateArgs.empty();
        for(const TypeRef* rawBase : ifaceDecl->baseInterfaces){
            if(!rawBase)
                continue;
            const TypeRef* subBase = hasSubst
                ? m_symbols.substituteTypeParams(*rawBase, ifaceDecl->templateParams, cleanIface.templateArgs)
                : rawBase;
            if(subBase && interfaceInheritsFrom(*subBase, cleanReq, depth + 1u))
                return true;
        }
    }
    return false;
}

b8 TypeChecker::classImplementsInterface(const ClassDecl& cls, const TypeRef* implClassRef, const TypeRef& requiredIface)const noexcept{
    const TypeRef* unwrappedReq = m_symbols.unwrapTypeAlias(&requiredIface);
    if(!unwrappedReq)
        return false;

    const TypeRef* unwrappedImplCls = implClassRef ? m_symbols.unwrapTypeAlias(implClassRef) : nullptr;
    const b8 hasClsSubst = unwrappedImplCls && !cls.templateParams.empty() && !unwrappedImplCls->templateArgs.empty();
    for(const TypeRef* rawImpl : cls.implementedInterfaces){
        if(!rawImpl)
            continue;
        const TypeRef* subImpl = hasClsSubst
            ? m_symbols.substituteTypeParams(*rawImpl, cls.templateParams, unwrappedImplCls->templateArgs)
            : rawImpl;
        if(subImpl && interfaceInheritsFrom(*subImpl, *unwrappedReq))
            return true;
    }
    return false;
}

b8 TypeChecker::checkTemplateArgBounds(
    const MVector<TemplateParamDecl>& params,
    const MVector<TypeArg>& args,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    const SourceSpan span,
    MVector<ParseError>& errors
){
    b8 ok = true;
    if(args.size() != params.size()){
        reportError(errors, span, "template argument count does not match template parameter list");
        ok = false;
    }
    const usize count = params.size() < args.size() ? params.size() : args.size();
    for(usize i = 0u; i < count; ++i){
        const TemplateParamDecl& param = params[i];
        const TypeArg& arg = args[i];
        if(param.constValueType){
            if(!NormalizeConstantTemplateArg(const_cast<TypeArg&>(arg), enclosingClass, enclosingMethod, m_symbols)){
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
        b8 isDependentParam = false;
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
        const TypeRef* resolvedArgType = m_symbols.unwrapTypeAlias(arg.type);
        for(const TypeRef* boundRef : param.interfaceBounds){
            if(!boundRef)
                continue;
            const TypeRef* subBound = m_symbols.substituteTypeParams(*boundRef, params, args);
            if(!argClass || !subBound || !classImplementsInterface(*argClass, resolvedArgType, *subBound)){
                reportError(errors, span, "template type argument does not implement required interface bound");
                ok = false;
            }
        }
    }
    return ok;
}

b8 TypeChecker::checkTemplateArgTypes(
    const MVector<TypeArg>& templateArgs,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    const SourceSpan fallbackSpan,
    MVector<ParseError>& errors
){
    b8 ok = true;
    for(const TypeArg& targ : templateArgs){
        if(targ.type){
            if(!checkTypeRef(*targ.type, enclosingClass, enclosingMethod, errors))
                ok = false;
            const ParamPassingMode::Enum argRefMode = m_symbols.resolveRefMode(targ.type);
            if(argRefMode == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(targ.type->name))){
                reportError(errors, targ.type->span.line > 0u ? targ.type->span : fallbackSpan, "template type argument of 'interface' type must specify 'in' or 'mut'; interfaces cannot be stored or instantiated by value");
                ok = false;
            }
        }
    }
    return ok;
}

b8 TypeChecker::checkTypeRef(
    const TypeRef& typeRef,
    const ClassDecl* enclosingClass,
    const MethodDecl* enclosingMethod,
    MVector<ParseError>& errors,
    const b8 allowVoid
){
    b8 ok = true;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(&typeRef);
    const b8 isVoid = IsVoidTypeRef(&typeRef) || IsVoidTypeRef(unwrapped);
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
    else if(typeRef.refMode != ParamPassingMode::None){
        if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(typeRef.name)){
            if(m_symbols.resolveRefMode(aliasDecl->aliasedType) != ParamPassingMode::None){
                reportError(errors, typeRef.span, "cannot apply 'in' or 'mut' to a 'type' alias that already specifies a reference mode");
                ok = false;
            }
        }
    }
    if(typeRef.isFuncType){
        if(typeRef.funcReturnType){
            if(!checkTypeRef(*typeRef.funcReturnType, enclosingClass, enclosingMethod, errors, true))
                ok = false;
            if(m_symbols.resolveRefMode(typeRef.funcReturnType) == ParamPassingMode::None && m_symbols.findInterface(m_symbols.resolveBaseTypeName(typeRef.funcReturnType->name))){
                reportError(errors, typeRef.span, "cannot return an 'interface' type by value");
                ok = false;
            }
        }
        for(const FuncParamType& fp : typeRef.funcParams){
            if(fp.type && !checkTypeRef(*fp.type, enclosingClass, enclosingMethod, errors))
                ok = false;
        }
    }
    if(IsSingleTypeParamBuiltinGeneric(typeRef.name)){
        if(typeRef.templateArgs.size() != 1u || !typeRef.templateArgs[0].type){
            reportError(errors, typeRef.span, "built-in generic type requires exactly 1 type argument");
            ok = false;
        }
    }
    else if(IsTwoTypeParamBuiltinGeneric(typeRef.name)){
        if(typeRef.templateArgs.size() != 2u || !typeRef.templateArgs[0].type || !typeRef.templateArgs[1].type){
            reportError(errors, typeRef.span, "built-in generic type requires exactly 2 type arguments");
            ok = false;
        }
    }
    else if(IsTypeAndCountBuiltinGeneric(typeRef.name)){
        if(typeRef.templateArgs.size() != 2u
            || !typeRef.templateArgs[0].type
            || !NormalizeConstantTemplateArg(const_cast<TypeArg&>(typeRef.templateArgs[1]), enclosingClass, enclosingMethod, m_symbols))
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

    if(!checkTemplateArgTypes(typeRef.templateArgs, enclosingClass, enclosingMethod, typeRef.span, errors))
        ok = false;
    return ok;
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

void TypeChecker::markStringToPathAdaptation(
    ClassDecl* ownerClass,
    const MethodDecl& method,
    Expr& expr,
    const MVector<ScopedVarInfo>& scope
)const noexcept{
    const MStringView exprType = inferExprTypeName(ownerClass, &method, expr, scope);
    if(expr.kind == ExprKind::StringLiteral || exprType == "String" || exprType == "StringView" || exprType == "CompactString")
        expr.adaptToTChar = true;
}

b8 TypeChecker::verifyConcreteClassImplementsInterface(
    ClassDecl* ownerClass,
    const MethodDecl& method,
    const Expr& rhsExpr,
    const TypeRef* expectedInterfaceType,
    const MVector<ScopedVarInfo>& scope,
    const SourceSpan errorSpan,
    const MStringView errorMessage,
    MVector<ParseError>& errors
){
    const TypeRef* rhsTypeRef = inferExprTypeRef(ownerClass, &method, rhsExpr, scope);
    const MStringView rhsType = rhsTypeRef
        ? m_symbols.resolveBaseTypeName(rhsTypeRef->name)
        : inferExprTypeName(ownerClass, &method, rhsExpr, scope);
    if(const ClassDecl* rhsCls = m_symbols.findClass(rhsType)){
        const TypeRef* resolvedExpected = m_symbols.unwrapTypeAlias(expectedInterfaceType);
        if(!resolvedExpected || !classImplementsInterface(*rhsCls, rhsTypeRef, *resolvedExpected)){
            reportError(errors, errorSpan, errorMessage);
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
