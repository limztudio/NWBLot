// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


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
    out.append("#include <core/common/log.h>\r\n\r\n\r\n");
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
                    out.append("const");
                out.append("{\r\n");
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
        out.append("){\r\n");
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
            out.append(" = ::NWB::MakeNotNull(&(");
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
    case StmtKind::ForRange:
    case StmtKind::ParallelForRange:{
        emitIndent(out, indent);
        if(stmt.kind == StmtKind::ParallelForRange)
            out.append("/* parallel_for */ ");
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
    case StmtKind::ForIndex:
    case StmtKind::ParallelForIndex:{
        emitIndent(out, indent);
        if(stmt.kind == StmtKind::ParallelForIndex)
            out.append("/* parallel_for */ ");
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
        emitIndent(out, indent);
        out.append("/* parallel ");
        out.append(stmt.name.data(), stmt.name.size());
        if(!stmt.parallelDependencies.empty()){
            out.append(" : ");
            for(usize i = 0u; i < stmt.parallelDependencies.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                out.append(stmt.parallelDependencies[i].data(), stmt.parallelDependencies[i].size());
            }
        }
        out.append(" */{\r\n");
        for(const Stmt* child : stmt.statements){
            if(child)
                emitStmt(out, ownerClass, method, *child, indent + 1u);
        }
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::ParallelCall:{
        emitIndent(out, indent);
        out.append("/* parallel call */ ");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(";\r\n");
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
        out.append("::NWB::Move(");
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
        out.append(expr.text.data(), expr.text.size());
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
                TypeRef maybeType(m_arena);
                maybeType.name = expr.lhs->text;
                m_headerEmitter.emitTypeCpp(out, maybeType);
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
        out.append("::NWB::BitCast<");
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
        out.append(") -> ");
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

