// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const Expr* FindConstructorFieldAssignment(const MVector<Stmt*>& stmts, const MStringView fieldName, b8* outIsTopLevelUnique = nullptr)noexcept{
    const Expr* firstFound = nullptr;
    usize totalMatches = 0u;
    b8 firstIsTopLevel = false;

    auto countRecursive = [&](auto& self, const MVector<Stmt*>& list, const b8 topLevel)noexcept -> void{
        for(const Stmt* stmt : list){
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
                if(!firstFound){
                    firstFound = stmt->expr;
                    firstIsTopLevel = topLevel;
                }
                ++totalMatches;
            }
            if(stmt->kind == StmtKind::Block || stmt->kind == StmtKind::If){
                self(self, stmt->statements, false);
                self(self, stmt->elseStatements, false);
            }
            else if(stmt->kind == StmtKind::Switch){
                for(const SwitchCase& sc : stmt->switchCases)
                    self(self, sc.statements, false);
            }
        }
    };

    countRecursive(countRecursive, stmts, true);
    if(outIsTopLevelUnique)
        *outIsTopLevelUnique = (firstFound != nullptr && firstIsTopLevel && totalMatches == 1u);
    return firstFound ? firstFound->rhs : nullptr;
}

[[nodiscard]] const Expr* FindUniqueTopLevelCtorAssignExpr(const MVector<Stmt*>& stmts, const MStringView fieldName)noexcept{
    b8 isTopLevelUnique = false;
    (void)FindConstructorFieldAssignment(stmts, fieldName, &isTopLevelUnique);
    if(!isTopLevelUnique)
        return nullptr;
    for(const Stmt* stmt : stmts){
        if(stmt
            && stmt->kind == StmtKind::ExprStmt
            && stmt->expr
            && stmt->expr->kind == ExprKind::Binary
            && stmt->expr->op == TokenType::Equal
            && stmt->expr->lhs
            && stmt->expr->lhs->kind == ExprKind::ThisMember
            && stmt->expr->lhs->text == fieldName
            && stmt->expr->rhs)
        {
            return stmt->expr;
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

    const b8 prevTaskScope = m_activeScopeUsesTaskScope;
    m_activeScopeUsesTaskScope = method.usesTaskScope;
    emitScopedSimdBody(out, method.body, indent);
    m_activeScopeUsesTaskScope = prevTaskScope;
    m_skippedCtorAssignExprs = nullptr;
}

void SourceEmitter::emitConstructorNotNullInitializers(
    MString& out,
    const MVector<FieldDecl*>& orderedFields,
    const MethodDecl& ctor
)const{
    m_skippedCtorAssignExprs = nullptr;
    if(!ctor.isConstructor || !ctor.hasBody)
        return;

    MVector<const Expr*>* skipped = AllocateAstNode<MVector<const Expr*>>(m_arena);
    b8 firstInit = true;
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type || m_symbols.resolveRefMode(field->type) == ParamPassingMode::None || field->defaultExpr || field->hasCtorInit)
            continue;
        const Expr* rhs = __hidden_adhd_source_emitter::FindConstructorFieldAssignment(ctor.body, field->name);
        if(!rhs)
            continue;
        if(const Expr* uniqueAssign = __hidden_adhd_source_emitter::FindUniqueTopLevelCtorAssignExpr(ctor.body, field->name))
            skipped->push_back(uniqueAssign);
        out.append(firstInit ? " : " : ", ");
        firstInit = false;
        out.append(field->name.data(), field->name.size());
        out.append("(::MakeNotNull(&(");
        emitExpr(out, *rhs);
        out.append(")))");
    }
    if(!skipped->empty())
        m_skippedCtorAssignExprs = skipped;
}


void SourceEmitter::emitIndent(MString& out, const u32 indent)const{
    for(u32 i = 0u; i < indent; ++i)
        out.append("    ");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

