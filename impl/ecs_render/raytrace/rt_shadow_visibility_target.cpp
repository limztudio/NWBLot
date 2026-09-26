// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_shadow_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
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
        // The graph-owned lagged-history copy may run on a dedicated Transfer family after the graphics/compute
        // producers finish. Keep all three real transports concurrently shareable rather than fabricating aliases.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/shadow/visibility")
    ;
    targets.shadowVisibility = m_graphics.createTexture(visibilityDesc);
    if(!targets.shadowVisibility){
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
    targets.shadowCoarseTransmittance = m_graphics.createTexture(coarseDesc);
    if(!targets.shadowCoarseTransmittance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"));
        return false;
    }

    // Ping-pong half-resolution soft visibility and geometry cache.
    targets.shadowSoftFormat = Core::Format::RGBA16_FLOAT;
    targets.shadowSoftGeometryFormat = Core::Format::RGBA16_FLOAT;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;

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
    targets.shadowSoftHalfA = m_graphics.createTexture(softHalfADesc);
    if(!targets.shadowSoftHalfA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"));
        return false;
    }

    Core::TextureDesc softHalfBDesc = softHalfADesc;
    softHalfBDesc.setName("engine/shadow/soft_half_b");
    targets.shadowSoftHalfB = m_graphics.createTexture(softHalfBDesc);
    if(!targets.shadowSoftHalfB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-B target"));
        return false;
    }

    Core::TextureDesc softGeometryDesc;
    softGeometryDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setFormat(targets.shadowSoftGeometryFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/shadow/soft_geometry")
    ;
    targets.shadowSoftGeometry = m_graphics.createTexture(softGeometryDesc);
    if(!targets.shadowSoftGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"));
        return false;
    }

    // Recreated history must not reproject through the previous target's matrix.
    Core::TextureDesc shadowHistADesc = softHalfADesc;
    shadowHistADesc.setName("engine/shadow/hist_a");
    targets.shadowHistA = m_graphics.createTexture(shadowHistADesc);
    if(!targets.shadowHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"));
        return false;
    }
    Core::TextureDesc shadowHistBDesc = softHalfADesc;
    shadowHistBDesc.setName("engine/shadow/hist_b");
    targets.shadowHistB = m_graphics.createTexture(shadowHistBDesc);
    if(!targets.shadowHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"));
        return false;
    }
    Core::TextureDesc shadowMomentsADesc = softHalfADesc;
    shadowMomentsADesc.setName("engine/shadow/moments_a");
    targets.shadowMomentsA = m_graphics.createTexture(shadowMomentsADesc);
    if(!targets.shadowMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"));
        return false;
    }
    Core::TextureDesc shadowMomentsBDesc = softHalfADesc;
    shadowMomentsBDesc.setName("engine/shadow/moments_b");
    targets.shadowMomentsB = m_graphics.createTexture(shadowMomentsBDesc);
    if(!targets.shadowMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"));
        return false;
    }
    Core::TextureDesc shadowSoftGeometryPrevDesc = softGeometryDesc;
    shadowSoftGeometryPrevDesc.setName("engine/shadow/soft_geometry_prev");
    targets.shadowSoftGeometryPrev = m_graphics.createTexture(shadowSoftGeometryPrevDesc);
    if(!targets.shadowSoftGeometryPrev){
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
    targets.transparentSoftHalf = m_graphics.createTexture(transparentSoftHalfDesc);
    if(!targets.transparentSoftHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"));
        return false;
    }
    Core::TextureDesc transparentHistADesc = softHalfADesc;
    transparentHistADesc.setName("engine/shadow/transparent_hist_a");
    targets.transparentHistA = m_graphics.createTexture(transparentHistADesc);
    if(!targets.transparentHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"));
        return false;
    }
    Core::TextureDesc transparentHistBDesc = softHalfADesc;
    transparentHistBDesc.setName("engine/shadow/transparent_hist_b");
    targets.transparentHistB = m_graphics.createTexture(transparentHistBDesc);
    if(!targets.transparentHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"));
        return false;
    }
    Core::TextureDesc transparentMomentsADesc = softHalfADesc;
    transparentMomentsADesc.setName("engine/shadow/transparent_moments_a");
    targets.transparentMomentsA = m_graphics.createTexture(transparentMomentsADesc);
    if(!targets.transparentMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"));
        return false;
    }
    Core::TextureDesc transparentMomentsBDesc = softHalfADesc;
    transparentMomentsBDesc.setName("engine/shadow/transparent_moments_b");
    targets.transparentMomentsB = m_graphics.createTexture(transparentMomentsBDesc);
    if(!targets.transparentMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"));
        return false;
    }

    // All resolve allocations belong to this target generation. Verify both history selectors once, before publication.
    for(u32 selector = 0u; selector < 2u; ++selector){
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
    const u32 edgeListCapacityRecords = targets.width * targets.height;
    Core::BufferDesc edgeListDesc;
    edgeListDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * static_cast<u64>(NWB_SW_SHADOW_EDGE_RECORD_WORDS) * static_cast<u64>(edgeListCapacityRecords))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("sw_shadow_edge_list"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle edgeListBuffer = m_graphics.createBuffer(edgeListDesc);
    if(!edgeListBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !RayTracingDetail::ReplaceHeapBuffer(
            heap,
            *edgeListBuffer.get(),
            Core::GpuDescriptorClass::StorageBuffer,
            true,
            m_rayTracingState.m_swShadowEdgeListHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW shadow edge-list buffer in the descriptor heap"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    m_rayTracingState.m_swShadowEdgeListBuffer = Move(edgeListBuffer);
    m_rayTracingState.m_swShadowEdgeListCapacity = edgeListCapacityRecords;
    return true;
}

bool RendererRayTracingSystem::renderShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool splitSoftTransparentFold,
    u32* const opaqueFrameIndex,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const bool splitOpaqueSoftResolve
){
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    NWB_ASSERT(deferredLightingResources.valid());
    if(!targets.shadowVisibility)
        return false;
    if(!m_rayTracingState.m_tlas || !m_rayTracingState.m_shadowPipeline)
        return false;

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
        return false;
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    if(!graphEntryStatesOwned){
        // Heap-selected resources still need explicit state transitions for direct compatibility callers.
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setAccelStructState(m_rayTracingState.m_tlas.get(), Core::ResourceStates::AccelStructRead);
    }

    // Hardware tracing shares the half-resolution soft-shadow resolve when available.
    if(m_rayTracingState.m_softShadowReady && m_rayTracingState.m_shadowSoftPipeline && m_rayTracingState.m_softShadowSlotMask != 0u){
        const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));

        if(!graphEntryStatesOwned){
            commandList.setTextureState(
                targets.shadowSoftHalfA.get(),
                ECSRenderDetail::s_ShadowVisibilitySubresources,
                Core::ResourceStates::UnorderedAccess
            );
            commandList.commitBarriers();
        }

        // Resolve reuses the trace outputs as UAV/SRV scratch.
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
        // Temporal merge writes history before the wavelet pass reads it.
        if(m_rayTracingState.m_softShadowTemporalReady){
            commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
        }

        // Advance the jitter sequence once for the primary shadow producer.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;

        {
            Core::GpuTimingMeasure opaqueTraceTiming(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_ShadowOpaqueTrace,
                m_graphics.getDevice(),
                commandList
            );
            Core::ComputeState softState;
            softState.setPipeline(m_rayTracingState.m_shadowSoftPipeline.get());
            commandList.setComputeState(softState);
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowSoftPipeline.get(), m_rayTracingState.m_tlasHeapHandle);

            ShadowRqSoftPushConstants softPush;
            softPush.width = targets.width;
            softPush.height = targets.height;
            softPush.frameIndex = frameIndex;
            softPush.softSampleCount = softShadowTemporalHistoryUsable()
                ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP
                : NWB_SW_SHADOW_SOFT_SPP;
            softPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
            softPush.normalSlot = targets.bindless.gbufferNormal.slot();
            softPush.depthSlot = targets.bindless.gbufferDepth.slot();
            softPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
            softPush.visibilityStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
            commandList.setPushConstants(&softPush, sizeof(softPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }

        // The split resolver declares this same-UAV dependency, so its graph prologue owns the trace fence. Direct
        // and unsplit compatibility paths retain the established local fence.
        if(!splitOpaqueSoftResolve){
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }

        // A prepared graph may expose the opaque geometry-to-resolve handoff before the transparent-fold tail.
        dispatchSoftShadowDenoiseAndTransparentFold(
            commandList,
            targets,
            deferredLightingResources,
            frameIndex,
            softGroupsX,
            softGroupsY,
            graphEntryStatesOwned,
            true,
            !splitOpaqueSoftResolve,
            !splitSoftTransparentFold,
            !splitSoftTransparentFold,
            false,
            false,
            false,
            graphOwnsOpaqueTemporalMergeEntryStates,
            false,
            !splitOpaqueSoftResolve,
            false
        );
        if(splitSoftTransparentFold){
            NWB_ASSERT(opaqueFrameIndex);
            if(opaqueFrameIndex)
                *opaqueFrameIndex = frameIndex;
        }
        return true;
    }

    // Full-resolution inline-RayQuery fallback.
    if(!graphEntryStatesOwned){
        commandList.setTextureState(
            targets.shadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::ResourceStates::UnorderedAccess
        );
        commandList.commitBarriers();
    }

    {
        Core::GpuTimingMeasure opaqueTraceTiming(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_ShadowOpaqueTrace,
            m_graphics.getDevice(),
            commandList
        );
        Core::ComputeState shadowState;
        shadowState.setPipeline(m_rayTracingState.m_shadowPipeline.get());
        commandList.setComputeState(shadowState);
        heap.bindCompute(commandList, *m_rayTracingState.m_shadowPipeline.get(), m_rayTracingState.m_tlasHeapHandle);
        ShadowRqPushConstants shadowPush;
        shadowPush.frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        shadowPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        shadowPush.normalSlot = targets.bindless.gbufferNormal.slot();
        shadowPush.depthSlot = targets.bindless.gbufferDepth.slot();
        shadowPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        shadowPush.visibilityStorageSlot = targets.bindless.shadowVisibilityStorage.slot();
        commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
        commandList.dispatch(
            DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
            DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
            1u
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

