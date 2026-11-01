// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>
#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Preserve each consumer's endpoint admission and subprecision paint policy at compile time. Rectangle lanes are x, y, width, height.
template<UiRectPrecision::Enum Precision>
[[nodiscard]] NWB_INLINE Expected<Rect> MakeUiRect(const SIMDVectorDouble rectangle)noexcept{
    Rect candidate;
    if(!TryMakeUiRect(VectorDoubleGetX(rectangle), VectorDoubleGetY(rectangle), VectorDoubleGetZ(rectangle), VectorDoubleGetW(rectangle), Precision, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

[[nodiscard]] SIMDVectorDouble InsetUiRectValue(const Rect& bounds, const Insets& padding)noexcept;

template<UiRectPrecision::Enum Precision>
[[nodiscard]] NWB_INLINE Expected<Rect> InsetUiRect(const Rect& bounds, const Insets& padding)noexcept{
    Rect candidate;
    if(!TryInsetUiRect(bounds, padding, Precision, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

template<UiRectPrecision::Enum Precision>
[[nodiscard]] NWB_INLINE Expected<Rect> IntersectUiRects(const Rect& lhs, const Rect& rhs)noexcept{
    Rect candidate;
    if(!TryIntersectUiRects(lhs, rhs, Precision, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

