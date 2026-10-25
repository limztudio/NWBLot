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


class MeshRuntimeValidation final : NoCopy{
public:
    [[nodiscard]] static SIMDVectorSingle MakeMeshPositionVector(const SIMDVectorSingle position)noexcept;
    [[nodiscard]] static SIMDVectorSingle MakeMeshNormalVector(const SIMDVectorSingle normal)noexcept;
    [[nodiscard]] static SIMDVectorSingle MakeMeshUvVector(const SIMDVectorSingle uv)noexcept;
    [[nodiscard]] static bool ValidDirectionVector(const SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static bool ValidTangentVector(const SIMDVectorSingle tangent)noexcept;
    [[nodiscard]] static bool ValidateMeshStreams(
        Span<const Float3U> positions,
        Span<const Half4U> normals,
        Span<const Half4U> tangents,
        Span<const Float2U> uv0,
        Span<const Half4U> colors,
        const TStringView contextText,
        const TStringView meshPathText
    );
    [[nodiscard]] static bool ValidateMeshletAttributeSkinSharing(
        Core::Alloc::ScratchArena& scratchArena,
        Span<const u8> positionRefDeltas,
        Span<const MeshletLocalVertexRef> localVertexRefs,
        Span<const MeshletDesc> meshlets,
        const TStringView contextText,
        const TStringView meshPathText
    );
    [[nodiscard]] static bool ValidateMeshletPayload(
        Core::Alloc::ScratchArena& scratchArena,
        Span<const u8> positionRefDeltas,
        Span<const u8> attributeRefDeltas,
        Span<const MeshletLocalVertexRef> localVertexRefs,
        Span<const MeshletDesc> meshlets,
        Span<const MeshletBounds> meshletBounds,
        Span<const u8> meshletPrimitiveIndices,
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
    [[nodiscard]] static bool ValidateSharedMeshPayload(
        Core::Alloc::ScratchArena& scratchArena,
        Span<const Float3U> positions,
        Span<const Half4U> normals,
        Span<const Half4U> tangents,
        Span<const Float2U> uv0,
        Span<const Half4U> colors,
        Span<const u8> positionRefDeltas,
        Span<const u8> attributeRefDeltas,
        Span<const MeshletLocalVertexRef> localVertexRefs,
        Span<const MeshletDesc> meshlets,
        Span<const MeshletBounds> meshletBounds,
        Span<const u8> meshletPrimitiveIndices,
        const usize skinCount,
        const bool skinRequired,
        const TStringView contextText,
        const TStringView meshPathText
    );
    template<typename MeshGeometryPayloadT>
    [[nodiscard]] static bool ValidateSharedMeshPayload(Core::Alloc::ScratchArena& scratchArena, const MeshGeometryPayloadT& payload, const usize skinCount, const bool skinRequired, const TStringView contextText, const TStringView meshPathText);


public:
    MeshRuntimeValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshGeometryPayloadT>
[[nodiscard]] bool MeshRuntimeValidation::ValidateSharedMeshPayload(Core::Alloc::ScratchArena& scratchArena, const MeshGeometryPayloadT& payload, const usize skinCount, const bool skinRequired, const TStringView contextText, const TStringView meshPathText){
    return ValidateSharedMeshPayload(
        scratchArena,
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

