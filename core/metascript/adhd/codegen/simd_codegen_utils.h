// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsRegisterPromotableSimdType(MStringView simdType)noexcept;
[[nodiscard]] b8 IsDoubleSimdType(MStringView simdType)noexcept;
[[nodiscard]] b8 IsIntSimdType(MStringView simdType)noexcept;
[[nodiscard]] b8 IsSignedIntSimdType(MStringView simdType)noexcept;
[[nodiscard]] MStringView IntSimdLaneType(MStringView simdType)noexcept;
[[nodiscard]] constexpr MStringView IntSimdRawWordType(const b8 isDouble)noexcept{ return isDouble ? "u64" : "u32"; }
[[nodiscard]] MStringView SimdLaneCallSuffix(u32 lane)noexcept;
[[nodiscard]] MStringView SimdSplatIntrinsic(u32 lane, b8 isDouble)noexcept;
[[nodiscard]] MStringView SimdInPlaceLaneMaskConstant(u8 laneMask, b8 isDouble)noexcept;
[[nodiscard]] MStringView SimdComponentMaskConstant(u8 laneMask, b8 isDouble)noexcept;
void AppendIntSimdLaneExtract(MString& out, MStringView varName, u32 lane, b8 isDouble, MStringView laneType);
[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept;
[[nodiscard]] inline b8 IsUnitFloatLiteral(const Expr& expr)noexcept{
    return (expr.kind == ExprKind::FloatLiteral || expr.kind == ExprKind::DoubleLiteral)
        && (expr.text == "1.0f" || expr.text == "1.0" || expr.text == "1.f" || expr.text == "1.0F" || expr.text == "1.F");
}
[[nodiscard]] inline b8 IsNegativeUnitFloatLiteral(const Expr& expr)noexcept{
    if(expr.kind == ExprKind::Unary && !expr.isPostfix && expr.op == TokenType::Minus && expr.lhs)
        return IsUnitFloatLiteral(*expr.lhs);
    return (expr.kind == ExprKind::FloatLiteral || expr.kind == ExprKind::DoubleLiteral)
        && (expr.text == "-1.0f" || expr.text == "-1.0" || expr.text == "-1.f" || expr.text == "-1.0F" || expr.text == "-1.F");
}
[[nodiscard]] inline b8 IsHalfFloatLiteral(const Expr& expr)noexcept{
    return (expr.kind == ExprKind::FloatLiteral || expr.kind == ExprKind::DoubleLiteral)
        && (expr.text == "0.5f" || expr.text == "0.5" || expr.text == ".5f" || expr.text == ".5" || expr.text == "0.5F" || expr.text == ".5F");
}
[[nodiscard]] MStringView TryMatchKnownSimdFloatVectorConstant(Span<const Expr* const> arguments, b8 isDouble)noexcept;
[[nodiscard]] b8 IsIdentityMatrixCtorArgs(Span<const Expr* const> arguments, const SymbolTable& symbols)noexcept;
[[nodiscard]] MStringView SimdRegisterCppType(MStringView simdType)noexcept;
[[nodiscard]] MStringView SimdLoadFunctionCpp(MStringView simdType)noexcept;
[[nodiscard]] MStringView SimdStoreFunctionCpp(MStringView simdType)noexcept;
[[nodiscard]] MStringView MapBuiltinMathFunction(MStringView name)noexcept;
[[nodiscard]] MStringView MapSimdRegisterCall(MStringView callee)noexcept;
[[nodiscard]] MStringView MapSimdScalarReductionSuffix(MStringView callee)noexcept;
[[nodiscard]] b8 IsSimdScalarReductionFunction(MStringView callee)noexcept;
[[nodiscard]] MStringView MapSimdUnaryFloatIntrinsic(MStringView callee, b8 isDouble)noexcept;
void AppendSimdLaneCountIntrinsicPrefix(MString& out, b8 isDouble, u32 effLanes, MStringView suffix);
[[nodiscard]] TokenType::Enum MapSimdAssignToBinaryOp(TokenType::Enum op)noexcept;
[[nodiscard]] MStringView SimdBinaryScalarOpSymbol(TokenType::Enum binaryOp)noexcept;
[[nodiscard]] b8 IsSimdIntLaneFallbackBinaryOp(TokenType::Enum binaryOp)noexcept;
[[nodiscard]] MStringView MapSimdBitwiseBinaryIntrinsic(TokenType::Enum binaryOp, b8 isDouble)noexcept;
[[nodiscard]] constexpr b8 IsSingleSwizzleLane(const MStringView text, u32& outLane)noexcept{
    return text.size() == 1u && TryMapSwizzleCharToLane(text[0], outLane);
}
[[nodiscard]] constexpr u32 MapSwizzleCharToLane(const char c)noexcept{
    u32 lane = 3u;
    return TryMapSwizzleCharToLane(c, lane) ? lane : 3u;
}
[[nodiscard]] b8 IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept;
[[nodiscard]] const Expr* TryUnwrapSimdSplatScalarArg(const Expr& expr, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 IsZeroScalarLiteral(const Expr& expr)noexcept;
[[nodiscard]] b8 IsPureSimdAddressExpr(const Expr& expr)noexcept;
[[nodiscard]] b8 AreSamePureSimdExpr(const Expr& a, const Expr& b)noexcept;
[[nodiscard]] b8 TryMatchSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept;
[[nodiscard]] b8 TryMatchNegatedSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept;
[[nodiscard]] b8 TryMatchCastSingleLaneAccess(const Expr* expr, MStringView expectedScalarType, const SymbolTable& symbols, const Expr*& outBase, u32& outLane)noexcept;
[[nodiscard]] MStringView ResolveLocalVarDeclSimdType(const Stmt& stmt, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
