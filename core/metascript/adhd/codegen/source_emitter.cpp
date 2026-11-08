// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_statement_emitter.h"
#include "simd_expression_analysis.h"
#include "source_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsComparisonOp(const TokenType::Enum op)noexcept{
    return op == TokenType::EqualEqual
        || op == TokenType::BangEqual
        || op == TokenType::Less
        || op == TokenType::LessEqual
        || op == TokenType::Greater
        || op == TokenType::GreaterEqual;
}

[[nodiscard]] constexpr b8 IsNonFloatLiteralExpr(const Expr* const expr)noexcept{
    return expr && (expr->kind == ExprKind::IntegerLiteral || expr->kind == ExprKind::BoolLiteral || expr->kind == ExprKind::StringLiteral);
}

[[nodiscard]] b8 StatementsNeedSimdFloatingPointEnv(const Span<const Stmt* const> stmts, const SymbolTable& symbols, b8 hasSimdScalarLocal = false)noexcept;

[[nodiscard]] b8 ExprNeedsSimdFloatingPointEnv(const Expr* const expr, const SymbolTable& symbols, const b8 hasSimdScalarLocal)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Binary && IsComparisonOp(expr->op)){
        if((hasSimdScalarLocal && !IsNonFloatLiteralExpr(expr->lhs) && !IsNonFloatLiteralExpr(expr->rhs))
            || SimdExpressionAnalysis::ExprContainsSimdMath(expr->lhs, symbols)
            || SimdExpressionAnalysis::ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }
    if(expr->kind == ExprKind::Call && expr->isBuiltinCall && expr->lhs && expr->lhs->kind == ExprKind::Identifier && expr->lhs->text == "isFinite"){
        if(hasSimdScalarLocal)
            return true;
        for(const Expr* arg : expr->arguments){
            if(SimdExpressionAnalysis::ExprContainsSimdMath(arg, symbols))
                return true;
        }
    }
    if(expr->kind == ExprKind::Lambda && StatementsNeedSimdFloatingPointEnv(expr->lambdaBody, symbols, hasSimdScalarLocal))
        return true;
    if(ExprNeedsSimdFloatingPointEnv(expr->lhs, symbols, hasSimdScalarLocal)
        || ExprNeedsSimdFloatingPointEnv(expr->rhs, symbols, hasSimdScalarLocal))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprNeedsSimdFloatingPointEnv(arg, symbols, hasSimdScalarLocal))
            return true;
    }
    return false;
}

[[nodiscard]] b8 StatementsNeedSimdFloatingPointEnv(const Span<const Stmt* const> stmts, const SymbolTable& symbols, b8 hasSimdScalarLocal)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(stmt->kind == StmtKind::VarDecl && SimdExpressionAnalysis::ResolveLocalVarDeclSimdType(*stmt, symbols).empty()){
            if(stmt->expr && SimdExpressionAnalysis::ExprContainsSimdMath(stmt->expr, symbols))
                hasSimdScalarLocal = true;
            for(const Expr* arg : stmt->arguments){
                if(SimdExpressionAnalysis::ExprContainsSimdMath(arg, symbols))
                    hasSimdScalarLocal = true;
            }
        }
        if(stmt->kind == StmtKind::ExprStmt && stmt->expr && stmt->expr->kind == ExprKind::Binary && stmt->expr->rhs && SimdExpressionAnalysis::ExprContainsSimdMath(stmt->expr->rhs, symbols))
            hasSimdScalarLocal = true;
        if(ExprNeedsSimdFloatingPointEnv(stmt->expr, symbols, hasSimdScalarLocal)
            || ExprNeedsSimdFloatingPointEnv(stmt->condition, symbols, hasSimdScalarLocal)
            || ExprNeedsSimdFloatingPointEnv(stmt->stepExpr, symbols, hasSimdScalarLocal))
            return true;
        if(stmt->initStmt){
            const Stmt* const initPtr = stmt->initStmt;
            if(StatementsNeedSimdFloatingPointEnv(Span<const Stmt* const>(&initPtr, 1u), symbols, hasSimdScalarLocal))
                return true;
        }
        for(const Expr* arg : stmt->arguments){
            if(ExprNeedsSimdFloatingPointEnv(arg, symbols, hasSimdScalarLocal))
                return true;
        }
        if(StatementsNeedSimdFloatingPointEnv(stmt->statements, symbols, hasSimdScalarLocal)
            || StatementsNeedSimdFloatingPointEnv(stmt->elseStatements, symbols, hasSimdScalarLocal))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(ExprNeedsSimdFloatingPointEnv(sc.matchExpr, symbols, hasSimdScalarLocal)
                || StatementsNeedSimdFloatingPointEnv(sc.statements, symbols, hasSimdScalarLocal))
                return true;
        }
        for(const NativeBinding& nb : stmt->nativeBindings){
            if(ExprNeedsSimdFloatingPointEnv(nb.sourceExpr, symbols, hasSimdScalarLocal))
                return true;
        }
    }
    return false;
}

[[nodiscard]] const Expr* FindConstructorFieldAssignment(const Span<const Stmt* const> stmts, const MStringView fieldName, const Expr*& outUniqueTopLevelAssignExpr)noexcept{
    const Expr* firstFound = nullptr;
    usize totalMatches = 0u;
    b8 firstIsTopLevel = false;

    const auto countRecursive = [&](const auto& self, const Span<const Stmt* const> list, const b8 topLevel)noexcept -> void{
        for(const Stmt* stmt : list){
            if(!stmt)
                continue;
            if(IsThisMemberSimpleAssignStmt(*stmt, fieldName) && stmt->expr->rhs){
                if(!firstFound){
                    firstFound = stmt->expr;
                    firstIsTopLevel = topLevel;
                }
                ++totalMatches;
            }
            if(stmt->kind == StmtKind::Block || stmt->kind == StmtKind::If){
                self(self, stmt->statements, false);
                self(self, stmt->elseStatements, false);
            }
            else if(stmt->kind == StmtKind::Switch){
                for(const SwitchCase& sc : stmt->switchCases)
                    self(self, sc.statements, false);
            }
        }
    };

    countRecursive(countRecursive, stmts, true);
    outUniqueTopLevelAssignExpr = (firstFound != nullptr && firstIsTopLevel && totalMatches == 1u) ? firstFound : nullptr;
    return firstFound ? firstFound->rhs : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 HasOnlyGlobalConstants(const TranslationUnit& unit)noexcept{
    return !unit.globalConstants.empty()
        && unit.topLevelNativeBlocks.empty()
        && unit.includedAdhdFiles.empty()
        && unit.resolvedIncludedFiles.empty()
        && unit.typeAliases.empty()
        && unit.enums.empty()
        && unit.interfaces.empty()
        && unit.classes.empty()
        && unit.templateInstantiations.empty()
        && unit.freeFunctions.empty()
        && unit.outOfLineMethods.empty();
}

b8 IsNameLiteralOnlyUnit(const TranslationUnit& unit)noexcept{
    if(!HasOnlyGlobalConstants(unit))
        return false;
    for(const Stmt* constant : unit.globalConstants){
        if(!constant || constant->isImported || !constant->varType || constant->varType->name != "Name" || !constant->varType->templateArgs.empty() || constant->isCtorInit || !constant->expr || constant->expr->kind != ExprKind::StringLiteral)
            return false;
    }
    return true;
}

[[nodiscard]] static b8 IsSimpleScalarExpression(const Expr& value, const TranslationUnit& unit, const usize precedingCount, const b8 allowNegative, const b8 allowFloating)noexcept{
    if(value.kind == ExprKind::IntegerLiteral)
        return true;
    if(value.kind == ExprKind::FloatLiteral || value.kind == ExprKind::DoubleLiteral)
        return allowFloating;
    if(value.kind == ExprKind::Identifier && !value.isBuiltinConstant){
        for(usize index = 0u; index < precedingCount; ++index){
            const Stmt* previous = unit.globalConstants[index];
            if(previous && previous->name == value.text && previous->varType)
                return previous->varType->name != "StringView" && previous->varType->name != "b8";
        }
        return false;
    }
    if(value.kind == ExprKind::Unary)
        return allowNegative && !value.isPostfix && value.text == "-" && value.lhs && IsSimpleScalarExpression(*value.lhs, unit, precedingCount, allowNegative, allowFloating);
    if(value.kind != ExprKind::Binary || value.isNotNullRebind || !value.lhs || !value.rhs || (value.text != "+" && value.text != "-" && value.text != "*"))
        return false;
    return IsSimpleScalarExpression(*value.lhs, unit, precedingCount, allowNegative, allowFloating)
        && IsSimpleScalarExpression(*value.rhs, unit, precedingCount, allowNegative, allowFloating);
}

b8 IsScalarLiteralOnlyUnit(const TranslationUnit& unit)noexcept{
    if(!HasOnlyGlobalConstants(unit))
        return false;
    for(usize index = 0u; index < unit.globalConstants.size(); ++index){
        const Stmt* constant = unit.globalConstants[index];
        if(!constant || constant->isImported || !constant->varType || constant->varType->isFuncType || constant->varType->isAuto
            || constant->varType->refMode != ParamPassingMode::None || !constant->varType->templateArgs.empty() || constant->isCtorInit || !constant->expr)
            return false;
        const MStringView typeName = constant->varType->name;
        const Expr* value = constant->expr;
        if(typeName == "StringView"){
            if(value->kind != ExprKind::StringLiteral || value->adaptToTChar)
                return false;
            continue;
        }
        if(typeName == "b8"){
            if(value->kind != ExprKind::BoolLiteral)
                return false;
            continue;
        }
        const b8 isSignedInteger = typeName == "i8" || typeName == "i16" || typeName == "i32" || typeName == "i64" || typeName == "isize";
        const b8 isUnsignedInteger = typeName == "u8" || typeName == "u16" || typeName == "u32" || typeName == "u64" || typeName == "usize";
        const b8 isFloating = typeName == "f32" || typeName == "f64";
        if(!isSignedInteger && !isUnsignedInteger && !isFloating)
            return false;
        if(!IsSimpleScalarExpression(*value, unit, index, isSignedInteger || isFloating, isFloating))
            return false;
    }
    return true;
}


MString SourceEmitter::emitSource(const TranslationUnit& unit, const MStringView headerIncludePath, const MStringView namespacePath)const{
    MString out(m_arena);
    out.reserve(s_AdhdEmitReserveBytes);

    ReflectionEmitter reflectionEmitter(m_symbols);

    out.append(s_AdhdGeneratedFileBanner.data(), s_AdhdGeneratedFileBanner.size());
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append("#include \"");
    out.append(headerIncludePath.data(), headerIncludePath.size());
    out.append("\"\r\n");
    if(!IsNameLiteralOnlyUnit(unit) && !IsScalarLiteralOnlyUnit(unit)){
        out.append("\r\n#include <global/assert.h>\r\n");
        out.append("#include <core/common/log.h>\r\n");
        out.append("#include <core/task/cpu/scheduler.h>\r\n");
    }
    out.append("\r\n\r\n");
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl)
            continue;

        if(classDecl->templateParams.empty()){
            for(const MethodDecl* method : classDecl->methods){
                if(!method)
                    continue;
                const b8 emittedInHeader = method->isInline || !method->templateParams.empty();
                if(method->isImported || !method->hasBody || emittedInHeader)
                    continue;

                emitOutOfClassCallable(out, classDecl, *method, true, false);
            }
        }

        if(!classDecl->isImported && classDecl->templateParams.empty() && (classDecl->isSerialize || classDecl->isReflect || !classDecl->assetTypeName.empty()))
            reflectionEmitter.emitClassReflectionDefinitions(out, *classDecl);
    }

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported || !fn->hasBody || fn->isInline || !fn->templateParams.empty())
            continue;

        emitOutOfClassCallable(out, nullptr, *fn, true, false);
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}

void SourceEmitter::emitOutOfClassCallable(MString& out, const ClassDecl* ownerClass, const MethodDecl& callable, const b8 emitBody, const b8 emitInlineKeyword)const{
    if(ownerClass && !ownerClass->templateParams.empty())
        m_typeEmitter.emitTemplateHeaderCpp(out, ownerClass->templateParams);
    m_typeEmitter.emitTemplateHeaderCpp(out, callable.templateParams);
    if(emitBody && emitInlineKeyword)
        out.append("inline ");

    m_typeEmitter.emitMethodSignature(out, ownerClass, callable, false);
    if(emitBody){
        if(ownerClass && callable.isConstructor)
            emitConstructorNotNullInitializers(out, ownerClass->orderedFields, callable);
        out.append("{\r\n");
        emitMethodBodyBlock(out, callable, 1u);
        out.append("}\r\n\r\n");
    }
    else{
        out.append(";\r\n\r\n");
    }
}

void SourceEmitter::emitMethodBodyBlock(MString& out, const MethodDecl& method, const u32 indent)const{
    if(statementsNeedSimdFloatingPointEnv(method.body))
        out.append("#pragma STDC FENV_ACCESS ON\r\n");
    if(method.usesScratchArena)
        emitScratchArenaPrologue(out, indent, method.name);
    if(method.usesTaskScope)
        ParallelStatementEmitter(*this).emitTaskScopePrologue(out, indent);

    const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
    m_activeSwitchBreakLabelLine = 0u;
    m_activeLocalVarDecls.clear();
    m_remainingBlockStatements = {};
    emitStatementList(out, method.body, indent);
    m_activeLocalVarDecls.clear();
    m_remainingBlockStatements = {};
    m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;
    m_skippedCtorAssignExprs = nullptr;
}

b8 SourceEmitter::statementsNeedSimdFloatingPointEnv(const Span<const Stmt* const> statements)const noexcept{
    return __hidden_adhd_source_emitter::StatementsNeedSimdFloatingPointEnv(statements, m_symbols);
}

void SourceEmitter::emitConstructorNotNullInitializers(MString& out, const Span<const FieldDecl* const> orderedFields, const MethodDecl& ctor)const{
    m_skippedCtorAssignExprs = nullptr;
    if(!ctor.isConstructor || !ctor.hasBody)
        return;

    MVector<const Expr*>* skipped = AllocateAstNode<MVector<const Expr*>>(m_arena);
    b8 firstInit = true;
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type || m_symbols.resolveRefMode(field->type) == ParamPassingMode::None || field->defaultExpr || field->hasCtorInit)
            continue;
        const Expr* uniqueAssign = nullptr;
        const Expr* rhs = __hidden_adhd_source_emitter::FindConstructorFieldAssignment(ctor.body, field->name, uniqueAssign);
        if(!rhs)
            continue;
        if(uniqueAssign)
            skipped->push_back(uniqueAssign);
        out.append(firstInit ? " : " : ", ");
        firstInit = false;
        out.append(field->name.data(), field->name.size());
        out.append("(::MakeNotNull(&(");
        emitExpr(out, *rhs);
        out.append(")))");
    }
    if(!skipped->empty())
        m_skippedCtorAssignExprs = skipped;
}




void SourceEmitter::emitIndent(MString& out, const u32 indent)const{
    for(u32 i = 0u; i < indent; ++i)
        out.append("    ");
}

void SourceEmitter::emitScratchArenaPrologue(MString& out, const u32 indent, const MStringView arenaName)const{
    emitIndent(out, indent);
    out.append("static ::NWB::Core::Alloc::GlobalArena __adhd_scratch(::Name(\"");
    out.append(arenaName.data(), arenaName.size());
    out.append("\"));\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

