// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_caustics_tasks.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticAccumulatorDecayTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const b8& shadowVisibilityPrepared,
    const b8 hardwareCaustics,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming
){
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
            .hardwareCaustics = hardwareCaustics,
        }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareSoftwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const b8& shadowVisibilityPrepared,
    const b8 graphOwnsAccumulatorBootstrapClear,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    b8& causticProducerDispatched
){
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
            .causticProducerDispatched = causticProducerDispatched,
        }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareHardwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const b8& shadowVisibilityPrepared,
    const b8 graphOwnsAccumulatorBootstrapClear,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticPhotonTiming,
    b8& causticProducerDispatched
){
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
            .causticProducerDispatched = causticProducerDispatched,
        }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticGeometryDownsampleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const b8& causticProducerDispatched,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming
){
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

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const b8& causticProducerDispatched,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& causticResolveTiming
){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveGraphTask::Payload{ .timingTicket = timingTicket, .causticProducerDispatched = causticProducerDispatched, .causticResolveTiming = causticResolveTiming, }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolvePrepareTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolvePrepareGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolvePrepareGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveWaveletGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveSecondWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveSecondWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveSecondWaveletGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveThirdWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveThirdWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveThirdWaveletGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveFourthWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveFourthWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveFourthWaveletGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveFifthWaveletTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveFifthWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveFifthWaveletGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveUpsampleTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveUpsampleGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveUpsampleGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

