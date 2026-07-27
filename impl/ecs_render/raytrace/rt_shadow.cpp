// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    if(handle.valid())
        return true;

    auto& device = *graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!__hidden_raytracing_system::EnsureHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material context buffer in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    auto& device = *graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!__hidden_raytracing_system::ReplaceHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to replace ray-trace material-context heap descriptor"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureRayTraceMaterialContextSlotsBuffer(){
    if(rayTracingState().m_rayTraceMaterialContextSlotsBuffer)
        return true;

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(RayTraceMaterialContextSlots))
        .setIsConstantBuffer(true)
        .setDebugName(Name("raytrace_material_context_slots"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_rayTraceMaterialContextSlotsBuffer = graphics().createBuffer(slotsBufferDesc);
    if(!rayTracingState().m_rayTraceMaterialContextSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create ray-trace material-context slot buffer"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::uploadRayTraceMaterialContextSlots(Core::CommandList& commandList){
    if(!ensureRayTraceMaterialContextSlotsBuffer())
        return false;

    RayTraceMaterialContextSlots slots;
    const auto resolveStorageSlot = [](const Core::Buffer* buffer, const Core::GpuDescriptorHandle handle, u32& outSlot) -> bool{
        if(!buffer){
            outSlot = 0u;
            return true;
        }
        if(!handle.valid() || handle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
            return false;
        outSlot = handle.slot();
        return true;
    };

    const bool complete =
        resolveStorageSlot(rayTracingState().m_sceneBvhNodeBuffer.get(), rayTracingState().m_sceneBvhNodeHeapHandle, slots.sceneBvhNodes)
        && resolveStorageSlot(rayTracingState().m_sceneInstanceBuffer.get(), rayTracingState().m_sceneInstanceHeapHandle, slots.sceneInstances)
        && resolveStorageSlot(rayTracingState().m_shadowInstanceMaterialBuffer.get(), rayTracingState().m_shadowInstanceMaterialHeapHandle, slots.instanceMaterial)
        && resolveStorageSlot(rayTracingState().m_shadowMaterialTypedBuffer.get(), rayTracingState().m_shadowMaterialTypedHeapHandle, slots.materialTyped)
        && resolveStorageSlot(rayTracingState().m_shadowInstanceBuffer.get(), rayTracingState().m_shadowInstanceHeapHandle, slots.meshInstances)
    ;
    if(!complete){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return false;
    }

    Core::Buffer* const slotsBuffer = rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get();
    commandList.setBufferState(slotsBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(slotsBuffer, &slots, sizeof(slots));
    commandList.setBufferState(slotsBuffer, Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    auto& device = *graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !__hidden_raytracing_system::EnsureHeapBuffer(
            heap,
            *slotsBuffer,
            Core::GpuDescriptorClass::UniformBuffer,
            false,
            rayTracingState().m_shadowMaterialContextSlotsHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material-context selector in the descriptor heap"));
        return false;
    }
    return true;
}

void RendererRayTracingSystem::releaseRayTraceMaterialContextHeapHandles(){
    auto& device = *graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(rayTracingState().m_sceneBvhNodeHeapHandle);
        heap.free(rayTracingState().m_sceneInstanceHeapHandle);
        heap.free(rayTracingState().m_shadowInstanceMaterialHeapHandle);
        heap.free(rayTracingState().m_shadowMaterialTypedHeapHandle);
        heap.free(rayTracingState().m_shadowInstanceHeapHandle);
        heap.free(rayTracingState().m_shadowMaterialContextSlotsHeapHandle);
        heap.free(rayTracingState().m_swShadowEdgeStatsHeapHandle);
        heap.free(rayTracingState().m_swShadowEdgeCounterHeapHandle);
        heap.free(rayTracingState().m_swShadowEdgeListHeapHandle);
        heap.free(rayTracingState().m_swShadowIndirectArgsHeapHandle);
    }
    rayTracingState().m_sceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_sceneInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_shadowMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowEdgeStatsHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
    // The shadow-visibility image is the shared output of the shadow subsystem: both the hardware ray-traced
    // and the software-BVH backends write per-light colored transmittance into it, one Texture2DArray layer
    // per shadow slot (NWB_SCENE_SHADOW_SLOT_COUNT). The deferred lighting pass always samples it, so it is
    // allocated unconditionally and cleared to "all lit" (white) each frame (then overwritten by whichever
    // backend runs) to keep a single binding/shader path regardless of ray-tracing support.
    targets.shadowVisibilityFormat = Core::Format::RGBA16_FLOAT;

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setInUAV(true)
        .setName("engine/shadow/visibility")
    ;
    targets.shadowVisibility = graphics().createTexture(visibilityDesc);
    if(!targets.shadowVisibility){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow visibility target"));
        return false;
    }

    // Stage-2 adaptive transparent shadow scratch: a HALF-res sibling of the visibility target (same slot layers, RGBA16F)
    // the coarse software trace writes one transmittance per 2x2 block into and the adaptive resolve interpolates/refines.
    // Allocated alongside the visibility target so it shares the resize lifecycle (resetDeferredFrameTargets rebuilds the
    // SW shadow heap-slot payload, which selects whichever coarse handle is current). Round UP so a coarse texel covers its 2x2
    // block even for odd extents -- matching the caustic half-res buffers.
    targets.shadowCoarseTransmittanceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc coarseDesc;
    coarseDesc
        .setWidth((targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setHeight((targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowCoarseTransmittanceFormat)
        .setInUAV(true)
        .setName("engine/shadow/coarse_transmittance")
    ;
    targets.shadowCoarseTransmittance = graphics().createTexture(coarseDesc);
    if(!targets.shadowCoarseTransmittance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"));
        return false;
    }

    // Soft opaque shadow HALF-res targets: the two ping-pong soft buffers (RGBA16F Texture2DArrays, one
    // layer per shadow slot -- the jittered trace writes A, the a-trous resolve alternates A<->B, the upsample
    // reads B into the full-res visibility) + the single-layer packed geometry cache (octahedral normal + camera
    // distance + validity) the geometry downsample fills for the edge-stop. Half the render extent (rounded up so a
    // half texel covers its SOFT_FACTOR block for odd extents), matching the caustic half-res buffers. Allocated with
    // the visibility target so they share the resize lifecycle (resetDeferredFrameTargets rebuilds the resolve set).
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
        .setName("engine/shadow/soft_half_a")
    ;
    targets.shadowSoftHalfA = graphics().createTexture(softHalfADesc);
    if(!targets.shadowSoftHalfA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"));
        return false;
    }

    Core::TextureDesc softHalfBDesc = softHalfADesc;
    softHalfBDesc.setName("engine/shadow/soft_half_b");
    targets.shadowSoftHalfB = graphics().createTexture(softHalfBDesc);
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
        .setName("engine/shadow/soft_geometry")
    ;
    targets.shadowSoftGeometry = graphics().createTexture(softGeometryDesc);
    if(!targets.shadowSoftGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"));
        return false;
    }

    // Soft opaque shadow TEMPORAL accumulation HALF-res targets: the accumulated-visibility + moments ping-pong
    // Texture2DArrays (mirror softHalfADesc: NWB_SCENE_SHADOW_SLOT_COUNT layers, UAV) + the previous-frame single-layer
    // geometry cache (mirror softGeometryDesc). Allocated here so they share the resize lifecycle; a freshly (re)created
    // history holds no valid samples, so re-seed the temporal state (the next merge treats every pixel as n=0 = pure
    // current) and invalidate the stashed prev-frame worldToClip so a resize can't reproject through a stale matrix into
    // freshly-allocated garbage history.
    Core::TextureDesc shadowHistADesc = softHalfADesc;
    shadowHistADesc.setName("engine/shadow/hist_a");
    targets.shadowHistA = graphics().createTexture(shadowHistADesc);
    if(!targets.shadowHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"));
        return false;
    }
    Core::TextureDesc shadowHistBDesc = softHalfADesc;
    shadowHistBDesc.setName("engine/shadow/hist_b");
    targets.shadowHistB = graphics().createTexture(shadowHistBDesc);
    if(!targets.shadowHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"));
        return false;
    }
    Core::TextureDesc shadowMomentsADesc = softHalfADesc;
    shadowMomentsADesc.setName("engine/shadow/moments_a");
    targets.shadowMomentsA = graphics().createTexture(shadowMomentsADesc);
    if(!targets.shadowMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"));
        return false;
    }
    Core::TextureDesc shadowMomentsBDesc = softHalfADesc;
    shadowMomentsBDesc.setName("engine/shadow/moments_b");
    targets.shadowMomentsB = graphics().createTexture(shadowMomentsBDesc);
    if(!targets.shadowMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"));
        return false;
    }
    Core::TextureDesc shadowSoftGeometryPrevDesc = softGeometryDesc;
    shadowSoftGeometryPrevDesc.setName("engine/shadow/soft_geometry_prev");
    targets.shadowSoftGeometryPrev = graphics().createTexture(shadowSoftGeometryPrevDesc);
    if(!targets.shadowSoftGeometryPrev){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow previous-frame geometry cache target"));
        return false;
    }
    rayTracingState().m_softShadowTemporalSeeded = false;
    rayTracingState().m_prevWorldToClipValid = false;
    rayTracingState().m_softShadowHistoryFrontIsA = 1u;

    // Soft COLORED TRANSPARENT shadow HALF-res targets: the PARALLEL colored pipeline's buffers -- the raw colored
    // soft trace output + its accumulated-visibility & moments ping-pong (mirroring the opaque set exactly: same softHalfADesc
    // format/extent/layers/UAV). The geometry cache + prevWorldToClip are SHARED (not duplicated). Allocated here so they
    // share the resize lifecycle; the transparent history uses the SAME m_softShadowHistoryFrontIsA selector as the opaque
    // history (one frame-end flip covers both), so a freshly (re)created transparent history is covered by the temporal
    // re-seed above (the first merge treats every pixel as n=0 = pure current).
    Core::TextureDesc transparentSoftHalfDesc = softHalfADesc;
    transparentSoftHalfDesc.setName("engine/shadow/transparent_soft_half");
    targets.transparentSoftHalf = graphics().createTexture(transparentSoftHalfDesc);
    if(!targets.transparentSoftHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"));
        return false;
    }
    Core::TextureDesc transparentHistADesc = softHalfADesc;
    transparentHistADesc.setName("engine/shadow/transparent_hist_a");
    targets.transparentHistA = graphics().createTexture(transparentHistADesc);
    if(!targets.transparentHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"));
        return false;
    }
    Core::TextureDesc transparentHistBDesc = softHalfADesc;
    transparentHistBDesc.setName("engine/shadow/transparent_hist_b");
    targets.transparentHistB = graphics().createTexture(transparentHistBDesc);
    if(!targets.transparentHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"));
        return false;
    }
    Core::TextureDesc transparentMomentsADesc = softHalfADesc;
    transparentMomentsADesc.setName("engine/shadow/transparent_moments_a");
    targets.transparentMomentsA = graphics().createTexture(transparentMomentsADesc);
    if(!targets.transparentMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"));
        return false;
    }
    Core::TextureDesc transparentMomentsBDesc = softHalfADesc;
    transparentMomentsBDesc.setName("engine/shadow/transparent_moments_b");
    targets.transparentMomentsB = graphics().createTexture(transparentMomentsBDesc);
    if(!targets.transparentMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"));
        return false;
    }

    // Compacted edge list (recreated on resize alongside the visibility/coarse targets, so the SW shadow heap-slot
    // refresh that already triggers on the visibility-pointer change refreshes it too). Each record is NWB_SW_SHADOW_EDGE_RECORD_WORDS
    // u32. Lives on rayTracingState (a buffer, not a frame target) since it is shadow-subsystem scratch the lighting never samples.
    // SIZING: capacity = one record per full-res pixel, but the classify pass appends one record per (pixel, active shadow slot) edge, so the
    // TIGHT worst-case demand is width*height*activeShadowSlots (slots capped at NWB_SCENE_SHADOW_SLOT_COUNT=8). One-per-pixel is
    // deliberately NOT that bound: at the measured ~3% edge fraction the demand is ~0.03*slots per pixel, so width*height is 4-16x
    // the realistic worst case even with many lights -- and provisioning the 8x tight bound would burn ~73MB of 96%-empty scratch.
    // Overflow (a pathological all-edge multi-slot frame) is SAFE, not corrupt: the append still increments the counter but the
    // indexed list write is guarded by edgeCapacity, the build-args pass clamps the trace count to it, and the indirect
    // pass's tail guard reads only in-range records -- so overflowed edges take the bilinear-interpolated fallback.
    const u32 edgeListCapacityRecords = targets.width * targets.height;
    Core::BufferDesc edgeListDesc;
    edgeListDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * static_cast<u64>(NWB_SW_SHADOW_EDGE_RECORD_WORDS) * static_cast<u64>(edgeListCapacityRecords))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("sw_shadow_edge_list"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle edgeListBuffer = graphics().createBuffer(edgeListDesc);
    if(!edgeListBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        rayTracingState().m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    auto& device = *graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !__hidden_raytracing_system::ReplaceHeapBuffer(
            heap,
            *edgeListBuffer.get(),
            Core::GpuDescriptorClass::StorageBuffer,
            true,
            rayTracingState().m_swShadowEdgeListHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW shadow edge-list buffer in the descriptor heap"));
        rayTracingState().m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    rayTracingState().m_swShadowEdgeListBuffer = Move(edgeListBuffer);
    rayTracingState().m_swShadowEdgeListCapacity = edgeListCapacityRecords;
    return true;
}

bool RendererRayTracingSystem::renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return false;
    if(!rayTracingState().m_tlas || !rayTracingState().m_shadowPipeline)
        return false;

    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !rayTracingState().m_tlasHeapHandle.valid()
        || !targets.bindless.valid()
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow trace heap resources are incomplete"));
        return false;
    }

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // All resources are selected by global heap slots; transition every backing object explicitly.
    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setAccelStructState(rayTracingState().m_tlas.get(), Core::ResourceStates::AccelStructRead);

    // When the soft resources are ready this frame and at least one light holds a shadow slot, route the HW opaque shadow
    // through the same half-res soft denoise chain the SW path uses. The HW opaque-soft RayQuery trace casts SPP
    // cone-jittered opaque rays per half-res pixel into shadowSoftHalfA, then the shared
    // dispatchSoftShadowDenoiseAndTransparentFold denoises it into the full-res visibility.
    if(rayTracingState().m_softShadowReady && rayTracingState().m_shadowSoftPipeline && rayTracingState().m_softShadowSlotMask != 0u){
        const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));

        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // Enable UAV barriers on the soft buffers + geometry cache for the resolve (mirror the SW soft block). The
        // trace write of soft-A -> the resolve PREPARE / merge reads it; soft-B + geometry are the resolve scratch.
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
        // Temporal accumulator buffers: enable UAV barriers so the merge's history/moments writes are ordered before the
        // a-trous PREPARE reads the accumulated history as an SRV. No-op when temporal is off (the merge never dispatches).
        if(rayTracingState().m_softShadowTemporalReady){
            commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
        }

        // HW opaque-soft trace: one cone-jittered opaque RayQuery sample-set per HALF-res pixel into soft-A (all slot
        // lights at once). Advance the per-frame cone-jitter seed once (the HW RayQuery path is the primary shadow
        // producer this frame, mutually exclusive with the no-RT software traversal).
        const u32 frameIndex = rayTracingState().m_softShadowFrameIndex++;

        Core::ComputeState softState;
        softState.setPipeline(rayTracingState().m_shadowSoftPipeline.get());
        commandList.setComputeState(softState);
        heap.bindCompute(commandList, *rayTracingState().m_shadowSoftPipeline.get(), rayTracingState().m_tlasHeapHandle);

        ShadowRqSoftPushConstants softPush;
        softPush.width = targets.width;
        softPush.height = targets.height;
        softPush.frameIndex = frameIndex;
        softPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        softPush.normalSlot = targets.bindless.gbufferNormal.slot();
        softPush.depthSlot = targets.bindless.gbufferDepth.slot();
        softPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        softPush.visibilityStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
        commandList.setPushConstants(&softPush, sizeof(softPush));
        commandList.dispatch(softGroupsX, softGroupsY, 1u);

        // Preserve the UAV write boundary before the heap-selected resolve reads soft-A as an SRV.
        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // Denoise the half-res soft-A trace into the full-res visibility. When m_softTransparentReady is true, the same
        // chain also traces colored transmittance against the transparent-only software scene BVH and multiplies the
        // denoised result onto the soft-opaque visibility. Otherwise this produces only the soft opaque shadow, and
        // system.cpp may run the hybrid multiply fallback.
        dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, frameIndex, softGroupsX, softGroupsY);
        return true;
    }

    // Soft path not ready -> the existing FULL-resolution 1-spp inline-RayQuery fallback (never a regression). One
    // occlusion ray per output pixel, written straight into the full-res visibility array the deferred lighting samples.
    // The shader reads its dispatch bounds from the output's own dimensions.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::ComputeState shadowState;
    shadowState.setPipeline(rayTracingState().m_shadowPipeline.get());
    commandList.setComputeState(shadowState);
    heap.bindCompute(commandList, *rayTracingState().m_shadowPipeline.get(), rayTracingState().m_tlasHeapHandle);
    // Soft shadow cone-jitter: advance the per-frame seed once here. A soft light samples inside its source cone; a
    // zero-radius light jitters to the axis exactly and keeps the hard-shadow result.
    ShadowRqPushConstants shadowPush;
    shadowPush.frameIndex = rayTracingState().m_softShadowFrameIndex++;
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
    return true;
}

void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // White (full transmittance) across every slot layer = fully lit. This is the default the deferred
    // lighting pass samples whenever no shadow backend wrote the image this frame (ray tracing unavailable,
    // no trace-able geometry, or a trace that could not be dispatched), and the value every light without a
    // shadow slot keeps.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets, bool multiplyOntoOpaque){
    // Software shadow traversal. Two callers:
    //  - No-RayQuery fallback (multiplyOntoOpaque=false): the only shadow backend; traces ALL occluders and OVERWRITES
    //    the visibility (opaque blocks + transparent tints).
    //  - Hybrid on RT hardware (multiplyOntoOpaque=true): the HW RayQuery pass (renderShadowVisibility) already wrote
    //    the opaque binary mask; this traces the TRANSPARENT-ONLY scene BVH and MULTIPLIES its colored transmittance
    //    onto that mask. Whether the SW scene BVH holds all occluders or only the transparent ones is decided by
    //    buildSceneSwBvh; this pass only needs to know to multiply rather than overwrite.
    if(!targets.shadowVisibility)
        return false;
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    // No software scene BVH this frame (no traceable instances) -> the caller clears the mask to all-lit.
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhInstanceCount == 0u)
        return false;
    // Every decomposed pass uses the global heap. Validate the selector and persistent work-buffer generations before
    // recording any dispatch; target storage handles are recreated with the deferred target generation.
    if(!rayTracingState().m_swShadowOpaquePrepassPipeline || rayTracingState().m_swShadowMeshCount == 0u)
        return false;

    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !targets.bindless.valid()
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_shadowMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowCoarseTransmittanceStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.transparentSoftHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowEdgeStatsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowEdgeCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowEdgeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_swShadowIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-shadow heap resources are incomplete"));
        return false;
    }

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The per-mesh BVH node buffers were left in UnorderedAccess by the build pass; stage every heap-selected
    // software traversal input before dispatch.
    transitionSwShadowTraversalResources(commandList);
    if(rayTracingState().m_shadowInstanceBuffer)
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
    // The trace selects its G-buffer, scene-shading, and light-list inputs through the descriptor heap, so their
    // resource states are explicit because the heap does not encode them.
    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
    // The two software sub-passes that write the visibility UAV (the opaque pre-pass + the transparent resolve/multiply)
    // need a UAV barrier between them, and the Stage-2 coarse->resolve handoff needs one on the coarse texture. Enable
    // UAV barriers on both so each commitBarriers between dispatches syncs the read-after-write hazard on the same image.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true);
    commandList.commitBarriers();

    // Set 0 is the automatically bound push-only gap; each pass state carries no local descriptor object.
    const auto passState = [&](const Core::ComputePipelineHandle& pipeline){
        Core::ComputeState state;
        state.setPipeline(pipeline.get());
        return state;
    };

    // SW traversal accesses every resource through the descriptor heap, so bind tables after each ComputeState.
    const auto bindPassHeap = [&](const Core::ComputePipelineHandle& pipeline){
        heap.bindCompute(commandList, *pipeline.get());
    };

    const auto makePush = [&](){
        SwShadowHeapPushConstants push;
        push.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        push.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        push.materialContextSlotsHeapSlot = rayTracingState().m_shadowMaterialContextSlotsHeapHandle.slot();
        push.visibilityStorageSlot = targets.bindless.shadowVisibilityStorage.slot();
        push.coarseStorageSlot = targets.bindless.shadowCoarseTransmittanceStorage.slot();
        push.softHalfStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
        push.transparentSoftHalfStorageSlot = targets.bindless.transparentSoftHalfStorage.slot();
        push.edgeStatsStorageSlot = rayTracingState().m_swShadowEdgeStatsHeapHandle.slot();
        push.edgeCounterStorageSlot = rayTracingState().m_swShadowEdgeCounterHeapHandle.slot();
        push.edgeListStorageSlot = rayTracingState().m_swShadowEdgeListHeapHandle.slot();
        push.indirectArgsStorageSlot = rayTracingState().m_swShadowIndirectArgsHeapHandle.slot();
        return push;
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);
    const u32 coarseWidth = (targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseHeight = (targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseGroupsX = DivideUp(coarseWidth, groupSize);
    const u32 coarseGroupsY = DivideUp(coarseHeight, groupSize);

    // Set true once the soft colored-transparent fold has multiplied its denoised transmittance onto the soft-opaque
    // visibility. The transparent coarse/adaptive/uniform fallback below is skipped so the colored shadow is not folded
    // twice.
    bool softTransparentRan = false;

    // No-RayQuery software path: there is no HW opaque mask, so first write the full-res OPAQUE binary mask, then fold the
    // transparent colored shadow onto it. This mirrors the hybrid path (HW opaque mask + transparent) while keeping hard
    // opaque shadows full-res sharp.
    if(!multiplyOntoOpaque){
        // The soft pipeline overwrites every slot's visibility at upsample, so skip the full-res opaque prepass when
        // soft will run; keep it as the fallback when soft is not ready this frame.
        const bool softWillRun = rayTracingState().m_softShadowReady && rayTracingState().m_softShadowSlotMask != 0u;
        if(!softWillRun){
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants opaquePush = makePush();
            opaquePush.width = targets.width;
            opaquePush.height = targets.height;
            commandList.setComputeState(passState(rayTracingState().m_swShadowOpaquePrepassPipeline));
            bindPassHeap(rayTracingState().m_swShadowOpaquePrepassPipeline);
            commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }

        // Sync before the transparent pass. The opaque mask (the prepass, or the soft resolve below, wrote shadowVisibility)
        // -> the transparent pass reads+multiplies it: a write->read/write hazard the visibility UAV barrier covers, so it
        // stays UnorderedAccess. The transparent coarse pass writes its own storage image next.
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // Soft opaque shadow (all light types): half-res jittered opaque trace (directional softens by its constant angular
        // radius, point/spot by the distance-dependent cone their source sphere subtends -- the jitter is type-aware inside
        // the trace) -> geometry downsample -> a-trous denoise -> bilateral upsample, OVERWRITING every slot's full-res
        // visibility. Runs only when the resolve resources are ready this frame AND at least one light holds a shadow slot
        // (softWillRun); else the full-res prepass mask above is the shadow (a clean fallback). The transparent pass below
        // still folds its colored shadow onto the (now soft) opaque mask, so transparent colored shadow keeps working.
        if(softWillRun){
            const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
            const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
            const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
            const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);

            // Advance the per-frame cone-jitter seed once (the no-RT software traversal is the primary shadow producer
            // this frame, mutually exclusive with the HW RayQuery path).
            const u32 frameIndex = rayTracingState().m_softShadowFrameIndex++;

            // Soft opaque trace: one cone-jittered opaque visibility sample per HALF-res pixel into soft-A (all slot
            // lights at once). Enable UAV barriers on the soft buffers + geometry cache for the resolve.
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
            // Stage-3 temporal accumulator buffers retain UAV ordering before the heap-selected merge/resolve changes
            // their sampled/storage roles. No-op when temporal is off (the merge never dispatches).
            if(rayTracingState().m_softShadowTemporalReady){
                commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
            }

            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants softTracePush = makePush();
            softTracePush.width = targets.width;
            softTracePush.height = targets.height;
            softTracePush.frameIndex = frameIndex;
            commandList.setComputeState(passState(rayTracingState().m_swShadowSoftOpaquePipeline));
            bindPassHeap(rayTracingState().m_swShadowSoftOpaquePipeline);
            commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);

            // Preserve the UAV write boundary before the heap-selected resolve changes soft-A to ShaderResource.
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Denoise the half-res soft-A trace into the full-res visibility (geometry downsample -> per-slot temporal
            // merge + a-trous resolve -> the guarded soft transparent trace+fold -> temporal history swap). Backend-
            // agnostic: it reads ONLY the shared soft/temporal buffers + the G-buffer, so the SAME helper serves the HW
            // opaque-soft trace (which wrote soft-A above via the RayQuery pipeline). softTransparentRan mirrors the fold's
            // effect (the helper's fold sets it internally iff m_softTransparentReady): the transparent fallback below is
            // then skipped exactly as before so the colored shadow is not double-folded.
            dispatchSoftShadowDenoiseAndTransparentFold(commandList, targets, frameIndex, softGroupsX, softGroupsY);
            softTransparentRan = rayTracingState().m_softTransparentReady;
        }
    }

    // Transparent colored-shadow fallback (coarse/adaptive/uniform multiply). Skipped when the soft transparent fold ran;
    // the two paths are exclusive per slot so the colored shadow is never double-folded.
    if(!softTransparentRan && rayTracingState().m_swShadowAdaptiveEnabled){
        // Adaptive transparent shadow. Shared base: the coarse trace. The resolve is either an in-place conditional
        // re-trace or, when compacted mode is enabled, classify+append -> build-args -> DispatchIndirect trace. The
        // compacted path launches only edge rays as coherent waves instead of a full-res grid that diverges on edge lanes.
        // Edge-fraction instrumentation rides a slow cadence: snapshot the GPU counter every s_SwShadowEdgeStatsPeriod
        // ticks and read it back s_SwShadowEdgeStatsLogDelay ticks later (by then GPU-complete, so the map never stalls).
        const bool compact = rayTracingState().m_swShadowCompactEnabled;
        const u32 tick = rayTracingState().m_swShadowEdgeStatsTick++;
        const bool snapshot =
            rayTracingState().m_swShadowEdgeStatsEnabled
            && !rayTracingState().m_swShadowEdgeStatsPending
            && (tick % s_SwShadowEdgeStatsPeriod == 0u)
        ;

        if(snapshot){
            commandList.clearBufferUInt(rayTracingState().m_swShadowEdgeStatsBuffer.get(), 0u);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }

        // Transparent coarse: one transparent trace per coarse block written into the coarse buffer (colored
        // transmittance only). Shared base for both the compacted and the Stage-2 adaptive resolve.
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants coarsePush = makePush();
        coarsePush.width = targets.width;
        coarsePush.height = targets.height;
        coarsePush.coarseWidth = coarseWidth;
        coarsePush.coarseHeight = coarseHeight;
        commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentCoarsePipeline));
        bindPassHeap(rayTracingState().m_swShadowTransparentCoarsePipeline);
        commandList.setPushConstants(&coarsePush, sizeof(coarsePush));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);

        // Sync the coarse buffer before the resolve UAV-reads it (UAV write -> UAV read on the same image).
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        if(compact){
            // Compacted resolve. Reset the per-frame append counter; the list needs no clear because the indirect trace
            // reads only indices below the clamped count, all written this frame. Stage the compaction buffers writable.
            commandList.setEnableUavBarriersForBuffer(rayTracingState().m_swShadowEdgeCounterBuffer.get(), true);
            commandList.setEnableUavBarriersForBuffer(rayTracingState().m_swShadowEdgeListBuffer.get(), true);
            commandList.clearBufferUInt(rayTracingState().m_swShadowEdgeCounterBuffer.get(), 0u);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Classify (Stage-3): classify each pixel/light; interior -> interpolate + fold in place; edge -> append to
            // the list and leave the PRISTINE opaque mask for the indirect trace's single overwrite. collectStats tallies
            // the fraction on snapshots.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants classifyPush = makePush();
            classifyPush.width = targets.width;
            classifyPush.height = targets.height;
            classifyPush.coarseWidth = coarseWidth;
            classifyPush.coarseHeight = coarseHeight;
            classifyPush.edgeThreshold = rayTracingState().m_swShadowEdgeThreshold;
            classifyPush.collectStats = snapshot ? 1u : 0u;
            classifyPush.edgeCapacity = rayTracingState().m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentClassifyPipeline));
            bindPassHeap(rayTracingState().m_swShadowTransparentClassifyPipeline);
            commandList.setPushConstants(&classifyPush, sizeof(classifyPush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);

            // Sync the append counter + edge list (classify producer -> buildargs/indirect consumers) and the visibility
            // WAW (interior/overflow writes -> indirect edge overwrites). UAV barriers are enabled on all three.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Build args: 1 thread builds DispatchIndirectArguments{ceil(count/64),1,1} from the clamped append count.
            commandList.setBufferState(rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants argsPush = makePush();
            argsPush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            argsPush.edgeCapacity = rayTracingState().m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentBuildArgsPipeline));
            bindPassHeap(rayTracingState().m_swShadowTransparentBuildArgsPipeline);
            commandList.setPushConstants(&argsPush, sizeof(argsPush));
            commandList.dispatch(1u, 1u, 1u);

            // Sync the args write before the indirect consume, and keep the list/counter readable by the indirect trace.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Indirect trace: DispatchIndirect over the compacted edge records, one ray per thread. Its ComputeState
            // carries the indirect-args buffer; setComputeState auto-transitions it
            // UnorderedAccess->IndirectArgument.
            SwShadowHeapPushConstants tracePush = makePush();
            tracePush.width = targets.width;
            tracePush.height = targets.height;
            tracePush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
            commandList.commitBarriers();
            Core::ComputeState computeStateIndirect = passState(rayTracingState().m_swShadowTransparentIndirectPipeline);
            computeStateIndirect.setIndirectParams(rayTracingState().m_swShadowIndirectArgsBuffer.get());
            commandList.setComputeState(computeStateIndirect);
            bindPassHeap(rayTracingState().m_swShadowTransparentIndirectPipeline);
            commandList.setPushConstants(&tracePush, sizeof(tracePush));
            commandList.dispatchIndirect(0u);
        }
        else{
            // Stage-2 resolve: full-res adaptive (interpolate interior / re-trace edges in place, fold onto the opaque mask).
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants resolvePush = makePush();
            resolvePush.width = targets.width;
            resolvePush.height = targets.height;
            resolvePush.coarseWidth = coarseWidth;
            resolvePush.coarseHeight = coarseHeight;
            resolvePush.edgeThreshold = rayTracingState().m_swShadowEdgeThreshold;
            resolvePush.collectStats = snapshot ? 1u : 0u;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentResolvePipeline));
            bindPassHeap(rayTracingState().m_swShadowTransparentResolvePipeline);
            commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }

        if(snapshot){
            // Snapshot the counter into the CPU-readable buffer; the map happens s_SwShadowEdgeStatsLogDelay ticks later.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::CopySource);
            commandList.commitBarriers();
            commandList.copyBuffer(
                rayTracingState().m_swShadowEdgeStatsReadback.get(), 0u,
                rayTracingState().m_swShadowEdgeStatsBuffer.get(), 0u,
                static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT)
            );
            rayTracingState().m_swShadowEdgeStatsPending = true;
            rayTracingState().m_swShadowEdgeStatsPendingTick = tick;
        }
        else if(
            rayTracingState().m_swShadowEdgeStatsPending
            && (tick - rayTracingState().m_swShadowEdgeStatsPendingTick) >= s_SwShadowEdgeStatsLogDelay
        ){
            const u32* stats = static_cast<const u32*>(graphics().getDevice()->mapBuffer(rayTracingState().m_swShadowEdgeStatsReadback.get(), Core::CpuAccessMode::Read));
            if(stats){
                const u32 traced = stats[NWB_SW_SHADOW_EDGE_STATS_TRACED];
                const u32 total = stats[NWB_SW_SHADOW_EDGE_STATS_TOTAL];
                graphics().getDevice()->unmapBuffer(rayTracingState().m_swShadowEdgeStatsReadback.get());
                const f64 fraction = (total > 0u) ? (100.0 * static_cast<f64>(traced) / static_cast<f64>(total)) : 0.0;
                NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: SW shadow adaptive edge fraction = {}% ({} traced / {} total rays, threshold {})")
                    , fraction
                    , static_cast<u64>(traced)
                    , static_cast<u64>(total)
                    , static_cast<f64>(rayTracingState().m_swShadowEdgeThreshold)
                );
            }
            rayTracingState().m_swShadowEdgeStatsPending = false;
        }
    }
    else if(!softTransparentRan){
        // Non-adaptive baseline: uniform transparent multiply at HALF resolution, one trace per 2x2 block folded onto
        // each full-res pixel's own opaque mask. Kept for comparison against the adaptive path.
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants pushConstants = makePush();
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentUniformPipeline));
        bindPassHeap(rayTracingState().m_swShadowTransparentUniformPipeline);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);
    }

    if(!rayTracingState().m_swShadowDispatchLogged){
        rayTracingState().m_swShadowDispatchLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched software shadow traversal ({}x{}, {} instances)")
            , static_cast<u64>(targets.width)
            , static_cast<u64>(targets.height)
            , static_cast<u64>(rayTracingState().m_sceneBvhInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::hybridTransparentShadowReady()const noexcept{
    return rayTracingState().m_hybridTransparentShadowReady;
}

bool RendererRayTracingSystem::softTransparentShadowReady()const noexcept{
    return rayTracingState().m_softTransparentReady;
}

void RendererRayTracingSystem::appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const{
    // Full/half RayQuery trace layouts are push-only. The deferred selector, G-buffers, output StorageImage, and TLAS
    // are all global descriptor-heap selections.
    static_assert(sizeof(ShadowRqSoftPushConstants) >= sizeof(ShadowRqPushConstants), "shadow-trace push-constant range must cover both the hard and soft trace push structs");
    layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowRqSoftPushConstants)));
}

bool RendererRayTracingSystem::ensureShadowPipeline(){
    if(rayTracingState().m_shadowPipeline)
        return true;
    if(rayTracingState().m_shadowPipelineFailed)
        return false;
    // Hardware shadow trace is inline RayQuery in a COMPUTE shader (not the RT pipeline), so it needs RayQuery +
    // the acceleration structure feature (the TLAS it queries).
    if(!graphics().queryFeatureSupport(Core::Feature::RayQuery) || !graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Set 0 contains only the push range; the TLAS and every ordinary resource are heap-addressed.
        appendShadowTraceBindingLayout(layoutDesc);

        rayTracingState().m_shadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
            rayTracingState().m_shadowPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowShader,
        AssetsGraphicsShadow::s_RayQueryShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_ShadowRayQuery"
    )){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowShader)
        .addBindingLayout(rayTracingState().m_shadowBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    rayTracingState().m_shadowPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureShadowSoftPipeline(){
    if(rayTracingState().m_shadowSoftPipeline)
        return true;
    if(rayTracingState().m_shadowSoftPipelineFailed)
        return false;
    // Same feature gate as the hard trace: inline RayQuery in a COMPUTE shader against the TLAS.
    if(!graphics().queryFeatureSupport(Core::Feature::RayQuery) || !graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    // The soft trace REUSES the shared shadow binding layout (identical trace context; only the bound visibility-output
    // texture differs). ensureShadowPipeline creates it; the HW prepare branch runs that before this, so it is resident.
    if(!rayTracingState().m_shadowBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow binding layout missing for the soft RayQuery pipeline"));
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowSoftShader,
        AssetsGraphicsShadow::s_RayQuerySoftShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_ShadowRayQuerySoft"
    )){
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowSoftShader)
        .addBindingLayout(rayTracingState().m_shadowBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    rayTracingState().m_shadowSoftPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowSoftPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery soft shadow compute pipeline"));
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery soft shadow compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPipeline(){
    // Idempotent: the shared layout and persistent compaction buffers are created once (guarded by
    // m_swShadowBindingLayout), and each per-pass pipeline creation below is guarded by its own handle. A hard failure
    // is sticky.
    if(rayTracingState().m_swShadowPipelineFailed)
        return false;

    auto& device = *graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software shadows require the initialized global descriptor heap"));
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_swShadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Every selector, writable target, and work buffer is heap-addressed by one fixed push ABI.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SwShadowHeapPushConstants)));

        rayTracingState().m_swShadowBindingLayout = device.createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swShadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        // The transparent adaptive config is fixed at its shipping defaults (adaptive ON, compact ON, edge threshold 0.1,
        // stats OFF -- see renderer_state.h). Create the persistent edge-fraction counter + its CPU-readable
        // snapshot: both are tiny and always bound (the shader always declares slots 15/16), so they exist alongside the
        // layout regardless of the config -- the config only selects the dispatched mode + whether stats are tallied.

        Core::BufferDesc edgeStatsDesc;
        edgeStatsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("sw_shadow_edge_stats"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowEdgeStatsBuffer = graphics().createBuffer(edgeStatsDesc);
        if(!rayTracingState().m_swShadowEdgeStatsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-stats buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc edgeStatsReadbackDesc;
        edgeStatsReadbackDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT))
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setDebugName(Name("sw_shadow_edge_stats_readback"))
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        rayTracingState().m_swShadowEdgeStatsReadback = graphics().createBuffer(edgeStatsReadbackDesc);
        if(!rayTracingState().m_swShadowEdgeStatsReadback){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-stats readback buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        // Stage-3 compaction: the persistent per-frame append counter (2 u32) + the indirect dispatch-args buffer (3 u32,
        // created BOTH UAV-writable -- build-args writes it -- AND isDrawIndirectArgs so dispatchIndirect's validateIndirectBuffer
        // accepts it). The variable-size edge list is allocated per-resolution in createShadowVisibilityTarget.
        Core::BufferDesc edgeCounterDesc;
        edgeCounterDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_COUNTER_SIZE))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("sw_shadow_edge_counter"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowEdgeCounterBuffer = graphics().createBuffer(edgeCounterDesc);
        if(!rayTracingState().m_swShadowEdgeCounterBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-counter buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc indirectArgsDesc;
        indirectArgsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setDebugName(Name("sw_shadow_indirect_args"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowIndirectArgsBuffer = graphics().createBuffer(indirectArgsDesc);
        if(!rayTracingState().m_swShadowIndirectArgsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow indirect-args buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }
    }

    const bool heapResourcesReady =
        __hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_swShadowEdgeStatsHeapHandle)
        && __hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_swShadowEdgeCounterHeapHandle)
        && __hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_swShadowIndirectArgsHeapHandle)
    ;
    if(!heapResourcesReady){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-shadow work buffers in the descriptor heap"));
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }

    // One named pipeline per pass, all against the shared push-only layout. Each kernel consumes the same heap-slot map.
    const bool passesReady =
        ensureSwShadowPassPipeline(rayTracingState().m_swShadowOpaquePrepassShader, rayTracingState().m_swShadowOpaquePrepassPipeline, AssetsGraphicsShadow::s_SwOpaquePrepassShaderName, "ECSRender_SwShadowOpaquePrepass")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowSoftOpaqueShader, rayTracingState().m_swShadowSoftOpaquePipeline, AssetsGraphicsShadow::s_SwSoftOpaqueShaderName, "ECSRender_SwShadowSoftOpaque")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentCoarseShader, rayTracingState().m_swShadowTransparentCoarsePipeline, AssetsGraphicsShadow::s_SwTransparentCoarseShaderName, "ECSRender_SwShadowTransparentCoarse")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentResolveShader, rayTracingState().m_swShadowTransparentResolvePipeline, AssetsGraphicsShadow::s_SwTransparentResolveShaderName, "ECSRender_SwShadowTransparentResolve")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentClassifyShader, rayTracingState().m_swShadowTransparentClassifyPipeline, AssetsGraphicsShadow::s_SwTransparentClassifyShaderName, "ECSRender_SwShadowTransparentClassify")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentBuildArgsShader, rayTracingState().m_swShadowTransparentBuildArgsPipeline, AssetsGraphicsShadow::s_SwTransparentBuildArgsShaderName, "ECSRender_SwShadowTransparentBuildArgs")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentIndirectShader, rayTracingState().m_swShadowTransparentIndirectPipeline, AssetsGraphicsShadow::s_SwTransparentIndirectShaderName, "ECSRender_SwShadowTransparentIndirect")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentUniformShader, rayTracingState().m_swShadowTransparentUniformPipeline, AssetsGraphicsShadow::s_SwTransparentUniformShaderName, "ECSRender_SwShadowTransparentUniform")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentSoftShader, rayTracingState().m_swShadowTransparentSoftPipeline, AssetsGraphicsShadow::s_SwTransparentSoftShaderName, "ECSRender_SwShadowTransparentSoft")
    ;
    if(!passesReady){
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const char* debugLabel){
    // Idempotent per-pass loader + compute-pipeline creator against the SHARED software-shadow binding layout (created by
    // ensureSwShadowPipeline before any pass is built). Returns true if the pipeline is already/newly resident; a failure
    // here bubbles up to fail the whole SW shadow ensure for the frame.
    if(pipeline)
        return true;

    if(!m_renderer.shaderSystem().loadShader(
        shader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        debugLabel
    ))
        return false;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(rayTracingState().m_swShadowBindingLayout)
    ;
    // Every SW shadow pass requires the global resource/sampler heap layouts.
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    pipeline = graphics().getDevice()->createComputePipeline(pipelineDesc);
    if(!pipeline){
        // debugLabel identifies the failing pass in the shader-load path already; keep the message argument-free (the
        // NWB_TEXT log string is wide, and debugLabel is a narrow const char* the wide formatter cannot consume).
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // The per-instance occluder material table is CPU-written each frame and read by the shadow shaders, so it
    // is a structured SRV (no UAV) that grows by doubling like the TLAS / scene-instance buffers. Shared by the
    // hardware and software backends (only one runs per frame), built lockstep with that backend's instances.
    if(rayTracingState().m_shadowInstanceMaterialBuffer && rayTracingState().m_shadowInstanceMaterialCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowInstanceMaterialBuffer.get(),
            rayTracingState().m_shadowInstanceMaterialHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(
        rayTracingState().m_shadowInstanceMaterialCapacity,
        instanceCount,
        s_ShadowInstanceMaterialInitialCapacity
    );

    Core::BufferDesc materialBufferDesc;
    materialBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbRtInstanceMaterialGpu) * capacity))
        .setStructStride(sizeof(NwbRtInstanceMaterialGpu))
        .setDebugName(Name("shadow_instance_material"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialBuffer = graphics().createBuffer(materialBufferDesc);
    if(!materialBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*materialBuffer.get(), rayTracingState().m_shadowInstanceMaterialHeapHandle))
        return false;
    rayTracingState().m_shadowInstanceMaterialBuffer = Move(materialBuffer);
    rayTracingState().m_shadowInstanceMaterialCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow-owned combined instance buffer (g_NwbMeshInstances for the trace): InstanceGpuData per occluder,
    // structured SRV, grows by doubling like the draw pass's instance buffer. Built each frame over ALL gathered
    // occluders so the trace's surface hook can resolve the mutable storage offset that lives in translation.w.
    if(instanceCount == 0u)
        return !rayTracingState().m_shadowInstanceBuffer || ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowInstanceBuffer.get(),
            rayTracingState().m_shadowInstanceHeapHandle
        );
    if(rayTracingState().m_shadowInstanceBuffer && rayTracingState().m_shadowInstanceCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowInstanceBuffer.get(),
            rayTracingState().m_shadowInstanceHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(rayTracingState().m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(Name("shadow_instance_context"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = graphics().createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), rayTracingState().m_shadowInstanceHeapHandle))
        return false;
    rayTracingState().m_shadowInstanceBuffer = Move(instanceBuffer);
    rayTracingState().m_shadowInstanceCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Shadow-owned combined material-typed buffer (g_NwbMaterialTypedWords for the trace): each occluder's
    // constant + mutable typed blocks, word-strided structured SRV, grows by doubling like the draw pass's typed
    // buffer. Always at least one word so the binding is valid even with no transparent occluders.
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
    if(rayTracingState().m_shadowMaterialTypedBuffer && rayTracingState().m_shadowMaterialTypedCapacity >= requiredByteCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *rayTracingState().m_shadowMaterialTypedBuffer.get(),
            rayTracingState().m_shadowMaterialTypedHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(rayTracingState().m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setDebugName(Name("shadow_material_typed"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialTypedBuffer = graphics().createBuffer(materialTypedBufferDesc);
    if(!materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*materialTypedBuffer.get(), rayTracingState().m_shadowMaterialTypedHeapHandle))
        return false;
    rayTracingState().m_shadowMaterialTypedBuffer = Move(materialTypedBuffer);
    rayTracingState().m_shadowMaterialTypedCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::uploadShadowMaterialContextBuffers(
    Core::CommandList& commandList,
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    // The combined typed buffer always has content (at minimum the padded word reserved below) so the trace's
    // material-context binding is always valid; the instance buffer may be empty only when no occluder resolved a
    // material, in which case the trace never indexes it (no transparent hit dispatches).
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;

    if(!ensureShadowInstanceContextBuffer(instanceData.size()) || !ensureShadowMaterialTypedBuffer(uploadBytes))
        return false;

    if(!instanceData.empty()){
        Core::Buffer* instanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
        commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.writeBuffer(instanceBuffer, instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
        commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(materialTypedBuffer, materialTypedBytes.data(), uploadBytes);
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

