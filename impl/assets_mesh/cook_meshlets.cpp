// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlets.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVector MeshCookMeshlets::MakeMeshletPositionVector(const SIMDVector position){
    return VectorSetW(position, 0.0f);
}


MeshletTriangleVectors MeshCookMeshlets::MakeMeshletTriangleVectors(
    const SIMDVector position0,
    const SIMDVector position1,
    const SIMDVector position2,
    const SIMDVector centroid,
    const SIMDVector areaNormal
){
    return MeshletTriangleVectors{
        {
            position0,
            position1,
            position2,
        },
        centroid,
        areaNormal
    };
}


SIMDVector MeshCookMeshlets::NormalizeMeshletDirectionOrZero(const SIMDVector value){
    return Vector3NormalizeOr(value, VectorZero(), ::s_FrameDirectionEpsilon);
}


usize MeshCookMeshlets::EstimateMeshletSourceBytes(
    const Core::Assets::AssetVector<u32>& indices,
    const MeshCookEntry& entry
){
    return EstimateCommonMeshletSourceBytes(indices, entry);
}


usize MeshCookMeshlets::EstimateMeshletRuntimeBytes(const MeshCookEntry& entry){
    return EstimateCommonMeshletRuntimeBytes(entry);
}


void MeshCookMeshlets::ResetMeshletScoreState(MeshletScoreState& state){
    AabbTests::Reset(state.minBounds, state.maxBounds);
    state.centroidSum = VectorZero();
    state.normalSum = VectorZero();
    state.normalAxis = VectorZero();
    state.primitiveCount = 0u;
    state.radius = 0.0f;
    state.coneCutoff = -1.0f;
    state.coneEnabled = false;
}


void MeshCookMeshlets::AccumulateMeshletScoreBounds(
    const SIMDVector (&trianglePositions)[s_MeshletTriangleIndexCount],
    SIMDVector& minBounds,
    SIMDVector& maxBounds
){
    AabbTests::ExpandTriangle(trianglePositions[0u], trianglePositions[1u], trianglePositions[2u], minBounds, maxBounds);
}


f32 MeshCookMeshlets::PredictMeshletScoreRadius(
    const MeshletScoreState& state,
    const SIMDVector (&trianglePositions)[s_MeshletTriangleIndexCount]
){
    SIMDVector minBounds = state.minBounds;
    SIMDVector maxBounds = state.maxBounds;
    AccumulateMeshletScoreBounds(trianglePositions, minBounds, maxBounds);

    return AabbTests::Radius(minBounds, maxBounds);
}


f32 MeshCookMeshlets::MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVector triangleCentroid){
    if(state.primitiveCount == 0u)
        return 0.0f;

    const SIMDVector meshletCentroid = VectorScale(state.centroidSum, 1.0f / static_cast<f32>(state.primitiveCount));
    return VectorGetX(Vector3Length(VectorSubtract(triangleCentroid, meshletCentroid)));
}


f32 MeshCookMeshlets::MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVector triangleAreaNormal){
    const SIMDVector candidateNormal = NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!::FrameValidDirection(state.normalAxis) || !::FrameValidDirection(candidateNormal))
        return 0.0f;

    return VectorGetX(Vector3Dot(state.normalAxis, candidateNormal));
}


void MeshCookMeshlets::UpdateMeshletScoreConeCutoff(
    const SIMDVector axis,
    const SIMDVector triangleAreaNormal,
    bool& hasNormal,
    f32& coneCutoff
){
    const SIMDVector faceNormal = NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!::FrameValidDirection(faceNormal))
        return;

    hasNormal = true;
    coneCutoff = Min(coneCutoff, VectorGetX(Vector3Dot(axis, faceNormal)));
}


bool MeshCookMeshlets::FindNextUnvisitedMeshletTriangle(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    u32& outTriangleIndex
){
    outTriangleIndex = 0u;
    for(usize triangleIndex = searchOffset; triangleIndex < trianglePrecompute.triangles.size(); ++triangleIndex){
        if(trianglePrecompute.visitedTriangles[triangleIndex] != 0u)
            continue;

        outTriangleIndex = static_cast<u32>(triangleIndex);
        return true;
    }

    return false;
}


void MeshCookMeshlets::UpdateBestMeshletCandidateFromResult(
    const MeshletCandidateSearchResult& candidate,
    bool& found,
    MeshletFrontierCandidate& outCandidate
){
    if(!candidate.found)
        return;

    if(
        found
        && (
            candidate.candidate.score < outCandidate.score
            || (
                candidate.candidate.score == outCandidate.score
                && candidate.candidate.triangleIndex > outCandidate.triangleIndex
            )
        )
    )
        return;

    found = true;
    outCandidate = candidate.candidate;
}


bool MeshCookMeshlets::AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    if(!AddMeshletTriangleToBuilder(nwbFilePath, metaKind, triangle, localSourceVertexRefs, meshlet, primitiveIndices))
        return false;

    localTriangleIndices.push_back(triangleIndex);
    const MeshletTriangleVectors& triangleVectors = trianglePrecompute.triangleCalculations[triangleIndex].vectors;
    const auto triangleAreaNormalAt = [&](const u32 otherTriangleIndex){
        return trianglePrecompute.triangleCalculations[otherTriangleIndex].vectors.areaNormal;
    };
    AddMeshletTriangleToScoreState(localTriangleIndices, triangleVectors, triangleAreaNormalAt, scoreState);
    trianglePrecompute.visitedTriangles[triangleIndex] = 1u;
    AddMeshletTriangleNeighborsToFrontier(trianglePrecompute, triangleIndex, frontier, frontierFlags);
    return true;
}


bool MeshCookMeshlets::GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const usize seedSearchOffset,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Assets::AssetVector<MeshletCandidateSearchResult>& parallelCandidates,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            MeshletFrontierCandidate disconnectedCandidate;
            if(!FindBestDisconnectedMeshletCandidate(
                trianglePrecompute,
                seedSearchOffset + 1u,
                localTriangleIndices,
                scoreState,
                meshlet,
                localSourceVertexRefs,
                cpuScheduler,
                parallelCandidates,
                disconnectedCandidate
            ))
                break;

            if(!AddVisitedMeshletTriangle(
                nwbFilePath,
                metaKind,
                trianglePrecompute,
                disconnectedCandidate.triangleIndex,
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

        MeshletFrontierCandidate bestCandidate;
        if(!FindBestMeshletFrontierCandidate(
            trianglePrecompute,
            frontier,
            localTriangleIndices,
            scoreState,
            meshlet,
            localSourceVertexRefs,
            bestCandidate
        ))
            break;

        RemoveMeshletFrontierCandidate(frontier, frontierFlags, bestCandidate.frontierOffset);
        if(!AddVisitedMeshletTriangle(
            nwbFilePath,
            metaKind,
            trianglePrecompute,
            bestCandidate.triangleIndex,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

