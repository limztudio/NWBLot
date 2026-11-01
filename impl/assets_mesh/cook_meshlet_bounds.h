// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"
#include "meshlet_payload_packing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletBoundsCalculation{
    SIMDVectorSingle center = {};
    SIMDVectorSingle coneAxis = {};
    f32 radius = 0.0f;
    f32 coneCutoff = -1.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Bounding sphere and normal cone of one finished meshlet.
class MeshletBoundsBuilder final : NoCopy{
    struct AreaNormalAccumulator{
        SIMDVectorSingle& normal;
        u32& count;


    public:
        void operator()(SIMDVectorSingle faceNormal)const noexcept;
    };
    struct ConeCutoffAccumulator{
        SIMDVectorSingle& axis;
        f32& cutoff;


    public:
        void operator()(SIMDVectorSingle meshletFaceNormal)const noexcept;
    };


public:
    template<typename PositionAtT, typename VisitFaceNormalsT>
    [[nodiscard]] static MeshletBoundsCalculation CalculateMeshletBounds(const u32 positionCount, const u32 primitiveCount, const PositionAtT& positionAt, const VisitFaceNormalsT& visitFaceNormals)noexcept(
        noexcept(AabbTests::Expand(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>(), DeclVal<SIMDVectorSingle&>()))
        && noexcept(VectorSubtract(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>()))
        && noexcept(visitFaceNormals(AreaNormalAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<u32&>() }))
        && noexcept(visitFaceNormals(ConeCutoffAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<f32&>() }))
    );
    [[nodiscard]] static MeshletBounds BuildMeshletBounds(const MeshCookEntry& entry, const MeshletDesc& meshlet)noexcept;


public:
    MeshletBoundsBuilder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PositionAtT, typename VisitFaceNormalsT>
MeshletBoundsCalculation MeshletBoundsBuilder::CalculateMeshletBounds(const u32 positionCount, const u32 primitiveCount, const PositionAtT& positionAt, const VisitFaceNormalsT& visitFaceNormals)noexcept(
    noexcept(AabbTests::Expand(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>(), DeclVal<SIMDVectorSingle&>()))
    && noexcept(VectorSubtract(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>()))
    && noexcept(visitFaceNormals(AreaNormalAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<u32&>() }))
    && noexcept(visitFaceNormals(ConeCutoffAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<f32&>() }))
){
    MeshletBoundsCalculation result;

    SIMDVectorSingle minBounds;
    SIMDVectorSingle maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex)
        AabbTests::Expand(positionAt(localPositionIndex), minBounds, maxBounds);

    result.center = AabbTests::Center(minBounds, maxBounds);
    f32 radiusSquared = 0.0f;
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex){
        const SIMDVectorSingle delta = VectorSubtract(positionAt(localPositionIndex), result.center);
        radiusSquared = Max(radiusSquared, VectorGetX(Vector3LengthSq(delta)));
    }
    result.radius = Sqrt(radiusSquared);

    SIMDVectorSingle areaWeightedNormal = VectorZero();
    u32 validFaceNormalCount = 0u;
    visitFaceNormals(AreaNormalAccumulator{ areaWeightedNormal, validFaceNormalCount });

    result.coneAxis = Vector3NormalizeOr(areaWeightedNormal, VectorZero(), ::s_FrameDirectionEpsilon);
    if(validFaceNormalCount == primitiveCount && ::FrameValidDirection(result.coneAxis)){
        result.coneCutoff = 1.0f;
        visitFaceNormals(ConeCutoffAccumulator{ result.coneAxis, result.coneCutoff });
        if(result.coneCutoff <= 0.0f)
            result.coneCutoff = -1.0f;
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

