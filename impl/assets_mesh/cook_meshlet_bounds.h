// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


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
namespace MeshletBoundsBuilder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AreaNormalAccumulator{
    SIMDVectorSingle& normal;
    u32& count;


public:
    void operator()(const SIMDVectorSingle faceNormal)const noexcept{
        if(!::FrameValidDirection(faceNormal))
            return;

        normal = VectorAdd(normal, faceNormal);
        ++count;
    }
};

struct ConeCutoffAccumulator{
    SIMDVectorSingle& axis;
    f32& cutoff;


public:
    void operator()(const SIMDVectorSingle meshletFaceNormal)const noexcept{
        const SIMDVectorSingle faceNormal = Vector3NormalizeOr(meshletFaceNormal, VectorZero(), ::s_FrameDirectionEpsilon);
        if(::FrameValidDirection(faceNormal))
            cutoff = Min(cutoff, VectorGetX(Vector3Dot(axis, faceNormal)));
    }
};

template<typename PositionAtT, typename VisitFaceNormalsT>
[[nodiscard]] MeshletBoundsCalculation CalculateMeshletBounds(u32 positionCount, u32 primitiveCount, const PositionAtT& positionAt, const VisitFaceNormalsT& visitFaceNormals)noexcept(
    noexcept(AabbTests::Expand(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>(), DeclVal<SIMDVectorSingle&>()))
    && noexcept(VectorSubtract(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>()))
    && noexcept(visitFaceNormals(AreaNormalAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<u32&>() }))
    && noexcept(visitFaceNormals(ConeCutoffAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<f32&>() }))
);
[[nodiscard]] inline MeshletBounds BuildMeshletBounds(const MeshCookEntry& entry, const MeshletDesc& meshlet)noexcept{
    const u32 positionCount = MeshletPositionCount(meshlet);
    NWB_ASSERT(positionCount <= s_MeshMaxMeshletVertices);
    alignas(SIMDVectorSingle) Array<SIMDVectorSingle, s_MeshMaxMeshletVertices> positions{};
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        positions[localPositionIndex] = VectorSetW(LoadFloat(entry.positions[ref.position]), 0.0f);
    }

    const auto positionAt = [&](const u32 localPositionIndex)noexcept{
        return positions[localPositionIndex];
    };
    const auto localPositionAt = [&](const u8 localVertexIndex)noexcept(noexcept(DeclVal<void(*)(const MeshletLocalVertexRef&)noexcept>()(entry.meshletLocalVertexRefs[meshlet.localVertexOffset + localVertexIndex]))){
        const MeshletLocalVertexRef& localVertexRef = entry.meshletLocalVertexRefs[meshlet.localVertexOffset + localVertexIndex];
        return positions[localVertexRef.localDeformedPosition];
    };
    const auto visitFaceNormals = [&](auto&& callback)noexcept(
        noexcept(DeclVal<void(*)(u8)noexcept>()(entry.meshletPrimitiveIndices[usize{}]))
        && noexcept(localPositionAt(u8{}))
        && noexcept(callback(TriangleTests::AreaNormal(SIMDVectorSingle{}, SIMDVectorSingle{}, SIMDVectorSingle{})))
    ){
        for(u32 primitiveIndex = 0u; primitiveIndex < MeshletPrimitiveCount(meshlet); ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * s_MeshletTriangleIndexCount;
            const u8 localVertex0 = entry.meshletPrimitiveIndices[primitiveOffset + 0u];
            const u8 localVertex1 = entry.meshletPrimitiveIndices[primitiveOffset + 1u];
            const u8 localVertex2 = entry.meshletPrimitiveIndices[primitiveOffset + 2u];
            const SIMDVectorSingle p0 = localPositionAt(localVertex0);
            const SIMDVectorSingle p1 = localPositionAt(localVertex1);
            const SIMDVectorSingle p2 = localPositionAt(localVertex2);
            callback(TriangleTests::AreaNormal(p0, p1, p2));
        }
    };
    const MeshletBoundsCalculation calculation = CalculateMeshletBounds(positionCount, MeshletPrimitiveCount(meshlet), positionAt, visitFaceNormals);

    MeshletBounds bounds;
    StoreFloat(VectorSetW(calculation.center, calculation.radius * s_MeshletBoundsRadiusInflation), bounds.sphere);
    bounds.conePacked = PackMeshletCone(calculation.coneAxis, calculation.coneCutoff);
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

