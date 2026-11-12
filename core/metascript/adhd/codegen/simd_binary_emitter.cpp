// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_binary_emitter.h"
#include "simd_emission_fragments.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    if(binaryOp == TokenType::Plus){
        emitBinaryRegCall(isInt ? (isDouble ? "::VectorDoubleAddInt(" : "::VectorAddInt(") : (isDouble ? "::VectorDoubleAdd(" : "::VectorAdd("), lhs, rhs);
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
        }
        else{
            emitBinaryRegCall(isDouble ? "::VectorDoubleSubtract(" : "::VectorSubtract(", lhs, rhs);
        }
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
        emitBinaryRegCall(isDouble ? "::VectorDoubleMultiply(" : "::VectorMultiply(", lhs, rhs);
        return true;
    }
    if(binaryOp == TokenType::Slash){
        emitBinaryRegCall(isDouble ? "::VectorDoubleDivide(" : "::VectorDivide(", lhs, rhs);
        return true;
    }
    if(const MStringView bitwisePrefix = MapSimdBitwiseBinaryIntrinsic(binaryOp, isDouble); !bitwisePrefix.empty()){
        emitBinaryRegCall(bitwisePrefix, lhs, rhs);
        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

