// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../sema/type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SimdVarBinding{
    bool isThisMember = false;
    bool isLocalDecl = false;
    bool isLoopElement = false;
    bool readBeforeWrite = false;
    bool written = false;
    bool unconditionallyWrittenFirst = false;
    bool needsStorageSync = false;
    bool capturedByMutLambda = false;
    MStringView name;
    MStringView simdType;
    MString regName;

    explicit SimdVarBinding(MetaArena& arena)noexcept
        : regName(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsRegisterPromotableSimdType(MStringView simdType)noexcept;
[[nodiscard]] bool IsDoubleSimdType(MStringView simdType)noexcept;
[[nodiscard]] bool IsIntSimdType(MStringView simdType)noexcept;
[[nodiscard]] bool IsSignedIntSimdType(MStringView simdType)noexcept;
[[nodiscard]] u32 InferEffectiveSimdLanes(const Expr& expr)noexcept;
[[nodiscard]] bool IsUnitFloatLiteral(const Expr& expr)noexcept;
[[nodiscard]] MStringView SimdRegisterCppType(MStringView simdType)noexcept;
[[nodiscard]] MStringView SimdLoadFunctionCpp(MStringView simdType)noexcept;
[[nodiscard]] MStringView SimdStoreFunctionCpp(MStringView simdType)noexcept;
[[nodiscard]] MStringView MapBuiltinMathFunction(MStringView name)noexcept;
[[nodiscard]] bool IsSimdAssignmentOp(TokenType::Enum op)noexcept;
[[nodiscard]] MStringView SimdCompoundScalarOpSymbol(TokenType::Enum op)noexcept;
[[nodiscard]] bool IsSingleSwizzleLane(MStringView text, u32& outLane)noexcept;
[[nodiscard]] u32 MapSwizzleCharToLane(char c)noexcept;
[[nodiscard]] bool IsSimdMulExpr(const Expr* expr, const Expr*& outMulA, const Expr*& outMulB)noexcept;

[[nodiscard]] bool ExprContainsSimdMath(const Expr* expr, const SymbolTable& symbols)noexcept;
void CollectScopedSimdBindings(
    MetaArena& arena,
    MVector<SimdVarBinding>& outBindings,
    const MVector<Stmt*>& statements,
    const SymbolTable& symbols
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

