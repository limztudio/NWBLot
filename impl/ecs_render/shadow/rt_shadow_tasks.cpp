// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_shadow_tasks.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingShadowVisibilityTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;

    payload.opaqueProduced = false;
    payload.opaqueFrameIndex = 0u;
    // A retry must never retain a timestamp whose producer command list is about to be replaced.
    if(payload.shadowVisibilityTiming.has_value()){
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
    }
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().discardTiming();
        payload.asyncTiming.reset();
    }

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(queue->queueClass == Core::CommandQueue::Compute){
        payload.asyncTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncShadow, payload.graphics.getDevice(), commandList);
    }
    payload.shadowVisibilityTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, payload.graphics.getDevice(), commandList);

    b8 opaqueRecorded = false;
    if(payload.prepared){
        const auto opaqueFrame = payload.hardwareShadowSupported
            ? payload.raytracingSystem.renderShadowVisibilityOpaque(commandList, payload.targets, payload.deferredLightingResources, payload.graphOwnsOpaqueTemporalMergeEntryStates, &payload.lightSpace)
            : payload.raytracingSystem.renderGpuBvhShadowVisibilityOpaque(commandList, payload.targets, payload.deferredLightingResources, payload.graphOwnsOpaqueTemporalMergeEntryStates, &payload.lightSpace)
        ;
        opaqueRecorded = opaqueFrame.has_value();
        if(opaqueFrame)
            payload.opaqueFrameIndex = *opaqueFrame;
    }
    if(!opaqueRecorded){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow producer failed; retaining all-lit visibility"));
        payload.raytracingSystem.clearShadowVisibility(commandList, payload.targets);
        // Later callbacks declare this output as UAV. Realign the tracker after the typed fallback clear.
        commandList.setTextureState(payload.targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
        if(payload.asyncTiming.has_value()){
            payload.asyncTiming.value().discardTiming();
            payload.asyncTiming.reset();
        }
        return true;
    }

    payload.opaqueProduced = true;
    // Both timestamp ranges end in the terminal fold callback, but their debug markers must close before this graph task's marker closes.
    const b8 visibilityMarkerFinished = Core::FinishSplitGpuTimingMarker(&payload.shadowVisibilityTiming);
    b8 asyncMarkerFinished = true;
    if(payload.asyncTiming.has_value())
        asyncMarkerFinished = Core::FinishSplitGpuTimingMarker(&payload.asyncTiming);
    if(!asyncMarkerFinished || !visibilityMarkerFinished){
        Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
        Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
        return false;
    }
    return true;
}

void ShadowVisibilityOpaqueGraphTask::Discarded(Payload& payload)noexcept{
    payload.opaqueProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueFirstWaveletGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.opaqueResolveTiming.has_value()){
        payload.opaqueResolveTiming.value().discardTiming();
        payload.opaqueResolveTiming.reset();
    }
    payload.opaqueResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueResolve, payload.graphics.getDevice(), commandList);
    if(payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(
        commandList,
        payload.targets,
        payload.deferredLightingResources,
        payload.opaqueFrameIndex,
        payload.hardwareShadowSupported,
        payload.graphOwnsOpaqueTemporalMergeEntryStates,
        payload.deferWavelet ? SoftShadowOpaqueResolvePhase::TemporalOnly : SoftShadowOpaqueResolvePhase::TemporalAndWavelet
    )){
        if(payload.deferUpsample){
            payload.opaqueResolveTiming.value().finishTiming(commandList);
            payload.opaqueResolveTiming.reset();
            return true;
        }
        return Core::FinishSplitGpuTimingMarker(&payload.opaqueResolveTiming);
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow first wavelet failed; retaining all-lit visibility"));
    RayTracingShadowVisibilityTaskDetail::RecoverOpaqueSoftShadowAllLit(payload, commandList);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityOpaqueResolveTailGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(!payload.opaqueResolveTiming.has_value())
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.raytracingSystem.renderSoftOpaqueShadowResolveTail(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.hardwareShadowSupported)){
        payload.opaqueResolveTiming.value().finishTiming(commandList);
        payload.opaqueResolveTiming.reset();
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split opaque soft-shadow resolve tail failed; retaining all-lit visibility"));
    RayTracingShadowVisibilityTaskDetail::RecoverOpaqueSoftShadowAllLit(payload, commandList);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftTraceGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    payload.transparentTraceProduced = false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.raytracingSystem.renderSoftTransparentShadowTrace(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, true, &payload.lightSpace)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace could not record; preserving opaque visibility"));
        return true;
    }
    payload.transparentTraceProduced = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftTemporalMergeGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    const auto recoverOpaqueTemporal = [&](){
        if(!payload.combinedTemporal)
            return true;
        return payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.hardwareShadowSupported, true, SoftShadowOpaqueResolvePhase::TemporalOnly);
    };
    if(!payload.transparentTraceProduced)
        return recoverOpaqueTemporal();
    if(payload.transparentResolveTiming.has_value()){
        payload.transparentResolveTiming.value().discardTiming();
        payload.transparentResolveTiming.reset();
    }
    payload.transparentResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, payload.graphics.getDevice(), commandList);
    const b8 merged = payload.combinedTemporal
        ? payload.raytracingSystem.renderSoftShadowCombinedTemporalMerge(commandList, payload.targets)
        : payload.raytracingSystem.renderSoftTransparentShadowTemporalMerge(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.graphOwnsTransparentTemporalMergeEntryStates)
    ;
    if(merged){
        return Core::FinishSplitGpuTimingMarker(&payload.transparentResolveTiming);
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow temporal merge failed; preserving opaque visibility"));
    payload.transparentTraceProduced = false;
    payload.transparentResolveTiming.value().discardTiming();
    payload.transparentResolveTiming.reset();
    return recoverOpaqueTemporal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftFirstWaveletGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(payload.combinedWavelet){
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(payload.transparentTraceProduced && payload.transparentResolveTiming.has_value()){
            if(payload.raytracingSystem.renderSoftShadowCombinedWavelet(commandList, payload.targets))
                return true;
        }
        // Opaque temporal merge already completed. Recover only its first wavelet, without blending history twice.
        payload.transparentTraceProduced = false;
        Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
        return payload.raytracingSystem.renderSoftOpaqueShadowResolvePhase(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, true, true, SoftShadowOpaqueResolvePhase::WaveletOnly);
    }
    if(!payload.transparentTraceProduced)
        return true;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.startsTransparentResolveTiming){
        if(payload.transparentResolveTiming.has_value()){
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
        payload.transparentResolveTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, payload.graphics.getDevice(), commandList);
    }
    else if(!payload.transparentResolveTiming.has_value()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope; preserving opaque visibility"));
        payload.transparentTraceProduced = false;
        return true;
    }

    if(payload.raytracingSystem.renderSoftTransparentShadowFirstWavelet(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex, payload.graphOwnsTransparentWaveletInputBoundary)){
        if(payload.startsTransparentResolveTiming && !Core::FinishSplitGpuTimingMarker(&payload.transparentResolveTiming))
            return false;
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow first wavelet failed; preserving opaque visibility"));
    payload.transparentTraceProduced = false;
    if(payload.transparentResolveTiming.has_value()){
        payload.transparentResolveTiming.value().discardTiming();
        payload.transparentResolveTiming.reset();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowTransparentSoftFoldGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;
    if(!payload.opaqueProduced)
        return true;
    if(!payload.shadowVisibilityTiming.has_value() || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value()))
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.transparentTraceProduced){
        if(!payload.transparentResolveTiming.has_value())
            return false;
        const b8 resolved = payload.combinedUpsample
            ? payload.raytracingSystem.renderSoftShadowTerminalUpsample(commandList, payload.targets, payload.deferredLightingResources, true)
            : payload.raytracingSystem.renderSoftTransparentShadowFold(commandList, payload.targets, payload.deferredLightingResources, payload.opaqueFrameIndex)
        ;
        if(resolved){
            payload.transparentResolveTiming.value().finishTiming(commandList);
            payload.transparentResolveTiming.reset();
        }
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow resolve failed; preserving opaque visibility"));
            payload.transparentTraceProduced = false;
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
    }else{
        if(payload.transparentResolveTiming.has_value()){
            payload.transparentResolveTiming.value().discardTiming();
            payload.transparentResolveTiming.reset();
        }
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: split transparent soft-shadow trace or first wavelet failed; preserving opaque visibility"));
    }

    // Fusion deferred the opaque upsample, so a failed transparent phase must still publish opaque visibility.
    if(payload.combinedUpsample && !payload.transparentTraceProduced){
        if(!payload.raytracingSystem.renderSoftShadowTerminalUpsample(commandList, payload.targets, payload.deferredLightingResources, false))
            return false;
    }

    payload.shadowVisibilityTiming.value().finishTiming(commandList);
    payload.shadowVisibilityTiming.reset();
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().finishTiming(commandList);
        payload.asyncTiming.reset();
    }
    return true;
}

void ShadowTransparentSoftFoldGraphTask::Discarded(Payload& payload)noexcept{
    payload.transparentTraceProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
    payload.raytracingSystem.discardSoftShadowTemporalHistory();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowVisibilityGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.deferredLightingResources.valid())
        return false;

    const GraphOwnedAdaptiveShadowPlan& adaptivePlan = payload.graphOwnedAdaptivePlan;
    const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan = adaptivePlan.enabled ? &adaptivePlan : nullptr;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    Optional<Core::GpuTimingMeasure> asyncTiming;
    if(queue->queueClass == Core::CommandQueue::Compute){
        asyncTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncShadow, payload.graphics.getDevice(), commandList);
    }

    b8 shadowVisibilityWritten = false;
    if(payload.prepared && payload.hardwareShadowSupported){
        shadowVisibilityWritten = payload.raytracingSystem.renderShadowVisibility(commandList, payload.targets, payload.deferredLightingResources).has_value();
        if(!shadowVisibilityWritten)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: ray-traced shadow visibility pass failed"));
    }
    else if(payload.prepared){
        shadowVisibilityWritten = payload.raytracingSystem.renderGpuBvhShadowVisibility(commandList, payload.targets, payload.deferredLightingResources, false, false, false, graphOwnedAdaptivePlan).has_value();
    }
    // The preceding typed white clear remains the all-lit result when no producer records.

    if(asyncTiming){
        asyncTiming->finishTiming(commandList);
        asyncTiming.reset();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

