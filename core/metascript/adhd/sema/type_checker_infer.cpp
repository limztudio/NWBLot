// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


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

[[nodiscard]] b8 ExprHasMutLambdaCapture(const Expr* expr, const MStringView varName)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& cap : expr->lambdaCaptures){
            if(cap.mode == ParamPassingMode::Mut && cap.name == varName)
                return true;
        }
        for(const Stmt* child : expr->lambdaBody){
            if(child && child->expr && ExprHasMutLambdaCapture(child->expr, varName))
                return true;
        }
    }
    if(ExprHasMutLambdaCapture(expr->lhs, varName) || ExprHasMutLambdaCapture(expr->rhs, varName))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprHasMutLambdaCapture(arg, varName))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtPreventsMoveAtLastUse(const Stmt* stmt, const MStringView varName, const SymbolTable& symbols)noexcept{
    if(!stmt)
        return false;
    if((stmt->kind == StmtKind::ParallelBlock || stmt->kind == StmtKind::ParallelCall) && StmtReferencesIdentifier(stmt, varName))
        return true;
    if(stmt->kind == StmtKind::VarDecl && stmt->name != varName && stmt->varType && symbols.resolveRefMode(stmt->varType) != ParamPassingMode::None && StmtReferencesIdentifier(stmt, varName))
        return true;
    if(ExprHasMutLambdaCapture(stmt->expr, varName) || ExprHasMutLambdaCapture(stmt->condition, varName) || ExprHasMutLambdaCapture(stmt->stepExpr, varName))
        return true;
    for(const Expr* arg : stmt->arguments){
        if(ExprHasMutLambdaCapture(arg, varName))
            return true;
    }
    for(const NativeBinding& nb : stmt->nativeBindings){
        if(ExprHasMutLambdaCapture(nb.sourceExpr, varName))
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
        if(ExprHasMutLambdaCapture(sc.matchExpr, varName))
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


MStringView TypeChecker::inferExprTypeName(ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = FindScopedVar(scope, expr.text))
            return resolveScopedVarBaseType(*local);
        if(const Stmt* gc = m_symbols.findGlobalConstant(expr.text); gc && gc->varType)
            return m_symbols.resolveBaseTypeName(gc->varType->name);
        if(const MStringView builtinConstType = FindBuiltinGlobalConstantType(expr.text); !builtinConstType.empty())
            return builtinConstType;
    }
    else if(expr.kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, expr.text); field && field->type)
            return m_symbols.resolveBaseTypeName(field->type->name);
    }
    else if(expr.kind == ExprKind::MemberAccess && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        if(const BuiltinSimdVectorInfo* simdInfo = FindBuiltinSimdVectorType(lhsType); simdInfo && expr.text.size() == 1u){
            const MChar c = expr.text[0];
            return (c == 'w' || c == 'a') ? simdInfo->wScalarType : simdInfo->xyzScalarType;
        }
        if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(lhsType); unalignedInfo && expr.text.size() == 1u)
            return unalignedInfo->laneScalarType;
        if(expr.isSwizzle)
            return lhsType;
        if(const TypeRef* memberType = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(memberType->name);
    }
    else if(expr.kind == ExprKind::Index && expr.lhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, method, *expr.lhs, scope);
        if(const BuiltinSimdVectorInfo* simdInfo = FindBuiltinSimdVectorType(lhsType))
            return simdInfo->xyzScalarType;
        if(const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(lhsType))
            return unalignedInfo->laneScalarType;
        if(const TypeRef* indexedType = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(indexedType->name);
    }
    else if(expr.kind == ExprKind::Unary && expr.lhs){
        if(const TypeRef* unaryTypeRef = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(unaryTypeRef->name);
        if(expr.op == TokenType::Bang)
            return "b8";
        return inferExprTypeName(ownerClass, method, *expr.lhs, scope);
    }
    else if(expr.kind == ExprKind::Binary){
        if(const TypeRef* binTypeRef = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(binTypeRef->name);
        if(
            expr.op == TokenType::EqualEqual || expr.op == TokenType::BangEqual
            || expr.op == TokenType::Less || expr.op == TokenType::LessEqual
            || expr.op == TokenType::Greater || expr.op == TokenType::GreaterEqual
            || expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe
        )
            return "b8";
        const MStringView lhsT = expr.lhs ? inferExprTypeName(ownerClass, method, *expr.lhs, scope) : MStringView{};
        const MStringView rhsT = expr.rhs ? inferExprTypeName(ownerClass, method, *expr.rhs, scope) : MStringView{};
        if(SymbolTable::IsSimdType(lhsT))
            return lhsT;
        if(SymbolTable::IsSimdType(rhsT))
            return rhsT;
        if(m_symbols.findEnum(lhsT))
            return lhsT;
        if(m_symbols.findEnum(rhsT))
            return rhsT;
        return !lhsT.empty() ? lhsT : rhsT;
    }
    else if(expr.kind == ExprKind::Call && expr.lhs){
        if(const TypeRef* callType = inferExprTypeRef(ownerClass, method, expr, scope))
            return m_symbols.resolveBaseTypeName(callType->name);
        if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            if(const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(expr.lhs->text)){
                if(memberInfo->returnRule == BuiltinMemberReturnRule::Usize)
                    return "usize";
                if(memberInfo->returnRule == BuiltinMemberReturnRule::Bool)
                    return "b8";
                if(memberInfo->returnRule == BuiltinMemberReturnRule::StringView)
                    return "StringView";
            }
        }
        if(expr.lhs->kind == ExprKind::Identifier && !expr.arguments.empty() && expr.arguments[0]){
            if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr.lhs->text)){
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::Bool)
                    return "b8";
                if(fnInfo->returnRule == BuiltinFunctionReturnRule::Usize)
                    return "usize";
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
    else if((expr.kind == ExprKind::Cast || expr.kind == ExprKind::BitCast) && expr.targetType){
        return m_symbols.resolveBaseTypeName(expr.targetType->name);
    }
    else if(expr.kind == ExprKind::ScopeAccess && !expr.scopeChain.empty()){
        return resolveScopeAccessEnumTypeName(ownerClass, method, expr, scope);
    }
    else if(expr.kind == ExprKind::StringLiteral){
        return "StringView";
    }
    else if(expr.kind == ExprKind::BoolLiteral){
        return "b8";
    }
    else if(expr.kind == ExprKind::FloatLiteral){
        return "f32";
    }
    else if(expr.kind == ExprKind::DoubleLiteral){
        return "f64";
    }
    else if(expr.kind == ExprKind::IntegerLiteral){
        const MStringView t = expr.text;
        b8 hasU = false;
        b8 hasL = false;
        for(usize i = t.size(); i > 0u; --i){
            const MChar c = t[i - 1u];
            if(c == 'u' || c == 'U')
                hasU = true;
            else if(c == 'l' || c == 'L')
                hasL = true;
            else
                break;
        }
        if(hasL)
            return hasU ? "u64" : "i64";
        return hasU ? "u32" : "i32";
    }
    return {};
}

const TypeRef* TypeChecker::inferExprTypeRef(ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    if(expr.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = FindScopedVar(scope, expr.text))
            return m_symbols.unwrapTypeAlias(local->type);
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
        const MStringView opSym = MapTokenTypeToOperatorSymbol(expr.op);
        if(!opSym.empty()){
            const TypeRef* lhsTypeRef = nullptr;
            const MStringView lhsType = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
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
    else if(expr.kind == ExprKind::Call && expr.lhs){
        const usize callArgCount = expr.arguments.size();
        MVector<MStringView> argTypeHints(m_arena);
        MVector<u8> argIsMutHints(m_arena);
        buildCallArgHints(ownerClass, method, expr.arguments, scope, argTypeHints, argIsMutHints);
        if(expr.lhs->kind == ExprKind::Identifier){
            if(const MethodDecl* fn = m_symbols.findFreeFunction(expr.lhs->text, &callArgCount, argTypeHints, argIsMutHints)){
                if(!fn->returnType)
                    return nullptr;
                const TypeRef* rt = (!fn->templateParams.empty() && !expr.callTemplateArgs.empty())
                    ? m_symbols.substituteTypeParams(*fn->returnType, fn->templateParams, expr.callTemplateArgs)
                    : fn->returnType;
                return m_symbols.unwrapTypeAlias(rt);
            }
            if(const ScopedVarInfo* local = FindScopedVar(scope, expr.lhs->text)){
                if(const TypeRef* ft = m_symbols.unwrapTypeAlias(local->type); ft && ft->isFuncType && ft->funcReturnType)
                    return m_symbols.unwrapTypeAlias(ft->funcReturnType);
                if(local->lambdaExpr && local->lambdaExpr->lambdaReturnType)
                    return m_symbols.unwrapTypeAlias(local->lambdaExpr->lambdaReturnType);
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
                const TypeRef* rt = (!m->templateParams.empty() && !expr.callTemplateArgs.empty())
                    ? m_symbols.substituteTypeParams(*m->returnType, m->templateParams, expr.callTemplateArgs)
                    : m->returnType;
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
            if(recvTypeRef && !recvTypeRef->templateArgs.empty() && recvTypeRef->templateArgs[0].type){
                if(const BuiltinMemberMethodInfo* memberInfo = FindBuiltinMemberMethod(mName)){
                    if(memberInfo->returnRule == BuiltinMemberReturnRule::OptionalOrExpectedValue && (lhsType == "Optional" || lhsType == "Expected"))
                        return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[0].type);
                    if(memberInfo->returnRule == BuiltinMemberReturnRule::ExpectedError && lhsType == "Expected" && recvTypeRef->templateArgs.size() >= 2u && recvTypeRef->templateArgs[1].type)
                        return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[1].type);
                    if(memberInfo->returnRule == BuiltinMemberReturnRule::FirstTypeArg && (IsSingleTypeParamBuiltinGeneric(lhsType) || IsTypeAndCountBuiltinGeneric(lhsType)))
                        return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[0].type);
                    if(memberInfo->returnRule == BuiltinMemberReturnRule::AtomicValue && lhsType == "Atomic")
                        return m_symbols.unwrapTypeAlias(recvTypeRef->templateArgs[0].type);
                    if(mName == "subspan" && lhsType == "Span")
                        return m_symbols.unwrapTypeAlias(recvTypeRef);
                }
            }
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                const b8 recvIsConst = method ? isReceiverConst(ownerClass, *method, *expr.lhs->lhs, scope) : false;
                if(const MethodDecl* m = m_symbols.findClassMethod(*cls, mName, &callArgCount, recvIsConst, ownerClass != cls, argTypeHints, argIsMutHints)){
                    if(!m->returnType)
                        return nullptr;
                    TypeRef* rt = m_symbols.substituteTypeParams(m->returnType, cls->templateParams, recvTypeRef);
                    if(!m->templateParams.empty() && !expr.callTemplateArgs.empty())
                        rt = m_symbols.substituteTypeParams(*rt, m->templateParams, expr.callTemplateArgs);
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

MStringView TypeChecker::inferExprBaseType(ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope, const TypeRef** outTypeRef)const noexcept{
    const TypeRef* typeRef = inferExprTypeRef(ownerClass, method, expr, scope);
    if(outTypeRef)
        *outTypeRef = typeRef;
    return typeRef
        ? m_symbols.resolveBaseTypeName(typeRef->name)
        : inferExprTypeName(ownerClass, method, expr, scope);
}

void TypeChecker::analyzeBlockMoveAtLastUse(const Span<Stmt* const> statements, const Span<const ScopedVarInfo> scope, const usize savedScopeSize)const noexcept{
    for(usize i = savedScopeSize; i < scope.size(); ++i){
        const ScopedVarInfo& local = scope[i];
        if(local.isParam || local.isConst || (local.type && m_symbols.resolveRefMode(local.type) != ParamPassingMode::None))
            continue;

        usize lastUseIndex = statements.size();
        for(usize s = 0u; s < statements.size(); ++s){
            if(__hidden_adhd_type_checker_infer::StmtReferencesIdentifier(statements[s], local.name))
                lastUseIndex = s;
        }

        if(SymbolTable::IsSimdType(resolveScopedVarBaseType(local)))
            continue;

        b8 capturedOrAsyncOrAliased = false;
        for(usize j = savedScopeSize; j < scope.size(); ++j){
            if(const Expr* lam = scope[j].lambdaExpr){
                for(const LambdaCapture& cap : lam->lambdaCaptures){
                    if(cap.mode == ParamPassingMode::Mut && cap.name == local.name){
                        capturedOrAsyncOrAliased = true;
                        break;
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

