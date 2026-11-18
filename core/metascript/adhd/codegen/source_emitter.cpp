// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"
#include "constructor_argument_emitter.h"
#include "asset_cook_role_emitter.h"
#include "parallel_statement_emitter.h"
#include "simd_expression_analysis.h"

#include "reflection_emitter.h"
#include <core/metascript/adhd/ast/builtin_type_semantics.h>
#include <core/metascript/adhd/ast/operator_semantics.h>
#include <core/metascript/adhd/sema/asset_type_analyzer.h>
#include <core/metascript/adhd/sema/type_layout_analyzer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_source_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsNonFloatLiteralExpr(const Expr* const expr)noexcept{
    return expr && (expr->kind == ExprKind::IntegerLiteral || expr->kind == ExprKind::BoolLiteral || expr->kind == ExprKind::StringLiteral);
}

[[nodiscard]] b8 StatementsNeedSimdFloatingPointEnv(const Span<const Stmt* const> stmts, const SymbolTable& symbols, b8 hasSimdScalarLocal = false)noexcept;

[[nodiscard]] b8 ExprNeedsSimdFloatingPointEnv(const Expr* const expr, const SymbolTable& symbols, const b8 hasSimdScalarLocal)noexcept{
    if(!expr)
        return false;
    if(!expr->resolvedSimdType.empty() && !IsIntSimdType(expr->resolvedSimdType))
        return true;
    if(BuiltinTypeSemantics::IsFloatScalarType(expr->resolvedScalarType) && (expr->kind == ExprKind::Binary || expr->kind == ExprKind::Unary || expr->kind == ExprKind::Call))
        return true;
    if(const BuiltinUnalignedVectorInfo* info = FindBuiltinUnalignedVectorType(expr->resolvedUnalignedType); info && (info->isHalfFloat || BuiltinTypeSemantics::IsFloatScalarType(info->laneScalarType)))
        return true;
    if(expr->kind == ExprKind::Binary && OperatorSemantics::IsRelationalOrEqualityOperatorToken(expr->op)){
        if((hasSimdScalarLocal && !IsNonFloatLiteralExpr(expr->lhs) && !IsNonFloatLiteralExpr(expr->rhs))
            || SimdExpressionAnalysis::ExprContainsSimdMath(expr->lhs, symbols)
            || SimdExpressionAnalysis::ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }
    if(expr->kind == ExprKind::Call && expr->isBuiltinCall && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(expr->lhs->text); fnInfo && fnInfo->rejectsSimdStorage){
            if(hasSimdScalarLocal)
                return true;
            for(const Expr* arg : expr->arguments){
                if(SimdExpressionAnalysis::ExprContainsSimdMath(arg, symbols))
                    return true;
            }
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

[[nodiscard]] b8 StmtsReferenceIdentifier(const Span<const Stmt* const> stmts, const MStringView name)noexcept;

[[nodiscard]] b8 ExprReferencesIdentifier(const Expr* const expr, const MStringView name)noexcept{
    if(!expr)
        return false;
    if(expr->kind == ExprKind::Identifier && expr->text == name)
        return true;
    if(expr->kind == ExprKind::Lambda && StmtsReferenceIdentifier(expr->lambdaBody, name))
        return true;
    if(ExprReferencesIdentifier(expr->lhs, name) || ExprReferencesIdentifier(expr->rhs, name))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprReferencesIdentifier(arg, name))
            return true;
    }
    for(const LambdaCapture& cap : expr->lambdaCaptures){
        if(cap.name == name)
            return true;
    }
    return false;
}

[[nodiscard]] b8 StmtsReferenceIdentifier(const Span<const Stmt* const> stmts, const MStringView name)noexcept{
    for(const Stmt* stmt : stmts){
        if(!stmt)
            continue;
        if(ExprReferencesIdentifier(stmt->expr, name)
            || ExprReferencesIdentifier(stmt->condition, name)
            || ExprReferencesIdentifier(stmt->stepExpr, name))
            return true;
        if(stmt->initStmt){
            const Stmt* const initPtr = stmt->initStmt;
            if(StmtsReferenceIdentifier(Span<const Stmt* const>(&initPtr, 1u), name))
                return true;
        }
        for(const Expr* arg : stmt->arguments){
            if(ExprReferencesIdentifier(arg, name))
                return true;
        }
        if(StmtsReferenceIdentifier(stmt->statements, name) || StmtsReferenceIdentifier(stmt->elseStatements, name))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(ExprReferencesIdentifier(sc.matchExpr, name) || StmtsReferenceIdentifier(sc.statements, name))
                return true;
        }
        for(const NativeBinding& nb : stmt->nativeBindings){
            if(ExprReferencesIdentifier(nb.sourceExpr, name))
                return true;
        }
        if(stmt->kind == StmtKind::NativeBlock && !name.empty()){
            usize pos = 0u;
            while((pos = stmt->nativeCode.find(name, pos)) != MStringView::npos){
                const b8 leftOk = pos == 0u || !IsAsciiIdentifierChar(stmt->nativeCode[pos - 1u]);
                const usize endPos = pos + name.size();
                const b8 rightOk = endPos >= stmt->nativeCode.size() || !IsAsciiIdentifierChar(stmt->nativeCode[endPos]);
                if(leftOk && rightOk)
                    return true;
                pos = endPos;
            }
        }
    }
    return false;
}

template<typename IsReadyField>
[[nodiscard]] b8 ExprDisallowsMemberInitializerLift(const Expr* const expr, const IsReadyField& isReadyField)noexcept{
    if(!expr)
        return false;
    if((expr->kind == ExprKind::ThisMember && !expr->text.empty() && !isReadyField(expr->text)) || expr->kind == ExprKind::Lambda || expr->injectScratchArena)
        return true;
    if(ExprDisallowsMemberInitializerLift(expr->lhs, isReadyField) || ExprDisallowsMemberInitializerLift(expr->rhs, isReadyField))
        return true;
    for(const Expr* arg : expr->arguments){
        if(ExprDisallowsMemberInitializerLift(arg, isReadyField))
            return true;
    }
    return false;
}

[[nodiscard]] const Expr* FindConstructorFieldAssignment(const Span<const Stmt* const> stmts, const MStringView fieldName, const Expr*& outUniqueTopLevelAssignExpr, b8& outInLeadingAssignPrefix)noexcept{
    const Expr* firstFound = nullptr;
    usize totalMatches = 0u;
    b8 firstIsTopLevel = false;
    b8 firstInLeadingPrefix = false;
    b8 stillInLeadingPrefix = true;

    const auto countRecursive = [&](const auto& self, const Span<const Stmt* const> list, const b8 topLevel)noexcept -> void{
        for(const Stmt* stmt : list){
            if(!stmt)
                continue;
            const b8 isThisMemberAssign = IsThisMemberSimpleAssignStmt(*stmt);
            if(IsThisMemberSimpleAssignStmt(*stmt, fieldName) && stmt->expr->rhs){
                if(!firstFound){
                    firstFound = stmt->expr;
                    firstIsTopLevel = topLevel;
                    firstInLeadingPrefix = topLevel && stillInLeadingPrefix;
                }
                ++totalMatches;
            }
            if(topLevel && !isThisMemberAssign)
                stillInLeadingPrefix = false;
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
    outInLeadingAssignPrefix = (outUniqueTopLevelAssignExpr != nullptr) && firstInLeadingPrefix;
    return firstFound ? firstFound->rhs : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 HasOnlyGlobalConstants(const TranslationUnit& unit)noexcept{
    return !unit.globalConstants.empty()
        && unit.staticAssertions.empty()
        && unit.assetRegistrations.empty()
        && unit.registrationCalls.empty()
        && unit.topLevelNativeBlocks.empty()
        && unit.sourceNativeBlocks.empty()
        && unit.includedAdhdFiles.empty()
        && unit.resolvedIncludedFiles.empty()
        && unit.importedCppHeaders.empty()
        && unit.typeAliases.empty()
        && unit.enums.empty()
        && unit.interfaces.empty()
        && unit.classes.empty()
        && unit.templateInstantiations.empty()
        && unit.freeFunctions.empty()
        && unit.outOfLineMethods.empty();
}

b8 IsNameLiteralOnlyUnit(const TranslationUnit& unit)noexcept{
    if(!HasOnlyGlobalConstants(unit) || !unit.requiredGlobalHeaders.empty())
        return false;
    for(const Stmt* constant : unit.globalConstants){
        if(!constant || constant->isImported || constant->isExtern || !constant->varType || !IsLiteralConstObjectType(constant->varType->name) || !constant->varType->templateArgs.empty() || constant->isCtorInit || !constant->expr || constant->expr->kind != ExprKind::StringLiteral)
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
                return !IsStringBorrowViewType(previous->varType->name) && !BuiltinTypeSemantics::IsBoolScalarType(previous->varType->name);
        }
        return false;
    }
    if(value.kind == ExprKind::Unary)
        return allowNegative && !value.isPostfix && value.op == TokenType::Minus && value.lhs && IsSimpleScalarExpression(*value.lhs, unit, precedingCount, allowNegative, allowFloating);
    if(value.kind != ExprKind::Binary || value.isNotNullRebind || !value.lhs || !value.rhs || (value.op != TokenType::Plus && value.op != TokenType::Minus && value.op != TokenType::Star))
        return false;
    return IsSimpleScalarExpression(*value.lhs, unit, precedingCount, allowNegative, allowFloating)
        && IsSimpleScalarExpression(*value.rhs, unit, precedingCount, allowNegative, allowFloating);
}

b8 IsScalarLiteralOnlyUnit(const TranslationUnit& unit)noexcept{
    if(!HasOnlyGlobalConstants(unit) || !unit.requiredGlobalHeaders.empty())
        return false;
    for(usize index = 0u; index < unit.globalConstants.size(); ++index){
        const Stmt* constant = unit.globalConstants[index];
        if(!constant || constant->isImported || constant->isExtern || !constant->varType || constant->varType->isFuncType || constant->varType->isAuto
            || constant->varType->refMode != ParamPassingMode::None || !constant->varType->templateArgs.empty() || constant->isCtorInit || !constant->expr)
            return false;
        const MStringView typeName = constant->varType->name;
        const Expr* value = constant->expr;
        if(IsStringBorrowViewType(typeName)){
            if(value->kind != ExprKind::StringLiteral || value->adaptToTChar)
                return false;
            continue;
        }
        if(BuiltinTypeSemantics::IsBoolScalarType(typeName)){
            if(value->kind != ExprKind::BoolLiteral)
                return false;
            continue;
        }
        const BuiltinScalarTypeInfo* scalarInfo = BuiltinTypeSemantics::FindBuiltinScalarType(typeName);
        if(!scalarInfo || (!scalarInfo->isInteger && !scalarInfo->isFloat))
            return false;
        const b8 isSignedInteger = scalarInfo->isInteger && scalarInfo->isSigned;
        const b8 isFloating = scalarInfo->isFloat;
        if(!IsSimpleScalarExpression(*value, unit, index, isSignedInteger || isFloating, isFloating))
            return false;
    }
    return true;
}


MString SourceEmitter::emitSource(const TranslationUnit& unit, const MStringView headerIncludePath, const MStringView namespacePath)const{
    m_typeEmitter.setCurrentNamespace(namespacePath);
    MString out(m_arena);
    out.reserve(s_AdhdEmitReserveBytes);

    ReflectionEmitter reflectionEmitter(m_symbols, m_typeEmitter);
    const AssetTypeAnalyzer assetTypes(m_symbols);
    const AssetCodecContractInfo& codec = ContractSemantics::s_AssetCodec;

    const auto needsAutoCodecRegistrar = [&](const ClassDecl& classDecl)noexcept -> b8{
        if(classDecl.assetTypeName.empty())
            return false;
        if(assetTypes.findImplementedAssetInterface(classDecl) != nullptr)
            return false;
        if(assetTypes.findAssetLoadBinaryMethod(classDecl, true) == nullptr)
            return false;
        if(unit.sourceNativeBlocks.empty())
            return true;
        MString codecName(m_arena);
        codecName.append(classDecl.name.data(), classDecl.name.size());
        const MStringView codecSuffix = ContractSemantics::ResolveAssetCodecSuffix(classDecl.name);
        codecName.append(codecSuffix.data(), codecSuffix.size());
        for(const MStringView& rawBlock : unit.sourceNativeBlocks){
            if(rawBlock.find(codecName) != MStringView::npos)
                return false;
        }
        return true;
    };
    b8 hasAutoCodecRegistrar = false;
    for(const ClassDecl* classDecl : unit.classes){
        if(classDecl && needsAutoCodecRegistrar(*classDecl)){
            hasAutoCodecRegistrar = true;
            break;
        }
    }

    out.append(s_AdhdGeneratedFileBanner.data(), s_AdhdGeneratedFileBanner.size());
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append(CodegenSymbols::s_IncludePrefix.data(), CodegenSymbols::s_IncludePrefix.size());
    out.push_back('"');
    out.append(headerIncludePath.data(), headerIncludePath.size());
    out.append("\"\r\n");
    for(const MStringView header : unit.registrationHeaders){
        out.append("#include ");
        out.append(header.data(), header.size());
        out.append("\r\n");
    }
    if(!IsNameLiteralOnlyUnit(unit) && !IsScalarLiteralOnlyUnit(unit)){
        out.append("\r\n");
        ContractSemantics::ForEachPreambleHeader(PreambleHeaderGroup::SourceDefault, [&](const MStringView hdr){
            out.append(CodegenSymbols::s_IncludePrefix.data(), CodegenSymbols::s_IncludePrefix.size());
            out.append(hdr.data(), hdr.size());
            out.append("\r\n");
        });
        for(const MethodDecl* fn : unit.freeFunctions){
            if(fn && !fn->isImported && (fn->assetCookRole == AssetCookRole::Entry || !fn->pairedSidecar.empty())){
                out.append("#if defined(NWB_COOK)\r\n#include <core/assets/cook_metadata.h>\r\n#endif\r\n");
                break;
            }
        }
        for(const MethodDecl* fn : unit.freeFunctions){
            if(fn && !fn->isImported && fn->assetCookRole == AssetCookRole::GatherMerge){
                out.append("#include <core/assets/gather_merge_registry.h>\r\n");
                break;
            }
        }
        if(hasAutoCodecRegistrar && !codec.registrarHeader.empty()){
            out.append(CodegenSymbols::s_IncludePrefix.data(), CodegenSymbols::s_IncludePrefix.size());
            out.append(codec.registrarHeader.data(), codec.registrarHeader.size());
            out.append("\r\n");
        }
    }
    out.append("\r\n\r\n");
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl || classDecl->isExtern)
            continue;

        if(classDecl->templateParams.empty()){
            for(const MethodDecl* method : classDecl->methods){
                if(!method || method->isExtern || method->isImported || !method->hasBody || ShouldEmitCallableInHeader(classDecl, *method))
                    continue;

                emitOutOfClassCallable(out, classDecl, *method, true, false, false);
            }
        }

        if(!classDecl->isImported && classDecl->templateParams.empty() && classDecl->assetTypeName.empty() && (classDecl->isSerialize || classDecl->isReflect))
            reflectionEmitter.emitClassReflectionDefinitions(out, *classDecl);
        if(needsAutoCodecRegistrar(*classDecl)){
            const MStringView codecSuffix = ContractSemantics::ResolveAssetCodecSuffix(classDecl->name);
            out.append("static ");
            out.append(codec.registrarCppType.data(), codec.registrarCppType.size());
            out.push_back(' ');
            out.append(CodegenSymbols::s_AssetCodecRegPrefix.data(), CodegenSymbols::s_AssetCodecRegPrefix.size());
            out.append(classDecl->name.data(), classDecl->name.size());
            out.append("(&");
            out.append(codec.factoryCppTemplate.data(), codec.factoryCppTemplate.size());
            out.push_back('<');
            out.append(classDecl->name.data(), classDecl->name.size());
            out.append(codecSuffix.data(), codecSuffix.size());
            out.append(">);\r\n\r\n");
        }
    }

    for(const MethodDecl* fn : unit.freeFunctions){
        if(!fn || fn->isImported || fn->isExtern || !fn->hasBody || ShouldEmitCallableInHeader(nullptr, *fn))
            continue;

        emitOutOfClassCallable(out, nullptr, *fn, true, false, false);
    }
    AssetCookRoleEmitter::Emit(out, unit, m_symbols);

    for(const RegistrationCall& registration : unit.registrationCalls){
        out.append(registration.cppName.data(), registration.cppName.size());
        out.push_back('(');
        for(usize i = 0u; i < registration.arguments.size(); ++i){
            if(i)
                out.append(", ");
            const auto argument = registration.arguments[i];
            out.append(argument.data(), argument.size());
        }
        out.append(")\r\n\r\n");
    }

    if(!namespacePath.empty()){
        out.append("};\r\n\r\n\r\n");
    }

    if(!unit.sourceNativeBlocks.empty()){
        if(!namespacePath.empty()){
            out.append("using namespace ");
            out.append(namespacePath.data(), namespacePath.size());
            out.append(";\r\n\r\n");
        }
        for(const MStringView& rawBlock : unit.sourceNativeBlocks){
            AppendTextDiscardCrAndExpandLf(out, rawBlock);
            out.append("\r\n");
        }
        out.append("\r\n");
    }

    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n");
    return out;
}

void SourceEmitter::emitOutOfClassCallable(MString& out, const ClassDecl* ownerClass, const MethodDecl& callable, const b8 emitBody, const b8 emitInlineKeyword, const b8 includeTemplateDefaults)const{
    const ClassDecl* prevOwnerClass = m_activeOwnerClass;
    const b8 prevOwnerNeedsArena = m_activeOwnerNeedsArena;
    m_activeOwnerClass = ownerClass;
    m_activeOwnerNeedsArena = ownerClass ? ownerClass->needsClassArena : false;

    if(ownerClass && !ownerClass->templateParams.empty())
        m_typeEmitter.emitTemplateHeaderCpp(out, ownerClass->templateParams);
    m_typeEmitter.emitCallableTemplateHeaderCpp(out, callable, includeTemplateDefaults);
    if(emitBody && emitInlineKeyword)
        out.append("inline ");

    m_typeEmitter.emitMethodSignature(out, ownerClass, callable, false, m_activeOwnerNeedsArena);
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

    m_activeOwnerClass = prevOwnerClass;
    m_activeOwnerNeedsArena = prevOwnerNeedsArena;
}

b8 SourceEmitter::instantiatedExprNeedsScratchArena(const Expr* const expr)const{
    if(!expr || m_activeTemplateParams.empty())
        return false;
    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier && !expr->isBuiltinCall && !m_symbols.findFreeFunction(expr->lhs->text)){
        TypeRef candidateType(m_arena);
        candidateType.name = expr->lhs->text;
        candidateType.templateArgs = expr->callTemplateArgs;
        if(const TypeRef* subType = m_symbols.substituteTypeParams(candidateType, m_activeTemplateParams, m_activeTemplateArgs)){
            const TypeLayoutAnalyzer layoutAnalyzer(m_symbols);
            const MStringView subBase = m_symbols.resolveBaseTypeName(subType->name);
            if(layoutAnalyzer.requiresMotherArena(*subType) || SymbolTable::IsArenaBackedContainer(subBase) || layoutAnalyzer.isNonRootMotherArenaType(*subType))
                return true;
        }
    }
    if(instantiatedExprNeedsScratchArena(expr->lhs) || instantiatedExprNeedsScratchArena(expr->rhs))
        return true;
    for(const Expr* arg : expr->arguments){
        if(instantiatedExprNeedsScratchArena(arg))
            return true;
    }
    return instantiatedStatementsNeedScratchArena(expr->lambdaBody);
}

b8 SourceEmitter::instantiatedStatementsNeedScratchArena(const Span<const Stmt* const> statements)const{
    if(m_activeTemplateParams.empty())
        return false;
    const TypeLayoutAnalyzer layoutAnalyzer(m_symbols);
    for(const Stmt* stmt : statements){
        if(!stmt)
            continue;
        if(stmt->varType && !stmt->varType->isAuto){
            if(const TypeRef* subVar = m_symbols.substituteTypeParams(*stmt->varType, m_activeTemplateParams, m_activeTemplateArgs)){
                if(layoutAnalyzer.requiresMotherArena(*subVar))
                    return true;
            }
        }
        if(instantiatedExprNeedsScratchArena(stmt->expr)
            || instantiatedExprNeedsScratchArena(stmt->condition)
            || instantiatedExprNeedsScratchArena(stmt->stepExpr))
            return true;
        for(const Expr* arg : stmt->arguments){
            if(instantiatedExprNeedsScratchArena(arg))
                return true;
        }
        if(stmt->initStmt){
            const Stmt* const initPtr = stmt->initStmt;
            if(instantiatedStatementsNeedScratchArena(Span<const Stmt* const>(&initPtr, 1u)))
                return true;
        }
        if(instantiatedStatementsNeedScratchArena(stmt->statements) || instantiatedStatementsNeedScratchArena(stmt->elseStatements))
            return true;
        for(const SwitchCase& sc : stmt->switchCases){
            if(instantiatedExprNeedsScratchArena(sc.matchExpr) || instantiatedStatementsNeedScratchArena(sc.statements))
                return true;
        }
    }
    return false;
}

void SourceEmitter::emitMethodBodyBlock(MString& out, const MethodDecl& method, const u32 indent)const{
    if(statementsNeedSimdFloatingPointEnv(method.body))
        out.append("#pragma STDC FENV_ACCESS ON\r\n");
    emitUnusedParamSuppressions(out, method.params, method.body, indent);
    const b8 needsScratch = method.usesScratchArena || instantiatedStatementsNeedScratchArena(method.body);
    if(method.returnsFromOwnerArena){
        emitIndent(out, indent);
        out.append("::ArenaBase& __adhd_caller_arena = ").append(activeClassArenaExpr()).append(";\r\n");
    }
    const b8 hasResultArena = method.injectsCallerArena || method.returnsFromOwnerArena;
    if(!method.isConstructor && !method.isDestructor && hasResultArena){
        emitIndent(out, indent);
        if(needsScratch)
            out.append("[[maybe_unused]] ::ArenaBase& __adhd_scratch = __adhd_caller_arena;\r\n");
        else
            out.append("(void)__adhd_caller_arena;\r\n");
    }
    else if(needsScratch){
        emitScratchArenaPrologue(out, indent, method.name);
    }
    if(method.usesTaskScope)
        ParallelStatementEmitter(*this).emitTaskScopePrologue(out, indent);

    const b8 prevUnwind = m_allowsCppUnwind;
    m_allowsCppUnwind = method.allowsCppUnwind || !method.exceptionProbes.empty();
    const b8 prevScalarSimd = m_emittingScalarSimd;
    m_emittingScalarSimd = true;
    const TypeRef* prevCallerReturnType = m_activeCallerArenaReturnType;
    const u32 prevParallelWorkerDepth = m_parallelWorkerDepth;
    const u32 prevSwitchBreakLabel = m_activeSwitchBreakLabelLine;
    m_activeCallerArenaReturnType = (!method.isConstructor && !method.isDestructor && hasResultArena) ? method.returnType : nullptr;
    m_parallelWorkerDepth = 0u;
    m_activeSwitchBreakLabelLine = 0u;
    m_activeLocalVarDecls.clear();
    m_remainingBlockStatements = {};
    emitStatementList(out, method.body, indent);
    m_activeLocalVarDecls.clear();
    m_remainingBlockStatements = {};
    m_activeSwitchBreakLabelLine = prevSwitchBreakLabel;
    m_parallelWorkerDepth = prevParallelWorkerDepth;
    m_activeCallerArenaReturnType = prevCallerReturnType;
    m_emittingScalarSimd = prevScalarSimd;
    m_allowsCppUnwind = prevUnwind;
    m_skippedCtorAssignExprs = nullptr;
}

b8 SourceEmitter::statementsNeedSimdFloatingPointEnv(const Span<const Stmt* const> statements)const noexcept{
    return __hidden_adhd_source_emitter::StatementsNeedSimdFloatingPointEnv(statements, m_symbols);
}

void SourceEmitter::emitUnusedParamSuppressions(MString& out, const Span<const ParamDecl> params, const Span<const Stmt* const> statements, const u32 indent)const{
    for(const ParamDecl& param : params){
        if(!param.name.empty() && !__hidden_adhd_source_emitter::StmtsReferenceIdentifier(statements, param.name)){
            emitIndent(out, indent);
            out.append("(void)");
            out.append(param.name.data(), param.name.size());
            out.append(";\r\n");
        }
    }
}

void SourceEmitter::emitConstructorNotNullInitializers(MString& out, const Span<const FieldDecl* const> orderedFields, const MethodDecl& ctor)const{
    m_skippedCtorAssignExprs = nullptr;
    if(!ctor.isConstructor || !ctor.hasBody)
        return;

    MVector<const Expr*>* skipped = AllocateAstNode<MVector<const Expr*>>(m_arena);
    b8 firstInit = true;
    if(m_activeOwnerClass && !m_activeOwnerClass->systemTypeName.empty()){
        if(const ClassRoleContractInfo* sysContract = ContractSemantics::FindClassRoleContract(ClassRoleKind::System)){
            out.append(" : ");
            out.append(sysContract->cppBaseTypeOrTemplate.data(), sysContract->cppBaseTypeOrTemplate.size());
            out.push_back('(');
            out.append(CodegenSymbols::s_MotherArenaVar.data(), CodegenSymbols::s_MotherArenaVar.size());
            out.push_back(')');
            firstInit = false;
        }
    }
    if(m_activeOwnerClass){
        for(const TypeRef* iface : m_activeOwnerClass->implementedInterfaces){
            if(!iface)
                continue;
            if(m_activeOwnerClass->concreteBase && m_symbols.resolveBaseTypeName(iface->name) == m_activeOwnerClass->concreteBase->name){
                if(m_activeOwnerClass->concreteBase->canBorrowMotherArena() && m_activeOwnerClass->concreteBase->needsClassArena){
                    out.append(firstInit ? " : " : ", ");
                    firstInit = false;
                    m_typeEmitter.emitTypeCpp(out, *iface);
                    out.push_back('(');
                    out.append(CodegenSymbols::s_MotherArenaVar).push_back(')');
                }
                continue;
            }
            if(!ctor.initializedCppBase.empty() && iface->name == ctor.initializedCppBase){
                out.append(firstInit ? " : " : ", ");
                firstInit = false;
                m_typeEmitter.emitTypeCpp(out, *iface);
                out.push_back('(');
                for(usize i = 0u; i < ctor.cppBaseArguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    const MStringView argument = ctor.cppBaseArguments[i];
                    out.append(argument.data(), argument.size());
                }
                out.push_back(')');
                continue;
            }
            const InterfaceCtorForwardInfo* fwd = ContractSemantics::FindInterfaceCtorForward(m_symbols.resolveBaseTypeName(iface->name));
            if(!fwd)
                continue;
            for(const ParamDecl& param : ctor.params){
                if(param.type && !param.name.empty() && m_symbols.resolveBaseTypeName(param.type->name) == fwd->paramAdhdTypeName){
                    out.append(firstInit ? " : " : ", ");
                    firstInit = false;
                    out.append(fwd->cppBaseType.data(), fwd->cppBaseType.size());
                    out.push_back('(');
                    out.append(param.name.data(), param.name.size());
                    out.push_back(')');
                    break;
                }
            }
        }
    }
    if(
        m_activeOwnerClass
        && m_activeOwnerClass->canBorrowMotherArena()
        && m_activeOwnerNeedsArena
    ){
        out.append(firstInit ? " : " : ", ");
        firstInit = false;
        out.append(CodegenSymbols::s_MemberArenaVar.data(), CodegenSymbols::s_MemberArenaVar.size());
        out.push_back('(');
        out.append(CodegenSymbols::s_MakeNotNullFunc.data(), CodegenSymbols::s_MakeNotNullFunc.size());
        out.append("(&");
        out.append(CodegenSymbols::s_MotherArenaVar.data(), CodegenSymbols::s_MotherArenaVar.size());
        out.append("))");
    }
    MVector<MStringView> readyFields(m_arena);
    const auto isReadyField = [&](const MStringView name)noexcept{
        if(m_activeOwnerClass && m_activeOwnerClass->concreteBase){
            const FieldDecl* inherited = SymbolTable::FindClassField(*m_activeOwnerClass, name);
            if(inherited && inherited->declaringClass != m_activeOwnerClass)
                return true;
        }
        for(const MStringView ready : readyFields){
            if(ready == name)
                return true;
        }
        return false;
    };
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type)
            continue;
        const Expr* uniqueAssign = nullptr;
        b8 inLeadingPrefix = false;
        const Expr* rhs = __hidden_adhd_source_emitter::FindConstructorFieldAssignment(ctor.body, field->name, uniqueAssign, inLeadingPrefix);
        if(!rhs){
            if(m_activeOwnerClass && m_activeOwnerClass->preserveFieldOrder)
                readyFields.push_back(field->name);
            continue;
        }
        if(field->defaultExpr || field->hasCtorInit)
            continue;
        const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(field->type);
        if(fieldRefMode != ParamPassingMode::None){
            if(uniqueAssign)
                skipped->push_back(uniqueAssign);
            out.append(firstInit ? " : " : ", ");
            firstInit = false;
            out.append(field->name.data(), field->name.size());
            out.append("(::MakeNotNull(::AddressOf(");
            emitExpr(out, *rhs);
            out.append(")))");
        }
        else if(uniqueAssign && inLeadingPrefix && !__hidden_adhd_source_emitter::ExprDisallowsMemberInitializerLift(rhs, isReadyField)){
            const MStringView baseFieldType = m_symbols.resolveBaseTypeName(field->type->name);
            const b8 isDirectCtorCall = rhs->kind == ExprKind::Call
                && !rhs->isBuiltinCall
                && rhs->lhs
                && rhs->lhs->kind == ExprKind::Identifier
                && m_symbols.resolveBaseTypeName(rhs->lhs->text) == baseFieldType
                && !SymbolTable::IsSimdType(baseFieldType)
                && !SymbolTable::IsUnalignedVectorType(baseFieldType)
                && m_symbols.findFreeFunction(baseFieldType) == nullptr;
            const TypeLayoutAnalyzer layoutAnalyzer(m_symbols);
            const b8 requiresMemberInit = !m_symbols.isSelfDefaultedType(field->type->name, m_activeOwnerClass);
            const ClassDecl* fieldClass = m_symbols.findClass(baseFieldType);
            // Asset assignment preserves the destination virtual path. Construct the named asset directly.
            const b8 constructsAsset = isDirectCtorCall && fieldClass && !fieldClass->assetTypeName.empty();
            if(field->usesClassArena && !requiresMemberInit && !constructsAsset && (!isDirectCtorCall || layoutAnalyzer.isTypeAssignable(*field->type, false)))
                continue;
            if(!isDirectCtorCall && !requiresMemberInit)
                continue;
            skipped->push_back(uniqueAssign);
            out.append(firstInit ? " : " : ", ");
            firstInit = false;
            out.append(field->name.data(), field->name.size());
            if(isDirectCtorCall){
                const MStringView injectedArena = (field->usesClassArena || rhs->injectClassArena) ? activeClassArenaExpr() : MStringView{};
                ConstructorArgumentEmitter(*this).emit(out, baseFieldType, rhs->arguments, injectedArena, false, true);
            }
            else{
                out.push_back('(');
                emitExpr(out, *rhs);
                out.push_back(')');
            }
        }
        else
            continue;
        if(m_activeOwnerClass && m_activeOwnerClass->preserveFieldOrder)
            readyFields.push_back(field->name);
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
    out.append("::NWB::Core::Alloc::ScratchArena __adhd_scratch_storage(::Name(\"");
    out.append(arenaName.data(), arenaName.size());
    out.append("\"));\r\n");
    emitIndent(out, indent);
    out.append("[[maybe_unused]] ::ArenaBase& __adhd_scratch = __adhd_scratch_storage;\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

