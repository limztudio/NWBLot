// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_statement_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter_parallel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendTaskOptions(MString& out, const Stmt& stmt){
    MStringView costEnum = "Any";
    MStringView targetEnum = "Worker";
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, enumName, cppCostEnum, cppTargetEnum) \
    if(stmt.parallelCore == ParallelCoreKind::enumName){ costEnum = cppCostEnum; targetEnum = cppTargetEnum; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_CORE

    MStringView priorityEnum = "Normal";
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, enumName, cppPriorityEnum) \
    if(stmt.parallelPriority == ParallelPriorityKind::enumName) priorityEnum = cppPriorityEnum;
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY

    out.append("::NWB::Core::CpuTaskOptions{{}, ::NWB::Core::CpuTaskCost::");
    out.append(costEnum.data(), costEnum.size());
    out.append(", ::NWB::Core::CpuTaskPriority::");
    out.append(priorityEnum.data(), priorityEnum.size());
    out.append(", ::NWB::Core::CpuTaskTarget::");
    out.append(targetEnum.data(), targetEnum.size());
    out.push_back('}');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 StmtsContainTaskScope(const Span<const Stmt* const> statements)noexcept{
    for(const Stmt* s : statements){
        if(s && ParallelStatementEmitter::StmtUsesTaskScope(*s))
            return true;
    }
    return false;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ParallelStatementEmitter::BlockTaskScopeTracker::BlockTaskScopeTracker(const SourceEmitter& emitter, const Span<const Stmt* const> statements)noexcept
    : m_emitter(emitter)
    , m_prevHasActiveTaskScope(emitter.m_hasActiveTaskScope)
{
    if(!emitter.m_hasActiveTaskScope && NeedsLocalTaskScopeInList(statements, m_firstTaskStmtIndex))
        m_pendingLocalScope = true;
}

ParallelStatementEmitter::BlockTaskScopeTracker::~BlockTaskScopeTracker()noexcept{
    m_emitter.m_hasActiveTaskScope = m_prevHasActiveTaskScope;
}

void ParallelStatementEmitter::BlockTaskScopeTracker::beforeStatement(MString& out, const usize index, const u32 indent){
    if(m_pendingLocalScope && index == m_firstTaskStmtIndex){
        ParallelStatementEmitter(m_emitter).emitTaskScopeDecl(out, indent);
        m_emitter.m_hasActiveTaskScope = true;
        m_pendingLocalScope = false;
    }
}

void ParallelStatementEmitter::BlockTaskScopeTracker::afterStatement(MString& out, const Stmt& stmt, const Span<const Stmt* const> remaining, const u32 indent){
    if(m_emitter.m_hasActiveTaskScope && stmt.kind == StmtKind::VarDecl && StatementsContainAsyncParallelStmt(remaining))
        ParallelStatementEmitter(m_emitter).emitTaskScopeJoinGuard(out, indent, m_emitter.m_taskGuardCounter++);
}

b8 ParallelStatementEmitter::StmtUsesTaskScope(const Stmt& stmt)noexcept{
    switch(stmt.kind){
    case StmtKind::ParallelForRange:
    case StmtKind::ParallelForIndex:
    case StmtKind::ParallelBlock:
    case StmtKind::ParallelCall:
        return true;
    case StmtKind::ExprStmt:
        return IsParallelWaitCallExpr(stmt.expr);
    case StmtKind::Block:
    case StmtKind::While:
    case StmtKind::DoWhile:
    case StmtKind::ForRange:
    case StmtKind::ForIndex:
        return __hidden_adhd_source_emitter_parallel::StmtsContainTaskScope(stmt.statements);
    case StmtKind::If:
        return __hidden_adhd_source_emitter_parallel::StmtsContainTaskScope(stmt.statements)
            || __hidden_adhd_source_emitter_parallel::StmtsContainTaskScope(stmt.elseStatements);
    case StmtKind::Switch:{
        for(const SwitchCase& sc : stmt.switchCases){
            if(__hidden_adhd_source_emitter_parallel::StmtsContainTaskScope(sc.statements))
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

b8 ParallelStatementEmitter::StatementsContainAsyncParallelStmt(const Span<const Stmt* const> statements)noexcept{
    for(const Stmt* s : statements){
        if(!s)
            continue;
        switch(s->kind){
        case StmtKind::ParallelBlock:
        case StmtKind::ParallelCall:
            return true;
        case StmtKind::Block:
        case StmtKind::While:
        case StmtKind::DoWhile:
        case StmtKind::ForRange:
        case StmtKind::ForIndex:
            if(StatementsContainAsyncParallelStmt(s->statements))
                return true;
            break;
        case StmtKind::If:
            if(StatementsContainAsyncParallelStmt(s->statements) || StatementsContainAsyncParallelStmt(s->elseStatements))
                return true;
            break;
        case StmtKind::Switch:{
            for(const SwitchCase& sc : s->switchCases){
                if(StatementsContainAsyncParallelStmt(sc.statements))
                    return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return false;
}

b8 ParallelStatementEmitter::NeedsLocalTaskScopeInList(const Span<const Stmt* const> statements, usize& outFirstTaskStmtIndex)noexcept{
    usize count = 0u;
    usize firstIdx = 0u;
    for(usize i = 0u; i < statements.size(); ++i){
        const Stmt* const s = statements[i];
        if(s && StmtUsesTaskScope(*s)){
            if(count == 0u)
                firstIdx = i;
            ++count;
        }
    }
    if(count == 0u)
        return false;
    const Stmt* const first = statements[firstIdx];
    if(
        count == 1u
        && (first->kind == StmtKind::Block || first->kind == StmtKind::If || first->kind == StmtKind::Switch || IsSequentialLoopStmtKind(first->kind))
    )
        return false;
    outFirstTaskStmtIndex = firstIdx;
    return true;
}

void ParallelStatementEmitter::emitTaskSchedulerPrologue(MString& out, const u32 indent)const{
    m_emitter.emitIndent(out, indent);
    out.append("::NWB::Core::CpuTaskScheduler& s_AdhdTaskScheduler = ::NWB::Core::SharedCpuTaskScheduler();\r\n");
    m_emitter.emitIndent(out, indent);
    out.append("[[maybe_unused]] ::Futex __adhd_parallel_arena_mutex;\r\n");
}

void ParallelStatementEmitter::emitTaskScopeDecl(MString& out, const u32 indent)const{
    m_emitter.emitIndent(out, indent);
    out.append("::NWB::Core::CpuTaskScope __adhd_task_scope(s_AdhdTaskScheduler);\r\n");
}

void ParallelStatementEmitter::emitTaskScopeJoinGuard(MString& out, const u32 indent, const u32 guardId)const{
    m_emitter.emitIndent(out, indent);
    out.append("const ::NWB::Core::CpuTaskScope::JoinGuard __adhd_task_guard_");
    AppendUnsignedDecimal(out, guardId);
    out.append("(__adhd_task_scope);\r\n");
}

void ParallelStatementEmitter::emitLoopBodyTaskGuardIfNeeded(MString& out, const Span<const Stmt* const> statements, const u32 indent)const{
    if(m_emitter.m_hasActiveTaskScope && StatementsContainAsyncParallelStmt(statements))
        emitTaskScopeJoinGuard(out, indent, m_emitter.m_taskGuardCounter++);
}

b8 ParallelStatementEmitter::tryEmitParallelWaitStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    if(!IsParallelWaitCallExpr(stmt.expr))
        return false;

    if(stmt.expr->arguments.empty()){
        m_emitter.emitIndent(out, indent);
        out.append("__adhd_task_scope.wait();\r\n");
    }
    else{
        for(const Expr* arg : stmt.expr->arguments){
            if(!arg || arg->kind != ExprKind::Identifier)
                continue;
            m_emitter.emitIndent(out, indent);
            out.append("s_AdhdTaskScheduler.wait(__adhd_task_");
            out.append(arg->text.data(), arg->text.size());
            out.append(");\r\n");
        }
    }
    return true;
}

void ParallelStatementEmitter::emitParallelStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    using namespace __hidden_adhd_source_emitter_parallel;

    const auto emitWorkerStatements = [&](const Stmt* const declToPush){
        const usize prevDecls = m_emitter.m_activeLocalVarDecls.size();
        const TypeRef* prevRet = m_emitter.m_activeCallerArenaReturnType;
        const b8 prevActiveTaskScope = m_emitter.m_hasActiveTaskScope;
        if(declToPush)
            m_emitter.m_activeLocalVarDecls.push_back(declToPush);
        m_emitter.m_activeCallerArenaReturnType = nullptr;
        m_emitter.m_hasActiveTaskScope = false;
        ++m_emitter.m_parallelWorkerDepth;
        m_emitter.emitStatementList(out, stmt.statements, indent + 1u);
        --m_emitter.m_parallelWorkerDepth;
        m_emitter.m_hasActiveTaskScope = prevActiveTaskScope;
        m_emitter.m_activeCallerArenaReturnType = prevRet;
        m_emitter.m_activeLocalVarDecls.resize(prevDecls);
    };

    switch(stmt.kind){
    case StmtKind::ParallelForRange:{
        m_emitter.emitIndent(out, indent);
        out.append("__adhd_task_scope.parallelForChecked(0u, (");
        if(stmt.expr)
            m_emitter.emitExpr(out, *stmt.expr);
        out.append(").size(), 1u, [&](const usize __adhd_idx)noexcept{\r\n");
        if(m_emitter.statementsNeedSimdFloatingPointEnv(stmt.statements))
            out.append("#pragma STDC FENV_ACCESS ON\r\n");
        if(stmt.usesScratchArena)
            m_emitter.emitScratchArenaPrologue(out, indent + 1u, "parallel");
        m_emitter.emitIndent(out, indent + 1u);
        if(stmt.rangeMode == ParamPassingMode::In)
            out.append("const ");
        if(stmt.varType)
            m_emitter.m_typeEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.append("& ");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = (");
        if(stmt.expr)
            m_emitter.emitExpr(out, *stmt.expr);
        out.append(")[__adhd_idx];\r\n");

        emitWorkerStatements(!stmt.name.empty() ? &stmt : nullptr);

        m_emitter.emitIndent(out, indent);
        out.append("}, ");
        AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ParallelForIndex:{
        m_emitter.emitIndent(out, indent);
        out.append("__adhd_task_scope.parallelForChecked(static_cast<usize>(");
        if(stmt.initStmt && stmt.initStmt->expr)
            m_emitter.emitExpr(out, *stmt.initStmt->expr);
        else if(stmt.initStmt && stmt.initStmt->isCtorInit && !stmt.initStmt->arguments.empty() && stmt.initStmt->arguments[0])
            m_emitter.emitExpr(out, *stmt.initStmt->arguments[0]);
        else
            out.append("0u");
        out.append("), static_cast<usize>(");
        if(stmt.condition && stmt.condition->kind == ExprKind::Binary && stmt.condition->rhs){
            m_emitter.emitExpr(out, *stmt.condition->rhs);
            if(stmt.condition->op == TokenType::LessEqual)
                out.append(" + 1u");
        }
        else if(stmt.condition)
            m_emitter.emitExpr(out, *stmt.condition);
        else
            out.append("0u");
        out.append("), ");
        if(stmt.stepExpr && stmt.stepExpr->kind == ExprKind::Binary){
            if(stmt.stepExpr->op == TokenType::PlusEqual && stmt.stepExpr->rhs){
                out.append("static_cast<usize>(");
                m_emitter.emitExpr(out, *stmt.stepExpr->rhs);
                out.push_back(')');
            }
            else if(stmt.stepExpr->op == TokenType::Equal && stmt.stepExpr->rhs && stmt.stepExpr->rhs->kind == ExprKind::Binary && stmt.stepExpr->rhs->op == TokenType::Plus){
                const MStringView idxName = stmt.initStmt ? MStringView(stmt.initStmt->name.data(), stmt.initStmt->name.size()) : MStringView{};
                const Expr* stepOperand = (stmt.stepExpr->rhs->rhs && stmt.stepExpr->rhs->rhs->kind == ExprKind::Identifier && stmt.stepExpr->rhs->rhs->text == idxName)
                    ? stmt.stepExpr->rhs->lhs
                    : stmt.stepExpr->rhs->rhs;
                out.append("static_cast<usize>(");
                if(stepOperand)
                    m_emitter.emitExpr(out, *stepOperand);
                else
                    out.append("1u");
                out.push_back(')');
            }
            else{
                out.append("1u");
            }
        }
        else{
            out.append("1u");
        }
        out.append(", [&](const usize __adhd_idx)noexcept{\r\n");
        if(m_emitter.statementsNeedSimdFloatingPointEnv(stmt.statements))
            out.append("#pragma STDC FENV_ACCESS ON\r\n");
        if(stmt.usesScratchArena)
            m_emitter.emitScratchArenaPrologue(out, indent + 1u, "parallel");
        if(stmt.initStmt && stmt.initStmt->varType){
            m_emitter.emitIndent(out, indent + 1u);
            if(stmt.initStmt->varType->isAuto){
                out.append("const auto ");
                out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
                out.append(" = __adhd_idx;\r\n");
            }
            else{
                out.append("const ");
                m_emitter.m_typeEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
                out.push_back(' ');
                out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
                out.append(" = static_cast<");
                m_emitter.m_typeEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
                out.append(">(__adhd_idx);\r\n");
            }
        }
        emitWorkerStatements((stmt.initStmt && !stmt.initStmt->name.empty()) ? &stmt : nullptr);
        m_emitter.emitIndent(out, indent);
        out.append("}, ");
        AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ParallelBlock:{
        const auto appendBlockSuffix = [&](){
            if(!stmt.name.empty())
                out.append(stmt.name.data(), stmt.name.size());
            else
                AppendUnsignedDecimal(out, stmt.span.line);
        };
        if(!stmt.parallelDependencies.empty()){
            m_emitter.emitIndent(out, indent);
            out.append("const ::Array<::NWB::Core::CpuTaskHandle, ");
            AppendUnsignedDecimal(out, stmt.parallelDependencies.size());
            out.append("u> __adhd_deps_");
            appendBlockSuffix();
            out.append(" = { ");
            for(usize i = 0u; i < stmt.parallelDependencies.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                out.append("__adhd_task_");
                out.append(stmt.parallelDependencies[i].data(), stmt.parallelDependencies[i].size());
            }
            out.append(" };\r\n");
        }
        m_emitter.emitIndent(out, indent);
        if(!stmt.name.empty()){
            out.append("const ::NWB::Core::CpuTaskHandle __adhd_task_");
            appendBlockSuffix();
            out.append(" = __adhd_task_scope.submitChecked([&]()noexcept{\r\n");
        }
        else{
            out.append("__adhd_task_scope.submitDetachedChecked([&]()noexcept{\r\n");
        }
        if(m_emitter.statementsNeedSimdFloatingPointEnv(stmt.statements))
            out.append("#pragma STDC FENV_ACCESS ON\r\n");
        if(stmt.usesScratchArena)
            m_emitter.emitScratchArenaPrologue(out, indent + 1u, "parallel");
        emitWorkerStatements((stmt.initStmt && !stmt.initStmt->name.empty()) ? &stmt : nullptr);
        m_emitter.emitIndent(out, indent);
        out.append("}, ");
        AppendTaskOptions(out, stmt);
        if(!stmt.parallelDependencies.empty()){
            out.append(", __adhd_deps_");
            appendBlockSuffix();
            out.append(".data(), ");
            AppendUnsignedDecimal(out, stmt.parallelDependencies.size());
            out.push_back('u');
        }
        out.append(");\r\n");
        break;
    }
    case StmtKind::ParallelCall:{
        if(stmt.expr && stmt.expr->kind == ExprKind::Call && !stmt.expr->arguments.empty()){
            m_emitter.emitIndent(out, indent);
            out.append("{\r\n");
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                const Expr* arg = stmt.expr->arguments[i];
                if(arg && arg->isMutArg)
                    continue;
                m_emitter.emitIndent(out, indent + 1u);
                out.append("const auto __adhd_arg_");
                AppendUnsignedDecimal(out, i);
                out.append(" = (");
                if(arg)
                    m_emitter.emitExpr(out, *arg);
                out.append(");\r\n");
            }
            m_emitter.emitIndent(out, indent + 1u);
            out.append("__adhd_task_scope.submitDetachedChecked([&");
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                const Expr* arg = stmt.expr->arguments[i];
                if(!arg || !arg->isMutArg){
                    out.append(", __adhd_arg_");
                    AppendUnsignedDecimal(out, i);
                }
            }
            out.append("]()noexcept{ ");
            if(stmt.usesScratchArena){
                out.append("\r\n");
                m_emitter.emitScratchArenaPrologue(out, indent + 2u, "parallel");
                m_emitter.emitIndent(out, indent + 2u);
            }
            const MStringView parallelCallee = (stmt.expr->lhs && stmt.expr->lhs->kind == ExprKind::Identifier)
                ? stmt.expr->lhs->text
                : MStringView{};
            const MStringView baseParallelCallee = !parallelCallee.empty()
                ? m_emitter.m_symbols.resolveBaseTypeName(parallelCallee)
                : MStringView{};
            const b8 parallelArenaLast = !stmt.expr->isBuiltinCall
                && !stmt.expr->isUserCallableCall
                && !baseParallelCallee.empty()
                && !m_emitter.m_symbols.hasTypeSymbol(baseParallelCallee)
                && SymbolTable::IsArenaBackedContainer(baseParallelCallee)
                && !IsArenaFirstCtorType(baseParallelCallee);
            const b8 parallelArenaFirst = !parallelArenaLast;
            const MStringView parallelArena = stmt.expr->injectClassArena
                ? m_emitter.activeClassArenaExpr()
                : (stmt.expr->injectScratchArena ? CodegenSymbols::s_ScratchArenaVar : MStringView{});
            m_emitter.emitCallTargetExpr(out, stmt.expr->lhs, stmt.expr->isBuiltinCall, stmt.expr->isUserCallableCall, stmt.expr->callTemplateArgs);
            out.push_back('(');
            if(!parallelArena.empty() && parallelArenaFirst){
                out.append(parallelArena.data(), parallelArena.size());
                if(!stmt.expr->arguments.empty())
                    out.append(", ");
            }
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                const Expr* arg = stmt.expr->arguments[i];
                if(arg && arg->isMutArg)
                    m_emitter.emitExpr(out, *arg);
                else{
                    out.append("__adhd_arg_");
                    AppendUnsignedDecimal(out, i);
                }
            }
            if(!parallelArena.empty() && !parallelArenaFirst){
                if(!stmt.expr->arguments.empty())
                    out.append(", ");
                out.append(parallelArena.data(), parallelArena.size());
            }
            out.append("); }, ");
            AppendTaskOptions(out, stmt);
            out.append(");\r\n");
            m_emitter.emitIndent(out, indent);
            out.append("}\r\n");
        }
        else{
            m_emitter.emitIndent(out, indent);
            out.append("__adhd_task_scope.submitDetachedChecked([&]()noexcept{ ");
            if(stmt.usesScratchArena){
                out.append("\r\n");
                m_emitter.emitScratchArenaPrologue(out, indent + 1u, "parallel");
                m_emitter.emitIndent(out, indent + 1u);
            }
            if(stmt.expr)
                m_emitter.emitExpr(out, *stmt.expr);
            out.append("; }, ");
            AppendTaskOptions(out, stmt);
            out.append(");\r\n");
        }
        break;
    }
    default:
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

