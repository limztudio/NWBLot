// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
    if(targets.shadowReceiverFactor != s_ShadowReceiverFactorHalf && targets.shadowReceiverFactor != s_ShadowReceiverFactorQuarter)
        return false;
    // Deferred lighting always samples this per-slot transmittance target.
    targets.shadowVisibilityFormat = Core::Format::RGBA16_FLOAT;

    const auto createShadowTarget = [&](Core::TextureDesc desc, const Name& name, const tchar* const errorMessage, Core::TextureHandle& outTexture){
        desc.setName(name);
        auto result = m_graphics.createTexture(desc);
        if(!result){
            outTexture.reset();
            NWB_LOGGER_ERROR(errorMessage);
            return false;
        }
        outTexture = Move(*result);
        return true;
    };

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setIsUAV(true)
        // The lagged-history copy may run on Transfer after producers finish. Keep all three transports shareable.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    if(!createShadowTarget(visibilityDesc, "engine/shadow/visibility", NWB_TEXT("RendererSystem: failed to create shadow visibility target"), targets.shadowVisibility))
        return false;

    // Round up half-resolution scratch so odd extents retain coverage.
    targets.shadowCoarseTransmittanceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc coarseDesc;
    coarseDesc
        .setWidth((targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setHeight((targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowCoarseTransmittanceFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    if(!createShadowTarget(coarseDesc, "engine/shadow/coarse_transmittance", NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"), targets.shadowCoarseTransmittance))
        return false;

    // Ping-pong half-resolution soft visibility and geometry cache.
    targets.shadowSoftFormat = Core::Format::RGBA16_FLOAT;
    targets.shadowSoftGeometryFormat = Core::Format::RGBA16_FLOAT;
    const u32 softHalfWidth = (targets.width + targets.shadowReceiverFactor - 1u) / targets.shadowReceiverFactor;
    const u32 softHalfHeight = (targets.height + targets.shadowReceiverFactor - 1u) / targets.shadowReceiverFactor;

    Core::TextureDesc softHalfDesc;
    softHalfDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowSoftFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    Core::TextureDesc softGeometryDesc;
    softGeometryDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setFormat(targets.shadowSoftGeometryFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;

    // Recreated history must not reproject through the previous target's matrix.
    if(
        !createShadowTarget(softHalfDesc, "engine/shadow/soft_half_a", NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"), targets.shadowSoftHalfA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/soft_half_b", NWB_TEXT("RendererSystem: failed to create soft shadow half-B target"), targets.shadowSoftHalfB)
        || !createShadowTarget(softGeometryDesc, "engine/shadow/soft_geometry", NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"), targets.shadowSoftGeometry)
        || !createShadowTarget(softHalfDesc, "engine/shadow/hist_a", NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"), targets.shadowHistA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/hist_b", NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"), targets.shadowHistB)
        || !createShadowTarget(softHalfDesc, "engine/shadow/moments_a", NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"), targets.shadowMomentsA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/moments_b", NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"), targets.shadowMomentsB)
        || !createShadowTarget(softGeometryDesc, "engine/shadow/soft_geometry_prev", NWB_TEXT("RendererSystem: failed to create soft shadow previous-frame geometry cache target"), targets.shadowSoftGeometryPrev)
    )
        return false;

    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_rayTracingState.m_transparentShadowSamplingHistory.reset();
    m_rayTracingState.m_softShadowTemporalSeeded = false;
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    m_rayTracingState.m_prevWorldToClipValid = false;
    m_rayTracingState.m_softShadowHistoryFrontIsA = 1u;

    // Transparent temporal history shares geometry and the opaque history selector.
    if(
        !createShadowTarget(softHalfDesc, "engine/shadow/transparent_soft_half", NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"), targets.transparentSoftHalf)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_hist_a", NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"), targets.transparentHistA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_hist_b", NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"), targets.transparentHistB)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_moments_a", NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"), targets.transparentMomentsA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_moments_b", NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"), targets.transparentMomentsB)
    )
        return false;

    // All resolve allocations belong to this target generation. Verify both history selectors once, before publication.
    for(u32 selector = 0u; selector < s_ShadowHistorySelectorCount; ++selector){
        const b8 frontIsA = selector != 0u;
        const SoftShadowCombinedWaveletInputs waveletInputs{
            .opaqueHistory = frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get(),
            .opaqueMoments = frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get(),
            .transparentHistory = frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get(),
            .transparentMoments = frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get(),
            .geometry = targets.shadowSoftGeometry.get(),
            .opaqueOutput = targets.shadowSoftHalfB.get(),
            .transparentOutput = targets.shadowSoftHalfA.get(),
        };
        if(!waveletInputs.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft-shadow target allocation produced aliased resolve resources"));
            return false;
        }
    }

    // Edge records are capped at one per pixel; overflow falls back to interpolation.
    const u64 edgeListCapacity = static_cast<u64>(targets.width) * targets.height;
    if(edgeListCapacity > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow edge-list capacity exceeds u32 limits"));
        return false;
    }
    const u32 edgeListCapacityRecords = static_cast<u32>(edgeListCapacity);
    Core::BufferDesc edgeListDesc;
    edgeListDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * static_cast<u64>(NWB_SW_SHADOW_EDGE_RECORD_WORDS) * static_cast<u64>(edgeListCapacityRecords))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("sw_shadow_edge_list"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto edgeListBufferResult = m_graphics.createBuffer(edgeListDesc);
    if(!edgeListBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    Core::BufferHandle edgeListBuffer = Move(*edgeListBufferResult);
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !RayTracingDetail::ReplaceHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *edgeListBuffer.get(), m_rayTracingState.m_swShadowEdgeListHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW shadow edge-list buffer in the descriptor heap"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    m_rayTracingState.m_swShadowEdgeListBuffer = Move(edgeListBuffer);
    m_rayTracingState.m_swShadowEdgeListCapacity = edgeListCapacityRecords;

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: created shadow receiver grid factor={} full={}x{} receiver={}x{}"), targets.shadowReceiverFactor, targets.width, targets.height, softHalfWidth, softHalfHeight);

    return true;
}

Expected<u32> RendererRayTracingSystem::renderShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 splitOpaqueSoftResolve,
    const LightSpaceShadowSnapshot* const lightSpace
){
    u32 producedFrameIndex = 0u;
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    NWB_ASSERT(deferredLightingResources.valid());
    if(!targets.shadowVisibility)
        return MakeUnexpected(Failure{});
    if(!m_rayTracingState.m_tlas || !m_rayTracingState.m_shadowPipeline)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !m_rayTracingState.m_tlasHeapHandle.valid()
        || !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow trace heap resources are incomplete"));
        return MakeUnexpected(Failure{});
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    // Hardware tracing shares the half-resolution soft-shadow resolve when available.
    if(m_rayTracingState.m_softShadowReady && m_rayTracingState.m_shadowSoftPipeline && m_rayTracingState.m_softShadowSlotMask != 0u){
        const u32 softHalfWidth = (targets.width + targets.shadowReceiverFactor - 1u) / targets.shadowReceiverFactor;
        const u32 softHalfHeight = (targets.height + targets.shadowReceiverFactor - 1u) / targets.shadowReceiverFactor;
        const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));


        // Resolve reuses the trace outputs as UAV/SRV scratch.
        enableSoftShadowResolveBarriers(commandList, targets);

        // Advance the jitter sequence once for the primary shadow producer.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;

        {
            Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

            Core::ComputeState softState;
            softState.setPipeline(m_rayTracingState.m_shadowSoftPipeline.get());
            commandList.setComputeState(softState);
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowSoftPipeline.get(), m_rayTracingState.m_tlasHeapHandle);

            ShadowRqSoftPushConstants softPush;
            softPush.width = targets.width;
            softPush.receiverFactor = targets.shadowReceiverFactor;
            softPush.height = targets.height;
            softPush.frameIndex = frameIndex;
            softPush.softSampleCount = softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP;
            softPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
            softPush.normalSlot = targets.bindless.gbufferNormal.slot();
            softPush.depthSlot = targets.bindless.gbufferDepth.slot();
            softPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
            softPush.visibilityStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
            commandList.setPushConstants(&softPush, sizeof(softPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }

        if(lightSpace && lightSpace->ready){
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            if(!RecordLightSpaceResolve(
                *lightSpace,
                frameIndex, softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.bindless.shadowSoftHalfAStorage.slot(), false,
                commandList, heap, m_graphics.gpuTiming(), *targets.shadowSoftHalfA
            ))
                return MakeUnexpected(Failure{});
            if(!m_lightSpaceShadow.m_csgDispatchLogged){
                m_lightSpaceShadow.m_csgDispatchLogged = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched CSG light-space shadows (hardware_compose=1, {} instances)"), static_cast<u64>(lightSpace->push.instanceCount));
            }
        }

        // Split resolve declares the trace fence; unsplit soft resolve phases retain their internal UAV fence.
        dispatchPrimarySoftShadowResolve(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, splitOpaqueSoftResolve, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates);
        return producedFrameIndex;
    }

    // Full-resolution inline-RayQuery fallback.

    {
        Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

        Core::ComputeState shadowState;
        shadowState.setPipeline(m_rayTracingState.m_shadowPipeline.get());
        commandList.setComputeState(shadowState);
        heap.bindCompute(commandList, *m_rayTracingState.m_shadowPipeline.get(), m_rayTracingState.m_tlasHeapHandle);
        ShadowRqPushConstants shadowPush;
        shadowPush.frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = shadowPush.frameIndex;
        shadowPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        shadowPush.normalSlot = targets.bindless.gbufferNormal.slot();
        shadowPush.depthSlot = targets.bindless.gbufferDepth.slot();
        shadowPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        shadowPush.visibilityStorageSlot = targets.bindless.shadowVisibilityStorage.slot();
        commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
        commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), 1u);
    }
    return producedFrameIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

