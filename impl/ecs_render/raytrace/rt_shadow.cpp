// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    // SW shadow binding set, which re-binds whichever coarse handle is current). Round UP so a coarse texel covers its 2x2
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

    // Compacted edge list (recreated on resize alongside the visibility/coarse targets, so the SW shadow binding-set
    // rebuild that already triggers on the visibility-pointer change re-binds it). Each record is NWB_SW_SHADOW_EDGE_RECORD_WORDS
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
    rayTracingState().m_swShadowEdgeListBuffer = graphics().createBuffer(edgeListDesc);
    if(!rayTracingState().m_swShadowEdgeListBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        rayTracingState().m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    rayTracingState().m_swShadowEdgeListCapacity = edgeListCapacityRecords;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return false;
    if(!rayTracingState().m_tlas || !rayTracingState().m_shadowPipeline || !rayTracingState().m_shadowBindingSet)
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // Transition shared per-mesh geometry and material context to ShaderResource after buildSceneTlas used positions
    // and indices for acceleration-structure input. Caustic and GI read these buffers through descriptor-heap slots;
    // the binding set derives the remaining states.
    for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
        commandList.setBufferState(rayTracingState().m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
    }
    commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);

    // When the soft resources are ready this frame and at least one light holds a shadow slot, route the HW opaque shadow
    // through the same half-res soft denoise chain the SW path uses. The HW opaque-soft RayQuery trace casts SPP
    // cone-jittered opaque rays per half-res pixel into shadowSoftHalfA, then the shared
    // dispatchSoftShadowDenoiseAndTransparentFold denoises it into the full-res visibility.
    if(rayTracingState().m_softShadowReady && rayTracingState().m_shadowSoftPipeline && rayTracingState().m_shadowSoftBindingSet && rayTracingState().m_softShadowSlotMask != 0u){
        const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));

        // Derive the soft trace's resource states (TLAS read, G-buffer SRVs, scene/light, and shadowSoftHalfA as the UAV
        // output) from its binding set; the per-mesh + material-context buffers were staged to ShaderResource above.
        commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowSoftBindingSet.get());
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
        softState.addBindingSet(rayTracingState().m_shadowSoftBindingSet.get());
        commandList.setComputeState(softState);

        ShadowRqSoftPushConstants softPush;
        softPush.width = targets.width;
        softPush.height = targets.height;
        softPush.frameIndex = frameIndex;
        commandList.setPushConstants(&softPush, sizeof(softPush));
        commandList.dispatch(softGroupsX, softGroupsY, 1u);

        // Sync soft-A (soft trace write -> the resolve PREPARE reads it as an SRV). The resolve's
        // setResourceStatesForBindingSet transitions soft-A UnorderedAccess -> ShaderResource.
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
    commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState shadowState;
    shadowState.setPipeline(rayTracingState().m_shadowPipeline.get());
    shadowState.addBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.setComputeState(shadowState);
    // Soft shadow cone-jitter: advance the per-frame seed once here. A soft light samples inside its source cone; a
    // zero-radius light jitters to the axis exactly and keeps the hard-shadow result.
    ShadowRqPushConstants shadowPush;
    shadowPush.frameIndex = rayTracingState().m_softShadowFrameIndex++;
    commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
        1u
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    // No software scene BVH this frame (no traceable instances) -> the caller clears the mask to all-lit.
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhInstanceCount == 0u)
        return false;
    // Every decomposed pass pipeline must be resident (ensureSwShadowPipeline creates them all-or-nothing) alongside the
    // shared binding set + the per-mesh geometry; the opaque prepass is a representative liveness check for the set.
    if(!rayTracingState().m_swShadowOpaquePrepassPipeline || !rayTracingState().m_swShadowBindingSet || rayTracingState().m_swShadowMeshCount == 0u)
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The per-mesh BVH node buffers were left in UnorderedAccess by the build pass; move each distinct mesh's
    // node + geometry buffers to ShaderResource for the traversal reads. The scene node buffer was already
    // uploaded as a shader resource by buildSceneSwBvh; setResourceStatesForBindingSet derives the rest
    // (G-buffer SRVs, visibility UAV).
    for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
        commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
    }
    // The per-hit transmittance dispatch reads the shadow-owned material-context buffers (built + uploaded by
    // buildSceneSwBvh on the shadow-prepare command list); move them explicitly alongside the per-mesh geometry
    // before traversal.
    commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
    // The two software sub-passes that write the visibility UAV (the opaque pre-pass + the transparent resolve/multiply)
    // need a UAV barrier between them, and the Stage-2 coarse->resolve handoff needs one on the coarse texture. Enable
    // UAV barriers on both so each commitBarriers between dispatches syncs the read-after-write hazard on the same image.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true);
    commandList.setResourceStatesForBindingSet(rayTracingState().m_swShadowBindingSet.get());
    commandList.commitBarriers();

    // Each pass has its own pipeline but shares the one binding set, so build the ComputeState right before dispatch.
    const auto passState = [&](const Core::ComputePipelineHandle& pipeline){
        Core::ComputeState state;
        state.setPipeline(pipeline.get());
        state.addBindingSet(rayTracingState().m_swShadowBindingSet.get());
        return state;
    };

    // SW traversal accesses per-mesh geometry through the descriptor heap, so bind its tables after each ComputeState
    // and before dispatch. bindCompute touches only sets 8/9; non-bindless builds skip it.
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    const bool heapLive = heap.isInitialized();
    const auto bindPassHeap = [&](const Core::ComputePipelineHandle& pipeline){
        if(heapLive)
            heap.bindCompute(commandList, *pipeline.get());
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
            SwShadowOpaquePrepassPushConstants opaquePush;
            opaquePush.width = targets.width;
            opaquePush.height = targets.height;
            opaquePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
            commandList.setComputeState(passState(rayTracingState().m_swShadowOpaquePrepassPipeline));
            bindPassHeap(rayTracingState().m_swShadowOpaquePrepassPipeline);
            commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }

        // Sync before the transparent pass. The opaque mask (the prepass, or the soft resolve below, wrote shadowVisibility)
        // -> the transparent pass reads+multiplies it: a write->read/write hazard the visibility UAV barrier covers, so it
        // stays UnorderedAccess. The transparent coarse pass writes the coarse buffer next, so stage it as ShaderResource
        // here; setComputeState emits the ShaderResource->UnorderedAccess barrier that orders it. Both are cheap state
        // transitions kept unconditional so the soft-not-transparent-ready fallback stays ordered.
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
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
            // Stage-3 temporal accumulator buffers: enable UAV barriers so the merge's history/moments writes are ordered
            // before the a-trous PREPARE reads the accumulated history as an SRV (setResourceStatesForBindingSet handles the
            // UAV->SRV transition). No-op when temporal is off (the merge never dispatches).
            if(rayTracingState().m_softShadowTemporalReady){
                commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
            }

            SwShadowSoftOpaquePushConstants softTracePush;
            softTracePush.width = targets.width;
            softTracePush.height = targets.height;
            softTracePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
            softTracePush.frameIndex = frameIndex;
            commandList.setComputeState(passState(rayTracingState().m_swShadowSoftOpaquePipeline));
            bindPassHeap(rayTracingState().m_swShadowSoftOpaquePipeline);
            commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);

            // Sync soft-A (soft trace write -> the resolve PREPARE reads it as an SRV). The resolve's
            // setResourceStatesForBindingSet transitions soft-A UnorderedAccess -> ShaderResource here.
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
        SwShadowTransparentCoarsePushConstants coarsePush;
        coarsePush.width = targets.width;
        coarsePush.height = targets.height;
        coarsePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
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
            SwShadowTransparentClassifyPushConstants classifyPush;
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
            SwShadowTransparentBuildArgsPushConstants argsPush;
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
            SwShadowTransparentIndirectPushConstants tracePush;
            tracePush.width = targets.width;
            tracePush.height = targets.height;
            tracePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
            tracePush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            Core::ComputeState computeStateIndirect = passState(rayTracingState().m_swShadowTransparentIndirectPipeline);
            computeStateIndirect.setIndirectParams(rayTracingState().m_swShadowIndirectArgsBuffer.get());
            commandList.setComputeState(computeStateIndirect);
            bindPassHeap(rayTracingState().m_swShadowTransparentIndirectPipeline);
            commandList.setPushConstants(&tracePush, sizeof(tracePush));
            commandList.dispatchIndirect(0u);
        }
        else{
            // Stage-2 resolve: full-res adaptive (interpolate interior / re-trace edges in place, fold onto the opaque mask).
            SwShadowTransparentResolvePushConstants resolvePush;
            resolvePush.width = targets.width;
            resolvePush.height = targets.height;
            resolvePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
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
        SwShadowTransparentUniformPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::hybridTransparentShadowReady()const noexcept{
    return rayTracingState().m_hybridTransparentShadowReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::softTransparentShadowReady()const noexcept{
    return rayTracingState().m_softTransparentReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const{
    // The hardware inline-RayQuery occlusion trace's bindings, in NWB_SHADOW_RT_* slot order (occlusion.slangi /
    // shadow_rayquery.slangi declare them). Shared by the full-resolution hard fallback and the half-resolution soft
    // trace, so both use the identical resource slot map.
    layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, 1)); // inline RayQuery reads the TLAS from compute
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT, 1)); // soft trace: half-res; hard fallback: full-res
    // (slot 7, the per-instance occluder material table, is intentionally absent: the opaque fast path loads no
    // per-instance material. The shared buffer stays for the SW/GI/caustics paths -- see binding_slots.h.)
    // Only shared material-context buffers remain in this slot map; the opaque trace reads no per-mesh geometry.
    layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MATERIAL_TYPED, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INSTANCES, 1));
    // Soft shadow cone-jitter: the frame counter (NwbShadowRqPushConstants) seeds the per-pixel low-discrepancy jitter
    // sample for soft-light opaque traces. Sized to the LARGER of the two pipelines that share this layout -- the full-res
    // hard trace sets only ShadowRqPushConstants (frameIndex), while the half-res SOFT trace (shadow_rayquery_soft_cs) sets
    // the wider ShadowRqSoftPushConstants (width/height/frameIndex); a range sized to the max keeps both layout-compatible
    // (each pipeline sets only its own smaller-or-equal struct), mirroring the SW SwShadowMaxPushConstants pattern.
    static_assert(sizeof(ShadowRqSoftPushConstants) >= sizeof(ShadowRqPushConstants), "shadow-trace push-constant range must cover both the hard and soft trace push structs");
    layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowRqSoftPushConstants)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::appendShadowTraceBindingSet(Core::BindingSetDesc& desc, DeferredFrameTargets& targets, Core::Texture* visibilityTarget)const{
    // The visibility UAV target differs per pass (half-res for the trace, full-res for the hard fallback); everything
    // else uses the identical trace layout. Its shadow-owned material context is built by buildSceneTlas over ALL
    // gathered occluders, unlike the draw-pass buffers, which hold only the opaque set at trace time.
    desc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, rayTracingState().m_tlas.get()));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT,
        visibilityTarget,
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    // Slot 7 is unused because the opaque fast path loads no per-instance material.
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MATERIAL_TYPED, rayTracingState().m_shadowMaterialTypedBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INSTANCES, rayTracingState().m_shadowInstanceBuffer.get()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    if(!rayTracingState().m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Backend C: the inline-RayQuery trace layout is segment-coherent pure-resource (TLAS + G-buffer SRVs + scene
        // CB + light SRV + visibility UAV + shared material-context SRVs) with no samplers, and its pipeline carries
        // no heap layout (the HW RayQuery dispatches never call heap.bindCompute -- only the no-RayQuery SW path does).
        // RayTracingAccelStruct is descriptor-buffer-compatible via vkGetDescriptorEXT, so this is the first TLAS
        // layout to route to the descriptor-buffer path; push constants still ride the pipeline layout alongside it.
        layoutDesc.setUseDescriptorBuffer(true);
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
        Core::ShaderArchive::s_DefaultVariant,
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
    rayTracingState().m_shadowPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMeshCount > 0u);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    // The shared trace layout carries the shadow-owned material context (g_NwbMaterialTypedWords +
    // g_NwbMeshInstances) built by buildSceneTlas over ALL gathered occluders, rather than the draw-pass buffers,
    // which hold only the opaque set at trace time. buildSceneTlas uploads both before this binding set is created.
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);

    // Rebuild when any binding input that can change without a full invalidate changes: the TLAS (recreated when
    // the live instance count outgrows its capacity), the instance-material table, the distinct-mesh count (the
    // per-mesh descriptor arrays repopulate when the set of traced meshes changes), and the shadow-owned
    // material-context buffers (recreated when they outgrow their capacity). A resize resets the set via
    // resetDeferredFrameTargets, so the target inputs need no separate key.
    const Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    const Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    const u32 meshCount = rayTracingState().m_shadowMeshCount;
    if(
        rayTracingState().m_shadowBindingSet
        && rayTracingState().m_shadowBindingSetTlas == tlas
        && rayTracingState().m_shadowBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_shadowBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_shadowBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_shadowBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    // The full-res trace writes the FULL-res visibility (one ray per output pixel) at NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT
    // -- the same buffer the deferred lighting samples, mirroring the software traversal (no resolve pass in between).
    appendShadowTraceBindingSet(bindingSetDesc, targets, targets.shadowVisibility.get());

    auto* device = graphics().getDevice();
    rayTracingState().m_shadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_shadowBindingLayout);
    if(!rayTracingState().m_shadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding set"));
        rayTracingState().m_shadowBindingSetTlas = nullptr;
        rayTracingState().m_shadowBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_shadowBindingSetMaterialTyped = nullptr;
        rayTracingState().m_shadowBindingSetMeshInstances = nullptr;
        rayTracingState().m_shadowBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_shadowBindingSetTlas = tlas;
    rayTracingState().m_shadowBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_shadowBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_shadowBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_shadowBindingSetMeshCount = meshCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        Core::ShaderArchive::s_DefaultVariant,
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
    rayTracingState().m_shadowSoftPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowSoftPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery soft shadow compute pipeline"));
        rayTracingState().m_shadowSoftPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery soft shadow compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowSoftBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMeshCount > 0u);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);

    // Rebuild on the SAME tracked-pointer keys as ensureShadowBindingSet (its own copies, so the soft set is independent
    // of the hard set's rebuild): the TLAS, the instance-material table, the distinct-mesh count, and the shadow-owned
    // material-context buffers. A resize resets the set via resetDeferredFrameTargets, so the target inputs need no key.
    const Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    const Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    const u32 meshCount = rayTracingState().m_shadowMeshCount;
    if(
        rayTracingState().m_shadowSoftBindingSet
        && rayTracingState().m_shadowSoftBindingSetTlas == tlas
        && rayTracingState().m_shadowSoftBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_shadowSoftBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_shadowSoftBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_shadowSoftBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    // The soft trace writes the HALF-res soft-A buffer (shadowSoftHalfA) at NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT --
    // the SAME buffer the SW soft trace writes, so the shared geometry-downsample -> temporal -> a-trous -> upsample
    // denoise chain reads it identically. Everything else is the identical trace context as the full-res set.
    appendShadowTraceBindingSet(bindingSetDesc, targets, targets.shadowSoftHalfA.get());

    auto* device = graphics().getDevice();
    rayTracingState().m_shadowSoftBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_shadowBindingLayout);
    if(!rayTracingState().m_shadowSoftBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow binding set"));
        rayTracingState().m_shadowSoftBindingSetTlas = nullptr;
        rayTracingState().m_shadowSoftBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_shadowSoftBindingSetMaterialTyped = nullptr;
        rayTracingState().m_shadowSoftBindingSetMeshInstances = nullptr;
        rayTracingState().m_shadowSoftBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_shadowSoftBindingSetTlas = tlas;
    rayTracingState().m_shadowSoftBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_shadowSoftBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_shadowSoftBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_shadowSoftBindingSetMeshCount = meshCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSwShadowPipeline(){
    // Idempotent: the shared layout + persistent Stage-2/3 buffers are created once (guarded by m_swShadowBindingLayout),
    // and each per-pass pipeline creation below is itself idempotent (guarded by its own handle). A prior hard failure is
    // sticky.
    if(rayTracingState().m_swShadowPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_swShadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SW_SHADOW_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, 1));
        // Per-mesh geometry is fetched from the global descriptor heap through slots carried by the material record.
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_INSTANCE_MATERIAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MATERIAL_TYPED, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INSTANCES, 1));
        // Stage-2 adaptive transparent shadow: the half-res coarse transmittance scratch (written by the coarse trace,
        // UAV-read by the resolve) + the edge-fraction stats counter. Always present in the layout (the shader always
        // declares them); renderer state chooses which pass is dispatched and whether the counter is tallied.
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_COARSE, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_STATS, 1));
        // Compaction UAVs: the edge append counter, the compacted edge-record list, and the indirect dispatch-args buffer.
        // Always present in the layout; renderer state selects compacted-indirect or coarse/adaptive resolve.
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_INDIRECT_ARGS, 1));
        // Soft opaque shadow: the half-res soft visibility UAV the jittered trace writes (read by the separate
        // shadow_resolve pipeline). Always in the layout.
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_SOFT_HALF, 1));
        // Soft COLORED TRANSPARENT shadow: the half-res colored soft transmittance UAV the soft transparent trace
        // writes (read by the SEPARATE RGB shadow_resolve pipeline). Always in the layout -- only the soft transparent trace
        // kernel declares/writes it; the other passes leave it inert. Recreated with the visibility target on resize.
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF, 1));
        // Push-constant range sized to the LARGEST pass struct: every per-pass pipeline shares this layout and each
        // dispatch sets only its own (smaller) struct's bytes -- see SwShadowMaxPushConstants.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SwShadowMaxPushConstants)));

        rayTracingState().m_swShadowBindingLayout = device->createBindingLayout(layoutDesc);
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

    // One named pipeline per pass, all against the shared layout. Each pass's kernel references only its own subset of
    // the slot map, so the shared binding set drives them identically. Any single failure fails the whole ensure, leaving
    // the frame's SW shadow backend not ready.
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    // Pin the global descriptor-index heap's resource (set 8) + sampler (set 9) layouts onto every SW shadow pass
    // pipeline -- the shader-layout-only side of the split: the traversal reads per-mesh geometry through these sets by
    // the host-provided slot index. The classic SW shadow layout is added first, so it keeps positional set 0; the two
    // heap layouts carry explicit sets 8/9 and createPipelineLayoutForBindingLayouts gap-fills sets 1-7 with the empty
    // set layout. Guarded on a live heap so builds without one keep the pure set-0 layout.
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
    pipeline = graphics().getDevice()->createComputePipeline(pipelineDesc);
    if(!pipeline){
        // debugLabel identifies the failing pass in the shader-load path already; keep the message argument-free (the
        // NWB_TEXT log string is wide, and debugLabel is a narrow const char* the wide formatter cannot consume).
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSwShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_swShadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_sceneBvhNodeBuffer);
    NWB_ASSERT(rayTracingState().m_sceneInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowMeshCount > 0u);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(targets.shadowCoarseTransmittance);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.transparentSoftHalf);
    NWB_ASSERT(rayTracingState().m_swShadowEdgeStatsBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowEdgeCounterBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowEdgeListBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowIndirectArgsBuffer);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    // The material-constants context the per-hit transmittance dispatch reads (g_NwbMaterialTypedWords +
    // g_NwbMeshInstances) is the SHADOW-OWNED combined pair built by buildSceneSwBvh over ALL gathered occluders,
    // NOT the draw pass's buffers (those hold only one transparency class at trace time). buildSceneSwBvh uploads
    // both before this binding set is created.
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);

    // Rebuild when any binding input that can change without a full invalidate changes: the scene node /
    // instance buffers (recreated when they outgrow their capacity), the visibility target (recreated on
    // resize, which also resets this set via resetDeferredFrameTargets), the distinct-mesh count (the per-mesh
    // descriptor arrays repopulate when the set of traced meshes changes), and the shadow-owned material-context
    // buffers (recreated when they outgrow their capacity).
    Core::Buffer* sceneNodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    const Core::Texture* visibilityTarget = targets.shadowVisibility.get();
    const u32 meshCount = rayTracingState().m_swShadowMeshCount;
    if(
        rayTracingState().m_swShadowBindingSet
        && rayTracingState().m_swShadowBindingSetSceneNodes == sceneNodeBuffer
        && rayTracingState().m_swShadowBindingSetInstances == instanceBuffer
        && rayTracingState().m_swShadowBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_swShadowBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_swShadowBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_swShadowBindingSetVisibility == visibilityTarget
        && rayTracingState().m_swShadowBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SW_SHADOW_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_NODES, sceneNodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT,
        targets.shadowVisibility.get(),
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, instanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_INSTANCE_MATERIAL, instanceMaterialBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MATERIAL_TYPED, materialTypedBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INSTANCES, meshInstanceBuffer));
    // Stage-2 adaptive transparent shadow: the half-res coarse transmittance scratch (UAV) + the edge-fraction counter
    // (UAV). The coarse texture is recreated with the visibility target on resize, so tracking the visibility pointer in
    // the rebuild guard also covers it; the stats buffer is persistent.
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_COARSE,
        targets.shadowCoarseTransmittance.get(),
        targets.shadowCoarseTransmittanceFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_STATS, rayTracingState().m_swShadowEdgeStatsBuffer.get()));
    // Stage-3 compaction UAVs: append counter, compacted edge list, indirect dispatch-args.
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, rayTracingState().m_swShadowEdgeCounterBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_LIST, rayTracingState().m_swShadowEdgeListBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_INDIRECT_ARGS, rayTracingState().m_swShadowIndirectArgsBuffer.get()));
    // Soft opaque shadow: the half-res soft visibility UAV (recreated with the visibility target on resize, so tracking
    // the visibility pointer in the rebuild guard also covers it).
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_SOFT_HALF,
        targets.shadowSoftHalfA.get(),
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    // Soft COLORED TRANSPARENT shadow: the half-res colored soft transmittance UAV (recreated with the visibility
    // target on resize, so the tracked visibility-pointer rebuild guard also covers it).
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF,
        targets.transparentSoftHalf.get(),
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));

    // SW shadow reads per-mesh geometry through material-record descriptor-heap slots; backing buffers remain
    // transitioned for those reads.

    auto* device = graphics().getDevice();
    rayTracingState().m_swShadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_swShadowBindingLayout);
    if(!rayTracingState().m_swShadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding set"));
        rayTracingState().m_swShadowBindingSetSceneNodes = nullptr;
        rayTracingState().m_swShadowBindingSetInstances = nullptr;
        rayTracingState().m_swShadowBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_swShadowBindingSetMaterialTyped = nullptr;
        rayTracingState().m_swShadowBindingSetMeshInstances = nullptr;
        rayTracingState().m_swShadowBindingSetVisibility = nullptr;
        rayTracingState().m_swShadowBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_swShadowBindingSetSceneNodes = sceneNodeBuffer;
    rayTracingState().m_swShadowBindingSetInstances = instanceBuffer;
    rayTracingState().m_swShadowBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_swShadowBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_swShadowBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_swShadowBindingSetVisibility = visibilityTarget;
    rayTracingState().m_swShadowBindingSetMeshCount = meshCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // The per-instance occluder material table is CPU-written each frame and read by the shadow shaders, so it
    // is a structured SRV (no UAV) that grows by doubling like the TLAS / scene-instance buffers. Shared by the
    // hardware and software backends (only one runs per frame), built lockstep with that backend's instances.
    if(rayTracingState().m_shadowInstanceMaterialBuffer && rayTracingState().m_shadowInstanceMaterialCapacity >= instanceCount)
        return true;

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
    rayTracingState().m_shadowInstanceMaterialBuffer = graphics().createBuffer(materialBufferDesc);
    if(!rayTracingState().m_shadowInstanceMaterialBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    rayTracingState().m_shadowInstanceMaterialCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow-owned combined instance buffer (g_NwbMeshInstances for the trace): InstanceGpuData per occluder,
    // structured SRV, grows by doubling like the draw pass's instance buffer. Built each frame over ALL gathered
    // occluders so the trace's surface hook can resolve the mutable storage offset that lives in translation.w.
    if(instanceCount == 0u)
        return true;
    if(rayTracingState().m_shadowInstanceBuffer && rayTracingState().m_shadowInstanceCapacity >= instanceCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(rayTracingState().m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(Name("shadow_instance_context"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_shadowInstanceBuffer = graphics().createBuffer(instanceBufferDesc);
    if(!rayTracingState().m_shadowInstanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    rayTracingState().m_shadowInstanceCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Shadow-owned combined material-typed buffer (g_NwbMaterialTypedWords for the trace): each occluder's
    // constant + mutable typed blocks, word-strided structured SRV, grows by doubling like the draw pass's typed
    // buffer. Always at least one word so the binding is valid even with no transparent occluders.
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
    if(rayTracingState().m_shadowMaterialTypedBuffer && rayTracingState().m_shadowMaterialTypedCapacity >= requiredByteCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(rayTracingState().m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setDebugName(Name("shadow_material_typed"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_shadowMaterialTypedBuffer = graphics().createBuffer(materialTypedBufferDesc);
    if(!rayTracingState().m_shadowMaterialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    rayTracingState().m_shadowMaterialTypedCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

