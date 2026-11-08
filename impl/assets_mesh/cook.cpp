// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_metadata.h"
#include "cook_source_streams.h"
#include "meshlet_ref_encode.h"
#include "cook_meshlets.h"
#include "binary_payload_io.h"
#include "meshlet_triangle_indices.h"

#include <impl/assets_mesh/payload_validation.adhd.h>
#include <impl/assets_mesh/mesh_binary_codec.adhd.h>
#include <impl/assets_mesh/cook_stream_reorder.adhd.h>
#include <impl/assets_mesh/cook_solid_topology.adhd.h>

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_mesh_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ParseSourceMeshMeta(const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& outEntry){
    auto streamsResult = MeshCookSourceStreams::ParseCommonSourceMeshStreams(nwbFilePath, asset, s_MeshMetaKind, outEntry.positions.get_allocator().arena(), scratchArena);
    if(!streamsResult)
        return false;

    SourceMeshStreams& streams = *streamsResult;
    CopySourceMeshStreamsToCookStreams(
        streams.positions,
        streams.normals,
        streams.tangents,
        streams.uv0,
        streams.colors,
        streams.vertexRefs,
        outEntry.positions,
        outEntry.normals,
        outEntry.tangents,
        outEntry.uv0,
        outEntry.colors,
        outEntry.vertexRefs
    );
    return MeshCookMeshlets::BuildMeshlets(nwbFilePath, s_MeshMetaKind, streams.indices, cpuScheduler, scratchArena, outEntry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MeshCookEntry> ParseMeshCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    return Core::Assets::ParseCookMetadataDocument<MeshCookEntry>(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        s_MeshMetaText,
        [&nwbFilePath, &arena, &cpuScheduler, &scratchArena](const Name virtualPath, const Core::Metascript::Value& asset){
            return ParseMeshCookMetadata(virtualPath, nwbFilePath, asset, arena, cpuScheduler, scratchArena);
        },
        scratchArena
    );
}

Expected<MeshCookEntry> ParseMeshCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena){
    MeshCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_MeshMetaText))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_MeshMetaText, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_MeshMetaText, &IsAllowedMeshAssetField))
        return MakeUnexpected(Failure{});
    if(!__hidden_assets_mesh_cook::ParseSourceMeshMeta(nwbFilePath, asset, cpuScheduler, scratchArena, entry))
        return MakeUnexpected(Failure{});
    return entry;
}

Expected<Mesh> BuildMeshAsset(MeshCookEntry& meshEntry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    if(!ReorderMeshStreamsByMeshletTraversal(
        meshEntry.virtualPath,
        meshEntry.meshlets,
        meshEntry.positions,
        meshEntry.normals,
        meshEntry.tangents,
        meshEntry.uv0,
        meshEntry.colors,
        meshEntry.meshletPositionStreamRefs,
        meshEntry.meshletAttributeStreamRefs
    ))
        return MakeUnexpected(Failure{});
    if(!EncodeMeshletRefDeltas(meshEntry.meshletPositionStreamRefs, meshEntry.meshletAttributeStreamRefs, false, [&](const usize meshletIndex, const TStringView reason){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet {} {}"), s_MeshMetaKind, ConvertString(meshEntry.virtualPath.resolvedText()), meshletIndex, reason);
        return false;
    }, meshEntry.meshlets, meshEntry.meshletPositionRefDeltas, meshEntry.meshletAttributeRefDeltas))
        return MakeUnexpected(Failure{});

    const auto triangleIndices = BuildMeshletTriangleIndices(meshEntry.meshlets, meshEntry.meshletLocalVertexRefs, meshEntry.meshletPositionRefDeltas, meshEntry.meshletPrimitiveIndices, meshEntry.positions.size(), scratchArena);
    if(!triangleIndices)
        return MakeUnexpected(Failure{});
    Core::Assets::AssetVector<u32> solidWords(arena);
    if(!BuildSolidTriangleWordsInto(meshEntry.positions, *triangleIndices, solidWords))
        return MakeUnexpected(Failure{});

    Mesh mesh(arena, meshEntry.virtualPath);
    mesh.setPayload(
        Move(meshEntry.positions),
        Move(meshEntry.normals),
        Move(meshEntry.tangents),
        Move(meshEntry.uv0),
        Move(meshEntry.colors),
        Move(meshEntry.meshlets),
        Move(meshEntry.meshletBounds),
        Move(meshEntry.meshletPositionRefDeltas),
        Move(meshEntry.meshletAttributeRefDeltas),
        Move(meshEntry.meshletLocalVertexRefs),
        Move(meshEntry.meshletPrimitiveIndices),
        Move(solidWords)
    );
    if(!mesh.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return mesh;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, MeshBinaryPayload::s_MeshSerializeContext))
        return false;

    Core::Alloc::ScratchArena scratchArena(s_MeshValidatePayloadArena);
    const Mesh& mesh = static_cast<const Mesh&>(asset);
    if(!mesh.validatePayload(scratchArena))
        return false;

    return MeshBinaryCodec().serialize(
        mesh.positionStream(),
        mesh.normalStream(),
        mesh.tangentStream(),
        mesh.uv0Stream(),
        mesh.colorStream(),
        mesh.meshlets(),
        mesh.meshletBounds(),
        mesh.meshletPositionRefDeltas(),
        mesh.meshletAttributeRefDeltas(),
        mesh.meshletLocalVertexRefs(),
        mesh.meshletPrimitiveIndices(),
        mesh.solidTriangleWords(),
        outBinary
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

