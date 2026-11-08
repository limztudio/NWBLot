// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shadow/quality_settings.adhd.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;
struct DeferredFrameTargets;
struct RayTracingShadowPreparationResourceSnapshot;
struct RayTracingDeferredGraphResourceSnapshot;
struct RayTracingShadowVisibilityGraphPlanSnapshot;
struct RayTracingSceneGraphReads;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowVisibilityResourceInputs{
    const DeferredFrameTargets& deferredTargets;
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan;
    const RayTracingSceneGraphReads& hardwareSceneReads;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId shadowVisibility;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Span<const Core::GpuGraphResourceId> traceGeometryResources;
    b8 traceGeometryStatesGraphOwned;
    b8 splitSoftTransparentFold;
    b8 graphOwnsOpaqueTemporalMergeEntryStates;
    b8 softShadowHistoryReadable;
    b8 opaqueHistoryFrontIsA;
    b8 hardwareTransparentTrace;
    b8 adaptiveCompact;
};

struct ShadowVisibilityResourcePlan{
    Core::GpuGraphResourceId sceneGeometryDomain;
    Core::GpuGraphResourceId sceneTlas;
    Array<Core::GpuGraphResourceId, s_HwTransparentScratchBufferCount> hardwareTransparentScratch{};
    Core::GpuGraphResourceId adaptiveEdgeCounter;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses;


public:
    explicit ShadowVisibilityResourcePlan(Core::Alloc::ScratchArena& scratchArena)noexcept
        : resourceUses(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowVisibilityResourcePlanner final : NoCopy{
public:
    ShadowVisibilityResourcePlanner(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] b8 prepare(const ShadowVisibilityResourceInputs& inputs, ShadowVisibilityResourcePlan& result);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

