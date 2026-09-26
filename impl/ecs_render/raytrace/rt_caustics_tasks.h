// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_HwRaygenExportName = "CausticHwRayGen";
static constexpr AStringView s_HwMissExportName = "CausticHwMiss";
static constexpr AStringView s_HwHitGroupExportName = "CausticHwHitGroup";

template<typename Payload>
static void ConfirmCausticAccumulatorClears(Payload& payload){
    if(payload.raytracingSystem && payload.graphOwnsNonTemporalAccumulatorClear)
        payload.raytracingSystem->confirmCausticAccumulatorNonTemporalClear();
    if(
        payload.raytracingSystem
        && payload.graphOwnsAccumulatorBootstrapClear
        && payload.causticProducerDispatched
        && *payload.causticProducerDispatched
    )
        payload.raytracingSystem->confirmCausticAccumulatorBootstrapClear();
}

// A warm temporal accumulator decays before either photon route writes its atomic splats.  This remains a separate graph task so the compiler lowers the accumulator's UAV dependency into the producer callback rather than depending on a packet-local state reassertion after the decay dispatch.
struct CausticAccumulatorDecayGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        const bool* shadowVisibilityPrepared = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        f32 decayFactor = 0.f;
        bool hardwareCaustics = false;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.raytracingSystem
            || !payload.graphics
            || !payload.targets
            || !payload.timingTicket
            || !payload.causticPhotonTiming
        )
            return false;

        // Match the selected producer's existing no-work and failed-shadow behavior.  A graph declaration can still retain the accumulator dependency, but no dispatch is issued unless the producer would run.
        if(!payload.shadowVisibilityPrepared || !*payload.shadowVisibilityPrepared)
            return true;
        const bool hasWork = payload.hardwareCaustics
            ? payload.raytracingSystem->hasHwCausticWork(payload.meshView)
            : payload.raytracingSystem->hasCausticWork(payload.meshView)
        ;
        if(!hasWork)
            return true;

        // A prior rejected record can retry this task before its graph transaction gets discarded.  Release the incomplete query reservation before starting the retry's one caustic-photons interval.
        DiscardGpuTimingMeasure(payload.causticPhotonTiming);
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.causticPhotonTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            payload.graphics->getDevice(),
            commandList
        );
        if(!Core::FinishSplitGpuTimingMarker(payload.causticPhotonTiming))
            return false;
        const bool dispatched = payload.raytracingSystem->dispatchCausticAccumulatorDecay(
            commandList,
            *payload.targets,
            payload.decayFactor,
            payload.graphEntryStatesOwned
        );
        if(!dispatched){
            DiscardGpuTimingMeasure(payload.causticPhotonTiming);
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned caustic accumulator decay pass failed"));
        }
        return true;
    }

    static void discarded(Payload& payload){
        DiscardGpuTimingMeasure(payload.causticPhotonTiming);
    }
};

// Caustic producers own typed graph-task payloads; RendererFramePipeline composes their packet chain.
// The renderer still supplies declaration-filtered external state until the graph has every producer in the same frame transaction.
struct SoftwareCausticsGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* shadowVisibilityPrepared = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsAccumulatorBootstrapClear = false;
        bool graphOwnsNonTemporalAccumulatorClear = false;
        bool graphOwnsAccumulatorDecay = false;
        bool graphOwnsResolve = false;
        bool* causticProducerDispatched = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        if(!payload.raytracingSystem || !payload.targets || !payload.deferredLightingResources.valid() || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        // The typed graph clear retains black irradiance whenever no producer dispatches. Non-temporal reset, fresh bootstrap, and warm temporal decay can all be graph-owned before this callback; direct callers retain the legacy non-temporal reset here.
        if(!payload.graphOwnsNonTemporalAccumulatorClear)
            payload.raytracingSystem->clearNonTemporalCausticAccumulator(commandList, *payload.targets);
        if(payload.shadowVisibilityPrepared && *payload.shadowVisibilityPrepared){
            const bool causticsDispatched = payload.raytracingSystem->renderGpuBvhCaustics(
                commandList,
                payload.meshView,
                *payload.targets,
                payload.deferredLightingResources,
                payload.graphEntryStatesOwned,
                payload.graphOwnsAccumulatorBootstrapClear,
                payload.graphOwnsAccumulatorDecay,
                payload.graphOwnsResolve,
                payload.causticPhotonTiming
            );
            if(!causticsDispatched)
                DiscardGpuTimingMeasure(payload.causticPhotonTiming);
            if(payload.causticProducerDispatched)
                *payload.causticProducerDispatched = causticsDispatched;
            if(!causticsDispatched && payload.raytracingSystem->hasCausticWork(payload.meshView))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic render pass failed"));
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        ConfirmCausticAccumulatorClears(payload);
    }

    static void discarded(Payload& payload){
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        Core::DiscardGpuTimingMeasure(payload.causticPhotonTiming);
    }
};


struct HardwareCausticsGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        ECSRenderDetail::MeshViewBufferSnapshot meshView;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* shadowVisibilityPrepared = nullptr;
        Optional<Core::GpuTimingMeasure>* causticPhotonTiming = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsAccumulatorBootstrapClear = false;
        bool graphOwnsNonTemporalAccumulatorClear = false;
        bool graphOwnsAccumulatorDecay = false;
        bool graphOwnsResolve = false;
        bool* causticProducerDispatched = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        if(!payload.raytracingSystem || !payload.targets || !payload.deferredLightingResources.valid() || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        // The typed graph clear retains black irradiance whenever no producer dispatches. Non-temporal reset, fresh bootstrap, and warm temporal decay can all be graph-owned before this callback; direct callers retain the legacy non-temporal reset here.
        if(!payload.graphOwnsNonTemporalAccumulatorClear)
            payload.raytracingSystem->clearNonTemporalCausticAccumulator(commandList, *payload.targets);
        if(payload.shadowVisibilityPrepared && *payload.shadowVisibilityPrepared){
            const bool causticsDispatched = payload.raytracingSystem->renderHwCaustics(
                commandList,
                payload.meshView,
                *payload.targets,
                payload.deferredLightingResources,
                payload.graphEntryStatesOwned,
                payload.graphOwnsAccumulatorBootstrapClear,
                payload.graphOwnsAccumulatorDecay,
                payload.graphOwnsResolve,
                payload.causticPhotonTiming
            );
            if(!causticsDispatched)
                DiscardGpuTimingMeasure(payload.causticPhotonTiming);
            if(payload.causticProducerDispatched)
                *payload.causticProducerDispatched = causticsDispatched;
            if(!causticsDispatched && payload.raytracingSystem->hasHwCausticWork(payload.meshView))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic render pass failed"));
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        ConfirmCausticAccumulatorClears(payload);
    }

    static void discarded(Payload& payload){
        if(payload.causticProducerDispatched)
            *payload.causticProducerDispatched = false;
        Core::DiscardGpuTimingMeasure(payload.causticPhotonTiming);
    }
};

// Geometry downsample follows the selected photon producer in the same graph packet.
// Its timing begin is retained until wavelet resolve records the endpoint, preserving the established full-resolve interval across callbacks.
struct CausticGeometryDownsampleGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* causticProducerDispatched = nullptr;
        Optional<Core::GpuTimingMeasure>* causticResolveTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.graphics || !payload.targets || !payload.timingTicket || !payload.causticResolveTiming)
            return false;
        DiscardGpuTimingMeasure(payload.causticResolveTiming);
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.causticResolveTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_CausticResolve,
            payload.graphics->getDevice(),
            commandList
        );
        payload.raytracingSystem->dispatchGraphCausticGeometryDownsample(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        // The next callback writes the timestamp endpoint on this same primary command list. Close this callback's nested marker now, before the packet recorder advances to the wavelet task marker.
        return Core::FinishSplitGpuTimingMarker(payload.causticResolveTiming);
    }

    static void discarded(Payload& payload){
        DiscardGpuTimingMeasure(payload.causticResolveTiming);
    }
};


// Resolve prepare owns the first half-resolution ping-pong write. The following wavelet body receives its input and output states from graph barriers, while the later alternating passes stay inside the native callback.
struct CausticResolvePrepareGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolvePrepare(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The first wavelet pass consumes the prepare output and writes its counterpart. The next four alternating passes stay in separate graph callbacks, while only the upsample body stays native
struct CausticResolveWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The second wavelet pass consumes the first graph-owned output and returns to the parity-selected surface.
// The next three alternating passes stay in separate graph callbacks, but this exact handoff receives graph-owned entry states.
struct CausticResolveSecondWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveSecondWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The third wavelet pass consumes the second graph-owned output and writes its counterpart.
// The next two alternating passes stay in separate graph callbacks, but this exact handoff receives graph-owned entry states.
struct CausticResolveThirdWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
        CausticResolveActivitySnapshot activity;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.activity.matches(payload.raytracingSystem->causticResolveActivitySnapshot(*payload.targets)))
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveThirdWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The fourth wavelet pass consumes the third graph-owned output and returns to the parity-selected surface.
// The final alternating pass stays in a separate graph callback, but this exact handoff receives graph-owned entry states.
struct CausticResolveFourthWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
        CausticResolveActivitySnapshot activity;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.activity.matches(payload.raytracingSystem->causticResolveActivitySnapshot(*payload.targets)))
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveFourthWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The fifth wavelet pass consumes the fourth graph-owned output and produces the fixed half-B upsample input.
// This final ping-pong handoff receives graph-owned entry states before the graph-owned upsample callback.
struct CausticResolveFifthWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
        CausticResolveActivitySnapshot activity;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.activity.matches(payload.raytracingSystem->causticResolveActivitySnapshot(*payload.targets)))
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveFifthWavelet(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// Upsample consumes the fifth-wavelet output after the compiler has lowered the final ping-pong UAV-to-SRV handoff. The following empty callback only closes the retained full-resolve timing interval.
struct CausticResolveUpsampleGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const bool* causticProducerDispatched = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets)
            return false;
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched)
            return true;
        payload.raytracingSystem->dispatchGraphCausticResolveUpsample(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        );
        return true;
    }
};


// The empty timing-close callback follows graph-owned upsample and finishes the retained full-resolve interval.
struct CausticResolveGraphTask{
    struct Payload{
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* causticProducerDispatched = nullptr;
        Optional<Core::GpuTimingMeasure>* causticResolveTiming = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.timingTicket || !payload.causticResolveTiming)
            return false;
        // Preserve the existing no-producer contract: the graph-owned irradiance clear remains authoritative and no resolve dispatch is emitted when the selected photon producer did not record.
        if(!payload.causticProducerDispatched || !*payload.causticProducerDispatched){
            DiscardGpuTimingMeasure(payload.causticResolveTiming);
            return true;
        }
        if(!payload.causticResolveTiming->has_value())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.causticResolveTiming->value().finishTiming(commandList);
        payload.causticResolveTiming->reset();
        return true;
    }

    static void discarded(Payload& payload){
        DiscardGpuTimingMeasure(payload.causticResolveTiming);
    }
};

// A ping-pong resolve target with sampled and storage slots.
struct CausticResolvePassResources{
    Core::Texture* texture = nullptr;
    u32 sampledSlot = 0u;
    u32 storageSlot = 0u;
};


inline void DispatchCausticResolvePass(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    Core::ComputePipeline& pipeline,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates,
    const CausticResolvePassResources& input,
    const CausticResolvePassResources& output,
    const f32 effectiveIntensity,
    const u32 stepWidth,
    const CausticResolveStage::Enum stage,
    const u32 groupsX,
    const u32 groupsY,
    const CausticResolveActivitySnapshot& activity = {}
){
    NWB_ASSERT(input.texture);
    NWB_ASSERT(output.texture);
    NWB_ASSERT(input.texture != output.texture);
    constexpr u32 s_ActivityStepWidth4 = 4u;
    constexpr u32 s_ActivityStepWidth8 = 8u;
    constexpr u32 s_ActivityStepWidth16 = 16u;
    const bool usesActivity = stage == CausticResolveStage::Wavelet && activity.valid();
    const i32 activityInput = usesActivity ? (stepWidth == s_ActivityStepWidth8 ? 0 : (stepWidth == s_ActivityStepWidth16 ? 1 : -1)) : -1;
    const i32 activityOutput = usesActivity ? (stepWidth == s_ActivityStepWidth4 ? 0 : (stepWidth == s_ActivityStepWidth8 ? 1 : -1)) : -1;
    if(!graphOwnsPassEntryStates){
        // Shared G-buffer reads are graph-declared for normal callers. Compatibility callers retain their original state setup,
        // later ping-pong passes explicitly establish their own dynamic input/output states.
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        commandList.setTextureState(input.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticResolveGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(output.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        if(activityInput >= 0)
            commandList.setBufferState(activity.buffers[activityInput].get(), Core::ResourceStates::ShaderResource);
        if(activityOutput >= 0){
            commandList.setEnableUavBarriersForBuffer(activity.buffers[activityOutput].get(), true);
            commandList.setBufferState(activity.buffers[activityOutput].get(), Core::ResourceStates::UnorderedAccess);
        }
        commandList.commitBarriers();
    }

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;

    CausticResolvePushConstants resolvePush;
    resolvePush.width = targets.width;
    resolvePush.height = targets.height;
    resolvePush.halfWidth = halfWidth;
    resolvePush.halfHeight = halfHeight;
    resolvePush.causticIntensity = effectiveIntensity;
    resolvePush.stepWidth = stepWidth;
    resolvePush.stage = static_cast<u32>(stage);
    resolvePush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
    resolvePush.depthSlot = targets.bindless.gbufferDepth.slot();
    resolvePush.inputColorSlot = input.sampledSlot;
    resolvePush.geometrySlot = targets.bindless.causticResolveGeometry.slot();
    resolvePush.accumulatorSlot = targets.bindless.causticAccumulator.slot();
    resolvePush.outputStorageSlot = output.storageSlot;
    if(activityInput >= 0)
        resolvePush.activityInputSlot = activity.descriptors[activityInput].slot();
    if(activityOutput >= 0)
        resolvePush.activityOutputSlot = activity.descriptors[activityOutput].slot();

    Core::ComputeState computeState;
    computeState.setPipeline(&pipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, pipeline);
    commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
    commandList.dispatch(groupsX, groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

