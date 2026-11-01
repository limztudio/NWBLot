// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlet_bounds.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshletBoundsBuilder::AreaNormalAccumulator::operator()(const SIMDVectorSingle faceNormal)const noexcept{
    if(!::FrameValidDirection(faceNormal))
        return;

    normal = VectorAdd(normal, faceNormal);
    ++count;
}

void MeshletBoundsBuilder::ConeCutoffAccumulator::operator()(const SIMDVectorSingle meshletFaceNormal)const noexcept{
    const SIMDVectorSingle faceNormal = Vector3NormalizeOr(meshletFaceNormal, VectorZero(), ::s_FrameDirectionEpsilon);
    if(::FrameValidDirection(faceNormal))
        cutoff = Min(cutoff, VectorGetX(Vector3Dot(axis, faceNormal)));
}

MeshletBounds MeshletBoundsBuilder::BuildMeshletBounds(const MeshCookEntry& entry, const MeshletDesc& meshlet)noexcept{
    const u32 positionCount = MeshletPositionCount(meshlet);
    NWB_ASSERT(positionCount <= s_MeshMaxMeshletVertices);
    Array<SIMDVectorSingle, s_MeshMaxMeshletVertices> positions{};
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

