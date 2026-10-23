// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"
#include "cook_meshlet_bounds.h"
#include "cook_meshlet_candidates.h"
#include "cook_meshlet_metrics.h"
#include "cook_meshlet_refs.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Greedy meshlet growth: seed an unvisited triangle, grow along its frontier, then flush bounds and zipped refs.
class MeshCookMeshlets final : NoCopy{
public:
    template<typename FrontierVectorT>
    static void ClearMeshletFrontier(FrontierVectorT& frontier, Vector<u8, Core::Alloc::ScratchArena>& frontierFlags)noexcept(MeshletCookExceptionDetail::FrontierIndexIterationIsNothrow<FrontierVectorT>() && noexcept(frontier.clear()));
    template<typename FrontierVectorT>
    static void RemoveMeshletFrontierCandidate(FrontierVectorT& frontier, Vector<u8, Core::Alloc::ScratchArena>& frontierFlags, const usize frontierOffset)noexcept(
        noexcept(DeclVal<void(*)(u32)noexcept>()(frontier[frontierOffset]))
        && noexcept(frontier[frontierOffset] = frontier.back())
        && noexcept(frontier.pop_back())
    );
    template<typename FrontierVectorT>
    static void AddMeshletTriangleNeighborsToFrontier(const MeshletTrianglePrecompute& trianglePrecompute, const u32 triangleIndex, FrontierVectorT& frontier, Vector<u8, Core::Alloc::ScratchArena>& frontierFlags)noexcept(noexcept(frontier.push_back(DeclVal<const u32&>())));
    template<typename VertexRefVectorT, typename PrimitiveIndexVectorT>
    [[nodiscard]] static bool AddMeshletTriangleToBuilder(const Path& nwbFilePath, const TStringView metaKind, const MeshletTriangleData& triangle, VertexRefVectorT& localSourceVertexRefs, MeshletDesc& meshlet, PrimitiveIndexVectorT& primitiveIndices);
    [[nodiscard]] static bool AddVisitedMeshletTriangle(
        const Path& nwbFilePath,
        const TStringView metaKind,
        const u32 triangleIndex,
        MeshletTrianglePrecompute& trianglePrecompute,
        Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
        Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
        MeshletDesc& meshlet,
        Core::Assets::AssetVector<u8>& primitiveIndices,
        MeshletScoreState& scoreState,
        Vector<u32, Core::Alloc::ScratchArena>& frontier,
        Vector<u8, Core::Alloc::ScratchArena>& frontierFlags
    );
    [[nodiscard]] static bool GrowMeshletFromFrontier(
        const Path& nwbFilePath,
        const TStringView metaKind,
        const usize seedSearchOffset,
        Core::CpuTaskScheduler& cpuScheduler,
        MeshletTrianglePrecompute& trianglePrecompute,
        Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
        Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
        MeshletDesc& meshlet,
        Core::Assets::AssetVector<u8>& primitiveIndices,
        MeshletScoreState& scoreState,
        Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
        Vector<u32, Core::Alloc::ScratchArena>& frontier,
        Vector<u8, Core::Alloc::ScratchArena>& frontierFlags
    );
    template<typename CookEntryT>
    static bool BuildMeshlets(const Path& nwbFilePath, const TStringView metaKind, const Core::Assets::AssetVector<u32>& indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, CookEntryT& entry);


public:
    MeshCookMeshlets() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FrontierVectorT>
void MeshCookMeshlets::ClearMeshletFrontier(FrontierVectorT& frontier, Vector<u8, Core::Alloc::ScratchArena>& frontierFlags)noexcept(MeshletCookExceptionDetail::FrontierIndexIterationIsNothrow<FrontierVectorT>() && noexcept(frontier.clear())){
    for(const u32 triangleIndex : frontier)
        frontierFlags[triangleIndex] = 0u;
    frontier.clear();
}


template<typename FrontierVectorT>
void MeshCookMeshlets::RemoveMeshletFrontierCandidate(FrontierVectorT& frontier, Vector<u8, Core::Alloc::ScratchArena>& frontierFlags, const usize frontierOffset)noexcept(
    noexcept(DeclVal<void(*)(u32)noexcept>()(frontier[frontierOffset]))
    && noexcept(frontier[frontierOffset] = frontier.back())
    && noexcept(frontier.pop_back())
){
    const u32 triangleIndex = frontier[frontierOffset];
    frontierFlags[triangleIndex] = 0u;
    frontier[frontierOffset] = frontier.back();
    frontier.pop_back();
}


template<typename FrontierVectorT>
void MeshCookMeshlets::AddMeshletTriangleNeighborsToFrontier(const MeshletTrianglePrecompute& trianglePrecompute, const u32 triangleIndex, FrontierVectorT& frontier, Vector<u8, Core::Alloc::ScratchArena>& frontierFlags)noexcept(noexcept(frontier.push_back(DeclVal<const u32&>()))){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    for(const u32 positionIndex : triangle.positions){
        const u32 triangleOffsetBegin = trianglePrecompute.positionTriangleOffsets[positionIndex];
        const u32 triangleOffsetEnd = trianglePrecompute.positionTriangleOffsets[positionIndex + 1u];
        for(u32 triangleOffset = triangleOffsetBegin; triangleOffset < triangleOffsetEnd; ++triangleOffset){
            const u32 neighborTriangleIndex = trianglePrecompute.positionTriangleIndices[triangleOffset];
            if(neighborTriangleIndex == triangleIndex || trianglePrecompute.visitedTriangles[neighborTriangleIndex] != 0u || frontierFlags[neighborTriangleIndex] != 0u)
                continue;

            frontierFlags[neighborTriangleIndex] = 1u;
            frontier.push_back(neighborTriangleIndex);
        }
    }
}


template<typename VertexRefVectorT, typename PrimitiveIndexVectorT>
bool MeshCookMeshlets::AddMeshletTriangleToBuilder(const Path& nwbFilePath, const TStringView metaKind, const MeshletTriangleData& triangle, VertexRefVectorT& localSourceVertexRefs, MeshletDesc& meshlet, PrimitiveIndexVectorT& primitiveIndices){
    for(const u32 vertexRefIndex : triangle.vertexRefs){
        const auto found = MeshletCandidateSearch::FindMeshletLocalVertex(localSourceVertexRefs, vertexRefIndex);
        u8 localVertex = found ? *found : 0u;
        if(!found){
            if(localSourceVertexRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': triangle cannot fit within one meshlet"), metaKind, PathToString<tchar>(nwbFilePath));
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


template<typename CookEntryT>
bool MeshCookMeshlets::BuildMeshlets(const Path& nwbFilePath, const TStringView metaKind, const Core::Assets::AssetVector<u32>& indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, CookEntryT& entry){
    entry.meshlets.clear();
    entry.meshletBounds.clear();
    entry.meshletPositionStreamRefs.clear();
    entry.meshletAttributeStreamRefs.clear();
    entry.meshletLocalVertexRefs.clear();
    entry.meshletPrimitiveIndices.clear();
    entry.meshlets.reserve((indices.size() / s_MeshletTriangleIndexCount + s_MeshMaxMeshletTriangles - 1u) / s_MeshMaxMeshletTriangles);
    entry.meshletBounds.reserve(entry.meshlets.capacity());
    entry.meshletPositionStreamRefs.reserve(indices.size());
    entry.meshletAttributeStreamRefs.reserve(indices.size());
    entry.meshletLocalVertexRefs.reserve(indices.size());
    entry.meshletPrimitiveIndices.reserve(indices.size());

    auto precomputed = MeshletTriangleGeometry::PrecomputeMeshletTriangleData(nwbFilePath, metaKind, indices, entry, scratchArena);
    if(!precomputed)
        return false;
    MeshletTrianglePrecompute& trianglePrecompute = *precomputed;

    MeshletDesc current;
    Vector<u32, Core::Alloc::ScratchArena> localSourceVertexRefs(scratchArena);
    Vector<MeshletPositionStreamRef, Core::Alloc::ScratchArena> localPositionRefs(scratchArena);
    Vector<MeshletAttributeStreamRef, Core::Alloc::ScratchArena> localAttributeRefs(scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> localAttributeSkins(scratchArena);
    Vector<MeshletLocalVertexRef, Core::Alloc::ScratchArena> localVertexRefs(scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> localTriangleIndices(scratchArena);
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena> parallelCandidates(scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> frontier(scratchArena);
    Vector<u8, Core::Alloc::ScratchArena> frontierFlags(scratchArena);
    localSourceVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localPositionRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeRefs.reserve(s_MeshMaxMeshletVertices);
    localAttributeSkins.reserve(s_MeshMaxMeshletVertices);
    localVertexRefs.reserve(s_MeshMaxMeshletVertices);
    localTriangleIndices.reserve(s_MeshMaxMeshletTriangles);
    parallelCandidates.reserve((static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u) * s_MeshletDisconnectedCandidateParallelOversubscription);
    frontier.reserve(trianglePrecompute.triangles.size());
    frontierFlags.resize(trianglePrecompute.triangles.size(), 0u);

    MeshletScoreState scoreState;

    auto resetCurrentMeshlet = [&]()noexcept(noexcept(static_cast<u32>(entry.meshletPrimitiveIndices.size()))){
        current = MeshletDesc{};
        current.primitiveOffset = static_cast<u32>(entry.meshletPrimitiveIndices.size());
        localSourceVertexRefs.clear();
        localTriangleIndices.clear();
        MeshletCandidateSearch::ResetMeshletScoreState(scoreState);
    };

    auto flushMeshlet = [&]() -> bool{
        const u32 primitiveCount = MeshletPrimitiveCount(current);
        if(primitiveCount == 0u)
            return true;

        if(!MeshletZippedRefBuilder::BuildZippedMeshletRefs(nwbFilePath, metaKind, entry, localSourceVertexRefs, localPositionRefs, localAttributeRefs, localAttributeSkins, localVertexRefs))
            return false;

        current.localVertexOffset = static_cast<u32>(entry.meshletLocalVertexRefs.size());
        current.positionRefOffset = static_cast<u32>(entry.meshletPositionStreamRefs.size());
        current.attributeRefOffset = static_cast<u32>(entry.meshletAttributeStreamRefs.size());
        current.counts = PackMeshletCounts(static_cast<u32>(localVertexRefs.size()), primitiveCount, static_cast<u32>(localPositionRefs.size()), static_cast<u32>(localAttributeRefs.size()));

        entry.meshletPositionStreamRefs.insert(entry.meshletPositionStreamRefs.end(), localPositionRefs.begin(), localPositionRefs.end());
        entry.meshletAttributeStreamRefs.insert(entry.meshletAttributeStreamRefs.end(), localAttributeRefs.begin(), localAttributeRefs.end());
        entry.meshletLocalVertexRefs.insert(entry.meshletLocalVertexRefs.end(), localVertexRefs.begin(), localVertexRefs.end());
        entry.meshlets.push_back(current);
        entry.meshletBounds.push_back(MeshletBoundsBuilder::BuildMeshletBounds(entry, current));

        resetCurrentMeshlet();
        return true;
    };

    usize seedSearchOffset = 0u;
    while(const auto seedTriangleIndex = MeshletTriangleGeometry::FindNextUnvisitedMeshletTriangle(trianglePrecompute, seedSearchOffset)){
        seedSearchOffset = *seedTriangleIndex;
        ClearMeshletFrontier(frontier, frontierFlags);
        resetCurrentMeshlet();

        if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, *seedTriangleIndex, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, current, entry.meshletPrimitiveIndices, scoreState, frontier, frontierFlags))
            return false;

        if(!GrowMeshletFromFrontier(nwbFilePath, metaKind, seedSearchOffset, cpuScheduler, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, current, entry.meshletPrimitiveIndices, scoreState, parallelCandidates, frontier, frontierFlags))
            return false;

        ClearMeshletFrontier(frontier, frontierFlags);
        if(!flushMeshlet())
            return false;
    }

    if(entry.meshlets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet build produced no meshlets"), metaKind, PathToString<tchar>(nwbFilePath));
        return false;
    }

    MeshletCookMetricsReport::LogMeshletCookMetrics(nwbFilePath, metaKind, indices, entry);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

