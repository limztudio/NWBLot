// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "last_use_move_analyzer.h"
#include "type_checker.h"

#include <global/source_text.h>


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

[[nodiscard]] b8 NativeCodeReferencesIdentifier(const MStringView nativeCode, const MStringView varName)noexcept{
    if(nativeCode.empty() || varName.empty())
        return false;
    usize i = 0u;
    while(i < nativeCode.size()){
        const MChar ch = nativeCode[i];
        if(ch == '\'' && i > 0u && (IsAsciiDigit(nativeCode[i - 1u]) || IsAsciiIdentifierChar(nativeCode[i - 1u]))){
            ++i;
            continue;
        }
        if(ch == '"' || ch == '\''){
            i = SkipQuotedLiteral(nativeCode, i);
            continue;
        }
        if(i + 1u < nativeCode.size() && ch == '/' && nativeCode[i + 1u] == '/'){
            while(i < nativeCode.size() && nativeCode[i] != '\n')
                ++i;
            continue;
        }
        if(i + 1u < nativeCode.size() && ch == '/' && nativeCode[i + 1u] == '*'){
            const usize closeComment = nativeCode.find("*/", i + 2u);
            if(closeComment == MStringView::npos)
                break;
            i = closeComment + 2u;
            continue;
        }
        if(IsAsciiIdentifierStart(ch)){
            const usize start = i;
            while(i < nativeCode.size() && IsAsciiIdentifierChar(nativeCode[i]))
                ++i;
            if(nativeCode.substr(start, i - start) == varName)
                return true;
            continue;
        }
        ++i;
    }
    return false;
}

[[nodiscard]] b8 StmtReferencesIdentifier(const Stmt* stmt, const MStringView varName)noexcept{
    if(!stmt)
        return false;
    if(stmt->kind == StmtKind::NativeBlock && NativeCodeReferencesIdentifier(stmt->nativeCode, varName))
        return true;
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

