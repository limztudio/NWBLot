// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_softshadow_dispatch{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowReprojectMergeHeapResources{
    Core::Texture* softTrace = nullptr;
    Core::Texture* historyIn = nullptr;
    Core::Texture* momentsIn = nullptr;
    Core::Texture* historyOut = nullptr;
    Core::Texture* momentsOut = nullptr;
    u32 softTraceSlot = 0u;
    u32 historyInSlot = 0u;
    u32 momentsInSlot = 0u;
    u32 historyOutStorageSlot = 0u;
    u32 momentsOutStorageSlot = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    const u32 historyValid = temporalHistoryReadable ? 1u : 0u;

    const auto dispatchMerge = [&](
        const __hidden_rt_softshadow_dispatch::ShadowReprojectMergeHeapResources& resources, const b8 graphOwnsSoftTraceInputState, const b8 graphOwnsMergeCurrentGeometryEntryState, const b8 graphOwnsMergeStaticReadEntryStates,
        const b8 graphOwnsMergeTemporalEntryStates
    ){
        NWB_ASSERT(resources.softTrace && resources.historyIn && resources.momentsIn && resources.historyOut && resources.momentsOut);
        if(!graphOwnsSoftTraceInputState)
            commandList.setTextureState(resources.softTrace, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        if(!graphOwnsMergeTemporalEntryStates){
            if(temporalHistoryReadable){
                commandList.setTextureState(resources.historyIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                commandList.setTextureState(resources.momentsIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
            }
            commandList.setTextureState(resources.historyOut, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(resources.momentsOut, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        }
        if(!graphOwnsMergeCurrentGeometryEntryState)
            commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        if(!graphOwnsMergeStaticReadEntryStates){
            if(temporalHistoryReadable)
                commandList.setTextureState(targets.shadowSoftGeometryPrev.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        commandList.setEnableUavBarriersForTexture(resources.historyOut, true);
        commandList.setEnableUavBarriersForTexture(resources.momentsOut, true);
        commandList.commitBarriers();

        const ShadowReprojectMergePushConstants push = MakeShadowReprojectMergePushConstants(
            Span<const f32>(m_rayTracingState.m_prevWorldToClip.raw, 16u),
            targets.width,
            targets.height,
            softHalfWidth,
            softHalfHeight,
            slotRangeCount,
            historyValid,
            resources.softTraceSlot,
            resources.historyInSlot,
            resources.momentsInSlot,
            targets.bindless.shadowSoftGeometry.slot(),
            targets.bindless.shadowSoftGeometryPrev.slot(),
            targets.bindless.gbufferWorldPosition.slot(),
            resources.historyOutStorageSlot,
            resources.momentsOutStorageSlot,
            targets.shadowReceiverFactor
        );

        commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_shadowReprojectMergePipeline));
        heap.bindCompute(commandList, *m_rayTracingState.m_shadowReprojectMergePipeline.get());
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(softGroupsX, softGroupsY, 1u);
    };

    if(dispatchOpaqueResolve || dispatchOpaqueResolveTail){
        const __hidden_rt_softshadow_dispatch::ShadowReprojectMergeHeapResources opaqueMerge = frontIsA
            ? __hidden_rt_softshadow_dispatch::ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistA.slot(), targets.bindless.shadowMomentsA.slot(),
                targets.bindless.shadowHistBStorage.slot(), targets.bindless.shadowMomentsBStorage.slot()
            }
            : __hidden_rt_softshadow_dispatch::ShadowReprojectMergeHeapResources{
                targets.shadowSoftHalfA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(),
                targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistB.slot(), targets.bindless.shadowMomentsB.slot(),
                targets.bindless.shadowHistAStorage.slot(), targets.bindless.shadowMomentsAStorage.slot()
            }
        ;
        // Merge writes the NEXT pair; the wavelet must read that same pair, never stale A.
        Core::Texture* const opaqueResolveMoments = frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get();
        const u32 opaqueResolveMomentsSlot = frontIsA ? targets.bindless.shadowMomentsB.slot() : targets.bindless.shadowMomentsA.slot();
        if(dispatchOpaqueResolve && opaqueTemporalActive && opaquePhase != SoftShadowOpaqueResolvePhase::WaveletOnly){
            Core::GpuTimingMeasure opaqueTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTemporal, m_graphics.getDevice(), commandList);

            // The graph owns the selected history/moments plus stable previous-geometry/world reads.
            // The geometry downsample above still needs this callback's local UAV-to-SRV transition before the opaque merge samples it.
            dispatchMerge(opaqueMerge, graphOwnsOpaqueTraceToFirstWaveletBoundary, graphOwnsOpaqueGeometryToResolveBoundary, graphOwnsOpaqueTemporalMergeEntryStates, graphOwnsOpaqueTemporalMergeEntryStates);
        }

        if(opaquePhase != SoftShadowOpaqueResolvePhase::TemporalOnly){
            // The first wavelet reads this frame's trace or temporal merge directly, preserving its exact filtering input.
            Core::Texture* const opaqueWaveletInput = opaqueTemporalActive ? (frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get()) : targets.shadowSoftHalfA.get();
            const u32 opaqueWaveletInputSlot = opaqueTemporalActive ? (frontIsA ? targets.bindless.shadowHistB.slot() : targets.bindless.shadowHistA.slot()) : targets.bindless.shadowSoftHalfA.slot();
            SoftShadowResolveDispatch opaqueDispatch;
            opaqueDispatch.waveletPipeline = m_rayTracingState.m_softShadowResolve.m_scalar.m_wavelet.m_pipeline.get();
            opaqueDispatch.upsamplePipeline = m_rayTracingState.m_softShadowResolve.m_scalar.m_upsample.m_pipeline.get();
            opaqueDispatch.firstWaveletResources = {
                opaqueWaveletInput, opaqueResolveMoments, targets.shadowSoftHalfB.get(),
                opaqueWaveletInputSlot, opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
            };
            opaqueDispatch.outputHalfAResources = {
                targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfA.get(),
                targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
            };
            opaqueDispatch.outputHalfBResources = {
                targets.shadowSoftHalfA.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
                targets.bindless.shadowSoftHalfA.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
            };
            opaqueDispatch.upsampleResources = {
                targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
                targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
            };
            opaqueDispatch.visibilityTexture = targets.shadowVisibility.get();
            opaqueDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
            opaqueDispatch.sceneShading = targets.bindless.sceneShading.slot();
            opaqueDispatch.temporalMomentsValid = opaqueTemporalActive;
            opaqueDispatch.graphOwnsWaveletGeometryEntryState = graphOwnsOpaqueGeometryToResolveBoundary;
            opaqueDispatch.graphOwnsUpsampleStaticEntryStates = true;
            const b8 graphOwnsDeferredOpaqueWaveletInputs = opaquePhase == SoftShadowOpaqueResolvePhase::WaveletOnly;
            opaqueDispatch.graphOwnsWaveletMomentsEntryState = graphOwnsDeferredOpaqueWaveletInputs;
            opaqueDispatch.graphOwnsFirstWaveletInputState = graphOwnsDeferredOpaqueWaveletInputs || (graphOwnsOpaqueTraceToFirstWaveletBoundary && !opaqueTemporalActive);
            opaqueDispatch.graphOwnsFirstWaveletOutputState = dispatchOpaqueResolve && !dispatchOpaqueResolveTail;
            const b8 graphOwnsOneWaveletOpaqueResolveTailEntryStates = !dispatchOpaqueResolve && dispatchOpaqueResolveTail && NWB_SHADOW_RESOLVE_PASS_COUNT == 1u;
            opaqueDispatch.graphOwnsUpsampleInputColorEntryState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
            opaqueDispatch.graphOwnsUpsampleVisibilityOutputState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
            opaqueDispatch.firstWaveletWritesHalfA = false;
            opaqueDispatch.fold = SoftShadowUpsampleFold::Overwrite;
            opaqueDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_PASS_COUNT);
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
                // The software route is retained for devices without hardware ray queries.
                if(!graphOwnsOpaqueToTransparentBoundary){
                    commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                }
                commandList.setEnableUavBarriersForTexture(targets.transparentSoftHalf.get(), true);
                if(!graphOwnsOpaqueToTransparentBoundary)
                    commandList.commitBarriers();

                const b8 samplingHistoryUsable = temporalHistoryReadable && m_rayTracingState.m_softTransparentTemporalReady && m_rayTracingState.m_softwareTransparentSampling.m_history.usable();
                if(samplingHistoryUsable && !graphOwnsOpaqueToTransparentBoundary){
                    // These reads also belong to the split trace task; unsplit phases retain local transitions.
                    commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.shadowSoftGeometryPrev.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(frontIsA ? targets.transparentMomentsA.get() : targets.transparentMomentsB.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                    commandList.commitBarriers();
                }
                const SoftwareTransparentSamplingPush tracePush = MakeSoftwareTransparentSamplingPush(
                    Span<const f32>(m_rayTracingState.m_prevWorldToClip.raw, 16u),
                    targets.width,
                    targets.height,
                    m_rayTracingState.m_sceneBvhInstanceCount,
                    frameIndex,
                    transparentShadowSampleCount(),
                    targets.bindless.slotsBufferDescriptor.slot(),
                    m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
                    targets.bindless.transparentSoftHalfStorage.slot(),
                    samplingHistoryUsable,
                    targets.bindless.shadowSoftGeometry.slot(),
                    targets.bindless.shadowSoftGeometryPrev.slot(),
                    frontIsA ? targets.bindless.transparentMomentsA.slot() : targets.bindless.transparentMomentsB.slot(),
                    targets.shadowReceiverFactor
                );
                const auto& pipeline = m_rayTracingState.m_softwareTransparentSampling.m_pipelines[samplingHistoryUsable ? 1u : 0u];
                commandList.setComputeState(RayTracingDetail::MakeComputePassState(pipeline));
                heap.bindCompute(commandList, *pipeline.get());
                commandList.setPushConstants(&tracePush, sizeof(tracePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
                reportTransparentShadowSampling(tracePush.softSampleCount);
            }
        }
    }

    const b8 dispatchTransparentWavelet = dispatchTransparentResolve || dispatchTransparentResolveTail;
    const b8 dispatchTransparentMerge = dispatchTransparentTemporalMerge || (dispatchTransparentResolve && !splitTransparentResolve);
    if((dispatchTransparentMerge || dispatchTransparentWavelet) && m_rayTracingState.m_softTransparentReady){
        const b8 transparentTemporalActive = m_rayTracingState.m_softTransparentTemporalReady;
        const __hidden_rt_softshadow_dispatch::ShadowReprojectMergeHeapResources transparentMerge = frontIsA
            ? __hidden_rt_softshadow_dispatch::ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistA.slot(), targets.bindless.transparentMomentsA.slot(),
                targets.bindless.transparentHistBStorage.slot(), targets.bindless.transparentMomentsBStorage.slot()
            }
            : __hidden_rt_softshadow_dispatch::ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistB.slot(), targets.bindless.transparentMomentsB.slot(),
                targets.bindless.transparentHistAStorage.slot(), targets.bindless.transparentMomentsAStorage.slot()
            }
        ;
        // Keep the RGB wavelet's temporal variance paired with the history just emitted by the transparent merge.
        Core::Texture* const transparentResolveMoments = frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get();
        const u32 transparentResolveMomentsSlot = frontIsA ? targets.bindless.transparentMomentsB.slot() : targets.bindless.transparentMomentsA.slot();
        if(dispatchTransparentMerge && transparentTemporalActive){
            Core::GpuTimingMeasure transparentTemporalTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTemporal, m_graphics.getDevice(), commandList);

            dispatchMerge(transparentMerge, graphOwnsTransparentTraceToResolveBoundary, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates, graphOwnsTransparentTemporalMergeEntryStates);
            m_rayTracingState.m_transparentShadowSamplingHistory.record(m_rayTracingState.m_softShadowSlotMask);
        }

        if(dispatchTransparentWavelet){
            Core::Texture* const transparentWaveletInput = transparentTemporalActive ? (frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get()) : targets.transparentSoftHalf.get();
            const u32 transparentWaveletInputSlot = transparentTemporalActive ? (frontIsA ? targets.bindless.transparentHistB.slot() : targets.bindless.transparentHistA.slot()) : targets.bindless.transparentSoftHalf.slot();
            SoftShadowResolveDispatch transparentDispatch;
            transparentDispatch.waveletPipeline = m_rayTracingState.m_softShadowResolve.m_rgb.m_wavelet.m_pipeline.get();
            transparentDispatch.upsamplePipeline = m_rayTracingState.m_softShadowResolve.m_rgb.m_upsample.m_pipeline.get();
            transparentDispatch.firstWaveletResources = {
                transparentWaveletInput, transparentResolveMoments, targets.shadowSoftHalfA.get(),
                transparentWaveletInputSlot, transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
            };
            transparentDispatch.outputHalfAResources = {
                targets.shadowSoftHalfB.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
                targets.bindless.shadowSoftHalfB.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
            };
            transparentDispatch.outputHalfBResources = {
                targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfB.get(),
                targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
            };
            transparentDispatch.upsampleResources = {
                targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
                targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
            };
            transparentDispatch.visibilityTexture = targets.shadowVisibility.get();
            transparentDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
            transparentDispatch.sceneShading = targets.bindless.sceneShading.slot();
            transparentDispatch.temporalMomentsValid = transparentTemporalActive;
            const b8 graphOwnsTransparentTemporalMergeToWaveletBoundary = dispatchTransparentResolve && splitTransparentResolve && !dispatchTransparentTemporalMerge && transparentTemporalActive;
            transparentDispatch.graphOwnsFirstWaveletInputState = (graphOwnsTransparentTraceToResolveBoundary && !transparentTemporalActive) || graphOwnsTransparentTemporalMergeToWaveletBoundary;
            transparentDispatch.graphOwnsWaveletMomentsEntryState = graphOwnsTransparentTemporalMergeToWaveletBoundary;
            transparentDispatch.graphOwnsWaveletGeometryEntryState = graphOwnsTransparentTraceToResolveBoundary;
            transparentDispatch.graphOwnsUpsampleStaticEntryStates = true;
            transparentDispatch.graphOwnsFirstWaveletOutputState = dispatchTransparentResolve && splitTransparentResolve;
            const b8 graphOwnsOneWaveletTransparentResolveTailEntryStates = !dispatchTransparentResolve && dispatchTransparentResolveTail && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u;
            transparentDispatch.graphOwnsUpsampleInputColorEntryState = graphOwnsOneWaveletTransparentResolveTailEntryStates;
            transparentDispatch.graphOwnsUpsampleVisibilityOutputState = graphOwnsOneWaveletTransparentResolveTailEntryStates;
            transparentDispatch.firstWaveletWritesHalfA = true;
            transparentDispatch.fold = SoftShadowUpsampleFold::Multiply;
            transparentDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT);
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

