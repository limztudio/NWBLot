// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Deterministic CPU-side rebuild orchestration for deformable CSG editing.
//
// Applies sequential cuts in order (one wall rebuild plus cap fill per active
// cut) through the shared validator, cutter-field, wall, and cap classes,
// so preview and commit viability always agree.
struct CsgDeformPipelineResult{
    CsgDeformViability viability;
    CsgDeformStats stats;
};

static_assert(IsStandardLayout_V<CsgDeformPipelineResult>, "CsgDeformPipelineResult must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgDeformPipelineResult>, "CsgDeformPipelineResult must stay cheap to pass by value");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgDeformPipeline final : NoCopy{
public:
    CsgDeformPipeline() = delete;


public:
// Preview and commit share one rebuild path so viability always agrees:
// - Sequential cuts apply in order, one rebuild per active cut.
// - Rebuild splits triangles at the zero crossing, never welds source verts.
// - Walls are the kept split triangles with interpolated attributes.
// - Caps fill cut boundary loops with a deterministic fan.
// - One epsilon, one edge-cache rule, one cap orientation rule for both preview and commit.
    [[nodiscard]] static bool RebuildSequentialCuts(
        Core::Alloc::ScratchArena& scratchArena,
        NotNull<const CsgDeformVertex*> inputVertices,
        const usize inputVertexCount,
        NotNull<const CsgDeformTriangle*> inputTriangles,
        const usize inputTriangleCount,
        const CsgDeformCutDesc* cuts,
        const usize cutCount,
        const CsgDeformBuildOptions& options,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles,
        CsgDeformPipelineResult& outResult
    );
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

