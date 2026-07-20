#pragma once


#include <core/assets/binary_payload_io.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshAssetBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename HeaderT>
[[nodiscard]] bool MeshBaseHeaderComplete(const HeaderT& header){
    return
        header.positionCount != 0u
        && header.normalCount != 0u
        && header.tangentCount != 0u
        && header.uv0Count != 0u
        && header.colorCount != 0u
        && header.meshletCount != 0u
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
    usize& inOutCursor,
    const HeaderT& header,
    PositionContainer& outPositions,
    NormalContainer& outNormals,
    TangentContainer& outTangents,
    Uv0Container& outUv0,
    ColorContainer& outColors,
    const tchar* failureContext
){
    return Core::Assets::ReadVectorPayload(binary, inOutCursor, header.positionCount, outPositions, failureContext, NWB_TEXT("positions"))
        && Core::Assets::ReadVectorPayload(binary, inOutCursor, header.normalCount, outNormals, failureContext, NWB_TEXT("normals"))
        && Core::Assets::ReadVectorPayload(binary, inOutCursor, header.tangentCount, outTangents, failureContext, NWB_TEXT("tangents"))
        && Core::Assets::ReadVectorPayload(binary, inOutCursor, header.uv0Count, outUv0, failureContext, NWB_TEXT("uv0"))
        && Core::Assets::ReadVectorPayload(binary, inOutCursor, header.colorCount, outColors, failureContext, NWB_TEXT("colors"))
    ;
}

template<
    typename HeaderT,
    typename MeshletContainer,
    typename MeshletBoundsContainer,
    typename MeshletPositionRefDeltaContainer,
    typename MeshletAttributeRefDeltaContainer,
    typename MeshletLocalVertexRefContainer,
    typename MeshletPrimitiveIndexContainer
>
[[nodiscard]] bool ReadMeshletStreams(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const HeaderT& header,
    MeshletContainer& outMeshlets,
    MeshletBoundsContainer& outMeshletBounds,
    MeshletPositionRefDeltaContainer& outMeshletPositionRefDeltas,
    MeshletAttributeRefDeltaContainer& outMeshletAttributeRefDeltas,
    MeshletLocalVertexRefContainer& outMeshletLocalVertexRefs,
    MeshletPrimitiveIndexContainer& outMeshletPrimitiveIndices,
    const tchar* failureContext
){
    if(!Core::Assets::ReadVectorPayload(binary, inOutCursor, header.meshletCount, outMeshlets, failureContext, NWB_TEXT("meshlets")))
        return false;
    if(!Core::Assets::ReadVectorPayload(
        binary,
        inOutCursor,
        header.meshletBoundCount,
        outMeshletBounds,
        failureContext,
        NWB_TEXT("meshlet bounds")
    ))
        return false;
    if(!Core::Assets::ReadVectorPayload(
        binary,
        inOutCursor,
        header.meshletPositionRefDeltaByteCount,
        outMeshletPositionRefDeltas,
        failureContext,
        NWB_TEXT("meshlet position ref deltas")
    ))
        return false;
    if(!Core::Assets::ReadVectorPayload(
        binary,
        inOutCursor,
        header.meshletAttributeRefDeltaByteCount,
        outMeshletAttributeRefDeltas,
        failureContext,
        NWB_TEXT("meshlet attribute ref deltas")
    ))
        return false;
    if(!Core::Assets::ReadVectorPayload(
        binary,
        inOutCursor,
        header.meshletLocalVertexRefCount,
        outMeshletLocalVertexRefs,
        failureContext,
        NWB_TEXT("meshlet local vertex refs")
    ))
        return false;
    return Core::Assets::ReadVectorPayload(
        binary,
        inOutCursor,
        header.meshletPrimitiveIndexCount,
        outMeshletPrimitiveIndices,
        failureContext,
        NWB_TEXT("meshlet primitive indices")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshT>
[[nodiscard]] bool AddMeshBaseReserveBytes(usize& reserveBytes, const MeshT& mesh){
    return AddBinaryVectorReserveBytes(reserveBytes, mesh.positionStream())
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
    ;
}

template<typename HeaderT, typename MeshT>
void FillMeshBaseHeader(HeaderT& header, const MeshT& mesh){
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
[[nodiscard]] bool AppendMeshAttributeStreams(
    Core::Assets::AssetBytes& outBinary,
    const MeshT& mesh,
    const tchar* failureContext
){
    return Core::Assets::AppendVectorPayload(outBinary, mesh.positionStream(), failureContext, NWB_TEXT("positions"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.normalStream(), failureContext, NWB_TEXT("normals"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.tangentStream(), failureContext, NWB_TEXT("tangents"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.uv0Stream(), failureContext, NWB_TEXT("uv0"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.colorStream(), failureContext, NWB_TEXT("colors"))
    ;
}

template<typename MeshT>
[[nodiscard]] bool AppendMeshletStreams(
    Core::Assets::AssetBytes& outBinary,
    const MeshT& mesh,
    const tchar* failureContext
){
    return Core::Assets::AppendVectorPayload(outBinary, mesh.meshlets(), failureContext, NWB_TEXT("meshlets"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletBounds(), failureContext, NWB_TEXT("meshlet bounds"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletPositionRefDeltas(), failureContext, NWB_TEXT("meshlet position ref deltas"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletAttributeRefDeltas(), failureContext, NWB_TEXT("meshlet attribute ref deltas"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletLocalVertexRefs(), failureContext, NWB_TEXT("meshlet local vertex refs"))
        && Core::Assets::AppendVectorPayload(outBinary, mesh.meshletPrimitiveIndices(), failureContext, NWB_TEXT("meshlet primitive indices"))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

