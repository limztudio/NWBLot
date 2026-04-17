// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "not_null.h"
#include "matrix_math.h"
#include "detail/source_sh_math/source_sh_math.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SH{


static constexpr usize s_MinOrder = SourceShMathInternal::SH_MIN_ORDER;
static constexpr usize s_MaxOrder = SourceShMathInternal::SH_MAX_ORDER;


[[nodiscard]] NWB_INLINE bool IsValidOrder(const usize order)noexcept{
    return order >= s_MinOrder && order <= s_MaxOrder;
}

[[nodiscard]] NWB_INLINE usize CoefficientCount(const usize order)noexcept{
    return order * order;
}


[[nodiscard]] NWB_INLINE bool EvalDirection(NotNull<f32*> result, const usize order, VectorArg dir)noexcept{
    return SourceShMathInternal::SHEvalDirection(result.get(), order, dir) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Rotate(
    NotNull<f32*> result,
    const usize order,
    MatrixArg rotMatrix,
    NotNull<const f32*> input
)noexcept{
    return SourceShMathInternal::SHRotate(result.get(), order, rotMatrix.nativeMatrix(), input.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool RotateZ(
    NotNull<f32*> result,
    const usize order,
    const f32 angle,
    NotNull<const f32*> input
)noexcept{
    return SourceShMathInternal::SHRotateZ(result.get(), order, angle, input.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Add(
    NotNull<f32*> result,
    const usize order,
    NotNull<const f32*> inputA,
    NotNull<const f32*> inputB
)noexcept{
    return SourceShMathInternal::SHAdd(result.get(), order, inputA.get(), inputB.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Scale(
    NotNull<f32*> result,
    const usize order,
    NotNull<const f32*> input,
    const f32 scale
)noexcept{
    return SourceShMathInternal::SHScale(result.get(), order, input.get(), scale) != nullptr;
}

[[nodiscard]] NWB_INLINE f32 Dot(const usize order, NotNull<const f32*> inputA, NotNull<const f32*> inputB)noexcept{
    return SourceShMathInternal::SHDot(order, inputA.get(), inputB.get());
}

[[nodiscard]] NWB_INLINE bool Multiply(
    NotNull<f32*> result,
    const usize order,
    NotNull<const f32*> inputF,
    NotNull<const f32*> inputG
)noexcept{
    return SourceShMathInternal::SHMultiply(result.get(), order, inputF.get(), inputG.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Multiply2(NotNull<f32*> result, NotNull<const f32*> inputF, NotNull<const f32*> inputG)noexcept{
    return SourceShMathInternal::SHMultiply2(result.get(), inputF.get(), inputG.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Multiply3(NotNull<f32*> result, NotNull<const f32*> inputF, NotNull<const f32*> inputG)noexcept{
    return SourceShMathInternal::SHMultiply3(result.get(), inputF.get(), inputG.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Multiply4(NotNull<f32*> result, NotNull<const f32*> inputF, NotNull<const f32*> inputG)noexcept{
    return SourceShMathInternal::SHMultiply4(result.get(), inputF.get(), inputG.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Multiply5(NotNull<f32*> result, NotNull<const f32*> inputF, NotNull<const f32*> inputG)noexcept{
    return SourceShMathInternal::SHMultiply5(result.get(), inputF.get(), inputG.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool Multiply6(NotNull<f32*> result, NotNull<const f32*> inputF, NotNull<const f32*> inputG)noexcept{
    return SourceShMathInternal::SHMultiply6(result.get(), inputF.get(), inputG.get()) != nullptr;
}

[[nodiscard]] NWB_INLINE bool EvalDirectionalLight(
    const usize order,
    VectorArg dir,
    VectorArg color,
    NotNull<f32*> resultR,
    f32* resultG = nullptr,
    f32* resultB = nullptr
)noexcept{
    return SourceShMathInternal::SHEvalDirectionalLight(order, dir, color, resultR.get(), resultG, resultB);
}

[[nodiscard]] NWB_INLINE bool EvalSphericalLight(
    const usize order,
    VectorArg position,
    const f32 radius,
    VectorArg color,
    NotNull<f32*> resultR,
    f32* resultG = nullptr,
    f32* resultB = nullptr
)noexcept{
    return SourceShMathInternal::SHEvalSphericalLight(order, position, radius, color, resultR.get(), resultG, resultB);
}

[[nodiscard]] NWB_INLINE bool EvalConeLight(
    const usize order,
    VectorArg dir,
    const f32 radius,
    VectorArg color,
    NotNull<f32*> resultR,
    f32* resultG = nullptr,
    f32* resultB = nullptr
)noexcept{
    return SourceShMathInternal::SHEvalConeLight(order, dir, radius, color, resultR.get(), resultG, resultB);
}

[[nodiscard]] NWB_INLINE bool EvalHemisphereLight(
    const usize order,
    VectorArg dir,
    VectorArg topColor,
    VectorArg bottomColor,
    NotNull<f32*> resultR,
    f32* resultG = nullptr,
    f32* resultB = nullptr
)noexcept{
    return SourceShMathInternal::SHEvalHemisphereLight(order, dir, topColor, bottomColor, resultR.get(), resultG, resultB);
}


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

