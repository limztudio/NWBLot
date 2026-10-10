// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool HasValidUiRectDimensions(const Rect& rectangle)noexcept{
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y)
        && IsFinite(rectangle.width) && rectangle.width >= 0.0f
        && IsFinite(rectangle.height) && rectangle.height >= 0.0f
    ;
}

[[nodiscard]] inline bool HasPreciseUiRectEndpoints(const Rect& rectangle)noexcept{
    return
        IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
        && (rectangle.width == 0.0f || rectangle.x + rectangle.width > rectangle.x)
        && (rectangle.height == 0.0f || rectangle.y + rectangle.height > rectangle.y)
    ;
}

// Zero or subprecision extents remain valid when their rounded endpoints are finite.
[[nodiscard]] inline bool IsValidUiRect(const Rect& rectangle)noexcept{
    return
        HasValidUiRectDimensions(rectangle)
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

// Controls with travel geometry additionally reject positive extents lost to coordinate precision.
[[nodiscard]] inline bool IsPreciseUiRect(const Rect& rectangle)noexcept{
    return HasValidUiRectDimensions(rectangle) && HasPreciseUiRectEndpoints(rectangle);
}

// Image placements require both precise rounded extents and f64 endpoint sums inside the f32 upper bound.
[[nodiscard]] inline bool IsBoundedUiRect(const Rect& rectangle)noexcept{
    return
        HasValidUiRectDimensions(rectangle)
        && static_cast<f64>(rectangle.x) + rectangle.width <= Limit<f32>::s_Max
        && static_cast<f64>(rectangle.y) + rectangle.height <= Limit<f32>::s_Max
        && HasPreciseUiRectEndpoints(rectangle)
    ;
}

[[nodiscard]] inline bool IsValidUiPadding(const Insets& padding)noexcept{
    return
        IsFinite(padding.left) && padding.left >= 0.0f && IsFinite(padding.top) && padding.top >= 0.0f
        && IsFinite(padding.right) && padding.right >= 0.0f && IsFinite(padding.bottom) && padding.bottom >= 0.0f
    ;
}

[[nodiscard]] inline bool IsBoundedUiPadding(const Insets& padding)noexcept{
    return
        IsValidUiPadding(padding)
        && static_cast<f64>(padding.left) + padding.right <= Limit<f32>::s_Max
        && static_cast<f64>(padding.top) + padding.bottom <= Limit<f32>::s_Max
    ;
}

[[nodiscard]] inline bool IsValidUiColor(const Color& color)noexcept{
    return
        IsFinite(color.r) && color.r >= 0.0f && IsFinite(color.g) && color.g >= 0.0f
        && IsFinite(color.b) && color.b >= 0.0f && IsFinite(color.a) && color.a >= 0.0f && color.a <= 1.0f
    ;
}

[[nodiscard]] inline bool IsValidUiExtent(const Point& point)noexcept{
    return IsFinite(point.x) && point.x >= 0.0f && IsFinite(point.y) && point.y >= 0.0f;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

