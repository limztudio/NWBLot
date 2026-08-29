// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_softshadow{


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


bool RendererRayTracingSystem::softShadowTemporalHistoryUsable()const noexcept{
    return
        m_rayTracingState.m_softShadowTemporalReady
        && m_rayTracingState.m_prevWorldToClipValid
        && m_rayTracingState.m_softShadowTemporalSeeded
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const u32 frameIndex,
    const u32 softGroupsX,
    const u32 softGroupsY,
    const bool graphEntryStatesOwned,
    const bool dispatchOpaqueGeometry,
    const bool dispatchOpaqueResolve,
    const bool dispatchTransparentTrace,
    const bool dispatchTransparentResolve,
    const bool graphOwnsOpaqueGeometryToResolveBoundary,
    const bool graphOwnsOpaqueToTransparentBoundary,
    const bool graphOwnsTransparentTraceToResolveBoundary,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const bool graphOwnsTransparentTemporalMergeEntryStates,
    const bool dispatchOpaqueResolveTail,
    const bool graphOwnsOpaqueTraceToFirstWaveletBoundary,
    const bool dispatchTransparentResolveTail,
    const bool splitTransparentResolve,
    const bool dispatchTransparentTemporalMerge
){
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(m_deferredState.m_sceneShadingBuffer);
    NWB_ASSERT(m_deferredState.m_lightBuffer);

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

    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;

    const auto passState = [&](const Core::ComputePipelineHandle& pipeline){
        Core::ComputeState state;
        state.setPipeline(pipeline.get());
        // Set 0 is push-only. Keep it represented so fixed heap sets retain their pipeline-layout indices.
        return state;
    };
    const auto bindHeap = [&](const Core::ComputePipelineHandle& pipeline){
        heap.bindCompute(commandList, *pipeline.get());
    };

    if(dispatchOpaqueGeometry){
        // Geometry downsample: the shared graph already declares its descriptor-visible inputs and the first geometry
        // cache UAV state. Direct compatibility callers retain the native prologue; all later soft-shadow lifecycle
        // transitions remain local to this task.
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_deferredState.m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        }
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
        if(!graphEntryStatesOwned)
            commandList.commitBarriers();

        {
            Core::GpuTimingMeasure geometryTiming(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_ShadowGeometryDownsample,
                m_graphics.getDevice(),
                commandList
            );
            ShadowGeometryDownsamplePushConstants geometryPush;
            geometryPush.width = targets.width;
            geometryPush.height = targets.height;
            geometryPush.halfWidth = softHalfWidth;
            geometryPush.halfHeight = softHalfHeight;
            geometryPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
            geometryPush.normalSlot = targets.bindless.gbufferNormal.slot();
            geometryPush.depthSlot = targets.bindless.gbufferDepth.slot();
            geometryPush.outputStorageSlot = targets.bindless.shadowSoftGeometryStorage.slot();
            geometryPush.sceneShadingSlot = targets.bindless.sceneShading.slot();
            commandList.setComputeState(passState(m_rayTracingState.m_shadowGeometryDownsamplePipeline));
            bindHeap(m_rayTracingState.m_shadowGeometryDownsamplePipeline);
            commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }
    }

    if(
        !dispatchOpaqueResolve
        && !dispatchOpaqueResolveTail
        && !dispatchTransparentTrace
        && !dispatchTransparentResolve
        && !dispatchTransparentResolveTail
        && !dispatchTransparentTemporalMerge
    )
        return;

    u32 slotRangeCount = 0u;
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) != 0u)
            slotRangeCount = slot + 1u;
    }
    if(slotRangeCount == 0u)
        return;

    const bool frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    const bool opaqueTemporalActive = m_rayTracingState.m_softShadowTemporalReady;
    const bool temporalHistoryReadable = softShadowTemporalHistoryUsable();
    const u32 historyValid = temporalHistoryReadable ? 1u : 0u;

    const auto dispatchMerge = [&](const __hidden_rt_softshadow::ShadowReprojectMergeHeapResources& resources, const bool graphOwnsSoftTraceInputState, const bool graphOwnsMergeCurrentGeometryEntryState, const bool graphOwnsMergeStaticReadEntryStates, const bool graphOwnsMergeTemporalEntryStates){
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

        ShadowReprojectMergePushConstants push;
        push.prevWorldToClip = m_rayTracingState.m_prevWorldToClip;
        push.width = targets.width;
        push.height = targets.height;
        push.halfWidth = softHalfWidth;
        push.halfHeight = softHalfHeight;
        push.lightSlotStart = 0u;
        push.lightSlotCount = slotRangeCount;
        push.historyValid = historyValid;
        push.softTraceSlot = resources.softTraceSlot;
        push.historyInSlot = resources.historyInSlot;
        push.momentsInSlot = resources.momentsInSlot;
        push.geometryCurrSlot = targets.bindless.shadowSoftGeometry.slot();
        push.geometryPrevSlot = targets.bindless.shadowSoftGeometryPrev.slot();
        push.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        push.historyOutputStorageSlot = resources.historyOutStorageSlot;
        push.momentsOutputStorageSlot = resources.momentsOutStorageSlot;

        commandList.setComputeState(passState(m_rayTracingState.m_shadowReprojectMergePipeline));
        bindHeap(m_rayTracingState.m_shadowReprojectMergePipeline);
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(softGroupsX, softGroupsY, 1u);
    };

    if(dispatchOpaqueResolve || dispatchOpaqueResolveTail){
    const __hidden_rt_softshadow::ShadowReprojectMergeHeapResources opaqueMerge = frontIsA
        ? __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
            targets.shadowSoftHalfA.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistA.slot(), targets.bindless.shadowMomentsA.slot(),
            targets.bindless.shadowHistBStorage.slot(), targets.bindless.shadowMomentsBStorage.slot()
        }
        : __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
            targets.shadowSoftHalfA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistB.slot(), targets.bindless.shadowMomentsB.slot(),
            targets.bindless.shadowHistAStorage.slot(), targets.bindless.shadowMomentsAStorage.slot()
        }
    ;
    // The merge writes the NEXT history/moments pair. The variance-guided wavelet must read that same pair, not the
    // incoming A buffer on every other frame; otherwise its temporal variance is stale (and initially undefined).
    Core::Texture* const opaqueResolveMoments = frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get();
    const u32 opaqueResolveMomentsSlot = frontIsA ? targets.bindless.shadowMomentsB.slot() : targets.bindless.shadowMomentsA.slot();
    if(dispatchOpaqueResolve && opaqueTemporalActive){
        Core::GpuTimingMeasure opaqueTemporalTiming(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_ShadowOpaqueTemporal,
            m_graphics.getDevice(),
            commandList
        );
        // The graph owns the selected history/moments plus stable previous-geometry/world reads. The geometry
        // downsample above still needs this callback's local UAV-to-SRV transition before the opaque merge samples it.
        dispatchMerge(
            opaqueMerge,
            graphOwnsOpaqueTraceToFirstWaveletBoundary,
            graphOwnsOpaqueGeometryToResolveBoundary,
            graphOwnsOpaqueTemporalMergeEntryStates,
            graphOwnsOpaqueTemporalMergeEntryStates
        );
    }

    // Feed the first wavelet directly from this frame's trace or temporal merge. PREPARE was only a half-res copy into
    // soft-B before this same wavelet, so eliminating it preserves the exact filtering input while removing a dispatch.
    Core::Texture* const opaqueWaveletInput = opaqueTemporalActive
        ? (frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get())
        : targets.shadowSoftHalfA.get()
    ;
    const u32 opaqueWaveletInputSlot = opaqueTemporalActive
        ? (frontIsA ? targets.bindless.shadowHistB.slot() : targets.bindless.shadowHistA.slot())
        : targets.bindless.shadowSoftHalfA.slot()
    ;
    SoftShadowResolveDispatch opaqueDispatch;
    opaqueDispatch.pipeline = m_rayTracingState.m_shadowResolvePipeline.get();
    opaqueDispatch.firstWaveletResources = {
        opaqueWaveletInput, opaqueWaveletInput, opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        opaqueWaveletInputSlot, opaqueWaveletInputSlot, opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.outputHalfAResources = {
        targets.shadowSoftHalfB.get(), targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfA.get(),
        targets.bindless.shadowSoftHalfB.slot(), targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
    };
    opaqueDispatch.outputHalfBResources = {
        targets.shadowSoftHalfA.get(), targets.shadowSoftHalfA.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowSoftHalfA.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.upsampleResources = {
        targets.shadowSoftHalfB.get(), targets.shadowSoftHalfB.get(), opaqueResolveMoments, targets.shadowSoftHalfB.get(),
        targets.bindless.shadowSoftHalfB.slot(), targets.bindless.shadowSoftHalfB.slot(), opaqueResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
    };
    opaqueDispatch.visibilityTexture = targets.shadowVisibility.get();
    opaqueDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
    opaqueDispatch.sceneShading = targets.bindless.sceneShading.slot();
    opaqueDispatch.temporalMomentsValid = opaqueTemporalActive;
    opaqueDispatch.graphOwnsWaveletGeometryEntryState = graphOwnsOpaqueGeometryToResolveBoundary;
    opaqueDispatch.graphOwnsUpsampleStaticEntryStates = graphEntryStatesOwned;
    opaqueDispatch.graphOwnsFirstWaveletInputState =
        graphOwnsOpaqueTraceToFirstWaveletBoundary && !opaqueTemporalActive
    ;
    opaqueDispatch.graphOwnsFirstWaveletOutputState =
        dispatchOpaqueResolve && !dispatchOpaqueResolveTail && graphEntryStatesOwned
    ;
    const bool graphOwnsOneWaveletOpaqueResolveTailEntryStates =
        !dispatchOpaqueResolve
        && dispatchOpaqueResolveTail
        && graphEntryStatesOwned
        && NWB_SHADOW_RESOLVE_PASS_COUNT == 1u
    ;
    opaqueDispatch.graphOwnsUpsampleInputColorEntryState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
    opaqueDispatch.graphOwnsUpsampleVisibilityOutputState = graphOwnsOneWaveletOpaqueResolveTailEntryStates;
    opaqueDispatch.firstWaveletWritesHalfA = false;
    opaqueDispatch.fold = SoftShadowUpsampleFold::Overwrite;
    opaqueDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_PASS_COUNT);
    if(dispatchOpaqueResolve && dispatchOpaqueResolveTail){
        Core::GpuTimingMeasure opaqueResolveTiming(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_ShadowOpaqueResolve,
            m_graphics.getDevice(),
            commandList
        );
        dispatchSoftShadowResolve(commandList, targets, 0u, slotRangeCount, opaqueDispatch);
    }
    else if(dispatchOpaqueResolve)
        dispatchSoftShadowResolve(commandList, targets, 0u, slotRangeCount, opaqueDispatch, true, false);
    else
        dispatchSoftShadowResolve(commandList, targets, 0u, slotRangeCount, opaqueDispatch, false, true);
    }

    if(dispatchTransparentTrace && m_rayTracingState.m_softTransparentReady){
        {
            Core::GpuTimingMeasure transparentTraceTiming(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_ShadowTransparentTrace,
                m_graphics.getDevice(),
                commandList
            );
            // The normal deferred graph already supplies the transparent trace's heap-selected traversal and
            // descriptor buffers. Direct compatibility callers retain the native static bridge. A split graph tail
            // additionally owns the opaque-resolve-to-transparent-trace image/UAV boundary in its prologue.
            if(!graphEntryStatesOwned){
                transitionSwShadowTraversalResources(commandList);
                commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
                commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
                commandList.setBufferState(m_deferredState.m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
                commandList.setBufferState(m_deferredState.m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
            }
            if(!graphOwnsOpaqueToTransparentBoundary){
                commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            }
            commandList.setEnableUavBarriersForTexture(targets.transparentSoftHalf.get(), true);
            if(!graphOwnsOpaqueToTransparentBoundary || !graphEntryStatesOwned)
                commandList.commitBarriers();

            SwShadowHeapPushConstants tracePush;
            tracePush.width = targets.width;
            tracePush.height = targets.height;
            tracePush.instanceCount = m_rayTracingState.m_sceneBvhInstanceCount;
            tracePush.frameIndex = frameIndex;
            tracePush.softSampleCount = NWB_SW_SHADOW_TRANSPARENT_SPP;
            tracePush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
            tracePush.materialContextSlotsHeapSlot = m_rayTracingState.m_shadowMaterialContextSlotsHeapHandle.slot();
            tracePush.visibilityStorageSlot = targets.bindless.shadowVisibilityStorage.slot();
            tracePush.coarseStorageSlot = targets.bindless.shadowCoarseTransmittanceStorage.slot();
            tracePush.softHalfStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
            tracePush.transparentSoftHalfStorageSlot = targets.bindless.transparentSoftHalfStorage.slot();
            tracePush.edgeStatsStorageSlot = m_rayTracingState.m_swShadowEdgeStatsHeapHandle.slot();
            tracePush.edgeCounterStorageSlot = m_rayTracingState.m_swShadowEdgeCounterHeapHandle.slot();
            tracePush.edgeListStorageSlot = m_rayTracingState.m_swShadowEdgeListHeapHandle.slot();
            tracePush.indirectArgsStorageSlot = m_rayTracingState.m_swShadowIndirectArgsHeapHandle.slot();
            commandList.setComputeState(passState(m_rayTracingState.m_swShadowTransparentSoftPipeline));
            bindHeap(m_rayTracingState.m_swShadowTransparentSoftPipeline);
            commandList.setPushConstants(&tracePush, sizeof(tracePush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }
    }

    const bool dispatchTransparentWavelet = dispatchTransparentResolve || dispatchTransparentResolveTail;
    const bool dispatchTransparentMerge =
        dispatchTransparentTemporalMerge || (dispatchTransparentResolve && !splitTransparentResolve)
    ;
    if((dispatchTransparentMerge || dispatchTransparentWavelet) && m_rayTracingState.m_softTransparentReady){
        const bool transparentTemporalActive = m_rayTracingState.m_softTransparentTemporalReady;
        const __hidden_rt_softshadow::ShadowReprojectMergeHeapResources transparentMerge = frontIsA
            ? __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistA.slot(), targets.bindless.transparentMomentsA.slot(),
                targets.bindless.transparentHistBStorage.slot(), targets.bindless.transparentMomentsBStorage.slot()
            }
            : __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistB.slot(), targets.bindless.transparentMomentsB.slot(),
                targets.bindless.transparentHistAStorage.slot(), targets.bindless.transparentMomentsAStorage.slot()
            }
        ;
        // Keep the RGB wavelet's temporal variance paired with the history just emitted by the transparent merge.
        Core::Texture* const transparentResolveMoments = frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get();
        const u32 transparentResolveMomentsSlot = frontIsA ? targets.bindless.transparentMomentsB.slot() : targets.bindless.transparentMomentsA.slot();
        if(dispatchTransparentMerge && transparentTemporalActive){
            Core::GpuTimingMeasure transparentTemporalTiming(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_ShadowTransparentTemporal,
                m_graphics.getDevice(),
                commandList
            );
            dispatchMerge(
                transparentMerge,
                graphOwnsTransparentTraceToResolveBoundary,
                graphOwnsTransparentTemporalMergeEntryStates,
                graphOwnsTransparentTemporalMergeEntryStates,
                graphOwnsTransparentTemporalMergeEntryStates
            );
        }

        if(dispatchTransparentWavelet){
        Core::Texture* const transparentWaveletInput = transparentTemporalActive
            ? (frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get())
            : targets.transparentSoftHalf.get()
        ;
        const u32 transparentWaveletInputSlot = transparentTemporalActive
            ? (frontIsA ? targets.bindless.transparentHistB.slot() : targets.bindless.transparentHistA.slot())
            : targets.bindless.transparentSoftHalf.slot()
        ;
        SoftShadowResolveDispatch transparentDispatch;
        transparentDispatch.pipeline = m_rayTracingState.m_shadowResolveRgbPipeline.get();
        transparentDispatch.firstWaveletResources = {
            transparentWaveletInput, transparentWaveletInput, transparentResolveMoments, targets.shadowSoftHalfA.get(),
            transparentWaveletInputSlot, transparentWaveletInputSlot, transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
        };
        transparentDispatch.outputHalfAResources = {
            targets.shadowSoftHalfB.get(), targets.shadowSoftHalfB.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
            targets.bindless.shadowSoftHalfB.slot(), targets.bindless.shadowSoftHalfB.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
        };
        transparentDispatch.outputHalfBResources = {
            targets.shadowSoftHalfA.get(), targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfB.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfBStorage.slot()
        };
        transparentDispatch.upsampleResources = {
            targets.shadowSoftHalfA.get(), targets.shadowSoftHalfA.get(), transparentResolveMoments, targets.shadowSoftHalfA.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowSoftHalfA.slot(), transparentResolveMomentsSlot, targets.bindless.shadowSoftHalfAStorage.slot()
        };
        transparentDispatch.visibilityTexture = targets.shadowVisibility.get();
        transparentDispatch.visibilityStorage = targets.bindless.shadowVisibilityStorage.slot();
        transparentDispatch.sceneShading = targets.bindless.sceneShading.slot();
        transparentDispatch.temporalMomentsValid = transparentTemporalActive;
        const bool graphOwnsTransparentTemporalMergeToWaveletBoundary =
            dispatchTransparentResolve
            && splitTransparentResolve
            && !dispatchTransparentTemporalMerge
            && graphEntryStatesOwned
            && transparentTemporalActive
        ;
        transparentDispatch.graphOwnsFirstWaveletInputState =
            (graphOwnsTransparentTraceToResolveBoundary && !transparentTemporalActive)
            || graphOwnsTransparentTemporalMergeToWaveletBoundary
        ;
        transparentDispatch.graphOwnsWaveletMomentsEntryState =
            graphOwnsTransparentTemporalMergeToWaveletBoundary
        ;
        transparentDispatch.graphOwnsWaveletGeometryEntryState =
            graphEntryStatesOwned && graphOwnsTransparentTraceToResolveBoundary
        ;
        transparentDispatch.graphOwnsUpsampleStaticEntryStates = graphEntryStatesOwned;
        transparentDispatch.graphOwnsFirstWaveletOutputState =
            dispatchTransparentResolve && splitTransparentResolve && graphEntryStatesOwned
        ;
        const bool graphOwnsOneWaveletTransparentResolveTailEntryStates =
            !dispatchTransparentResolve
            && dispatchTransparentResolveTail
            && graphEntryStatesOwned
            && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u
        ;
        transparentDispatch.graphOwnsUpsampleInputColorEntryState =
            graphOwnsOneWaveletTransparentResolveTailEntryStates
        ;
        transparentDispatch.graphOwnsUpsampleVisibilityOutputState =
            graphOwnsOneWaveletTransparentResolveTailEntryStates
        ;
        transparentDispatch.firstWaveletWritesHalfA = true;
        transparentDispatch.fold = SoftShadowUpsampleFold::Multiply;
        transparentDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT);
        if(dispatchTransparentResolve && !splitTransparentResolve){
            Core::GpuTimingMeasure transparentResolveTiming(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_ShadowTransparentResolve,
                m_graphics.getDevice(),
                commandList
            );
            dispatchSoftShadowResolve(commandList, targets, 0u, slotRangeCount, transparentDispatch);
        }
        else if(dispatchTransparentResolve)
            dispatchSoftShadowResolve(commandList, targets, 0u, slotRangeCount, transparentDispatch, true, false);
        else
            dispatchSoftShadowResolve(commandList, targets, 0u, slotRangeCount, transparentDispatch, false, true);
        }
    }

    // Do not mutate the target-generation handles while the sibling caustics and surfel-GI workers can still validate
    // targets.bindless. RendererFramePipeline finalizes this pending CPU-side swap only after its complete ordered Graphics
    // submission succeeds.
    if(
        ((dispatchTransparentResolve && !splitTransparentResolve) || dispatchTransparentResolveTail)
        && m_rayTracingState.m_softShadowTemporalReady
    )
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
}

bool RendererRayTracingSystem::ensureShadowGeometryDownsamplePipeline(){
    if(m_rayTracingState.m_shadowGeometryDownsamplePipeline)
        return true;
    if(m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_rayTracingState.m_shadowGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowGeometryDownsamplePushConstants)));
        m_rayTracingState.m_shadowGeometryDownsampleBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_shadowGeometryDownsampleBindingLayout){
            m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }
    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowGeometryDownsampleShader,
        AssetsGraphicsShadow::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_ShadowGeometryDownsample"
    )){
        m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowGeometryDownsampleShader)
        .addBindingLayout(m_rayTracingState.m_shadowGeometryDownsampleBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_shadowGeometryDownsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowGeometryDownsamplePipeline){
        m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSoftShadowResolvePipeline(){
    if(m_rayTracingState.m_shadowResolvePipeline)
        return true;
    if(m_rayTracingState.m_shadowResolvePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_rayTracingState.m_shadowResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowResolvePushConstants)));
        m_rayTracingState.m_shadowResolveBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_shadowResolveBindingLayout){
            m_rayTracingState.m_shadowResolvePipelineFailed = true;
            return false;
        }
    }
    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowResolveShader,
        AssetsGraphicsShadow::s_SoftResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowResolve"
    )){
        m_rayTracingState.m_shadowResolvePipelineFailed = true;
        return false;
    }
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowResolveShader)
        .addBindingLayout(m_rayTracingState.m_shadowResolveBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_shadowResolvePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowResolvePipeline){
        m_rayTracingState.m_shadowResolvePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSoftTransparentResolvePipeline(){
    if(m_rayTracingState.m_shadowResolveRgbPipeline)
        return true;
    if(m_rayTracingState.m_shadowResolveRgbPipelineFailed || !m_rayTracingState.m_shadowResolveBindingLayout)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowResolveRgbShader,
        AssetsGraphicsShadow::s_SoftResolveRgbShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowResolveRgb"
    )){
        m_rayTracingState.m_shadowResolveRgbPipelineFailed = true;
        return false;
    }
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowResolveRgbShader)
        .addBindingLayout(m_rayTracingState.m_shadowResolveBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_shadowResolveRgbPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowResolveRgbPipeline){
        m_rayTracingState.m_shadowResolveRgbPipelineFailed = true;
        return false;
    }
    return true;
}

void RendererRayTracingSystem::dispatchSoftShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const u32 slotStart,
    const u32 slotCount,
    const SoftShadowResolveDispatch& dispatch,
    const bool dispatchFirstWavelet,
    const bool dispatchTail
){
    NWB_ASSERT(dispatch.pipeline);
    NWB_ASSERT(dispatch.visibilityTexture);
    const u32 halfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    const auto runPass = [&](const SoftShadowResolvePassResources& resources, const u32 stepWidth, const ShadowResolveStage::Enum stage, const u32 groupsX, const u32 groupsY, const bool graphOwnsInputColorState = false, const bool graphOwnsOutputState = false){
        NWB_ASSERT(resources.softHalfTexture && resources.inputColorTexture && resources.momentsTexture && resources.outputTexture);
        switch(stage){
            case ShadowResolveStage::Prepare:
                commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                commandList.setTextureState(resources.softHalfTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                commandList.setTextureState(resources.outputTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(resources.outputTexture, true);
                break;
            case ShadowResolveStage::Wavelet:
                if(!dispatch.graphOwnsWaveletGeometryEntryState)
                    commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(dispatch.temporalMomentsValid && !dispatch.graphOwnsWaveletMomentsEntryState)
                    commandList.setTextureState(resources.momentsTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(resources.outputTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(resources.outputTexture, true);
                break;
            case ShadowResolveStage::Upsample:
                if(!dispatch.graphOwnsUpsampleStaticEntryStates){
                    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setBufferState(m_deferredState.m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
                }
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(dispatch.visibilityTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(dispatch.visibilityTexture, true);
                break;
        }
        commandList.commitBarriers();

        ShadowResolvePushConstants push;
        push.width = targets.width;
        push.height = targets.height;
        push.halfWidth = halfWidth;
        push.halfHeight = halfHeight;
        push.stepWidth = stepWidth;
        push.stage = static_cast<u32>(stage);
        push.lightSlotStart = slotStart;
        push.lightSlotCount = slotCount;
        push.momentsValid = dispatch.temporalMomentsValid ? 1u : 0u;
        push.upsampleFold = static_cast<u32>(dispatch.fold);
        push.geometrySlot = bindless.shadowSoftGeometry.slot();
        push.depthSlot = bindless.gbufferDepth.slot();
        push.worldPositionSlot = bindless.gbufferWorldPosition.slot();
        push.normalSlot = bindless.gbufferNormal.slot();
        push.softHalfSlot = resources.softHalf;
        push.inputColorSlot = resources.inputColor;
        push.momentsSlot = resources.moments;
        push.outputStorageSlot = resources.outputStorage;
        push.visibilityStorageSlot = dispatch.visibilityStorage;
        push.sceneShadingSlot = dispatch.sceneShading;

        Core::ComputeState state;
        state.setPipeline(dispatch.pipeline);
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *dispatch.pipeline);
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    static_assert((NWB_SHADOW_RESOLVE_PASS_COUNT % 2) == 1, "opaque resolve pass count must be odd");
    static_assert((NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT % 2) == 1, "transparent resolve pass count must be odd");
    NWB_ASSERT(dispatch.waveletPassCount != 0u && (dispatch.waveletPassCount % 2u) == 1u);
    NWB_ASSERT(dispatchFirstWavelet || dispatchTail);

    if(dispatchFirstWavelet){
        runPass(
            dispatch.firstWaveletResources,
            1u,
            ShadowResolveStage::Wavelet,
            halfGroupsX,
            halfGroupsY,
            dispatch.graphOwnsFirstWaveletInputState,
            dispatch.graphOwnsFirstWaveletOutputState
        );
    }
    if(!dispatchTail)
        return;

    bool sourceIsHalfA = dispatch.firstWaveletWritesHalfA;
    [[maybe_unused]] const SoftShadowResolvePassResources* lastWaveletResources = &dispatch.firstWaveletResources;
    for(u32 pass = 1u; pass < dispatch.waveletPassCount; ++pass){
        const SoftShadowResolvePassResources& nextWaveletResources = sourceIsHalfA
            ? dispatch.outputHalfBResources
            : dispatch.outputHalfAResources
        ;
        runPass(
            nextWaveletResources,
            1u << pass,
            ShadowResolveStage::Wavelet,
            halfGroupsX,
            halfGroupsY
        );
        lastWaveletResources = &nextWaveletResources;
        sourceIsHalfA = !sourceIsHalfA;
    }
    NWB_ASSERT(dispatch.upsampleResources.inputColorTexture == lastWaveletResources->outputTexture);
    runPass(
        dispatch.upsampleResources,
        1u,
        ShadowResolveStage::Upsample,
        fullGroupsX,
        fullGroupsY,
        dispatch.graphOwnsUpsampleInputColorEntryState,
        dispatch.graphOwnsUpsampleVisibilityOutputState
    );
}

bool RendererRayTracingSystem::ensureShadowReprojectMergePipeline(){
    if(m_rayTracingState.m_shadowReprojectMergePipeline)
        return true;
    if(m_rayTracingState.m_shadowReprojectMergePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_rayTracingState.m_shadowReprojectMergeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowReprojectMergePushConstants)));
        m_rayTracingState.m_shadowReprojectMergeBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_shadowReprojectMergeBindingLayout){
            m_rayTracingState.m_shadowReprojectMergePipelineFailed = true;
            return false;
        }
    }
    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowReprojectMergeShader,
        AssetsGraphicsShadow::s_SoftReprojectMergeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowReprojectMerge"
    )){
        m_rayTracingState.m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowReprojectMergeShader)
        .addBindingLayout(m_rayTracingState.m_shadowReprojectMergeBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_shadowReprojectMergePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowReprojectMergePipeline){
        m_rayTracingState.m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    return true;
}

void RendererRayTracingSystem::swapSoftShadowTemporalHistory(DeferredFrameTargets& targets){
    if(!m_rayTracingState.m_softShadowTemporalReady)
        return;

    if(m_drawState.m_meshViewGpuDataValid){
        const auto* meshView = reinterpret_cast<const ECSRenderDetail::MeshViewGpuData*>(m_drawState.m_meshViewGpuData);
        NWB_MEMCPY(&m_rayTracingState.m_prevWorldToClip, sizeof(m_rayTracingState.m_prevWorldToClip), &meshView->worldToClip, sizeof(m_rayTracingState.m_prevWorldToClip));
        m_rayTracingState.m_prevWorldToClipValid = true;
    }
    m_rayTracingState.m_softShadowTemporalSeeded = true;
    Swap(targets.shadowSoftGeometry, targets.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometry, targets.bindless.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometryStorage, targets.bindless.shadowSoftGeometryPrevStorage);
    m_rayTracingState.m_softShadowHistoryFrontIsA ^= 1u;
}

void RendererRayTracingSystem::finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets){
    if(!m_rayTracingState.m_softShadowTemporalHistoryAdvancePending)
        return;

    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    swapSoftShadowTemporalHistory(targets);
}

void RendererRayTracingSystem::discardSoftShadowTemporalHistory(){
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

