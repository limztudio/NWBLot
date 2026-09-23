// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiAgeFreeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = &asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiHashBuildTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiSpawnTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiTraceBuildArgsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiTraceTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiResolveTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsCellHeadClear,
    const bool graphOwnsHashBuild,
    const bool graphOwnsSpawn,
    const bool graphOwnsTraceBuildArgs,
    const bool graphOwnsTrace,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const asyncTiming
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsCellHeadClear = graphOwnsCellHeadClear,
            .graphOwnsHashBuild = graphOwnsHashBuild,
            .graphOwnsSpawn = graphOwnsSpawn,
            .graphOwnsTraceBuildArgs = graphOwnsTraceBuildArgs,
            .graphOwnsTrace = graphOwnsTrace,
            .graphOwnsResolve = graphOwnsResolve,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareSurfelResourceInitializationLifecycleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc
){
    return graph.addTask<SurfelGiInitializationLifecycleGraphTask>(
        desc,
        SurfelGiInitializationLifecycleGraphTask::Payload{
            .raytracingSystem = this,
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

