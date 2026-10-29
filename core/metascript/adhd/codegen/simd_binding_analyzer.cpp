// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_binding_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_analyzer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 StmtsContainSimdMath(const MVector<Stmt*>& statements, const SymbolTable& symbols)noexcept;

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

[[nodiscard]] b8 StmtsContainSimdMath(const MVector<Stmt*>& statements, const SymbolTable& symbols)noexcept{
    for(const Stmt* stmt : statements){
        if(StmtContainsSimdMath(stmt, symbols))
            return true;
    }
    return false;
}

SimdVarBinding* FindOrCreateBinding(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const b8 isThisMember,
    const MStringView name,
    const MStringView simdType
){
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

void MarkPartialSimdBindingWrite(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const Expr& targetExpr
){
    const b8 isThis = targetExpr.kind == ExprKind::ThisMember;
    SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, isThis, targetExpr.text, targetExpr.resolvedSimdType);
    if(!binding->isLocalDecl && !binding->unconditionallyWrittenFirst)
        binding->readBeforeWrite = true;
    binding->written = true;
}

void PreRegisterLocalSimdDecls(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const MVector<Stmt*>& statements,
    const SymbolTable& symbols
){
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        switch(stmt->kind){
        case StmtKind::VarDecl:{
            if(stmt->usesScratchArena || (stmt->varType && stmt->varType->refMode != ParamPassingMode::None))
                break;

            MStringView varSimdType;
            if(stmt->varType && IsRegisterPromotableSimdType(symbols.resolveBaseTypeName(stmt->varType->name)))
                varSimdType = symbols.resolveBaseTypeName(stmt->varType->name);
            else if(stmt->expr && IsRegisterPromotableSimdType(stmt->expr->resolvedSimdType))
                varSimdType = stmt->expr->resolvedSimdType;

            if(!varSimdType.empty()){
                SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, false, stmt->name, varSimdType);
                binding->isLocalDecl = true;
            }
            break;
        }
        case StmtKind::Block:
        case StmtKind::ParallelBlock:
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            break;
        case StmtKind::If:
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            PreRegisterLocalSimdDecls(arena, bindings, stmt->elseStatements, symbols);
            break;
        case StmtKind::Switch:
            for(const SwitchCase& sc : stmt->switchCases)
                PreRegisterLocalSimdDecls(arena, bindings, sc.statements, symbols);
            break;
        case StmtKind::ForRange:
        case StmtKind::ParallelForRange:{
            if(stmt->varType){
                const MStringView resolvedLoopSimd = symbols.resolveBaseTypeName(stmt->varType->name);
                if(IsRegisterPromotableSimdType(resolvedLoopSimd)){
                    SimdVarBinding* loopBinding = FindOrCreateBinding(arena, bindings, false, stmt->name, resolvedLoopSimd);
                    loopBinding->isLoopElement = true;
                }
            }
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            break;
        }
        case StmtKind::ForIndex:
        case StmtKind::ParallelForIndex:
        case StmtKind::While:
        case StmtKind::DoWhile:
            PreRegisterLocalSimdDecls(arena, bindings, stmt->statements, symbols);
            break;
        default:
            break;
        }
    }
}

void AnalyzeSimdExprReads(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const Expr* expr,
    const SymbolTable& symbols,
    const b8 requireStorage,
    const b8 canUnconditionallyWrite
){
    if(!expr)
        return;

    if((expr->kind == ExprKind::Identifier || expr->kind == ExprKind::ThisMember) && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        const b8 isThis = expr->kind == ExprKind::ThisMember;
        SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, isThis, expr->text, expr->resolvedSimdType);
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
        const b8 isSimdSwizzleOrLane = !expr->isMutArg
            && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)
            && (expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane));
        AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, !isSimdSwizzleOrLane, false);
        return;
    }

    if(expr->kind == ExprKind::Index){
        u32 vecLanes = 0u;
        const b8 isSimdVecIndex = !expr->isMutArg
            && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)
            && symbols.isSimdVectorType(expr->lhs->resolvedSimdType, vecLanes);
        AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, !isSimdVecIndex, false);
        AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
        return;
    }

    if(expr->kind == ExprKind::Call){
        b8 isSimdIntrinsicOrCtor = false;
        if(expr->lhs && expr->lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr->lhs->text;
            const MStringView baseCallee = symbols.resolveBaseTypeName(callee);
            if(symbols.isSimdType(baseCallee) || !MapBuiltinMathFunction(callee).empty())
                isSimdIntrinsicOrCtor = true;
        }
        if(!isSimdIntrinsicOrCtor)
            AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, true, false);
        for(const Expr* arg : expr->arguments)
            AnalyzeSimdExprReads(arena, bindings, arg, symbols, !isSimdIntrinsicOrCtor, false);
        return;
    }

    if(expr->kind == ExprKind::Binary && IsSimdAssignmentOp(expr->op) && expr->lhs){
        if(IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
            AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
            if(expr->lhs->kind == ExprKind::Identifier || expr->lhs->kind == ExprKind::ThisMember){
                const b8 isThis = expr->lhs->kind == ExprKind::ThisMember;
                SimdVarBinding* binding = FindOrCreateBinding(arena, bindings, isThis, expr->lhs->text, expr->lhs->resolvedSimdType);
                if(!binding->isLocalDecl){
                    if(expr->op == TokenType::Equal && canUnconditionallyWrite && !binding->isLoopElement && !binding->readBeforeWrite && !binding->written)
                        binding->unconditionallyWrittenFirst = true;
                    else if(!binding->unconditionallyWrittenFirst)
                        binding->readBeforeWrite = true;
                }
                binding->written = true;
            }
            else{
                AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, true, false);
            }
            return;
        }

        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr->lhs->text, lane)
            && (expr->lhs->lhs->kind == ExprKind::Identifier || expr->lhs->lhs->kind == ExprKind::ThisMember))
        {
            AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
            MarkPartialSimdBindingWrite(arena, bindings, *expr->lhs->lhs);
            return;
        }

        u32 vecLanes = 0u;
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && symbols.isSimdVectorType(expr->lhs->lhs->resolvedSimdType, vecLanes)
            && (expr->lhs->lhs->kind == ExprKind::Identifier || expr->lhs->lhs->kind == ExprKind::ThisMember))
        {
            AnalyzeSimdExprReads(arena, bindings, expr->lhs->rhs, symbols, false, false);
            AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
            MarkPartialSimdBindingWrite(arena, bindings, *expr->lhs->lhs);
            return;
        }
    }

    if(expr->kind == ExprKind::Cast || expr->kind == ExprKind::BitCast){
        AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, true, false);
        return;
    }

    if(expr->kind == ExprKind::Binary && !IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        const b8 lhsIsSimd = expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType);
        const b8 rhsIsSimd = expr->rhs && IsRegisterPromotableSimdType(expr->rhs->resolvedSimdType);
        AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, lhsIsSimd, false);
        AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, rhsIsSimd, false);
        return;
    }

    AnalyzeSimdExprReads(arena, bindings, expr->lhs, symbols, false, false);
    AnalyzeSimdExprReads(arena, bindings, expr->rhs, symbols, false, false);
}

void ScanStmtsForSimdBindings(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const MVector<Stmt*>& statements,
    const SymbolTable& symbols,
    const b8 inConditionalOrLoop,
    b8& ioHasSeenPossibleReturn
){
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        const b8 canUnconditionallyWrite = !inConditionalOrLoop && !ioHasSeenPossibleReturn;

        switch(stmt->kind){
        case StmtKind::VarDecl:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, false, false);
            for(const Expr* arg : stmt->arguments)
                AnalyzeSimdExprReads(arena, bindings, arg, symbols, false, false);
            break;
        }
        case StmtKind::ExprStmt:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, false, canUnconditionallyWrite);
            break;
        }
        case StmtKind::Return:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, stmt->wrapReturnNotNull, false);
            ioHasSeenPossibleReturn = true;
            break;
        }
        case StmtKind::Block:{
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, inConditionalOrLoop, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::If:{
            AnalyzeSimdExprReads(arena, bindings, stmt->condition, symbols, false, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            ScanStmtsForSimdBindings(arena, bindings, stmt->elseStatements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::Switch:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, false, false);
            for(const SwitchCase& sc : stmt->switchCases){
                AnalyzeSimdExprReads(arena, bindings, sc.matchExpr, symbols, false, false);
                ScanStmtsForSimdBindings(arena, bindings, sc.statements, symbols, true, ioHasSeenPossibleReturn);
            }
            break;
        }
        case StmtKind::ForRange:
        case StmtKind::ParallelForRange:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, true, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ForIndex:
        case StmtKind::ParallelForIndex:{
            if(stmt->initStmt){
                AnalyzeSimdExprReads(arena, bindings, stmt->initStmt->expr, symbols, false, false);
                for(const Expr* arg : stmt->initStmt->arguments)
                    AnalyzeSimdExprReads(arena, bindings, arg, symbols, false, false);
            }
            AnalyzeSimdExprReads(arena, bindings, stmt->condition, symbols, false, false);
            AnalyzeSimdExprReads(arena, bindings, stmt->stepExpr, symbols, false, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::While:
        case StmtKind::DoWhile:{
            AnalyzeSimdExprReads(arena, bindings, stmt->condition, symbols, false, false);
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelBlock:{
            ScanStmtsForSimdBindings(arena, bindings, stmt->statements, symbols, true, ioHasSeenPossibleReturn);
            break;
        }
        case StmtKind::ParallelCall:{
            AnalyzeSimdExprReads(arena, bindings, stmt->expr, symbols, true, false);
            if(stmt->expr && stmt->expr->kind == ExprKind::Call){
                for(const Expr* arg : stmt->expr->arguments){
                    if(!arg || !arg->isMutArg)
                        continue;
                    if((arg->kind == ExprKind::Identifier || arg->kind == ExprKind::ThisMember) && IsRegisterPromotableSimdType(arg->resolvedSimdType)){
                        SimdVarBinding* b = FindOrCreateBinding(arena, bindings, arg->kind == ExprKind::ThisMember, arg->text, arg->resolvedSimdType);
                        b->needsStorageSync = true;
                        b->capturedByMutLambda = true;
                    }
                }
            }
            break;
        }
        case StmtKind::NativeBlock:{
            for(const NativeBinding& nb : stmt->nativeBindings)
                AnalyzeSimdExprReads(arena, bindings, nb.sourceExpr, symbols, true, false);
            break;
        }
        case StmtKind::Diagnostic:{
            for(const Expr* arg : stmt->arguments)
                AnalyzeSimdExprReads(arena, bindings, arg, symbols, true, false);
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

void MarkLambdaCapturedSimdBindingsInStmts(MVector<SimdVarBinding>& bindings, const MVector<Stmt*>& statements)noexcept{
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


void CollectScopedSimdBindings(
    MetaArena& arena,
    MVector<SimdVarBinding>& bindings,
    const MVector<Stmt*>& statements,
    const SymbolTable& symbols
){
    using namespace __hidden_adhd_simd_analyzer;

    if(!StmtsContainSimdMath(statements, symbols))
        return;

    PreRegisterLocalSimdDecls(arena, bindings, statements, symbols);
    b8 hasSeenPossibleReturn = false;
    ScanStmtsForSimdBindings(arena, bindings, statements, symbols, false, hasSeenPossibleReturn);
    MarkLambdaCapturedSimdBindingsInStmts(bindings, statements);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
