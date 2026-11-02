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

struct MeshletScoreState{
    SIMDVectorSingle minBounds;
    SIMDVectorSingle maxBounds;
    SIMDVectorSingle centroidSum;
    SIMDVectorSingle normalSum;
    SIMDVectorSingle normalAxis;
    SIMDVectorSingle radius = VectorZero();
    SIMDVectorSingle coneCutoff = s_SIMDNegativeOne;
    u32 primitiveCount = 0u;
    b8 coneEnabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scores frontier and disconnected triangles against the growing meshlet and picks the best fit.
namespace MeshletCandidateSearch{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ResetMeshletScoreState(MeshletScoreState& state)noexcept;
[[nodiscard]] f32 PredictMeshletScoreRadius(const MeshletScoreState& state, const Array<SIMDVectorSingle, s_MeshletTriangleIndexCount>& trianglePositions)noexcept;
[[nodiscard]] f32 MeshletScoreCentroidDistance(const MeshletScoreState& state, SIMDVectorSingle triangleCentroid)noexcept;
[[nodiscard]] f32 MeshletScoreNormalCoherence(const MeshletScoreState& state, SIMDVectorSingle triangleAreaNormal)noexcept;
void UpdateMeshletScoreConeCutoff(SIMDVectorSingle axis, SIMDVectorSingle triangleAreaNormal, b8& hasNormal, SIMDVectorSingle& coneCutoff)noexcept;
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
)noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), Vector3NormalizeOr(VectorAdd(state.normalSum, DeclVal<SIMDVectorSingle>()), VectorZero(), ::s_FrameDirectionEpsilon), 0u, true, triangleAreaNormalAt)));
template<typename TriangleAreaNormalAtT>
[[nodiscard]] f32 ScoreMeshletCandidate(
    Span<const u32> triangleIndices,
    const MeshletScoreState& state,
    u32 triangleIndex,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    u32 sharedVertexCount,
    u32 missingVertexCount,
    b8 disconnected
)noexcept(noexcept(PredictMeshletScoreConeWidening(DeclVal<Span<const u32>>(), state, 0u, triangleVectors.areaNormal, triangleAreaNormalAt)));
void UpdateBestMeshletCandidateIfBetter(
    const MeshletTrianglePrecompute& trianglePrecompute,
    Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    Span<const u32> localVertexRefs,
    usize frontierOffset,
    u32 triangleIndex,
    b8 disconnected,
    b8& found,
    MeshletFrontierCandidate& outCandidate
)noexcept;
[[nodiscard]] Expected<MeshletFrontierCandidate> FindBestMeshletFrontierCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    Span<const u32> frontier,
    Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    Span<const u32> localVertexRefs
)noexcept;
[[nodiscard]] Expected<MeshletFrontierCandidate> FindBestDisconnectedMeshletCandidateRange(
    const MeshletTrianglePrecompute& trianglePrecompute,
    usize searchBegin,
    usize searchEnd,
    Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    Span<const u32> localVertexRefs
)noexcept;
[[nodiscard]] Expected<MeshletFrontierCandidate> FindBestDisconnectedMeshletCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    usize searchOffset,
    Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    Span<const u32> localVertexRefs,
    Core::CpuTaskScheduler& cpuScheduler,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates
);
template<typename TriangleAreaNormalAtT>
void AddMeshletTriangleToScoreState(Span<const u32> triangleIndices, const MeshletTriangleVectors& triangleVectors, const TriangleAreaNormalAtT& triangleAreaNormalAt, MeshletScoreState& state)noexcept(
    noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), state.normalAxis, 0u, false, triangleAreaNormalAt))
);


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
)noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), Vector3NormalizeOr(VectorAdd(state.normalSum, DeclVal<SIMDVectorSingle>()), VectorZero(), ::s_FrameDirectionEpsilon), 0u, true, triangleAreaNormalAt))){
    if(!state.coneEnabled)
        return 0.0f;

    const SIMDVectorSingle predictedAxis = Vector3NormalizeOr(VectorAdd(state.normalSum, triangleAreaNormal), VectorZero(), ::s_FrameDirectionEpsilon);
    const auto predictedCone = ComputeMeshletScoreConeCutoff(triangleIndices, predictedAxis, triangleIndex, true, triangleAreaNormalAt);
    if(!predictedCone.enabled)
        return 1.0f;

    return Max(VectorGetX(state.coneCutoff) - VectorGetX(predictedCone.cutoff), 0.0f);
}


template<typename TriangleAreaNormalAtT>
f32 MeshletCandidateSearch::ScoreMeshletCandidate(
    const Span<const u32> triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    const u32 sharedVertexCount,
    const u32 missingVertexCount,
    const b8 disconnected
)noexcept(noexcept(PredictMeshletScoreConeWidening(DeclVal<Span<const u32>>(), state, 0u, triangleVectors.areaNormal, triangleAreaNormalAt))){
    const f32 predictedRadius = PredictMeshletScoreRadius(state, triangleVectors.positions);
    const f32 predictedRadiusGrowth = Max(predictedRadius - VectorGetX(state.radius), 0.0f);
    const f32 centroidDistance = MeshletScoreCentroidDistance(state, triangleVectors.centroid);
    const f32 normalCoherence = MeshletScoreNormalCoherence(state, triangleVectors.areaNormal);
    const f32 coneWidening = PredictMeshletScoreConeWidening(triangleIndices, state, triangleIndex, triangleVectors.areaNormal, triangleAreaNormalAt);
    return EvaluateMeshletCandidateScore(sharedVertexCount, missingVertexCount, predictedRadiusGrowth, centroidDistance, normalCoherence, coneWidening, disconnected);
}


template<typename TriangleAreaNormalAtT>
void MeshletCandidateSearch::AddMeshletTriangleToScoreState(const Span<const u32> triangleIndices, const MeshletTriangleVectors& triangleVectors, const TriangleAreaNormalAtT& triangleAreaNormalAt, MeshletScoreState& state)noexcept(
    noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), state.normalAxis, 0u, false, triangleAreaNormalAt))
){
    AabbTests::ExpandTriangle(triangleVectors.positions[0u], triangleVectors.positions[1u], triangleVectors.positions[2u], state.minBounds, state.maxBounds);
    state.radius = Vector3Length(AabbTests::Extents(state.minBounds, state.maxBounds));
    state.centroidSum = VectorAdd(state.centroidSum, triangleVectors.centroid);
    state.normalSum = VectorAdd(state.normalSum, triangleVectors.areaNormal);
    state.normalAxis = Vector3NormalizeOr(state.normalSum, VectorZero(), ::s_FrameDirectionEpsilon);
    ++state.primitiveCount;
    const auto cone = ComputeMeshletScoreConeCutoff(triangleIndices, state.normalAxis, 0u, false, triangleAreaNormalAt);
    state.coneCutoff = cone.cutoff;
    state.coneEnabled = cone.enabled;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

