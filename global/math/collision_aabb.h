// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AabbTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool NWB_SIMD_CALL Valid(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
void NWB_SIMD_CALL Reset(SIMDVectorSingle& outMinBounds, SIMDVectorSingle& outMaxBounds)noexcept;
void NWB_SIMD_CALL Expand(SIMDVectorSingle point, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept;
void NWB_SIMD_CALL ExpandTriangle(
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2,
    SIMDVectorSingle& inOutMinBounds,
    SIMDVectorSingle& inOutMaxBounds
)noexcept;
[[nodiscard]] bool NWB_SIMD_CALL Intersects(
    SIMDVectorSingle lhsMinBounds,
    SIMDVectorSingle lhsMaxBounds,
    SIMDVectorSingle rhsMinBounds,
    SIMDVectorSingle rhsMaxBounds
)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Center(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Extents(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
[[nodiscard]] f32 NWB_SIMD_CALL SurfaceArea(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
[[nodiscard]] f32 NWB_SIMD_CALL Radius(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
struct Bounds{
    SIMDVectorSingle minBounds;
    SIMDVectorSingle maxBounds;
};

[[nodiscard]] Expected<Bounds> NWB_SIMD_CALL Transform(
    const SIMDMatrix& localToWorld,
    SIMDVectorSingle localMinBounds,
    SIMDVectorSingle localMaxBounds
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbTests::Valid(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return
        !Vector3IsNaN(minBounds)
        && !Vector3IsInfinite(minBounds)
        && !Vector3IsNaN(maxBounds)
        && !Vector3IsInfinite(maxBounds)
        && Vector3LessOrEqual(minBounds, maxBounds)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbTests::Reset(SIMDVectorSingle& outMinBounds, SIMDVectorSingle& outMaxBounds)noexcept{
    outMinBounds = VectorReplicate(s_MaxF32);
    outMaxBounds = VectorReplicate(-s_MaxF32);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbTests::Expand(const SIMDVectorSingle point, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept{
    CollisionDetail::ExpandMinMax(point, inOutMinBounds, inOutMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbTests::ExpandTriangle(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2,
    SIMDVectorSingle& inOutMinBounds,
    SIMDVectorSingle& inOutMaxBounds
)noexcept{
    AabbTests::Expand(v0, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v1, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v2, inOutMinBounds, inOutMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbTests::Intersects(
    const SIMDVectorSingle lhsMinBounds,
    const SIMDVectorSingle lhsMaxBounds,
    const SIMDVectorSingle rhsMinBounds,
    const SIMDVectorSingle rhsMaxBounds
)noexcept{
    return
        AabbTests::Valid(lhsMinBounds, lhsMaxBounds)
        && AabbTests::Valid(rhsMinBounds, rhsMaxBounds)
        && CollisionDetail::MinMaxIntersects(lhsMinBounds, lhsMaxBounds, rhsMinBounds, rhsMaxBounds)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL AabbTests::Center(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorAdd(minBounds, maxBounds), CollisionDetail::s_Half), 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL AabbTests::Extents(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorSubtract(maxBounds, minBounds), CollisionDetail::s_Half), 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE f32 NWB_SIMD_CALL AabbTests::SurfaceArea(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    const SIMDVectorSingle extent = VectorSubtract(maxBounds, minBounds);
    const SIMDVectorSingle pairProducts = VectorMultiply(extent, VectorSwizzle<1, 2, 0, 3>(extent));
    const SIMDVectorSingle area = VectorScale(
        VectorAdd(
            VectorAdd(VectorSplatX(pairProducts), VectorSplatZ(pairProducts)),
            VectorSplatY(pairProducts)
        ),
        2.0f
    );
    return VectorGetX(area);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE f32 NWB_SIMD_CALL AabbTests::Radius(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return VectorGetX(Vector3Length(AabbTests::Extents(minBounds, maxBounds)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<AabbTests::Bounds> NWB_SIMD_CALL AabbTests::Transform(
    const SIMDMatrix& localToWorld,
    const SIMDVectorSingle localMinBounds,
    const SIMDVectorSingle localMaxBounds
)noexcept{
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return MakeUnexpected(Failure{});
    if(MatrixIsNaN(localToWorld) || MatrixIsInfinite(localToWorld))
        return MakeUnexpected(Failure{});

    Bounds bounds;
    AabbTests::Reset(bounds.minBounds, bounds.maxBounds);
    for(u32 corner = 0u; corner < CollisionDetail::s_AabbCornerCount; ++corner){
        const SIMDVectorSingle cornerSelect = VectorSelectControl(
            corner & CollisionDetail::s_BoxCornerXSelectBit,
            (corner >> CollisionDetail::s_BoxCornerYSelectShift) & CollisionDetail::s_BoxCornerXSelectBit,
            (corner >> CollisionDetail::s_BoxCornerZSelectShift) & CollisionDetail::s_BoxCornerXSelectBit,
            0u
        );
        const SIMDVectorSingle localPoint = VectorSelect(localMinBounds, localMaxBounds, cornerSelect);
        const SIMDVectorSingle point = Vector3Transform(localPoint, localToWorld);
        if(Vector3IsNaN(point) || Vector3IsInfinite(point))
            return MakeUnexpected(Failure{});

        AabbTests::Expand(point, bounds.minBounds, bounds.maxBounds);
    }
    if(!AabbTests::Valid(bounds.minBounds, bounds.maxBounds))
        return MakeUnexpected(Failure{});
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

