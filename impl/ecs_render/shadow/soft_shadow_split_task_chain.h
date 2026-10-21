// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "soft_shadow_split_resource_uses.h"

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SoftShadowSplitTaskChainInputs{
    DeferredFrameTargets& targets;
    const DeferredLightingGraphResources& deferredLightingResources;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan;
    const bool& shadowPrepared;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& asyncTiming;
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming;
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
    bool& opaqueProduced;
    bool& transparentTraceProduced;
    u32& opaqueFrameIndex;
    SoftShadowSplitRoute route;
    const Core::GpuGraphResourceId* traceGeometryResources = nullptr;
    usize traceGeometryResourceCount = 0u;
    const Core::GpuTaskResourceSetUse* traceResourceSetUses = nullptr;
    usize traceResourceSetUseCount = 0u;
    Core::GpuTaskResourceSetUse traceGeometrySetUse{};
    const Core::GpuTaskExternalStateSource* stateSources = nullptr;
    usize stateSourceCount = 0u;
    const Core::GpuExternalCompletionId* historyWriterDrainDependencies = nullptr;
    usize historyWriterDrainDependencyCount = 0u;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Core::GpuTaskId prefixTask;
};

struct SoftShadowSplitTasks{
    Core::GpuTaskId opaque;
    Core::GpuTaskId opaqueFirstWavelet;
    Core::GpuTaskId opaqueResolve;
    Core::GpuTaskId transparentTrace;
    Core::GpuTaskId transparentTemporalMerge;
    Core::GpuTaskId transparentFirstWavelet;
    Core::GpuTaskId fold;
    Core::GpuTaskId diagnostics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The prepared soft-transparent shadow route: light-space maps when ready, the opaque producer with its wavelet and resolve tail, then the transparent trace, temporal merge, first wavelet, and terminal fold.
// Tasks declared before a failure stay in `tasks`.
class SoftShadowSplitTaskChainBuilder final : NoCopy{
public:
    SoftShadowSplitTaskChainBuilder(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, Core::Alloc::GlobalArena& arena, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_graphics(graphics)
        , m_arena(arena)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] bool declare(Core::Alloc::ScratchArena& scratchArena, const SoftShadowSplitTaskChainInputs& inputs, SoftShadowSplitResourceUses& uses, SoftShadowSplitTasks& tasks);


private:
    // Adds the map, cap, and CSG bound reads to the opaque producer and transparent trace.
    [[nodiscard]] bool declareLightSpaceShadowMaps(
        Core::Alloc::ScratchArena& scratchArena,
        const SoftShadowSplitTaskChainInputs& inputs,
        SoftShadowSplitResourceUses& uses,
        Core::GpuTaskId& shadowTraceDependency,
        Core::GpuGraphResourceId& csgShadowContext
    );
    [[nodiscard]] bool declareOpaqueTasks(
        const SoftShadowSplitTaskChainInputs& inputs,
        const SoftShadowSplitResourceUses& uses,
        const Core::GpuTaskId& shadowTraceDependency,
        const Core::GpuTaskSchedulingHint& tailScheduling,
        SoftShadowSplitTasks& tasks
    );
    [[nodiscard]] bool declareTransparentTasks(
        Core::Alloc::ScratchArena& scratchArena,
        const SoftShadowSplitTaskChainInputs& inputs,
        const SoftShadowSplitResourceUses& uses,
        const Core::GpuTaskSchedulingHint& tailScheduling,
        Core::GpuGraphResourceId csgShadowContext,
        SoftShadowSplitTasks& tasks
    );


private:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    Core::Alloc::GlobalArena& m_arena;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

