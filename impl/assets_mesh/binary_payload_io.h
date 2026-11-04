// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_mesh/payload_validation.adhd.h>

#include <core/assets/binary_payload_io.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_MeshLoadBinaryContext = NWB_TEXT("Mesh::loadBinary");
#if defined(NWB_COOK)
inline constexpr TStringView s_MeshSerializeContext = NWB_TEXT("MeshAssetCodec::serialize");
#endif
inline constexpr TStringView s_MeshAssetKindLabel = NWB_TEXT("mesh");

static_assert(sizeof(MeshBinaryHeader) == sizeof(u32) + (sizeof(u64) * 11u), "MeshBinaryHeader layout drifted");
static_assert(alignof(MeshBinaryHeader) == 1u, "MeshBinaryHeader must stay packed");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshAssetBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_PositionStreamNameView = NWB_TEXT("position");
inline constexpr TStringView s_NormalStreamNameView = NWB_TEXT("normal");
inline constexpr TStringView s_TangentStreamNameView = NWB_TEXT("tangent");
inline constexpr TStringView s_Uv0StreamNameView = NWB_TEXT("uv0");
inline constexpr TStringView s_ColorStreamNameView = NWB_TEXT("color");
inline constexpr TStringView s_PositionsStreamLabel = NWB_TEXT("positions");
inline constexpr TStringView s_NormalsStreamLabel = NWB_TEXT("normals");
inline constexpr TStringView s_TangentsStreamLabel = NWB_TEXT("tangents");
inline constexpr TStringView s_Uv0StreamLabel = NWB_TEXT("uv0");
inline constexpr TStringView s_ColorsStreamLabel = NWB_TEXT("colors");
inline constexpr TStringView s_MeshletsStreamLabel = NWB_TEXT("meshlets");
inline constexpr TStringView s_MeshletBoundsStreamLabel = NWB_TEXT("meshlet bounds");
inline constexpr TStringView s_MeshletPositionRefDeltasStreamLabel = NWB_TEXT("meshlet position ref deltas");
inline constexpr TStringView s_MeshletAttributeRefDeltasStreamLabel = NWB_TEXT("meshlet attribute ref deltas");
inline constexpr TStringView s_MeshletLocalVertexRefsStreamLabel = NWB_TEXT("meshlet local vertex refs");
inline constexpr TStringView s_MeshletPrimitiveIndicesStreamLabel = NWB_TEXT("meshlet primitive indices");
inline constexpr TStringView s_SolidTriangleWordsStreamLabel = NWB_TEXT("solid triangle words");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ReadMeshAttributeStreams(
    Span<const u8> binary,
    const MeshBinaryHeader& header,
    TStringView failureContext,
    usize& inOutCursor,
    Core::Assets::AssetVector<Float3U>& outPositions,
    Core::Assets::AssetVector<Half4U>& outNormals,
    Core::Assets::AssetVector<Half4U>& outTangents,
    Core::Assets::AssetVector<Float2U>& outUv0,
    Core::Assets::AssetVector<Half4U>& outColors
);

[[nodiscard]] b8 ReadMeshletStreams(
    Span<const u8> binary,
    const MeshBinaryHeader& header,
    TStringView failureContext,
    usize& inOutCursor,
    Core::Assets::AssetVector<MeshletDesc>& outMeshlets,
    Core::Assets::AssetVector<MeshletBounds>& outMeshletBounds,
    Core::Assets::AssetVector<u8>& outMeshletPositionRefDeltas,
    Core::Assets::AssetVector<u8>& outMeshletAttributeRefDeltas,
    Core::Assets::AssetVector<MeshletLocalVertexRef>& outMeshletLocalVertexRefs,
    Core::Assets::AssetVector<u8>& outMeshletPrimitiveIndices,
    Core::Assets::AssetVector<u32>& outSolidTriangleWords
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshT>
[[nodiscard]] b8 AddMeshBaseReserveBytes(const MeshT& mesh, usize& inOutReserveBytes)noexcept(
    noexcept(
        AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.positionStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.normalStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.tangentStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.uv0Stream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.colorStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshlets())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletBounds())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletPositionRefDeltas())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletAttributeRefDeltas())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletLocalVertexRefs())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletPrimitiveIndices())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.solidTriangleWords())
    )
){
    return
        AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.positionStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.normalStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.tangentStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.uv0Stream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.colorStream())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshlets())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletBounds())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletPositionRefDeltas())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletAttributeRefDeltas())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletLocalVertexRefs())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.meshletPrimitiveIndices())
        && AddBinaryVectorReserveBytes(inOutReserveBytes, mesh.solidTriangleWords())
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
