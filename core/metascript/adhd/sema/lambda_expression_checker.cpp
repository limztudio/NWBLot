// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lambda_expression_checker.h"
#include "type_layout_analyzer.h"


NWB_ADHD_BEGIN


b8 LambdaExpressionChecker::check(ClassDecl* ownerClass, const Span<const ScopedVarInfo> scope, MethodDecl& method, Expr& expr, MVector<ParseError>& errors){
    b8 ok = true;

    if(expr.lambdaReturnType && !m_checker.checkReturnTypeRef(*expr.lambdaReturnType, ownerClass, &method, expr.span, errors))
        ok = false;
    expr.lambdaInjectsCallerArena = expr.lambdaReturnType
        && m_checker.m_symbols.resolveRefMode(expr.lambdaReturnType) == ParamPassingMode::None
        && TypeLayoutAnalyzer(m_checker.m_symbols).requiresMotherArena(*expr.lambdaReturnType);
    MVector<ScopedVarInfo> lambdaScope(m_checker.m_arena);
    for(LambdaCapture& cap : expr.lambdaCaptures){
        if(!cap.name.empty() && m_checker.FindScopedVar(lambdaScope, cap.name)){
            m_checker.reportError(errors, cap.span, "duplicate capture name in lambda");
            ok = false;
        }
        if(cap.name == "this"){
            if(!ownerClass){
                m_checker.reportError(errors, cap.span, "cannot capture 'this' in a free function");
                ok = false;
            }
            else if(cap.mode == ParamPassingMode::Mut && m_checker.isThisConstInScope(method, scope)){
                m_checker.reportError(errors, cap.span, "cannot capture 'this' as 'mut' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
            ScopedVarInfo thisCapVar;
            thisCapVar.name = cap.name;
            thisCapVar.mode = cap.mode;
            thisCapVar.isConst = (cap.mode == ParamPassingMode::In);
            thisCapVar.isParam = true;
            lambdaScope.push_back(thisCapVar);
            continue;
        }
        const ScopedVarInfo* outer = m_checker.FindScopedVar(scope, cap.name);
        const Stmt* globalConst = outer ? nullptr : m_checker.m_symbols.findGlobalConstant(cap.name);
        cap.isGlobalConstant = (globalConst != nullptr);
        if(!outer && !globalConst){
            m_checker.reportError(errors, cap.span, "unknown variable in lambda capture list");
            ok = false;
        }
        if(outer && cap.mode == ParamPassingMode::Mut && (outer->mode == ParamPassingMode::In || outer->isConst)){
            m_checker.reportError(errors, cap.span, "cannot capture 'in' or 'const' variable as 'mut' in lambda");
            ok = false;
        }
        else if(globalConst && cap.mode == ParamPassingMode::Mut){
            m_checker.reportError(errors, cap.span, "cannot capture 'const' global variable as 'mut' in lambda");
            ok = false;
        }
        if(outer && cap.mode == ParamPassingMode::Mut){
            expr.hasMutLocalCapture = true;
            if(IsNonThreadSafeContainer(m_checker.resolveScopedVarBaseType(*outer))){
                expr.hasMutContainerCapture = true;
                if(m_checker.m_parallelDepth > 0u && outer->declaredParallelDepth < m_checker.m_parallelDepth){
                    m_checker.reportError(errors, cap.span, "shared 'mut' capture of non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited");
                    ok = false;
                }
            }
        }
        ScopedVarInfo capVar;
        capVar.name = cap.name;
        capVar.type = outer ? outer->type : (globalConst ? globalConst->varType : nullptr);
        capVar.inferredTypeName = outer ? outer->inferredTypeName : MStringView{};
        capVar.mode = cap.mode;
        capVar.isConst = (cap.mode == ParamPassingMode::In) || (globalConst != nullptr);
        capVar.isParam = true;
        capVar.declaredParallelDepth = outer ? outer->declaredParallelDepth : 0u;
        lambdaScope.push_back(capVar);
    }
    for(const ParamDecl& p : expr.lambdaParams){
        if(!p.name.empty() && m_checker.FindScopedVar(lambdaScope, p.name)){
            m_checker.reportError(errors, p.span, "duplicate parameter or capture name in lambda");
            ok = false;
        }
        if(p.type){
            if(!m_checker.checkTypeRef(*p.type, ownerClass, &method, errors))
                ok = false;
            if(!p.type->isFuncType && SymbolTable::IsSimdStorageType(m_checker.m_symbols.resolveBaseTypeName(p.type->name))){
                m_checker.reportError(errors, p.span, "SIMD memory-layout type cannot be used as a parameter; use 'VectorSingle', 'VectorDouble', 'MatrixSingle', or 'MatrixDouble' instead");
                ok = false;
            }
        }
        ScopedVarInfo paramVar;
        paramVar.name = p.name;
        paramVar.type = p.type;
        paramVar.mode = p.mode;
        paramVar.isConst = (p.mode == ParamPassingMode::In);
        paramVar.isParam = true;
        lambdaScope.push_back(paramVar);
    }
    const b8 savedInLambda = m_checker.m_inLambda;
    const b8 savedLambdaMutatesContainer = m_checker.m_activeLambdaMutatesContainer;
    const TypeRef* savedLambdaRet = m_checker.m_activeLambdaReturnType;
    const u32 savedLoop = m_checker.m_loopDepth;
    const u32 savedSwitch = m_checker.m_switchDepth;
    const b8 savedMethodScratch = method.usesScratchArena;
    const b8 savedMethodTaskScope = method.usesTaskScope;
    const b8 savedParScratch = m_checker.m_activeParallelBlockUsesScratch;
    m_checker.m_inLambda = true;
    m_checker.m_activeLambdaMutatesContainer = false;
    m_checker.m_activeLambdaReturnType = expr.lambdaReturnType;
    m_checker.m_loopDepth = 0u;
    m_checker.m_switchDepth = 0u;
    method.usesScratchArena = false;
    method.usesTaskScope = false;
    m_checker.m_activeParallelBlockUsesScratch = false;
    if(!m_checker.checkStatementList(ownerClass, method, expr.lambdaBody, lambdaScope, errors))
        ok = false;
    if(m_checker.m_activeLambdaMutatesContainer)
        expr.hasMutContainerCapture = true;
    if(expr.lambdaReturnType && !IsVoidTypeRef(m_checker.m_symbols.unwrapTypeAlias(expr.lambdaReturnType))){
        if(!m_checker.StatementsContainReturnOrNative(expr.lambdaBody)){
            m_checker.reportError(errors, expr.span, "non-void lambda must contain a 'return' statement");
            ok = false;
        }
    }
    expr.lambdaUsesScratchArena = method.usesScratchArena;
    expr.lambdaUsesTaskScope = method.usesTaskScope;
    method.usesScratchArena = savedMethodScratch;
    method.usesTaskScope = savedMethodTaskScope;
    m_checker.m_activeParallelBlockUsesScratch = savedParScratch;
    m_checker.m_inLambda = savedInLambda;
    m_checker.m_activeLambdaMutatesContainer = savedLambdaMutatesContainer;
    m_checker.m_activeLambdaReturnType = savedLambdaRet;
    m_checker.m_loopDepth = savedLoop;
    m_checker.m_switchDepth = savedSwitch;

    return ok;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

