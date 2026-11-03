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
#include "cook_topology.h"

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
    MeshCookSourceStreams::CopySourceStreams(streams, outEntry);
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


namespace MeshAssetBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FillMeshBaseHeader(const Mesh& mesh, MeshBinaryHeader& outHeader)noexcept{
    outHeader = MakeMeshBinaryHeader(
        mesh.positionStream().size(),
        mesh.normalStream().size(),
        mesh.tangentStream().size(),
        mesh.uv0Stream().size(),
        mesh.colorStream().size(),
        mesh.meshlets().size(),
        mesh.meshletBounds().size(),
        mesh.meshletPositionRefDeltas().size(),
        mesh.meshletAttributeRefDeltas().size(),
        mesh.meshletLocalVertexRefs().size(),
        mesh.meshletPrimitiveIndices().size()
    );
}

[[nodiscard]] b8 AppendMeshAttributeStreams(const Mesh& mesh, const TStringView failureContext, Core::Assets::AssetBytes& outBinary){
    return
        Core::Assets::AppendVectorPayload(mesh.positionStream(), failureContext, s_PositionsStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.normalStream(), failureContext, s_NormalsStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.tangentStream(), failureContext, s_TangentsStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.uv0Stream(), failureContext, s_Uv0StreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.colorStream(), failureContext, s_ColorsStreamLabel, outBinary)
    ;
}

[[nodiscard]] b8 AppendMeshletStreams(const Mesh& mesh, const TStringView failureContext, Core::Assets::AssetBytes& outBinary){
    return
        Core::Assets::AppendVectorPayload(mesh.meshlets(), failureContext, s_MeshletsStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.meshletBounds(), failureContext, s_MeshletBoundsStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.meshletPositionRefDeltas(), failureContext, s_MeshletPositionRefDeltasStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.meshletAttributeRefDeltas(), failureContext, s_MeshletAttributeRefDeltasStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.meshletLocalVertexRefs(), failureContext, s_MeshletLocalVertexRefsStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.meshletPrimitiveIndices(), failureContext, s_MeshletPrimitiveIndicesStreamLabel, outBinary)
        && Core::Assets::AppendVectorPayload(mesh.solidTriangleWords(), failureContext, s_SolidTriangleWordsStreamLabel, outBinary)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, MeshBinaryPayload::s_MeshSerializeContext))
        return false;

    Core::Alloc::ScratchArena scratchArena(s_MeshValidatePayloadArena);
    const Mesh& mesh = static_cast<const Mesh&>(asset);
    if(!mesh.validatePayload(scratchArena))
        return false;

    usize reserveBytes = sizeof(MeshBinaryHeader);
    const b8 canReserve = MeshAssetBinaryPayload::AddMeshBaseReserveBytes(mesh, reserveBytes);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    MeshBinaryHeader header;
    MeshAssetBinaryPayload::FillMeshBaseHeader(mesh, header);
    AppendPOD(outBinary, header);

    const TStringView serializeFailureContext = MeshBinaryPayload::s_MeshSerializeContext;
    if(!MeshAssetBinaryPayload::AppendMeshAttributeStreams(mesh, serializeFailureContext, outBinary))
        return false;
    return MeshAssetBinaryPayload::AppendMeshletStreams(mesh, serializeFailureContext, outBinary);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

