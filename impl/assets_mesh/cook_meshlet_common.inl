// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
[[nodiscard]] static const Float3U& MeshletPositionStreamValue(
    const CookEntryT& entry,
    const MeshletPositionStreamRef& ref
){
    return entry.positions[ref.position];
}

template<typename CookEntryT>
[[nodiscard]] static const Float3U& MeshletLocalPositionStreamValue(
    const CookEntryT& entry,
    const MeshletDesc& meshlet,
    const u32 localVertexIndex
){
    const MeshletLocalVertexRef& localVertexRef = entry.meshletLocalVertexRefs[meshlet.localVertexOffset + localVertexIndex];
    const usize positionRefIndex = meshlet.positionRefOffset + localVertexRef.localDeformedPosition;
    const MeshletPositionStreamRef& positionRef = entry.meshletPositionStreamRefs[positionRefIndex];
    return MeshletPositionStreamValue(entry, positionRef);
}

template<typename CookEntryT>
[[nodiscard]] static const Float3U& MeshletSourceVertexPositionStreamValue(
    const CookEntryT& entry,
    const u32 vertexRefIndex
){
    const MeshVertexRef& vertexRef = entry.vertexRefs[vertexRefIndex];
    return entry.positions[vertexRef.position];
}

[[nodiscard]] static SIMDVector BuildMeshletFaceNormal(
    const SIMDVector p0,
    const SIMDVector p1,
    const SIMDVector p2
){
    return Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0));
}

struct MeshletTriangleData{
    u32 vertexRefs[3] = {};
    Float4U centroid;
    Float4U areaNormal;
};

struct MeshletTrianglePrecompute{
    Core::Assets::AssetVector<MeshletTriangleData> triangles;
    Core::Assets::AssetVector<u32> vertexTriangleOffsets;
    Core::Assets::AssetVector<u32> vertexTriangleIndices;
    Core::Assets::AssetVector<u8> visitedTriangles;

    explicit MeshletTrianglePrecompute(Core::Assets::AssetArena& arena)
        : triangles(arena)
        , vertexTriangleOffsets(arena)
        , vertexTriangleIndices(arena)
        , visitedTriangles(arena)
    {}
};

struct MeshletFrontierCandidate{
    usize frontierOffset = 0u;
    u32 triangleIndex = 0u;
    f32 score = 0.0f;
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
    bool hasGeometry = false;
    bool coneEnabled = false;
};

struct MeshletCookMetrics{
    u32 meshletCount = 0u;
    u32 minPrimitiveCount = Limit<u32>::s_Max;
    u32 maxPrimitiveCount = 0u;
    u32 minVertexCount = Limit<u32>::s_Max;
    u32 maxVertexCount = 0u;
    u64 primitiveCountSum = 0u;
    u64 vertexCountSum = 0u;
    u64 positionCountSum = 0u;
    u64 attributeCountSum = 0u;
    f64 radiusSum = 0.0;
    u32 coneDisabledCount = 0u;
    u32 coneEnabledCount = 0u;
    f64 coneCutoffSum = 0.0;
    f32 worstConeCutoff = 1.0f;
};

static constexpr f32 s_MeshletScoreSharedVertexWeight = 20.0f;
static constexpr f32 s_MeshletScoreNewVertexWeight = 8.0f;
static constexpr f32 s_MeshletScoreRadiusWeight = 4.0f;
static constexpr f32 s_MeshletScoreCentroidWeight = 1.0f;
static constexpr f32 s_MeshletScoreNormalWeight = 10.0f;
static constexpr f32 s_MeshletScoreConePenaltyWeight = 12.0f;
static constexpr f32 s_MeshletScoreDisconnectedPenalty = 24.0f;

[[nodiscard]] static SIMDVector NormalizeMeshletDirectionOrZero(const SIMDVector value){
    if(!Core::Mesh::FrameValidDirection(value))
        return VectorZero();

    return VectorSetW(Vector3Normalize(value), 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

