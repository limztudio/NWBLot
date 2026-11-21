// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "last_use_move_analyzer.h"
#include "type_checker.h"
#include "type_layout_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_last_use_move_analyzer{


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

[[nodiscard]] b8 ExprMayRetainAlias(const Expr* expr, const MStringView alias)noexcept{
    if(!expr)
        return false;
    if((expr->isRetainedArg || expr->wrapInNotNull || expr->kind == ExprKind::Lambda) && ExprReferencesIdentifier(expr, alias))
        return true;
    if(expr->isNotNullRebind && ExprReferencesIdentifier(expr->rhs, alias))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprMayRetainAlias(arg, alias))
            return true;
    }
    return ExprMayRetainAlias(expr->lhs, alias) || ExprMayRetainAlias(expr->rhs, alias);
}

[[nodiscard]] b8 StmtMayRetainAlias(const Stmt* stmt, const MStringView alias, const SymbolTable& symbols)noexcept{
    if(!stmt)
        return false;
    if((stmt->kind == StmtKind::ParallelBlock || stmt->kind == StmtKind::ParallelCall)
        && StmtReferencesIdentifier(stmt, alias))
        return true;
    if(stmt->kind == StmtKind::VarDecl && stmt->name != alias && stmt->varType
        && (symbols.resolveRefMode(stmt->varType) != ParamPassingMode::None || IsBorrowViewType(symbols.resolveBaseTypeName(stmt->varType->name)))
        && StmtReferencesIdentifier(stmt, alias))
        return true;
    if(ExprMayRetainAlias(stmt->expr, alias) || ExprMayRetainAlias(stmt->condition, alias) || ExprMayRetainAlias(stmt->stepExpr, alias)
        || StmtMayRetainAlias(stmt->initStmt, alias, symbols))
        return true;
    for(const Expr* arg : stmt->arguments){
        if(ExprMayRetainAlias(arg, alias))
            return true;
    }
    for(const Stmt* child : stmt->statements){
        if(StmtMayRetainAlias(child, alias, symbols))
            return true;
        if(stmt->kind == StmtKind::ForRange && ExprReferencesIdentifier(stmt->expr, alias) && StmtMayRetainAlias(child, stmt->name, symbols))
            return true;
    }
    for(const Stmt* child : stmt->elseStatements){
        if(StmtMayRetainAlias(child, alias, symbols))
            return true;
    }
    for(const SwitchCase& sc : stmt->switchCases){
        for(const Stmt* child : sc.statements){
            if(StmtMayRetainAlias(child, alias, symbols))
                return true;
        }
    }
    return false;
}

void ScanExprForMoveBatch(
    const Expr* const expr,
    const Span<const MStringView> candidateNames,
    const u64 activeMask,
    const SymbolTable& symbols,
    u64& inOutRefMask,
    u64& inOutPreventMask
)noexcept;

void ScanStmtForMoveBatch(
    const Stmt* const stmt,
    const Span<const MStringView> candidateNames,
    const u64 activeMask,
    const SymbolTable& symbols,
    const Stmt* const endedScope,
    u64& inOutRefMask,
    u64& inOutPreventMask
)noexcept{
    if(!stmt || activeMask == 0ull)
        return;

    u64 localRefMask = 0ull;
    const Stmt* const nextEndedScope = endedScope ? endedScope : stmt;
    if(stmt->initStmt)
        ScanStmtForMoveBatch(stmt->initStmt, candidateNames, activeMask, symbols, nextEndedScope, localRefMask, inOutPreventMask);

    ScanExprForMoveBatch(stmt->expr, candidateNames, activeMask, symbols, localRefMask, inOutPreventMask);
    ScanExprForMoveBatch(stmt->condition, candidateNames, activeMask, symbols, localRefMask, inOutPreventMask);
    ScanExprForMoveBatch(stmt->stepExpr, candidateNames, activeMask, symbols, localRefMask, inOutPreventMask);
    for(const Expr* arg : stmt->arguments)
        ScanExprForMoveBatch(arg, candidateNames, activeMask, symbols, localRefMask, inOutPreventMask);
    for(const Stmt* child : stmt->statements)
        ScanStmtForMoveBatch(child, candidateNames, activeMask, symbols, nextEndedScope, localRefMask, inOutPreventMask);
    for(const Stmt* child : stmt->elseStatements)
        ScanStmtForMoveBatch(child, candidateNames, activeMask, symbols, nextEndedScope, localRefMask, inOutPreventMask);
    for(const SwitchCase& sc : stmt->switchCases){
        ScanExprForMoveBatch(sc.matchExpr, candidateNames, activeMask, symbols, localRefMask, inOutPreventMask);
        for(const Stmt* child : sc.statements)
            ScanStmtForMoveBatch(child, candidateNames, activeMask, symbols, nextEndedScope, localRefMask, inOutPreventMask);
    }

    inOutRefMask |= localRefMask;
    const u64 relevantRefs = localRefMask & activeMask;
    if(relevantRefs == 0ull)
        return;

    if(stmt->kind == StmtKind::ParallelBlock || stmt->kind == StmtKind::ParallelCall){
        inOutPreventMask |= relevantRefs;
        return;
    }

    if(stmt->kind == StmtKind::VarDecl && stmt->varType){
        const MStringView baseVarType = symbols.resolveBaseTypeName(stmt->varType->name);
        if(symbols.resolveRefMode(stmt->varType) != ParamPassingMode::None || IsBorrowViewType(baseVarType)){
            if(!endedScope || StmtMayRetainAlias(endedScope, stmt->name, symbols)){
                for(usize k = 0u; k < candidateNames.size(); ++k){
                    const u64 bit = 1ull << k;
                    if((relevantRefs & bit) != 0ull && stmt->name != candidateNames[k])
                        inOutPreventMask |= bit;
                }
            }
        }
    }
}

void ScanExprForMoveBatch(
    const Expr* const expr,
    const Span<const MStringView> candidateNames,
    const u64 activeMask,
    const SymbolTable& symbols,
    u64& inOutRefMask,
    u64& inOutPreventMask
)noexcept{
    if(!expr || activeMask == 0ull)
        return;

    if(expr->kind == ExprKind::Identifier){
        for(usize k = 0u; k < candidateNames.size(); ++k){
            const u64 bit = 1ull << k;
            if((activeMask & bit) != 0ull && expr->text == candidateNames[k])
                inOutRefMask |= bit;
        }
    }
    else if(expr->kind == ExprKind::ScopeAccess && !expr->scopeChain.empty()){
        for(usize k = 0u; k < candidateNames.size(); ++k){
            const u64 bit = 1ull << k;
            if((activeMask & bit) != 0ull && expr->scopeChain[0] == candidateNames[k])
                inOutRefMask |= bit;
        }
    }
    else if(expr->kind == ExprKind::Lambda){
        for(const LambdaCapture& cap : expr->lambdaCaptures){
            for(usize k = 0u; k < candidateNames.size(); ++k){
                const u64 bit = 1ull << k;
                if((activeMask & bit) != 0ull && cap.name == candidateNames[k]){
                    inOutRefMask |= bit;
                    if(cap.mode == ParamPassingMode::Mut)
                        inOutPreventMask |= bit;
                }
            }
        }
        for(const Stmt* child : expr->lambdaBody)
            ScanStmtForMoveBatch(child, candidateNames, activeMask, symbols, nullptr, inOutRefMask, inOutPreventMask);
    }

    ScanExprForMoveBatch(expr->lhs, candidateNames, activeMask, symbols, inOutRefMask, inOutPreventMask);
    ScanExprForMoveBatch(expr->rhs, candidateNames, activeMask, symbols, inOutRefMask, inOutPreventMask);
    for(const Expr* arg : expr->arguments)
        ScanExprForMoveBatch(arg, candidateNames, activeMask, symbols, inOutRefMask, inOutPreventMask);
}

struct MoveBatchCandidate{
    const ScopedVarInfo* local = nullptr;
    MStringView baseType;
    usize lastUseIndex = 0u;
    b8 prevented = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LastUseMoveAnalyzer::analyze(const Span<Stmt* const> statements, const Span<const ScopedVarInfo> scope, const usize savedScopeSize)const noexcept{
    constexpr usize s_MaxBatchSize = 64u;
    Array<__hidden_last_use_move_analyzer::MoveBatchCandidate, s_MaxBatchSize> batch = {};
    Array<MStringView, s_MaxBatchSize> batchNames = {};
    usize batchCount = 0u;

    const auto flushBatch = [&]()noexcept{
        if(batchCount == 0u)
            return;

        u64 activeMask = (batchCount == s_MaxBatchSize) ? ~0ull : ((1ull << batchCount) - 1ull);
        for(usize k = 0u; k < batchCount; ++k)
            batch[k].lastUseIndex = statements.size();

        for(usize s = 0u; s < statements.size() && activeMask != 0ull; ++s){
            u64 refMask = 0ull;
            u64 preventMask = 0ull;
            __hidden_last_use_move_analyzer::ScanStmtForMoveBatch(
                statements[s],
                Span<const MStringView>(batchNames.data(), batchCount),
                activeMask,
                m_symbols,
                nullptr,
                refMask,
                preventMask
            );
            if(preventMask != 0ull){
                for(usize k = 0u; k < batchCount; ++k){
                    const u64 bit = 1ull << k;
                    if((preventMask & bit) != 0ull){
                        batch[k].prevented = true;
                        activeMask &= ~bit;
                    }
                }
            }
            u64 hitActive = refMask & activeMask;
            for(usize k = 0u; hitActive != 0ull && k < batchCount; ++k){
                const u64 bit = 1ull << k;
                if((hitActive & bit) != 0ull){
                    batch[k].lastUseIndex = s;
                    hitActive &= ~bit;
                }
            }
        }

        for(usize k = 0u; k < batchCount; ++k){
            const __hidden_last_use_move_analyzer::MoveBatchCandidate& cand = batch[k];
            if(cand.prevented || cand.lastUseIndex >= statements.size())
                continue;

            const ScopedVarInfo& local = *cand.local;
            const MStringView localBaseType = cand.baseType;
            Stmt* lastStmt = statements[cand.lastUseIndex];
            auto tryMarkLastUseValueOrIdent = [&](Expr* candidate)noexcept{
                if(!candidate || candidate->isMutArg || candidate->wrapInNotNull)
                    return;
                if(candidate->kind == ExprKind::Identifier && candidate->text == local.name){
                    candidate->moveAtLastUse = true;
                }
                else if(
                    candidate->kind == ExprKind::Call
                    && candidate->arguments.empty()
                    && candidate->lhs
                    && candidate->lhs->kind == ExprKind::MemberAccess
                    && !candidate->lhs->isPointerMemberAccess
                    && FindSpecialMethodSemantics(candidate->lhs->text) == SpecialMethodKind::UnwrapValue
                    && candidate->lhs->lhs
                    && candidate->lhs->lhs->kind == ExprKind::Identifier
                    && candidate->lhs->lhs->text == local.name
                ){
                    candidate->lhs->lhs->moveAtLastUse = true;
                }
            };

            if(
                lastStmt->kind == StmtKind::ExprStmt
                && lastStmt->expr
                && lastStmt->expr->kind == ExprKind::Binary
                && lastStmt->expr->op == TokenType::Equal
                && !lastStmt->expr->isNotNullRebind
                && !__hidden_last_use_move_analyzer::ExprReferencesIdentifier(lastStmt->expr->lhs, local.name)
            ){
                tryMarkLastUseValueOrIdent(lastStmt->expr->rhs);
                continue;
            }

            const b8 localUsesMotherArena = (local.type && TypeLayoutAnalyzer(m_symbols).requiresMotherArena(*local.type))
                || (!m_symbols.hasTypeSymbol(localBaseType) && SymbolTable::IsArenaBackedContainer(localBaseType));
            if(localUsesMotherArena)
                continue;

            const b8 isDirectCallStmt = (lastStmt->kind == StmtKind::ExprStmt && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call)
                || (lastStmt->kind == StmtKind::Return && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call)
                || (lastStmt->kind == StmtKind::VarDecl && lastStmt->name != local.name && lastStmt->expr && lastStmt->expr->kind == ExprKind::Call);
            if(!isDirectCallStmt || __hidden_last_use_move_analyzer::ExprReferencesIdentifier(lastStmt->expr->lhs, local.name))
                continue;

            usize argRefCount = 0u;
            for(const Expr* arg : lastStmt->expr->arguments){
                if(__hidden_last_use_move_analyzer::ExprReferencesIdentifier(arg, local.name))
                    ++argRefCount;
            }
            if(argRefCount == 1u){
                for(Expr* arg : lastStmt->expr->arguments)
                    tryMarkLastUseValueOrIdent(arg);
            }
        }

        batchCount = 0u;
    };

    for(usize i = savedScopeSize; i < scope.size(); ++i){
        const ScopedVarInfo& local = scope[i];
        if(local.isParam || local.isConst || (local.type && m_symbols.resolveRefMode(local.type) != ParamPassingMode::None))
            continue;

        const MStringView localBaseType = ResolveScopedVarBaseType(m_symbols, local);
        if(SymbolTable::IsSimdType(localBaseType) || m_symbols.isSmallValueType(localBaseType))
            continue;

        b8 capturedOrAliased = false;
        for(usize j = 0u; j < scope.size(); ++j){
            if((scope[j].borrowsLocalStringOwner && scope[j].borrowedLocalOwnerName == local.name) || scope[j].referenceOwnerName == local.name){
                capturedOrAliased = true;
                break;
            }
            if(j >= savedScopeSize){
                if(const Expr* lam = scope[j].lambdaExpr){
                    for(const LambdaCapture& cap : lam->lambdaCaptures){
                        if(cap.name == local.name){
                            capturedOrAliased = true;
                            break;
                        }
                    }
                }
            }
            if(capturedOrAliased)
                break;
        }
        if(capturedOrAliased)
            continue;

        batch[batchCount] = __hidden_last_use_move_analyzer::MoveBatchCandidate{ &local, localBaseType, statements.size(), false };
        batchNames[batchCount] = local.name;
        ++batchCount;
        if(batchCount == s_MaxBatchSize)
            flushBatch();
    }

    flushBatch();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

