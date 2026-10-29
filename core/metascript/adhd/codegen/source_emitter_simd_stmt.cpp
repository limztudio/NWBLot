// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitSimdVectorCtorReg(
    MString& out,
    const MStringView simdType,
    const MVector<Expr*>& arguments
)const{
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);

    if(m_symbols.isSimdMatrixType(simdType)){
        if(arguments.empty()){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        if(arguments.size() == 1u && arguments[0] && !arguments[0]->resolvedSimdType.empty()){
            emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        out.append(isDouble ? "::MatrixDoubleSet(" : "::MatrixSet(");
        if(simdType == "f64x3x3" && arguments.size() == 9u){
            for(usize r = 0u; r < 3u; ++r){
                for(usize c = 0u; c < 3u; ++c){
                    if(r > 0u || c > 0u)
                        out.append(", ");
                    const usize argIdx = r * 3u + c;
                    if(arguments[argIdx])
                        emitExpr(out, *arguments[argIdx]);
                    else
                        out.append("0.0");
                }
                out.append(", 0.0");
            }
            out.append(", 0.0, 0.0, 0.0, 1.0)");
            return;
        }
        for(usize i = 0u; i < 16u; ++i){
            if(i > 0u)
                out.append(", ");
            if(i < arguments.size() && arguments[i])
                emitExpr(out, *arguments[i]);
            else if(i == 15u && arguments.size() == 12u)
                out.append(isDouble ? "1.0" : "1.0f");
            else
                out.append(isDouble ? "0.0" : "0.0f");
        }
        out.push_back(')');
        return;
    }

    if(arguments.empty()){
        if(isInt)
            out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
        else
            out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
        return;
    }
    if(arguments.size() == 1u && arguments[0]){
        if(!arguments[0]->resolvedSimdType.empty()){
            emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        if(isInt){
            out.append(isDouble ? "::VectorDoubleReplicateInt(static_cast<u64>(" : "::VectorReplicateInt(static_cast<u32>(");
            emitExpr(out, *arguments[0]);
            out.append("))");
        }
        else{
            out.append(isDouble ? "::VectorDoubleReplicate(" : "::VectorReplicate(");
            emitExpr(out, *arguments[0]);
            out.push_back(')');
        }
        return;
    }

    if(isInt){
        out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
        for(usize i = 0u; i < 4u; ++i){
            if(i > 0u)
                out.append(", ");
            out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
            if(i < arguments.size() && arguments[i])
                emitExpr(out, *arguments[i]);
            else
                out.append("0u");
            out.push_back(')');
        }
        out.push_back(')');
        return;
    }

    out.append(isDouble ? "::VectorDoubleSet(" : "::VectorSet(");
    for(usize i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        if(i < arguments.size() && arguments[i])
            emitExpr(out, *arguments[i]);
        else
            out.append(isDouble ? "0.0" : "0.0f");
    }
    out.push_back(')');
}

const SimdVarBinding* SourceEmitter::findSimdBinding(const b8 isThisMember, const MStringView name)const noexcept{
    if(!m_activeSimdBindings)
        return nullptr;
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isThisMember == isThisMember && binding.name == name)
            return &binding;
    }
    return nullptr;
}

const SimdVarBinding* SourceEmitter::findSimdBindingForExpr(const Expr* const expr)const noexcept{
    if(!expr)
        return nullptr;
    if(expr->kind == ExprKind::Identifier)
        return findSimdBinding(false, expr->text);
    if(expr->kind == ExprKind::ThisMember)
        return findSimdBinding(true, expr->text);
    return nullptr;
}

void SourceEmitter::emitSimdStoreForBinding(
    MString& out,
    const SimdVarBinding& binding,
    const u32 indent
)const{
    const MStringView storeFn = SimdStoreFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    out.append(storeFn.data(), storeFn.size());
    out.push_back('(');
    out.append(binding.regName.data(), binding.regName.size());
    out.append(", ");
    if(binding.isThisMember)
        out.append("this->");
    out.append(binding.name.data(), binding.name.size());
    out.append(");\r\n");
}

void SourceEmitter::emitSimdLoadReloadForBinding(
    MString& out,
    const SimdVarBinding& binding,
    const u32 indent
)const{
    const MStringView loadFn = SimdLoadFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    out.append(binding.regName.data(), binding.regName.size());
    out.append(" = ");
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    if(binding.isThisMember)
        out.append("this->");
    out.append(binding.name.data(), binding.name.size());
    out.append(");\r\n");
}

void SourceEmitter::emitSimdLoopElementLoad(
    MString& out,
    const SimdVarBinding& loopBinding,
    const u32 indent
)const{
    const MStringView regType = SimdRegisterCppType(loopBinding.simdType);
    const MStringView loadFn = SimdLoadFunctionCpp(loopBinding.simdType);
    emitIndent(out, indent);
    if(!loopBinding.written && !loopBinding.needsStorageSync)
        out.append("const ");
    out.append(regType.data(), regType.size());
    out.push_back(' ');
    out.append(loopBinding.regName.data(), loopBinding.regName.size());
    out.append(" = ");
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    out.append(loopBinding.name.data(), loopBinding.name.size());
    out.append(");\r\n");
}

void SourceEmitter::emitSimdMutCallReloads(
    MString& out,
    const Expr& expr,
    const u32 indent
)const{
    if(!m_activeSimdBindings)
        return;
    if(expr.lhs)
        emitSimdMutCallReloads(out, *expr.lhs, indent);
    if(expr.rhs)
        emitSimdMutCallReloads(out, *expr.rhs, indent);
    for(const Expr* arg : expr.arguments){
        if(arg)
            emitSimdMutCallReloads(out, *arg, indent);
    }
    if(expr.kind == ExprKind::Call){
        for(const Expr* arg : expr.arguments){
            if(!arg || !arg->isMutArg)
                continue;
            const SimdVarBinding* mutBinding = findSimdBindingForExpr(arg);
            if(!mutBinding && (arg->kind == ExprKind::MemberAccess || arg->kind == ExprKind::Index) && arg->lhs)
                mutBinding = findSimdBindingForExpr(arg->lhs);
            if(mutBinding)
                emitSimdLoadReloadForBinding(out, *mutBinding, indent);
        }
        const b8 isThisMethodCall = (expr.lhs && expr.lhs->kind == ExprKind::ThisMember);
        for(const SimdVarBinding& binding : *m_activeSimdBindings){
            if(binding.capturedByMutLambda || (isThisMethodCall && binding.isThisMember))
                emitSimdLoadReloadForBinding(out, binding, indent);
        }
    }
}

void SourceEmitter::emitSimdBlockStores(MString& out, const u32 indent, const b8 loopElementsOnly)const{
    if(!m_activeSimdBindings)
        return;
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isLocalDecl || !binding.written)
            continue;
        if(loopElementsOnly && !binding.isLoopElement)
            continue;
        emitSimdStoreForBinding(out, binding, indent);
    }
}

void SourceEmitter::emitScopedSimdBody(
    MString& out,
    const MVector<Stmt*>& statements,
    const u32 indent
)const{
    MVector<SimdVarBinding> simdBindings(m_arena);
    CollectScopedSimdBindings(m_arena, simdBindings, statements, m_symbols);

    const MVector<SimdVarBinding>* prevBindings = m_activeSimdBindings;
    m_activeSimdBindings = simdBindings.empty() ? nullptr : &simdBindings;

    for(const SimdVarBinding& binding : simdBindings){
        if(binding.isLocalDecl || binding.isLoopElement)
            continue;

        const MStringView regType = SimdRegisterCppType(binding.simdType);
        const MStringView loadFn = SimdLoadFunctionCpp(binding.simdType);

        emitIndent(out, indent);
        if(!binding.written && !binding.needsStorageSync)
            out.append("const ");
        out.append(regType.data(), regType.size());
        out.push_back(' ');
        out.append(binding.regName.data(), binding.regName.size());
        if(binding.readBeforeWrite || !binding.written){
            out.append(" = ");
            out.append(loadFn.data(), loadFn.size());
            out.push_back('(');
            if(binding.isThisMember)
                out.append("this->");
            out.append(binding.name.data(), binding.name.size());
            out.append(");\r\n");
        }
        else{
            out.append("{};\r\n");
        }
    }

    emitStatementList(out, statements, indent);

    const b8 endsWithTerminal = !statements.empty() && statements.back() && (
        statements.back()->kind == StmtKind::Return
        || statements.back()->kind == StmtKind::Break
        || statements.back()->kind == StmtKind::Continue
    );
    if(!endsWithTerminal)
        emitSimdBlockStores(out, indent, false);

    m_activeSimdBindings = prevBindings;
}

b8 SourceEmitter::tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    const SimdVarBinding* simdLocal = findSimdBinding(false, stmt.name);
    if(simdLocal && simdLocal->isLocalDecl){
        const MStringView regType = SimdRegisterCppType(simdLocal->simdType);
        emitIndent(out, indent);
        if(stmt.isConst && !simdLocal->written && !simdLocal->needsStorageSync)
            out.append("const ");
        out.append(regType.data(), regType.size());
        out.push_back(' ');
        out.append(simdLocal->regName.data(), simdLocal->regName.size());
        out.append(" = ");
        if(stmt.isCtorInit){
            emitSimdVectorCtorReg(out, simdLocal->simdType, stmt.arguments);
        }
        else if(stmt.expr){
            emitSimdRegExpr(out, *stmt.expr, simdLocal->simdType);
        }
        else{
            const b8 isDouble = IsDoubleSimdType(simdLocal->simdType);
            const b8 isInt = IsIntSimdType(simdLocal->simdType);
            if(m_symbols.isSimdMatrixType(simdLocal->simdType))
                out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            else if(isInt)
                out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
            else
                out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
        }
        out.append(";\r\n");

        if(simdLocal->needsStorageSync){
            emitIndent(out, indent);
            if(stmt.varType)
                m_typeEmitter.emitTypeCpp(out, *stmt.varType);
            else{
                TypeRef inferredType(m_arena);
                inferredType.name = simdLocal->simdType;
                m_typeEmitter.emitTypeCpp(out, inferredType);
            }
            out.push_back(' ');
            out.append(stmt.name.data(), stmt.name.size());
            out.append("{};\r\n");
            emitSimdStoreForBinding(out, *simdLocal, indent);
        }
        if(stmt.expr)
            emitSimdMutCallReloads(out, *stmt.expr, indent);
        for(const Expr* arg : stmt.arguments){
            if(arg)
                emitSimdMutCallReloads(out, *arg, indent);
        }
        return true;
    }
    return false;
}

b8 SourceEmitter::tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.expr && stmt.expr->kind == ExprKind::Binary
        && IsSimdAssignmentOp(stmt.expr->op)
        && stmt.expr->lhs && stmt.expr->rhs)
    {
        const Expr& lhs = *stmt.expr->lhs;
        const Expr& rhs = *stmt.expr->rhs;

        if(IsRegisterPromotableSimdType(lhs.resolvedSimdType)){
            const MStringView simdType = lhs.resolvedSimdType;
            const SimdVarBinding* targetBinding = findSimdBindingForExpr(&lhs);
            const SimdVarBinding* rhsBinding = findSimdBindingForExpr(&rhs);

            const b8 shouldLowerNonBindingAssign = !targetBinding && (
                stmt.expr->op != TokenType::Equal
                || rhsBinding != nullptr
                || ExprContainsSimdMath(&rhs, m_symbols)
            );

            if(targetBinding || shouldLowerNonBindingAssign){
                emitIndent(out, indent);
                if(targetBinding){
                    out.append(targetBinding->regName.data(), targetBinding->regName.size());
                    out.append(" = ");
                }
                else{
                    const MStringView storeFn = SimdStoreFunctionCpp(simdType);
                    out.append(storeFn.data(), storeFn.size());
                    out.push_back('(');
                }

                if(stmt.expr->op == TokenType::Equal){
                    emitSimdRegExpr(out, rhs, simdType);
                }
                else{
                    (void)tryEmitSimdBinaryOpReg(out, MapSimdAssignToBinaryOp(stmt.expr->op), lhs, rhs, simdType);
                }

                if(targetBinding){
                    out.append(";\r\n");
                    if(targetBinding->needsStorageSync)
                        emitSimdStoreForBinding(out, *targetBinding, indent);
                }
                else{
                    out.append(", ");
                    emitExpr(out, lhs);
                    out.append(");\r\n");
                }
                emitSimdMutCallReloads(out, rhs, indent);
                return true;
            }
        }

        u32 lane = 0u;
        if(lhs.kind == ExprKind::MemberAccess && lhs.lhs
            && IsRegisterPromotableSimdType(lhs.lhs->resolvedSimdType)
            && IsSingleSwizzleLane(lhs.text, lane))
        {
            const SimdVarBinding* targetBinding = findSimdBindingForExpr(lhs.lhs);

            if(targetBinding){
                const MStringView simdType = targetBinding->simdType;
                const b8 isDouble = IsDoubleSimdType(simdType);
                const b8 isInt = IsIntSimdType(simdType);
                static constexpr const char* s_LaneSuffix[4] = { "X(", "Y(", "Z(", "W(" };

                emitIndent(out, indent);
                out.append(targetBinding->regName.data(), targetBinding->regName.size());
                out.append(" = ");
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
                else
                    out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
                out.append(s_LaneSuffix[lane]);
                out.append(targetBinding->regName.data(), targetBinding->regName.size());
                out.append(", ");
                if(isInt)
                    out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                if(stmt.expr->op == TokenType::Equal){
                    emitExpr(out, rhs);
                }
                else{
                    const MStringView opSym = SimdCompoundScalarOpSymbol(stmt.expr->op);
                    out.push_back('(');
                    emitExpr(out, lhs);
                    out.append(opSym.data(), opSym.size());
                    out.push_back('(');
                    emitExpr(out, rhs);
                    out.append("))");
                }
                if(isInt)
                    out.push_back(')');
                out.append(");\r\n");

                if(targetBinding->needsStorageSync)
                    emitSimdStoreForBinding(out, *targetBinding, indent);
                emitSimdMutCallReloads(out, rhs, indent);
                return true;
            }
        }

        u32 vecLanes = 0u;
        if(lhs.kind == ExprKind::Index && lhs.lhs && lhs.rhs
            && IsRegisterPromotableSimdType(lhs.lhs->resolvedSimdType)
            && m_symbols.isSimdVectorType(lhs.lhs->resolvedSimdType, vecLanes))
        {
            const SimdVarBinding* targetBinding = findSimdBindingForExpr(lhs.lhs);

            if(targetBinding){
                const MStringView simdType = targetBinding->simdType;
                const b8 isDouble = IsDoubleSimdType(simdType);
                const b8 isInt = IsIntSimdType(simdType);

                emitIndent(out, indent);
                out.append(targetBinding->regName.data(), targetBinding->regName.size());
                out.append(" = ");
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleSetIntByIndex(" : "::VectorSetIntByIndex(");
                else
                    out.append(isDouble ? "::VectorDoubleSetByIndex(" : "::VectorSetByIndex(");
                out.append(targetBinding->regName.data(), targetBinding->regName.size());
                out.append(", ");
                if(isInt)
                    out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                if(stmt.expr->op == TokenType::Equal){
                    emitExpr(out, rhs);
                }
                else{
                    const MStringView opSym = SimdCompoundScalarOpSymbol(stmt.expr->op);
                    out.push_back('(');
                    emitExpr(out, lhs);
                    out.append(opSym.data(), opSym.size());
                    out.push_back('(');
                    emitExpr(out, rhs);
                    out.append("))");
                }
                if(isInt)
                    out.push_back(')');
                out.append(", static_cast<usize>(");
                emitExpr(out, *lhs.rhs);
                out.append("));\r\n");

                if(targetBinding->needsStorageSync)
                    emitSimdStoreForBinding(out, *targetBinding, indent);
                emitSimdMutCallReloads(out, rhs, indent);
                return true;
            }
        }
    }
    return false;
}

b8 SourceEmitter::tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.expr && IsRegisterPromotableSimdType(stmt.expr->resolvedSimdType)){
        const SimdVarBinding* retBinding = findSimdBindingForExpr(stmt.expr);

        const b8 needsRegStore = (retBinding && retBinding->isLocalDecl && !retBinding->needsStorageSync)
            || ExprContainsSimdMath(stmt.expr, m_symbols);
        if(needsRegStore){
            const MStringView simdType = stmt.expr->resolvedSimdType;
            const MStringView storeFn = SimdStoreFunctionCpp(simdType);
            TypeRef retType(m_arena);
            retType.name = simdType;

            emitIndent(out, indent);
            m_typeEmitter.emitTypeCpp(out, retType);
            out.append(" __adhd_simd_ret{};\r\n");
            emitIndent(out, indent);
            out.append(storeFn.data(), storeFn.size());
            out.push_back('(');
            emitSimdRegExpr(out, *stmt.expr, simdType);
            out.append(", __adhd_simd_ret);\r\n");
            emitIndent(out, indent);
            out.append("return __adhd_simd_ret;\r\n");
            return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

