// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AabbTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SIMDCALL Valid(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
void SIMDCALL Reset(SIMDVector& outMinBounds, SIMDVector& outMaxBounds)noexcept;
void SIMDCALL Expand(SIMDVector point, SIMDVector& inOutMinBounds, SIMDVector& inOutMaxBounds)noexcept;
void SIMDCALL ExpandTriangle(
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    SIMDVector& inOutMinBounds,
    SIMDVector& inOutMaxBounds
)noexcept;
[[nodiscard]] bool SIMDCALL Intersects(
    SIMDVector lhsMinBounds,
    SIMDVector lhsMaxBounds,
    SIMDVector rhsMinBounds,
    SIMDVector rhsMaxBounds
)noexcept;
[[nodiscard]] SIMDVector SIMDCALL Center(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
[[nodiscard]] SIMDVector SIMDCALL Extents(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
[[nodiscard]] f32 SIMDCALL SurfaceArea(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
[[nodiscard]] f32 SIMDCALL Radius(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
[[nodiscard]] bool SIMDCALL Transform(
    const SIMDMatrix& localToWorld,
    SIMDVector localMinBounds,
    SIMDVector localMaxBounds,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool SIMDCALL AabbTests::Valid(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return
        !Vector3IsNaN(minBounds)
        && !Vector3IsInfinite(minBounds)
        && !Vector3IsNaN(maxBounds)
        && !Vector3IsInfinite(maxBounds)
        && Vector3LessOrEqual(minBounds, maxBounds)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void SIMDCALL AabbTests::Reset(SIMDVector& outMinBounds, SIMDVector& outMaxBounds)noexcept{
    outMinBounds = VectorReplicate(s_MaxF32);
    outMaxBounds = VectorReplicate(-s_MaxF32);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void SIMDCALL AabbTests::Expand(const SIMDVector point, SIMDVector& inOutMinBounds, SIMDVector& inOutMaxBounds)noexcept{
    CollisionDetail::ExpandMinMax(point, inOutMinBounds, inOutMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void SIMDCALL AabbTests::ExpandTriangle(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    SIMDVector& inOutMinBounds,
    SIMDVector& inOutMaxBounds
)noexcept{
    AabbTests::Expand(v0, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v1, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v2, inOutMinBounds, inOutMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool SIMDCALL AabbTests::Intersects(
    const SIMDVector lhsMinBounds,
    const SIMDVector lhsMaxBounds,
    const SIMDVector rhsMinBounds,
    const SIMDVector rhsMaxBounds
)noexcept{
    return
        AabbTests::Valid(lhsMinBounds, lhsMaxBounds)
        && AabbTests::Valid(rhsMinBounds, rhsMaxBounds)
        && CollisionDetail::MinMaxIntersects(lhsMinBounds, lhsMaxBounds, rhsMinBounds, rhsMaxBounds)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL AabbTests::Center(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorAdd(minBounds, maxBounds), CollisionDetail::s_Half), 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL AabbTests::Extents(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorSubtract(maxBounds, minBounds), CollisionDetail::s_Half), 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE f32 SIMDCALL AabbTests::SurfaceArea(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    const SIMDVector extent = VectorSubtract(maxBounds, minBounds);
    const SIMDVector pairProducts = VectorMultiply(extent, VectorSwizzle<1, 2, 0, 3>(extent));
    const SIMDVector area = VectorScale(
        VectorAdd(
            VectorAdd(VectorSplatX(pairProducts), VectorSplatZ(pairProducts)),
            VectorSplatY(pairProducts)
        ),
        2.0f
    );
    return VectorGetX(area);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE f32 SIMDCALL AabbTests::Radius(const SIMDVector minBounds, const SIMDVector maxBounds)noexcept{
    return VectorGetX(Vector3Length(AabbTests::Extents(minBounds, maxBounds)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL AabbTests::Transform(
    const SIMDMatrix& localToWorld,
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
)noexcept{
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return false;
    if(MatrixIsNaN(localToWorld) || MatrixIsInfinite(localToWorld))
        return false;

    AabbTests::Reset(outMinBounds, outMaxBounds);
    for(u32 corner = 0u; corner < CollisionDetail::s_AabbCornerCount; ++corner){
        const SIMDVector cornerSelect = VectorSelectControl(
            corner & CollisionDetail::s_BoxCornerXSelectBit,
            (corner >> CollisionDetail::s_BoxCornerYSelectShift) & CollisionDetail::s_BoxCornerXSelectBit,
            (corner >> CollisionDetail::s_BoxCornerZSelectShift) & CollisionDetail::s_BoxCornerXSelectBit,
            0u
        );
        const SIMDVector localPoint = VectorSelect(localMinBounds, localMaxBounds, cornerSelect);
        const SIMDVector point = Vector3Transform(localPoint, localToWorld);
        if(Vector3IsNaN(point) || Vector3IsInfinite(point))
            return false;

        AabbTests::Expand(point, outMinBounds, outMaxBounds);
    }
    return AabbTests::Valid(outMinBounds, outMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

