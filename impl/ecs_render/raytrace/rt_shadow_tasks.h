// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingShadowVisibilityTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A prepared soft-transparent frame records opaque production, opaque resolve, transparent trace, and terminal
// fold as one native packet. The shared state is stack-owned by the renderer for this graph transaction; it never
// survives acceptance or a retry.
struct ShadowVisibilityOpaqueGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        LightSpaceShadowSnapshot lightSpace;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming = nullptr;
        const bool* prepared = nullptr;
        bool* opaqueProduced = nullptr;
        u32* opaqueFrameIndex = nullptr;
        bool hardwareShadowSupported = false;
        bool graphEntryStatesOwned = false;
        bool graphOwnsOpaqueTemporalMergeEntryStates = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.graphics
            || !payload.targets
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.shadowVisibilityTiming
            || !payload.opaqueProduced
            || !payload.opaqueFrameIndex
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(!queue)
            return false;

        *payload.opaqueProduced = false;
        *payload.opaqueFrameIndex = 0u;
        // A retry must never retain a timestamp whose producer command list is about to be replaced.
        if(payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }
        if(payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(queue->queueClass == Core::CommandQueue::Compute){
            payload.asyncTiming->emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncShadow,
                payload.graphics->getDevice(),
                commandList
            );
        }
        payload.shadowVisibilityTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_ShadowVisibility,
            payload.graphics->getDevice(),
            commandList
        );

        bool opaqueRecorded = false;
        if(payload.prepared && *payload.prepared){
            opaqueRecorded = payload.hardwareShadowSupported
                ? payload.raytracingSystem->renderShadowVisibilityOpaque(
                    commandList,
                    *payload.targets,
                    payload.deferredLightingResources,
                    *payload.opaqueFrameIndex,
                    payload.graphEntryStatesOwned,
                    payload.graphOwnsOpaqueTemporalMergeEntryStates
                )
                : payload.raytracingSystem->renderGpuBvhShadowVisibilityOpaque(
                    commandList,
                    *payload.targets,
                    payload.deferredLightingResources,
                    *payload.opaqueFrameIndex,
                    payload.graphEntryStatesOwned,
                    payload.graphOwnsOpaqueTemporalMergeEntryStates,
                    &payload.lightSpace
                )
            ;
        }
        if(!opaqueRecorded){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow producer failed; retaining all-lit visibility"));
            payload.raytracingSystem->clearShadowVisibility(commandList, *payload.targets);
            // The following graph callbacks declare the output as UAV. Keep the command-list tracker aligned with
            // their declared no-op handoffs even when this fallback only recorded a typed clear.
            commandList.setTextureState(
                payload.targets->shadowVisibility.get(),
                ECSRenderDetail::s_ShadowVisibilitySubresources,
                Core::ResourceStates::UnorderedAccess
            );
            commandList.commitBarriers();
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            return true;
        }

        *payload.opaqueProduced = true;
        // Both timestamp ranges end in the terminal fold callback, but their debug markers must close before this
        // graph task's marker closes.
        const bool visibilityMarkerFinished = Core::FinishSplitGpuTimingMarker(payload.shadowVisibilityTiming);
        bool asyncMarkerFinished = true;
        if(payload.asyncTiming->has_value())
            asyncMarkerFinished = Core::FinishSplitGpuTimingMarker(payload.asyncTiming);
        if(!asyncMarkerFinished || !visibilityMarkerFinished){
            Core::DiscardGpuTimingMeasure(payload.asyncTiming);
            Core::DiscardGpuTimingMeasure(payload.shadowVisibilityTiming);
            return false;
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.opaqueProduced)
            *payload.opaqueProduced = false;
        Core::DiscardGpuTimingMeasure(payload.shadowVisibilityTiming);
        Core::DiscardGpuTimingMeasure(payload.asyncTiming);
    }
};


// The opaque producer leaves the trace and current geometry scratch in their declared states. This adjacent task
// owns their sampled entry before temporal merge and, unless fused later, the first wavelet. Its tail retains dynamic ping-pong and
// upsample work, while the terminal transparent fold remains the output/acceptance owner.
struct ShadowVisibilityOpaqueFirstWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* opaqueResolveTiming = nullptr;
        bool* opaqueProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool hardwareShadowSupported = false;
        bool graphEntryStatesOwned = false;
        bool graphOwnsOpaqueTemporalMergeEntryStates = false;
        bool deferUpsample = false;
        bool deferWavelet = false;
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
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.shadowVisibilityTiming
            || !payload.opaqueResolveTiming
            || !payload.opaqueProduced
            || !payload.opaqueFrameIndex
        )
            return false;
        if(!*payload.opaqueProduced)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.opaqueResolveTiming->has_value()){
            payload.opaqueResolveTiming->value().discardTiming();
            payload.opaqueResolveTiming->reset();
        }
        payload.opaqueResolveTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_ShadowOpaqueResolve,
            payload.graphics->getDevice(),
            commandList
        );
        if(payload.raytracingSystem->renderSoftOpaqueShadowResolvePhase(
            commandList,
            *payload.targets,
            payload.deferredLightingResources,
            *payload.opaqueFrameIndex,
            payload.hardwareShadowSupported,
            payload.graphEntryStatesOwned,
            payload.graphOwnsOpaqueTemporalMergeEntryStates,
            payload.deferWavelet ? SoftShadowOpaqueResolvePhase::TemporalOnly : SoftShadowOpaqueResolvePhase::TemporalAndWavelet
        )){
            if(payload.deferUpsample){
                payload.opaqueResolveTiming->value().finishTiming(commandList);
                payload.opaqueResolveTiming->reset();
                return true;
            }
            return Core::FinishSplitGpuTimingMarker(payload.opaqueResolveTiming);
        }

        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow first wavelet failed; retaining all-lit visibility"));
        payload.raytracingSystem->clearShadowVisibility(commandList, *payload.targets);
        // The skipped transparent callbacks still declare this output as UAV. Keep the native state tracker aligned
        // with their no-op graph handoff after the typed fallback clear.
        commandList.setTextureState(
            payload.targets->shadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::ResourceStates::UnorderedAccess
        );
        commandList.commitBarriers();
        *payload.opaqueProduced = false;
        if(payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
        if(payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }
        payload.opaqueResolveTiming->value().discardTiming();
        payload.opaqueResolveTiming->reset();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.opaqueProduced)
            *payload.opaqueProduced = false;
        Core::DiscardGpuTimingMeasure(payload.asyncTiming);
        Core::DiscardGpuTimingMeasure(payload.shadowVisibilityTiming);
        Core::DiscardGpuTimingMeasure(payload.opaqueResolveTiming);
    }
};


struct ShadowVisibilityOpaqueResolveTailGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* opaqueResolveTiming = nullptr;
        bool* opaqueProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool hardwareShadowSupported = false;
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
            || !payload.targets
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.shadowVisibilityTiming
            || !payload.opaqueResolveTiming
            || !payload.opaqueProduced
            || !payload.opaqueFrameIndex
        )
            return false;
        if(!*payload.opaqueProduced)
            return true;
        if(!payload.opaqueResolveTiming->has_value())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.raytracingSystem->renderSoftOpaqueShadowResolveTail(
            commandList,
            *payload.targets,
            payload.deferredLightingResources,
            *payload.opaqueFrameIndex,
            payload.hardwareShadowSupported,
            payload.graphEntryStatesOwned
        )){
            payload.opaqueResolveTiming->value().finishTiming(commandList);
            payload.opaqueResolveTiming->reset();
            return true;
        }

        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow resolve tail failed; retaining all-lit visibility"));
        payload.raytracingSystem->clearShadowVisibility(commandList, *payload.targets);
        commandList.setTextureState(
            payload.targets->shadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::ResourceStates::UnorderedAccess
        );
        commandList.commitBarriers();
        *payload.opaqueProduced = false;
        if(payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
        if(payload.shadowVisibilityTiming->has_value()){
            payload.shadowVisibilityTiming->value().discardTiming();
            payload.shadowVisibilityTiming->reset();
        }
        payload.opaqueResolveTiming->value().discardTiming();
        payload.opaqueResolveTiming->reset();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.opaqueProduced)
            *payload.opaqueProduced = false;
        Core::DiscardGpuTimingMeasure(payload.asyncTiming);
        Core::DiscardGpuTimingMeasure(payload.shadowVisibilityTiming);
        Core::DiscardGpuTimingMeasure(payload.opaqueResolveTiming);
    }
};


// The prepared path keeps trace and resolve as separate callbacks so the graph lowers the transparent half output
// from UAV to shader-read between them. The terminal resolve task retains the legacy timing and acceptance owner.
struct ShadowTransparentSoftTraceGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        LightSpaceShadowSnapshot lightSpace;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* opaqueProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool* transparentTraceProduced = nullptr;
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
            || !payload.targets
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
            || !payload.opaqueProduced
            || !payload.opaqueFrameIndex
            || !payload.transparentTraceProduced
        )
            return false;

        *payload.transparentTraceProduced = false;
        if(!*payload.opaqueProduced)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.raytracingSystem->renderSoftTransparentShadowTrace(
            commandList,
            *payload.targets,
            payload.deferredLightingResources,
            *payload.opaqueFrameIndex,
            payload.graphEntryStatesOwned,
            true,
            &payload.lightSpace
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace could not record; preserving opaque visibility"));
            return true;
        }
        *payload.transparentTraceProduced = true;
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentTraceProduced)
            *payload.transparentTraceProduced = false;
    }
};


// The transparent trace publishes RGB half-resolution visibility. On temporal frames this task freezes the selected
// history pair, publishes the next pair for the wavelet, and starts the resolve timing envelope.
struct ShadowTransparentSoftTemporalMergeGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* transparentResolveTiming = nullptr;
        const bool* opaqueProduced = nullptr;
        bool* transparentTraceProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsTransparentTemporalMergeEntryStates = false;
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
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
            || !payload.transparentResolveTiming
            || !payload.opaqueProduced
            || !payload.transparentTraceProduced
            || !payload.opaqueFrameIndex
        )
            return false;
        if(!*payload.opaqueProduced || !*payload.transparentTraceProduced)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.transparentResolveTiming->has_value()){
            payload.transparentResolveTiming->value().discardTiming();
            payload.transparentResolveTiming->reset();
        }
        payload.transparentResolveTiming->emplace(
            payload.graphics->gpuTiming(),
            RendererGpuTimingScope::s_ShadowTransparentResolve,
            payload.graphics->getDevice(),
            commandList
        );
        if(payload.raytracingSystem->renderSoftTransparentShadowTemporalMerge(
            commandList,
            *payload.targets,
            payload.deferredLightingResources,
            *payload.opaqueFrameIndex,
            payload.graphEntryStatesOwned,
            payload.graphOwnsTransparentTemporalMergeEntryStates
        )){
            return Core::FinishSplitGpuTimingMarker(payload.transparentResolveTiming);
        }

        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow temporal merge failed; preserving opaque visibility"));
        *payload.transparentTraceProduced = false;
        payload.transparentResolveTiming->value().discardTiming();
        payload.transparentResolveTiming->reset();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentTraceProduced)
            *payload.transparentTraceProduced = false;
        Core::DiscardGpuTimingMeasure(payload.transparentResolveTiming);
    }
};


// The transparent trace or temporal merge publishes the first RGB wavelet input. The terminal fold keeps the final
// upsample plus the established output/acceptance endpoint.
struct ShadowTransparentSoftFirstWaveletGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* transparentResolveTiming = nullptr;
        const bool* opaqueProduced = nullptr;
        bool* transparentTraceProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsTransparentWaveletInputBoundary = false;
        bool startsTransparentResolveTiming = true;
        bool combinedWavelet = false;
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
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
            || !payload.transparentResolveTiming
            || !payload.opaqueProduced
            || !payload.transparentTraceProduced
            || !payload.opaqueFrameIndex
        )
            return false;
        if(!*payload.opaqueProduced)
            return true;
        if(payload.combinedWavelet){
            Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
            if(*payload.transparentTraceProduced && payload.transparentResolveTiming->has_value()){
                if(payload.raytracingSystem->renderSoftShadowCombinedWavelet(commandList, *payload.targets))
                    return true;
            }
            // Opaque temporal merge already completed. Recover only its first wavelet, without blending history twice.
            *payload.transparentTraceProduced = false;
            Core::DiscardGpuTimingMeasure(payload.transparentResolveTiming);
            return payload.raytracingSystem->renderSoftOpaqueShadowResolvePhase(
                commandList,
                *payload.targets,
                payload.deferredLightingResources,
                *payload.opaqueFrameIndex,
                true,
                payload.graphEntryStatesOwned,
                true,
                SoftShadowOpaqueResolvePhase::WaveletOnly
            );
        }
        if(!*payload.transparentTraceProduced)
            return true;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.startsTransparentResolveTiming){
            if(payload.transparentResolveTiming->has_value()){
                payload.transparentResolveTiming->value().discardTiming();
                payload.transparentResolveTiming->reset();
            }
            payload.transparentResolveTiming->emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_ShadowTransparentResolve,
                payload.graphics->getDevice(),
                commandList
            );
        }
        else if(!payload.transparentResolveTiming->has_value()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope; preserving opaque visibility"));
            *payload.transparentTraceProduced = false;
            return true;
        }

        if(payload.raytracingSystem->renderSoftTransparentShadowFirstWavelet(
            commandList,
            *payload.targets,
            payload.deferredLightingResources,
            *payload.opaqueFrameIndex,
            payload.graphEntryStatesOwned,
            payload.graphOwnsTransparentWaveletInputBoundary
        )){
            if(payload.startsTransparentResolveTiming && !Core::FinishSplitGpuTimingMarker(payload.transparentResolveTiming))
                return false;
            return true;
        }

        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow first wavelet failed; preserving opaque visibility"));
        *payload.transparentTraceProduced = false;
        if(payload.transparentResolveTiming->has_value()){
            payload.transparentResolveTiming->value().discardTiming();
            payload.transparentResolveTiming->reset();
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentTraceProduced)
            *payload.transparentTraceProduced = false;
        Core::DiscardGpuTimingMeasure(payload.transparentResolveTiming);
    }
};


struct ShadowTransparentSoftFoldGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* shadowVisibilityTiming = nullptr;
        Optional<Core::GpuTimingMeasure>* transparentResolveTiming = nullptr;
        const bool* opaqueProduced = nullptr;
        bool* transparentTraceProduced = nullptr;
        const u32* opaqueFrameIndex = nullptr;
        bool graphEntryStatesOwned = false;
        bool combinedUpsample = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
            || !payload.asyncTiming
            || !payload.shadowVisibilityTiming
            || !payload.transparentResolveTiming
            || !payload.opaqueProduced
            || !payload.transparentTraceProduced
            || !payload.opaqueFrameIndex
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(!queue)
            return false;
        if(!*payload.opaqueProduced)
            return true;
        if(
            !payload.shadowVisibilityTiming->has_value()
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming->has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(*payload.transparentTraceProduced){
            if(!payload.transparentResolveTiming->has_value())
                return false;
            const bool resolved = payload.combinedUpsample
                ? payload.raytracingSystem->renderSoftShadowTerminalUpsample(
                    commandList, *payload.targets, payload.deferredLightingResources, true, payload.graphEntryStatesOwned
                )
                : payload.raytracingSystem->renderSoftTransparentShadowFold(
                    commandList, *payload.targets, payload.deferredLightingResources, *payload.opaqueFrameIndex,
                    payload.graphEntryStatesOwned
                )
            ;
            if(resolved){
                payload.transparentResolveTiming->value().finishTiming(commandList);
                payload.transparentResolveTiming->reset();
            }
            else{
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow resolve failed; preserving opaque visibility"));
                *payload.transparentTraceProduced = false;
                payload.transparentResolveTiming->value().discardTiming();
                payload.transparentResolveTiming->reset();
            }
        }else{
            if(payload.transparentResolveTiming->has_value()){
                payload.transparentResolveTiming->value().discardTiming();
                payload.transparentResolveTiming->reset();
            }
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace or first wavelet failed; preserving opaque visibility"));
        }

        // Fusion deferred the opaque upsample, so a failed transparent phase must still publish opaque visibility.
        if(payload.combinedUpsample && !*payload.transparentTraceProduced){
            if(!payload.raytracingSystem->renderSoftShadowTerminalUpsample(
                commandList, *payload.targets, payload.deferredLightingResources, false, payload.graphEntryStatesOwned
            ))
                return false;
        }

        payload.shadowVisibilityTiming->value().finishTiming(commandList);
        payload.shadowVisibilityTiming->reset();
        if(payload.asyncTiming->has_value()){
            payload.asyncTiming->value().finishTiming(commandList);
            payload.asyncTiming->reset();
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.transparentTraceProduced)
            *payload.transparentTraceProduced = false;
        Core::DiscardGpuTimingMeasure(payload.asyncTiming);
        Core::DiscardGpuTimingMeasure(payload.shadowVisibilityTiming);
        Core::DiscardGpuTimingMeasure(payload.transparentResolveTiming);
        if(payload.raytracingSystem)
            payload.raytracingSystem->discardSoftShadowTemporalHistory();
    }
};


// Shadow visibility owns its graph task; pipeline composes the caustics successor.
struct ShadowVisibilityGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const bool* prepared = nullptr;
        bool hardwareShadowSupported = false;
        bool graphEntryStatesOwned = false;
        bool graphOwnsAllLitVisibilityClear = false;
        GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan;
        mutable bool adaptiveRouteRecorded = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        payload.adaptiveRouteRecorded = false;
        if(
            !payload.raytracingSystem
            || !payload.graphics
            || !payload.targets
            || !payload.deferredLightingResources.valid()
            || !payload.timingTicket
        )
            return false;

        GraphOwnedAdaptiveShadowPlan adaptivePlan = payload.graphOwnedAdaptivePlan;
        adaptivePlan.adaptiveRouteRecorded = &payload.adaptiveRouteRecorded;
        const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan =
            adaptivePlan.enabled ? &adaptivePlan : nullptr
        ;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(!queue)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Optional<Core::GpuTimingMeasure> asyncTiming;
        if(queue->queueClass == Core::CommandQueue::Compute){
            asyncTiming.emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncShadow,
                payload.graphics->getDevice(),
                commandList
            );
        }

        bool shadowVisibilityWritten = false;
        if(payload.prepared && *payload.prepared && payload.hardwareShadowSupported){
            shadowVisibilityWritten = payload.raytracingSystem->renderShadowVisibility(
                commandList,
                *payload.targets,
                payload.deferredLightingResources,
                payload.graphEntryStatesOwned
            );
            if(!shadowVisibilityWritten)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
        }
        else if(payload.prepared && *payload.prepared){
            shadowVisibilityWritten = payload.raytracingSystem->renderGpuBvhShadowVisibility(
                commandList,
                *payload.targets,
                payload.deferredLightingResources,
                payload.graphEntryStatesOwned,
                false,
                nullptr,
                false,
                false,
                graphOwnedAdaptivePlan
            );
        }
        // The retained monolithic graph path always records a typed white clear immediately before this callback.
        // A producer overwrites it; a no-producer/preflight-failure path leaves its all-lit result intact. Direct
        // compatibility callers and split soft-shadow fallback callbacks retain their local native clear.
        if(!shadowVisibilityWritten && !payload.graphOwnsAllLitVisibilityClear)
            payload.raytracingSystem->clearShadowVisibility(commandList, *payload.targets);

        if(asyncTiming){
            asyncTiming->finishTiming(commandList);
            asyncTiming.reset();
        }
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(!payload.raytracingSystem)
            return;
        if(payload.graphOwnedAdaptivePlan.enabled){
            payload.raytracingSystem->confirmGraphOwnedAdaptiveShadowSubmission(
                payload.graphOwnedAdaptivePlan,
                payload.adaptiveRouteRecorded,
                token
            );
        }
    }

    static void discarded(Payload& payload){
        payload.adaptiveRouteRecorded = false;
        if(payload.raytracingSystem)
            payload.raytracingSystem->discardSoftShadowTemporalHistory();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

