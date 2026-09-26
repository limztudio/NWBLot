// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"
#include "deform_pipeline.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deterministic CPU-side deformable CSG editing facade.


// Preview + commit share CsgDeformPipeline (one rebuild per cut; split-at-zero, no vert weld; kept splits become
// walls, boundary loops get deterministic fan caps; one epsilon/edge-cache/cap rule). This header keeps the include path.

// Shared viability classifier used by both preview and commit.
[[nodiscard]] CsgDeformViability CheckCsgDeformCutsViability(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    usize cutCount,
    const CsgDeformBuildOptions& options
);

// Preview rebuild into scratch storage. Same code path as commit.
[[nodiscard]] bool PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles,
    CsgDeformStats& outStats
);

// Commit rebuild. Runs the same rebuild as preview into scratch, then copies into the commit arena, so preview viability always matches commit viability.
[[nodiscard]] bool CommitCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    Core::Alloc::GlobalArena& commitArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::GlobalArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles,
    CsgDeformStats& outStats
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

