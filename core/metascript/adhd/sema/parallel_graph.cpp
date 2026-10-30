// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ParallelGraphValidator::validateMethod(const MethodDecl& method, MVector<ParseError>& errors){
    if(!method.hasBody)
        return true;

    return validateBlock(method.body, errors);
}

b8 ParallelGraphValidator::validateBlock(const Span<const Stmt* const> statements, MVector<ParseError>& errors){
    MVector<MStringView> declaredTaskNames(m_arena);
    b8 ok = true;

    const auto validateExprLambdas = [&](const auto& self, const Expr* expr) -> void{
        if(!expr)
            return;
        if(expr->kind == ExprKind::Lambda){
            if(!validateBlock(expr->lambdaBody, errors))
                ok = false;
        }
        self(self, expr->lhs);
        self(self, expr->rhs);
        for(const Expr* arg : expr->arguments)
            self(self, arg);
    };

    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;

        if(stmt->initStmt)
            validateExprLambdas(validateExprLambdas, stmt->initStmt->expr);
        validateExprLambdas(validateExprLambdas, stmt->expr);
        validateExprLambdas(validateExprLambdas, stmt->condition);
        validateExprLambdas(validateExprLambdas, stmt->stepExpr);
        for(const Expr* arg : stmt->arguments)
            validateExprLambdas(validateExprLambdas, arg);
        for(const NativeBinding& nb : stmt->nativeBindings)
            validateExprLambdas(validateExprLambdas, nb.sourceExpr);

        switch(stmt->kind){
        case StmtKind::ParallelBlock:{
            if(!stmt->name.empty()){
                for(const MStringView& existing : declaredTaskNames){
                    if(existing == stmt->name){
                        ReportAdhdDiagnostic(m_arena, errors, stmt->span, "duplicate 'parallel' task name in the same scope");
                        ok = false;
                        break;
                    }
                }
            }

            for(usize dIdx = 0u; dIdx < stmt->parallelDependencies.size(); ++dIdx){
                const MStringView dep = stmt->parallelDependencies[dIdx];
                for(usize prevD = 0u; prevD < dIdx; ++prevD){
                    if(stmt->parallelDependencies[prevD] == dep){
                        ReportAdhdDiagnostic(m_arena, errors, stmt->span, "duplicate task dependency in 'parallel' block");
                        ok = false;
                        break;
                    }
                }
                if(!stmt->name.empty() && dep == stmt->name){
                    ReportAdhdDiagnostic(m_arena, errors, stmt->span, "'parallel' task cannot depend on itself");
                    ok = false;
                    continue;
                }

                b8 foundDep = false;
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

            if(!stmt->name.empty())
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
                validateExprLambdas(validateExprLambdas, sc.matchExpr);
                if(!validateBlock(sc.statements, errors))
                    ok = false;
            }
            break;
        }
        case StmtKind::ExprStmt:{
            if(stmt->expr && stmt->expr->kind == ExprKind::Call && stmt->expr->lhs && stmt->expr->lhs->kind == ExprKind::Identifier && stmt->expr->lhs->text == "wait"){
                for(usize aIdx = 0u; aIdx < stmt->expr->arguments.size(); ++aIdx){
                    const Expr* arg = stmt->expr->arguments[aIdx];
                    if(!arg || arg->kind != ExprKind::Identifier){
                        ReportAdhdDiagnostic(m_arena, errors, stmt->span, "'wait' argument must be a 'parallel' task name");
                        ok = false;
                        continue;
                    }
                    for(usize prevA = 0u; prevA < aIdx; ++prevA){
                        const Expr* prevArg = stmt->expr->arguments[prevA];
                        if(prevArg && prevArg->kind == ExprKind::Identifier && prevArg->text == arg->text){
                            ReportAdhdDiagnostic(m_arena, errors, stmt->span, "duplicate task name in 'wait' statement");
                            ok = false;
                            break;
                        }
                    }
                    b8 foundTask = false;
                    for(const MStringView& existing : declaredTaskNames){
                        if(existing == arg->text){
                            foundTask = true;
                            break;
                        }
                    }
                    if(!foundTask){
                        ReportAdhdDiagnostic(m_arena, errors, stmt->span, "'wait' target must reference an earlier 'parallel' task in the same scope");
                        ok = false;
                    }
                }
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

