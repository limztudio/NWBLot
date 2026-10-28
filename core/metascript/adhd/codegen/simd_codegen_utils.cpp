// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_binding_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_codegen_utils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsFloatSimdVectorType(const MStringView simdType)noexcept{
    return simdType == "f32x4"
        || simdType == "f64x2"
        || simdType == "f64x3"
        || simdType == "f64x4"
    ;
}

[[nodiscard]] constexpr bool TryMapSwizzleCharToLane(const char c, u32& outLane)noexcept{
    switch(c){
    case 'x':
    case 'r':
        outLane = 0u;
        return true;
    case 'y':
    case 'g':
        outLane = 1u;
        return true;
    case 'z':
    case 'b':
        outLane = 2u;
        return true;
    case 'w':
    case 'a':
        outLane = 3u;
        return true;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsRegisterPromotableSimdType(const MStringView simdType)noexcept{
    return simdType == "f32x4"
        || simdType == "i32x4"
        || simdType == "u32x4"
        || simdType == "f64x2"
        || simdType == "f64x3"
        || simdType == "f64x4"
        || simdType == "i64x4"
        || simdType == "u64x4"
        || simdType == "f32x3x4"
        || simdType == "f32x4x4"
        || simdType == "f64x3x3"
        || simdType == "f64x3x4"
        || simdType == "f64x4x4"
    ;
}

[[nodiscard]] bool IsDoubleSimdType(const MStringView simdType)noexcept{
    return simdType == "f64x2"
        || simdType == "f64x3"
        || simdType == "f64x4"
        || simdType == "i64x4"
        || simdType == "u64x4"
        || simdType == "f64x3x3"
        || simdType == "f64x3x4"
        || simdType == "f64x4x4"
    ;
}

[[nodiscard]] bool IsIntSimdType(const MStringView simdType)noexcept{
    return simdType == "i32x4"
        || simdType == "u32x4"
        || simdType == "i64x4"
        || simdType == "u64x4"
    ;
}

[[nodiscard]] bool IsSignedIntSimdType(const MStringView simdType)noexcept{
    return simdType == "i32x4" || simdType == "i64x4";
}

[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept{
    if(expr.kind == ExprKind::MemberAccess && expr.isSwizzle && expr.text.size() >= 2u && expr.text.size() <= 4u)
        return static_cast<u32>(expr.text.size());
    if(expr.resolvedSimdType == "f64x2")
        return 2u;
    if(expr.resolvedSimdType == "f64x3")
        return 3u;
    return 4u;
}

[[nodiscard]] bool IsUnitFloatLiteral(const Expr& expr)noexcept{
    if(expr.kind != ExprKind::FloatLiteral && expr.kind != ExprKind::DoubleLiteral)
        return false;
    return expr.text == "1.0f" || expr.text == "1.0" || expr.text == "1.f";
}

[[nodiscard]] MStringView SimdRegisterCppType(const MStringView simdType)noexcept{
    if(simdType == "f32x3x4" || simdType == "f32x4x4")
        return "::SIMDMatrix";
    if(simdType == "f64x3x3" || simdType == "f64x3x4" || simdType == "f64x4x4")
        return "::SIMDMatrixDouble";
    if(IsDoubleSimdType(simdType))
        return "::SIMDVectorDouble";
    return "::SIMDVectorSingle";
}

[[nodiscard]] MStringView SimdLoadFunctionCpp(const MStringView simdType)noexcept{
    if(simdType == "i32x4" || simdType == "u32x4")
        return "::LoadInt";
    if(simdType == "i64x4" || simdType == "u64x4")
        return "::LoadDoubleInt";
    if(IsDoubleSimdType(simdType))
        return "::LoadDouble";
    return "::LoadFloat";
}

[[nodiscard]] MStringView SimdStoreFunctionCpp(const MStringView simdType)noexcept{
    if(simdType == "i32x4" || simdType == "u32x4")
        return "::StoreInt";
    if(simdType == "i64x4" || simdType == "u64x4")
        return "::StoreDoubleInt";
    if(IsDoubleSimdType(simdType))
        return "::StoreDouble";
    return "::StoreFloat";
}

[[nodiscard]] MStringView MapBuiltinMathFunction(const MStringView name)noexcept{
    if(name == "min") return "::Min";
    if(name == "max") return "::Max";
    if(name == "abs") return "::Abs";
    if(name == "clamp") return "::Clamp";
    if(name == "floor") return "::Floor";
    if(name == "ceil") return "::Ceil";
    if(name == "round") return "::Round";
    if(name == "sqrt") return "::Sqrt";
    if(name == "rsqrt") return "::VectorReciprocalSqrt";
    if(name == "lerp") return "::VectorLerp";
    if(name == "dot") return "::Vector4Dot";
    if(name == "cross") return "::Vector3Cross";
    if(name == "length") return "::Vector4Length";
    if(name == "lengthSq") return "::Vector4LengthSq";
    if(name == "normalize") return "::Vector4Normalize";
    if(name == "mul") return "::MatrixMultiply";
    if(name == "transpose") return "::MatrixTranspose";
    if(name == "inverse") return "::MatrixInverse";
    if(name == "sin") return "::Sin";
    if(name == "cos") return "::Cos";
    if(name == "tan") return "::Tan";
    return {};
}

[[nodiscard]] bool IsSimdAssignmentOp(const TokenType::Enum op)noexcept{
    switch(op){
    case TokenType::Equal:
    case TokenType::PlusEqual:
    case TokenType::MinusEqual:
    case TokenType::StarEqual:
    case TokenType::SlashEqual:
    case TokenType::AmpersandEqual:
    case TokenType::PipeEqual:
    case TokenType::CaretEqual:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] MStringView SimdCompoundScalarOpSymbol(const TokenType::Enum op)noexcept{
    switch(op){
    case TokenType::PlusEqual: return " + ";
    case TokenType::MinusEqual: return " - ";
    case TokenType::StarEqual: return " * ";
    case TokenType::SlashEqual: return " / ";
    case TokenType::AmpersandEqual: return " & ";
    case TokenType::PipeEqual: return " | ";
    case TokenType::CaretEqual: return " ^ ";
    default: return {};
    }
}

[[nodiscard]] bool IsSingleSwizzleLane(const MStringView text, u32& outLane)noexcept{
    return text.size() == 1u && __hidden_adhd_simd_codegen_utils::TryMapSwizzleCharToLane(text[0], outLane);
}

[[nodiscard]] u32 MapSwizzleCharToLane(const char c)noexcept{
    u32 lane = 3u;
    (void)__hidden_adhd_simd_codegen_utils::TryMapSwizzleCharToLane(c, lane);
    return lane;
}

[[nodiscard]] bool IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept{
    if(!expr || expr->kind != ExprKind::Binary || expr->op != TokenType::Star || !expr->lhs || !expr->rhs)
        return false;
    if(expr->resolvedSimdType.empty() || !__hidden_adhd_simd_codegen_utils::IsFloatSimdVectorType(expr->resolvedSimdType))
        return false;
    outMulA = expr->lhs;
    outMulB = expr->rhs;
    return true;
}

[[nodiscard]] bool ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept{
    if(!expr)
        return false;

    if(expr->kind == ExprKind::MemberAccess && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType)){
        u32 lane = 0u;
        if(expr->isSwizzle || IsSingleSwizzleLane(expr->text, lane))
            return true;
    }

    if(expr->kind == ExprKind::Index && expr->lhs && IsRegisterPromotableSimdType(expr->lhs->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Unary && IsRegisterPromotableSimdType(expr->resolvedSimdType))
        return true;

    if(expr->kind == ExprKind::Binary && IsRegisterPromotableSimdType(expr->resolvedSimdType)){
        if(expr->op != TokenType::Equal)
            return true;
        if(ExprContainsSimdMath(expr->rhs, symbols))
            return true;
    }

    if(expr->kind == ExprKind::Binary && IsSimdAssignmentOp(expr->op) && expr->lhs){
        u32 lane = 0u;
        if(expr->lhs->kind == ExprKind::MemberAccess && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType)
            && IsSingleSwizzleLane(expr->lhs->text, lane))
        {
            return true;
        }
        if(expr->lhs->kind == ExprKind::Index && expr->lhs->lhs
            && IsRegisterPromotableSimdType(expr->lhs->lhs->resolvedSimdType))
        {
            return true;
        }
    }

    if(expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier){
        const MStringView callee = expr->lhs->text;
        const MStringView baseCallee = symbols.resolveBaseTypeName(callee);
        if(symbols.isSimdType(baseCallee) && IsRegisterPromotableSimdType(expr->resolvedSimdType))
            return true;
        if(!MapBuiltinMathFunction(callee).empty()){
            if(IsRegisterPromotableSimdType(expr->resolvedSimdType))
                return true;
            for(const Expr* arg : expr->arguments){
                if(arg && IsRegisterPromotableSimdType(arg->resolvedSimdType))
                    return true;
            }
        }
    }

    if(ExprContainsSimdMath(expr->lhs, symbols) || ExprContainsSimdMath(expr->rhs, symbols))
        return true;

    for(const Expr* arg : expr->arguments){
        if(ExprContainsSimdMath(arg, symbols))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
