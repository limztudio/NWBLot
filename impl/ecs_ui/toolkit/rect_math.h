// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/ecs_ui/toolkit/paint_types.adhd.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Rectangle lanes are x, y, width, height; comparisons preserve scalar Min/Max ties and NaNs on every backend.
[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL IntersectRectBoundsValue(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle lhsEnd = VectorAdd(VectorSwizzle<0, 1, 0, 1>(lhs), VectorSwizzle<2, 3, 2, 3>(lhs));
    const SIMDVectorSingle rhsEnd = VectorAdd(VectorSwizzle<0, 1, 0, 1>(rhs), VectorSwizzle<2, 3, 2, 3>(rhs));
    const SIMDVectorSingle origin = VectorMax(lhs, rhs);
    const SIMDVectorSingle end = VectorMin(lhsEnd, rhsEnd);
    return VectorPermute<0, 1, 4, 5>(origin, end);
}

[[nodiscard]] NWB_INLINE b8 NWB_SIMD_CALL HasVisibleRectIntersection(const SIMDVectorSingle rectangle, const SIMDVectorSingle clip)noexcept{
    const SIMDVectorSingle bounds = IntersectRectBoundsValue(rectangle, clip);
    return VectorGetZ(bounds) > VectorGetX(bounds) && VectorGetW(bounds) > VectorGetY(bounds);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL IntersectRectValue(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle bounds = IntersectRectBoundsValue(lhs, rhs);
    const SIMDVectorSingle extent = VectorSubtract(VectorSwizzle<2, 3, 2, 3>(bounds), VectorSwizzle<0, 1, 0, 1>(bounds));
    const SIMDVectorSingle size = VectorMax(extent, VectorZero());
    return VectorPermute<0, 1, 4, 5>(bounds, size);
}

// Padding lanes are left, top, right, bottom; retain the two separately rounded size subtractions.
[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL InsetRectValue(const SIMDVectorSingle rectangle, const SIMDVectorSingle padding)noexcept{
    const SIMDVectorSingle origin = VectorAdd(VectorSwizzle<0, 1, 0, 1>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding));
    const SIMDVectorSingle extentX = VectorSubtract(VectorSubtract(VectorSplatZ(rectangle), VectorSplatX(padding)), VectorSplatZ(padding));
    const SIMDVectorSingle extentY = VectorSubtract(VectorSubtract(VectorSplatW(rectangle), VectorSplatY(padding)), VectorSplatW(padding));
    const SIMDVectorSingle size = VectorMax(VectorPermute<0, 4, 1, 5>(extentX, extentY), VectorZero());
    return VectorPermute<0, 1, 4, 5>(origin, size);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle LoadRect(const Rect& rectangle)noexcept{
    return VectorSet(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
}

[[nodiscard]] NWB_INLINE Rect NWB_SIMD_CALL StoreRect(SIMDVectorSingle rectangle)noexcept{
    return { VectorGetX(rectangle), VectorGetY(rectangle), VectorGetZ(rectangle), VectorGetW(rectangle) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

