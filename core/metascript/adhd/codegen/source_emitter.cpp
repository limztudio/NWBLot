// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendUnsigned(MString& out, usize value){
    char buf[32];
    usize idx = 0u;
    if(value == 0u){
        out.push_back('0');
        return;
    }
    while(value > 0u && idx < sizeof(buf)){
        buf[idx++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    while(idx > 0u)
        out.push_back(buf[--idx]);
}

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

[[nodiscard]] MStringView MapBuiltinMathFunction(const MStringView name)noexcept{
    if(name == "min") return "::Min";
    if(name == "max") return "::Max";
    if(name == "abs") return "::Abs";
    if(name == "clamp") return "::Clamp";
    if(name == "saturate") return "::Saturate";
    if(name == "floor") return "::Floor";
    if(name == "ceil") return "::Ceil";
    if(name == "round") return "::Truncate";
    if(name == "sqrt") return "::Sqrt";
    if(name == "rsqrt") return "::VectorReciprocalSqrt";
    if(name == "lerp") return "::VectorLerp";
    if(name == "dot") return "::Vector4Dot";
    if(name == "cross") return "::Vector3Cross";
    if(name == "length") return "::Vector4Length";
    if(name == "lengthSq") return "::Vector4LengthSq";
    if(name == "normalize") return "::Vector4Normalize";
    if(name == "mul") return "::MatrixMultiply";
    if(name == "transpose") return "::MatrixTranspose";
    if(name == "inverse") return "::MatrixInverse";
    if(name == "sin") return "::Sin";
    if(name == "cos") return "::Cos";
    if(name == "tan") return "::Tan";
    if(name == "asin") return "::ASin";
    if(name == "acos") return "::ACos";
    if(name == "atan") return "::ATan";
    if(name == "atan2") return "::ATan2";
    if(name == "exp") return "::Exp";
    if(name == "log") return "::Log";
    if(name == "pow") return "::Pow";
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString SourceEmitter::emitSource(
    const TranslationUnit& unit,
    const MStringView headerIncludePath,
    const MStringView namespacePath
)const{
    MString out(m_arena);
    out.reserve(4096u);

    ReflectionEmitter reflectionEmitter(m_arena, m_symbols, m_headerEmitter);

    out.append("// limztudio@gmail.com\r\n");
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append("#include \"");
    out.append(headerIncludePath.data(), headerIncludePath.size());
    out.append("\"\r\n\r\n");
    out.append("#include <global/assert.h>\r\n");
    out.append("#include <core/common/log.h>\r\n");
    out.append("#include <core/task/cpu/scheduler.h>\r\n\r\n\r\n");
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl)
            continue;

        if(classDecl->templateParams.empty()){
            for(const MethodDecl* method : classDecl->methods){
                if(!method || !method->hasBody || method->isInline || !method->templateParams.empty())
                    continue;

                if(!method->isConstructor && !method->isDestructor){
                    if(method->returnType)
                        m_headerEmitter.emitTypeCpp(out, *method->returnType);
                    else
                        out.append("void");
                    out.push_back(' ');
                }

                out.append(classDecl->name.data(), classDecl->name.size());
                out.append("::");
                if(method->isDestructor)
                    out.push_back('~');
                if(method->isOperator)
                    out.append("operator");
                out.append(method->name.data(), method->name.size());

                out.push_back('(');
                for(usize i = 0u; i < method->params.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    const ParamDecl& p = method->params[i];
                    if(p.type)
                        m_headerEmitter.emitParamTypeCpp(out, p.mode, *p.type);
                    out.push_back(' ');
                    out.append(p.name.data(), p.name.size());
                }
                out.push_back(')');
                if(method->isConst)
                    out.append("const noexcept{\r\n");
                else
                    out.append("noexcept{\r\n");
                emitMethodBodyBlock(out, classDecl, *method, 1u);
                out.append("}\r\n\r\n");
            }
        }

        if(classDecl->isSerialize || classDecl->isReflect || !classDecl->assetTypeName.empty())
            reflectionEmitter.emitClassReflectionDefinitions(out, *classDecl);
    }

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || !fn->hasBody || fn->isInline || !fn->templateParams.empty())
            continue;

        if(fn->returnType)
            m_headerEmitter.emitTypeCpp(out, *fn->returnType);
        else
            out.append("void");
        out.push_back(' ');
        out.append(fn->name.data(), fn->name.size());
        out.push_back('(');
        for(usize i = 0u; i < fn->params.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const ParamDecl& p = fn->params[i];
            if(p.type)
                m_headerEmitter.emitParamTypeCpp(out, p.mode, *p.type);
            out.push_back(' ');
            out.append(p.name.data(), p.name.size());
        }
        out.append(")noexcept{\r\n");
        emitMethodBodyBlock(out, nullptr, *fn, 1u);
        out.append("}\r\n\r\n");
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}

void SourceEmitter::emitMethodBodyBlock(
    MString& out,
    const ClassDecl* ownerClass,
    const MethodDecl& method,
    const u32 indent
)const{
    if(method.usesScratchArena){
        emitIndent(out, indent);
        out.append("::NWB::Core::Alloc::ScratchArena<> __adhd_scratch;\r\n");
    }
    if(method.usesTaskScope){
        emitIndent(out, indent);
        out.append("static ::NWB::Core::CpuTaskScheduler s_AdhdTaskScheduler;\r\n");
        emitIndent(out, indent);
        out.append("::NWB::Core::CpuTaskScope __adhd_task_scope(s_AdhdTaskScheduler);\r\n");
        emitIndent(out, indent);
        out.append("::ScopeExit __adhd_scope_join([&__adhd_task_scope]()noexcept{ __adhd_task_scope.wait(); });\r\n");
    }

    for(const Stmt* stmt : method.body){
        if(stmt)
            emitStmt(out, ownerClass, method, *stmt, indent);
    }
}

void SourceEmitter::emitStmt(
    MString& out,
    const ClassDecl* ownerClass,
    const MethodDecl& method,
    const Stmt& stmt,
    const u32 indent
)const{
    switch(stmt.kind){
    case StmtKind::Block:{
        emitIndent(out, indent);
        out.append("{\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::VarDecl:{
        emitIndent(out, indent);
        if(stmt.isConst)
            out.append("const ");
        if(stmt.varType)
            m_headerEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.push_back(' ');
        out.append(stmt.name.data(), stmt.name.size());

        if(stmt.varType && stmt.varType->refMode != ParamPassingMode::None && stmt.expr){
            out.append(" = ::MakeNotNull(&(");
            emitExpr(out, *stmt.expr);
            out.append("));\r\n");
        }
        else if(stmt.isCtorInit){
            out.push_back('(');
            for(usize i = 0u; i < stmt.arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(stmt.arguments[i])
                    emitExpr(out, *stmt.arguments[i]);
            }
            if(stmt.usesScratchArena){
                if(!stmt.arguments.empty())
                    out.append(", ");
                out.append("__adhd_scratch");
            }
            out.append(");\r\n");
        }
        else if(stmt.expr){
            out.append(" = ");
            emitExpr(out, *stmt.expr);
            out.append(";\r\n");
        }
        else if(stmt.usesScratchArena){
            out.append("(__adhd_scratch);\r\n");
        }
        else{
            out.append("{};\r\n");
        }
        break;
    }
    case StmtKind::ExprStmt:{
        emitIndent(out, indent);
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(";\r\n");
        break;
    }
    case StmtKind::Return:{
        emitIndent(out, indent);
        out.append("return");
        if(stmt.expr){
            out.push_back(' ');
            emitExpr(out, *stmt.expr);
        }
        out.append(";\r\n");
        break;
    }
    case StmtKind::Break:{
        emitIndent(out, indent);
        out.append("break;\r\n");
        break;
    }
    case StmtKind::Continue:{
        emitIndent(out, indent);
        out.append("continue;\r\n");
        break;
    }
    case StmtKind::If:{
        emitIndent(out, indent);
        out.append("if(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append("){\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
        if(!stmt.elseStatements.empty()){
            emitIndent(out, indent);
            out.append("else{\r\n");
            for(const Stmt* child : stmt.elseStatements){
                if(child)
                    emitStmt(out, ownerClass, method, *child, indent + 1u);
            }
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        break;
    }
    case StmtKind::Switch:{
        if(stmt.lowerSwitchToIfChain){
            bool firstBranch = true;
            for(const SwitchCase& sc : stmt.switchCases){
                emitIndent(out, indent);
                if(sc.isDefault){
                    out.append("else{\r\n");
                }
                else{
                    out.append(firstBranch ? "if(" : "else if(");
                    if(stmt.expr)
                        emitExpr(out, *stmt.expr);
                    out.append(" == ");
                    if(sc.matchExpr)
                        emitExpr(out, *sc.matchExpr);
                    out.append("){\r\n");
                    firstBranch = false;
                }
                for(const Stmt* child : sc.statements){
                    if(child && child->kind != StmtKind::Break)
                        emitStmt(out, ownerClass, method, *child, indent + 1u);
                }
                emitIndent(out, indent);
                out.append("}\r\n");
            }
        }
        else{
            emitIndent(out, indent);
            out.append("switch(");
            if(stmt.expr)
                emitExpr(out, *stmt.expr);
            out.append("){\r\n");
            for(const SwitchCase& sc : stmt.switchCases){
                emitIndent(out, indent);
                if(sc.isDefault){
                    out.append("default:{\r\n");
                }
                else{
                    out.append("case ");
                    if(sc.matchExpr)
                        emitExpr(out, *sc.matchExpr);
                    out.append(":{\r\n");
                }
                for(const Stmt* child : sc.statements){
                    if(child)
                        emitStmt(out, ownerClass, method, *child, indent + 1u);
                }
                emitIndent(out, indent);
                out.append("}\r\n");
            }
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        break;
    }
    case StmtKind::ForRange:{
        emitIndent(out, indent);
        out.append("for(");
        if(stmt.rangeMode == ParamPassingMode::In)
            out.append("const ");
        if(stmt.varType)
            m_headerEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.append("& ");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" : ");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append("){\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
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
            m_headerEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.append("& ");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" = (");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(")[__adhd_idx];\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}, ");
        __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ForIndex:{
        emitIndent(out, indent);
        out.append("for(");
        if(stmt.initStmt && stmt.initStmt->varType){
            m_headerEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.push_back(' ');
            out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
            out.append(" = ");
            if(stmt.initStmt->expr)
                emitExpr(out, *stmt.initStmt->expr);
        }
        out.append("; ");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append("; ");
        if(stmt.stepExpr)
            emitExpr(out, *stmt.stepExpr);
        out.append("){\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
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
        if(stmt.condition && stmt.condition->kind == ExprKind::Binary && stmt.condition->rhs)
            emitExpr(out, *stmt.condition->rhs);
        else if(stmt.condition)
            emitExpr(out, *stmt.condition);
        else
            out.append("0u");
        out.append("), 1u, [&](const usize __adhd_idx)noexcept{\r\n");
        if(stmt.initStmt && stmt.initStmt->varType){
            emitIndent(out, indent + 1u);
            out.append("const ");
            m_headerEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.push_back(' ');
            out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
            out.append(" = static_cast<");
            m_headerEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.append(">(__adhd_idx);\r\n");
        }
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}, ");
        __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
        out.append(");\r\n");
        break;
    }
    case StmtKind::While:{
        emitIndent(out, indent);
        out.append("while(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append("){\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::DoWhile:{
        emitIndent(out, indent);
        out.append("do{\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}while(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
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
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}, ");
        __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
        if(!stmt.parallelDependencies.empty()){
            out.append(", __adhd_deps_");
            out.append(stmt.name.data(), stmt.name.size());
            out.append(", ");
            __hidden_adhd_source_emitter::AppendUnsigned(out, stmt.parallelDependencies.size());
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
        emitIndent(out, indent);
        out.append("(void)__adhd_task_scope.submit([=]()noexcept{ ");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append("; }, ");
        __hidden_adhd_source_emitter::AppendTaskOptions(out, stmt);
        out.append(");\r\n");
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
        out.append(stmt.nativeCode.data(), stmt.nativeCode.size());
        out.append("\r\n");
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
    }
}

void SourceEmitter::emitExpr(MString& out, const Expr& expr)const{
    if(expr.moveAtLastUse){
        out.append("::Move(");
        out.append(expr.text.data(), expr.text.size());
        out.push_back(')');
        return;
    }

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:{
        if(expr.derefNotNull){
            out.append("(*");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::StringLiteral:{
        if(expr.adaptToTChar){
            out.append("NWB_TEXT(\"");
            out.append(expr.text.data(), expr.text.size());
            out.append("\")");
        }
        else{
            out.push_back('"');
            out.append(expr.text.data(), expr.text.size());
            out.push_back('"');
        }
        break;
    }
    case ExprKind::ThisMember:{
        out.append("this->");
        out.append(expr.text.data(), expr.text.size());
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isPointerMemberAccess)
            out.append("->");
        else
            out.push_back('.');
        out.append(expr.text.data(), expr.text.size());
        if(expr.isSwizzle)
            out.append("()");
        break;
    }
    case ExprKind::ScopeAccess:{
        for(usize i = 0u; i < expr.scopeChain.size(); ++i){
            if(i > 0u)
                out.append("::");
            out.append(expr.scopeChain[i].data(), expr.scopeChain[i].size());
        }
        break;
    }
    case ExprKind::Index:{
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back('[');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(']');
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs){
            if(expr.lhs->kind == ExprKind::Identifier){
                const MStringView mappedMath = __hidden_adhd_source_emitter::MapBuiltinMathFunction(expr.lhs->text);
                if(!mappedMath.empty()){
                    out.append(mappedMath.data(), mappedMath.size());
                }
                else{
                    TypeRef maybeType(m_arena);
                    maybeType.name = expr.lhs->text;
                    m_headerEmitter.emitTypeCpp(out, maybeType);
                }
            }
            else{
                emitExpr(out, *expr.lhs);
            }
        }
        if(!expr.callTemplateArgs.empty()){
            out.push_back('<');
            for(usize i = 0u; i < expr.callTemplateArgs.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                const TypeArg& targ = expr.callTemplateArgs[i];
                if(targ.isConstant || !targ.constantText.empty())
                    out.append(targ.constantText.data(), targ.constantText.size());
                else if(targ.type)
                    m_headerEmitter.emitTypeCpp(out, *targ.type);
            }
            out.push_back('>');
        }
        out.push_back('(');
        for(usize i = 0u; i < expr.arguments.size(); ++i){
            if(i > 0u)
                out.append(", ");
            if(expr.arguments[i])
                emitExpr(out, *expr.arguments[i]);
        }
        out.push_back(')');
        break;
    }
    case ExprKind::Unary:{
        if(expr.isPostfix){
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(expr.text.data(), expr.text.size());
        }
        else{
            out.append(expr.text.data(), expr.text.size());
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.isNotNullRebind){
            out.push_back('(');
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(" = ::MakeNotNull(&(");
            if(expr.rhs)
                emitExpr(out, *expr.rhs);
            out.append(")))");
            break;
        }
        out.push_back('(');
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(' ');
        out.append(expr.text.data(), expr.text.size());
        out.push_back(' ');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Cast:{
        out.append("static_cast<");
        if(expr.targetType)
            m_headerEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::BitCast:{
        out.append("::BitCast<");
        if(expr.targetType)
            m_headerEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Lambda:{
        out.push_back('[');
        for(usize i = 0u; i < expr.lambdaCaptures.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const LambdaCapture& cap = expr.lambdaCaptures[i];
            if(cap.isThis){
                out.append("this");
            }
            else if(cap.mode == ParamPassingMode::Mut){
                out.push_back('&');
                out.append(cap.name.data(), cap.name.size());
            }
            else{
                out.append(cap.name.data(), cap.name.size());
            }
        }
        out.append("](");
        for(usize i = 0u; i < expr.lambdaParams.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const ParamDecl& p = expr.lambdaParams[i];
            if(p.type)
                m_headerEmitter.emitParamTypeCpp(out, p.mode, *p.type);
            out.push_back(' ');
            out.append(p.name.data(), p.name.size());
        }
        out.append(")noexcept -> ");
        if(expr.lambdaReturnType)
            m_headerEmitter.emitTypeCpp(out, *expr.lambdaReturnType);
        else
            out.append("void");
        out.append("{\r\n");
        MethodDecl dummyMethod(m_arena);
        for(const Stmt* s : expr.lambdaBody){
            if(s)
                emitStmt(out, nullptr, dummyMethod, *s, 2u);
        }
        out.append("    }");
        break;
    }
    }
}

void SourceEmitter::emitIndent(MString& out, const u32 indent)const{
    for(u32 i = 0u; i < indent; ++i)
        out.append("    ");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

