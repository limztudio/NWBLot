// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitStatementList(
    MString& out,
    const MVector<Stmt*>& statements,
    const u32 indent
)const{
    for(const Stmt* stmt : statements){
        if(stmt)
            emitStmt(out, *stmt, indent);
    }
}

void SourceEmitter::emitSwitchIfChainStatements(
    MString& out,
    const MVector<Stmt*>& statements,
    const u32 indent
)const{
    for(const Stmt* child : statements){
        if(!child || child->kind == StmtKind::Break)
            continue;
        if(child->kind == StmtKind::Block){
            emitIndent(out, indent);
            out.append("{\r\n");
            emitSwitchIfChainStatements(out, child->statements, indent + 1u);
            emitIndent(out, indent);
            out.append("}\r\n");
        }
        else if(child->kind == StmtKind::If){
            emitIndent(out, indent);
            out.append("if(");
            if(child->condition)
                emitExpr(out, *child->condition);
            out.append("){\r\n");
            emitSwitchIfChainStatements(out, child->statements, indent + 1u);
            emitIndent(out, indent);
            out.append("}\r\n");
            if(!child->elseStatements.empty()){
                emitIndent(out, indent);
                out.append("else{\r\n");
                emitSwitchIfChainStatements(out, child->elseStatements, indent + 1u);
                emitIndent(out, indent);
                out.append("}\r\n");
            }
        }
        else{
            emitStmt(out, *child, indent);
        }
    }
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
            if(!baseVarType.empty() && m_symbols.isSimdMatrixType(baseVarType)){
                if(stmt.arguments.empty()){
                    out.append("{};\r\n");
                }
                else{
                    out.append("{ .raw = { ");
                    for(usize i = 0u; i < stmt.arguments.size(); ++i){
                        if(i > 0u)
                            out.append(", ");
                        if(stmt.arguments[i])
                            emitExpr(out, *stmt.arguments[i]);
                    }
                    out.append(" } };\r\n");
                }
            }
            else if(stmt.arguments.empty() && !stmt.usesScratchArena){
                out.append("{};\r\n");
            }
            else{
                out.push_back('(');
                u32 ctorVecLanes = 0u;
                const b8 isScalarSplatCtor = !baseVarType.empty()
                    && (m_symbols.isSimdVectorType(baseVarType, ctorVecLanes) || m_symbols.isUnalignedVectorType(baseVarType, ctorVecLanes))
                    && stmt.arguments.size() == 1u && stmt.arguments[0]
                    && stmt.arguments[0]->resolvedSimdType.empty();
                if(isScalarSplatCtor){
                    for(u32 lane = 0u; lane < ctorVecLanes; ++lane){
                        if(lane > 0u)
                            out.append(", ");
                        emitExpr(out, *stmt.arguments[0]);
                    }
                }
                else{
                    for(usize i = 0u; i < stmt.arguments.size(); ++i){
                        if(i > 0u)
                            out.append(", ");
                        if(stmt.arguments[i])
                            emitExpr(out, *stmt.arguments[i]);
                    }
                }
                if(stmt.usesScratchArena){
                    if(!stmt.arguments.empty())
                        out.append(", ");
                    out.append("__adhd_scratch");
                }
                out.append(");\r\n");
            }
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
        emitSimdBlockStores(out, indent, false);
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
        emitSimdBlockStores(out, indent, true);
        emitIndent(out, indent);
        out.append("break;\r\n");
        break;
    }
    case StmtKind::Continue:{
        emitSimdBlockStores(out, indent, true);
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
            const b8 cacheSubject = stmt.expr
                && stmt.expr->kind != ExprKind::Identifier
                && stmt.expr->kind != ExprKind::ThisMember;
            const u32 bodyIndent = cacheSubject ? (indent + 1u) : indent;
            if(cacheSubject){
                emitIndent(out, indent);
                out.append("{\r\n");
                emitIndent(out, bodyIndent);
                out.append("const auto& __adhd_switch_subject = (");
                emitExpr(out, *stmt.expr);
                out.append(");\r\n");
            }

            b8 firstBranch = true;
            usize idx = 0u;
            while(idx < stmt.switchCases.size()){
                usize groupEnd = idx;
                b8 groupHasDefault = stmt.switchCases[idx].isDefault;
                while(groupEnd + 1u < stmt.switchCases.size() && stmt.switchCases[groupEnd].statements.empty()){
                    ++groupEnd;
                    if(stmt.switchCases[groupEnd].isDefault)
                        groupHasDefault = true;
                }

                emitIndent(out, bodyIndent);
                if(groupHasDefault){
                    out.append(firstBranch ? "{\r\n" : "else{\r\n");
                    firstBranch = false;
                }
                else{
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
                }
                emitSwitchIfChainStatements(out, stmt.switchCases[groupEnd].statements, bodyIndent + 1u);
                emitIndent(out, bodyIndent);
                out.append("}\r\n");
                idx = groupEnd + 1u;
            }
            if(cacheSubject){
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

        emitStatementList(out, stmt.statements, indent + 1u);

        if(loopBinding && loopBinding->isLoopElement && loopBinding->written){
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
        }
        out.append("; ");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
        out.append("; ");
        if(stmt.stepExpr)
            emitExpr(out, *stmt.stepExpr);
        out.append("){\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
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
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}\r\n");
        break;
    }
    case StmtKind::DoWhile:{
        emitIndent(out, indent);
        out.append("do{\r\n");
        emitStatementList(out, stmt.statements, indent + 1u);
        emitIndent(out, indent);
        out.append("}while(");
        if(stmt.condition)
            emitExpr(out, *stmt.condition);
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
