// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "source_emitter.h"
#include "constructor_argument_emitter.h"
#include "builtin_call_emitter.h"
#include "simd_storage_emitter.h"
#include "simd_logic_emitter.h"
#include "simd_expression_analysis.h"
#include "parallel_statement_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SourceEmitter::tryEmitFusedSimdLogicChain(MString& out, const Expr& expr, const b8 omitOuterBinaryParens)const{
    return SimdLogicEmitter(*this).tryEmit(out, expr, omitOuterBinaryParens);
}


void SourceEmitter::emitExpr(MString& out, const Expr& expr, const b8 omitOuterBinaryParens)const{
    const TokenType::Enum prevUnfusedLogicOp = m_unfusedLogicOp;
    if(expr.kind != ExprKind::Binary || expr.op != prevUnfusedLogicOp)
        m_unfusedLogicOp = TokenType::EndOfFile;

    if(expr.wrapInSimdResult){
        out.append(IsDoubleSimdType(expr.resolvedSimdType) ? "::SIMDVectorDoubleResult{ " : "::SIMDVectorSingleResult{ ");
        Expr unwrapped = expr;
        unwrapped.wrapInSimdResult = false;
        emitExpr(out, unwrapped);
        out.append(" }");
        return;
    }

    if(expr.adaptToTChar && expr.kind != ExprKind::StringLiteral){
        out.append("::ConvertString(");
        Expr unwrapped = expr;
        unwrapped.adaptToTChar = false;
        emitExpr(out, unwrapped);
        out.push_back(')');
        return;
    }

    if(expr.moveAtLastUse){
        out.append("::Move(");
        out.append(expr.text.data(), expr.text.size());
        out.push_back(')');
        return;
    }

    if(isDirectSimdRegExpr(expr)){
        emitSimdRegExpr(out, expr, expr.resolvedSimdType);
        return;
    }

    switch(expr.kind){
    case ExprKind::IntegerLiteral:
    case ExprKind::FloatLiteral:
    case ExprKind::DoubleLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::Identifier:{
        if(expr.derefNotNull){
            out.append("(*");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            if(const BuiltinGlobalConstantInfo* globalConst = expr.isBuiltinConstant ? FindBuiltinGlobalConstant(expr.text) : nullptr)
                out.append(globalConst->cppExportName.data(), globalConst->cppExportName.size());
            else
                out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::StringLiteral:{
        if(expr.adaptToTChar){
            out.append("NWB_TEXT(\"");
            out.append(expr.text.data(), expr.text.size());
            out.append("\")");
        }
        else{
            out.push_back('"');
            out.append(expr.text.data(), expr.text.size());
            out.push_back('"');
        }
        break;
    }
    case ExprKind::ThisMember:{
        if(expr.text.empty()){
            out.append("(*this)");
        }
        else if(expr.derefNotNull){
            out.append("(*this->");
            out.append(expr.text.data(), expr.text.size());
            out.push_back(')');
        }
        else{
            out.append("this->");
            out.append(expr.text.data(), expr.text.size());
        }
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier && expr.lhs->callTemplateArgs.empty() && !expr.derefNotNull && !expr.isPointerMemberAccess && !m_symbols.findGlobalConstant(expr.lhs->text)){
            const MStringView resolvedBaseType = m_symbols.resolveBaseTypeName(expr.lhs->text);
            MStringView limitSuffix;
            if(BuiltinTypeSemantics::IsNumericScalarType(resolvedBaseType) && BuiltinTypeSemantics::TryMapScalarLimitMember(expr.text, &limitSuffix)){
                out.append("::Limit<::");
                out.append(resolvedBaseType.data(), resolvedBaseType.size());
                out.append(limitSuffix.data(), limitSuffix.size());
                break;
            }
        }
        u32 lane = 0u;
        if(!expr.isMutArg && expr.lhs && SymbolTable::IsSimdRegisterVectorType(expr.lhs->resolvedSimdType) && IsSingleSwizzleLane(expr.text, lane)){
            emitSimdLaneGetExpr(out, *expr.lhs, lane, nullptr);
            break;
        }
        if(expr.derefNotNull)
            out.append("(*(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isPointerMemberAccess)
            out.append("->");
        else
            out.push_back('.');
        out.append(expr.text.data(), expr.text.size());
        if(expr.derefNotNull)
            out.append("))");
        break;
    }
    case ExprKind::ScopeAccess:{
        for(usize i = 0u; i < expr.scopeChain.size(); ++i){
            if(i > 0u)
                out.append("::");
            out.append(expr.scopeChain[i].data(), expr.scopeChain[i].size());
        }
        break;
    }
    case ExprKind::Index:{
        if(!expr.isMutArg && expr.lhs && expr.rhs && SymbolTable::IsSimdRegisterVectorType(expr.lhs->resolvedSimdType)){
            u32 staticLane = 0u;
            if(SimdExpressionAnalysis::TryMatchConstLaneIndex(*expr.rhs, staticLane))
                emitSimdLaneGetExpr(out, *expr.lhs, staticLane, nullptr);
            else
                emitSimdLaneGetExpr(out, *expr.lhs, 0u, expr.rhs);
            break;
        }
        if(expr.isStringByteMutRef)
            out.append("reinterpret_cast<::u8&>(");
        else if(expr.isStringByteConstRef)
            out.append("reinterpret_cast<const ::u8&>(");
        else if(expr.isStringByteRead)
            out.append("static_cast<::u8>(");
        if(expr.derefNotNull)
            out.append("(*(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isMatrixRowIndex)
            out.append((expr.lhs && SymbolTable::IsSimdRegisterMatrixType(expr.lhs->resolvedSimdType)) ? ".v[" : ".rows[");
        else if(expr.isVectorRawIndex)
            out.append(".raw[");
        else
            out.push_back('[');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(']');
        if(expr.derefNotNull)
            out.append("))");
        if(expr.isStringByteMutRef || expr.isStringByteConstRef || expr.isStringByteRead)
            out.push_back(')');
        break;
    }
    case ExprKind::Call:{
        if(BuiltinCallEmitter(*this).tryEmit(out, expr))
            break;

        if(expr.isStringSubstr && expr.lhs && expr.lhs->kind == ExprKind::MemberAccess && expr.lhs->lhs){
            out.append("([](const auto& __adhd_str, const ::usize __adhd_pos = 0u, const ::usize __adhd_count = ::AStringView::npos)noexcept{ const ::AStringView __adhd_view(__adhd_str.data(), __adhd_str.size()); const ::usize __adhd_clamped = __adhd_pos < __adhd_view.size() ? __adhd_pos : __adhd_view.size(); const ::AStringView __adhd_sub = __adhd_view.substr(__adhd_clamped, __adhd_count); return ::RemoveCVRef_T<decltype(__adhd_str)>(__adhd_sub.data(), __adhd_sub.size(), __adhd_str.get_allocator()); }(");
            emitExpr(out, *expr.lhs->lhs);
            for(const Expr* arg : expr.arguments){
                if(!arg)
                    continue;
                out.append(", ");
                emitExpr(out, *arg);
            }
            out.append("))");
            break;
        }
        const MStringView baseCallee = (expr.lhs && expr.lhs->kind == ExprKind::Identifier)
            ? m_symbols.resolveBaseTypeName(expr.lhs->text)
            : MStringView{};
        if((SymbolTable::IsSimdStorageType(baseCallee) || SymbolTable::IsUnalignedVectorType(baseCallee)) && !expr.arguments.empty()){
            MString storeCallStr(m_arena);
            if(SimdStorageEmitter(*this).tryEmit(storeCallStr, baseCallee, expr.arguments, "__adhd_tmp")){
                TypeRef tmpType(m_arena);
                tmpType.name = expr.lhs->text;
                out.append("([&]()noexcept{ ");
                m_typeEmitter.emitTypeCpp(out, tmpType);
                out.append(" __adhd_tmp{}; ");
                out.append(storeCallStr.data(), storeCallStr.size());
                out.append("; return __adhd_tmp; }())");
                break;
            }
        }
        if(expr.isStringByteMutRef)
            out.append("reinterpret_cast<::u8&>(");
        else if(expr.isStringByteConstRef)
            out.append("reinterpret_cast<const ::u8&>(");
        else if(expr.isStringByteRead)
            out.append("static_cast<::u8>(");
        if(expr.derefNotNull)
            out.append("(*");
        emitCallTargetExpr(out, expr.lhs, expr.isBuiltinCall);
        if(!expr.callTemplateArgs.empty())
            m_typeEmitter.emitTypeArgListCpp(out, expr.callTemplateArgs);
        const MStringView injectedArena = expr.injectClassArena
            ? MStringView("s_ClassArena")
            : (expr.injectScratchArena ? MStringView("__adhd_scratch") : MStringView{});
        ConstructorArgumentEmitter(*this).emit(out, baseCallee, expr.arguments, injectedArena, false, false);
        if(expr.unwrapSimdResultValue)
            out.append(".value");
        if(expr.derefNotNull)
            out.push_back(')');
        if(expr.isStringByteMutRef || expr.isStringByteConstRef || expr.isStringByteRead)
            out.push_back(')');
        break;
    }
    case ExprKind::Unary:{
        if(expr.isPostfix){
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(expr.text.data(), expr.text.size());
        }
        else{
            out.append(expr.text.data(), expr.text.size());
            if(expr.lhs){
                const b8 wrapPrefixChild = (expr.lhs->kind == ExprKind::Unary && !expr.lhs->isPostfix);
                if(wrapPrefixChild)
                    out.push_back('(');
                emitExpr(out, *expr.lhs);
                if(wrapPrefixChild)
                    out.push_back(')');
            }
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.isNotNullRebind){
            if(!omitOuterBinaryParens)
                out.push_back('(');
            if(expr.lhs)
                emitExpr(out, *expr.lhs);
            out.append(" = ::MakeNotNull(&(");
            if(expr.rhs)
                emitExpr(out, *expr.rhs);
            out.append(omitOuterBinaryParens ? "))" : ")))");
            break;
        }
        if(expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe){
            if(prevUnfusedLogicOp != expr.op && tryEmitFusedSimdLogicChain(out, expr, omitOuterBinaryParens))
                break;
            m_unfusedLogicOp = expr.op;
        }
        if(!omitOuterBinaryParens)
            out.push_back('(');
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(' ');
        out.append(expr.text.data(), expr.text.size());
        out.push_back(' ');
        if(expr.rhs){
            m_unfusedLogicOp = (expr.op == TokenType::AmpersandAmpersand || expr.op == TokenType::PipePipe) ? expr.op : TokenType::EndOfFile;
            emitExpr(out, *expr.rhs);
        }
        m_unfusedLogicOp = TokenType::EndOfFile;
        if(!omitOuterBinaryParens)
            out.push_back(')');
        break;
    }
    case ExprKind::Cast:{
        out.append("static_cast<");
        if(expr.targetType)
            m_typeEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::BitCast:{
        if(
            expr.targetType
            && expr.targetType->refMode == ParamPassingMode::None
            && expr.targetType->templateArgs.empty()
            && expr.lhs
            && !expr.lhs->isMutArg
        ){
            const Expr* vecBase = nullptr;
            u32 staticLane = 0u;
            const Expr* dynamicIdx = nullptr;
            if(
                expr.lhs->kind == ExprKind::MemberAccess
                && !expr.lhs->isSwizzle
                && expr.lhs->lhs
                && SymbolTable::IsSimdRegisterVectorType(expr.lhs->lhs->resolvedSimdType)
                && IsSingleSwizzleLane(expr.lhs->text, staticLane)
            ){
                vecBase = expr.lhs->lhs;
            }
            else if(
                expr.lhs->kind == ExprKind::Index
                && expr.lhs->lhs
                && expr.lhs->rhs
                && SymbolTable::IsSimdRegisterVectorType(expr.lhs->lhs->resolvedSimdType)
            ){
                vecBase = expr.lhs->lhs;
                if(!SimdExpressionAnalysis::TryMatchConstLaneIndex(*expr.lhs->rhs, staticLane))
                    dynamicIdx = expr.lhs->rhs;
            }
            if(vecBase && !dynamicIdx){
                while(
                    vecBase->kind == ExprKind::MemberAccess
                    && vecBase->isSwizzle
                    && vecBase->lhs
                    && vecBase->lhs->resolvedSimdType == vecBase->resolvedSimdType
                    && staticLane < vecBase->text.size()
                ){
                    staticLane = MapSwizzleCharToLane(vecBase->text[staticLane]);
                    vecBase = vecBase->lhs;
                }
            }
            if(vecBase){
                const MStringView vecSimd = vecBase->resolvedSimdType;
                const b8 isDouble = IsDoubleSimdType(vecSimd);
                const MStringView targetScalar = m_symbols.resolveBaseTypeName(expr.targetType->name);
                const b8 isUnsignedIntTarget = isDouble ? (targetScalar == "u64") : (targetScalar == "u32");
                const b8 isSignedIntTarget = isDouble ? (targetScalar == "i64") : (targetScalar == "i32");
                const b8 isFloatTarget = isDouble ? (targetScalar == "f64") : (targetScalar == "f32");
                if(isUnsignedIntTarget || isSignedIntTarget || isFloatTarget){
                    if(isSignedIntTarget)
                        out.append(isDouble ? "static_cast<i64>(" : "static_cast<i32>(");
                    if(isFloatTarget)
                        out.append(isDouble ? "::VectorDoubleGet" : "::VectorGet");
                    else
                        out.append(isDouble ? "::VectorDoubleGetInt" : "::VectorGetInt");
                    if(dynamicIdx){
                        out.append("ByIndex(");
                        emitSimdRegExpr(out, *vecBase, vecSimd);
                        out.append(", static_cast<usize>(");
                        emitExpr(out, *dynamicIdx);
                        out.append("))");
                    }
                    else{
                        const MStringView laneSuffix = SimdLaneCallSuffix(staticLane);
                        out.append(laneSuffix.data(), laneSuffix.size());
                        emitSimdRegExpr(out, *vecBase, vecSimd);
                        out.push_back(')');
                    }
                    if(isSignedIntTarget)
                        out.push_back(')');
                    break;
                }
            }
        }
        out.append("::BitCast<");
        if(expr.targetType)
            m_typeEmitter.emitTypeCpp(out, *expr.targetType);
        out.append(">(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(')');
        break;
    }
    case ExprKind::Lambda:{
        out.push_back('[');
        b8 firstEmittedCap = true;
        for(usize i = 0u; i < expr.lambdaCaptures.size(); ++i){
            const LambdaCapture& cap = expr.lambdaCaptures[i];
            if(cap.isGlobalConstant)
                continue;
            if(!firstEmittedCap)
                out.append(", ");
            firstEmittedCap = false;
            if(cap.name == "this"){
                out.append("this");
            }
            else if(cap.mode == ParamPassingMode::Mut){
                out.push_back('&');
                out.append(cap.name.data(), cap.name.size());
            }
            else{
                out.append(cap.name.data(), cap.name.size());
            }
        }
        out.push_back(']');
        m_typeEmitter.emitParamListCpp(out, expr.lambdaParams);
        out.append("noexcept -> ");
        if(expr.lambdaReturnType)
            m_typeEmitter.emitTypeCpp(out, *expr.lambdaReturnType);
        else
            out.append("void");
        out.append("{\r\n");
        if(statementsNeedSimdFloatingPointEnv(expr.lambdaBody))
            out.append("#pragma STDC FENV_ACCESS ON\r\n");
        if(expr.lambdaUsesScratchArena)
            emitScratchArenaPrologue(out, 2u, "lambda");
        if(expr.lambdaUsesTaskScope)
            ParallelStatementEmitter(*this).emitTaskScopePrologue(out, 2u);
        const u32 prevSwitchBreak = m_activeSwitchBreakLabelLine;
        m_activeSwitchBreakLabelLine = 0u;
        emitStatementList(out, expr.lambdaBody, 2u);
        m_activeSwitchBreakLabelLine = prevSwitchBreak;
        out.append("    }");
        break;
    }
    }
}

void SourceEmitter::emitCallTargetExpr(MString& out, const Expr* lhs, const b8 isBuiltinCall)const{
    if(!lhs)
        return;
    if(lhs->kind == ExprKind::Identifier){
        const MStringView mappedMath = isBuiltinCall ? MapBuiltinMathFunction(lhs->text) : MStringView{};
        if(!mappedMath.empty()){
            out.append(mappedMath.data(), mappedMath.size());
        }
        else{
            TypeRef maybeType(m_arena);
            maybeType.name = lhs->text;
            m_typeEmitter.emitTypeCpp(out, maybeType);
        }
    }
    else{
        emitExpr(out, *lhs);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

