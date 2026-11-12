// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SimdExpressionAnalysis final{
public:
    [[nodiscard]] static const Expr* TryUnwrapSimdSplatScalarArg(const Expr& expr, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static b8 IsZeroScalarLiteral(const Expr& expr, const SymbolTable* symbols = nullptr)noexcept;
    [[nodiscard]] static b8 IsPureSimdAddressExpr(const Expr& expr)noexcept;
    [[nodiscard]] static b8 AreSamePureSimdExpr(const Expr& a, const Expr& b)noexcept;
    [[nodiscard]] static b8 TryMatchConstLaneIndex(const Expr& indexExpr, u32& outLane)noexcept;
    [[nodiscard]] static b8 TryMatchSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept;
    [[nodiscard]] static b8 TryMatchNegatedSingleLaneAccess(const Expr* expr, const Expr*& outBase, u32& outLane)noexcept;
    [[nodiscard]] static b8 TryMatchCastSingleLaneAccess(const Expr* expr, MStringView expectedScalarType, const SymbolTable& symbols, const Expr*& outBase, u32& outLane)noexcept;
    [[nodiscard]] static MStringView ResolveLocalVarDeclSimdType(const Stmt& stmt, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static b8 ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

