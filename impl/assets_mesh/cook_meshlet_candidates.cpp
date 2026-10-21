// limztudio@gmail.com
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


void MeshletCandidateSearch::AccumulateMeshletScoreBounds(const SIMDVectorSingle (&trianglePositions)[s_MeshletTriangleIndexCount], SIMDVectorSingle& minBounds, SIMDVectorSingle& maxBounds)noexcept{
    AabbTests::ExpandTriangle(trianglePositions[0u], trianglePositions[1u], trianglePositions[2u], minBounds, maxBounds);
}


SIMDVectorSingle MeshletCandidateSearch::PredictMeshletScoreRadius(const MeshletScoreState& state, const SIMDVectorSingle (&trianglePositions)[s_MeshletTriangleIndexCount])noexcept{
    SIMDVectorSingle minBounds = state.minBounds;
    SIMDVectorSingle maxBounds = state.maxBounds;
    AccumulateMeshletScoreBounds(trianglePositions, minBounds, maxBounds);

    return Vector3Length(AabbTests::Extents(minBounds, maxBounds));
}


SIMDVectorSingle MeshletCandidateSearch::MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVectorSingle triangleCentroid)noexcept{
    if(state.primitiveCount == 0u)
        return VectorZero();

    const SIMDVectorSingle meshletCentroid = VectorDivide(state.centroidSum, VectorReplicate(static_cast<f32>(state.primitiveCount)));
    return Vector3Length(VectorSubtract(triangleCentroid, meshletCentroid));
}


SIMDVectorSingle MeshletCandidateSearch::MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVectorSingle triangleAreaNormal)noexcept{
    const SIMDVectorSingle candidateNormal = MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!::FrameValidDirection(state.normalAxis) || !::FrameValidDirection(candidateNormal))
        return VectorZero();

    return Vector3Dot(state.normalAxis, candidateNormal);
}


void MeshletCandidateSearch::UpdateMeshletScoreConeCutoff(const SIMDVectorSingle axis, const SIMDVectorSingle triangleAreaNormal, bool& hasNormal, SIMDVectorSingle& coneCutoff)noexcept{
    const SIMDVectorSingle faceNormal = MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!::FrameValidDirection(faceNormal))
        return;

    hasNormal = true;
    // Cutoff tracks the minimum dot over faces; both candidates stay replicated on SIMD lanes.
    coneCutoff = VectorMin(coneCutoff, Vector3Dot(axis, faceNormal));
}


void MeshletCandidateSearch::UpdateBestMeshletCandidateFromResult(const MeshletCandidateSearchResult& candidate, bool& found, MeshletFrontierCandidate& outCandidate)noexcept{
    if(!candidate.found)
        return;

    if(found && (candidate.candidate.score < outCandidate.score || (candidate.candidate.score == outCandidate.score && candidate.candidate.triangleIndex > outCandidate.triangleIndex)))
        return;

    found = true;
    outCandidate = candidate.candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

