// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/operator_semantics.h>

#include "binary_expression_checker.h"
#include "borrow_lifetime_analyzer.h"
#include "lambda_capture_analyzer.h"
#include "type_layout_analyzer.h"


NWB_ADHD_BEGIN


b8 BinaryExpressionChecker::applyInterfaceRefRebind(
    const ClassDecl* ownerClass,
    const MethodDecl& method,
    const TypeRef* targetType,
    const ParamPassingMode::Enum targetRefMode,
    const Span<const ScopedVarInfo> scope,
    const MStringView errorMessage,
    Expr& assignExpr,
    MVector<ParseError>& errors,
    b8& inOutRhsNeedsMut
){
    assignExpr.lhs->derefNotNull = false;
    assignExpr.isNotNullRebind = true;
    if(targetRefMode == ParamPassingMode::Mut){
        inOutRhsNeedsMut = true;
    }
    else if(targetRefMode == ParamPassingMode::In && assignExpr.rhs){
        if(m_checker.isRvalueTemporaryExpr(ownerClass, &method, *assignExpr.rhs, scope)){
            m_checker.reportError(errors, assignExpr.span, "cannot rebind 'in' reference to a literal or rvalue temporary");
            return false;
        }
        if(assignExpr.rhs->isStringByteRead){
            assignExpr.rhs->isStringByteRead = false;
            assignExpr.rhs->isStringByteConstRef = true;
        }
    }
    const MStringView baseTypeName = targetType ? m_checker.m_symbols.resolveBaseTypeName(targetType->name) : MStringView{};
    if(assignExpr.rhs && !baseTypeName.empty() && m_checker.m_symbols.findInterface(baseTypeName)){
        return m_checker.verifyConcreteClassImplementsInterface(ownerClass, method, *assignExpr.rhs, targetType, scope, assignExpr.span, errorMessage, errors);
    }
    return true;
}

b8 BinaryExpressionChecker::check(ClassDecl* ownerClass, const Span<const ScopedVarInfo> scope, MethodDecl& method, Expr& expr, MVector<ParseError>& errors){
    b8 ok = true;

    const b8 isAssign = OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op);
    b8 lhsNeedsMut = isAssign;
    b8 rhsNeedsMut = false;
    if(expr.lhs && expr.op != TokenType::Equal){
        const TypeRef* lhsTypeRef = nullptr;
        const MStringView lhsType = m_checker.inferExprBaseType(ownerClass, &method, *expr.lhs, scope, &lhsTypeRef);
        const MStringView opSym = OperatorSemantics::MapTokenTypeToOperatorSymbol(expr.op);
        const b8 recvIsConst = m_checker.isReceiverConst(ownerClass, method, *expr.lhs, scope);
        const ClassDecl* opDeclCls = nullptr;
        if(const MethodDecl* opMethod = m_checker.resolveCustomOperator(
            ownerClass,
            &method,
            lhsTypeRef,
            lhsType,
            opSym,
            1u,
            recvIsConst,
            &opDeclCls
        )){
            if(opDeclCls && ownerClass != opDeclCls && opMethod->access != AccessSpecifier::Public){
                m_checker.reportError(errors, expr.span, "cannot call 'private' or 'protected' operator outside of its declaring class");
                ok = false;
            }
            if(!opMethod->isConst)
                lhsNeedsMut = true;
            if(!opMethod->params.empty() && opMethod->params[0].mode == ParamPassingMode::Mut)
                rhsNeedsMut = true;
        }
        else if(opDeclCls || m_checker.m_symbols.findInterface(lhsType) || m_checker.FindTemplateParam(ownerClass, &method, lhsType)){
            m_checker.reportError(errors, expr.span, "binary operator is not defined on operand type");
            ok = false;
        }
    }
    const FieldDecl* lhsThisField = nullptr;
    const ScopedVarInfo* lhsLocal = nullptr;
    const TypeRef* lhsMemberOrIndexTypeRef = nullptr;
    if(expr.op == TokenType::Equal && expr.lhs){
        if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
            if(expr.lhs->text.empty()){
                m_checker.reportError(errors, expr.lhs->span, "cannot assign to 'this'");
                ok = false;
            }
            lhsThisField = SymbolTable::FindClassField(*ownerClass, expr.lhs->text);
            if(lhsThisField && lhsThisField->type){
                const MStringView baseFieldTypeName = m_checker.m_symbols.resolveBaseTypeName(lhsThisField->type->name);
                const ParamPassingMode::Enum fieldRefMode = m_checker.m_symbols.resolveRefMode(lhsThisField->type);
                if(fieldRefMode != ParamPassingMode::None && (method.isConstructor || m_checker.m_symbols.findInterface(baseFieldTypeName) || m_checker.m_symbols.isAbstractBuiltinType(baseFieldTypeName))){
                    if(m_checker.isThisConstInScope(method, scope)){
                        m_checker.reportError(errors, expr.lhs->span, "cannot mutate 'this.member' inside a 'const' method or 'in this' lambda");
                        ok = false;
                    }
                    lhsNeedsMut = false;
                }
            }
        }
        else if(expr.lhs->kind == ExprKind::Identifier){
            lhsLocal = m_checker.FindScopedVar(scope, expr.lhs->text);
            if(lhsLocal && lhsLocal->type && !lhsLocal->isParam){
                const MStringView baseLocalTypeName = m_checker.m_symbols.resolveBaseTypeName(lhsLocal->type->name);
                const ParamPassingMode::Enum localRefMode = (lhsLocal->mode != ParamPassingMode::None) ? lhsLocal->mode : m_checker.m_symbols.resolveRefMode(lhsLocal->type);
                if(localRefMode != ParamPassingMode::None && (m_checker.m_symbols.findInterface(baseLocalTypeName) || m_checker.m_symbols.isAbstractBuiltinType(baseLocalTypeName)))
                    lhsNeedsMut = false;
            }
        }
        else if(expr.lhs->kind == ExprKind::MemberAccess || expr.lhs->kind == ExprKind::Index){
            lhsMemberOrIndexTypeRef = m_checker.inferExprTypeRef(ownerClass, &method, *expr.lhs, scope);
            const ParamPassingMode::Enum lhsRefMode = m_checker.m_symbols.resolveRefMode(lhsMemberOrIndexTypeRef);
            const MStringView baseLhsTypeName = lhsMemberOrIndexTypeRef ? m_checker.m_symbols.resolveBaseTypeName(lhsMemberOrIndexTypeRef->name) : MStringView{};
            if(lhsRefMode != ParamPassingMode::None && (m_checker.m_symbols.findInterface(baseLhsTypeName) || m_checker.m_symbols.isAbstractBuiltinType(baseLhsTypeName))){
                lhsNeedsMut = false;
                if(expr.lhs->lhs){
                    const b8 savedDeref = expr.lhs->lhs->derefNotNull;
                    if(!m_checker.checkExpr(ownerClass, scope, true, method, *expr.lhs->lhs, errors))
                        ok = false;
                    expr.lhs->lhs->derefNotNull = savedDeref;
                }
            }
        }
    }
    if(expr.lhs && !m_checker.checkExpr(ownerClass, scope, lhsNeedsMut, method, *expr.lhs, errors))
        ok = false;
    b8 rhsInClassArenaCtx = false;
    if(lhsThisField){
        const MStringView baseFieldTypeName = lhsThisField->type ? m_checker.m_symbols.resolveBaseTypeName(lhsThisField->type->name) : MStringView{};
        if(lhsThisField->type && TypeLayoutAnalyzer(m_checker.m_symbols).requiresMotherArena(*lhsThisField->type))
            rhsInClassArenaCtx = true;
        const ParamPassingMode::Enum fieldRefMode = lhsThisField->type ? m_checker.m_symbols.resolveRefMode(lhsThisField->type) : ParamPassingMode::None;
        if(fieldRefMode != ParamPassingMode::None && (method.isConstructor || m_checker.m_symbols.findInterface(baseFieldTypeName) || m_checker.m_symbols.isAbstractBuiltinType(baseFieldTypeName))){
            if(!applyInterfaceRefRebind(
                ownerClass,
                method,
                lhsThisField->type,
                fieldRefMode,
                scope,
                "cannot bind interface reference member to concrete class that does not implement the interface",
                expr,
                errors,
                rhsNeedsMut
            )){
                ok = false;
            }
        }
        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(lhsThisField->type, expr.rhs);
    }
    else if(lhsLocal && lhsLocal->type){
        const MStringView baseLocalTypeName = m_checker.m_symbols.resolveBaseTypeName(lhsLocal->type->name);
        if(expr.lhs->derefNotNull && (m_checker.m_symbols.findInterface(baseLocalTypeName) || m_checker.m_symbols.isAbstractBuiltinType(baseLocalTypeName))){
            const ParamPassingMode::Enum localRefMode = (lhsLocal->mode == ParamPassingMode::Mut || m_checker.m_symbols.resolveRefMode(lhsLocal->type) == ParamPassingMode::Mut)
                ? ParamPassingMode::Mut
                : ParamPassingMode::In;
            if(!applyInterfaceRefRebind(
                ownerClass,
                method,
                lhsLocal->type,
                localRefMode,
                scope,
                "cannot rebind interface reference to concrete class that does not implement the interface",
                expr,
                errors,
                rhsNeedsMut
            )){
                ok = false;
            }
        }
        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(lhsLocal->type, expr.rhs);
        if(expr.rhs){
            ScopedVarInfo* mutableLocal = const_cast<ScopedVarInfo*>(lhsLocal);
            if(expr.rhs->kind == ExprKind::Lambda){
                mutableLocal->lambdaExpr = expr.rhs;
            }
            else if(expr.rhs->kind == ExprKind::Identifier){
                const ScopedVarInfo* rhsLocal = m_checker.FindScopedVar(scope, expr.rhs->text);
                mutableLocal->lambdaExpr = rhsLocal ? rhsLocal->lambdaExpr : nullptr;
            }
            else{
                mutableLocal->lambdaExpr = nullptr;
            }
        }
    }
    else if(lhsMemberOrIndexTypeRef){
        LambdaCaptureAnalyzer(scope, m_checker.m_symbols).propagateFuncCaptureBytes(const_cast<TypeRef*>(lhsMemberOrIndexTypeRef), expr.rhs);
        const ParamPassingMode::Enum lhsRefMode = m_checker.m_symbols.resolveRefMode(lhsMemberOrIndexTypeRef);
        const MStringView baseLhsTypeName = m_checker.m_symbols.resolveBaseTypeName(lhsMemberOrIndexTypeRef->name);
        if(lhsRefMode != ParamPassingMode::None && (m_checker.m_symbols.findInterface(baseLhsTypeName) || m_checker.m_symbols.isAbstractBuiltinType(baseLhsTypeName))){
            if(!applyInterfaceRefRebind(
                ownerClass,
                method,
                lhsMemberOrIndexTypeRef,
                lhsRefMode,
                scope,
                "cannot rebind interface reference to concrete class that does not implement the interface",
                expr,
                errors,
                rhsNeedsMut
            )){
                ok = false;
            }
        }
    }
    if(expr.rhs){
        if(rhsNeedsMut)
            expr.rhs->isMutArg = true;
        const b8 savedClassArenaCtx = m_checker.m_inClassArenaContext;
        if(rhsInClassArenaCtx)
            m_checker.m_inClassArenaContext = true;
        if(!m_checker.checkExpr(ownerClass, scope, rhsNeedsMut, method, *expr.rhs, errors))
            ok = false;
        m_checker.m_inClassArenaContext = savedClassArenaCtx;
        if(expr.op == TokenType::Equal && expr.lhs && expr.lhs->kind == ExprKind::ThisMember && LambdaCaptureAnalyzer(scope, m_checker.m_symbols).isEscapingMutCaptureLambda(expr.rhs)){
            m_checker.reportError(errors, expr.span, "cannot store lambda with 'mut' capture into 'this.member'; mutable reference captures cannot escape their enclosing scope");
            ok = false;
        }
    }
    if(expr.lhs && expr.rhs){
        const MStringView lhsType = m_checker.inferExprTypeName(ownerClass, &method, *expr.lhs, scope);
        const MStringView rhsType = m_checker.inferExprTypeName(ownerClass, &method, *expr.rhs, scope);
        if(expr.op != TokenType::Equal && expr.op != TokenType::EqualEqual && expr.op != TokenType::BangEqual){
            if(SymbolTable::IsSimdStorageType(lhsType) || SymbolTable::IsUnalignedVectorType(lhsType) || SymbolTable::IsSimdStorageType(rhsType) || SymbolTable::IsUnalignedVectorType(rhsType)){
                m_checker.reportError(errors, expr.span, "SIMD arithmetic and bitwise operators require 'VectorSingle', 'VectorDouble', 'MatrixSingle', or 'MatrixDouble'; load memory-layout values first");
                ok = false;
            }
        }
        else if(expr.op == TokenType::Equal){
            if(const TypeRef* lhsTargetTypeRef = m_checker.inferExprTypeRef(ownerClass, &method, *expr.lhs, scope)){
                if(!lhsTargetTypeRef->templateArgs.empty() && lhsTargetTypeRef->templateArgs[0].type){
                    if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(lhsType); genericInfo && (genericInfo->inlineLayoutKind == InlineLayoutKind::Expected || genericInfo->inlineLayoutKind == InlineLayoutKind::Optional)){
                        const MStringView valBase = m_checker.m_symbols.resolveBaseTypeName(lhsTargetTypeRef->templateArgs[0].type->name);
                        if(SymbolTable::IsSimdRegisterVectorType(valBase) && expr.rhs->resolvedSimdType == valBase)
                            expr.rhs->wrapInSimdResult = true;
                    }
                }
                if(!expr.isNotNullRebind && !expr.rhs->wrapInSimdResult && expr.lhs->resolvedSimdType.empty() && m_checker.m_symbols.resolveRefMode(lhsTargetTypeRef) == ParamPassingMode::None){
                    const TypeLayoutAnalyzer layoutAnalyzer(m_checker.m_symbols);
                    const TypeRef* unwrappedLhs = m_checker.m_symbols.unwrapTypeAlias(lhsTargetTypeRef);
                    const BuiltinGenericTypeInfo* lhsGenericInfo = unwrappedLhs ? FindBuiltinGenericType(unwrappedLhs->name) : nullptr;
                    if(lhsGenericInfo && lhsGenericInfo->inlineLayoutKind == InlineLayoutKind::Optional && !unwrappedLhs->templateArgs.empty() && unwrappedLhs->templateArgs[0].type && layoutAnalyzer.requiresMotherArena(*unwrappedLhs->templateArgs[0].type)){
                        const Expr* rootLhs = expr.lhs;
                        while(rootLhs && (rootLhs->kind == ExprKind::MemberAccess || rootLhs->kind == ExprKind::Index))
                            rootLhs = rootLhs->lhs;
                        if(rootLhs && rootLhs->kind == ExprKind::ThisMember && ownerClass){
                            expr.useAdhdOptionalClassAssign = true;
                            ownerClass->needsClassArena = true;
                        }
                        else{
                            expr.useAdhdOptionalScratchAssign = true;
                            method.usesScratchArena = true;
                            m_checker.m_activeParallelBlockUsesScratch = true;
                        }
                    }
                    else if(layoutAnalyzer.requiresMotherArena(*lhsTargetTypeRef)){
                        const ClassDecl* lhsCls = unwrappedLhs ? m_checker.m_symbols.findClass(unwrappedLhs->name) : nullptr;
                        b8 hasCustomAssign = false;
                        if(lhsCls){
                            for(const MethodDecl* m : lhsCls->methods){
                                if(m && m->isOperator && OperatorSemantics::MapOperatorSymbolToTokenType(m->name) == TokenType::Equal){
                                    hasCustomAssign = true;
                                    break;
                                }
                            }
                        }
                        if(!hasCustomAssign)
                            expr.useAdhdAssign = true;
                    }
                }
            }
        }
        const EnumDecl* lhsEnum = m_checker.m_symbols.findEnum(lhsType);
        const EnumDecl* rhsEnum = m_checker.m_symbols.findEnum(rhsType);
        const BuiltinEnumInfo* lhsBuiltinEnum = (lhsEnum || m_checker.m_symbols.hasTypeSymbol(lhsType)) ? nullptr : FindBuiltinEnum(lhsType);
        const BuiltinEnumInfo* rhsBuiltinEnum = (rhsEnum || m_checker.m_symbols.hasTypeSymbol(rhsType)) ? nullptr : FindBuiltinEnum(rhsType);
        if(OperatorSemantics::IsBitwiseFlagOperatorToken(expr.op)){
            const b8 lhsNonFlag = (lhsEnum && !lhsEnum->isFlag) || (lhsBuiltinEnum && !lhsBuiltinEnum->isFlag);
            const b8 rhsNonFlag = !m_checker.m_symbols.findClass(lhsType) && ((rhsEnum && !rhsEnum->isFlag) || (rhsBuiltinEnum && !rhsBuiltinEnum->isFlag));
            if(lhsNonFlag || rhsNonFlag){
                m_checker.reportError(errors, expr.span, "bitwise operators are only supported on 'flag' types, not 'enum' types");
                ok = false;
            }
            else if((lhsEnum && rhsEnum && lhsEnum != rhsEnum) || (lhsBuiltinEnum && rhsBuiltinEnum && lhsBuiltinEnum != rhsBuiltinEnum) || (lhsEnum && rhsBuiltinEnum) || (lhsBuiltinEnum && rhsEnum)){
                m_checker.reportError(errors, expr.span, "cannot combine values of two different 'flag' types in a bitwise operation");
                ok = false;
            }
        }
        else if((lhsEnum || lhsBuiltinEnum) && OperatorSemantics::IsArithmeticOrShiftAssignOperatorToken(expr.op)){
            m_checker.reportError(errors, expr.span, "arithmetic and shift compound assignment operators are not supported on 'enum' or 'flag' types");
            ok = false;
        }
        if(!m_checker.m_symbols.hasTypeSymbol(lhsType) && RequiresTCharAdaptation(lhsType))
            m_checker.markStringToPathAdaptation(ownerClass, &method, scope, *expr.rhs);
        if(expr.op == TokenType::Equal && lhsType == "StringView"){
            if(BorrowLifetimeAnalyzer(m_checker).borrowsTemporaryStringOwner(ownerClass, &method, *expr.rhs, scope)){
                m_checker.reportError(errors, expr.span, "cannot assign 'StringView' from a temporary owning string");
                ok = false;
            }
            else{
                MStringView borrowedOwnerName;
                usize borrowedOwnerIdx = 0u;
                if(BorrowLifetimeAnalyzer(m_checker).borrowsLocalStringOwner(ownerClass, &method, *expr.rhs, scope, &borrowedOwnerName, &borrowedOwnerIdx)){
                    const Expr* rootLhs = expr.lhs;
                    while(rootLhs && (rootLhs->kind == ExprKind::MemberAccess || rootLhs->kind == ExprKind::Index))
                        rootLhs = rootLhs->lhs;
                    const ScopedVarInfo* rootLocal = nullptr;
                    usize rootLocalIdx = 0u;
                    if(rootLhs && rootLhs->kind == ExprKind::Identifier){
                        for(usize i = scope.size(); i > 0u; --i){
                            if(scope[i - 1u].name == rootLhs->text){
                                rootLocal = &scope[i - 1u];
                                rootLocalIdx = i - 1u;
                                break;
                            }
                        }
                    }
                    if(!rootLocal || rootLocal->isParam || rootLocal->mode != ParamPassingMode::None || m_checker.m_symbols.resolveRefMode(rootLocal->type) != ParamPassingMode::None){
                        m_checker.reportError(errors, expr.span, "cannot store 'StringView' borrowing a stack-local string into an escaping target");
                        ok = false;
                    }
                    else if(rootLocalIdx < borrowedOwnerIdx){
                        m_checker.reportError(errors, expr.span, "cannot assign 'StringView' from a shorter-lived block-local string");
                        ok = false;
                    }
                    else{
                        ScopedVarInfo* mutableRoot = const_cast<ScopedVarInfo*>(rootLocal);
                        mutableRoot->borrowsLocalStringOwner = true;
                        mutableRoot->borrowedLocalOwnerName = borrowedOwnerName;
                    }
                }
            }
        }
    }

    return ok;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

