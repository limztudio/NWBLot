// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>
#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingCausticsTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_HwRaygenExportName = "CausticHwRayGen";
static constexpr AStringView s_HwMissExportName = "CausticHwMiss";
static constexpr AStringView s_HwHitGroupExportName = "CausticHwHitGroup";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Payload>
static void ConfirmCausticAccumulatorClears(Payload& payload)noexcept(
    noexcept(static_cast<b8>(payload.graphOwnsAccumulatorBootstrapClear && payload.causticProducerDispatched))
    && noexcept(payload.raytracingSystem.confirmCausticAccumulatorBootstrapClear())
){
    if(payload.graphOwnsAccumulatorBootstrapClear && payload.causticProducerDispatched)
        payload.raytracingSystem.confirmCausticAccumulatorBootstrapClear();
}

// A warm accumulator decays before either photon route writes splats. Separate task so the compiler lowers the UAV dep into the producer.
struct CausticAccumulatorDecayGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

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
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        static_cast<void>(context);

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

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.causticPhotonTiming);
    }
};

// Caustic producers own typed graph-task payloads; RendererFramePipeline composes their packet chain.
// The renderer still supplies declaration-filtered external state until the graph has every producer in the same frame transaction.
template<b8 Hardware>
struct CausticProducerGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

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
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        static_cast<void>(context);
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

    static void Discarded(Payload& payload){
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
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        static_cast<void>(context);
        Core::DiscardGpuTimingMeasure(&payload.causticResolveTiming);
        if(!payload.causticProducerDispatched)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        payload.causticResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_CausticResolve, payload.graphics.getDevice(), commandList);
        payload.raytracingSystem.dispatchCausticGeometryDownsample(commandList, payload.targets);
        // The next callback ends timestamps on this same list. Close this nested marker before the recorder advances.
        return Core::FinishSplitGpuTimingMarker(&payload.causticResolveTiming);
    }

    static void Discarded(Payload& payload){
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
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context)noexcept(noexcept((payload.raytracingSystem.*Dispatch)(commandList, payload.targets))){
        static_cast<void>(context);
        if(!payload.activity.matches(payload.raytracingSystem.causticResolveActivitySnapshot(payload.targets)))
            return false;
        if(payload.causticProducerDispatched)
            (payload.raytracingSystem.*Dispatch)(commandList, payload.targets);
        return true;
    }
};


template<u32 PassIndex>
struct CausticResolveWaveletGraphTaskDefinition{
    using Payload = CausticResolveRecordInputs;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        static_cast<void>(context);
        if(!payload.activity.matches(payload.raytracingSystem.causticResolveActivitySnapshot(payload.targets)))
            return false;
        if(payload.causticProducerDispatched)
            payload.raytracingSystem.dispatchCausticResolveWaveletPass(commandList, payload.targets, PassIndex);
        return true;
    }
};


using CausticResolvePrepareGraphTask = CausticResolvePhaseGraphTask<&RendererRayTracingSystem::dispatchCausticResolvePrepare>;
using CausticResolveWaveletGraphTask = CausticResolveWaveletGraphTaskDefinition<0u>;
using CausticResolveSecondWaveletGraphTask = CausticResolveWaveletGraphTaskDefinition<1u>;
using CausticResolveThirdWaveletGraphTask = CausticResolveWaveletGraphTaskDefinition<2u>;
using CausticResolveFourthWaveletGraphTask = CausticResolveWaveletGraphTaskDefinition<3u>;
using CausticResolveFifthWaveletGraphTask = CausticResolveWaveletGraphTaskDefinition<4u>;
using CausticResolveUpsampleGraphTask = CausticResolvePhaseGraphTask<&RendererRayTracingSystem::dispatchCausticResolveUpsample>;


// The empty timing-close callback follows graph-owned upsample and finishes the retained full-resolve interval.
struct CausticResolveGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    // All bindings are required: resolve always closes the caustic-resolve timing against this frame's ticket.
    struct Payload{
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& causticProducerDispatched;
        Optional<Core::GpuTimingMeasure>& causticResolveTiming;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        static_cast<void>(context);
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

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.causticResolveTiming);
    }
};

inline void EnableCausticResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
}


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
    // Sequence indices match the graph: prepare, five wavelets, then upsample.
    const u32 activityPassIndex = stage == CausticResolveStage::Wavelet ? static_cast<u32>(CountTrailingZeros(stepWidth)) + 1u : (stage == CausticResolveStage::PrepareDownsample ? 0u : NWB_CAUSTIC_RESOLVE_PASS_COUNT + 1u);
    const b8 usesActivity = activity.valid();
    const i32 activityInput = usesActivity && activityPassIndex > 0u ? static_cast<i32>((activityPassIndex - 1u) % 2u) : -1;
    const i32 activityOutput = usesActivity && activityPassIndex <= NWB_CAUSTIC_RESOLVE_PASS_COUNT ? static_cast<i32>(activityPassIndex % 2u) : -1;

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

