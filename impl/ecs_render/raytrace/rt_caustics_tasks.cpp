// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::GpuTaskId RendererRayTracingSystem::declareCausticAccumulatorDecayTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool* const shadowVisibilityPrepared,
    const f32 decayFactor,
    const bool hardwareCaustics,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticAccumulatorDecayGraphTask>(
        desc,
        __hidden_caustics::CausticAccumulatorDecayGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .meshView = meshView,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .timingTicket = &timingTicket,
            .causticPhotonTiming = causticPhotonTiming,
            .decayFactor = decayFactor,
            .hardwareCaustics = hardwareCaustics,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSoftwareCausticsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const bool* const shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    bool* const causticProducerDispatched
){
    return graph.addTask<__hidden_caustics::SoftwareCausticsGraphTask>(
        desc,
        __hidden_caustics::SoftwareCausticsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .meshView = meshView,
            .timingTicket = &timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .graphOwnsAccumulatorDecay = graphOwnsAccumulatorDecay,
            .graphOwnsResolve = graphOwnsResolve,
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
    const bool* const shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsNonTemporalAccumulatorClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    bool* const causticProducerDispatched
){
    return graph.addTask<__hidden_caustics::HardwareCausticsGraphTask>(
        desc,
        __hidden_caustics::HardwareCausticsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .meshView = meshView,
            .timingTicket = &timingTicket,
            .shadowVisibilityPrepared = shadowVisibilityPrepared,
            .causticPhotonTiming = causticPhotonTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsAccumulatorBootstrapClear = graphOwnsAccumulatorBootstrapClear,
            .graphOwnsNonTemporalAccumulatorClear = graphOwnsNonTemporalAccumulatorClear,
            .graphOwnsAccumulatorDecay = graphOwnsAccumulatorDecay,
            .graphOwnsResolve = graphOwnsResolve,
            .causticProducerDispatched = causticProducerDispatched,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareCausticGeometryDownsampleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool* const causticProducerDispatched,
    Optional<Core::GpuTimingMeasure>* const causticResolveTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticGeometryDownsampleGraphTask>(
        desc,
        __hidden_caustics::CausticGeometryDownsampleGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .causticProducerDispatched = causticProducerDispatched,
            .causticResolveTiming = causticResolveTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool* const causticProducerDispatched,
    Optional<Core::GpuTimingMeasure>* const causticResolveTiming
){
    return graph.addTask<__hidden_caustics::CausticResolveGraphTask>(
        desc,
        __hidden_caustics::CausticResolveGraphTask::Payload{
            .timingTicket = &timingTicket,
            .causticProducerDispatched = causticProducerDispatched,
            .causticResolveTiming = causticResolveTiming,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolvePrepareTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolvePrepareGraphTask>(
        desc,
        __hidden_caustics::CausticResolvePrepareGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveSecondWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveSecondWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveSecondWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveThirdWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveThirdWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveThirdWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveFourthWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveFourthWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveFourthWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveFifthWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveFifthWaveletGraphTask>(
        desc,
        __hidden_caustics::CausticResolveFifthWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .activity = causticResolveActivitySnapshot(targets),
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareCausticResolveUpsampleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const bool* const causticProducerDispatched,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_caustics::CausticResolveUpsampleGraphTask>(
        desc,
        __hidden_caustics::CausticResolveUpsampleGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .causticProducerDispatched = causticProducerDispatched,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

