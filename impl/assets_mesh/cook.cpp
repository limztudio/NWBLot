// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "skin_cook.h"
#include "cook_metadata.h"
#include "cook_source_streams.h"
#include "meshlet_ref_encode.h"
#include "cook_meshlets.h"
#include "binary_payload_io.h"
#include "meshlet_triangle_indices.h"
#include "skin_influence_metadata_parser.h"

#include <impl/assets_mesh/payload_validation.adhd.h>
#include <impl/assets_mesh/mesh_binary_codec.adhd.h>
#include <impl/assets_mesh/skin_binary_codec.adhd.h>
#include <impl/assets_mesh/cook_stream_reorder.adhd.h>
#include <impl/assets_mesh/cook_solid_topology.adhd.h>
#include <impl/assets_mesh/cook_meshlet_scoring.adhd.h>
#include <impl/assets_mesh/cook_meshlet_metrics.adhd.h>

#include <impl/assets_skeleton/cook_matrix.h>

#include <core/assets/binary_payload_io.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>

#include <global/algorithm.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshCookMeshlets::GrowMeshletFromFrontier(
    const AStringView metaKind,
    const AStringView filePathText,
    const usize seedSearchOffset,
    Core::CpuTaskScheduler& cpuScheduler,
    MeshletTrianglePrecompute& trianglePrecompute,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            const auto disconnectedCandidate = MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(trianglePrecompute, seedSearchOffset + 1u, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs, cpuScheduler, parallelCandidates);
            if(!disconnectedCandidate)
                break;

            if(!AddVisitedMeshletTriangle(
                metaKind,
                filePathText,
                disconnectedCandidate->triangleIndex,
                trianglePrecompute.triangles,
                trianglePrecompute.triangleCalculations,
                trianglePrecompute.positionTriangleOffsets,
                trianglePrecompute.positionTriangleIndices,
                trianglePrecompute.visitedTriangles,
                localSourceVertexRefs,
                localTriangleIndices,
                meshlet,
                primitiveIndices,
                scoreState,
                frontier,
                frontierFlags
            ))
                return false;
            continue;
        }

        const auto bestCandidate = FindBestMeshletFrontierCandidateFromPrecompute(
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            frontier,
            localTriangleIndices,
            scoreState,
            meshlet,
            localSourceVertexRefs
        );
        if(!bestCandidate)
            break;

        RemoveMeshletFrontierCandidate(bestCandidate->frontierOffset, frontier, frontierFlags);
        if(!AddVisitedMeshletTriangle(
            metaKind,
            filePathText,
            bestCandidate->triangleIndex,
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.positionTriangleOffsets,
            trianglePrecompute.positionTriangleIndices,
            trianglePrecompute.visitedTriangles,
            localSourceVertexRefs,
            localTriangleIndices,
            meshlet,
            primitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;
    }

    return true;
}


b8 MeshCookMeshlets::BuildMeshlets(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& entry){
    auto precomputed = MeshletTriangleGeometry::PrecomputeMeshletTriangleData(nwbFilePath, metaKind, indices, entry, scratchArena);
    if(!precomputed)
        return false;
    MeshletTrianglePrecompute& trianglePrecompute = *precomputed;

    MeshletDesc current;
    const AString<Core::Alloc::ScratchArena> filePathText = PathToString<char>(scratchArena, nwbFilePath);
    Core::Assets::AssetArena& assetArena = entry.positions.get_allocator().arena();
    Core::Assets::AssetVector<u32> localSourceVertexRefs(assetArena);
    Core::Assets::AssetVector<u32> localTriangleIndices(assetArena);
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena> parallelCandidates(scratchArena);
    Core::Assets::AssetVector<u32> frontier(assetArena);
    Core::Assets::AssetVector<u8> frontierFlags(assetArena);
    PrepareMeshletBuildOutputStreams(
        indices.size(),
        trianglePrecompute.triangles.size(),
        entry.meshlets,
        entry.meshletBounds,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletLocalVertexRefs,
        entry.meshletPrimitiveIndices,
        localSourceVertexRefs,
        localTriangleIndices,
        frontier,
        frontierFlags
    );
    parallelCandidates.reserve((static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u) * s_MeshletDisconnectedCandidateParallelOversubscription);

    MeshletScoreState scoreState;

    auto resetCurrentMeshlet = [&]()noexcept{
        ResetMeshletBuilderState(entry.meshletPrimitiveIndices.size(), current, localSourceVertexRefs, localTriangleIndices, scoreState);
    };

    auto flushMeshlet = [&]() -> b8{
        if(!FlushBuiltMeshlet(
            s_MeshMetaKindText,
            AStringView(filePathText),
            entry.positions,
            entry.vertexRefs,
            entry.meshletPrimitiveIndices,
            localSourceVertexRefs,
            current,
            entry.meshletPositionStreamRefs,
            entry.meshletAttributeStreamRefs,
            entry.meshletLocalVertexRefs,
            entry.meshlets,
            entry.meshletBounds
        ))
            return false;

        resetCurrentMeshlet();
        return true;
    };

    usize seedSearchOffset = 0u;
    while(const Expected<u32> seedTriangleIndex = TryFindNextUnvisitedMeshletTriangle(trianglePrecompute.visitedTriangles, trianglePrecompute.triangles.size(), seedSearchOffset)){
        seedSearchOffset = *seedTriangleIndex;
        ClearMeshletFrontier(frontier, frontierFlags);
        resetCurrentMeshlet();

        if(!AddVisitedMeshletTriangle(
            s_MeshMetaKindText,
            AStringView(filePathText),
            *seedTriangleIndex,
            trianglePrecompute.triangles,
            trianglePrecompute.triangleCalculations,
            trianglePrecompute.positionTriangleOffsets,
            trianglePrecompute.positionTriangleIndices,
            trianglePrecompute.visitedTriangles,
            localSourceVertexRefs,
            localTriangleIndices,
            current,
            entry.meshletPrimitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;

        if(!GrowMeshletFromFrontier(s_MeshMetaKindText, AStringView(filePathText), seedSearchOffset, cpuScheduler, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, current, entry.meshletPrimitiveIndices, scoreState, parallelCandidates, frontier, frontierFlags))
            return false;

        ClearMeshletFrontier(frontier, frontierFlags);
        if(!flushMeshlet())
            return false;
    }

    if(entry.meshlets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet build produced no meshlets"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }

    LogMeshletCookMetrics(
        s_MeshMetaKindText,
        AStringView(filePathText),
        indices,
        entry.positions,
        entry.normals,
        entry.tangents,
        entry.uv0,
        entry.colors,
        entry.vertexRefs,
        entry.meshlets,
        entry.meshletBounds,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletLocalVertexRefs,
        entry.meshletPrimitiveIndices
    );
    return true;
}


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


namespace __hidden_skin_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Core::Assets::AssetVector<SkeletonJointMatrix>> ParseInverseBindMatrices(const Path& nwbFilePath, const Value& asset, Core::Assets::AssetArena& arena){
    Core::Assets::AssetVector<SkeletonJointMatrix> values(arena);

    const auto matrices = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinInverseBindMatricesField);
    if(!matrices)
        return MakeUnexpected(matrices.error());

    const auto& matrixList = (*matrices)->asList();
    values.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        const auto matrix = AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(nwbFilePath, matrixList[matrixIndex], s_SkinMetaKind, s_SkinInverseBindMatricesField);
        if(!matrix)
            return MakeUnexpected(Failure{});

        if(!IsValidSkinInverseBindMatrix(LoadFloat(*matrix))){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix"), PathToString<tchar>(nwbFilePath), matrixIndex);
            return MakeUnexpected(Failure{});
        }
        values.push_back(*matrix);
    }

    if(values.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(s_SkinInverseBindMatricesField));
        return MakeUnexpected(Failure{});
    }
    return values;
}

[[nodiscard]] static b8 ValidateSkinInfluenceJointIndices(const Path& nwbFilePath, const SkinCookEntry& entry){
    const Core::Assets::AssetString nwbFilePathText = PathToString(entry.influences.get_allocator().arena(), nwbFilePath);
    return ValidateSkinCookInfluenceJointIndices(AStringView(nwbFilePathText), entry.inverseBindMatrices.size(), entry.influences);
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


b8 SkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_SkinSerializeContext))
        return false;

    const Skin& skin = static_cast<const Skin&>(asset);
    if(!skin.validatePayload())
        return false;

    return SkinBinaryCodec().serialize(
        skin.mesh().name().hash(),
        skin.skeleton().name().hash(),
        skin.influences(),
        skin.inverseBindMatrices(),
        outBinary
    );
}

Expected<SkinCookEntry> ParseSkinCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return Core::Assets::ParseCookMetadataDocument<SkinCookEntry>(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        s_SkinMetaDiagnosticPrefix,
        [&nwbFilePath, &arena](const Name virtualPath, const Core::Metascript::Value& asset){
            return ParseSkinCookMetadata(virtualPath, nwbFilePath, asset, arena);
        },
        scratchArena
    );
}

Expected<SkinCookEntry> ParseSkinCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena){
    using namespace __hidden_skin_cook;

    SkinCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_SkinMetaDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, &IsAllowedSkinMetadataField))
        return MakeUnexpected(Failure{});
    auto meshResult = Core::Assets::ReadMetadataAssetRefField<Mesh>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinMetaMeshField, true);
    if(!meshResult)
        return MakeUnexpected(Failure{});
    entry.mesh = *meshResult;
    auto skeletonResult = Core::Assets::ReadMetadataAssetRefField<Skeleton>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinMetaSkeletonField, true);
    if(!skeletonResult)
        return MakeUnexpected(Failure{});
    entry.skeleton = *skeletonResult;
    if(!SkinInfluenceMetadataParser::Parse(nwbFilePath, asset, NWB_TEXT("Skin"), {}, true, entry.influences))
        return MakeUnexpected(Failure{});
    auto inverseBindMatricesResult = ParseInverseBindMatrices(nwbFilePath, asset, arena);
    if(!inverseBindMatricesResult)
        return MakeUnexpected(Failure{});
    entry.inverseBindMatrices = Move(*inverseBindMatricesResult);
    if(!ValidateSkinInfluenceJointIndices(nwbFilePath, entry))
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<Skin> BuildSkinAsset(SkinCookEntry& skinEntry, Core::Assets::AssetArena& arena){
    Skin asset(arena, skinEntry.virtualPath);
    asset.setMesh(skinEntry.mesh);
    asset.setSkeleton(skinEntry.skeleton);
    asset.setPayload(Move(skinEntry.influences), Move(skinEntry.inverseBindMatrices));
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
