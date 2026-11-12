// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SimdConstantAnalysis final{
public:
    [[nodiscard]] static Span<const Expr* const> ExtractGlobalConstCtorArgs(const Stmt& globalConst)noexcept;
    [[nodiscard]] static const Stmt* FindLocalConstVarDecl(MStringView name, Span<const Stmt* const> localVarDecls)noexcept;
    [[nodiscard]] static b8 TryMatchIdentityRowIndex(u32& outRowIndex, const Expr& expr, const SymbolTable& symbols, Span<const Stmt* const> localVarDecls = {})noexcept;
    [[nodiscard]] static u32 InferEffectiveSimdLanes(const Expr& expr, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static const Expr& UnwrapConstScalarExpr(const Expr& expr, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsUnitFloatLiteral(const Expr& expr, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsNegativeUnitFloatLiteral(const Expr& expr, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsHalfFloatLiteral(const Expr& expr, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static MStringView TryMatchKnownSimdFloatVectorConstant(Span<const Expr* const> arguments, b8 isDouble, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsSimdZeroFloatVectorExpr(const Expr& expr, b8 isDouble, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsSimdUnitFloatVectorExpr(const Expr& expr, b8 isDouble, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsSimdNegativeUnitFloatVectorExpr(const Expr& expr, b8 isDouble, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsIdentityMatrixCtorArgs(Span<const Expr* const> arguments, const SymbolTable& symbols)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

