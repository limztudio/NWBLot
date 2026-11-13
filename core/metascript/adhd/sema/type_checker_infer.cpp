// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"

#include <core/metascript/literal_decoder.h>

#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include <core/metascript/adhd/ast/operator_semantics.h>


#include "callable_template_deduction.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_infer{

template<typename ArenaT>
static TypeRef* SynthesizeBuiltinReturnTypeRef(
    ArenaT& arena,
    const MStringView returnTypeName,
    const MStringView returnTypeArgName,
    const MStringView returnNestedTypeArgName,
    const MStringView returnErrorTypeName
)noexcept{
    TypeRef* synth = AllocateAstNode<TypeRef>(arena);
    synth->name = returnTypeName;
    if(!returnTypeArgName.empty()){
        TypeRef* valType = AllocateAstNode<TypeRef>(arena);
        valType->name = returnTypeArgName;
        if(!returnNestedTypeArgName.empty()){
            TypeRef* nestedType = AllocateAstNode<TypeRef>(arena);
            nestedType->name = returnNestedTypeArgName;
            TypeArg nestedArg;
            nestedArg.type = nestedType;
            valType->templateArgs.push_back(nestedArg);
        }
        TypeArg valArg;
        valArg.type = valType;
        synth->templateArgs.push_back(valArg);
    }
    if(!returnErrorTypeName.empty()){
        TypeRef* errType = AllocateAstNode<TypeRef>(arena);
        errType->name = returnErrorTypeName;
        TypeArg errArg;
        errArg.type = errType;
        synth->templateArgs.push_back(errArg);
    }
    return synth;
}

template<typename ArenaT>
static const TypeRef* SynthesizeCallTemplateArg0ReturnType(
    ArenaT& arena,
    const SymbolTable& symbols,
    const BuiltinFunctionInfo& fnInfo,
    const TypeArg& resolvedTArg
)noexcept{
    if(!resolvedTArg.type)
        return nullptr;
    if(fnInfo.returnTypeName.empty())
        return symbols.unwrapTypeAlias(resolvedTArg.type);
    TypeArg innerArg = resolvedTArg;
    if(!fnInfo.returnTypeArgName.empty()){
        TypeRef* innerType = AllocateAstNode<TypeRef>(arena);
        innerType->name = fnInfo.returnTypeArgName;
        innerType->templateArgs.push_back(resolvedTArg);
        innerArg.type = innerType;
    }
    TypeRef* synth = AllocateAstNode<TypeRef>(arena);
    synth->name = fnInfo.returnTypeName;
    synth->templateArgs.push_back(innerArg);
    if(!fnInfo.returnErrorTypeName.empty()){
        TypeRef* errType = AllocateAstNode<TypeRef>(arena);
        errType->name = fnInfo.returnErrorTypeName;
        TypeArg errArg;
        errArg.type = errType;
        synth->templateArgs.push_back(errArg);
    }
    return synth;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MStringView TypeChecker::inferExprTypeName(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    if(!expr.resolvedSimdType.empty())
        return expr.resolvedSimdType;
    if(!expr.resolvedUnalignedType.empty())
        return expr.resolvedUnalignedType;
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = FindScopedVar(scope, expr.text))
            return resolveScopedVarBaseType(*local);
        if(!m_symbols.findGlobalConstant(expr.text) && !HasConstValueTemplateParam(ownerClass, method, expr.text)){
            if(const BuiltinGlobalConstantInfo* builtinConst = FindBuiltinGlobalConstant(expr.text))
                return builtinConst->adhdTypeName;
        }
    }
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier && expr.lhs->callTemplateArgs.empty() && !FindScopedVar(scope, expr.lhs->text) && !m_symbols.findGlobalConstant(expr.lhs->text)){
            const MStringView resolvedBaseType = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(BuiltinTypeSemantics::IsNumericScalarType(resolvedBaseType) && BuiltinTypeSemantics::TryMapScalarLimitMember(expr.text))
                return resolvedBaseType;
        }
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        if(const BuiltinSimdVectorInfo* simdInfo = FindBuiltinSimdVectorType(lhsType); simdInfo && expr.text.size() == 1u){
            u32 lane = 0u;
            return (TryMapSwizzleCharToLane(expr.text[0], lane) && lane == 3u) ? simdInfo->wScalarType : simdInfo->xyzScalarType;
        }
        if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(lhsType); unalignedInfo && expr.text.size() == 1u)
            return unalignedInfo->laneScalarType;
        if(expr.isSwizzle)
            return lhsType;
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        if(const BuiltinSimdVectorInfo* simdInfo = FindBuiltinSimdVectorType(lhsType))
            return simdInfo->xyzScalarType;
        if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(lhsType))
            return unalignedInfo->laneScalarType;
        if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(lhsType))
            return matInfo->rowVectorType;
        if(!m_symbols.hasTypeSymbol(lhsType)){
            if(const BuiltinReceiverMethodInfo* builtinOpIdx = FindBuiltinReceiverMethod(lhsType, "[]"))
                return builtinOpIdx->returnTypeName;
        }
    }

    if(expr.kind == ExprKind::Unary && expr.lhs)
        return inferExprBaseType(ownerClass, method, expr, scope, nullptr);
    if(expr.kind == ExprKind::Binary)
        return inferExprBaseType(ownerClass, method, expr, scope, nullptr);

    if(const TypeRef* resolvedRef = inferExprTypeRef(ownerClass, method, expr, scope))
        return m_symbols.resolveBaseTypeName(resolvedRef->name);
    if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView recvBase = inferExprTypeName(ownerClass, method, *expr.lhs->lhs, scope);
            const MStringView mName = expr.lhs->text;
            if(!m_symbols.hasTypeSymbol(recvBase) && !FindTemplateParam(ownerClass, method, recvBase)){
                if(const BuiltinReceiverMethodInfo* recvMethod = FindBuiltinReceiverMethod(recvBase, mName)){
                    if(recvMethod->returnTypeName.empty() && !expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
                        return m_symbols.resolveBaseTypeName(expr.callTemplateArgs[0].type->name);
                    return recvMethod->returnTypeName;
                }
                if(const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(mName)){
                    if(memberInfo->returnRule == BuiltinMemberReturnRule::FixedType)
                        return memberInfo->returnTypeName;
                }
            }
        }
        const BuiltinFunctionInfo* fnInfo = nullptr;
        if(expr.lhs->kind == ExprKind::Identifier && !m_symbols.hasTypeSymbol(m_symbols.resolveBaseTypeName(expr.lhs->text)))
            fnInfo = FindBuiltinFunction(expr.lhs->text);
        else if(
            expr.lhs->kind == ExprKind::ScopeAccess
            && !expr.lhs->lhs
            && expr.lhs->text.empty()
            && expr.lhs->scopeChain.size() == 2u
            && !FindScopedVar(scope, expr.lhs->scopeChain[0])
            && !m_symbols.findGlobalConstant(expr.lhs->scopeChain[0])
        ){
            if(const ClassDecl* scopedCls = m_symbols.findClass(m_symbols.resolveBaseTypeName(expr.lhs->scopeChain[0]))){
                if(
                    (!scopedCls->assetTypeName.empty() && expr.lhs->scopeChain[1] == "AssetTypeName")
                    || (!scopedCls->systemTypeName.empty() && expr.lhs->scopeChain[1] == "SystemTypeName")
                    || (!scopedCls->componentTypeName.empty() && expr.lhs->scopeChain[1] == "ComponentTypeName")
                )
                    return "Name";
            }
            if(const InterfaceDecl* scopedIface = m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.lhs->scopeChain[0]))){
                if(!scopedIface->assetTypeName.empty() && expr.lhs->scopeChain[1] == "AssetTypeName")
                    return "Name";
            }
            if(!m_symbols.hasTypeSymbol(expr.lhs->scopeChain[0]))
                fnInfo = FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1]);
        }
        if(fnInfo){
            if(fnInfo->returnRule == BuiltinFunctionReturnRule::FixedType)
                return fnInfo->returnTypeName;
            if(fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0OrFixedType){
                if(!expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
                    return m_symbols.resolveBaseTypeName(expr.callTemplateArgs[0].type->name);
                return fnInfo->returnTypeName;
            }
            if(fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0){
                if(!fnInfo->returnTypeName.empty())
                    return fnInfo->returnTypeName;
                if(!expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
                    return m_symbols.resolveBaseTypeName(expr.callTemplateArgs[0].type->name);
                if(!fnInfo->requiresExplicitTypeArg && !expr.arguments.empty() && expr.arguments[0])
                    return inferExprBaseType(ownerClass, method, *expr.arguments[0], scope);
                return {};
            }
            if(!expr.arguments.empty() && expr.arguments[0]){
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::LaneScalar){
                    const MStringView argType = inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                    if(const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(argType))
                        return vecInfo->xyzScalarType;
                    return argType;
                }
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::MatrixMultiply){
                    const MStringView lhsT = inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                    if(expr.arguments.size() == 2u && expr.arguments[1]){
                        const MStringView rhsT = inferExprTypeName(ownerClass, method, *expr.arguments[1], scope);
                        u32 rhsLanes = 0u;
                        if(SymbolTable::IsSimdMatrixType(lhsT) && SymbolTable::IsSimdVectorType(rhsT, rhsLanes))
                            return rhsT;
                    }
                    return lhsT;
                }
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::PromoteSimd){
                    for(const Expr* arg : expr.arguments){
                        if(!arg)
                            continue;
                        const MStringView argT = inferExprTypeName(ownerClass, method, *arg, scope);
                        if(SymbolTable::IsSimdType(argT))
                            return argT;
                    }
                    if(expr.lhs->text == "Lerp"){
                        MStringView promoted;
                        b8 allScalar = true;
                        for(const Expr* argument : expr.arguments){
                            const MStringView type = argument ? inferExprTypeName(ownerClass, method, *argument, scope) : MStringView{};
                            if(!BuiltinTypeSemantics::IsScalarType(type)){
                                allScalar = false;
                                break;
                            }
                            if(type == "f64")
                                promoted = "f64";
                            else if(type == "f32" && promoted != "f64")
                                promoted = "f32";
                        }
                        if(allScalar && !promoted.empty())
                            return promoted;
                    }
                    else if(expr.lhs->text != "Rsqrt" && !expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
                        return m_symbols.resolveBaseTypeName(expr.callTemplateArgs[0].type->name);
                    return inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                }
            }
        }
    }
    else if(expr.kind == ExprKind::ScopeAccess && !expr.scopeChain.empty()){
        if(
            !expr.lhs
            && expr.text.empty()
            && expr.scopeChain.size() == 2u
            && !FindScopedVar(scope, expr.scopeChain[0])
            && !m_symbols.findGlobalConstant(expr.scopeChain[0])
        ){
            if(const ClassDecl* scopedCls = m_symbols.findClass(m_symbols.resolveBaseTypeName(expr.scopeChain[0]))){
                if(
                    (!scopedCls->assetTypeName.empty() && expr.scopeChain[1] == "s_AssetTypeName")
                    || (!scopedCls->systemTypeName.empty() && expr.scopeChain[1] == "s_SystemTypeName")
                    || (!scopedCls->componentTypeName.empty() && expr.scopeChain[1] == "s_ComponentTypeName")
                )
                    return "Name";
                if(
                    (!scopedCls->assetTypeName.empty() && expr.scopeChain[1] == "s_AssetTypeText")
                    || (!scopedCls->systemTypeName.empty() && expr.scopeChain[1] == "s_SystemTypeText")
                    || (!scopedCls->componentTypeName.empty() && expr.scopeChain[1] == "s_ComponentTypeText")
                )
                    return "StringView";
            }
            if(const InterfaceDecl* scopedIface = m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.scopeChain[0]))){
                if(!scopedIface->assetTypeName.empty() && expr.scopeChain[1] == "s_AssetTypeName")
                    return "Name";
                if(!scopedIface->assetTypeName.empty() && expr.scopeChain[1] == "s_AssetTypeText")
                    return "StringView";
            }
            if(!m_symbols.hasTypeSymbol(expr.scopeChain[0]) && !HasBuiltinEnumEntry(expr.scopeChain[0], expr.scopeChain[1])){
                if(const BuiltinGlobalConstantInfo* scopedConst = FindBuiltinScopedConstant(expr.scopeChain[0], expr.scopeChain[1]))
                    return scopedConst->adhdTypeName;
            }
        }
        return resolveScopeAccessEnumTypeName(ownerClass, method, expr, scope);
    }
    else if(const MStringView fixedLitType = LookupFixedLiteralExprTypeName(expr.kind); !fixedLitType.empty()){
        return fixedLitType;
    }
    else if(expr.kind == ExprKind::IntegerLiteral){
        const MStringView t = expr.text;
        b8 hasU = false;
        b8 hasL = false;
        for(usize i = t.size(); i > 0u && LiteralDecoder::IsIntegerLiteralSuffixChar(t[i - 1u]); --i){
            const MChar c = t[i - 1u];
            if(c == 'u' || c == 'U')
                hasU = true;
            else
                hasL = true;
        }
        if(hasL)
            return hasU ? "u64" : "i64";
        return hasU ? "u32" : "i32";
    }
    return {};
}

const TypeRef* TypeChecker::inferExprTypeRef(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = FindScopedVar(scope, expr.text)){
            const TypeRef* unwrappedLocal = m_symbols.unwrapTypeAlias(local->type);
            if(unwrappedLocal && unwrappedLocal->isAuto && !local->inferredTypeName.empty()){
                TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                synth->name = local->inferredTypeName;
                synth->refMode = local->mode;
                synth->span = unwrappedLocal->span;
                return m_symbols.unwrapTypeAlias(synth);
            }
            return unwrappedLocal;
        }
        if(const Stmt* gc = m_symbols.findGlobalConstant(expr.text))
            return m_symbols.unwrapTypeAlias(gc->varType);
    }
    else if(expr.kind == ExprKind::ThisMember && ownerClass){
        if(expr.text.empty()){
            TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
            synth->name = ownerClass->name;
            synth->span = expr.span;
            for(const TemplateParamDecl& tp : ownerClass->templateParams){
                TypeArg targ;
                if(tp.constValueType){
                    targ.constantText = tp.name;
                }
                else{
                    TypeRef* paramType = AllocateAstNode<TypeRef>(m_arena);
                    paramType->name = tp.name;
                    paramType->span = expr.span;
                    targ.type = paramType;
                }
                synth->templateArgs.push_back(targ);
            }
            return synth;
        }
        if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, expr.text))
            return m_symbols.unwrapTypeAlias(field->type);
    }
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        const TypeRef* lhsTypeRef = nullptr;
        const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            if(const FieldDecl* field = SymbolTable::FindClassField(*cls, expr.text)){
                if(!field->type)
                    return nullptr;
                const TypeRef* ft = m_symbols.substituteTypeParams(field->type, cls->templateParams, lhsTypeRef);
                return m_symbols.unwrapTypeAlias(ft);
            }
        }
        else if(!m_symbols.hasTypeSymbol(lhsType)){
            if(const BuiltinFieldInfo* fieldInfo = FindBuiltinField(lhsType, expr.text)){
                TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                synth->name = fieldInfo->fieldTypeName;
                if(!fieldInfo->elemTypeName.empty()){
                    TypeRef* elemType = AllocateAstNode<TypeRef>(m_arena);
                    elemType->name = fieldInfo->elemTypeName;
                    TypeArg elemArg;
                    elemArg.type = elemType;
                    synth->templateArgs.push_back(elemArg);
                }
                if(!fieldInfo->countText.empty()){
                    TypeArg countArg;
                    countArg.constantText = fieldInfo->countText;
                    synth->templateArgs.push_back(countArg);
                }
                return synth;
            }
        }
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const TypeRef* containerType = nullptr;
        const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &containerType);
        if(!m_symbols.hasTypeSymbol(lhsType)){
            if(IsStringLikeType(lhsType)){
                TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                synth->name = "u8";
                return synth;
            }
            if(const BuiltinReceiverMethodInfo* builtinOpIdx = FindBuiltinReceiverMethod(lhsType, "[]"); builtinOpIdx && builtinOpIdx->synthesizeTypeRef){
                TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                synth->name = builtinOpIdx->returnTypeName;
                return synth;
            }
            if(containerType){
                if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(lhsType)){
                    if(genericInfo->isReflectedMap && containerType->templateArgs.size() >= 2u && containerType->templateArgs[1].type)
                        return m_symbols.unwrapTypeAlias(containerType->templateArgs[1].type);
                    if(genericInfo->isIndexableSequence && !containerType->templateArgs.empty() && containerType->templateArgs[0].type)
                        return m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type);
                }
            }
        }
        const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
        const ClassDecl* opDeclCls = nullptr;
        if(const MethodDecl* opIdx = resolveCustomOperator(ownerClass, method, containerType, lhsType, "[]", 1u, recvIsConst, &opDeclCls)){
            if(!opIdx->returnType)
                return nullptr;
            const TypeRef* rt = opDeclCls
                ? m_symbols.substituteTypeParams(opIdx->returnType, opDeclCls->templateParams, containerType)
                : opIdx->returnType;
            return m_symbols.unwrapTypeAlias(rt);
        }
    }
    else if((expr.kind == ExprKind::Unary || expr.kind == ExprKind::Binary) && expr.lhs && expr.op != TokenType::Equal){
        if(!expr.resolvedSimdType.empty() || !expr.resolvedUnalignedType.empty())
            return nullptr;
        const MStringView opSym = OperatorSemantics::MapTokenTypeToOperatorSymbol(expr.op);
        if(!opSym.empty()){
            const TypeRef* lhsTypeRef = nullptr;
            const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
            if(m_symbols.findClass(lhsType) || m_symbols.findInterface(lhsType) || FindTemplateParam(ownerClass, method, lhsType)){
                const usize opArity = (expr.kind == ExprKind::Unary) ? 0u : 1u;
                const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
                const ClassDecl* opDeclCls = nullptr;
                if(const MethodDecl* opMethod = resolveCustomOperator(ownerClass, method, lhsTypeRef, lhsType, opSym, opArity, recvIsConst, &opDeclCls)){
                    if(!opMethod->returnType)
                        return nullptr;
                    const TypeRef* rt = opDeclCls
                        ? m_symbols.substituteTypeParams(opMethod->returnType, opDeclCls->templateParams, lhsTypeRef)
                        : opMethod->returnType;
                    return m_symbols.unwrapTypeAlias(rt);
                }
            }
        }
    }
    else if(expr.kind == ExprKind::Call && expr.lhs){
        const usize callArgCount = expr.arguments.size();
        MVector<MStringView> argTypeHints(m_arena);
        MVector<u8> argIsMutHints(m_arena);
        buildCallArgHints(ownerClass, method, expr.arguments, scope, argTypeHints, argIsMutHints);
        if(expr.lhs->kind == ExprKind::Identifier){
            if(const MethodDecl* fn = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount, argTypeHints, argIsMutHints)){
                if(!fn->returnType)
                    return nullptr;
                const TypeRef* rt = fn->returnType;
                if(!fn->templateParams.empty()){
                    if(!expr.callTemplateArgs.empty()){
                        rt = m_symbols.substituteTypeParams(*fn->returnType, fn->templateParams, expr.callTemplateArgs);
                    }
                    else{
                        MVector<TypeArg> deducedArgs(m_arena);
                        CallableTemplateDeduction(*this).deduce(ownerClass, method, *fn, expr.arguments, scope, deducedArgs);
                        if(FindIf(deducedArgs.begin(), deducedArgs.end(), [](const TypeArg& arg)noexcept{ return arg.type != nullptr; }) != deducedArgs.end())
                            rt = m_symbols.substituteTypeParams(*fn->returnType, fn->templateParams, deducedArgs);
                    }
                }
                return m_symbols.unwrapTypeAlias(rt);
            }
            if(const ScopedVarInfo* local = FindScopedVar(scope, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(local->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(ft->funcReturnType);
                if(local->lambdaExpr && local->lambdaExpr->lambdaReturnType)
                    return m_symbols.unwrapTypeAlias(local->lambdaExpr->lambdaReturnType);
            }
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if(m_symbols.findClass(baseCallee) || m_symbols.findTypeAlias(expr.lhs->text)){
                if(isConstructibleTypeName(expr.lhs->text, baseCallee)){
                    TypeRef* constructed = AllocateAstNode<TypeRef>(m_arena);
                    constructed->name = expr.lhs->text;
                    constructed->templateArgs = expr.callTemplateArgs;
                    return m_symbols.unwrapTypeAlias(constructed);
                }
            }
            if(!m_symbols.hasTypeSymbol(baseCallee)){
                if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text)){
                    if(fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0OrFixedType && !expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
                        return m_symbols.unwrapTypeAlias(expr.callTemplateArgs[0].type);
                    if((fnInfo->returnRule == BuiltinFunctionReturnRule::FixedType || fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0OrFixedType) && !fnInfo->returnTypeName.empty())
                        return __hidden_adhd_type_infer::SynthesizeBuiltinReturnTypeRef(m_arena, fnInfo->returnTypeName, fnInfo->returnTypeArgName, fnInfo->returnNestedTypeArgName, fnInfo->returnErrorTypeName);
                    if(fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0){
                        TypeArg resolvedTArg{};
                        if(!expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type){
                            resolvedTArg = expr.callTemplateArgs[0];
                        }
                        else if(!fnInfo->requiresExplicitTypeArg && !expr.arguments.empty() && expr.arguments[0]){
                            const TypeRef* arg0Ref = nullptr;
                            const MStringView arg0Base = inferExprBaseType(ownerClass, method, *expr.arguments[0], scope, &arg0Ref);
                            if(arg0Ref){
                                resolvedTArg.type = const_cast<TypeRef*>(arg0Ref);
                            }
                            else if(!arg0Base.empty()){
                                TypeRef* deducedType = AllocateAstNode<TypeRef>(m_arena);
                                deducedType->name = arg0Base;
                                resolvedTArg.type = deducedType;
                            }
                        }
                        if(const TypeRef* synth = __hidden_adhd_type_infer::SynthesizeCallTemplateArg0ReturnType(m_arena, m_symbols, *fnInfo, resolvedTArg))
                            return synth;
                    }
                }
            }
            if(isConstructibleTypeName(expr.lhs->text, baseCallee)){
                TypeRef* constructed = AllocateAstNode<TypeRef>(m_arena);
                constructed->name = expr.lhs->text;
                constructed->templateArgs = expr.callTemplateArgs;
                return m_symbols.unwrapTypeAlias(constructed);
            }
        }
        else if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            const b8 isConstThis = method ? isThisConstInScope(*method, scope) : false;
            if(const MethodDecl* m = m_symbols.findClassMethod(*ownerClass, expr.lhs->text, &callArgCount, isConstThis, false, argTypeHints, argIsMutHints)){
                if(!m->returnType)
                    return nullptr;
                const TypeRef* rt = m->returnType;
                if(!m->templateParams.empty()){
                    if(!expr.callTemplateArgs.empty()){
                        rt = m_symbols.substituteTypeParams(*m->returnType, m->templateParams, expr.callTemplateArgs);
                    }
                    else{
                        MVector<TypeArg> deducedArgs(m_arena);
                        CallableTemplateDeduction(*this).deduce(ownerClass, method, *m, expr.arguments, scope, deducedArgs);
                        if(FindIf(deducedArgs.begin(), deducedArgs.end(), [](const TypeArg& arg)noexcept{ return arg.type != nullptr; }) != deducedArgs.end())
                            rt = m_symbols.substituteTypeParams(*m->returnType, m->templateParams, deducedArgs);
                    }
                }
                return m_symbols.unwrapTypeAlias(rt);
            }
            if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(field->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(ft->funcReturnType);
            }
            const BuiltinReceiverMethodInfo* inheritedRecv = nullptr;
            if(!ownerClass->assetTypeName.empty() || !ownerClass->subsystemTypeName.empty())
                inheritedRecv = FindBuiltinReceiverMethod("IAsset", expr.lhs->text);
            else if(!ownerClass->systemTypeName.empty())
                inheritedRecv = FindBuiltinReceiverMethod("ISystem", expr.lhs->text);
            if(!inheritedRecv){
                for(const TypeRef* iface : ownerClass->implementedInterfaces){
                    if(!iface)
                        continue;
                    const MStringView ifaceName = m_symbols.resolveBaseTypeName(iface->name);
                    if(!m_symbols.findInterface(ifaceName)){
                        inheritedRecv = FindBuiltinReceiverMethod(ifaceName, expr.lhs->text);
                        if(inheritedRecv)
                            break;
                    }
                }
            }
            if(inheritedRecv && !inheritedRecv->returnTypeName.empty())
                return __hidden_adhd_type_infer::SynthesizeBuiltinReturnTypeRef(m_arena, inheritedRecv->returnTypeName, inheritedRecv->returnTypeArgName, inheritedRecv->returnNestedTypeArgName, inheritedRecv->returnErrorTypeName);
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const TypeRef* recvTypeRef = nullptr;
            const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope, &recvTypeRef);
            const MStringView mName = expr.lhs->text;
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs->lhs, scope) : false;
                if(const MethodDecl* m = m_symbols.findClassMethod(*cls, mName, &callArgCount, recvIsConst, ownerClass != cls, argTypeHints, argIsMutHints)){
                    if(!m->returnType)
                        return nullptr;
                    TypeRef* rt = m_symbols.substituteTypeParams(m->returnType, cls->templateParams, recvTypeRef);
                    if(!m->templateParams.empty()){
                        if(!expr.callTemplateArgs.empty()){
                            rt = m_symbols.substituteTypeParams(*rt, m->templateParams, expr.callTemplateArgs);
                        }
                        else{
                            MVector<TypeArg> deducedArgs(m_arena);
                            CallableTemplateDeduction(*this).deduce(ownerClass, method, *m, expr.arguments, scope, deducedArgs);
                            if(FindIf(deducedArgs.begin(), deducedArgs.end(), [](const TypeArg& arg)noexcept{ return arg.type != nullptr; }) != deducedArgs.end())
                                rt = m_symbols.substituteTypeParams(*rt, m->templateParams, deducedArgs);
                        }
                    }
                    return m_symbols.unwrapTypeAlias(rt);
                }
                if(const FieldDecl* field = SymbolTable::FindClassField(*cls, mName)){
                    const TypeRef* ft = m_symbols.substituteTypeParams(field->type, cls->templateParams, recvTypeRef);
                    if(const TypeRef* uft = m_symbols.unwrapTypeAlias(ft); uft && uft->isFuncType && uft->funcReturnType)
                        return m_symbols.unwrapTypeAlias(uft->funcReturnType);
                }
                const BuiltinReceiverMethodInfo* inheritedRecv = nullptr;
                if(!cls->assetTypeName.empty() || !cls->subsystemTypeName.empty())
                    inheritedRecv = FindBuiltinReceiverMethod("IAsset", mName);
                else if(!cls->systemTypeName.empty())
                    inheritedRecv = FindBuiltinReceiverMethod("ISystem", mName);
                if(!inheritedRecv){
                    for(const TypeRef* iface : cls->implementedInterfaces){
                        if(!iface)
                            continue;
                        const MStringView ifaceName = m_symbols.resolveBaseTypeName(iface->name);
                        if(!m_symbols.findInterface(ifaceName)){
                            inheritedRecv = FindBuiltinReceiverMethod(ifaceName, mName);
                            if(inheritedRecv)
                                break;
                        }
                    }
                }
                if(inheritedRecv && !inheritedRecv->returnTypeName.empty())
                    return __hidden_adhd_type_infer::SynthesizeBuiltinReturnTypeRef(m_arena, inheritedRecv->returnTypeName, inheritedRecv->returnTypeArgName, inheritedRecv->returnNestedTypeArgName, inheritedRecv->returnErrorTypeName);
            }
            else if(const InterfaceDecl* ifaceDecl = recvTypeRef ? m_symbols.findInterface(lhsType) : nullptr){
                if(const MethodDecl* m = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints); m && m->returnType)
                    return m_symbols.unwrapTypeAlias(m->returnType);
                if(!ifaceDecl->assetTypeName.empty()){
                    if(const BuiltinReceiverMethodInfo* inheritedRecv = FindBuiltinReceiverMethod("IAsset", mName); inheritedRecv && !inheritedRecv->returnTypeName.empty())
                        return __hidden_adhd_type_infer::SynthesizeBuiltinReturnTypeRef(m_arena, inheritedRecv->returnTypeName, inheritedRecv->returnTypeArgName, inheritedRecv->returnNestedTypeArgName, inheritedRecv->returnErrorTypeName);
                }
            }
            else if(const TemplateParamDecl* tp = FindTemplateParam(ownerClass, method, lhsType)){
                for(const TypeRef* boundRef : tp->interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(const MethodDecl* m = m_symbols.findInterfaceMethod(*boundRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints))
                        return m->returnType ? m_symbols.unwrapTypeAlias(m->returnType) : nullptr;
                }
            }
            else if(!m_symbols.hasTypeSymbol(lhsType)){
                if(const BuiltinReceiverMethodInfo* recvMethod = FindBuiltinReceiverMethod(lhsType, mName); recvMethod && recvMethod->synthesizeTypeRef){
                    if(recvMethod->returnTypeName.empty() && !expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
                        return m_symbols.unwrapTypeAlias(expr.callTemplateArgs[0].type);
                    return __hidden_adhd_type_infer::SynthesizeBuiltinReturnTypeRef(m_arena, recvMethod->returnTypeName, recvMethod->returnTypeArgName, recvMethod->returnNestedTypeArgName, recvMethod->returnErrorTypeName);
                }
                if(recvTypeRef && FindBuiltinGenericType(lhsType) && !recvTypeRef->templateArgs.empty() && recvTypeRef->templateArgs[0].type){
                    if(const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(mName)){
                        if(memberInfo->returnRule == BuiltinMemberReturnRule::FirstTypeArg)
                            return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[0].type);
                        if(memberInfo->returnRule == BuiltinMemberReturnRule::SecondTypeArgOrDefault){
                            if(recvTypeRef->templateArgs.size() >= 2u && recvTypeRef->templateArgs[1].type)
                                return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[1].type);
                            TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                            synth->name = memberInfo->returnTypeName;
                            return synth;
                        }
                        if(memberInfo->returnRule == BuiltinMemberReturnRule::ReceiverType)
                            return m_symbols.unwrapTypeAlias(recvTypeRef);
                    }
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::Lambda){
            if(expr.lhs->lambdaReturnType)
                return m_symbols.unwrapTypeAlias(expr.lhs->lambdaReturnType);
        }
        else if(
            expr.lhs->kind == ExprKind::ScopeAccess
            && !expr.lhs->lhs
            && expr.lhs->text.empty()
            && expr.lhs->scopeChain.size() == 2u
            && !FindScopedVar(scope, expr.lhs->scopeChain[0])
            && !m_symbols.findGlobalConstant(expr.lhs->scopeChain[0])
            && !m_symbols.hasTypeSymbol(expr.lhs->scopeChain[0])
        ){
            if(const BuiltinFunctionInfo* fnInfo = FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1])){
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0OrFixedType && !expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type)
                    return m_symbols.unwrapTypeAlias(expr.callTemplateArgs[0].type);
                if((fnInfo->returnRule == BuiltinFunctionReturnRule::FixedType || fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0OrFixedType) && !fnInfo->returnTypeName.empty())
                    return __hidden_adhd_type_infer::SynthesizeBuiltinReturnTypeRef(m_arena, fnInfo->returnTypeName, fnInfo->returnTypeArgName, fnInfo->returnNestedTypeArgName, fnInfo->returnErrorTypeName);
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::CallTemplateArg0){
                    TypeArg resolvedTArg{};
                    if(!expr.callTemplateArgs.empty() && expr.callTemplateArgs[0].type){
                        resolvedTArg = expr.callTemplateArgs[0];
                    }
                    else if(!fnInfo->requiresExplicitTypeArg && !expr.arguments.empty() && expr.arguments[0]){
                        const TypeRef* arg0Ref = nullptr;
                        const MStringView arg0Base = inferExprBaseType(ownerClass, method, *expr.arguments[0], scope, &arg0Ref);
                        if(arg0Ref){
                            resolvedTArg.type = const_cast<TypeRef*>(arg0Ref);
                        }
                        else if(!arg0Base.empty()){
                            TypeRef* deducedType = AllocateAstNode<TypeRef>(m_arena);
                            deducedType->name = arg0Base;
                            resolvedTArg.type = deducedType;
                        }
                    }
                    if(const TypeRef* synth = __hidden_adhd_type_infer::SynthesizeCallTemplateArg0ReturnType(m_arena, m_symbols, *fnInfo, resolvedTArg))
                        return synth;
                }
            }
        }
        else{
            if(const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, method, *expr.lhs, scope)){
                if(const TypeRef* uft = m_symbols.unwrapTypeAlias(lhsTypeRef); uft && uft->isFuncType && uft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(uft->funcReturnType);
            }
        }
    }
    else if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.targetType){
        return m_symbols.unwrapTypeAlias(expr.targetType);
    }
    return nullptr;
}

MStringView TypeChecker::inferExprBaseType(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope, const TypeRef** outTypeRef)const noexcept{
    if(!expr.resolvedSimdType.empty()){
        if(outTypeRef)
            *outTypeRef = nullptr;
        return expr.resolvedSimdType;
    }
    if(!expr.resolvedUnalignedType.empty()){
        if(outTypeRef)
            *outTypeRef = nullptr;
        return expr.resolvedUnalignedType;
    }
    if((expr.kind == ExprKind::Unary && expr.lhs) || expr.kind == ExprKind::Binary){
        const TypeRef* lhsTypeRef = nullptr;
        const MStringView lhsT = expr.lhs ? inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef) : MStringView{};
        if(expr.lhs && expr.op != TokenType::Equal){
            const MStringView opSym = OperatorSemantics::MapTokenTypeToOperatorSymbol(expr.op);
            if(!opSym.empty() && (m_symbols.findClass(lhsT) || m_symbols.findInterface(lhsT) || FindTemplateParam(ownerClass, method, lhsT))){
                const usize opArity = (expr.kind == ExprKind::Unary) ? 0u : 1u;
                const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
                const ClassDecl* opDeclCls = nullptr;
                if(const MethodDecl* opMethod = resolveCustomOperator(ownerClass, method, lhsTypeRef, lhsT, opSym, opArity, recvIsConst, &opDeclCls)){
                    const TypeRef* unwrapped = nullptr;
                    if(opMethod->returnType){
                        const TypeRef* rt = opDeclCls
                            ? m_symbols.substituteTypeParams(opMethod->returnType, opDeclCls->templateParams, lhsTypeRef)
                            : opMethod->returnType;
                        unwrapped = m_symbols.unwrapTypeAlias(rt);
                    }
                    if(outTypeRef)
                        *outTypeRef = unwrapped;
                    return unwrapped ? m_symbols.resolveBaseTypeName(unwrapped->name) : MStringView{};
                }
            }
        }
        if(outTypeRef)
            *outTypeRef = nullptr;
        if(OperatorSemantics::IsBoolReturnOperatorToken(expr.op))
            return "b8";
        if(expr.kind == ExprKind::Unary){
            if(SymbolTable::IsSimdType(lhsT))
                const_cast<Expr&>(expr).resolvedSimdType = lhsT;
            return lhsT;
        }
        if(SymbolTable::IsSimdType(lhsT)){
            const_cast<Expr&>(expr).resolvedSimdType = lhsT;
            return lhsT;
        }
        const MStringView rhsT = expr.rhs ? inferExprTypeName(ownerClass, method, *expr.rhs, scope) : MStringView{};
        if(SymbolTable::IsSimdType(rhsT)){
            const_cast<Expr&>(expr).resolvedSimdType = rhsT;
            return rhsT;
        }
        if(m_symbols.findEnum(lhsT) || (!m_symbols.hasTypeSymbol(lhsT) && FindBuiltinEnum(lhsT)))
            return lhsT;
        if(m_symbols.findEnum(rhsT) || (!m_symbols.hasTypeSymbol(rhsT) && FindBuiltinEnum(rhsT)))
            return rhsT;
        if(!OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op)
            && BuiltinTypeSemantics::IsScalarType(lhsT) && BuiltinTypeSemantics::IsScalarType(rhsT)){
            if(lhsT == "f64" || rhsT == "f64")
                return "f64";
            if(lhsT == "f32" || rhsT == "f32")
                return "f32";
        }
        return !lhsT.empty() ? lhsT : rhsT;
    }
    const TypeRef* typeRef = inferExprTypeRef(ownerClass, method, expr, scope);
    if(outTypeRef)
        *outTypeRef = typeRef;
    return typeRef
        ? m_symbols.resolveBaseTypeName(typeRef->name)
        : inferExprTypeName(ownerClass, method, expr, scope);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

