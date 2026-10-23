// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_surfel_tasks.h"
#include "surfel_gi_task_builder.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> RendererRayTracingSystem::declareSurfelResourceInitializationLifecycleTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc){
    return graph.addTask<SurfelGiInitializationLifecycleGraphTask>(desc, SurfelGiInitializationLifecycleGraphTask::Payload{ .raytracingSystem = *this, });
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareAgeFreeTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming){
    return m_graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask::Payload{ .raytracingSystem = m_system, .graphics = m_system.m_graphics, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareHashBuildTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming){
    return m_graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareSpawnTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming){
    return m_graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareTraceBuildArgsTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming){
    return m_graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareTraceTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming){
    return m_graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareResolveTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>& asyncTiming){
    return m_graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareTerminalTask(const Core::GpuTaskDesc& desc, const DeferredLightingGraphResources& deferredLightingResources, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket, Optional<Core::GpuTimingMeasure>* const asyncTiming){
    return m_graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

