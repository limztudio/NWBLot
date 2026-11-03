// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlet_candidates.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshletCandidateSearch::ResetMeshletScoreState(MeshletScoreState& state)noexcept{
    AabbTests::Reset(state.minBounds, state.maxBounds);
    state.centroidSum = VectorZero();
    state.normalSum = VectorZero();
    state.normalAxis = VectorZero();
    state.primitiveCount = 0u;
    state.radius = VectorZero();
    state.coneCutoff = s_SIMDNegativeOne;
    state.coneEnabled = false;
}


f32 MeshletCandidateSearch::PredictMeshletScoreRadius(const MeshletScoreState& state, const Array<SIMDVectorSingle, s_MeshletTriangleIndexCount>& trianglePositions)noexcept{
    SIMDVectorSingle minBounds = state.minBounds;
    SIMDVectorSingle maxBounds = state.maxBounds;
    AabbTests::ExpandTriangle(trianglePositions[0u], trianglePositions[1u], trianglePositions[2u], minBounds, maxBounds);

    return VectorGetX(Vector3Length(AabbTests::Extents(minBounds, maxBounds)));
}


f32 MeshletCandidateSearch::MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVectorSingle triangleCentroid)noexcept{
    if(state.primitiveCount == 0u)
        return 0.0f;

    const SIMDVectorSingle meshletCentroid = VectorDivide(state.centroidSum, VectorReplicate(static_cast<f32>(state.primitiveCount)));
    return VectorGetX(Vector3Length(VectorSubtract(triangleCentroid, meshletCentroid)));
}


f32 MeshletCandidateSearch::MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVectorSingle triangleAreaNormal)noexcept{
    const SIMDVectorSingle candidateNormal = Vector3NormalizeOr(triangleAreaNormal, VectorZero(), ::s_FrameDirectionEpsilon);
    if(!::FrameValidDirection(state.normalAxis) || !::FrameValidDirection(candidateNormal))
        return 0.0f;

    return VectorGetX(Vector3Dot(state.normalAxis, candidateNormal));
}


void MeshletCandidateSearch::UpdateMeshletScoreConeCutoff(const SIMDVectorSingle axis, const SIMDVectorSingle triangleAreaNormal, b8& hasNormal, SIMDVectorSingle& coneCutoff)noexcept{
    const SIMDVectorSingle faceNormal = Vector3NormalizeOr(triangleAreaNormal, VectorZero(), ::s_FrameDirectionEpsilon);
    if(!::FrameValidDirection(faceNormal))
        return;

    hasNormal = true;
    // Cutoff tracks the minimum dot over faces; both candidates stay replicated on SIMD lanes.
    coneCutoff = VectorMin(coneCutoff, Vector3Dot(axis, faceNormal));
}


void MeshletCandidateSearch::UpdateBestMeshletCandidateIfBetter(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs,
    const usize frontierOffset,
    const u32 triangleIndex,
    const b8 disconnected,
    b8& found,
    MeshletFrontierCandidate& outCandidate
)noexcept{
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    const Expected<MeshletVertexCounts> vertexCounts = TryMeshletCanFitTriangle(meshlet, localVertexRefs, triangle);
    if(!vertexCounts)
        return;

    const f32 score = ScoreMeshletCandidate(
        triangleIndices,
        scoreState,
        triangleIndex,
        trianglePrecompute.loadTriangleVectors(triangleIndex),
        trianglePrecompute.triangleAreaNormalAt(),
        vertexCounts->shared,
        vertexCounts->missing,
        disconnected
    );
    if(!IsBetterMeshletCandidate(score, triangleIndex, found, outCandidate))
        return;

    found = true;
    outCandidate.frontierOffset = frontierOffset;
    outCandidate.triangleIndex = triangleIndex;
    outCandidate.score = score;
}


Expected<MeshletFrontierCandidate> MeshletCandidateSearch::FindBestMeshletFrontierCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Span<const u32> frontier,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs
)noexcept{
    b8 found = false;
    MeshletFrontierCandidate candidate;
    for(usize frontierOffset = 0u; frontierOffset < frontier.size(); ++frontierOffset){
        const u32 triangleIndex = frontier[frontierOffset];
        UpdateBestMeshletCandidateIfBetter(trianglePrecompute, triangleIndices, scoreState, meshlet, localVertexRefs, frontierOffset, triangleIndex, false, found, candidate);
    }

    if(!found)
        return MakeUnexpected(Failure{});
    return candidate;
}


Expected<MeshletFrontierCandidate> MeshletCandidateSearch::FindBestDisconnectedMeshletCandidateRange(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchBegin,
    const usize searchEnd,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs
)noexcept{
    b8 found = false;
    MeshletFrontierCandidate candidate;
    for(usize triangleIndex = searchBegin; triangleIndex < searchEnd; ++triangleIndex){
        if(trianglePrecompute.visitedTriangles[triangleIndex] != 0u)
            continue;

        UpdateBestMeshletCandidateIfBetter(trianglePrecompute, triangleIndices, scoreState, meshlet, localVertexRefs, 0u, static_cast<u32>(triangleIndex), true, found, candidate);
    }

    if(!found)
        return MakeUnexpected(Failure{});
    return candidate;
}


Expected<MeshletFrontierCandidate> MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs,
    Core::CpuTaskScheduler& cpuScheduler,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates
){
    const usize triangleCount = trianglePrecompute.triangles.size();
    if(searchOffset >= triangleCount)
        return MakeUnexpected(Failure{});

    const usize searchCount = triangleCount - searchOffset;
    if(!cpuScheduler.isParallelEnabled() || searchCount < s_MeshletDisconnectedCandidateParallelThreshold){
        return FindBestDisconnectedMeshletCandidateRange(trianglePrecompute, searchOffset, triangleCount, triangleIndices, scoreState, meshlet, localVertexRefs);
    }

    const usize chunkCount = ResolveMeshletDisconnectedCandidateChunkCount(searchCount, cpuScheduler.workerThreadCount());

    parallelCandidates.clear();
    parallelCandidates.resize(chunkCount);
    const auto parallelWork = cpuScheduler.parallelFor(static_cast<usize>(0), chunkCount, [&](const usize chunkIndex)noexcept(
        noexcept(FindBestDisconnectedMeshletCandidateRange(trianglePrecompute, DeclVal<const usize&>(), DeclVal<const usize&>(), triangleIndices, scoreState, meshlet, localVertexRefs))
    ){
        usize chunkBegin = 0u;
        usize chunkEnd = 0u;
        ResolveMeshletDisconnectedCandidateChunkRange(searchOffset, searchCount, chunkCount, chunkIndex, chunkBegin, chunkEnd);

        MeshletCandidateSearchResult result;
        const auto foundCandidate = FindBestDisconnectedMeshletCandidateRange(trianglePrecompute, chunkBegin, chunkEnd, triangleIndices, scoreState, meshlet, localVertexRefs);
        result.found = foundCandidate.has_value();
        if(foundCandidate)
            result.candidate = *foundCandidate;
        parallelCandidates[chunkIndex] = result;
    });
    if(!parallelWork){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshCook: disconnected candidate preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
        return MakeUnexpected(Failure{});
    }

    b8 found = false;
    MeshletFrontierCandidate candidate;
    for(const MeshletCandidateSearchResult& result : parallelCandidates)
        UpdateBestMeshletCandidateFromResult(result, found, candidate);

    if(!found)
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

