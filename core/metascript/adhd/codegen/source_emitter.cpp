// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const Expr* FindConstructorFieldAssignment(const MVector<Stmt*>& stmts, const MStringView fieldName)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(stmt->kind == StmtKind::ExprStmt
            && stmt->expr
            && stmt->expr->kind == ExprKind::Binary
            && stmt->expr->op == TokenType::Equal
            && stmt->expr->lhs
            && stmt->expr->lhs->kind == ExprKind::ThisMember
            && stmt->expr->lhs->text == fieldName
            && stmt->expr->rhs)
        {
            return stmt->expr->rhs;
        }
        if(stmt->kind == StmtKind::Block || stmt->kind == StmtKind::If){
            if(const Expr* nested = FindConstructorFieldAssignment(stmt->statements, fieldName))
                return nested;
            if(const Expr* nestedElse = FindConstructorFieldAssignment(stmt->elseStatements, fieldName))
                return nestedElse;
        }
        else if(stmt->kind == StmtKind::Switch){
            for(const SwitchCase& sc : stmt->switchCases){
                if(const Expr* nestedCase = FindConstructorFieldAssignment(sc.statements, fieldName))
                    return nestedCase;
            }
        }
    }
    return nullptr;
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

    ReflectionEmitter reflectionEmitter(m_symbols);

    out.append(s_AdhdGeneratedFileBanner.data(), s_AdhdGeneratedFileBanner.size());
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
                if(!method)
                    continue;
                const b8 emittedInHeader = method->isInline || !method->templateParams.empty();
                if(method->isImported || !method->hasBody || emittedInHeader)
                    continue;

                if(method->isNoInline)
                    out.append("NWB_NOINLINE ");

                m_typeEmitter.emitMethodSignature(out, classDecl, *method, false);
                if(method->isConstructor)
                    emitConstructorNotNullInitializers(out, classDecl->orderedFields, *method);
                out.append("{\r\n");
                emitMethodBodyBlock(out, *method, 1u);
                out.append("}\r\n\r\n");
            }
        }

        if(!classDecl->isImported && classDecl->templateParams.empty() && (classDecl->isSerialize || classDecl->isReflect || !classDecl->assetTypeName.empty()))
            reflectionEmitter.emitClassReflectionDefinitions(out, *classDecl);
    }

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported || !fn->hasBody || fn->isInline || !fn->templateParams.empty())
            continue;

        if(fn->isNoInline)
            out.append("NWB_NOINLINE ");

        m_typeEmitter.emitMethodSignature(out, nullptr, *fn, false);
        out.append("{\r\n");
        emitMethodBodyBlock(out, *fn, 1u);
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
    const MethodDecl& method,
    const u32 indent
)const{
    if(method.usesScratchArena){
        emitIndent(out, indent);
        out.append("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"");
        out.append(method.name.data(), method.name.size());
        out.append("\"));\r\n");
    }
    if(method.usesTaskScope)
        emitTaskScopePrologue(out, indent);

    emitScopedSimdBody(out, method.body, indent);
}

void SourceEmitter::emitConstructorNotNullInitializers(
    MString& out,
    const MVector<FieldDecl*>& orderedFields,
    const MethodDecl& ctor
)const{
    if(!ctor.isConstructor || !ctor.hasBody)
        return;

    b8 firstInit = true;
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type || m_symbols.resolveRefMode(field->type) == ParamPassingMode::None || field->defaultExpr || field->hasCtorInit)
            continue;
        const Expr* rhs = __hidden_adhd_source_emitter::FindConstructorFieldAssignment(ctor.body, field->name);
        if(!rhs)
            continue;
        out.append(firstInit ? " : " : ", ");
        firstInit = false;
        out.append(field->name.data(), field->name.size());
        out.append("(::MakeNotNull(&(");
        emitExpr(out, *rhs);
        out.append(")))");
    }
}


void SourceEmitter::emitIndent(MString& out, const u32 indent)const{
    for(u32 i = 0u; i < indent; ++i)
        out.append("    ");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

