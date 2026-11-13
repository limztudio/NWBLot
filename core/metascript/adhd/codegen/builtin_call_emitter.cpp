// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "builtin_call_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuiltinCallEmitter::tryEmit(MString& out, const Expr& expr)const{
    const BuiltinFunctionInfo* scopedFn = (expr.isBuiltinCall && expr.lhs && expr.lhs->kind == ExprKind::ScopeAccess && expr.lhs->scopeChain.size() == 2u)
        ? FindBuiltinScopedFunction(expr.lhs->scopeChain[0], expr.lhs->scopeChain[1])
        : nullptr;
    if(expr.isBuiltinCall && expr.lhs && (expr.lhs->kind == ExprKind::Identifier || scopedFn != nullptr)){
        const MStringView callee = (expr.lhs->kind == ExprKind::Identifier) ? expr.lhs->text : scopedFn->adhdName;
        const MStringView registerCall = expr.callTemplateArgs.empty() ? MapSimdRegisterCall(callee) : MStringView{};
        if(!registerCall.empty()){
            out.append(registerCall.data(), registerCall.size());
            out.push_back('(');
            for(usize i = 0u; i < expr.arguments.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(expr.arguments[i]){
                    if(!expr.arguments[i]->isMutArg && IsRegisterPromotableSimdType(expr.arguments[i]->resolvedSimdType))
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
                if(effLanes1 > effLanes)
                    effLanes = effLanes1;
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
        if(const BuiltinFunctionInfo* fnInfo = FindBuiltinFunction(callee)){
            if(fnInfo->callLowering == BuiltinCallLowering::BinaryByteViewArg0 || fnInfo->callLowering == BuiltinCallLowering::BinaryByteViewArg1){
                const usize byteViewArgIdx = (fnInfo->callLowering == BuiltinCallLowering::BinaryByteViewArg0) ? 0u : 1u;
                if(expr.arguments.size() > byteViewArgIdx && expr.arguments[byteViewArgIdx]){
                    out.append(fnInfo->cppExportName.data(), fnInfo->cppExportName.size());
                    if(!expr.callTemplateArgs.empty())
                        m_emitter.m_typeEmitter.emitTypeArgListCpp(out, expr.callTemplateArgs);
                    out.push_back('(');
                    b8 hasArg = false;
                    if(fnInfo->prependsScratchArena){
                        const MStringView arenaExpr = expr.injectClassArena ? m_emitter.activeClassArenaExpr() : MStringView("__adhd_scratch");
                        out.append(arenaExpr.data(), arenaExpr.size());
                        hasArg = true;
                    }
                    for(usize i = 0u; i < expr.arguments.size(); ++i){
                        if(hasArg)
                            out.append(", ");
                        hasArg = true;
                        if(i == byteViewArgIdx){
                            out.append("([](const auto& __adhd_bytes)noexcept{ if constexpr(requires{ __adhd_bytes.data(); __adhd_bytes.size(); }){ return ::BinaryByteView{ reinterpret_cast<const ::u8*>(__adhd_bytes.data()), __adhd_bytes.size() }; }else{ const ::AStringView __adhd_sv(__adhd_bytes); return ::BinaryByteView{ reinterpret_cast<const ::u8*>(__adhd_sv.data()), __adhd_sv.size() }; } }(");
                            if(expr.arguments[i])
                                m_emitter.emitExpr(out, *expr.arguments[i]);
                            out.append("))");
                        }
                        else if(expr.arguments[i]){
                            m_emitter.emitExpr(out, *expr.arguments[i]);
                        }
                    }
                    out.push_back(')');
                    return true;
                }
            }
            switch(fnInfo->callLowering){
            case BuiltinCallLowering::BinaryByteViewArg0:
            case BuiltinCallLowering::BinaryByteViewArg1:
                break;
            case BuiltinCallLowering::StagedWriteTextAtomic:{
                if(expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                    out.append("(::StagedFileWriter(");
                    m_emitter.emitExpr(out, *expr.arguments[0]);
                    out.append(").writeTextAtomic(");
                    m_emitter.emitExpr(out, *expr.arguments[1]);
                    out.append("))");
                    return true;
                }
                break;
            }
            case BuiltinCallLowering::StagedWriteBinaryAtomic:{
                if(expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                    out.append("(::StagedFileWriter(");
                    m_emitter.emitExpr(out, *expr.arguments[0]);
                    out.append(").writeBinaryAtomic(");
                    m_emitter.emitExpr(out, *expr.arguments[1]);
                    out.append("))");
                    return true;
                }
                break;
            }
            case BuiltinCallLowering::StagedWriteAtomicChecked:{
                if(expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                    out.append("([](const auto& __adhd_p, const auto& __adhd_b){ return ::StagedFileWriter(__adhd_p).writeAtomicChecked(reinterpret_cast<const ::u8*>(__adhd_b.data()), __adhd_b.size()); }(");
                    m_emitter.emitExpr(out, *expr.arguments[0]);
                    out.append(", ");
                    m_emitter.emitExpr(out, *expr.arguments[1]);
                    out.append("))");
                    return true;
                }
                break;
            }
            case BuiltinCallLowering::Arg0AndBytePtrSizeArg1:{
                if(expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                    out.append("([](const auto& __adhd_p, const auto& __adhd_b){ return ");
                    out.append(fnInfo->cppExportName.data(), fnInfo->cppExportName.size());
                    out.append("(__adhd_p, reinterpret_cast<const ::u8*>(__adhd_b.data()), __adhd_b.size()); }(");
                    m_emitter.emitExpr(out, *expr.arguments[0]);
                    out.append(", ");
                    m_emitter.emitExpr(out, *expr.arguments[1]);
                    out.append("))");
                    return true;
                }
                break;
            }
            case BuiltinCallLowering::None:
                break;
            }
            if(fnInfo->prependsScratchArena){
                out.append(fnInfo->cppExportName.data(), fnInfo->cppExportName.size());
                if(!expr.callTemplateArgs.empty())
                    m_emitter.m_typeEmitter.emitTypeArgListCpp(out, expr.callTemplateArgs);
                out.push_back('(');
                const MStringView arenaExpr = expr.injectClassArena ? m_emitter.activeClassArenaExpr() : MStringView("__adhd_scratch");
                out.append(arenaExpr.data(), arenaExpr.size());
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    out.append(", ");
                    if(expr.arguments[i])
                        m_emitter.emitExpr(out, *expr.arguments[i]);
                }
                out.push_back(')');
                return true;
            }
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

