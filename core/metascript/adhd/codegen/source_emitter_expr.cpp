// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitExpr(MString& out, const Expr& expr, const b8 omitOuterBinaryParens)const{
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

    if(IsRegisterPromotableSimdType(expr.resolvedSimdType)){
        b8 isSimdCtorWithRegArg = false;
        if(expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier
            && m_symbols.isSimdType(m_symbols.resolveBaseTypeName(expr.lhs->text)))
        {
            for(const Expr* arg : expr.arguments){
                if(arg && (findSimdBindingForExpr(arg) || ExprContainsSimdMath(arg, m_symbols))){
                    isSimdCtorWithRegArg = true;
                    break;
                }
            }
        }
        const b8 isDirectSimdRegOp = (expr.kind == ExprKind::MemberAccess && expr.isSwizzle)
            || expr.kind == ExprKind::Unary
            || (expr.kind == ExprKind::Binary && expr.op != TokenType::Equal)
            || (expr.kind == ExprKind::Call && expr.lhs && expr.lhs->kind == ExprKind::Identifier
                && !MapBuiltinMathFunction(expr.lhs->text).empty())
            || isSimdCtorWithRegArg;
        if(isDirectSimdRegOp){
            const MStringView simdType = expr.resolvedSimdType;
            const MStringView storeFn = SimdStoreFunctionCpp(simdType);
            TypeRef tmpType(m_arena);
            tmpType.name = simdType;

            out.append("([&]()noexcept{ ");
            m_typeEmitter.emitTypeCpp(out, tmpType);
            out.append(" __adhd_tmp{}; ");
            out.append(storeFn.data(), storeFn.size());
            out.push_back('(');
            emitSimdRegExpr(out, expr, simdType);
            out.append(", __adhd_tmp); return __adhd_tmp; }())");
            return;
        }
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
            const MStringView globalConstCpp = FindBuiltinGlobalConstantCppName(expr.text);
            if(!globalConstCpp.empty())
                out.append(globalConstCpp.data(), globalConstCpp.size());
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
        if(expr.derefNotNull){
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
        u32 lane = 0u;
        if(!expr.isMutArg && expr.lhs && IsRegisterPromotableSimdType(expr.lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr.text, lane))
        {
            if(findSimdBindingForExpr(expr.lhs) || ExprContainsSimdMath(expr.lhs, m_symbols)){
                emitSimdLaneGetExpr(out, *expr.lhs, lane, nullptr);
                break;
            }
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
        u32 vecLanes = 0u;
        if(!expr.isMutArg && expr.lhs && expr.rhs
            && IsRegisterPromotableSimdType(expr.lhs->resolvedSimdType)
            && m_symbols.isSimdVectorType(expr.lhs->resolvedSimdType, vecLanes))
        {
            if(findSimdBindingForExpr(expr.lhs) || ExprContainsSimdMath(expr.lhs, m_symbols)){
                emitSimdLaneGetExpr(out, *expr.lhs, 0u, expr.rhs);
                break;
            }
        }
        if(expr.derefNotNull)
            out.append("(*(");
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        if(expr.isVectorRawIndex)
            out.append(".raw[");
        else
            out.push_back('[');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
        out.push_back(']');
        if(expr.derefNotNull)
            out.append("))");
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr.lhs->text;
            MStringView scalarReductionSuffix;
#define NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION(adhdName, laneIntrinsicSuffix) \
            if(callee == adhdName) scalarReductionSuffix = laneIntrinsicSuffix;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_SCALAR_REDUCTION
            if(!scalarReductionSuffix.empty()
                && !expr.arguments.empty() && expr.arguments[0]
                && IsRegisterPromotableSimdType(expr.arguments[0]->resolvedSimdType))
            {
                const MStringView argSimd = expr.arguments[0]->resolvedSimdType;
                const b8 isDouble = IsDoubleSimdType(argSimd);
                const u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0]);
                out.append(isDouble ? "::VectorDoubleGetX(" : "::VectorGetX(");
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, scalarReductionSuffix);
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(expr.arguments[i])
                        emitSimdRegExpr(out, *expr.arguments[i], argSimd);
                }
                out.append("))");
                break;
            }
            if(callee == "lerp" && expr.arguments.size() == 3u
                && expr.arguments[0] && expr.arguments[1] && expr.arguments[2]
                && expr.resolvedSimdType.empty())
            {
                out.append("((");
                emitExpr(out, *expr.arguments[0]);
                out.append(") + ((");
                emitExpr(out, *expr.arguments[1]);
                out.append(") - (");
                emitExpr(out, *expr.arguments[0]);
                out.append(")) * (");
                emitExpr(out, *expr.arguments[2]);
                out.append("))");
                break;
            }
            if(callee == "rsqrt" && expr.arguments.size() == 1u
                && expr.arguments[0] && expr.resolvedSimdType.empty())
            {
                out.append("(static_cast<decltype(");
                emitExpr(out, *expr.arguments[0]);
                out.append(")>(1) / ::Sqrt(");
                emitExpr(out, *expr.arguments[0]);
                out.append("))");
                break;
            }
        }

        if(expr.derefNotNull)
            out.append("(*");
        if(expr.lhs){
            if(expr.lhs->kind == ExprKind::Identifier){
                const MStringView mappedMath = MapBuiltinMathFunction(expr.lhs->text);
                if(!mappedMath.empty()){
                    out.append(mappedMath.data(), mappedMath.size());
                }
                else{
                    TypeRef maybeType(m_arena);
                    maybeType.name = expr.lhs->text;
                    m_typeEmitter.emitTypeCpp(out, maybeType);
                }
            }
            else{
                emitExpr(out, *expr.lhs);
            }
        }
        if(!expr.callTemplateArgs.empty())
            m_typeEmitter.emitTypeArgListCpp(out, expr.callTemplateArgs);
        const MStringView baseCallee = (expr.lhs && expr.lhs->kind == ExprKind::Identifier)
            ? m_symbols.resolveBaseTypeName(expr.lhs->text)
            : MStringView{};
        const MStringView injectedArena = expr.injectClassArena
            ? MStringView("s_ClassArena")
            : (expr.injectScratchArena ? MStringView("__adhd_scratch") : MStringView{});
        emitCtorOrCallArgs(out, baseCallee, expr.arguments, injectedArena, false, false);
        if(expr.derefNotNull)
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
        if(!omitOuterBinaryParens)
            out.push_back('(');
        if(expr.lhs)
            emitExpr(out, *expr.lhs);
        out.push_back(' ');
        out.append(expr.text.data(), expr.text.size());
        out.push_back(' ');
        if(expr.rhs)
            emitExpr(out, *expr.rhs);
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
        out.append("](");
        for(usize i = 0u; i < expr.lambdaParams.size(); ++i){
            if(i > 0u)
                out.append(", ");
            const ParamDecl& p = expr.lambdaParams[i];
            if(p.type)
                m_typeEmitter.emitParamTypeCpp(out, p.mode, *p.type);
            out.push_back(' ');
            out.append(p.name.data(), p.name.size());
        }
        out.append(")noexcept -> ");
        if(expr.lambdaReturnType)
            m_typeEmitter.emitTypeCpp(out, *expr.lambdaReturnType);
        else
            out.append("void");
        out.append("{\r\n");
        if(expr.lambdaUsesScratchArena){
            emitIndent(out, 2u);
            out.append("::NWB::Core::Alloc::ScratchArena __adhd_scratch(::Name(\"lambda\"));\r\n");
        }
        if(expr.lambdaUsesTaskScope)
            emitTaskScopePrologue(out, 2u);
        const b8 prevTaskScope = m_activeScopeUsesTaskScope;
        m_activeScopeUsesTaskScope = expr.lambdaUsesTaskScope;
        emitScopedSimdBody(out, expr.lambdaBody, 2u);
        m_activeScopeUsesTaskScope = prevTaskScope;
        out.append("    }");
        break;
    }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

