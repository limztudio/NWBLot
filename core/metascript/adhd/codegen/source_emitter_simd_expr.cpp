// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SourceEmitter::emitSimdVectorCtorReg(
    MString& out,
    const MStringView simdType,
    const MVector<Expr*>& arguments
)const{
    const bool isDouble = IsDoubleSimdType(simdType);
    const bool isInt = IsIntSimdType(simdType);

    if(m_symbols.isSimdMatrixType(simdType)){
        if(arguments.empty()){
            out.append(isDouble ? "::SIMDMatrixDouble{}" : "::SIMDMatrix{}");
            return;
        }
        if(arguments.size() == 1u && arguments[0] && !arguments[0]->resolvedSimdType.empty()){
            emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        out.append(isDouble ? "::MatrixDoubleSet(" : "::MatrixSet(");
        if(simdType == "f64x3x3" && arguments.size() == 9u){
            for(usize r = 0u; r < 3u; ++r){
                for(usize c = 0u; c < 3u; ++c){
                    if(r > 0u || c > 0u)
                        out.append(", ");
                    const usize argIdx = r * 3u + c;
                    if(arguments[argIdx])
                        emitExpr(out, *arguments[argIdx]);
                    else
                        out.append("0.0");
                }
                out.append(", 0.0");
            }
            out.append(", 0.0, 0.0, 0.0, 1.0)");
            return;
        }
        for(usize i = 0u; i < 16u; ++i){
            if(i > 0u)
                out.append(", ");
            if(i < arguments.size() && arguments[i])
                emitExpr(out, *arguments[i]);
            else if(i == 15u && arguments.size() == 12u)
                out.append(isDouble ? "1.0" : "1.0f");
            else
                out.append(isDouble ? "0.0" : "0.0f");
        }
        out.push_back(')');
        return;
    }

    if(arguments.empty()){
        if(isInt)
            out.append(isDouble ? "::VectorDoubleFalseInt()" : "::VectorFalseInt()");
        else
            out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
        return;
    }
    if(arguments.size() == 1u && arguments[0]){
        if(!arguments[0]->resolvedSimdType.empty()){
            emitSimdRegExpr(out, *arguments[0], simdType);
            return;
        }
        if(isInt){
            out.append(isDouble ? "::VectorDoubleReplicateInt(static_cast<u64>(" : "::VectorReplicateInt(static_cast<u32>(");
            emitExpr(out, *arguments[0]);
            out.append("))");
        }
        else{
            out.append(isDouble ? "::VectorDoubleReplicate(" : "::VectorReplicate(");
            emitExpr(out, *arguments[0]);
            out.push_back(')');
        }
        return;
    }

    if(isInt){
        out.append(isDouble ? "::VectorDoubleSetInt(" : "::VectorSetInt(");
        for(usize i = 0u; i < 4u; ++i){
            if(i > 0u)
                out.append(", ");
            out.append(isDouble ? "static_cast<u64>(" : "static_cast<u32>(");
            if(i < arguments.size() && arguments[i])
                emitExpr(out, *arguments[i]);
            else
                out.append("0u");
            out.push_back(')');
        }
        out.push_back(')');
        return;
    }

    out.append(isDouble ? "::VectorDoubleSet(" : "::VectorSet(");
    for(usize i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        if(i < arguments.size() && arguments[i])
            emitExpr(out, *arguments[i]);
        else
            out.append(isDouble ? "0.0" : "0.0f");
    }
    out.push_back(')');
}

void SourceEmitter::emitSimdRegExpr(
    MString& out,
    const Expr& expr,
    const MStringView expectedSimdType
)const{
    const MStringView simdType = !expr.resolvedSimdType.empty() ? expr.resolvedSimdType : expectedSimdType;
    const bool isDouble = IsDoubleSimdType(simdType);
    const bool isInt = IsIntSimdType(simdType);

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
        if(expr.lhs && expr.rhs){
            const Expr* mulA = nullptr;
            const Expr* mulB = nullptr;
            if(expr.op == TokenType::Plus){
                if(IsSimdMulExpr(expr.lhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.push_back(')');
                    return;
                }
                if(IsSimdMulExpr(expr.rhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    out.push_back(')');
                    return;
                }
                if(isInt)
                    out.append(isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(");
                else
                    out.append(isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Minus){
                if(IsSimdMulExpr(expr.lhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.push_back(')');
                    return;
                }
                if(IsSimdMulExpr(expr.rhs, mulA, mulB)){
                    out.append(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(");
                    emitSimdRegExpr(out, *mulA, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *mulB, simdType);
                    out.append(", ");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Star){
                if(!expr.lhs->resolvedSimdType.empty() && expr.rhs->resolvedSimdType.empty() && !isInt){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    emitSimdRegExpr(out, *expr.lhs, simdType);
                    out.append(", ");
                    emitExpr(out, *expr.rhs);
                    out.push_back(')');
                    return;
                }
                if(expr.lhs->resolvedSimdType.empty() && !expr.rhs->resolvedSimdType.empty() && !isInt){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.append(", ");
                    emitExpr(out, *expr.lhs);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Slash){
                if(!isInt && expr.lhs->resolvedSimdType.empty() && !expr.rhs->resolvedSimdType.empty()
                    && IsUnitFloatLiteral(*expr.lhs))
                {
                    out.append(isDouble ? "::VectorDoubleReciprocal(" : "::VectorReciprocal(");
                    emitSimdRegExpr(out, *expr.rhs, simdType);
                    out.push_back(')');
                    return;
                }
                out.append(isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Ampersand){
                out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Pipe){
                out.append(isDouble ? "::VectorDoubleOrInt(" : "::VectorOrInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
            if(expr.op == TokenType::Caret){
                out.append(isDouble ? "::VectorDoubleXorInt(" : "::VectorXorInt(");
                emitSimdRegExpr(out, *expr.lhs, simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.rhs, simdType);
                out.push_back(')');
                return;
            }
        }
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
            if(callee == "abs" || callee == "floor" || callee == "ceil" || callee == "round"
                || callee == "sqrt" || callee == "rsqrt" || callee == "sin" || callee == "cos" || callee == "tan")
            {
                if(callee == "abs")
                    out.append(isDouble ? "::VectorDoubleAbs(" : "::VectorAbs(");
                else if(callee == "floor")
                    out.append(isDouble ? "::VectorDoubleFloor(" : "::VectorFloor(");
                else if(callee == "ceil")
                    out.append(isDouble ? "::VectorDoubleCeiling(" : "::VectorCeiling(");
                else if(callee == "round")
                    out.append(isDouble ? "::VectorDoubleRound(" : "::VectorRound(");
                else if(callee == "sqrt")
                    out.append(isDouble ? "::VectorDoubleSqrt(" : "::VectorSqrt(");
                else if(callee == "rsqrt")
                    out.append(isDouble ? "::VectorDoubleReciprocalSqrt(" : "::VectorReciprocalSqrt(");
                else if(callee == "sin")
                    out.append(isDouble ? "::VectorDoubleSin(" : "::VectorSin(");
                else if(callee == "cos")
                    out.append(isDouble ? "::VectorDoubleCos(" : "::VectorCos(");
                else
                    out.append(isDouble ? "::VectorDoubleTan(" : "::VectorTan(");
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
                if(effLanes == 2u)
                    out.append(isDouble ? "::VectorDouble2Cross(" : "::Vector2Cross(");
                else
                    out.append(isDouble ? "::VectorDouble3Cross(" : "::Vector3Cross(");
                emitSimdRegExpr(out, *expr.arguments[0], simdType);
                out.append(", ");
                emitSimdRegExpr(out, *expr.arguments[1], simdType);
                out.push_back(')');
                return;
            }
            if(callee == "normalize" && !expr.arguments.empty() && expr.arguments[0]){
                const u32 effLanes = InferEffectiveSimdLanes(*expr.arguments[0]);
                if(effLanes == 2u)
                    out.append(isDouble ? "::VectorDouble2Normalize(" : "::Vector2Normalize(");
                else if(effLanes == 3u)
                    out.append(isDouble ? "::VectorDouble3Normalize(" : "::Vector3Normalize(");
                else
                    out.append(isDouble ? "::VectorDouble4Normalize(" : "::Vector4Normalize(");
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
                if(effLanes == 2u)
                    out.append(isDouble ? "::VectorDouble2Transform(" : "::Vector2Transform(");
                else if(effLanes == 3u)
                    out.append(isDouble ? "::VectorDouble3Transform(" : "::Vector3Transform(");
                else
                    out.append(isDouble ? "::VectorDouble4Transform(" : "::Vector4Transform(");
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

