// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "cook_metadata.h"
#include "cook_source_streams.h"
#include "cook_stream_reorder.h"
#include "cook_ref_encoding.h"
#include "cook_meshlets.h"
#include "arena_names.h"
#include "binary_payload_io.h"
#include "binary_payload.h"
#include "meshlet_ref_codec.h"
#include "meshlet_payload_packing.h"

#include <core/alloc/scratch.h>
#include <core/task/cpu/scheduler.h>
#include <core/assets/paths.h>
#include <global/math/frame.h>
#include <core/metascript/parser.h>
#include <global/binary.h>
#include <global/text_utils.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_mesh_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseSourceMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    SourceMeshStreams streams(outEntry.positions.get_allocator().arena(), scratchArena);
    if(!MeshCookSourceStreams::ParseCommonSourceMeshStreams(
        discoveredFile,
        asset,
        s_MeshMetaKind,
        false,
        streams,
        0u,
        scratchArena
    ))
        return false;

    MeshCookSourceStreams::CopySourceStreams(streams, outEntry);
    return MeshCookMeshlets::BuildMeshlets(
        discoveredFile.filePath,
        s_MeshMetaKind,
        streams.indices,
        outEntry,
        cpuScheduler
    );
}

static bool ValidateMeshAssetFields(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset
){
    return Core::Assets::ValidateMetadataAssetFields(
        discoveredFile.filePath,
        asset,
        "Mesh meta",
        { "positions", "normals", "tangents", "uv0", "colors", "vertex_refs", "indices" }
    );
}

static bool ParseMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const Name& virtualPath,
    MeshCookEntry& outEntry,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry = MeshCookEntry(outEntry.positions.get_allocator().arena());

    if(!Core::Assets::CheckMetadataAssetMap(discoveredFile.filePath, asset, "Mesh meta"))
        return false;

    if(!Core::Assets::AssignCookEntryVirtualPath(outEntry, virtualPath, discoveredFile.filePath, "Mesh meta"))
        return false;
    if(!ValidateMeshAssetFields(discoveredFile, asset))
        return false;
    return ParseSourceMeshMeta(discoveredFile, asset, outEntry, cpuScheduler, scratchArena);
}

static bool ParseMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = NAME_NONE;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        virtualPath,
        scratchArena
    ))
        return false;
    return ParseMeshMeta(discoveredFile, doc.asset(), virtualPath, outEntry, cpuScheduler, scratchArena);
}

static bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh){
    Core::Alloc::ScratchArena scratchArena(AssetsMeshArenaScope::s_BuildMeshAssetArena);
    if(!MeshCookStreamReorder::ReorderMeshStreamsByMeshletTraversal(meshEntry, scratchArena))
        return false;
    if(!MeshCookRefEncoding::EncodeMeshletRefs(meshEntry, false, s_MeshMetaKind))
        return false;

    outMesh = Mesh(meshEntry.positions.get_allocator().arena(), meshEntry.virtualPath);
    outMesh.setPayload(
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
        Move(meshEntry.meshletPrimitiveIndices)
    );
    return outMesh.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMeshCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    DiscoveredNwbFile discoveredFile(nwbFilePath.arena());
    if(!MeshCookMetadata::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_assets_mesh_cook::ParseMeshMeta(discoveredFile, doc, outEntry, cpuScheduler, scratchArena);
}

bool ParseMeshCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
){
    DiscoveredNwbFile discoveredFile(nwbFilePath.arena());
    discoveredFile.filePath = nwbFilePath;
    return __hidden_assets_mesh_cook::ParseMeshMeta(discoveredFile, asset, virtualPath, outEntry, cpuScheduler, scratchArena);
}

bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh){
    return __hidden_assets_mesh_cook::BuildMeshAsset(meshEntry, outMesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, MakeNotNull(NWB_TEXT("MeshAssetCodec::serialize"))))
        return false;

    const Mesh& mesh = static_cast<const Mesh&>(asset);
    if(!mesh.validatePayload())
        return false;

    usize reserveBytes = sizeof(MeshBinaryPayload::MeshHeaderBinary);
    const bool canReserve = MeshAssetBinaryPayload::AddMeshBaseReserveBytes(reserveBytes, mesh);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    MeshBinaryPayload::MeshHeaderBinary header;
    MeshAssetBinaryPayload::FillMeshBaseHeader(header, mesh);
    AppendPOD(outBinary, header);

    const NotNull<const tchar*> serializeFailureContext = MakeNotNull(NWB_TEXT("MeshAssetCodec::serialize"));
    if(!MeshAssetBinaryPayload::AppendMeshAttributeStreams(outBinary, mesh, serializeFailureContext))
        return false;
    return MeshAssetBinaryPayload::AppendMeshletStreams(outBinary, mesh, serializeFailureContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

