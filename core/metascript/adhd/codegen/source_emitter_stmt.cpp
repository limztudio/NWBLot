// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_statement_emitter.h"
#include "constructor_argument_emitter.h"
#include "source_emitter.h"
#include "switch_statement_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitStatementList(MString& out, const Span<const Stmt* const> statements, const u32 indent)const{
    const usize prevLocalDeclCount = m_activeLocalVarDecls.size();
    const Span<const Stmt* const> prevRemaining = m_remainingBlockStatements;
    for(usize i = 0u; i < statements.size(); ++i){
        const Stmt* stmt = statements[i];
        if(!stmt)
            continue;
        m_remainingBlockStatements = Span<const Stmt* const>(statements.data() + (i + 1u), statements.size() - (i + 1u));
        emitStmt(out, *stmt, indent);
        if(stmt->kind == StmtKind::VarDecl && !stmt->name.empty())
            m_activeLocalVarDecls.push_back(stmt);
    }
    m_remainingBlockStatements = prevRemaining;
    m_activeLocalVarDecls.resize(prevLocalDeclCount);
}

void SourceEmitter::emitLoopBodyStatements(MString& out, const Span<const Stmt* const> statements, const u32 indent)const{
    const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
    m_activeSwitchBreakLabelLine = 0u;
    emitStatementList(out, statements, indent);
    m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;
}

void SourceEmitter::emitStmt(MString& out, const Stmt& stmt, const u32 indent)const{
    switch(stmt.kind){
    case StmtKind::Block:{
        emitIndent(out, indent);
        out.append("{\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::VarDecl:{
        if(tryEmitSimdVarDeclStmt(out, stmt, indent))
            break;
        emitIndent(out, indent);
        if(stmt.isConst)
            out.append("const ");
        if(stmt.varType)
            m_typeEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.push_back(' ');
        out.append(stmt.name.data(), stmt.name.size());

        if(stmt.varType && m_symbols.resolveRefMode(stmt.varType) != ParamPassingMode::None && stmt.expr){
            out.append(" = ::MakeNotNull(&(");
            emitExpr(out, *stmt.expr);
            out.append("));\r\n");
        }
        else if(stmt.isCtorInit){
            const MStringView baseVarType = stmt.varType ? m_symbols.resolveBaseTypeName(stmt.varType->name) : MStringView{};
            ConstructorArgumentEmitter(*this).emit(
                out,
                baseVarType,
                stmt.arguments,
                stmt.usesScratchArena ? MStringView("__adhd_scratch") : MStringView{},
                false,
                true
            );
            out.append(";\r\n");
        }
        else if(stmt.expr){
            if(stmt.usesScratchArena && stmt.expr->kind == ExprKind::StringLiteral){
                out.push_back('(');
                emitExpr(out, *stmt.expr);
                out.append(", __adhd_scratch);\r\n");
            }
            else{
                out.append(" = ");
                emitExpr(out, *stmt.expr);
                out.append(";\r\n");
            }
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
        if(m_skippedCtorAssignExprs && stmt.expr){
            b8 skip = false;
            for(const Expr* skippedExpr : *m_skippedCtorAssignExprs){
                if(skippedExpr == stmt.expr){
                    skip = true;
                    break;
                }
            }
            if(skip)
                break;
        }
        if(tryEmitSimdAssignStmt(out, stmt, indent))
            break;
        if(ParallelStatementEmitter(*this).tryEmitParallelWaitStmt(out, stmt, indent))
            break;

        emitIndent(out, indent);
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(";\r\n");
        break;
    }
    case StmtKind::Return:{
        if(tryEmitSimdReturnStmt(out, stmt, indent))
            break;
        emitIndent(out, indent);
        out.append("return");
        if(stmt.expr){
            if(stmt.wrapReturnNotNull){
                out.append(" ::MakeNotNull(&(");
                emitExpr(out, *stmt.expr);
                out.append("))");
            }
            else{
                out.push_back(' ');
                emitExpr(out, *stmt.expr);
            }
        }
        out.append(";\r\n");
        break;
    }
    case StmtKind::Break:{
        emitIndent(out, indent);
        if(m_activeSwitchBreakLabelLine != 0u){
            out.append("goto __adhd_switch_break_");
            AppendUnsignedDecimal(out, m_activeSwitchBreakLabelLine);
            out.append(";\r\n");
        }
        else{
            out.append("break;\r\n");
        }
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
            emitExpr(out, *stmt.condition, true);
        out.append("){\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        if(!stmt.elseStatements.empty()){
            emitIndent(out, indent);
            out.append("else{\r\n");
            emitStatementList(out, stmt.elseStatements, indent + 1u);
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        break;
    }
    case StmtKind::Switch:{
        SwitchStatementEmitter(*this).emit(out, stmt, indent);
        break;
    }
    case StmtKind::ForRange:{
        emitIndent(out, indent);
        out.append("for(");
        if(stmt.rangeMode == ParamPassingMode::In)
            out.append("const ");
        if(stmt.varType)
            m_typeEmitter.emitTypeCpp(out, *stmt.varType);
        else
            out.append("auto");
        out.append("& ");
        out.append(stmt.name.data(), stmt.name.size());
        out.append(" : ");
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append("){\r\n");
        const usize prevForRangeDecls = m_activeLocalVarDecls.size();
        if(!stmt.name.empty())
            m_activeLocalVarDecls.push_back(&stmt);
        emitLoopBodyStatements(out, stmt.statements, indent + 1u);
        m_activeLocalVarDecls.resize(prevForRangeDecls);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::ForIndex:{
        emitIndent(out, indent);
        out.append("for(");
        const usize prevForIdxDecls = m_activeLocalVarDecls.size();
        if(stmt.initStmt && stmt.initStmt->varType){
            m_typeEmitter.emitTypeCpp(out, *stmt.initStmt->varType);
            out.push_back(' ');
            out.append(stmt.initStmt->name.data(), stmt.initStmt->name.size());
            out.append(" = ");
            if(stmt.initStmt->expr)
                emitExpr(out, *stmt.initStmt->expr);
            else if(stmt.initStmt->isCtorInit && !stmt.initStmt->arguments.empty() && stmt.initStmt->arguments[0])
                emitExpr(out, *stmt.initStmt->arguments[0]);
            else
                out.append("0");
            if(!stmt.initStmt->name.empty())
                m_activeLocalVarDecls.push_back(&stmt);
        }
        else if(stmt.initStmt && stmt.initStmt->kind == StmtKind::ExprStmt && stmt.initStmt->expr){
            emitExpr(out, *stmt.initStmt->expr);
        }
        out.append("; ");
        if(stmt.condition)
            emitExpr(out, *stmt.condition, true);
        out.append("; ");
        if(stmt.stepExpr)
            emitExpr(out, *stmt.stepExpr);
        out.append("){\r\n");
        emitLoopBodyStatements(out, stmt.statements, indent + 1u);
        m_activeLocalVarDecls.resize(prevForIdxDecls);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::While:{
        emitIndent(out, indent);
        out.append("while(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition, true);
        out.append("){\r\n");
        emitLoopBodyStatements(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::DoWhile:{
        emitIndent(out, indent);
        out.append("do{\r\n");
        emitLoopBodyStatements(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}while(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition, true);
        out.append(");\r\n");
        break;
    }
    case StmtKind::ParallelForRange:
    case StmtKind::ParallelForIndex:
    case StmtKind::ParallelBlock:
    case StmtKind::ParallelCall:{
        ParallelStatementEmitter(*this).emitParallelStmt(out, stmt, indent);
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
        AppendTextDiscardCrAndExpandLf(out, stmt.nativeCode);
        out.append("\r\n");
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::Diagnostic:{
        emitIndent(out, indent);
        MStringView macroPrefix;
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, enumName, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
        if(stmt.diagnosticKind == DiagnosticKind::enumName){ \
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
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
