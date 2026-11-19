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

[[nodiscard]] b8 StmtPreventsMoveAtLastUse(const Stmt* stmt, MStringView varName, const SymbolTable& symbols, const Stmt* endedScope = nullptr)noexcept;

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

[[nodiscard]] b8 StmtPreventsMoveAtLastUse(const Stmt* stmt, const MStringView varName, const SymbolTable& symbols, const Stmt* endedScope)noexcept{
    if(!stmt)
        return false;
    if((stmt->kind == StmtKind::ParallelBlock || stmt->kind == StmtKind::ParallelCall) && StmtReferencesIdentifier(stmt, varName))
        return true;
    if(stmt->kind == StmtKind::VarDecl && stmt->name != varName && stmt->varType && StmtReferencesIdentifier(stmt, varName)){
        const MStringView baseVarType = symbols.resolveBaseTypeName(stmt->varType->name);
        if(symbols.resolveRefMode(stmt->varType) != ParamPassingMode::None || IsBorrowViewType(baseVarType)){
            if(!endedScope || StmtMayRetainAlias(endedScope, stmt->name, symbols))
                return true;
        }
    }
    if(ExprHasMutLambdaCapture(stmt->expr, varName, symbols) || ExprHasMutLambdaCapture(stmt->condition, varName, symbols) || ExprHasMutLambdaCapture(stmt->stepExpr, varName, symbols))
        return true;
    for(const Expr* arg : stmt->arguments){
        if(ExprHasMutLambdaCapture(arg, varName, symbols))
            return true;
    }
    if(stmt->initStmt && StmtPreventsMoveAtLastUse(stmt->initStmt, varName, symbols, endedScope ? endedScope : stmt))
        return true;
    for(const Stmt* child : stmt->statements){
        if(StmtPreventsMoveAtLastUse(child, varName, symbols, endedScope ? endedScope : stmt))
            return true;
    }
    for(const Stmt* child : stmt->elseStatements){
        if(StmtPreventsMoveAtLastUse(child, varName, symbols, endedScope ? endedScope : stmt))
            return true;
    }
    for(const SwitchCase& sc : stmt->switchCases){
        if(ExprHasMutLambdaCapture(sc.matchExpr, varName, symbols))
            return true;
        for(const Stmt* child : sc.statements){
            if(StmtPreventsMoveAtLastUse(child, varName, symbols, endedScope ? endedScope : stmt))
                return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LastUseMoveAnalyzer::analyze(const Span<Stmt* const> statements, const Span<const ScopedVarInfo> scope, const usize savedScopeSize)const noexcept{
    for(usize i = savedScopeSize; i < scope.size(); ++i){
        const ScopedVarInfo& local = scope[i];
        if(local.isParam || local.isConst || (local.type && m_symbols.resolveRefMode(local.type) != ParamPassingMode::None))
            continue;

        const MStringView localBaseType = ResolveScopedVarBaseType(m_symbols, local);
        if(SymbolTable::IsSimdType(localBaseType) || m_symbols.isSmallValueType(localBaseType))
            continue;

        usize lastUseIndex = statements.size();
        for(usize s = statements.size(); s > 0u; --s){
            if(__hidden_last_use_move_analyzer::StmtReferencesIdentifier(statements[s - 1u], local.name)){
                lastUseIndex = s - 1u;
                break;
            }
        }

        b8 capturedOrAsyncOrAliased = false;
        for(usize j = 0u; j < scope.size(); ++j){
            if((scope[j].borrowsLocalStringOwner && scope[j].borrowedLocalOwnerName == local.name) || scope[j].referenceOwnerName == local.name){
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
            if(__hidden_last_use_move_analyzer::StmtPreventsMoveAtLastUse(statements[s], local.name, m_symbols)){
                capturedOrAsyncOrAliased = true;
                break;
            }
        }
        if(capturedOrAsyncOrAliased || lastUseIndex >= statements.size())
            continue;

        Stmt* lastStmt = statements[lastUseIndex];
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

