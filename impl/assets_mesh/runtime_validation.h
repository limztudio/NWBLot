// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"


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
    [[nodiscard]] static b8 ValidateMeshStreams(Span<const Float3U> positions, Span<const Half4U> normals, Span<const Half4U> tangents, Span<const Float2U> uv0, Span<const Half4U> colors, const TStringView contextText, const TStringView meshPathText);
    [[nodiscard]] static b8 ValidateMeshletPayload(
        Span<const u8> positionRefDeltas,
        Span<const u8> attributeRefDeltas,
        Span<const MeshletLocalVertexRef> localVertexRefs,
        Span<const MeshletDesc> meshlets,
        Span<const MeshletBounds> meshletBounds,
        Span<const u8> meshletPrimitiveIndices,
        const usize positionCount,
        const usize normalCount,
        const usize tangentCount,
        const usize uv0Count,
        const usize colorCount,
        const TStringView contextText,
        const TStringView meshPathText
    );
    [[nodiscard]] static b8 ValidateSharedMeshPayload(
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
        const TStringView contextText,
        const TStringView meshPathText
    );
    [[nodiscard]] static b8 ValidateSharedMeshPayload(const Mesh& payload, const TStringView contextText, const TStringView meshPathText);


public:
    MeshRuntimeValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

