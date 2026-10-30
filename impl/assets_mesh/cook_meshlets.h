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
    static void ClearMeshletFrontier(Vector<u32, Core::Alloc::ScratchArena>& frontier, Span<u8> frontierFlags)noexcept;
    static void RemoveMeshletFrontierCandidate(Vector<u32, Core::Alloc::ScratchArena>& frontier, Span<u8> frontierFlags, const usize frontierOffset)noexcept;
    static void AddMeshletTriangleNeighborsToFrontier(const MeshletTrianglePrecompute& trianglePrecompute, const u32 triangleIndex, Vector<u32, Core::Alloc::ScratchArena>& frontier, Span<u8> frontierFlags);
    [[nodiscard]] static b8 AddMeshletTriangleToBuilder(const Path& nwbFilePath, const TStringView metaKind, const MeshletTriangleData& triangle, Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs, MeshletDesc& meshlet, Core::Assets::AssetVector<u8>& primitiveIndices);
    [[nodiscard]] static b8 AddVisitedMeshletTriangle(
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
        Span<u8> frontierFlags
    );
    [[nodiscard]] static b8 GrowMeshletFromFrontier(
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
        Span<u8> frontierFlags
    );
    [[nodiscard]] static b8 BuildMeshlets(const Path& nwbFilePath, const TStringView metaKind, Span<const u32> indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& entry);


public:
    MeshCookMeshlets() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

