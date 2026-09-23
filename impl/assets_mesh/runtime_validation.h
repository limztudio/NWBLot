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
    [[nodiscard]] static SIMDVector MakeMeshPositionVector(const SIMDVector position);
    [[nodiscard]] static SIMDVector MakeMeshNormalVector(const SIMDVector normal);
    [[nodiscard]] static SIMDVector MakeMeshTangentVector(const SIMDVector tangent);
    [[nodiscard]] static SIMDVector MakeMeshUvVector(const SIMDVector uv);
    [[nodiscard]] static SIMDVector MakeMeshColorVector(const SIMDVector color);
    [[nodiscard]] static bool ValidDirectionVector(const SIMDVector direction);
    [[nodiscard]] static bool ValidTangentVector(const SIMDVector tangent);
    [[nodiscard]] static bool ValidateMeshStreams(
    const Core::Assets::AssetVector<Float3U>& positions,
    const Core::Assets::AssetVector<Half4U>& normals,
    const Core::Assets::AssetVector<Half4U>& tangents,
    const Core::Assets::AssetVector<Float2U>& uv0,
    const Core::Assets::AssetVector<Half4U>& colors,
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
    );
    [[nodiscard]] static bool ValidateMeshletAttributeSkinSharing(
    const Core::Assets::AssetVector<u8>& positionRefDeltas,
    const Core::Assets::AssetVector<MeshletLocalVertexRef>& localVertexRefs,
    const Core::Assets::AssetVector<MeshletDesc>& meshlets,
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
    );
    [[nodiscard]] static bool ValidateMeshletPayload(
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
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
    );
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
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
    );
    template<typename MeshGeometryPayloadT>
    [[nodiscard]] static bool ValidateSharedMeshPayload(
    const MeshGeometryPayloadT& payload,
    const usize skinCount,
    const bool skinRequired,
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText
    );


public:
    MeshRuntimeValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshGeometryPayloadT>
[[nodiscard]] bool MeshRuntimeValidation::ValidateSharedMeshPayload(
    const MeshGeometryPayloadT& payload,
    const usize skinCount,
    const bool skinRequired,
    const NotNull<const tchar*> contextText,
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

