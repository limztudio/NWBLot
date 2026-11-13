// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "callable_argument_checker.h"
#include "concrete_type_analyzer.h"
#include "const_eval.h"
#include "lambda_capture_analyzer.h"
#include "type_comparator.h"
#include "type_resolver.h"


NWB_ADHD_BEGIN


b8 CallableArgumentChecker::check(
    ClassDecl* ownerClass,
    const Span<const ScopedVarInfo> scope,
    const MethodDecl* targetCallable,
    const ClassDecl* targetOwnerCls,
    const TypeRef* recvTypeRef,
    const BuiltinReceiverSignatureInfo* builtinReceiverSignature,
    const Span<const TypeArg> callTemplateArgs,
    const TypeRef* invokedFuncType,
    const ScopedVarInfo* localCallable,
    const MStringView calledMemberName,
    MethodDecl& method,
    MVector<Expr*>& arguments,
    MVector<ParseError>& errors
){
    b8 ok = true;
    const SourceSpan errSpan = !arguments.empty() && arguments[0] ? arguments[0]->span : method.span;
    if(targetCallable && targetCallable->params.size() != arguments.size()){
        m_checker.reportError(errors, errSpan, "wrong number of arguments in call");
        ok = false;
    }
    else if(invokedFuncType && invokedFuncType->isFuncType && invokedFuncType->funcParams.size() != arguments.size()){
        m_checker.reportError(errors, errSpan, "wrong number of arguments in Func call");
        ok = false;
    }
    else if(localCallable && localCallable->lambdaExpr && localCallable->lambdaExpr->lambdaParams.size() != arguments.size()){
        m_checker.reportError(errors, errSpan, "wrong number of arguments in lambda call");
        ok = false;
    }
    if(builtinReceiverSignature && arguments.size() != builtinReceiverSignature->arity){
        m_checker.reportError(errors, errSpan, "wrong number of arguments in built-in method call");
        ok = false;
    }
    for(usize i = 0u; i < arguments.size(); ++i){
        Expr* arg = arguments[i];
        if(!arg)
            continue;
        b8 paramIsMut = false;
        TypeRef* expectedParamTypeRef = nullptr;
        MStringView expectedParamType;
        const BuiltinFunctionInfo* builtinFnInfo = (!recvTypeRef && !builtinReceiverSignature && !calledMemberName.empty())
            ? FindBuiltinFunction(calledMemberName)
            : nullptr;
        const BuiltinFunctionArgRuleInfo* builtinArgRule = (builtinReceiverSignature && i < builtinReceiverSignature->argRules.size())
            ? FindBuiltinFunctionArgRule(builtinReceiverSignature->argRules[i])
            : ((builtinFnInfo && i < builtinFnInfo->argRules.size()) ? FindBuiltinFunctionArgRule(builtinFnInfo->argRules[i]) : nullptr);
        if(targetCallable && i < targetCallable->params.size()){
            paramIsMut = (targetCallable->params[i].mode == ParamPassingMode::Mut);
            if(targetCallable->params[i].type){
                TypeRef* subParamType = targetCallable->params[i].type;
                if(targetOwnerCls)
                    subParamType = m_checker.m_symbols.substituteTypeParams(subParamType, targetOwnerCls->templateParams, recvTypeRef);
                if(!targetCallable->templateParams.empty() && !callTemplateArgs.empty())
                    subParamType = m_checker.m_symbols.substituteTypeParams(*subParamType, targetCallable->templateParams, callTemplateArgs);
                expectedParamTypeRef = subParamType;
                expectedParamType = m_checker.m_symbols.resolveBaseTypeName(subParamType->name);
                LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(targetCallable->params[i].type, arg);
                if(targetOwnerCls){
                    if(const TypeRef* paramFunc = TypeResolver(m_checker.m_symbols).extractUnderlyingFuncType(targetCallable->params[i].type); paramFunc && paramFunc->inferredCaptureBytes > 0u){
                        for(FieldDecl* clsField : targetOwnerCls->fields){
                            if(!clsField || !clsField->type)
                                continue;
                            if(TypeRef* fieldFunc = TypeResolver(m_checker.m_symbols).extractUnderlyingFuncType(clsField->type); fieldFunc && fieldFunc->inferredCaptureBytes < paramFunc->inferredCaptureBytes)
                                fieldFunc->inferredCaptureBytes = paramFunc->inferredCaptureBytes;
                        }
                        for(MethodDecl* clsMethod : targetOwnerCls->methods){
                            if(!clsMethod || !clsMethod->isConstructor)
                                continue;
                            for(ParamDecl& ctorParam : clsMethod->params){
                                if(!ctorParam.type)
                                    continue;
                                if(TypeRef* ctorParamFunc = TypeResolver(m_checker.m_symbols).extractUnderlyingFuncType(ctorParam.type); ctorParamFunc && ctorParamFunc->inferredCaptureBytes < paramFunc->inferredCaptureBytes)
                                    ctorParamFunc->inferredCaptureBytes = paramFunc->inferredCaptureBytes;
                            }
                        }
                    }
                }
            }
        }
        else if(invokedFuncType && invokedFuncType->isFuncType && i < invokedFuncType->funcParams.size()){
            paramIsMut = (invokedFuncType->funcParams[i].mode == ParamPassingMode::Mut);
            if(invokedFuncType->funcParams[i].type){
                expectedParamTypeRef = invokedFuncType->funcParams[i].type;
                expectedParamType = m_checker.m_symbols.resolveBaseTypeName(invokedFuncType->funcParams[i].type->name);
                LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(invokedFuncType->funcParams[i].type, arg);
            }
        }
        else if(localCallable && localCallable->lambdaExpr && i < localCallable->lambdaExpr->lambdaParams.size()){
            paramIsMut = (localCallable->lambdaExpr->lambdaParams[i].mode == ParamPassingMode::Mut);
            if(localCallable->lambdaExpr->lambdaParams[i].type){
                expectedParamTypeRef = localCallable->lambdaExpr->lambdaParams[i].type;
                expectedParamType = m_checker.m_symbols.resolveBaseTypeName(localCallable->lambdaExpr->lambdaParams[i].type->name);
                LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(localCallable->lambdaExpr->lambdaParams[i].type, arg);
            }
        }
        else if(recvTypeRef && !recvTypeRef->templateArgs.empty() && !m_checker.m_symbols.hasTypeSymbol(m_checker.m_symbols.resolveBaseTypeName(recvTypeRef->name))){
            if(const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(calledMemberName)){
                if(i == 0u && memberInfo->firstArgIsMut)
                    paramIsMut = true;
                if(i == 0u && memberInfo->adaptsFirstArgToElement && recvTypeRef->templateArgs[0].type){
                    expectedParamTypeRef = recvTypeRef->templateArgs[0].type;
                    expectedParamType = m_checker.m_symbols.resolveBaseTypeName(expectedParamTypeRef->name);
                    if(m_checker.m_symbols.resolveRefMode(expectedParamTypeRef) == ParamPassingMode::Mut)
                        paramIsMut = true;
                    LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(expectedParamTypeRef, arg);
                }
                else if(i == 1u && memberInfo->adaptsSecondArgToElement && recvTypeRef->templateArgs.size() >= 2u && recvTypeRef->templateArgs[1].type){
                    expectedParamTypeRef = recvTypeRef->templateArgs[1].type;
                    expectedParamType = m_checker.m_symbols.resolveBaseTypeName(expectedParamTypeRef->name);
                    if(m_checker.m_symbols.resolveRefMode(expectedParamTypeRef) == ParamPassingMode::Mut)
                        paramIsMut = true;
                    LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(expectedParamTypeRef, arg);
                }
            }
        }
        else if(builtinArgRule){
            expectedParamType = builtinArgRule->expectedParamType;
            paramIsMut = builtinArgRule->isMut;
        }
        const b8 wrapContainerRefElement = !targetCallable
            && !invokedFuncType
            && !localCallable
            && expectedParamTypeRef
            && m_checker.m_symbols.resolveRefMode(expectedParamTypeRef) != ParamPassingMode::None;
        arg->isMutArg = paramIsMut || IsBorrowViewType(expectedParamType);
        if(RequiresTCharAdaptation(expectedParamType))
            m_checker.markStringToPathAdaptation(ownerClass, &method, scope, *arg);
        if(!m_checker.checkExpr(ownerClass, scope, paramIsMut, method, *arg, errors))
            ok = false;
        if(wrapContainerRefElement)
            arg->wrapInNotNull = true;
        if(builtinFnInfo && builtinFnInfo->wrapsFormatArgs && i >= builtinFnInfo->minArity){
            const MStringView fmtArgType = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope);
            if(!m_checker.m_symbols.hasTypeSymbol(fmtArgType)){
                if(const BuiltinObjectTypeInfo* fmtObj = FindBuiltinObjectType(fmtArgType))
                    arg->formatArgWrapKind = fmtObj->formatArgWrapKind;
            }
        }
        if(expectedParamTypeRef){
            const TypeRef* unwrappedParam = m_checker.m_symbols.unwrapTypeAlias(expectedParamTypeRef);
            if(unwrappedParam && !unwrappedParam->templateArgs.empty() && unwrappedParam->templateArgs[0].type && !m_checker.m_symbols.hasTypeSymbol(unwrappedParam->name)){
                if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(unwrappedParam->name); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional)){
                    const MStringView valBase = m_checker.m_symbols.resolveBaseTypeName(unwrappedParam->templateArgs[0].type->name);
                    if(SymbolTable::IsSimdRegisterVectorType(valBase) && arg->resolvedSimdType == valBase)
                        arg->wrapInSimdResult = true;
                }
            }
        }
        if(builtinFnInfo && (builtinFnInfo->returnRule == BuiltinFunctionReturnRule::PromoteSimd || builtinFnInfo->returnRule == BuiltinFunctionReturnRule::LaneScalar || builtinFnInfo->returnRule == BuiltinFunctionReturnRule::MatrixMultiply || builtinFnInfo->rejectsSimdStorage)){
            const MStringView actualArgType = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope);
            if(SymbolTable::IsSimdStorageType(actualArgType) || SymbolTable::IsUnalignedVectorType(actualArgType)){
                m_checker.reportError(errors, arg->span, "SIMD math intrinsics require 'VectorSingle', 'VectorDouble', 'MatrixSingle', or 'MatrixDouble'; load memory-layout values first");
                ok = false;
            }
        }
        if(builtinArgRule && !builtinArgRule->errorMessage.empty()){
            const TypeRef* actualRef = nullptr;
            const MStringView actualArgType = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope, &actualRef);
            const b8 isKnownAdhdType = !actualArgType.empty()
                && (BuiltinTypeSemantics::IsBuiltinTypeName(actualArgType) || m_checker.m_symbols.hasTypeSymbol(actualArgType));
            b8 typeMismatch = (!builtinArgRule->requiredExactType.empty() && isKnownAdhdType && actualArgType != builtinArgRule->requiredExactType)
                || (builtinArgRule->requiresStringLike && isKnownAdhdType && !(builtinArgRule->isMut ? IsOwningStringType(actualArgType) : IsStringLikeType(actualArgType)))
                || (builtinArgRule->requiresIntegerScalar && isKnownAdhdType && !BuiltinTypeSemantics::IsIntegerScalarType(actualArgType))
                || (builtinArgRule->rule == BuiltinFunctionArgRule::NumericScalar && isKnownAdhdType && !BuiltinTypeSemantics::IsNumericScalarType(actualArgType));
            if(!typeMismatch && (builtinArgRule->rule == BuiltinFunctionArgRule::UsizeSpan || builtinArgRule->rule == BuiltinFunctionArgRule::ByteSpan || builtinArgRule->rule == BuiltinFunctionArgRule::MutByteFixedVector)){
                const TypeRef* containerType = actualRef ? m_checker.m_symbols.unwrapTypeAlias(actualRef) : nullptr;
                const usize expectedArguments = builtinArgRule->rule == BuiltinFunctionArgRule::MutByteFixedVector ? 2u : 1u;
                const TypeRef* elementType = containerType && containerType->templateArgs.size() == expectedArguments && containerType->templateArgs[0].type ? m_checker.m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type) : nullptr;
                const MStringView requiredElement = builtinArgRule->rule == BuiltinFunctionArgRule::UsizeSpan ? MStringView("usize") : MStringView("u8");
                typeMismatch = !elementType || m_checker.m_symbols.resolveBaseTypeName(elementType->name) != requiredElement;
            }
            const b8 checksByteSequence = builtinArgRule->rule == BuiltinFunctionArgRule::ReadableByteSequence || builtinArgRule->rule == BuiltinFunctionArgRule::MutableByteSequence;
            const b8 checksU32Sequence = builtinArgRule->rule == BuiltinFunctionArgRule::MutableU32Sequence;
            const b8 checksU64Sequence = builtinArgRule->rule == BuiltinFunctionArgRule::MutableU64Sequence;
            if(!typeMismatch && isKnownAdhdType && (checksByteSequence || checksU32Sequence || checksU64Sequence)){
                const b8 isContiguous = actualArgType == builtinArgRule->expectedParamType || DoesImplicitConversionPropagateFirstTypeArg(actualArgType, builtinArgRule->expectedParamType);
                const TypeRef* containerType = actualRef ? m_checker.m_symbols.unwrapTypeAlias(actualRef) : nullptr;
                const TypeRef* elementType = containerType && !containerType->templateArgs.empty() && containerType->templateArgs[0].type ? m_checker.m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type) : nullptr;
                const MStringView requiredElement = checksU32Sequence ? MStringView("u32") : (checksU64Sequence ? MStringView("u64") : MStringView("u8"));
                typeMismatch = !isContiguous || !elementType || m_checker.m_symbols.resolveBaseTypeName(elementType->name) != requiredElement;
            }
            if(!typeMismatch && isKnownAdhdType && builtinArgRule->rule == BuiltinFunctionArgRule::MutableU64Array64){
                const TypeRef* arrayType = actualRef ? m_checker.m_symbols.unwrapTypeAlias(actualRef) : nullptr;
                const TypeRef* elementType = arrayType && arrayType->templateArgs.size() == 2u && arrayType->templateArgs[0].type ? m_checker.m_symbols.unwrapTypeAlias(arrayType->templateArgs[0].type) : nullptr;
                const MStringView countText = arrayType && arrayType->templateArgs.size() == 2u ? arrayType->templateArgs[1].constantOrTypeName() : MStringView{};
                const b8 dependentCount = HasConstValueTemplateParam(ownerClass, &method, countText);
                typeMismatch = !elementType || m_checker.m_symbols.resolveBaseTypeName(elementType->name) != "u64"
                    || (!dependentCount && !AreConstantTemplateArgsEquivalent(countText, "64", m_checker.m_symbols));
            }
            if(typeMismatch){
                m_checker.reportError(errors, arg->span, builtinArgRule->errorMessage);
                ok = false;
            }
        }
        else if(!targetCallable && calledMemberName == "substr"){
            const MStringView actualArgType = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope);
            if(!actualArgType.empty() && !BuiltinTypeSemantics::IsIntegerScalarType(actualArgType)){
                m_checker.reportError(errors, arg->span, "arguments to 'substr' must have integer types");
                ok = false;
            }
        }
        if(expectedParamTypeRef && !m_checker.m_symbols.hasTypeSymbol(expectedParamType) && FindBuiltinGenericType(expectedParamType) && IsBorrowViewType(expectedParamType)){
            const TypeRef* actualRef = nullptr;
            const MStringView actualArgType = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope, &actualRef);
            if(!m_checker.m_symbols.hasTypeSymbol(actualArgType) && FindBuiltinGenericType(actualArgType)){
                const b8 canBorrow = actualArgType == expectedParamType || DoesImplicitConversionPropagateFirstTypeArg(actualArgType, expectedParamType);
                if(!canBorrow){
                    m_checker.reportError(errors, arg->span, "argument cannot be converted to the required borrowed view");
                    ok = false;
                }
                else{
                    const TypeRef* expectedView = m_checker.m_symbols.unwrapTypeAlias(expectedParamTypeRef);
                    const TypeRef* actualContainer = actualRef ? m_checker.m_symbols.unwrapTypeAlias(actualRef) : nullptr;
                    const TypeRef* expectedElement = expectedView && !expectedView->templateArgs.empty() ? m_checker.m_symbols.unwrapTypeAlias(expectedView->templateArgs[0].type) : nullptr;
                    const TypeRef* actualElement = actualContainer && !actualContainer->templateArgs.empty() ? m_checker.m_symbols.unwrapTypeAlias(actualContainer->templateArgs[0].type) : nullptr;
                    const ConcreteTypeAnalyzer concrete(m_checker.m_symbols);
                    const auto isFullyConcrete = [&](const auto& self, const TypeRef* type)->b8{
                        type = type ? m_checker.m_symbols.unwrapTypeAlias(type) : nullptr;
                        if(!type || !concrete.isConcreteTypeRef(*type))
                            return false;
                        for(const TypeArg& typeArg : type->templateArgs){
                            if(typeArg.type && !self(self, typeArg.type))
                                return false;
                        }
                        return true;
                    };
                    if(isFullyConcrete(isFullyConcrete, expectedElement) && isFullyConcrete(isFullyConcrete, actualElement) && !TypeComparator(m_checker.m_symbols).areEquivalent(actualElement, expectedElement)){
                        m_checker.reportError(errors, arg->span, "argument element type does not match the required borrowed view");
                        ok = false;
                    }
                }
            }
        }
        if(!expectedParamType.empty() && m_checker.m_symbols.findInterface(expectedParamType)){
            if(!m_checker.verifyConcreteClassImplementsInterface(
                ownerClass,
                method,
                *arg,
                expectedParamTypeRef,
                scope,
                arg->span,
                "cannot pass concrete class to interface parameter: class does not implement the interface",
                errors
            )){
                ok = false;
            }
        }
        const auto checkUnboundTemplateParams = [&](const Span<const TemplateParamDecl> tparams){
            for(const TemplateParamDecl& tp : tparams){
                if(tp.constValueType || tp.interfaceBounds.empty())
                    continue;
                if(tp.name == expectedParamTypeRef->name && expectedParamTypeRef->templateArgs.empty()){
                    const TypeRef* argTypeRef = nullptr;
                    const MStringView argBase = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope, &argTypeRef);
                    MVector<TypeArg> singleArgList(m_checker.m_arena);
                    TypeArg singleArg;
                    if(argTypeRef){
                        singleArg.type = const_cast<TypeRef*>(argTypeRef);
                    }
                    else if(!argBase.empty()){
                        TypeRef* synth = AllocateAstNode<TypeRef>(m_checker.m_arena);
                        synth->name = argBase;
                        singleArg.type = synth;
                    }
                    singleArgList.push_back(singleArg);
                    const TemplateParamDecl singleParam = tp;
                    for(const TypeRef* boundRef : tp.interfaceBounds){
                        if(!boundRef)
                            continue;
                        const TypeRef* effectiveBound = singleArg.type
                            ? m_checker.m_symbols.substituteTypeParams(*boundRef, Span<const TemplateParamDecl>(&singleParam, 1u), singleArgList)
                            : boundRef;
                        if(!m_checker.verifyConcreteClassImplementsInterface(
                            ownerClass,
                            method,
                            *arg,
                            effectiveBound,
                            scope,
                            arg->span,
                            "template type argument does not implement required interface bound",
                            errors
                        )){
                            ok = false;
                        }
                    }
                }
                else if(!expectedParamTypeRef->templateArgs.empty()){
                    const TypeRef* argTypeRef = m_checker.inferExprTypeRef(ownerClass, &method, *arg, scope);
                    if(const TypeRef* deducedSub = TypeResolver(m_checker.m_symbols).deduceTemplateParamTypeRef(expectedParamTypeRef, argTypeRef, tp.name)){
                        MVector<TypeArg> singleArgList(m_checker.m_arena);
                        TypeArg singleArg;
                        singleArg.type = const_cast<TypeRef*>(deducedSub);
                        singleArgList.push_back(singleArg);
                        const TemplateParamDecl singleParam = tp;
                        if(!m_checker.checkTemplateArgBounds(Span<const TemplateParamDecl>(&singleParam, 1u), singleArgList, ownerClass, &method, arg->span, errors))
                            ok = false;
                    }
                }
            }
        };
        if(targetCallable && expectedParamTypeRef && callTemplateArgs.empty() && !targetCallable->templateParams.empty())
            checkUnboundTemplateParams(targetCallable->templateParams);
        if(targetOwnerCls && expectedParamTypeRef && (!recvTypeRef || recvTypeRef->templateArgs.empty()) && !targetOwnerCls->templateParams.empty())
            checkUnboundTemplateParams(targetOwnerCls->templateParams);
    }
    if(targetCallable && callTemplateArgs.empty() && !targetCallable->templateParams.empty()){
        for(const TemplateParamDecl& tp : targetCallable->templateParams){
            if(tp.constValueType)
                continue;
            MStringView firstDeducedBase;
            for(usize i = 0u; i < arguments.size() && i < targetCallable->params.size(); ++i){
                const Expr* arg = arguments[i];
                const TypeRef* pType = targetCallable->params[i].type;
                if(!arg || !pType)
                    continue;
                MStringView curDeducedBase;
                if(pType->name == tp.name && pType->templateArgs.empty()){
                    curDeducedBase = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope);
                }
                else if(!pType->templateArgs.empty()){
                    const TypeRef* argTypeRef = m_checker.inferExprTypeRef(ownerClass, &method, *arg, scope);
                    if(const TypeRef* sub = TypeResolver(m_checker.m_symbols).deduceTemplateParamTypeRef(pType, argTypeRef, tp.name))
                        curDeducedBase = m_checker.m_symbols.resolveBaseTypeName(sub->name);
                }
                if(curDeducedBase.empty())
                    continue;
                if(firstDeducedBase.empty()){
                    firstDeducedBase = curDeducedBase;
                }
                else if(firstDeducedBase != curDeducedBase && !(BuiltinTypeSemantics::IsNumericScalarType(firstDeducedBase) && BuiltinTypeSemantics::IsNumericScalarType(curDeducedBase))){
                    m_checker.reportError(errors, arg->span, "conflicting argument types deduced for template parameter");
                    ok = false;
                    break;
                }
            }
        }
    }
    return ok;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

