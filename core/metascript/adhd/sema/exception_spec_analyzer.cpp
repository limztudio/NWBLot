// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>
#include <core/metascript/adhd/ast/contract_semantics.h>
#include <core/metascript/adhd/ast/operator_semantics.h>

#include "exception_spec_analyzer.h"
#include "type_checker.h"
#include "type_comparator.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_exception_spec_analyzer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 DoesBuiltinTypeRequireArenaOnNonEmptyInit(const MStringView baseType, const TypeRef* typeRef, const SymbolTable& symbols, const TypeLayoutAnalyzer& layoutAnalyzer)noexcept{
    if(typeRef)
        return layoutAnalyzer.requiresMotherArena(*typeRef);
    return !baseType.empty()
        && !symbols.hasTypeSymbol(baseType)
        && (SymbolTable::IsArenaBackedContainer(baseType) || IsArenaFirstCtorType(baseType));
}

[[nodiscard]] b8 ExternClassHasSafeTrivialDefault(const ClassDecl& cls)noexcept{
    return !cls.isAbstract && (cls.isNothrowDefaultConstructible || cls.isSmallValue || cls.isBorrowView || cls.isBinarySerializable || cls.emitsBraceFieldInit);
}

[[nodiscard]] b8 IsProvenNothrowBuiltinConstructibleType(const MStringView baseType, const SymbolTable& symbols)noexcept{
    if(baseType.empty() || symbols.hasTypeSymbol(baseType))
        return symbols.findEnum(baseType) != nullptr;
    if(BuiltinTypeSemantics::IsScalarType(baseType) || SymbolTable::IsSimdType(baseType) || FindBuiltinEnum(baseType) != nullptr)
        return true;
    if(const BuiltinObjectTypeInfo* obj = FindBuiltinObjectType(baseType))
        return !obj->isAbstract && (obj->isNothrowDefaultConstructible || obj->isSmallValue || obj->isBorrowView || obj->isBinarySerializable || obj->emitsBraceFieldInit);
    if(const BuiltinGenericTypeInfo* generic = FindBuiltinGenericType(baseType)){
        return generic->isCopyMoveConstructible
            || generic->isSmallValue
            || generic->isBorrowView
            || generic->isBinarySerializable
            || generic->emitsBraceFieldInit
            || generic->inlineLayoutKind == InlineLayoutKind::Atomic
            || (generic->isArenaBacked && generic->isSelfDefaulted);
    }
    return false;
}

[[nodiscard]] b8 HasPolymorphicSdkMatchingNothrow(const ClassDecl& sdk, const MethodDecl& method, const SymbolTable& symbols)noexcept{
    const TypeComparator comparator(symbols);
    for(const MethodDecl* candidate : sdk.methods){
        if(!candidate || candidate->name != method.name || candidate->isConst != method.isConst)
            continue;
        if(!comparator.areEquivalent(method.returnType, candidate->returnType))
            continue;
        if(!candidate->isVariadic && !comparator.doParamListsMatchWithSubst(method.params, candidate->params, {}, {}))
            continue;
        return !candidate->allowsCppUnwind;
    }
    return false;
}

[[nodiscard]] b8 IsNothrowOverrideContract(const ClassDecl& cls, const MethodDecl& method, const SymbolTable& symbols, MetaArena& arena, const u32 depth = 0u){
    if(depth > 32u || !method.isOverride)
        return false;

    const TypeComparator comparator(symbols);
    for(const TypeRef* baseType : cls.implementedInterfaces){
        if(!baseType)
            continue;
        const MStringView baseName = symbols.resolveBaseTypeName(baseType->name);
        if(const ClassDecl* sdk = symbols.findPolymorphicSdkInterface(baseName)){
            if(HasPolymorphicSdkMatchingNothrow(*sdk, method, symbols))
                return true;
            continue;
        }
        if(const InterfaceDecl* ifaceDecl = symbols.findInterface(baseName); ifaceDecl && ifaceDecl->isExtern){
            MVector<MethodDecl*> ifaceMethods(arena);
            symbols.collectAllInterfaceMethods(*baseType, ifaceMethods);
            for(const MethodDecl* req : ifaceMethods){
                if(!req || req->name != method.name || req->isConst != method.isConst || req->isOperator != method.isOperator)
                    continue;
                if(!comparator.areEquivalent(method.returnType, req->returnType) || !comparator.doParamListsMatch(method.params, req->params))
                    continue;
                if(req->isExtern && !req->allowsCppUnwind)
                    return true;
            }
            continue;
        }
        if(const auto* signature = FindBuiltinReceiverSignature(baseName, method.name)){
            if(signature->arity == method.params.size() && !signature->allowsCppUnwind)
                return true;
        }
    }

    if(!cls.systemTypeName.empty()){
        if(const ClassRoleContractInfo* sysContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::System)){
            if(const ClassDecl* sdk = symbols.findPolymorphicSdkInterface(sysContract->implicitInterfaceAdhdName)){
                if(HasPolymorphicSdkMatchingNothrow(*sdk, method, symbols))
                    return true;
            }
            else if(const auto* signature = FindBuiltinReceiverSignature(sysContract->implicitInterfaceAdhdName, method.name)){
                if(signature->arity == method.params.size() && !signature->allowsCppUnwind)
                    return true;
            }
        }
    }

    if(cls.concreteBase){
        const usize arity = method.params.size();
        if(const MethodDecl* baseMethod = symbols.findClassMethod(*cls.concreteBase, method.name, &arity, method.isConst)){
            if(baseMethod->isOverride
                && comparator.areEquivalent(method.returnType, baseMethod->returnType)
                && comparator.doParamListsMatch(method.params, baseMethod->params)){
                if(IsNothrowOverrideContract(*cls.concreteBase, *baseMethod, symbols, arena, depth + 1u))
                    return true;
            }
        }
    }

    return false;
}

void ResetStmtLambdas(Span<Stmt* const> stmts)noexcept;

void ResetExprLambdas(Expr* expr)noexcept{
    if(!expr)
        return;
    if(expr->kind == ExprKind::Lambda)
        expr->lambdaAllowsUnwind = false;
    ResetExprLambdas(expr->lhs);
    ResetExprLambdas(expr->rhs);
    for(Expr* arg : expr->arguments)
        ResetExprLambdas(arg);
    ResetStmtLambdas(expr->lambdaBody);
}

void ResetStmtLambdas(const Span<Stmt* const> stmts)noexcept{
    for(Stmt* s : stmts){
        if(!s)
            continue;
        ResetExprLambdas(s->expr);
        ResetExprLambdas(s->condition);
        ResetExprLambdas(s->stepExpr);
        for(Expr* arg : s->arguments)
            ResetExprLambdas(arg);
        if(s->initStmt){
            Stmt* initPtr = s->initStmt;
            ResetStmtLambdas(Span<Stmt* const>(&initPtr, 1u));
        }
        ResetStmtLambdas(s->statements);
        ResetStmtLambdas(s->elseStatements);
        for(SwitchCase& sc : s->switchCases)
            ResetStmtLambdas(sc.statements);
    }
}

[[nodiscard]] b8 CanClassHaveBodyUnwind(const MethodDecl& m)noexcept{
    return m.hasBody
        && !m.isExtern
        && !m.declaredBodylessInImport
        && !m.forceNothrowContract
        && !m.isOpenVirtualContract;
}

[[nodiscard]] b8 CanFreeFunctionHaveBodyUnwind(const MethodDecl& fn)noexcept{
    return fn.hasBody
        && !fn.isExtern
        && !fn.declaredBodylessInImport
        && fn.exceptionProbes.empty();
}

[[nodiscard]] b8 HasRoleContract(const ClassDecl& cls)noexcept{
    return !cls.assetTypeName.empty()
        || !cls.systemTypeName.empty()
        || !cls.subsystemTypeName.empty()
        || !cls.componentTypeName.empty();
}

[[nodiscard]] b8 IsLocalVariableIdentifier(const Expr& expr, const Span<const ScopedVarInfo> scope)noexcept{
    if(expr.kind != ExprKind::Identifier)
        return false;
    for(usize i = scope.size(); i > 0u; --i){
        if(scope[i - 1u].name == expr.text)
            return !scope[i - 1u].isParam && scope[i - 1u].mode == ParamPassingMode::None;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ExceptionSpecAnalyzer::runMethodPass(const ClassDecl* ownerClass, MethodDecl& method, b8& outLambdaChanged)const{
    b8 changed = false;
    MVector<ScopedVarInfo> scope(m_arena);
    scope.reserve(method.params.size() + 8u);
    for(const ParamDecl& p : method.params){
        ScopedVarInfo info;
        info.name = p.name;
        info.type = p.type;
        info.mode = p.mode;
        info.isConst = (p.mode == ParamPassingMode::In);
        info.isParam = true;
        info.retainsBorrow = p.retainsBorrow;
        scope.push_back(info);
    }

    b8 bodyUnwinds = false;
    if(ownerClass && method.isConstructor)
        bodyUnwinds |= constructorImplicitlyUnwinds(*ownerClass, method, scope, outLambdaChanged);
    if(ownerClass && method.isDestructor)
        bodyUnwinds |= destructorImplicitlyUnwinds(*ownerClass);

    bodyUnwinds |= analyzeStatementList(ownerClass, &method, method.body, scope, outLambdaChanged);

    if(ownerClass ? __hidden_adhd_exception_spec_analyzer::CanClassHaveBodyUnwind(method) : __hidden_adhd_exception_spec_analyzer::CanFreeFunctionHaveBodyUnwind(method)){
        if(bodyUnwinds && !method.allowsCppUnwind){
            method.allowsCppUnwind = true;
            changed = true;
        }
    }
    return changed;
}

void ExceptionSpecAnalyzer::runFieldDefaultsPass(const ClassDecl& cls, b8& outLambdaChanged)const{
    MVector<ScopedVarInfo> emptyScope(m_arena);
    for(FieldDecl* field : cls.fields){
        if(!field)
            continue;
        if(field->defaultExpr)
            (void)analyzeExpr(&cls, nullptr, *field->defaultExpr, emptyScope, outLambdaChanged);
        for(Expr* arg : field->ctorArgs){
            if(arg)
                (void)analyzeExpr(&cls, nullptr, *arg, emptyScope, outLambdaChanged);
        }
    }
}

b8 ExceptionSpecAnalyzer::exprCopiesArenaValue(const Expr& valueExpr, const TypeRef* targetType, MVector<ScopedVarInfo>& scope)const{
    if(!targetType || !__hidden_adhd_exception_spec_analyzer::DoesBuiltinTypeRequireArenaOnNonEmptyInit(m_symbols.resolveBaseTypeName(targetType->name), targetType, m_symbols, m_layoutAnalyzer))
        return false;
    if(valueExpr.kind == ExprKind::StringLiteral)
        return true;
    if(valueExpr.kind == ExprKind::Call || IsAlwaysRvalueExprKind(valueExpr.kind))
        return false;
    return !__hidden_adhd_exception_spec_analyzer::IsLocalVariableIdentifier(valueExpr, scope);
}

b8 ExceptionSpecAnalyzer::classDefaultCtorUnwinds(const ClassDecl& cls, const b8 requirePublic)const noexcept{
    if(const MethodDecl* ctor0 = m_symbols.findClassConstructor(cls, 0u, requirePublic))
        return ctor0->allowsCppUnwind;
    if(cls.isExtern)
        return !__hidden_adhd_exception_spec_analyzer::ExternClassHasSafeTrivialDefault(cls);
    return !m_layoutAnalyzer.isClassNothrowDefaultConstructible(cls, cls.orderedFields.empty() ? cls.fields : cls.orderedFields);
}

b8 ExceptionSpecAnalyzer::classCallCtorUnwinds(
    const ClassDecl& cls,
    const Span<const Expr* const> args,
    const ClassDecl* ownerClass,
    const MethodDecl* method,
    const Span<const ScopedVarInfo> scope
)const{
    const usize argCount = args.size();
    MVector<MStringView> argTypes(m_arena);
    MVector<u8> argIsMut(m_arena);
    m_checker.buildCallArgHints(ownerClass, method, args, scope, argTypes, argIsMut);
    const b8 requirePublic = !m_symbols.canAccessPrivateMembers(cls, ownerClass);
    if(const MethodDecl* ctor = m_symbols.findClassConstructor(cls, argCount, requirePublic, argTypes, argIsMut))
        return ctor->allowsCppUnwind;
    if(cls.isExtern){
        if(argCount == 0u)
            return !__hidden_adhd_exception_spec_analyzer::ExternClassHasSafeTrivialDefault(cls);
        return true;
    }
    if(argCount == 0u)
        return classDefaultCtorUnwinds(cls, requirePublic);
    return false;
}

b8 ExceptionSpecAnalyzer::constructorImplicitlyUnwinds(const ClassDecl& ownerClass, const MethodDecl& ctor, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const{
    if(!ctor.initializedCppBase.empty())
        return true;
    if(ownerClass.concreteBase){
        if(classDefaultCtorUnwinds(*ownerClass.concreteBase, false))
            return true;
    }

    b8 unwinds = false;
    const Span<const FieldDecl* const> fields = ownerClass.orderedFields.empty() ? ownerClass.fields : ownerClass.orderedFields;
    for(const FieldDecl* field : fields){
        if(!field || field->isStatic || !field->type)
            continue;
        const TypeRef* fieldType = m_symbols.unwrapTypeAlias(field->type);
        if(!fieldType || m_symbols.resolveRefMode(fieldType) != ParamPassingMode::None)
            continue;
        const MStringView fieldBase = m_symbols.resolveBaseTypeName(fieldType->name);

        if(field->defaultExpr){
            if(analyzeExpr(&ownerClass, &ctor, *field->defaultExpr, scope, outLambdaChanged))
                unwinds = true;
            if(field->defaultExpr->kind == ExprKind::StringLiteral && m_layoutAnalyzer.requiresMotherArena(*fieldType))
                unwinds = true;
            else if(m_layoutAnalyzer.requiresMotherArena(*fieldType) && !m_checker.isRvalueTemporaryExpr(&ownerClass, &ctor, *field->defaultExpr, scope))
                unwinds = true;
        }
        else if(field->hasCtorInit){
            for(Expr* arg : field->ctorArgs){
                if(arg && analyzeExpr(&ownerClass, &ctor, *arg, scope, outLambdaChanged))
                    unwinds = true;
            }
            if(!field->ctorArgs.empty() && m_layoutAnalyzer.requiresMotherArena(*fieldType))
                unwinds = true;
            if(const ClassDecl* fieldCls = m_symbols.findClass(fieldBase)){
                if(classCallCtorUnwinds(*fieldCls, field->ctorArgs, &ownerClass, &ctor, scope))
                    unwinds = true;
            }
            else if(field->ctorArgs.size() == 1u && field->ctorArgs[0] && field->ctorArgs[0]->kind == ExprKind::StringLiteral && IsStringLikeType(fieldBase) && m_layoutAnalyzer.requiresMotherArena(*fieldType)){
                unwinds = true;
            }
            else if(!__hidden_adhd_exception_spec_analyzer::IsProvenNothrowBuiltinConstructibleType(fieldBase, m_symbols)){
                unwinds = true;
            }
        }
        else{
            if(const ClassDecl* fieldCls = m_symbols.findClass(fieldBase)){
                if(classDefaultCtorUnwinds(*fieldCls, !m_symbols.canAccessPrivateMembers(*fieldCls, &ownerClass)))
                    unwinds = true;
            }
            else if(!(fieldType && fieldType->isFuncType) && !__hidden_adhd_exception_spec_analyzer::IsProvenNothrowBuiltinConstructibleType(fieldBase, m_symbols)){
                unwinds = true;
            }
        }
    }
    return unwinds;
}

b8 ExceptionSpecAnalyzer::destructorImplicitlyUnwinds(const ClassDecl& ownerClass)const noexcept{
    if(ownerClass.concreteBase){
        for(const MethodDecl* m : ownerClass.concreteBase->methods){
            if(m && m->isDestructor && m->allowsCppUnwind)
                return true;
        }
    }
    for(const FieldDecl* field : ownerClass.fields){
        if(!field || field->isStatic || !field->type)
            continue;
        const TypeRef* fieldType = m_symbols.unwrapTypeAlias(field->type);
        if(!fieldType || m_symbols.resolveRefMode(fieldType) != ParamPassingMode::None)
            continue;
        const MStringView fieldBase = m_symbols.resolveBaseTypeName(fieldType->name);
        if(const ClassDecl* fieldCls = m_symbols.findClass(fieldBase); fieldCls && !fieldCls->isExtern){
            for(const MethodDecl* fm : fieldCls->methods){
                if(fm && fm->isDestructor && fm->allowsCppUnwind)
                    return true;
            }
        }
    }
    return false;
}

void ExceptionSpecAnalyzer::pushVarDeclToScope(const ClassDecl* ownerClass, const MethodDecl* method, const Stmt& stmt, MVector<ScopedVarInfo>& scope)const{
    ScopedVarInfo info;
    info.name = stmt.name;
    info.type = stmt.varType;
    info.mode = stmt.varType ? stmt.varType->refMode : ParamPassingMode::None;
    info.isConst = stmt.isConst;
    if(stmt.expr && stmt.expr->kind == ExprKind::Lambda)
        info.lambdaExpr = stmt.expr;
    if(stmt.varType && stmt.varType->isAuto && stmt.expr){
        info.inferredTypeName = m_checker.inferExprBaseType(ownerClass, method, *stmt.expr, scope);
        if(const TypeRef* resolved = m_checker.inferExprTypeRef(ownerClass, method, *stmt.expr, scope)){
            TypeRef* copy = AllocateAstNode<TypeRef>(m_arena);
            *copy = *resolved;
            copy->refMode = info.mode;
            info.type = copy;
        }
    }
    scope.push_back(info);
}

b8 ExceptionSpecAnalyzer::analyzeStatementList(const ClassDecl* ownerClass, const MethodDecl* method, const Span<Stmt* const> statements, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const{
    const usize savedScopeSize = scope.size();
    b8 unwinds = false;
    for(Stmt* stmt : statements){
        if(!stmt)
            continue;
        if(analyzeStatement(ownerClass, method, *stmt, scope, outLambdaChanged))
            unwinds = true;
    }
    while(scope.size() > savedScopeSize)
        scope.pop_back();
    return unwinds;
}

b8 ExceptionSpecAnalyzer::analyzeStatement(const ClassDecl* ownerClass, const MethodDecl* method, Stmt& stmt, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const{
    b8 unwinds = false;
    switch(stmt.kind){
    case StmtKind::Block:
        unwinds |= analyzeStatementList(ownerClass, method, stmt.statements, scope, outLambdaChanged);
        break;
    case StmtKind::VarDecl:{
        if(stmt.expr && analyzeExpr(ownerClass, method, *stmt.expr, scope, outLambdaChanged))
            unwinds = true;
        for(Expr* arg : stmt.arguments){
            if(arg && analyzeExpr(ownerClass, method, *arg, scope, outLambdaChanged))
                unwinds = true;
        }

        const ParamPassingMode::Enum refMode = stmt.varType ? m_symbols.resolveRefMode(stmt.varType) : ParamPassingMode::None;
        const TypeRef* effectiveType = (stmt.varType && stmt.varType->isAuto && stmt.expr)
            ? m_checker.inferExprTypeRef(ownerClass, method, *stmt.expr, scope)
            : m_symbols.unwrapTypeAlias(stmt.varType);
        const MStringView baseType = effectiveType
            ? m_symbols.resolveBaseTypeName(effectiveType->name)
            : ((stmt.varType && stmt.varType->isAuto && stmt.expr)
                ? m_checker.inferExprBaseType(ownerClass, method, *stmt.expr, scope)
                : MStringView{});

        if(refMode == ParamPassingMode::None && !baseType.empty()){
            if(const ClassDecl* cls = m_symbols.findClass(baseType)){
                if(stmt.isCtorInit){
                    if(classCallCtorUnwinds(*cls, stmt.arguments, ownerClass, method, scope))
                        unwinds = true;
                }
                else if(!stmt.expr){
                    if(classDefaultCtorUnwinds(*cls, !m_symbols.canAccessPrivateMembers(*cls, ownerClass)))
                        unwinds = true;
                }
                else if(exprCopiesArenaValue(*stmt.expr, effectiveType, scope)){
                    unwinds = true;
                }
            }
            else if(stmt.isCtorInit){
                if(!__hidden_adhd_exception_spec_analyzer::IsProvenNothrowBuiltinConstructibleType(baseType, m_symbols))
                    unwinds = true;
                else if(!stmt.arguments.empty() && __hidden_adhd_exception_spec_analyzer::DoesBuiltinTypeRequireArenaOnNonEmptyInit(baseType, effectiveType, m_symbols, m_layoutAnalyzer))
                    unwinds = true;
            }
            else if(!stmt.expr){
                if(!(effectiveType && effectiveType->isFuncType) && !__hidden_adhd_exception_spec_analyzer::IsProvenNothrowBuiltinConstructibleType(baseType, m_symbols))
                    unwinds = true;
            }
            else if(exprCopiesArenaValue(*stmt.expr, effectiveType, scope)){
                unwinds = true;
            }
        }

        pushVarDeclToScope(ownerClass, method, stmt, scope);
        break;
    }
    case StmtKind::ExprStmt:
        if(stmt.expr)
            unwinds |= analyzeExpr(ownerClass, method, *stmt.expr, scope, outLambdaChanged);
        break;
    case StmtKind::Return:
        if(stmt.expr){
            if(analyzeExpr(ownerClass, method, *stmt.expr, scope, outLambdaChanged))
                unwinds = true;
            if(method && method->returnType && m_symbols.resolveRefMode(method->returnType) == ParamPassingMode::None){
                const TypeRef* retType = m_symbols.unwrapTypeAlias(method->returnType);
                if(exprCopiesArenaValue(*stmt.expr, retType, scope))
                    unwinds = true;
            }
        }
        break;
    case StmtKind::If:
        if(stmt.condition && analyzeExpr(ownerClass, method, *stmt.condition, scope, outLambdaChanged))
            unwinds = true;
        if(analyzeStatementList(ownerClass, method, stmt.statements, scope, outLambdaChanged))
            unwinds = true;
        if(analyzeStatementList(ownerClass, method, stmt.elseStatements, scope, outLambdaChanged))
            unwinds = true;
        break;
    case StmtKind::Switch:
        if(stmt.expr && analyzeExpr(ownerClass, method, *stmt.expr, scope, outLambdaChanged))
            unwinds = true;
        for(const SwitchCase& sc : stmt.switchCases){
            if(sc.matchExpr && analyzeExpr(ownerClass, method, *sc.matchExpr, scope, outLambdaChanged))
                unwinds = true;
            if(analyzeStatementList(ownerClass, method, sc.statements, scope, outLambdaChanged))
                unwinds = true;
        }
        break;
    case StmtKind::ForRange:{
        if(stmt.expr && analyzeExpr(ownerClass, method, *stmt.expr, scope, outLambdaChanged))
            unwinds = true;
        const usize savedScopeSize = scope.size();
        ScopedVarInfo loopVar;
        loopVar.name = stmt.name;
        loopVar.mode = stmt.rangeMode;
        loopVar.isConst = (stmt.rangeMode == ParamPassingMode::In || stmt.isConst);
        const TypeRef* rangeTypeRef = stmt.expr ? m_checker.inferExprTypeRef(ownerClass, method, *stmt.expr, scope) : nullptr;
        const TypeRef* elemTypeRef = nullptr;
        if(stmt.varType && !stmt.varType->isAuto){
            elemTypeRef = m_symbols.unwrapTypeAlias(stmt.varType);
            loopVar.type = stmt.varType;
        }
        else if(rangeTypeRef){
            const TypeRef* unwrappedRange = m_symbols.unwrapTypeAlias(rangeTypeRef);
            if(unwrappedRange && !unwrappedRange->templateArgs.empty() && unwrappedRange->templateArgs[0].type){
                elemTypeRef = m_symbols.unwrapTypeAlias(unwrappedRange->templateArgs[0].type);
                loopVar.type = unwrappedRange->templateArgs[0].type;
            }
        }
        if(stmt.rangeMode == ParamPassingMode::None && elemTypeRef && m_layoutAnalyzer.requiresMotherArena(*elemTypeRef))
            unwinds = true;
        scope.push_back(loopVar);
        if(analyzeStatementList(ownerClass, method, stmt.statements, scope, outLambdaChanged))
            unwinds = true;
        while(scope.size() > savedScopeSize)
            scope.pop_back();
        break;
    }
    case StmtKind::ForIndex:{
        const usize savedScopeSize = scope.size();
        if(stmt.initStmt && analyzeStatement(ownerClass, method, *stmt.initStmt, scope, outLambdaChanged))
            unwinds = true;
        if(stmt.condition && analyzeExpr(ownerClass, method, *stmt.condition, scope, outLambdaChanged))
            unwinds = true;
        if(stmt.stepExpr && analyzeExpr(ownerClass, method, *stmt.stepExpr, scope, outLambdaChanged))
            unwinds = true;
        if(analyzeStatementList(ownerClass, method, stmt.statements, scope, outLambdaChanged))
            unwinds = true;
        while(scope.size() > savedScopeSize)
            scope.pop_back();
        break;
    }
    case StmtKind::While:
    case StmtKind::DoWhile:
        if(stmt.condition && analyzeExpr(ownerClass, method, *stmt.condition, scope, outLambdaChanged))
            unwinds = true;
        if(analyzeStatementList(ownerClass, method, stmt.statements, scope, outLambdaChanged))
            unwinds = true;
        break;
    case StmtKind::ParallelForRange:
    case StmtKind::ParallelForIndex:
    case StmtKind::ParallelBlock:
    case StmtKind::ParallelCall:
        unwinds = true;
        if(stmt.initStmt)
            (void)analyzeStatement(ownerClass, method, *stmt.initStmt, scope, outLambdaChanged);
        if(stmt.condition)
            (void)analyzeExpr(ownerClass, method, *stmt.condition, scope, outLambdaChanged);
        if(stmt.stepExpr)
            (void)analyzeExpr(ownerClass, method, *stmt.stepExpr, scope, outLambdaChanged);
        if(stmt.expr)
            (void)analyzeExpr(ownerClass, method, *stmt.expr, scope, outLambdaChanged);
        for(Expr* arg : stmt.arguments){
            if(arg)
                (void)analyzeExpr(ownerClass, method, *arg, scope, outLambdaChanged);
        }
        (void)analyzeStatementList(ownerClass, method, stmt.statements, scope, outLambdaChanged);
        break;
    case StmtKind::Diagnostic:
        for(Expr* arg : stmt.arguments){
            if(arg && analyzeExpr(ownerClass, method, *arg, scope, outLambdaChanged))
                unwinds = true;
        }
        break;
    case StmtKind::Break:
    case StmtKind::Continue:
        break;
    }
    return unwinds;
}

b8 ExceptionSpecAnalyzer::analyzeExpr(const ClassDecl* ownerClass, const MethodDecl* method, Expr& expr, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const{
    b8 unwinds = false;
    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::StringLiteral:
    case ExprKind::Identifier:
    case ExprKind::ThisMember:
    case ExprKind::LayoutQuery:
        break;
    case ExprKind::ScopeAccess:
        if(expr.lhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs, scope, outLambdaChanged);
        break;
    case ExprKind::MemberAccess:
        if(expr.lhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs, scope, outLambdaChanged);
        break;
    case ExprKind::Cast:
    case ExprKind::BitCast:
        if(expr.lhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs, scope, outLambdaChanged);
        if(expr.lhs && expr.targetType && m_symbols.resolveRefMode(expr.targetType) == ParamPassingMode::None){
            const TypeRef* targetUnwrapped = m_symbols.unwrapTypeAlias(expr.targetType);
            if(exprCopiesArenaValue(*expr.lhs, targetUnwrapped, scope))
                unwinds = true;
        }
        break;
    case ExprKind::Index:{
        if(expr.lhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs, scope, outLambdaChanged);
        if(expr.rhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.rhs, scope, outLambdaChanged);
        if(expr.lhs){
            const TypeRef* lhsTypeRef = nullptr;
            const MStringView lhsBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
            if(!m_symbols.hasTypeSymbol(lhsBase)){
                if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(lhsBase); genericInfo && genericInfo->isReflectedMap)
                    unwinds = true;
            }
            else if(const ClassDecl* cls = m_symbols.findClass(lhsBase)){
                const b8 recvIsConst = method ? m_checker.isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
                if(const MethodDecl* opMethod = m_checker.resolveCustomOperator(ownerClass, method, lhsTypeRef, lhsBase, OperatorSemantics::s_SubscriptSymbol, 1u, recvIsConst)){
                    if(opMethod->allowsCppUnwind)
                        unwinds = true;
                }
                else if(cls->isExtern){
                    unwinds = true;
                }
            }
        }
        break;
    }
    case ExprKind::Unary:{
        if(expr.lhs){
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs, scope, outLambdaChanged);
            const TypeRef* lhsTypeRef = nullptr;
            const MStringView lhsBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
            const MStringView opSym = OperatorSemantics::MapTokenTypeToOperatorSymbol(expr.op);
            if(!opSym.empty() && m_symbols.hasTypeSymbol(lhsBase)){
                const b8 recvIsConst = method ? m_checker.isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
                if(const MethodDecl* opMethod = m_checker.resolveCustomOperator(ownerClass, method, lhsTypeRef, lhsBase, opSym, 0u, recvIsConst)){
                    if(opMethod->allowsCppUnwind)
                        unwinds = true;
                }
            }
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.lhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs, scope, outLambdaChanged);
        if(expr.rhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.rhs, scope, outLambdaChanged);
        if(!expr.lhs)
            break;

        const TypeRef* lhsTypeRef = nullptr;
        const MStringView lhsBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs, scope, &lhsTypeRef);
        if(OperatorSemantics::IsCompoundOrSimpleAssignment(expr.op)){
            if(__hidden_adhd_exception_spec_analyzer::DoesBuiltinTypeRequireArenaOnNonEmptyInit(lhsBase, lhsTypeRef, m_symbols, m_layoutAnalyzer))
                unwinds = true;
            if(m_symbols.hasTypeSymbol(lhsBase)){
                const MStringView opSym = OperatorSemantics::MapTokenTypeToOperatorSymbol(expr.op);
                const b8 recvIsConst = method ? m_checker.isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
                if(const MethodDecl* opMethod = m_checker.resolveCustomOperator(ownerClass, method, lhsTypeRef, lhsBase, opSym, 1u, recvIsConst)){
                    if(opMethod->allowsCppUnwind)
                        unwinds = true;
                }
                else if(expr.op == TokenType::Equal){
                    if(const ClassDecl* cls = m_symbols.findClass(lhsBase); cls && cls->needsClassArena)
                        unwinds = true;
                }
            }
        }
        else{
            if(expr.op == TokenType::Plus && !m_symbols.hasTypeSymbol(lhsBase) && IsStringLikeType(lhsBase) && SymbolTable::IsArenaBackedContainer(lhsBase))
                unwinds = true;
            const MStringView opSym = OperatorSemantics::MapTokenTypeToOperatorSymbol(expr.op);
            if(!opSym.empty() && m_symbols.hasTypeSymbol(lhsBase)){
                const b8 recvIsConst = method ? m_checker.isReceiverConst(ownerClass, *method, *expr.lhs, scope) : false;
                if(const MethodDecl* opMethod = m_checker.resolveCustomOperator(ownerClass, method, lhsTypeRef, lhsBase, opSym, 1u, recvIsConst)){
                    if(opMethod->allowsCppUnwind)
                        unwinds = true;
                }
            }
        }
        break;
    }
    case ExprKind::Lambda:{
        const usize savedScopeSize = scope.size();
        MVector<ScopedVarInfo> lambdaScope(m_arena);
        lambdaScope.reserve(expr.lambdaCaptures.size() + expr.lambdaParams.size());
        for(const LambdaCapture& cap : expr.lambdaCaptures){
            if(cap.isGlobalConstant)
                continue;
            if(KeywordSemantics::IsThisKeyword(cap.name)){
                ScopedVarInfo thisCap;
                thisCap.name = KeywordSemantics::s_This;
                thisCap.isConst = (cap.mode == ParamPassingMode::In) || (method && method->isConst);
                lambdaScope.push_back(thisCap);
                continue;
            }
            if(const ScopedVarInfo* outer = TypeChecker::FindScopedVar(scope, cap.name)){
                ScopedVarInfo capInfo = *outer;
                capInfo.mode = cap.mode;
                capInfo.isConst = (cap.mode == ParamPassingMode::In);
                if(cap.mode != ParamPassingMode::Mut && capInfo.type && m_symbols.resolveRefMode(capInfo.type) == ParamPassingMode::None){
                    const TypeRef* unwrappedCap = m_symbols.unwrapTypeAlias(capInfo.type);
                    if(unwrappedCap && m_layoutAnalyzer.requiresMotherArena(*unwrappedCap))
                        unwinds = true;
                }
                lambdaScope.push_back(capInfo);
            }
        }
        for(const ParamDecl& p : expr.lambdaParams){
            ScopedVarInfo pInfo;
            pInfo.name = p.name;
            pInfo.type = p.type;
            pInfo.mode = p.mode;
            pInfo.isConst = (p.mode == ParamPassingMode::In);
            pInfo.isParam = true;
            pInfo.retainsBorrow = p.retainsBorrow;
            lambdaScope.push_back(pInfo);
        }
        const b8 lambdaBodyUnwinds = analyzeStatementList(ownerClass, method, expr.lambdaBody, lambdaScope, outLambdaChanged);
        while(scope.size() > savedScopeSize)
            scope.pop_back();
        if(lambdaBodyUnwinds && !expr.lambdaAllowsUnwind){
            expr.lambdaAllowsUnwind = true;
            outLambdaChanged = true;
        }
        break;
    }
    case ExprKind::Call:
        unwinds |= analyzeCallExpr(ownerClass, method, expr, scope, outLambdaChanged);
        break;
    }
    return unwinds;
}

b8 ExceptionSpecAnalyzer::analyzeCallExpr(const ClassDecl* ownerClass, const MethodDecl* method, Expr& expr, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const{
    b8 unwinds = false;
    if(expr.lhs && expr.lhs->kind != ExprKind::Identifier && expr.lhs->kind != ExprKind::ThisMember && expr.lhs->kind != ExprKind::ScopeAccess){
        if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs)
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs->lhs, scope, outLambdaChanged);
        else
            unwinds |= analyzeExpr(ownerClass, method, *expr.lhs, scope, outLambdaChanged);
    }
    for(Expr* arg : expr.arguments){
        if(arg && analyzeExpr(ownerClass, method, *arg, scope, outLambdaChanged))
            unwinds = true;
    }
    if(!expr.lhs)
        return unwinds;

    const usize argCount = expr.arguments.size();
    MVector<MStringView> argTypes(m_arena);
    MVector<u8> argIsMut(m_arena);
    m_checker.buildCallArgHints(ownerClass, method, expr.arguments, scope, argTypes, argIsMut);

    if(expr.lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr.lhs->text;
        DiagnosticKind::Enum diagKind = DiagnosticKind::Assert;
        if(KeywordSemantics::TryParseDiagnosticKind(callee, diagKind))
            return unwinds;

        if(const ScopedVarInfo* localVar = TypeChecker::FindScopedVar(scope, callee)){
            if(localVar->lambdaExpr)
                return unwinds || localVar->lambdaExpr->lambdaAllowsUnwind;
            return true;
        }
        if(TypeChecker::FindTemplateParam(ownerClass, method, callee) != nullptr)
            return true;

        if(const MethodDecl* fn = m_symbols.findFreeFunction(callee, &argCount, argTypes, argIsMut))
            return unwinds || fn->allowsCppUnwind;

        const MStringView baseCallee = m_symbols.resolveBaseTypeName(callee);
        if(const ClassDecl* ctorCls = m_symbols.findClass(baseCallee))
            return unwinds || classCallCtorUnwinds(*ctorCls, expr.arguments, ownerClass, method, scope);

        if(!m_symbols.hasTypeSymbol(baseCallee)){
            if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(callee))
                return unwinds || fnInfo->allowsCppUnwind;

            if(const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(baseCallee)){
                if(!__hidden_adhd_exception_spec_analyzer::IsProvenNothrowBuiltinConstructibleType(baseCallee, m_symbols))
                    return true;
                if(genericInfo->isArenaBacked && argCount > 0u)
                    return true;
                TypeRef constructedRef(m_arena);
                constructedRef.name = callee;
                constructedRef.templateArgs = expr.callTemplateArgs;
                if(m_layoutAnalyzer.requiresMotherArena(constructedRef)){
                    for(const Expr* arg : expr.arguments){
                        if(arg && !m_checker.isRvalueTemporaryExpr(ownerClass, method, *arg, scope))
                            return true;
                    }
                }
                return unwinds;
            }

            if(m_checker.isConstructibleTypeName(callee, baseCallee)
                && __hidden_adhd_exception_spec_analyzer::IsProvenNothrowBuiltinConstructibleType(baseCallee, m_symbols)){
                if(argCount > 0u){
                    if(SymbolTable::IsArenaBackedContainer(baseCallee) || IsArenaFirstCtorType(baseCallee))
                        return true;
                }
                return unwinds;
            }
        }
        return true;
    }

    if(expr.lhs->kind == ExprKind::Lambda)
        return unwinds || expr.lhs->lambdaAllowsUnwind;

    if(expr.lhs->kind == ExprKind::ThisMember && ownerClass){
        const b8 isConstThis = method ? m_checker.isThisConstInScope(*method, scope) : false;
        if(const MethodDecl* m = m_symbols.findClassMethod(*ownerClass, expr.lhs->text, &argCount, isConstThis, false, argTypes, argIsMut))
            return unwinds || m->allowsCppUnwind;
        if(SymbolTable::FindClassField(*ownerClass, expr.lhs->text) != nullptr)
            return true;
        if(const BuiltinReceiverMethodInfo* inheritedRecv = m_symbols.findInheritedBuiltinReceiverMethod(ownerClass, nullptr, expr.lhs->text)){
            if(const BuiltinReceiverSignatureInfo* sig = FindBuiltinReceiverSignature(inheritedRecv->receiverTypeName, expr.lhs->text))
                return unwinds || sig->allowsCppUnwind;
            return unwinds || inheritedRecv->allowsCppUnwind;
        }
        return true;
    }

    if(
        expr.lhs->kind == ExprKind::ScopeAccess
        && !expr.lhs->lhs
        && expr.lhs->text.empty()
        && expr.lhs->scopeChain.size() == 2u
        && m_checker.isUnshadowedTypeScopeAccess(scope, expr.lhs->scopeChain[0])
    ){
        const MStringView scopeName = m_symbols.resolveBaseTypeName(expr.lhs->scopeChain[0]);
        const MStringView memberName = expr.lhs->scopeChain[1];
        if(const ClassDecl* scopedCls = m_symbols.findClass(scopeName)){
            const b8 requirePublic = !m_symbols.canAccessPrivateMembers(*scopedCls, ownerClass);
            if(const MethodDecl* m = m_symbols.findClassMethod(*scopedCls, memberName, &argCount, false, requirePublic, argTypes, argIsMut, true))
                return unwinds || m->allowsCppUnwind;
            if(SymbolTable::FindClassField(*scopedCls, memberName) != nullptr)
                return true;
            if(scopedCls->isExtern)
                return true;
        }
        if(!m_symbols.hasTypeSymbol(expr.lhs->scopeChain[0])){
            if(const BuiltinFunctionInfo* fnInfo = FindBuiltinScopedFunction(expr.lhs->scopeChain[0], memberName))
                return unwinds || fnInfo->allowsCppUnwind;
        }
        return true;
    }

    if(expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
        const TypeRef* recvTypeRef = nullptr;
        const MStringView recvBase = m_checker.inferExprBaseType(ownerClass, method, *expr.lhs->lhs, scope, &recvTypeRef);
        const MStringView mName = expr.lhs->text;
        const b8 recvIsConst = method ? m_checker.isReceiverConst(ownerClass, *method, *expr.lhs->lhs, scope) : false;

        if(TypeChecker::FindTemplateParam(ownerClass, method, recvBase) != nullptr)
            return true;

        if(const ClassDecl* cls = m_symbols.findClass(recvBase)){
            const b8 requirePublic = !m_symbols.canAccessPrivateMembers(*cls, ownerClass);
            if(const MethodDecl* m = m_symbols.findClassMethod(*cls, mName, &argCount, recvIsConst, requirePublic, argTypes, argIsMut))
                return unwinds || m->allowsCppUnwind;
            if(SymbolTable::FindClassField(*cls, mName) != nullptr)
                return true;
            if(const BuiltinReceiverMethodInfo* inheritedRecv = m_symbols.findInheritedBuiltinReceiverMethod(cls, nullptr, mName)){
                if(const BuiltinReceiverSignatureInfo* sig = FindBuiltinReceiverSignature(inheritedRecv->receiverTypeName, mName))
                    return unwinds || sig->allowsCppUnwind;
                return unwinds || inheritedRecv->allowsCppUnwind;
            }
            return true;
        }

        if(recvTypeRef){
            if(const InterfaceDecl* ifaceDecl = m_symbols.findInterface(recvBase)){
                if(!ifaceDecl->isExtern)
                    return true;
                if(const MethodDecl* m = m_symbols.findInterfaceMethod(*recvTypeRef, mName, &argCount, 0u, argTypes, argIsMut))
                    return unwinds || m->allowsCppUnwind;
                return true;
            }
        }

        if(!m_symbols.hasTypeSymbol(recvBase)){
            const SpecialMethodKind::Enum specialMethod = FindSpecialMethodSemantics(mName);
            if(
                specialMethod == SpecialMethodKind::CheckedElementAccess
                || specialMethod == SpecialMethodKind::EcsEmplaceSystem
                || specialMethod == SpecialMethodKind::EcsEmplaceComponent
                || specialMethod == SpecialMethodKind::AssetLoadBinary
            )
                return true;
            if(const BuiltinReceiverSignatureInfo* sig = FindBuiltinReceiverSignature(recvBase, mName))
                return unwinds || sig->allowsCppUnwind;
            if(const BuiltinReceiverMethodInfo* recvMethod = FindBuiltinReceiverMethod(recvBase, mName))
                return unwinds || recvMethod->allowsCppUnwind;
            if(const BuiltinMemberMethodInfo* memberMethod = FindBuiltinMemberMethod(mName))
                return unwinds || memberMethod->allowsCppUnwind;
        }
        return true;
    }

    return true;
}

void ExceptionSpecAnalyzer::analyze(TranslationUnit& unit)const{
    for(InterfaceDecl* iface : unit.interfaces){
        if(!iface || iface->isExtern)
            continue;
        for(MethodDecl* m : iface->methods){
            if(!m || m->isExtern)
                continue;
            m->isOpenVirtualContract = true;
            m->allowsCppUnwind = true;
        }
    }

    for(ClassDecl* cls : unit.classes){
        if(!cls)
            continue;
        if(cls->isExtern)
            continue;

        const b8 hasRole = __hidden_adhd_exception_spec_analyzer::HasRoleContract(*cls);
        b8 hasNonTrivialInterfaceBase = false;
        for(const TypeRef* base : cls->implementedInterfaces){
            if(base && !IsNonCopyableBaseInterfaceName(m_symbols.resolveBaseTypeName(base->name))){
                hasNonTrivialInterfaceBase = true;
                break;
            }
        }
        for(FieldDecl* f : cls->fields){
            if(!f)
                continue;
            __hidden_adhd_exception_spec_analyzer::ResetExprLambdas(f->defaultExpr);
            for(Expr* arg : f->ctorArgs)
                __hidden_adhd_exception_spec_analyzer::ResetExprLambdas(arg);
        }
        for(MethodDecl* m : cls->methods){
            if(!m || m->isExtern)
                continue;
            m->forceNothrowContract = false;
            m->isOpenVirtualContract = false;
            if(m->isDestructor){
                if(m_symbols.hasPolymorphicBase(*cls) || hasNonTrivialInterfaceBase || cls->isExtensible || cls->concreteBase != nullptr || hasRole)
                    m->forceNothrowContract = true;
            }
            else if(m->isConstructor){
                if(hasRole)
                    m->forceNothrowContract = true;
            }
            else if(m->isOverride){
                if(__hidden_adhd_exception_spec_analyzer::IsNothrowOverrideContract(*cls, *m, m_symbols, m_arena))
                    m->forceNothrowContract = true;
                else if(cls->isExtensible)
                    m->isOpenVirtualContract = true;
            }

            if(m->forceNothrowContract)
                m->allowsCppUnwind = false;
            else if(m->isOpenVirtualContract)
                m->allowsCppUnwind = true;
            else if(!m->hasBody || m->declaredBodylessInImport)
                m->allowsCppUnwind = true;
            else
                m->allowsCppUnwind = false;

            __hidden_adhd_exception_spec_analyzer::ResetStmtLambdas(m->body);
        }
    }

    for(MethodDecl* fn : unit.freeFunctions){
        if(!fn)
            continue;
        if(fn->isExtern)
            continue;
        if(!fn->exceptionProbes.empty()){
            __hidden_adhd_exception_spec_analyzer::ResetStmtLambdas(fn->body);
            continue;
        }
        fn->allowsCppUnwind = !fn->hasBody || fn->declaredBodylessInImport;
        __hidden_adhd_exception_spec_analyzer::ResetStmtLambdas(fn->body);
    }

    const usize maxIterations = 64u;
    for(usize iter = 0u; iter < maxIterations; ++iter){
        b8 changed = false;
        for(ClassDecl* cls : unit.classes){
            if(!cls || cls->isExtern)
                continue;
            runFieldDefaultsPass(*cls, changed);
            for(MethodDecl* m : cls->methods){
                if(!m || m->isExtern || !m->hasBody)
                    continue;
                if(runMethodPass(cls, *m, changed))
                    changed = true;
            }
        }
        for(MethodDecl* fn : unit.freeFunctions){
            if(!fn || fn->isExtern || !fn->hasBody)
                continue;
            if(runMethodPass(nullptr, *fn, changed))
                changed = true;
        }
        if(!changed)
            break;
    }

    for(ClassDecl* cls : unit.classes){
        if(!cls || cls->isExtern)
            continue;
        for(MethodDecl* m : cls->methods){
            if(m && m->isDestructor && !m->forceNothrowContract && __hidden_adhd_exception_spec_analyzer::IsNothrowOverrideContract(*cls, *m, m_symbols, m_arena))
                m->allowsCppUnwind = false;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

