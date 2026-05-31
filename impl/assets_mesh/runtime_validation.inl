// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshPayloadValidation::CountFitsU32;
using MeshPayloadValidation::FiniteVector;

#include "meshlet_ref_validation.inl"
#include "runtime_validation_diagnostics.inl"
#include "runtime_validation_streams.inl"
#include "runtime_validation_meshlets.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateSharedMeshPayload(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<u8>& attributeRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const Core::Assets::AssetVector<MeshletBounds>& meshletBounds,
    const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices,
    const usize skinCount,
    const bool skinRequired,
    const tchar* contextText,
    const TStringView meshPathText
){
    if(!ValidateMeshStreams(positions, normals, tangents, uv0, colors, contextText, meshPathText))
        return false;

    return ValidateMeshletPayload(
        positionRefDeltas,
        attributeRefDeltas,
        localVertexRefs,
        meshlets,
        meshletBounds,
        meshletPrimitiveIndices,
        positions.size(),
        skinCount,
        normals.size(),
        tangents.size(),
        uv0.size(),
        colors.size(),
        skinRequired,
        contextText,
        meshPathText
    );
}

template<typename MeshGeometryPayloadT>
[[nodiscard]] static bool ValidateSharedMeshPayload(
    const MeshGeometryPayloadT& payload,
    const usize skinCount,
    const bool skinRequired,
    const tchar* contextText,
    const TStringView meshPathText
){
    return ValidateSharedMeshPayload(
        payload.positionStream(),
        payload.normalStream(),
        payload.tangentStream(),
        payload.uv0Stream(),
        payload.colorStream(),
        payload.meshletPositionRefDeltas(),
        payload.meshletAttributeRefDeltas(),
        payload.meshletLocalVertexRefs(),
        payload.meshlets(),
        payload.meshletBounds(),
        payload.meshletPrimitiveIndices(),
        skinCount,
        skinRequired,
        contextText,
        meshPathText
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

