
#pragma once


#include <core/assets/binary_payload_io.h>
#include <global/binary.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshAssetBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool ReadVector(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    ValueContainer& outValues,
    const tchar* failureContext,
    const tchar* label
){
    return Core::Assets::ReadVectorPayload(binary, inOutCursor, count, outValues, failureContext, label);
}


template<typename Header>
[[nodiscard]] bool ReadHeader(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    Header& outHeader,
    const u32 expectedMagic,
    const tchar* failureContext
){
    if(!ReadPOD(binary, inOutCursor, outHeader)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed header"), failureContext);
        return false;
    }

    if(outHeader.magic != expectedMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: invalid mesh asset format; recook required"), failureContext);
        return false;
    }

    return true;
}

[[nodiscard]] inline bool ReadComplete(
    const Core::Assets::AssetBytes& binary,
    const usize cursor,
    const tchar* failureContext
){
    return Core::Assets::ReadCompletePayload(binary, cursor, failureContext);
}

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
    return ReadVector(binary, inOutCursor, header.positionCount, outPositions, failureContext, NWB_TEXT("positions"))
        && ReadVector(binary, inOutCursor, header.normalCount, outNormals, failureContext, NWB_TEXT("normals"))
        && ReadVector(binary, inOutCursor, header.tangentCount, outTangents, failureContext, NWB_TEXT("tangents"))
        && ReadVector(binary, inOutCursor, header.uv0Count, outUv0, failureContext, NWB_TEXT("uv0"))
        && ReadVector(binary, inOutCursor, header.colorCount, outColors, failureContext, NWB_TEXT("colors"))
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
    if(!ReadVector(binary, inOutCursor, header.meshletCount, outMeshlets, failureContext, NWB_TEXT("meshlets")))
        return false;
    if(!ReadVector(
        binary,
        inOutCursor,
        header.meshletBoundCount,
        outMeshletBounds,
        failureContext,
        NWB_TEXT("meshlet bounds")
    ))
        return false;
    if(!ReadVector(
        binary,
        inOutCursor,
        header.meshletPositionRefDeltaByteCount,
        outMeshletPositionRefDeltas,
        failureContext,
        NWB_TEXT("meshlet position ref deltas")
    ))
        return false;
    if(!ReadVector(
        binary,
        inOutCursor,
        header.meshletAttributeRefDeltaByteCount,
        outMeshletAttributeRefDeltas,
        failureContext,
        NWB_TEXT("meshlet attribute ref deltas")
    ))
        return false;
    if(!ReadVector(
        binary,
        inOutCursor,
        header.meshletLocalVertexRefCount,
        outMeshletLocalVertexRefs,
        failureContext,
        NWB_TEXT("meshlet local vertex refs")
    ))
        return false;
    return ReadVector(
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


template<typename ValueContainer>
[[nodiscard]] bool AppendVector(
    Core::Assets::AssetBytes& outBinary,
    const ValueContainer& values,
    const tchar* failureContext,
    const tchar* label
){
    return Core::Assets::AppendVectorPayload(outBinary, values, failureContext, label);
}

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
    return AppendVector(outBinary, mesh.positionStream(), failureContext, NWB_TEXT("positions"))
        && AppendVector(outBinary, mesh.normalStream(), failureContext, NWB_TEXT("normals"))
        && AppendVector(outBinary, mesh.tangentStream(), failureContext, NWB_TEXT("tangents"))
        && AppendVector(outBinary, mesh.uv0Stream(), failureContext, NWB_TEXT("uv0"))
        && AppendVector(outBinary, mesh.colorStream(), failureContext, NWB_TEXT("colors"))
    ;
}

template<typename MeshT>
[[nodiscard]] bool AppendMeshletStreams(
    Core::Assets::AssetBytes& outBinary,
    const MeshT& mesh,
    const tchar* failureContext
){
    return AppendVector(outBinary, mesh.meshlets(), failureContext, NWB_TEXT("meshlets"))
        && AppendVector(outBinary, mesh.meshletBounds(), failureContext, NWB_TEXT("meshlet bounds"))
        && AppendVector(outBinary, mesh.meshletPositionRefDeltas(), failureContext, NWB_TEXT("meshlet position ref deltas"))
        && AppendVector(outBinary, mesh.meshletAttributeRefDeltas(), failureContext, NWB_TEXT("meshlet attribute ref deltas"))
        && AppendVector(outBinary, mesh.meshletLocalVertexRefs(), failureContext, NWB_TEXT("meshlet local vertex refs"))
        && AppendVector(outBinary, mesh.meshletPrimitiveIndices(), failureContext, NWB_TEXT("meshlet primitive indices"))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

