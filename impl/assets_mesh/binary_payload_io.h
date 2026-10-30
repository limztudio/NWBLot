// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "meshlet_constants.h"
#include <impl/assets_mesh/payload_types.adhd.h>

#include <core/assets/binary_payload_io.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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


[[nodiscard]] inline b8 MeshBaseHeaderComplete(const MeshBinaryHeader& header)noexcept{
    return
        header.positionCount != 0u
        && header.normalCount != 0u
        && header.tangentCount != 0u
        && header.uv0Count != 0u
        && header.colorCount != 0u
        && header.meshletCount != 0u
        && header.meshletCount <= static_cast<u64>(Limit<u32>::s_Max / NWB_MESHLET_BOUNDS_STRIDE)
        && header.meshletBoundCount == header.meshletCount
        && header.meshletPositionRefDeltaByteCount != 0u
        && header.meshletAttributeRefDeltaByteCount != 0u
        && header.meshletLocalVertexRefCount != 0u
        && header.meshletPrimitiveIndexCount != 0u
    ;
}

[[nodiscard]] inline b8 ReadMeshAttributeStreams(
    const Span<const u8> binary,
    const MeshBinaryHeader& header,
    const TStringView failureContext,
    usize& inOutCursor,
    Core::Assets::AssetVector<Float3U>& outPositions,
    Core::Assets::AssetVector<Half4U>& outNormals,
    Core::Assets::AssetVector<Half4U>& outTangents,
    Core::Assets::AssetVector<Float2U>& outUv0,
    Core::Assets::AssetVector<Half4U>& outColors
){
    return
        Core::Assets::ReadVectorPayload(binary, header.positionCount, failureContext, s_PositionsStreamLabel, inOutCursor, outPositions)
        && Core::Assets::ReadVectorPayload(binary, header.normalCount, failureContext, s_NormalsStreamLabel, inOutCursor, outNormals)
        && Core::Assets::ReadVectorPayload(binary, header.tangentCount, failureContext, s_TangentsStreamLabel, inOutCursor, outTangents)
        && Core::Assets::ReadVectorPayload(binary, header.uv0Count, failureContext, s_Uv0StreamLabel, inOutCursor, outUv0)
        && Core::Assets::ReadVectorPayload(binary, header.colorCount, failureContext, s_ColorsStreamLabel, inOutCursor, outColors)
    ;
}

[[nodiscard]] inline b8 ReadMeshletStreams(
    const Span<const u8> binary,
    const MeshBinaryHeader& header,
    const TStringView failureContext,
    usize& inOutCursor,
    Core::Assets::AssetVector<MeshletDesc>& outMeshlets,
    Core::Assets::AssetVector<MeshletBounds>& outMeshletBounds,
    Core::Assets::AssetVector<u8>& outMeshletPositionRefDeltas,
    Core::Assets::AssetVector<u8>& outMeshletAttributeRefDeltas,
    Core::Assets::AssetVector<MeshletLocalVertexRef>& outMeshletLocalVertexRefs,
    Core::Assets::AssetVector<u8>& outMeshletPrimitiveIndices,
    Core::Assets::AssetVector<u32>& outSolidTriangleWords
){
    if(!Core::Assets::ReadVectorPayload(binary, header.meshletCount, failureContext, s_MeshletsStreamLabel, inOutCursor, outMeshlets))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.meshletBoundCount, failureContext, s_MeshletBoundsStreamLabel, inOutCursor, outMeshletBounds))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.meshletPositionRefDeltaByteCount, failureContext, s_MeshletPositionRefDeltasStreamLabel, inOutCursor, outMeshletPositionRefDeltas))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.meshletAttributeRefDeltaByteCount, failureContext, s_MeshletAttributeRefDeltasStreamLabel, inOutCursor, outMeshletAttributeRefDeltas))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.meshletLocalVertexRefCount, failureContext, s_MeshletLocalVertexRefsStreamLabel, inOutCursor, outMeshletLocalVertexRefs))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.meshletPrimitiveIndexCount, failureContext, s_MeshletPrimitiveIndicesStreamLabel, inOutCursor, outMeshletPrimitiveIndices))
        return false;
    return Core::Assets::ReadVectorPayload(binary, (header.meshletPrimitiveIndexCount / s_MeshletTriangleIndexCount + s_MeshletSolidTriangleWordRoundUp) / s_MeshletSolidTriangleWordBits, failureContext, s_SolidTriangleWordsStreamLabel, inOutCursor, outSolidTriangleWords);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshT>
[[nodiscard]] b8 AddMeshBaseReserveBytes(usize& reserveBytes, const MeshT& mesh)noexcept(
    noexcept(
        AddBinaryVectorReserveBytes(reserveBytes, mesh.positionStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.normalStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.tangentStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.uv0Stream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.colorStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshlets())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletBounds())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletPositionRefDeltas())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletAttributeRefDeltas())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletLocalVertexRefs())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletPrimitiveIndices())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.solidTriangleWords())
    )
){
    return
        AddBinaryVectorReserveBytes(reserveBytes, mesh.positionStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.normalStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.tangentStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.uv0Stream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.colorStream())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshlets())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletBounds())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletPositionRefDeltas())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletAttributeRefDeltas())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletLocalVertexRefs())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.meshletPrimitiveIndices())
        && AddBinaryVectorReserveBytes(reserveBytes, mesh.solidTriangleWords())
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

