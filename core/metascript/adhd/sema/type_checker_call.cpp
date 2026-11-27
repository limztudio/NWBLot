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
    const BuiltinReceiverSignatureInfo* builtinReceiverSignature = nullptr;
    b8 builtinReceiverIsConst = false;
    b8 calleeSignatureKnown = false;
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
            markScratchArenaUsed(method);
        }
    };
    const auto isBuiltinFnArityInvalid = [callArgCount](const BuiltinFunctionInfo& fnInfo)noexcept{
        return fnInfo.isVariadic
            ? (callArgCount < fnInfo.minArity)
            : (callArgCount < fnInfo.minArity || callArgCount > fnInfo.arity);
    };
    const auto applyBuiltinFunctionCall = [&](const BuiltinFunctionInfo& fnInfo, const MStringView memberName, const b8 badArity){
        expr.isBuiltinCall = true;
        calledMemberName = memberName;
        recordBuiltinReturnTypeHeaders(fnInfo.returnTypeName, fnInfo.returnTypeArgName, fnInfo.returnNestedTypeArgName, fnInfo.returnErrorTypeName);
        if(fnInfo.prependsScratchArena)
            injectActiveArena();
        if(badArity){
            reportError(errors, expr.span, "wrong number of arguments to built-in math/SIMD function");
            ok = false;
        }
        if(fnInfo.requiresExplicitTypeArg && (expr.callTemplateArgs.size() != 1u || !expr.callTemplateArgs[0].type)){
            reportError(errors, expr.span, "built-in function requires exactly 1 explicit type argument '<T>'");
            ok = false;
        }
    };
    if(expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(KeywordSemantics::IsParallelWaitKeyword(expr.lhs->text)){
                reportError(errors, expr.span, "'wait' can only be used as a standalone join statement");
                ok = false;
            }
            targetCallable = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount, argTypeHints, argIsMutHints);
            if(!targetCallable){
                localCallable = FindScopedVar(scope, expr.lhs->text);
                if(localCallable && localCallable->type)
                    invokedFuncType = m_symbols.unwrapTypeAlias(localCallable->type);
            }
            if(targetCallable){
                expr.lhs->resolvedCppNamespace = targetCallable->cppNamespace;
                expr.lhs->resolvedCppExportName = targetCallable->cppExportName;
                recordRequiredHeader(targetCallable->requiredHeader);
            }
            if(targetCallable || localCallable){
                expr.isUserCallableCall = true;
            }
            else if(const ClassDecl* abstractCls = m_symbols.findClass(baseCallee); abstractCls && abstractCls->isAbstract){
                recordRequiredHeader(abstractCls->requiredHeader);
                reportError(errors, expr.span, "cannot instantiate an abstract class");
                ok = false;
            }
            else if(m_symbols.isInterfaceOrAbstractType(baseCallee)){
                recordBuiltinTypeHeader(baseCallee);
                reportError(errors, expr.span, "cannot instantiate an 'interface' type directly");
                ok = false;
            }
            else if(const ClassDecl* ctorCls = m_symbols.findClass(baseCallee)){
                recordRequiredHeader(ctorCls->requiredHeader);
                targetOwnerCls = ctorCls;
                targetCallable = m_symbols.findClassConstructor(*ctorCls, callArgCount, !m_symbols.canAccessPrivateMembers(*ctorCls, ownerClass), argTypeHints, argIsMutHints);
                expr.lhs->resolvedCppNamespace = (targetCallable && !targetCallable->cppNamespace.empty()) ? targetCallable->cppNamespace : ctorCls->cppNamespace;
                if(!ctorCls->cppExportName.empty())
                    expr.lhs->resolvedCppExportName = ctorCls->cppExportName;
                if(targetCallable){
                    recordRequiredHeader(targetCallable->requiredHeader);
                    if(!m_symbols.canAccessMember(*ctorCls, ownerClass, targetCallable->declaringClass, targetCallable->access)){
                        reportError(errors, expr.span, "cannot call 'private' or 'protected' constructor outside of its declaring class");
                        ok = false;
                    }
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
                    if(!targetCallable && !(ctorCls->isExtern && ctorCls->arenaCtorMode == ArenaCtorMode::First)){
                        if(DoesSynthesizedClassCtorPassArena(*ctorCls, baseCallee, callArgCount, argTypeHints.empty() ? MStringView{} : argTypeHints[0]))
                            callerPassedArena = true;
                    }
                    if(!callerPassedArena)
                        injectActiveArena();
                }
            }
            else if(!m_symbols.hasTypeSymbol(baseCallee) && FindBuiltinFunction(expr.lhs->text) != nullptr){
                const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text);
                recordRequiredHeader(fnInfo->requiredHeader);
                applyBuiltinFunctionCall(*fnInfo, expr.lhs->text, isBuiltinFnArityInvalid(*fnInfo));
            }
            else{
                recordBuiltinTypeHeader(baseCallee);
                if(!m_symbols.hasTypeSymbol(baseCallee) && (SymbolTable::IsArenaBackedContainer(baseCallee) || IsArenaFirstCtorType(baseCallee)))
                    injectActiveArena();
                if(!checkBuiltinVectorOrMatrixCtorCall(ownerClass, method, baseCallee, expr.arguments, scope, expr.span, errors, false))
                    ok = false;
                maybeAdaptInitToTChar(ownerClass, method, scope, baseCallee, nullptr, expr.arguments);
            }
            calleeSignatureKnown = (targetCallable != nullptr)
                || (invokedFuncType && invokedFuncType->isFuncType)
                || (localCallable && localCallable->lambdaExpr)
                || expr.isBuiltinCall
                || isConstructibleTypeName(expr.lhs->text, baseCallee);
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            const b8 isConstThis = isThisConstInScope(method, scope);
            targetOwnerCls = ownerClass;
            targetCallable = m_symbols.findClassMethod(*ownerClass, expr.lhs->text, &callArgCount, isConstThis, false, argTypeHints, argIsMutHints);
            if(targetCallable && !m_symbols.canAccessMember(*ownerClass, ownerClass, targetCallable->declaringClass, targetCallable->access)){
                reportError(errors, expr.span, "cannot access an inherited private member");
                ok = false;
            }
            if(!targetCallable){
                const BuiltinReceiverMethodInfo* inheritedRecvInfo = m_symbols.findInheritedBuiltinReceiverMethod(ownerClass, nullptr, expr.lhs->text);
                if(inheritedRecvInfo){
                    calleeSignatureKnown = true;
                    recordBuiltinReturnTypeHeaders(inheritedRecvInfo->returnTypeName, inheritedRecvInfo->returnTypeArgName, inheritedRecvInfo->returnNestedTypeArgName, inheritedRecvInfo->returnErrorTypeName);
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
            if(targetCallable || (invokedFuncType && invokedFuncType->isFuncType))
                calleeSignatureKnown = true;
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
            builtinReceiverIsConst = recvIsConst;
            const ClassDecl* targetCls = m_symbols.findClass(objType);
            const InterfaceDecl* targetIface = m_symbols.findInterface(objType);
            const b8 isUserObjType = m_symbols.hasTypeSymbol(objType) || FindTemplateParam(ownerClass, &method, objType) != nullptr;
            const BuiltinReceiverMethodInfo* builtinRecvInfo = !isUserObjType
                ? FindBuiltinReceiverMethod(objType, mName)
                : m_symbols.findInheritedBuiltinReceiverMethod(targetCls, targetIface, mName);
            if(!isUserObjType && builtinRecvInfo)
                builtinReceiverSignature = FindBuiltinReceiverSignature(objType, mName);
            const BuiltinMemberMethodInfo* builtinMemberInfo = isUserObjType ? nullptr : FindBuiltinMemberMethod(mName);
            const b8 isMutatingContainerOp = builtinRecvInfo ? builtinRecvInfo->isMutating : (builtinMemberInfo && builtinMemberInfo->isMutating);
            if(builtinRecvInfo){
                targetBuiltinRecvInfo = builtinRecvInfo;
                recordBuiltinReturnTypeHeaders(builtinRecvInfo->returnTypeName, builtinRecvInfo->returnTypeArgName, builtinRecvInfo->returnNestedTypeArgName, builtinRecvInfo->returnErrorTypeName);
            }
            else if(!isUserObjType && builtinMemberInfo){
                targetBuiltinMemberInfo = builtinMemberInfo;
                targetBuiltinRecvObjType = objType;
            }
            const SpecialMethodKind::Enum specialMethod = FindSpecialMethodSemantics(mName);
            if(
                !targetCls
                && (specialMethod == SpecialMethodKind::EcsEmplaceSystem || (specialMethod == SpecialMethodKind::EcsEmplaceComponent && callArgCount > 0u))
                && !expr.callTemplateArgs.empty()
                && expr.callTemplateArgs[0].type
            ){
                const MStringView emplacedTypeName = m_symbols.resolveBaseTypeName(expr.callTemplateArgs[0].type->name);
                if(const ClassDecl* emplacedCls = m_symbols.findClass(emplacedTypeName)){
                    targetOwnerCls = emplacedCls;
                    targetCallable = m_symbols.findClassConstructor(*emplacedCls, callArgCount, !m_symbols.canAccessPrivateMembers(*emplacedCls, ownerClass), argTypeHints, argIsMutHints);
                }
            }
            if(targetCls){
                targetOwnerCls = targetCls;
                recordRequiredHeader(targetCls->requiredHeader);
                targetCallable = m_symbols.findClassMethod(*targetCls, mName, &callArgCount, recvIsConst, !m_symbols.canAccessPrivateMembers(*targetCls, ownerClass), argTypeHints, argIsMutHints);
                if(targetCallable){
                    recordRequiredHeader(targetCallable->requiredHeader);
                    if(!targetCallable->cppExportName.empty()){
                        const usize lastSep = targetCallable->cppExportName.rfind(ContractSemantics::s_CppScopeSeparator);
                        expr.lhs->resolvedCppExportName = (lastSep != MStringView::npos)
                            ? targetCallable->cppExportName.substr(lastSep + ContractSemantics::s_CppScopeSeparator.size())
                            : targetCallable->cppExportName;
                    }
                    if(!m_symbols.canAccessMember(*targetCls, ownerClass, targetCallable->declaringClass, targetCallable->access)){
                        reportError(errors, expr.span, "cannot call 'private' or 'protected' method outside of its declaring class");
                        ok = false;
                    }
                }
                else if(const FieldDecl* field = SymbolTable::FindClassField(*targetCls, mName)){
                    if(!m_symbols.canAccessMember(*targetCls, ownerClass, field->declaringClass, field->access)){
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
                recordRequiredHeader(targetIface->requiredHeader);
                targetCallable = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints);
                if(targetCallable)
                    recordRequiredHeader(targetCallable->requiredHeader);
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
            else if(
                IsNonThreadSafeContainer(objType)
                && m_parallelDepth > 0u
                && (isMutatingContainerOp || (!builtinRecvInfo && !targetBuiltinMemberInfo && !(invokedFuncType && invokedFuncType->isFuncType) && !recvIsConst))
            ){
                if(!isTaskLocalIdentifierExpr(*expr.lhs->lhs, scope)){
                    reportError(errors, expr.span, "mutating non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers");
                    ok = false;
                }
            }
            const b8 isKnownCalleeMember = (targetCallable != nullptr)
                || (invokedFuncType && invokedFuncType->isFuncType)
                || (builtinRecvInfo != nullptr)
                || (targetBuiltinMemberInfo != nullptr)
                || (targetCls != nullptr)
                || (targetIface != nullptr)
                || (FindTemplateParam(ownerClass, &method, objType) != nullptr);
            if(isKnownCalleeMember)
                calleeSignatureKnown = true;

            const b8 isStringFamily = !isUserObjType && IsStringLikeType(objType);
            if(isStringFamily && specialMethod == SpecialMethodKind::Substr){
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
            else if(!isUserObjType && specialMethod == SpecialMethodKind::UnwrapValue && recvTypeRef && !recvTypeRef->templateArgs.empty() && recvTypeRef->templateArgs[0].type){
                if(IsOptionalOrExpectedGenericType(objType)){
                    const TypeRef* element = recvTypeRef->templateArgs[0].type;
                    const ParamPassingMode::Enum refMode = m_symbols.resolveRefMode(element);
                    if(refMode != ParamPassingMode::None){
                        expr.derefNotNull = true;
                        if(isMutationTarget && refMode == ParamPassingMode::In){
                            reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                            ok = false;
                        }
                    }
                    else if(SymbolTable::IsSimdRegisterVectorType(m_symbols.resolveBaseTypeName(element->name)))
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

            if(
                (targetCallable && !targetCallable->isConst)
                || isMutatingContainerOp
                || expr.isStringByteMutRef
                || (isMutationTarget && builtinRecvInfo && builtinRecvInfo->returnTypeName.empty())
                || (!isKnownCalleeMember && !recvIsConst && isPotentiallyMutableLvalueExpr(ownerClass, method, *expr.lhs->lhs, scope))
            ){
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
            calleeSignatureKnown = true;
        }
        else if(
            expr.lhs->kind == ExprKind::ScopeAccess
            && !expr.lhs->lhs
            && expr.lhs->text.empty()
            && expr.lhs->scopeChain.size() == 2u
            && isUnshadowedTypeScopeAccess(scope, expr.lhs->scopeChain[0])
            && m_symbols.findClass(m_symbols.resolveBaseTypeName(expr.lhs->scopeChain[0])) != nullptr
        ){
            const ClassDecl* scopedCls = m_symbols.findClass(m_symbols.resolveBaseTypeName(expr.lhs->scopeChain[0]));
            targetOwnerCls = scopedCls;
            targetCallable = m_symbols.findClassMethod(*scopedCls, expr.lhs->scopeChain[1], &callArgCount, false, !m_symbols.canAccessPrivateMembers(*scopedCls, ownerClass), argTypeHints, argIsMutHints, true);
            recordRequiredHeader(scopedCls->requiredHeader);
            if(targetCallable){
                if(!m_symbols.canAccessMember(*scopedCls, ownerClass, targetCallable->declaringClass, targetCallable->access)){
                    reportError(errors, expr.span, "cannot call 'private' or 'protected' method outside of its declaring class");
                    ok = false;
                }
                recordRequiredHeader(targetCallable->requiredHeader);
                expr.lhs->resolvedCppNamespace = !targetCallable->cppNamespace.empty() ? targetCallable->cppNamespace : scopedCls->cppNamespace;
                if(!targetCallable->cppExportName.empty()){
                    if(targetCallable->cppExportName.find(ContractSemantics::s_CppScopeSeparator) != MStringView::npos || scopedCls->cppExportName.empty())
                        expr.lhs->resolvedCppExportName = targetCallable->cppExportName;
                    else
                        expr.lhs->resolvedCppExportName = JoinScopedCppName(m_arena, scopedCls->cppExportName, targetCallable->cppExportName);
                }
                else if(!scopedCls->cppExportName.empty()){
                    expr.lhs->resolvedCppExportName = JoinScopedCppName(m_arena, scopedCls->cppExportName, targetCallable->name);
                }
                calleeSignatureKnown = true;
            }
            else if(const FieldDecl* staticField = SymbolTable::FindClassField(*scopedCls, expr.lhs->scopeChain[1]); staticField && staticField->isStatic){
                if(!m_symbols.canAccessMember(*scopedCls, ownerClass, staticField->declaringClass, staticField->access)){
                    reportError(errors, expr.span, "cannot access 'private' or 'protected' field outside of its declaring class");
                    ok = false;
                }
                invokedFuncType = m_symbols.unwrapTypeAlias(staticField->type);
                calleeSignatureKnown = (invokedFuncType && invokedFuncType->isFuncType);
                if(staticField->type && (!invokedFuncType || !invokedFuncType->isFuncType) && !FindTemplateParam(ownerClass, &method, staticField->type->name)){
                    reportError(errors, expr.span, "class static member is not a callable function");
                    ok = false;
                }
            }
        }
        else if(
            expr.lhs->kind == ExprKind::ScopeAccess
            && expr.lhs->scopeChain.size() == 2u
            && isUnshadowedTypeScopeAccess(scope, expr.lhs->scopeChain[0])
            && !m_symbols.hasTypeSymbol(expr.lhs->scopeChain[0])
            && FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1]) != nullptr
        ){
            const BuiltinFunctionInfo* fnInfo = FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1]);
            recordBuiltinTypeHeader(expr.lhs->scopeChain[0]);
            recordRequiredHeader(fnInfo->requiredHeader);
            const b8 badArity = isBuiltinFnArityInvalid(*fnInfo);
            applyBuiltinFunctionCall(*fnInfo, fnInfo->adhdName, badArity);
            calleeSignatureKnown = expr.isBuiltinCall;
        }
        else{
            const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, &method, *expr.lhs, scope);
            invokedFuncType = m_symbols.unwrapTypeAlias(lhsTypeRef);
            calleeSignatureKnown = (invokedFuncType && invokedFuncType->isFuncType);
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

    if(callReturnType){
        recordBuiltinTypeHeader(callReturnType->name);
        if(const TypeRef* unwrappedRet = m_symbols.unwrapTypeAlias(callReturnType))
            recordBuiltinTypeHeader(unwrappedRet->name);
    }

    if(targetCallable && targetCallable->returnsFromOwnerArena && expr.lhs){
        expr.returnsFromThisArena = expr.lhs->kind == ExprKind::ThisMember;
        if(ownerClass && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs && expr.lhs->lhs->kind == ExprKind::ThisMember){
            const FieldDecl* receiver = SymbolTable::FindClassField(*ownerClass, expr.lhs->lhs->text);
            expr.returnsFromThisArena = receiver && receiver->type && m_symbols.resolveRefMode(receiver->type) == ParamPassingMode::None;
        }
    }
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
            && FindSpecialMethodSemantics(targetBuiltinMemberInfo->methodName) == SpecialMethodKind::Deref
            && IsSmartPointerDerefType(targetBuiltinRecvObjType)
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
        builtinReceiverSignature,
        builtinReceiverIsConst,
        calleeSignatureKnown,
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
        if(IsOptionalOrExpectedGenericType(baseCallee)){
            TypeRef target(m_arena);
            target.name = expr.lhs->text;
            target.templateArgs = expr.callTemplateArgs;
            if(wrappedReferenceType(&target)){
                if(!checkWrappedReferenceArgument(ownerClass, method, target, *expr.arguments[0], scope, errors))
                    ok = false;
            }
            else if(SymbolTable::IsSimdRegisterVectorType(expr.arguments[0]->resolvedSimdType))
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

