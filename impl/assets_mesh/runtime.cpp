// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload_io.h"
#include "meshlet_payload_packing.h"
#include "meshlet_triangle_indices.h"
#include "meshlet_triangle_visit.h"
#include "meshlet_vertex_attributes.h"

#include <core/common/log.h>
#include <core/assets/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_MeshAssetCodecAutoRegistrar, MeshAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Mesh::loadBinary(const Span<const u8> binary){
    if(!checkVirtualPath(MeshBinaryPayload::s_MeshLoadBinaryContext))
        return false;

    const TStringView loadFailureContext = MeshBinaryPayload::s_MeshLoadBinaryContext;
    usize cursor = 0;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<MeshBinaryHeader>(binary, s_MeshBinaryMagic, loadFailureContext, MeshBinaryPayload::s_MeshAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const MeshBinaryHeader& header = *headerResult;

    if(!ValidateMeshBaseHeader(header))
        return false;

    Mesh candidate(m_positionStream.get_allocator().arena(), virtualPath());
    if(!MeshAssetBinaryPayload::ReadMeshAttributeStreams(binary, header, loadFailureContext, cursor, candidate.m_positionStream, candidate.m_normalStream, candidate.m_tangentStream, candidate.m_uv0Stream, candidate.m_colorStream))
        return false;
    if(!MeshAssetBinaryPayload::ReadMeshletStreams(
        binary,
        header,
        loadFailureContext,
        cursor,
        candidate.m_meshlets,
        candidate.m_meshletBounds,
        candidate.m_meshletPositionRefDeltas,
        candidate.m_meshletAttributeRefDeltas,
        candidate.m_meshletLocalVertexRefs,
        candidate.m_meshletPrimitiveIndices,
        candidate.m_solidTriangleWords
    ))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, loadFailureContext))
        return false;

    Core::Alloc::ScratchArena scratchArena(s_MeshValidatePayloadArena);
    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}

b8 Mesh::validatePayload([[maybe_unused]] Core::Alloc::ScratchArena& scratchArena)const{
    const AStringView meshPathText = ResolveMeshVirtualPathText(virtualPath());

    if(hasIncompleteGeometryPayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has incomplete payload"), ConvertString(meshPathText));
        return false;
    }
    if(!HasValidMeshSolidTriangleTailPadding(meshletPrimitiveIndices().size(), solidTriangleWords().back())){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has invalid solid triangle padding"), ConvertString(meshPathText));
        return false;
    }

    if(!ValidateSharedMeshPayload(
        m_positionStream,
        m_normalStream,
        m_tangentStream,
        m_uv0Stream,
        m_colorStream,
        m_meshletPositionRefDeltas,
        m_meshletAttributeRefDeltas,
        m_meshletLocalVertexRefs,
        m_meshlets,
        m_meshletBounds,
        m_meshletPrimitiveIndices,
        s_MeshValidatePayloadContextText,
        meshPathText
    ))
        return false;

    return true;
}

void Mesh::setPayload(
    Core::Assets::AssetVector<Float3U>&& positions,
    Core::Assets::AssetVector<Half4U>&& normals,
    Core::Assets::AssetVector<Half4U>&& tangents,
    Core::Assets::AssetVector<Float2U>&& uv0,
    Core::Assets::AssetVector<Half4U>&& colors,
    Core::Assets::AssetVector<MeshletDesc>&& meshlets,
    Core::Assets::AssetVector<MeshletBounds>&& meshletBounds,
    Core::Assets::AssetVector<u8>&& meshletPositionRefDeltas,
    Core::Assets::AssetVector<u8>&& meshletAttributeRefDeltas,
    Core::Assets::AssetVector<MeshletLocalVertexRef>&& meshletLocalVertexRefs,
    Core::Assets::AssetVector<u8>&& meshletPrimitiveIndices,
    Core::Assets::AssetVector<u32>&& solidTriangleWords
)noexcept{
    m_positionStream = Move(positions);
    m_normalStream = Move(normals);
    m_tangentStream = Move(tangents);
    m_uv0Stream = Move(uv0);
    m_colorStream = Move(colors);
    m_meshlets = Move(meshlets);
    m_meshletBounds = Move(meshletBounds);
    m_meshletPositionRefDeltas = Move(meshletPositionRefDeltas);
    m_meshletAttributeRefDeltas = Move(meshletAttributeRefDeltas);
    m_meshletLocalVertexRefs = Move(meshletLocalVertexRefs);
    m_meshletPrimitiveIndices = Move(meshletPrimitiveIndices);
    m_solidTriangleWords = Move(solidTriangleWords);
}

b8 Mesh::hasIncompleteGeometryPayload()const noexcept{
    return
        m_positionStream.empty()
        || m_normalStream.empty()
        || m_tangentStream.empty()
        || m_uv0Stream.empty()
        || m_colorStream.empty()
        || m_meshlets.empty()
        || m_meshletBounds.empty()
        || m_meshletPositionRefDeltas.empty()
        || m_meshletAttributeRefDeltas.empty()
        || m_meshletLocalVertexRefs.empty()
        || m_meshletPrimitiveIndices.empty()
        || m_solidTriangleWords.size() != static_cast<usize>(ComputeMeshSolidTriangleWordCount(m_meshletPrimitiveIndices.size()))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshAssetBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ReadMeshAttributeStreams(
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

b8 ReadMeshletStreams(
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
    return Core::Assets::ReadVectorPayload(
        binary,
        ComputeMeshSolidTriangleWordCount(header.meshletPrimitiveIndexCount),
        failureContext,
        s_SolidTriangleWordsStreamLabel,
        inOutCursor,
        outSolidTriangleWords
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVectorSingle FoldMeshletConeOctAxis(const SIMDVectorSingle axis)noexcept{
    Float4 axisVec;
    StoreFloat(axis, axisVec);
    return LoadFloat(FoldMeshletConeOctAxisVec(axisVec));
}

u32 PackMeshletConeOct16(const SIMDVectorSingle axis)noexcept{
    Float4 axisVec;
    StoreFloat(axis, axisVec);
    return PackMeshletConeOct16Vec(axisVec);
}

SIMDVectorSingle UnpackMeshletConeOct16Axis(const u32 conePacked)noexcept{
    return LoadFloat(UnpackMeshletConeOct16AxisVec(conePacked));
}

f32 ConservativePackedMeshletConeCutoff(const SIMDVectorSingle axis, const f32 cutoff, const SIMDVectorSingle unpackedAxis)noexcept{
    Float4 axisVec;
    Float4 unpackedAxisVec;
    StoreFloat(axis, axisVec);
    StoreFloat(unpackedAxis, unpackedAxisVec);
    return ConservativePackedMeshletConeCutoffVec(axisVec, cutoff, unpackedAxisVec);
}

u32 PackMeshletCone(const SIMDVectorSingle axis, const f32 cutoff)noexcept{
    Float4 axisVec;
    StoreFloat(axis, axisVec);
    return PackMeshletConeVec(axisVec, cutoff);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildMeshletTriangleIndices(
    const Span<const MeshletDesc> meshlets,
    const Span<const MeshletLocalVertexRef> localVertexRefs,
    const Span<const u8> positionRefDeltas,
    const Span<const u8> primitiveIndices,
    const usize positionCount,
    Core::Alloc::ScratchArena& scratchArena
){
    const usize primitiveIndexCount = primitiveIndices.size();

    Vector<u32, Core::Alloc::ScratchArena> outIndices(primitiveIndexCount, scratchArena);

    if(!ForEachMeshletTriangleCorner(meshlets, localVertexRefs, primitiveIndices, [&](const MeshletDesc& meshlet, const usize primitiveByte, const MeshletLocalVertexRef& localVertexRef)noexcept -> b8 {
        const b8 skinRequired = meshlet.skinBase != s_MeshMissingStreamIndex;
        const u32 localPositionIndex = static_cast<u32>(localVertexRef.localDeformedPosition);
        const Expected<MeshletPositionStreamRef> positionRef = TryDecodeMeshletPositionRef(positionRefDeltas, meshlet, localPositionIndex, skinRequired);
        if(!positionRef)
            return false;
        if(static_cast<usize>(positionRef->position) >= positionCount)
            return false;

        outIndices[primitiveByte] = positionRef->position;
        return true;
    }))
        return MakeUnexpected(Failure{});
    return outIndices;
}

Expected<Vector<AttribGpu, Core::Alloc::ScratchArena>> BuildMeshletTriangleAttributes(
    const Span<const MeshletDesc> meshlets,
    const Span<const MeshletLocalVertexRef> localVertexRefs,
    const Span<const u8> attributeRefDeltas,
    const Span<const u8> primitiveIndices,
    const Span<const Half4U> normalStream,
    const Span<const Float2U> uv0Stream,
    Core::Alloc::ScratchArena& scratchArena
){
    const usize primitiveIndexCount = primitiveIndices.size();
    const usize normalCount = normalStream.size();
    const usize uv0Count = uv0Stream.size();

    Vector<AttribGpu, Core::Alloc::ScratchArena> outAttributes(primitiveIndexCount, scratchArena);

    if(!ForEachMeshletTriangleCorner(meshlets, localVertexRefs, primitiveIndices, [&](const MeshletDesc& meshlet, const usize primitiveByte, const MeshletLocalVertexRef& localVertexRef)noexcept -> b8 {
            const u32 localAttributeIndex = static_cast<u32>(localVertexRef.localAttribute);
            const Expected<MeshletAttributeStreamRef> attributeRef = TryDecodeMeshletAttributeRef(attributeRefDeltas, meshlet, localAttributeIndex);
            if(!attributeRef)
                return false;
            if(static_cast<usize>(attributeRef->normal) >= normalCount || static_cast<usize>(attributeRef->uv0) >= uv0Count)
                return false;

            AttribGpu& attribute = outAttributes[primitiveByte];
            attribute.normal = normalStream[attributeRef->normal];
            attribute.uv0 = uv0Stream[attributeRef->uv0];
            return true;
        }
    ))
        return MakeUnexpected(Failure{});
    return outAttributes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

