// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_binary_emitter.h"
#include "simd_emission_fragments.h"
#include "simd_optimization_analysis.h"

NWB_ADHD_BEGIN

b8 SimdBinaryEmitter::tryEmit(MString& out, const TokenType::Enum binaryOp, const Expr& lhs, const Expr& rhs, const MStringView simdType)const{

    const b8 isDouble = IsDoubleSimdType(simdType);
    const b8 isInt = IsIntSimdType(simdType);
    const b8 isMatrix = SymbolTable::IsSimdMatrixType(simdType);

    const auto emitBinaryRegCall = [&](const MStringView fnPrefix, const Expr& a, const Expr& b){
        out.append(fnPrefix.data(), fnPrefix.size());
        m_emitter.emitSimdRegExpr(out, a, simdType);
        out.append(", ");
        m_emitter.emitSimdRegExpr(out, b, simdType);
        out.push_back(')');
    };

    const auto emitTernaryRegCall = [&](const MStringView fnPrefix, const Expr& a, const Expr& b, const Expr& c){
        out.append(fnPrefix.data(), fnPrefix.size());
        m_emitter.emitSimdRegExpr(out, a, simdType);
        out.append(", ");
        m_emitter.emitSimdRegExpr(out, b, simdType);
        out.append(", ");
        m_emitter.emitSimdRegExpr(out, c, simdType);
        out.push_back(')');
    };

    if(isMatrix){
        const MStringView matRegType = SimdRegisterCppType(simdType);
        if(binaryOp == TokenType::Plus || binaryOp == TokenType::Minus){
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __a = ");
            m_emitter.emitSimdRegExpr(out, lhs, simdType);
            out.append("; const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __b = ");
            m_emitter.emitSimdRegExpr(out, rhs, simdType);
            SimdEmissionFragments::EmitSimdMatrixBinaryRowInitSuffix(
                out,
                matRegType,
                binaryOp == TokenType::Plus
                    ? (isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(")
                    : (isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(")
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
            m_emitter.emitSimdRegExpr(out, matOperand, simdType);
            out.append(isDouble ? "; const f64 __s = static_cast<f64>(" : "; const f32 __s = static_cast<f32>(");
            m_emitter.emitExpr(out, scalarOperand);
            out.push_back(')');
            SimdEmissionFragments::EmitSimdMatrixUnaryRowInitSuffix(out, matRegType, isDouble ? "::VectorDoubleScale(" : "::VectorScale(", ", __s)");
            return true;
        }
        if(binaryOp == TokenType::Slash && rhs.resolvedSimdType.empty()){
            out.append("([&]()noexcept{ const ");
            out.append(matRegType.data(), matRegType.size());
            out.append(" __m = ");
            m_emitter.emitSimdRegExpr(out, lhs, simdType);
            out.append(isDouble ? "; const ::SIMDVectorDouble __s = ::VectorDoubleReplicate(static_cast<f64>(" : "; const ::SIMDVectorSingle __s = ::VectorReplicate(static_cast<f32>(");
            m_emitter.emitExpr(out, rhs);
            out.append("))");
            SimdEmissionFragments::EmitSimdMatrixUnaryRowInitSuffix(out, matRegType, isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(", ", __s)");
            return true;
        }
    }

    const Expr* mulA = nullptr;
    const Expr* mulB = nullptr;
    if(!isInt && (binaryOp == TokenType::Plus || binaryOp == TokenType::Minus || binaryOp == TokenType::Star || binaryOp == TokenType::Slash)){
        const Expr* lhsScalar = SimdOptimizationAnalysis::TryGetSimdScalarOperand(lhs, m_emitter.m_symbols);
        const Expr* rhsScalar = SimdOptimizationAnalysis::TryGetSimdScalarOperand(rhs, m_emitter.m_symbols);
        if(
            lhsScalar
            && rhsScalar
            && !SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*lhsScalar, simdType, m_emitter.m_symbols)
            && !SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*rhsScalar, simdType, m_emitter.m_symbols)
        ){
            const b8 skipForIdentityOrZero = ((binaryOp == TokenType::Plus || binaryOp == TokenType::Minus || binaryOp == TokenType::Star) && (SimdExpressionAnalysis::IsZeroScalarLiteral(*lhsScalar, &m_emitter.m_symbols) || SimdExpressionAnalysis::IsZeroScalarLiteral(*rhsScalar, &m_emitter.m_symbols)))
                || (binaryOp == TokenType::Slash && SimdExpressionAnalysis::IsZeroScalarLiteral(*lhsScalar, &m_emitter.m_symbols))
                || (binaryOp == TokenType::Star && (SimdConstantAnalysis::IsUnitFloatLiteral(*lhsScalar, &m_emitter.m_symbols) || SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*lhsScalar, &m_emitter.m_symbols) || SimdConstantAnalysis::IsUnitFloatLiteral(*rhsScalar, &m_emitter.m_symbols) || SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*rhsScalar, &m_emitter.m_symbols)))
                || (binaryOp == TokenType::Slash && (SimdConstantAnalysis::IsUnitFloatLiteral(*rhsScalar, &m_emitter.m_symbols) || SimdConstantAnalysis::IsNegativeUnitFloatLiteral(*rhsScalar, &m_emitter.m_symbols)));
            if(!skipForIdentityOrZero){
                const MStringView opSym = SimdBinaryScalarOpSymbol(binaryOp);
                out.append(isDouble ? "::VectorDoubleReplicate((" : "::VectorReplicate((");
                m_emitter.emitExpr(out, *lhsScalar);
                out.append(opSym.data(), opSym.size());
                m_emitter.emitExpr(out, *rhsScalar);
                out.append("))");
                return true;
            }
        }
    }
    if(binaryOp == TokenType::Plus){
        if(!isInt){
            if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(rhs, isDouble, &m_emitter.m_symbols)){
                m_emitter.emitSimdRegExpr(out, lhs, simdType);
                return true;
            }
            if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols)){
                m_emitter.emitSimdRegExpr(out, rhs, simdType);
                return true;
            }
            const Expr* negMulA = nullptr;
            const Expr* negMulB = nullptr;
            if(SimdOptimizationAnalysis::TryMatchSimdNegatedMulExpr(&lhs, simdType, negMulA, negMulB, m_emitter.m_symbols)){
                emitTernaryRegCall(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(", *negMulA, *negMulB, rhs);
                return true;
            }
            if(SimdOptimizationAnalysis::TryMatchSimdNegatedMulExpr(&rhs, simdType, negMulA, negMulB, m_emitter.m_symbols)){
                emitTernaryRegCall(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(", *negMulA, *negMulB, lhs);
                return true;
            }
            const Expr* rhsMulA = nullptr;
            const Expr* rhsMulB = nullptr;
            const b8 lhsIsMul = SimdExpressionAnalysis::IsSimdMulExpr(&lhs, mulA, mulB, &m_emitter.m_symbols);
            const b8 rhsIsMul = SimdExpressionAnalysis::IsSimdMulExpr(&rhs, rhsMulA, rhsMulB, &m_emitter.m_symbols);
            if(lhsIsMul){
                const Expr* negA = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulA, simdType, m_emitter.m_symbols);
                const Expr* negB = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulB, simdType, m_emitter.m_symbols);
                if(negA && negB){
                    mulA = negA;
                    mulB = negB;
                }
            }
            if(rhsIsMul){
                const Expr* negA = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*rhsMulA, simdType, m_emitter.m_symbols);
                const Expr* negB = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*rhsMulB, simdType, m_emitter.m_symbols);
                if(negA && negB){
                    rhsMulA = negA;
                    rhsMulB = negB;
                }
            }
            if(lhsIsMul){
                if(const Expr* negRhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_emitter.m_symbols)){
                    emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(", *mulA, *mulB, *negRhs);
                    return true;
                }
            }
            if(rhsIsMul){
                if(const Expr* negLhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_emitter.m_symbols)){
                    emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(", *rhsMulA, *rhsMulB, *negLhs);
                    return true;
                }
            }
            if(lhsIsMul && rhsIsMul){
                const b8 lhsNeedsRep = SimdOptimizationAnalysis::WouldSimdMulOperandRequireReplicateInFma(*mulA, simdType, m_emitter.m_symbols)
                    || SimdOptimizationAnalysis::WouldSimdMulOperandRequireReplicateInFma(*mulB, simdType, m_emitter.m_symbols);
                const b8 rhsNeedsRep = SimdOptimizationAnalysis::WouldSimdMulOperandRequireReplicateInFma(*rhsMulA, simdType, m_emitter.m_symbols)
                    || SimdOptimizationAnalysis::WouldSimdMulOperandRequireReplicateInFma(*rhsMulB, simdType, m_emitter.m_symbols);
                if(lhsNeedsRep && !rhsNeedsRep){
                    emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *rhsMulA, *rhsMulB, lhs);
                    return true;
                }
            }
            if(lhsIsMul){
                emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, rhs);
                return true;
            }
            if(rhsIsMul){
                emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *rhsMulA, *rhsMulB, lhs);
                return true;
            }
            if(const Expr* negRhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_emitter.m_symbols)){
                emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", lhs, *negRhs);
                return true;
            }
            if(const Expr* negLhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_emitter.m_symbols)){
                emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", rhs, *negLhs);
                return true;
            }
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
            m_emitter.emitSimdRegExpr(out, lhs, simdType);
            out.append(", ");
            SimdEmissionFragments::EmitSimdIntTwosComplementNegatePrefix(out, isDouble);
            m_emitter.emitSimdRegExpr(out, rhs, simdType);
            SimdEmissionFragments::EmitSimdIntTwosComplementNegateSuffix(out, isDouble);
            out.push_back(')');
            return true;
        }
        if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(rhs, isDouble, &m_emitter.m_symbols)){
            m_emitter.emitSimdRegExpr(out, lhs, simdType);
            return true;
        }
        if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols)){
            if(const Expr* negRhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_emitter.m_symbols)){
                m_emitter.emitSimdRegExpr(out, *negRhs, simdType);
                return true;
            }
            out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
            m_emitter.emitSimdRegExpr(out, rhs, simdType);
            out.push_back(')');
            return true;
        }
        if(SimdOptimizationAnalysis::TryMatchSimdNegatedMulExpr(&rhs, simdType, mulA, mulB, m_emitter.m_symbols)){
            emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, lhs);
            return true;
        }
        if(SimdExpressionAnalysis::IsSimdMulExpr(&lhs, mulA, mulB, &m_emitter.m_symbols) && !SimdOptimizationAnalysis::TryMatchSimdNegatedMulExpr(&lhs, simdType, mulA, mulB, m_emitter.m_symbols)){
            const Expr* negA = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulA, simdType, m_emitter.m_symbols);
            const Expr* negB = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulB, simdType, m_emitter.m_symbols);
            if(negA && negB){
                mulA = negA;
                mulB = negB;
            }
            if(const Expr* negRhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_emitter.m_symbols)){
                emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplyAdd(" : "::VectorMultiplyAdd(", *mulA, *mulB, *negRhs);
                return true;
            }
            emitTernaryRegCall(isDouble ? "::VectorDoubleMultiplySubtract(" : "::VectorMultiplySubtract(", *mulA, *mulB, rhs);
            return true;
        }
        if(SimdExpressionAnalysis::IsSimdMulExpr(&rhs, mulA, mulB, &m_emitter.m_symbols)){
            const Expr* negA = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulA, simdType, m_emitter.m_symbols);
            const Expr* negB = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(*mulB, simdType, m_emitter.m_symbols);
            if(negA && negB){
                mulA = negA;
                mulB = negB;
            }
            emitTernaryRegCall(isDouble ? "::VectorDoubleNegativeMultiplySubtract(" : "::VectorNegativeMultiplySubtract(", *mulA, *mulB, lhs);
            return true;
        }
        if(const Expr* negRhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_emitter.m_symbols)){
            emitBinaryRegCall(isDouble ? "::VectorDoubleAdd(" : "::VectorAdd(", lhs, *negRhs);
            return true;
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", lhs, rhs);
        return true;
    }
    if(isInt && IsSimdIntLaneFallbackBinaryOp(binaryOp)){
        const MStringView regType = SimdRegisterCppType(simdType);
        const MStringView laneType = IntSimdLaneType(simdType);
        const MStringView rawWordType = IntSimdRawWordType(isDouble);
        const MStringView opSym = SimdBinaryScalarOpSymbol(binaryOp);
        out.append("([&]()noexcept{ const ");
        out.append(regType.data(), regType.size());
        out.append(" __lhs = ");
        m_emitter.emitSimdRegExpr(out, lhs, simdType);
        out.append("; const ");
        out.append(regType.data(), regType.size());
        out.append(" __rhs = ");
        m_emitter.emitSimdRegExpr(out, rhs, simdType);
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
        if(!isInt){
            const Expr* negLhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_emitter.m_symbols);
            const Expr* negRhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_emitter.m_symbols);
            if(negLhs && negRhs)
                return tryEmit(out, binaryOp, *negLhs, *negRhs, simdType);
            if(!lhs.resolvedSimdType.empty() || !lhs.resolvedUnalignedType.empty()){
                if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(rhs, isDouble, &m_emitter.m_symbols) && SimdExpressionAnalysis::IsPureSimdAddressExpr(lhs)){
                    out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
                    return true;
                }
                if(SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(rhs, isDouble, &m_emitter.m_symbols)){
                    m_emitter.emitSimdRegExpr(out, lhs, simdType);
                    return true;
                }
                if(SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(rhs, isDouble, &m_emitter.m_symbols)){
                    out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                    m_emitter.emitSimdRegExpr(out, lhs, simdType);
                    out.push_back(')');
                    return true;
                }
                const Expr* rhsScalar = SimdOptimizationAnalysis::TryGetSimdScalarOperand(rhs, m_emitter.m_symbols);
                if(rhsScalar && !SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*rhsScalar, simdType, m_emitter.m_symbols)){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    m_emitter.emitSimdRegExpr(out, lhs, simdType);
                    out.append(", ");
                    m_emitter.emitExpr(out, *rhsScalar);
                    out.push_back(')');
                    return true;
                }
            }
            if(!rhs.resolvedSimdType.empty() || !rhs.resolvedUnalignedType.empty()){
                if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols) && SimdExpressionAnalysis::IsPureSimdAddressExpr(rhs)){
                    out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
                    return true;
                }
                if(SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols)){
                    m_emitter.emitSimdRegExpr(out, rhs, simdType);
                    return true;
                }
                if(SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols)){
                    out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                    m_emitter.emitSimdRegExpr(out, rhs, simdType);
                    out.push_back(')');
                    return true;
                }
                const Expr* lhsScalar = SimdOptimizationAnalysis::TryGetSimdScalarOperand(lhs, m_emitter.m_symbols);
                if(lhsScalar && !SimdOptimizationAnalysis::CanEmitScalarAsSimdVectorWithoutReplicate(*lhsScalar, simdType, m_emitter.m_symbols)){
                    out.append(isDouble ? "::VectorDoubleScale(" : "::VectorScale(");
                    m_emitter.emitSimdRegExpr(out, rhs, simdType);
                    out.append(", ");
                    m_emitter.emitExpr(out, *lhsScalar);
                    out.push_back(')');
                    return true;
                }
            }
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply(", lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Slash){
        if(!isInt){
            const Expr* negLhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(lhs, simdType, m_emitter.m_symbols);
            const Expr* negRhs = SimdOptimizationAnalysis::TryUnwrapSimdNegatedVecExpr(rhs, simdType, m_emitter.m_symbols);
            if(negLhs && negRhs)
                return tryEmit(out, binaryOp, *negLhs, *negRhs, simdType);
        }
        if(!isInt && (!lhs.resolvedSimdType.empty() || !lhs.resolvedUnalignedType.empty())){
            if(SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(rhs, isDouble, &m_emitter.m_symbols)){
                m_emitter.emitSimdRegExpr(out, lhs, simdType);
                return true;
            }
            if(SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(rhs, isDouble, &m_emitter.m_symbols)){
                out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                m_emitter.emitSimdRegExpr(out, lhs, simdType);
                out.push_back(')');
                return true;
            }
        }
        if(!isInt && (!rhs.resolvedSimdType.empty() || !rhs.resolvedUnalignedType.empty())){
            if(SimdConstantAnalysis::IsSimdZeroFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols) && SimdExpressionAnalysis::IsPureSimdAddressExpr(rhs)){
                out.append(isDouble ? "::VectorDoubleZero()" : "::VectorZero()");
                return true;
            }
            const b8 lhsIsUnit = SimdConstantAnalysis::IsSimdUnitFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols);
            const b8 lhsIsNegUnit = !lhsIsUnit && SimdConstantAnalysis::IsSimdNegativeUnitFloatVectorExpr(lhs, isDouble, &m_emitter.m_symbols);
            if(lhsIsUnit || lhsIsNegUnit){
                if(lhsIsNegUnit)
                    out.append(isDouble ? "::VectorDoubleNegate(" : "::VectorNegate(");
                if(const Expr* lenVec = SimdOptimizationAnalysis::TryMatchSimdLengthVecArg(&rhs, isDouble, m_emitter.m_symbols)){
                    const u32 effLanes = SimdConstantAnalysis::InferEffectiveSimdLanes(*lenVec, &m_emitter.m_symbols);
                    AppendSimdLaneCountIntrinsicPrefix(out, isDouble, effLanes, "ReciprocalLength");
                    m_emitter.emitSimdRegExpr(out, *lenVec, lenVec->resolvedSimdType);
                    out.push_back(')');
                }
                else if(
                    rhs.kind == ExprKind::Call
                    && rhs.isBuiltinCall
                    && rhs.lhs
                    && rhs.lhs->kind == ExprKind::Identifier
                    && rhs.lhs->text == "sqrt"
                    && rhs.arguments.size() == 1u
                    && rhs.arguments[0]
                    && rhs.arguments[0]->resolvedSimdType == simdType
                ){
                    out.append(isDouble ? "::VectorDoubleReciprocalSqrt(" : "::VectorReciprocalSqrt(");
                    m_emitter.emitSimdRegExpr(out, *rhs.arguments[0], simdType);
                    out.push_back(')');
                }
                else{
                    out.append(isDouble ? "::VectorDoubleReciprocal(" : "::VectorReciprocal(");
                    m_emitter.emitSimdRegExpr(out, rhs, simdType);
                    out.push_back(')');
                }
                if(lhsIsNegUnit)
                    out.push_back(')');
                return true;
            }
        }
        emitBinaryRegCall(isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(", lhs, rhs);
        return true;
    }
    if(const MStringView bitwisePrefix = MapSimdBitwiseBinaryIntrinsic(binaryOp, isDouble); !bitwisePrefix.empty()){
        emitBinaryRegCall(bitwisePrefix, lhs, rhs);
        return true;
    }
    return false;
}

NWB_ADHD_END
