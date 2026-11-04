// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitSimdVectorCtorReg(MString& out, const MStringView simdType, const Span<const Expr* const> arguments)const{
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);

    if(SymbolTable::IsSimdMatrixType(simdType)){
        if(arguments.empty()){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        if(arguments.size() == 1u && arguments[0] && !arguments[0]->resolvedSimdType.empty()){
            emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        out.append(isDouble ? "::MatrixDoubleSet(" : "::MatrixSet(");
        const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(simdType);
        const u32 elemCount = matInfo ? matInfo->elemCount : 16u;
        const b8 isSplat = (arguments.size() == 1u);
        if(elemCount == 9u){
            for(usize r = 0u; r < 3u; ++r){
                for(usize c = 0u; c < 3u; ++c){
                    if(r > 0u || c > 0u)
                        out.append(", ");
                    const usize argIdx = isSplat ? 0u : (r * 3u + c);
                    if(argIdx < arguments.size() && arguments[argIdx])
                        emitExpr(out, *arguments[argIdx]);
                    else
                        out.append(isDouble ? "0.0" : "0.0f");
                }
                out.append(isDouble ? ", 0.0" : ", 0.0f");
            }
            out.append(isDouble ? ", 0.0, 0.0, 0.0, 1.0)" : ", 0.0f, 0.0f, 0.0f, 1.0f)");
            return;
        }
        for(usize i = 0u; i < 16u; ++i){
            if(i > 0u)
                out.append(", ");
            const usize argIdx = isSplat ? 0u : i;
            if(i < elemCount && argIdx < arguments.size() && arguments[argIdx])
                emitExpr(out, *arguments[argIdx]);
            else if(i == 15u && elemCount == 12u)
                out.append(isDouble ? "1.0" : "1.0f");
            else
                out.append(isDouble ? "0.0" : "0.0f");
        }
        out.push_back(')');
        return;
    }

    b8 allZeroArgs = true;
    for(const Expr* arg : arguments){
        if(!arg || !IsZeroScalarLiteral(*arg)){
            allZeroArgs = false;
            break;
        }
    }
    if(arguments.empty() || allZeroArgs){
        if(isInt)
            out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
        else
            out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
        return;
    }
    if(arguments.size() == 1u && arguments[0]){
        emitSimdRegExpr(out, *arguments[0], simdType);
        return;
    }

    const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(simdType);
    const Expr* unalignedBase = nullptr;
    u32 firstUnalignedLane = 0u;
    if(
        simdVecInfo
        && !simdVecInfo->isMixed
        && TryMatchSingleLaneAccess(arguments[0], unalignedBase, firstUnalignedLane)
        && firstUnalignedLane == 0u
        && !unalignedBase->resolvedUnalignedType.empty()
    ){
        const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(unalignedBase->resolvedUnalignedType);
        if(unalignedInfo && !unalignedInfo->isHalfFloat && unalignedInfo->laneScalarType == simdVecInfo->xyzScalarType && arguments.size() >= unalignedInfo->lanes){
            b8 matchesUnalignedPrefix = true;
            for(u32 i = 1u; i < unalignedInfo->lanes; ++i){
                const Expr* laneBase = nullptr;
                u32 laneIdx = 0u;
                if(!TryMatchSingleLaneAccess(arguments[i], laneBase, laneIdx) || laneIdx != i || !AreSamePureSimdExpr(*unalignedBase, *laneBase)){
                    matchesUnalignedPrefix = false;
                    break;
                }
            }
            if(matchesUnalignedPrefix){
                b8 trailingAllZero = true;
                for(usize i = unalignedInfo->lanes; i < arguments.size(); ++i){
                    if(!arguments[i] || !IsZeroScalarLiteral(*arguments[i])){
                        trailingAllZero = false;
                        break;
                    }
                }
                if(trailingAllZero){
                    out.append(simdVecInfo->loadCppFunc.data(), simdVecInfo->loadCppFunc.size());
                    out.push_back('(');
                    emitExpr(out, *unalignedBase);
                    out.push_back(')');
                    return;
                }
                if(unalignedInfo->lanes == 3u && arguments.size() == 4u && arguments[3]){
                    if(isInt)
                        out.append(isDouble ? "::VectorDoubleSetIntW(" : "::VectorSetIntW(");
                    else
                        out.append(isDouble ? "::VectorDoubleSetW(" : "::VectorSetW(");
                    out.append(simdVecInfo->loadCppFunc.data(), simdVecInfo->loadCppFunc.size());
                    out.push_back('(');
                    emitExpr(out, *unalignedBase);
                    out.append("), ");
                    if(isInt)
                        out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                    emitExpr(out, *arguments[3]);
                    if(isInt)
                        out.push_back(')');
                    out.push_back(')');
                    return;
                }
            }
        }
    }

    Array<const Expr*, 4> laneBases{};
    Array<u32, 4> laneIndices{};
    for(usize i = 0u; i < 4u && i < arguments.size(); ++i){
        const Expr* base = nullptr;
        u32 idx = 0u;
        if(TryMatchSingleLaneAccess(arguments[i], base, idx) && base->resolvedSimdType == simdType){
            laneBases[i] = base;
            laneIndices[i] = idx;
        }
    }

    if(arguments.size() == 4u){
        for(u32 replaceLane = 0u; replaceLane < 4u; ++replaceLane){
            if(laneBases[replaceLane] != nullptr || !arguments[replaceLane])
                continue;
            const Expr* v0 = nullptr;
            b8 threeNaturalMatch = true;
            for(u32 i = 0u; i < 4u; ++i){
                if(i == replaceLane)
                    continue;
                if(!laneBases[i] || laneIndices[i] != i){
                    threeNaturalMatch = false;
                    break;
                }
                if(!v0)
                    v0 = laneBases[i];
                else if(!AreSamePureSimdExpr(*v0, *laneBases[i])){
                    threeNaturalMatch = false;
                    break;
                }
            }
            if(threeNaturalMatch && v0){
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
                else
                    out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
                const MStringView laneSuffix = SimdLaneCallSuffix(replaceLane);
                out.append(laneSuffix.data(), laneSuffix.size());
                emitSimdRegExpr(out, *v0, simdType);
                out.append(", ");
                if(isInt)
                    out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                emitExpr(out, *arguments[replaceLane]);
                if(isInt)
                    out.push_back(')');
                out.push_back(')');
                return;
            }
        }
    }

    {
        const Expr* v0 = nullptr;
        const Expr* v1 = nullptr;
        Array<u32, 4> permIdx{};
        b8 hasZeroLane = false;
        b8 canSwizzleOrPermute = true;
        for(u32 i = 0u; i < 4u; ++i){
            if(laneBases[i]){
                if(!v0){
                    v0 = laneBases[i];
                    permIdx[i] = laneIndices[i];
                }
                else if(AreSamePureSimdExpr(*v0, *laneBases[i])){
                    permIdx[i] = laneIndices[i];
                }
                else if(!v1 && !hasZeroLane){
                    v1 = laneBases[i];
                    permIdx[i] = 4u + laneIndices[i];
                }
                else if(v1 && AreSamePureSimdExpr(*v1, *laneBases[i])){
                    permIdx[i] = 4u + laneIndices[i];
                }
                else{
                    canSwizzleOrPermute = false;
                    break;
                }
            }
            else if(!v1 && (i >= arguments.size() || (arguments[i] && IsZeroScalarLiteral(*arguments[i])))){
                hasZeroLane = true;
                permIdx[i] = 4u + i;
            }
            else{
                canSwizzleOrPermute = false;
                break;
            }
        }
        if(canSwizzleOrPermute && v0){
            if(!v1 && !hasZeroLane){
                if(permIdx[0] == 0u && permIdx[1] == 1u && permIdx[2] == 2u && permIdx[3] == 3u){
                    emitSimdRegExpr(out, *v0, simdType);
                    return;
                }
                out.append(isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
                for(u32 i = 0u; i < 4u; ++i){
                    if(i > 0u)
                        out.append(", ");
                    AppendUnsignedDecimal(out, permIdx[i]);
                    out.push_back('u');
                }
                out.append(">(");
                emitSimdRegExpr(out, *v0, simdType);
                out.push_back(')');
                return;
            }
            out.append(isDouble ? "::VectorDoublePermute<" : "::VectorPermute<");
            for(u32 i = 0u; i < 4u; ++i){
                if(i > 0u)
                    out.append(", ");
                AppendUnsignedDecimal(out, permIdx[i]);
                out.push_back('u');
            }
            out.append(">(");
            emitSimdRegExpr(out, *v0, simdType);
            out.append(", ");
            if(v1){
                emitSimdRegExpr(out, *v1, simdType);
            }
            else if(isInt){
                out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
            }
            else{
                out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
            }
            out.push_back(')');
            return;
        }
    }

    if(
        arguments.size() == 4u
        && arguments[0] && arguments[1] && arguments[2] && arguments[3]
        && arguments[0]->resolvedSimdType.empty()
        && IsPureSimdAddressExpr(*arguments[0])
        && AreSamePureSimdExpr(*arguments[0], *arguments[1])
        && AreSamePureSimdExpr(*arguments[0], *arguments[2])
        && AreSamePureSimdExpr(*arguments[0], *arguments[3])
    ){
        emitSimdRegExpr(out, *arguments[0], simdType);
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

b8 SourceEmitter::isDirectSimdRegExpr(const Expr& expr)const noexcept{
    if(!IsRegisterPromotableSimdType(expr.resolvedSimdType))
        return false;
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle)
        return true;
    if(expr.kind == ExprKind::Unary)
        return true;
    if(expr.kind == ExprKind::Binary && expr.op != TokenType::Equal)
        return true;
    if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        if(expr.isBuiltinCall)
            return true;
        const MStringView baseCallee = m_symbols.resolveBaseTypeName(expr.lhs->text);
        if(SymbolTable::IsSimdMatrixType(baseCallee))
            return true;
        if(SymbolTable::IsSimdType(baseCallee)){
            for(const Expr* arg : expr.arguments){
                if(!arg)
                    continue;
                if(findSimdBindingForExpr(arg) || ExprContainsSimdMath(arg, m_symbols))
                    return true;
                const Expr* laneBase = nullptr;
                u32 laneIdx = 0u;
                if(TryMatchSingleLaneAccess(arg, laneBase, laneIdx) && !laneBase->resolvedUnalignedType.empty())
                    return true;
            }
        }
    }
    return false;
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

const SimdVarBinding* SourceEmitter::findSimdRootBindingForExpr(const Expr* const expr)const noexcept{
    if(!expr)
        return nullptr;
    if(const SimdVarBinding* direct = findSimdBindingForExpr(expr))
        return direct;
    if((expr->kind == ExprKind::MemberAccess || expr->kind == ExprKind::Index) && expr->lhs)
        return findSimdBindingForExpr(expr->lhs);
    return nullptr;
}

void SourceEmitter::emitSimdBindingInitialDecl(MString& out, const SimdVarBinding& binding, const b8 loadFromStorage, const b8 qualifyThisMember, const u32 indent)const{
    const MStringView regType = SimdRegisterCppType(binding.simdType);
    const MStringView loadFn = SimdLoadFunctionCpp(binding.simdType);
    emitIndent(out, indent);
    if(!binding.written && !binding.needsStorageSync)
        out.append("const ");
    out.append(regType.data(), regType.size());
    out.push_back(' ');
    out.append(binding.regName.data(), binding.regName.size());
    if(loadFromStorage){
        out.append(" = ");
        out.append(loadFn.data(), loadFn.size());
        out.push_back('(');
        if(qualifyThisMember && binding.isThisMember)
            out.append("this->");
        out.append(binding.name.data(), binding.name.size());
        out.append(");\r\n");
    }
    else{
        out.append("{};\r\n");
    }
}

void SourceEmitter::emitSimdStoreForBinding(MString& out, const SimdVarBinding& binding, const u32 indent)const{
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

void SourceEmitter::emitSimdLoadReloadForBinding(MString& out, const SimdVarBinding& binding, const u32 indent)const{
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

void SourceEmitter::emitSimdMutCallReloads(MString& out, const Expr& expr, const u32 indent)const{
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
            if(const SimdVarBinding* mutBinding = findSimdRootBindingForExpr(arg))
                emitSimdLoadReloadForBinding(out, *mutBinding, indent);
        }
        const b8 isThisMethodCall = (expr.lhs && expr.lhs->kind == ExprKind::ThisMember);
        for(const SimdVarBinding& binding : *m_activeSimdBindings){
            if(binding.capturedByMutLambda || (isThisMethodCall && binding.isThisMember))
                emitSimdLoadReloadForBinding(out, binding, indent);
        }
    }
}

void SourceEmitter::emitSimdBlockStores(MString& out, const u32 indent)const{
    if(!m_activeSimdBindings)
        return;
    b8 hasPendingStore = (m_activeSimdLoopBinding != nullptr);
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isLocalDecl || binding.isLoopElement || !binding.written)
            continue;
        hasPendingStore = true;
        break;
    }
    if(!hasPendingStore)
        return;
    if(m_activeScopeUsesTaskScope){
        emitIndent(out, indent);
        out.append("__adhd_task_scope.wait();\r\n");
    }
    if(m_activeSimdLoopBinding)
        emitSimdStoreForBinding(out, *m_activeSimdLoopBinding, indent);
    for(const SimdVarBinding& binding : *m_activeSimdBindings){
        if(binding.isLocalDecl || binding.isLoopElement || !binding.written)
            continue;
        emitSimdStoreForBinding(out, binding, indent);
    }
}

void SourceEmitter::emitScopedSimdBody(MString& out, const Span<const Stmt* const> statements, const u32 indent)const{
    MVector<SimdVarBinding> simdBindings(m_arena);
    CollectScopedSimdBindings(statements, m_symbols, m_arena, simdBindings);

    const MVector<SimdVarBinding>* prevBindings = m_activeSimdBindings;
    const SimdVarBinding* prevLoopBinding = m_activeSimdLoopBinding;
    const u32 prevInnerBreakDepth = m_activeSimdLoopInnerBreakDepth;
    const u32 prevInnerContinueDepth = m_activeSimdLoopInnerContinueDepth;
    const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
    m_activeSimdBindings = simdBindings.empty() ? nullptr : &simdBindings;
    m_activeSimdLoopBinding = nullptr;
    m_activeSimdLoopInnerBreakDepth = 0u;
    m_activeSimdLoopInnerContinueDepth = 0u;
    m_activeSwitchBreakLabelLine = 0u;

    for(const SimdVarBinding& binding : simdBindings){
        if(binding.isLocalDecl || binding.isLoopElement)
            continue;
        emitSimdBindingInitialDecl(out, binding, binding.readBeforeWrite || !binding.written, true, indent);
    }

    emitStatementList(out, statements, indent);

    const b8 endsWithTerminal = !statements.empty() && statements.back() && IsTerminalJumpStmtKind(statements.back()->kind);
    if(!endsWithTerminal)
        emitSimdBlockStores(out, indent);

    m_activeSimdBindings = prevBindings;
    m_activeSimdLoopBinding = prevLoopBinding;
    m_activeSimdLoopInnerBreakDepth = prevInnerBreakDepth;
    m_activeSimdLoopInnerContinueDepth = prevInnerContinueDepth;
    m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;
}

b8 SourceEmitter::tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.usesScratchArena || (stmt.varType && stmt.varType->refMode != ParamPassingMode::None))
        return false;
    const SimdVarBinding* simdLocal = findSimdBinding(false, stmt.name);
    if(simdLocal && simdLocal->isLocalDecl){
        const MStringView varSimdType = ResolveLocalVarDeclSimdType(stmt, m_symbols);
        if(varSimdType != simdLocal->simdType)
            return false;
        const MStringView regType = SimdRegisterCppType(simdLocal->simdType);
        emitIndent(out, indent);
        if(stmt.isConst && !simdLocal->written && !simdLocal->needsStorageSync)
            out.append("const ");
        out.append(regType.data(), regType.size());
        out.push_back(' ');
        out.append(simdLocal->regName.data(), simdLocal->regName.size());
        out.append(" = ");
        if(stmt.expr && !stmt.isCtorInit)
            emitSimdRegExpr(out, *stmt.expr, simdLocal->simdType);
        else
            emitSimdVectorCtorReg(out, simdLocal->simdType, stmt.arguments);
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
    if(stmt.expr && stmt.expr->kind == ExprKind::Binary && IsCompoundOrSimpleAssignment(stmt.expr->op) && stmt.expr->lhs && stmt.expr->rhs){
        const Expr& lhs = *stmt.expr->lhs;
        const Expr& rhs = *stmt.expr->rhs;

        if(IsRegisterPromotableSimdType(lhs.resolvedSimdType)){
            const MStringView simdType = lhs.resolvedSimdType;
            const SimdVarBinding* targetBinding = findSimdBindingForExpr(&lhs);
            const SimdVarBinding* rhsBinding = findSimdBindingForExpr(&rhs);

            const b8 shouldLowerNonBindingAssign = !targetBinding && (stmt.expr->op != TokenType::Equal || rhsBinding != nullptr || isDirectSimdRegExpr(rhs));

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

                if(stmt.expr->op == TokenType::Equal || !tryEmitSimdBinaryOpReg(out, MapSimdAssignToBinaryOp(stmt.expr->op), lhs, rhs, simdType))
                    emitSimdRegExpr(out, rhs, simdType);

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

        if(stmt.expr->op == TokenType::Equal && !lhs.resolvedUnalignedType.empty()){
            const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(lhs.resolvedUnalignedType);
            if(
                unalignedInfo
                && !unalignedInfo->isHalfFloat
                && rhs.kind == ExprKind::Call
                && rhs.lhs
                && rhs.lhs->kind == ExprKind::Identifier
                && m_symbols.resolveBaseTypeName(rhs.lhs->text) == lhs.resolvedUnalignedType
                && rhs.arguments.size() == unalignedInfo->lanes
            ){
                const Expr* srcSimdVec = nullptr;
                b8 matchesAllLanes = true;
                for(u32 i = 0u; i < unalignedInfo->lanes; ++i){
                    const Expr* laneBase = nullptr;
                    u32 laneIdx = 0u;
                    if(!TryMatchSingleLaneAccess(rhs.arguments[i], laneBase, laneIdx) || laneIdx != i){
                        matchesAllLanes = false;
                        break;
                    }
                    if(!srcSimdVec)
                        srcSimdVec = laneBase;
                    else if(!AreSamePureSimdExpr(*srcSimdVec, *laneBase)){
                        matchesAllLanes = false;
                        break;
                    }
                }
                const BuiltinSimdVectorInfo* simdVecInfo = srcSimdVec ? FindBuiltinSimdVectorType(srcSimdVec->resolvedSimdType) : nullptr;
                if(matchesAllLanes && simdVecInfo && !simdVecInfo->isMixed && simdVecInfo->xyzScalarType == unalignedInfo->laneScalarType){
                    emitIndent(out, indent);
                    out.append(simdVecInfo->storeCppFunc.data(), simdVecInfo->storeCppFunc.size());
                    out.push_back('(');
                    emitSimdRegExpr(out, *srcSimdVec, srcSimdVec->resolvedSimdType);
                    out.append(", ");
                    emitExpr(out, lhs);
                    out.append(");\r\n");
                    return true;
                }
            }
        }

        u32 lane = 0u;
        u32 vecLanes = 0u;
        const b8 isStaticLaneAssign = lhs.kind == ExprKind::MemberAccess && lhs.lhs
            && IsRegisterPromotableSimdType(lhs.lhs->resolvedSimdType)
            && IsSingleSwizzleLane(lhs.text, lane);
        const b8 isDynamicIndexAssign = lhs.kind == ExprKind::Index && lhs.lhs && lhs.rhs
            && IsRegisterPromotableSimdType(lhs.lhs->resolvedSimdType)
            && SymbolTable::IsSimdVectorType(lhs.lhs->resolvedSimdType, vecLanes);
        if(isStaticLaneAssign || isDynamicIndexAssign){
            const SimdVarBinding* targetBinding = findSimdBindingForExpr(lhs.lhs);
            if(targetBinding){
                const MStringView simdType = targetBinding->simdType;
                const b8 isDouble = IsDoubleSimdType(simdType);
                const b8 isInt = IsIntSimdType(simdType);

                emitIndent(out, indent);
                out.append(targetBinding->regName.data(), targetBinding->regName.size());
                out.append(" = ");
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleSetInt" : "::VectorSetInt");
                else
                    out.append(isDouble ? "::VectorDoubleSet" : "::VectorSet");
                if(isDynamicIndexAssign){
                    out.append("ByIndex(");
                }
                else{
                    const MStringView laneSuffix = SimdLaneCallSuffix(lane);
                    out.append(laneSuffix.data(), laneSuffix.size());
                }
                out.append(targetBinding->regName.data(), targetBinding->regName.size());
                out.append(", ");
                if(isInt)
                    out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
                if(stmt.expr->op == TokenType::Equal){
                    emitExpr(out, rhs);
                }
                else{
                    const MStringView opSym = SimdBinaryScalarOpSymbol(MapSimdAssignToBinaryOp(stmt.expr->op));
                    out.push_back('(');
                    emitExpr(out, lhs);
                    out.append(opSym.data(), opSym.size());
                    out.push_back('(');
                    emitExpr(out, rhs);
                    out.append("))");
                }
                if(isInt)
                    out.push_back(')');
                if(isDynamicIndexAssign){
                    out.append(", static_cast<usize>(");
                    emitExpr(out, *lhs.rhs);
                    out.append("));\r\n");
                }
                else{
                    out.append(");\r\n");
                }

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
    if(stmt.wrapReturnNotNull)
        return false;
    if(stmt.expr && IsRegisterPromotableSimdType(stmt.expr->resolvedSimdType)){
        const SimdVarBinding* retBinding = findSimdBindingForExpr(stmt.expr);

        const b8 needsRegStore = (retBinding && retBinding->isLocalDecl && !retBinding->needsStorageSync)
            || isDirectSimdRegExpr(*stmt.expr);
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

