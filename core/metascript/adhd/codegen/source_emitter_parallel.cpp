// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter_parallel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendTaskOptions(MString& out, const Stmt& stmt){
    MStringView costEnum = "Any";
    MStringView targetEnum = "Worker";
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, astEnum, cppCostEnum, cppTargetEnum) \
    if(stmt.parallelCore == astEnum){ costEnum = cppCostEnum; targetEnum = cppTargetEnum; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_CORE

    MStringView priorityEnum = "Normal";
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, astEnum, cppPriorityEnum) \
    if(stmt.parallelPriority == astEnum) priorityEnum = cppPriorityEnum;
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

[[nodiscard]] b8 ExprUsesScratchArena(const Expr* expr)noexcept{
    if(!expr || expr->kind == ExprKind::Lambda)
        return false;
    if(expr->injectScratchArena)
        return true;
    if(ExprUsesScratchArena(expr->lhs) || ExprUsesScratchArena(expr->rhs))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprUsesScratchArena(arg))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StatementsUseScratchArena(const MVector<Stmt*>& statements)noexcept;

[[nodiscard]] b8 StmtUsesScratchArena(const Stmt* stmt)noexcept{
    if(!stmt)
        return false;
    if(stmt->kind == StmtKind::ParallelForRange
        || stmt->kind == StmtKind::ParallelForIndex
        || stmt->kind == StmtKind::ParallelBlock)
    {
        return false;
    }
    if(stmt->usesScratchArena)
        return true;
    if(stmt->initStmt && StmtUsesScratchArena(stmt->initStmt))
        return true;
    if(ExprUsesScratchArena(stmt->expr)
        || ExprUsesScratchArena(stmt->condition)
        || ExprUsesScratchArena(stmt->stepExpr))
    {
        return true;
    }
    for(const Expr* arg : stmt->arguments){
        if(ExprUsesScratchArena(arg))
            return true;
    }
    if(StatementsUseScratchArena(stmt->statements) || StatementsUseScratchArena(stmt->elseStatements))
        return true;
    for(const SwitchCase& sc : stmt->switchCases){
        if(ExprUsesScratchArena(sc.matchExpr) || StatementsUseScratchArena(sc.statements))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StatementsUseScratchArena(const MVector<Stmt*>& statements)noexcept{
    for(const Stmt* s : statements){
        if(StmtUsesScratchArena(s))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitTaskScopePrologue(MString& out, const u32 indent)const{
    emitIndent(out, indent);
    out.append("static ::NWB::Core::CpuTaskScheduler s_AdhdTaskScheduler;\r\n");
    emitIndent(out, indent);
    out.append("::NWB::Core::CpuTaskScope __adhd_task_scope(s_AdhdTaskScheduler);\r\n");
    emitIndent(out, indent);
    out.append("::ScopeExit __adhd_scope_join([&__adhd_task_scope]()noexcept{ __adhd_task_scope.wait(); });\r\n");
}

b8 SourceEmitter::tryEmitParallelWaitStmt(
    MString& out,
    const Stmt& stmt,
    const u32 indent
)const{
    if(!stmt.expr
        || stmt.expr->kind != ExprKind::Call
        || !stmt.expr->lhs
        || stmt.expr->lhs->kind != ExprKind::Identifier
        || stmt.expr->lhs->text != "wait")
    {
        return false;
    }

    if(stmt.expr->arguments.empty()){
        emitIndent(out, indent);
        out.append("__adhd_task_scope.wait();\r\n");
    }
    else{
        for(const Expr* arg : stmt.expr->arguments){
            if(!arg || arg->kind != ExprKind::Identifier)
                continue;
            emitIndent(out, indent);
            out.append("s_AdhdTaskScheduler.wait(__adhd_task_");
            out.append(arg->text.data(), arg->text.size());
            out.append(");\r\n");
        }
    }
    if(m_activeSimdBindings){
        for(const SimdVarBinding& binding : *m_activeSimdBindings){
            if(binding.capturedByMutLambda)
                emitSimdLoadReloadForBinding(out, binding, indent);
        }
    }
    return true;
}

void SourceEmitter::emitParallelStmt(
    MString& out,
    const Stmt& stmt,
    const u32 indent
)const{
    using namespace __hidden_adhd_source_emitter_parallel;

    switch(stmt.kind){
    case StmtKind::ParallelForRange:{
        emitIndent(out, indent);
        out.append("(void)__adhd_task_scope.parallelFor(0u, (");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(").size(), 1u, [&](const usize __adhd_idx)noexcept{\r\n");
        if(StatementsUseScratchArena(stmt.statements)){
            emitIndent(out, indent + 1u);
            out.append("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"parallel\"));\r\n");
        }
        emitIndent(out, indent + 1u);
        if(stmt.rangeMode == ParamPassingMode::In)
            out.append("const ");
        if(stmt.varType)
            m_typeEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.append("& ");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = (");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(")[__adhd_idx];\r\n");

        const SimdVarBinding* loopBinding = findSimdBinding(false, stmt.name);
        if(loopBinding && loopBinding->isLoopElement)
            emitSimdLoopElementLoad(out, *loopBinding, indent + 1u);

        emitStatementList(out, stmt.statements, indent + 1u);

        if(stmt.rangeMode == ParamPassingMode::Mut && loopBinding && loopBinding->isLoopElement && loopBinding->written)
            emitSimdStoreForBinding(out, *loopBinding, indent + 1u);

        emitIndent(out, indent);
        out.append("}, ");
        AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ParallelForIndex:{
        emitIndent(out, indent);
        out.append("(void)__adhd_task_scope.parallelFor(static_cast<usize>(");
        if(stmt.initStmt && stmt.initStmt->expr)
            emitExpr(out, *stmt.initStmt->expr);
        else if(stmt.initStmt && stmt.initStmt->isCtorInit && !stmt.initStmt->arguments.empty() && stmt.initStmt->arguments[0])
            emitExpr(out, *stmt.initStmt->arguments[0]);
        else
            out.append("0u");
        out.append("), static_cast<usize>(");
        if(stmt.condition && stmt.condition->kind == ExprKind::Binary && stmt.condition->rhs){
            emitExpr(out, *stmt.condition->rhs);
            if(stmt.condition->op == TokenType::LessEqual)
                out.append(" + 1u");
        }
        else if(stmt.condition)
            emitExpr(out, *stmt.condition);
        else
            out.append("0u");
        out.append("), ");
        if(stmt.stepExpr && stmt.stepExpr->kind == ExprKind::Binary){
            if(stmt.stepExpr->op == TokenType::PlusEqual && stmt.stepExpr->rhs){
                out.append("static_cast<usize>(");
                emitExpr(out, *stmt.stepExpr->rhs);
                out.push_back(')');
            }
            else if(stmt.stepExpr->op == TokenType::Equal
                && stmt.stepExpr->rhs
                && stmt.stepExpr->rhs->kind == ExprKind::Binary
                && stmt.stepExpr->rhs->op == TokenType::Plus
                && stmt.stepExpr->rhs->rhs)
            {
                out.append("static_cast<usize>(");
                emitExpr(out, *stmt.stepExpr->rhs->rhs);
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
        if(StatementsUseScratchArena(stmt.statements)){
            emitIndent(out, indent + 1u);
            out.append("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"parallel\"));\r\n");
        }
        if(stmt.initStmt && stmt.initStmt->varType){
            emitIndent(out, indent + 1u);
            out.append("const ");
            m_typeEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.push_back(' ');
            out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
            out.append(" = static_cast<");
            m_typeEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.append(">(__adhd_idx);\r\n");
        }
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}, ");
        AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ParallelBlock:{
        auto appendBlockSuffix = [&](){
            if(!stmt.name.empty())
                out.append(stmt.name.data(), stmt.name.size());
            else
                AppendAdhdUnsigned(out, stmt.span.line);
        };
        if(!stmt.parallelDependencies.empty()){
            emitIndent(out, indent);
            out.append("const ::NWB::Core::CpuTaskHandle __adhd_deps_");
            appendBlockSuffix();
            out.append("[] = { ");
            for(usize i = 0u; i < stmt.parallelDependencies.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                out.append("__adhd_task_");
                out.append(stmt.parallelDependencies[i].data(), stmt.parallelDependencies[i].size());
            }
            out.append(" };\r\n");
        }
        emitIndent(out, indent);
        out.append("const auto __adhd_submit_");
        appendBlockSuffix();
        out.append(" = __adhd_task_scope.submit([&]()noexcept{\r\n");
        if(StatementsUseScratchArena(stmt.statements)){
            emitIndent(out, indent + 1u);
            out.append("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"parallel\"));\r\n");
        }
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}, ");
        AppendTaskOptions(out, stmt);
        if(!stmt.parallelDependencies.empty()){
            out.append(", __adhd_deps_");
            appendBlockSuffix();
            out.append(", ");
            AppendAdhdUnsigned(out, stmt.parallelDependencies.size());
            out.push_back('u');
        }
        out.append(");\r\n");
        emitIndent(out, indent);
        out.append("[[maybe_unused]] const ::NWB::Core::CpuTaskHandle __adhd_task_");
        appendBlockSuffix();
        out.append(" = __adhd_submit_");
        appendBlockSuffix();
        out.append(".has_value() ? *__adhd_submit_");
        appendBlockSuffix();
        out.append(" : ::NWB::Core::CpuTaskHandle{};\r\n");
        break;
    }
    case StmtKind::ParallelCall:{
        if(stmt.expr && stmt.expr->kind == ExprKind::Call && !stmt.expr->arguments.empty()){
            emitIndent(out, indent);
            out.append("{\r\n");
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                const Expr* arg = stmt.expr->arguments[i];
                if(arg && arg->isMutArg)
                    continue;
                emitIndent(out, indent + 1u);
                out.append("const auto __adhd_arg_");
                AppendAdhdUnsigned(out, i);
                out.append(" = (");
                if(arg)
                    emitExpr(out, *arg);
                out.append(");\r\n");
            }
            emitIndent(out, indent + 1u);
            out.append("(void)__adhd_task_scope.submit([&");
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                const Expr* arg = stmt.expr->arguments[i];
                if(!arg || !arg->isMutArg){
                    out.append(", __adhd_arg_");
                    AppendAdhdUnsigned(out, i);
                }
            }
            out.append("]()noexcept{ ");
            if(stmt.expr->lhs)
                emitExpr(out, *stmt.expr->lhs);
            if(!stmt.expr->callTemplateArgs.empty())
                m_typeEmitter.emitTypeArgListCpp(out, stmt.expr->callTemplateArgs);
            out.push_back('(');
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                const Expr* arg = stmt.expr->arguments[i];
                if(arg && arg->isMutArg){
                    emitExpr(out, *arg);
                }
                else{
                    out.append("__adhd_arg_");
                    AppendAdhdUnsigned(out, i);
                }
            }
            out.append("); }, ");
            AppendTaskOptions(out, stmt);
            out.append(");\r\n");
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        else{
            emitIndent(out, indent);
            out.append("(void)__adhd_task_scope.submit([&]()noexcept{ ");
            if(stmt.expr)
                emitExpr(out, *stmt.expr);
            out.append("; }, ");
            AppendTaskOptions(out, stmt);
            out.append(");\r\n");
        }
        break;
    }
    case StmtKind::NativeBlock:{
        emitIndent(out, indent);
        out.append("{\r\n");
        for(const NativeBinding& binding : stmt.nativeBindings){
            if(binding.sourceExpr && binding.sourceExpr->kind == ExprKind::Identifier && binding.sourceExpr->text == binding.aliasName)
                continue;
            emitIndent(out, indent + 1u);
            if(binding.mode == ParamPassingMode::In)
                out.append("const auto& ");
            else
                out.append("auto& ");
            out.append(binding.aliasName.data(), binding.aliasName.size());
            out.append(" = (");
            if(binding.sourceExpr)
                emitExpr(out, *binding.sourceExpr);
            out.append(");\r\n");
        }
        emitIndent(out, indent + 1u);
        AppendAdhdTextCrlf(out, stmt.nativeCode);
        out.append("\r\n");
        for(const NativeBinding& binding : stmt.nativeBindings){
            if(binding.mode != ParamPassingMode::Mut || !binding.sourceExpr)
                continue;
            const SimdVarBinding* mutBinding = findSimdBindingForExpr(binding.sourceExpr);
            if(!mutBinding && (binding.sourceExpr->kind == ExprKind::MemberAccess || binding.sourceExpr->kind == ExprKind::Index))
                mutBinding = findSimdBindingForExpr(binding.sourceExpr->lhs);
            if(mutBinding)
                emitSimdLoadReloadForBinding(out, *mutBinding, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::Diagnostic:{
        emitIndent(out, indent);
        MStringView macroPrefix;
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, astEnum, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
        if(stmt.diagnosticKind == astEnum){ \
            if(stmt.arguments.size() > 1u) \
                macroPrefix = stmt.isEssential ? MStringView(essentialMsgMacroPrefix) : MStringView(defaultMsgMacroPrefix); \
            else \
                macroPrefix = stmt.isEssential ? MStringView(essentialMacroPrefix) : MStringView(defaultMacroPrefix); \
        }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
        out.append(macroPrefix.data(), macroPrefix.size());
        for(usize i = 0u; i < stmt.arguments.size(); ++i){
            if(i > 0u)
                out.append(", ");
            if(stmt.arguments[i])
                emitExpr(out, *stmt.arguments[i]);
        }
        out.append(");\r\n");
        break;
    }
    default:
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

