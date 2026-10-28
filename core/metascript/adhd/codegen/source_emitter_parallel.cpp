// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter_parallel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendTaskOptions(MString& out, const Stmt& stmt){
    out.append("::NWB::Core::CpuTaskOptions{{}, ::NWB::Core::CpuTaskCost::");
    switch(stmt.parallelCore){
    case ParallelCoreKind::Heavy: out.append("Heavy"); break;
    case ParallelCoreKind::Light: out.append("Light"); break;
    case ParallelCoreKind::Any:
    case ParallelCoreKind::MainThread: out.append("Any"); break;
    }
    out.append(", ::NWB::Core::CpuTaskPriority::");
    switch(stmt.parallelPriority){
    case ParallelPriorityKind::Critical: out.append("Critical"); break;
    case ParallelPriorityKind::Normal: out.append("Normal"); break;
    case ParallelPriorityKind::Background: out.append("Background"); break;
    }
    out.append(", ::NWB::Core::CpuTaskTarget::");
    if(stmt.parallelCore == ParallelCoreKind::MainThread)
        out.append("MainThread");
    else
        out.append("Worker");
    out.push_back('}');
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

bool SourceEmitter::tryEmitParallelWaitStmt(
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

        if(loopBinding && loopBinding->isLoopElement && loopBinding->written)
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
        out.append("), 1u, [&](const usize __adhd_idx)noexcept{\r\n");
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
        if(!stmt.parallelDependencies.empty()){
            emitIndent(out, indent);
            out.append("const ::NWB::Core::CpuTaskHandle __adhd_deps_");
            out.append(stmt.name.data(), stmt.name.size());
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
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = __adhd_task_scope.submit([&]()noexcept{\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}, ");
        AppendTaskOptions(out, stmt);
        if(!stmt.parallelDependencies.empty()){
            out.append(", __adhd_deps_");
            out.append(stmt.name.data(), stmt.name.size());
            out.append(", ");
            AppendAdhdUnsigned(out, stmt.parallelDependencies.size());
            out.push_back('u');
        }
        out.append(");\r\n");
        emitIndent(out, indent);
        out.append("[[maybe_unused]] const ::NWB::Core::CpuTaskHandle __adhd_task_");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = __adhd_submit_");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(".has_value() ? *__adhd_submit_");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" : ::NWB::Core::CpuTaskHandle{};\r\n");
        break;
    }
    case StmtKind::ParallelCall:{
        if(stmt.expr && stmt.expr->kind == ExprKind::Call && !stmt.expr->arguments.empty()){
            emitIndent(out, indent);
            out.append("{\r\n");
            for(usize i = 0u; i < stmt.expr->arguments.size(); ++i){
                const Expr* arg = stmt.expr->arguments[i];
                emitIndent(out, indent + 1u);
                if(arg && arg->isMutArg)
                    out.append("auto& __adhd_arg_");
                else
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
                out.append("__adhd_arg_");
                AppendAdhdUnsigned(out, i);
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
        switch(stmt.diagnosticKind){
        case DiagnosticKind::Assert:
            if(stmt.arguments.size() > 1u)
                out.append(stmt.isEssential ? "NWB_FATAL_ASSERT_MSG(" : "NWB_ASSERT_MSG(");
            else
                out.append(stmt.isEssential ? "NWB_FATAL_ASSERT(" : "NWB_ASSERT(");
            break;
        case DiagnosticKind::Info:
            out.append(stmt.isEssential ? "NWB_LOGGER_ESSENTIAL_INFO(" : "NWB_LOGGER_INFO(");
            break;
        case DiagnosticKind::Warning:
            out.append(stmt.isEssential ? "NWB_LOGGER_CRITICAL_WARNING(" : "NWB_LOGGER_WARNING(");
            break;
        case DiagnosticKind::Error:
            out.append(stmt.isEssential ? "NWB_LOGGER_FATAL(" : "NWB_LOGGER_ERROR(");
            break;
        }
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

