// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/contract_semantics.h>
#include <core/metascript/adhd/ast/keyword_semantics.h>

#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "type_checker.h"
#include "constructor_argument_checker.h"

#include "asset_type_analyzer.h"
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
    if(fn.hasSynthesizedArenaParams)
        return true;
    if(fn.usesScratchArena || fn.usesTaskScope || fn.isOutOfLine)
        return false;
    if(!fn.templateParams.empty())
        return true;
    u32 stmtCount = 0u;
    return StmtListIsAutoInlineable(fn.body, stmtCount);
}

[[nodiscard]] b8 ComputeCallableInjectsCallerArena(const MethodDecl& fn, const SymbolTable& symbols)noexcept{
    if(fn.isConstructor || fn.isDestructor || fn.returnsFromOwnerArena || !fn.returnType)
        return false;
    if(symbols.resolveRefMode(fn.returnType) != ParamPassingMode::None)
        return false;
    return TypeLayoutAnalyzer(symbols).requiresMotherArena(*fn.returnType);
}

[[nodiscard]] b8 ComputeCallableHasSynthesizedArenaParams(const MethodDecl& fn, const SymbolTable& symbols)noexcept{
    if(fn.isDestructor || fn.isOverride || fn.isReflect || fn.isOperator)
        return false;
    for(const ParamDecl& p : fn.params){
        if(!p.type || p.type->isAuto || p.type->isFuncType)
            continue;
        if(symbols.resolveRefMode(p.type) != ParamPassingMode::None)
            continue;
        const TypeRef* unwrapped = symbols.unwrapTypeAlias(p.type);
        if(unwrapped && unwrapped->refMode == ParamPassingMode::None && SymbolTable::IsArenaBackedContainer(unwrapped->name))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const ScopedVarInfo* TypeChecker::FindScopedVar(const Span<const ScopedVarInfo> scope, const MStringView name, usize* const outIndex)noexcept{
    for(usize i = scope.size(); i > 0u; --i){
        if(scope[i - 1u].name == name){
            if(outIndex)
                *outIndex = i - 1u;
            return &scope[i - 1u];
        }
    }
    return nullptr;
}

const Expr* TypeChecker::UnwrapMemberOrIndexRoot(const Expr* expr)noexcept{
    while(expr && (expr->kind == ExprKind::MemberAccess || expr->kind == ExprKind::Index))
        expr = expr->lhs;
    return expr;
}

b8 TypeChecker::DoesSynthesizedClassCtorPassArena(const ClassDecl& ctorCls, const MStringView baseClassName, const usize argCount, const MStringView firstArgBaseType)noexcept{
    if(argCount >= 2u)
        return true;
    if(argCount != 1u || firstArgBaseType == baseClassName)
        return false;
    if(!ctorCls.assetTypeName.empty() && (IsLiteralConstObjectType(firstArgBaseType) || IsStringLikeType(firstArgBaseType)))
        return false;
    return true;
}

b8 TypeChecker::checkTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors){
    b8 ok = true;
    m_activeUnit = &unit;
    m_recordGlobalHeaders = false;
    m_recordedRequiredHeaders.clear();
    m_recordedTypeHeaders.clear();

    if(!m_symbols.build(unit, errors))
        ok = false;

    if(!CallableDeclarationLinker(m_arena, m_symbols).link(unit, errors))
        ok = false;

    for(const EnumDecl* enumDecl : unit.enums){
        if(!enumDecl)
            continue;
        if(enumDecl->isImported || enumDecl->isExtern){
            if(enumDecl->entries.size() > 16u){
                HashSet<MStringView, MetaArena, MStringViewHash> seenEntries(m_arena);
                seenEntries.reserve(enumDecl->entries.size());
                for(const EnumEntry& entry : enumDecl->entries){
                    if(!seenEntries.insert(entry.name).second){
                        reportError(errors, entry.span, enumDecl->isFlag ? "duplicate enumerator name in flag" : "duplicate enumerator name in enum");
                        ok = false;
                    }
                }
            }
            else{
                for(usize i = 0u; i < enumDecl->entries.size(); ++i){
                    const EnumEntry& entry = enumDecl->entries[i];
                    for(usize j = 0u; j < i; ++j){
                        if(enumDecl->entries[j].name == entry.name){
                            reportError(errors, entry.span, enumDecl->isFlag ? "duplicate enumerator name in flag" : "duplicate enumerator name in enum");
                            ok = false;
                            break;
                        }
                    }
                }
            }
            continue;
        }

        u32 autoFlagBitIndex = 0u;
        MethodDecl dummyEnumCtx(m_arena);
        MVector<ScopedVarInfo> enumScope(m_arena);
        TypeRef* enumUnderlyingRef = AllocateAstNode<TypeRef>(m_arena);
        enumUnderlyingRef->name = enumDecl->name;
        const b8 useHashDup = enumDecl->entries.size() > 16u;
        HashSet<MStringView, MetaArena, MStringViewHash> seenEntries(m_arena);
        if(useHashDup)
            seenEntries.reserve(enumDecl->entries.size());

        for(usize i = 0u; i < enumDecl->entries.size(); ++i){
            const EnumEntry& entry = enumDecl->entries[i];
            if(const MStringView sentryErr = KeywordSemantics::FindForbiddenEnumSentryError(entry.name, enumDecl->isFlag); !sentryErr.empty()){
                reportError(errors, entry.span, sentryErr);
                ok = false;
            }
            if(useHashDup){
                if(!seenEntries.insert(entry.name).second){
                    reportError(errors, entry.span, enumDecl->isFlag ? "duplicate enumerator name in flag" : "duplicate enumerator name in enum");
                    ok = false;
                }
            }
            else{
                for(usize j = 0u; j < i; ++j){
                    if(enumDecl->entries[j].name == entry.name){
                        reportError(errors, entry.span, enumDecl->isFlag ? "duplicate enumerator name in flag" : "duplicate enumerator name in enum");
                        ok = false;
                        break;
                    }
                }
            }
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
            ScopedVarInfo entryVar;
            entryVar.name = entry.name;
            entryVar.inferredTypeName = enumDecl->name;
            entryVar.type = enumUnderlyingRef;
            entryVar.isConst = true;
            enumScope.push_back(entryVar);
        }
    }

    for(TypeAliasDecl* aliasDecl : unit.typeAliases){
        m_recordGlobalHeaders = !aliasDecl->isImported && !aliasDecl->isExtern;
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

    {
        const TypeLayoutAnalyzer preLayoutAnalyzer(m_symbols);
        for(ClassDecl* cls : unit.classes){
            if(!cls || cls->isExtern || !cls->canBorrowMotherArena())
                continue;
            for(const MethodDecl* method : cls->methods){
                if(method && method->returnsFromOwnerArena){
                    cls->needsClassArena = true;
                    break;
                }
            }
        }
        const usize maxArenaPasses = unit.classes.size() + 1u;
        for(usize pass = 0u; pass < maxArenaPasses; ++pass){
            b8 changed = false;
            for(ClassDecl* cls : unit.classes){
                if(!cls || cls->isExtern || cls->isSerialize || !cls->subsystemTypeName.empty() || cls->needsClassArena)
                    continue;
                if(cls->concreteBase && cls->concreteBase->canBorrowMotherArena() && cls->concreteBase->needsClassArena){
                    cls->needsClassArena = true;
                    changed = true;
                    continue;
                }
                for(const FieldDecl* field : cls->fields){
                    if(field && !field->isStatic && field->type && preLayoutAnalyzer.requiresMotherArena(*field->type)){
                        cls->needsClassArena = true;
                        changed = true;
                        break;
                    }
                }
            }
            if(!changed)
                break;
        }
        for(InterfaceDecl* iface : unit.interfaces){
            if(!iface || iface->isExtern)
                continue;
            for(MethodDecl* m : iface->methods){
                if(m && !m->isExtern)
                    m->injectsCallerArena = __hidden_adhd_type_checker::ComputeCallableInjectsCallerArena(*m, m_symbols);
            }
        }
        for(ClassDecl* cls : unit.classes){
            if(!cls || cls->isExtern)
                continue;
            for(MethodDecl* m : cls->methods){
                if(!m || m->isExtern)
                    continue;
                m->injectsCallerArena = __hidden_adhd_type_checker::ComputeCallableInjectsCallerArena(*m, m_symbols);
                m->hasSynthesizedArenaParams = __hidden_adhd_type_checker::ComputeCallableHasSynthesizedArenaParams(*m, m_symbols);
                if(m->isConstructor && !ConstructorArgumentChecker(*this).markRetainedParameters(*cls, *m, m->body, errors))
                    ok = false;
            }
        }
        for(MethodDecl* fn : unit.freeFunctions){
            if(!fn || fn->isExtern)
                continue;
            fn->injectsCallerArena = __hidden_adhd_type_checker::ComputeCallableInjectsCallerArena(*fn, m_symbols);
            fn->hasSynthesizedArenaParams = __hidden_adhd_type_checker::ComputeCallableHasSynthesizedArenaParams(*fn, m_symbols);
        }
    }

    for(Stmt* gc : unit.globalConstants){
        if(!gc)
            continue;
        MethodDecl dummyGlobalCtx(m_arena);
        MVector<ScopedVarInfo> emptyScope(m_arena);
        if(gc->isExtern){
            m_recordGlobalHeaders = false;
            if(gc->varType && !checkTypeRef(*gc->varType, nullptr, nullptr, errors))
                ok = false;
            continue;
        }
        if(gc->isImported){
            if(gc->expr && gc->varType && gc->varType->isAuto)
                deduceGlobalConstantAutoType(*gc, dummyGlobalCtx, emptyScope);
            continue;
        }
        m_recordGlobalHeaders = true;
        if(gc->varType){
            if(!checkTypeRef(*gc->varType, nullptr, nullptr, errors))
                ok = false;
            const MStringView baseGcType = m_symbols.resolveBaseTypeName(gc->varType->name);
            if(m_symbols.isInterfaceOrAbstractType(baseGcType)){
                reportError(errors, gc->span, "file-level 'const' cannot have an 'interface' type");
                ok = false;
            }
            if(m_symbols.resolveRefMode(gc->varType) != ParamPassingMode::None){
                reportError(errors, gc->span, "file-level 'const' cannot declare an 'in' or 'mut' reference");
                ok = false;
            }
            if((!m_symbols.hasTypeSymbol(baseGcType) && (SymbolTable::IsArenaBackedContainer(baseGcType) || IsAtomicGenericType(baseGcType))) || TypeLayoutAnalyzer(m_symbols).requiresMotherArena(*gc->varType)){
                reportError(errors, gc->span, "file-level 'const' cannot have an arena-backed or 'Atomic' type; use compile-time fixed types");
                ok = false;
            }
            maybeAdaptInitToTChar(nullptr, dummyGlobalCtx, emptyScope, baseGcType, gc->expr, gc->arguments);
        }
        if(gc->expr){
            if(!checkExpr(nullptr, emptyScope, false, dummyGlobalCtx, *gc->expr, errors))
                ok = false;
            if(gc->varType && gc->varType->isAuto)
                deduceGlobalConstantAutoType(*gc, dummyGlobalCtx, emptyScope);
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
        m_recordGlobalHeaders = !iface->isImported && !iface->isExtern;
        if(!checkInterfaceDecl(*iface, errors))
            ok = false;
    }

    for(ClassDecl* classDecl : unit.classes){
        m_recordGlobalHeaders = !classDecl->isImported && !classDecl->isExtern;
        if(!checkClassDecl(*classDecl, errors))
            ok = false;
        if(!classDecl->isImported && !classDecl->isExtern){
            for(MethodDecl* m : classDecl->methods){
                if(m && !m->isExtern)
                    m->isInline = (!classDecl->assetTypeName.empty() && m->hasBody && !m->isOutOfLine) || __hidden_adhd_type_checker::ShouldAutoInlineCallable(*m);
            }
        }
    }

    for(MethodDecl* fn : unit.freeFunctions){
        m_recordGlobalHeaders = !fn->isImported && !fn->isExtern;
        if(!fn->isExtern && !fn->isOperator && !fn->name.empty() && !IsAsciiUpper(fn->name[0])){
            reportError(errors, fn->span, "global function name must start with an uppercase letter");
            ok = false;
        }
        if(!checkCallableSignature(nullptr, *fn, errors))
            ok = false;
        if(!fn->hasBody){
            if(!fn->isImported && !fn->isExtern){
                reportError(errors, fn->span, "declared function has no implementation body");
                ok = false;
            }
            continue;
        }
        if(fn->isImported || fn->isExtern){
            fn->isInline = !fn->isExtern && __hidden_adhd_type_checker::ShouldAutoInlineCallable(*fn);
            continue;
        }
        if(!m_parallelValidator.validateMethod(*fn, errors))
            ok = false;
        if(!checkMethodBody(nullptr, *fn, errors))
            ok = false;
        fn->isInline = __hidden_adhd_type_checker::ShouldAutoInlineCallable(*fn);
    }

    m_recordGlobalHeaders = true;
    for(const AssetRegistration& registration : unit.assetRegistrations){
        const ClassDecl* cls = m_symbols.findClass(registration.className);
        const InterfaceDecl* iface = m_symbols.findInterface(registration.className);
        const b8 concrete = cls && !cls->isExtern && cls->templateParams.empty() && (!cls->subsystemTypeName.empty() || !cls->assetTypeName.empty());
        if(!concrete && !(iface && !iface->isExtern && !iface->assetTypeName.empty())){
            reportError(errors, registration.span, "register_asset requires an asset or a subsystem");
            ok = false;
        }
        if(!checkTypeRef(*registration.registrar, nullptr, nullptr, errors)){
            ok = false;
            continue;
        }
        const TypeRef* registrar = m_symbols.unwrapTypeAlias(registration.registrar);
        const b8 familyCodec = registrar && registrar->name == ContractSemantics::s_CodecRegistrationType && registrar->templateArgs.size() == 1u;
        const b8 typedRegistrar = registrar && ((registrar->name == ContractSemantics::s_AssetRegistrationType && registrar->templateArgs.size() == 3u)
            || (registrar->name == ContractSemantics::s_FamilyCookRegistrationType && registrar->templateArgs.size() == 4u));
        const b8 familyMember = typedRegistrar
            && m_symbols.resolveBaseTypeName(registrar->templateArgs[1].constantOrTypeName()) == registration.className;
        const b8 assetKind = registrar && (registrar->name == ContractSemantics::s_CompositeRootRegistrationType || registrar->name == ContractSemantics::s_NonStandaloneAssetRegistrationType) && registrar->templateArgs.size() == 1u
            && m_symbols.resolveBaseTypeName(registrar->templateArgs[0].constantOrTypeName()) == registration.className;
        if(!familyCodec && !familyMember && !assetKind){
            reportError(errors, registration.span, "register_asset requires a codec registrar or a typed registrar for the named asset");
            ok = false;
        }
    }
    for(const StaticAssertion& assertion : unit.staticAssertions){
        MethodDecl fileScope(m_arena);
        if(!checkExpr(nullptr, {}, false, fileScope, *assertion.condition, errors))
            ok = false;
    }

    m_recordGlobalHeaders = false;
    if(!m_layoutOptimizer.optimizeTranslationUnit(unit, errors))
        ok = false;

    m_recordGlobalHeaders = true;
    for(ClassDecl* owner : unit.classes){
        if(!owner->isImported || owner->isExtern || owner->templateParams.empty())
            continue;
        b8 emitsMethods = false;
        for(const ClassTemplateInstantiation* inst : unit.templateInstantiations){
            if(inst && !inst->isImported && inst->templateDecl == owner){
                emitsMethods = true;
                break;
            }
        }
        if(!emitsMethods)
            continue;
        for(MethodDecl* method : owner->methods){
            if(method->hasBody && !method->isExtern && !checkMethodBody(owner, *method, errors))
                ok = false;
        }
    }

    m_activeUnit = nullptr;
    return ok;
}

void TypeChecker::recordRequiredHeader(const MStringView header)noexcept{
    if(!m_recordGlobalHeaders || !m_activeUnit || header.empty())
        return;
    if(m_recordedRequiredHeaders.emplace(header, 1u).second)
        m_activeUnit->requiredGlobalHeaders.push_back(header);
}

void TypeChecker::recordBuiltinTypeHeader(const MStringView typeName)noexcept{
    if(!m_recordGlobalHeaders || !m_activeUnit || typeName.empty())
        return;
    if(!m_recordedTypeHeaders.emplace(typeName, 1u).second)
        return;
    if(const TypeAliasDecl* aliasDecl = m_symbols.findTypeAlias(typeName))
        recordRequiredHeader(aliasDecl->requiredHeader);
    const MStringView base = m_symbols.resolveBaseTypeName(typeName);
    if(base != typeName){
        if(const TypeAliasDecl* baseAliasDecl = m_symbols.findTypeAlias(base))
            recordRequiredHeader(baseAliasDecl->requiredHeader);
    }
    if(const EnumDecl* enumDecl = m_symbols.findEnum(base))
        recordRequiredHeader(enumDecl->requiredHeader);
    else if(const ClassDecl* cls = m_symbols.findClass(base))
        recordRequiredHeader(cls->requiredHeader);
    else if(const InterfaceDecl* iface = m_symbols.findInterface(base))
        recordRequiredHeader(iface->requiredHeader);
    if(base.empty() || m_symbols.hasTypeSymbol(base) || BuiltinTypeSemantics::IsScalarType(base))
        return;
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(base))
        recordRequiredHeader(objInfo->requiredHeader);
    else if(const BuiltinEnumInfo* enumInfo = FindBuiltinEnum(base))
        recordRequiredHeader(enumInfo->requiredHeader);
    else if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(base))
        recordRequiredHeader(genericInfo->requiredHeader);
}

void TypeChecker::recordBuiltinReturnTypeHeaders(
    const MStringView returnTypeName,
    const MStringView returnTypeArgName,
    const MStringView returnNestedTypeArgName,
    const MStringView returnErrorTypeName
)noexcept{
    recordBuiltinTypeHeader(returnTypeName);
    recordBuiltinTypeHeader(returnTypeArgName);
    recordBuiltinTypeHeader(returnNestedTypeArgName);
    recordBuiltinTypeHeader(returnErrorTypeName);
}

void TypeChecker::markScratchArenaUsed(MethodDecl& method)noexcept{
    method.usesScratchArena = true;
    m_activeParallelBlockUsesScratch = true;
}

b8 TypeChecker::isUnshadowedTypeScopeAccess(const Span<const ScopedVarInfo> scope, const MStringView scopeName)const noexcept{
    return !FindScopedVar(scope, scopeName) && !m_symbols.findGlobalConstant(scopeName);
}

b8 TypeChecker::isOwnedStackLocalVar(const ScopedVarInfo& local)const noexcept{
    return !local.isParam
        && local.mode == ParamPassingMode::None
        && m_symbols.resolveRefMode(local.type) == ParamPassingMode::None
        && !IsBorrowViewType(resolveScopedVarBaseType(local));
}

const TypeRef* TypeChecker::wrappedReferenceType(const TypeRef* wrapper)const noexcept{
    const TypeRef* const type = m_symbols.unwrapTypeAlias(wrapper);
    if(!type || !IsOptionalOrExpectedGenericType(type->name) || m_symbols.hasTypeSymbol(type->name) || type->templateArgs.empty())
        return nullptr;
    const TypeRef* const value = type->templateArgs[0].type;
    return m_symbols.resolveRefMode(value) != ParamPassingMode::None ? value : nullptr;
}

b8 TypeChecker::checkWrappedReferenceArgument(const ClassDecl* ownerClass, const MethodDecl& method, const TypeRef& target, Expr& argument, const Span<const ScopedVarInfo> scope, MVector<ParseError>& errors)const{
    const TypeRef* const value = wrappedReferenceType(&target);
    const TypeRef* const source = m_symbols.unwrapTypeAlias(inferExprTypeRef(ownerClass, &method, argument, scope));
    if(!value || (source && source->name == m_symbols.resolveBaseTypeName(target.name) && source->name != m_symbols.resolveBaseTypeName(value->name) && wrappedReferenceType(source)))
        return true;
    if(argument.kind == ExprKind::Call && argument.isBuiltinCall && argument.lhs && argument.lhs->kind == ExprKind::Identifier){
        if(const BuiltinFunctionInfo* fn = FindBuiltinFunction(argument.lhs->text); fn && fn->returnRule == BuiltinFunctionReturnRule::Unexpected)
            return true;
    }
    if(BorrowLifetimeAnalyzer(*this).isTemporaryOwnerExpr(ownerClass, &method, argument, scope, true)){
        reportError(errors, argument.span, "a stored reference requires an lvalue with an enclosing lifetime");
        return false;
    }
    if(m_symbols.resolveRefMode(value) == ParamPassingMode::Mut && !isPotentiallyMutableLvalueExpr(ownerClass, method, argument, scope)){
        reportError(errors, argument.span, "a stored mutable reference requires a mutable lvalue");
        return false;
    }
    PromoteStringByteReadToConstRef(argument);
    argument.wrapInNotNull = true;
    return true;
}

b8 TypeChecker::isTaskLocalIdentifierExpr(const Expr& expr, const Span<const ScopedVarInfo> scope)const noexcept{
    if(expr.kind != ExprKind::Identifier)
        return false;
    const ScopedVarInfo* local = FindScopedVar(scope, expr.text);
    return local && local->declaredParallelDepth == m_parallelDepth;
}

void TypeChecker::markSimdResultWrapperAdaptation(const TypeRef* const targetTypeRef, Expr& argExpr)const noexcept{
    if(!targetTypeRef)
        return;
    const TypeRef* unwrapped = m_symbols.unwrapTypeAlias(targetTypeRef);
    if(!unwrapped || unwrapped->templateArgs.empty() || !unwrapped->templateArgs[0].type || m_symbols.hasTypeSymbol(unwrapped->name))
        return;
    if(IsOptionalOrExpectedGenericType(unwrapped->name)){
        const MStringView valBase = m_symbols.resolveBaseTypeName(unwrapped->templateArgs[0].type->name);
        if(SymbolTable::IsSimdRegisterVectorType(valBase) && argExpr.resolvedSimdType == valBase)
            argExpr.wrapInSimdResult = true;
    }
}

void TypeChecker::markFormatArgWrap(
    const ClassDecl* ownerClass,
    const MethodDecl* method,
    Expr& arg,
    const Span<const ScopedVarInfo> scope,
    const b8 requireDiagnosticWrapFlag
)const noexcept{
    const MStringView fmtArgType = inferExprBaseType(ownerClass, method, arg, scope);
    if(m_symbols.hasTypeSymbol(fmtArgType))
        return;
    if(const BuiltinObjectTypeInfo* fmtObj = FindBuiltinObjectType(fmtArgType)){
        if(!requireDiagnosticWrapFlag){
            arg.formatArgWrapKind = fmtObj->formatArgWrapKind;
        }
        else if(const BuiltinFormatArgWrapInfo* wrapInfo = FindFormatArgWrapInfo(fmtObj->formatArgWrapKind); wrapInfo && wrapInfo->wrapInDiagnosticFormat){
            arg.formatArgWrapKind = fmtObj->formatArgWrapKind;
        }
    }
}

void TypeChecker::maybeAdaptInitToTChar(
    const ClassDecl* ownerClass,
    const MethodDecl& method,
    const Span<const ScopedVarInfo> scope,
    const MStringView baseTypeName,
    Expr* const initExpr,
    const Span<Expr* const> ctorArgs
)const noexcept{
    if(m_symbols.hasTypeSymbol(baseTypeName) || !RequiresTCharAdaptation(baseTypeName))
        return;
    if(initExpr)
        markStringToPathAdaptation(ownerClass, &method, scope, *initExpr);
    for(Expr* arg : ctorArgs){
        if(arg)
            markStringToPathAdaptation(ownerClass, &method, scope, *arg);
    }
}

void TypeChecker::deduceGlobalConstantAutoType(Stmt& gc, MethodDecl& dummyCtx, const Span<const ScopedVarInfo> emptyScope)const noexcept{
    if(!gc.expr || !gc.varType || !gc.varType->isAuto)
        return;
    if(const TypeRef* inferredRef = inferExprTypeRef(nullptr, &dummyCtx, *gc.expr, emptyScope); inferredRef && !inferredRef->isAuto){
        const SourceSpan savedSpan = gc.varType->span;
        *gc.varType = *inferredRef;
        gc.varType->span = savedSpan;
        gc.varType->isAuto = false;
        return;
    }
    const MStringView inferredName = inferExprTypeName(nullptr, &dummyCtx, *gc.expr, emptyScope);
    if(!inferredName.empty()){
        gc.varType->isAuto = false;
        gc.varType->name = inferredName;
    }
}

b8 TypeChecker::checkBuiltinVectorOrMatrixCtorCall(
    const ClassDecl* ownerClass,
    const MethodDecl& method,
    const MStringView baseCallee,
    const Span<Expr* const> arguments,
    const Span<const ScopedVarInfo> scope,
    const SourceSpan errorSpan,
    MVector<ParseError>& errors,
    const b8 checkSingleValueMaxArity
)const{
    b8 ok = true;
    const b8 isBuiltin = !m_symbols.hasTypeSymbol(baseCallee);
    b8 checkedVectorArity = false;
    if(isBuiltin){
        if(!IsValidBuiltinVectorOrMatrixCtorArity(baseCallee, arguments.size())){
            reportError(errors, errorSpan, "wrong number of arguments to built-in vector/matrix constructor");
            ok = false;
            checkedVectorArity = true;
        }
        else if(arguments.size() == 2u && arguments[0] && !SymbolTable::IsSimdRegisterVectorType(baseCallee)){
            const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(baseCallee);
            const BuiltinUnalignedVectorInfo* unalignedInfo = simdVecInfo ? nullptr : FindBuiltinUnalignedVectorType(baseCallee);
            const b8 requiresSimdRegArg0 = (simdVecInfo && simdVecInfo->lanes == 4u && simdVecInfo->altCtorArity != 2u)
                || (unalignedInfo && unalignedInfo->lanes == 4u && unalignedInfo->altCtorArity != 2u);
            if(requiresSimdRegArg0 && !SymbolTable::IsSimdRegisterVectorType(inferExprTypeName(ownerClass, &method, *arguments[0], scope))){
                reportError(errors, errorSpan, "wrong number of arguments to built-in vector/matrix constructor");
                ok = false;
                checkedVectorArity = true;
            }
        }
        if(const BuiltinSimdVectorInfo* simdVecInfo = FindBuiltinSimdVectorType(baseCallee); simdVecInfo && simdVecInfo->isMixed && arguments.size() == 1u && arguments[0]){
            const MStringView arg0Type = inferExprTypeName(ownerClass, &method, *arguments[0], scope);
            if(arg0Type != baseCallee){
                reportError(errors, errorSpan, "mixed float/integer SIMD vector type does not support single-scalar splat construction; provide all 4 lane arguments");
                ok = false;
            }
        }
    }
    if(
        !checkedVectorArity
        && checkSingleValueMaxArity
        && arguments.size() > 1u
        && (
            m_symbols.isEnumType(baseCallee)
            || (isBuiltin && (BuiltinTypeSemantics::IsScalarType(baseCallee) || IsSingleValueCtorBuiltinType(baseCallee)))
        )
    ){
        reportError(errors, errorSpan, "scalar, enum, or single-value constructor accepts at most 1 argument");
        ok = false;
    }
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
    const ScopedVarInfo* lambdaThis = m_inLambda ? FindScopedVar(scope, KeywordSemantics::s_This) : nullptr;
    return method.isConst || (lambdaThis && lambdaThis->isConst);
}

b8 TypeChecker::validateThisAccess(const ClassDecl* ownerClass, const SourceSpan span, const Span<const ScopedVarInfo> scope, MVector<ParseError>& errors)const{
    if(!ownerClass){
        reportError(errors, span, "'this' cannot be used in a free function");
        return false;
    }
    if(m_inLambda && !FindScopedVar(scope, KeywordSemantics::s_This)){
        reportError(errors, span, "accessing 'this' inside a lambda requires capturing 'in this' or 'mut this'");
        return false;
    }
    return true;
}

b8 TypeChecker::isConstructibleTypeName(const MStringView calleeName, const MStringView baseCallee)const noexcept{
    if(m_symbols.findClass(baseCallee) != nullptr || m_symbols.findTypeAlias(calleeName) != nullptr)
        return true;
    if(m_symbols.hasTypeSymbol(baseCallee))
        return false;
    if(const BuiltinObjectTypeInfo* objInfo = FindBuiltinObjectType(baseCallee))
        return !objInfo->isAbstract;
    return SymbolTable::IsSimdType(baseCallee)
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
        return m_symbols.findClassOperator(*cls, opSym, &arity, recvIsConst, !m_symbols.canAccessPrivateMembers(*cls, ownerClass));
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
    if(KeywordSemantics::IsThisKeyword(expr.text)){
        if(ownerClass){
            if(const FieldDecl* field = SymbolTable::FindClassField(*ownerClass, rootName); field && field->type){
                const MStringView fieldTypeName = m_symbols.resolveBaseTypeName(field->type->name);
                if(m_symbols.isEnumType(fieldTypeName))
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
                if(!fieldTypeName.empty() && m_symbols.isEnumType(fieldTypeName))
                    return fieldTypeName;
            }
        }
        else if(!m_symbols.hasTypeSymbol(lhsBase)){
            if(const BuiltinFieldInfo* bf = FindBuiltinField(lhsBase, rootName)){
                const MStringView bfTypeName = m_symbols.resolveBaseTypeName(bf->fieldTypeName);
                if(!bfTypeName.empty() && m_symbols.isEnumType(bfTypeName))
                    return bfTypeName;
            }
        }
        return {};
    }
    if(const ScopedVarInfo* local = FindScopedVar(scope, rootName)){
        const MStringView localTypeName = resolveScopedVarBaseType(*local);
        if(!localTypeName.empty() && m_symbols.isEnumType(localTypeName))
            return localTypeName;
        return {};
    }
    if(const Stmt* gc = m_symbols.findGlobalConstant(rootName); gc && gc->varType){
        const MStringView gcTypeName = m_symbols.resolveBaseTypeName(gc->varType->name);
        if(!gcTypeName.empty() && m_symbols.isEnumType(gcTypeName))
            return gcTypeName;
        return {};
    }
    const MStringView resolvedRoot = m_symbols.resolveBaseTypeName(rootName);
    if(!resolvedRoot.empty() && m_symbols.isEnumType(resolvedRoot))
        return resolvedRoot;
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

