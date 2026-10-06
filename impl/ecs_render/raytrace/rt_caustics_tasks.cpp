// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuTaskId RendererRayTracingSystem::declareCausticAccumulatorDecayTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool& shadowVisibilityPrepared,
    const f32 decayFactor,
    const bool hardwareCaustics,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticAccumulatorDecayGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticAccumulatorDecayGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .meshView = meshView,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .timingTicket = timingTicket,
            .causticPhotonTiming = causticPhotonTiming,
            .decayFactor = decayFactor,
            .hardwareCaustics = hardwareCaustics,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSoftwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool& shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    bool& causticProducerDispatched,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear){
    return graph.addTask<RayTracingCausticsTaskDetail::SoftwareCausticsGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::SoftwareCausticsGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .meshView = meshView,
            .timingTicket = timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .causticProducerDispatched = causticProducerDispatched,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareHardwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool& shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    bool& causticProducerDispatched,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear){
    return graph.addTask<RayTracingCausticsTaskDetail::HardwareCausticsGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::HardwareCausticsGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .meshView = meshView,
            .timingTicket = timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .causticProducerDispatched = causticProducerDispatched,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareCausticGeometryDownsampleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool& causticProducerDispatched,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticGeometryDownsampleGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticGeometryDownsampleGraphTask::Payload{
            .raytracingSystem = *this,
            .graphics = m_graphics,
            .targets = targets,
            .timingTicket = timingTicket,
            .causticProducerDispatched = causticProducerDispatched,
            .causticResolveTiming = causticResolveTiming,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool& causticProducerDispatched,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveGraphTask::Payload{
            .timingTicket = timingTicket,
            .causticProducerDispatched = causticProducerDispatched,
            .causticResolveTiming = causticResolveTiming,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolvePrepareTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool& causticProducerDispatched){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolvePrepareGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolvePrepareGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool& causticProducerDispatched){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveSecondWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool& causticProducerDispatched){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveSecondWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveSecondWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveThirdWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool& causticProducerDispatched){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveThirdWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveThirdWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveFourthWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool& causticProducerDispatched){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveFourthWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveFourthWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveFifthWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool& causticProducerDispatched){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveFifthWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveFifthWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveUpsampleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool& causticProducerDispatched){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveUpsampleGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveUpsampleGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

