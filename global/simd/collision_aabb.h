// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AabbTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Calculation result only; callers store retained bounds in Float/Double storage types.
struct alignas(SIMDVectorSingle) Bounds{
    SIMDVectorSingle minBounds;
    SIMDVectorSingle maxBounds;
};
static_assert(alignof(Expected<Bounds>) >= alignof(Bounds));

[[nodiscard]] bool NWB_SIMD_CALL Valid(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
void NWB_SIMD_CALL Reset(SIMDVectorSingle& outMinBounds, SIMDVectorSingle& outMaxBounds)noexcept;
void NWB_SIMD_CALL Expand(SIMDVectorSingle point, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept;
void NWB_SIMD_CALL ExpandTriangle(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept;
[[nodiscard]] bool NWB_SIMD_CALL Intersects(SIMDVectorSingle lhsMinBounds, SIMDVectorSingle lhsMaxBounds, SIMDVectorSingle rhsMinBounds, SIMDVectorSingle rhsMaxBounds)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Center(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Extents(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL SurfaceArea(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Radius(SIMDVectorSingle minBounds, SIMDVectorSingle maxBounds)noexcept;
[[nodiscard]] Expected<Bounds> NWB_SIMD_CALL Transform(const SIMDMatrix& localToWorld, SIMDVectorSingle localMinBounds, SIMDVectorSingle localMaxBounds)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AabbDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Calculation result only; callers store retained bounds in Float/Double storage types.
struct alignas(SIMDVectorDouble) Bounds{
    SIMDVectorDouble minBounds;
    SIMDVectorDouble maxBounds;
};
static_assert(alignof(Expected<Bounds>) >= alignof(Bounds));

[[nodiscard]] bool NWB_SIMD_CALL Valid(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
void NWB_SIMD_CALL Reset(SIMDVectorDouble& outMinBounds, SIMDVectorDouble& outMaxBounds)noexcept;
void NWB_SIMD_CALL Expand(SIMDVectorDouble point, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept;
void NWB_SIMD_CALL ExpandTriangle(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept;
[[nodiscard]] bool NWB_SIMD_CALL Intersects(SIMDVectorDouble lhsMinBounds, SIMDVectorDouble lhsMaxBounds, SIMDVectorDouble rhsMinBounds, SIMDVectorDouble rhsMaxBounds)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Center(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Extents(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL SurfaceArea(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Radius(SIMDVectorDouble minBounds, SIMDVectorDouble maxBounds)noexcept;
[[nodiscard]] Expected<Bounds> NWB_SIMD_CALL Transform(const SIMDMatrixDouble& localToWorld, SIMDVectorDouble localMinBounds, SIMDVectorDouble localMaxBounds)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbTests::Valid(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return !Vector3IsNaN(minBounds) && !Vector3IsInfinite(minBounds) && !Vector3IsNaN(maxBounds) && !Vector3IsInfinite(maxBounds) && Vector3LessOrEqual(minBounds, maxBounds);
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbDoubleTests::Valid(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return !VectorDouble3IsNaN(minBounds) && !VectorDouble3IsInfinite(minBounds) && !VectorDouble3IsNaN(maxBounds) && !VectorDouble3IsInfinite(maxBounds) && VectorDouble3LessOrEqual(minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbTests::Reset(SIMDVectorSingle& outMinBounds, SIMDVectorSingle& outMaxBounds)noexcept{
    outMinBounds = VectorReplicate(s_MaxF32);
    outMaxBounds = VectorReplicate(-s_MaxF32);
}

NWB_INLINE void NWB_SIMD_CALL AabbDoubleTests::Reset(SIMDVectorDouble& outMinBounds, SIMDVectorDouble& outMaxBounds)noexcept{
    outMinBounds = VectorDoubleReplicate(Limit<f64>::s_Max);
    outMaxBounds = VectorDoubleReplicate(-Limit<f64>::s_Max);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL AabbTests::Expand(const SIMDVectorSingle point, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept{
    CollisionDetail::ExpandMinMax(point, inOutMinBounds, inOutMaxBounds);
}

NWB_INLINE void NWB_SIMD_CALL AabbDoubleTests::Expand(const SIMDVectorDouble point, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept{
    CollisionDoubleDetail::ExpandMinMax(point, inOutMinBounds, inOutMaxBounds);
}

NWB_INLINE void NWB_SIMD_CALL AabbTests::ExpandTriangle(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept{
    AabbTests::Expand(v0, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v1, inOutMinBounds, inOutMaxBounds);
    AabbTests::Expand(v2, inOutMinBounds, inOutMaxBounds);
}

NWB_INLINE void NWB_SIMD_CALL AabbDoubleTests::ExpandTriangle(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept{
    AabbDoubleTests::Expand(v0, inOutMinBounds, inOutMaxBounds);
    AabbDoubleTests::Expand(v1, inOutMinBounds, inOutMaxBounds);
    AabbDoubleTests::Expand(v2, inOutMinBounds, inOutMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbTests::Intersects(const SIMDVectorSingle lhsMinBounds, const SIMDVectorSingle lhsMaxBounds, const SIMDVectorSingle rhsMinBounds, const SIMDVectorSingle rhsMaxBounds)noexcept{
    return AabbTests::Valid(lhsMinBounds, lhsMaxBounds) && AabbTests::Valid(rhsMinBounds, rhsMaxBounds) && CollisionDetail::MinMaxIntersects(lhsMinBounds, lhsMaxBounds, rhsMinBounds, rhsMaxBounds);
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL AabbDoubleTests::Intersects(const SIMDVectorDouble lhsMinBounds, const SIMDVectorDouble lhsMaxBounds, const SIMDVectorDouble rhsMinBounds, const SIMDVectorDouble rhsMaxBounds)noexcept{
    return AabbDoubleTests::Valid(lhsMinBounds, lhsMaxBounds) && AabbDoubleTests::Valid(rhsMinBounds, rhsMaxBounds) && CollisionDoubleDetail::MinMaxIntersects(lhsMinBounds, lhsMaxBounds, rhsMinBounds, rhsMaxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL AabbTests::Center(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorAdd(minBounds, maxBounds), CollisionDetail::s_Half), 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL AabbDoubleTests::Center(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return VectorDoubleSetW(VectorDoubleScale(VectorDoubleAdd(minBounds, maxBounds), CollisionDoubleDetail::s_DoubleHalf), 0.0);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL AabbTests::Extents(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return VectorSetW(VectorScale(VectorSubtract(maxBounds, minBounds), CollisionDetail::s_Half), 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL AabbDoubleTests::Extents(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return VectorDoubleSetW(VectorDoubleScale(VectorDoubleSubtract(maxBounds, minBounds), CollisionDoubleDetail::s_DoubleHalf), 0.0);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL AabbTests::SurfaceArea(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    const SIMDVectorSingle extent = VectorSubtract(maxBounds, minBounds);
    const SIMDVectorSingle pairProducts = VectorMultiply(extent, VectorSwizzle<1, 2, 0, 3>(extent));
    const SIMDVectorSingle area = VectorScale(VectorAdd(VectorAdd(VectorSplatX(pairProducts), VectorSplatZ(pairProducts)), VectorSplatY(pairProducts)), 2.0f);
    return area;
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL AabbDoubleTests::SurfaceArea(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    const SIMDVectorDouble extent = VectorDoubleSubtract(maxBounds, minBounds);
    const SIMDVectorDouble pairProducts = VectorDoubleMultiply(extent, VectorDoubleSwizzle<1, 2, 0, 3>(extent));
    const SIMDVectorDouble area = VectorDoubleScale(VectorDoubleAdd(VectorDoubleAdd(VectorDoubleSplatX(pairProducts), VectorDoubleSplatZ(pairProducts)), VectorDoubleSplatY(pairProducts)), 2.0);
    return area;
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL AabbTests::Radius(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return Vector3Length(AabbTests::Extents(minBounds, maxBounds));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL AabbDoubleTests::Radius(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return VectorDouble3Length(AabbDoubleTests::Extents(minBounds, maxBounds));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<AabbTests::Bounds> NWB_SIMD_CALL AabbTests::Transform(const SIMDMatrix& localToWorld, const SIMDVectorSingle localMinBounds, const SIMDVectorSingle localMaxBounds)noexcept{
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

[[nodiscard]] inline Expected<AabbDoubleTests::Bounds> NWB_SIMD_CALL AabbDoubleTests::Transform(const SIMDMatrixDouble& localToWorld, const SIMDVectorDouble localMinBounds, const SIMDVectorDouble localMaxBounds)noexcept{
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

