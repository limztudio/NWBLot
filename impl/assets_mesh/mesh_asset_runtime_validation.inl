// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshPayloadValidation::CountFitsU32;
using MeshPayloadValidation::FiniteVector;

#include "meshlet_ref_validation.inl"
#include "mesh_asset_runtime_validation_streams.inl"
#include "mesh_asset_runtime_validation_meshlets.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateSharedMeshPayload(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const Core::Assets::AssetVector<MeshletDeformedPositionRef>& positionRefs,
    const Core::Assets::AssetVector<MeshletShadingAttributeRef>& attributeRefs,
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

    if(!ValidateMeshletPositionRefs(
        positionRefs,
        positions.size(),
        skinCount,
        skinRequired,
        contextText,
        meshPathText
    ))
        return false;

    return ValidateMeshletPayload(
        positionRefs,
        attributeRefs,
        localVertexRefs,
        meshlets,
        meshletBounds,
        meshletPrimitiveIndices,
        normals.size(),
        tangents.size(),
        uv0.size(),
        colors.size(),
        skinRequired,
        contextText,
        meshPathText
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

