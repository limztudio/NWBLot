// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingCausticsTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Payload>
static void ConfirmCausticAccumulatorClears(Payload& payload)noexcept(noexcept(static_cast<b8>(payload.graphOwnsAccumulatorBootstrapClear && payload.causticProducerDispatched)) && noexcept(payload.raytracingSystem.confirmCausticAccumulatorBootstrapClear())){
    if(payload.graphOwnsAccumulatorBootstrapClear && payload.causticProducerDispatched)
        payload.raytracingSystem.confirmCausticAccumulatorBootstrapClear();
}

// A warm accumulator decays before either photon route writes splats. Separate task so the compiler lowers the UAV dep into the producer.
struct CausticAccumulatorDecayGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: decay always brackets the caustic-photon timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        const b8& shadowVisibilityPrepared;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming;
        b8 hardwareCaustics = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){

        // Match the producer's no-work behavior: keep the dep, but skip dispatch unless the producer would run.
        if(!payload.shadowVisibilityPrepared)
            return true;
        const b8 hasWork = payload.hardwareCaustics ? payload.raytracingSystem.hasHwCausticWork(payload.meshView) : payload.raytracingSystem.hasCausticWork(payload.meshView);
        if(!hasWork)
            return true;

        // A rejected record may retry before discard. Release the incomplete reservation before the retry's interval.
        Core::DiscardGpuTimingMeasure(&payload.causticPhotonTiming);
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        payload.causticPhotonTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, payload.graphics.getDevice(), commandList);
        if(!Core::FinishSplitGpuTimingMarker(&payload.causticPhotonTiming))
            return false;
        const b8 dispatched = payload.raytracingSystem.dispatchCausticAccumulatorDecay(commandList, payload.targets);
        if(!dispatched){
            Core::DiscardGpuTimingMeasure(&payload.causticPhotonTiming);
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned caustic accumulator decay pass failed"));
        }
        return true;
    }

    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(&payload.causticPhotonTiming);
    }
};

// Caustic producers own typed graph-task payloads; RendererFramePipeline composes their packet chain.
// The renderer still supplies declaration-filtered external state until the graph has every producer in the same frame transaction.
template<b8 Hardware>
struct CausticProducerGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: photons always record against this frame's ticket and publish dispatch.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& shadowVisibilityPrepared;
        Optional<Core::GpuTimingMeasure>& causticPhotonTiming;
        b8 graphOwnsAccumulatorBootstrapClear = false;
        b8& causticProducerDispatched;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        payload.causticProducerDispatched = false;
        if(!payload.deferredLightingResources.valid())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        // Typed clears and warm decay precede photons; acceptance publishes their CPU lifecycle.
        if(payload.shadowVisibilityPrepared){
            b8 causticsDispatched;
            if constexpr(Hardware)
                causticsDispatched = payload.raytracingSystem.renderHwCaustics(commandList, payload.meshView, payload.targets, &payload.causticPhotonTiming);
            else
                causticsDispatched = payload.raytracingSystem.renderGpuBvhCaustics(commandList, payload.meshView, payload.targets, &payload.causticPhotonTiming);
            if(!causticsDispatched)
                Core::DiscardGpuTimingMeasure(&payload.causticPhotonTiming);
            payload.causticProducerDispatched = causticsDispatched;
            if(!causticsDispatched){
                if constexpr(Hardware){
                    if(payload.raytracingSystem.hasHwCausticWork(payload.meshView))
                        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic render pass failed"));
                }
                else if(payload.raytracingSystem.hasCausticWork(payload.meshView))
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic render pass failed"));
            }
        }
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        ConfirmCausticAccumulatorClears(payload);
        if(payload.causticProducerDispatched)
            payload.raytracingSystem.confirmRayTraceContextReadSubmission(token);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.causticProducerDispatched = false;
        Core::DiscardGpuTimingMeasure(&payload.causticPhotonTiming);
    }
};

using SoftwareCausticsGraphTask = CausticProducerGraphTask<false>;
using HardwareCausticsGraphTask = CausticProducerGraphTask<true>;

// Geometry downsample follows the selected photon producer in the same graph packet.
// Its timing begin is retained until wavelet resolve records the endpoint, preserving the established full-resolve interval across callbacks.
struct CausticGeometryDownsampleGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: geometry downsample always brackets the caustic-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& causticProducerDispatched;
        Optional<Core::GpuTimingMeasure>& causticResolveTiming;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        Core::DiscardGpuTimingMeasure(&payload.causticResolveTiming);
        if(!payload.causticProducerDispatched)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        payload.causticResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_CausticResolve, payload.graphics.getDevice(), commandList);
        payload.raytracingSystem.dispatchCausticGeometryDownsample(commandList, payload.targets);
        // The next callback ends timestamps on this same list. Close this nested marker before the recorder advances.
        return Core::FinishSplitGpuTimingMarker(&payload.causticResolveTiming);
    }

    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(&payload.causticResolveTiming);
    }
};

// All resolve phases retain the same activity and producer bindings.
// Specializations remain separate graph tasks, preserving each ping-pong resource handoff.
struct CausticResolveRecordInputs{
    RendererRayTracingSystem& raytracingSystem;
    DeferredFrameTargets& targets;
    const b8& causticProducerDispatched;
    CausticResolveActivitySnapshot activity;
};

template<auto Dispatch>
struct CausticResolvePhaseGraphTask{
    using Payload = CausticResolveRecordInputs;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context)noexcept(noexcept((payload.raytracingSystem.*Dispatch)(commandList, payload.targets))){
        if(!payload.activity.matches(payload.raytracingSystem.causticResolveActivitySnapshot(payload.targets)))
            return false;
        if(payload.causticProducerDispatched)
            (payload.raytracingSystem.*Dispatch)(commandList, payload.targets);
        return true;
    }
};

struct CausticResolveWaveletGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        const b8& causticProducerDispatched;
        CausticResolveActivitySnapshot activity;
        u32 passIndex = 0u;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        if(!payload.activity.matches(payload.raytracingSystem.causticResolveActivitySnapshot(payload.targets)))
            return false;
        if(payload.causticProducerDispatched)
            payload.raytracingSystem.dispatchCausticResolveWaveletPass(commandList, payload.targets, payload.passIndex);
        return true;
    }
};

using CausticResolvePrepareGraphTask = CausticResolvePhaseGraphTask<&RendererRayTracingSystem::dispatchCausticResolvePrepare>;
using CausticResolveUpsampleGraphTask = CausticResolvePhaseGraphTask<&RendererRayTracingSystem::dispatchCausticResolveUpsample>;

// The empty timing-close callback follows graph-owned upsample and finishes the retained full-resolve interval.
struct CausticResolveGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    // All bindings are required: resolve always closes the caustic-resolve timing against this frame's ticket.
    struct Payload{
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& causticProducerDispatched;
        Optional<Core::GpuTimingMeasure>& causticResolveTiming;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        // No-producer contract: the graph-owned irradiance clear wins, no resolve dispatch when the producer never recorded.
        if(!payload.causticProducerDispatched){
            Core::DiscardGpuTimingMeasure(&payload.causticResolveTiming);
            return true;
        }
        if(!payload.causticResolveTiming.has_value())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        payload.causticResolveTiming.value().finishTiming(commandList);
        payload.causticResolveTiming.reset();
        return true;
    }

    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(&payload.causticResolveTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolvePrepareTask(const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, Core::GpuTaskGraph& graph, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolvePrepareGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolvePrepareGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveWaveletTask(
    const Core::GpuTaskDesc& desc,
    const u32 passIndex,
    const b8& causticProducerDispatched,
    Core::GpuTaskGraph& graph,
    DeferredFrameTargets& targets
){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveWaveletGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveWaveletGraphTask::Payload{
            .raytracingSystem = *this,
            .targets = targets,
            .causticProducerDispatched = causticProducerDispatched,
            .activity = causticResolveActivitySnapshot(targets),
            .passIndex = passIndex,
        }
    );
}

Expected<Core::GpuTaskId> RendererRayTracingSystem::declareCausticResolveUpsampleTask(const Core::GpuTaskDesc& desc, const b8& causticProducerDispatched, Core::GpuTaskGraph& graph, DeferredFrameTargets& targets){
    return graph.addTask<RayTracingCausticsTaskDetail::CausticResolveUpsampleGraphTask>(
        desc,
        RayTracingCausticsTaskDetail::CausticResolveUpsampleGraphTask::Payload{ .raytracingSystem = *this, .targets = targets, .causticProducerDispatched = causticProducerDispatched, .activity = causticResolveActivitySnapshot(targets), }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

