// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_expr{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EmitSimdMatrixRowLoopSuffix(MString& out, const MStringView matRegType, const MStringView rowExpr){
    out.append("; ");
    out.append(matRegType.data(), matRegType.size());
    out.append(" __r{}; for(usize __i = 0u; __i < 4u; ++__i) __r.v[__i] = ");
    out.append(rowExpr.data(), rowExpr.size());
    out.append("; return __r; }())");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SourceEmitter::tryEmitSimdBinaryOpReg(
    MString& out,
    const TokenType::Enum binaryOp,
    const Expr& lhs,
    const Expr& rhs,
    const MStringView simdType
)const{
    const bool isDouble = IsDoubleSimdType(simdType);
    const bool isInt = IsIntSimdType(simdType);
    const bool isMatrix = m_symbols.isSimdMatrixType(simdType);

    const auto emitBinaryRegCall = [&](const MStringView fnPrefix, const Expr& a, const Expr& b){
        out.append(fnPrefix.data(), fnPrefix.size());
        emitSimdRegExpr(out, a, simdType);
        out.append(", ");
        emitSimdRegExpr(out, b, simdType);
        out.push_back(')');
    };

    const auto emitTernaryRegCall = [&](const MStringView fnPrefix, const Expr& a, const Expr& b, const Expr& c){
        out.append(fnPrefix.data(), fnPrefix.size());
        emitSimdRegExpr(out, a, simdType);
        out.append(", ");
        emitSimdRegExpr(out, b, simdType);
        out.append(", ");
        emitSimdRegExpr(out, c, simdType);
        out.push_back(')');
    };

    if(isMatrix){
        using namespace __hidden_adhd_simd_expr;
        const MStringView matRegType = SimdRegisterCppType(simdType);
        if(binaryOp == TokenType::Plus || binaryOp == TokenType::Minus){
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __a = ");
            emitSimdRegExpr(out, lhs, simdType);
            out.append("; const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __b = ");
            emitSimdRegExpr(out, rhs, simdType);
            EmitSimdMatrixRowLoopSuffix(
                out,
                matRegType,
                binaryOp == TokenType::Plus
                    ? (isDouble ? "::VectorDoubleAdd(__a.v[__i], __b.v[__i])" : "::VectorAdd(__a.v[__i], __b.v[__i])")
                    : (isDouble ? "::VectorDoubleSubtract(__a.v[__i], __b.v[__i])" : "::VectorSubtract(__a.v[__i], __b.v[__i])")
            );
            return true;
        }
        if(binaryOp == TokenType::Star){
            if(!lhs.resolvedSimdType.empty() && !rhs.resolvedSimdType.empty()){
                emitBinaryRegCall(isDouble ? "::MatrixDoubleMultiply(" : "::MatrixMultiply(", lhs, rhs);
                return true;
            }
            const Expr& matOperand = !lhs.resolvedSimdType.empty() ? lhs : rhs;
            const Expr& scalarOperand = !lhs.resolvedSimdType.empty() ? rhs : lhs;
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __m = ");
            emitSimdRegExpr(out, matOperand, simdType);
            out.append(isDouble ? "; const f64 __s = static_cast<f64>(" : "; const f32 __s = static_cast<f32>(");
            emitExpr(out, scalarOperand);
            out.push_back(')');
            EmitSimdMatrixRowLoopSuffix(out, matRegType, isDouble ? "::VectorDoubleScale(__m.v[__i], __s)" : "::VectorScale(__m.v[__i], __s)");
            return true;
        }
        if(binaryOp == TokenType::Slash && rhs.resolvedSimdType.empty()){
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __m = ");
            emitSimdRegExpr(out, lhs, simdType);
            out.append(isDouble ? "; const ::SIMDVectorDouble __s = ::VectorDoubleReplicate(static_cast<f64>(" : "; const ::SIMDVectorSingle __s = ::VectorReplicate(static_cast<f32>(");
            emitExpr(out, rhs);
            out.append("))");
            EmitSimdMatrixRowLoopSuffix(out, matRegType, isDouble ? "::VectorDoubleDivide(__m.v[__i], __s)" : "::VectorDivide(__m.v[__i], __s)");
            return true;
        }
    }

    const Expr* mulA = nullptr;
    const Expr* mulB = nullptr;
    if(binaryOp == TokenType::Plus){
        if(IsSimdMulExpr(&lhs, mulA, mulB)){
            emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, rhs);
            return true;
        }
        if(IsSimdMulExpr(&rhs, mulA, mulB)){
            emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, lhs);
            return true;
        }
        const MStringView addPrefix = isInt
            ? (isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(")
            : (isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(");
        emitBinaryRegCall(addPrefix, lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Minus){
        if(isInt){
            out.append(isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(");
            emitSimdRegExpr(out, lhs, simdType);
            out.append(isDouble ? ", ::VectorDoubleAddInt(::VectorDoubleXorInt(" : ", ::VectorAddInt(::VectorXorInt(");
            emitSimdRegExpr(out, rhs, simdType);
            out.append(isDouble ? ", ::VectorDoubleTrueInt()), ::VectorDoubleReplicateInt(1ull)))" : ", ::VectorTrueInt()), ::VectorReplicateInt(1u)))");
            return true;
        }
        if(IsSimdMulExpr(&lhs, mulA, mulB)){
            emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(", *mulA, *mulB, rhs);
            return true;
        }
        if(IsSimdMulExpr(&rhs, mulA, mulB)){
            emitTernaryRegCall(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(", *mulA, *mulB, lhs);
            return true;
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", lhs, rhs);
        return true;
    }
    if(isInt && (binaryOp == TokenType::Star || binaryOp == TokenType::Slash
        || binaryOp == TokenType::Percent || binaryOp == TokenType::LessLess || binaryOp == TokenType::GreaterGreater))
    {
        const MStringView regType = SimdRegisterCppType(simdType);
        const MStringView laneType = IntSimdLaneType(simdType);
        const MStringView rawWordType = IntSimdRawWordType(isDouble);
        const MStringView opSym = SimdBinaryScalarOpSymbol(binaryOp);
        out.append("([&]()noexcept{ const ");
        out.append(regType.data(), regType.size());
        out.append(" __lhs = ");
        emitSimdRegExpr(out, lhs, simdType);
        out.append("; const ");
        out.append(regType.data(), regType.size());
        out.append(" __rhs = ");
        emitSimdRegExpr(out, rhs, simdType);
        out.append("; return ");
        out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
        for(u32 lane = 0u; lane < 4u; ++lane){
            if(lane > 0u)
                out.append(", ");
            out.append("static_cast<");
            out.append(rawWordType.data(), rawWordType.size());
            out.append(">(");
            AppendIntSimdLaneExtract(out, "__lhs", lane, isDouble, laneType);
            out.append(opSym.data(), opSym.size());
            AppendIntSimdLaneExtract(out, "__rhs", lane, isDouble, laneType);
            out.push_back(')');
        }
        out.append("); }())");
        return true;
    }
    if(binaryOp == TokenType::Star){
        if(!lhs.resolvedSimdType.empty() && rhs.resolvedSimdType.empty() && !isInt){
            out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
            emitSimdRegExpr(out, lhs, simdType);
            out.append(", ");
            emitExpr(out, rhs);
            out.push_back(')');
            return true;
        }
        if(lhs.resolvedSimdType.empty() && !rhs.resolvedSimdType.empty() && !isInt){
            out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
            emitSimdRegExpr(out, rhs, simdType);
            out.append(", ");
            emitExpr(out, lhs);
            out.push_back(')');
            return true;
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply(", lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Slash){
        if(!isInt && lhs.resolvedSimdType.empty() && !rhs.resolvedSimdType.empty() && IsUnitFloatLiteral(lhs)){
            out.append(isDouble ? "::VectorDoubleReciprocal(" : "::VectorReciprocal(");
            emitSimdRegExpr(out, rhs, simdType);
            out.push_back(')');
            return true;
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(", lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Ampersand){
        emitBinaryRegCall(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(", lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Pipe){
        emitBinaryRegCall(isDouble ? "::VectorDoubleOrInt(" : "::VectorOrInt(", lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Caret){
        emitBinaryRegCall(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(", lhs, rhs);
        return true;
    }
    return false;
}

void SourceEmitter::emitSimdRegExpr(
    MString& out,
    const Expr& expr,
    const MStringView expectedSimdType
)const{
    const MStringView simdType = !expr.resolvedSimdType.empty() ? expr.resolvedSimdType : expectedSimdType;
    const bool isDouble = IsDoubleSimdType(simdType);
    const bool isInt = IsIntSimdType(simdType);
    const bool isMatrix = m_symbols.isSimdMatrixType(simdType);

    if(expr.resolvedSimdType.empty() && !expectedSimdType.empty()){
        if(isInt){
            out.append(isDouble ? "::VectorDoubleReplicateInt(static_cast<u64>(" : "::VectorReplicateInt(static_cast<u32>(");
            emitExpr(out, expr);
            out.append("))");
        }
        else{
            out.append(isDouble ? "::VectorDoubleReplicate(" : "::VectorReplicate(");
            emitExpr(out, expr);
            out.push_back(')');
        }
        return;
    }

    switch(expr.kind){
    case ExprKind::Identifier:{
        const SimdVarBinding* binding = findSimdBinding(false, expr.text);
        if(binding){
            out.append(binding->regName.data(), binding->regName.size());
            return;
        }
        break;
    }
    case ExprKind::ThisMember:{
        const SimdVarBinding* binding = findSimdBinding(true, expr.text);
        if(binding){
            out.append(binding->regName.data(), binding->regName.size());
            return;
        }
        break;
    }
    case ExprKind::MemberAccess:{
        if(expr.isSwizzle && expr.lhs){
            const u32 len = static_cast<u32>(expr.text.size());
            const u32 e0 = len > 0u ? MapSwizzleCharToLane(expr.text[0]) : 0u;
            const u32 e1 = len > 1u ? MapSwizzleCharToLane(expr.text[1]) : 1u;
            const u32 e2 = len > 2u ? MapSwizzleCharToLane(expr.text[2]) : 2u;
            const u32 e3 = len > 3u ? MapSwizzleCharToLane(expr.text[3]) : 3u;
            if(e0 == 0u && e1 == 1u && e2 == 2u && e3 == 3u){
                emitSimdRegExpr(out, *expr.lhs, simdType);
                return;
            }
            out.append(isDouble ? "::VectorDoubleSwizzle<" : "::VectorSwizzle<");
            AppendAdhdUnsigned(out, e0);
            out.append("u, ");
            AppendAdhdUnsigned(out, e1);
            out.append("u, ");
            AppendAdhdUnsigned(out, e2);
            out.append("u, ");
            AppendAdhdUnsigned(out, e3);
            out.append("u>(");
            emitSimdRegExpr(out, *expr.lhs, expr.lhs->resolvedSimdType);
            out.push_back(')');
            return;
        }
        break;
    }
    case ExprKind::Unary:{
        if(expr.lhs){
            if(expr.op == TokenType::Minus){
                if(isMatrix){
                    const MStringView matRegType = SimdRegisterCppType(simdType);
                    out.append("([&]()noexcept{ const ");
                    out.append(matRegType.data(), matRegType.size());
                    out.append(" __m = ");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    __hidden_adhd_simd_expr::EmitSimdMatrixRowLoopSuffix(out, matRegType, isDouble ? "::VectorDoubleNegate(__m.v[__i])" : "::VectorNegate(__m.v[__i])");
                    return;
                }
                if(isInt){
                    out.append(isDouble ? "::VectorDoubleAddInt(::VectorDoubleXorInt(" : "::VectorAddInt(::VectorXorInt(");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    out.append(isDouble ? ", ::VectorDoubleTrueInt()), ::VectorDoubleReplicateInt(1ull))" : ", ::VectorTrueInt()), ::VectorReplicateInt(1u))");
                    return;
                }
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Tilde){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(isDouble ? ", ::VectorDoubleTrueInt())" : ", ::VectorTrueInt())");
                return;
            }
        }
        break;
    }
    case ExprKind::Binary:{
        if(expr.lhs && expr.rhs && tryEmitSimdBinaryOpReg(out, expr.op, *expr.lhs, *expr.rhs, simdType))
            return;
        break;
    }
    case ExprKind::Call:{
        if(expr.lhs && expr.lhs->kind == ExprKind::Identifier){
            const MStringView callee = expr.lhs->text;
            const MStringView baseCallee = m_symbols.resolveBaseTypeName(callee);
            u32 ctorLanes = 0u;
            if(m_symbols.isSimdVectorType(baseCallee, ctorLanes) || m_symbols.isSimdMatrixType(baseCallee)){
                emitSimdVectorCtorReg(out, simdType, expr.arguments);
                return;
            }

            if(isInt && (callee == "min" || callee == "max" || callee == "clamp" || callee == "abs")){
                const MStringView regType = SimdRegisterCppType(simdType);
                const MStringView laneType = IntSimdLaneType(simdType);
                const MStringView rawWordType = IntSimdRawWordType(isDouble);
                const MStringView scalarFn = MapBuiltinMathFunction(callee);
                out.append("([&]()noexcept{ ");
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    out.append("const ");
                    out.append(regType.data(), regType.size());
                    out.append(" __a");
                    AppendAdhdUnsigned(out, i);
                    out.append(" = ");
                    if(expr.arguments[i])
                        emitSimdRegExpr(out, *expr.arguments[i], simdType);
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
                        MString argVarName(m_arena);
                        argVarName.append("__a");
                        AppendAdhdUnsigned(argVarName, i);
                        AppendIntSimdLaneExtract(out, MStringView(argVarName.data(), argVarName.size()), lane, isDouble, laneType);
                    }
                    out.append("))");
                }
                out.append("); }())");
                return;
            }

            if(callee == "min" || callee == "max" || callee == "clamp"){
                if(callee == "min")
                    out.append(isDouble ? "::VectorDoubleMin(" : "::VectorMin(");
                else if(callee == "max")
                    out.append(isDouble ? "::VectorDoubleMax(" : "::VectorMax(");
                else
                    out.append(isDouble ? "::VectorDoubleClamp(" : "::VectorClamp(");
                for(usize i = 0u; i < expr.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(expr.arguments[i])
                        emitSimdRegExpr(out, *expr.arguments[i], simdType);
                }
                out.push_back(')');
                return;
            }
            const MStringView unaryIntrinsic = MapSimdUnaryFloatIntrinsic(callee, isDouble);
            if(!unaryIntrinsic.empty()){
                out.append(unaryIntrinsic.data(), unaryIntrinsic.size());
                if(!expr.arguments.empty() && expr.arguments[0])
                    emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.push_back(')');
                return;
            }
            if(callee == "lerp" && expr.arguments.size() == 3u && expr.arguments[0] && expr.arguments[1] && expr.arguments[2]){
                const bool vectorT = !expr.arguments[2]->resolvedSimdType.empty();
                if(vectorT)
                    out.append(isDouble ? "::VectorDoubleLerpV(" : "::VectorLerpV(");
                else
                    out.append(isDouble ? "::VectorDoubleLerp(" : "::VectorLerp(");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.arguments[1], simdType);
                out.append(", ");
                if(vectorT)
                    emitSimdRegExpr(out, *expr.arguments[2], simdType);
                else
                    emitExpr(out, *expr.arguments[2]);
                out.push_back(')');
                return;
            }
            if(callee == "cross" && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0]);
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes == 2u ? 2u : 3u, "Cross");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.arguments[1], simdType);
                out.push_back(')');
                return;
            }
            if(callee == "normalize" && !expr.arguments.empty() && expr.arguments[0]){
                const u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0]);
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "Normalize");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.push_back(')');
                return;
            }
            if(callee == "mul" && expr.arguments.size() == 2u && expr.arguments[0] && expr.arguments[1]){
                const MStringView lhsSimd = expr.arguments[0]->resolvedSimdType;
                const MStringView rhsSimd = expr.arguments[1]->resolvedSimdType;
                const bool lhsIsMat = m_symbols.isSimdMatrixType(lhsSimd);
                const bool rhsIsMat = m_symbols.isSimdMatrixType(rhsSimd);
                if(lhsIsMat && rhsIsMat){
                    out.append(isDouble ? "::MatrixDoubleMultiply(" : "::MatrixMultiply(");
                    emitSimdRegExpr(out, *expr.arguments[0], lhsSimd);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.arguments[1], rhsSimd);
                    out.push_back(')');
                    return;
                }
                const Expr& vecExpr = lhsIsMat ? *expr.arguments[1] : *expr.arguments[0];
                const Expr& matExpr = lhsIsMat ? *expr.arguments[0] : *expr.arguments[1];
                const u32 effLanes = InferEffectiveSimdLanes(vecExpr);
                AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "Transform");
                emitSimdRegExpr(out, vecExpr, vecExpr.resolvedSimdType);
                out.append(", ");
                emitSimdRegExpr(out, matExpr, matExpr.resolvedSimdType);
                out.push_back(')');
                return;
            }
            if(callee == "transpose" && !expr.arguments.empty() && expr.arguments[0]){
                const Expr& arg0 = *expr.arguments[0];
                if(arg0.kind == ExprKind::Call && arg0.lhs && arg0.lhs->kind == ExprKind::Identifier
                    && arg0.lhs->text == "mul" && arg0.arguments.size() == 2u && arg0.arguments[0] && arg0.arguments[1]
                    && m_symbols.isSimdMatrixType(arg0.arguments[0]->resolvedSimdType)
                    && m_symbols.isSimdMatrixType(arg0.arguments[1]->resolvedSimdType))
                {
                    out.append(isDouble ? "::MatrixDoubleMultiplyTranspose(" : "::MatrixMultiplyTranspose(");
                    emitSimdRegExpr(out, *arg0.arguments[0], arg0.arguments[0]->resolvedSimdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *arg0.arguments[1], arg0.arguments[1]->resolvedSimdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::MatrixDoubleTranspose(" : "::MatrixTranspose(");
                emitSimdRegExpr(out, arg0, simdType);
                out.push_back(')');
                return;
            }
            if(callee == "inverse" && !expr.arguments.empty() && expr.arguments[0]){
                out.append(isDouble ? "::MatrixDoubleInverse(nullptr, " : "::MatrixInverse(nullptr, ");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.push_back(')');
                return;
            }
        }
        break;
    }
    default:
        break;
    }

    const MStringView loadFn = SimdLoadFunctionCpp(simdType);
    out.append(loadFn.data(), loadFn.size());
    out.push_back('(');
    emitExpr(out, expr);
    out.push_back(')');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

