// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/numeric_simd.h>

#include "validation.h"

#include <global/math/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiRectPrecision{
    enum Enum : u8{
        RoundedEndpoints,
        BoundedEndpoints,
        PreciseExtents,
        EmptyPaint,
    };
};

// Preserve each consumer's endpoint admission and subprecision paint policy at compile time.
template<UiRectPrecision::Enum Precision>
[[nodiscard]] NWB_INLINE Expected<Rect> MakeUiRect(const f64 x, const f64 y, const f64 width, const f64 height)noexcept{
    if(
        !IsFinite(x) || x < -Limit<f32>::s_Max || x > Limit<f32>::s_Max
        || !IsFinite(y) || y < -Limit<f32>::s_Max || y > Limit<f32>::s_Max
        || !IsFinite(width) || width < 0.0 || width > Limit<f32>::s_Max
        || !IsFinite(height) || height < 0.0 || height > Limit<f32>::s_Max
    )
        return MakeUnexpected(Failure{});
    if constexpr(Precision != UiRectPrecision::RoundedEndpoints){
        const SIMDVectorDouble end = SIMDVectorDouble{ x, y } + SIMDVectorDouble{ width, height };
        if(end.x < -Limit<f32>::s_Max || end.x > Limit<f32>::s_Max || end.y < -Limit<f32>::s_Max || end.y > Limit<f32>::s_Max)
            return MakeUnexpected(Failure{});
    }
    Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    if constexpr(Precision == UiRectPrecision::EmptyPaint){
        if(candidate.width > 0.0f && NumericSimd::Add(candidate.x, candidate.width) <= candidate.x)
            candidate.width = 0.0f;
        if(candidate.height > 0.0f && NumericSimd::Add(candidate.y, candidate.height) <= candidate.y)
            candidate.height = 0.0f;
        if(!IsBoundedUiRect(candidate))
            return MakeUnexpected(Failure{});
    }
    else{
        if(!IsValidUiRect(candidate))
            return MakeUnexpected(Failure{});
        if constexpr(Precision == UiRectPrecision::PreciseExtents){
            if((width > 0.0 && NumericSimd::Add(candidate.x, candidate.width) <= candidate.x) || (height > 0.0 && NumericSimd::Add(candidate.y, candidate.height) <= candidate.y))
                return MakeUnexpected(Failure{});
        }
    }
    return candidate;
}

template<UiRectPrecision::Enum Precision>
[[nodiscard]] NWB_INLINE Expected<Rect> IntersectUiRects(const Rect& lhs, const Rect& rhs)noexcept{
    const SIMDVectorDouble lhsOrigin{ static_cast<f64>(lhs.x), static_cast<f64>(lhs.y) };
    const SIMDVectorDouble rhsOrigin{ static_cast<f64>(rhs.x), static_cast<f64>(rhs.y) };
    const SIMDVectorDouble origin = (lhsOrigin > rhsOrigin) ? lhsOrigin : rhsOrigin;
    const SIMDVectorDouble lhsEnd = lhsOrigin + SIMDVectorDouble{ lhs.width, lhs.height };
    const SIMDVectorDouble rhsEnd = rhsOrigin + SIMDVectorDouble{ rhs.width, rhs.height };
    const SIMDVectorDouble end = (lhsEnd < rhsEnd) ? lhsEnd : rhsEnd;
    const SIMDVectorDouble extent = end - origin;
    const SIMDVectorDouble zero{};
    const SIMDVectorDouble size = (zero > extent) ? zero : extent;
    return MakeUiRect<Precision>(origin.x, origin.y, size.x, size.y);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

