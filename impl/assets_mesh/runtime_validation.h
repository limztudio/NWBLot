// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshRuntimeValidation final : NoCopy{
public:
    [[nodiscard]] static SIMDVectorSingle MakeMeshPositionVector(const SIMDVectorSingle position)noexcept;
    [[nodiscard]] static SIMDVectorSingle MakeMeshNormalVector(const SIMDVectorSingle normal)noexcept;
    [[nodiscard]] static SIMDVectorSingle MakeMeshUvVector(const SIMDVectorSingle uv)noexcept;
    [[nodiscard]] static b8 ValidDirectionVector(const SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static b8 ValidTangentVector(const SIMDVectorSingle tangent)noexcept;
    [[nodiscard]] static b8 ValidateMeshStreams(
        Span<const Float3U> positions,
        Span<const Half4U> normals,
        Span<const Half4U> tangents,
        Span<const Float2U> uv0,
        Span<const Half4U> colors,
        const TStringView contextText,
        const TStringView meshPathText
    );
    [[nodiscard]] static b8 ValidateMeshletAttributeSkinSharing(
        Core::Alloc::ScratchArena& scratchArena,
        Span<const u8> positionRefDeltas,
        Span<const MeshletLocalVertexRef> localVertexRefs,
        Span<const MeshletDesc> meshlets,
        const TStringView contextText,
        const TStringView meshPathText
    );
    [[nodiscard]] static b8 ValidateMeshletPayload(
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
        const b8 skinRequired,
        const TStringView contextText,
        const TStringView meshPathText
    );
    [[nodiscard]] static b8 ValidateSharedMeshPayload(
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
        const b8 skinRequired,
        const TStringView contextText,
        const TStringView meshPathText
    );
    template<typename MeshGeometryPayloadT>
    [[nodiscard]] static b8 ValidateSharedMeshPayload(Core::Alloc::ScratchArena& scratchArena, const MeshGeometryPayloadT& payload, const usize skinCount, const b8 skinRequired, const TStringView contextText, const TStringView meshPathText);


public:
    MeshRuntimeValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshGeometryPayloadT>
[[nodiscard]] b8 MeshRuntimeValidation::ValidateSharedMeshPayload(Core::Alloc::ScratchArena& scratchArena, const MeshGeometryPayloadT& payload, const usize skinCount, const b8 skinRequired, const TStringView contextText, const TStringView meshPathText){
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

