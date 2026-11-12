// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "type_checker.h"
#include "return_statement_checker.h"
#include "local_variable_checker.h"
#include "callable_argument_checker.h"

#include "borrow_lifetime_analyzer.h"
#include "lambda_capture_analyzer.h"
#include "last_use_move_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeChecker::StatementsContainReturnOrNative(const Span<const Stmt* const> stmts)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(stmt->kind == StmtKind::Return || stmt->kind == StmtKind::NativeBlock)
            return true;
        if(StatementsContainReturnOrNative(stmt->statements) || StatementsContainReturnOrNative(stmt->elseStatements))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(StatementsContainReturnOrNative(sc.statements))
                return true;
        }
    }
    return false;
}

b8 TypeChecker::checkMethodBody(ClassDecl* ownerClass, MethodDecl& method, MVector<ParseError>& errors){
    if(!method.hasBody)
        return true;

    b8 ok = true;
    MVector<ScopedVarInfo> scope(m_arena);
    for(const ParamDecl& param : method.params){
        if(!param.name.empty() && FindScopedVar(scope, param.name)){
            reportError(errors, param.span, "duplicate parameter name");
            ok = false;
        }
        ScopedVarInfo info;
        info.name = param.name;
        info.type = param.type;
        info.mode = param.mode;
        info.isConst = (param.mode == ParamPassingMode::In);
        info.isParam = true;
        scope.push_back(info);
    }

    if(!checkStatementList(ownerClass, method, method.body, scope, errors))
        ok = false;
    if(!method.isConstructor && !method.isDestructor && !IsVoidTypeRef(m_symbols.unwrapTypeAlias(method.returnType))){
        if(!StatementsContainReturnOrNative(method.body)){
            reportError(errors, method.span, "non-void function or method must contain a 'return' statement");
            ok = false;
        }
    }
    return ok;
}

b8 TypeChecker::checkStatementList(ClassDecl* ownerClass, MethodDecl& method, MVector<Stmt*>& statements, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors){
    const usize savedScopeSize = scope.size();
    b8 ok = true;

    for(Stmt* stmt : statements){
        if(!stmt)
            continue;
        if(!checkStatement(ownerClass, method, *stmt, scope, errors))
            ok = false;
    }

    LastUseMoveAnalyzer(m_symbols).analyze(statements, scope, savedScopeSize);
    while(scope.size() > savedScopeSize)
        scope.pop_back();

    return ok;
}







b8 TypeChecker::checkStatement(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors){
    b8 ok = true;

    auto checkLoopOrParallelBody = [&](const b8 isPar) -> b8{
        const u32 savedLoop = m_loopDepth;
        const u32 savedSwitch = m_switchDepth;
        const b8 savedParScratch = m_activeParallelBlockUsesScratch;
        if(isPar){
            m_loopDepth = 0u;
            m_switchDepth = 0u;
            m_activeParallelBlockUsesScratch = false;
            ++m_parallelDepth;
        }
        else{
            ++m_loopDepth;
        }
        const b8 bodyOk = checkStatementList(ownerClass, method, stmt.statements, scope, errors);
        if(isPar){
            --m_parallelDepth;
            stmt.usesScratchArena = m_activeParallelBlockUsesScratch;
            m_activeParallelBlockUsesScratch = savedParScratch;
        }
        m_loopDepth = savedLoop;
        m_switchDepth = savedSwitch;
        return bodyOk;
    };

    switch(stmt.kind){
    case StmtKind::Block:{
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::ParallelBlock:{
        method.usesTaskScope = true;
        if(!checkLoopOrParallelBody(true))
            ok = false;
        break;
    }
    case StmtKind::VarDecl:{
        if(!LocalVariableChecker(*this).check(ownerClass, method, stmt, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::ExprStmt:{
        if(IsParallelWaitCallExpr(stmt.expr)){
            method.usesTaskScope = true;
            if(m_parallelDepth > 0u){
                reportError(errors, stmt.span, "'wait' cannot be used inside a 'parallel' or 'parallel_for' construct");
                ok = false;
            }
            if(!stmt.expr->callTemplateArgs.empty()){
                reportError(errors, stmt.span, "'wait' does not accept template arguments");
                ok = false;
            }
            if(stmt.expr->arguments.empty()){
                reportError(errors, stmt.span, "'wait' requires at least one 'parallel' task name argument");
                ok = false;
            }
            break;
        }
        if(stmt.expr && !checkExpr(ownerClass, scope, false, method, *stmt.expr, errors))
            ok = false;
        break;
    }
    case StmtKind::Return:{
        if(!ReturnStatementChecker(*this).check(ownerClass, scope, method, stmt, errors))
            ok = false;
        break;
    }
    case StmtKind::Break:{
        if(m_loopDepth == 0u && m_switchDepth == 0u){
            reportError(errors, stmt.span, "'break' cannot be used outside of a sequential loop or 'switch'");
            ok = false;
        }
        break;
    }
    case StmtKind::Continue:{
        if(m_loopDepth == 0u){
            reportError(errors, stmt.span, "'continue' cannot be used outside of a sequential loop");
            ok = false;
        }
        break;
    }
    case StmtKind::ParallelCall:{
        method.usesTaskScope = true;
        const b8 savedParScratch = m_activeParallelBlockUsesScratch;
        m_activeParallelBlockUsesScratch = false;
        ++m_parallelDepth;
        if(stmt.expr && !checkExpr(ownerClass, scope, false, method, *stmt.expr, errors))
            ok = false;
        --m_parallelDepth;
        stmt.usesScratchArena = m_activeParallelBlockUsesScratch;
        m_activeParallelBlockUsesScratch = savedParScratch || stmt.usesScratchArena;
        break;
    }
    case StmtKind::If:{
        if(stmt.condition && !checkExpr(ownerClass, scope, false, method, *stmt.condition, errors))
            ok = false;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        if(!stmt.elseStatements.empty() && !checkStatementList(ownerClass, method, stmt.elseStatements, scope, errors))
            ok = false;
        break;
    }
    case StmtKind::Switch:{
        if(stmt.expr){
            if(!checkExpr(ownerClass, scope, false, method, *stmt.expr, errors))
                ok = false;
            const TypeRef* subjTypeRef = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
            const MStringView subjectType = inferExprTypeName(ownerClass, &method, *stmt.expr, scope);
            const b8 isUserSubjType = m_symbols.hasTypeSymbol(subjectType);
            const b8 isNonSwitchableBuiltin = !isUserSubjType && (BuiltinTypeSemantics::IsFloatScalarType(subjectType) || subjectType == "void" || SymbolTable::IsSimdType(subjectType) || SymbolTable::IsUnalignedVectorType(subjectType) || FindBuiltinGenericType(subjectType) != nullptr);
            if(!subjectType.empty() && (isNonSwitchableBuiltin || (subjTypeRef && subjTypeRef->isFuncType))){
                reportError(errors, stmt.span, "invalid 'switch' subject type; floating-point, SIMD, container, callable, and 'void' types cannot be switched on");
                ok = false;
            }
            if(!isUserSubjType && SymbolTable::IsStringSwitchType(subjectType)){
                stmt.lowerSwitchToIfChain = true;
            }
            else{
                const ClassDecl* switchCls = nullptr;
                const MethodDecl* eqOp = resolveCustomOperator(ownerClass, &method, subjTypeRef, subjectType, "==", 1u, true, &switchCls);
                if(switchCls){
                    if(!eqOp){
                        reportError(errors, stmt.span, "switch on custom class requires 'operator==' to be defined on the class");
                        ok = false;
                    }
                    stmt.lowerSwitchToIfChain = true;
                }
                else if(subjTypeRef && m_symbols.findInterface(subjectType)){
                    if(!eqOp){
                        reportError(errors, stmt.span, "switch on interface requires 'operator==' to be declared on the interface");
                        ok = false;
                    }
                    stmt.lowerSwitchToIfChain = true;
                }
                else if(FindTemplateParam(ownerClass, &method, subjectType)){
                    if(!eqOp){
                        reportError(errors, stmt.span, "switch on template parameter requires 'operator==' in its interface bounds");
                        ok = false;
                    }
                    stmt.lowerSwitchToIfChain = true;
                }
                else if(eqOp){
                    stmt.lowerSwitchToIfChain = true;
                }
            }
        }
        ++m_switchDepth;
        for(usize i = 0u; i < stmt.switchCases.size(); ++i){
            SwitchCase& sc = stmt.switchCases[i];
            if(sc.matchExpr){
                if(!checkExpr(ownerClass, scope, false, method, *sc.matchExpr, errors))
                    ok = false;
                if(sc.matchExpr->kind == ExprKind::StringLiteral)
                    stmt.lowerSwitchToIfChain = true;
                for(usize j = 0u; j < i; ++j){
                    const SwitchCase& prevSc = stmt.switchCases[j];
                    if(prevSc.isDefault || !prevSc.matchExpr)
                        continue;
                    b8 isDup = false;
                    if(prevSc.matchExpr->kind == sc.matchExpr->kind){
                        if(IsLiteralExprKind(sc.matchExpr->kind) || sc.matchExpr->kind == ExprKind::Identifier){
                            isDup = (prevSc.matchExpr->text == sc.matchExpr->text);
                        }
                        else if(sc.matchExpr->kind == ExprKind::ScopeAccess && prevSc.matchExpr->scopeChain.size() == 2u && sc.matchExpr->scopeChain.size() == 2u){
                            isDup = (prevSc.matchExpr->scopeChain[0] == sc.matchExpr->scopeChain[0]
                                && prevSc.matchExpr->scopeChain[1] == sc.matchExpr->scopeChain[1]);
                        }
                    }
                    if(!isDup && !stmt.lowerSwitchToIfChain && sc.matchExpr->kind != ExprKind::StringLiteral && prevSc.matchExpr->kind != ExprKind::StringLiteral){
                        i64 prevConst = 0;
                        i64 curConst = 0;
                        if(TryEvaluateExprConstant(prevSc.matchExpr, m_symbols, 0u, nullptr, prevConst) && TryEvaluateExprConstant(sc.matchExpr, m_symbols, 0u, nullptr, curConst)){
                            const b8 prevIsBool = (prevSc.matchExpr->kind == ExprKind::BoolLiteral);
                            const b8 curIsBool = (sc.matchExpr->kind == ExprKind::BoolLiteral);
                            if(prevIsBool == curIsBool && prevConst == curConst)
                                isDup = true;
                        }
                    }
                    if(isDup){
                        reportError(errors, sc.matchExpr->span, "duplicate 'case' value in 'switch' statement");
                        ok = false;
                        break;
                    }
                }
            }
            if(!checkStatementList(ownerClass, method, sc.statements, scope, errors))
                ok = false;
        }
        --m_switchDepth;
        break;
    }
    case StmtKind::ForRange:
    case StmtKind::ParallelForRange:{
        const b8 isPar = (stmt.kind == StmtKind::ParallelForRange);
        if(isPar)
            method.usesTaskScope = true;
        if(!stmt.name.empty() && FindScopedVar(scope, stmt.name)){
            reportError(errors, stmt.span, "duplicate variable or parameter name in scope");
            ok = false;
        }
        if(stmt.expr && !checkExpr(ownerClass, scope, stmt.rangeMode == ParamPassingMode::Mut, method, *stmt.expr, errors))
            ok = false;
        const TypeRef* elemTypeRef = nullptr;
        MStringView resolvedElemName;
        if(stmt.varType){
            if(stmt.expr){
                const TypeRef* containerType = inferExprTypeRef(ownerClass, &method, *stmt.expr, scope);
                if(isPar && containerType){
                    const MStringView containerBaseName = m_symbols.resolveBaseTypeName(containerType->name);
                    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(containerBaseName); genericInfo && genericInfo->isNonRandomAccessContainer){
                        reportError(errors, stmt.span, "range-based 'parallel_for' requires a random-access indexable container (such as 'Vector', 'Array', or 'Span')");
                        ok = false;
                    }
                }
                if(containerType && !containerType->templateArgs.empty() && containerType->templateArgs[0].type){
                    elemTypeRef = m_symbols.unwrapTypeAlias(containerType->templateArgs[0].type);
                    if(elemTypeRef){
                        if(stmt.rangeMode == ParamPassingMode::Mut && m_symbols.resolveRefMode(elemTypeRef) == ParamPassingMode::In){
                            reportError(errors, stmt.span, "cannot iterate with 'mut' over a container of read-only 'in' references");
                            ok = false;
                        }
                        resolvedElemName = m_symbols.resolveBaseTypeName(elemTypeRef->name);
                        if(stmt.varType->isAuto){
                            if(elemTypeRef->templateArgs.empty() && !elemTypeRef->isFuncType && SymbolTable::IsSimdType(resolvedElemName)){
                                stmt.varType->isAuto = false;
                                stmt.varType->name = resolvedElemName;
                            }
                        }
                        else if(elemTypeRef->refMode != ParamPassingMode::None && stmt.varType->refMode == ParamPassingMode::None){
                            stmt.varType->refMode = elemTypeRef->refMode;
                        }
                    }
                }
            }
            if(!checkTypeRef(*stmt.varType, ownerClass, &method, errors))
                ok = false;
        }
        const usize saved = scope.size();
        ScopedVarInfo loopVar;
        loopVar.name = stmt.name;
        loopVar.type = elemTypeRef ? const_cast<TypeRef*>(elemTypeRef) : stmt.varType;
        loopVar.inferredTypeName = resolvedElemName;
        loopVar.mode = stmt.rangeMode;
        loopVar.isConst = (stmt.rangeMode == ParamPassingMode::In);
        loopVar.isParam = true;
        scope.push_back(loopVar);
        if(!checkLoopOrParallelBody(isPar))
            ok = false;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::ForIndex:
    case StmtKind::ParallelForIndex:{
        const b8 isPar = (stmt.kind == StmtKind::ParallelForIndex);
        if(isPar){
            method.usesTaskScope = true;
            const b8 validInit = stmt.initStmt && stmt.initStmt->kind == StmtKind::VarDecl && (stmt.initStmt->expr != nullptr || stmt.initStmt->isCtorInit);
            const b8 validCond = stmt.condition && stmt.condition->kind == ExprKind::Binary
                && (stmt.condition->op == TokenType::Less || stmt.condition->op == TokenType::LessEqual)
                && stmt.condition->lhs != nullptr
                && stmt.condition->lhs->kind == ExprKind::Identifier
                && stmt.initStmt != nullptr
                && stmt.condition->lhs->text == stmt.initStmt->name
                && stmt.condition->rhs != nullptr;
            b8 validStep = false;
            if(stmt.stepExpr && stmt.initStmt){
                const MStringView idxName = stmt.initStmt->name;
                if(stmt.stepExpr->kind == ExprKind::Unary && stmt.stepExpr->op == TokenType::PlusPlus && stmt.stepExpr->lhs && stmt.stepExpr->lhs->kind == ExprKind::Identifier && stmt.stepExpr->lhs->text == idxName){
                    validStep = true;
                }
                else if(stmt.stepExpr->kind == ExprKind::Binary && stmt.stepExpr->lhs && stmt.stepExpr->lhs->kind == ExprKind::Identifier && stmt.stepExpr->lhs->text == idxName && stmt.stepExpr->rhs){
                    const Expr* stepAmountExpr = nullptr;
                    if(stmt.stepExpr->op == TokenType::PlusEqual){
                        stepAmountExpr = stmt.stepExpr->rhs;
                    }
                    else if(stmt.stepExpr->op == TokenType::Equal && stmt.stepExpr->rhs->kind == ExprKind::Binary && stmt.stepExpr->rhs->op == TokenType::Plus && stmt.stepExpr->rhs->lhs && stmt.stepExpr->rhs->rhs){
                        if(stmt.stepExpr->rhs->lhs->kind == ExprKind::Identifier && stmt.stepExpr->rhs->lhs->text == idxName)
                            stepAmountExpr = stmt.stepExpr->rhs->rhs;
                        else if(stmt.stepExpr->rhs->rhs->kind == ExprKind::Identifier && stmt.stepExpr->rhs->rhs->text == idxName)
                            stepAmountExpr = stmt.stepExpr->rhs->lhs;
                    }
                    if(stepAmountExpr){
                        i64 constStep = 1;
                        validStep = !TryEvaluateExprConstant(stepAmountExpr, m_symbols, 0u, nullptr, constStep) || constStep > 0;
                    }
                }
            }
            if(!validInit || !validCond || !validStep){
                reportError(errors, stmt.span, "indexed 'parallel_for' requires canonical bounds ('parallel_for(usize i = start; i < end; ++i)')");
                ok = false;
            }
        }
        const usize saved = scope.size();
        if(stmt.initStmt && !checkStatement(ownerClass, method, *stmt.initStmt, scope, errors))
            ok = false;
        if(isPar && saved < scope.size()){
            const MStringView idxType = resolveScopedVarBaseType(scope[saved]);
            if(!idxType.empty() && !BuiltinTypeSemantics::IsIntegerScalarType(idxType) && !FindTemplateParam(ownerClass, &method, idxType)){
                reportError(errors, stmt.span, "indexed 'parallel_for' requires an integer loop variable type");
                ok = false;
            }
        }
        if(isPar)
            ++m_parallelDepth;
        if(stmt.condition && !checkExpr(ownerClass, scope, false, method, *stmt.condition, errors))
            ok = false;
        if(stmt.stepExpr && !checkExpr(ownerClass, scope, false, method, *stmt.stepExpr, errors))
            ok = false;
        if(isPar)
            --m_parallelDepth;
        if(isPar){
            for(usize i = saved; i < scope.size(); ++i)
                scope[i].isConst = true;
        }
        if(!checkLoopOrParallelBody(isPar))
            ok = false;
        while(scope.size() > saved)
            scope.pop_back();
        break;
    }
    case StmtKind::While:
    case StmtKind::DoWhile:{
        if(stmt.condition && !checkExpr(ownerClass, scope, false, method, *stmt.condition, errors))
            ok = false;
        ++m_loopDepth;
        if(!checkStatementList(ownerClass, method, stmt.statements, scope, errors))
            ok = false;
        --m_loopDepth;
        break;
    }
    case StmtKind::NativeBlock:{
        for(usize i = 0u; i < stmt.nativeBindings.size(); ++i){
            const NativeBinding& binding = stmt.nativeBindings[i];
            for(usize j = 0u; j < i; ++j){
                if(!binding.aliasName.empty() && stmt.nativeBindings[j].aliasName == binding.aliasName){
                    reportError(errors, binding.span, "duplicate binding name in 'native(...)' capture list");
                    ok = false;
                    break;
                }
            }
            if(binding.sourceExpr){
                if(binding.mode == ParamPassingMode::Mut)
                    binding.sourceExpr->isMutArg = true;
                if(!checkExpr(ownerClass, scope, binding.mode == ParamPassingMode::Mut, method, *binding.sourceExpr, errors))
                    ok = false;
            }
        }
        break;
    }
    case StmtKind::Diagnostic:{
        for(usize i = 0u; i < stmt.arguments.size(); ++i){
            Expr* arg = stmt.arguments[i];
            if(!arg)
                continue;
            if(!checkExpr(ownerClass, scope, false, method, *arg, errors))
                ok = false;
            const b8 isAfterCond = (stmt.diagnosticKind != DiagnosticKind::Assert) || (i >= 1u);
            if(isAfterCond)
                markStringToPathAdaptation(ownerClass, &method, scope, *arg);
        }
        break;
    }
    default:
        break;
    }

    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

