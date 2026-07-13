
#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "arena_names.h"
#include "binary_payload_io.h"
#include "binary_payload.h"
#include "meshlet_ref_codec.h"
#include "meshlet_payload_packing.h"

#include <core/alloc/scratch.h>
#include <core/alloc/thread.h>
#include <core/assets/paths.h>
#include <core/mesh/frame_math.h>
#include <core/metascript/parser.h>
#include <global/binary.h>
#include <global/text_utils.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.inl"
#include "cook_stream_reorder.inl"
#include "cook_ref_encoding.inl"

static bool ParseSourceMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::Alloc::ThreadPool& threadPool,
    Core::Alloc::ScratchArena& scratchArena
);
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
    Core::Alloc::ThreadPool& threadPool,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry = MeshCookEntry(outEntry.positions.get_allocator().arena());

    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh meta '{}': asset is not a map"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    outEntry.virtualPath = virtualPath;
    if(!outEntry.virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh meta '{}': virtual path must not be empty"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    if(!ValidateMeshAssetFields(discoveredFile, asset))
        return false;
    return ParseSourceMeshMeta(discoveredFile, asset, outEntry, threadPool, scratchArena);
}

static bool ParseMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::Alloc::ThreadPool& threadPool,
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
    return ParseMeshMeta(discoveredFile, doc.asset(), virtualPath, outEntry, threadPool, scratchArena);
}

static bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh){
    Core::Alloc::ScratchArena scratchArena(AssetsMeshArenaScope::s_BuildMeshAssetArena);
    if(!ReorderMeshStreamsByMeshletTraversal(meshEntry, scratchArena))
        return false;
    if(!EncodeMeshletRefs(meshEntry, false, s_MeshMetaKind))
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

#include "cook_source.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMeshCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::Alloc::ThreadPool& threadPool,
    Core::Alloc::ScratchArena& scratchArena
){
    __hidden_cook::DiscoveredNwbFile discoveredFile(nwbFilePath.arena());
    if(!__hidden_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_cook::ParseMeshMeta(discoveredFile, doc, outEntry, threadPool, scratchArena);
}

bool ParseMeshCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::Alloc::ThreadPool& threadPool,
    Core::Alloc::ScratchArena& scratchArena
){
    __hidden_cook::DiscoveredNwbFile discoveredFile(nwbFilePath.arena());
    discoveredFile.filePath = nwbFilePath;
    return __hidden_cook::ParseMeshMeta(discoveredFile, asset, virtualPath, outEntry, threadPool, scratchArena);
}

bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh){
    return __hidden_cook::BuildMeshAsset(meshEntry, outMesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Mesh::s_AssetTypeText)
        );
        return false;
    }

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

    const tchar* const serializeFailureContext = NWB_TEXT("MeshAssetCodec::serialize");
    if(!MeshAssetBinaryPayload::AppendMeshAttributeStreams(outBinary, mesh, serializeFailureContext))
        return false;
    return MeshAssetBinaryPayload::AppendMeshletStreams(outBinary, mesh, serializeFailureContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

