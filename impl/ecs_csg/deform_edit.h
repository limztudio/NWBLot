// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"

#include "deform_pipeline.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deterministic CPU-side deformable CSG editing facade.


// Preview and commit share one rebuild path (CsgDeformPipeline) so viability always agrees. This header preserves the existing include path; domain logic lives in 
// deform_types / deform_validator / deform_cutter_field / deform_wall_builder / deform_cap_builder / deform_pipeline.
// - Sequential cuts apply in order, one rebuild per active cut.
// - Rebuild splits triangles at the zero crossing, never welds source verts.
// - Walls are the kept split triangles with interpolated attributes.
// - Caps fill cut boundary loops with a deterministic fan.
// - One epsilon, one edge-cache rule, one cap orientation rule for both preview and commit.

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

// Commit rebuild. Runs the same rebuild as preview into scratch, then copies
// into the commit arena, so preview viability always matches commit viability.
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

