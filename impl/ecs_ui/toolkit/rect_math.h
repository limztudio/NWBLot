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
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL IntersectRectBoundsValue(SIMDVectorSingle lhs, SIMDVectorSingle rhs)noexcept;
[[nodiscard]] b8 NWB_SIMD_CALL HasVisibleRectIntersection(SIMDVectorSingle rectangle, SIMDVectorSingle clip)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL IntersectRectValue(SIMDVectorSingle lhs, SIMDVectorSingle rhs)noexcept;

// Padding lanes are left, top, right, bottom; retain the two separately rounded size subtractions.
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL InsetRectValue(SIMDVectorSingle rectangle, SIMDVectorSingle padding)noexcept;

[[nodiscard]] NWB_INLINE SIMDVectorSingle LoadRect(const Rect& rectangle)noexcept{
    return VectorSet(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
}

[[nodiscard]] NWB_INLINE Rect NWB_SIMD_CALL StoreRect(SIMDVectorSingle rectangle)noexcept{
    return { VectorGetX(rectangle), VectorGetY(rectangle), VectorGetZ(rectangle), VectorGetW(rectangle) };
}

[[nodiscard]] Insets MaxUiPadding(const Insets& lhs, const Insets& rhs)noexcept;

template<typename PaddingT>
    requires (!IsSame_V<PaddingT, Insets>) && requires(const PaddingT& padding){
        padding.left;
        padding.top;
        padding.right;
        padding.bottom;
    }
[[nodiscard]] NWB_INLINE Insets MaxUiPadding(const Insets& lhs, const PaddingT& rhs)noexcept{
    return MaxUiPadding(lhs, Insets{ rhs.left, rhs.top, rhs.right, rhs.bottom });
}

[[nodiscard]] NWB_INLINE SIMDMatrixDouble MakeInvalidUiMatrixDouble()noexcept{
    return SIMDMatrixDouble{ .v = { VectorDoubleReplicate(Limit<f64>::s_Infinity), VectorDoubleZero(), VectorDoubleZero(), VectorDoubleZero() } };
}

[[nodiscard]] b8 AreUiMatrixRowsFinite(const SIMDMatrixDouble& values, u32 rowCount)noexcept;
[[nodiscard]] b8 AreUiMatrixRowsNonNegativeFinite(const SIMDMatrixDouble& values, u32 rowCount)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

