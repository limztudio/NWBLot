// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>
#include "type_layout_analyzer.h"

#include "callable_argument_checker.h"
#include "borrow_lifetime_analyzer.h"
#include "concrete_type_analyzer.h"
#include "const_eval.h"
#include "lambda_capture_analyzer.h"
#include "type_comparator.h"
#include "type_resolver.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CallableArgumentChecker::requiresRetainedClassTemporary(const TypeRef& expected, const TypeRef& actual)const noexcept{
    const SymbolTable& symbols = m_checker.m_symbols;
    const TypeRef* target = symbols.unwrapTypeAlias(&expected);
    const TypeRef* source = symbols.unwrapTypeAlias(&actual);
    if(!target || !source)
        return false;
    const ClassDecl* targetClass = symbols.findClass(target->name);
    if(!targetClass || targetClass->isExtern)
        return false;
    const TypeComparator types(symbols);
    if(target->name == source->name && types.areTemplateArgListsEqual(target->templateArgs, source->templateArgs))
        return false;
    const ClassDecl* sourceClass = symbols.findClass(source->name);
    if(sourceClass && m_checker.classImplementsInterface(*sourceClass, source, *target))
        return false;
    for(const MethodDecl* ctor : targetClass->methods){
        if(!ctor->isConstructor || ctor->params.size() != 1u || !ctor->params.front().type)
            continue;
        const TypeRef* input = symbols.substituteTypeParams(ctor->params.front().type, targetClass->templateParams, target);
        input = symbols.unwrapTypeAlias(input);
        if(input && ((input->name == source->name && types.areTemplateArgListsEqual(input->templateArgs, source->templateArgs))
            || (sourceClass && m_checker.classImplementsInterface(*sourceClass, source, *input))))
            return true;
    }
    return false;
}


b8 CallableArgumentChecker::checkBuiltinVisitor(
    ClassDecl* const ownerClass,
    const Span<const ScopedVarInfo> scope,
    MethodDecl& method,
    Expr& arg,
    const BuiltinVisitorRuleInfo& rule,
    const b8 receiverIsConst,
    MVector<ParseError>& errors
){
    const b8 readsKeyValue = !rule.keyType.empty();

    const Expr* lambda = arg.kind == ExprKind::Lambda ? &arg : nullptr;
    const MethodDecl* function = nullptr;
    const TypeRef* callableType = m_checker.m_symbols.unwrapTypeAlias(m_checker.inferExprTypeRef(ownerClass, &method, arg, scope));
    if(callableType && !callableType->isFuncType)
        callableType = nullptr;
    b8 directFunction = false;
    if(arg.kind == ExprKind::Identifier){
        const ScopedVarInfo* local = m_checker.FindScopedVar(scope, arg.text);
        if(local && !callableType){
            lambda = local->lambdaExpr;
            function = local->callableFunction;
        }
        else if(!local && !callableType && !m_checker.m_symbols.findGlobalConstant(arg.text)){
            b8 ambiguous = false;
            function = m_checker.m_symbols.findUniqueFreeFunction(arg.text, ambiguous);
            if(ambiguous){
                m_checker.reportError(errors, arg.span, "visitor must have an unambiguous callable signature");
                return false;
            }
            directFunction = function != nullptr;
        }
    }
    if(!lambda && !callableType && !function){
        m_checker.reportError(errors, arg.span, "visitor must have a known callable signature");
        return false;
    }
    if(directFunction)
        arg.resolvedCppNamespace = function->cppNamespace;
    const usize expectedCount = readsKeyValue ? 2u : 1u;
    const usize actualCount = lambda ? lambda->lambdaParams.size() : (callableType ? callableType->funcParams.size() : function->params.size());
    if(actualCount != expectedCount){
        m_checker.reportError(errors, arg.span, "visitor has the wrong number of parameters");
        return false;
    }
    for(usize i = 0u; i < expectedCount; ++i){
        const TypeRef* parameterType = lambda ? lambda->lambdaParams[i].type : (callableType ? callableType->funcParams[i].type : function->params[i].type);
        const ParamPassingMode::Enum mode = lambda ? lambda->lambdaParams[i].mode : (callableType ? callableType->funcParams[i].mode : function->params[i].mode);
        const b8 key = readsKeyValue && i == 0u;
        const MStringView expectedType = key ? rule.keyType : rule.valueType;
        const b8 genericLambdaParameter = lambda && parameterType && parameterType->isAuto;
        if(!parameterType || (!genericLambdaParameter && (m_checker.m_symbols.hasTypeSymbol(expectedType) || m_checker.m_symbols.resolveBaseTypeName(parameterType->name) != expectedType))){
            m_checker.reportError(errors, arg.span, "visitor parameter has the wrong type");
            return false;
        }
        if(mode != ParamPassingMode::In && (key || !rule.mutableWithReceiver || receiverIsConst || mode != ParamPassingMode::Mut)){
            m_checker.reportError(errors, arg.span, "visitor cannot mutate a read-only value");
            return false;
        }
    }
    const TypeRef* returnType = lambda ? lambda->lambdaReturnType : (callableType ? callableType->funcReturnType : function->returnType);
    if((lambda && lambda->lambdaInjectsCallerArena) || (function && function->injectsCallerArena) || (returnType && TypeLayoutAnalyzer(m_checker.m_symbols).requiresMotherArena(*returnType))){
        m_checker.reportError(errors, arg.span, "visitor return value cannot require a caller arena");
        return false;
    }
    return true;
}


b8 CallableArgumentChecker::check(
    ClassDecl* ownerClass,
    const Span<const ScopedVarInfo> scope,
    const MethodDecl* targetCallable,
    const ClassDecl* targetOwnerCls,
    const TypeRef* recvTypeRef,
    const BuiltinReceiverSignatureInfo* builtinReceiverSignature,
    const b8 builtinReceiverIsConst,
    const b8 calleeSignatureKnown,
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
    const b8 arityValid = targetCallable && targetCallable->acceptsArgCount(arguments.size());
    if(targetCallable && !arityValid){
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
        const b8 retainsBorrow = targetCallable ? (i < targetCallable->params.size() && targetCallable->params[i].retainsBorrow)
            : builtinReceiverSignature ? (i < builtinReceiverSignature->arity && (builtinReceiverSignature->retainedArgMask & (1u << i)) != 0u)
            : builtinFnInfo && i < builtinFnInfo->arity && (builtinFnInfo->retainedArgMask & (1u << i)) != 0u;
        if(retainsBorrow){
            arg->isRetainedArg = true;
            const BorrowLifetimeAnalyzer lifetimes(m_checker);
            if(lifetimes.isTemporaryOwnerExpr(ownerClass, &method, *arg, scope, true)){
                m_checker.reportError(errors, arg->span, "retained reference requires a stable lvalue");
                ok = false;
            }
            const ScopedVarInfo* owner = lifetimes.localReferenceOwner(ownerClass, method, *arg, scope, true);
            if(owner && owner->isParam && owner->type && m_checker.m_symbols.isSmallValueType(owner->type->name)){
                m_checker.reportError(errors, arg->span, "a retained reference cannot bind to a copied parameter; declare the parameter as 'in borrow'");
                ok = false;
            }
        }
        if(targetCallable && i < targetCallable->params.size()){
            const ParamDecl& param = targetCallable->params[i];
            const b8 forwardedTemporary = param.forwardsArgument && m_checker.isRvalueTemporaryExpr(ownerClass, &method, *arg, scope);
            paramIsMut = param.mode == ParamPassingMode::Mut && !param.copiesArgument && !forwardedTemporary;
            if(targetCallable->params[i].type){
                TypeRef* subParamType = targetCallable->params[i].type;
                if(targetOwnerCls)
                    subParamType = m_checker.m_symbols.substituteTypeParams(subParamType, targetOwnerCls->templateParams, recvTypeRef);
                if(!targetCallable->templateParams.empty() && !callTemplateArgs.empty())
                    subParamType = m_checker.m_symbols.substituteTypeParams(*subParamType, targetCallable->templateParams, callTemplateArgs);
                expectedParamTypeRef = subParamType;
                expectedParamType = m_checker.m_symbols.resolveBaseTypeName(subParamType->name);
                LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(targetCallable->params[i].type, arg);
                if(arg->wrapInFuncType && subParamType != targetCallable->params[i].type){
                    if(const TypeRef* unwrappedSubFunc = m_checker.m_symbols.unwrapTypeAlias(subParamType); unwrappedSubFunc && unwrappedSubFunc->isFuncType)
                        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(const_cast<TypeRef*>(unwrappedSubFunc), arg);
                    else if(TypeRef* extractedSubFunc = TypeResolver(m_checker.m_symbols).extractUnderlyingFuncType(subParamType))
                        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(extractedSubFunc, arg);
                }
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
            if(builtinFnInfo && i < builtinFnInfo->arity && (builtinFnInfo->explicitTypeParamMask & (1u << i)) && callTemplateArgs.size() == 1u && callTemplateArgs[0].type){
                expectedParamTypeRef = callTemplateArgs[0].type;
                expectedParamType = m_checker.m_symbols.resolveBaseTypeName(expectedParamTypeRef->name);
                const TypeRef* actualRef = nullptr;
                const MStringView actualBase = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope, &actualRef);
                const TypeRef* expected = m_checker.m_symbols.unwrapTypeAlias(expectedParamTypeRef);
                const TypeRef* actual = m_checker.m_symbols.unwrapTypeAlias(actualRef);
                const TypeComparator types(m_checker.m_symbols);
                const b8 callableTypes = actual && expected && (actual->isFuncType || expected->isFuncType);
                const b8 mismatched = callableTypes ? !types.areEquivalent(actual, expected)
                    : ((!actualBase.empty() && actualBase != expectedParamType) || (actual && expected && !types.areTemplateArgListsEqual(actual->templateArgs, expected->templateArgs)));
                if(mismatched){
                    m_checker.reportError(errors, arg->span, "argument type does not match the explicit type parameter");
                    ok = false;
                }
                if(paramIsMut && !m_checker.isPotentiallyMutableLvalueExpr(ownerClass, method, *arg, scope)){
                    m_checker.reportError(errors, arg->span, "a mutable type parameter requires a mutable lvalue");
                    ok = false;
                }
            }
        }
        if(retainsBorrow && expectedParamTypeRef){
            const TypeRef* actual = nullptr;
            const MStringView actualBase = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope, &actual);
            if(!actualBase.empty() && actual && requiresRetainedClassTemporary(*expectedParamTypeRef, *actual)){
                m_checker.reportError(errors, arg->span, "a retained reference cannot bind to a temporary created by an implicit class conversion");
                ok = false;
            }
        }
        if(!calleeSignatureKnown && m_checker.isPotentiallyMutableLvalueExpr(ownerClass, method, *arg, scope))
            paramIsMut = true;
        const b8 wrapContainerRefElement = !targetCallable
            && !invokedFuncType
            && !localCallable
            && expectedParamTypeRef
            && m_checker.m_symbols.resolveRefMode(expectedParamTypeRef) != ParamPassingMode::None;
        arg->isMutArg = paramIsMut || IsBorrowViewType(expectedParamType);
        if(i == 0u && ((builtinFnInfo && builtinFnInfo->callLowering == BuiltinCallLowering::PlatformLiteralArg0)
            || (targetCallable && targetCallable->isExtern && targetCallable->callLoweringKind == static_cast<u8>(ExternCallableFlagKind::PlatformLiteralArg0)))){
            if(arg->kind != ExprKind::StringLiteral){
                m_checker.reportError(errors, arg->span, "platform text requires a string literal");
                ok = false;
            }
            else
                arg->adaptToTChar = true;
        }
        if(RequiresTCharAdaptation(expectedParamType))
            m_checker.markStringToPathAdaptation(ownerClass, &method, scope, *arg);
        if(!m_checker.checkExpr(ownerClass, scope, paramIsMut, method, *arg, errors))
            ok = false;
        if(builtinArgRule){
            if(const BuiltinVisitorRuleInfo* visitor = FindBuiltinVisitorRule(builtinArgRule->rule); visitor && !checkBuiltinVisitor(ownerClass, scope, method, *arg, *visitor, builtinReceiverIsConst, errors))
                ok = false;
        }
        if(wrapContainerRefElement)
            arg->wrapInNotNull = true;
        if(builtinFnInfo && builtinFnInfo->wrapsFormatArgs && i >= builtinFnInfo->minArity)
            m_checker.markFormatArgWrap(ownerClass, &method, *arg, scope, false);
        m_checker.markSimdResultWrapperAdaptation(expectedParamTypeRef, *arg);
        if(builtinFnInfo && (builtinFnInfo->returnRule == BuiltinFunctionReturnRule::PromoteSimd || builtinFnInfo->returnRule == BuiltinFunctionReturnRule::LaneScalar || builtinFnInfo->returnRule == BuiltinFunctionReturnRule::MatrixMultiply || builtinFnInfo->rejectsSimdStorage)){
            const MStringView actualArgType = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope);
            if(SymbolTable::IsSimdOrUnalignedStorageType(actualArgType)){
                m_checker.reportError(errors, arg->span, "SIMD math intrinsics require 'VectorSingle', 'VectorDouble', 'MatrixSingle', or 'MatrixDouble'; load memory-layout values first");
                ok = false;
            }
        }
        if(builtinArgRule && !builtinArgRule->errorMessage.empty()){
            const TypeRef* actualRef = nullptr;
            const MStringView actualArgType = m_checker.inferExprBaseType(ownerClass, &method, *arg, scope, &actualRef);
            const b8 isKnownAdhdType = !actualArgType.empty()
                && (BuiltinTypeSemantics::IsBuiltinTypeName(actualArgType) || m_checker.m_symbols.hasTypeSymbol(actualArgType));
            b8 exactTypeMismatch = !builtinArgRule->requiredExactType.empty() && isKnownAdhdType && actualArgType != builtinArgRule->requiredExactType;
            if(exactTypeMismatch && m_checker.m_symbols.isPolymorphicBaseBuiltinType(builtinArgRule->requiredExactType)){
                TypeRef required(m_checker.m_arena);
                required.name = builtinArgRule->requiredExactType;
                if(const ClassDecl* cls = m_checker.m_symbols.findClass(actualArgType))
                    exactTypeMismatch = !m_checker.classImplementsInterface(*cls, actualRef, required);
                else if(actualRef)
                    exactTypeMismatch = !m_checker.interfaceInheritsFrom(*actualRef, required);
            }
            b8 typeMismatch = exactTypeMismatch
                || (builtinArgRule->requiresStringLike && isKnownAdhdType && !(builtinArgRule->isMut ? IsOwningStringType(actualArgType) : IsStringLikeType(actualArgType)))
                || (builtinArgRule->requiresIntegerScalar && isKnownAdhdType && !BuiltinTypeSemantics::IsIntegerScalarType(actualArgType))
                || (IsNumericScalarArgRule(builtinArgRule->rule) && isKnownAdhdType && !BuiltinTypeSemantics::IsNumericScalarType(actualArgType));
            if(const BuiltinContainerArgConstraintInfo* constraint = FindContainerArgConstraint(builtinArgRule->rule); !typeMismatch && constraint){
                if(constraint->isExactElementCheck){
                    const TypeRef* containerType = actualRef ? m_checker.m_symbols.unwrapTypeAlias(actualRef) : nullptr;
                    const TypeRef* elementType = containerType && containerType->templateArgs.size() == constraint->templateArgCount && containerType->templateArgs[0].type ? m_checker.m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type) : nullptr;
                    typeMismatch = !elementType || m_checker.m_symbols.resolveBaseTypeName(elementType->name) != constraint->elementTypeName;
                }
                else if(isKnownAdhdType && constraint->checksContiguousSequence){
                    const b8 isContiguous = actualArgType == builtinArgRule->expectedParamType || DoesImplicitConversionPropagateFirstTypeArg(actualArgType, builtinArgRule->expectedParamType);
                    const TypeRef* containerType = actualRef ? m_checker.m_symbols.unwrapTypeAlias(actualRef) : nullptr;
                    const TypeRef* elementType = containerType && !containerType->templateArgs.empty() && containerType->templateArgs[0].type ? m_checker.m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type) : nullptr;
                    const b8 lowersByteView = builtinFnInfo && ((i == 0u && builtinFnInfo->callLowering == BuiltinCallLowering::BinaryByteViewArg0)
                        || (i == 1u && builtinFnInfo->callLowering == BuiltinCallLowering::BinaryByteViewArg1));
                    const b8 alreadyByteView = lowersByteView && actualArgType == "BinaryByteView" && !m_checker.m_symbols.hasTypeSymbol(actualArgType);
                    typeMismatch = !alreadyByteView && (!isContiguous || !elementType || m_checker.m_symbols.resolveBaseTypeName(elementType->name) != constraint->elementTypeName);
                }
                else if(isKnownAdhdType && !constraint->countText.empty()){
                    const TypeRef* arrayType = actualRef ? m_checker.m_symbols.unwrapTypeAlias(actualRef) : nullptr;
                    const TypeRef* elementType = arrayType && arrayType->templateArgs.size() == constraint->templateArgCount && arrayType->templateArgs[0].type ? m_checker.m_symbols.unwrapTypeAlias(arrayType->templateArgs[0].type) : nullptr;
                    const MStringView countText = arrayType && arrayType->templateArgs.size() == constraint->templateArgCount ? arrayType->templateArgs[1].constantOrTypeName() : MStringView{};
                    const b8 dependentCount = HasConstValueTemplateParam(ownerClass, &method, countText);
                    typeMismatch = !elementType || m_checker.m_symbols.resolveBaseTypeName(elementType->name) != constraint->elementTypeName
                        || (!dependentCount && !AreConstantTemplateArgsEquivalent(countText, constraint->countText, m_checker.m_symbols));
                }
            }
            if(typeMismatch){
                m_checker.reportError(errors, arg->span, builtinArgRule->errorMessage);
                ok = false;
            }
        }
        else if(!targetCallable && FindSpecialMethodSemantics(calledMemberName) == SpecialMethodKind::Substr){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

