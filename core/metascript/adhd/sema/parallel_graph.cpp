// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParallelGraphValidator::validateMethod(const MethodDecl& method, MVector<ParseError>& errors){
    if(!method.hasBody)
        return true;

    return validateBlock(method.body, errors);
}

bool ParallelGraphValidator::validateBlock(const MVector<Stmt*>& statements, MVector<ParseError>& errors){
    MVector<MStringView> declaredTaskNames(m_arena);
    bool ok = true;

    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        switch(stmt->kind){
        case StmtKind::ParallelBlock:{
            for(const MStringView& existing : declaredTaskNames){
                if(existing == stmt->name){
                    ReportAdhdDiagnostic(m_arena, errors, stmt->span, "duplicate 'parallel' task name in the same scope");
                    ok = false;
                    break;
                }
            }

            for(const MStringView& dep : stmt->parallelDependencies){
                if(dep == stmt->name){
                    ReportAdhdDiagnostic(m_arena, errors, stmt->span, "'parallel' task cannot depend on itself");
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
                    ReportAdhdDiagnostic(m_arena, errors, stmt->span, "'parallel' task dependency must reference an earlier 'parallel' task in the same scope");
                    ok = false;
                }
            }

            declaredTaskNames.push_back(stmt->name);
            if(!validateBlock(stmt->statements, errors))
                ok = false;
            break;
        }
        case StmtKind::Block:
        case StmtKind::ForRange:
        case StmtKind::ForIndex:
        case StmtKind::While:
        case StmtKind::DoWhile:
        case StmtKind::ParallelForRange:
        case StmtKind::ParallelForIndex:{
            if(!validateBlock(stmt->statements, errors))
                ok = false;
            break;
        }
        case StmtKind::If:{
            if(!validateBlock(stmt->statements, errors))
                ok = false;
            if(!validateBlock(stmt->elseStatements, errors))
                ok = false;
            break;
        }
        case StmtKind::Switch:{
            for(const SwitchCase& sc : stmt->switchCases){
                if(!validateBlock(sc.statements, errors))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

