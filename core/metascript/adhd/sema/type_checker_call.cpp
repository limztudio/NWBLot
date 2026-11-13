// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "type_checker.h"
#include "callable_argument_checker.h"
#include "callable_template_deduction.h"

#include "borrow_lifetime_analyzer.h"
#include "lambda_capture_analyzer.h"
#include "type_layout_analyzer.h"
#include "type_resolver.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




b8 TypeChecker::checkCallExpr(ClassDecl* ownerClass, const Span<const ScopedVarInfo> scope, const b8 isMutationTarget, MethodDecl& method, Expr& expr, MVector<ParseError>& errors){
    b8 ok = true;
    expr.isBuiltinCall = false;

    if(expr.lhs && !checkExpr(ownerClass, scope, false, method, *expr.lhs, errors))
        ok = false;

    const MethodDecl* targetCallable = nullptr;
    const ClassDecl* targetOwnerCls = nullptr;
    const TypeRef* recvTypeRef = nullptr;
    MStringView calledMemberName;
    const BuiltinReceiverMethodInfo* targetBuiltinRecvInfo = nullptr;
    const BuiltinMemberMethodInfo* targetBuiltinMemberInfo = nullptr;
    MStringView targetBuiltinRecvObjType;
    const ScopedVarInfo* localCallable = nullptr;
    ScopedVarInfo tempInlineLambdaVar;
    const TypeRef* invokedFuncType = nullptr;
    const usize callArgCount = expr.arguments.size();
    MVector<MStringView> argTypeHints(m_arena);
    MVector<u8> argIsMutHints(m_arena);
    buildCallArgHints(ownerClass, &method, expr.arguments, scope, argTypeHints, argIsMutHints);
    auto injectActiveArena = [&]()noexcept{
        if((m_inClassArenaContext || (ownerClass && !ownerClass->assetTypeName.empty())) && ownerClass){
            expr.injectClassArena = true;
            ownerClass->needsClassArena = true;
        }
        else{
            expr.injectScratchArena = true;
            method.usesScratchArena = true;
            m_activeParallelBlockUsesScratch = true;
        }
    };
    if(expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(expr.lhs->text == "wait"){
                reportError(errors, expr.span, "'wait' can only be used as a standalone join statement");
                ok = false;
            }
            targetCallable = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount, argTypeHints, argIsMutHints);
            if(!targetCallable){
                localCallable = FindScopedVar(scope, expr.lhs->text);
                if(localCallable && localCallable->type)
                    invokedFuncType = m_symbols.unwrapTypeAlias(localCallable->type);
            }
            if(targetCallable)
                expr.lhs->resolvedCppNamespace = targetCallable->cppNamespace;
            if(targetCallable || localCallable){
                expr.isUserCallableCall = true;
            }
            else if(m_symbols.findInterface(baseCallee) || m_symbols.isAbstractBuiltinType(baseCallee)){
                recordBuiltinTypeHeader(baseCallee);
                reportError(errors, expr.span, "cannot instantiate an 'interface' type directly");
                ok = false;
            }
            else if(const ClassDecl* ctorCls = m_symbols.findClass(baseCallee)){
                targetOwnerCls = ctorCls;
                targetCallable = m_symbols.findClassConstructor(*ctorCls, callArgCount, ownerClass != ctorCls, argTypeHints, argIsMutHints);
                if(targetCallable && ownerClass != ctorCls && targetCallable->access != AccessSpecifier::Public){
                    reportError(errors, expr.span, "cannot call 'private' or 'protected' constructor outside of its declaring class");
                    ok = false;
                }
                if(!ctorCls->templateParams.empty() && !expr.callTemplateArgs.empty()){
                    TypeRef* tempClsRef = AllocateAstNode<TypeRef>(m_arena);
                    tempClsRef->name = baseCallee;
                    tempClsRef->templateArgs = expr.callTemplateArgs;
                    recvTypeRef = tempClsRef;
                }
                TypeRef constructedRef(m_arena);
                constructedRef.name = expr.lhs->text;
                constructedRef.templateArgs = expr.callTemplateArgs;
                if(TypeLayoutAnalyzer(m_symbols).isNonRootMotherArenaType(constructedRef)){
                    b8 callerPassedArena = false;
                    if(!targetCallable){
                        if(!ctorCls->assetTypeName.empty()){
                            if(callArgCount >= 2u)
                                callerPassedArena = true;
                            else if(callArgCount == 1u && !argTypeHints.empty() && argTypeHints[0] != "Name" && !IsStringLikeType(argTypeHints[0]) && argTypeHints[0] != baseCallee)
                                callerPassedArena = true;
                        }
                        else if(callArgCount >= 2u || (callArgCount == 1u && !argTypeHints.empty() && argTypeHints[0] != baseCallee)){
                            callerPassedArena = true;
                        }
                    }
                    if(!callerPassedArena)
                        injectActiveArena();
                }
            }
            else if(!m_symbols.hasTypeSymbol(baseCallee) && FindBuiltinFunction(expr.lhs->text) != nullptr){
                const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text);
                expr.isBuiltinCall = true;
                calledMemberName = expr.lhs->text;
                recordRequiredHeader(fnInfo->requiredHeader);
                recordBuiltinTypeHeader(fnInfo->returnTypeName);
                recordBuiltinTypeHeader(fnInfo->returnTypeArgName);
                recordBuiltinTypeHeader(fnInfo->returnNestedTypeArgName);
                recordBuiltinTypeHeader(fnInfo->returnErrorTypeName);
                if(fnInfo->prependsScratchArena)
                    injectActiveArena();
                const b8 badArity = fnInfo->isVariadic
                    ? (callArgCount < fnInfo->minArity)
                    : (callArgCount < fnInfo->minArity || callArgCount > fnInfo->arity);
                if(badArity){
                    reportError(errors, expr.span, "wrong number of arguments to built-in math/SIMD function");
                    ok = false;
                }
                const b8 requiresExplicitTypeArg = fnInfo->requiresExplicitTypeArg;
                if(requiresExplicitTypeArg && (expr.callTemplateArgs.size() != 1u || !expr.callTemplateArgs[0].type)){
                    reportError(errors, expr.span, "built-in function requires exactly 1 explicit type argument '<T>'");
                    ok = false;
                }
            }
            else{
                recordBuiltinTypeHeader(baseCallee);
                if(!m_symbols.hasTypeSymbol(baseCallee) && SymbolTable::IsArenaBackedContainer(baseCallee))
                    injectActiveArena();
                if(!m_symbols.hasTypeSymbol(baseCallee) && !IsValidBuiltinVectorOrMatrixCtorArity(baseCallee, callArgCount)){
                    reportError(errors, expr.span, "wrong number of arguments to built-in vector/matrix constructor");
                    ok = false;
                }
                else if(!m_symbols.hasTypeSymbol(baseCallee) && callArgCount == 2u && expr.arguments[0] && !SymbolTable::IsSimdRegisterVectorType(baseCallee)){
                    const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(baseCallee);
                    const BuiltinUnalignedVectorInfo* unalignedInfo = simdVecInfo ? nullptr : FindBuiltinUnalignedVectorType(baseCallee);
                    const b8 requiresSimdRegArg0 = (simdVecInfo && simdVecInfo->lanes == 4u && simdVecInfo->altCtorArity != 2u)
                        || (unalignedInfo && unalignedInfo->lanes == 4u && unalignedInfo->altCtorArity != 2u);
                    if(requiresSimdRegArg0 && !SymbolTable::IsSimdRegisterVectorType(inferExprTypeName(ownerClass, &method, *expr.arguments[0], scope))){
                        reportError(errors, expr.span, "wrong number of arguments to built-in vector/matrix constructor");
                        ok = false;
                    }
                }
                if(!m_symbols.hasTypeSymbol(baseCallee)){
                    if(const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(baseCallee); simdVecInfo && simdVecInfo->isMixed && callArgCount == 1u && expr.arguments[0]){
                        const MStringView arg0Type = inferExprTypeName(ownerClass, &method, *expr.arguments[0], scope);
                        if(arg0Type != baseCallee){
                            reportError(errors, expr.span, "mixed float/integer SIMD vector type does not support single-scalar splat construction; provide all 4 lane arguments");
                            ok = false;
                        }
                    }
                }
                if(!m_symbols.hasTypeSymbol(baseCallee) && RequiresTCharAdaptation(baseCallee)){
                    for(Expr* arg : expr.arguments){
                        if(arg)
                            markStringToPathAdaptation(ownerClass, &method, scope, *arg);
                    }
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            const b8 isConstThis = isThisConstInScope(method, scope);
            targetOwnerCls = ownerClass;
            targetCallable = m_symbols.findClassMethod(*ownerClass, expr.lhs->text, &callArgCount, isConstThis, false, argTypeHints, argIsMutHints);
            if(!targetCallable){
                const BuiltinReceiverMethodInfo* inheritedRecvInfo = nullptr;
                if(!ownerClass->assetTypeName.empty() || !ownerClass->subsystemTypeName.empty())
                    inheritedRecvInfo = FindBuiltinReceiverMethod("IAsset", expr.lhs->text);
                else if(!ownerClass->systemTypeName.empty())
                    inheritedRecvInfo = FindBuiltinReceiverMethod("ISystem", expr.lhs->text);
                if(!inheritedRecvInfo){
                    for(const TypeRef* iface : ownerClass->implementedInterfaces){
                        if(!iface)
                            continue;
                        const MStringView ifaceName = m_symbols.resolveBaseTypeName(iface->name);
                        if(!m_symbols.findInterface(ifaceName)){
                            inheritedRecvInfo = FindBuiltinReceiverMethod(ifaceName, expr.lhs->text);
                            if(inheritedRecvInfo)
                                break;
                        }
                    }
                }
                if(inheritedRecvInfo){
                    recordBuiltinTypeHeader(inheritedRecvInfo->returnTypeName);
                    recordBuiltinTypeHeader(inheritedRecvInfo->returnTypeArgName);
                    recordBuiltinTypeHeader(inheritedRecvInfo->returnNestedTypeArgName);
                    recordBuiltinTypeHeader(inheritedRecvInfo->returnErrorTypeName);
                    if(isConstThis && inheritedRecvInfo->isMutating){
                        reportError(errors, expr.span, "cannot call non-'const' method on 'this' inside a 'const' method or 'in this' lambda");
                        ok = false;
                    }
                }
                if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, expr.lhs->text)){
                    invokedFuncType = m_symbols.unwrapTypeAlias(field->type);
                    if(field->type && (!invokedFuncType || !invokedFuncType->isFuncType) && !FindTemplateParam(ownerClass, &method, field->type->name)){
                        reportError(errors, expr.span, "class member field is not a callable function");
                        ok = false;
                    }
                }
            }
            if(isConstThis && targetCallable && !targetCallable->isConst){
                reportError(errors, expr.span, "cannot call non-'const' method on 'this' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView objType = inferExprBaseType(ownerClass, &method, *expr.lhs->lhs, scope, &recvTypeRef);
            const MStringView mName = expr.lhs->text;
            calledMemberName = mName;
            const b8 recvIsConst = isReceiverConst(ownerClass, method, *expr.lhs->lhs, scope);
            const ClassDecl* targetCls = m_symbols.findClass(objType);
            const InterfaceDecl* targetIface = m_symbols.findInterface(objType);
            const b8 isAssetClassObj = (targetCls && (!targetCls->assetTypeName.empty() || !targetCls->subsystemTypeName.empty()))
                || (targetIface && !targetIface->assetTypeName.empty());
            const b8 isSystemClassObj = targetCls && !targetCls->systemTypeName.empty();
            const b8 isUserObjType = m_symbols.hasTypeSymbol(objType) || FindTemplateParam(ownerClass, &method, objType) != nullptr;
            const BuiltinReceiverMethodInfo* builtinRecvInfo = !isUserObjType
                ? FindBuiltinReceiverMethod(objType, mName)
                : (isAssetClassObj
                    ? FindBuiltinReceiverMethod("IAsset", mName)
                    : (isSystemClassObj ? FindBuiltinReceiverMethod("ISystem", mName) : nullptr));
            if(!builtinRecvInfo && targetCls){
                for(const TypeRef* iface : targetCls->implementedInterfaces){
                    if(!iface)
                        continue;
                    const MStringView ifaceName = m_symbols.resolveBaseTypeName(iface->name);
                    if(!m_symbols.findInterface(ifaceName)){
                        builtinRecvInfo = FindBuiltinReceiverMethod(ifaceName, mName);
                        if(builtinRecvInfo)
                            break;
                    }
                }
            }
            const BuiltinMemberMethodInfo* builtinMemberInfo = isUserObjType ? nullptr : FindBuiltinMemberMethod(mName);
            const b8 isMutatingContainerOp = builtinRecvInfo ? builtinRecvInfo->isMutating : (builtinMemberInfo && builtinMemberInfo->isMutating);
            if(builtinRecvInfo){
                targetBuiltinRecvInfo = builtinRecvInfo;
                recordBuiltinTypeHeader(builtinRecvInfo->returnTypeName);
                recordBuiltinTypeHeader(builtinRecvInfo->returnTypeArgName);
                recordBuiltinTypeHeader(builtinRecvInfo->returnNestedTypeArgName);
                recordBuiltinTypeHeader(builtinRecvInfo->returnErrorTypeName);
            }
            else if(!isUserObjType && builtinMemberInfo){
                targetBuiltinMemberInfo = builtinMemberInfo;
                targetBuiltinRecvObjType = objType;
            }
            if(
                !targetCls
                && (mName == "addSystem" || (mName == "addComponent" && callArgCount > 0u))
                && !expr.callTemplateArgs.empty()
                && expr.callTemplateArgs[0].type
            ){
                const MStringView emplacedTypeName = m_symbols.resolveBaseTypeName(expr.callTemplateArgs[0].type->name);
                if(const ClassDecl* emplacedCls = m_symbols.findClass(emplacedTypeName)){
                    targetOwnerCls = emplacedCls;
                    targetCallable = m_symbols.findClassConstructor(*emplacedCls, callArgCount, ownerClass != emplacedCls, argTypeHints, argIsMutHints);
                }
            }
            if(targetCls){
                targetOwnerCls = targetCls;
                targetCallable = m_symbols.findClassMethod(*targetCls, mName, &callArgCount, recvIsConst, ownerClass != targetCls, argTypeHints, argIsMutHints);
                if(targetCallable){
                    if(ownerClass != targetCls && targetCallable->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot call 'private' or 'protected' method outside of its declaring class");
                        ok = false;
                    }
                }
                else if(const FieldDecl* field = SymbolTable::FindClassField(*targetCls, mName)){
                    if(ownerClass != targetCls && field->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot access 'private' or 'protected' field outside of its declaring class");
                        ok = false;
                    }
                    const TypeRef* ft = m_symbols.substituteTypeParams(field->type, targetCls->templateParams, recvTypeRef);
                    invokedFuncType = m_symbols.unwrapTypeAlias(ft);
                    if(ft && (!invokedFuncType || !invokedFuncType->isFuncType) && !FindTemplateParam(ownerClass, &method, ft->name)){
                        reportError(errors, expr.span, "class member field is not a callable function");
                        ok = false;
                    }
                }
            }
            else if(recvTypeRef && targetIface){
                targetCallable = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints);
                if(!targetCallable && !builtinRecvInfo){
                    reportError(errors, expr.span, "unknown method on interface");
                    ok = false;
                }
                else if(recvIsConst && ((targetCallable && !targetCallable->isConst) || (builtinRecvInfo && builtinRecvInfo->isMutating))){
                    reportError(errors, expr.span, "cannot call non-'const' method on 'in' or 'const' interface receiver");
                    ok = false;
                }
            }
            else if(const TemplateParamDecl* tp = FindTemplateParam(ownerClass, &method, objType)){
                for(const TypeRef* boundRef : tp->interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(const MethodDecl* boundMethod = m_symbols.findInterfaceMethod(*boundRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints)){
                        targetCallable = boundMethod;
                        break;
                    }
                }
                if(!targetCallable){
                    reportError(errors, expr.span, "method call on template parameter requires a matching interface bound ('<T : IInterface>')");
                    ok = false;
                }
                else if(recvIsConst && !targetCallable->isConst){
                    reportError(errors, expr.span, "cannot call non-'const' method on 'in' or 'const' bounded template parameter");
                    ok = false;
                }
            }
            else if(IsNonThreadSafeContainer(objType) && m_parallelDepth > 0u && isMutatingContainerOp){
                b8 isTaskLocalContainer = false;
                if(expr.lhs->lhs->kind == ExprKind::Identifier){
                    if(const ScopedVarInfo* recvLocal = FindScopedVar(scope, expr.lhs->lhs->text))
                        isTaskLocalContainer = (recvLocal->declaredParallelDepth == m_parallelDepth);
                }
                if(!isTaskLocalContainer){
                    reportError(errors, expr.span, "mutating non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers");
                    ok = false;
                }
            }

            const b8 isStringFamily = !isUserObjType && IsStringLikeType(objType);
            if(isStringFamily && mName == "substr"){
                if(callArgCount > 2u){
                    reportError(errors, expr.span, "'substr' accepts at most 2 arguments");
                    ok = false;
                }
                if(SymbolTable::IsArenaBackedContainer(objType))
                    expr.isStringSubstr = true;
                if(isMutationTarget){
                    reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
                    ok = false;
                }
            }
            else if(!isUserObjType && mName == "value" && recvTypeRef && !recvTypeRef->templateArgs.empty() && recvTypeRef->templateArgs[0].type){
                if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(objType); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional)){
                    if(SymbolTable::IsSimdRegisterVectorType(m_symbols.resolveBaseTypeName(recvTypeRef->templateArgs[0].type->name)))
                        expr.unwrapSimdResultValue = true;
                }
            }
            else if(!isUserObjType && IsStringByteAccessorMethod(objType, mName)){
                if(callArgCount != 0u){
                    reportError(errors, expr.span, "'front' and 'back' do not accept arguments");
                    ok = false;
                }
                if(isMutationTarget){
                    if(!HasMutableStringElements(objType)){
                        reportError(errors, expr.span, "cannot mutate elements of 'StringView' or 'CompactString'");
                        ok = false;
                    }
                    else{
                        expr.isStringByteMutRef = true;
                    }
                }
                else{
                    expr.isStringByteRead = true;
                }
            }

            if((targetCallable && !targetCallable->isConst) || isMutatingContainerOp || expr.isStringByteMutRef || (isMutationTarget && builtinRecvInfo && builtinRecvInfo->returnTypeName.empty())){
                const b8 savedDeref = expr.lhs->lhs->derefNotNull;
                if(!checkExpr(ownerClass, scope, true, method, *expr.lhs->lhs, errors))
                    ok = false;
                expr.lhs->lhs->derefNotNull = savedDeref;
            }
        }
        else if(expr.lhs->kind == ExprKind::Lambda){
            tempInlineLambdaVar.lambdaExpr = expr.lhs;
            tempInlineLambdaVar.declaredParallelDepth = m_parallelDepth;
            localCallable = &tempInlineLambdaVar;
        }
        else if(
            expr.lhs->kind == ExprKind::ScopeAccess
            && expr.lhs->scopeChain.size() == 2u
            && !FindScopedVar(scope, expr.lhs->scopeChain[0])
            && !m_symbols.findGlobalConstant(expr.lhs->scopeChain[0])
            && !m_symbols.hasTypeSymbol(expr.lhs->scopeChain[0])
            && FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1]) != nullptr
        ){
            const BuiltinFunctionInfo* fnInfo = FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1]);
            recordBuiltinTypeHeader(expr.lhs->scopeChain[0]);
            recordRequiredHeader(fnInfo->requiredHeader);
            const b8 badArity = fnInfo->isVariadic
                ? (callArgCount < fnInfo->minArity)
                : (callArgCount < fnInfo->minArity || callArgCount > fnInfo->arity);
            if(!badArity || !m_hasTopLevelNativeBlocks){
                expr.isBuiltinCall = true;
                calledMemberName = fnInfo->adhdName;
                recordBuiltinTypeHeader(fnInfo->returnTypeName);
                recordBuiltinTypeHeader(fnInfo->returnTypeArgName);
                recordBuiltinTypeHeader(fnInfo->returnNestedTypeArgName);
                recordBuiltinTypeHeader(fnInfo->returnErrorTypeName);
                if(fnInfo->prependsScratchArena)
                    injectActiveArena();
                if(badArity){
                    reportError(errors, expr.span, "wrong number of arguments to built-in math/SIMD function");
                    ok = false;
                }
                const b8 requiresExplicitTypeArg = fnInfo->requiresExplicitTypeArg;
                if(requiresExplicitTypeArg && (expr.callTemplateArgs.size() != 1u || !expr.callTemplateArgs[0].type)){
                    reportError(errors, expr.span, "built-in function requires exactly 1 explicit type argument '<T>'");
                    ok = false;
                }
            }
        }
        else{
            const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, &method, *expr.lhs, scope);
            invokedFuncType = m_symbols.unwrapTypeAlias(lhsTypeRef);
        }
    }
    if(m_parallelDepth > 0u && localCallable && localCallable->lambdaExpr && localCallable->lambdaExpr->hasMutContainerCapture && localCallable->declaredParallelDepth < m_parallelDepth){
        reportError(errors, expr.span, "calling lambda that captures outer non-thread-safe container as 'mut' inside 'parallel' or 'parallel_for' is prohibited");
        ok = false;
    }

    const TypeRef* callReturnType = nullptr;
    if(targetCallable && !targetCallable->isConstructor){
        TypeRef* mutableRetType = targetCallable->returnType;
        if(targetOwnerCls)
            mutableRetType = m_symbols.substituteTypeParams(mutableRetType, targetOwnerCls->templateParams, recvTypeRef);
        if(mutableRetType && !targetCallable->templateParams.empty() && !expr.callTemplateArgs.empty())
            mutableRetType = m_symbols.substituteTypeParams(*mutableRetType, targetCallable->templateParams, expr.callTemplateArgs);
        callReturnType = mutableRetType;
    }
    else if(invokedFuncType && invokedFuncType->isFuncType)
        callReturnType = invokedFuncType->funcReturnType;
    else if(localCallable && localCallable->lambdaExpr)
        callReturnType = localCallable->lambdaExpr->lambdaReturnType;

    if(targetCallable && !targetCallable->isConstructor && targetCallable->injectsCallerArena){
        injectActiveArena();
    }
    else if(invokedFuncType && invokedFuncType->isFuncType && invokedFuncType->funcReturnType && m_symbols.resolveRefMode(invokedFuncType->funcReturnType) == ParamPassingMode::None && TypeLayoutAnalyzer(m_symbols).requiresMotherArena(*invokedFuncType->funcReturnType)){
        injectActiveArena();
    }
    else if(localCallable && localCallable->lambdaExpr && localCallable->lambdaExpr->lambdaInjectsCallerArena){
        injectActiveArena();
    }

    const ParamPassingMode::Enum callRetRefMode = m_symbols.resolveRefMode(callReturnType);

    if(isMutationTarget){
        const b8 isCtorOrTypeConstruction = (targetCallable && targetCallable->isConstructor && expr.lhs && expr.lhs->kind == ExprKind::Identifier)
            || (!targetCallable && !localCallable && expr.lhs && expr.lhs->kind == ExprKind::Identifier
                && isConstructibleTypeName(expr.lhs->text, m_symbols.resolveBaseTypeName(expr.lhs->text)));
        const BuiltinFunctionInfo* builtinFn = (expr.isBuiltinCall && expr.lhs && expr.lhs->kind == ExprKind::Identifier)
            ? FindBuiltinFunction(expr.lhs->text)
            : nullptr;
        const b8 isBuiltinFnRefReturn = (builtinFn && builtinFn->returnsReference);
        if(isCtorOrTypeConstruction || (expr.isBuiltinCall && !isBuiltinFnRefReturn)){
            reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
            ok = false;
        }
        else if(targetBuiltinRecvInfo && targetBuiltinRecvInfo->returnsReference && expr.lhs && expr.lhs->lhs && BorrowLifetimeAnalyzer(*this).isTemporaryOwnerExpr(ownerClass, &method, *expr.lhs->lhs, scope)){
            reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
            ok = false;
        }
        else if(
            targetBuiltinMemberInfo
            && targetBuiltinMemberInfo->methodName == "deref"
            && (targetBuiltinRecvObjType == "UniquePtr" || targetBuiltinRecvObjType == "NotNullUniquePtr" || targetBuiltinRecvObjType == "RefCountPtr")
            && expr.lhs
            && expr.lhs->lhs
            && BorrowLifetimeAnalyzer(*this).isTemporaryOwnerExpr(ownerClass, &method, *expr.lhs->lhs, scope)
        ){
            reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
            ok = false;
        }
        else if(callReturnType){
            if(callRetRefMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                ok = false;
            }
            else if(callRetRefMode == ParamPassingMode::None){
                reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
                ok = false;
            }
        }
    }

    if(!expr.callTemplateArgs.empty()){
        if(targetCallable && !targetCallable->isConstructor){
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
    b8 allowFirstVoidCallArg = false;
    if(!targetCallable && !localCallable && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(baseCallee); genericInfo && genericInfo->inlineLayoutKind == InlineLayoutKind::Expected)
            allowFirstVoidCallArg = true;
    }
    if(!checkTemplateArgTypes(expr.callTemplateArgs, ownerClass, &method, expr.span, errors, allowFirstVoidCallArg))
        ok = false;

    if(!CallableArgumentChecker(*this).check(
        ownerClass,
        scope,
        targetCallable,
        targetOwnerCls,
        recvTypeRef,
        expr.callTemplateArgs,
        invokedFuncType,
        localCallable,
        calledMemberName,
        method,
        expr.arguments,
        errors
    )){
        ok = false;
    }

    if(!targetCallable && !localCallable && expr.lhs && expr.lhs->kind == ExprKind::Identifier && expr.arguments.size() == 1u && expr.arguments[0]){
        const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
        if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(baseCallee); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional)){
            if(SymbolTable::IsSimdRegisterVectorType(expr.arguments[0]->resolvedSimdType))
                expr.arguments[0]->wrapInSimdResult = true;
        }
    }

    if(targetCallable && expr.callTemplateArgs.empty() && !targetCallable->templateParams.empty())
        CallableTemplateDeduction(*this).deduce(ownerClass, &method, *targetCallable, expr.arguments, scope, expr.deducedTemplateArgs);

    return ok;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

