// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParallelGraphValidator::validateMethod(MethodDecl& method, MVector<ParseError>& errors){
    if(!method.hasBody)
        return true;

    bool usesTaskScope = false;
    const bool ok = validateBlock(method.body, usesTaskScope, errors);
    if(usesTaskScope)
        method.usesTaskScope = true;
    return ok;
}

bool ParallelGraphValidator::validateBlock(MVector<Stmt*>& statements, bool& outUsesTaskScope, MVector<ParseError>& errors){
    MVector<MStringView> declaredTaskNames(m_arena);
    bool ok = true;

    for(Stmt* stmt : statements){
        if(!stmt)
            continue;

        switch(stmt->kind){
        case StmtKind::ParallelBlock:{
            outUsesTaskScope = true;
            for(const MStringView& existing : declaredTaskNames){
                if(existing == stmt->name){
                    reportError(errors, stmt->span, "duplicate 'parallel' task name in the same scope");
                    ok = false;
                    break;
                }
            }

            for(const MStringView& dep : stmt->parallelDependencies){
                if(dep == stmt->name){
                    reportError(errors, stmt->span, "'parallel' task cannot depend on itself");
                    ok = false;
                    continue;
                }

                bool foundDep = false;
                for(const MStringView& existing : declaredTaskNames){
                    if(existing == dep){
                        foundDep = true;
                        break;
                    }
                }
                if(!foundDep){
                    reportError(errors, stmt->span, "'parallel' task dependency must reference an earlier 'parallel' task in the same scope");
                    ok = false;
                }
            }

            declaredTaskNames.push_back(stmt->name);
            if(!validateBlock(stmt->statements, outUsesTaskScope, errors))
                ok = false;
            break;
        }
        case StmtKind::ParallelCall:{
            outUsesTaskScope = true;
            break;
        }
        case StmtKind::Block:
        case StmtKind::ForRange:
        case StmtKind::ForIndex:
        case StmtKind::While:
        case StmtKind::DoWhile:
        case StmtKind::ParallelForRange:
        case StmtKind::ParallelForIndex:{
            if(!validateBlock(stmt->statements, outUsesTaskScope, errors))
                ok = false;
            break;
        }
        case StmtKind::If:{
            if(!validateBlock(stmt->statements, outUsesTaskScope, errors))
                ok = false;
            if(!validateBlock(stmt->elseStatements, outUsesTaskScope, errors))
                ok = false;
            break;
        }
        case StmtKind::Switch:{
            for(SwitchCase& sc : stmt->switchCases){
                if(!validateBlock(sc.statements, outUsesTaskScope, errors))
                    ok = false;
            }
            break;
        }
        default:
            break;
        }
    }

    return ok;
}

void ParallelGraphValidator::reportError(MVector<ParseError>& errors, const SourceSpan span, const MStringView message)const{
    errors.push_back(ParseError{span.line, span.column, MString(message.data(), message.size(), m_arena)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

