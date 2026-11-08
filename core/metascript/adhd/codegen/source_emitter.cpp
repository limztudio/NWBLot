// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_statement_emitter.h"
#include "source_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const Expr* FindConstructorFieldAssignment(const Span<const Stmt* const> stmts, const MStringView fieldName, const Expr*& outUniqueTopLevelAssignExpr)noexcept{
    const Expr* firstFound = nullptr;
    usize totalMatches = 0u;
    b8 firstIsTopLevel = false;

    const auto countRecursive = [&](const auto& self, const Span<const Stmt* const> list, const b8 topLevel)noexcept -> void{
        for(const Stmt* stmt : list){
            if(!stmt)
                continue;
            if(IsThisMemberSimpleAssignStmt(*stmt, fieldName) && stmt->expr->rhs){
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
    outUniqueTopLevelAssignExpr = (firstFound != nullptr && firstIsTopLevel && totalMatches == 1u) ? firstFound : nullptr;
    return firstFound ? firstFound->rhs : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString SourceEmitter::emitSource(const TranslationUnit& unit, const MStringView headerIncludePath, const MStringView namespacePath)const{
    MString out(m_arena);
    out.reserve(s_AdhdEmitReserveBytes);

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

                emitOutOfClassCallable(out, classDecl, *method, true, false);
            }
        }

        if(!classDecl->isImported && classDecl->templateParams.empty() && (classDecl->isSerialize || classDecl->isReflect || !classDecl->assetTypeName.empty()))
            reflectionEmitter.emitClassReflectionDefinitions(out, *classDecl);
    }

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported || !fn->hasBody || fn->isInline || !fn->templateParams.empty())
            continue;

        emitOutOfClassCallable(out, nullptr, *fn, true, false);
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}

void SourceEmitter::emitOutOfClassCallable(MString& out, const ClassDecl* ownerClass, const MethodDecl& callable, const b8 emitBody, const b8 emitInlineKeyword)const{
    if(ownerClass && !ownerClass->templateParams.empty())
        m_typeEmitter.emitTemplateHeaderCpp(out, ownerClass->templateParams);
    m_typeEmitter.emitTemplateHeaderCpp(out, callable.templateParams);
    if(emitBody && emitInlineKeyword)
        out.append("inline ");

    m_typeEmitter.emitMethodSignature(out, ownerClass, callable, false);
    if(emitBody){
        if(ownerClass && callable.isConstructor)
            emitConstructorNotNullInitializers(out, ownerClass->orderedFields, callable);
        out.append("{\r\n");
        emitMethodBodyBlock(out, callable, 1u);
        out.append("}\r\n\r\n");
    }
    else{
        out.append(";\r\n\r\n");
    }
}

void SourceEmitter::emitMethodBodyBlock(MString& out, const MethodDecl& method, const u32 indent)const{
    if(method.usesScratchArena)
        emitScratchArenaPrologue(out, indent, method.name);
    if(method.usesTaskScope)
        ParallelStatementEmitter(*this).emitTaskScopePrologue(out, indent);

    const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
    m_activeSwitchBreakLabelLine = 0u;
    m_activeLocalVarDecls.clear();
    m_remainingBlockStatements = {};
    emitStatementList(out, method.body, indent);
    m_activeLocalVarDecls.clear();
    m_remainingBlockStatements = {};
    m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;
    m_skippedCtorAssignExprs = nullptr;
}

void SourceEmitter::emitConstructorNotNullInitializers(MString& out, const Span<const FieldDecl* const> orderedFields, const MethodDecl& ctor)const{
    m_skippedCtorAssignExprs = nullptr;
    if(!ctor.isConstructor || !ctor.hasBody)
        return;

    MVector<const Expr*>* skipped = AllocateAstNode<MVector<const Expr*>>(m_arena);
    b8 firstInit = true;
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type || m_symbols.resolveRefMode(field->type) == ParamPassingMode::None || field->defaultExpr || field->hasCtorInit)
            continue;
        const Expr* uniqueAssign = nullptr;
        const Expr* rhs = __hidden_adhd_source_emitter::FindConstructorFieldAssignment(ctor.body, field->name, uniqueAssign);
        if(!rhs)
            continue;
        if(uniqueAssign)
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

void SourceEmitter::emitScratchArenaPrologue(MString& out, const u32 indent, const MStringView arenaName)const{
    emitIndent(out, indent);
    out.append("static ::NWB::Core::Alloc::GlobalArena __adhd_scratch(::Name(\"");
    out.append(arenaName.data(), arenaName.size());
    out.append("\"));\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
