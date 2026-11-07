// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker_infer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 StmtReferencesIdentifier(const Stmt* stmt, MStringView varName)noexcept;

[[nodiscard]] b8 ExprReferencesIdentifier(const Expr* expr, const MStringView varName)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Identifier && expr->text == varName)
        return true;
    if(expr->kind == ExprKind::ScopeAccess && !expr->scopeChain.empty() && expr->scopeChain[0] == varName)
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

[[nodiscard]] b8 StmtReferencesIdentifier(const Stmt* stmt, const MStringView varName)noexcept{
    if(!stmt)
        return false;
    if(stmt->initStmt && StmtReferencesIdentifier(stmt->initStmt, varName))
        return true;
    if(ExprReferencesIdentifier(stmt->expr, varName) || ExprReferencesIdentifier(stmt->condition, varName) || ExprReferencesIdentifier(stmt->stepExpr, varName))
        return true;
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

[[nodiscard]] b8 StmtPreventsMoveAtLastUse(const Stmt* stmt, MStringView varName, const SymbolTable& symbols)noexcept;

[[nodiscard]] b8 ExprHasMutLambdaCapture(const Expr* expr, const MStringView varName, const SymbolTable& symbols)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& cap : expr->lambdaCaptures){
            if(cap.mode == ParamPassingMode::Mut && cap.name == varName)
                return true;
        }
        for(const Stmt* child : expr->lambdaBody){
            if(StmtPreventsMoveAtLastUse(child, varName, symbols))
                return true;
        }
    }
    if(ExprHasMutLambdaCapture(expr->lhs, varName, symbols) || ExprHasMutLambdaCapture(expr->rhs, varName, symbols))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprHasMutLambdaCapture(arg, varName, symbols))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtPreventsMoveAtLastUse(const Stmt* stmt, const MStringView varName, const SymbolTable& symbols)noexcept{
    if(!stmt)
        return false;
    if((stmt->kind == StmtKind::ParallelBlock || stmt->kind == StmtKind::ParallelCall) && StmtReferencesIdentifier(stmt, varName))
        return true;
    if(stmt->kind == StmtKind::VarDecl && stmt->name != varName && stmt->varType && StmtReferencesIdentifier(stmt, varName)){
        const MStringView baseVarType = symbols.resolveBaseTypeName(stmt->varType->name);
        if(symbols.resolveRefMode(stmt->varType) != ParamPassingMode::None || IsBorrowViewType(baseVarType))
            return true;
    }
    if(ExprHasMutLambdaCapture(stmt->expr, varName, symbols) || ExprHasMutLambdaCapture(stmt->condition, varName, symbols) || ExprHasMutLambdaCapture(stmt->stepExpr, varName, symbols))
        return true;
    for(const Expr* arg : stmt->arguments){
        if(ExprHasMutLambdaCapture(arg, varName, symbols))
            return true;
    }
    for(const NativeBinding& nb : stmt->nativeBindings){
        if(ExprHasMutLambdaCapture(nb.sourceExpr, varName, symbols))
            return true;
    }
    if(stmt->initStmt && StmtPreventsMoveAtLastUse(stmt->initStmt, varName, symbols))
        return true;
    for(const Stmt* child : stmt->statements){
        if(StmtPreventsMoveAtLastUse(child, varName, symbols))
            return true;
    }
    for(const Stmt* child : stmt->elseStatements){
        if(StmtPreventsMoveAtLastUse(child, varName, symbols))
            return true;
    }
    for(const SwitchCase& sc : stmt->switchCases){
        if(ExprHasMutLambdaCapture(sc.matchExpr, varName, symbols))
            return true;
        for(const Stmt* child : sc.statements){
            if(StmtPreventsMoveAtLastUse(child, varName, symbols))
                return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
            if(IsNumericScalarType(resolvedBaseType) && TryMapScalarLimitMember(expr.text))
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
            if(const BuiltinReceiverMethodInfo* recvMethod = FindBuiltinReceiverMethod(recvBase, mName))
                return recvMethod->returnTypeName;
            if(const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(mName)){
                if(memberInfo->returnRule == BuiltinMemberReturnRule::FixedType)
                    return memberInfo->returnTypeName;
            }
        }
        if(expr.lhs->kind == ExprKind::Identifier && !expr.arguments.empty() && expr.arguments[0]){
            if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text)){
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::FixedType)
                    return fnInfo->returnTypeName;
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
                    return inferExprTypeName(ownerClass, method, *expr.arguments[0], scope);
                }
            }
        }
    }
    else if(expr.kind == ExprKind::ScopeAccess && !expr.scopeChain.empty()){
        return resolveScopeAccessEnumTypeName(ownerClass, method, expr, scope);
    }
    else if(const MStringView fixedLitType = LookupFixedLiteralExprTypeName(expr.kind); !fixedLitType.empty()){
        return fixedLitType;
    }
    else if(expr.kind == ExprKind::IntegerLiteral){
        const MStringView t = expr.text;
        b8 hasU = false;
        b8 hasL = false;
        for(usize i = t.size(); i > 0u && IsIntegerLiteralSuffixChar(t[i - 1u]); --i){
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
        if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, expr.text))
            return m_symbols.unwrapTypeAlias(field->type);
    }
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        const TypeRef* lhsTypeRef = nullptr;
        const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
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
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            if(const FieldDecl* field = SymbolTable::FindClassField(*cls, expr.text)){
                if(!field->type)
                    return nullptr;
                const TypeRef* ft = m_symbols.substituteTypeParams(field->type, cls->templateParams, lhsTypeRef);
                return m_symbols.unwrapTypeAlias(ft);
            }
        }
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const TypeRef* containerType = nullptr;
        const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &containerType);
        if(IsStringLikeType(lhsType)){
            TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
            synth->name = "u8";
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
        const MStringView opSym = MapTokenTypeToOperatorSymbol(expr.op);
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
                        deduceCallTemplateArgs(ownerClass, method, *fn, expr.arguments, scope, deducedArgs);
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
            if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text)){
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::FixedType && !fnInfo->returnTypeName.empty()){
                    TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                    synth->name = fnInfo->returnTypeName;
                    if(!fnInfo->returnTypeArgName.empty()){
                        TypeRef* valType = AllocateAstNode<TypeRef>(m_arena);
                        valType->name = fnInfo->returnTypeArgName;
                        TypeArg valArg;
                        valArg.type = valType;
                        synth->templateArgs.push_back(valArg);
                    }
                    return synth;
                }
            }
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
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
                        deduceCallTemplateArgs(ownerClass, method, *m, expr.arguments, scope, deducedArgs);
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
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const TypeRef* recvTypeRef = nullptr;
            const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope, &recvTypeRef);
            const MStringView mName = expr.lhs->text;
            if(const BuiltinReceiverMethodInfo* recvMethod = FindBuiltinReceiverMethod(lhsType, mName); recvMethod && recvMethod->synthesizeTypeRef){
                TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                synth->name = recvMethod->returnTypeName;
                return synth;
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
                            deduceCallTemplateArgs(ownerClass, method, *m, expr.arguments, scope, deducedArgs);
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
            }
            else if(recvTypeRef && m_symbols.findInterface(lhsType)){
                if(const MethodDecl* m = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints); m && m->returnType)
                    return m_symbols.unwrapTypeAlias(m->returnType);
            }
            else if(const TemplateParamDecl* tp = FindTemplateParam(ownerClass, method, lhsType)){
                for(const TypeRef* boundRef : tp->interfaceBounds){
                    if(!boundRef)
                        continue;
                    if(const MethodDecl* m = m_symbols.findInterfaceMethod(*boundRef, mName, &callArgCount, 0u, argTypeHints, argIsMutHints))
                        return m->returnType ? m_symbols.unwrapTypeAlias(m->returnType) : nullptr;
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::Lambda){
            if(expr.lhs->lambdaReturnType)
                return m_symbols.unwrapTypeAlias(expr.lhs->lambdaReturnType);
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
            const MStringView opSym = MapTokenTypeToOperatorSymbol(expr.op);
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
        if(IsBoolReturnOperatorToken(expr.op))
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
        if(m_symbols.findEnum(lhsT))
            return lhsT;
        if(m_symbols.findEnum(rhsT))
            return rhsT;
        return !lhsT.empty() ? lhsT : rhsT;
    }
    const TypeRef* typeRef = inferExprTypeRef(ownerClass, method, expr, scope);
    if(outTypeRef)
        *outTypeRef = typeRef;
    return typeRef
        ? m_symbols.resolveBaseTypeName(typeRef->name)
        : inferExprTypeName(ownerClass, method, expr, scope);
}

b8 TypeChecker::isTemporaryOwnerExpr(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    const TypeRef* exprTypeRef = nullptr;
    const MStringView baseType = inferExprBaseType(ownerClass, method, expr, scope, &exprTypeRef);
    if(m_symbols.resolveRefMode(exprTypeRef) != ParamPassingMode::None)
        return false;
    if(IsBorrowViewType(baseType))
        return false;

    switch(expr.kind){
    case ExprKind::Identifier:
    case ExprKind::ThisMember:
        return false;
    case ExprKind::MemberAccess:
        return expr.lhs != nullptr && isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
    case ExprKind::Index:{
        if(!expr.lhs)
            return false;
        const TypeRef* containerTypeRef = nullptr;
        const MStringView lhsBase = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &containerTypeRef);
        if(IsBorrowViewType(lhsBase))
            return false;
        const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
        if(const MethodDecl* customOp = resolveCustomOperator(ownerClass, method, containerTypeRef, lhsBase, "[]", 1u, recvIsConst))
            return m_symbols.resolveRefMode(customOp->returnType) == ParamPassingMode::None;
        return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            const MStringView recvBase = inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope);
            if(IsContainerOrWrapperValueAccessMethod(recvBase, mName, true)){
                return isTemporaryOwnerExpr(ownerClass, method, *expr.lhs->lhs, scope);
            }
            if(IsBorrowViewType(recvBase)){
                const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(mName);
                if((memberInfo && !memberInfo->isMutating && memberInfo->returnRule == BuiltinMemberReturnRule::FirstTypeArg) || IsStringByteAccessorMethod(recvBase, mName))
                    return false;
            }
        }
        return true;
    }
    case ExprKind::Binary:
        if(IsCompoundOrSimpleAssignment(expr.op))
            return expr.lhs != nullptr && isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
        return true;
    case ExprKind::Unary:
        if(!expr.isPostfix && IsIncrementOrDecrementOperatorToken(expr.op))
            return expr.lhs != nullptr && isTemporaryOwnerExpr(ownerClass, method, *expr.lhs, scope);
        return true;
    default:
        return true;
    }
}

b8 TypeChecker::exprBorrowsTemporaryStringOwner(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    const MStringView baseType = inferExprBaseType(ownerClass, method, expr, scope);
    if(IsOwningStringType(baseType))
        return isTemporaryOwnerExpr(ownerClass, method, expr, scope);
    if(baseType != "StringView")
        return false;

    if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if((IsBuiltinStringViewBorrowFunction(expr.lhs->text) || baseCallee == "StringView") && !expr.arguments.empty() && expr.arguments[0])
                return exprBorrowsTemporaryStringOwner(ownerClass, method, *expr.arguments[0], scope);
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            const MStringView recvBase = inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope);
            if(IsStringReceiverBorrowViewMethod(recvBase, mName)){
                if(exprBorrowsTemporaryStringOwner(ownerClass, method, *expr.lhs->lhs, scope))
                    return true;
            }
        }
        for(const Expr* arg : expr.arguments){
            if(!arg)
                continue;
            const MStringView argBase = inferExprBaseType(ownerClass, method, *arg, scope);
            if(IsStringLikeType(argBase) && exprBorrowsTemporaryStringOwner(ownerClass, method, *arg, scope))
                return true;
        }
        return false;
    }
    if(expr.kind == ExprKind::Binary && IsCompoundOrSimpleAssignment(expr.op) && expr.rhs)
        return exprBorrowsTemporaryStringOwner(ownerClass, method, *expr.rhs, scope);
    if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.lhs)
        return exprBorrowsTemporaryStringOwner(ownerClass, method, *expr.lhs, scope);
    return false;
}

b8 TypeChecker::exprBorrowsLocalStringOwner(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope, MStringView* outOwnerName, usize* outOwnerScopeIndex)const noexcept{
    const auto resolveLocalIndex = [&](const MStringView name, usize& outIndex)noexcept -> const ScopedVarInfo*{
        for(usize i = scope.size(); i > 0u; --i){
            if(scope[i - 1u].name == name){
                outIndex = i - 1u;
                return &scope[i - 1u];
            }
        }
        return nullptr;
    };

    const auto checkRootLocalOwner = [&](const Expr& candidate)noexcept -> b8{
        const Expr* root = &candidate;
        while(root){
            if(root->kind == ExprKind::MemberAccess && root->lhs){
                const TypeRef* midType = inferExprTypeRef(ownerClass, method, *root, scope);
                if(m_symbols.resolveRefMode(midType) != ParamPassingMode::None)
                    return false;
                root = root->lhs;
                continue;
            }
            if(root->kind == ExprKind::Index && root->lhs){
                const TypeRef* midType = inferExprTypeRef(ownerClass, method, *root, scope);
                if(m_symbols.resolveRefMode(midType) != ParamPassingMode::None)
                    return false;
                const MStringView parentBase = inferExprBaseType(ownerClass, method, *root->lhs, scope);
                if(IsBorrowViewType(parentBase))
                    return false;
                root = root->lhs;
                continue;
            }
            if(root->kind == ExprKind::Call && root->lhs && root->lhs->kind == ExprKind::MemberAccess && root->lhs->lhs){
                const MStringView mName = root->lhs->text;
                const MStringView recvBase = inferExprBaseType(ownerClass, method, *root->lhs->lhs, scope);
                if(IsContainerOrWrapperValueAccessMethod(recvBase, mName, false)){
                    root = root->lhs->lhs;
                    continue;
                }
            }
            break;
        }
        if(!root || root->kind != ExprKind::Identifier)
            return false;
        usize localIdx = 0u;
        const ScopedVarInfo* local = resolveLocalIndex(root->text, localIdx);
        if(!local)
            return false;
        if(local->borrowsLocalStringOwner){
            usize ownerIdx = localIdx;
            if(!local->borrowedLocalOwnerName.empty())
                resolveLocalIndex(local->borrowedLocalOwnerName, ownerIdx);
            if(outOwnerName)
                *outOwnerName = local->borrowedLocalOwnerName;
            if(outOwnerScopeIndex)
                *outOwnerScopeIndex = ownerIdx;
            return true;
        }
        const MStringView candidateBase = inferExprBaseType(ownerClass, method, candidate, scope);
        if(IsOwningStringType(candidateBase)
            && !local->isParam
            && local->mode == ParamPassingMode::None
            && m_symbols.resolveRefMode(local->type) == ParamPassingMode::None
            && !IsBorrowViewType(resolveScopedVarBaseType(*local))){
            if(outOwnerName)
                *outOwnerName = local->name;
            if(outOwnerScopeIndex)
                *outOwnerScopeIndex = localIdx;
            return true;
        }
        return false;
    };

    const MStringView baseType = inferExprBaseType(ownerClass, method, expr, scope);
    if(IsOwningStringType(baseType))
        return checkRootLocalOwner(expr);
    if(checkRootLocalOwner(expr))
        return true;
    if(baseType != "StringView")
        return false;

    if(expr.kind == ExprKind::Call && expr.lhs){
        if(expr.lhs->kind == ExprKind::Identifier){
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
            if((IsBuiltinStringViewBorrowFunction(expr.lhs->text) || baseCallee == "StringView") && !expr.arguments.empty() && expr.arguments[0])
                return exprBorrowsLocalStringOwner(ownerClass, method, *expr.arguments[0], scope, outOwnerName, outOwnerScopeIndex);
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            const MStringView mName = expr.lhs->text;
            const MStringView recvBase = inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope);
            if(IsStringReceiverBorrowViewMethod(recvBase, mName)){
                if(exprBorrowsLocalStringOwner(ownerClass, method, *expr.lhs->lhs, scope, outOwnerName, outOwnerScopeIndex))
                    return true;
            }
        }
        for(const Expr* arg : expr.arguments){
            if(!arg)
                continue;
            const MStringView argBase = inferExprBaseType(ownerClass, method, *arg, scope);
            if(IsStringLikeType(argBase)
                && exprBorrowsLocalStringOwner(ownerClass, method, *arg, scope, outOwnerName, outOwnerScopeIndex)){
                return true;
            }
        }
        return false;
    }
    if(expr.kind == ExprKind::Binary && IsCompoundOrSimpleAssignment(expr.op) && expr.rhs)
        return exprBorrowsLocalStringOwner(ownerClass, method, *expr.rhs, scope, outOwnerName, outOwnerScopeIndex);
    if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.lhs)
        return exprBorrowsLocalStringOwner(ownerClass, method, *expr.lhs, scope, outOwnerName, outOwnerScopeIndex);
    return false;
}

void TypeChecker::deduceCallTemplateArgs(
    const ClassDecl* ownerClass,
    const MethodDecl* enclosingMethod,
    const MethodDecl& targetCallable,
    const Span<const Expr* const> arguments,
    const Span<const ScopedVarInfo> scope,
    MVector<TypeArg>& outDeducedArgs
)const{
    outDeducedArgs.clear();
    outDeducedArgs.reserve(targetCallable.templateParams.size());
    for(const TemplateParamDecl& tp : targetCallable.templateParams){
        TypeArg dArg;
        if(!tp.constValueType){
            for(usize k = 0u; k < arguments.size() && k < targetCallable.params.size(); ++k){
                const Expr* arg = arguments[k];
                const TypeRef* pType = targetCallable.params[k].type;
                if(!arg || !pType)
                    continue;
                if(pType->name == tp.name && pType->templateArgs.empty()){
                    const TypeRef* argRef = nullptr;
                    const MStringView argBase = inferExprBaseType(ownerClass, enclosingMethod, *arg, scope, &argRef);
                    if(argRef){
                        dArg.type = const_cast<TypeRef*>(argRef);
                        break;
                    }
                    if(!argBase.empty()){
                        TypeRef* synth = AllocateAstNode<TypeRef>(m_arena);
                        synth->name = argBase;
                        dArg.type = synth;
                        break;
                    }
                }
                else if(!pType->templateArgs.empty()){
                    const TypeRef* argRef = inferExprTypeRef(ownerClass, enclosingMethod, *arg, scope);
                    if(const TypeRef* sub = DeduceTemplateParamTypeRef(pType, argRef, tp.name, m_symbols)){
                        dArg.type = const_cast<TypeRef*>(sub);
                        break;
                    }
                }
            }
        }
        outDeducedArgs.push_back(dArg);
    }
}

void TypeChecker::analyzeBlockMoveAtLastUse(const Span<Stmt* const> statements, const Span<const ScopedVarInfo> scope, const usize savedScopeSize)const noexcept{
    for(usize i = savedScopeSize; i < scope.size(); ++i){
        const ScopedVarInfo& local = scope[i];
        if(local.isParam || local.isConst || (local.type && m_symbols.resolveRefMode(local.type) != ParamPassingMode::None))
            continue;

        const MStringView localBaseType = resolveScopedVarBaseType(local);
        if(SymbolTable::IsSimdType(localBaseType) || m_symbols.isSmallValueType(localBaseType))
            continue;

        usize lastUseIndex = statements.size();
        for(usize s = statements.size(); s > 0u; --s){
            if(__hidden_adhd_type_checker_infer::StmtReferencesIdentifier(statements[s - 1u], local.name)){
                lastUseIndex = s - 1u;
                break;
            }
        }

        b8 capturedOrAsyncOrAliased = false;
        for(usize j = 0u; j < scope.size(); ++j){
            if(scope[j].borrowsLocalStringOwner && scope[j].borrowedLocalOwnerName == local.name){
                capturedOrAsyncOrAliased = true;
                break;
            }
            if(j >= savedScopeSize){
                if(const Expr* lam = scope[j].lambdaExpr){
                    for(const LambdaCapture& cap : lam->lambdaCaptures){
                        if(cap.name == local.name){
                            capturedOrAsyncOrAliased = true;
                            break;
                        }
                    }
                }
            }
            if(capturedOrAsyncOrAliased)
                break;
        }
        for(usize s = 0u; !capturedOrAsyncOrAliased && s < statements.size(); ++s){
            if(__hidden_adhd_type_checker_infer::StmtPreventsMoveAtLastUse(statements[s], local.name, m_symbols)){
                capturedOrAsyncOrAliased = true;
                break;
            }
        }
        if(capturedOrAsyncOrAliased || lastUseIndex >= statements.size())
            continue;

        Stmt* lastStmt = statements[lastUseIndex];
        const b8 isDirectCallStmt = (lastStmt->kind == StmtKind::ExprStmt && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call)
            || (lastStmt->kind == StmtKind::Return && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call)
            || (lastStmt->kind == StmtKind::VarDecl && lastStmt->name != local.name && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call);
        if(!isDirectCallStmt || __hidden_adhd_type_checker_infer::ExprReferencesIdentifier(lastStmt->expr->lhs, local.name))
            continue;

        usize argRefCount = 0u;
        for(const Expr* arg : lastStmt->expr->arguments){
            if(__hidden_adhd_type_checker_infer::ExprReferencesIdentifier(arg, local.name))
                ++argRefCount;
        }
        if(argRefCount == 1u){
            for(Expr* arg : lastStmt->expr->arguments){
                if(arg && !arg->isMutArg && arg->kind == ExprKind::Identifier && arg->text == local.name)
                    arg->moveAtLastUse = true;
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

