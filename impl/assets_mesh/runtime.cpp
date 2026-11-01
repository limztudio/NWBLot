// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload_io.h"
#include "meshlet_payload_packing.h"
#include "meshlet_triangle_indices.h"
#include "meshlet_vertex_attributes.h"
#include "runtime_validation.h"

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

    if(header.meshClass != Core::Mesh::MeshClass::Static){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::loadBinary failed: invalid mesh class"));
        return false;
    }
    if(!MeshBaseHeaderComplete(header)){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::loadBinary failed: mesh payload is incomplete"));
        return false;
    }
    if(header.skinCount != 0u || header.skeletonJointCount != 0u || header.inverseBindMatrixCount != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::loadBinary failed: static mesh contains skinned payload"));
        return false;
    }

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

b8 Mesh::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    const TString<Core::Alloc::ScratchArena> meshPathText = Core::Assets::AssetVirtualPathText(*this, scratchArena);

    if(hasIncompleteGeometryPayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has incomplete payload"), meshPathText);
        return false;
    }
    const usize triangleCount = meshletPrimitiveIndices().size() / s_MeshletTriangleIndexCount;
    const u32 tailBits = static_cast<u32>(triangleCount % s_MeshletSolidTriangleWordBits);
    if(tailBits != 0u && (solidTriangleWords().back() >> tailBits) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh::validatePayload failed: mesh '{}' has invalid solid triangle padding"), meshPathText);
        return false;
    }

    if(!MeshRuntimeValidation::ValidateSharedMeshPayload(
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
        NWB_TEXT("Mesh::validatePayload"),
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
        || m_solidTriangleWords.size() != (m_meshletPrimitiveIndices.size() / s_MeshletTriangleIndexCount + s_MeshletSolidTriangleWordRoundUp) / s_MeshletSolidTriangleWordBits
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
        (header.meshletPrimitiveIndexCount / s_MeshletTriangleIndexCount + s_MeshletSolidTriangleWordRoundUp) / s_MeshletSolidTriangleWordBits,
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
    const SIMDVectorSingle absXY = VectorAndInt(VectorAbs(axis), s_SIMDMaskXY);
    const SIMDVectorSingle foldedMagnitude = VectorSubtract(s_SIMDOne, VectorSwizzle<1, 0, 2, 3>(absXY));
    const SIMDVectorSingle foldedSign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, VectorGreaterOrEqual(axis, VectorZero()));
    const SIMDVectorSingle foldedXY = VectorMultiply(foldedMagnitude, foldedSign);
    const SIMDVectorSingle folded = VectorSelect(axis, foldedXY, s_SIMDMaskXY);
    return VectorSelect(axis, folded, VectorLess(VectorSplatZ(axis), VectorZero()));
}

u32 PackMeshletConeOct16(const SIMDVectorSingle axis)noexcept{
    SIMDVectorSingle octAxis = VectorSetW(axis, 0.0f);
    const SIMDVectorSingle lengthVector = VectorSum(VectorAbs(octAxis));
    if(!VectorIsFinite(lengthVector, VectorComponentMask::s_XYZW) || !Vector4Greater(lengthVector, VectorReplicate(s_MeshletConeAxisLengthEpsilon)))
        return s_MeshletConeAxisFallback;

    octAxis = FoldMeshletConeOctAxis(VectorMultiply(octAxis, VectorReciprocal(lengthVector)));
    octAxis = VectorMultiplyAddExpression(octAxis, VectorReplicate(s_MeshletOctEncodeBias), VectorReplicate(s_MeshletOctEncodeBias));

    const SIMDVectorSingle quantized = VectorMultiplyAddExpression(VectorSaturate(octAxis), VectorReplicate(s_MeshletUnorm8Max), VectorReplicate(s_MeshletUnorm8RoundingBias));
    return (static_cast<u32>(VectorGetX(quantized)) << s_MeshletConeAxisXShift) | (static_cast<u32>(VectorGetY(quantized)) << s_MeshletConeAxisYShift);
}

SIMDVectorSingle UnpackMeshletConeOct16Axis(const u32 conePacked)noexcept{
    const SIMDVectorSingle packed = VectorSet(static_cast<f32>((conePacked >> s_MeshletConeAxisXShift) & s_MeshletPackedByteMask), static_cast<f32>((conePacked >> s_MeshletConeAxisYShift) & s_MeshletPackedByteMask), 0.0f, 0.0f);
    SIMDVectorSingle axis = VectorScale(packed, 1.0f / s_MeshletUnorm8Max);
    axis = VectorAndInt(VectorMultiplyAddExpression(axis, VectorReplicate(s_MeshletOctDecodeScale), VectorReplicate(-s_MeshletOctDecodeBias)), s_SIMDMaskXY);
    axis = VectorSelect(axis, VectorSubtract(s_SIMDOne, VectorSum(VectorAbs(axis))), s_SIMDMaskZ);
    axis = FoldMeshletConeOctAxis(axis);
    return Vector3NormalizeOr(axis, VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_MeshletConeAxisLengthSquaredEpsilon);
}

f32 ConservativePackedMeshletConeCutoff(const SIMDVectorSingle axis, const f32 cutoff, const SIMDVectorSingle unpackedAxis)noexcept{
    const SIMDVectorSingle normalizedAxis = Vector3NormalizeOr(axis, unpackedAxis, s_MeshletConeAxisLengthSquaredEpsilon);
    const f32 axisDot = Clamp(VectorGetX(Vector3Dot(normalizedAxis, unpackedAxis)), -1.0f, 1.0f);
    const f32 safeCutoff = Clamp(cutoff, 0.0f, 1.0f);
    const f32 cutoffSin = Sqrt(Max(0.0f, 1.0f - safeCutoff * safeCutoff));
    const f32 axisSin = Sqrt(Max(0.0f, 1.0f - axisDot * axisDot));
    return safeCutoff * axisDot - cutoffSin * axisSin;
}

u32 PackMeshletCone(const SIMDVectorSingle axis, const f32 cutoff)noexcept{
    if(cutoff <= 0.0f)
        return 0u;

    const u32 packedAxis = PackMeshletConeOct16(axis);
    const u32 packedCutoff = PackMeshletConeCutoffUnorm8(ConservativePackedMeshletConeCutoff(axis, cutoff, UnpackMeshletConeOct16Axis(packedAxis)));
    if(packedCutoff == 0u)
        return 0u;

    return packedAxis | (packedCutoff << s_MeshletConeCutoffShift) | (s_MeshletConeFlagEnabled << s_MeshletConeFlagShift);
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
        MeshletPositionStreamRef positionRef;
        if(!TryDecodeMeshletPositionRef(positionRefDeltas, meshlet, localPositionIndex, skinRequired, positionRef))
            return false;
        if(static_cast<usize>(positionRef.position) >= positionCount)
            return false;

        outIndices[primitiveByte] = positionRef.position;
        return true;
    }))
        return MakeUnexpected(Failure{});
    return outIndices;
}

Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildMeshletTriangleIndices(const Mesh& payload, Core::Alloc::ScratchArena& scratchArena){
    return BuildMeshletTriangleIndices(payload.meshlets(), payload.meshletLocalVertexRefs(), payload.meshletPositionRefDeltas(), payload.meshletPrimitiveIndices(), payload.positionStream().size(), scratchArena);
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
            MeshletAttributeStreamRef attributeRef;
            if(!TryDecodeMeshletAttributeRef(attributeRefDeltas, meshlet, localAttributeIndex, attributeRef))
                return false;
            if(static_cast<usize>(attributeRef.normal) >= normalCount || static_cast<usize>(attributeRef.uv0) >= uv0Count)
                return false;

            AttribGpu& attribute = outAttributes[primitiveByte];
            attribute.normal = normalStream[attributeRef.normal];
            attribute.uv0 = uv0Stream[attributeRef.uv0];
            return true;
        }
    ))
        return MakeUnexpected(Failure{});
    return outAttributes;
}

Expected<Vector<AttribGpu, Core::Alloc::ScratchArena>> BuildMeshletTriangleAttributes(const Mesh& payload, Core::Alloc::ScratchArena& scratchArena){
    return BuildMeshletTriangleAttributes(payload.meshlets(), payload.meshletLocalVertexRefs(), payload.meshletAttributeRefDeltas(), payload.meshletPrimitiveIndices(), payload.normalStream(), payload.uv0Stream(), scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

