// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"

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
    [[nodiscard]] bool createShadowVisibilityTarget(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCausticTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool prepareShadowVisibilityResources(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Alloc::ScratchArena& scratchArena, bool& outBackendReady);
    [[nodiscard]] bool renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // Software-BVH shadow traversal. multiplyOntoOpaque=false: standalone no-RT path (opaque + transparent, overwrite).
    // multiplyOntoOpaque=true: hybrid path on RT hardware -- traces the TRANSPARENT-only scene BVH and multiplies its colored transmittance onto the HW opaque binary mask already in the visibility buffer.
    [[nodiscard]] bool renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets, bool multiplyOntoOpaque = false);
    [[nodiscard]] bool prepareGpuBvhCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasCausticWork()const noexcept;
    // Hardware ray-traced caustic photon producer (P4) -- the byte-parallel sibling of the SW producer above, run on
    // the HW branch (RayTracingAccelStruct supported). Reuses the TLAS + the shadow material/geometry buffers + the
    // shared R32_UINT accumulator + resolve; adds only a per-mesh position SRV array for the geometric face normal.
    [[nodiscard]] bool prepareHwCausticResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool hasHwCausticWork()const noexcept;
    // Surfel GI: the feature gate + prep + render hooks. hasSurfelWork returns m_surfelEnabled (set in
    // prepareShadowVisibilityResources once the SW scene BVH is resident). prepareSurfelResources creates the
    // persistent pool/hash/counter/params buffers + pipelines, clears them on (re)creation, and uploads the params
    // CB. renderSurfelGi runs the spawn -> hash-build -> trace passes (the SW trace reuses the SW scene BVH). See
    // .helper/surfel_gi_plan.md.
    [[nodiscard]] bool hasSurfelWork()const noexcept;
    [[nodiscard]] bool prepareSurfelResources(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderSurfelGi(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // Lazily create the persistent surfel buffers (pool / cell-head / counter / params CB) + the three pass
    // pipelines. The buffers live on RendererRayTracingState so a window resize does not reset convergence.
    [[nodiscard]] bool ensureSurfelResources();
    // The three pass pipelines. The spawn + hash-build read only the surfel buffers + G-buffer; the trace reuses the
    // SW scene BVH (slots 0-10) exactly like the SW shadow/caustic trace. Distinct layouts (the cell-head is an SRV at
    // spawn, a UAV at hash-build).
    [[nodiscard]] bool ensureSurfelSpawnPipeline();
    [[nodiscard]] bool ensureSurfelHashBuildPipeline();
    [[nodiscard]] bool ensureSurfelTracePipeline();
    // The spawn set (surfel buffers + G-buffer world-position/normal; rebuilt on G-buffer change) + the hash-build set
    // (surfel buffers only; built once) + the trace set (SW scene BVH + per-mesh arrays + surfel constants/pool;
    // rebuilt when the scene BVH / distinct-mesh count changes, mirroring the SW shadow set).
    [[nodiscard]] bool ensureSurfelSpawnBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureSurfelHashBuildBindingSet();
    [[nodiscard]] bool ensureSurfelTraceBindingSet();
    // The resolve pass: a COMPUTE gather-once-per-pixel into the screen-space surfelIrradiance texture the deferred
    // lighting samples (keeps the RW pool off the pixel shader -> no frames-in-flight pool race). Pipeline + its set
    // (surfel constants/pool SRV/cell-head SRV + G-buffer world-position/normal + surfelIrradiance UAV; rebuilt on a
    // target change).
    [[nodiscard]] bool ensureSurfelResolvePipeline();
    [[nodiscard]] bool ensureSurfelResolveBindingSet(DeferredFrameTargets& targets);
    // True when the prepare built the hybrid transparent-shadow software resources this frame (RT hardware + the scene
    // has a transparent occluder). The render then runs renderGpuBvhShadowVisibility(..., multiplyOntoOpaque=true) after
    // the HW opaque pass, folding the colored transparent shadow onto the binary opaque mask -- ONLY as the
    // !softTransparentShadowReady fallback (Phase 2 moved the colored fold inside renderShadowVisibility's soft chain).
    [[nodiscard]] bool hybridTransparentShadowReady()const noexcept;
    // True when the prepare built the soft transparent trace+fold resources this frame (the HW/SW opaque soft path is
    // ready AND the transparent SW scene BVH + RGB resolve/merge sets built). When true, the colored TRANSPARENT shadow
    // is traced + denoised + multiplied onto the soft-opaque visibility INSIDE renderShadowVisibility's soft chain, so
    // the old hybrid multiply (renderGpuBvhShadowVisibility, multiplyOntoOpaque=true) is skipped as redundant.
    [[nodiscard]] bool softTransparentShadowReady()const noexcept;


private:
    [[nodiscard]] bool buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources);
    [[nodiscard]] bool ensureShadowPipeline();
    [[nodiscard]] bool ensureShadowBindingSet(DeferredFrameTargets& targets);
    // Hardware (RayQuery) SOFT OPAQUE half-res trace: clones of the two above, but the pipeline loads
    // s_RayQuerySoftShaderName (REUSING the shared m_shadowBindingLayout -- identical trace context) and the binding set
    // binds shadowSoftHalfA (half-res soft-A) as the visibility-output UAV instead of the full-res shadowVisibility, so
    // the HW opaque shadow feeds the SAME half-res -> temporal -> a-trous -> upsample denoise chain as the SW path.
    [[nodiscard]] bool ensureShadowSoftPipeline();
    [[nodiscard]] bool ensureShadowSoftBindingSet(DeferredFrameTargets& targets);
    // Shared hardware opaque-shadow-trace bindings (TLAS + G-buffer + scene/light + per-mesh geometry + material
    // context), appended by the trace pipeline/set. (Factored out for clarity; visibilityTarget is the full-res output.)
    void appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const;
    void appendShadowTraceBindingSet(Core::BindingSetDesc& desc, DeferredFrameTargets& targets, Core::Texture* visibilityTarget)const;
    // ensureSwShadowPipeline creates the shared software-shadow binding layout + persistent adaptive buffers once, then
    // creates one named compute pipeline per decomposed pass via ensureSwShadowPassPipeline.
    [[nodiscard]] bool ensureSwShadowPipeline();
    [[nodiscard]] bool ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const char* debugLabel);
    [[nodiscard]] bool ensureSwShadowBindingSet(DeferredFrameTargets& targets);
    // Soft opaque shadow: the half-res geometry downsample pre-pass + the a-trous wavelet resolve/upsample.
    // dispatchSoftShadowResolve runs the denoise chain for a contiguous RANGE of shadow slots in one dispatch, overwriting
    // each of those slots' full-res visibility with the denoised soft shadow.
    [[nodiscard]] bool ensureSoftShadowResolvePipeline();
    [[nodiscard]] bool ensureSoftShadowResolveBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureShadowGeometryDownsamplePipeline();
    [[nodiscard]] bool ensureShadowGeometryDownsampleBindingSet(DeferredFrameTargets& targets);
    // Soft colored-transparent shadow: the RGB a-trous resolve pipeline (the SAME shadow_resolve source cooked
    // with NWB_SHADOW_RESOLVE_CHANNELS=3, sharing the resolve binding LAYOUT) + the five parallel transparent resolve
    // binding sets (over transparentSoftHalf/transparentHist as SOFT_HALF, the SAME half-res ping-pong scratch as OUTPUT,
    // and the SAME full-res shadowVisibility as the fold-multiply VISIBILITY target). The transparent merge REUSES the
    // opaque merge pipeline; only its two front/back binding sets are parallel (built over the transparent hist/moments).
    [[nodiscard]] bool ensureSoftTransparentResolvePipeline();
    [[nodiscard]] bool ensureSoftTransparentResolveBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureShadowTransparentReprojectMergeBindingSet(DeferredFrameTargets& targets);
    // The set of binding sets + pipeline + fold mode a dispatchSoftShadowResolve call runs against, so ONE dispatch routine
    // serves BOTH the opaque (scalar pipeline, its own base sets, Overwrite fold) and the transparent (RGB pipeline, its own
    // base sets, Multiply fold) resolve. The prepareOverride (temporal) still swaps the PREPARE input to the accumulated
    // history; nullptr keeps the raw-trace path AND drives momentsValid=0 (the spatial-variance fallback).
    struct SoftShadowResolveDispatch{
        Core::ComputePipeline* pipeline = nullptr;
        Core::BindingSet* outputHalfA = nullptr; // wavelet ping-pong: reads scratch-B, writes scratch-A (odd passes)
        Core::BindingSet* outputHalfB = nullptr; // PREPARE + even wavelet passes: reads scratch-A, writes scratch-B
        Core::BindingSet* upsample = nullptr;    // final: reads scratch-A, writes/folds the full-res visibility
        Core::BindingSet* prepareOverride = nullptr; // temporal: PREPARE reads the accumulated history instead of the raw trace
        SoftShadowUpsampleFold::Enum fold = SoftShadowUpsampleFold::Overwrite;
        // A-trous wavelet pass count for this signal: opaque = NWB_SHADOW_RESOLVE_PASS_COUNT (5), the cheaper smooth transparent
        // tint = NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT (3). MUST be ODD -- the ping-pong leaves the final result in soft-A
        // (the fixed upsample set's input) only for an odd count; dispatchSoftShadowResolve asserts the parity. Defaulted to 5
        // (= NWB_SHADOW_RESOLVE_PASS_COUNT, which this header does not include) and set explicitly at each build site.
        u32 waveletPassCount = 5u;
    };
    // dispatchSoftShadowResolve runs the a-trous PREPARE -> N wavelet passes -> upsample for a CONTIGUOUS RANGE of shadow
    // slots [slotStart, slotStart+slotCount), against the sets + pipeline + fold in `dispatch`. The resolve shader loops the
    // range per pixel, so ONE dispatch covers every active slot layer of the Texture2DArray targets (each layer independent):
    // this collapses the former per-slot dispatch chain into a single issue, cutting the dispatch/barrier count. See
    // SoftShadowResolveDispatch.
    void dispatchSoftShadowResolve(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 slotStart, u32 slotCount, const SoftShadowResolveDispatch& dispatch);
    // Backend-agnostic soft-shadow denoise + transparent fold, run AFTER whichever backend (SW BVH or HW RayQuery) wrote
    // the half-res soft opaque trace into shadowSoftHalfA (and synced it to UnorderedAccess): geometry downsample ->
    // per-slot [temporal reproject-merge -> a-trous resolve OVERWRITE] -> the guarded soft colored-transparent trace+fold
    // (SW-only in Phase 1) -> temporal history swap. Reads ONLY the shared soft/temporal buffers + the G-buffer, so the
    // SAME chain denoises both backends. softGroupsX/Y are the half-res dispatch grid; frameIndex seeds the trace jitter.
    void dispatchSoftShadowDenoiseAndTransparentFold(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 frameIndex, u32 softGroupsX, u32 softGroupsY);
    // Soft opaque shadow TEMPORAL accumulation: the reproject-merge pass
    // inserted per slot between the soft trace and the a-trous resolve. ensureShadowReprojectMergePipeline builds its
    // pipeline + layout; ensureShadowReprojectMergeBindingSet builds the two front/back sets (history-in SRV and history-out
    // UAV never alias). swapSoftShadowTemporalHistory stashes this frame's worldToClip for next-frame reprojection +
    // ping-pongs the history / moments / geometry buffers at frame end (guarded on m_softShadowTemporalReady).
    [[nodiscard]] bool ensureShadowReprojectMergePipeline();
    [[nodiscard]] bool ensureShadowReprojectMergeBindingSet(DeferredFrameTargets& targets);
    void swapSoftShadowTemporalHistory(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureSwCausticPipeline();
    [[nodiscard]] bool ensureSwCausticBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureCausticResolvePipeline();
    [[nodiscard]] bool ensureCausticResolveBindingSet(DeferredFrameTargets& targets);
    // Geometry downsample pre-pass: fills the half-res geometry cache (world + receiver validity) the resolve reads.
    [[nodiscard]] bool ensureCausticGeometryDownsamplePipeline();
    [[nodiscard]] bool ensureCausticGeometryDownsampleBindingSet(DeferredFrameTargets& targets);
    // Accumulator decay pre-pass (splat-space temporal EMA): multiplies the resident R32_UINT accumulator by the temporal
    // decay factor before the producer splats this frame's photons (only used when temporal is enabled, decay > 0).
    [[nodiscard]] bool ensureCausticAccumulatorDecayPipeline();
    [[nodiscard]] bool ensureCausticAccumulatorDecayBindingSet(DeferredFrameTargets& targets);
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
    // pipeline + both ping-pong binding sets are ready (ensureCausticResolvePipeline/BindingSet).
    void dispatchCausticResolve(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureCausticRtPipeline();
    [[nodiscard]] bool ensureCausticRtBindingSet(DeferredFrameTargets& targets);
    [[nodiscard]] bool ensureBvhSortPipeline();
    [[nodiscard]] bool ensureBvhSortBuffers(usize paddedCount);
    [[nodiscard]] bool bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount);
    [[nodiscard]] bool ensureBvhBuildPipeline();
    [[nodiscard]] bool ensureBvhVisitCounterBuffer(usize primitiveCount);
    [[nodiscard]] bool createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer);
    [[nodiscard]] bool ensureMeshBvhBindingSet(Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, Core::Buffer* nodeBuffer, Core::Buffer* parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool ensureMeshSwBvhResources(Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool buildMeshSwBvh(Core::CommandList& commandList, Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, u32 primitiveCount, const SIMDVector aabbMin, const SIMDVector aabbMax, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool refitMeshSwBvh(Core::CommandList& commandList, Core::Buffer* positionBuffer, Core::Buffer* triangleIndexBuffer, u32 primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer, Core::BindingSetHandle& bindingSet);
    [[nodiscard]] bool updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources);
    [[nodiscard]] bool ensureSceneBvhBuffers(u32 instanceCount);
    [[nodiscard]] bool ensureCausticEmissionTargetBuffer(usize targetCount);
    [[nodiscard]] bool ensureShadowInstanceMaterialBuffer(usize instanceCount);
    [[nodiscard]] bool uploadShadowMaterialContextBuffers(
        Core::CommandList& commandList,
        const InstanceGpuDataVector& instanceData,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool ensureShadowInstanceContextBuffer(usize instanceCount);
    [[nodiscard]] bool ensureShadowMaterialTypedBuffer(usize byteCount);

#if defined(NWB_DEBUG)
private:
    void runBvhSortSelfTest();
    void runBvhBuildSelfTest();
    void runSceneBvhSelfTest();
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

