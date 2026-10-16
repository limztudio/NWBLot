// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlets.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVectorSingle MeshCookMeshlets::MakeMeshletPositionVector(const SIMDVectorSingle position)noexcept{
    return VectorSetW(position, 0.0f);
}


MeshletTriangleVectors MeshCookMeshlets::MakeMeshletTriangleVectors(const SIMDVectorSingle position0, const SIMDVectorSingle position1, const SIMDVectorSingle position2, const SIMDVectorSingle centroid, const SIMDVectorSingle areaNormal)noexcept{
    return MeshletTriangleVectors{ { position0, position1, position2, }, centroid, areaNormal };
}


SIMDVectorSingle MeshCookMeshlets::NormalizeMeshletDirectionOrZero(const SIMDVectorSingle value)noexcept{
    return Vector3NormalizeOr(value, VectorZero(), ::s_FrameDirectionEpsilon);
}


usize MeshCookMeshlets::EstimateMeshletSourceBytes(const Core::Assets::AssetVector<u32>& indices, const MeshCookEntry& entry)noexcept{
    return EstimateCommonMeshletSourceBytes(indices, entry);
}


usize MeshCookMeshlets::EstimateMeshletRuntimeBytes(const MeshCookEntry& entry)noexcept{
    return EstimateCommonMeshletRuntimeBytes(entry);
}


void MeshCookMeshlets::ResetMeshletScoreState(MeshletScoreState& state)noexcept{
    AabbTests::Reset(state.minBounds, state.maxBounds);
    state.centroidSum = VectorZero();
    state.normalSum = VectorZero();
    state.normalAxis = VectorZero();
    state.primitiveCount = 0u;
    state.radius = 0.0f;
    state.coneCutoff = -1.0f;
    state.coneEnabled = false;
}


void MeshCookMeshlets::AccumulateMeshletScoreBounds(const SIMDVectorSingle (&trianglePositions)[s_MeshletTriangleIndexCount], SIMDVectorSingle& minBounds, SIMDVectorSingle& maxBounds)noexcept{
    AabbTests::ExpandTriangle(trianglePositions[0u], trianglePositions[1u], trianglePositions[2u], minBounds, maxBounds);
}


f32 MeshCookMeshlets::PredictMeshletScoreRadius(const MeshletScoreState& state, const SIMDVectorSingle (&trianglePositions)[s_MeshletTriangleIndexCount])noexcept{
    SIMDVectorSingle minBounds = state.minBounds;
    SIMDVectorSingle maxBounds = state.maxBounds;
    AccumulateMeshletScoreBounds(trianglePositions, minBounds, maxBounds);

    return AabbTests::Radius(minBounds, maxBounds);
}


f32 MeshCookMeshlets::MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVectorSingle triangleCentroid)noexcept{
    if(state.primitiveCount == 0u)
        return 0.0f;

    const SIMDVectorSingle meshletCentroid = VectorDivide(state.centroidSum, VectorReplicate(static_cast<f32>(state.primitiveCount)));
    return VectorGetX(Vector3Length(VectorSubtract(triangleCentroid, meshletCentroid)));
}


f32 MeshCookMeshlets::MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVectorSingle triangleAreaNormal)noexcept{
    const SIMDVectorSingle candidateNormal = NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!::FrameValidDirection(state.normalAxis) || !::FrameValidDirection(candidateNormal))
        return 0.0f;

    return VectorGetX(Vector3Dot(state.normalAxis, candidateNormal));
}


void MeshCookMeshlets::UpdateMeshletScoreConeCutoff(const SIMDVectorSingle axis, const SIMDVectorSingle triangleAreaNormal, bool& hasNormal, f32& coneCutoff)noexcept{
    const SIMDVectorSingle faceNormal = NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!::FrameValidDirection(faceNormal))
        return;

    hasNormal = true;
    // Cutoff tracks the minimum dot over faces; both candidates stay replicated on SIMD lanes.
    coneCutoff = VectorGetX(VectorMin(VectorReplicate(coneCutoff), Vector3Dot(axis, faceNormal)));
}


Expected<u32> MeshCookMeshlets::FindNextUnvisitedMeshletTriangle(const MeshletTrianglePrecompute& trianglePrecompute, const usize searchOffset)noexcept{
    for(usize triangleIndex = searchOffset; triangleIndex < trianglePrecompute.triangles.size(); ++triangleIndex){
        if(trianglePrecompute.visitedTriangles[triangleIndex] != 0u)
            continue;

        return static_cast<u32>(triangleIndex);
    }

    return MakeUnexpected(Failure{});
}


void MeshCookMeshlets::UpdateBestMeshletCandidateFromResult(const MeshletCandidateSearchResult& candidate, bool& found, MeshletFrontierCandidate& outCandidate)noexcept{
    if(!candidate.found)
        return;

    if(found && (candidate.candidate.score < outCandidate.score || (candidate.candidate.score == outCandidate.score && candidate.candidate.triangleIndex > outCandidate.triangleIndex)))
        return;

    found = true;
    outCandidate = candidate.candidate;
}


bool MeshCookMeshlets::AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const TStringView metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    Vector<u8, Core::Alloc::ScratchArena>& frontierFlags
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    if(!AddMeshletTriangleToBuilder(nwbFilePath, metaKind, triangle, localSourceVertexRefs, meshlet, primitiveIndices))
        return false;

    localTriangleIndices.push_back(triangleIndex);
    const MeshletTriangleVectors& triangleVectors = trianglePrecompute.triangleCalculations[triangleIndex].vectors;
    const auto triangleAreaNormalAt = [&](const u32 otherTriangleIndex)noexcept{
        return trianglePrecompute.triangleCalculations[otherTriangleIndex].vectors.areaNormal;
    };
    AddMeshletTriangleToScoreState(localTriangleIndices, triangleVectors, triangleAreaNormalAt, scoreState);
    trianglePrecompute.visitedTriangles[triangleIndex] = 1u;
    AddMeshletTriangleNeighborsToFrontier(trianglePrecompute, triangleIndex, frontier, frontierFlags);
    return true;
}


bool MeshCookMeshlets::GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const TStringView metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const usize seedSearchOffset,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::CpuTaskScheduler& cpuScheduler,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    Vector<u8, Core::Alloc::ScratchArena>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            const auto disconnectedCandidate = FindBestDisconnectedMeshletCandidate(trianglePrecompute, seedSearchOffset + 1u, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs, cpuScheduler, parallelCandidates);
            if(!disconnectedCandidate)
                break;

            if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, trianglePrecompute, disconnectedCandidate->triangleIndex, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
                return false;
            continue;
        }

        const auto bestCandidate = FindBestMeshletFrontierCandidate(trianglePrecompute, frontier, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs);
        if(!bestCandidate)
            break;

        RemoveMeshletFrontierCandidate(frontier, frontierFlags, bestCandidate->frontierOffset);
        if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, trianglePrecompute, bestCandidate->triangleIndex, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

