// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "cook_metadata.h"
#include "meshlet_payload_packing.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/task/cpu/scheduler.h>
#include <global/math/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesh cook meshlet partition and build.


struct MeshletTriangleVectors{
    SIMDVector positions[s_MeshletTriangleIndexCount] = {};
    SIMDVector centroid = {};
    SIMDVector areaNormal = {};
};

struct MeshletTriangleData{
    u32 vertexRefs[s_MeshletTriangleIndexCount] = {};
    u32 positions[s_MeshletTriangleIndexCount] = {};
};


// CPU cook-scratch only. These values never cross an asset or GPU-memory boundary, so the meshlet scoring helpers can stay entirely on calculation types after PrecomputeMeshletTriangleData performs the source-storage loads.
struct MeshletTriangleCalculation{
    MeshletTriangleVectors vectors;
};

struct MeshletTrianglePrecompute{
    Core::Assets::AssetVector<MeshletTriangleData> triangles;
    Core::Assets::AssetVector<MeshletTriangleCalculation> triangleCalculations;
    Core::Assets::AssetVector<u32> positionTriangleOffsets;
    Core::Assets::AssetVector<u32> positionTriangleIndices;
    Core::Assets::AssetVector<u8> visitedTriangles;

    explicit MeshletTrianglePrecompute(Core::Assets::AssetArena& arena)
        : triangles(arena)
        , triangleCalculations(arena)
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


static constexpr f32 s_MeshletBoundsRadiusInflation = 1.25f;

struct MeshletBoundsCalculation{
    SIMDVector center = {};
    SIMDVector coneAxis = {};
    f32 radius = 0.0f;
    f32 coneCutoff = -1.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshCookMeshlets final : NoCopy{
public:
    [[nodiscard]] static SIMDVector MakeMeshletPositionVector(const SIMDVector position);
    [[nodiscard]] static MeshletTriangleVectors MakeMeshletTriangleVectors(
    const SIMDVector position0,
    const SIMDVector position1,
    const SIMDVector position2,
    const SIMDVector centroid,
    const SIMDVector areaNormal
    );
    [[nodiscard]] static SIMDVector NormalizeMeshletDirectionOrZero(const SIMDVector value);
    template<typename VectorT>
    [[nodiscard]] static usize MeshletCookVectorBytes(const VectorT& values);
    template<typename CookEntryT>
    [[nodiscard]] static usize EstimateCommonMeshletSourceBytes(
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry
    );
    [[nodiscard]] static usize EstimateMeshletSourceBytes(
    const Core::Assets::AssetVector<u32>& indices,
    const MeshCookEntry& entry
    );
    template<typename CookEntryT>
    [[nodiscard]] static usize EstimateCommonMeshletRuntimeBytes(const CookEntryT& entry);
    [[nodiscard]] static usize EstimateMeshletRuntimeBytes(const MeshCookEntry& entry);
    template<typename CookEntryT>
    [[nodiscard]] static MeshletCookMetrics BuildMeshletCookMetrics(const CookEntryT& entry);
    template<typename CookEntryT>
    static void LogMeshletCookMetrics(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry
    );
    template<typename CookEntryT>
    [[nodiscard]] static bool PrecomputeMeshletTriangleData(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry,
    MeshletTrianglePrecompute& outData
    );
    template<typename PositionAtT, typename VisitFaceNormalsT>
    [[nodiscard]] static MeshletBoundsCalculation CalculateMeshletBounds(
    const u32 positionCount,
    const u32 primitiveCount,
    const PositionAtT& positionAt,
    const VisitFaceNormalsT& visitFaceNormals
    );
    template<typename CookEntryT>
    static MeshletBounds BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet);
    template<typename VertexRefVectorT>
    [[nodiscard]] static bool FindMeshletLocalVertex(
    const VertexRefVectorT& localVertexRefs,
    const u32 vertexRefIndex,
    u8& outLocalVertex
    );
    template<typename VertexRefVectorT>
    [[nodiscard]] static u32 CountMeshletMissingVertices(
    const VertexRefVectorT& localVertexRefs,
    const MeshletTriangleData& triangle,
    u32& outSharedVertexCount
    );
    static void ResetMeshletScoreState(MeshletScoreState& state);
    static void AccumulateMeshletScoreBounds(
    const SIMDVector (&trianglePositions)[s_MeshletTriangleIndexCount],
    SIMDVector& minBounds,
    SIMDVector& maxBounds
    );
    [[nodiscard]] static f32 PredictMeshletScoreRadius(
    const MeshletScoreState& state,
    const SIMDVector (&trianglePositions)[s_MeshletTriangleIndexCount]
    );
    [[nodiscard]] static f32 MeshletScoreCentroidDistance(const MeshletScoreState& state, const SIMDVector triangleCentroid);
    [[nodiscard]] static f32 MeshletScoreNormalCoherence(const MeshletScoreState& state, const SIMDVector triangleAreaNormal);
    static void UpdateMeshletScoreConeCutoff(
    const SIMDVector axis,
    const SIMDVector triangleAreaNormal,
    bool& hasNormal,
    f32& coneCutoff
    );
    template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
    [[nodiscard]] static f32 ComputeMeshletScoreConeCutoff(
    const TriangleIndexVectorT& triangleIndices,
    const SIMDVector axis,
    const u32 extraTriangleIndex,
    const bool hasExtraTriangle,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    bool& outConeEnabled
    );
    template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
    [[nodiscard]] static f32 PredictMeshletScoreConeWidening(
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const SIMDVector triangleAreaNormal,
    const TriangleAreaNormalAtT& triangleAreaNormalAt
    );
    template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
    [[nodiscard]] static f32 ScoreMeshletCandidate(
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    const u32 sharedVertexCount,
    const u32 missingVertexCount,
    const bool disconnected
    );
    [[nodiscard]] static bool FindNextUnvisitedMeshletTriangle(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    u32& outTriangleIndex
    );
    template<typename VertexRefVectorT>
    [[nodiscard]] static bool MeshletCanFitTriangle(
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    const MeshletTriangleData& triangle,
    u32& outSharedVertexCount,
    u32& outMissingVertexCount
    );
    template<typename TriangleIndexVectorT, typename VertexRefVectorT>
    static void UpdateBestMeshletCandidateIfBetter(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    const usize frontierOffset,
    const u32 triangleIndex,
    const bool disconnected,
    bool& found,
    MeshletFrontierCandidate& outCandidate
    );
    static void UpdateBestMeshletCandidateFromResult(
    const MeshletCandidateSearchResult& candidate,
    bool& found,
    MeshletFrontierCandidate& outCandidate
    );
    template<typename TriangleIndexVectorT, typename VertexRefVectorT>
    [[nodiscard]] static bool FindBestMeshletFrontierCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Core::Assets::AssetVector<u32>& frontier,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    MeshletFrontierCandidate& outCandidate
    );
    template<typename TriangleIndexVectorT, typename VertexRefVectorT>
    [[nodiscard]] static bool FindBestDisconnectedMeshletCandidateRange(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchBegin,
    const usize searchEnd,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    MeshletFrontierCandidate& outCandidate
    );
    template<typename TriangleIndexVectorT, typename VertexRefVectorT>
    [[nodiscard]] static bool FindBestDisconnectedMeshletCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Assets::AssetVector<MeshletCandidateSearchResult>& parallelCandidates,
    MeshletFrontierCandidate& outCandidate
    );
    template<typename FrontierVectorT>
    static void ClearMeshletFrontier(FrontierVectorT& frontier, Core::Assets::AssetVector<u8>& frontierFlags);
    template<typename FrontierVectorT>
    static void RemoveMeshletFrontierCandidate(
    FrontierVectorT& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags,
    const usize frontierOffset
    );
    template<typename FrontierVectorT>
    static void AddMeshletTriangleNeighborsToFrontier(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    FrontierVectorT& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
    );
    template<typename VertexRefVectorT, typename PrimitiveIndexVectorT>
    [[nodiscard]] static bool AddMeshletTriangleToBuilder(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const MeshletTriangleData& triangle,
    VertexRefVectorT& localSourceVertexRefs,
    MeshletDesc& meshlet,
    PrimitiveIndexVectorT& primitiveIndices
    );
    template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
    static void AddMeshletTriangleToScoreState(
    const TriangleIndexVectorT& triangleIndices,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    MeshletScoreState& state
    );
    [[nodiscard]] static bool AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
    );
    [[nodiscard]] static bool GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const usize seedSearchOffset,
    Core::Assets::AssetVector<u32>& localSourceVertexRefs,
    Core::Assets::AssetVector<u32>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Assets::AssetVector<MeshletCandidateSearchResult>& parallelCandidates,
    Core::Assets::AssetVector<u32>& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
    );
    template<typename PositionRefVectorT>
    [[nodiscard]] static bool FindMeshletPositionRef(
    const PositionRefVectorT& refs,
    const MeshletPositionStreamRef& ref,
    u16& outLocalPosition
    );
    template<typename AttributeRefVectorT, typename AttributeSkinVectorT>
    [[nodiscard]] static bool FindMeshletAttributeRef(
    const AttributeRefVectorT& refs,
    const AttributeSkinVectorT& skins,
    const MeshletAttributeStreamRef& ref,
    const u32 skin,
    u16& outLocalAttribute
    );
    template<
    typename CookEntryT,
    typename LocalVertexVectorT,
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename AttributeSkinVectorT,
    typename LocalVertexRefVectorT
    >
    [[nodiscard]] static bool BuildZippedMeshletRefs(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const CookEntryT& entry,
    const LocalVertexVectorT& sourceVertexRefs,
    PositionRefVectorT& outPositionRefs,
    AttributeRefVectorT& outAttributeRefs,
    AttributeSkinVectorT& outAttributeSkins,
    LocalVertexRefVectorT& outLocalVertexRefs
    );
    template<typename CookEntryT>
    static bool BuildMeshlets(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    CookEntryT& entry,
    Core::CpuTaskScheduler& cpuScheduler
    );


public:
    MeshCookMeshlets() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VectorT>
usize MeshCookMeshlets::MeshletCookVectorBytes(const VectorT& values){
    return values.size() * sizeof(typename VectorT::value_type);
}


template<typename CookEntryT>
usize MeshCookMeshlets::EstimateCommonMeshletSourceBytes(
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry
){
    return MeshletCookVectorBytes(indices)
        + MeshletCookVectorBytes(entry.positions)
        + MeshletCookVectorBytes(entry.normals)
        + MeshletCookVectorBytes(entry.tangents)
        + MeshletCookVectorBytes(entry.uv0)
        + MeshletCookVectorBytes(entry.colors)
        + MeshletCookVectorBytes(entry.vertexRefs)
    ;
}


template<typename CookEntryT>
usize MeshCookMeshlets::EstimateCommonMeshletRuntimeBytes(const CookEntryT& entry){
    return MeshletCookVectorBytes(entry.positions)
        + MeshletCookVectorBytes(entry.normals)
        + MeshletCookVectorBytes(entry.tangents)
        + MeshletCookVectorBytes(entry.uv0)
        + MeshletCookVectorBytes(entry.colors)
        + MeshletCookVectorBytes(entry.meshlets)
        + MeshletCookVectorBytes(entry.meshletBounds)
        + MeshletCookVectorBytes(entry.meshletPositionStreamRefs)
        + MeshletCookVectorBytes(entry.meshletAttributeStreamRefs)
        + MeshletCookVectorBytes(entry.meshletLocalVertexRefs)
        + MeshletCookVectorBytes(entry.meshletPrimitiveIndices)
    ;
}


template<typename CookEntryT>
MeshletCookMetrics MeshCookMeshlets::BuildMeshletCookMetrics(const CookEntryT& entry){
    MeshletCookMetrics metrics;
    metrics.meshletCount = static_cast<u32>(entry.meshlets.size());
    for(usize meshletIndex = 0u; meshletIndex < entry.meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = entry.meshlets[meshletIndex];
        const MeshletBounds& bounds = entry.meshletBounds[meshletIndex];
        const u32 primitiveCount = MeshletPrimitiveCount(meshlet);
        const u32 vertexCount = MeshletVertexCount(meshlet);
        metrics.minPrimitiveCount = Min(metrics.minPrimitiveCount, primitiveCount);
        metrics.maxPrimitiveCount = Max(metrics.maxPrimitiveCount, primitiveCount);
        metrics.minVertexCount = Min(metrics.minVertexCount, vertexCount);
        metrics.maxVertexCount = Max(metrics.maxVertexCount, vertexCount);
        metrics.primitiveCountSum += primitiveCount;
        metrics.vertexCountSum += vertexCount;
        metrics.positionCountSum += MeshletPositionCount(meshlet);
        metrics.attributeCountSum += MeshletAttributeCount(meshlet);
        metrics.radiusSum += bounds.sphere.w;

        if(!MeshletConeEnabled(bounds)){
            ++metrics.coneDisabledCount;
            continue;
        }

        const f32 coneCutoff = static_cast<f32>(MeshletConePackedCutoff(bounds)) * (1.0f / s_MeshletUnorm8Max);
        ++metrics.coneEnabledCount;
        metrics.coneCutoffSum += coneCutoff;
        metrics.worstConeCutoff = Min(metrics.worstConeCutoff, coneCutoff);
    }

    if(metrics.meshletCount == 0u){
        metrics.minPrimitiveCount = 0u;
        metrics.minVertexCount = 0u;
    }

    return metrics;
}


template<typename CookEntryT>
void MeshCookMeshlets::LogMeshletCookMetrics(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry
){
    const MeshletCookMetrics metrics = BuildMeshletCookMetrics(entry);
    if(metrics.meshletCount == 0u)
        return;

    const f64 invMeshletCount = 1.0 / static_cast<f64>(metrics.meshletCount);
    const f64 primitiveCountAverage = static_cast<f64>(metrics.primitiveCountSum) * invMeshletCount;
    const f64 vertexCountAverage = static_cast<f64>(metrics.vertexCountSum) * invMeshletCount;
    const f64 positionCountAverage = static_cast<f64>(metrics.positionCountSum) * invMeshletCount;
    const f64 attributeCountAverage = static_cast<f64>(metrics.attributeCountSum) * invMeshletCount;
    const f64 radiusAverage = metrics.radiusSum * invMeshletCount;
    const f64 coneDisabledPercentage = static_cast<f64>(metrics.coneDisabledCount) * 100.0 * invMeshletCount;
    const f64 coneCutoffAverage = metrics.coneEnabledCount != 0u
        ? metrics.coneCutoffSum / static_cast<f64>(metrics.coneEnabledCount)
        : 0.0
    ;
    const f32 worstConeCutoff = metrics.coneEnabledCount != 0u ? metrics.worstConeCutoff : 0.0f;
    const usize sourceBytes = EstimateMeshletSourceBytes(indices, entry);
    const usize runtimeBytes = EstimateMeshletRuntimeBytes(entry);

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{} meta '{}': meshlet cook metrics - meshlets {}, primitives avg {:.2f} min {} max {}, local vertices avg {:.2f} min {} max {}, deformed positions avg {:.2f}, attributes avg {:.2f}, sphere radius avg {:.4f}, cones disabled {:.2f}% ({}/{}), cone cutoff avg {:.4f} worst {:.4f}, bytes source {} runtime {}")
        , metaKind.get()
        , PathToString<tchar>(nwbFilePath)
        , metrics.meshletCount
        , primitiveCountAverage
        , metrics.minPrimitiveCount
        , metrics.maxPrimitiveCount
        , vertexCountAverage
        , metrics.minVertexCount
        , metrics.maxVertexCount
        , positionCountAverage
        , attributeCountAverage
        , radiusAverage
        , coneDisabledPercentage
        , metrics.coneDisabledCount
        , metrics.meshletCount
        , coneCutoffAverage
        , worstConeCutoff
        , sourceBytes
        , runtimeBytes
    );
}


template<typename CookEntryT>
bool MeshCookMeshlets::PrecomputeMeshletTriangleData(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const CookEntryT& entry,
    MeshletTrianglePrecompute& outData
){
    outData.triangles.clear();
    outData.triangleCalculations.clear();
    outData.positionTriangleOffsets.clear();
    outData.positionTriangleIndices.clear();
    outData.visitedTriangles.clear();

    const usize triangleCount = indices.size() / s_MeshletTriangleIndexCount;
    if(triangleCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet triangle count exceeds u32 limits")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outData.triangles.resize(triangleCount);
    outData.triangleCalculations.resize(triangleCount);
    outData.positionTriangleOffsets.resize(entry.positions.size() + 1u, 0u);
    outData.positionTriangleIndices.resize(indices.size());
    outData.visitedTriangles.resize(triangleCount, 0u);

    for(const u32 vertexRefIndex : indices){
        const MeshVertexRef& vertexRef = entry.vertexRefs[vertexRefIndex];
        ++outData.positionTriangleOffsets[vertexRef.position + 1u];
    }

    for(usize positionIndex = 0u; positionIndex < entry.positions.size(); ++positionIndex)
        outData.positionTriangleOffsets[positionIndex + 1u] += outData.positionTriangleOffsets[positionIndex];

    Core::Assets::AssetVector<u32> positionTriangleCursor(entry.positions.get_allocator().arena());
    positionTriangleCursor.insert(
        positionTriangleCursor.end(),
        outData.positionTriangleOffsets.begin(),
        outData.positionTriangleOffsets.end()
    );

    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        MeshletTriangleData& triangle = outData.triangles[triangleIndex];
        MeshletTriangleCalculation& calculation = outData.triangleCalculations[triangleIndex];
        const usize indexOffset = triangleIndex * s_MeshletTriangleIndexCount;
        triangle.vertexRefs[0] = indices[indexOffset + 0u];
        triangle.vertexRefs[1] = indices[indexOffset + 1u];
        triangle.vertexRefs[2] = indices[indexOffset + 2u];
        triangle.positions[0] = entry.vertexRefs[triangle.vertexRefs[0]].position;
        triangle.positions[1] = entry.vertexRefs[triangle.vertexRefs[1]].position;
        triangle.positions[2] = entry.vertexRefs[triangle.vertexRefs[2]].position;

        const SIMDVector p0 = MakeMeshletPositionVector(LoadFloat(entry.positions[triangle.positions[0u]]));
        const SIMDVector p1 = MakeMeshletPositionVector(LoadFloat(entry.positions[triangle.positions[1u]]));
        const SIMDVector p2 = MakeMeshletPositionVector(LoadFloat(entry.positions[triangle.positions[2u]]));
        const SIMDVector centroid = VectorScale(VectorAdd(VectorAdd(p0, p1), p2), 1.0f / 3.0f);
        const SIMDVector areaNormal = TriangleTests::AreaNormal(p0, p1, p2);
        calculation.vectors = MakeMeshletTriangleVectors(
            p0,
            p1,
            p2,
            VectorSetW(centroid, 0.0f),
            VectorSetW(areaNormal, 0.0f)
        );

        const u32 triangleIndexU32 = static_cast<u32>(triangleIndex);
        for(const u32 positionIndex : triangle.positions){
            const u32 adjacencyOffset = positionTriangleCursor[positionIndex]++;
            outData.positionTriangleIndices[adjacencyOffset] = triangleIndexU32;
        }
    }

    return true;
}


template<typename PositionAtT, typename VisitFaceNormalsT>
MeshletBoundsCalculation MeshCookMeshlets::CalculateMeshletBounds(
    const u32 positionCount,
    const u32 primitiveCount,
    const PositionAtT& positionAt,
    const VisitFaceNormalsT& visitFaceNormals
){
    MeshletBoundsCalculation result;

    SIMDVector minBounds;
    SIMDVector maxBounds;
    AabbTests::Reset(minBounds, maxBounds);
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex)
        AabbTests::Expand(positionAt(localPositionIndex), minBounds, maxBounds);

    result.center = AabbTests::Center(minBounds, maxBounds);
    SIMDVector radiusSquared = VectorZero();
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex){
        const SIMDVector delta = VectorSubtract(positionAt(localPositionIndex), result.center);
        radiusSquared = VectorMax(radiusSquared, Vector3LengthSq(delta));
    }
    result.radius = VectorGetX(VectorSqrt(radiusSquared));

    SIMDVector areaWeightedNormal = VectorZero();
    u32 validFaceNormalCount = 0u;
    visitFaceNormals([&](const SIMDVector faceNormal){
        if(!::FrameValidDirection(faceNormal))
            return;

        areaWeightedNormal = VectorAdd(areaWeightedNormal, faceNormal);
        ++validFaceNormalCount;
    });

    result.coneAxis = NormalizeMeshletDirectionOrZero(areaWeightedNormal);
    if(validFaceNormalCount == primitiveCount && ::FrameValidDirection(result.coneAxis)){
        result.coneCutoff = 1.0f;
        visitFaceNormals([&](const SIMDVector meshletFaceNormal){
            const SIMDVector faceNormal = NormalizeMeshletDirectionOrZero(meshletFaceNormal);
            if(::FrameValidDirection(faceNormal))
                result.coneCutoff = Min(result.coneCutoff, VectorGetX(Vector3Dot(result.coneAxis, faceNormal)));
        });
        if(result.coneCutoff <= 0.0f)
            result.coneCutoff = -1.0f;
    }

    return result;
}


template<typename CookEntryT>
MeshletBounds MeshCookMeshlets::BuildMeshletBounds(const CookEntryT& entry, const MeshletDesc& meshlet){
    const u32 positionCount = MeshletPositionCount(meshlet);
    NWB_ASSERT(positionCount <= s_MeshMaxMeshletVertices);
    SIMDVector positions[s_MeshMaxMeshletVertices];
    for(u32 localPositionIndex = 0u; localPositionIndex < positionCount; ++localPositionIndex){
        const MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
        positions[localPositionIndex] = MakeMeshletPositionVector(LoadFloat(entry.positions[ref.position]));
    }

    const auto positionAt = [&](const u32 localPositionIndex){
        return positions[localPositionIndex];
    };
    const auto localPositionAt = [&](const u8 localVertexIndex){
        const MeshletLocalVertexRef& localVertexRef = entry.meshletLocalVertexRefs[
            meshlet.localVertexOffset + localVertexIndex
        ];
        return positions[localVertexRef.localDeformedPosition];
    };
    const auto visitFaceNormals = [&](auto&& callback){
        for(u32 primitiveIndex = 0u; primitiveIndex < MeshletPrimitiveCount(meshlet); ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset
                + static_cast<usize>(primitiveIndex) * s_MeshletTriangleIndexCount;
            const u8 localVertex0 = entry.meshletPrimitiveIndices[primitiveOffset + 0u];
            const u8 localVertex1 = entry.meshletPrimitiveIndices[primitiveOffset + 1u];
            const u8 localVertex2 = entry.meshletPrimitiveIndices[primitiveOffset + 2u];
            const SIMDVector p0 = localPositionAt(localVertex0);
            const SIMDVector p1 = localPositionAt(localVertex1);
            const SIMDVector p2 = localPositionAt(localVertex2);
            callback(TriangleTests::AreaNormal(p0, p1, p2));
        }
    };
    const MeshletBoundsCalculation calculation = CalculateMeshletBounds(
        positionCount,
        MeshletPrimitiveCount(meshlet),
        positionAt,
        visitFaceNormals
    );

    MeshletBounds bounds;
    StoreFloat(
        VectorSetW(calculation.center, calculation.radius * s_MeshletBoundsRadiusInflation),
        bounds.sphere
    );
    bounds.conePacked = PackMeshletCone(calculation.coneAxis, calculation.coneCutoff);
    return bounds;
}


template<typename VertexRefVectorT>
bool MeshCookMeshlets::FindMeshletLocalVertex(
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
u32 MeshCookMeshlets::CountMeshletMissingVertices(
    const VertexRefVectorT& localVertexRefs,
    const MeshletTriangleData& triangle,
    u32& outSharedVertexCount
){
    u32 missingVertexCount = 0u;
    outSharedVertexCount = 0u;
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

        u8 localVertex = 0u;
        if(FindMeshletLocalVertex(localVertexRefs, vertexRefIndex, localVertex))
            ++outSharedVertexCount;
        else
            ++missingVertexCount;
    }

    return missingVertexCount;
}


template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
f32 MeshCookMeshlets::ComputeMeshletScoreConeCutoff(
    const TriangleIndexVectorT& triangleIndices,
    const SIMDVector axis,
    const u32 extraTriangleIndex,
    const bool hasExtraTriangle,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    bool& outConeEnabled
){
    outConeEnabled = false;
    if(!::FrameValidDirection(axis))
        return -1.0f;

    bool hasNormal = false;
    f32 coneCutoff = 1.0f;
    for(const u32 triangleIndex : triangleIndices){
        const SIMDVector triangleAreaNormal = triangleAreaNormalAt(triangleIndex);
        UpdateMeshletScoreConeCutoff(axis, triangleAreaNormal, hasNormal, coneCutoff);
    }
    if(hasExtraTriangle){
        const SIMDVector triangleAreaNormal = triangleAreaNormalAt(extraTriangleIndex);
        UpdateMeshletScoreConeCutoff(axis, triangleAreaNormal, hasNormal, coneCutoff);
    }

    if(!hasNormal || coneCutoff <= 0.0f)
        return -1.0f;

    outConeEnabled = true;
    return coneCutoff;
}


template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
f32 MeshCookMeshlets::PredictMeshletScoreConeWidening(
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const SIMDVector triangleAreaNormal,
    const TriangleAreaNormalAtT& triangleAreaNormalAt
){
    if(!state.coneEnabled)
        return 0.0f;

    const SIMDVector predictedAxis = NormalizeMeshletDirectionOrZero(VectorAdd(state.normalSum, triangleAreaNormal));
    bool predictedConeEnabled = false;
    const f32 predictedConeCutoff = ComputeMeshletScoreConeCutoff(
        triangleIndices,
        predictedAxis,
        triangleIndex,
        true,
        triangleAreaNormalAt,
        predictedConeEnabled
    );
    if(!predictedConeEnabled)
        return 1.0f;

    return Max(0.0f, state.coneCutoff - predictedConeCutoff);
}


template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
f32 MeshCookMeshlets::ScoreMeshletCandidate(
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& state,
    const u32 triangleIndex,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    const u32 sharedVertexCount,
    const u32 missingVertexCount,
    const bool disconnected
){
    const f32 predictedRadius = PredictMeshletScoreRadius(state, triangleVectors.positions);
    const f32 predictedRadiusGrowth = Max(0.0f, predictedRadius - state.radius);
    const f32 centroidDistance = MeshletScoreCentroidDistance(state, triangleVectors.centroid);
    const f32 normalCoherence = MeshletScoreNormalCoherence(state, triangleVectors.areaNormal);
    const f32 coneWidening = PredictMeshletScoreConeWidening(
        triangleIndices,
        state,
        triangleIndex,
        triangleVectors.areaNormal,
        triangleAreaNormalAt
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


template<typename VertexRefVectorT>
bool MeshCookMeshlets::MeshletCanFitTriangle(
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    const MeshletTriangleData& triangle,
    u32& outSharedVertexCount,
    u32& outMissingVertexCount
){
    if(MeshletPrimitiveCount(meshlet) + 1u > s_MeshMaxMeshletTriangles)
        return false;

    outMissingVertexCount = CountMeshletMissingVertices(localVertexRefs, triangle, outSharedVertexCount);
    return localVertexRefs.size() + outMissingVertexCount <= s_MeshMaxMeshletVertices;
}


template<typename TriangleIndexVectorT, typename VertexRefVectorT>
void MeshCookMeshlets::UpdateBestMeshletCandidateIfBetter(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    const usize frontierOffset,
    const u32 triangleIndex,
    const bool disconnected,
    bool& found,
    MeshletFrontierCandidate& outCandidate
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    u32 sharedVertexCount = 0u;
    u32 missingVertexCount = 0u;
    if(!MeshletCanFitTriangle(meshlet, localVertexRefs, triangle, sharedVertexCount, missingVertexCount))
        return;

    const MeshletTriangleVectors& triangleVectors = trianglePrecompute.triangleCalculations[triangleIndex].vectors;
    const auto triangleAreaNormalAt = [&](const u32 otherTriangleIndex){
        return trianglePrecompute.triangleCalculations[otherTriangleIndex].vectors.areaNormal;
    };
    const f32 score = ScoreMeshletCandidate(
        triangleIndices,
        scoreState,
        triangleIndex,
        triangleVectors,
        triangleAreaNormalAt,
        sharedVertexCount,
        missingVertexCount,
        disconnected
    );
    if(found && (score < outCandidate.score || (score == outCandidate.score && triangleIndex > outCandidate.triangleIndex)))
        return;

    found = true;
    outCandidate.frontierOffset = frontierOffset;
    outCandidate.triangleIndex = triangleIndex;
    outCandidate.score = score;
}


template<typename TriangleIndexVectorT, typename VertexRefVectorT>
bool MeshCookMeshlets::FindBestMeshletFrontierCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const Core::Assets::AssetVector<u32>& frontier,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    MeshletFrontierCandidate& outCandidate
){
    bool found = false;
    for(usize frontierOffset = 0u; frontierOffset < frontier.size(); ++frontierOffset){
        const u32 triangleIndex = frontier[frontierOffset];
        UpdateBestMeshletCandidateIfBetter(
            trianglePrecompute,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            frontierOffset,
            triangleIndex,
            false,
            found,
            outCandidate
        );
    }

    return found;
}


template<typename TriangleIndexVectorT, typename VertexRefVectorT>
bool MeshCookMeshlets::FindBestDisconnectedMeshletCandidateRange(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchBegin,
    const usize searchEnd,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    MeshletFrontierCandidate& outCandidate
){
    bool found = false;
    for(usize triangleIndex = searchBegin; triangleIndex < searchEnd; ++triangleIndex){
        if(trianglePrecompute.visitedTriangles[triangleIndex] != 0u)
            continue;

        UpdateBestMeshletCandidateIfBetter(
            trianglePrecompute,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            0u,
            static_cast<u32>(triangleIndex),
            true,
            found,
            outCandidate
        );
    }

    return found;
}


template<typename TriangleIndexVectorT, typename VertexRefVectorT>
bool MeshCookMeshlets::FindBestDisconnectedMeshletCandidate(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const usize searchOffset,
    const TriangleIndexVectorT& triangleIndices,
    const MeshletScoreState& scoreState,
    const MeshletDesc& meshlet,
    const VertexRefVectorT& localVertexRefs,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Assets::AssetVector<MeshletCandidateSearchResult>& parallelCandidates,
    MeshletFrontierCandidate& outCandidate
){
    const usize triangleCount = trianglePrecompute.triangles.size();
    if(searchOffset >= triangleCount)
        return false;

    const usize searchCount = triangleCount - searchOffset;
    if(!cpuScheduler.isParallelEnabled() || searchCount < s_MeshletDisconnectedCandidateParallelThreshold){
        return FindBestDisconnectedMeshletCandidateRange(
            trianglePrecompute,
            searchOffset,
            triangleCount,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            outCandidate
        );
    }

    const usize workerCount = static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u;
    const usize maxChunkCount = workerCount * s_MeshletDisconnectedCandidateParallelOversubscription;
    const usize chunkCount = searchCount < maxChunkCount ? searchCount : maxChunkCount;
    const usize chunkSize = searchCount / chunkCount;
    const usize remainder = searchCount % chunkCount;

    parallelCandidates.clear();
    parallelCandidates.resize(chunkCount);
    cpuScheduler.parallelFor(static_cast<usize>(0), chunkCount, [&](const usize chunkIndex){
        const usize chunkBegin = searchOffset + chunkIndex * chunkSize + (chunkIndex < remainder ? chunkIndex : remainder);
        const usize chunkEnd = chunkBegin + chunkSize + (chunkIndex < remainder ? 1u : 0u);

        MeshletCandidateSearchResult result;
        result.found = FindBestDisconnectedMeshletCandidateRange(
            trianglePrecompute,
            chunkBegin,
            chunkEnd,
            triangleIndices,
            scoreState,
            meshlet,
            localVertexRefs,
            result.candidate
        );
        parallelCandidates[chunkIndex] = result;
    });

    bool found = false;
    for(const MeshletCandidateSearchResult& result : parallelCandidates)
        UpdateBestMeshletCandidateFromResult(result, found, outCandidate);

    return found;
}


template<typename FrontierVectorT>
void MeshCookMeshlets::ClearMeshletFrontier(FrontierVectorT& frontier, Core::Assets::AssetVector<u8>& frontierFlags){
    for(const u32 triangleIndex : frontier)
        frontierFlags[triangleIndex] = 0u;
    frontier.clear();
}


template<typename FrontierVectorT>
void MeshCookMeshlets::RemoveMeshletFrontierCandidate(
    FrontierVectorT& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags,
    const usize frontierOffset
){
    const u32 triangleIndex = frontier[frontierOffset];
    frontierFlags[triangleIndex] = 0u;
    frontier[frontierOffset] = frontier.back();
    frontier.pop_back();
}


template<typename FrontierVectorT>
void MeshCookMeshlets::AddMeshletTriangleNeighborsToFrontier(
    const MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    FrontierVectorT& frontier,
    Core::Assets::AssetVector<u8>& frontierFlags
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    for(const u32 positionIndex : triangle.positions){
        const u32 triangleOffsetBegin = trianglePrecompute.positionTriangleOffsets[positionIndex];
        const u32 triangleOffsetEnd = trianglePrecompute.positionTriangleOffsets[positionIndex + 1u];
        for(u32 triangleOffset = triangleOffsetBegin; triangleOffset < triangleOffsetEnd; ++triangleOffset){
            const u32 neighborTriangleIndex = trianglePrecompute.positionTriangleIndices[triangleOffset];
            if(
                neighborTriangleIndex == triangleIndex
                || trianglePrecompute.visitedTriangles[neighborTriangleIndex] != 0u
                || frontierFlags[neighborTriangleIndex] != 0u
            )
                continue;

            frontierFlags[neighborTriangleIndex] = 1u;
            frontier.push_back(neighborTriangleIndex);
        }
    }
}


template<typename VertexRefVectorT, typename PrimitiveIndexVectorT>
bool MeshCookMeshlets::AddMeshletTriangleToBuilder(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const MeshletTriangleData& triangle,
    VertexRefVectorT& localSourceVertexRefs,
    MeshletDesc& meshlet,
    PrimitiveIndexVectorT& primitiveIndices
){
    for(const u32 vertexRefIndex : triangle.vertexRefs){
        u8 localVertex = 0u;
        const bool found = FindMeshletLocalVertex(localSourceVertexRefs, vertexRefIndex, localVertex);
        if(!found){
            if(localSourceVertexRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': triangle cannot fit within one meshlet")
                    , metaKind.get()
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            localVertex = static_cast<u8>(localSourceVertexRefs.size());
            localSourceVertexRefs.push_back(vertexRefIndex);
        }
        primitiveIndices.push_back(localVertex);
    }
    meshlet.counts = PackMeshletCounts(0u, MeshletPrimitiveCount(meshlet) + 1u, 0u, 0u);
    return true;
}


template<typename TriangleIndexVectorT, typename TriangleAreaNormalAtT>
void MeshCookMeshlets::AddMeshletTriangleToScoreState(
    const TriangleIndexVectorT& triangleIndices,
    const MeshletTriangleVectors& triangleVectors,
    const TriangleAreaNormalAtT& triangleAreaNormalAt,
    MeshletScoreState& state
){
    AccumulateMeshletScoreBounds(triangleVectors.positions, state.minBounds, state.maxBounds);
    state.radius = AabbTests::Radius(state.minBounds, state.maxBounds);
    state.centroidSum = VectorAdd(state.centroidSum, triangleVectors.centroid);
    state.normalSum = VectorAdd(state.normalSum, triangleVectors.areaNormal);
    state.normalAxis = NormalizeMeshletDirectionOrZero(state.normalSum);
    ++state.primitiveCount;
    state.coneCutoff = ComputeMeshletScoreConeCutoff(
        triangleIndices,
        state.normalAxis,
        0u,
        false,
        triangleAreaNormalAt,
        state.coneEnabled
    );
}


template<typename PositionRefVectorT>
bool MeshCookMeshlets::FindMeshletPositionRef(
    const PositionRefVectorT& refs,
    const MeshletPositionStreamRef& ref,
    u16& outLocalPosition
){
    outLocalPosition = 0u;
    for(usize localIndex = 0u; localIndex < refs.size(); ++localIndex){
        if(refs[localIndex].position != ref.position || refs[localIndex].skin != ref.skin)
            continue;

        outLocalPosition = static_cast<u16>(localIndex);
        return true;
    }

    return false;
}


template<typename AttributeRefVectorT, typename AttributeSkinVectorT>
bool MeshCookMeshlets::FindMeshletAttributeRef(
    const AttributeRefVectorT& refs,
    const AttributeSkinVectorT& skins,
    const MeshletAttributeStreamRef& ref,
    const u32 skin,
    u16& outLocalAttribute
){
    outLocalAttribute = 0u;
    for(usize localIndex = 0u; localIndex < refs.size(); ++localIndex){
        const MeshletAttributeStreamRef& existing = refs[localIndex];
        if(
            existing.normal != ref.normal
            || existing.tangent != ref.tangent
            || existing.uv0 != ref.uv0
            || existing.color != ref.color
            || skins[localIndex] != skin
        )
            continue;

        outLocalAttribute = static_cast<u16>(localIndex);
        return true;
    }

    return false;
}


template<
    typename CookEntryT,
    typename LocalVertexVectorT,
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename AttributeSkinVectorT,
    typename LocalVertexRefVectorT
>
[[nodiscard]] bool MeshCookMeshlets::BuildZippedMeshletRefs(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const CookEntryT& entry,
    const LocalVertexVectorT& sourceVertexRefs,
    PositionRefVectorT& outPositionRefs,
    AttributeRefVectorT& outAttributeRefs,
    AttributeSkinVectorT& outAttributeSkins,
    LocalVertexRefVectorT& outLocalVertexRefs
){
    outPositionRefs.clear();
    outAttributeRefs.clear();
    outAttributeSkins.clear();
    outLocalVertexRefs.clear();
    outPositionRefs.reserve(sourceVertexRefs.size());
    outAttributeRefs.reserve(sourceVertexRefs.size());
    outAttributeSkins.reserve(sourceVertexRefs.size());
    outLocalVertexRefs.reserve(sourceVertexRefs.size());

    for(const u32 vertexRefIndex : sourceVertexRefs){
        const MeshVertexRef& source = entry.vertexRefs[vertexRefIndex];
        const MeshletPositionStreamRef positionRef{ source.position, source.skin };
        const MeshletAttributeStreamRef attributeRef{ source.normal, source.tangent, source.uv0, source.color };

        u16 localPosition = 0u;
        if(!FindMeshletPositionRef(outPositionRefs, positionRef, localPosition)){
            if(outPositionRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet deformed positions exceed local index limits")
                    , metaKind.get()
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            localPosition = static_cast<u16>(outPositionRefs.size());
            outPositionRefs.push_back(positionRef);
        }

        u16 localAttribute = 0u;
        if(!FindMeshletAttributeRef(outAttributeRefs, outAttributeSkins, attributeRef, source.skin, localAttribute)){
            if(outAttributeRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet attributes exceed local index limits")
                    , metaKind.get()
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            localAttribute = static_cast<u16>(outAttributeRefs.size());
            outAttributeRefs.push_back(attributeRef);
            outAttributeSkins.push_back(source.skin);
        }

        outLocalVertexRefs.push_back(MeshletLocalVertexRef{ localPosition, localAttribute });
    }

    return true;
}


template<typename CookEntryT>
bool MeshCookMeshlets::BuildMeshlets(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    CookEntryT& entry,
    Core::CpuTaskScheduler& cpuScheduler
){
    entry.meshlets.clear();
    entry.meshletBounds.clear();
    entry.meshletPositionStreamRefs.clear();
    entry.meshletAttributeStreamRefs.clear();
    entry.meshletLocalVertexRefs.clear();
    entry.meshletPrimitiveIndices.clear();
    entry.meshlets.reserve(
        (indices.size() / s_MeshletTriangleIndexCount + s_MeshMaxMeshletTriangles - 1u) / s_MeshMaxMeshletTriangles
    );
    entry.meshletBounds.reserve(entry.meshlets.capacity());
    entry.meshletPositionStreamRefs.reserve(indices.size());
    entry.meshletAttributeStreamRefs.reserve(indices.size());
    entry.meshletLocalVertexRefs.reserve(indices.size());
    entry.meshletPrimitiveIndices.reserve(indices.size());

    MeshletTrianglePrecompute trianglePrecompute(entry.positions.get_allocator().arena());
    if(!PrecomputeMeshletTriangleData(nwbFilePath, metaKind, indices, entry, trianglePrecompute))
        return false;

    MeshletDesc current;
    Core::Assets::AssetVector<u32> localSourceVertexRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletPositionStreamRef> localPositionRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletAttributeStreamRef> localAttributeRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u32> localAttributeSkins(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletLocalVertexRef> localVertexRefs(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u32> localTriangleIndices(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<MeshletCandidateSearchResult> parallelCandidates(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u32> frontier(entry.positions.get_allocator().arena());
    Core::Assets::AssetVector<u8> frontierFlags(entry.positions.get_allocator().arena());
    localSourceVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localPositionRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeSkins.reserve(s_MeshMaxMeshletVertices);
    localVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localTriangleIndices.reserve(s_MeshMaxMeshletTriangles);
    frontier.reserve(s_MeshMaxMeshletTriangles * s_MeshletTriangleIndexCount);
    frontierFlags.resize(trianglePrecompute.triangles.size(), 0u);

    MeshletScoreState scoreState;

    auto resetCurrentMeshlet = [&](){
        current = MeshletDesc{};
        current.primitiveOffset = static_cast<u32>(entry.meshletPrimitiveIndices.size());
        localSourceVertexRefs.clear();
        localTriangleIndices.clear();
        ResetMeshletScoreState(scoreState);
    };

    auto flushMeshlet = [&]() -> bool{
        const u32 primitiveCount = MeshletPrimitiveCount(current);
        if(primitiveCount == 0u)
            return true;

        if(!BuildZippedMeshletRefs(
            nwbFilePath,
            metaKind,
            entry,
            localSourceVertexRefs,
            localPositionRefs,
            localAttributeRefs,
            localAttributeSkins,
            localVertexRefs
        ))
            return false;

        current.localVertexOffset = static_cast<u32>(entry.meshletLocalVertexRefs.size());
        current.positionRefOffset = static_cast<u32>(entry.meshletPositionStreamRefs.size());
        current.attributeRefOffset = static_cast<u32>(entry.meshletAttributeStreamRefs.size());
        current.counts = PackMeshletCounts(
            static_cast<u32>(localVertexRefs.size()),
            primitiveCount,
            static_cast<u32>(localPositionRefs.size()),
            static_cast<u32>(localAttributeRefs.size())
        );

        entry.meshletPositionStreamRefs.insert(
            entry.meshletPositionStreamRefs.end(),
            localPositionRefs.begin(),
            localPositionRefs.end()
        );
        entry.meshletAttributeStreamRefs.insert(
            entry.meshletAttributeStreamRefs.end(),
            localAttributeRefs.begin(),
            localAttributeRefs.end()
        );
        entry.meshletLocalVertexRefs.insert(
            entry.meshletLocalVertexRefs.end(),
            localVertexRefs.begin(),
            localVertexRefs.end()
        );
        entry.meshlets.push_back(current);
        entry.meshletBounds.push_back(BuildMeshletBounds(entry, current));

        resetCurrentMeshlet();
        return true;
    };

    u32 seedTriangleIndex = 0u;
    usize seedSearchOffset = 0u;
    while(FindNextUnvisitedMeshletTriangle(trianglePrecompute, seedSearchOffset, seedTriangleIndex)){
        seedSearchOffset = seedTriangleIndex;
        ClearMeshletFrontier(frontier, frontierFlags);
        resetCurrentMeshlet();

        if(!AddVisitedMeshletTriangle(
            nwbFilePath,
            metaKind,
            trianglePrecompute,
            seedTriangleIndex,
            localSourceVertexRefs,
            localTriangleIndices,
            current,
            entry.meshletPrimitiveIndices,
            scoreState,
            frontier,
            frontierFlags
        ))
            return false;

        if(!GrowMeshletFromFrontier(
            nwbFilePath,
            metaKind,
            trianglePrecompute,
            seedSearchOffset,
            localSourceVertexRefs,
            localTriangleIndices,
            current,
            entry.meshletPrimitiveIndices,
            scoreState,
            cpuScheduler,
            parallelCandidates,
            frontier,
            frontierFlags
        ))
            return false;

        ClearMeshletFrontier(frontier, frontierFlags);
        if(!flushMeshlet())
            return false;
    }

    if(entry.meshlets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet build produced no meshlets")
            , metaKind.get()
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    LogMeshletCookMetrics(nwbFilePath, metaKind, indices, entry);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

