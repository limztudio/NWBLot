// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"
#include <impl/assets_mesh/cook_types.adhd.h>

#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletConeScore{
    SIMDVectorSingle cutoff = s_SIMDNegativeOne;
    b8 enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scores frontier and disconnected triangles against the growing meshlet and picks the best fit.
namespace MeshletCandidateSearch{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void ResetMeshletScoreState(MeshletScoreState& state)noexcept{
    Impl::ResetMeshletScoreState(state);
}

inline void UpdateMeshletScoreConeCutoff(const SIMDVectorSingle axis, const SIMDVectorSingle triangleAreaNormal, b8& hasNormal, SIMDVectorSingle& coneCutoff)noexcept{
    Float4 axisVec;
    Float4 normalVec;
    Float4 cutoffVec;
    StoreFloat(axis, axisVec);
    StoreFloat(triangleAreaNormal, normalVec);
    StoreFloat(coneCutoff, cutoffVec);
    UpdateMeshletScoreConeCutoffVec(axisVec, normalVec, hasNormal, cutoffVec);
    coneCutoff = LoadFloat(cutoffVec);
}

template<typename TriangleAreaNormalAtT>
[[nodiscard]] MeshletConeScore ComputeMeshletScoreConeCutoff(
    Span<const u32> triangleIndices,
    SIMDVectorSingle axis,
    u32 extraTriangleIndex,
    b8 hasExtraTriangle,
    const TriangleAreaNormalAtT& triangleAreaNormalAt
)noexcept(
    noexcept(DeclVal<void(*)(SIMDVectorSingle)noexcept>()(triangleAreaNormalAt(DeclVal<const u32&>())))
);
template<typename TriangleAreaNormalAtT>
[[nodiscard]] f32 PredictMeshletScoreConeWidening(
    Span<const u32> triangleIndices,
    const MeshletScoreState& state,
    u32 triangleIndex,
    SIMDVectorSingle triangleAreaNormal,
    const TriangleAreaNormalAtT& triangleAreaNormalAt
)noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), Vector3NormalizeOr(VectorAdd(LoadFloat(state.normalSum), DeclVal<SIMDVectorSingle>()), VectorZero(), ::s_FrameDirectionEpsilon), 0u, true, triangleAreaNormalAt)));

[[nodiscard]] inline Expected<MeshletFrontierCandidate> FindBestMeshletFrontierCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Span<const u32> frontier,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs
)noexcept{
    return FindBestMeshletFrontierCandidateFromPrecompute(
        trianglePrecompute.triangles,
        trianglePrecompute.triangleCalculations,
        frontier,
        triangleIndices,
        scoreState,
        meshlet,
        localVertexRefs
    );
}

[[nodiscard]] inline Expected<MeshletFrontierCandidate> FindBestDisconnectedMeshletCandidateRange(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchBegin,
    const usize searchEnd,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs
)noexcept{
    return FindBestDisconnectedMeshletCandidateRangeFromPrecompute(
        trianglePrecompute.triangles,
        trianglePrecompute.triangleCalculations,
        trianglePrecompute.visitedTriangles,
        searchBegin,
        searchEnd,
        triangleIndices,
        scoreState,
        meshlet,
        localVertexRefs
    );
}

[[nodiscard]] inline Expected<MeshletFrontierCandidate> FindBestDisconnectedMeshletCandidate(
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TriangleAreaNormalAtT>
MeshletConeScore MeshletCandidateSearch::ComputeMeshletScoreConeCutoff(const Span<const u32> triangleIndices, const SIMDVectorSingle axis, const u32 extraTriangleIndex, const b8 hasExtraTriangle, const TriangleAreaNormalAtT& triangleAreaNormalAt)noexcept(
    noexcept(DeclVal<void(*)(SIMDVectorSingle)noexcept>()(triangleAreaNormalAt(DeclVal<const u32&>())))
){
    if(!::FrameValidDirection(axis))
        return {};

    b8 hasNormal = false;
    SIMDVectorSingle coneCutoff = s_SIMDOne;
    for(const u32 triangleIndex : triangleIndices){
        const SIMDVectorSingle triangleAreaNormal = triangleAreaNormalAt(triangleIndex);
        UpdateMeshletScoreConeCutoff(axis, triangleAreaNormal, hasNormal, coneCutoff);
    }
    if(hasExtraTriangle){
        const SIMDVectorSingle triangleAreaNormal = triangleAreaNormalAt(extraTriangleIndex);
        UpdateMeshletScoreConeCutoff(axis, triangleAreaNormal, hasNormal, coneCutoff);
    }

    if(!hasNormal || !Vector4Greater(coneCutoff, VectorZero()))
        return {};

    return MeshletConeScore{ coneCutoff, true };
}


template<typename TriangleAreaNormalAtT>
f32 MeshletCandidateSearch::PredictMeshletScoreConeWidening(
    const Span<const u32> triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const SIMDVectorSingle triangleAreaNormal,
    const TriangleAreaNormalAtT& triangleAreaNormalAt
)noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), Vector3NormalizeOr(VectorAdd(LoadFloat(state.normalSum), DeclVal<SIMDVectorSingle>()), VectorZero(), ::s_FrameDirectionEpsilon), 0u, true, triangleAreaNormalAt))){
    if(!state.coneEnabled)
        return 0.0f;

    const SIMDVectorSingle predictedAxis = Vector3NormalizeOr(VectorAdd(LoadFloat(state.normalSum), triangleAreaNormal), VectorZero(), ::s_FrameDirectionEpsilon);
    const auto predictedCone = ComputeMeshletScoreConeCutoff(triangleIndices, predictedAxis, triangleIndex, true, triangleAreaNormalAt);
    if(!predictedCone.enabled)
        return 1.0f;

    return Max(state.coneCutoff.x - VectorGetX(predictedCone.cutoff), 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

