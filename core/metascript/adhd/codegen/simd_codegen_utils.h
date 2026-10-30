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
[[nodiscard]] MStringView IntSimdRawWordType(b8 isDouble)noexcept;
[[nodiscard]] MStringView SimdLaneCallSuffix(u32 lane)noexcept;
void AppendIntSimdLaneExtract(MString& out, MStringView varName, u32 lane, b8 isDouble, MStringView laneType);
[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept;
[[nodiscard]] b8 IsUnitFloatLiteral(const Expr& expr)noexcept;
[[nodiscard]] MStringView SimdRegisterCppType(MStringView simdType)noexcept;
[[nodiscard]] MStringView SimdLoadFunctionCpp(MStringView simdType)noexcept;
[[nodiscard]] MStringView SimdStoreFunctionCpp(MStringView simdType)noexcept;
[[nodiscard]] MStringView MapBuiltinMathFunction(MStringView name)noexcept;
[[nodiscard]] MStringView MapSimdUnaryFloatIntrinsic(MStringView callee, b8 isDouble)noexcept;
void AppendSimdLaneCountIntrinsicPrefix(MString& out, b8 isDouble, u32 effLanes, MStringView suffix);
[[nodiscard]] b8 IsSimdAssignmentOp(TokenType::Enum op)noexcept;
[[nodiscard]] TokenType::Enum MapSimdAssignToBinaryOp(TokenType::Enum op)noexcept;
[[nodiscard]] MStringView SimdBinaryScalarOpSymbol(TokenType::Enum binaryOp)noexcept;
[[nodiscard]] MStringView SimdCompoundScalarOpSymbol(TokenType::Enum op)noexcept;
[[nodiscard]] b8 IsSingleSwizzleLane(MStringView text, u32& outLane)noexcept;
[[nodiscard]] u32 MapSwizzleCharToLane(char c)noexcept;
[[nodiscard]] b8 IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept;
[[nodiscard]] MStringView ResolveLocalVarDeclSimdType(const Stmt& stmt, const SymbolTable& symbols)noexcept;
[[nodiscard]] b8 ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

