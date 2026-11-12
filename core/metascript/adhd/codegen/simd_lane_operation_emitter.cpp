// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_lane_operation_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SimdLaneOperationEmitter::tryEmit()const{
    MString& out = m_output;
    const MStringView simdType = m_simdType;
    const Span<const Expr* const> arguments = m_arguments;
    const b8 isDouble = m_isDouble;
    const b8 isInt = m_isInt;
    const auto classifyActiveLaneColumn = [&](const Array<const Expr*, 4>& column, const u32 activeLanes, const Expr*& outSource)noexcept -> b8{
        outSource = nullptr;
        for(u32 lane = 0u; lane < activeLanes; ++lane){
            const Expr* source = nullptr;
            u32 sourceLane = 0u;
            MStringView registerType;
            MStringView loadFunction;
            u32 sourceLanes = 4u;
            if(!column[lane] || !SimdExpressionAnalysis::TryMatchSingleLaneAccess(column[lane], source, sourceLane) || !source || sourceLane != lane
                || !SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(*source, isDouble, registerType, loadFunction, sourceLanes)
                || lane >= sourceLanes || (outSource && !SimdExpressionAnalysis::AreSamePureSimdExpr(*outSource, *source)))
                return false;
            outSource = source;
        }
        return outSource != nullptr;
    };

    if(isInt || arguments.size() < 2u || arguments.size() > 4u || !arguments[0] || arguments[0]->kind != ExprKind::Binary)
        return false;
    const TokenType::Enum firstOp = arguments[0]->op;
    const b8 isAddSub = firstOp == TokenType::Plus || firstOp == TokenType::Minus;
    if(!isAddSub && firstOp != TokenType::Star && firstOp != TokenType::Slash)
        return false;
    Array<const Expr*, 4> lhsCol{};
    Array<const Expr*, 4> rhsCol{};
    u32 activeLanes = 0u;
    u8 plusMask = 0u;
    u8 minusMask = 0u;
    for(u32 i = 0u; i < arguments.size(); ++i){
        const Expr* arg = arguments[i];
        if(arg && SimdExpressionAnalysis::IsZeroScalarLiteral(*arg, &m_emitter.m_symbols)){
            for(u32 j = i + 1u; j < arguments.size(); ++j){
                if(!arguments[j] || !SimdExpressionAnalysis::IsZeroScalarLiteral(*arguments[j], &m_emitter.m_symbols))
                    return false;
            }
            break;
        }
        if(!arg || arg->kind != ExprKind::Binary || !arg->lhs || !arg->rhs || (isAddSub ? (arg->op != TokenType::Plus && arg->op != TokenType::Minus) : arg->op != firstOp))
            return false;
        lhsCol[i] = arg->lhs;
        rhsCol[i] = arg->rhs;
        if(arg->op == TokenType::Plus)
            plusMask |= static_cast<u8>(1u << i);
        else if(arg->op == TokenType::Minus)
            minusMask |= static_cast<u8>(1u << i);
        ++activeLanes;
    }
    if(activeLanes < 2u)
        return false;
    const Expr* emitLhs = nullptr;
    const Expr* emitRhs = nullptr;
    if(!classifyActiveLaneColumn(lhsCol, activeLanes, emitLhs) || !classifyActiveLaneColumn(rhsCol, activeLanes, emitRhs))
        return false;
    const u8 activeMask = static_cast<u8>((1u << activeLanes) - 1u);
    const MStringView maskConst = SimdInPlaceLaneMaskConstant(activeMask, isDouble);
    if(activeLanes < 4u && maskConst.empty())
        return false;
    out.append("([&]()noexcept{ const auto __a = ");
    m_emitter.emitSimdRegExpr(out, *emitLhs, simdType);
    out.append("; const auto __b = ");
    m_emitter.emitSimdRegExpr(out, *emitRhs, simdType);
    if(plusMask && minusMask){
        const auto emitMask = [&](const u8 mask){
            out.append(isDouble ? "::VectorDoubleSelectControl(" : "::VectorSelectControl(");
            for(u32 lane = 0u; lane < 4u; ++lane){
                if(lane)
                    out.append(", ");
                out.append((mask & static_cast<u8>(1u << lane)) ? "1u" : "0u");
            }
            out.push_back(')');
        };
        out.append("; const auto __plus = ");
        emitMask(plusMask);
        out.append("; const auto __minus = ");
        emitMask(minusMask);
    }
    out.append("; return ");
    const auto emitMaskedOperand = [&](const MStringView value, const MStringView mask, const b8 denominator){
        if(!mask.empty()){
            out.append(isDouble ? "::VectorDoubleSelect(" : "::VectorSelect(");
            out.append(denominator ? (isDouble ? "::s_DoubleSIMDOne, " : "::s_SIMDOne, ") : (isDouble ? "::VectorDoubleZero(), " : "::VectorZero(), "));
        }
        out.append(value.data(), value.size());
        if(!mask.empty()){
            out.append(", ");
            out.append(mask.data(), mask.size());
            out.push_back(')');
        }
    };
    const auto emitOperation = [&](const TokenType::Enum op, const MStringView mask){
        out.append(isDouble ? "::VectorDouble" : "::Vector");
        out.append(op == TokenType::Plus ? "Add(" : (op == TokenType::Minus ? "Subtract(" : (op == TokenType::Star ? "Multiply(" : "Divide(")));
        emitMaskedOperand("__a", mask, false);
        out.append(", ");
        emitMaskedOperand("__b", mask, op == TokenType::Slash);
        out.push_back(')');
    };
    if(plusMask && minusMask){
        const MStringView addMask = "__plus";
        const MStringView subMask = "__minus";
        out.append(isDouble ? "::VectorDoubleOrInt(::VectorDoubleAndInt(" : "::VectorOrInt(::VectorAndInt(");
        emitOperation(TokenType::Plus, addMask);
        out.append(", ");
        out.append(addMask.data(), addMask.size());
        out.append(isDouble ? "), ::VectorDoubleAndInt(" : "), ::VectorAndInt(");
        emitOperation(TokenType::Minus, subMask);
        out.append(", ");
        out.append(subMask.data(), subMask.size());
        out.append("))");
    }
    else{
        if(activeLanes < 4u)
            out.append(isDouble ? "::VectorDoubleAndInt(" : "::VectorAndInt(");
        emitOperation(firstOp, activeLanes < 4u ? maskConst : MStringView{});
        if(activeLanes < 4u){
            out.append(", ");
            out.append(maskConst.data(), maskConst.size());
            out.push_back(')');
        }
    }
    out.append("; }())");
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

