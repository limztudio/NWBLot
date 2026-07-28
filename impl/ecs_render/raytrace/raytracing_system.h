// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>

#include <core/alloc/scratch.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// UPSAMPLE fold mode (mirrors shadow_resolve_cs.slang's pushConstants.upsampleFold): OVERWRITE the full-res visibility
// (soft OPAQUE) vs MULTIPLY the denoised colored transmittance onto it (soft TRANSPARENT fold).
namespace SoftShadowUpsampleFold{
    enum Enum : u32{
        Overwrite = 0u,
        Multiply = 1u,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererRayTracingSystem(RendererSystem& renderer);


public:
    void logCapabilityOnce();

    [[nodiscard]] bool buildPendingMeshBlas(Core::CommandList& commandList);
    [[nodiscard]] bool buildPendingMeshSwBvh(Core::CommandList& commandList);
    [[nodiscard]] bool buildSceneTlas(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool buildSceneSwBvh(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool prepareCausticEmissionTargets(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena);
    // Retire caustic-owned emission-target and material-context heap descriptors before invalidation releases their
    // backing buffers.
    void releaseCausticEmissionTargetHeapHandle();
    [[nodiscard]] bool createShadowVisibilityTarget(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCausticTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool prepareShadowVisibilityResources(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Alloc::ScratchArena& scratchArena, bool& outBackendReady);
    // The five trace material-context buffers are global heap descriptors. This uploads their current slot numbers
    // into the small selector payload shared by RT shadow, surfel GI, and caustic producers.
    [[nodiscard]] bool uploadRayTraceMaterialContextSlots(Core::CommandList& commandList);
    // Retire the five heap descriptors before resource invalidation releases their backing buffers.
    void releaseRayTraceMaterialContextHeapHandles();
    // Retire the shared SW-BVH scratch descriptors before resource invalidation releases their backing buffers.
    void releaseSwBvhScratchHeapHandles();
    // Retire the persistent surfel descriptor generations before resource invalidation releases their backing buffers.
    void releaseSurfelGiHeapHandles();
    // Normalizes the G-buffer and trace inputs shared by the independent shadow, caustics, surfel-GI, and AVBOIT
    // packets. The prelude records these transitions once before all four packets import the resulting state snapshot.
    void normalizePostGbufferPacketResources(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // Software-BVH shadow traversal. multiplyOntoOpaque=false: standalone no-RT path (opaque + transparent, overwrite).
    // multiplyOntoOpaque=true: hybrid path on RT hardware -- traces the TRANSPARENT-only scene BVH and multiplies its colored transmittance onto the HW opaque binary mask already in the visibility buffer.
    [[nodiscard]] bool renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets, bool multiplyOntoOpaque = false);
    [[nodiscard]] bool prepareGpuBvhCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasCausticWork()const noexcept;
    // Hardware ray-traced caustic photon producer -- the byte-parallel sibling of the SW producer above, run on
    // the HW branch (RayTracingAccelStruct supported). Reuses the TLAS, heap-selected material/geometry context, and
    // shared R32_UINT accumulator + resolve; its closest hit reconstructs the surface through material-record slots.
    [[nodiscard]] bool prepareHwCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasHwCausticWork()const noexcept;
    // Surfel GI: the feature gate + prep + render hooks. hasSurfelWork returns m_surfelEnabled (set in
    // prepareShadowVisibilityResources once the SW scene BVH is resident). prepareSurfelResources creates the
    // persistent pool/hash/counter/params buffers + pipelines, clears them on (re)creation, and uploads the params
    // CB. renderSurfelGi consumes the prepared heap slots and records the complete surfel update and resolve sequence
    // (the SW trace reuses the SW scene BVH).
    [[nodiscard]] bool hasSurfelWork()const noexcept;
    [[nodiscard]] bool prepareSurfelResources(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // The first surfel-pool clear is recorded into the shadow-preparation list. Keep the pool marked dirty until that
    // list is actually submitted, so a rejected preparation packet retries the initialization rather than tracing
    // uninitialized persistent buffers.
    void finalizeSurfelResourceInitialization();
    void discardSurfelResourceInitialization();
    [[nodiscard]] bool renderSurfelGi(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // Lazily create the persistent surfel buffers (pool / cell-head / counter / params CB) and their per-pass
    // pipelines. The buffers live on RendererRayTracingState so a window resize does not reset convergence.
    [[nodiscard]] bool ensureSurfelResources();
    // The surfel pass pipelines use only their common push range plus the global heap resource/sampler layouts.
    [[nodiscard]] bool ensureSurfelSpawnPipeline();
    // Age-free recycling: one thread per pool slot; frees surfels unseen for maxAge frames + pushes their ids onto
    // the free-list. Reads only the persistent buffers, so pipeline + set are built once (like hash-build).
    [[nodiscard]] bool ensureSurfelAgeFreePipeline();
    [[nodiscard]] bool ensureSurfelHashBuildPipeline();
    [[nodiscard]] bool ensureSurfelTracePipeline();
    // HW-RayQuery trace twin: the same surfel trace over the TLAS (inline RayQuery) instead of the SW BVH. Selected
    // by m_surfelUseHwTrace on the HW-shadow branch; gated on RayQuery + accel-struct support (like ensureShadowPipeline).
    [[nodiscard]] bool ensureSurfelTraceHwPipeline();
    // The resolve pass: a COMPUTE gather-once-per-pixel into the screen-space surfelIrradiance texture the deferred
    // lighting samples (keeps the RW pool off the pixel shader -> no frames-in-flight pool race). Its field/input/
    // output resources are heap-selected by the common push block.
    [[nodiscard]] bool ensureSurfelResolvePipeline();
    // Half-res upsample: reconstructs the full-res surfelIrradiance from heap-selected half irradiance plus G-buffer
    // normal/world-position and a heap-selected storage output.
    [[nodiscard]] bool ensureSurfelUpsamplePipeline();
    // Trace dispatchIndirect: a 1-thread build-args pass writes ceil(BUMP_TOP/divisor) into the trace's indirect-args
    // buffer, so the trace dispatches per LIVE surfel. It uses the same heap slots as the other surfel passes.
    [[nodiscard]] bool ensureSurfelTraceBuildArgsPipeline();
    // True when the prepare built the hybrid transparent-shadow software resources this frame (RT hardware + the scene
    // has a transparent occluder). Used as the !softTransparentShadowReady fallback that folds colored transparent shadow
    // onto the HW opaque pass with renderGpuBvhShadowVisibility(..., multiplyOntoOpaque=true).
    [[nodiscard]] bool hybridTransparentShadowReady()const noexcept;
    // True when the prepare built the soft transparent trace+fold resources this frame (the HW/SW opaque soft path is
    // ready AND the transparent SW scene BVH + RGB resolve/merge sets built). When true, the colored TRANSPARENT shadow
    // is traced + denoised + multiplied onto the soft-opaque visibility inside renderShadowVisibility's soft chain.
    [[nodiscard]] bool softTransparentShadowReady()const noexcept;
    // Soft-shadow recording cannot swap the target-generation handles while the sibling caustics and surfel-GI packets
    // validate the shared bindless bundle. Finalize the deferred temporal swap only after the ordered submission
    // succeeds, or discard it when packet recording/submission is abandoned.
    void finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets);
    void discardSoftShadowTemporalHistory();


private:
    [[nodiscard]] bool buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources);
    // Allocates the software-BVH pipelines, shared scratch, and per-mesh storage before the per-frame command-list
    // update records its build/refit dispatches. Runtime meshes are prepared every frame because their resource set
    // is selected from the current runtime-mesh cache entry; static meshes remain dirty until their first build.
    [[nodiscard]] bool preparePendingMeshSwBvhResources();
    [[nodiscard]] bool ensureShadowPipeline();
    // Hardware (RayQuery) SOFT OPAQUE half-res trace. It reuses the push-only trace layout; heap slots select soft-A
    // as its storage output, so the HW opaque shadow feeds the same denoise chain as the SW path.
    [[nodiscard]] bool ensureShadowSoftPipeline();
    // Shared hardware opaque-shadow trace layout. Every resource, including the TLAS, is heap-selected; this local
    // layout contains only the push range.
    void appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const;
    // ensureSwShadowPipeline creates the shared software-shadow binding layout + persistent adaptive buffers once, then
    // creates one named compute pipeline per decomposed pass via ensureSwShadowPassPipeline.
    [[nodiscard]] bool ensureSwShadowPipeline();
    [[nodiscard]] bool ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const char* debugLabel);
    // Soft opaque shadow: the half-res geometry downsample pre-pass + the a-trous wavelet resolve/upsample.
    // dispatchSoftShadowResolve runs the denoise chain for a contiguous RANGE of shadow slots in one dispatch, overwriting
    // each of those slots' full-res visibility with the denoised soft shadow.
    [[nodiscard]] bool ensureSoftShadowResolvePipeline();
    [[nodiscard]] bool ensureShadowGeometryDownsamplePipeline();
    // Soft colored-transparent shadow: RGB a-trous resolve pipeline sharing the same push-only layout.
    [[nodiscard]] bool ensureSoftTransparentResolvePipeline();
    // The sampled Texture2DArray inputs and writable storage output for one resolve role. Keep backing objects next to
    // their heap slots so explicit state transitions cannot drift from the pushed selectors.
    struct SoftShadowResolvePassResources{
        Core::Texture* softHalfTexture = nullptr;
        Core::Texture* inputColorTexture = nullptr;
        Core::Texture* momentsTexture = nullptr;
        Core::Texture* outputTexture = nullptr;
        u32 softHalf = 0u;
        u32 inputColor = 0u;
        u32 moments = 0u;
        u32 outputStorage = 0u;
    };
    // Heap-only resolve dispatch description. The optional temporal override swaps PREPARE to accumulated history;
    // no resource is represented by a pipeline-local descriptor object.
    struct SoftShadowResolveDispatch{
        Core::ComputePipeline* pipeline = nullptr;
        SoftShadowResolvePassResources outputHalfAResources;
        SoftShadowResolvePassResources outputHalfBResources;
        SoftShadowResolvePassResources upsampleResources;
        SoftShadowResolvePassResources prepareOverrideResources;
        Core::Texture* visibilityTexture = nullptr;
        u32 visibilityStorage = 0u;
        u32 sceneShading = 0u;
        bool usePrepareOverride = false;
        SoftShadowUpsampleFold::Enum fold = SoftShadowUpsampleFold::Overwrite;
        // A-trous wavelet pass count for this signal: opaque = NWB_SHADOW_RESOLVE_PASS_COUNT (1), the cheaper smooth transparent
        // tint = NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT (1). MUST be ODD -- the ping-pong leaves the final result in soft-A
        // (the fixed upsample set's input) only for an odd count; dispatchSoftShadowResolve asserts the parity. Defaulted to 1
        // (= NWB_SHADOW_RESOLVE_PASS_COUNT, which this header does not include) and set explicitly at each build site.
        u32 waveletPassCount = 1u;
    };
    // dispatchSoftShadowResolve runs the a-trous PREPARE -> N wavelet passes -> upsample for a CONTIGUOUS RANGE of shadow
    // slots [slotStart, slotStart+slotCount), against heap slots + pipeline + fold in `dispatch`. The resolve shader loops the
    // range per pixel, so one dispatch covers every active Texture2DArray layer (each layer independent), reducing
    // dispatch and barrier count. See SoftShadowResolveDispatch.
    void dispatchSoftShadowResolve(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 slotStart, u32 slotCount, const SoftShadowResolveDispatch& dispatch);
    // Backend-agnostic soft-shadow denoise + transparent fold, run AFTER whichever backend (SW BVH or HW RayQuery) wrote
    // the half-res soft opaque trace into shadowSoftHalfA (and synced it to UnorderedAccess): geometry downsample ->
    // per-slot [temporal reproject-merge -> a-trous resolve OVERWRITE] -> the guarded soft colored-transparent trace+fold
    // -> deferred temporal-history finalization. Reads only the shared soft/temporal buffers + the G-buffer, so the same
    // chain denoises both backends. softGroupsX/Y are the half-res dispatch grid; frameIndex seeds the trace jitter.
    void dispatchSoftShadowDenoiseAndTransparentFold(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 frameIndex, u32 softGroupsX, u32 softGroupsY);
    // Soft opaque shadow TEMPORAL accumulation: the reproject-merge pass
    // inserted per slot between the soft trace and the a-trous resolve. The pipeline is push-only and heap slots select
    // every sampled and writable image. swapSoftShadowTemporalHistory stashes this frame's worldToClip for next-frame reprojection +
    // ping-pongs the history / moments / geometry buffers at frame end (guarded on m_softShadowTemporalReady).
    [[nodiscard]] bool ensureShadowReprojectMergePipeline();
    void swapSoftShadowTemporalHistory(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureSwCausticPipeline();
    [[nodiscard]] bool ensureCausticMaterialContextSlotsHeapHandle();
    [[nodiscard]] bool ensureCausticResolvePipeline();
    // Geometry downsample pre-pass: fills the half-res geometry cache (world + receiver validity) the resolve reads.
    [[nodiscard]] bool ensureCausticGeometryDownsamplePipeline();
    // Resolve/temporal resources shared by the software-BVH and hardware-ray-tracing caustic producers.
    [[nodiscard]] bool causticResolveResourcesReady(const DeferredFrameTargets& targets, f32 temporalDecay)const;
    // Accumulator decay pre-pass (splat-space temporal EMA): multiplies the resident R32_UINT accumulator by the temporal
    // decay factor before the producer splats this frame's photons (only used when temporal is enabled, decay > 0).
    [[nodiscard]] bool ensureCausticAccumulatorDecayPipeline();
    // Returns the fixed splat-space EMA decay factor (m_causticTemporalDecay = 0.85). <=0 would mean temporal off.
    [[nodiscard]] f32 causticTemporalDecay();
    // Splat-space temporal step, run at the top of each caustic producer (SW/HW) when temporal is enabled: on the first
    // enabled frame (or after a resize) the accumulator holds no history so it is CLEARED to 0; every later frame the
    // decay pass multiplies it by decayFactor in place (accum_N = decay*accum_{N-1}). Leaves the accumulator in
    // UnorderedAccess for the producer's atomic-adds.
    void prepareCausticAccumulatorForSplat(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor);
    // Runs the N-pass edge-avoiding a-trous wavelet resolve (shared by the SW + HW caustic paths): converts the splat
    // accumulator to denoised irradiance, ping-ponging the irradiance + scratch buffers so the final pass lands in
    // irradiance. The accumulator must already hold this frame's splat (producer dispatched). Assumes the resolve
    // pipeline is ready and every target-generation heap slot is live.
    void dispatchCausticResolve(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureCausticRtPipeline();
    [[nodiscard]] bool ensureBvhSortPipeline();
    [[nodiscard]] bool ensureBvhSortBuffers(usize paddedCount);
    [[nodiscard]] bool bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount);
    [[nodiscard]] bool ensureBvhBuildPipeline();
    [[nodiscard]] bool ensureBvhVisitCounterBuffer(usize primitiveCount);
    [[nodiscard]] bool createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool ensureMeshSwBvhResources(u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool meshSwBvhResourcesReady(const Core::BufferHandle& nodeBuffer, const Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle);
    [[nodiscard]] bool buildMeshSwBvh(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool buildMeshSwBvhPrepared(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle);
    [[nodiscard]] bool refitMeshSwBvh(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle& nodeHeapHandle, Core::GpuDescriptorHandle& parentHeapHandle);
    [[nodiscard]] bool refitMeshSwBvhPrepared(Core::CommandList& commandList, u32 positionHeapSlot, u32 triangleIndexHeapSlot, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::GpuDescriptorHandle nodeHeapHandle, Core::GpuDescriptorHandle parentHeapHandle);
    [[nodiscard]] bool updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources);
    [[nodiscard]] bool ensureSceneBvhBuffers(u32 instanceCount);
    [[nodiscard]] bool ensureRayTraceMaterialContextSlotsBuffer();
    [[nodiscard]] bool ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    [[nodiscard]] bool replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle);
    // Stages the shared heap-selected software-BVH traversal inputs; callers own pass-specific resources and barriers.
    void transitionSwShadowTraversalResources(Core::CommandList& commandList);
    [[nodiscard]] bool ensureCausticEmissionTargetBuffer(usize targetCount);
    [[nodiscard]] bool ensureShadowInstanceMaterialBuffer(usize instanceCount);
    [[nodiscard]] bool uploadShadowMaterialContextBuffers(
        Core::CommandList& commandList,
        const InstanceGpuDataVector& instanceData,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool ensureShadowInstanceContextBuffer(usize instanceCount);
    [[nodiscard]] bool ensureShadowMaterialTypedBuffer(usize byteCount);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

