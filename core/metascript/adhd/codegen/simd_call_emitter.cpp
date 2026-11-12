// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_call_emitter.h"
#include "simd_constructor_emitter.h"
#include "simd_optimization_analysis.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SimdCallEmitter::tryEmit(MString& out, const Expr& expr, const MStringView simdType)const{
    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);
    if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr.lhs->text;
        const MStringView baseCallee = m_emitter.m_symbols.resolveBaseTypeName(callee);
        u32 ctorLanes = 0u;
        if(SymbolTable::IsSimdVectorType(baseCallee, ctorLanes) || SymbolTable::IsSimdMatrixType(baseCallee)){
            SimdConstructorEmitter(m_emitter, out, simdType, expr.arguments).emit();
            return true;
        }
        if(!expr.isBuiltinCall)
            return false;

        b8 isIntFallbackCallee = false;
#define NWB_ADHD_EXPORT_SIMD_INT_FALLBACK(adhdName) \
        if(callee == adhdName) isIntFallbackCallee = true;
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_INT_FALLBACK
        if(isInt && isIntFallbackCallee){
            const MStringView regType = SimdRegisterCppType(simdType);
            const MStringView laneType = IntSimdLaneType(simdType);
            const MStringView rawWordType = IntSimdRawWordType(isDouble);
            const MStringView scalarFn = MapBuiltinMathFunction(callee);
            out.append("([&]()noexcept{ ");
            for(usize i = 0u; i < expr.arguments.size(); ++i){
                out.append("const ");
                out.append(regType.data(), regType.size());
                out.append(" __a");
                AppendUnsignedDecimal(out, i);
                out.append(" = ");
                if(expr.arguments[i])
                    m_emitter.emitSimdRegExpr(out, *expr.arguments[i], simdType);
                out.append("; ");
            }
            out.append("return ");
            out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
            for(u32 lane = 0u; lane < 4u; ++lane){
                if(lane > 0u)
                    out.append(", ");
                out.append("static_cast<");
                out.append(rawWordType.data(), rawWordType.size());
                out.append(">(");
                out.append(scalarFn.data(), scalarFn.size());
                out.push_back('(');
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    MString argVarName(m_emitter.m_arena);
                    argVarName.append("__a");
                    AppendUnsignedDecimal(argVarName, i);
                    AppendIntSimdLaneExtract(out, MStringView(argVarName.data(), argVarName.size()), lane, isDouble, laneType);
                }
                out.append("))");
            }
            out.append("); }())");
            return true;
        }

        auto emitSimdCallRegArgsAndClose = [&](const Span<Expr* const> callArgs){
            for(usize i = 0u; i < callArgs.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(callArgs[i]){
                    if(!callArgs[i]->isMutArg && IsRegisterPromotableSimdType(callArgs[i]->resolvedSimdType))
                        m_emitter.emitSimdRegExpr(out, *callArgs[i], callArgs[i]->resolvedSimdType);
                    else
                        m_emitter.emitSimdRegExpr(out, *callArgs[i], simdType);
                }
            }
            out.push_back(')');
        };

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
        const MStringView unaryIntrinsic = MapSimdUnaryFloatIntrinsic(callee, isDouble);
        if(!unaryIntrinsic.empty()){
            out.append(unaryIntrinsic.data(), unaryIntrinsic.size());
            emitSimdCallRegArgsAndClose(expr.arguments);
            return true;
        }
        if(callee == "Lerp" && expr.arguments.size() == 3u && expr.arguments[0] && expr.arguments[1] && expr.arguments[2]){
            const Expr* scalarT = expr.arguments[2]->resolvedSimdType.empty()
                ? expr.arguments[2]
                : SimdExpressionAnalysis::TryUnwrapSimdSplatScalarArg(*expr.arguments[2], m_emitter.m_symbols);
            const b8 vectorT = !scalarT || SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*scalarT, simdType, m_emitter.m_symbols);
            if(vectorT)
                out.append(isDouble ? "::VectorDoubleLerpV(" : "::VectorLerpV(");
            else
                out.append(isDouble ? "::VectorDoubleLerp(" : "::VectorLerp(");
            m_emitter.emitSimdRegExpr(out, *expr.arguments[0], simdType);
            out.append(", ");
            m_emitter.emitSimdRegExpr(out, *expr.arguments[1], simdType);
            out.append(", ");
            if(vectorT)
                m_emitter.emitSimdRegExpr(out, *expr.arguments[2], simdType);
            else
                m_emitter.emitExpr(out, *scalarT);
            out.push_back(')');
            return true;
        }
        MStringView laneVectorSuffix;
        u32 maxVectorLanes = 4u;
#define NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC(adhdName, laneIntrinsicSuffix, maxLanes) \
        if(callee == adhdName){ laneVectorSuffix = laneIntrinsicSuffix; maxVectorLanes = maxLanes; }
#include "../export_functions.h"
#undef NWB_ADHD_EXPORT_SIMD_LANE_VECTOR_INTRINSIC
        if(!laneVectorSuffix.empty() && !expr.arguments.empty() && expr.arguments[0]){
            u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[0], &m_emitter.m_symbols);
            if(expr.arguments.size() >= 2u && expr.arguments[1] && !expr.arguments[1]->resolvedSimdType.empty()){
                const u32 effLanes1 = SimdConstantAnalysis::InferEffectiveSimdLanes(*expr.arguments[1], &m_emitter.m_symbols);
                if(effLanes1 > effLanes)
                    effLanes = effLanes1;
            }
            AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes > maxVectorLanes ? maxVectorLanes : effLanes, laneVectorSuffix);
            emitSimdCallRegArgsAndClose(expr.arguments);
            return true;
        }
        if(callee == "Mul" && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
            const MStringView lhsSimd = expr.arguments[0]->resolvedSimdType;
            const MStringView rhsSimd = expr.arguments[1]->resolvedSimdType;
            const b8 lhsIsMat = SymbolTable::IsSimdMatrixType(lhsSimd);
            const b8 rhsIsMat = SymbolTable::IsSimdMatrixType(rhsSimd);
            if(lhsIsMat && rhsIsMat){
                out.append(isDouble ? "::MatrixDoubleMultiply(" : "::MatrixMultiply(");
                m_emitter.emitSimdRegExpr(out, *expr.arguments[0], lhsSimd);
                out.append(", ");
                m_emitter.emitSimdRegExpr(out, *expr.arguments[1], rhsSimd);
                out.push_back(')');
                return true;
            }
            const Expr& vecExpr = lhsIsMat ? *expr.arguments[1] : *expr.arguments[0];
            const Expr& matExpr = lhsIsMat ? *expr.arguments[0] : *expr.arguments[1];
            u32 effLanes = 4u;
            if(vecExpr.kind == ExprKind::MemberAccess && vecExpr.isSwizzle && vecExpr.text.size() >= 2u && vecExpr.text.size() <= 4u)
                effLanes = static_cast<u32>(vecExpr.text.size());
            else
                (void)SymbolTable::IsSimdVectorType(vecExpr.resolvedSimdType, effLanes);
            AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "Transform");
            m_emitter.emitSimdRegExpr(out, vecExpr, vecExpr.resolvedSimdType);
            out.append(", ");
            m_emitter.emitSimdRegExpr(out, matExpr, matExpr.resolvedSimdType);
            out.push_back(')');
            return true;
        }
        if(callee == "Transpose" && !expr.arguments.empty() && expr.arguments[0]){
            const Expr& arg0 = *expr.arguments[0];
            const Expr* matA = nullptr;
            const Expr* matB = nullptr;
            if(
                arg0.kind == ExprKind::Call && arg0.lhs && arg0.lhs->kind == ExprKind::Identifier
                && arg0.isBuiltinCall && arg0.lhs->text == "Mul" && arg0.arguments.size() == 2u && arg0.arguments[0] && arg0.arguments[1]
                && SymbolTable::IsSimdMatrixType(arg0.arguments[0]->resolvedSimdType)
                && SymbolTable::IsSimdMatrixType(arg0.arguments[1]->resolvedSimdType)
            ){
                matA = arg0.arguments[0];
                matB = arg0.arguments[1];
            }
            else if(
                arg0.kind == ExprKind::Binary && arg0.op == TokenType::Star && arg0.lhs && arg0.rhs
                && SymbolTable::IsSimdMatrixType(arg0.lhs->resolvedSimdType)
                && SymbolTable::IsSimdMatrixType(arg0.rhs->resolvedSimdType)
            ){
                matA = arg0.lhs;
                matB = arg0.rhs;
            }
            if(matA && matB){
                out.append(isDouble ? "::MatrixDoubleMultiplyTranspose(" : "::MatrixMultiplyTranspose(");
                m_emitter.emitSimdRegExpr(out, *matA, matA->resolvedSimdType);
                out.append(", ");
                m_emitter.emitSimdRegExpr(out, *matB, matB->resolvedSimdType);
                out.push_back(')');
                return true;
            }
            out.append(isDouble ? "::MatrixDoubleTranspose(" : "::MatrixTranspose(");
            m_emitter.emitSimdRegExpr(out, arg0, simdType);
            out.push_back(')');
            return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

