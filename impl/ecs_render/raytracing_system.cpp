// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererRayTracingSystem::RendererRayTracingSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase<RendererSystem>(renderer)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::logCapabilityOnce(){
    if(rayTracingState().m_capabilityLogged)
        return;

    rayTracingState().m_capabilityLogged = true;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: ray tracing capability - accel struct {}, pipeline {}, ray query {}")
        , graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        , graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)
        , graphics().queryFeatureSupport(Core::Feature::RayQuery)
    );

#if defined(NWB_DEBUG)
    runBvhSortSelfTest();
    runBvhBuildSelfTest();
    runSceneBvhSelfTest();
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareShadowVisibilityResources(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    Core::Alloc::ScratchArena& scratchArena,
    bool& outBackendReady
){
    outBackendReady = false;
    if(!targets.shadowVisibility)
        return false;

    // Caustic emission-target gather (P1) runs once per frame regardless of the shadow backend; it populates the
    // refractive-instance world AABBs the caustic producer will aim at and the count that gates caustic-light
    // assignment in updateSceneShadingBuffer. A failure here is non-fatal to shadows (no consumer reads it yet).
    if(!prepareCausticEmissionTargets(commandList, scratchArena))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target gather failed"));

    const bool hardwareShadowSupported =
        graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
    ;
    if(hardwareShadowSupported){
        if(!buildPendingMeshBlas(commandList))
            return true;

        const bool sceneReady = buildSceneTlas(commandList, scratchArena);
        if(!sceneReady)
            return true;

        // Hardware path casts the OPAQUE (binary) shadow via inline RayQuery.
        outBackendReady =
            ensureShadowPipeline()
            && ensureShadowBindingSet(targets)
        ;

        // Hybrid TRANSPARENT shadow: when the scene holds a transparent occluder, also build the software scene/mesh
        // BVH and traversal pipeline. Its gather matches buildSceneTlas's (same RendererComponent view, aligned
        // conditions), so the scene-BVH leaf index equals the hardware InstanceID and the material context it rebuilds
        // is byte-identical -- leaving the HW caustic (which reads that context by InstanceID) untouched. The render
        // runs the SW traversal as a second pass that MULTIPLIES its colored transparent transmittance onto the opaque
        // mask. Built BEFORE prepareHwCausticResources so the (idempotently) rebuilt context buffers are final before
        // the caustic binding set captures them. Opaque-only scenes skip all of this and pay no software cost.
        rayTracingState().m_hybridTransparentShadowReady = false;
        if(outBackendReady && rayTracingState().m_sceneHasTransparentOccluder){
            if(!buildPendingMeshSwBvh(commandList))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent shadow per-mesh software BVH build failed"));
            // Guard m_swShadowMeshCount > 0 before ensureSwShadowBindingSet (which asserts it): if no per-mesh software
            // BVH was available this frame the software pass simply does not run (HW opaque-only), rather than aborting.
            const bool swReady =
                buildSceneSwBvh(commandList, scratchArena)
                && rayTracingState().m_swShadowMeshCount > 0u
                && rayTracingState().m_sceneBvhInstanceCount > 0u
                && ensureSwShadowPipeline()
                && ensureSwShadowBindingSet(targets)
            ;
            if(swReady)
                rayTracingState().m_hybridTransparentShadowReady = true;
            else
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow preparation failed; transparent shadows absent this frame"));
        }

        // HW soft-shadow unification (Phase 1): route the HW OPAQUE shadow through the SAME half-res soft denoise chain
        // the SW path uses, so the HW opaque shadow becomes a half-res jittered -> temporal -> a-trous -> upsampled soft
        // shadow. The HW opaque-soft trace (m_shadowSoftPipeline) writes the SAME shadowSoftHalfA buffer, then the shared
        // geometry-downsample + a-trous resolve (+ temporal reproject-merge) denoise it -- writing the SAME m_softShadow*
        // state the SW branch writes (NOT a fork), so the render's soft block is backend-agnostic. Non-fatal: a failure
        // leaves m_softShadowReady false and the HW render falls back to the existing full-res 1-spp hard-ish trace.
        rayTracingState().m_softShadowReady =
            outBackendReady
            && ensureShadowSoftPipeline()
            && ensureShadowSoftBindingSet(targets)
            && ensureShadowGeometryDownsamplePipeline()
            && ensureShadowGeometryDownsampleBindingSet(targets)
            && ensureSoftShadowResolvePipeline()
            && ensureSoftShadowResolveBindingSet(targets)
        ;
        if(outBackendReady && !rayTracingState().m_softShadowReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft opaque shadow resource preparation failed; HW shadows fall back to the full-res trace this frame"));

        // Soft opaque shadow TEMPORAL accumulation (shared reproject-merge): same as the SW branch. Non-fatal: a failure
        // leaves m_softShadowTemporalReady false and the soft path feeds the raw trace straight into the a-trous.
        rayTracingState().m_softShadowTemporalReady =
            rayTracingState().m_softShadowReady
            && ensureShadowReprojectMergePipeline()
            && ensureShadowReprojectMergeBindingSet(targets)
        ;
        if(rayTracingState().m_softShadowReady && !rayTracingState().m_softShadowTemporalReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft opaque shadow temporal resource preparation failed; no temporal accumulation this frame"));

        // HW soft-shadow unification (Phase 2): the colored TRANSPARENT shadow now rides the SAME soft transparent
        // trace+fold the SW path uses (traced against the transparent-only software scene BVH built above via
        // m_hybridTransparentShadowReady), folded (multiplied) onto the soft-opaque visibility inside the shared
        // dispatchSoftShadowDenoiseAndTransparentFold -- retiring the old hybrid multiply (renderGpuBvhShadowVisibility,
        // multiplyOntoOpaque=true), which system.cpp now keeps only as the !softTransparentReady fallback. Gated on BOTH
        // the HW opaque soft path (m_softShadowReady -- the fold folds onto its visibility) AND the transparent SW BVH
        // being ready (m_hybridTransparentShadowReady -- guarantees m_swShadowBindingSet + m_swShadowTransparentSoftPipeline
        // + the transparent-only scene BVH exist for the trace). Opaque-only scenes leave m_hybridTransparentShadowReady
        // false, so m_softTransparentReady stays false and the transparent block is skipped (correct). Same ensure* the
        // SW branch calls -- they build the RGB resolve pipeline + transparent resolve/merge binding sets over the
        // transparent half-res buffers. Non-fatal: a sub-ensure failure leaves the state false and the fallback multiply
        // runs.
        rayTracingState().m_softTransparentReady =
            rayTracingState().m_softShadowReady
            && rayTracingState().m_hybridTransparentShadowReady
            && ensureSoftTransparentResolvePipeline()
            && ensureSoftTransparentResolveBindingSet(targets)
        ;
        if(rayTracingState().m_softShadowReady && rayTracingState().m_hybridTransparentShadowReady && !rayTracingState().m_softTransparentReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft transparent shadow resource preparation failed; colored shadows fall back to the hybrid multiply this frame"));

        rayTracingState().m_softTransparentTemporalReady =
            rayTracingState().m_softTransparentReady
            && rayTracingState().m_softShadowTemporalReady
            && ensureShadowTransparentReprojectMergeBindingSet(targets)
        ;
        if(rayTracingState().m_softTransparentReady && rayTracingState().m_softShadowTemporalReady && !rayTracingState().m_softTransparentTemporalReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft transparent shadow temporal resource preparation failed; no colored temporal accumulation this frame"));

        // Build the hardware caustic producer resources alongside the shadow ones (same TLAS + per-mesh geometry +
        // material context). Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op),
        // mirroring the SW-branch prepareGpuBvhCausticResources call below.
        if(!prepareHwCausticResources(targets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic producer resource preparation failed"));

        return true;
    }

    // No hardware ray tracing: build/refit the per-mesh software BVHs from the already skinned geometry, then
    // build the per-frame software scene/instance BVH over them before the render pass consumes it.
    if(!buildPendingMeshSwBvh(commandList))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow BVH update failed"));
    if(!buildSceneSwBvh(commandList, scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow scene BVH build failed"));
        return true;
    }

    // Enable surfel GI on the SW path (the surfel trace reuses the SW scene BVH the SW shadow/caustic paths built) and
    // create its resources THIS frame -- here, in the prepare phase, right after the SW scene BVH is resident. Doing
    // the lazy creation after the enable (instead of a frame earlier, before the enable) removes surfel GI's one-frame
    // startup latency: renderSurfelGi in the render phase can spawn/hash/trace on the very same frame the surfels
    // become active, so a single bootstrap frame already shows a bounce (the unfocused smoke app renders only that
    // frame). The pool/hash/pipeline resources live on RendererRayTracingState so a resize does not reset convergence.
    if(rayTracingState().m_sceneBvhInstanceCount > 0u && rayTracingState().m_swShadowMeshCount > 0u){
        rayTracingState().m_surfelEnabled = true;
        if(!prepareSurfelResources(commandList, targets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI resource preparation failed"));
    }

    if(rayTracingState().m_sceneBvhInstanceCount == 0u)
        return true;

    outBackendReady = ensureSwShadowPipeline() && ensureSwShadowBindingSet(targets);

    // Soft opaque shadow (all light types): build the geometry-downsample + a-trous resolve pipelines/binding sets so
    // the render can denoise the half-res jittered opaque trace into the full-res visibility. Non-fatal to shadows -- a
    // failure leaves m_softShadowReady false and the slot lights keep their hard opaque mask.
    rayTracingState().m_softShadowReady =
        outBackendReady
        && ensureShadowGeometryDownsamplePipeline()
        && ensureShadowGeometryDownsampleBindingSet(targets)
        && ensureSoftShadowResolvePipeline()
        && ensureSoftShadowResolveBindingSet(targets)
    ;
    if(outBackendReady && !rayTracingState().m_softShadowReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow resource preparation failed; shadows hard this frame"));

    // Soft opaque shadow TEMPORAL accumulation: build the reproject-merge pipeline + its two front/back binding
    // sets AND the two temporal SOFT_HALF resolve variants (the a-trous then reads the accumulated history instead of the
    // raw trace). Always on when the resources build; non-fatal: a failure leaves m_softShadowTemporalReady false and the
    // soft path feeds the raw trace straight into the a-trous (the Stage-1/2 spatial-only fallback).
    rayTracingState().m_softShadowTemporalReady =
        rayTracingState().m_softShadowReady
        && ensureShadowReprojectMergePipeline()
        && ensureShadowReprojectMergeBindingSet(targets)
    ;
    if(rayTracingState().m_softShadowReady && !rayTracingState().m_softShadowTemporalReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow temporal resource preparation failed; no temporal accumulation this frame"));

    // Soft COLORED TRANSPARENT shadow: build the RGB a-trous resolve pipeline + the parallel transparent resolve
    // binding sets (over the transparent half-res buffers), gated on the opaque soft path being ready (it shares the
    // geometry cache + the resolve binding layout + the ping-pong scratch). Non-fatal: a failure leaves m_softTransparentReady
    // false so the soft transparent fold is skipped and the transparent coarse/adaptive fallback runs (no double-fold --
    // they are exclusive). The transparent TEMPORAL path additionally needs the (shared) merge pipeline + the
    // parallel transparent merge binding sets; a failure there leaves m_softTransparentTemporalReady false and the transparent
    // resolve reads the raw colored trace straight into the RGB a-trous (the spatial fallback).
    rayTracingState().m_softTransparentReady =
        rayTracingState().m_softShadowReady
        && ensureSoftTransparentResolvePipeline()
        && ensureSoftTransparentResolveBindingSet(targets)
    ;
    if(rayTracingState().m_softShadowReady && !rayTracingState().m_softTransparentReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft transparent shadow resource preparation failed; colored shadows fall back to the hard-ish path this frame"));

    rayTracingState().m_softTransparentTemporalReady =
        rayTracingState().m_softTransparentReady
        && rayTracingState().m_softShadowTemporalReady
        && ensureShadowTransparentReprojectMergeBindingSet(targets)
    ;
    if(rayTracingState().m_softTransparentReady && rayTracingState().m_softShadowTemporalReady && !rayTracingState().m_softTransparentTemporalReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft transparent shadow temporal resource preparation failed; no colored temporal accumulation this frame"));

    // Build the software caustic producer + resolve resources alongside the SW shadow resources (same SW scene BVH +
    // per-mesh geometry). Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op).
    if(!prepareGpuBvhCausticResources(targets))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic producer resource preparation failed"));

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

