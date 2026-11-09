// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Rectangle lanes are x, y, width, height; comparisons preserve scalar Min/Max ties and NaNs on every backend.
[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL IntersectRectBoundsValue(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle lhsEnd = VectorAdd(VectorSwizzle<0, 1, 0, 1>(lhs), VectorSwizzle<2, 3, 2, 3>(lhs));
    const SIMDVectorSingle rhsEnd = VectorAdd(VectorSwizzle<0, 1, 0, 1>(rhs), VectorSwizzle<2, 3, 2, 3>(rhs));
    const SIMDVectorSingle origin = VectorMax(lhs, rhs);
    const SIMDVectorSingle end = VectorMin(lhsEnd, rhsEnd);
    return VectorPermute<0, 1, 4, 5>(origin, end);
}

[[nodiscard]] FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL HasVisibleRectIntersection(const SIMDVectorSingle rectangle, const SIMDVectorSingle clip)noexcept{
    const SIMDVectorSingle bounds = IntersectRectBoundsValue(rectangle, clip);
    return VectorGetZ(bounds) > VectorGetX(bounds) && VectorGetW(bounds) > VectorGetY(bounds);
}

// The preserving policy retains NaNs and signed zero in extent lanes.
template<b8 PreserveExtent>
[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL IntersectRectValueImpl(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle bounds = IntersectRectBoundsValue(lhs, rhs);
    const SIMDVectorSingle extent = VectorSubtract(VectorSwizzle<2, 3, 2, 3>(bounds), VectorSwizzle<0, 1, 0, 1>(bounds));
    const SIMDVectorSingle size = PreserveExtent ? VectorMax(VectorZero(), extent) : VectorMax(extent, VectorZero());
    return VectorPermute<0, 1, 4, 5>(bounds, size);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL IntersectRectValue(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    return IntersectRectValueImpl<false>(lhs, rhs);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL IntersectRectValuePreservingExtent(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    return IntersectRectValueImpl<true>(lhs, rhs);
}

// Padding lanes are left, top, right, bottom; retain the two separately rounded size subtractions.
template<b8 PreserveExtent>
[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL InsetRectValueImpl(const SIMDVectorSingle rectangle, const SIMDVectorSingle padding)noexcept{
    const SIMDVectorSingle origin = VectorAdd(VectorSwizzle<0, 1, 0, 1>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding));
    const SIMDVectorSingle extentX = VectorSubtract(VectorSubtract(VectorSplatZ(rectangle), VectorSplatX(padding)), VectorSplatZ(padding));
    const SIMDVectorSingle extentY = VectorSubtract(VectorSubtract(VectorSplatW(rectangle), VectorSplatY(padding)), VectorSplatW(padding));
    const SIMDVectorSingle extent = VectorPermute<0, 4, 1, 5>(extentX, extentY);
    const SIMDVectorSingle size = PreserveExtent ? VectorMax(VectorZero(), extent) : VectorMax(extent, VectorZero());
    return VectorPermute<0, 1, 4, 5>(origin, size);
}


[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL InsetRectValue(const SIMDVectorSingle rectangle, const SIMDVectorSingle padding)noexcept{
    return InsetRectValueImpl<false>(rectangle, padding);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL InsetRectValuePreservingExtent(const SIMDVectorSingle rectangle, const SIMDVectorSingle padding)noexcept{
    return InsetRectValueImpl<true>(rectangle, padding);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

