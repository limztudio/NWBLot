// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"

#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletFrontierCandidate{
    usize frontierOffset = 0u;
    u32 triangleIndex = 0u;
    f32 score = 0.0f;
};

struct MeshletCandidateSearchResult{
    MeshletFrontierCandidate candidate;
    bool found = false;
};

struct MeshletVertexCounts{
    u32 shared;
    u32 missing;
};

struct MeshletConeScore{
    SIMDVectorSingle cutoff = s_SIMDNegativeOne;
    bool enabled = false;
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
    bool coneEnabled = false;
};

static constexpr f32 s_MeshletScoreSharedVertexWeight = 20.0f;
static constexpr f32 s_MeshletScoreNewVertexWeight = 8.0f;
static constexpr f32 s_MeshletScoreRadiusWeight = 4.0f;
static constexpr f32 s_MeshletScoreCentroidWeight = 1.0f;
static constexpr f32 s_MeshletScoreNormalWeight = 10.0f;
static constexpr f32 s_MeshletScoreConePenaltyWeight = 12.0f;
static constexpr f32 s_MeshletScoreDisconnectedPenalty = 24.0f;
static constexpr usize s_MeshletDisconnectedCandidateParallelThreshold = 4096u;
static constexpr usize s_MeshletDisconnectedCandidateParallelOversubscription = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshletCookExceptionDetail{


template<typename Range>
[[nodiscard]] constexpr bool FrontierIndexIterationIsNothrow()noexcept{
    if constexpr(IsArray_V<Range>)
        return noexcept(DeclVal<void(*)(u32)noexcept>()(DeclVal<RemoveExtent_T<Range>&>()));
    else if constexpr(requires(Range& values){ values.begin(); values.end(); }){
        using Iterator = RemoveCVRef_T<decltype(DeclVal<Range&>().begin())>;
        using Sentinel = RemoveCVRef_T<decltype(DeclVal<Range&>().end())>;
        return
            noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<Range&>().begin()))
            && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<Range&>().end()))
            && IsNothrowDestructible_V<Iterator>
            && IsNothrowDestructible_V<Sentinel>
            && noexcept(static_cast<bool>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
            && noexcept(++DeclVal<Iterator&>())
            && noexcept(DeclVal<void(*)(u32)noexcept>()(*DeclVal<Iterator&>()))
        ;
    }
    else
        return false;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scores frontier and disconnected triangles against the growing meshlet and picks the best fit.
class MeshletCandidateSearch final : NoCopy{
public:
    [[nodiscard]] static Expected<u8> FindMeshletLocalVertex(Span<const u32> localVertexRefs, const u32 vertexRefIndex)noexcept;
    [[nodiscard]] static MeshletVertexCounts CountMeshletMissingVertices(Span<const u32> localVertexRefs, const MeshletTriangleData& triangle)noexcept;
    static void ResetMeshletScoreState(MeshletScoreState& state)noexcept;
    static void AccumulateMeshletScoreBounds(const SIMDVectorSingle (&trianglePositions)[s_MeshletTriangleIndexCount], SIMDVectorSingle& minBounds, SIMDVectorSingle& maxBounds)noexcept;
    [[nodiscard]] static SIMDVectorSingle PredictMeshletScoreRadius(const MeshletScoreState& state, const SIMDVectorSingle (&trianglePositions)[s_MeshletTriangleIndexCount])noexcept;
    [[nodiscard]] static SIMDVectorSingle MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVectorSingle triangleCentroid)noexcept;
    [[nodiscard]] static SIMDVectorSingle MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVectorSingle triangleAreaNormal)noexcept;
    static void UpdateMeshletScoreConeCutoff(const SIMDVectorSingle axis, const SIMDVectorSingle triangleAreaNormal, bool& hasNormal, SIMDVectorSingle& coneCutoff)noexcept;
    template<typename TriangleAreaNormalAtT>
    [[nodiscard]] static MeshletConeScore ComputeMeshletScoreConeCutoff(
        Span<const u32> triangleIndices,
        const SIMDVectorSingle axis,
        const u32 extraTriangleIndex,
        const bool hasExtraTriangle,
        const TriangleAreaNormalAtT& triangleAreaNormalAt
    )noexcept(
        noexcept(DeclVal<void(*)(SIMDVectorSingle)noexcept>()(triangleAreaNormalAt(DeclVal<const u32&>())))
    );
    template<typename TriangleAreaNormalAtT>
    [[nodiscard]] static SIMDVectorSingle PredictMeshletScoreConeWidening(
        Span<const u32> triangleIndices,
        const MeshletScoreState& state,
        const u32 triangleIndex,
        const SIMDVectorSingle triangleAreaNormal,
        const TriangleAreaNormalAtT& triangleAreaNormalAt
    )noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(VectorAdd(state.normalSum, triangleAreaNormal)), triangleIndex, true, triangleAreaNormalAt)));
    template<typename TriangleAreaNormalAtT>
    [[nodiscard]] static SIMDVectorSingle ScoreMeshletCandidate(
        Span<const u32> triangleIndices,
        const MeshletScoreState& state,
        const u32 triangleIndex,
        const MeshletTriangleVectors& triangleVectors,
        const TriangleAreaNormalAtT& triangleAreaNormalAt,
        const u32 sharedVertexCount,
        const u32 missingVertexCount,
        const bool disconnected
    )noexcept(noexcept(PredictMeshletScoreConeWidening(DeclVal<Span<const u32>>(), state, triangleIndex, triangleVectors.areaNormal, triangleAreaNormalAt)));
    [[nodiscard]] static Expected<MeshletVertexCounts> MeshletCanFitTriangle(const MeshletDesc& meshlet, Span<const u32> localVertexRefs, const MeshletTriangleData& triangle)noexcept;
    static void UpdateBestMeshletCandidateIfBetter(
        const MeshletTrianglePrecompute& trianglePrecompute,
        Span<const u32> triangleIndices,
        const MeshletScoreState& scoreState,
        const MeshletDesc& meshlet,
        Span<const u32> localVertexRefs,
        const usize frontierOffset,
        const u32 triangleIndex,
        const bool disconnected,
        bool& found,
        MeshletFrontierCandidate& outCandidate
    )noexcept;
    static void UpdateBestMeshletCandidateFromResult(const MeshletCandidateSearchResult& candidate, bool& found, MeshletFrontierCandidate& outCandidate)noexcept;
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


inline Expected<u8> MeshletCandidateSearch::FindMeshletLocalVertex(const Span<const u32> localVertexRefs, const u32 vertexRefIndex)noexcept{
    for(usize localIndex = 0u; localIndex < localVertexRefs.size(); ++localIndex){
        if(localVertexRefs[localIndex] != vertexRefIndex)
            continue;

        return static_cast<u8>(localIndex);
    }

    return MakeUnexpected(Failure{});
}


inline MeshletVertexCounts MeshletCandidateSearch::CountMeshletMissingVertices(const Span<const u32> localVertexRefs, const MeshletTriangleData& triangle)noexcept{
    MeshletVertexCounts counts{};
    for(usize cornerIndex = 0u; cornerIndex < s_MeshletTriangleIndexCount; ++cornerIndex){
        const u32 vertexRefIndex = triangle.vertexRefs[cornerIndex];
        bool repeatedInTriangle = false;
        for(usize previousCornerIndex = 0u; previousCornerIndex < cornerIndex; ++previousCornerIndex){
            if(triangle.vertexRefs[previousCornerIndex] == vertexRefIndex){
                repeatedInTriangle = true;
                break;
            }
        }
        if(repeatedInTriangle)
            continue;

        if(FindMeshletLocalVertex(localVertexRefs, vertexRefIndex))
            ++counts.shared;
        else
            ++counts.missing;
    }

    return counts;
}


template<typename TriangleAreaNormalAtT>
MeshletConeScore MeshletCandidateSearch::ComputeMeshletScoreConeCutoff(const Span<const u32> triangleIndices, const SIMDVectorSingle axis, const u32 extraTriangleIndex, const bool hasExtraTriangle, const TriangleAreaNormalAtT& triangleAreaNormalAt)noexcept(
    noexcept(DeclVal<void(*)(SIMDVectorSingle)noexcept>()(triangleAreaNormalAt(DeclVal<const u32&>())))
){
    if(!::FrameValidDirection(axis))
        return {};

    bool hasNormal = false;
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
SIMDVectorSingle MeshletCandidateSearch::PredictMeshletScoreConeWidening(
    const Span<const u32> triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const SIMDVectorSingle triangleAreaNormal,
    const TriangleAreaNormalAtT& triangleAreaNormalAt
)noexcept(noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(VectorAdd(state.normalSum, triangleAreaNormal)), triangleIndex, true, triangleAreaNormalAt))){
    if(!state.coneEnabled)
        return VectorZero();

    const SIMDVectorSingle predictedAxis = MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(VectorAdd(state.normalSum, triangleAreaNormal));
    const auto predictedCone = ComputeMeshletScoreConeCutoff(triangleIndices, predictedAxis, triangleIndex, true, triangleAreaNormalAt);
    if(!predictedCone.enabled)
        return s_SIMDOne;

    // Widening is the positive part of (state - predicted); the difference and the zero floor both stay on SIMD lanes.
    return VectorMax(VectorSubtract(state.coneCutoff, predictedCone.cutoff), VectorZero());
}


template<typename TriangleAreaNormalAtT>
SIMDVectorSingle MeshletCandidateSearch::ScoreMeshletCandidate(
    const Span<const u32> triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    const u32 sharedVertexCount,
    const u32 missingVertexCount,
    const bool disconnected
)noexcept(noexcept(PredictMeshletScoreConeWidening(DeclVal<Span<const u32>>(), state, triangleIndex, triangleVectors.areaNormal, triangleAreaNormalAt))){
    const SIMDVectorSingle predictedRadius = PredictMeshletScoreRadius(state, triangleVectors.positions);
    const SIMDVectorSingle predictedRadiusGrowth = VectorMax(VectorSubtract(predictedRadius, state.radius), VectorZero());
    const SIMDVectorSingle centroidDistance = MeshletScoreCentroidDistance(state, triangleVectors.centroid);
    const SIMDVectorSingle normalCoherence = MeshletScoreNormalCoherence(state, triangleVectors.areaNormal);
    const SIMDVectorSingle coneWidening = PredictMeshletScoreConeWidening(triangleIndices, state, triangleIndex, triangleVectors.areaNormal, triangleAreaNormalAt);
    SIMDVectorSingle score = VectorSubtract(VectorScale(VectorReplicate(static_cast<f32>(sharedVertexCount)), s_MeshletScoreSharedVertexWeight), VectorScale(VectorReplicate(static_cast<f32>(missingVertexCount)), s_MeshletScoreNewVertexWeight));
    score = VectorNegativeMultiplySubtractExpression(VectorReplicate(s_MeshletScoreRadiusWeight), predictedRadiusGrowth, score);
    score = VectorNegativeMultiplySubtractExpression(VectorReplicate(s_MeshletScoreCentroidWeight), centroidDistance, score);
    score = VectorMultiplyAddExpression(VectorReplicate(s_MeshletScoreNormalWeight), normalCoherence, score);
    score = VectorNegativeMultiplySubtractExpression(VectorReplicate(s_MeshletScoreConePenaltyWeight), coneWidening, score);
    return VectorSubtract(score, VectorReplicate(disconnected ? s_MeshletScoreDisconnectedPenalty : 0.0f));
}


inline Expected<MeshletVertexCounts> MeshletCandidateSearch::MeshletCanFitTriangle(const MeshletDesc& meshlet, const Span<const u32> localVertexRefs, const MeshletTriangleData& triangle)noexcept{
    if(MeshletPrimitiveCount(meshlet) + 1u > s_MeshMaxMeshletTriangles)
        return MakeUnexpected(Failure{});

    const auto counts = CountMeshletMissingVertices(localVertexRefs, triangle);
    if(localVertexRefs.size() + counts.missing > s_MeshMaxMeshletVertices)
        return MakeUnexpected(Failure{});
    return counts;
}


inline void MeshletCandidateSearch::UpdateBestMeshletCandidateIfBetter(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs,
    const usize frontierOffset,
    const u32 triangleIndex,
    const bool disconnected,
    bool& found,
    MeshletFrontierCandidate& outCandidate
)noexcept{
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    const auto vertexCounts = MeshletCanFitTriangle(meshlet, localVertexRefs, triangle);
    if(!vertexCounts)
        return;

    const MeshletTriangleCalculation& triangleCalculation = trianglePrecompute.triangleCalculations[triangleIndex];
    const MeshletTriangleVectors triangleVectors = MeshletTriangleGeometry::MakeMeshletTriangleVectors(LoadFloat(triangleCalculation.positions[0u]), LoadFloat(triangleCalculation.positions[1u]), LoadFloat(triangleCalculation.positions[2u]), LoadFloat(triangleCalculation.centroid), LoadFloat(triangleCalculation.areaNormal));
    const auto triangleAreaNormalAt = [&](const u32 otherTriangleIndex)noexcept{
        return LoadFloat(trianglePrecompute.triangleCalculations[otherTriangleIndex].areaNormal);
    };
    const f32 score = VectorGetX(ScoreMeshletCandidate(triangleIndices, scoreState, triangleIndex, triangleVectors, triangleAreaNormalAt, vertexCounts->shared, vertexCounts->missing, disconnected));
    if(found && (score < outCandidate.score || (score == outCandidate.score && triangleIndex > outCandidate.triangleIndex)))
        return;

    found = true;
    outCandidate.frontierOffset = frontierOffset;
    outCandidate.triangleIndex = triangleIndex;
    outCandidate.score = score;
}


inline Expected<MeshletFrontierCandidate> MeshletCandidateSearch::FindBestMeshletFrontierCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Span<const u32> frontier,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs
)noexcept{
    bool found = false;
    MeshletFrontierCandidate candidate;
    for(usize frontierOffset = 0u; frontierOffset < frontier.size(); ++frontierOffset){
        const u32 triangleIndex = frontier[frontierOffset];
        UpdateBestMeshletCandidateIfBetter(trianglePrecompute, triangleIndices, scoreState, meshlet, localVertexRefs, frontierOffset, triangleIndex, false, found, candidate);
    }

    if(!found)
        return MakeUnexpected(Failure{});
    return candidate;
}


inline Expected<MeshletFrontierCandidate> MeshletCandidateSearch::FindBestDisconnectedMeshletCandidateRange(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchBegin,
    const usize searchEnd,
    const Span<const u32> triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const Span<const u32> localVertexRefs
)noexcept{
    bool found = false;
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


inline Expected<MeshletFrontierCandidate> MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(
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

    const usize workerCount = static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u;
    const usize maxChunkCount = workerCount * s_MeshletDisconnectedCandidateParallelOversubscription;
    const usize chunkCount = searchCount < maxChunkCount ? searchCount : maxChunkCount;
    const usize chunkSize = searchCount / chunkCount;
    const usize remainder = searchCount % chunkCount;

    parallelCandidates.clear();
    parallelCandidates.resize(chunkCount);
    const auto parallelWork = cpuScheduler.parallelFor(static_cast<usize>(0), chunkCount, [&](const usize chunkIndex)noexcept(
        noexcept(FindBestDisconnectedMeshletCandidateRange(trianglePrecompute, DeclVal<const usize&>(), DeclVal<const usize&>(), triangleIndices, scoreState, meshlet, localVertexRefs))
    ){
        const usize chunkBegin = searchOffset + chunkIndex * chunkSize + (chunkIndex < remainder ? chunkIndex : remainder);
        const usize chunkEnd = chunkBegin + chunkSize + (chunkIndex < remainder ? 1u : 0u);

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

    bool found = false;
    MeshletFrontierCandidate candidate;
    for(const MeshletCandidateSearchResult& result : parallelCandidates)
        UpdateBestMeshletCandidateFromResult(result, found, candidate);

    if(!found)
        return MakeUnexpected(Failure{});
    return candidate;
}


template<typename TriangleAreaNormalAtT>
void MeshletCandidateSearch::AddMeshletTriangleToScoreState(const Span<const u32> triangleIndices, const MeshletTriangleVectors& triangleVectors, const TriangleAreaNormalAtT& triangleAreaNormalAt, MeshletScoreState& state)noexcept(
    noexcept(ComputeMeshletScoreConeCutoff(DeclVal<Span<const u32>>(), state.normalAxis, 0u, false, triangleAreaNormalAt))
){
    AccumulateMeshletScoreBounds(triangleVectors.positions, state.minBounds, state.maxBounds);
    state.radius = Vector3Length(AabbTests::Extents(state.minBounds, state.maxBounds));
    state.centroidSum = VectorAdd(state.centroidSum, triangleVectors.centroid);
    state.normalSum = VectorAdd(state.normalSum, triangleVectors.areaNormal);
    state.normalAxis = MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(state.normalSum);
    ++state.primitiveCount;
    const auto cone = ComputeMeshletScoreConeCutoff(triangleIndices, state.normalAxis, 0u, false, triangleAreaNormalAt);
    state.coneCutoff = cone.cutoff;
    state.coneEnabled = cone.enabled;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

