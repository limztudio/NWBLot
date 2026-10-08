// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"
#include "deform_pipeline.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Preview and commit share split-at-zero rebuilds, unwelded seams, epsilon, edge-cache and cap rules.

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
[[nodiscard]] Expected<CsgDeformStats, CsgDeformFailure> PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles
);

// Rebuild in scratch, then copy into the commit arena; viability matches preview.
[[nodiscard]] Expected<CsgDeformStats, CsgDeformFailure> CommitCsgDeformCuts(
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
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

