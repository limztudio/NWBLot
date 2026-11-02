// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


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
namespace MeshCookMeshlets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ClearMeshletFrontier(Vector<u32, Core::Alloc::ScratchArena>& frontier, Span<u8> frontierFlags)noexcept;
void RemoveMeshletFrontierCandidate(usize frontierOffset, Vector<u32, Core::Alloc::ScratchArena>& frontier, Span<u8> frontierFlags)noexcept;
void AddMeshletTriangleNeighborsToFrontier(const MeshletTrianglePrecompute& trianglePrecompute, u32 triangleIndex, Vector<u32, Core::Alloc::ScratchArena>& frontier, Span<u8> frontierFlags);
[[nodiscard]] b8 AddMeshletTriangleToBuilder(
    const Path& nwbFilePath,
    TStringView metaKind,
    const MeshletTriangleData& triangle,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices
);
[[nodiscard]] b8 AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    TStringView metaKind,
    u32 triangleIndex,
    MeshletTrianglePrecompute& trianglePrecompute,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    Span<u8> frontierFlags
);
[[nodiscard]] b8 GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    TStringView metaKind,
    usize seedSearchOffset,
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
[[nodiscard]] b8 BuildMeshlets(const Path& nwbFilePath, TStringView metaKind, Span<const u32> indices, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, MeshCookEntry& entry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

