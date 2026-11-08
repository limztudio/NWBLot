// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


NWB_ADHD_BEGIN


class SimdOptimizationAnalysis final{
public:
    [[nodiscard]] static const Expr* TryMatchSimdLengthSqVecArg(const Expr* expr, const b8 isDouble, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static const Expr* TryMatchSimdLengthVecArg(const Expr* expr, const b8 isDouble, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static b8 CanEmitScalarAsSimdVectorWithoutReplicate(const Expr& scalarExpr, const MStringView simdType, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static b8 WouldSimdMulOperandRequireReplicateInFma(const Expr& operand, const MStringView simdType, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static b8 TryMatchSimdTransform3Arg(
    const Expr& vecExpr,
    const MStringView simdType,
    const SymbolTable& symbols,
    const Expr*& outBaseVec,
    MStringView& outBaseRegType,
    MStringView& outBaseLoadFn,
    u32& outTransformLanes,
    b8& outIsNormal
)noexcept;
    [[nodiscard]] static const Expr* TryUnwrapSimdNegatedVecExpr(const Expr& expr, const MStringView simdType, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static b8 TryMatchSimdNegatedMulExpr(
    const Expr* expr,
    const MStringView simdType,
    const SymbolTable& symbols,
    const Expr*& outA,
    const Expr*& outB
)noexcept;
    [[nodiscard]] static const Expr* TryGetSimdScalarOperand(const Expr& operand, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static const Expr* TryMatchSimdSaturateCallArg(
    const Expr& callExpr,
    const MStringView simdType,
    const b8 isDouble,
    const SymbolTable& symbols
)noexcept;
    [[nodiscard]] static const Expr* TryGetTransposeSourceMatrix(
    const Expr& matExpr,
    const Span<const Stmt* const> localVarDecls
)noexcept;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

