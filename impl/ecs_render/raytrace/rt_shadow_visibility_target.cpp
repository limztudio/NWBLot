// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_shadow_tasks.h>

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
    if(targets.shadowReceiverFactor != s_ShadowReceiverFactorHalf && targets.shadowReceiverFactor != s_ShadowReceiverFactorQuarter)
        return false;
    // Deferred lighting always samples this per-slot transmittance target.
    targets.shadowVisibilityFormat = Core::Format::RGBA16_FLOAT;

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setInUAV(true)
        // The graph-owned lagged-history copy may run on a dedicated Transfer family after the graphics/compute producers finish. Keep all three real transports concurrently shareable rather than fabricating aliases.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setName("engine/shadow/visibility")
    ;
    auto shadowVisibilityResult = m_graphics.createTexture(visibilityDesc);
    if(shadowVisibilityResult)
        targets.shadowVisibility = Move(*shadowVisibilityResult);
    else
        targets.shadowVisibility.reset();
    if(!shadowVisibilityResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow visibility target"));
        return false;
    }

    // Round up half-resolution scratch so odd extents retain coverage.
    targets.shadowCoarseTransmittanceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc coarseDesc;
    coarseDesc
        .setWidth((targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setHeight((targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowCoarseTransmittanceFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/shadow/coarse_transmittance")
    ;
    auto shadowCoarseTransmittanceResult = m_graphics.createTexture(coarseDesc);
    if(shadowCoarseTransmittanceResult)
        targets.shadowCoarseTransmittance = Move(*shadowCoarseTransmittanceResult);
    else
        targets.shadowCoarseTransmittance.reset();
    if(!shadowCoarseTransmittanceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"));
        return false;
    }

    // Ping-pong half-resolution soft visibility and geometry cache.
    targets.shadowSoftFormat = Core::Format::RGBA16_FLOAT;
    targets.shadowSoftGeometryFormat = Core::Format::RGBA16_FLOAT;
    const u32 softHalfWidth = (targets.width + targets.shadowReceiverFactor - 1u) / targets.shadowReceiverFactor;
    const u32 softHalfHeight = (targets.height + targets.shadowReceiverFactor - 1u) / targets.shadowReceiverFactor;

    Core::TextureDesc softHalfADesc;
    softHalfADesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowSoftFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/shadow/soft_half_a")
    ;
    auto shadowSoftHalfAResult = m_graphics.createTexture(softHalfADesc);
    if(shadowSoftHalfAResult)
        targets.shadowSoftHalfA = Move(*shadowSoftHalfAResult);
    else
        targets.shadowSoftHalfA.reset();
    if(!shadowSoftHalfAResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"));
        return false;
    }

    Core::TextureDesc softHalfBDesc = softHalfADesc;
    softHalfBDesc.setName("engine/shadow/soft_half_b");
    auto shadowSoftHalfBResult = m_graphics.createTexture(softHalfBDesc);
    if(shadowSoftHalfBResult)
        targets.shadowSoftHalfB = Move(*shadowSoftHalfBResult);
    else
        targets.shadowSoftHalfB.reset();
    if(!shadowSoftHalfBResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-B target"));
        return false;
    }

    Core::TextureDesc softGeometryDesc;
    softGeometryDesc.setWidth(softHalfWidth).setHeight(softHalfHeight).setFormat(targets.shadowSoftGeometryFormat).setInUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/shadow/soft_geometry");
    auto shadowSoftGeometryResult = m_graphics.createTexture(softGeometryDesc);
    if(shadowSoftGeometryResult)
        targets.shadowSoftGeometry = Move(*shadowSoftGeometryResult);
    else
        targets.shadowSoftGeometry.reset();
    if(!shadowSoftGeometryResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"));
        return false;
    }

    // Recreated history must not reproject through the previous target's matrix.
    Core::TextureDesc shadowHistADesc = softHalfADesc;
    shadowHistADesc.setName("engine/shadow/hist_a");
    auto shadowHistAResult = m_graphics.createTexture(shadowHistADesc);
    if(shadowHistAResult)
        targets.shadowHistA = Move(*shadowHistAResult);
    else
        targets.shadowHistA.reset();
    if(!shadowHistAResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"));
        return false;
    }
    Core::TextureDesc shadowHistBDesc = softHalfADesc;
    shadowHistBDesc.setName("engine/shadow/hist_b");
    auto shadowHistBResult = m_graphics.createTexture(shadowHistBDesc);
    if(shadowHistBResult)
        targets.shadowHistB = Move(*shadowHistBResult);
    else
        targets.shadowHistB.reset();
    if(!shadowHistBResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"));
        return false;
    }
    Core::TextureDesc shadowMomentsADesc = softHalfADesc;
    shadowMomentsADesc.setName("engine/shadow/moments_a");
    auto shadowMomentsAResult = m_graphics.createTexture(shadowMomentsADesc);
    if(shadowMomentsAResult)
        targets.shadowMomentsA = Move(*shadowMomentsAResult);
    else
        targets.shadowMomentsA.reset();
    if(!shadowMomentsAResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"));
        return false;
    }
    Core::TextureDesc shadowMomentsBDesc = softHalfADesc;
    shadowMomentsBDesc.setName("engine/shadow/moments_b");
    auto shadowMomentsBResult = m_graphics.createTexture(shadowMomentsBDesc);
    if(shadowMomentsBResult)
        targets.shadowMomentsB = Move(*shadowMomentsBResult);
    else
        targets.shadowMomentsB.reset();
    if(!shadowMomentsBResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"));
        return false;
    }
    Core::TextureDesc shadowSoftGeometryPrevDesc = softGeometryDesc;
    shadowSoftGeometryPrevDesc.setName("engine/shadow/soft_geometry_prev");
    auto shadowSoftGeometryPrevResult = m_graphics.createTexture(shadowSoftGeometryPrevDesc);
    if(shadowSoftGeometryPrevResult)
        targets.shadowSoftGeometryPrev = Move(*shadowSoftGeometryPrevResult);
    else
        targets.shadowSoftGeometryPrev.reset();
    if(!shadowSoftGeometryPrevResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow previous-frame geometry cache target"));
        return false;
    }
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_rayTracingState.m_transparentShadowSamplingHistory.reset();
    m_rayTracingState.m_softShadowTemporalSeeded = false;
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    m_rayTracingState.m_prevWorldToClipValid = false;
    m_rayTracingState.m_softShadowHistoryFrontIsA = 1u;

    // Transparent temporal history shares geometry and the opaque history selector.
    Core::TextureDesc transparentSoftHalfDesc = softHalfADesc;
    transparentSoftHalfDesc.setName("engine/shadow/transparent_soft_half");
    auto transparentSoftHalfResult = m_graphics.createTexture(transparentSoftHalfDesc);
    if(transparentSoftHalfResult)
        targets.transparentSoftHalf = Move(*transparentSoftHalfResult);
    else
        targets.transparentSoftHalf.reset();
    if(!transparentSoftHalfResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"));
        return false;
    }
    Core::TextureDesc transparentHistADesc = softHalfADesc;
    transparentHistADesc.setName("engine/shadow/transparent_hist_a");
    auto transparentHistAResult = m_graphics.createTexture(transparentHistADesc);
    if(transparentHistAResult)
        targets.transparentHistA = Move(*transparentHistAResult);
    else
        targets.transparentHistA.reset();
    if(!transparentHistAResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"));
        return false;
    }
    Core::TextureDesc transparentHistBDesc = softHalfADesc;
    transparentHistBDesc.setName("engine/shadow/transparent_hist_b");
    auto transparentHistBResult = m_graphics.createTexture(transparentHistBDesc);
    if(transparentHistBResult)
        targets.transparentHistB = Move(*transparentHistBResult);
    else
        targets.transparentHistB.reset();
    if(!transparentHistBResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"));
        return false;
    }
    Core::TextureDesc transparentMomentsADesc = softHalfADesc;
    transparentMomentsADesc.setName("engine/shadow/transparent_moments_a");
    auto transparentMomentsAResult = m_graphics.createTexture(transparentMomentsADesc);
    if(transparentMomentsAResult)
        targets.transparentMomentsA = Move(*transparentMomentsAResult);
    else
        targets.transparentMomentsA.reset();
    if(!transparentMomentsAResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"));
        return false;
    }
    Core::TextureDesc transparentMomentsBDesc = softHalfADesc;
    transparentMomentsBDesc.setName("engine/shadow/transparent_moments_b");
    auto transparentMomentsBResult = m_graphics.createTexture(transparentMomentsBDesc);
    if(transparentMomentsBResult)
        targets.transparentMomentsB = Move(*transparentMomentsBResult);
    else
        targets.transparentMomentsB.reset();
    if(!transparentMomentsBResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"));
        return false;
    }

    // All resolve allocations belong to this target generation. Verify both history selectors once, before publication.
    constexpr u32 s_HistorySelectorCount = 2u;
    for(u32 selector = 0u; selector < s_HistorySelectorCount; ++selector){
        const bool frontIsA = selector != 0u;
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
    if(!heap.isInitialized() || !RayTracingDetail::ReplaceHeapBuffer(heap, *edgeListBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_swShadowEdgeListHeapHandle)){
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
    const bool splitSoftTransparentFold,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const bool splitOpaqueSoftResolve,
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
                commandList, heap, m_graphics.gpuTiming(), *lightSpace, *targets.shadowSoftHalfA,
                frameIndex, softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.bindless.shadowSoftHalfAStorage.slot(), false
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

