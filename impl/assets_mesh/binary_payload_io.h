// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "meshlet_constants.h"
#include "payload_types.h"

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


template<typename HeaderT>
[[nodiscard]] bool MeshBaseHeaderComplete(const HeaderT& header)noexcept(
    noexcept(
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
    )
    && IsNothrowConvertible_V<decltype(
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
    ), bool>
){
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

template<
    typename HeaderT,
    typename PositionContainer,
    typename NormalContainer,
    typename TangentContainer,
    typename Uv0Container,
    typename ColorContainer
>
[[nodiscard]] bool ReadMeshAttributeStreams(
    const Core::Assets::AssetBytes& binary,
    const HeaderT& header,
    const TStringView failureContext,
    usize& inOutCursor,
    PositionContainer& outPositions,
    NormalContainer& outNormals,
    TangentContainer& outTangents,
    Uv0Container& outUv0,
    ColorContainer& outColors
){
    return
        Core::Assets::ReadVectorPayload(binary, header.positionCount, failureContext, s_PositionsStreamLabel, inOutCursor, outPositions)
        && Core::Assets::ReadVectorPayload(binary, header.normalCount, failureContext, s_NormalsStreamLabel, inOutCursor, outNormals)
        && Core::Assets::ReadVectorPayload(binary, header.tangentCount, failureContext, s_TangentsStreamLabel, inOutCursor, outTangents)
        && Core::Assets::ReadVectorPayload(binary, header.uv0Count, failureContext, s_Uv0StreamLabel, inOutCursor, outUv0)
        && Core::Assets::ReadVectorPayload(binary, header.colorCount, failureContext, s_ColorsStreamLabel, inOutCursor, outColors)
    ;
}

template<
    typename HeaderT,
    typename MeshletContainer,
    typename MeshletBoundsContainer,
    typename MeshletPositionRefDeltaContainer,
    typename MeshletAttributeRefDeltaContainer,
    typename MeshletLocalVertexRefContainer,
    typename MeshletPrimitiveIndexContainer,
    typename SolidTriangleWordContainer
>
[[nodiscard]] bool ReadMeshletStreams(
    const Core::Assets::AssetBytes& binary,
    const HeaderT& header,
    const TStringView failureContext,
    usize& inOutCursor,
    MeshletContainer& outMeshlets,
    MeshletBoundsContainer& outMeshletBounds,
    MeshletPositionRefDeltaContainer& outMeshletPositionRefDeltas,
    MeshletAttributeRefDeltaContainer& outMeshletAttributeRefDeltas,
    MeshletLocalVertexRefContainer& outMeshletLocalVertexRefs,
    MeshletPrimitiveIndexContainer& outMeshletPrimitiveIndices,
    SolidTriangleWordContainer& outSolidTriangleWords
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
[[nodiscard]] bool AddMeshBaseReserveBytes(usize& reserveBytes, const MeshT& mesh)noexcept(
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

template<typename HeaderT, typename MeshT>
void FillMeshBaseHeader(HeaderT& header, const MeshT& mesh)noexcept(
    noexcept(header.meshClass = mesh.meshClass())
    && noexcept(header.positionCount = static_cast<u64>(mesh.positionStream().size()))
    && noexcept(header.normalCount = static_cast<u64>(mesh.normalStream().size()))
    && noexcept(header.tangentCount = static_cast<u64>(mesh.tangentStream().size()))
    && noexcept(header.uv0Count = static_cast<u64>(mesh.uv0Stream().size()))
    && noexcept(header.colorCount = static_cast<u64>(mesh.colorStream().size()))
    && noexcept(header.meshletCount = static_cast<u64>(mesh.meshlets().size()))
    && noexcept(header.meshletBoundCount = static_cast<u64>(mesh.meshletBounds().size()))
    && noexcept(header.meshletPositionRefDeltaByteCount = static_cast<u64>(mesh.meshletPositionRefDeltas().size()))
    && noexcept(header.meshletAttributeRefDeltaByteCount = static_cast<u64>(mesh.meshletAttributeRefDeltas().size()))
    && noexcept(header.meshletLocalVertexRefCount = static_cast<u64>(mesh.meshletLocalVertexRefs().size()))
    && noexcept(header.meshletPrimitiveIndexCount = static_cast<u64>(mesh.meshletPrimitiveIndices().size()))
){
    header.meshClass = mesh.meshClass();
    header.positionCount = static_cast<u64>(mesh.positionStream().size());
    header.normalCount = static_cast<u64>(mesh.normalStream().size());
    header.tangentCount = static_cast<u64>(mesh.tangentStream().size());
    header.uv0Count = static_cast<u64>(mesh.uv0Stream().size());
    header.colorCount = static_cast<u64>(mesh.colorStream().size());
    header.meshletCount = static_cast<u64>(mesh.meshlets().size());
    header.meshletBoundCount = static_cast<u64>(mesh.meshletBounds().size());
    header.meshletPositionRefDeltaByteCount = static_cast<u64>(mesh.meshletPositionRefDeltas().size());
    header.meshletAttributeRefDeltaByteCount = static_cast<u64>(mesh.meshletAttributeRefDeltas().size());
    header.meshletLocalVertexRefCount = static_cast<u64>(mesh.meshletLocalVertexRefs().size());
    header.meshletPrimitiveIndexCount = static_cast<u64>(mesh.meshletPrimitiveIndices().size());
}

template<typename MeshT>
[[nodiscard]] bool AppendMeshAttributeStreams(Core::Assets::AssetBytes& outBinary, const MeshT& mesh, const TStringView failureContext){
    return
        Core::Assets::AppendVectorPayload(outBinary, mesh.positionStream(), failureContext, s_PositionsStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.normalStream(), failureContext, s_NormalsStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.tangentStream(), failureContext, s_TangentsStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.uv0Stream(), failureContext, s_Uv0StreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.colorStream(), failureContext, s_ColorsStreamLabel)
    ;
}

template<typename MeshT>
[[nodiscard]] bool AppendMeshletStreams(Core::Assets::AssetBytes& outBinary, const MeshT& mesh, const TStringView failureContext){
    return
        Core::Assets::AppendVectorPayload(outBinary, mesh.meshlets(), failureContext, s_MeshletsStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletBounds(), failureContext, s_MeshletBoundsStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletPositionRefDeltas(), failureContext, s_MeshletPositionRefDeltasStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletAttributeRefDeltas(), failureContext, s_MeshletAttributeRefDeltasStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletLocalVertexRefs(), failureContext, s_MeshletLocalVertexRefsStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletPrimitiveIndices(), failureContext, s_MeshletPrimitiveIndicesStreamLabel)
        && Core::Assets::AppendVectorPayload(outBinary, mesh.solidTriangleWords(), failureContext, s_SolidTriangleWordsStreamLabel)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

