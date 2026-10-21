// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_MeshletBoundsRadiusInflation = 1.25f;

struct MeshletBoundsCalculation{
    SIMDVectorSingle center = {};
    SIMDVectorSingle coneAxis = {};
    SIMDVectorSingle radius = VectorZero();
    SIMDVectorSingle coneCutoff = s_SIMDNegativeOne;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Bounding sphere and normal cone of one finished meshlet.
class MeshletBoundsBuilder final : NoCopy{
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
        SIMDVectorSingle& cutoff;


    public:
        void operator()(const SIMDVectorSingle meshletFaceNormal)const noexcept{
            const SIMDVectorSingle faceNormal = MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(meshletFaceNormal);
            if(::FrameValidDirection(faceNormal))
                cutoff = VectorMin(cutoff, Vector3Dot(axis, faceNormal));
        }
    };


public:
    template<typename PositionAtT, typename VisitFaceNormalsT>
    [[nodiscard]] static MeshletBoundsCalculation CalculateMeshletBounds(const u32 positionCount, const u32 primitiveCount, const PositionAtT& positionAt, const VisitFaceNormalsT& visitFaceNormals)noexcept(
        noexcept(AabbTests::Expand(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>(), DeclVal<SIMDVectorSingle&>()))
        && noexcept(VectorSubtract(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>()))
        && noexcept(visitFaceNormals(AreaNormalAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<u32&>() }))
        && noexcept(visitFaceNormals(ConeCutoffAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<SIMDVectorSingle&>() }))
    );
    template<typename CookEntryT>
    static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet)noexcept(
        noexcept(DeclVal<void(*)(const MeshletPositionStreamRef&)noexcept>()(entry.meshletPositionStreamRefs[meshlet.positionRefOffset + DeclVal<u32&>()]))
        && noexcept(MeshletTriangleGeometry::MakeMeshletPositionVector(LoadFloat(entry.positions[DeclVal<const MeshletPositionStreamRef&>().position])))
        && noexcept(DeclVal<void(*)(const MeshletLocalVertexRef&)noexcept>()(entry.meshletLocalVertexRefs[meshlet.localVertexOffset + DeclVal<const u8&>()]))
        && noexcept(DeclVal<void(*)(u8)noexcept>()(entry.meshletPrimitiveIndices[DeclVal<usize>()]))
    );


public:
    MeshletBoundsBuilder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PositionAtT, typename VisitFaceNormalsT>
MeshletBoundsCalculation MeshletBoundsBuilder::CalculateMeshletBounds(const u32 positionCount, const u32 primitiveCount, const PositionAtT& positionAt, const VisitFaceNormalsT& visitFaceNormals)noexcept(
    noexcept(AabbTests::Expand(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>(), DeclVal<SIMDVectorSingle&>()))
    && noexcept(VectorSubtract(positionAt(DeclVal<u32&>()), DeclVal<SIMDVectorSingle&>()))
    && noexcept(visitFaceNormals(AreaNormalAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<u32&>() }))
    && noexcept(visitFaceNormals(ConeCutoffAccumulator{ DeclVal<SIMDVectorSingle&>(), DeclVal<SIMDVectorSingle&>() }))
){
    MeshletBoundsCalculation result;

    SIMDVectorSingle minBounds;
    SIMDVectorSingle maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex)
        AabbTests::Expand(positionAt(localPositionIndex), minBounds, maxBounds);

    result.center = AabbTests::Center(minBounds, maxBounds);
    SIMDVectorSingle radiusSquared = VectorZero();
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex){
        const SIMDVectorSingle delta = VectorSubtract(positionAt(localPositionIndex), result.center);
        radiusSquared = VectorMax(radiusSquared, Vector3LengthSq(delta));
    }
    result.radius = VectorSqrt(radiusSquared);

    SIMDVectorSingle areaWeightedNormal = VectorZero();
    u32 validFaceNormalCount = 0u;
    visitFaceNormals(AreaNormalAccumulator{ areaWeightedNormal, validFaceNormalCount });

    result.coneAxis = MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(areaWeightedNormal);
    if(validFaceNormalCount == primitiveCount && ::FrameValidDirection(result.coneAxis)){
        result.coneCutoff = s_SIMDOne;
        visitFaceNormals(ConeCutoffAccumulator{ result.coneAxis, result.coneCutoff });
        if(VectorGetX(result.coneCutoff) <= 0.0f)
            result.coneCutoff = s_SIMDNegativeOne;
    }

    return result;
}


template<typename CookEntryT>
MeshletBounds MeshletBoundsBuilder::BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet)noexcept(
    noexcept(DeclVal<void(*)(const MeshletPositionStreamRef&)noexcept>()(entry.meshletPositionStreamRefs[meshlet.positionRefOffset + DeclVal<u32&>()]))
    && noexcept(MeshletTriangleGeometry::MakeMeshletPositionVector(LoadFloat(entry.positions[DeclVal<const MeshletPositionStreamRef&>().position])))
    && noexcept(DeclVal<void(*)(const MeshletLocalVertexRef&)noexcept>()(entry.meshletLocalVertexRefs[meshlet.localVertexOffset + DeclVal<const u8&>()]))
    && noexcept(DeclVal<void(*)(u8)noexcept>()(entry.meshletPrimitiveIndices[DeclVal<usize>()]))
){
    const u32 positionCount = MeshletPositionCount(meshlet);
    NWB_ASSERT(positionCount <= s_MeshMaxMeshletVertices);
    SIMDVectorSingle positions[s_MeshMaxMeshletVertices];
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        positions[localPositionIndex] = MeshletTriangleGeometry::MakeMeshletPositionVector(LoadFloat(entry.positions[ref.position]));
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
    StoreFloat(VectorSelect(calculation.center, VectorScale(calculation.radius, s_MeshletBoundsRadiusInflation), s_SIMDMaskW), bounds.sphere);
    bounds.conePacked = PackMeshletCone(calculation.coneAxis, calculation.coneCutoff);
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

