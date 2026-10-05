// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "meshlet_payload_packing.h"
#include "meshlet_ref_codec.h"
#include "meshlet_ref_validation.h"
#include "runtime_validation_diagnostics.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mesh payload stream and meshlet validation.


class MeshRuntimeValidation final : NoCopy{
public:
    [[nodiscard]] static SIMDVector makeMeshPositionVector(const SIMDVector position);
    [[nodiscard]] static SIMDVector makeMeshNormalVector(const SIMDVector normal);
    [[nodiscard]] static SIMDVector makeMeshTangentVector(const SIMDVector tangent);
    [[nodiscard]] static SIMDVector makeMeshUvVector(const SIMDVector uv);
    [[nodiscard]] static SIMDVector makeMeshColorVector(const SIMDVector color);
    [[nodiscard]] static bool validDirectionVector(const SIMDVector direction);
    [[nodiscard]] static bool validTangentVector(const SIMDVector tangent);
    [[nodiscard]] static bool validateMeshStreams(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const TStringView contextText,
    const TStringView meshPathText
    );
    [[nodiscard]] static bool validateMeshletAttributeSkinSharing(
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const TStringView contextText,
    const TStringView meshPathText
    );
    [[nodiscard]] static bool validateMeshletPayload(
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<u8>& attributeRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const Core::Assets::AssetVector<MeshletBounds>& meshletBounds,
    const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices,
    const usize positionCount,
    const usize skinCount,
    const usize normalCount,
    const usize tangentCount,
    const usize uv0Count,
    const usize colorCount,
    const bool skinRequired,
    const TStringView contextText,
    const TStringView meshPathText
    );
    [[nodiscard]] static bool validateSharedMeshPayload(
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
    const TStringView contextText,
    const TStringView meshPathText
    );
    template<typename MeshGeometryPayloadT>
    [[nodiscard]] static bool validateSharedMeshPayload(
    const MeshGeometryPayloadT& payload,
    const usize skinCount,
    const bool skinRequired,
    const TStringView contextText,
    const TStringView meshPathText
    );


public:
    MeshRuntimeValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshGeometryPayloadT>
[[nodiscard]] bool MeshRuntimeValidation::validateSharedMeshPayload(
    const MeshGeometryPayloadT& payload,
    const usize skinCount,
    const bool skinRequired,
    const TStringView contextText,
    const TStringView meshPathText
){
    return validateSharedMeshPayload(
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

