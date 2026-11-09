// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "switch_statement_emitter.h"


NWB_ADHD_BEGIN


namespace __hidden_adhd_source_emitter_stmt{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 StatementsContainBreak(const Span<const Stmt* const> statements)noexcept{
    for(const Stmt* s : statements){
        if(!s)
            continue;
        if(s->kind == StmtKind::Break)
            return true;
        if(s->kind == StmtKind::Block || s->kind == StmtKind::If){
            if(StatementsContainBreak(s->statements) || StatementsContainBreak(s->elseStatements))
                return true;
        }
    }
    return false;
}

[[nodiscard]] b8 StatementsContainOuterLoopContinue(const Span<const Stmt* const> statements)noexcept{
    for(const Stmt* s : statements){
        if(!s)
            continue;
        if(s->kind == StmtKind::Continue)
            return true;
        if(s->kind == StmtKind::Block || s->kind == StmtKind::If){
            if(StatementsContainOuterLoopContinue(s->statements) || StatementsContainOuterLoopContinue(s->elseStatements))
                return true;
        }
        else if(s->kind == StmtKind::Switch){
            for(const SwitchCase& sc : s->switchCases){
                if(StatementsContainOuterLoopContinue(sc.statements))
                    return true;
            }
        }
    }
    return false;
}

[[nodiscard]] b8 CaseNeedsDoWhileBreakLowering(const Span<const Stmt* const> statements)noexcept{
    for(usize i = 0u; i < statements.size(); ++i){
        const Stmt* s = statements[i];
        if(!s)
            continue;
        const b8 isLast = (i + 1u == statements.size())
            || (i + 2u == statements.size() && statements.back() && statements.back()->kind == StmtKind::Break);
        if(s->kind == StmtKind::Break && !isLast)
            return true;
        if(s->kind == StmtKind::Block || s->kind == StmtKind::If){
            if(!isLast && (StatementsContainBreak(s->statements) || StatementsContainBreak(s->elseStatements)))
                return true;
            if(CaseNeedsDoWhileBreakLowering(s->statements) || CaseNeedsDoWhileBreakLowering(s->elseStatements))
                return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


void SwitchStatementEmitter::emitIfChainStatements(MString& out, const Span<const Stmt* const> statements, const u32 indent)const{
    const usize prevLocalDeclCount = m_emitter.m_activeLocalVarDecls.size();
    const Span<const Stmt* const> prevRemaining = m_emitter.m_remainingBlockStatements;
    for(usize i = 0u; i < statements.size(); ++i){
        const Stmt* child = statements[i];
        if(!child)
            continue;
        const b8 isTrailingBreak = (child->kind == StmtKind::Break) && (i + 1u == statements.size());
        if(isTrailingBreak)
            continue;
        m_emitter.m_remainingBlockStatements = Span<const Stmt* const>(statements.data() + (i + 1u), statements.size() - (i + 1u));
        if(child->kind == StmtKind::Block){
            m_emitter.emitIndent(out, indent);
            out.append("{\r\n");
            if(i + 1u == statements.size() || (i + 2u == statements.size() && statements.back() && statements.back()->kind == StmtKind::Break))
                emitIfChainStatements(out, child->statements, indent + 1u);
            else
                m_emitter.emitStatementList(out, child->statements, indent + 1u);
            m_emitter.emitIndent(out, indent);
            out.append("}\r\n");
        }
        else if(child->kind == StmtKind::If){
            const b8 isLastInCase = (i + 1u == statements.size())
                || (i + 2u == statements.size() && statements.back() && statements.back()->kind == StmtKind::Break);
            m_emitter.emitIndent(out, indent);
            out.append("if(");
            if(child->condition)
                m_emitter.emitExpr(out, *child->condition, true);
            out.append("){\r\n");
            if(isLastInCase)
                emitIfChainStatements(out, child->statements, indent + 1u);
            else
                m_emitter.emitStatementList(out, child->statements, indent + 1u);
            m_emitter.emitIndent(out, indent);
            out.append("}\r\n");
            if(!child->elseStatements.empty()){
                m_emitter.emitIndent(out, indent);
                out.append("else{\r\n");
                if(isLastInCase)
                    emitIfChainStatements(out, child->elseStatements, indent + 1u);
                else
                    m_emitter.emitStatementList(out, child->elseStatements, indent + 1u);
                m_emitter.emitIndent(out, indent);
                out.append("}\r\n");
            }
        }
        else{
            m_emitter.emitStmt(out, *child, indent);
            if(child->kind == StmtKind::VarDecl && !child->name.empty())
                m_emitter.m_activeLocalVarDecls.push_back(child);
        }
    }
    m_emitter.m_remainingBlockStatements = prevRemaining;
    m_emitter.m_activeLocalVarDecls.resize(prevLocalDeclCount);
}


void SwitchStatementEmitter::emit(MString& out, const Stmt& stmt, const u32 indent)const{
    if(stmt.lowerSwitchToIfChain){
        b8 needsEarlyBreakLowering = false;
        b8 hasOuterContinue = false;
        for(const SwitchCase& sc : stmt.switchCases){
            if(__hidden_adhd_source_emitter_stmt::CaseNeedsDoWhileBreakLowering(sc.statements))
                needsEarlyBreakLowering = true;
            if(__hidden_adhd_source_emitter_stmt::StatementsContainOuterLoopContinue(sc.statements))
                hasOuterContinue = true;
        }
        const b8 needsDoWhileWrap = needsEarlyBreakLowering && !hasOuterContinue;
        const b8 needsBreakLabel = needsEarlyBreakLowering && hasOuterContinue;
        const b8 cacheSubject = stmt.expr
            && stmt.expr->kind != ExprKind::Identifier
            && stmt.expr->kind != ExprKind::ThisMember;
        const u32 bodyIndent = (cacheSubject || needsDoWhileWrap) ? (indent + 1u) : indent;
        if(needsDoWhileWrap){
            m_emitter.emitIndent(out, indent);
            out.append("do{\r\n");
            if(cacheSubject){
                m_emitter.emitIndent(out, bodyIndent);
                out.append("const auto& __adhd_switch_subject = (");
                m_emitter.emitExpr(out, *stmt.expr);
                out.append(");\r\n");
            }
        }
        else if(cacheSubject){
            m_emitter.emitIndent(out, indent);
            out.append("{\r\n");
            m_emitter.emitIndent(out, bodyIndent);
            out.append("const auto& __adhd_switch_subject = (");
            m_emitter.emitExpr(out, *stmt.expr);
            out.append(");\r\n");
        }

        const u32 prevBreakLabelLine = m_emitter.m_activeSwitchBreakLabelLine;
        if(needsBreakLabel)
            m_emitter.m_activeSwitchBreakLabelLine = stmt.span.line != 0u ? stmt.span.line : 1u;
        else if(needsDoWhileWrap)
            m_emitter.m_activeSwitchBreakLabelLine = 0u;

        b8 firstBranch = true;
        usize defaultGroupEnd = stmt.switchCases.size();
        usize idx = 0u;
        while(idx < stmt.switchCases.size()){
            usize groupEnd = idx;
            b8 groupHasDefault = stmt.switchCases[idx].isDefault;
            while(groupEnd + 1u < stmt.switchCases.size() && stmt.switchCases[groupEnd].statements.empty()){
                ++groupEnd;
                if(stmt.switchCases[groupEnd].isDefault)
                    groupHasDefault = true;
            }

            if(groupHasDefault){
                defaultGroupEnd = groupEnd;
                idx = groupEnd + 1u;
                continue;
            }

            m_emitter.emitIndent(out, bodyIndent);
            out.append(firstBranch ? "if(" : "else if(");
            b8 firstCond = true;
            for(usize k = idx; k <= groupEnd; ++k){
                if(stmt.switchCases[k].isDefault)
                    continue;
                if(!firstCond)
                    out.append(" || ");
                firstCond = false;
                if(cacheSubject)
                    out.append("__adhd_switch_subject");
                else if(stmt.expr)
                    m_emitter.emitExpr(out, *stmt.expr);
                out.append(" == ");
                if(stmt.switchCases[k].matchExpr)
                    m_emitter.emitExpr(out, *stmt.switchCases[k].matchExpr);
            }
            out.append("){\r\n");
            firstBranch = false;
            emitIfChainStatements(out, stmt.switchCases[groupEnd].statements, bodyIndent + 1u);
            m_emitter.emitIndent(out, bodyIndent);
            out.append("}\r\n");
            idx = groupEnd + 1u;
        }
        if(defaultGroupEnd < stmt.switchCases.size()){
            m_emitter.emitIndent(out, bodyIndent);
            out.append(firstBranch ? "{\r\n" : "else{\r\n");
            emitIfChainStatements(out, stmt.switchCases[defaultGroupEnd].statements, bodyIndent + 1u);
            m_emitter.emitIndent(out, bodyIndent);
            out.append("}\r\n");
        }

        m_emitter.m_activeSwitchBreakLabelLine = prevBreakLabelLine;

        if(needsBreakLabel){
            m_emitter.emitIndent(out, bodyIndent);
            out.append("__adhd_switch_break_");
            AppendUnsignedDecimal(out, stmt.span.line != 0u ? stmt.span.line : 1u);
            out.append(":;\r\n");
        }
        if(needsDoWhileWrap){
            m_emitter.emitIndent(out, indent);
            out.append("}while(false);\r\n");
        }
        else if(cacheSubject){
            m_emitter.emitIndent(out, indent);
            out.append("}\r\n");
        }
    }
    else{
        const u32 prevBreakLabelLine = m_emitter.m_activeSwitchBreakLabelLine;
        m_emitter.m_activeSwitchBreakLabelLine = 0u;
        m_emitter.emitIndent(out, indent);
        out.append("switch(");
        if(stmt.expr)
            m_emitter.emitExpr(out, *stmt.expr);
        out.append("){\r\n");
        for(const SwitchCase& sc : stmt.switchCases){
            m_emitter.emitIndent(out, indent);
            if(sc.isDefault){
                out.append("default:");
            }
            else{
                out.append("case ");
                if(sc.matchExpr)
                    m_emitter.emitExpr(out, *sc.matchExpr);
                out.push_back(':');
            }
            if(sc.statements.empty()){
                out.append("\r\n");
            }
            else{
                out.append("{\r\n");
                m_emitter.emitStatementList(out, sc.statements, indent + 1u);
                m_emitter.emitIndent(out, indent);
                out.append("}\r\n");
            }
        }
        m_emitter.emitIndent(out, indent);
        out.append("}\r\n");
        m_emitter.m_activeSwitchBreakLabelLine = prevBreakLabelLine;
    }
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

