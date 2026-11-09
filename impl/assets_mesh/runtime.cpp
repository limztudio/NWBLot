// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "skin_asset.h"
#include "binary_payload_io.h"
#include "meshlet_triangle_indices.h"
#include "meshlet_triangle_visit.h"
#include "meshlet_vertex_attributes.h"

#include <impl/assets_mesh/meshlet_ref_decoding.adhd.h>
#include <impl/assets_mesh/payload_validation.adhd.h>
#include <impl/assets_mesh/mesh_binary_codec.adhd.h>
#include <impl/assets_mesh/skin_binary_codec.adhd.h>

#include <core/common/log.h>
#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_MeshAssetCodecAutoRegistrar, MeshAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SkinAssetCodecAutoRegistrar, SkinAssetCodec);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_SkinningSubsystemAssetAutoRegistrar, SkinningSubsystem);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Mesh::loadBinary(const Span<const u8> binary){
    if(!checkVirtualPath(MeshBinaryPayload::s_MeshLoadBinaryContext))
        return false;

    Mesh candidate(m_positionStream.get_allocator().arena(), virtualPath());
    if(!MeshBinaryCodec().deserialize(
        binary,
        candidate.m_positionStream,
        candidate.m_normalStream,
        candidate.m_tangentStream,
        candidate.m_uv0Stream,
        candidate.m_colorStream,
        candidate.m_meshlets,
        candidate.m_meshletBounds,
        candidate.m_meshletPositionRefDeltas,
        candidate.m_meshletAttributeRefDeltas,
        candidate.m_meshletLocalVertexRefs,
        candidate.m_meshletPrimitiveIndices,
        candidate.m_solidTriangleWords
    ))
        return false;

    Core::Alloc::ScratchArena scratchArena(s_MeshValidatePayloadArena);
    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}

b8 Mesh::validatePayload([[maybe_unused]] Core::Alloc::ScratchArena& scratchArena)const{
    return ValidateMeshAssetPayload(
        virtualPath(),
        m_positionStream,
        m_normalStream,
        m_tangentStream,
        m_uv0Stream,
        m_colorStream,
        m_meshlets,
        m_meshletBounds,
        m_meshletPositionRefDeltas,
        m_meshletAttributeRefDeltas,
        m_meshletLocalVertexRefs,
        m_meshletPrimitiveIndices,
        m_solidTriangleWords
    );
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
    return HasIncompleteMeshGeometryPayload(
        m_positionStream,
        m_normalStream,
        m_tangentStream,
        m_uv0Stream,
        m_colorStream,
        m_meshlets,
        m_meshletBounds,
        m_meshletPositionRefDeltas,
        m_meshletAttributeRefDeltas,
        m_meshletLocalVertexRefs,
        m_meshletPrimitiveIndices,
        m_solidTriangleWords
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Skin::loadBinary(const Span<const u8> binary){
    Skin candidate(m_influences.get_allocator().arena(), virtualPath());
    if(!SkinBinaryCodec().deserialize(binary, candidate.m_mesh.virtualPath, candidate.m_skeleton.virtualPath, candidate.m_influences, candidate.m_inverseBindMatrices) || !candidate.validatePayload())
        return false;
    *this = Move(candidate);
    return true;
}

b8 Skin::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("Skin::validatePayload")))
        return false;
    return ValidateSkinAssetPayload(m_mesh.valid(), m_skeleton.valid(), m_influences, m_inverseBindMatrices);
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
    Vector<u32, Core::Alloc::ScratchArena> outIndices(primitiveIndices.size(), scratchArena);
    if(!DecodeMeshletTriangleIndicesInto(meshlets, localVertexRefs, positionRefDeltas, primitiveIndices, positionCount, outIndices))
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
    Vector<AttribGpu, Core::Alloc::ScratchArena> outAttributes(primitiveIndices.size(), scratchArena);
    if(!DecodeMeshletTriangleAttributesInto(meshlets, localVertexRefs, attributeRefDeltas, primitiveIndices, normalStream, uv0Stream, outAttributes))
        return MakeUnexpected(Failure{});
    return outAttributes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
