// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_HwRaygenExportName = "CausticHwRayGen";
static constexpr AStringView s_HwMissExportName = "CausticHwMiss";
static constexpr AStringView s_HwHitGroupExportName = "CausticHwHitGroup";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareCausticEmissionTargets(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    // Caustic emission-target gather (P1, CPU only): collect the world-space AABB of every refractive instance in
    // the scene -- the domain the caustic photon producer aims at. Runs once per frame regardless of the shadow
    // backend (HW TLAS or SW BVH); mirrors buildSceneSwBvh's mesh/transform resolve + 8-corner world AABB but does
    // NOT require a software BVH and keeps ONLY instances whose material is refractive. The count gates caustic-light
    // assignment (see ResolveCausticLights): zero refractive instances -> zero caustic lights.
    rayTracingState().m_causticRefractiveInstanceCount = 0u;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return true;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    Vector<NwbCausticEmissionTargetGpu, Core::Alloc::ScratchArena> targets{ scratchArena };
    targets.reserve(candidateCount);

    SIMDVector combinedMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVector combinedMax = VectorReplicate(-s_RayTracingFiniteInfinity);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        MeshResources* mesh = nullptr;
        RenderableMeshDesc resolvedMesh;
        const bool meshReady = __hidden_raytracing_system::ResolveRenderableMeshResources(
            *meshSystem,
            m_renderer.meshSystem(),
            entity,
            resolvedMesh,
            mesh
        );
        // Need valid object-space bounds to build a world AABB; the per-mesh BVH / position buffers (required by
        // the SW shadow trace) are NOT required here -- the emission target is geometry-agnostic.
        if(!meshReady || !mesh || !mesh->csgLocalBounds.valid())
            continue;

        // Only refractive instances are emission targets (the producer's classification, mirroring buildSceneTlas
        // / buildSceneSwBvh's MaterialSurfaceInfo.refractive read).
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        if(!materialInfo || !materialInfo->refractive)
            continue;

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transform
            ? __hidden_raytracing_system::BuildObjectToWorld(
                LoadFloat(transform->scale),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            )
            : MatrixIdentity()
        ;

        // Use the shared 8-corner transform so caustic targets and the scene BVH retain identical world bounds.
        SIMDVector localMin = LoadFloatInt(mesh->csgLocalBounds.minBounds);
        SIMDVector localMax = LoadFloatInt(mesh->csgLocalBounds.maxBounds);
        if(resolvedMesh.runtime){
            // Inflate the bind-pose extent about its center (component-wise, in SIMD) for a deforming refractor (no
            // per-frame CPU bound exists); keeps the photon emission domain over a skinned pose that reaches past the
            // rest bounds. center = (min+max)/2; half = (max-min)/2 * inflation; [min,max] = center -+ half.
            const SIMDVector center = VectorMultiply(VectorAdd(localMin, localMax), VectorReplicate(0.5f));
            const SIMDVector half = VectorMultiply(VectorSubtract(localMax, localMin), VectorReplicate(0.5f * s_CausticRuntimeBoundsInflation));
            localMin = VectorSubtract(center, half);
            localMax = VectorAdd(center, half);
        }
        SIMDVector worldMin{};
        SIMDVector worldMax{};
        if(!AabbTests::Transform(objectToWorld, localMin, localMax, worldMin, worldMax))
            continue;

        combinedMin = VectorMin(combinedMin, worldMin);
        combinedMax = VectorMax(combinedMax, worldMax);

        NwbCausticEmissionTargetGpu target;
        StoreFloat(worldMin, &target.aabbMin);
        StoreFloat(worldMax, &target.aabbMax);
        target.aabbMin.w = 0.f;
        target.aabbMax.w = 0.f;
        targets.push_back(target);
    }

    const u32 targetCount = static_cast<u32>(targets.size());
    if(targetCount == 0u){
        // No refractive instances: leave the resident buffer untouched, keep the count zero (every light's
        // caustic slot stays -1 downstream), and reset the combined extent.
        rayTracingState().m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticRefractiveInstanceCount = 0u;
        return true;
    }

    if(!ensureCausticEmissionTargetBuffer(targetCount))
        return false;

    Core::Buffer* targetBuffer = rayTracingState().m_causticEmissionTargetBuffer.get();
    commandList.setBufferState(targetBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(targetBuffer, targets.data(), targets.size() * sizeof(NwbCausticEmissionTargetGpu));
    commandList.setBufferState(targetBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    StoreFloat(combinedMin, &rayTracingState().m_causticTargetBoundsMin);
    StoreFloat(combinedMax, &rayTracingState().m_causticTargetBoundsMax);
    rayTracingState().m_causticRefractiveInstanceCount = targetCount;

    // No temporal motion-reject / reprojection: the resolve is a purely SPATIAL a-trous wavelet denoise (no history),
    // so a moving (even non-rigidly morphing) caustic is ghost-free by construction -- nothing here needs to track or
    // reseed on refractor motion.
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::createCausticTargets(DeferredFrameTargets& targets){
    // Additive caustic producer targets, the inverted-lifecycle sibling of the shadow visibility target. The
    // deferred lighting pass always samples the resolved irradiance (nwbBxdfCausticIrradiance), so it is allocated
    // unconditionally and cleared to BLACK each frame (the additive identity, vs the shadow buffer's white) to keep
    // a single binding/shader path regardless of whether a caustic producer ran:
    //  - causticIrradiance:  RGBA16F FULL-res, the resolve's UPSAMPLE output the lighting adds pre-tonemap.
    //  - causticAccumulator: R32_UINT FULL-res, one Texture2DArray layer per RGB channel, the fixed-point splat target the
    //                        producer InterlockedAdds into (no float image atomics exist on the backend).
    //  - causticHistory / causticResolveHalf: the two HALF-res RGBA16F ping-pong buffers for the half-res a-trous wavelet
    //                        (the prepare pass writes causticHistory, the wavelet alternates the two, the final lands in
    //                        whichever the upsample reads back to the full-res irradiance). Half-res = 1/4 the pixels.
    targets.causticIrradianceFormat = Core::Format::RGBA16_FLOAT;
    targets.causticAccumulatorFormat = Core::Format::R32_UINT;
    targets.causticHistoryFormat = Core::Format::RGBA16_FLOAT;

    // A (re)created accumulator holds no valid splat history, so re-seed the splat-space temporal EMA: the next
    // temporal-enabled frame clears the accumulator instead of decaying it. (This runs on the initial create AND on a
    // resize, both of which allocate a fresh accumulator texture.)
    rayTracingState().m_causticAccumulatorInitialized = false;

    // Round UP so a half-res pixel always covers its 2x2 full-res block even for odd extents.
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;

    Core::TextureDesc irradianceDesc;
    irradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setInUAV(true)
        .setName("engine/caustic/irradiance")
    ;
    targets.causticIrradiance = graphics().createTexture(irradianceDesc);
    if(!targets.causticIrradiance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic irradiance target"));
        return false;
    }

    // Surfel-GI resolved irradiance: full-res RGBA16F. The surfel_resolve_cs COMPUTE pass gathers the surfel field once
    // per pixel into this (rgb = indirect irradiance, a = 1 where a surfel covered the pixel); the deferred lighting
    // samples it instead of the read-write surfel pool -- keeping the pool off the pixel shader eliminates the
    // frames-in-flight pool race. Same lifecycle as causticIrradiance (full-res, recreated on resize).
    targets.surfelIrradianceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc surfelIrradianceDesc;
    surfelIrradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.surfelIrradianceFormat)
        .setInUAV(true)
        .setName("engine/gi/surfel_irradiance")
    ;
    targets.surfelIrradiance = graphics().createTexture(surfelIrradianceDesc);
    if(!targets.surfelIrradiance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel irradiance target"));
        return false;
    }

    // U6 half-res surfel producer: the resolve gathers into this (1/HALF_FACTOR each axis); surfel_upsample_cs reconstructs
    // the full-res surfelIrradiance above. Same RGBA16F format; transient (no clear -- written + read within the GI block).
    Core::TextureDesc surfelIrradianceHalfDesc;
    surfelIrradianceHalfDesc
        .setWidth(DivideUp(targets.width, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR)))
        .setHeight(DivideUp(targets.height, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR)))
        .setFormat(targets.surfelIrradianceFormat)
        .setInUAV(true)
        .setName("engine/gi/surfel_irradiance_half")
    ;
    targets.surfelIrradianceHalf = graphics().createTexture(surfelIrradianceHalfDesc);
    if(!targets.surfelIrradianceHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel half-res irradiance target"));
        return false;
    }

    Core::TextureDesc accumulatorDesc;
    accumulatorDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(ECSRenderDetail::s_CausticAccumulatorChannelCount)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.causticAccumulatorFormat)
        .setInUAV(true)
        .setName("engine/caustic/accumulator")
    ;
    targets.causticAccumulator = graphics().createTexture(accumulatorDesc);
    if(!targets.causticAccumulator){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator target"));
        return false;
    }

    Core::TextureDesc historyDesc;
    historyDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setName("engine/caustic/atrous_half_a")
    ;
    targets.causticHistory = graphics().createTexture(historyDesc);
    if(!targets.causticHistory){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-A target"));
        return false;
    }

    Core::TextureDesc halfBDesc;
    halfBDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setName("engine/caustic/atrous_half_b")
    ;
    targets.causticResolveHalf = graphics().createTexture(halfBDesc);
    if(!targets.causticResolveHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-B target"));
        return false;
    }

    // Half-res geometry cache (xyz = world, w = receiver validity) for the resolve's per-tap edge-stop geometry.
    Core::TextureDesc geometryDesc;
    geometryDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setName("engine/caustic/resolve_geometry")
    ;
    targets.causticResolveGeometry = graphics().createTexture(geometryDesc);
    if(!targets.causticResolveGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve geometry cache target"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticIrradiance || !targets.causticAccumulator)
        return;

    // Per-frame reset of the additive caustic targets. Black irradiance = the additive identity (the inverse of the
    // shadow buffer's white default): the value the deferred lighting samples whenever no caustic producer ran this
    // frame, so the additive term is a pixel-identical no-op. Always cleared.
    commandList.setTextureState(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));

    // The accumulator is the R32_UINT fixed-point splat target (one Texture2DArray layer per RGB channel). When the
    // splat-space temporal EMA is ENABLED (NWB_CAUSTIC_TEMPORAL_DECAY > 0) it must PERSIST across frames -- the producer
    // decays it in place instead of clearing (prepareCausticAccumulatorForSplat) -- so it is NOT cleared here. When
    // temporal is disabled it is a per-frame target and is cleared to 0 here. The a-trous scratch causticHistory needs no
    // clear either way because every wavelet pass fully overwrites it.
    if(causticTemporalDecay() <= 0.f){
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchCausticResolve(Core::CommandList& commandList, DeferredFrameTargets& targets){
    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticResolve, graphics().getDevice(), commandList);

    // The producer wrote the accumulator as a UAV; move it to ShaderResource for the PREPARE pass to read (un-scale +
    // area-normalize). setResourceStatesForBindingSet derives the rest each pass (G-buffer SRVs + the ping-pong UAV/SRV
    // roles of the irradiance + scratch buffers as they swap).
    commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::ShaderResource);

    // The resolve runs at HALF resolution (1/4 the pixels); only the final UPSAMPLE dispatches at full res.
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    // Geometry downsample pre-pass: fill the half-res geometry cache (world position + receiver validity) ONCE, so the
    // PREPARE + WAVELET passes tap one half-res texel for the edge-stop geometry instead of re-reading the full-res
    // world-position + depth G-buffer at the half pixel's 2x location every a-trous tap. The framework transitions the
    // cache UAV(write here) -> SRV(read in the resolve passes).
    {
        commandList.setResourceStatesForBindingSet(rayTracingState().m_causticGeometryDownsampleBindingSet.get());
        commandList.commitBarriers();

        CausticGeometryDownsamplePushConstants geometryPush;
        geometryPush.width = targets.width;
        geometryPush.height = targets.height;
        geometryPush.halfWidth = halfWidth;
        geometryPush.halfHeight = halfHeight;

        Core::ComputeState geometryState;
        geometryState.setPipeline(rayTracingState().m_causticGeometryDownsamplePipeline.get());
        geometryState.addBindingSet(rayTracingState().m_causticGeometryDownsampleBindingSet.get());
        commandList.setComputeState(geometryState);
        commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
        commandList.dispatch(halfGroupsX, halfGroupsY, 1u);
    }

    // Splat-space temporal EMA normalization: with decay enabled the accumulator holds the EMA accum = photons/(1-decay)
    // at the static steady state, so pre-multiply the exposure by (1-decay) to keep the STATIC caustic brightness
    // byte-unchanged vs the non-temporal path. Disabled (decay <= 0) -> factor 1, the exposure is exactly s_CausticIntensity.
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;

    const auto runPass = [&](Core::BindingSet* const bindingSet, const u32 stepWidth, const CausticResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        commandList.setResourceStatesForBindingSet(bindingSet);
        commandList.commitBarriers();

        CausticResolvePushConstants resolvePush;
        resolvePush.width = targets.width;
        resolvePush.height = targets.height;
        resolvePush.halfWidth = halfWidth;
        resolvePush.halfHeight = halfHeight;
        resolvePush.causticIntensity = effectiveIntensity;
        resolvePush.stepWidth = stepWidth;
        resolvePush.stage = static_cast<u32>(stage);

        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_causticResolvePipeline.get());
        computeState.addBindingSet(bindingSet);
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    // PREPARE+DOWNSAMPLE (half-res): sum each half-res pixel's 2x2 accumulator block, un-scale + area-normalize ONCE into
    // the prepared buffer the wavelet reads. The target is seeded by parity so the ping-pong always ENDS in half-B (the
    // upsample input) regardless of PASS_COUNT: an even count starts in half-B, an odd count in half-A.
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    runPass(
        prepareToHalfB ? rayTracingState().m_causticResolveBindingSetOutputHalfB.get() : rayTracingState().m_causticResolveBindingSetOutputHalfA.get(),
        1u, CausticResolveStage::PrepareDownsample, halfGroupsX, halfGroupsY
    );

    // Half-res edge-avoiding a-trous wavelet passes at a doubling dilation. Each pass writes the buffer NOT holding its
    // input (OutputHalfA reads half-B + writes half-A; OutputHalfB reads half-A + writes half-B). srcIsHalfB tracks where
    // the latest result lives, seeded from the prepare target so after PASS_COUNT passes the final result is in half-B.
    bool srcIsHalfB = prepareToHalfB;
    for(u32 pass = 0u; pass < static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT); ++pass){
        Core::BindingSet* const bindingSet = srcIsHalfB
            ? rayTracingState().m_causticResolveBindingSetOutputHalfA.get()
            : rayTracingState().m_causticResolveBindingSetOutputHalfB.get()
        ;
        runPass(bindingSet, 1u << pass, CausticResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        srcIsHalfB = !srcIsHalfB;
    }

    // UPSAMPLE (full-res): edge-aware bilateral resample of the final half-res caustic (half-B) into the full-res
    // irradiance buffer the deferred lighting adds.
    runPass(rayTracingState().m_causticResolveBindingSetUpsample.get(), 1u, CausticResolveStage::Upsample, fullGroupsX, fullGroupsY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::prepareCausticAccumulatorForSplat(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor){
    // Splat-space temporal EMA step, run at the top of the SW/HW producer when temporal is enabled (decayFactor > 0).
    // clearCausticTargets left the accumulator untouched (the clear is deferred to here), so exactly ONE of two paths
    // runs per frame:
    //  - First enabled frame (or the frame after a resize/invalidation): the accumulator holds no valid history, so clear
    //    it to 0. The producer then splats a normal single-frame caustic on top -- identical to the pre-temporal frame 0.
    //  - Every later frame: dispatch the decay pass (accum_N = decayFactor*accum_{N-1}) in place; the producer atomic-adds
    //    this frame's photons on top, so the accumulator holds the EMA. A UAV barrier between the decay dispatch and the
    //    producer's atomic-adds (setEnableUavBarriersForTexture + commitBarriers) syncs the read-after-write on the image.
    // Both paths leave the accumulator in UnorderedAccess for the producer's atomic-adds.
    if(!rayTracingState().m_causticAccumulatorInitialized){
        rayTracingState().m_causticAccumulatorInitialized = true;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
        return;
    }

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setResourceStatesForBindingSet(rayTracingState().m_causticAccumulatorDecayBindingSet.get());
    commandList.commitBarriers();

    CausticAccumulatorDecayPushConstants decayPush;
    decayPush.width = targets.width;
    decayPush.height = targets.height;
    decayPush.decayFactor = decayFactor;

    Core::ComputeState decayState;
    decayState.setPipeline(rayTracingState().m_causticAccumulatorDecayPipeline.get());
    decayState.addBindingSet(rayTracingState().m_causticAccumulatorDecayBindingSet.get());
    commandList.setComputeState(decayState);
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );

    // Sync the decay's UAV writes before the producer's atomic-adds hit the same image.
    commandList.commitBarriers();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::hasCausticWork()const noexcept{
    // The software caustic producer runs only on the no-hardware-ray-tracing path, and only when the scene holds at
    // least one caustic light AND at least one refractive instance (else the black-cleared irradiance buffer is the
    // additive no-op). The SW scene BVH must also have been built (it is the same geometry the photons trace).
    const bool hardwareShadowSupported =
        graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
    ;
    return
        !hardwareShadowSupported
        && rayTracingState().m_causticLightCount > 0u
        && rayTracingState().m_causticRefractiveInstanceCount > 0u
        && rayTracingState().m_sceneBvhInstanceCount > 0u
        && rayTracingState().m_swShadowMeshCount > 0u
        && rayTracingState().m_causticEmissionTargetBuffer
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareGpuBvhCausticResources(DeferredFrameTargets& targets){
    // Build the producer + resolve pipelines and their binding sets, mirroring the SW shadow prepare. Called from
    // the render-prepare path after the SW scene BVH + caustic emission targets are ready. Gated on the prepare-time
    // facts (refractive instances gathered + the SW scene BVH built + the emission-target buffer resident); the
    // caustic-LIGHT gate lives in renderGpuBvhCaustics (the light count is resolved later, in the render pass). This
    // keeps the binding sets ready the same frame the gate first opens. A failure leaves the producer idle (the
    // black-cleared caustic buffer remains the additive no-op).
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && graphics().queryFeatureSupport(Core::Feature::RayQuery))
        return true;
    if(
        rayTracingState().m_causticRefractiveInstanceCount == 0u
        || rayTracingState().m_sceneBvhInstanceCount == 0u
        || rayTracingState().m_swShadowMeshCount == 0u
        || !rayTracingState().m_causticEmissionTargetBuffer
    )
        return true;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !drawState().m_meshViewBuffer)
        return true;

    const bool producerReady = ensureSwCausticPipeline() && ensureSwCausticBindingSet(targets);
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticGeometryDownsampleBindingSet(targets)
        && ensureCausticResolvePipeline()
        && ensureCausticResolveBindingSet(targets)
    ;
    // The splat-space temporal EMA decay pre-pass is only dispatched when temporal is enabled (decay > 0); gate its
    // pipeline + binding set on that so the disabled path never builds them.
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || (ensureCausticAccumulatorDecayPipeline() && ensureCausticAccumulatorDecayBindingSet(targets))
    ;
    return producerReady && resolveReady && temporalReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets, const f32 temporalDecay)const{
    return
        rayTracingState().m_causticResolvePipeline
        && rayTracingState().m_causticResolveBindingSetOutputHalfA
        && rayTracingState().m_causticResolveBindingSetOutputHalfB
        && rayTracingState().m_causticResolveBindingSetUpsample
        && rayTracingState().m_causticGeometryDownsamplePipeline
        && rayTracingState().m_causticGeometryDownsampleBindingSet
        && (temporalDecay <= 0.f || (rayTracingState().m_causticAccumulatorDecayPipeline && rayTracingState().m_causticAccumulatorDecayBindingSet))
        && targets.causticAccumulator
        && targets.causticIrradiance
        && targets.causticHistory
        && targets.causticResolveHalf
        && targets.causticResolveGeometry
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Software caustic photon producer + resolve — the no-hardware-ray-tracing fallback (P3). Dispatched in the
    // SW-fallback branch right after the SW shadow pass and BEFORE deferred lighting (which reads the resolved
    // irradiance). The accumulators were already cleared to black by clearCausticTargets this frame. Runs only when
    // hasCausticWork() holds (>=1 caustic light AND >=1 refractive instance), so an empty buffer = additive no-op.

    if(!hasCausticWork())
        return false;
    const f32 temporalDecay = causticTemporalDecay();
    if(!rayTracingState().m_swCausticPipeline || !rayTracingState().m_swCausticBindingSet || !causticResolveResourcesReady(targets, temporalDecay))
        return false;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // Splat-space temporal EMA step (enabled paths only): decay the resident accumulator (or clear it on the first
        // frame / after a resize) before this frame's splat. clearCausticTargets skipped the accumulator clear when
        // temporal is on, so this owns the accumulator's per-frame reset. No-op when temporal is disabled (the
        // accumulator was already cleared to 0 by clearCausticTargets).
        if(temporalDecay > 0.f)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

        // The per-mesh BVH node + geometry buffers were left in ShaderResource by the SW shadow pass; re-assert the
        // state for every traced mesh, plus the shadow-owned material-context buffers (for the per-hit surface
        // dispatch) and the caustic emission-target buffer. setResourceStatesForBindingSet derives the rest (the
        // scene buffers, the view buffer, the G-buffer depth SRV, the accumulator UAV).
        for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
            commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        }
        commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_swCausticBindingSet.get());
        commandList.commitBarriers();

        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        // SW 2x temporal reuse: emit HALF the full grid each frame on a frame-parity checkerboard (the EMA recombines
        // the two halves). gridSide stays the FULL emission grid side (the shader derives the per-frame checkerboard
        // cells from it); photonCount is the per-frame budget = gridSide^2 / 2, so the physical flux formula doubles
        // per photon and each frame deposits the full domain power over its half.
        pushConstants.photonCount = s_CausticSwPhotonCount / 2u;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticSwPhotonGridSide;
        pushConstants.frameIndex = rayTracingState().m_swCausticFrameIndex;

        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_swCausticPipeline.get());
        computeState.addBindingSet(rayTracingState().m_swCausticBindingSet.get());
        commandList.setComputeState(computeState);
        // SW caustic traversal accesses per-mesh geometry through the descriptor heap, so bind its tables after the
        // ComputeState and before dispatch. bindCompute touches only sets 8/9; non-bindless builds skip it.
        Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
        if(heap.isInitialized())
            heap.bindCompute(commandList, *rayTracingState().m_swCausticPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(DivideUp(s_CausticSwPhotonCount / 2u, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        // Advance the checkerboard phase for next frame.
        rayTracingState().m_swCausticFrameIndex = rayTracingState().m_swCausticFrameIndex + 1u;
    }

    dispatchCausticResolve(commandList, targets);

    if(!rayTracingState().m_swCausticDispatchLogged){
        rayTracingState().m_swCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched software caustic producer ({} photons/frame temporal-reuse over {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(s_CausticSwPhotonCount / 2u)
            , static_cast<u64>(s_CausticSwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSwCausticPipeline(){
    if(rayTracingState().m_swCausticPipeline)
        return true;
    if(rayTracingState().m_swCausticPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_swCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_INSTANCE_MATERIAL, 1));
        // Per-mesh geometry is fetched from the global descriptor heap through slots carried by the material record.
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MATERIAL_TYPED, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_VIEW, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_SW_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_swCausticBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic binding layout"));
            rayTracingState().m_swCausticPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_swCausticShader,
        AssetsGraphicsCaustic::s_SwPhotonShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SwCausticPhotons"
    )){
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_swCausticShader)
        .addBindingLayout(rayTracingState().m_swCausticBindingLayout)
    ;
    // Pin the global descriptor-index heap's resource (set 8) + sampler (set 9) layouts onto the SW caustic
    // photon-trace pipeline -- the shader-layout-only side of the split: the traversal reads per-mesh geometry through
    // these sets by the host-provided slot index. The classic SW caustic layout is added first, so it keeps positional
    // set 0; the two heap layouts carry explicit sets 8/9 and createPipelineLayoutForBindingLayouts gap-fills sets 1-7
    // with the empty set layout. Guarded on a live heap so builds without one keep the pure set-0 layout. Mirrors the
    // SW-shadow scaffold (rt_shadow.cpp ensureSwShadowPassPipeline).
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
    rayTracingState().m_swCausticPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_swCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSwCausticBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_swCausticBindingLayout);
    NWB_ASSERT(rayTracingState().m_sceneBvhNodeBuffer);
    NWB_ASSERT(rayTracingState().m_sceneInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowMeshCount > 0u);
    NWB_ASSERT(rayTracingState().m_causticEmissionTargetBuffer);
    NWB_ASSERT(targets.causticAccumulator);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    NWB_ASSERT(drawState().m_meshViewBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);

    Core::Buffer* sceneNodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    Core::Buffer* emissionTargetBuffer = rayTracingState().m_causticEmissionTargetBuffer.get();
    Core::Buffer* viewBuffer = drawState().m_meshViewBuffer.get();
    const Core::Texture* depthTarget = targets.depth.get();
    const Core::Texture* worldPositionTarget = targets.worldPosition.get();
    const Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    const u32 meshCount = rayTracingState().m_swShadowMeshCount;
    if(
        rayTracingState().m_swCausticBindingSet
        && rayTracingState().m_swCausticBindingSetSceneNodes == sceneNodeBuffer
        && rayTracingState().m_swCausticBindingSetInstances == instanceBuffer
        && rayTracingState().m_swCausticBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_swCausticBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_swCausticBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_swCausticBindingSetEmissionTargets == emissionTargetBuffer
        && rayTracingState().m_swCausticBindingSetView == viewBuffer
        && rayTracingState().m_swCausticBindingSetDepth == depthTarget
        && rayTracingState().m_swCausticBindingSetWorldPosition == worldPositionTarget
        && rayTracingState().m_swCausticBindingSetAccumulator == accumulatorTarget
        && rayTracingState().m_swCausticBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_NODES, sceneNodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_INSTANCES, instanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_INSTANCE_MATERIAL, instanceMaterialBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MATERIAL_TYPED, materialTypedBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_INSTANCES, meshInstanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS, emissionTargetBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_VIEW, viewBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_SW_BINDING_ACCUMULATOR,
        targets.causticAccumulator.get(),
        targets.causticAccumulatorFormat,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::TextureDimension::Texture2DArray
    ));

    // SW caustic traversal reads per-mesh geometry through material-record descriptor-heap slots; backing buffers
    // remain transitioned for those reads.

    auto* device = graphics().getDevice();
    rayTracingState().m_swCausticBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_swCausticBindingLayout);
    if(!rayTracingState().m_swCausticBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic binding set"));
        rayTracingState().m_swCausticBindingSetSceneNodes = nullptr;
        rayTracingState().m_swCausticBindingSetInstances = nullptr;
        rayTracingState().m_swCausticBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_swCausticBindingSetMaterialTyped = nullptr;
        rayTracingState().m_swCausticBindingSetMeshInstances = nullptr;
        rayTracingState().m_swCausticBindingSetEmissionTargets = nullptr;
        rayTracingState().m_swCausticBindingSetView = nullptr;
        rayTracingState().m_swCausticBindingSetDepth = nullptr;
        rayTracingState().m_swCausticBindingSetWorldPosition = nullptr;
        rayTracingState().m_swCausticBindingSetAccumulator = nullptr;
        rayTracingState().m_swCausticBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_swCausticBindingSetSceneNodes = sceneNodeBuffer;
    rayTracingState().m_swCausticBindingSetInstances = instanceBuffer;
    rayTracingState().m_swCausticBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_swCausticBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_swCausticBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_swCausticBindingSetEmissionTargets = emissionTargetBuffer;
    rayTracingState().m_swCausticBindingSetView = viewBuffer;
    rayTracingState().m_swCausticBindingSetDepth = depthTarget;
    rayTracingState().m_swCausticBindingSetWorldPosition = worldPositionTarget;
    rayTracingState().m_swCausticBindingSetAccumulator = accumulatorTarget;
    rayTracingState().m_swCausticBindingSetMeshCount = meshCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticResolvePipeline(){
    if(rayTracingState().m_causticResolvePipeline)
        return true;
    if(rayTracingState().m_causticResolvePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_causticResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Phase 3 (Backend C): the caustic resolve set is the first pass migrated to VK_EXT_descriptor_buffer. Its
        // shape is segment-coherent pure-resource (5 texture SRVs + 1 texture UAV, no samplers) with push constants,
        // which the descriptor-buffer path serves wholesale. The opt-in declares intent only; where the extension is
        // absent the backend downgrades this layout to non-descriptor-buffer-compatible and the classic descriptor-set
        // path (Backend A) serves the pass unchanged, so no device capability gate is needed here.
        layoutDesc.setUseDescriptorBuffer(true);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_RESOLVE_BINDING_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_INPUT_COLOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_GEOMETRY, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticResolvePushConstants)));

        rayTracingState().m_causticResolveBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve binding layout"));
            rayTracingState().m_causticResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticResolveShader,
        AssetsGraphicsCaustic::s_ResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticResolve"
    )){
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticResolveShader)
        .addBindingLayout(rayTracingState().m_causticResolveBindingLayout)
    ;
    rayTracingState().m_causticResolvePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve compute pipeline"));
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticResolveBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_causticResolveBindingLayout);
    NWB_ASSERT(targets.causticAccumulator);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.causticIrradiance);
    NWB_ASSERT(targets.causticHistory);
    NWB_ASSERT(targets.causticResolveHalf);
    NWB_ASSERT(targets.causticResolveGeometry);

    // Non-const (BindingSetItem needs a mutable Texture*); the const cache fields still compare/assign fine.
    Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* irradianceTarget = targets.causticIrradiance.get();
    Core::Texture* halfATarget = targets.causticHistory.get();
    Core::Texture* halfBTarget = targets.causticResolveHalf.get();
    Core::Texture* geometryTarget = targets.causticResolveGeometry.get();
    if(
        rayTracingState().m_causticResolveBindingSetOutputHalfA
        && rayTracingState().m_causticResolveBindingSetOutputHalfB
        && rayTracingState().m_causticResolveBindingSetUpsample
        && rayTracingState().m_causticResolveBindingSetAccumulator == accumulatorTarget
        && rayTracingState().m_causticResolveBindingSetWorldPosition == worldPositionTarget
        && rayTracingState().m_causticResolveBindingSetDepth == depthTarget
        && rayTracingState().m_causticResolveBindingSetIrradiance == irradianceTarget
        && rayTracingState().m_causticResolveBindingSetHalfA == halfATarget
        && rayTracingState().m_causticResolveBindingSetHalfB == halfBTarget
        && rayTracingState().m_causticResolveBindingSetGeometry == geometryTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // Three binding sets. All share the accumulator + G-buffer SRVs and only swap the (output UAV, input-color SRV) pair:
    //  - OutputHalfA: writes half-A reading half-B (the PREPARE pass + the odd wavelet passes),
    //  - OutputHalfB: writes half-B reading half-A (the even wavelet passes),
    //  - Upsample:    writes the FULL-res irradiance reading the final half-res caustic (half-B).
    // The half buffers are half-res and the irradiance is full-res, but the sets are dimensionless (the bound textures
    // carry the extent), so the same layout serves all three.
    const auto buildSet = [&](Core::Texture* outputTex, Core::Format::Enum outputFormat, Core::Texture* inputTex, Core::Format::Enum inputFormat) -> Core::BindingSetHandle {
        Core::BindingSetDesc desc(arena());
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR,
            accumulatorTarget,
            targets.causticAccumulatorFormat,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION,
            worldPositionTarget,
            targets.worldPositionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH,
            depthTarget,
            targets.depthFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_CAUSTIC_RESOLVE_BINDING_OUTPUT,
            outputTex,
            outputFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_INPUT_COLOR,
            inputTex,
            inputFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_GEOMETRY,
            geometryTarget,
            targets.causticHistoryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        return device->createBindingSet(desc, rayTracingState().m_causticResolveBindingLayout);
    };

    Core::BindingSetHandle outputHalfA = buildSet(halfATarget, targets.causticHistoryFormat, halfBTarget, targets.causticHistoryFormat);
    Core::BindingSetHandle outputHalfB = buildSet(halfBTarget, targets.causticHistoryFormat, halfATarget, targets.causticHistoryFormat);
    Core::BindingSetHandle upsample    = buildSet(irradianceTarget, targets.causticIrradianceFormat, halfBTarget, targets.causticHistoryFormat);
    if(!outputHalfA || !outputHalfB || !upsample){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve a-trous binding sets"));
        rayTracingState().m_causticResolveBindingSetOutputHalfA = nullptr;
        rayTracingState().m_causticResolveBindingSetOutputHalfB = nullptr;
        rayTracingState().m_causticResolveBindingSetUpsample = nullptr;
        rayTracingState().m_causticResolveBindingSetAccumulator = nullptr;
        rayTracingState().m_causticResolveBindingSetWorldPosition = nullptr;
        rayTracingState().m_causticResolveBindingSetDepth = nullptr;
        rayTracingState().m_causticResolveBindingSetIrradiance = nullptr;
        rayTracingState().m_causticResolveBindingSetHalfA = nullptr;
        rayTracingState().m_causticResolveBindingSetHalfB = nullptr;
        rayTracingState().m_causticResolveBindingSetGeometry = nullptr;
        return false;
    }
    rayTracingState().m_causticResolveBindingSetOutputHalfA = Move(outputHalfA);
    rayTracingState().m_causticResolveBindingSetOutputHalfB = Move(outputHalfB);
    rayTracingState().m_causticResolveBindingSetUpsample = Move(upsample);
    rayTracingState().m_causticResolveBindingSetAccumulator = accumulatorTarget;
    rayTracingState().m_causticResolveBindingSetWorldPosition = worldPositionTarget;
    rayTracingState().m_causticResolveBindingSetDepth = depthTarget;
    rayTracingState().m_causticResolveBindingSetIrradiance = irradianceTarget;
    rayTracingState().m_causticResolveBindingSetHalfA = halfATarget;
    rayTracingState().m_causticResolveBindingSetHalfB = halfBTarget;
    rayTracingState().m_causticResolveBindingSetGeometry = geometryTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticGeometryDownsamplePipeline(){
    if(rayTracingState().m_causticGeometryDownsamplePipeline)
        return true;
    if(rayTracingState().m_causticGeometryDownsamplePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_causticGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Phase 3 (Backend C): caustic geometry downsample is the second pass migrated to VK_EXT_descriptor_buffer,
        // after caustic resolve. Its shape is segment-coherent pure-resource (2 texture SRVs + 1 texture UAV, no
        // samplers) with push constants, which the descriptor-buffer path serves wholesale. The opt-in declares intent
        // only; where the extension is absent the backend downgrades this layout to non-descriptor-buffer-compatible
        // and the classic descriptor-set path (Backend A) serves the pass unchanged, so no device capability gate is
        // needed here.
        layoutDesc.setUseDescriptorBuffer(true);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticGeometryDownsamplePushConstants)));

        rayTracingState().m_causticGeometryDownsampleBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticGeometryDownsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample binding layout"));
            rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticGeometryDownsampleShader,
        AssetsGraphicsCaustic::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticGeometryDownsample"
    )){
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticGeometryDownsampleShader)
        .addBindingLayout(rayTracingState().m_causticGeometryDownsampleBindingLayout)
    ;
    rayTracingState().m_causticGeometryDownsamplePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticGeometryDownsampleBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_causticGeometryDownsampleBindingLayout);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.causticResolveGeometry);

    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* geometryTarget = targets.causticResolveGeometry.get();
    if(
        rayTracingState().m_causticGeometryDownsampleBindingSet
        && rayTracingState().m_causticGeometryDownsampleWorldPosition == worldPositionTarget
        && rayTracingState().m_causticGeometryDownsampleDepth == depthTarget
        && rayTracingState().m_causticGeometryDownsampleGeometry == geometryTarget
    )
        return true;

    auto* device = graphics().getDevice();

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION,
        worldPositionTarget,
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH,
        depthTarget,
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT,
        geometryTarget,
        targets.causticHistoryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    Core::BindingSetHandle bindingSet = device->createBindingSet(desc, rayTracingState().m_causticGeometryDownsampleBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample binding set"));
        rayTracingState().m_causticGeometryDownsampleBindingSet = nullptr;
        rayTracingState().m_causticGeometryDownsampleWorldPosition = nullptr;
        rayTracingState().m_causticGeometryDownsampleDepth = nullptr;
        rayTracingState().m_causticGeometryDownsampleGeometry = nullptr;
        return false;
    }
    rayTracingState().m_causticGeometryDownsampleBindingSet = Move(bindingSet);
    rayTracingState().m_causticGeometryDownsampleWorldPosition = worldPositionTarget;
    rayTracingState().m_causticGeometryDownsampleDepth = depthTarget;
    rayTracingState().m_causticGeometryDownsampleGeometry = geometryTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


f32 RendererRayTracingSystem::causticTemporalDecay(){
    // Splat-space temporal EMA decay factor: 0.85 = a moderate ~6-7 frame time constant that de-sparkles a spinning
    // refractor while still following its motion. The renderer-state value is clamped to [0,1), so the EMA cannot diverge.
    return rayTracingState().m_causticTemporalDecay;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticAccumulatorDecayPipeline(){
    if(rayTracingState().m_causticAccumulatorDecayPipeline)
        return true;
    if(rayTracingState().m_causticAccumulatorDecayPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_causticAccumulatorDecayBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Phase 3 (Backend C): caustic accumulator decay is the third pass migrated to VK_EXT_descriptor_buffer, after
        // caustic resolve and geometry downsample. Its shape is segment-coherent pure-resource (a lone texture UAV, no
        // samplers) with push constants, which the descriptor-buffer path serves wholesale -- the minimal single-UAV
        // case. The opt-in declares intent only; where the extension is absent the backend downgrades this layout to
        // non-descriptor-buffer-compatible and the classic descriptor-set path (Backend A) serves the pass unchanged,
        // so no device capability gate is needed here.
        layoutDesc.setUseDescriptorBuffer(true);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_ACCUMULATOR_DECAY_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticAccumulatorDecayPushConstants)));

        rayTracingState().m_causticAccumulatorDecayBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticAccumulatorDecayBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay binding layout"));
            rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticAccumulatorDecayShader,
        AssetsGraphicsCaustic::s_AccumulatorDecayShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticAccumulatorDecay"
    )){
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticAccumulatorDecayShader)
        .addBindingLayout(rayTracingState().m_causticAccumulatorDecayBindingLayout)
    ;
    rayTracingState().m_causticAccumulatorDecayPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticAccumulatorDecayPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticAccumulatorDecayBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_causticAccumulatorDecayBindingLayout);
    NWB_ASSERT(targets.causticAccumulator);

    Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    if(
        rayTracingState().m_causticAccumulatorDecayBindingSet
        && rayTracingState().m_causticAccumulatorDecayAccumulator == accumulatorTarget
    )
        return true;

    auto* device = graphics().getDevice();

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_ACCUMULATOR_DECAY_BINDING_ACCUMULATOR,
        accumulatorTarget,
        targets.causticAccumulatorFormat,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::TextureDimension::Texture2DArray
    ));

    Core::BindingSetHandle bindingSet = device->createBindingSet(desc, rayTracingState().m_causticAccumulatorDecayBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay binding set"));
        rayTracingState().m_causticAccumulatorDecayBindingSet = nullptr;
        rayTracingState().m_causticAccumulatorDecayAccumulator = nullptr;
        return false;
    }
    rayTracingState().m_causticAccumulatorDecayBindingSet = Move(bindingSet);
    rayTracingState().m_causticAccumulatorDecayAccumulator = accumulatorTarget;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticRtPipeline(){
    if(rayTracingState().m_hwCausticPipeline)
        return true;
    if(rayTracingState().m_hwCausticPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_hwCausticBindingLayout){
        // Mirrors the shadow RT layout (TLAS + scene/light + instance-material + material-context) and adds the caustic
        // I/O (emission targets, view, G-buffer depth + world position for the splat, the R32_UINT accumulator UAV) +
        // the push constants (byte-identical to the SW producer's). No per-mesh geometry arrays: the closest-hit fetches
        // its per-corner attributes from the global descriptor heap (sets 8/9, pinned below) by the material record's
        // attributeSlot, and the refraction bends on that interpolated shading normal (so it needs no positions/indices).
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_CAUSTIC_RT_BINDING_TLAS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_INSTANCE_MATERIAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MATERIAL_TYPED, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_EMISSION_TARGETS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_VIEW, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RT_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RT_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_RT_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_hwCausticBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_hwCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding layout"));
            rayTracingState().m_hwCausticPipelineFailed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_renderer.shaderSystem().loadShader(raygenShader, AssetsGraphicsCaustic::s_HwRaygenShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::RayGeneration, "ECSRender_CausticHwRaygen")
        || !m_renderer.shaderSystem().loadShader(missShader, AssetsGraphicsCaustic::s_HwMissShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Miss, "ECSRender_CausticHwMiss")
        || !m_renderer.shaderSystem().loadShader(closestHitShader, AssetsGraphicsCaustic::s_HwClosestHitShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::ClosestHit, "ECSRender_CausticHwClosestHit")
    ){
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(arena());
    // Payload = NwbCausticHwPayload (3*float3 + 2*float + 2*uint = 52 bytes); round up to 64. Recursion stays 1 (the
    // shared bounce loop drives the bounces via a fresh TraceRay per segment, not shader recursion).
    pipelineDesc.setMaxPayloadSize(static_cast<u32>(sizeof(f32) * 16u));
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(rayTracingState().m_hwCausticBindingLayout);
    // Pin the global descriptor-index heap's resource (set 8) + sampler (set 9) layouts onto the hardware caustic
    // ray-tracing pipeline -- the shader-layout-only side of the split: the closest-hit reads each mesh's per-corner
    // attribute buffer through these sets by the host-provided slot index. The classic caustic RT layout is added
    // first, so it keeps positional set 0; the two heap layouts carry explicit sets 8/9 and
    // createPipelineLayoutForBindingLayouts gap-fills sets 1-7 with the empty set layout. Guarded on a live heap so
    // builds without one keep the pure set-0 layout. Mirrors the SW caustic scaffold (ensureSwCausticPipeline) -- the
    // first HW ray-tracing pipeline to carry the heap layouts.
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc.addBindingLayout(heap.getResourceLayout());
        pipelineDesc.addBindingLayout(heap.getSamplerLayout());
    }

    Core::RayTracingPipelineShaderDesc raygenDesc(arena());
    raygenDesc.setShader(raygenShader).setExportName(__hidden_caustics::s_HwRaygenExportName);
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(arena());
    missDesc.setShader(missShader).setExportName(__hidden_caustics::s_HwMissExportName);
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(arena());
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName(__hidden_caustics::s_HwHitGroupExportName);
    pipelineDesc.addHitGroup(hitGroupDesc);

    rayTracingState().m_hwCausticPipeline = device->createRayTracingPipeline(pipelineDesc);
    if(!rayTracingState().m_hwCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic pipeline"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = rayTracingState().m_hwCausticPipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic shader table"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }
    shaderTable->setRayGenerationShader(__hidden_caustics::s_HwRaygenExportName);
    shaderTable->addMissShader(__hidden_caustics::s_HwMissExportName);
    shaderTable->addHitGroup(__hidden_caustics::s_HwHitGroupExportName);
    rayTracingState().m_hwCausticShaderTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT caustic pipeline + shader table"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticRtBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_hwCausticBindingLayout);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMeshCount > 0u);
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_causticEmissionTargetBuffer);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    NWB_ASSERT(drawState().m_meshViewBuffer);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.causticAccumulator);

    // Rebuild when any cached input changes (mirrors the shadow + SW caustic sets): the TLAS, the shadow-owned
    // instance-material / material-context buffers, the emission targets, the view CB, the G-buffer SRVs the splat
    // reads, the accumulator UAV, and the distinct-mesh count (the per-mesh descriptor arrays repopulate).
    const Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    const Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    Core::Buffer* emissionTargetBuffer = rayTracingState().m_causticEmissionTargetBuffer.get();
    Core::Buffer* viewBuffer = drawState().m_meshViewBuffer.get();
    const Core::Texture* depthTarget = targets.depth.get();
    const Core::Texture* worldPositionTarget = targets.worldPosition.get();
    const Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    const u32 meshCount = rayTracingState().m_shadowMeshCount;
    if(
        rayTracingState().m_hwCausticBindingSet
        && rayTracingState().m_hwCausticBindingSetTlas == tlas
        && rayTracingState().m_hwCausticBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_hwCausticBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_hwCausticBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_hwCausticBindingSetEmissionTargets == emissionTargetBuffer
        && rayTracingState().m_hwCausticBindingSetView == viewBuffer
        && rayTracingState().m_hwCausticBindingSetDepth == depthTarget
        && rayTracingState().m_hwCausticBindingSetWorldPosition == worldPositionTarget
        && rayTracingState().m_hwCausticBindingSetAccumulator == accumulatorTarget
        && rayTracingState().m_hwCausticBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_CAUSTIC_RT_BINDING_TLAS, rayTracingState().m_tlas.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_INSTANCE_MATERIAL, rayTracingState().m_shadowInstanceMaterialBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MATERIAL_TYPED, materialTypedBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_INSTANCES, meshInstanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_EMISSION_TARGETS, emissionTargetBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_VIEW, viewBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_RT_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_RT_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_RT_BINDING_ACCUMULATOR,
        targets.causticAccumulator.get(),
        targets.causticAccumulatorFormat,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::TextureDimension::Texture2DArray
    ));

    // HW caustic closest-hit reads corner attributes through material-record descriptor-heap slots; backing buffers
    // remain transitioned for those reads.

    auto* device = graphics().getDevice();
    rayTracingState().m_hwCausticBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_hwCausticBindingLayout);
    if(!rayTracingState().m_hwCausticBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding set"));
        rayTracingState().m_hwCausticBindingSetTlas = nullptr;
        rayTracingState().m_hwCausticBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_hwCausticBindingSetMaterialTyped = nullptr;
        rayTracingState().m_hwCausticBindingSetMeshInstances = nullptr;
        rayTracingState().m_hwCausticBindingSetEmissionTargets = nullptr;
        rayTracingState().m_hwCausticBindingSetView = nullptr;
        rayTracingState().m_hwCausticBindingSetDepth = nullptr;
        rayTracingState().m_hwCausticBindingSetWorldPosition = nullptr;
        rayTracingState().m_hwCausticBindingSetAccumulator = nullptr;
        rayTracingState().m_hwCausticBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_hwCausticBindingSetTlas = tlas;
    rayTracingState().m_hwCausticBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_hwCausticBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_hwCausticBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_hwCausticBindingSetEmissionTargets = emissionTargetBuffer;
    rayTracingState().m_hwCausticBindingSetView = viewBuffer;
    rayTracingState().m_hwCausticBindingSetDepth = depthTarget;
    rayTracingState().m_hwCausticBindingSetWorldPosition = worldPositionTarget;
    rayTracingState().m_hwCausticBindingSetAccumulator = accumulatorTarget;
    rayTracingState().m_hwCausticBindingSetMeshCount = meshCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::hasHwCausticWork()const noexcept{
    // The hardware caustic producer runs only on the hardware-ray-tracing path, and only when the scene holds at
    // least one caustic light AND at least one refractive instance (else the black-cleared irradiance buffer is the
    // additive no-op). The TLAS + at least one tracked mesh must exist so the photon has geometry to hit.
    return graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && rayTracingState().m_causticLightCount > 0u
        && rayTracingState().m_causticRefractiveInstanceCount > 0u
        && rayTracingState().m_tlas
        && rayTracingState().m_shadowMeshCount > 0u
        && rayTracingState().m_causticEmissionTargetBuffer
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareHwCausticResources(DeferredFrameTargets& targets){
    // Build the hardware caustic producer + resolve resources, mirroring prepareGpuBvhCausticResources for the HW
    // path. Gated on the prepare-time invariants (refractive instances + the TLAS + tracked meshes + emission targets
    // + the caustic targets + the view buffer); the caustic-LIGHT gate lives in renderHwCaustics (the light count is
    // resolved later, in the render pass). A non-HW device early-returns true (nothing to build here).
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(
        rayTracingState().m_causticRefractiveInstanceCount == 0u
        || !rayTracingState().m_tlas
        || rayTracingState().m_shadowMeshCount == 0u
        || !rayTracingState().m_causticEmissionTargetBuffer
    )
        return true;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !drawState().m_meshViewBuffer)
        return true;

    const bool producerReady = ensureCausticRtPipeline() && ensureCausticRtBindingSet(targets);
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticGeometryDownsampleBindingSet(targets)
        && ensureCausticResolvePipeline()
        && ensureCausticResolveBindingSet(targets)
    ;
    // The splat-space temporal EMA decay pre-pass is only dispatched when temporal is enabled (decay > 0); gate its
    // pipeline + binding set on that so the disabled path never builds them.
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || (ensureCausticAccumulatorDecayPipeline() && ensureCausticAccumulatorDecayBindingSet(targets))
    ;
    return producerReady && resolveReady && temporalReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Hardware ray-traced caustic photon producer + resolve (P4) -- the byte-parallel sibling of renderGpuBvhCaustics,
    // dispatched in the HW branch right after the shadow pass + clearCausticTargets and BEFORE deferred lighting. The
    // raygen runs the SHARED iterative bounce loop (recursion 1) over the TLAS and splats into the SAME R32_UINT
    // accumulator the SAME resolve consumes, so the HW + SW paths converge to the same caustic (Monte-Carlo A/B).
    if(!hasHwCausticWork())
        return false;
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !rayTracingState().m_hwCausticPipeline
        || !rayTracingState().m_hwCausticShaderTable
        || !rayTracingState().m_hwCausticBindingSet
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // Splat-space temporal EMA step (enabled paths only): decay the resident accumulator (or clear it on the first
        // frame / after a resize) before this frame's splat. Byte-identical to the SW producer's temporal step.
        if(temporalDecay > 0.f)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

        // Move the per-mesh attribute byte buffers to ShaderResource (the heap descriptors the closest-hit reads by
        // attributeSlot point at them, so they still need the transition) + the shadow-owned material context + the
        // emission targets; setResourceStatesForBindingSet derives the TLAS, G-buffer SRVs, view CB, and the accumulator
        // UAV. HW caustics needs no index buffer because the fixed-function intersector supplies the hit triangle.
        for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot)
            commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_hwCausticBindingSet.get());
        commandList.commitBarriers();

        // Push constants byte-identical to the SW producer (same struct + same constants) so the photon grid / flux /
        // emission / jitter match the SW reference exactly. instanceCount is the live TLAS instance count (the SW
        // scene-BVH count is zero on the HW path), used only as the raygen's non-zero geometry guard.
        //
        // HW 2x temporal reuse: byte-identical to the SW producer -- emit HALF the full grid each frame on a
        // frame-parity checkerboard (the EMA recombines the two halves). gridSide stays the FULL emission grid side
        // (the raygen derives the per-frame checkerboard cells from it); photonCount is the per-frame budget =
        // gridSide^2 / 2, so the physical flux formula doubles per photon and each frame deposits the full domain
        // power over its half.
        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_tlasInstanceCount;
        pushConstants.photonCount = s_CausticHwPhotonCount / 2u;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticHwPhotonGridSide;
        pushConstants.frameIndex = rayTracingState().m_hwCausticFrameIndex;

        Core::RayTracingState rayTracingPassState;
        rayTracingPassState.setShaderTable(rayTracingState().m_hwCausticShaderTable.get());
        rayTracingPassState.addBindingSet(rayTracingState().m_hwCausticBindingSet.get());
        commandList.setRayTracingState(rayTracingPassState);
        // Closest-hit accesses corner attributes through the descriptor heap, so bind its tables after the
        // RayTracingState and before dispatchRays. bindRayTracing touches only sets 8/9; non-bindless builds skip it.
        Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
        if(heap.isInitialized())
            heap.bindRayTracing(commandList, *rayTracingState().m_hwCausticPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

        // Dispatch one ray per photon over a gridSide x (gridSide/2) grid == the SW half-budget photon grid, so the
        // raygen's photonIndex (y*W + x) equals the SW SV_DispatchThreadID index per photon and exactly the
        // half-budget photonCount rays launch (no wasted early-out threads). The grid side scales per build config
        // (dbg 128 / opt+fin 512) -- the producer is energy-conserving, so the higher count just densifies.
        Core::RayTracingDispatchRaysArguments dispatchArgs;
        dispatchArgs.setDimensions(s_CausticHwPhotonGridSide, s_CausticHwPhotonGridSide >> 1u, 1u);
        commandList.dispatchRays(dispatchArgs);
        // Advance the checkerboard phase for next frame.
        rayTracingState().m_hwCausticFrameIndex = rayTracingState().m_hwCausticFrameIndex + 1u;
    }

    // Identical resolve to the SW path (the SAME pipeline + ping-pong binding sets): the shared a-trous wavelet denoise.
    dispatchCausticResolve(commandList, targets);

    if(!rayTracingState().m_hwCausticDispatchLogged){
        rayTracingState().m_hwCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched hardware caustic producer ({} photons/frame temporal-reuse over {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(s_CausticHwPhotonCount / 2u)
            , static_cast<u64>(s_CausticHwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCausticEmissionTargetBuffer(usize targetCount){
    // The caustic emission-target list is CPU-written each frame and read by the caustic producer, so it is a
    // structured SRV (no UAV) that grows by doubling like the scene-BVH / instance-material buffers.
    if(rayTracingState().m_causticEmissionTargetBuffer && rayTracingState().m_causticEmissionTargetCapacity >= targetCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(
        rayTracingState().m_causticEmissionTargetCapacity,
        targetCount,
        s_CausticEmissionTargetInitialCapacity
    );

    Core::BufferDesc targetBufferDesc;
    targetBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbCausticEmissionTargetGpu) * capacity))
        .setStructStride(sizeof(NwbCausticEmissionTargetGpu))
        .setDebugName(Name("caustic_emission_targets"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_causticEmissionTargetBuffer = graphics().createBuffer(targetBufferDesc);
    if(!rayTracingState().m_causticEmissionTargetBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }
    rayTracingState().m_causticEmissionTargetCapacity = capacity;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)")
        , static_cast<u64>(capacity)
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

