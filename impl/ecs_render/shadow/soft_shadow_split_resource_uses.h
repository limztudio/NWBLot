// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/alloc/scratch.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Crossings, overflow list, and overflow arguments.
inline constexpr usize s_HardwareTransparentShadowScratchCount = 3u;

// Frozen prepared soft-transparent route shared by the split resource declarations and the split task chain.
struct SoftShadowSplitRoute{
    bool hardwareShadowSupported = false;
    bool hardwareCsgShadow = false;
    bool csgShadow = false;
    bool traceGeometryStatesGraphOwned = false;
    bool combinedSoftUpsample = false;
    bool combinedSoftWavelet = false;
    bool combinedSoftTemporal = false;
    bool graphOwnsOpaqueTemporalMergeEntryStates = false;
};

struct SoftShadowSplitResourceInputs{
    const DeferredFrameTargets& targets;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan;
    const RayTracingSceneGraphReads& hardwareSceneReads;
    const Core::GpuGraphResourceId (&hardwareTransparentScratch)[s_HardwareTransparentShadowScratchCount];
    SoftShadowSplitRoute route;
    const Core::GpuGraphResourceId* traceGeometryResources = nullptr;
    usize traceGeometryResourceCount = 0u;
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
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaque;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueFirstWavelet;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueResolve;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTrace;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentTemporalMerge;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentFirstWavelet;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> transparentFold;
    bool graphOwnsTransparentTemporalMergeEntryStates = false;


public:
    explicit SoftShadowSplitResourceUses(Core::Alloc::ScratchArena& scratchArena)noexcept
        : opaque(scratchArena)
        , opaqueFirstWavelet(scratchArena)
        , opaqueResolve(scratchArena)
        , transparentTrace(scratchArena)
        , transparentTemporalMerge(scratchArena)
        , transparentFirstWavelet(scratchArena)
        , transparentFold(scratchArena)
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
    [[nodiscard]] bool build(const SoftShadowSplitResourceInputs& inputs, SoftShadowSplitResourceUses& uses);


private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

