// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builtin_call_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


b8 BuiltinCallEmitter::tryEmit(MString& out, const Expr& expr)const{
    if(expr.isBuiltinCall && expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr.lhs->text;
        const MStringView registerCall = MapSimdRegisterCall(callee);
        if(!registerCall.empty()){
            out.append(registerCall.data(), registerCall.size());
            out.push_back('(');
            for(usize i = 0u; i < expr.arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(expr.arguments[i]){
                    if(IsRegisterPromotableSimdType(expr.arguments[i]->resolvedSimdType))
                        m_emitter.emitSimdRegExpr(out, *expr.arguments[i], expr.arguments[i]->resolvedSimdType);
                    else
                        m_emitter.emitExpr(out, *expr.arguments[i]);
                }
            }
            out.push_back(')');
            return true;
        }
        if(callee == "IsFinite" && expr.arguments.size() == 1u && expr.arguments[0] && (expr.arguments[0]->resolvedSimdType == "VectorSingle" || expr.arguments[0]->resolvedSimdType == "VectorDouble")){
            const Expr& arg0 = *expr.arguments[0];
            const b8 isDouble = IsDoubleSimdType(arg0.resolvedSimdType);
            u8 activeMask = 0xFu;
            const Expr* vecBase = &arg0;
            if(arg0.kind == ExprKind::MemberAccess && arg0.isSwizzle && arg0.lhs && arg0.lhs->resolvedSimdType == arg0.resolvedSimdType){
                u8 swizMask = 0u;
                for(usize i = 0u; i < arg0.text.size(); ++i)
                    swizMask |= static_cast<u8>(1u << MapSwizzleCharToLane(arg0.text[i]));
                if(!SimdComponentMaskConstant(swizMask, isDouble).empty()){
                    activeMask = swizMask;
                    vecBase = arg0.lhs;
                }
                else{
                    const u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(arg0, &m_emitter.m_symbols);
                    activeMask = effLanes == 2u ? 0x3u : (effLanes == 3u ? 0x7u : 0xFu);
                }
            }
            const MStringView maskConst = SimdComponentMaskConstant(activeMask, isDouble);
            out.append(isDouble ? "::VectorDoubleIsFinite(" : "::VectorIsFinite(");
            m_emitter.emitSimdRegExpr(out, *vecBase, arg0.resolvedSimdType);
            out.append(", ");
            out.append(maskConst.data(), maskConst.size());
            out.push_back(')');
            return true;
        }
        const MStringView scalarReductionSuffix = MapSimdScalarReductionSuffix(callee);
        if(!scalarReductionSuffix.empty() && !expr.arguments.empty() && expr.arguments[0] && IsRegisterPromotableSimdType(expr.arguments[0]->resolvedSimdType)){
            const MStringView argSimd = expr.arguments[0]->resolvedSimdType;
            const b8 isDouble = IsDoubleSimdType(argSimd);
            u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[0], &m_emitter.m_symbols);
            if(expr.arguments.size() == 2u && expr.arguments[1]){
                const u32 effLanes1 = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[1], &m_emitter.m_symbols);
                if(effLanes1 < effLanes)
                    effLanes = effLanes1;
            }
            if(callee == "Dot" && effLanes == 4u && !IsIntSimdType(argSimd) && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const Expr* sumArg = SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*expr.arguments[1], isDouble, &m_emitter.m_symbols)
                    ? expr.arguments[0]
                    : (SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(*expr.arguments[0], isDouble, &m_emitter.m_symbols) ? expr.arguments[1] : nullptr);
                if(sumArg){
                    out.append(isDouble ? "::VectorDoubleGetX(::VectorDoubleSum(" : "::VectorGetX(::VectorSum(");
                    m_emitter.emitSimdRegExpr(out, *sumArg, argSimd);
                    out.append("))");
                    return true;
                }
            }
            out.append(isDouble ? "::VectorDoubleGetX(" : "::VectorGetX(");
            AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, scalarReductionSuffix);
            for(usize i = 0u; i < expr.arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(expr.arguments[i])
                    m_emitter.emitSimdRegExpr(out, *expr.arguments[i], argSimd);
            }
            out.append("))");
            return true;
        }
        if(callee == "Lerp" && expr.arguments.size() == 3u && expr.arguments[0] && expr.arguments[1] && expr.arguments[2] && expr.resolvedSimdType.empty()){
            out.append("([](auto&& __adhd_lerp_a, auto&& __adhd_lerp_b, auto&& __adhd_lerp_t) -> decltype(auto) { return (::Forward<decltype(__adhd_lerp_a)>(__adhd_lerp_a) + ((::Forward<decltype(__adhd_lerp_b)>(__adhd_lerp_b) - __adhd_lerp_a) * ::Forward<decltype(__adhd_lerp_t)>(__adhd_lerp_t))); }(");
            m_emitter.emitExpr(out, *expr.arguments[0]);
            out.append(", ");
            m_emitter.emitExpr(out, *expr.arguments[1]);
            out.append(", ");
            m_emitter.emitExpr(out, *expr.arguments[2]);
            out.append("))");
            return true;
        }
        if(callee == "Rsqrt" && expr.arguments.size() == 1u && expr.arguments[0] && expr.resolvedSimdType.empty()){
            out.append("(static_cast<decltype(");
            m_emitter.emitExpr(out, *expr.arguments[0]);
            out.append(")>(1) / ::Sqrt(");
            m_emitter.emitExpr(out, *expr.arguments[0]);
            out.append("))");
            return true;
        }
        const Expr* maxBaseVec = nullptr;
        b8 maxIsDouble = false;
        u32 maxLanes = 0u;
        b8 maxIsAbs = false;
        if(callee == "Max" && SimdExpressionAnalysis::TryMatchSimdMaxComponentReduction(expr, maxBaseVec, maxIsDouble, maxLanes, maxIsAbs) && maxBaseVec){
            out.append(maxIsDouble ? "::VectorDoubleGetX(" : "::VectorGetX(");
            AppendSimdLaneCountIntrinsicPrefix(out, maxIsDouble, maxLanes, "MaxComponent");
            if(maxIsAbs)
                out.append(maxIsDouble ? "::VectorDoubleAbs(" : "::VectorAbs(");
            m_emitter.emitSimdRegExpr(out, *maxBaseVec, maxIsDouble ? "VectorDouble" : "VectorSingle");
            if(maxIsAbs)
                out.push_back(')');
            out.append("))");
            return true;
        }
    }
    return false;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
