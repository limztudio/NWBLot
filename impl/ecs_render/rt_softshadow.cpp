// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SoftShadowResolveBindingSetInputs{
    Core::Texture* softHalf = nullptr;
    Core::Texture* output = nullptr;
    Core::Texture* inputColor = nullptr;
    Core::Texture* moments = nullptr;
    Core::Format::Enum outputFormat = Core::Format::UNKNOWN;
    Core::TextureDimension::Enum outputDimension = Core::TextureDimension::Unknown;
};

[[nodiscard]] Core::BindingSetHandle CreateSoftShadowResolveBindingSet(
    Core::Alloc::GlobalArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    DeferredFrameTargets& targets,
    Core::Texture* const geometry,
    Core::Texture* const depth,
    Core::Texture* const visibility,
    Core::Texture* const worldPosition,
    Core::Texture* const normal,
    Core::Buffer* const sceneShading,
    const SoftShadowResolveBindingSetInputs& inputs
){
    Core::BindingSetDesc desc(arena);
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF,
        inputs.softHalf,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RESOLVE_BINDING_GEOMETRY,
        geometry,
        targets.shadowSoftGeometryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH,
        depth,
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RESOLVE_BINDING_OUTPUT,
        inputs.output,
        inputs.outputFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        inputs.outputDimension
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR,
        inputs.inputColor,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RESOLVE_BINDING_VISIBILITY,
        visibility,
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RESOLVE_BINDING_MOMENTS,
        inputs.moments,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS,
        worldPosition,
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL,
        normal,
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING, sceneShading));
    return device.createBindingSet(desc, layout);
}

[[nodiscard]] Core::BindingSetHandle CreateShadowReprojectMergeBindingSet(
    Core::Alloc::GlobalArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    DeferredFrameTargets& targets,
    Core::Texture* const softTrace,
    Core::Texture* const geometryCurr,
    Core::Texture* const geometryPrev,
    Core::Texture* const worldPosition,
    Core::Texture* const histIn,
    Core::Texture* const momentsIn,
    Core::Texture* const histOut,
    Core::Texture* const momentsOut
){
    Core::BindingSetDesc desc(arena);
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE,
        softTrace,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN,
        histIn,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN,
        momentsIn,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR,
        geometryCurr,
        targets.shadowSoftGeometryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV,
        geometryPrev,
        targets.shadowSoftGeometryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS,
        worldPosition,
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT,
        histOut,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT,
        momentsOut,
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    return device.createBindingSet(desc, layout);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 frameIndex, u32 softGroupsX, u32 softGroupsY){
    // Backend-agnostic soft-shadow denoise + transparent fold, run AFTER whichever backend (SW or HW) wrote the half-res
    // soft opaque trace into shadowSoftHalfA (and synced it to UnorderedAccess): geometry downsample -> per-slot [temporal
    // reproject-merge -> a-trous resolve OVERWRITE] -> the guarded soft COLORED-TRANSPARENT trace+fold -> temporal history
    // swap. It reads only the shared soft/temporal DeferredFrameTargets buffers + the G-buffer, so the same chain denoises
    // HW RayQuery and SW BVH opaque-soft traces. The transparent trace+fold always traces against the SW transparent-only
    // scene BVH via m_swShadowBindingSet; on the HW path this block stages those resources before the transparent trace.
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;

    // Only the transparent soft trace needs a per-pass ComputeState over the SW shadow binding set (it always traces
    // against the SW transparent-only scene BVH, on both backends); the opaque downsample/merge/resolve build their own
    // states from their dedicated sets. Reached only inside the m_softTransparentReady branch below.
    const auto passState = [&](const Core::ComputePipelineHandle& pipeline){
        Core::ComputeState state;
        state.setPipeline(pipeline.get());
        state.addBindingSet(rayTracingState().m_swShadowBindingSet.get());
        return state;
    };

    // Geometry downsample: fill the half-res packed geometry cache ONCE (slot-independent) before the per-slot
    // resolve loop taps it. Writes the cache UAV; each slot's resolve then reads it as an SRV (the cache is not
    // rewritten, so the UAV->SRV transition happens once and every directional slot shares it).
    {
        commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowGeometryDownsampleBindingSet.get());
        commandList.commitBarriers();

        ShadowGeometryDownsamplePushConstants geometryPush;
        geometryPush.width = targets.width;
        geometryPush.height = targets.height;
        geometryPush.halfWidth = softHalfWidth;
        geometryPush.halfHeight = softHalfHeight;

        Core::ComputeState geometryState;
        geometryState.setPipeline(rayTracingState().m_shadowGeometryDownsamplePipeline.get());
        geometryState.addBindingSet(rayTracingState().m_shadowGeometryDownsampleBindingSet.get());
        commandList.setComputeState(geometryState);
        commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
        commandList.dispatch(softGroupsX, softGroupsY, 1u);
    }

    // Stage-3 temporal insertion: when the merge is ready, select the merge binding set + the resolve's temporal
    // PREPARE override by the ping-pong front state (frontIsA==1 -> AtoB writes hist-B, resolve reads hist-B;
    // frontIsA==0 -> BtoA writes hist-A, resolve reads hist-A). historyValid gates the very first frame / a post-
    // resize frame to pure-current (n=0) so the merge never reprojects through a stale matrix into fresh garbage.
    const bool temporalActive = rayTracingState().m_softShadowTemporalReady;
    const bool frontIsA = rayTracingState().m_softShadowHistoryFrontIsA != 0u;
    Core::BindingSet* const mergeSet = temporalActive
        ? (frontIsA
            ? rayTracingState().m_shadowReprojectMergeBindingSetAtoB.get()
            : rayTracingState().m_shadowReprojectMergeBindingSetBtoA.get())
        : nullptr
    ;
    Core::BindingSet* const temporalPrepareSet = temporalActive
        ? (frontIsA
            ? rayTracingState().m_shadowResolveBindingSetTemporalHistB.get()
            : rayTracingState().m_shadowResolveBindingSetTemporalHistA.get())
        : nullptr
    ;
    const u32 historyValid = (temporalActive
        && rayTracingState().m_prevWorldToClipValid
        && rayTracingState().m_softShadowTemporalSeeded) ? 1u : 0u;

    // Active slot SPAN: the merge + resolve shaders loop [slotStart, slotStart+slotCount) per pixel over the per-slot
    // Texture2DArray layers, so one dispatch can cover the whole span (3 shadow-slot lights -> 1 dispatch), cutting the
    // dispatch/barrier count ~3x. This is compute-preserving: each layer is independent and the shader does the identical
    // per-slot work.
    // Cover [0, highestSetBit+1). For the normal contiguous case (lights get slots 0,1,2) this IS the active count; any
    // gap-slot inside the range is harmless (its unused visibility layer is trivially re-denoised into a dead target).
    u32 slotSpan = 0u;
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((rayTracingState().m_softShadowSlotMask & (1u << slot)) != 0u)
            slotSpan = slot + 1u;
    }
    const u32 slotRangeStart = 0u;
    const u32 slotRangeCount = slotSpan;

    // Denoise + upsample the whole active slot RANGE in ONE dispatch chain. With temporal on, a single range-wide
    // reproject-merge runs FIRST (accumulating this frame's trace into every slot's history), then the a-trous resolve
    // reads the accumulated history via temporalPrepareSet; else the resolve reads the raw trace directly.
    {
        if(temporalActive){
            // Reproject-merge (half-res, whole range): reads the raw trace (soft-A) + prev history/moments + curr/prev
            // geometry + the full-res world-position G-buffer; writes the accumulated visibility (history-out) +
            // moments for every slot layer. setResourceStatesForBindingSet transitions the raw trace + geometry +
            // world-pos to SRV and the history/moments out to UAV (covering the whole array in one shot); a UAV barrier
            // (enabled above) then orders the write before the resolve PREPARE reads history-out as an SRV.
            commandList.setResourceStatesForBindingSet(mergeSet);
            commandList.commitBarriers();

            ShadowReprojectMergePushConstants mergePush;
            mergePush.prevWorldToClip = rayTracingState().m_prevWorldToClip;
            mergePush.width = targets.width;
            mergePush.height = targets.height;
            mergePush.halfWidth = softHalfWidth;
            mergePush.halfHeight = softHalfHeight;
            mergePush.lightSlotStart = slotRangeStart;
            mergePush.lightSlotCount = slotRangeCount;
            mergePush.historyValid = historyValid;

            Core::ComputeState mergeState;
            mergeState.setPipeline(rayTracingState().m_shadowReprojectMergePipeline.get());
            mergeState.addBindingSet(mergeSet);
            commandList.setComputeState(mergeState);
            commandList.setPushConstants(&mergePush, sizeof(mergePush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }

        // Opaque soft resolve: scalar pipeline, its own base sets, OVERWRITE the visibility. The dispatch struct lets
        // ONE routine serve both the opaque (here) and the transparent (below) resolve.
        SoftShadowResolveDispatch opaqueDispatch;
        opaqueDispatch.pipeline = rayTracingState().m_shadowResolvePipeline.get();
        opaqueDispatch.outputHalfA = rayTracingState().m_shadowResolveBindingSetOutputHalfA.get();
        opaqueDispatch.outputHalfB = rayTracingState().m_shadowResolveBindingSetOutputHalfB.get();
        opaqueDispatch.upsample = rayTracingState().m_shadowResolveBindingSetUpsample.get();
        opaqueDispatch.prepareOverride = temporalPrepareSet;
        opaqueDispatch.fold = SoftShadowUpsampleFold::Overwrite;
        // Opaque path stays FULL quality: the 5-pass (dilation 1,2,4,8,16) a-trous the sharp binary blocker edge needs.
        opaqueDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_PASS_COUNT);
        dispatchSoftShadowResolve(commandList, targets, slotRangeStart, slotRangeCount, opaqueDispatch);
    }

    // The opaque resolve left the visibility in UnorderedAccess (its final UPSAMPLE UAV write) + soft scratch in
    // whatever the last pass set.

    // ---- Soft COLORED TRANSPARENT shadow, FOLD-MULTIPLIED onto the soft-opaque visibility ----
    // A parallel colored pipeline, separately traced + temporally denoised + RGB a-trous'd, folded (multiplied) onto
    // the opaque visibility only at the final upsample: visibility = opaqueSoftUpsampled * transparentSoftUpsampled.
    // The opaque binary Bernoulli signal and colored chord-variance RGB signal have different noise stats, so they are
    // denoised independently. This traces colored transmittance against the SW transparent-only scene BVH on both backends.
    if(rayTracingState().m_softTransparentReady){
        // (a) UAV barrier: the opaque UPSAMPLE's visibility WRITES must be ordered before the transparent UPSAMPLE's
        // read-modify-write READS the same image (a WAW/RAW hazard on shadowVisibility). The visibility UAV barrier
        // was enabled earlier for the opaque sub-passes; a same-state transition here emits the ordering barrier.
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // (a2) Stage the SW shadow binding set's resources before the transparent trace uses that set. This is the
        // critical HW-path barrier: the HW opaque-soft trace above ran through m_shadowSoftBindingSet, so the SW set's
        // BVH / per-mesh geometry / G-buffer SRVs / transparentSoftHalf UAV were NEVER transitioned for this trace --
        // they are still in whatever state their last writer left (per-mesh node/geometry buffers from the SW BVH build
        // pass, the material context from buildSceneSwBvh, etc.). Move each mesh's node/position/index/attribute buffer
        // to ShaderResource, then the two shadow-owned material-context buffers, then derive the rest (scene BVH read,
        // G-buffer SRVs, transparentSoftHalf UAV) from the SW set. This mirrors the staging at the top of
        // renderGpuBvhShadowVisibility EXACTLY. On the SW path it is a harmless idempotent no-op: those resources were
        // already staged at the top of the SW render, so setResourceStatesForBindingSet finds them already in their
        // target states and emits no barriers.
        for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
            commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        }
        commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_swShadowBindingSet.get());
        commandList.commitBarriers();

        // (b) Soft transparent trace: one cone-jittered COLORED transmittance sample per HALF-res pixel into
        // transparentSoftHalf (all slot lights at once), TRANSPARENT occluder class. Reuses the SAME frameIndex as the
        // opaque trace -- the shader's compile-time salt decorrelates its low-discrepancy stream. Enable the UAV
        // barrier on transparentSoftHalf so the trace write is ordered before the merge / resolve PREPARE reads it.
        commandList.setEnableUavBarriersForTexture(targets.transparentSoftHalf.get(), true);
        if(rayTracingState().m_softTransparentTemporalReady){
            commandList.setEnableUavBarriersForTexture(targets.transparentHistA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.transparentHistB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.transparentMomentsA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.transparentMomentsB.get(), true);
        }

        SwShadowTransparentSoftPushConstants transparentTracePush;
        transparentTracePush.width = targets.width;
        transparentTracePush.height = targets.height;
        transparentTracePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        transparentTracePush.frameIndex = frameIndex;
        commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentSoftPipeline));
        commandList.setPushConstants(&transparentTracePush, sizeof(transparentTracePush));
        commandList.dispatch(softGroupsX, softGroupsY, 1u);

        // Sync transparentSoftHalf (trace write -> the merge / resolve PREPARE reads it as an SRV).
        commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // (c) The geometry cache is ALREADY filled (shared with the opaque path, filled once above) -- do NOT re-run
        // the geometry downsample. Reuse the SAME temporal front-state gate values (frontIsA / historyValid) as the
        // opaque path so both histories stay in lockstep under the single frame-end selector flip.
        const bool transparentTemporalActive = rayTracingState().m_softTransparentTemporalReady;
        Core::BindingSet* const transparentMergeSet = transparentTemporalActive
            ? (frontIsA
                ? rayTracingState().m_transparentReprojectMergeBindingSetAtoB.get()
                : rayTracingState().m_transparentReprojectMergeBindingSetBtoA.get())
            : nullptr
        ;
        Core::BindingSet* const transparentPrepareSet = transparentTemporalActive
            ? (frontIsA
                ? rayTracingState().m_transparentResolveBindingSetTemporalHistB.get()
                : rayTracingState().m_transparentResolveBindingSetTemporalHistA.get())
            : nullptr
        ;

        {
            if(transparentTemporalActive){
                // Transparent reproject-merge (same RGB-safe merge pipeline; its own front/back set over the
                // transparent hist/moments + transparentSoftHalf + the SHARED geometry caches + world-position). ONE
                // range-wide dispatch, matching the opaque collapse: the shader loops [slotStart, slotStart+slotCount).
                commandList.setResourceStatesForBindingSet(transparentMergeSet);
                commandList.commitBarriers();

                ShadowReprojectMergePushConstants transparentMergePush;
                transparentMergePush.prevWorldToClip = rayTracingState().m_prevWorldToClip;
                transparentMergePush.width = targets.width;
                transparentMergePush.height = targets.height;
                transparentMergePush.halfWidth = softHalfWidth;
                transparentMergePush.halfHeight = softHalfHeight;
                transparentMergePush.lightSlotStart = slotRangeStart;
                transparentMergePush.lightSlotCount = slotRangeCount;
                transparentMergePush.historyValid = historyValid;

                Core::ComputeState transparentMergeState;
                transparentMergeState.setPipeline(rayTracingState().m_shadowReprojectMergePipeline.get());
                transparentMergeState.addBindingSet(transparentMergeSet);
                commandList.setComputeState(transparentMergeState);
                commandList.setPushConstants(&transparentMergePush, sizeof(transparentMergePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
            }

            // Transparent RGB resolve: RGB pipeline, its OWN base sets (over transparentSoftHalf + the shared soft-A/B
            // scratch + the SAME shadowVisibility as the fold target), MULTIPLY fold. The prepareOverride (temporal)
            // swaps PREPARE to the accumulated transparent history + drives momentsValid. ONE dispatch over the whole
            // active slot range (folds each slot's colored transmittance onto that slot's opaque visibility layer).
            SoftShadowResolveDispatch transparentDispatch;
            transparentDispatch.pipeline = rayTracingState().m_shadowResolveRgbPipeline.get();
            transparentDispatch.outputHalfA = rayTracingState().m_transparentResolveBindingSetOutputHalfA.get();
            transparentDispatch.outputHalfB = rayTracingState().m_transparentResolveBindingSetOutputHalfB.get();
            transparentDispatch.upsample = rayTracingState().m_transparentResolveBindingSetUpsample.get();
            transparentDispatch.prepareOverride = transparentPrepareSet;
            transparentDispatch.fold = SoftShadowUpsampleFold::Multiply;
            // Cheaper than opaque: the smooth colored tint reconstructs from a 3-pass (dilation 1,2,4) a-trous. Both odd.
            transparentDispatch.waveletPassCount = static_cast<u32>(NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT);
            dispatchSoftShadowResolve(commandList, targets, slotRangeStart, slotRangeCount, transparentDispatch);
        }
    }

    // Frame-end stash + ping-pong for the temporal accumulator (a no-op when temporal is off): stash this frame's
    // worldToClip for next-frame reprojection and swap the history / moments / geometry ping-pong so this frame's
    // accumulated output + geometry become next frame's history-in + prev-geometry. Covers BOTH the opaque and the
    // transparent histories (both keyed off the single m_softShadowHistoryFrontIsA selector, flipped once here).
    swapSoftShadowTemporalHistory(targets);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowGeometryDownsamplePipeline(){
    if(rayTracingState().m_shadowGeometryDownsamplePipeline)
        return true;
    if(rayTracingState().m_shadowGeometryDownsamplePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowGeometryDownsamplePushConstants)));

        rayTracingState().m_shadowGeometryDownsampleBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowGeometryDownsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow geometry downsample binding layout"));
            rayTracingState().m_shadowGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowGeometryDownsampleShader,
        AssetsGraphicsShadow::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_ShadowGeometryDownsample"
    )){
        rayTracingState().m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowGeometryDownsampleShader)
        .addBindingLayout(rayTracingState().m_shadowGeometryDownsampleBindingLayout)
    ;
    rayTracingState().m_shadowGeometryDownsamplePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow geometry downsample compute pipeline"));
        rayTracingState().m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowGeometryDownsampleBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowGeometryDownsampleBindingLayout);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);

    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* normalTarget = targets.normal.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* geometryTarget = targets.shadowSoftGeometry.get();
    if(
        rayTracingState().m_shadowGeometryDownsampleBindingSet
        && rayTracingState().m_shadowGeometryDownsampleWorldPosition == worldPositionTarget
        && rayTracingState().m_shadowGeometryDownsampleNormal == normalTarget
        && rayTracingState().m_shadowGeometryDownsampleDepth == depthTarget
        && rayTracingState().m_shadowGeometryDownsampleGeometry == geometryTarget
    )
        return true;

    auto* device = graphics().getDevice();

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION,
        worldPositionTarget,
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_NORMAL,
        normalTarget,
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH,
        depthTarget,
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT,
        geometryTarget,
        targets.shadowSoftGeometryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    Core::BindingSetHandle bindingSet = device->createBindingSet(desc, rayTracingState().m_shadowGeometryDownsampleBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow geometry downsample binding set"));
        rayTracingState().m_shadowGeometryDownsampleBindingSet = nullptr;
        rayTracingState().m_shadowGeometryDownsampleWorldPosition = nullptr;
        rayTracingState().m_shadowGeometryDownsampleNormal = nullptr;
        rayTracingState().m_shadowGeometryDownsampleDepth = nullptr;
        rayTracingState().m_shadowGeometryDownsampleGeometry = nullptr;
        return false;
    }
    rayTracingState().m_shadowGeometryDownsampleBindingSet = Move(bindingSet);
    rayTracingState().m_shadowGeometryDownsampleWorldPosition = worldPositionTarget;
    rayTracingState().m_shadowGeometryDownsampleNormal = normalTarget;
    rayTracingState().m_shadowGeometryDownsampleDepth = depthTarget;
    rayTracingState().m_shadowGeometryDownsampleGeometry = geometryTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftShadowResolvePipeline(){
    if(rayTracingState().m_shadowResolvePipeline)
        return true;
    if(rayTracingState().m_shadowResolvePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GEOMETRY, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RESOLVE_BINDING_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RESOLVE_BINDING_VISIBILITY, 1));
        // The SVGF temporal moments SRV (variance-coupled a-trous) + the full-res world-pos/normal SRVs and the
        // scene-shading CB (full-res-guided upsample). The moments SRV is a dummy on the non-temporal path (see dispatch).
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_MOMENTS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowResolvePushConstants)));

        rayTracingState().m_shadowResolveBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow resolve binding layout"));
            rayTracingState().m_shadowResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowResolveShader,
        AssetsGraphicsShadow::s_SoftResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowResolve"
    )){
        rayTracingState().m_shadowResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowResolveShader)
        .addBindingLayout(rayTracingState().m_shadowResolveBindingLayout)
    ;
    rayTracingState().m_shadowResolvePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow resolve compute pipeline"));
        rayTracingState().m_shadowResolvePipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftTransparentResolvePipeline(){
    // RGB variant of the soft-shadow a-trous resolve: the SAME shadow_resolve source cooked with
    // NWB_SHADOW_RESOLVE_CHANNELS=3 (via the shadow_resolve_rgb_cs wrapper). It shares the resolve BINDING LAYOUT (the
    // bindings are identical; only the wavelet channel count + a runtime fold flag differ), created by
    // ensureSoftShadowResolvePipeline -- always called first (m_softTransparentReady is gated on m_softShadowReady), so the
    // layout is resident. Idempotent per handle; a prior hard failure is sticky.
    if(rayTracingState().m_shadowResolveRgbPipeline)
        return true;
    if(rayTracingState().m_shadowResolveRgbPipelineFailed)
        return false;

    NWB_ASSERT(rayTracingState().m_shadowResolveBindingLayout); // opaque resolve pipeline (built first) owns the shared layout

    auto* device = graphics().getDevice();

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowResolveRgbShader,
        AssetsGraphicsShadow::s_SoftResolveRgbShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowResolveRgb"
    )){
        rayTracingState().m_shadowResolveRgbPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowResolveRgbShader)
        .addBindingLayout(rayTracingState().m_shadowResolveBindingLayout)
    ;
    rayTracingState().m_shadowResolveRgbPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowResolveRgbPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow RGB resolve compute pipeline"));
        rayTracingState().m_shadowResolveRgbPipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftShadowResolveBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowResolveBindingLayout);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.shadowSoftHalfB);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(targets.shadowHistA);
    NWB_ASSERT(targets.shadowHistB);
    NWB_ASSERT(targets.shadowMomentsA);
    NWB_ASSERT(targets.shadowMomentsB);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);

    Core::Texture* softATarget = targets.shadowSoftHalfA.get();
    Core::Texture* softBTarget = targets.shadowSoftHalfB.get();
    Core::Texture* geometryTarget = targets.shadowSoftGeometry.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* visibilityTarget = targets.shadowVisibility.get();
    // The two TEMPORAL SOFT_HALF variants read the accumulated history (hist-A / hist-B) as the PREPARE input instead of the
    // raw soft-A trace. Track their bound handles too so a resize / frame-end swap that changes which physical texture the
    // hist role points at rebuilds the sets (mirrors the base tracked-pointer rebuild).
    Core::Texture* histATarget = targets.shadowHistA.get();
    Core::Texture* histBTarget = targets.shadowHistB.get();
    // SVGF moments buffers (bound as the MOMENTS SRV on the matching temporal set; a dummy on the others),
    // plus the full-res world-position + normal G-buffers the guided upsample reads. All tracked for the resize rebuild.
    Core::Texture* momentsATarget = targets.shadowMomentsA.get();
    Core::Texture* momentsBTarget = targets.shadowMomentsB.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* normalTarget = targets.normal.get();
    if(
        rayTracingState().m_shadowResolveBindingSetOutputHalfA
        && rayTracingState().m_shadowResolveBindingSetOutputHalfB
        && rayTracingState().m_shadowResolveBindingSetUpsample
        && rayTracingState().m_shadowResolveBindingSetTemporalHistA
        && rayTracingState().m_shadowResolveBindingSetTemporalHistB
        && rayTracingState().m_shadowResolveBindingSetSoftHalfA == softATarget
        && rayTracingState().m_shadowResolveBindingSetSoftHalfB == softBTarget
        && rayTracingState().m_shadowResolveBindingSetGeometry == geometryTarget
        && rayTracingState().m_shadowResolveBindingSetDepth == depthTarget
        && rayTracingState().m_shadowResolveBindingSetVisibility == visibilityTarget
        && rayTracingState().m_shadowResolveBindingSetTemporalHistATex == histATarget
        && rayTracingState().m_shadowResolveBindingSetTemporalHistBTex == histBTarget
        && rayTracingState().m_shadowResolveBindingSetMomentsA == momentsATarget
        && rayTracingState().m_shadowResolveBindingSetMomentsB == momentsBTarget
        && rayTracingState().m_shadowResolveBindingSetWorldPos == worldPositionTarget
        && rayTracingState().m_shadowResolveBindingSetNormal == normalTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // SOFT_HALF is the soft trace target (soft-A), read only by the PREPARE stage (which runs on the OutputHalfB
    // set). The three sets differ in the (SOFT_HALF, OUTPUT, INPUT_COLOR) triple, chosen so NO set ever binds the same
    // texture as both an SRV and a UAV (which the resource-state framework cannot resolve to one state):
    //  - OutputHalfB: softHalf=soft-A, out=soft-B, in=soft-A -- PREPARE (copies soft-A -> soft-B) + the even wavelets.
    //  - OutputHalfA: softHalf=soft-B, out=soft-A, in=soft-B -- the odd wavelets. SOFT_HALF is bound-but-unused here, so
    //                 pointing it at soft-B (not soft-A == OUTPUT) avoids an SRV+UAV alias of soft-A in this set.
    //  - Upsample:    softHalf=soft-A, out=full-res visibility, in=soft-A (the final wavelet lands in soft-A, odd count).
    // The half + full targets are dimensionless in the set (the bound texture carries the extent), so one layout serves.
    // momentsTex: the MOMENTS SRV source for this set. For the two temporal variants it is the merge's history-OUT moments
    // buffer (the accumulated moments this frame's a-trous should read); for the non-temporal sets it is a valid-but-unused
    // dummy (any half-res array) -- the shader guards the read behind push.momentsValid == 0, so the dummy is never sampled.
    const auto buildSet = [&](Core::Texture* softHalfTex, Core::Texture* outputTex, Core::Format::Enum outputFormat, Core::TextureDimension::Enum outputDim, Core::Texture* inputTex, Core::Texture* momentsTex) -> Core::BindingSetHandle {
        SoftShadowResolveBindingSetInputs inputs;
        inputs.softHalf = softHalfTex;
        inputs.output = outputTex;
        inputs.inputColor = inputTex;
        inputs.moments = momentsTex;
        inputs.outputFormat = outputFormat;
        inputs.outputDimension = outputDim;
        return CreateSoftShadowResolveBindingSet(
            arena(),
            *device,
            rayTracingState().m_shadowResolveBindingLayout,
            targets,
            geometryTarget,
            depthTarget,
            visibilityTarget,
            worldPositionTarget,
            normalTarget,
            deferredState().m_sceneShadingBuffer.get(),
            inputs
        );
    };

    // OUTPUT is a half-res Texture2DArray for the ping-pong sets; for the upsample it is UNUSED (the upsample writes the
    // full-res VISIBILITY UAV instead) but must still be a valid binding -- point it at soft-B (a half-res array).
    // The non-temporal sets never sample the MOMENTS SRV (push.momentsValid == 0 on those dispatches), so bind moments-A as
    // an inert dummy. The two temporal variants bind the moments buffer PAIRED with the hist buffer they read as SOFT_HALF
    // (hist-A <-> moments-A, hist-B <-> moments-B), i.e. the merge's accumulated moments for this frame's a-trous.
    Core::BindingSetHandle outputHalfA = buildSet(softBTarget, softATarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, softBTarget, momentsATarget);
    Core::BindingSetHandle outputHalfB = buildSet(softATarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, softATarget, momentsATarget);
    Core::BindingSetHandle upsample    = buildSet(softATarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, softATarget, momentsATarget);
    // Two TEMPORAL variants: PREPARE reads the accumulated history (hist-A / hist-B) as SOFT_HALF (+ INPUT_COLOR, unused by
    // PREPARE), writes soft-B (out). SOFT_HALF == hist buffer is DISTINCT from the ping-pong output soft-A/soft-B, so no
    // SRV+UAV alias. The dispatch picks the variant matching the merge's history-out buffer (B when frontIsA, else A). Each
    // binds its paired moments buffer as the MOMENTS SRV so the variance-coupled a-trous reads the accumulated moments.
    Core::BindingSetHandle temporalHistA = buildSet(histATarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, histATarget, momentsATarget);
    Core::BindingSetHandle temporalHistB = buildSet(histBTarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, histBTarget, momentsBTarget);
    if(!outputHalfA || !outputHalfB || !upsample || !temporalHistA || !temporalHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow resolve binding sets"));
        rayTracingState().m_shadowResolveBindingSetOutputHalfA = nullptr;
        rayTracingState().m_shadowResolveBindingSetOutputHalfB = nullptr;
        rayTracingState().m_shadowResolveBindingSetUpsample = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistA = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistB = nullptr;
        rayTracingState().m_shadowResolveBindingSetSoftHalfA = nullptr;
        rayTracingState().m_shadowResolveBindingSetSoftHalfB = nullptr;
        rayTracingState().m_shadowResolveBindingSetGeometry = nullptr;
        rayTracingState().m_shadowResolveBindingSetDepth = nullptr;
        rayTracingState().m_shadowResolveBindingSetVisibility = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistATex = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistBTex = nullptr;
        rayTracingState().m_shadowResolveBindingSetMomentsA = nullptr;
        rayTracingState().m_shadowResolveBindingSetMomentsB = nullptr;
        rayTracingState().m_shadowResolveBindingSetWorldPos = nullptr;
        rayTracingState().m_shadowResolveBindingSetNormal = nullptr;
        return false;
    }
    rayTracingState().m_shadowResolveBindingSetOutputHalfA = Move(outputHalfA);
    rayTracingState().m_shadowResolveBindingSetOutputHalfB = Move(outputHalfB);
    rayTracingState().m_shadowResolveBindingSetUpsample = Move(upsample);
    rayTracingState().m_shadowResolveBindingSetTemporalHistA = Move(temporalHistA);
    rayTracingState().m_shadowResolveBindingSetTemporalHistB = Move(temporalHistB);
    rayTracingState().m_shadowResolveBindingSetSoftHalfA = softATarget;
    rayTracingState().m_shadowResolveBindingSetSoftHalfB = softBTarget;
    rayTracingState().m_shadowResolveBindingSetGeometry = geometryTarget;
    rayTracingState().m_shadowResolveBindingSetDepth = depthTarget;
    rayTracingState().m_shadowResolveBindingSetVisibility = visibilityTarget;
    rayTracingState().m_shadowResolveBindingSetTemporalHistATex = histATarget;
    rayTracingState().m_shadowResolveBindingSetTemporalHistBTex = histBTarget;
    rayTracingState().m_shadowResolveBindingSetMomentsA = momentsATarget;
    rayTracingState().m_shadowResolveBindingSetMomentsB = momentsBTarget;
    rayTracingState().m_shadowResolveBindingSetWorldPos = worldPositionTarget;
    rayTracingState().m_shadowResolveBindingSetNormal = normalTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftTransparentResolveBindingSet(DeferredFrameTargets& targets){
    // Parallel transparent resolve binding sets (mirror of ensureSoftShadowResolveBindingSet, over the colored
    // buffers). They share the resolve BINDING LAYOUT + the SAME half-res ping-pong SCRATCH (soft-A/soft-B) + the SAME
    // full-res shadowVisibility as the opaque resolve; only the (SOFT_HALF, INPUT-history, MOMENTS) sources differ:
    //  - the RAW colored trace lives in transparentSoftHalf (NOT soft-A), so PREPARE reads it as SOFT_HALF into soft-B.
    //  - the wavelets ping-pong on soft-A/soft-B exactly as opaque (INPUT_COLOR is the ping-pong scratch, not the raw trace).
    //  - the two temporal variants read the accumulated transparent history (transparentHistA/B) as SOFT_HALF instead.
    // Runs strictly AFTER the opaque resolve each frame (sequential dispatch), so sharing the scratch is race-free.
    NWB_ASSERT(rayTracingState().m_shadowResolveBindingLayout);
    NWB_ASSERT(targets.transparentSoftHalf);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.shadowSoftHalfB);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(targets.transparentHistA);
    NWB_ASSERT(targets.transparentHistB);
    NWB_ASSERT(targets.transparentMomentsA);
    NWB_ASSERT(targets.transparentMomentsB);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);

    Core::Texture* rawTraceTarget = targets.transparentSoftHalf.get();
    Core::Texture* scratchATarget = targets.shadowSoftHalfA.get();
    Core::Texture* scratchBTarget = targets.shadowSoftHalfB.get();
    Core::Texture* geometryTarget = targets.shadowSoftGeometry.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* visibilityTarget = targets.shadowVisibility.get();
    Core::Texture* histATarget = targets.transparentHistA.get();
    Core::Texture* histBTarget = targets.transparentHistB.get();
    Core::Texture* momentsATarget = targets.transparentMomentsA.get();
    Core::Texture* momentsBTarget = targets.transparentMomentsB.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* normalTarget = targets.normal.get();
    if(
        rayTracingState().m_transparentResolveBindingSetOutputHalfA
        && rayTracingState().m_transparentResolveBindingSetOutputHalfB
        && rayTracingState().m_transparentResolveBindingSetUpsample
        && rayTracingState().m_transparentResolveBindingSetTemporalHistA
        && rayTracingState().m_transparentResolveBindingSetTemporalHistB
        && rayTracingState().m_transparentResolveBindingSetSoftHalf == rawTraceTarget
        && rayTracingState().m_transparentResolveBindingSetScratchA == scratchATarget
        && rayTracingState().m_transparentResolveBindingSetScratchB == scratchBTarget
        && rayTracingState().m_transparentResolveBindingSetGeometry == geometryTarget
        && rayTracingState().m_transparentResolveBindingSetDepth == depthTarget
        && rayTracingState().m_transparentResolveBindingSetVisibility == visibilityTarget
        && rayTracingState().m_transparentResolveBindingSetHistA == histATarget
        && rayTracingState().m_transparentResolveBindingSetHistB == histBTarget
        && rayTracingState().m_transparentResolveBindingSetMomentsA == momentsATarget
        && rayTracingState().m_transparentResolveBindingSetMomentsB == momentsBTarget
        && rayTracingState().m_transparentResolveBindingSetWorldPos == worldPositionTarget
        && rayTracingState().m_transparentResolveBindingSetNormal == normalTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // buildSet: (SOFT_HALF SRV, OUTPUT UAV, INPUT_COLOR SRV, MOMENTS SRV). GEOMETRY/DEPTH/VISIBILITY/WORLDPOS/NORMAL/CB are
    // fixed. No set binds the same texture as both SRV and UAV: the raw colored trace + the two hist buffers are only ever
    // SRVs here; the OUTPUT UAV is always the ping-pong scratch (soft-A/soft-B), distinct from all of them; the VISIBILITY
    // UAV (the fold target) is a separate resource. (The upsample's OUTPUT is bound-but-unused -> soft-B, still no alias.)
    const auto buildSet = [&](Core::Texture* softHalfTex, Core::Texture* outputTex, Core::Texture* inputTex, Core::Texture* momentsTex) -> Core::BindingSetHandle {
        SoftShadowResolveBindingSetInputs inputs;
        inputs.softHalf = softHalfTex;
        inputs.output = outputTex;
        inputs.inputColor = inputTex;
        inputs.moments = momentsTex;
        inputs.outputFormat = targets.shadowSoftFormat;
        inputs.outputDimension = Core::TextureDimension::Texture2DArray;
        return CreateSoftShadowResolveBindingSet(
            arena(),
            *device,
            rayTracingState().m_shadowResolveBindingLayout,
            targets,
            geometryTarget,
            depthTarget,
            visibilityTarget,
            worldPositionTarget,
            normalTarget,
            deferredState().m_sceneShadingBuffer.get(),
            inputs
        );
    };

    // outputHalfB: PREPARE (SOFT_HALF = the raw colored trace -> soft-B) + even wavelets (INPUT_COLOR = soft-A -> soft-B).
    // outputHalfA: odd wavelets (SOFT_HALF bound-unused; INPUT_COLOR = soft-B -> soft-A). SOFT_HALF -> the raw trace here
    //              (an SRV distinct from the soft-A OUTPUT), NOT soft-A, so no SRV+UAV alias of the scratch.
    // upsample: reads INPUT_COLOR = soft-A (the final odd-count result), folds the VISIBILITY (OUTPUT = soft-B, unused).
    Core::BindingSetHandle outputHalfA = buildSet(rawTraceTarget, scratchATarget, scratchBTarget, momentsATarget);
    Core::BindingSetHandle outputHalfB = buildSet(rawTraceTarget, scratchBTarget, scratchATarget, momentsATarget);
    Core::BindingSetHandle upsample    = buildSet(rawTraceTarget, scratchBTarget, scratchATarget, momentsATarget);
    // Temporal variants: PREPARE reads the accumulated transparent history (hist-A / hist-B) as SOFT_HALF -> soft-B; the
    // wavelet ping-pong + INPUT_COLOR are still soft-A/soft-B. Each binds its paired transparent moments as the MOMENTS SRV.
    Core::BindingSetHandle temporalHistA = buildSet(histATarget, scratchBTarget, scratchATarget, momentsATarget);
    Core::BindingSetHandle temporalHistB = buildSet(histBTarget, scratchBTarget, scratchATarget, momentsBTarget);
    if(!outputHalfA || !outputHalfB || !upsample || !temporalHistA || !temporalHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow resolve binding sets"));
        rayTracingState().m_transparentResolveBindingSetOutputHalfA = nullptr;
        rayTracingState().m_transparentResolveBindingSetOutputHalfB = nullptr;
        rayTracingState().m_transparentResolveBindingSetUpsample = nullptr;
        rayTracingState().m_transparentResolveBindingSetTemporalHistA = nullptr;
        rayTracingState().m_transparentResolveBindingSetTemporalHistB = nullptr;
        rayTracingState().m_transparentResolveBindingSetSoftHalf = nullptr;
        rayTracingState().m_transparentResolveBindingSetScratchA = nullptr;
        rayTracingState().m_transparentResolveBindingSetScratchB = nullptr;
        rayTracingState().m_transparentResolveBindingSetGeometry = nullptr;
        rayTracingState().m_transparentResolveBindingSetDepth = nullptr;
        rayTracingState().m_transparentResolveBindingSetVisibility = nullptr;
        rayTracingState().m_transparentResolveBindingSetHistA = nullptr;
        rayTracingState().m_transparentResolveBindingSetHistB = nullptr;
        rayTracingState().m_transparentResolveBindingSetMomentsA = nullptr;
        rayTracingState().m_transparentResolveBindingSetMomentsB = nullptr;
        rayTracingState().m_transparentResolveBindingSetWorldPos = nullptr;
        rayTracingState().m_transparentResolveBindingSetNormal = nullptr;
        return false;
    }
    rayTracingState().m_transparentResolveBindingSetOutputHalfA = Move(outputHalfA);
    rayTracingState().m_transparentResolveBindingSetOutputHalfB = Move(outputHalfB);
    rayTracingState().m_transparentResolveBindingSetUpsample = Move(upsample);
    rayTracingState().m_transparentResolveBindingSetTemporalHistA = Move(temporalHistA);
    rayTracingState().m_transparentResolveBindingSetTemporalHistB = Move(temporalHistB);
    rayTracingState().m_transparentResolveBindingSetSoftHalf = rawTraceTarget;
    rayTracingState().m_transparentResolveBindingSetScratchA = scratchATarget;
    rayTracingState().m_transparentResolveBindingSetScratchB = scratchBTarget;
    rayTracingState().m_transparentResolveBindingSetGeometry = geometryTarget;
    rayTracingState().m_transparentResolveBindingSetDepth = depthTarget;
    rayTracingState().m_transparentResolveBindingSetVisibility = visibilityTarget;
    rayTracingState().m_transparentResolveBindingSetHistA = histATarget;
    rayTracingState().m_transparentResolveBindingSetHistB = histBTarget;
    rayTracingState().m_transparentResolveBindingSetMomentsA = momentsATarget;
    rayTracingState().m_transparentResolveBindingSetMomentsB = momentsBTarget;
    rayTracingState().m_transparentResolveBindingSetWorldPos = worldPositionTarget;
    rayTracingState().m_transparentResolveBindingSetNormal = normalTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchSoftShadowResolve(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 slotStart, u32 slotCount, const SoftShadowResolveDispatch& dispatch){
    // The a-trous denoise + upsample of ONE slot's half-res jittered visibility into the full-res visibility. Cloned from
    // dispatchCausticResolve: PREPARE (copy) -> N wavelet ping-pong passes -> bilateral upsample. Assumes the pipeline +
    // binding sets in `dispatch` are ready, the trace already wrote its raw half-res buffer (this frame) with a UAV barrier,
    // AND the slot-independent geometry downsample already filled the geometry cache (the caller runs it ONCE per frame).
    // ONE routine serves BOTH signals: the OPAQUE resolve (scalar pipeline, its own base sets, Overwrite fold) and the
    // TRANSPARENT resolve (RGB pipeline, its own base sets over transparentSoftHalf/transparentHist, Multiply fold). The
    // ping-pong SCRATCH (the outputHalfA/B sets' OUTPUT + INPUT) is the SAME half-res soft-A/soft-B for both, dispatched
    // strictly sequentially (opaque resolve fully done, then transparent), so they never race on the scratch.
    // TEMPORAL: when the merge ran, dispatch.prepareOverride is the temporal SOFT_HALF variant whose PREPARE reads
    // the ACCUMULATED history instead of the raw trace; it still writes soft-B, so the wavelet + upsample chain is identical.
    // prepareOverride == nullptr (temporal off / first frame) keeps the raw-trace PREPARE AND drives momentsValid=0.
    const u32 halfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));

    const u32 foldValue = static_cast<u32>(dispatch.fold);
    const auto runPass = [&](Core::BindingSet* const bindingSet, const u32 stepWidth, const ShadowResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        commandList.setResourceStatesForBindingSet(bindingSet);
        commandList.commitBarriers();

        ShadowResolvePushConstants resolvePush;
        resolvePush.width = targets.width;
        resolvePush.height = targets.height;
        resolvePush.halfWidth = halfWidth;
        resolvePush.halfHeight = halfHeight;
        resolvePush.stepWidth = stepWidth;
        resolvePush.stage = static_cast<u32>(stage);
        resolvePush.lightSlotStart = slotStart;
        resolvePush.lightSlotCount = slotCount; // ONE dispatch covers the whole active slot RANGE; the shader loops it per pixel
        // The moments SRV holds this-frame integrated temporal moments IFF the merge ran, which is exactly when the caller
        // passes a temporal prepareOverride. So prepareOverride != nullptr is the single source of the momentsValid flag: on
        // it the WAVELET's SVGF variance stop may use the temporal variance; off it never samples the (dummy) moments SRV.
        resolvePush.momentsValid = (dispatch.prepareOverride != nullptr) ? 1u : 0u;
        // OVERWRITE (opaque) vs MULTIPLY-onto-visibility (transparent fold). Ignored by PREPARE/WAVELET (only UPSAMPLE reads it).
        resolvePush.upsampleFold = foldValue;

        Core::ComputeState computeState;
        computeState.setPipeline(dispatch.pipeline);
        computeState.addBindingSet(bindingSet);
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    // PREPARE: copy the half-res traced visibility (SOFT_HALF) into soft-B. The base set (SOFT_HALF == raw trace, out=soft-B,
    // in=raw) never read-write aliases the scratch; the temporal override (SOFT_HALF == the merge's accumulated history buffer,
    // still out=soft-B) reads the accumulated visibility instead. Either way the result lives in soft-B for the wavelets.
    Core::BindingSet* const prepareSet = dispatch.prepareOverride ? dispatch.prepareOverride : dispatch.outputHalfB;
    runPass(prepareSet, 1u, ShadowResolveStage::Prepare, halfGroupsX, halfGroupsY);

    // Half-res a-trous wavelet passes at a doubling dilation, starting from soft-B. Each pass writes the buffer NOT
    // holding its input (outputHalfA reads soft-B writes soft-A; outputHalfB reads soft-A writes soft-B). srcIsHalfB
    // tracks where the latest result lives; PREPARE left it in soft-B so it starts true.
    // The per-signal pass count (opaque = NWB_SHADOW_RESOLVE_PASS_COUNT 5, transparent = NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
    // 3). Both must be ODD so the final result lands in soft-A (see the assert below); both literals are compile-time odd.
    static_assert((NWB_SHADOW_RESOLVE_PASS_COUNT % 2) == 1, "opaque resolve pass count must be ODD (final result must land in soft-A for the upsample)");
    static_assert((NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT % 2) == 1, "transparent resolve pass count must be ODD (final result must land in soft-A for the upsample)");
    bool srcIsHalfB = true;
    for(u32 pass = 0u; pass < dispatch.waveletPassCount; ++pass){
        Core::BindingSet* const bindingSet = srcIsHalfB ? dispatch.outputHalfA : dispatch.outputHalfB;
        runPass(bindingSet, 1u << pass, ShadowResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        srcIsHalfB = !srcIsHalfB;
    }

    // UPSAMPLE (full-res): edge-aware bilateral resample of the FINAL half-res visibility into the full-res visibility
    // slot. The final wavelet result lives in soft-A when waveletPassCount is ODD (5 opaque / 3 transparent) -- srcIsHalfB is
    // now false. Both upsample sets read soft-A (INPUT_COLOR) by construction; assert the parity so a pass-count change is caught.
    NWB_ASSERT(!srcIsHalfB); // waveletPassCount must be odd for the final result to land in soft-A (the upsample's input)
    runPass(dispatch.upsample, 1u, ShadowResolveStage::Upsample, fullGroupsX, fullGroupsY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowReprojectMergePipeline(){
    if(rayTracingState().m_shadowReprojectMergePipeline)
        return true;
    if(rayTracingState().m_shadowReprojectMergePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowReprojectMergeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowReprojectMergePushConstants)));

        rayTracingState().m_shadowReprojectMergeBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowReprojectMergeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow reproject-merge binding layout"));
            rayTracingState().m_shadowReprojectMergePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowReprojectMergeShader,
        AssetsGraphicsShadow::s_SoftReprojectMergeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowReprojectMerge"
    )){
        rayTracingState().m_shadowReprojectMergePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowReprojectMergeShader)
        .addBindingLayout(rayTracingState().m_shadowReprojectMergeBindingLayout)
    ;
    rayTracingState().m_shadowReprojectMergePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowReprojectMergePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow reproject-merge compute pipeline"));
        rayTracingState().m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowReprojectMergeBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowReprojectMergeBindingLayout);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.shadowSoftGeometryPrev);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.shadowHistA);
    NWB_ASSERT(targets.shadowHistB);
    NWB_ASSERT(targets.shadowMomentsA);
    NWB_ASSERT(targets.shadowMomentsB);

    Core::Texture* softTraceTarget = targets.shadowSoftHalfA.get();
    Core::Texture* geometryCurrTarget = targets.shadowSoftGeometry.get();
    Core::Texture* geometryPrevTarget = targets.shadowSoftGeometryPrev.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* histATarget = targets.shadowHistA.get();
    Core::Texture* histBTarget = targets.shadowHistB.get();
    Core::Texture* momentsATarget = targets.shadowMomentsA.get();
    Core::Texture* momentsBTarget = targets.shadowMomentsB.get();
    if(
        rayTracingState().m_shadowReprojectMergeBindingSetAtoB
        && rayTracingState().m_shadowReprojectMergeBindingSetBtoA
        && rayTracingState().m_shadowReprojectMergeSoftTrace == softTraceTarget
        && rayTracingState().m_shadowReprojectMergeGeometryCurr == geometryCurrTarget
        && rayTracingState().m_shadowReprojectMergeGeometryPrev == geometryPrevTarget
        && rayTracingState().m_shadowReprojectMergeWorldPosition == worldPositionTarget
        && rayTracingState().m_shadowReprojectMergeHistA == histATarget
        && rayTracingState().m_shadowReprojectMergeHistB == histBTarget
        && rayTracingState().m_shadowReprojectMergeMomentsA == momentsATarget
        && rayTracingState().m_shadowReprojectMergeMomentsB == momentsBTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // Two front/back sets so the accumulated-history SRV (history-in) and the accumulated-history UAV (history-out) never
    // bind the SAME texture (the resource-state framework cannot resolve one texture to both SRV and UAV in one set):
    //  - AtoB: histIn/momIn = A -> histOut/momOut = B  (used when m_softShadowHistoryFrontIsA == 1, i.e. A holds this frame's
    //          incoming history; the merge writes B, which the temporal-histB resolve variant then denoises).
    //  - BtoA: histIn/momIn = B -> histOut/momOut = A  (the mirror; A becomes the accumulated buffer the resolve reads).
    // All other bindings are shared: SOFT_TRACE=soft-A, GEOMETRY_CURR/PREV, WORLDPOS=the full-res world-position G-buffer.
    const auto buildSet = [&](Core::Texture* histInTex, Core::Texture* momInTex, Core::Texture* histOutTex, Core::Texture* momOutTex) -> Core::BindingSetHandle {
        return CreateShadowReprojectMergeBindingSet(
            arena(),
            *device,
            rayTracingState().m_shadowReprojectMergeBindingLayout,
            targets,
            softTraceTarget,
            geometryCurrTarget,
            geometryPrevTarget,
            worldPositionTarget,
            histInTex,
            momInTex,
            histOutTex,
            momOutTex
        );
    };

    Core::BindingSetHandle setAtoB = buildSet(histATarget, momentsATarget, histBTarget, momentsBTarget);
    Core::BindingSetHandle setBtoA = buildSet(histBTarget, momentsBTarget, histATarget, momentsATarget);
    if(!setAtoB || !setBtoA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow reproject-merge binding sets"));
        rayTracingState().m_shadowReprojectMergeBindingSetAtoB = nullptr;
        rayTracingState().m_shadowReprojectMergeBindingSetBtoA = nullptr;
        rayTracingState().m_shadowReprojectMergeSoftTrace = nullptr;
        rayTracingState().m_shadowReprojectMergeGeometryCurr = nullptr;
        rayTracingState().m_shadowReprojectMergeGeometryPrev = nullptr;
        rayTracingState().m_shadowReprojectMergeWorldPosition = nullptr;
        rayTracingState().m_shadowReprojectMergeHistA = nullptr;
        rayTracingState().m_shadowReprojectMergeHistB = nullptr;
        rayTracingState().m_shadowReprojectMergeMomentsA = nullptr;
        rayTracingState().m_shadowReprojectMergeMomentsB = nullptr;
        return false;
    }
    rayTracingState().m_shadowReprojectMergeBindingSetAtoB = Move(setAtoB);
    rayTracingState().m_shadowReprojectMergeBindingSetBtoA = Move(setBtoA);
    rayTracingState().m_shadowReprojectMergeSoftTrace = softTraceTarget;
    rayTracingState().m_shadowReprojectMergeGeometryCurr = geometryCurrTarget;
    rayTracingState().m_shadowReprojectMergeGeometryPrev = geometryPrevTarget;
    rayTracingState().m_shadowReprojectMergeWorldPosition = worldPositionTarget;
    rayTracingState().m_shadowReprojectMergeHistA = histATarget;
    rayTracingState().m_shadowReprojectMergeHistB = histBTarget;
    rayTracingState().m_shadowReprojectMergeMomentsA = momentsATarget;
    rayTracingState().m_shadowReprojectMergeMomentsB = momentsBTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowTransparentReprojectMergeBindingSet(DeferredFrameTargets& targets){
    // The two front/back TRANSPARENT reproject-merge binding sets (mirror of ensureShadowReprojectMergeBindingSet,
    // over the colored buffers). They drive the SAME m_shadowReprojectMergePipeline (the merge shader is fully RGB-safe and
    // reused verbatim); only the SOFT_TRACE / HISTORY / MOMENTS sources are the transparent buffers. The GEOMETRY_CURR/PREV
    // caches + the full-res world-position are SHARED with the opaque merge (same receivers), so this frame's transparent
    // history reprojects through the same stashed prevWorldToClip + gates against the same geometry as the opaque history.
    NWB_ASSERT(rayTracingState().m_shadowReprojectMergeBindingLayout);
    NWB_ASSERT(targets.transparentSoftHalf);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.shadowSoftGeometryPrev);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.transparentHistA);
    NWB_ASSERT(targets.transparentHistB);
    NWB_ASSERT(targets.transparentMomentsA);
    NWB_ASSERT(targets.transparentMomentsB);

    Core::Texture* softTraceTarget = targets.transparentSoftHalf.get();
    Core::Texture* geometryCurrTarget = targets.shadowSoftGeometry.get();
    Core::Texture* geometryPrevTarget = targets.shadowSoftGeometryPrev.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* histATarget = targets.transparentHistA.get();
    Core::Texture* histBTarget = targets.transparentHistB.get();
    Core::Texture* momentsATarget = targets.transparentMomentsA.get();
    Core::Texture* momentsBTarget = targets.transparentMomentsB.get();
    if(
        rayTracingState().m_transparentReprojectMergeBindingSetAtoB
        && rayTracingState().m_transparentReprojectMergeBindingSetBtoA
        && rayTracingState().m_transparentReprojectMergeSoftTrace == softTraceTarget
        && rayTracingState().m_transparentReprojectMergeGeometryCurr == geometryCurrTarget
        && rayTracingState().m_transparentReprojectMergeGeometryPrev == geometryPrevTarget
        && rayTracingState().m_transparentReprojectMergeWorldPosition == worldPositionTarget
        && rayTracingState().m_transparentReprojectMergeHistA == histATarget
        && rayTracingState().m_transparentReprojectMergeHistB == histBTarget
        && rayTracingState().m_transparentReprojectMergeMomentsA == momentsATarget
        && rayTracingState().m_transparentReprojectMergeMomentsB == momentsBTarget
    )
        return true;

    auto* device = graphics().getDevice();

    const auto buildSet = [&](Core::Texture* histInTex, Core::Texture* momInTex, Core::Texture* histOutTex, Core::Texture* momOutTex) -> Core::BindingSetHandle {
        return CreateShadowReprojectMergeBindingSet(
            arena(),
            *device,
            rayTracingState().m_shadowReprojectMergeBindingLayout,
            targets,
            softTraceTarget,
            geometryCurrTarget,
            geometryPrevTarget,
            worldPositionTarget,
            histInTex,
            momInTex,
            histOutTex,
            momOutTex
        );
    };

    Core::BindingSetHandle setAtoB = buildSet(histATarget, momentsATarget, histBTarget, momentsBTarget);
    Core::BindingSetHandle setBtoA = buildSet(histBTarget, momentsBTarget, histATarget, momentsATarget);
    if(!setAtoB || !setBtoA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow reproject-merge binding sets"));
        rayTracingState().m_transparentReprojectMergeBindingSetAtoB = nullptr;
        rayTracingState().m_transparentReprojectMergeBindingSetBtoA = nullptr;
        rayTracingState().m_transparentReprojectMergeSoftTrace = nullptr;
        rayTracingState().m_transparentReprojectMergeGeometryCurr = nullptr;
        rayTracingState().m_transparentReprojectMergeGeometryPrev = nullptr;
        rayTracingState().m_transparentReprojectMergeWorldPosition = nullptr;
        rayTracingState().m_transparentReprojectMergeHistA = nullptr;
        rayTracingState().m_transparentReprojectMergeHistB = nullptr;
        rayTracingState().m_transparentReprojectMergeMomentsA = nullptr;
        rayTracingState().m_transparentReprojectMergeMomentsB = nullptr;
        return false;
    }
    rayTracingState().m_transparentReprojectMergeBindingSetAtoB = Move(setAtoB);
    rayTracingState().m_transparentReprojectMergeBindingSetBtoA = Move(setBtoA);
    rayTracingState().m_transparentReprojectMergeSoftTrace = softTraceTarget;
    rayTracingState().m_transparentReprojectMergeGeometryCurr = geometryCurrTarget;
    rayTracingState().m_transparentReprojectMergeGeometryPrev = geometryPrevTarget;
    rayTracingState().m_transparentReprojectMergeWorldPosition = worldPositionTarget;
    rayTracingState().m_transparentReprojectMergeHistA = histATarget;
    rayTracingState().m_transparentReprojectMergeHistB = histBTarget;
    rayTracingState().m_transparentReprojectMergeMomentsA = momentsATarget;
    rayTracingState().m_transparentReprojectMergeMomentsB = momentsBTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::swapSoftShadowTemporalHistory(DeferredFrameTargets& targets){
    // Frame-end stash + ping-pong for the Stage-3 temporal accumulator. Runs only when the merge was live this frame
    // (m_softShadowTemporalReady), so the cadence never stalls the non-temporal / HW paths.
    //  - STASH: this frame's resolved worldToClip (cached in drawState().m_meshViewGpuData by updateMeshViewBuffer earlier
    //    this frame -- the first field of MeshViewGpuData) is copied into m_prevWorldToClip for NEXT frame's reprojection.
    //  - HISTORY / MOMENTS PING-PONG (SELECTOR FLIP, NOT a handle swap): the two merge binding sets (AtoB in=A/out=B, BtoA
    //    in=B/out=A) already encode both ping-pong directions against the FIXED physical A/B textures. Flipping the selector
    //    alone alternates which set runs: frame N (frontIsA=1) uses AtoB -> accumulates into B; frame N+1 (frontIsA=0) uses
    //    BtoA -> reads B (last frame's out) + accumulates into A; and so on. Swapping the HANDLES too would double-count and
    //    make the merge read the WRONG buffer, so the hist/moments handles are deliberately NOT swapped -- only the selector.
    //  - GEOMETRY PING-PONG (a real HANDLE SWAP): unlike history, the geometry cache has no per-set selector -- the
    //    downsample ALWAYS writes shadowSoftGeometry and the merge ALWAYS reads shadowSoftGeometryPrev, so this frame's curr
    //    must physically become next frame's prev. The handle swap changes which texture each role points at, so the
    //    geometry-downsample + merge binding sets rebuild next frame via their tracked-pointer compare (as a resize does).
    //  - SEED / VALID: the first merge has now run, so history is valid from next frame on.
    if(!rayTracingState().m_softShadowTemporalReady)
        return;

    if(drawState().m_meshViewGpuDataValid){
        // MeshViewGpuData::worldToClip is the leading 16 floats (row-major) of the cached byte blob; copy them raw into the
        // 64-byte push matrix (Float44U raw dump). reinterpret_cast is safe: the ray-tracing system is a RendererDrawState
        // friend and the byte buffer is exactly MeshViewGpuData-shaped (static_assert'd in mesh_view_private.h).
        const auto* meshView = reinterpret_cast<const ECSRenderDetail::MeshViewGpuData*>(drawState().m_meshViewGpuData);
        NWB_MEMCPY(&rayTracingState().m_prevWorldToClip, sizeof(rayTracingState().m_prevWorldToClip), &meshView->worldToClip, sizeof(rayTracingState().m_prevWorldToClip));
        rayTracingState().m_prevWorldToClipValid = true;
    }
    rayTracingState().m_softShadowTemporalSeeded = true;

    Swap(targets.shadowSoftGeometry, targets.shadowSoftGeometryPrev);
    rayTracingState().m_softShadowHistoryFrontIsA ^= 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

