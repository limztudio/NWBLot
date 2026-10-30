// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shadow/light_space_settings.adhd.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/alloc/scratch.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SoftShadowSplitResourceInputs{
    const DeferredFrameTargets& targets;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan;
    const RayTracingSceneGraphReads& hardwareSceneReads;
    const Core::GpuGraphResourceId (&hardwareTransparentScratch)[s_HardwareTransparentShadowScratchCount];
    SoftShadowSplitRoute route;
    Span<const Core::GpuGraphResourceId> traceGeometryResources{};
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId shadowVisibility;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Core::GpuGraphResourceId sceneGeometryDomain;
    Core::GpuGraphResourceId sceneTlas;
};

// Per-callback resource declarations of the prepared soft-transparent shadow route.
struct SoftShadowSplitResourceUses{
    static constexpr usize s_MaxOpaqueFirstWaveletUses = 12u;
    static constexpr usize s_MaxOpaqueResolveUses = 8u;
    static constexpr usize s_MaxTransparentTemporalMergeUses = 13u;
    static constexpr usize s_MaxTransparentFirstWaveletUses = 7u;
    static constexpr usize s_MaxTransparentFoldUses = 9u;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaque;
    Core::GpuTaskResourceUse opaqueFirstWavelet[s_MaxOpaqueFirstWaveletUses] = {};
    usize opaqueFirstWaveletCount = 0u;
    Core::GpuTaskResourceUse opaqueResolve[s_MaxOpaqueResolveUses] = {};
    usize opaqueResolveCount = 0u;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTrace;
    Core::GpuTaskResourceUse transparentTemporalMerge[s_MaxTransparentTemporalMergeUses] = {};
    usize transparentTemporalMergeCount = 0u;
    Core::GpuTaskResourceUse transparentFirstWavelet[s_MaxTransparentFirstWaveletUses] = {};
    usize transparentFirstWaveletCount = 0u;
    Core::GpuTaskResourceUse transparentFold[s_MaxTransparentFoldUses] = {};
    usize transparentFoldCount = 0u;
    b8 graphOwnsTransparentTemporalMergeEntryStates = false;


public:
    explicit SoftShadowSplitResourceUses(Core::Alloc::ScratchArena& scratchArena)noexcept
        : opaque(scratchArena)
        , transparentTrace(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Re-imports the soft-shadow targets under their shared graph identities and declares each split callback's exact states.
class SoftShadowSplitResourceUseBuilder final : NoCopy{
public:
    explicit SoftShadowSplitResourceUseBuilder(Core::GpuTaskGraph& graph)noexcept
        : m_graph(graph)
    {}


public:
    [[nodiscard]] b8 build(const SoftShadowSplitResourceInputs& inputs, SoftShadowSplitResourceUses& uses);


private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
