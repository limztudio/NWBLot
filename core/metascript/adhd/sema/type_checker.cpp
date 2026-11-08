// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/keyword_semantics.h>

#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "type_checker.h"
#include "constructor_argument_checker.h"

#include "borrow_lifetime_analyzer.h"
#include "callable_declaration_linker.h"
#include "type_layout_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_type_checker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 TypeRefReferencesAlias(const TypeRef* type, const MStringView targetAliasName, const SymbolTable& symbols, const u32 depth = 0u)noexcept{
    if(!type || depth > 16u)
        return false;
    if(type->isFuncType){
        if(TypeRefReferencesAlias(type->funcReturnType, targetAliasName, symbols, depth + 1u))
            return true;
        for(const FuncParamType& fp : type->funcParams){
            if(TypeRefReferencesAlias(fp.type, targetAliasName, symbols, depth + 1u))
                return true;
        }
        return false;
    }
    if(type->name == targetAliasName)
        return true;
    for(const TypeArg& targ : type->templateArgs){
        if(TypeRefReferencesAlias(targ.type, targetAliasName, symbols, depth + 1u))
            return true;
    }
    if(const TypeAliasDecl* nextAlias = symbols.findTypeAlias(type->name)){
        if(TypeRefReferencesAlias(nextAlias->aliasedType, targetAliasName, symbols, depth + 1u))
            return true;
    }
    return false;
}

[[nodiscard]] b8 ExprUsesOutOfLineConstruct(const Expr* expr)noexcept{
    if(!expr)
        return false;
    if(!expr->resolvedSimdType.empty() || expr->kind == ExprKind::Lambda)
        return true;
    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        DiagnosticKind::Enum diagKind = DiagnosticKind::Assert;
        if(KeywordSemantics::TryParseDiagnosticKind(expr->lhs->text, diagKind))
            return true;
    }
    if(ExprUsesOutOfLineConstruct(expr->lhs) || ExprUsesOutOfLineConstruct(expr->rhs))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprUsesOutOfLineConstruct(arg))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtListIsAutoInlineable(const Span<const Stmt* const> stmts, u32& inOutCount)noexcept{
    for(const Stmt* s : stmts){
        if(!s)
            continue;
        ++inOutCount;
        if(inOutCount > 16u)
            return false;
        switch(s->kind){
        case StmtKind::Return:
            if(ExprUsesOutOfLineConstruct(s->expr))
                return false;
            break;
        case StmtKind::ExprStmt:
            if(!IsThisMemberSimpleAssignStmt(*s))
                return false;
            if(ExprUsesOutOfLineConstruct(s->expr->lhs) || ExprUsesOutOfLineConstruct(s->expr->rhs))
                return false;
            break;
        case StmtKind::If:
            if(ExprUsesOutOfLineConstruct(s->condition))
                return false;
            if(!StmtListIsAutoInlineable(s->statements, inOutCount) || !StmtListIsAutoInlineable(s->elseStatements, inOutCount))
                return false;
            break;
        case StmtKind::Switch:
            if(ExprUsesOutOfLineConstruct(s->expr))
                return false;
            for(const SwitchCase& sc : s->switchCases){
                if(ExprUsesOutOfLineConstruct(sc.matchExpr))
                    return false;
                if(!StmtListIsAutoInlineable(sc.statements, inOutCount))
                    return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

[[nodiscard]] b8 ShouldAutoInlineCallable(const MethodDecl& fn)noexcept{
    if(!fn.hasBody)
        return false;
    if(fn.usesScratchArena || fn.usesTaskScope || fn.isOutOfLine)
        return false;
    if(!fn.templateParams.empty())
        return true;
    u32 stmtCount = 0u;
    return StmtListIsAutoInlineable(fn.body, stmtCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const ScopedVarInfo* TypeChecker::FindScopedVar(const Span<const ScopedVarInfo> scope, const MStringView name)noexcept{
    for(usize i = scope.size(); i > 0u; --i){
        if(scope[i - 1u].name == name)
            return &scope[i - 1u];
    }
    return nullptr;
}

b8 TypeChecker::checkTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    b8 ok = true;

    if(!m_symbols.build(unit, errors))
        ok = false;

    if(!CallableDeclarationLinker(m_arena, m_symbols).link(unit, errors))
        ok = false;

    for(const EnumDecl* enumDecl : unit.enums){
        if(!enumDecl)
            continue;
        u32 autoFlagBitIndex = 0u;
        MethodDecl dummyEnumCtx(m_arena);
        MVector<ScopedVarInfo> enumScope(m_arena);
        TypeRef* enumUnderlyingRef = AllocateAstNode<TypeRef>(m_arena);
        enumUnderlyingRef->name = enumDecl->name;

        for(usize i = 0u; i < enumDecl->entries.size(); ++i){
            const EnumEntry& entry = enumDecl->entries[i];
            if(entry.name == "Count"){
                reportError(errors, entry.span, enumDecl->isFlag
                    ? "use 'kCount' instead of 'Count' for flag count sentries"
                    : "use 'kCount' instead of 'Count' for enum count sentries");
                ok = false;
            }
            for(usize j = 0u; j < i; ++j){
                if(enumDecl->entries[j].name == entry.name){
                    reportError(errors, entry.span, enumDecl->isFlag ? "duplicate enumerator name in flag" : "duplicate enumerator name in enum");
                    ok = false;
                    break;
                }
            }
            if(!enumDecl->isImported){
                if(enumDecl->isFlag && !entry.valueExpr){
                    if(autoFlagBitIndex >= static_cast<u32>(enumDecl->underlyingSize) * 8u){
                        reportError(errors, entry.span, "auto-assigned 'flag' bit position exceeds underlying flag bit width");
                        ok = false;
                    }
                    ++autoFlagBitIndex;
                }
                if(entry.valueExpr){
                    if(!checkExpr(nullptr, enumScope, false, dummyEnumCtx, *entry.valueExpr, errors)){
                        ok = false;
                    }
                    else{
                        const MStringView valType = inferExprTypeName(nullptr, &dummyEnumCtx, *entry.valueExpr, enumScope);
                        i64 evalConstVal = 0;
                        if(
                            entry.valueExpr->kind == ExprKind::FloatLiteral
                            || entry.valueExpr->kind == ExprKind::DoubleLiteral
                            || entry.valueExpr->kind == ExprKind::StringLiteral
                            || BuiltinTypeSemantics::IsFloatScalarType(valType)
                            || SymbolTable::IsStringSwitchType(valType)
                            || !TryEvaluateEnumEntryConstant(*enumDecl, entry.name, m_symbols, 0u, evalConstVal)
                        ){
                            reportError(errors, entry.span, enumDecl->isFlag
                                ? "flag entry value must be an integral constant expression"
                                : "enum entry value must be an integral constant expression");
                            ok = false;
                        }
                    }
                }
            }
            ScopedVarInfo entryVar;
            entryVar.name = entry.name;
            entryVar.inferredTypeName = enumDecl->name;
            entryVar.type = enumUnderlyingRef;
            entryVar.isConst = true;
            enumScope.push_back(entryVar);
        }
    }

    for(TypeAliasDecl* aliasDecl : unit.typeAliases){
        if(!checkTemplateParamBounds(aliasDecl->templateParams, nullptr, nullptr, errors))
            ok = false;
        if(aliasDecl->aliasedType){
            if(__hidden_adhd_type_checker::TypeRefReferencesAlias(aliasDecl->aliasedType, aliasDecl->name, m_symbols)){
                reportError(errors, aliasDecl->span, "cyclic 'type' alias declaration is prohibited");
                ok = false;
            }
            MethodDecl aliasScope(m_arena);
            aliasScope.templateParams = aliasDecl->templateParams;
            if(!checkTypeRef(*aliasDecl->aliasedType, nullptr, &aliasScope, true, errors))
                ok = false;
        }
    }

    for(Stmt* gc : unit.globalConstants){
        if(!gc)
            continue;
        MethodDecl dummyGlobalCtx(m_arena);
        MVector<ScopedVarInfo> emptyScope(m_arena);
        if(gc->isImported){
            if(gc->expr && gc->varType && gc->varType->isAuto){
                if(const TypeRef* inferredRef = inferExprTypeRef(nullptr, &dummyGlobalCtx, *gc->expr, emptyScope); inferredRef && !inferredRef->isAuto){
                    const SourceSpan savedSpan = gc->varType->span;
                    *gc->varType = *inferredRef;
                    gc->varType->span = savedSpan;
                    gc->varType->isAuto = false;
                }
                else{
                    const MStringView inferredName = inferExprTypeName(nullptr, &dummyGlobalCtx, *gc->expr, emptyScope);
                    if(!inferredName.empty()){
                        gc->varType->isAuto = false;
                        gc->varType->name = inferredName;
                    }
                }
            }
            continue;
        }
        if(gc->varType){
            if(!checkTypeRef(*gc->varType, nullptr, nullptr, errors))
                ok = false;
            const MStringView baseGcType = m_symbols.resolveBaseTypeName(gc->varType->name);
            if(m_symbols.findInterface(baseGcType)){
                reportError(errors, gc->span, "file-level 'const' cannot have an 'interface' type");
                ok = false;
            }
            if(m_symbols.resolveRefMode(gc->varType) != ParamPassingMode::None){
                reportError(errors, gc->span, "file-level 'const' cannot declare an 'in' or 'mut' reference");
                ok = false;
            }
            if(SymbolTable::IsArenaBackedContainer(baseGcType) || IsAtomicGenericType(baseGcType)){
                reportError(errors, gc->span, "file-level 'const' cannot have an arena-backed or 'Atomic' type; use compile-time fixed types");
                ok = false;
            }
            if(RequiresTCharAdaptation(baseGcType)){
                if(gc->expr)
                    markStringToPathAdaptation(nullptr, &dummyGlobalCtx, emptyScope, *gc->expr);
                for(Expr* arg : gc->arguments){
                    if(arg)
                        markStringToPathAdaptation(nullptr, &dummyGlobalCtx, emptyScope, *arg);
                }
            }
        }
        if(gc->expr){
            if(!checkExpr(nullptr, emptyScope, false, dummyGlobalCtx, *gc->expr, errors))
                ok = false;
            if(gc->varType && gc->varType->isAuto){
                if(const TypeRef* inferredRef = inferExprTypeRef(nullptr, &dummyGlobalCtx, *gc->expr, emptyScope); inferredRef && !inferredRef->isAuto){
                    const SourceSpan savedSpan = gc->varType->span;
                    *gc->varType = *inferredRef;
                    gc->varType->span = savedSpan;
                    gc->varType->isAuto = false;
                }
                else{
                    const MStringView inferredName = !gc->expr->resolvedSimdType.empty()
                        ? gc->expr->resolvedSimdType
                        : inferExprTypeName(nullptr, &dummyGlobalCtx, *gc->expr, emptyScope);
                    if(!inferredName.empty()){
                        gc->varType->isAuto = false;
                        gc->varType->name = inferredName;
                    }
                }
            }
        }
        if(gc->varType && !gc->varType->isAuto && TypeLayoutAnalyzer(m_symbols).containsSimdRegisterType(*gc->varType)){
            reportError(errors, gc->span, "file-level 'const' cannot hold SIMD calculation type ('VectorSingle', 'VectorDouble', 'MatrixSingle', 'MatrixDouble'); use memory-layout types instead");
            ok = false;
        }
        if(!ConstructorArgumentChecker(*this).check(
            nullptr,
            gc->isCtorInit ? gc->varType : nullptr,
            emptyScope,
            gc->span,
            dummyGlobalCtx,
            gc->arguments,
            errors
        )){
            ok = false;
        }
    }

    for(InterfaceDecl* iface : unit.interfaces){
        if(!checkInterfaceDecl(*iface, errors))
            ok = false;
    }

    for(ClassDecl* classDecl : unit.classes){
        if(!checkClassDecl(*classDecl, errors))
            ok = false;
        for(MethodDecl* m : classDecl->methods){
            if(m)
                m->isInline = __hidden_adhd_type_checker::ShouldAutoInlineCallable(*m);
        }
    }

    for(MethodDecl* fn : unit.freeFunctions){
        if(!checkCallableSignature(nullptr, *fn, errors))
            ok = false;
        if(!fn->hasBody){
            if(!fn->isImported){
                reportError(errors, fn->span, "declared function has no implementation body");
                ok = false;
            }
            continue;
        }
        if(fn->isImported){
            fn->isInline = __hidden_adhd_type_checker::ShouldAutoInlineCallable(*fn);
            continue;
        }
        if(!m_parallelValidator.validateMethod(*fn, errors))
            ok = false;
        if(!checkMethodBody(nullptr, *fn, errors))
            ok = false;
        fn->isInline = __hidden_adhd_type_checker::ShouldAutoInlineCallable(*fn);
    }

    if(!m_layoutOptimizer.optimizeTranslationUnit(unit, errors))
        ok = false;

    return ok;
}

void TypeChecker::buildCallArgHints(const ClassDecl* ownerClass, const MethodDecl* method, const Span<const Expr* const> args, const Span<const ScopedVarInfo> scope, MVector<MStringView>& outArgTypes, MVector<u8>& outArgIsMut)const{
    outArgTypes.clear();
    outArgIsMut.clear();
    outArgTypes.reserve(args.size());
    outArgIsMut.reserve(args.size());
    for(const Expr* arg : args){
        b8 isMut = arg && arg->isMutArg;
        if(!isMut && arg && method){
            if(arg->kind == ExprKind::Identifier){
                if(FindScopedVar(scope, arg->text) != nullptr && !isReceiverConst(ownerClass, *method, *arg, scope))
                    isMut = true;
            }
            else if(arg->kind == ExprKind::ThisMember || arg->kind == ExprKind::MemberAccess || arg->kind == ExprKind::Index){
                if(!isReceiverConst(ownerClass, *method, *arg, scope))
                    isMut = true;
            }
        }
        outArgIsMut.push_back(isMut ? 1u : 0u);
        outArgTypes.push_back(arg ? inferExprTypeName(ownerClass, method, *arg, scope) : MStringView{});
    }
}

MStringView ResolveScopedVarBaseType(const SymbolTable& symbols, const ScopedVarInfo& var)noexcept{
    if(!var.inferredTypeName.empty())
        return var.inferredTypeName;
    if(var.type)
        return symbols.resolveBaseTypeName(var.type->name);
    return {};
}


MStringView TypeChecker::resolveScopedVarBaseType(const ScopedVarInfo& var)const noexcept{
    return ResolveScopedVarBaseType(m_symbols, var);
}

b8 TypeChecker::isThisConstInScope(const MethodDecl& method, const Span<const ScopedVarInfo> scope)const noexcept{
    const ScopedVarInfo* lambdaThis = m_inLambda ? FindScopedVar(scope, "this") : nullptr;
    return method.isConst || (lambdaThis && lambdaThis->isConst);
}

b8 TypeChecker::validateThisAccess(const ClassDecl* ownerClass, const SourceSpan span, const Span<const ScopedVarInfo> scope, MVector<ParseError>& errors)const{
    if(!ownerClass){
        reportError(errors, span, "'this' cannot be used in a free function");
        return false;
    }
    if(m_inLambda && !FindScopedVar(scope, "this")){
        reportError(errors, span, "accessing 'this' inside a lambda requires capturing 'in this' or 'mut this'");
        return false;
    }
    return true;
}

b8 TypeChecker::isConstructibleTypeName(const MStringView calleeName, const MStringView baseCallee)const noexcept{
    return m_symbols.findClass(baseCallee) != nullptr
        || m_symbols.findTypeAlias(calleeName) != nullptr
        || SymbolTable::IsSimdType(baseCallee)
        || SymbolTable::IsArenaBackedContainer(baseCallee)
        || m_symbols.isSelfDefaultedType(baseCallee);
}

b8 TypeChecker::isRvalueTemporaryExpr(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    if(IsAlwaysRvalueExprKind(expr.kind))
        return true;
    const TypeRef* retType = inferExprTypeRef(ownerClass, method, expr, scope);
    return m_symbols.resolveRefMode(retType) == ParamPassingMode::None && BorrowLifetimeAnalyzer(*this).isTemporaryOwnerExpr(ownerClass, method, expr, scope);
}

const MethodDecl* TypeChecker::resolveCustomOperator(
    const ClassDecl* ownerClass,
    const MethodDecl* method,
    const TypeRef* lhsTypeRef,
    const MStringView lhsBaseType,
    const MStringView opSym,
    const usize arity,
    const b8 recvIsConst,
    const ClassDecl** outDeclaringClass
)const noexcept{
    if(outDeclaringClass)
        *outDeclaringClass = nullptr;
    if(opSym.empty())
        return nullptr;
    if(const ClassDecl* cls = m_symbols.findClass(lhsBaseType)){
        if(outDeclaringClass)
            *outDeclaringClass = cls;
        return m_symbols.findClassOperator(*cls, opSym, &arity, recvIsConst, ownerClass != cls);
    }
    if(lhsTypeRef && m_symbols.findInterface(lhsBaseType))
        return m_symbols.findInterfaceMethod(*lhsTypeRef, opSym, &arity);
    if(const TemplateParamDecl* tp = FindTemplateParam(ownerClass, method, lhsBaseType)){
        for(const TypeRef* boundRef : tp->interfaceBounds){
            if(!boundRef)
                continue;
            if(const MethodDecl* boundOp = m_symbols.findInterfaceMethod(*boundRef, opSym, &arity))
                return boundOp;
        }
    }
    return nullptr;
}

MStringView TypeChecker::resolveScopeAccessEnumTypeName(const ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, const Span<const ScopedVarInfo> scope, const ClassDecl** outAccessedClass, const FieldDecl** outAccessedField)const noexcept{
    if(outAccessedClass)
        *outAccessedClass = nullptr;
    if(outAccessedField)
        *outAccessedField = nullptr;
    if(expr.scopeChain.empty())
        return {};

    const MStringView rootName = expr.scopeChain[0];
    if(expr.text == "this"){
        if(ownerClass){
            if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, rootName); field && field->type){
                const MStringView fieldTypeName = m_symbols.resolveBaseTypeName(field->type->name);
                if(m_symbols.findEnum(fieldTypeName))
                    return fieldTypeName;
            }
        }
        return {};
    }
    if(expr.lhs){
        const TypeRef* lhsTypeRef = nullptr;
        const MStringView lhsBase = inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
        if(const ClassDecl* targetCls = m_symbols.findClass(lhsBase)){
            if(const FieldDecl* field = SymbolTable::FindClassField(*targetCls, rootName); field && field->type){
                if(outAccessedClass)
                    *outAccessedClass = targetCls;
                if(outAccessedField)
                    *outAccessedField = field;
                const TypeRef* ft = m_symbols.substituteTypeParams(field->type, targetCls->templateParams, lhsTypeRef);
                const MStringView fieldTypeName = ft ? m_symbols.resolveBaseTypeName(ft->name) : MStringView{};
                if(!fieldTypeName.empty() && m_symbols.findEnum(fieldTypeName))
                    return fieldTypeName;
            }
        }
        return {};
    }
    if(const ScopedVarInfo* local = FindScopedVar(scope, rootName)){
        const MStringView localTypeName = resolveScopedVarBaseType(*local);
        if(!localTypeName.empty() && m_symbols.findEnum(localTypeName))
            return localTypeName;
        return {};
    }
    if(const Stmt* gc = m_symbols.findGlobalConstant(rootName); gc && gc->varType){
        const MStringView gcTypeName = m_symbols.resolveBaseTypeName(gc->varType->name);
        if(!gcTypeName.empty() && m_symbols.findEnum(gcTypeName))
            return gcTypeName;
        return {};
    }
    const MStringView resolvedRoot = m_symbols.resolveBaseTypeName(rootName);
    if(!resolvedRoot.empty() && m_symbols.findEnum(resolvedRoot))
        return resolvedRoot;
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

