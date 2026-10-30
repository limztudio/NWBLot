// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitStatementList(
    MString& out,
    const Span<const Stmt* const> statements,
    const u32 indent
)const{
    for(const Stmt* stmt : statements){
        if(stmt)
            emitStmt(out, *stmt, indent);
    }
}

void SourceEmitter::emitSwitchIfChainStatements(
    MString& out,
    const Span<const Stmt* const> statements,
    const u32 indent
)const{
    for(usize i = 0u; i < statements.size(); ++i){
        const Stmt* child = statements[i];
        if(!child)
            continue;
        const b8 isTrailingBreak = (child->kind == StmtKind::Break) && (i + 1u == statements.size());
        if(isTrailingBreak)
            continue;
        if(child->kind == StmtKind::Block){
            emitIndent(out, indent);
            out.append("{\r\n");
            if(i + 1u == statements.size() || (i + 2u == statements.size() && statements.back() && statements.back()->kind == StmtKind::Break))
                emitSwitchIfChainStatements(out, child->statements, indent + 1u);
            else
                emitStatementList(out, child->statements, indent + 1u);
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        else if(child->kind == StmtKind::If){
            const b8 isLastInCase = (i + 1u == statements.size())
                || (i + 2u == statements.size() && statements.back() && statements.back()->kind == StmtKind::Break);
            emitIndent(out, indent);
            out.append("if(");
            if(child->condition)
                emitExpr(out, *child->condition, true);
            out.append("){\r\n");
            if(isLastInCase)
                emitSwitchIfChainStatements(out, child->statements, indent + 1u);
            else
                emitStatementList(out, child->statements, indent + 1u);
            emitIndent(out, indent);
            out.append("}\r\n");
            if(!child->elseStatements.empty()){
                emitIndent(out, indent);
                out.append("else{\r\n");
                if(isLastInCase)
                    emitSwitchIfChainStatements(out, child->elseStatements, indent + 1u);
                else
                    emitStatementList(out, child->elseStatements, indent + 1u);
                emitIndent(out, indent);
                out.append("}\r\n");
            }
        }
        else{
            emitStmt(out, *child, indent);
        }
    }
}

void SourceEmitter::emitLoopBodyStatements(
    MString& out,
    const Span<const Stmt* const> statements,
    const u32 indent
)const{
    const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
    m_activeSwitchBreakLabelLine = 0u;
    ++m_activeSimdLoopInnerBreakDepth;
    ++m_activeSimdLoopInnerContinueDepth;
    emitStatementList(out, statements, indent);
    --m_activeSimdLoopInnerBreakDepth;
    --m_activeSimdLoopInnerContinueDepth;
    m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;
}

void SourceEmitter::emitStmt(
    MString& out,
    const Stmt& stmt,
    const u32 indent
)const{
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
            emitCtorOrCallArgs(
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
        if(stmt.expr)
            emitSimdMutCallReloads(out, *stmt.expr, indent);
        for(const Expr* arg : stmt.arguments){
            if(arg)
                emitSimdMutCallReloads(out, *arg, indent);
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
        if(tryEmitParallelWaitStmt(out, stmt, indent))
            break;

        emitIndent(out, indent);
        if(stmt.expr)
            emitExpr(out, *stmt.expr);
        out.append(";\r\n");

        if(stmt.expr)
            emitSimdMutCallReloads(out, *stmt.expr, indent);
        break;
    }
    case StmtKind::Return:{
        emitSimdBlockStores(out, indent);
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
        if(m_activeSimdLoopBinding && m_activeSimdLoopInnerBreakDepth == 0u && m_activeSwitchBreakLabelLine == 0u)
            emitSimdStoreForBinding(out, *m_activeSimdLoopBinding, indent);
        emitIndent(out, indent);
        if(m_activeSwitchBreakLabelLine != 0u){
            out.append("goto __adhd_switch_break_");
            AppendAdhdUnsigned(out, m_activeSwitchBreakLabelLine);
            out.append(";\r\n");
        }
        else{
            out.append("break;\r\n");
        }
        break;
    }
    case StmtKind::Continue:{
        if(m_activeSimdLoopBinding && m_activeSimdLoopInnerContinueDepth == 0u)
            emitSimdStoreForBinding(out, *m_activeSimdLoopBinding, indent);
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
                emitIndent(out, indent);
                out.append("do{\r\n");
                if(cacheSubject){
                    emitIndent(out, bodyIndent);
                    out.append("const auto& __adhd_switch_subject = (");
                    emitExpr(out, *stmt.expr);
                    out.append(");\r\n");
                }
            }
            else if(cacheSubject){
                emitIndent(out, indent);
                out.append("{\r\n");
                emitIndent(out, bodyIndent);
                out.append("const auto& __adhd_switch_subject = (");
                emitExpr(out, *stmt.expr);
                out.append(");\r\n");
            }

            const u32 prevBreakLabelLine = m_activeSwitchBreakLabelLine;
            if(needsBreakLabel)
                m_activeSwitchBreakLabelLine = stmt.span.line != 0u ? stmt.span.line : 1u;
            else if(needsDoWhileWrap)
                m_activeSwitchBreakLabelLine = 0u;
            if(needsDoWhileWrap)
                ++m_activeSimdLoopInnerBreakDepth;

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

                emitIndent(out, bodyIndent);
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
                        emitExpr(out, *stmt.expr);
                    out.append(" == ");
                    if(stmt.switchCases[k].matchExpr)
                        emitExpr(out, *stmt.switchCases[k].matchExpr);
                }
                out.append("){\r\n");
                firstBranch = false;
                emitSwitchIfChainStatements(out, stmt.switchCases[groupEnd].statements, bodyIndent + 1u);
                emitIndent(out, bodyIndent);
                out.append("}\r\n");
                idx = groupEnd + 1u;
            }
            if(defaultGroupEnd < stmt.switchCases.size()){
                emitIndent(out, bodyIndent);
                out.append(firstBranch ? "{\r\n" : "else{\r\n");
                emitSwitchIfChainStatements(out, stmt.switchCases[defaultGroupEnd].statements, bodyIndent + 1u);
                emitIndent(out, bodyIndent);
                out.append("}\r\n");
            }

            if(needsDoWhileWrap)
                --m_activeSimdLoopInnerBreakDepth;
            m_activeSwitchBreakLabelLine = prevBreakLabelLine;

            if(needsBreakLabel){
                emitIndent(out, bodyIndent);
                out.append("__adhd_switch_break_");
                AppendAdhdUnsigned(out, stmt.span.line != 0u ? stmt.span.line : 1u);
                out.append(":;\r\n");
            }
            if(needsDoWhileWrap){
                emitIndent(out, indent);
                out.append("}while(false);\r\n");
            }
            else if(cacheSubject){
                emitIndent(out, indent);
                out.append("}\r\n");
            }
        }
        else{
            const u32 prevBreakLabelLine = m_activeSwitchBreakLabelLine;
            m_activeSwitchBreakLabelLine = 0u;
            ++m_activeSimdLoopInnerBreakDepth;
            emitIndent(out, indent);
            out.append("switch(");
            if(stmt.expr)
                emitExpr(out, *stmt.expr);
            out.append("){\r\n");
            for(const SwitchCase& sc : stmt.switchCases){
                emitIndent(out, indent);
                if(sc.isDefault){
                    out.append("default:");
                }
                else{
                    out.append("case ");
                    if(sc.matchExpr)
                        emitExpr(out, *sc.matchExpr);
                    out.push_back(':');
                }
                if(sc.statements.empty()){
                    out.append("\r\n");
                }
                else{
                    out.append("{\r\n");
                    emitStatementList(out, sc.statements, indent + 1u);
                    emitIndent(out, indent);
                    out.append("}\r\n");
                }
            }
            emitIndent(out, indent);
            out.append("}\r\n");
            --m_activeSimdLoopInnerBreakDepth;
            m_activeSwitchBreakLabelLine = prevBreakLabelLine;
        }
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

        const SimdVarBinding* loopBinding = findSimdBinding(false, stmt.name);
        if(loopBinding && loopBinding->isLoopElement)
            emitSimdLoopElementLoad(out, *loopBinding, indent + 1u);

        const SimdVarBinding* prevSimdLoopBinding = m_activeSimdLoopBinding;
        const u32 prevInnerBreakDepth = m_activeSimdLoopInnerBreakDepth;
        const u32 prevInnerContinueDepth = m_activeSimdLoopInnerContinueDepth;
        const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
        m_activeSwitchBreakLabelLine = 0u;
        if(stmt.rangeMode == ParamPassingMode::Mut && loopBinding && loopBinding->isLoopElement && loopBinding->written){
            m_activeSimdLoopBinding = loopBinding;
            m_activeSimdLoopInnerBreakDepth = 0u;
            m_activeSimdLoopInnerContinueDepth = 0u;
        }
        else if(m_activeSimdLoopBinding){
            ++m_activeSimdLoopInnerBreakDepth;
            ++m_activeSimdLoopInnerContinueDepth;
        }

        emitStatementList(out, stmt.statements, indent + 1u);

        m_activeSimdLoopBinding = prevSimdLoopBinding;
        m_activeSimdLoopInnerBreakDepth = prevInnerBreakDepth;
        m_activeSimdLoopInnerContinueDepth = prevInnerContinueDepth;
        m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;

        if(stmt.rangeMode == ParamPassingMode::Mut && loopBinding && loopBinding->isLoopElement && loopBinding->written){
            const b8 endsWithTerminal = !stmt.statements.empty() && stmt.statements.back() && (
                stmt.statements.back()->kind == StmtKind::Return
                || stmt.statements.back()->kind == StmtKind::Break
                || stmt.statements.back()->kind == StmtKind::Continue
            );
            if(!endsWithTerminal)
                emitSimdStoreForBinding(out, *loopBinding, indent + 1u);
        }

        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::ForIndex:{
        emitIndent(out, indent);
        out.append("for(");
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
    case StmtKind::ParallelCall:
    case StmtKind::NativeBlock:
    case StmtKind::Diagnostic:{
        emitParallelStmt(out, stmt, indent);
        break;
    }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

