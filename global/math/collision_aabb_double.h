// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_double.h"
#include "collision_detail_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AabbDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool NWB_SIMD_CALL Valid(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
void NWB_SIMD_CALL Reset(SIMDVectorDouble& outMinBounds, SIMDVectorDouble& outMaxBounds)noexcept;
void NWB_SIMD_CALL Expand(SIMDVectorDouble point, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept;
void NWB_SIMD_CALL ExpandTriangle(
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2,
    SIMDVectorDouble& inOutMinBounds,
    SIMDVectorDouble& inOutMaxBounds
)noexcept;
[[nodiscard]] bool NWB_SIMD_CALL Intersects(
    SIMDVectorDouble lhsMinBounds,
    SIMDVectorDouble lhsMaxBounds,
    SIMDVectorDouble rhsMinBounds,
    SIMDVectorDouble rhsMaxBounds
)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Center(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Extents(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
[[nodiscard]] f64 NWB_SIMD_CALL SurfaceArea(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
[[nodiscard]] f64 NWB_SIMD_CALL Radius(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
struct Bounds{
    SIMDVectorDouble minBounds;
    SIMDVectorDouble maxBounds;
};

[[nodiscard]] Expected<Bounds> NWB_SIMD_CALL Transform(
    const SIMDMatrixDouble& localToWorld,
    SIMDVectorDouble localMinBounds,
    SIMDVectorDouble localMaxBounds
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbDoubleTests::Valid(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return
        !VectorDouble3IsNaN(minBounds)
        && !VectorDouble3IsInfinite(minBounds)
        && !VectorDouble3IsNaN(maxBounds)
        && !VectorDouble3IsInfinite(maxBounds)
        && VectorDouble3LessOrEqual(minBounds, maxBounds)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbDoubleTests::Reset(SIMDVectorDouble& outMinBounds, SIMDVectorDouble& outMaxBounds)noexcept{
    outMinBounds = VectorDoubleReplicate(Limit<f64>::s_Max);
    outMaxBounds = VectorDoubleReplicate(-Limit<f64>::s_Max);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbDoubleTests::Expand(const SIMDVectorDouble point, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept{
    CollisionDoubleDetail::ExpandMinMax(point, inOutMinBounds, inOutMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbDoubleTests::ExpandTriangle(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2,
    SIMDVectorDouble& inOutMinBounds,
    SIMDVectorDouble& inOutMaxBounds
)noexcept{
    AabbDoubleTests::Expand(v0, inOutMinBounds, inOutMaxBounds);
    AabbDoubleTests::Expand(v1, inOutMinBounds, inOutMaxBounds);
    AabbDoubleTests::Expand(v2, inOutMinBounds, inOutMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbDoubleTests::Intersects(
    const SIMDVectorDouble lhsMinBounds,
    const SIMDVectorDouble lhsMaxBounds,
    const SIMDVectorDouble rhsMinBounds,
    const SIMDVectorDouble rhsMaxBounds
)noexcept{
    return
        AabbDoubleTests::Valid(lhsMinBounds, lhsMaxBounds)
        && AabbDoubleTests::Valid(rhsMinBounds, rhsMaxBounds)
        && CollisionDoubleDetail::MinMaxIntersects(lhsMinBounds, lhsMaxBounds, rhsMinBounds, rhsMaxBounds)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL AabbDoubleTests::Center(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return VectorDoubleSetW(VectorDoubleScale(VectorDoubleAdd(minBounds, maxBounds), CollisionDoubleDetail::s_DoubleHalf), 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL AabbDoubleTests::Extents(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return VectorDoubleSetW(VectorDoubleScale(VectorDoubleSubtract(maxBounds, minBounds), CollisionDoubleDetail::s_DoubleHalf), 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE f64 NWB_SIMD_CALL AabbDoubleTests::SurfaceArea(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    const SIMDVectorDouble extent = VectorDoubleSubtract(maxBounds, minBounds);
    const SIMDVectorDouble pairProducts = VectorDoubleMultiply(extent, VectorDoubleSwizzle<1, 2, 0, 3>(extent));
    const SIMDVectorDouble area = VectorDoubleScale(
        VectorDoubleAdd(
            VectorDoubleAdd(VectorDoubleSplatX(pairProducts), VectorDoubleSplatZ(pairProducts)),
            VectorDoubleSplatY(pairProducts)
        ),
        2.0
    );
    return VectorDoubleGetX(area);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE f64 NWB_SIMD_CALL AabbDoubleTests::Radius(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return VectorDoubleGetX(VectorDouble3Length(AabbDoubleTests::Extents(minBounds, maxBounds)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<AabbDoubleTests::Bounds> NWB_SIMD_CALL AabbDoubleTests::Transform(
    const SIMDMatrixDouble& localToWorld,
    const SIMDVectorDouble localMinBounds,
    const SIMDVectorDouble localMaxBounds
)noexcept{
    if(!AabbDoubleTests::Valid(localMinBounds, localMaxBounds))
        return MakeUnexpected(Failure{});
    if(MatrixDoubleIsNaN(localToWorld) || MatrixDoubleIsInfinite(localToWorld))
        return MakeUnexpected(Failure{});

    Bounds bounds;
    AabbDoubleTests::Reset(bounds.minBounds, bounds.maxBounds);
    for(u64 corner = 0u; corner < CollisionDoubleDetail::s_DoubleAabbCornerCount; ++corner){
        const SIMDVectorDouble cornerSelect = VectorDoubleSelectControl(
            corner & CollisionDoubleDetail::s_DoubleBoxCornerXSelectBit,
            (corner >> CollisionDoubleDetail::s_DoubleBoxCornerYSelectShift) & CollisionDoubleDetail::s_DoubleBoxCornerXSelectBit,
            (corner >> CollisionDoubleDetail::s_DoubleBoxCornerZSelectShift) & CollisionDoubleDetail::s_DoubleBoxCornerXSelectBit,
            0u
        );
        const SIMDVectorDouble localPoint = VectorDoubleSelect(localMinBounds, localMaxBounds, cornerSelect);
        const SIMDVectorDouble point = VectorDouble3Transform(localPoint, localToWorld);
        if(VectorDouble3IsNaN(point) || VectorDouble3IsInfinite(point))
            return MakeUnexpected(Failure{});

        AabbDoubleTests::Expand(point, bounds.minBounds, bounds.maxBounds);
    }
    if(!AabbDoubleTests::Valid(bounds.minBounds, bounds.maxBounds))
        return MakeUnexpected(Failure{});
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

