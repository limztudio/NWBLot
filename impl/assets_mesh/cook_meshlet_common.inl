// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDVector MakeMeshletPositionVector(const SIMDVector position){
    return VectorSetW(position, 0.0f);
}

struct MeshletTriangleVectors{
    SIMDVector positions[s_MeshletTriangleIndexCount] = {};
    SIMDVector centroid = {};
    SIMDVector areaNormal = {};
};

struct MeshletTriangleData{
    u32 vertexRefs[s_MeshletTriangleIndexCount] = {};
    u32 positions[s_MeshletTriangleIndexCount] = {};
    Float4 positionVectors[s_MeshletTriangleIndexCount] = {};
    Float4 centroid = {};
    Float4 areaNormal = {};
};

[[nodiscard]] static MeshletTriangleVectors MakeMeshletTriangleVectors(
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

struct MeshletTrianglePrecompute{
    Core::Assets::AssetVector<MeshletTriangleData> triangles;
    Core::Assets::AssetVector<u32> positionTriangleOffsets;
    Core::Assets::AssetVector<u32> positionTriangleIndices;
    Core::Assets::AssetVector<u8> visitedTriangles;

    explicit MeshletTrianglePrecompute(Core::Assets::AssetArena& arena)
        : triangles(arena)
        , positionTriangleOffsets(arena)
        , positionTriangleIndices(arena)
        , visitedTriangles(arena)
    {}
};

struct MeshletFrontierCandidate{
    usize frontierOffset = 0u;
    u32 triangleIndex = 0u;
    f32 score = 0.0f;
};

struct MeshletCandidateSearchResult{
    MeshletFrontierCandidate candidate;
    bool found = false;
};

struct MeshletScoreState{
    SIMDVector minBounds;
    SIMDVector maxBounds;
    SIMDVector centroidSum;
    SIMDVector normalSum;
    SIMDVector normalAxis;
    u32 primitiveCount = 0u;
    f32 radius = 0.0f;
    f32 coneCutoff = -1.0f;
    bool coneEnabled = false;
};

struct MeshletCookMetrics{
    u32 meshletCount = 0u;
    u32 minPrimitiveCount = Limit<u32>::s_Max;
    u32 maxPrimitiveCount = 0u;
    u32 minVertexCount = Limit<u32>::s_Max;
    u32 maxVertexCount = 0u;
    f32 worstConeCutoff = 1.0f;
    u64 primitiveCountSum = 0u;
    u64 vertexCountSum = 0u;
    u64 positionCountSum = 0u;
    u64 attributeCountSum = 0u;
    f64 radiusSum = 0.0;
    u32 coneDisabledCount = 0u;
    u32 coneEnabledCount = 0u;
    f64 coneCutoffSum = 0.0;
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

[[nodiscard]] static SIMDVector NormalizeMeshletDirectionOrZero(const SIMDVector value){
    return Vector3NormalizeOr(value, VectorZero(), ::s_FrameDirectionEpsilon);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

