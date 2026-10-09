// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/math/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Rectangle lanes are x, y, width, height; comparisons preserve scalar Min/Max ties and NaNs on every backend.
[[nodiscard]] NWB_INLINE SIMDVector NWB_SIMD_CALL IntersectRectBoundsValue(SIMDVector lhs, SIMDVector rhs)noexcept{
    const SIMDVector lhsOrigin = VectorSwizzle<0, 1, 0, 1>(lhs);
    const SIMDVector rhsOrigin = VectorSwizzle<0, 1, 0, 1>(rhs);
    const SIMDVector lhsEnd = VectorAdd(lhsOrigin, VectorSwizzle<2, 3, 2, 3>(lhs));
    const SIMDVector rhsEnd = VectorAdd(rhsOrigin, VectorSwizzle<2, 3, 2, 3>(rhs));
    const SIMDVector origin = VectorSelect(rhsOrigin, lhsOrigin, VectorGreater(lhsOrigin, rhsOrigin));
    const SIMDVector end = VectorSelect(rhsEnd, lhsEnd, VectorLess(lhsEnd, rhsEnd));
    return VectorPermute<0, 1, 4, 5>(origin, end);
}

[[nodiscard]] NWB_INLINE SIMDVector NWB_SIMD_CALL IntersectRectValue(SIMDVector lhs, SIMDVector rhs)noexcept{
    const SIMDVector bounds = IntersectRectBoundsValue(lhs, rhs);
    const SIMDVector origin = VectorSwizzle<0, 1, 0, 1>(bounds);
    const SIMDVector end = VectorSwizzle<2, 3, 2, 3>(bounds);
    const SIMDVector extent = VectorSubtract(end, origin);
    const SIMDVector size = VectorSelect(extent, VectorZero(), VectorGreater(VectorZero(), extent));
    return VectorPermute<0, 1, 4, 5>(origin, size);
}

// Padding lanes are left, top, right, bottom; retain the two separately rounded size subtractions.
[[nodiscard]] NWB_INLINE SIMDVector NWB_SIMD_CALL InsetRectValue(SIMDVector rectangle, SIMDVector padding)noexcept{
    const SIMDVector origin = VectorAdd(VectorSwizzle<0, 1, 0, 1>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding));
    const SIMDVector extent = VectorSubtract(
        VectorSubtract(VectorSwizzle<2, 3, 2, 3>(rectangle), VectorSwizzle<0, 1, 0, 1>(padding)),
        VectorSwizzle<2, 3, 2, 3>(padding)
    );
    const SIMDVector size = VectorSelect(extent, VectorZero(), VectorGreater(VectorZero(), extent));
    return VectorPermute<0, 1, 4, 5>(origin, size);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

