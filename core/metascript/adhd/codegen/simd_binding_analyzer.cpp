// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_binding_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_analyzer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 StmtsContainSimdMath(Span<const Stmt* const> statements, const SymbolTable& symbols)noexcept;

[[nodiscard]] b8 StmtContainsSimdMath(const Stmt* stmt, const SymbolTable& symbols)noexcept{
    if(!stmt)
        return false;

    switch(stmt->kind){
    case StmtKind::Block:
    case StmtKind::ParallelBlock:
        return StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::VarDecl:{
        if(stmt->isCtorInit && stmt->varType && IsRegisterPromotableSimdType(symbols.resolveBaseTypeName(stmt->varType->name)))
            return true;
        if(ExprContainsSimdMath(stmt->expr, symbols))
            return true;
        for(const Expr* arg : stmt->arguments){
            if(ExprContainsSimdMath(arg, symbols))
                return true;
        }
        return false;
    }
    case StmtKind::ExprStmt:
    case StmtKind::Return:
    case StmtKind::ParallelCall:
        return ExprContainsSimdMath(stmt->expr, symbols);
    case StmtKind::If:
        return ExprContainsSimdMath(stmt->condition, symbols)
            || StmtsContainSimdMath(stmt->statements, symbols)
            || StmtsContainSimdMath(stmt->elseStatements, symbols);
    case StmtKind::Switch:{
        if(ExprContainsSimdMath(stmt->expr, symbols))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(ExprContainsSimdMath(sc.matchExpr, symbols) || StmtsContainSimdMath(sc.statements, symbols))
                return true;
        }
        return false;
    }
    case StmtKind::ForRange:
    case StmtKind::ParallelForRange:
        return ExprContainsSimdMath(stmt->expr, symbols) || StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::ForIndex:
    case StmtKind::ParallelForIndex:
        return StmtContainsSimdMath(stmt->initStmt, symbols)
            || ExprContainsSimdMath(stmt->condition, symbols)
            || ExprContainsSimdMath(stmt->stepExpr, symbols)
            || StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::While:
    case StmtKind::DoWhile:
        return ExprContainsSimdMath(stmt->condition, symbols) || StmtsContainSimdMath(stmt->statements, symbols);
    case StmtKind::Diagnostic:{
        for(const Expr* arg : stmt->arguments){
            if(ExprContainsSimdMath(arg, symbols))
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

[[nodiscard]] b8 StmtsContainSimdMath(const Span<const Stmt* const> statements, const SymbolTable& symbols)noexcept{
    for(const Stmt* stmt : statements){
        if(StmtContainsSimdMath(stmt, symbols))
            return true;
    }
    return false;
}

SimdVarBinding* FindOrCreateBinding(const b8 isThisMember, const MStringView name, const MStringView simdType, MetaArena& arena, MVector<SimdVarBinding>& bindings){
    for(SimdVarBinding& b : bindings){
        if(b.isThisMember == isThisMember && b.name == name)
            return &b;
    }

    bindings.emplace_back(arena);
    SimdVarBinding& created = bindings.back();
    created.isThisMember = isThisMember;
    created.name = name;
    created.simdType = simdType;
    created.regName.append(isThisMember ? "__adhd_simd_this_" : "__adhd_simd_");
    created.regName.append(name.data(), name.size());
    return &created;
}

void MarkPartialSimdBindingWrite(const Expr& targetExpr, MetaArena& arena, MVector<SimdVarBinding>& bindings){
    const b8 isThis = targetExpr.kind == ExprKind::ThisMember;
    SimdVarBinding* binding = FindOrCreateBinding(isThis, targetExpr.text, targetExpr.resolvedSimdType, arena, bindings);
    if(!binding->isLocalDecl && !binding->unconditionallyWrittenFirst)
        binding->readBeforeWrite = true;
    binding->written = true;
}

void PreRegisterLocalSimdDecls(const Span<const Stmt* const> statements, const SymbolTable& symbols, MetaArena& arena, MVector<SimdVarBinding>& bindings){
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        switch(stmt->kind){
        case StmtKind::VarDecl:{
            const MStringView varSimdType = ResolveLocalVarDeclSimdType(*stmt, symbols);
            if(!varSimdType.empty()){
                SimdVarBinding* binding = FindOrCreateBinding(false, stmt->name, varSimdType, arena, bindings);
                binding->isLocalDecl = true;
            }
            break;
        }
        case StmtKind::Block:
        case StmtKind::ParallelBlock:
            PreRegisterLocalSimdDecls(stmt->statements, symbols, arena, bindings);
            break;
        case StmtKind::If:
            PreRegisterLocalSimdDecls(stmt->statements, symbols, arena, bindings);
            PreRegisterLocalSimdDecls(stmt->elseStatements, symbols, arena, bindings);
            break;
        case StmtKind::Switch:
            for(const SwitchCase& sc : stmt->switchCases)
                PreRegisterLocalSimdDecls(sc.statements, symbols, arena, bindings);
            break;
        case StmtKind::ForRange:
        case StmtKind::ParallelForRange:{
            if(stmt->varType){
                const MStringView resolvedLoopSimd = symbols.resolveBaseTypeName(stmt->varType->name);
                if(IsRegisterPromotableSimdType(resolvedLoopSimd)){
                    SimdVarBinding* loopBinding = FindOrCreateBinding(false, stmt->name, resolvedLoopSimd, arena, bindings);
                    loopBinding->isLoopElement = true;
                }
            }
            PreRegisterLocalSimdDecls(stmt->statements, symbols, arena, bindings);
            break;
        }
        case StmtKind::ForIndex:
        case StmtKind::ParallelForIndex:
        case StmtKind::While:
        case StmtKind::DoWhile:
            PreRegisterLocalSimdDecls(stmt->statements, symbols, arena, bindings);
            break;
        default:
            break;
        }
    }
}

void CollectLocalDeclConflicts(const Span<const Stmt* const> statements, const SymbolTable& symbols, MVector<MStringView>& seenNames, MVector<MStringView>& seenSimdTypes, MVector<b8>& seenIsLoop, MVector<MStringView>& conflictedNames){
    const auto recordLocal = [&](const MStringView name, const MStringView simdType, const b8 isLoop){
        if(name.empty())
            return;
        for(usize i = 0u; i < seenNames.size(); ++i){
            if(seenNames[i] == name){
                if(!isLoop || !seenIsLoop[i] || seenSimdTypes[i] != simdType || simdType.empty()){
                    b8 alreadyAdded = false;
                    for(const MStringView existing : conflictedNames){
                        if(existing == name){
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if(!alreadyAdded)
                        conflictedNames.push_back(name);
                }
                return;
            }
        }
        seenNames.push_back(name);
        seenSimdTypes.push_back(simdType);
        seenIsLoop.push_back(isLoop);
    };

    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;
        switch(stmt->kind){
        case StmtKind::VarDecl:{
            const MStringView varSimdType = ResolveLocalVarDeclSimdType(*stmt, symbols);
            recordLocal(stmt->name, varSimdType, false);
            break;
        }
        case StmtKind::ForRange:
        case StmtKind::ParallelForRange:{
            MStringView loopSimdType;
            if(stmt->varType){
                const MStringView resolved = symbols.resolveBaseTypeName(stmt->varType->name);
                if(IsRegisterPromotableSimdType(resolved))
                    loopSimdType = resolved;
            }
            recordLocal(stmt->name, loopSimdType, true);
            CollectLocalDeclConflicts(stmt->statements, symbols, seenNames, seenSimdTypes, seenIsLoop, conflictedNames);
            break;
        }
        case StmtKind::ForIndex:
        case StmtKind::ParallelForIndex:{
            if(stmt->initStmt && stmt->initStmt->kind == StmtKind::VarDecl)
                recordLocal(stmt->initStmt->name, {}, false);
            CollectLocalDeclConflicts(stmt->statements, symbols, seenNames, seenSimdTypes, seenIsLoop, conflictedNames);
            break;
        }
        case StmtKind::Block:
        case StmtKind::ParallelBlock:
        case StmtKind::While:
        case StmtKind::DoWhile:
            CollectLocalDeclConflicts(stmt->statements, symbols, seenNames, seenSimdTypes, seenIsLoop, conflictedNames);
            break;
        case StmtKind::If:
            CollectLocalDeclConflicts(stmt->statements, symbols, seenNames, seenSimdTypes, seenIsLoop, conflictedNames);
            CollectLocalDeclConflicts(stmt->elseStatements, symbols, seenNames, seenSimdTypes, seenIsLoop, conflictedNames);
            break;
        case StmtKind::Switch:
            for(const SwitchCase& sc : stmt->switchCases)
                CollectLocalDeclConflicts(sc.statements, symbols, seenNames, seenSimdTypes, seenIsLoop, conflictedNames);
            break;
        default:
            break;
        }
    }
}

void AnalyzeSimdExprReads(const Expr* expr, const SymbolTable& symbols, const b8 requireStorage, const b8 canUnconditionallyWrite, MetaArena& arena, MVector<SimdVarBinding>& bindings){
    if(!expr)
        return;

    if((expr->kind == ExprKind::Identifier || expr->kind == ExprKind::ThisMember) && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        const b8 isThis = expr->kind == ExprKind::ThisMember;
        SimdVarBinding* binding = FindOrCreateBinding(isThis, expr->text, expr->resolvedSimdType, arena, bindings);
        if(!binding->isLocalDecl && !binding->unconditionallyWrittenFirst)
            binding->readBeforeWrite = true;
        if(requireStorage)
            binding->needsStorageSync = true;
        return;
    }

    if(expr->kind == ExprKind::Lambda)
        return;

    if(expr->kind == ExprKind::MemberAccess){
        u32 lane = 0u;
        const b8 isSimdSwizzleOrLane = !expr->isMutArg && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType) && (expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane));
        AnalyzeSimdExprReads(expr->lhs, symbols, !isSimdSwizzleOrLane, false, arena, bindings);
        return;
    }

    if(expr->kind == ExprKind::Index){
        u32 vecLanes = 0u;
        const b8 isSimdVecIndex = !expr->isMutArg && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType) && SymbolTable::IsSimdVectorType(expr->lhs->resolvedSimdType, vecLanes);
        AnalyzeSimdExprReads(expr->lhs, symbols, !isSimdVecIndex, false, arena, bindings);
        AnalyzeSimdExprReads(expr->rhs, symbols, false, false, arena, bindings);
        return;
    }

    if(expr->kind == ExprKind::Call){
        b8 isSimdIntrinsicOrCtor = false;
        if(expr->lhs && expr->lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr->lhs->text;
            const MStringView baseCallee = symbols.resolveBaseTypeName(callee);
            if(SymbolTable::IsSimdType(baseCallee) || !MapBuiltinMathFunction(callee).empty())
                isSimdIntrinsicOrCtor = true;
        }
        if(!isSimdIntrinsicOrCtor)
            AnalyzeSimdExprReads(expr->lhs, symbols, true, false, arena, bindings);
        for(const Expr* arg : expr->arguments)
            AnalyzeSimdExprReads(arg, symbols, !isSimdIntrinsicOrCtor, false, arena, bindings);
        return;
    }

    if(expr->kind == ExprKind::Binary && IsCompoundOrSimpleAssignment(expr->op) && expr->lhs){
        if(IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
            AnalyzeSimdExprReads(expr->rhs, symbols, false, false, arena, bindings);
            if(expr->lhs->kind == ExprKind::Identifier || expr->lhs->kind == ExprKind::ThisMember){
                const b8 isThis = expr->lhs->kind == ExprKind::ThisMember;
                SimdVarBinding* binding = FindOrCreateBinding(isThis, expr->lhs->text, expr->lhs->resolvedSimdType, arena, bindings);
                if(!binding->isLocalDecl){
                    if(expr->op == TokenType::Equal && canUnconditionallyWrite && !binding->isLoopElement && !binding->readBeforeWrite && !binding->written)
                        binding->unconditionallyWrittenFirst = true;
                    else if(!binding->unconditionallyWrittenFirst)
                        binding->readBeforeWrite = true;
                }
                binding->written = true;
            }
            else{
                AnalyzeSimdExprReads(expr->lhs, symbols, true, false, arena, bindings);
            }
            return;
        }

        u32 lane = 0u;
        if(
            expr->lhs->kind == ExprKind::MemberAccess
            && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr->lhs->text, lane)
            && (expr->lhs->lhs->kind == ExprKind::Identifier || expr->lhs->lhs->kind == ExprKind::ThisMember)
        ){
            AnalyzeSimdExprReads(expr->rhs, symbols, false, false, arena, bindings);
            MarkPartialSimdBindingWrite(*expr->lhs->lhs, arena, bindings);
            return;
        }

        u32 vecLanes = 0u;
        if(
            expr->lhs->kind == ExprKind::Index
            && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && SymbolTable::IsSimdVectorType(expr->lhs->lhs->resolvedSimdType, vecLanes)
            && (expr->lhs->lhs->kind == ExprKind::Identifier || expr->lhs->lhs->kind == ExprKind::ThisMember)
        ){
            AnalyzeSimdExprReads(expr->lhs->rhs, symbols, false, false, arena, bindings);
            AnalyzeSimdExprReads(expr->rhs, symbols, false, false, arena, bindings);
            MarkPartialSimdBindingWrite(*expr->lhs->lhs, arena, bindings);
            return;
        }
    }

    if(expr->kind == ExprKind::Cast || expr->kind == ExprKind::BitCast){
        AnalyzeSimdExprReads(expr->lhs, symbols, true, false, arena, bindings);
        return;
    }

    if(expr->kind == ExprKind::Binary && !IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        const b8 lhsIsSimd = expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType);
        const b8 rhsIsSimd = expr->rhs && IsRegisterPromotableSimdType(expr->rhs->resolvedSimdType);
        AnalyzeSimdExprReads(expr->lhs, symbols, lhsIsSimd, false, arena, bindings);
        AnalyzeSimdExprReads(expr->rhs, symbols, rhsIsSimd, false, arena, bindings);
        return;
    }

    AnalyzeSimdExprReads(expr->lhs, symbols, false, false, arena, bindings);
    AnalyzeSimdExprReads(expr->rhs, symbols, false, false, arena, bindings);
}

void ScanStmtsForSimdBindings(const Span<const Stmt* const> statements, const SymbolTable& symbols, const b8 inConditionalOrLoop, MetaArena& arena, MVector<SimdVarBinding>& bindings, b8& ioHasSeenPossibleReturn){
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        const b8 canUnconditionallyWrite = !inConditionalOrLoop && !ioHasSeenPossibleReturn;

        switch(stmt->kind){
        case StmtKind::VarDecl:{
            AnalyzeSimdExprReads(stmt->expr, symbols, false, false, arena, bindings);
            for(const Expr* arg : stmt->arguments)
                AnalyzeSimdExprReads(arg, symbols, false, false, arena, bindings);
            break;
        }
        case StmtKind::ExprStmt:{
            AnalyzeSimdExprReads(stmt->expr, symbols, false, canUnconditionallyWrite, arena, bindings);
            break;
        }
        case StmtKind::Return:{
            AnalyzeSimdExprReads(stmt->expr, symbols, stmt->wrapReturnNotNull, false, arena, bindings);
            ioHasSeenPossibleReturn = true;
            break;
        }
        case StmtKind::Block:{
            ScanStmtsForSimdBindings(stmt->statements, symbols, inConditionalOrLoop, arena, bindings, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::If:{
            AnalyzeSimdExprReads(stmt->condition, symbols, false, false, arena, bindings);
            ScanStmtsForSimdBindings(stmt->statements, symbols, true, arena, bindings, ioHasSeenPossibleReturn);
            ScanStmtsForSimdBindings(stmt->elseStatements, symbols, true, arena, bindings, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::Switch:{
            AnalyzeSimdExprReads(stmt->expr, symbols, false, false, arena, bindings);
            for(const SwitchCase& sc : stmt->switchCases){
                AnalyzeSimdExprReads(sc.matchExpr, symbols, false, false, arena, bindings);
                ScanStmtsForSimdBindings(sc.statements, symbols, true, arena, bindings, ioHasSeenPossibleReturn);
            }
            break;
        }
        case StmtKind::ForRange:
        case StmtKind::ParallelForRange:{
            AnalyzeSimdExprReads(stmt->expr, symbols, true, false, arena, bindings);
            ScanStmtsForSimdBindings(stmt->statements, symbols, true, arena, bindings, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ForIndex:
        case StmtKind::ParallelForIndex:{
            if(stmt->initStmt){
                AnalyzeSimdExprReads(stmt->initStmt->expr, symbols, false, false, arena, bindings);
                for(const Expr* arg : stmt->initStmt->arguments)
                    AnalyzeSimdExprReads(arg, symbols, false, false, arena, bindings);
            }
            AnalyzeSimdExprReads(stmt->condition, symbols, false, false, arena, bindings);
            AnalyzeSimdExprReads(stmt->stepExpr, symbols, false, false, arena, bindings);
            ScanStmtsForSimdBindings(stmt->statements, symbols, true, arena, bindings, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::While:
        case StmtKind::DoWhile:{
            AnalyzeSimdExprReads(stmt->condition, symbols, false, false, arena, bindings);
            ScanStmtsForSimdBindings(stmt->statements, symbols, true, arena, bindings, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelBlock:{
            ScanStmtsForSimdBindings(stmt->statements, symbols, true, arena, bindings, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelCall:{
            AnalyzeSimdExprReads(stmt->expr, symbols, true, false, arena, bindings);
            if(stmt->expr && stmt->expr->kind == ExprKind::Call){
                for(const Expr* arg : stmt->expr->arguments){
                    if(!arg || !arg->isMutArg)
                        continue;
                    if((arg->kind == ExprKind::Identifier || arg->kind == ExprKind::ThisMember) && IsRegisterPromotableSimdType(arg->resolvedSimdType)){
                        SimdVarBinding* b = FindOrCreateBinding(arg->kind == ExprKind::ThisMember, arg->text, arg->resolvedSimdType, arena, bindings);
                        b->needsStorageSync = true;
                        b->capturedByMutLambda = true;
                    }
                }
            }
            break;
        }
        case StmtKind::NativeBlock:{
            for(const NativeBinding& nb : stmt->nativeBindings)
                AnalyzeSimdExprReads(nb.sourceExpr, symbols, true, false, arena, bindings);
            break;
        }
        case StmtKind::Diagnostic:{
            for(const Expr* arg : stmt->arguments)
                AnalyzeSimdExprReads(arg, symbols, true, false, arena, bindings);
            break;
        }
        default:
            break;
        }
    }
}

void MarkLambdaCapturedSimdBindingsInExpr(MVector<SimdVarBinding>& bindings, const Expr* expr)noexcept{
    if(!expr)
        return;
    if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& cap : expr->lambdaCaptures){
            for(SimdVarBinding& b : bindings){
                if((cap.name == "this" && b.isThisMember) || (!b.isThisMember && b.name == cap.name)){
                    b.needsStorageSync = true;
                    if(cap.mode == ParamPassingMode::Mut)
                        b.capturedByMutLambda = true;
                    if(!b.isLocalDecl && !b.unconditionallyWrittenFirst)
                        b.readBeforeWrite = true;
                }
            }
        }
    }
    else if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::ThisMember){
        for(SimdVarBinding& b : bindings){
            if(b.isThisMember){
                b.needsStorageSync = true;
                b.capturedByMutLambda = true;
                if(!b.unconditionallyWrittenFirst)
                    b.readBeforeWrite = true;
            }
        }
    }
    MarkLambdaCapturedSimdBindingsInExpr(bindings, expr->lhs);
    MarkLambdaCapturedSimdBindingsInExpr(bindings, expr->rhs);
    for(const Expr* arg : expr->arguments)
        MarkLambdaCapturedSimdBindingsInExpr(bindings, arg);
}

void MarkLambdaCapturedSimdBindingsInStmts(MVector<SimdVarBinding>& bindings, const Span<const Stmt* const> statements)noexcept{
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;
        if(stmt->initStmt)
            MarkLambdaCapturedSimdBindingsInExpr(bindings, stmt->initStmt->expr);
        MarkLambdaCapturedSimdBindingsInExpr(bindings, stmt->expr);
        MarkLambdaCapturedSimdBindingsInExpr(bindings, stmt->condition);
        MarkLambdaCapturedSimdBindingsInExpr(bindings, stmt->stepExpr);
        for(const Expr* arg : stmt->arguments)
            MarkLambdaCapturedSimdBindingsInExpr(bindings, arg);
        for(const NativeBinding& nb : stmt->nativeBindings)
            MarkLambdaCapturedSimdBindingsInExpr(bindings, nb.sourceExpr);
        MarkLambdaCapturedSimdBindingsInStmts(bindings, stmt->statements);
        MarkLambdaCapturedSimdBindingsInStmts(bindings, stmt->elseStatements);
        for(const SwitchCase& sc : stmt->switchCases){
            MarkLambdaCapturedSimdBindingsInExpr(bindings, sc.matchExpr);
            MarkLambdaCapturedSimdBindingsInStmts(bindings, sc.statements);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CollectScopedSimdBindings(const Span<const Stmt* const> statements, const SymbolTable& symbols, MetaArena& arena, MVector<SimdVarBinding>& bindings){
    using namespace __hidden_adhd_simd_analyzer;

    if(!StmtsContainSimdMath(statements, symbols))
        return;

    PreRegisterLocalSimdDecls(statements, symbols, arena, bindings);
    b8 hasSeenPossibleReturn = false;
    ScanStmtsForSimdBindings(statements, symbols, false, arena, bindings, hasSeenPossibleReturn);
    MarkLambdaCapturedSimdBindingsInStmts(bindings, statements);

    MVector<MStringView> seenNames(arena);
    MVector<MStringView> seenSimdTypes(arena);
    MVector<b8> seenIsLoop(arena);
    MVector<MStringView> conflictedNames(arena);
    CollectLocalDeclConflicts(statements, symbols, seenNames, seenSimdTypes, seenIsLoop, conflictedNames);
    if(!conflictedNames.empty()){
        usize writeIdx = 0u;
        for(usize readIdx = 0u; readIdx < bindings.size(); ++readIdx){
            b8 isConflicted = false;
            if(!bindings[readIdx].isThisMember){
                for(const MStringView cname : conflictedNames){
                    if(bindings[readIdx].name == cname){
                        isConflicted = true;
                        break;
                    }
                }
            }
            if(!isConflicted){
                if(writeIdx != readIdx)
                    bindings[writeIdx] = bindings[readIdx];
                ++writeIdx;
            }
        }
        while(bindings.size() > writeIdx)
            bindings.pop_back();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

