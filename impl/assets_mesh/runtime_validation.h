// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshRuntimeValidation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ValidDirectionVector(SIMDVectorSingle direction)noexcept;
[[nodiscard]] b8 ValidTangentVector(SIMDVectorSingle tangent)noexcept;
[[nodiscard]] b8 ValidateMeshStreams(Span<const Float3U> positions, Span<const Half4U> normals, Span<const Half4U> tangents, Span<const Float2U> uv0, Span<const Half4U> colors, TStringView contextText, TStringView meshPathText);
[[nodiscard]] b8 ValidateMeshletPayload(
    Span<const u8> positionRefDeltas,
    Span<const u8> attributeRefDeltas,
    Span<const MeshletLocalVertexRef> localVertexRefs,
    Span<const MeshletDesc> meshlets,
    Span<const MeshletBounds> meshletBounds,
    Span<const u8> meshletPrimitiveIndices,
    usize positionCount,
    usize normalCount,
    usize tangentCount,
    usize uv0Count,
    usize colorCount,
    TStringView contextText,
    TStringView meshPathText
);
[[nodiscard]] b8 ValidateSharedMeshPayload(
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
    TStringView contextText,
    TStringView meshPathText
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

