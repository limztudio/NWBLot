// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"

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
class MeshletCandidateSearch final : NoCopy{
public:
    static void ResetMeshletScoreState(MeshletScoreState& state)noexcept;
    [[nodiscard]] static f32 PredictMeshletScoreRadius(const MeshletScoreState& state, const Array<SIMDVectorSingle, s_MeshletTriangleIndexCount>& trianglePositions)noexcept;
    [[nodiscard]] static f32 MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVectorSingle triangleCentroid)noexcept;
    [[nodiscard]] static f32 MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVectorSingle triangleAreaNormal)noexcept;
    static void UpdateMeshletScoreConeCutoff(const SIMDVectorSingle axis, const SIMDVectorSingle triangleAreaNormal, b8& hasNormal, SIMDVectorSingle& coneCutoff)noexcept;
    template<typename TriangleAreaNormalAtT>
    [[nodiscard]] static MeshletConeScore ComputeMeshletScoreConeCutoff(
        Span<const u32> triangleIndices,
        const SIMDVectorSingle axis,
        const u32 extraTriangleIndex,
        const b8 hasExtraTriangle,
        const TriangleAreaNormalAtT& triangleAreaNormalAt
    )noexcept(
        noexcept(DeclVal<void(*)(SIMDVectorSingle)noexcept>()(triangleAreaNormalAt(DeclVal<const u32&>())))
    );
    template<typename TriangleAreaNormalAtT>
    [[nodiscard]] static f32 PredictMeshletScoreConeWidening(
        Span<const u32> triangleIndices,
        const MeshletScoreState& state,
        const u32 triangleIndex,
        const SIMDVectorSingle triangleAreaNormal,
        const TriangleAreaNormalAtT& triangleAreaNormalAt
    )noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), Vector3NormalizeOr(VectorAdd(state.normalSum, triangleAreaNormal), VectorZero(), ::s_FrameDirectionEpsilon), triangleIndex, true, triangleAreaNormalAt)));
    template<typename TriangleAreaNormalAtT>
    [[nodiscard]] static f32 ScoreMeshletCandidate(
        Span<const u32> triangleIndices,
        const MeshletScoreState& state,
        const u32 triangleIndex,
        const MeshletTriangleVectors& triangleVectors,
        const TriangleAreaNormalAtT& triangleAreaNormalAt,
        const u32 sharedVertexCount,
        const u32 missingVertexCount,
        const b8 disconnected
    )noexcept(noexcept(PredictMeshletScoreConeWidening(DeclVal<Span<const u32>>(), state, triangleIndex, triangleVectors.areaNormal, triangleAreaNormalAt)));
    static void UpdateBestMeshletCandidateIfBetter(
        const MeshletTrianglePrecompute& trianglePrecompute,
        Span<const u32> triangleIndices,
        const MeshletScoreState& scoreState,
        const MeshletDesc& meshlet,
        Span<const u32> localVertexRefs,
        const usize frontierOffset,
        const u32 triangleIndex,
        const b8 disconnected,
        b8& found,
        MeshletFrontierCandidate& outCandidate
    )noexcept;
    [[nodiscard]] static Expected<MeshletFrontierCandidate> FindBestMeshletFrontierCandidate(
        const MeshletTrianglePrecompute& trianglePrecompute,
        Span<const u32> frontier,
        Span<const u32> triangleIndices,
        const MeshletScoreState& scoreState,
        const MeshletDesc& meshlet,
        Span<const u32> localVertexRefs
    )noexcept;
    [[nodiscard]] static Expected<MeshletFrontierCandidate> FindBestDisconnectedMeshletCandidateRange(
        const MeshletTrianglePrecompute& trianglePrecompute,
        const usize searchBegin,
        const usize searchEnd,
        Span<const u32> triangleIndices,
        const MeshletScoreState& scoreState,
        const MeshletDesc& meshlet,
        Span<const u32> localVertexRefs
    )noexcept;
    [[nodiscard]] static Expected<MeshletFrontierCandidate> FindBestDisconnectedMeshletCandidate(
        const MeshletTrianglePrecompute& trianglePrecompute,
        const usize searchOffset,
        Span<const u32> triangleIndices,
        const MeshletScoreState& scoreState,
        const MeshletDesc& meshlet,
        Span<const u32> localVertexRefs,
        Core::CpuTaskScheduler& cpuScheduler,
        Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates
    );
    template<typename TriangleAreaNormalAtT>
    static void AddMeshletTriangleToScoreState(Span<const u32> triangleIndices, const MeshletTriangleVectors& triangleVectors, const TriangleAreaNormalAtT& triangleAreaNormalAt, MeshletScoreState& state)noexcept(
        noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), state.normalAxis, 0u, false, triangleAreaNormalAt))
    );


public:
    MeshletCandidateSearch() = delete;
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
)noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), Vector3NormalizeOr(VectorAdd(state.normalSum, triangleAreaNormal), VectorZero(), ::s_FrameDirectionEpsilon), triangleIndex, true, triangleAreaNormalAt))){
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
)noexcept(noexcept(PredictMeshletScoreConeWidening(DeclVal<Span<const u32>>(), state, triangleIndex, triangleVectors.areaNormal, triangleAreaNormalAt))){
    const f32 predictedRadius = PredictMeshletScoreRadius(state, triangleVectors.positions);
    const f32 predictedRadiusGrowth = Max(predictedRadius - VectorGetX(state.radius), 0.0f);
    const f32 centroidDistance = MeshletScoreCentroidDistance(state, triangleVectors.centroid);
    const f32 normalCoherence = MeshletScoreNormalCoherence(state, triangleVectors.areaNormal);
    const f32 coneWidening = PredictMeshletScoreConeWidening(triangleIndices, state, triangleIndex, triangleVectors.areaNormal, triangleAreaNormalAt);
    return static_cast<f32>(sharedVertexCount) * s_MeshletScoreSharedVertexWeight
        - static_cast<f32>(missingVertexCount) * s_MeshletScoreNewVertexWeight
        - s_MeshletScoreRadiusWeight * predictedRadiusGrowth
        - s_MeshletScoreCentroidWeight * centroidDistance
        + s_MeshletScoreNormalWeight * normalCoherence
        - s_MeshletScoreConePenaltyWeight * coneWidening
        - (disconnected ? s_MeshletScoreDisconnectedPenalty : 0.0f);
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

