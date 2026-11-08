// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/shadow/soft_shadow_temporal_merge_recorder.h>
#include <impl/ecs_render/shadow/soft_shadow_resolve_dispatch_builder.h>
#include <impl/ecs_render/shadow/software_transparent_shadow_trace_recorder.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const u32 softGroupsX,
    const u32 softGroupsY,
    const b8 dispatchOpaqueGeometry,
    const b8 dispatchOpaqueResolve,
    const b8 dispatchTransparentTrace,
    const b8 dispatchTransparentResolve,
    const b8 graphOwnsOpaqueGeometryToResolveBoundary,
    const b8 graphOwnsOpaqueToTransparentBoundary,
    const b8 graphOwnsTransparentTraceToResolveBoundary,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 graphOwnsTransparentTemporalMergeEntryStates,
    const b8 dispatchOpaqueResolveTail,
    const b8 graphOwnsOpaqueTraceToFirstWaveletBoundary,
    const b8 dispatchTransparentResolveTail,
    const b8 splitTransparentResolve,
    const b8 dispatchTransparentTemporalMerge,
    const SoftShadowOpaqueResolvePhase::Enum opaquePhase
){
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.sceneShading, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftGeometryStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfBStorage, Core::GpuDescriptorClass::StorageImage)
    )
        return;

    const u32 softHalfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
    const u32 softHalfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);

    if(dispatchOpaqueGeometry){
        // Graph declares downsample inputs; later soft-shadow transitions stay local.
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);

        {
            Core::GpuTimingMeasure geometryTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowGeometryDownsample, m_graphics.getDevice(), commandList);

            const ShadowGeometryDownsamplePushConstants geometryPush = MakeShadowGeometryDownsamplePushConstants(
                targets.width,
                targets.height,
                softHalfWidth,
                softHalfHeight,
                targets.bindless.gbufferWorldPosition.slot(),
                targets.bindless.gbufferNormal.slot(),
                targets.bindless.gbufferDepth.slot(),
                targets.bindless.shadowSoftGeometryStorage.slot(),
                targets.bindless.sceneShading.slot(),
                targets.shadowReceiverFactor
            );
            commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_shadowGeometryDownsamplePipeline));
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowGeometryDownsamplePipeline.get());
            commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }
    }

    if(!dispatchOpaqueResolve && !dispatchOpaqueResolveTail && !dispatchTransparentTrace && !dispatchTransparentResolve && !dispatchTransparentResolveTail && !dispatchTransparentTemporalMerge)
        return;

    const u32 slotRangeCount = ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask);
    if(slotRangeCount == 0u)
        return;

    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    const b8 opaqueTemporalActive = m_rayTracingState.m_softShadowTemporalReady;
    const b8 temporalHistoryReadable = softShadowTemporalHistoryUsable();

    SoftShadowTemporalMergeRecorder mergeRecorder(
        m_rayTracingState, slotRangeCount, softHalfWidth, softHalfHeight, softGroupsX, softGroupsY, temporalHistoryReadable,
        commandList, targets, heap
    );

    if(dispatchOpaqueResolve || dispatchOpaqueResolveTail){
        const ShadowReprojectMergeHeapResources opaqueMerge = frontIsA
            ? ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistA.slot(), targets.bindless.shadowMomentsA.slot(),
                targets.bindless.shadowHistBStorage.slot(), targets.bindless.shadowMomentsBStorage.slot()
            }
            : ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistB.slot(), targets.bindless.shadowMomentsB.slot(),
                targets.bindless.shadowHistAStorage.slot(), targets.bindless.shadowMomentsAStorage.slot()
            }
        ;
        // Merge writes the NEXT pair; the wavelet must read that same pair, never stale A.
        if(dispatchOpaqueResolve && opaqueTemporalActive && opaquePhase != SoftShadowOpaqueResolvePhase::WaveletOnly){
            Core::GpuTimingMeasure opaqueTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTemporal, m_graphics.getDevice(), commandList);

            // The graph owns the selected history/moments plus stable previous-geometry/world reads.
            // The geometry downsample above still needs this callback's local UAV-to-SRV transition before the opaque merge samples it.
            mergeRecorder.record(opaqueMerge, graphOwnsOpaqueTraceToFirstWaveletBoundary, graphOwnsOpaqueGeometryToResolveBoundary, graphOwnsOpaqueTemporalMergeEntryStates, graphOwnsOpaqueTemporalMergeEntryStates);
        }

        if(opaquePhase != SoftShadowOpaqueResolvePhase::TemporalOnly){
            // The first wavelet reads this frame's trace or temporal merge directly, preserving its exact filtering input.
            const OpaqueSoftShadowResolveOptions opaqueOptions{
                .frontIsA = frontIsA,
                .temporalActive = opaqueTemporalActive,
                .graphOwnsGeometryEntry = graphOwnsOpaqueGeometryToResolveBoundary,
                .graphOwnsTraceToFirstWavelet = graphOwnsOpaqueTraceToFirstWaveletBoundary,
                .dispatchResolve = dispatchOpaqueResolve,
                .dispatchResolveTail = dispatchOpaqueResolveTail,
                .phase = opaquePhase,
            };
            const SoftShadowResolveDispatch opaqueDispatch = SoftShadowResolveDispatchBuilder::Opaque(opaqueOptions, m_rayTracingState, targets);
            if(dispatchOpaqueResolve && dispatchOpaqueResolveTail){
                Core::GpuTimingMeasure opaqueResolveTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueResolve, m_graphics.getDevice(), commandList);

                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch);
            }
            else if(dispatchOpaqueResolve)
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch, true, false);
            else
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, opaqueDispatch, false, true);
        }
    }

    if(dispatchTransparentTrace && m_rayTracingState.m_softTransparentReady){
        {
            Core::GpuTimingMeasure transparentTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTrace, m_graphics.getDevice(), commandList);

            if(hardwareTransparentShadowReady())
                dispatchHardwareTransparentShadow(commandList, targets, frameIndex);
            else{
                const SoftwareTransparentShadowTraceInputs traceInputs{
                    .frameIndex = frameIndex,
                    .sampleCount = transparentShadowSampleCount(),
                    .groupsX = softGroupsX,
                    .groupsY = softGroupsY,
                    .graphOwnsOpaqueToTransparentBoundary = graphOwnsOpaqueToTransparentBoundary,
                    .temporalHistoryReadable = temporalHistoryReadable,
                    .historyFrontIsA = frontIsA,
                };
                SoftwareTransparentShadowTraceRecorder::Record(traceInputs, m_rayTracingState, commandList, targets, heap);
                reportTransparentShadowSampling(traceInputs.sampleCount);
            }
        }
    }

    const b8 dispatchTransparentWavelet = dispatchTransparentResolve || dispatchTransparentResolveTail;
    const b8 dispatchTransparentMerge = dispatchTransparentTemporalMerge || (dispatchTransparentResolve && !splitTransparentResolve);
    if((dispatchTransparentMerge || dispatchTransparentWavelet) && m_rayTracingState.m_softTransparentReady){
        const b8 transparentTemporalActive = m_rayTracingState.m_softTransparentTemporalReady;
        const ShadowReprojectMergeHeapResources transparentMerge = frontIsA
            ? ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistA.slot(), targets.bindless.transparentMomentsA.slot(),
                targets.bindless.transparentHistBStorage.slot(), targets.bindless.transparentMomentsBStorage.slot()
            }
            : ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistB.slot(), targets.bindless.transparentMomentsB.slot(),
                targets.bindless.transparentHistAStorage.slot(), targets.bindless.transparentMomentsAStorage.slot()
            }
        ;
        // Keep the RGB wavelet's temporal variance paired with the history just emitted by the transparent merge.
        if(dispatchTransparentMerge && transparentTemporalActive){
            Core::GpuTimingMeasure transparentTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTemporal, m_graphics.getDevice(), commandList);

            mergeRecorder.record(transparentMerge, graphOwnsTransparentTraceToResolveBoundary, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates);
            m_rayTracingState.m_transparentShadowSamplingHistory.record(m_rayTracingState.m_softShadowSlotMask);
        }

        if(dispatchTransparentWavelet){
            const TransparentSoftShadowResolveOptions transparentOptions{
                .frontIsA = frontIsA,
                .temporalActive = transparentTemporalActive,
                .dispatchResolve = dispatchTransparentResolve,
                .dispatchResolveTail = dispatchTransparentResolveTail,
                .splitResolve = splitTransparentResolve,
                .dispatchTemporalMerge = dispatchTransparentTemporalMerge,
                .graphOwnsTraceToResolve = graphOwnsTransparentTraceToResolveBoundary,
            };
            const SoftShadowResolveDispatch transparentDispatch = SoftShadowResolveDispatchBuilder::Transparent(transparentOptions, m_rayTracingState, targets);
            if(dispatchTransparentResolve && !splitTransparentResolve){
                Core::GpuTimingMeasure transparentResolveTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentResolve, m_graphics.getDevice(), commandList);

                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch);
            }
            else if(dispatchTransparentResolve)
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch, true, false);
            else
                dispatchSoftShadowResolve(commandList, targets, deferredLightingResources, 0u, slotRangeCount, transparentDispatch, false, true);
        }
    }

    // Do not mutate the target-generation handles while the sibling caustics and surfel-GI workers can still validate targets.bindless.
    // RendererFramePipeline finalizes this pending CPU-side swap only after its complete ordered Graphics submission succeeds.
    if(((dispatchTransparentResolve && !splitTransparentResolve) || dispatchTransparentResolveTail) && m_rayTracingState.m_softShadowTemporalReady)
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::softShadowTemporalHistoryUsable()const noexcept{
    return SoftShadowTemporalHistoryUsable(m_rayTracingState.m_softShadowTemporalReady, m_rayTracingState.m_prevWorldToClipValid, m_rayTracingState.m_softShadowTemporalSeeded);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

