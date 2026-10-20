// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Share of free space placed before an item to center it, or the midpoint of an extent.
inline constexpr f32 s_RectCenterFraction = 0.5f;
inline constexpr f64 s_RectCenterFractionF64 = 0.5;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Rectangle lanes are x, y, width, height; comparisons preserve scalar Min/Max ties and NaNs on every backend.
[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL IntersectRectBoundsValue(SIMDVectorSingle lhs, SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle lhsOrigin = VectorSwizzle<0, 1, 0, 1>(lhs);
    const SIMDVectorSingle rhsOrigin = VectorSwizzle<0, 1, 0, 1>(rhs);
    const SIMDVectorSingle lhsEnd = VectorAdd(lhsOrigin, VectorSwizzle<2, 3, 2, 3>(lhs));
    const SIMDVectorSingle rhsEnd = VectorAdd(rhsOrigin, VectorSwizzle<2, 3, 2, 3>(rhs));
    const SIMDVectorSingle origin = VectorSelect(rhsOrigin, lhsOrigin, VectorGreater(lhsOrigin, rhsOrigin));
    const SIMDVectorSingle end = VectorSelect(rhsEnd, lhsEnd, VectorLess(lhsEnd, rhsEnd));
    return VectorPermute<0, 1, 4, 5>(origin, end);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL IntersectRectValue(SIMDVectorSingle lhs, SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle bounds = IntersectRectBoundsValue(lhs, rhs);
    const SIMDVectorSingle origin = VectorSwizzle<0, 1, 0, 1>(bounds);
    const SIMDVectorSingle end = VectorSwizzle<2, 3, 2, 3>(bounds);
    const SIMDVectorSingle extent = VectorSubtract(end, origin);
    const SIMDVectorSingle size = VectorSelect(extent, VectorZero(), VectorGreater(VectorZero(), extent));
    return VectorPermute<0, 1, 4, 5>(origin, size);
}

// Padding lanes are left, top, right, bottom; retain the two separately rounded size subtractions.
[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL InsetRectValue(SIMDVectorSingle rectangle, SIMDVectorSingle padding)noexcept{
    const SIMDVectorSingle origin = VectorAdd(VectorSwizzle<0, 1, 0, 1>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding));
    const SIMDVectorSingle extent = VectorSubtract(VectorSubtract(VectorSwizzle<2, 3, 2, 3>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding)), VectorSwizzle<2, 3, 2, 3>(padding));
    const SIMDVectorSingle size = VectorSelect(extent, VectorZero(), VectorGreater(VectorZero(), extent));
    return VectorPermute<0, 1, 4, 5>(origin, size);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

