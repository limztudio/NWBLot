// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Declares the surfel GI phase tasks; every payload borrows the ray-tracing system and the frame targets until the packet accepts.
class SurfelGiTaskBuilder final : NoCopy{
public:
    SurfelGiTaskBuilder(RendererRayTracingSystem& system, Core::GpuTaskGraph& graph)noexcept
        : m_system(system)
        , m_graph(graph)
    {}


public:
    [[nodiscard]] Expected<Core::GpuTaskId> declareAgeFreeTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming);
    [[nodiscard]] Expected<Core::GpuTaskId> declareHashBuildTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming);
    [[nodiscard]] Expected<Core::GpuTaskId> declareSpawnTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming);
    [[nodiscard]] Expected<Core::GpuTaskId> declareTraceBuildArgsTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming);
    [[nodiscard]] Expected<Core::GpuTaskId> declareTraceTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming);
    [[nodiscard]] Expected<Core::GpuTaskId> declareResolveTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming);
    // Records the phases the split route left over; only the resolve-owning route brackets the shared async range.
    [[nodiscard]] Expected<Core::GpuTaskId> declareTerminalTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>* asyncTiming);


private:
    RendererRayTracingSystem& m_system;
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

