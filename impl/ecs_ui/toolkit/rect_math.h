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
    Float4 lhsVec;
    Float4 rhsVec;
    StoreFloat(lhs, lhsVec);
    StoreFloat(rhs, rhsVec);
    return LoadFloat(IntersectRectBoundsVec(lhsVec, rhsVec));
}

[[nodiscard]] NWB_INLINE b8 NWB_SIMD_CALL HasVisibleRectIntersection(const SIMDVectorSingle rectangle, const SIMDVectorSingle clip)noexcept{
    Float4 rectangleVec;
    Float4 clipVec;
    StoreFloat(rectangle, rectangleVec);
    StoreFloat(clip, clipVec);
    return HasVisibleRectIntersectionVec(rectangleVec, clipVec);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL IntersectRectValue(const SIMDVectorSingle lhs, const SIMDVectorSingle rhs)noexcept{
    Float4 lhsVec;
    Float4 rhsVec;
    StoreFloat(lhs, lhsVec);
    StoreFloat(rhs, rhsVec);
    return LoadFloat(IntersectRectVec(lhsVec, rhsVec));
}

// Padding lanes are left, top, right, bottom; retain the two separately rounded size subtractions.
[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL InsetRectValue(const SIMDVectorSingle rectangle, const SIMDVectorSingle padding)noexcept{
    Float4 rectangleVec;
    Float4 paddingVec;
    StoreFloat(rectangle, rectangleVec);
    StoreFloat(padding, paddingVec);
    return LoadFloat(InsetRectVec(rectangleVec, paddingVec));
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

