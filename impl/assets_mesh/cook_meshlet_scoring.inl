// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VertexRefVectorT>
[[nodiscard]] static bool FindMeshletLocalVertex(
    const VertexRefVectorT& localVertexRefs,
    const u32 vertexRefIndex,
    u8& outLocalVertex
){
    outLocalVertex = 0u;
    for(usize localIndex = 0u; localIndex < localVertexRefs.size(); ++localIndex){
        if(localVertexRefs[localIndex] != vertexRefIndex)
            continue;

        outLocalVertex = static_cast<u8>(localIndex);
        return true;
    }

    return false;
}

template<typename VertexRefVectorT>
[[nodiscard]] static u32 CountMeshletMissingVertices(
    const VertexRefVectorT& localVertexRefs,
    const MeshletTriangleData& triangle,
    u32& outSharedVertexCount
){
    u32 missingVertexCount = 0u;
    outSharedVertexCount = 0u;
    for(usize cornerIndex = 0u; cornerIndex < 3u; ++cornerIndex){
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

        u8 localVertex = 0u;
        if(FindMeshletLocalVertex(localVertexRefs, vertexRefIndex, localVertex))
            ++outSharedVertexCount;
        else
            ++missingVertexCount;
    }

    return missingVertexCount;
}

static void ResetMeshletScoreState(MeshletScoreState& state){
    AabbTests::Reset(state.minBounds, state.maxBounds);
    state.centroidSum = VectorZero();
    state.normalSum = VectorZero();
    state.normalAxis = VectorZero();
    state.primitiveCount = 0u;
    state.radius = 0.0f;
    state.coneCutoff = -1.0f;
    state.coneEnabled = false;
}

static void AccumulateMeshletScoreBounds(
    const SIMDVector (&trianglePositions)[3],
    SIMDVector& minBounds,
    SIMDVector& maxBounds
){
    AabbTests::ExpandTriangle(trianglePositions[0u], trianglePositions[1u], trianglePositions[2u], minBounds, maxBounds);
}

[[nodiscard]] static f32 PredictMeshletScoreRadius(
    const MeshletScoreState& state,
    const SIMDVector (&trianglePositions)[3]
){
    SIMDVector minBounds = state.minBounds;
    SIMDVector maxBounds = state.maxBounds;
    AccumulateMeshletScoreBounds(trianglePositions, minBounds, maxBounds);

    return AabbTests::Radius(minBounds, maxBounds);
}

[[nodiscard]] static f32 MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVector triangleCentroid){
    if(state.primitiveCount == 0u)
        return 0.0f;

    const SIMDVector meshletCentroid = VectorScale(state.centroidSum, 1.0f / static_cast<f32>(state.primitiveCount));
    return VectorGetX(Vector3Length(VectorSubtract(triangleCentroid, meshletCentroid)));
}

[[nodiscard]] static f32 MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVector triangleAreaNormal){
    const SIMDVector candidateNormal = NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!Core::Mesh::FrameValidDirection(state.normalAxis) || !Core::Mesh::FrameValidDirection(candidateNormal))
        return 0.0f;

    return VectorGetX(Vector3Dot(state.normalAxis, candidateNormal));
}

static void UpdateMeshletScoreConeCutoff(
    const SIMDVector axis,
    const SIMDVector triangleAreaNormal,
    bool& hasNormal,
    f32& coneCutoff
){
    const SIMDVector faceNormal = NormalizeMeshletDirectionOrZero(triangleAreaNormal);
    if(!Core::Mesh::FrameValidDirection(faceNormal))
        return;

    hasNormal = true;
    coneCutoff = Min(coneCutoff, VectorGetX(Vector3Dot(axis, faceNormal)));
}

template<typename TriangleIndexVectorT>
[[nodiscard]] static f32 ComputeMeshletScoreConeCutoff(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const TriangleIndexVectorT& triangleIndices,
    const SIMDVector axis,
    const u32 extraTriangleIndex,
    const bool hasExtraTriangle,
    bool& outConeEnabled
){
    outConeEnabled = false;
    if(!Core::Mesh::FrameValidDirection(axis))
        return -1.0f;

    bool hasNormal = false;
    f32 coneCutoff = 1.0f;
    for(const u32 triangleIndex : triangleIndices){
        const SIMDVector triangleAreaNormal = LoadMeshletTriangleAreaNormal(trianglePrecompute.triangles[triangleIndex]);
        UpdateMeshletScoreConeCutoff(axis, triangleAreaNormal, hasNormal, coneCutoff);
    }
    if(hasExtraTriangle){
        const SIMDVector triangleAreaNormal = LoadMeshletTriangleAreaNormal(trianglePrecompute.triangles[extraTriangleIndex]);
        UpdateMeshletScoreConeCutoff(axis, triangleAreaNormal, hasNormal, coneCutoff);
    }

    if(!hasNormal || coneCutoff <= 0.0f)
        return -1.0f;

    outConeEnabled = true;
    return coneCutoff;
}

template<typename TriangleIndexVectorT>
[[nodiscard]] static f32 PredictMeshletScoreConeWidening(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const SIMDVector triangleAreaNormal
){
    if(!state.coneEnabled)
        return 0.0f;

    const SIMDVector predictedAxis = NormalizeMeshletDirectionOrZero(VectorAdd(state.normalSum, triangleAreaNormal));
    bool predictedConeEnabled = false;
    const f32 predictedConeCutoff = ComputeMeshletScoreConeCutoff(
        trianglePrecompute,
        triangleIndices,
        predictedAxis,
        triangleIndex,
        true,
        predictedConeEnabled
    );
    if(!predictedConeEnabled)
        return 1.0f;

    return Max(0.0f, state.coneCutoff - predictedConeCutoff);
}

template<typename TriangleIndexVectorT>
[[nodiscard]] static f32 ScoreMeshletCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const u32 sharedVertexCount,
    const u32 missingVertexCount,
    const bool disconnected
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    const MeshletTriangleVectors triangleVectors = LoadMeshletTriangleVectors(triangle);
    const f32 predictedRadius = PredictMeshletScoreRadius(state, triangleVectors.positions);
    const f32 predictedRadiusGrowth = Max(0.0f, predictedRadius - state.radius);
    const f32 centroidDistance = MeshletScoreCentroidDistance(state, triangleVectors.centroid);
    const f32 normalCoherence = MeshletScoreNormalCoherence(state, triangleVectors.areaNormal);
    const f32 coneWidening = PredictMeshletScoreConeWidening(
        trianglePrecompute,
        triangleIndices,
        state,
        triangleIndex,
        triangleVectors.areaNormal
    );
    return s_MeshletScoreSharedVertexWeight * static_cast<f32>(sharedVertexCount)
        - s_MeshletScoreNewVertexWeight * static_cast<f32>(missingVertexCount)
        - s_MeshletScoreRadiusWeight * predictedRadiusGrowth
        - s_MeshletScoreCentroidWeight * centroidDistance
        + s_MeshletScoreNormalWeight * normalCoherence
        - s_MeshletScoreConePenaltyWeight * coneWidening
        - (disconnected ? s_MeshletScoreDisconnectedPenalty : 0.0f)
    ;
}

[[nodiscard]] static bool FindNextUnvisitedMeshletTriangle(
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

