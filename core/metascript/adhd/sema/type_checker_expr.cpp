// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_checker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TypeChecker::checkExpr(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Expr& expr,
    const MVector<ScopedVarInfo>& scope,
    const b8 isMutationTarget,
    MVector<ParseError>& errors
){
    b8 ok = true;

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::StringLiteral:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot pass literal or temporary to a 'mut' target");
            ok = false;
        }
        break;
    }
    case ExprKind::Identifier:{
        const ScopedVarInfo* local = findScopedVar(scope, expr.text);
        if(local){
            const ParamPassingMode::Enum resolvedLocalRefMode = m_symbols.resolveRefMode(local->type);
            if((!local->isParam && local->mode != ParamPassingMode::None) || resolvedLocalRefMode != ParamPassingMode::None)
                expr.derefNotNull = true;

            if(isMutationTarget){
                if(local->mode == ParamPassingMode::In || (local->isParam && resolvedLocalRefMode == ParamPassingMode::In)){
                    reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                    ok = false;
                }
                else if(local->isConst){
                    reportError(errors, expr.span, "cannot mutate 'const' variable");
                    ok = false;
                }
                else if(m_parallelDepth > 0u && local->declaredParallelDepth < m_parallelDepth && local->type && IsNonThreadSafeContainer(m_symbols.resolveBaseTypeName(local->type->name))){
                    reportError(errors, expr.span, "shared 'mut' access to non-thread-safe container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                    ok = false;
                }
            }
        }
        else{
            b8 reportedUnqualifiedMember = false;
            if(ownerClass && (findClassField(*ownerClass, expr.text) || findClassMethod(*ownerClass, expr.text))){
                reportError(errors, expr.span, "class member must be accessed explicitly via 'this.member' in adhd");
                ok = false;
                reportedUnqualifiedMember = true;
            }
            if(!reportedUnqualifiedMember && isMutationTarget){
                if(m_symbols.findGlobalConstant(expr.text) || HasConstValueTemplateParam(ownerClass, &method, expr.text))
                    reportError(errors, expr.span, "cannot mutate 'const' variable");
                else
                    reportError(errors, expr.span, "cannot mutate non-variable identifier");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::ThisMember:{
        if(!ownerClass){
            reportError(errors, expr.span, "'this' cannot be used in a free function");
            ok = false;
            break;
        }
        const ScopedVarInfo* lambdaThis = m_inLambda ? findScopedVar(scope, "this") : nullptr;
        if(m_inLambda && !lambdaThis){
            reportError(errors, expr.span, "accessing 'this' inside a lambda requires capturing 'in this' or 'mut this'");
            ok = false;
        }
        const b8 isConstThis = method.isConst || (lambdaThis && lambdaThis->isConst);
        const FieldDecl* field = findClassField(*ownerClass, expr.text);
        const MethodDecl* memberMethod = findClassMethod(*ownerClass, expr.text);
        if(!field && !memberMethod){
            reportError(errors, expr.span, "unknown member on 'this'");
            ok = false;
            break;
        }
        const ParamPassingMode::Enum fieldRefMode = field ? m_symbols.resolveRefMode(field->type) : ParamPassingMode::None;
        if(fieldRefMode != ParamPassingMode::None)
            expr.derefNotNull = true;

        if(isMutationTarget){
            if(isConstThis){
                reportError(errors, expr.span, "cannot mutate 'this.member' inside a 'const' method or 'in this' lambda");
                ok = false;
            }
            else if(fieldRefMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' reference member");
                ok = false;
            }
            else if(m_parallelDepth > 0u && field && field->type && IsNonThreadSafeContainer(m_symbols.resolveBaseTypeName(field->type->name))){
                reportError(errors, expr.span, "shared 'mut' access to non-thread-safe member container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::ScopeAccess:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot mutate or pass scope-qualified constant/enumerator to a 'mut' target");
            ok = false;
        }
        if(!expr.scopeChain.empty()){
            if(const ScopedVarInfo* local = findScopedVar(scope, expr.scopeChain[0])){
                const MStringView localTypeName = !local->inferredTypeName.empty()
                    ? local->inferredTypeName
                    : (local->type ? m_symbols.resolveBaseTypeName(local->type->name) : MStringView{});
                if(!localTypeName.empty() && m_symbols.findEnum(localTypeName))
                    expr.scopeChain[0] = localTypeName;
            }
            else{
                const MStringView resolvedRoot = m_symbols.resolveBaseTypeName(expr.scopeChain[0]);
                if(!resolvedRoot.empty() && m_symbols.findEnum(resolvedRoot))
                    expr.scopeChain[0] = resolvedRoot;
            }
            if(const EnumDecl* enumDecl = m_symbols.findEnum(expr.scopeChain[0])){
                if(expr.scopeChain.size() < 2u || expr.scopeChain.size() > 3u){
                    reportError(errors, expr.span, "invalid enum scope access; expected 'EnumName::Entry', 'EnumName::Enum::Entry', or 'EnumName::Flag::Entry'");
                    ok = false;
                }
                else{
                    const MStringView entryName = expr.scopeChain.back();
                    const EnumEntry* matchedEntry = nullptr;
                    for(const EnumEntry& entry : enumDecl->entries){
                        if(entry.name == entryName){
                            matchedEntry = &entry;
                            break;
                        }
                    }
                    if(!matchedEntry){
                        reportError(errors, expr.span, "unknown enumerator in enum scope access");
                        ok = false;
                    }
                    else if(expr.scopeChain.size() == 3u){
                        const MStringView qualifier = expr.scopeChain[1];
                        if(qualifier != "Enum" && qualifier != "Flag"){
                            reportError(errors, expr.span, "invalid enum scope qualifier; expected '::Enum::' or '::Flag::'");
                            ok = false;
                        }
                        else if(qualifier == "Enum" && matchedEntry->isFlag){
                            reportError(errors, expr.span, "enumerator declared with 'flag' must be accessed via '::Flag::', not '::Enum::'");
                            ok = false;
                        }
                        else if(qualifier == "Flag" && !matchedEntry->isFlag){
                            reportError(errors, expr.span, "enumerator declared with 'enum' must be accessed via '::Enum::', not '::Flag::'");
                            ok = false;
                        }
                    }
                }
            }
            else{
                reportError(errors, expr.span, "invalid scope resolution '::'; only 'enum' types and 'enum' variables support '::' access in adhd");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, isMutationTarget, errors))
            ok = false;

        if(expr.lhs){
            if(expr.lhs->derefNotNull){
                expr.lhs->derefNotNull = false;
                expr.isPointerMemberAccess = true;
            }
            const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, &method, *expr.lhs, scope);
            const MStringView baseType = lhsTypeRef
                ? m_symbols.resolveBaseTypeName(lhsTypeRef->name)
                : inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
            u32 simdLanes = 4u;
            if(SymbolTable::isSimdVectorType(baseType, simdLanes)){
                b8 isSwizzlePattern = false;
                b8 validLanes = true;
                if(CheckSwizzlePattern(expr.text, simdLanes, isSwizzlePattern, validLanes) && isSwizzlePattern){
                    if(!validLanes){
                        reportError(errors, expr.span, "invalid SIMD swizzle component for vector width or mixed xyzw/rgba set");
                        ok = false;
                    }
                    else if(expr.text.size() > 1u && (baseType == "f32x3_i32" || baseType == "f32x3_u32" || baseType == "f64x3_i64" || baseType == "f64x3_u64")){
                        reportError(errors, expr.span, "multi-lane SIMD swizzling is not supported on mixed float/integer vector types");
                        ok = false;
                    }
                    else if(expr.text.size() > 1u){
                        expr.isSwizzle = true;
                    }
                }
            }
            else if(u32 unalignedLanes = 0u; SymbolTable::isUnalignedVectorType(baseType, unalignedLanes)){
                b8 isSwizzlePattern = false;
                b8 validLanes = true;
                if(!CheckSwizzlePattern(expr.text, unalignedLanes, isSwizzlePattern, validLanes) || !isSwizzlePattern || !validLanes || expr.text.size() != 1u){
                    reportError(errors, expr.span, "invalid component access on unaligned vector type; only single-lane .x/.y/.z/.w or .r/.g/.b/.a within vector width is supported");
                    ok = false;
                }
            }
            else if(const ClassDecl* targetCls = m_symbols.findClass(baseType)){
                const FieldDecl* field = findClassField(*targetCls, expr.text);
                const MethodDecl* memberMethod = findClassMethod(*targetCls, expr.text);
                if(!field && !memberMethod){
                    reportError(errors, expr.span, "unknown member on class");
                    ok = false;
                }
                else if(field && !memberMethod){
                    if(ownerClass != targetCls && field->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot access 'private' or 'protected' field outside of its declaring class");
                        ok = false;
                    }
                    const TypeRef* ft = (lhsTypeRef && !targetCls->templateParams.empty() && !lhsTypeRef->templateArgs.empty() && field->type)
                        ? m_symbols.substituteTypeParams(*field->type, targetCls->templateParams, lhsTypeRef->templateArgs)
                        : field->type;
                    const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(ft);
                    if(fieldRefMode != ParamPassingMode::None)
                        expr.derefNotNull = true;
                    if(isMutationTarget && fieldRefMode == ParamPassingMode::In){
                        reportError(errors, expr.span, "cannot mutate 'in' reference member");
                        ok = false;
                    }
                    else if(isMutationTarget && m_parallelDepth > 0u && ft && IsNonThreadSafeContainer(m_symbols.resolveBaseTypeName(ft->name))){
                        b8 isTaskLocalOwner = false;
                        if(expr.lhs->kind == ExprKind::Identifier){
                            if(const ScopedVarInfo* lhsLocal = findScopedVar(scope, expr.lhs->text))
                                isTaskLocalOwner = (lhsLocal->declaredParallelDepth == m_parallelDepth);
                        }
                        if(!isTaskLocalOwner){
                            reportError(errors, expr.span, "shared 'mut' access to non-thread-safe member container inside 'parallel' or 'parallel_for' is prohibited; use Parallel* containers or disjoint element indexing");
                            ok = false;
                        }
                    }
                }
            }
        }
        break;
    }
    case ExprKind::Index:{
        const u32 savedParallelDepth = m_parallelDepth;
        b8 lhsNeedsMut = isMutationTarget;
        b8 rhsNeedsMut = false;
        const MethodDecl* customOpIdx = nullptr;
        const TypeRef* containerTypeRef = expr.lhs ? inferExprTypeRef(ownerClass, &method, *expr.lhs, scope) : nullptr;
        const MStringView lhsBaseType = containerTypeRef
            ? m_symbols.resolveBaseTypeName(containerTypeRef->name)
            : (expr.lhs ? inferExprTypeName(ownerClass, &method, *expr.lhs, scope) : MStringView{});
        if(lhsBaseType != "Map" && lhsBaseType != "HashMap")
            m_parallelDepth = 0u;
        u32 vecIdxLanes = 0u;
        if(SymbolTable::isSimdVectorType(lhsBaseType, vecIdxLanes) || SymbolTable::isUnalignedVectorType(lhsBaseType, vecIdxLanes))
            expr.isVectorRawIndex = true;
        const b8 recvIsConst = expr.lhs ? isReceiverConst(ownerClass, method, *expr.lhs, scope) : false;
        const usize oneArg = 1u;
        if(const ClassDecl* cls = m_symbols.findClass(lhsBaseType)){
            customOpIdx = findClassOperator(*cls, "[]", &oneArg, recvIsConst, ownerClass != cls);
            if(!customOpIdx){
                reportError(errors, expr.span, "indexing custom class requires 'operator[]' to be defined on the class");
                ok = false;
            }
            else{
                if(ownerClass != cls && customOpIdx->access != AccessSpecifier::Public){
                    reportError(errors, expr.span, "cannot call 'private' or 'protected' operator outside of its declaring class");
                    ok = false;
                }
                lhsNeedsMut = !customOpIdx->isConst;
                if(!customOpIdx->params.empty() && customOpIdx->params[0].mode == ParamPassingMode::Mut)
                    rhsNeedsMut = true;
            }
        }
        else if(containerTypeRef && m_symbols.findInterface(lhsBaseType)){
            customOpIdx = m_symbols.findInterfaceMethod(*containerTypeRef, "[]", &oneArg);
            if(customOpIdx){
                lhsNeedsMut = !customOpIdx->isConst;
                if(!customOpIdx->params.empty() && customOpIdx->params[0].mode == ParamPassingMode::Mut)
                    rhsNeedsMut = true;
            }
        }
        if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, lhsNeedsMut, errors))
            ok = false;
        m_parallelDepth = savedParallelDepth;
        if(expr.rhs && !checkExpr(ownerClass, method, *expr.rhs, scope, rhsNeedsMut, errors))
            ok = false;

        const TypeRef* indexedElemType = inferExprTypeRef(ownerClass, &method, expr, scope);
        const ParamPassingMode::Enum elemRefMode = m_symbols.resolveRefMode(indexedElemType);
        if(elemRefMode != ParamPassingMode::None)
            expr.derefNotNull = true;
        if(isMutationTarget){
            if(elemRefMode == ParamPassingMode::In){
                reportError(errors, expr.span, "cannot mutate 'in' parameter or read-only borrow");
                ok = false;
            }
            else if(customOpIdx && customOpIdx->returnType && elemRefMode == ParamPassingMode::None){
                reportError(errors, expr.span, "cannot mutate or pass temporary rvalue return value to a 'mut' target");
                ok = false;
            }
        }
        break;
    }
    case ExprKind::Call:
        if(!checkCallExpr(ownerClass, method, expr, scope, isMutationTarget, errors))
            ok = false;
        break;
    case ExprKind::Unary:{
        b8 mutates = (expr.op == TokenType::PlusPlus || expr.op == TokenType::MinusMinus);
        if(expr.lhs){
            const MStringView lhsType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
            if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
                const MStringView opSym = MapTokenTypeToOperatorSymbol(expr.op);
                const usize zeroArgs = 0u;
                const b8 recvIsConst = isReceiverConst(ownerClass, method, *expr.lhs, scope);
                if(const MethodDecl* opMethod = findClassOperator(*cls, opSym, &zeroArgs, recvIsConst, ownerClass != cls)){
                    if(ownerClass != cls && opMethod->access != AccessSpecifier::Public){
                        reportError(errors, expr.span, "cannot call 'private' or 'protected' operator outside of its declaring class");
                        ok = false;
                    }
                    if(!opMethod->isConst)
                        mutates = true;
                }
            }
            if(!checkExpr(ownerClass, method, *expr.lhs, scope, mutates, errors))
                ok = false;
        }
        break;
    }
    case ExprKind::Binary:
        if(isMutationTarget && !IsCompoundOrSimpleAssignment(expr.op)){
            reportError(errors, expr.span, "cannot pass temporary or rvalue expression to a 'mut' target");
            ok = false;
        }
        if(!checkBinaryExpr(ownerClass, method, expr, scope, errors))
            ok = false;
        break;
    case ExprKind::Cast:
    case ExprKind::BitCast:{
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot pass cast result (rvalue) to a 'mut' target");
            ok = false;
        }
        if(expr.targetType){
            if(!checkTypeRef(*expr.targetType, ownerClass, &method, errors))
                ok = false;
            if(m_symbols.findInterface(m_symbols.resolveBaseTypeName(expr.targetType->name))){
                reportError(errors, expr.span, "casting to an 'interface' type is prohibited in adhd");
                ok = false;
            }
        }
        if(expr.lhs){
            if(!checkExpr(ownerClass, method, *expr.lhs, scope, false, errors))
                ok = false;
            const MStringView srcType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
            if(!srcType.empty() && m_symbols.findInterface(srcType)){
                reportError(errors, expr.span, "downcasting from an 'interface' type is prohibited in adhd");
                ok = false;
            }
            if(expr.kind == ExprKind::BitCast && expr.targetType && !srcType.empty()){
                const MStringView dstType = m_symbols.resolveBaseTypeName(expr.targetType->name);
                const auto hasKnownFixedLayout = [&](const MStringView baseName)noexcept -> b8{
                    if(baseName.empty() || findTemplateParam(ownerClass, &method, baseName))
                        return false;
                    return IsIntegerScalarType(baseName) || baseName == "b8" || IsFloatScalarType(baseName)
                        || SymbolTable::isSimdType(baseName) || SymbolTable::isUnalignedVectorType(baseName)
                        || m_symbols.findEnum(baseName) != nullptr;
                };
                if(m_symbols.resolveRefMode(expr.targetType) == ParamPassingMode::None
                    && hasKnownFixedLayout(dstType)
                    && hasKnownFixedLayout(srcType))
                {
                    TypeRef srcTypeRef(m_arena);
                    srcTypeRef.name = srcType;
                    usize dstSize = 0u;
                    usize dstAlign = 0u;
                    usize srcSize = 0u;
                    usize srcAlign = 0u;
                    m_layoutOptimizer.resolveTypeSizeAndAlign(*expr.targetType, dstSize, dstAlign);
                    m_layoutOptimizer.resolveTypeSizeAndAlign(srcTypeRef, srcSize, srcAlign);
                    if(dstSize != srcSize){
                        reportError(errors, expr.span, "bit_cast requires source and target types to have identical byte sizes");
                        ok = false;
                    }
                }
            }
        }
        break;
    }
    case ExprKind::Lambda:
        if(isMutationTarget){
            reportError(errors, expr.span, "cannot pass lambda temporary to a 'mut' target");
            ok = false;
        }
        if(!checkLambdaExpr(ownerClass, method, expr, scope, errors))
            ok = false;
        break;
    default:
        break;
    }

    const MStringView inferredType = inferExprTypeName(ownerClass, &method, expr, scope);
    if(SymbolTable::isSimdType(inferredType))
        expr.resolvedSimdType = inferredType;

    return ok;
}

b8 TypeChecker::checkBinaryExpr(
    ClassDecl* ownerClass,
    MethodDecl& method,
    Expr& expr,
    const MVector<ScopedVarInfo>& scope,
    MVector<ParseError>& errors
){
    b8 ok = true;

    const b8 isAssign = IsCompoundOrSimpleAssignment(expr.op);
    b8 lhsNeedsMut = isAssign;
    b8 rhsNeedsMut = false;
    if(expr.lhs && expr.op != TokenType::Equal){
        const MStringView lhsType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
        if(const ClassDecl* cls = m_symbols.findClass(lhsType)){
            const MStringView opSym = MapTokenTypeToOperatorSymbol(expr.op);
            const usize oneArg = 1u;
            const b8 recvIsConst = isReceiverConst(ownerClass, method, *expr.lhs, scope);
            if(const MethodDecl* opMethod = findClassOperator(*cls, opSym, &oneArg, recvIsConst, ownerClass != cls)){
                if(ownerClass != cls && opMethod->access != AccessSpecifier::Public){
                    reportError(errors, expr.span, "cannot call 'private' or 'protected' operator outside of its declaring class");
                    ok = false;
                }
                if(!opMethod->isConst)
                    lhsNeedsMut = true;
                if(!opMethod->params.empty() && opMethod->params[0].mode == ParamPassingMode::Mut)
                    rhsNeedsMut = true;
            }
        }
    }
    if(expr.op == TokenType::Equal && expr.lhs){
        if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text); field && field->type){
                const MStringView baseFieldTypeName = m_symbols.resolveBaseTypeName(field->type->name);
                const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(field->type);
                if(fieldRefMode != ParamPassingMode::None && (method.isConstructor || m_symbols.findInterface(baseFieldTypeName))){
                    const ScopedVarInfo* lambdaThis = m_inLambda ? findScopedVar(scope, "this") : nullptr;
                    if(method.isConst || (lambdaThis && lambdaThis->isConst)){
                        reportError(errors, expr.lhs->span, "cannot mutate 'this.member' inside a 'const' method or 'in this' lambda");
                        ok = false;
                    }
                    lhsNeedsMut = false;
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::Identifier){
            if(const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text); local && local->type && !local->isParam){
                const MStringView baseLocalTypeName = m_symbols.resolveBaseTypeName(local->type->name);
                const ParamPassingMode::Enum localRefMode = (local->mode != ParamPassingMode::None) ? local->mode : m_symbols.resolveRefMode(local->type);
                if(localRefMode != ParamPassingMode::None && m_symbols.findInterface(baseLocalTypeName))
                    lhsNeedsMut = false;
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess || expr.lhs->kind == ExprKind::Index){
            const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, &method, *expr.lhs, scope);
            const ParamPassingMode::Enum lhsRefMode = m_symbols.resolveRefMode(lhsTypeRef);
            const MStringView baseLhsTypeName = lhsTypeRef ? m_symbols.resolveBaseTypeName(lhsTypeRef->name) : MStringView{};
            if(lhsRefMode != ParamPassingMode::None && m_symbols.findInterface(baseLhsTypeName)){
                lhsNeedsMut = false;
                if(expr.lhs->lhs){
                    const b8 savedDeref = expr.lhs->lhs->derefNotNull;
                    if(!checkExpr(ownerClass, method, *expr.lhs->lhs, scope, true, errors))
                        ok = false;
                    expr.lhs->lhs->derefNotNull = savedDeref;
                }
            }
        }
    }
    if(expr.lhs && !checkExpr(ownerClass, method, *expr.lhs, scope, lhsNeedsMut, errors))
        ok = false;
    b8 rhsInClassArenaCtx = false;
    if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::ThisMember && ownerClass){
        if(const FieldDecl* field = findClassField(*ownerClass, expr.lhs->text)){
            const MStringView baseFieldTypeName = field->type ? m_symbols.resolveBaseTypeName(field->type->name) : MStringView{};
            if(field->type && SymbolTable::isArenaBackedContainer(baseFieldTypeName))
                rhsInClassArenaCtx = true;
            const ParamPassingMode::Enum fieldRefMode = field->type ? m_symbols.resolveRefMode(field->type) : ParamPassingMode::None;
            if(fieldRefMode != ParamPassingMode::None && (method.isConstructor || m_symbols.findInterface(baseFieldTypeName))){
                expr.lhs->derefNotNull = false;
                expr.isNotNullRebind = true;
                if(fieldRefMode == ParamPassingMode::Mut)
                    rhsNeedsMut = true;
                if(expr.rhs && m_symbols.findInterface(baseFieldTypeName)
                    && !verifyConcreteClassImplementsInterface(
                        ownerClass,
                        method,
                        *expr.rhs,
                        field->type,
                        scope,
                        expr.span,
                        "cannot bind interface reference member to concrete class that does not implement the interface",
                        errors
                    ))
                {
                    ok = false;
                }
            }
            propagateFuncCaptureBytes(field->type, expr.rhs, scope, m_symbols);
        }
    }
    if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const ScopedVarInfo* local = findScopedVar(scope, expr.lhs->text);
        if(local && local->type){
            const MStringView baseLocalTypeName = m_symbols.resolveBaseTypeName(local->type->name);
            if(expr.lhs->derefNotNull && m_symbols.findInterface(baseLocalTypeName)){
                expr.lhs->derefNotNull = false;
                expr.isNotNullRebind = true;
                if(local->mode == ParamPassingMode::Mut || m_symbols.resolveRefMode(local->type) == ParamPassingMode::Mut)
                    rhsNeedsMut = true;
                if(expr.rhs && !verifyConcreteClassImplementsInterface(
                    ownerClass,
                    method,
                    *expr.rhs,
                    local->type,
                    scope,
                    expr.span,
                    "cannot rebind interface reference to concrete class that does not implement the interface",
                    errors
                )){
                    ok = false;
                }
            }
            propagateFuncCaptureBytes(local->type, expr.rhs, scope, m_symbols);
            if(expr.rhs){
                ScopedVarInfo* mutableLocal = const_cast<ScopedVarInfo*>(local);
                if(expr.rhs->kind == ExprKind::Lambda){
                    mutableLocal->lambdaExpr = expr.rhs;
                }
                else if(expr.rhs->kind == ExprKind::Identifier){
                    const ScopedVarInfo* rhsLocal = findScopedVar(scope, expr.rhs->text);
                    mutableLocal->lambdaExpr = rhsLocal ? rhsLocal->lambdaExpr : nullptr;
                }
                else{
                    mutableLocal->lambdaExpr = nullptr;
                }
            }
        }
    }
    if(expr.op == TokenType::Equal && expr.lhs && (expr.lhs->kind == ExprKind::MemberAccess || expr.lhs->kind == ExprKind::Index)){
        const TypeRef* lhsTypeRef = inferExprTypeRef(ownerClass, &method, *expr.lhs, scope);
        const ParamPassingMode::Enum lhsRefMode = m_symbols.resolveRefMode(lhsTypeRef);
        const MStringView baseLhsTypeName = lhsTypeRef ? m_symbols.resolveBaseTypeName(lhsTypeRef->name) : MStringView{};
        if(lhsRefMode != ParamPassingMode::None && m_symbols.findInterface(baseLhsTypeName)){
            expr.lhs->derefNotNull = false;
            expr.isNotNullRebind = true;
            if(lhsRefMode == ParamPassingMode::Mut)
                rhsNeedsMut = true;
            if(expr.rhs && !verifyConcreteClassImplementsInterface(
                ownerClass,
                method,
                *expr.rhs,
                lhsTypeRef,
                scope,
                expr.span,
                "cannot rebind interface reference to concrete class that does not implement the interface",
                errors
            )){
                ok = false;
            }
        }
    }
    if(expr.rhs){
        if(rhsNeedsMut)
            expr.rhs->isMutArg = true;
        const b8 savedClassArenaCtx = m_inClassArenaContext;
        if(rhsInClassArenaCtx)
            m_inClassArenaContext = true;
        if(!checkExpr(ownerClass, method, *expr.rhs, scope, rhsNeedsMut, errors))
            ok = false;
        m_inClassArenaContext = savedClassArenaCtx;
        if(expr.op == TokenType::Equal
            && expr.lhs && expr.lhs->kind == ExprKind::ThisMember
            && isEscapingMutCaptureLambda(expr.rhs, scope))
        {
            reportError(errors, expr.span, "cannot store lambda with 'mut' capture into 'this.member'; mutable reference captures cannot escape their enclosing scope");
            ok = false;
        }
    }
    if(expr.lhs && expr.rhs){
        const MStringView lhsType = inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
        if(lhsType == "Path")
            markStringToPathAdaptation(ownerClass, method, *expr.rhs, scope);
    }

    return ok;
}

b8 TypeChecker::isReceiverConst(
    ClassDecl* ownerClass,
    const MethodDecl& method,
    const Expr& recv,
    const MVector<ScopedVarInfo>& scope
)const noexcept{
    if(recv.kind == ExprKind::Identifier){
        if(const ScopedVarInfo* local = findScopedVar(scope, recv.text)){
            if(local->mode == ParamPassingMode::In || local->isConst)
                return true;
            const ParamPassingMode::Enum localRefMode = m_symbols.resolveRefMode(local->type);
            if(localRefMode != ParamPassingMode::None)
                return localRefMode == ParamPassingMode::In;
            return false;
        }
        if(m_symbols.findGlobalConstant(recv.text))
            return true;
    }
    else if(recv.kind == ExprKind::ThisMember && ownerClass){
        const ScopedVarInfo* lambdaThis = m_inLambda ? findScopedVar(scope, "this") : nullptr;
        const b8 isConstThis = method.isConst || (lambdaThis && lambdaThis->isConst);
        if(const FieldDecl* field = findClassField(*ownerClass, recv.text)){
            const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(field->type);
            if(fieldRefMode != ParamPassingMode::None)
                return fieldRefMode == ParamPassingMode::In;
        }
        return isConstThis;
    }
    else if(recv.kind == ExprKind::MemberAccess && recv.lhs){
        if(const TypeRef* ft = inferExprTypeRef(ownerClass, &method, recv, scope)){
            const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(ft);
            if(fieldRefMode != ParamPassingMode::None)
                return fieldRefMode == ParamPassingMode::In;
        }
        return isReceiverConst(ownerClass, method, *recv.lhs, scope);
    }
    else if(recv.kind == ExprKind::Index || recv.kind == ExprKind::Call){
        if(const TypeRef* rt = inferExprTypeRef(ownerClass, &method, recv, scope)){
            const ParamPassingMode::Enum refMode = m_symbols.resolveRefMode(rt);
            if(refMode != ParamPassingMode::None)
                return refMode == ParamPassingMode::In;
        }
        if(recv.kind == ExprKind::Index && recv.lhs)
            return isReceiverConst(ownerClass, method, *recv.lhs, scope);
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
