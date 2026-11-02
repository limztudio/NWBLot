// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rect_math.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVectorSingle NWB_SIMD_CALL IntersectRectBoundsValue(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle lhsOrigin = VectorSwizzle<0, 1, 0, 1>(lhs);
    const SIMDVectorSingle rhsOrigin = VectorSwizzle<0, 1, 0, 1>(rhs);
    const SIMDVectorSingle lhsEnd = VectorAdd(lhsOrigin, VectorSwizzle<2, 3, 2, 3>(lhs));
    const SIMDVectorSingle rhsEnd = VectorAdd(rhsOrigin, VectorSwizzle<2, 3, 2, 3>(rhs));
    const SIMDVectorSingle origin = VectorSelect(rhsOrigin, lhsOrigin, VectorGreater(lhsOrigin, rhsOrigin));
    const SIMDVectorSingle end = VectorSelect(rhsEnd, lhsEnd, VectorLess(lhsEnd, rhsEnd));
    return VectorPermute<0, 1, 4, 5>(origin, end);
}

b8 NWB_SIMD_CALL HasVisibleRectIntersection(const SIMDVectorSingle rectangle, const SIMDVectorSingle clip)noexcept{
    const SIMDVectorSingle visibleBounds = IntersectRectBoundsValue(rectangle, clip);
    return (VectorMoveMask(VectorGreater(VectorSwizzle<2, 3, 2, 3>(visibleBounds), VectorSwizzle<0, 1, 0, 1>(visibleBounds))) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY;
}

SIMDVectorSingle NWB_SIMD_CALL IntersectRectValue(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    const SIMDVectorSingle bounds = IntersectRectBoundsValue(lhs, rhs);
    const SIMDVectorSingle origin = VectorSwizzle<0, 1, 0, 1>(bounds);
    const SIMDVectorSingle end = VectorSwizzle<2, 3, 2, 3>(bounds);
    const SIMDVectorSingle extent = VectorSubtract(end, origin);
    const SIMDVectorSingle size = VectorSelect(extent, VectorZero(), VectorGreater(VectorZero(), extent));
    return VectorPermute<0, 1, 4, 5>(origin, size);
}

SIMDVectorSingle NWB_SIMD_CALL InsetRectValue(const SIMDVectorSingle rectangle, const SIMDVectorSingle padding)noexcept{
    const SIMDVectorSingle origin = VectorAdd(VectorSwizzle<0, 1, 0, 1>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding));
    const SIMDVectorSingle extent = VectorSubtract(VectorSubtract(VectorSwizzle<2, 3, 2, 3>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding)), VectorSwizzle<2, 3, 2, 3>(padding));
    const SIMDVectorSingle size = VectorSelect(extent, VectorZero(), VectorGreater(VectorZero(), extent));
    return VectorPermute<0, 1, 4, 5>(origin, size);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

