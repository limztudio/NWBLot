// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "cook_metadata.h"
#include "cook_source_streams.h"
#include "cook_stream_reorder.h"
#include "cook_ref_encoding.h"
#include "cook_meshlets.h"
#include "binary_payload_io.h"
#include "binary_payload.h"
#include "meshlet_ref_codec.h"
#include "meshlet_payload_packing.h"
#include "meshlet_triangle_indices.h"
#include "cook_topology.h"

#include <core/alloc/scratch.h>
#include <core/task/cpu/scheduler.h>
#include <core/assets/paths.h>
#include <core/metascript/parser.h>
#include <core/common/log.h>

#include <global/simd/frame.h>
#include <global/binary.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_mesh_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseSourceMeshMeta(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Value& asset, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& outEntry){
    auto streamsResult = MeshCookSourceStreams::ParseCommonSourceMeshStreams(discoveredFile, asset, s_MeshMetaKind, outEntry.positions.get_allocator().arena(), scratchArena);
    if(!streamsResult)
        return false;

    SourceMeshStreams& streams = *streamsResult;
    MeshCookSourceStreams::CopySourceStreams(streams, outEntry);
    return MeshCookMeshlets::BuildMeshlets(discoveredFile.filePath, s_MeshMetaKind, streams.indices, cpuScheduler, scratchArena, outEntry);
}

static bool ValidateMeshAssetFields(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Value& asset){
    return Core::Assets::ValidateMetadataAssetFields(
        discoveredFile.filePath,
        asset,
        s_MeshMetaText,
        {
            MeshCookMetadata::s_PositionsFieldNameView,
            MeshCookMetadata::s_NormalsFieldNameView,
            MeshCookMetadata::s_TangentsFieldNameView,
            MeshCookMetadata::s_Uv0FieldNameView,
            MeshCookMetadata::s_ColorsFieldNameView,
            MeshCookMetadata::s_VertexRefsFieldNameView,
            MeshCookMetadata::s_IndicesFieldNameView,
        }
    );
}

static Expected<MeshCookEntry> ParseMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const Name& virtualPath,
    Core::Assets::AssetArena& arena,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    MeshCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(discoveredFile.filePath, asset, s_MeshMetaText))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, discoveredFile.filePath, s_MeshMetaText))
        return MakeUnexpected(Failure{});
    if(!ValidateMeshAssetFields(discoveredFile, asset))
        return MakeUnexpected(Failure{});
    if(!ParseSourceMeshMeta(discoveredFile, asset, cpuScheduler, scratchArena, entry))
        return MakeUnexpected(Failure{});
    return entry;
}

static Expected<MeshCookEntry> ParseMeshMeta(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena){
    Name virtualPath = s_NameNone;
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(discoveredFile.assetRoot, discoveredFile.virtualRoot, discoveredFile.filePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    virtualPath = *virtualPathResult;
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(discoveredFile.filePath, doc, s_MeshMetaText);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseMeshMeta(discoveredFile, **asset, virtualPath, arena, cpuScheduler, scratchArena);
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
    const auto discoveredFile = MeshCookMetadata::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath);
    if(!discoveredFile)
        return MakeUnexpected(Failure{});
    return __hidden_assets_mesh_cook::ParseMeshMeta(*discoveredFile, doc, arena, cpuScheduler, scratchArena);
}

Expected<MeshCookEntry> ParseMeshCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena){
    DiscoveredNwbFile discoveredFile(nwbFilePath.arena());
    discoveredFile.filePath = nwbFilePath;
    return __hidden_assets_mesh_cook::ParseMeshMeta(discoveredFile, asset, virtualPath, arena, cpuScheduler, scratchArena);
}

Expected<Mesh> BuildMeshAsset(MeshCookEntry& meshEntry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    if(!MeshCookStreamReorder::ReorderMeshStreamsByMeshletTraversal(meshEntry, scratchArena))
        return MakeUnexpected(Failure{});
    if(!MeshCookRefEncoding::EncodeMeshletRefs(meshEntry, s_MeshMetaKind))
        return MakeUnexpected(Failure{});

    const auto triangleIndices = BuildMeshletTriangleIndices(scratchArena, meshEntry.meshlets, meshEntry.meshletLocalVertexRefs, meshEntry.meshletPositionRefDeltas, meshEntry.meshletPrimitiveIndices, meshEntry.positions.size());
    if(!triangleIndices)
        return MakeUnexpected(Failure{});
    auto solidTriangleWords = BuildSolidTriangleWords(meshEntry.positions, *triangleIndices, scratchArena);
    if(!solidTriangleWords)
        return MakeUnexpected(Failure{});
    Core::Assets::AssetVector<u32> solidWords(solidTriangleWords->begin(), solidTriangleWords->end(), arena);

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


bool MeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, MeshBinaryPayload::s_MeshSerializeContext))
        return false;

    Core::Alloc::ScratchArena scratchArena(AssetsMeshArenaScope::s_ValidatePayloadArena);
    const Mesh& mesh = static_cast<const Mesh&>(asset);
    if(!mesh.validatePayload(scratchArena))
        return false;

    usize reserveBytes = sizeof(MeshBinaryPayload::MeshHeaderBinary);
    const bool canReserve = MeshAssetBinaryPayload::AddMeshBaseReserveBytes(reserveBytes, mesh);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    MeshBinaryPayload::MeshHeaderBinary header;
    MeshAssetBinaryPayload::FillMeshBaseHeader(header, mesh);
    AppendPOD(outBinary, header);

    const TStringView serializeFailureContext = MeshBinaryPayload::s_MeshSerializeContext;
    if(!MeshAssetBinaryPayload::AppendMeshAttributeStreams(outBinary, mesh, serializeFailureContext))
        return false;
    return MeshAssetBinaryPayload::AppendMeshletStreams(outBinary, mesh, serializeFailureContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

