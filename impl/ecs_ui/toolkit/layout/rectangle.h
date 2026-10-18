// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "validation.h"

#include <global/simd/vector.h>


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
        const SIMDVectorDouble end = VectorDoubleAdd(VectorDoubleSet(x, y, 0.0, 0.0), VectorDoubleSet(width, height, 0.0, 0.0));
        if(VectorDoubleGetX(end) < -Limit<f32>::s_Max || VectorDoubleGetX(end) > Limit<f32>::s_Max || VectorDoubleGetY(end) < -Limit<f32>::s_Max || VectorDoubleGetY(end) > Limit<f32>::s_Max)
            return MakeUnexpected(Failure{});
    }
    Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    if constexpr(Precision == UiRectPrecision::EmptyPaint){
        if(candidate.width > 0.0f && VectorGetX(VectorAdd(VectorReplicate(candidate.x), VectorReplicate(candidate.width))) <= candidate.x)
            candidate.width = 0.0f;
        if(candidate.height > 0.0f && VectorGetX(VectorAdd(VectorReplicate(candidate.y), VectorReplicate(candidate.height))) <= candidate.y)
            candidate.height = 0.0f;
        if(!IsBoundedUiRect(candidate))
            return MakeUnexpected(Failure{});
    }
    else{
        if(!IsValidUiRect(candidate))
            return MakeUnexpected(Failure{});
        if constexpr(Precision == UiRectPrecision::PreciseExtents){
            if((width > 0.0 && VectorGetX(VectorAdd(VectorReplicate(candidate.x), VectorReplicate(candidate.width))) <= candidate.x) || (height > 0.0 && VectorGetX(VectorAdd(VectorReplicate(candidate.y), VectorReplicate(candidate.height))) <= candidate.y))
                return MakeUnexpected(Failure{});
        }
    }
    return candidate;
}

template<UiRectPrecision::Enum Precision>
[[nodiscard]] NWB_INLINE Expected<Rect> IntersectUiRects(const Rect& lhs, const Rect& rhs)noexcept{
    const SIMDVectorDouble lhsOrigin = VectorDoubleSet(static_cast<f64>(lhs.x), static_cast<f64>(lhs.y), 0.0, 0.0);
    const SIMDVectorDouble rhsOrigin = VectorDoubleSet(static_cast<f64>(rhs.x), static_cast<f64>(rhs.y), 0.0, 0.0);
    const SIMDVectorDouble origin = VectorDoubleSelect(rhsOrigin, lhsOrigin, VectorDoubleGreater(lhsOrigin, rhsOrigin));
    const SIMDVectorDouble lhsEnd = VectorDoubleAdd(lhsOrigin, VectorDoubleSet(lhs.width, lhs.height, 0.0, 0.0));
    const SIMDVectorDouble rhsEnd = VectorDoubleAdd(rhsOrigin, VectorDoubleSet(rhs.width, rhs.height, 0.0, 0.0));
    const SIMDVectorDouble end = VectorDoubleSelect(rhsEnd, lhsEnd, VectorDoubleLess(lhsEnd, rhsEnd));
    const SIMDVectorDouble extent = VectorDoubleSubtract(end, origin);
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble size = VectorDoubleSelect(extent, zero, VectorDoubleGreater(zero, extent));
    return MakeUiRect<Precision>(VectorDoubleGetX(origin), VectorDoubleGetY(origin), VectorDoubleGetX(size), VectorDoubleGetY(size));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

