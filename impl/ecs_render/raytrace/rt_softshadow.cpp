// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_softshadow{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SoftShadowResolveBindingSetInputs{
    Core::Texture* output = nullptr;
    Core::Format::Enum outputFormat = Core::Format::UNKNOWN;
    Core::TextureDimension::Enum outputDimension = Core::TextureDimension::Unknown;
};

[[nodiscard]] Core::BindingSetHandle CreateSoftShadowResolveBindingSet(
    Core::Alloc::GlobalArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    DeferredFrameTargets& targets,
    Core::Texture* const visibility,
    Core::Buffer* const sceneShading,
    const SoftShadowResolveBindingSetInputs& inputs
){
    Core::BindingSetDesc desc(arena);
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RESOLVE_BINDING_OUTPUT,
        inputs.output,
        inputs.outputFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        inputs.outputDimension
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RESOLVE_BINDING_VISIBILITY,
        visibility,
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING, sceneShading));
    return device.createBindingSet(desc, layout);
};

[[nodiscard]] Core::BindingSetHandle CreateShadowReprojectMergeBindingSet(
    Core::Alloc::GlobalArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    DeferredFrameTargets& targets,
    Core::Texture* const histOut,
    Core::Texture* const momentsOut
){
    Core::BindingSetDesc desc(arena);
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

struct ShadowReprojectMergeTextures{
    Core::Texture* histA = nullptr;
    Core::Texture* histB = nullptr;
    Core::Texture* momentsA = nullptr;
    Core::Texture* momentsB = nullptr;
};

// The temporal merge's per-dispatch Array SRVs. The two geometry caches + full-res world-position are shared target
// roles, so the dispatch helper obtains their texture pointers and heap slots directly from DeferredFrameTargets.
struct ShadowReprojectMergeHeapResources{
    Core::Texture* softTrace = nullptr;
    Core::Texture* historyIn = nullptr;
    Core::Texture* momentsIn = nullptr;
    u32 softTraceSlot = 0u;
    u32 historyInSlot = 0u;
    u32 momentsInSlot = 0u;
};

struct ShadowReprojectMergeBindingCache{
    Core::BindingSetHandle& setAtoB;
    Core::BindingSetHandle& setBtoA;
    const Core::Texture*& histA;
    const Core::Texture*& histB;
    const Core::Texture*& momentsA;
    const Core::Texture*& momentsB;
};

[[nodiscard]] bool ShadowReprojectMergeCacheMatches(
    const ShadowReprojectMergeBindingCache& cache,
    const ShadowReprojectMergeTextures& textures
){
    return cache.setAtoB
        && cache.setBtoA
        && cache.histA == textures.histA
        && cache.histB == textures.histB
        && cache.momentsA == textures.momentsA
        && cache.momentsB == textures.momentsB
    ;
}

void ClearShadowReprojectMergeCache(const ShadowReprojectMergeBindingCache& cache){
    cache.setAtoB = nullptr;
    cache.setBtoA = nullptr;
    cache.histA = nullptr;
    cache.histB = nullptr;
    cache.momentsA = nullptr;
    cache.momentsB = nullptr;
}

void StoreShadowReprojectMergeCache(
    const ShadowReprojectMergeBindingCache& cache,
    const ShadowReprojectMergeTextures& textures,
    Core::BindingSetHandle&& setAtoB,
    Core::BindingSetHandle&& setBtoA
){
    cache.setAtoB = Move(setAtoB);
    cache.setBtoA = Move(setBtoA);
    cache.histA = textures.histA;
    cache.histB = textures.histB;
    cache.momentsA = textures.momentsA;
    cache.momentsB = textures.momentsB;
}

[[nodiscard]] bool EnsureShadowReprojectMergeBindingSets(
    Core::Alloc::GlobalArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    DeferredFrameTargets& targets,
    const ShadowReprojectMergeTextures& textures,
    const ShadowReprojectMergeBindingCache& cache,
    const tchar* const failureMessage
){
    if(ShadowReprojectMergeCacheMatches(cache, textures))
        return true;

    Core::BindingSetHandle setAtoB = CreateShadowReprojectMergeBindingSet(
        arena,
        device,
        layout,
        targets,
        textures.histB,
        textures.momentsB
    );
    Core::BindingSetHandle setBtoA = CreateShadowReprojectMergeBindingSet(
        arena,
        device,
        layout,
        targets,
        textures.histA,
        textures.momentsA
    );
    if(!setAtoB || !setBtoA){
        NWB_LOGGER_ERROR(failureMessage);
        ClearShadowReprojectMergeCache(cache);
        return false;
    }

    StoreShadowReprojectMergeCache(cache, textures, Move(setAtoB), Move(setBtoA));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 frameIndex, u32 softGroupsX, u32 softGroupsY){
    // Backend-agnostic soft-shadow denoise + transparent fold, run AFTER whichever backend (SW or HW) wrote the half-res
    // soft opaque trace into shadowSoftHalfA (and synced it to UnorderedAccess): geometry downsample -> per-slot [temporal
    // reproject-merge -> a-trous resolve OVERWRITE] -> the guarded soft COLORED-TRANSPARENT trace+fold -> temporal history
    // swap. It reads only the shared soft/temporal DeferredFrameTargets buffers + the G-buffer, so the same chain denoises
    // HW RayQuery and SW BVH opaque-soft traces. The transparent trace+fold always traces against the SW transparent-only
    // scene BVH via m_swShadowBindingSet; on the HW path this block stages those resources before the transparent trace.
    NWB_ASSERT(targets.bindless.valid());
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

    // The soft transparent trace accesses per-mesh geometry through the descriptor heap, so bind its tables after the
    // ComputeState and before each dispatch. bindCompute touches only sets 8/9; non-bindless builds skip it.
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    const bool heapLive = heap.isInitialized();
    const auto bindPassHeap = [&](const Core::ComputePipelineHandle& pipeline){
        if(heapLive)
            heap.bindCompute(commandList, *pipeline.get());
    };

    // Geometry downsample: fill the half-res packed geometry cache ONCE (slot-independent) before the per-slot
    // resolve loop taps it. Writes the cache UAV; each slot's resolve then reads it as an SRV (the cache is not
    // rewritten, so the UAV->SRV transition happens once and every directional slot shares it).
    {
        // These three reads moved out of the local set, so stage them explicitly before the local CB/UAV set is applied.
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowGeometryDownsampleBindingSet.get());
        commandList.commitBarriers();

        ShadowGeometryDownsamplePushConstants geometryPush;
        geometryPush.width = targets.width;
        geometryPush.height = targets.height;
        geometryPush.halfWidth = softHalfWidth;
        geometryPush.halfHeight = softHalfHeight;
        geometryPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        geometryPush.normalSlot = targets.bindless.gbufferNormal.slot();
        geometryPush.depthSlot = targets.bindless.gbufferDepth.slot();

        Core::ComputeState geometryState;
        geometryState.setPipeline(rayTracingState().m_shadowGeometryDownsamplePipeline.get());
        geometryState.addBindingSet(rayTracingState().m_shadowGeometryDownsampleBindingSet.get());
        commandList.setComputeState(geometryState);
        bindPassHeap(rayTracingState().m_shadowGeometryDownsamplePipeline);
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
    const __hidden_rt_softshadow::ShadowReprojectMergeHeapResources opaqueMergeResources = frontIsA
        ? __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
            targets.shadowSoftHalfA.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistA.slot(), targets.bindless.shadowMomentsA.slot()
        }
        : __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
            targets.shadowSoftHalfA.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowHistB.slot(), targets.bindless.shadowMomentsB.slot()
        }
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

    const auto dispatchReprojectMerge = [&](Core::BindingSet* const bindingSet, const __hidden_rt_softshadow::ShadowReprojectMergeHeapResources& resources){
        NWB_ASSERT(bindingSet);
        NWB_ASSERT(resources.softTrace);
        NWB_ASSERT(resources.historyIn);
        NWB_ASSERT(resources.momentsIn);
        // The six sampled inputs are global-heap descriptors, so stage them explicitly before the local output-UAV
        // binding set. The selected A/B input cannot alias that set's A/B output by the front-state invariant.
        commandList.setTextureState(resources.softTrace, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(resources.historyIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(resources.momentsIn, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.shadowSoftGeometryPrev.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(bindingSet);
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
        mergePush.softTraceSlot = resources.softTraceSlot;
        mergePush.historyInSlot = resources.historyInSlot;
        mergePush.momentsInSlot = resources.momentsInSlot;
        mergePush.geometryCurrSlot = targets.bindless.shadowSoftGeometry.slot();
        mergePush.geometryPrevSlot = targets.bindless.shadowSoftGeometryPrev.slot();
        mergePush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();

        Core::ComputeState mergeState;
        mergeState.setPipeline(rayTracingState().m_shadowReprojectMergePipeline.get());
        mergeState.addBindingSet(bindingSet);
        commandList.setComputeState(mergeState);
        bindPassHeap(rayTracingState().m_shadowReprojectMergePipeline);
        commandList.setPushConstants(&mergePush, sizeof(mergePush));
        commandList.dispatch(softGroupsX, softGroupsY, 1u);
    };

    // Denoise + upsample the whole active slot RANGE in ONE dispatch chain. With temporal on, a single range-wide
    // reproject-merge runs FIRST (accumulating this frame's trace into every slot's history), then the a-trous resolve
    // reads the accumulated history via temporalPrepareSet; else the resolve reads the raw trace directly.
    {
        if(temporalActive){
            // Reproject-merge (half-res, whole range): reads the raw trace (soft-A) + prev history/moments + curr/prev
            // geometry + the full-res world-position G-buffer; writes the accumulated visibility (history-out) +
            // moments for every slot layer. The helper explicitly stages the heap SRVs, binds sets 8/9 after the
            // local output-UAV set, and dispatches the common RGB-safe merge pipeline.
            dispatchReprojectMerge(mergeSet, opaqueMergeResources);
        }

        // Opaque soft resolve: scalar pipeline, its own base sets, OVERWRITE the visibility. The dispatch struct lets
        // ONE routine serve both the opaque (here) and the transparent (below) resolve.
        SoftShadowResolveDispatch opaqueDispatch;
        opaqueDispatch.pipeline = rayTracingState().m_shadowResolvePipeline.get();
        opaqueDispatch.outputHalfA = rayTracingState().m_shadowResolveBindingSetOutputHalfA.get();
        opaqueDispatch.outputHalfB = rayTracingState().m_shadowResolveBindingSetOutputHalfB.get();
        opaqueDispatch.upsample = rayTracingState().m_shadowResolveBindingSetUpsample.get();
        opaqueDispatch.prepareOverride = temporalPrepareSet;
        opaqueDispatch.outputHalfAResources = {
            targets.shadowSoftHalfB.get(), targets.shadowSoftHalfB.get(), targets.shadowMomentsA.get(),
            targets.bindless.shadowSoftHalfB.slot(), targets.bindless.shadowSoftHalfB.slot(), targets.bindless.shadowMomentsA.slot()
        };
        opaqueDispatch.outputHalfBResources = {
            targets.shadowSoftHalfA.get(), targets.shadowSoftHalfA.get(), targets.shadowMomentsA.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowMomentsA.slot()
        };
        opaqueDispatch.upsampleResources = {
            targets.shadowSoftHalfA.get(), targets.shadowSoftHalfA.get(), targets.shadowMomentsA.get(),
            targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowSoftHalfA.slot(), targets.bindless.shadowMomentsA.slot()
        };
        opaqueDispatch.prepareOverrideResources = frontIsA
            ? SoftShadowResolvePassResources{
                targets.shadowHistB.get(), targets.shadowHistB.get(), targets.shadowMomentsB.get(),
                targets.bindless.shadowHistB.slot(), targets.bindless.shadowHistB.slot(), targets.bindless.shadowMomentsB.slot()
            }
            : SoftShadowResolvePassResources{
                targets.shadowHistA.get(), targets.shadowHistA.get(), targets.shadowMomentsA.get(),
                targets.bindless.shadowHistA.slot(), targets.bindless.shadowHistA.slot(), targets.bindless.shadowMomentsA.slot()
            }
        ;
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
        bindPassHeap(rayTracingState().m_swShadowTransparentSoftPipeline);
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
        const __hidden_rt_softshadow::ShadowReprojectMergeHeapResources transparentMergeResources = frontIsA
            ? __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistA.slot(), targets.bindless.transparentMomentsA.slot()
            }
            : __hidden_rt_softshadow::ShadowReprojectMergeHeapResources{
                targets.transparentSoftHalf.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.transparentHistB.slot(), targets.bindless.transparentMomentsB.slot()
            }
        ;
        Core::BindingSet* const transparentPrepareSet = transparentTemporalActive
            ? (frontIsA
                ? rayTracingState().m_transparentResolveBindingSetTemporalHistB.get()
                : rayTracingState().m_transparentResolveBindingSetTemporalHistA.get())
            : nullptr
        ;

        {
            if(transparentTemporalActive){
                // Transparent reproject-merge uses the same RGB-safe pipeline and local A/B output sets as opaque;
                // only its three Array heap inputs change to the transparent target generation.
                dispatchReprojectMerge(transparentMergeSet, transparentMergeResources);
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
            transparentDispatch.outputHalfAResources = {
                targets.transparentSoftHalf.get(), targets.shadowSoftHalfB.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.shadowSoftHalfB.slot(), targets.bindless.transparentMomentsA.slot()
            };
            transparentDispatch.outputHalfBResources = {
                targets.transparentSoftHalf.get(), targets.shadowSoftHalfA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.shadowSoftHalfA.slot(), targets.bindless.transparentMomentsA.slot()
            };
            transparentDispatch.upsampleResources = {
                targets.transparentSoftHalf.get(), targets.shadowSoftHalfA.get(), targets.transparentMomentsA.get(),
                targets.bindless.transparentSoftHalf.slot(), targets.bindless.shadowSoftHalfA.slot(), targets.bindless.transparentMomentsA.slot()
            };
            transparentDispatch.prepareOverrideResources = frontIsA
                ? SoftShadowResolvePassResources{
                    targets.transparentHistB.get(), targets.transparentHistB.get(), targets.transparentMomentsB.get(),
                    targets.bindless.transparentHistB.slot(), targets.bindless.transparentHistB.slot(), targets.bindless.transparentMomentsB.slot()
                }
                : SoftShadowResolvePassResources{
                    targets.transparentHistA.get(), targets.transparentHistA.get(), targets.transparentMomentsA.get(),
                    targets.bindless.transparentHistA.slot(), targets.bindless.transparentHistA.slot(), targets.bindless.transparentMomentsA.slot()
                }
            ;
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
        // Phase 3 (Backend C): the G-buffer inputs now arrive through the global heap, leaving this local
        // descriptor-buffer segment with only the scene CB + geometry-cache UAV. Push constants carry the three
        // SampledImage slots, while the fixed high heap sets preserve Backend-A descriptor-indexing fallback.
        layoutDesc.setUseDescriptorBuffer(true);
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
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
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
        // Phase 3 (Backend C): all seven sampled frame images now use target-generation heap slots. The residual local
        // descriptor-buffer segment is the two UAVs + scene CB; the RGB variant shares it unchanged. The local binding
        // numbers intentionally remain sparse so the established ABI gaps are not renumbered.
        layoutDesc.setUseDescriptorBuffer(true);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RESOLVE_BINDING_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RESOLVE_BINDING_VISIBILITY, 1));
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
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
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
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
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

    // The sampled source role now travels in resolve push constants, so these local sets contain only the changing OUTPUT
    // UAV plus the shared VISIBILITY UAV and scene CB. The source slot mapping remains explicit at dispatch time, preserving
    // the no-SRV/UAV-alias arrangement the former local sets used.
    const auto buildSet = [&](Core::Texture* outputTex, Core::Format::Enum outputFormat, Core::TextureDimension::Enum outputDim) -> Core::BindingSetHandle {
        __hidden_rt_softshadow::SoftShadowResolveBindingSetInputs inputs;
        inputs.output = outputTex;
        inputs.outputFormat = outputFormat;
        inputs.outputDimension = outputDim;
        return __hidden_rt_softshadow::CreateSoftShadowResolveBindingSet(
            arena(),
            *device,
            rayTracingState().m_shadowResolveBindingLayout,
            targets,
            visibilityTarget,
            deferredState().m_sceneShadingBuffer.get(),
            inputs
        );
    };

    // OUTPUT is a half-res Texture2DArray for the ping-pong sets; for the upsample it is UNUSED (the upsample writes the
    // full-res VISIBILITY UAV instead) but must still be a valid binding -- point it at soft-B (a half-res array).
    // The non-temporal sets never sample the MOMENTS SRV (push.momentsValid == 0 on those dispatches), so bind moments-A as
    // an inert dummy. The two temporal variants bind the moments buffer PAIRED with the hist buffer they read as SOFT_HALF
    // (hist-A <-> moments-A, hist-B <-> moments-B), i.e. the merge's accumulated moments for this frame's a-trous.
    Core::BindingSetHandle outputHalfA = buildSet(softATarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray);
    Core::BindingSetHandle outputHalfB = buildSet(softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray);
    Core::BindingSetHandle upsample    = buildSet(softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray);
    // Two TEMPORAL variants: PREPARE reads the accumulated history (hist-A / hist-B) as SOFT_HALF (+ INPUT_COLOR, unused by
    // PREPARE), writes soft-B (out). SOFT_HALF == hist buffer is DISTINCT from the ping-pong output soft-A/soft-B, so no
    // SRV+UAV alias. The dispatch picks the variant matching the merge's history-out buffer (B when frontIsA, else A). Each
    // binds its paired moments buffer as the MOMENTS SRV so the variance-coupled a-trous reads the accumulated moments.
    Core::BindingSetHandle temporalHistA = buildSet(softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray);
    Core::BindingSetHandle temporalHistB = buildSet(softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray);
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

    // Only the changing scratch OUTPUT remains local. The raw/history/moments source images use the same explicit
    // heap-slot mapping as the opaque resolve and are staged at dispatch time.
    const auto buildSet = [&](Core::Texture* outputTex) -> Core::BindingSetHandle {
        __hidden_rt_softshadow::SoftShadowResolveBindingSetInputs inputs;
        inputs.output = outputTex;
        inputs.outputFormat = targets.shadowSoftFormat;
        inputs.outputDimension = Core::TextureDimension::Texture2DArray;
        return __hidden_rt_softshadow::CreateSoftShadowResolveBindingSet(
            arena(),
            *device,
            rayTracingState().m_shadowResolveBindingLayout,
            targets,
            visibilityTarget,
            deferredState().m_sceneShadingBuffer.get(),
            inputs
        );
    };

    // outputHalfB: PREPARE (SOFT_HALF = the raw colored trace -> soft-B) + even wavelets (INPUT_COLOR = soft-A -> soft-B).
    // outputHalfA: odd wavelets (SOFT_HALF bound-unused; INPUT_COLOR = soft-B -> soft-A). SOFT_HALF -> the raw trace here
    //              (an SRV distinct from the soft-A OUTPUT), NOT soft-A, so no SRV+UAV alias of the scratch.
    // upsample: reads INPUT_COLOR = soft-A (the final odd-count result), folds the VISIBILITY (OUTPUT = soft-B, unused).
    Core::BindingSetHandle outputHalfA = buildSet(scratchATarget);
    Core::BindingSetHandle outputHalfB = buildSet(scratchBTarget);
    Core::BindingSetHandle upsample    = buildSet(scratchBTarget);
    // Temporal variants: PREPARE reads the accumulated transparent history (hist-A / hist-B) as SOFT_HALF -> soft-B; the
    // wavelet ping-pong + INPUT_COLOR are still soft-A/soft-B. Each binds its paired transparent moments as the MOMENTS SRV.
    Core::BindingSetHandle temporalHistA = buildSet(scratchBTarget);
    Core::BindingSetHandle temporalHistB = buildSet(scratchBTarget);
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

    NWB_ASSERT(targets.bindless.valid());
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    const u32 foldValue = static_cast<u32>(dispatch.fold);
    const auto runPass = [&](Core::BindingSet* const bindingSet, const SoftShadowResolvePassResources& resources, const u32 stepWidth, const ShadowResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        NWB_ASSERT(resources.softHalfTexture);
        NWB_ASSERT(resources.inputColorTexture);
        NWB_ASSERT(resources.momentsTexture);
        // The seven reads now arrive through the heap rather than bindingSet, so retain the old automatic-state behavior
        // explicitly. The selected role never aliases its local OUTPUT UAV (the same invariant the former sets encoded).
        commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(resources.softHalfTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(resources.momentsTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
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
        resolvePush.geometrySlot = bindless.shadowSoftGeometry.slot();
        resolvePush.depthSlot = bindless.gbufferDepth.slot();
        resolvePush.worldPositionSlot = bindless.gbufferWorldPosition.slot();
        resolvePush.normalSlot = bindless.gbufferNormal.slot();
        resolvePush.softHalfSlot = resources.softHalf;
        resolvePush.inputColorSlot = resources.inputColor;
        resolvePush.momentsSlot = resources.moments;

        Core::ComputeState computeState;
        computeState.setPipeline(dispatch.pipeline);
        computeState.addBindingSet(bindingSet);
        commandList.setComputeState(computeState);
        if(heap.isInitialized())
            heap.bindCompute(commandList, *dispatch.pipeline);
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    // PREPARE: copy the half-res traced visibility (SOFT_HALF) into soft-B. The base set (SOFT_HALF == raw trace, out=soft-B,
    // in=raw) never read-write aliases the scratch; the temporal override (SOFT_HALF == the merge's accumulated history buffer,
    // still out=soft-B) reads the accumulated visibility instead. Either way the result lives in soft-B for the wavelets.
    Core::BindingSet* const prepareSet = dispatch.prepareOverride ? dispatch.prepareOverride : dispatch.outputHalfB;
    const SoftShadowResolvePassResources& prepareResources = dispatch.prepareOverride ? dispatch.prepareOverrideResources : dispatch.outputHalfBResources;
    runPass(prepareSet, prepareResources, 1u, ShadowResolveStage::Prepare, halfGroupsX, halfGroupsY);

    // Half-res a-trous wavelet passes at a doubling dilation, starting from soft-B. Each pass writes the buffer NOT
    // holding its input (outputHalfA reads soft-B writes soft-A; outputHalfB reads soft-A writes soft-B). srcIsHalfB
    // tracks where the latest result lives; PREPARE left it in soft-B so it starts true.
    // The per-signal pass count (opaque = NWB_SHADOW_RESOLVE_PASS_COUNT 1, transparent = NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
    // 1). Both must be ODD so the final result lands in soft-A (see the assert below); both literals are compile-time odd.
    static_assert((NWB_SHADOW_RESOLVE_PASS_COUNT % 2) == 1, "opaque resolve pass count must be ODD (final result must land in soft-A for the upsample)");
    static_assert((NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT % 2) == 1, "transparent resolve pass count must be ODD (final result must land in soft-A for the upsample)");
    bool srcIsHalfB = true;
    for(u32 pass = 0u; pass < dispatch.waveletPassCount; ++pass){
        Core::BindingSet* const bindingSet = srcIsHalfB ? dispatch.outputHalfA : dispatch.outputHalfB;
        const SoftShadowResolvePassResources& resources = srcIsHalfB ? dispatch.outputHalfAResources : dispatch.outputHalfBResources;
        runPass(bindingSet, resources, 1u << pass, ShadowResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        srcIsHalfB = !srcIsHalfB;
    }

    // UPSAMPLE (full-res): edge-aware bilateral resample of the FINAL half-res visibility into the full-res visibility
    // slot. The final wavelet result lives in soft-A when waveletPassCount is ODD (1 opaque / 1 transparent) -- srcIsHalfB is
    // now false. Both upsample sets read soft-A (INPUT_COLOR) by construction; assert the parity so a pass-count change is caught.
    NWB_ASSERT(!srcIsHalfB); // waveletPassCount must be odd for the final result to land in soft-A (the upsample's input)
    runPass(dispatch.upsample, dispatch.upsampleResources, 1u, ShadowResolveStage::Upsample, fullGroupsX, fullGroupsY);
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
        // The six sampled inputs are global-heap reads selected by push constants. Retain the sparse local UAV bindings
        // (6/7) rather than renumbering the ABI; Backend C's descriptor-buffer block now contains only those two writes.
        // Backend A continues through the identical heap-backed shader contract using descriptor indexing.
        layoutDesc.setUseDescriptorBuffer(true);
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
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
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

    // Two front/back local sets retain only the output UAVs. The incoming history, raw trace, geometry caches, and
    // world-position now arrive through immutable target-generation heap slots, but the A/B output roles still need
    // separate local sets so their descriptor-buffer blocks remain immutable.
    auto* device = graphics().getDevice();
    const __hidden_rt_softshadow::ShadowReprojectMergeTextures textures{
        targets.shadowHistA.get(),
        targets.shadowHistB.get(),
        targets.shadowMomentsA.get(),
        targets.shadowMomentsB.get()
    };
    const __hidden_rt_softshadow::ShadowReprojectMergeBindingCache cache{
        rayTracingState().m_shadowReprojectMergeBindingSetAtoB,
        rayTracingState().m_shadowReprojectMergeBindingSetBtoA,
        rayTracingState().m_shadowReprojectMergeHistA,
        rayTracingState().m_shadowReprojectMergeHistB,
        rayTracingState().m_shadowReprojectMergeMomentsA,
        rayTracingState().m_shadowReprojectMergeMomentsB
    };
    return __hidden_rt_softshadow::EnsureShadowReprojectMergeBindingSets(
        arena(),
        *device,
        rayTracingState().m_shadowReprojectMergeBindingLayout,
        targets,
        textures,
        cache,
        NWB_TEXT("RendererSystem: failed to create shadow reproject-merge binding sets")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowTransparentReprojectMergeBindingSet(DeferredFrameTargets& targets){
    // The two front/back TRANSPARENT reproject-merge sets mirror the opaque output-UAV sets. Their raw trace,
    // history/moments inputs, shared geometry caches, and full-res world-position are heap reads selected per dispatch.
    NWB_ASSERT(rayTracingState().m_shadowReprojectMergeBindingLayout);
    NWB_ASSERT(targets.transparentSoftHalf);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.shadowSoftGeometryPrev);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.transparentHistA);
    NWB_ASSERT(targets.transparentHistB);
    NWB_ASSERT(targets.transparentMomentsA);
    NWB_ASSERT(targets.transparentMomentsB);

    auto* device = graphics().getDevice();
    const __hidden_rt_softshadow::ShadowReprojectMergeTextures textures{
        targets.transparentHistA.get(),
        targets.transparentHistB.get(),
        targets.transparentMomentsA.get(),
        targets.transparentMomentsB.get()
    };
    const __hidden_rt_softshadow::ShadowReprojectMergeBindingCache cache{
        rayTracingState().m_transparentReprojectMergeBindingSetAtoB,
        rayTracingState().m_transparentReprojectMergeBindingSetBtoA,
        rayTracingState().m_transparentReprojectMergeHistA,
        rayTracingState().m_transparentReprojectMergeHistB,
        rayTracingState().m_transparentReprojectMergeMomentsA,
        rayTracingState().m_transparentReprojectMergeMomentsB
    };
    return __hidden_rt_softshadow::EnsureShadowReprojectMergeBindingSets(
        arena(),
        *device,
        rayTracingState().m_shadowReprojectMergeBindingLayout,
        targets,
        textures,
        cache,
        NWB_TEXT("RendererSystem: failed to create soft transparent shadow reproject-merge binding sets")
    );
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
    //    must physically become next frame's prev. The handle swap changes which texture each heap role names without
    //    rewriting an in-flight descriptor; only the geometry-downsample output set tracks the changing current target.
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
    // The descriptors are immutable per physical target generation. Swap their handles with the two role handles rather
    // than rewriting a live heap slot, so next frame's "current" geometry role still names the texture just selected above.
    Swap(targets.bindless.shadowSoftGeometry, targets.bindless.shadowSoftGeometryPrev);
    rayTracingState().m_softShadowHistoryFrontIsA ^= 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

