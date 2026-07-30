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

// A caustic resolve target role. Each ping-pong target has both a sampled read slot and a StorageImage write slot;
// the dispatch selects the non-aliasing input/output pair through push constants.
struct CausticResolvePassResources{
    Core::Texture* texture = nullptr;
    u32 sampledSlot = 0u;
    u32 storageSlot = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareCausticEmissionTargets(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    // Caustic emission-target gather (CPU only): collect the world-space AABB of every refractive instance in
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

void RendererRayTracingSystem::releaseCausticEmissionTargetHeapHandle(){
    if(
        !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
        && !rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
    )
        return;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(rayTracingState().m_causticEmissionTargetHeapHandle);
        heap.free(rayTracingState().m_causticMaterialContextSlotsHeapHandle);
    }
    rayTracingState().m_causticEmissionTargetHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_causticMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::ensureCausticMaterialContextSlotsHeapHandle(){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon selectors require the initialized global descriptor heap"));
        return false;
    }
    if(!rayTracingState().m_rayTraceMaterialContextSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon selectors require the ray-trace material-context payload"));
        return false;
    }

    Core::GpuDescriptorHandle& handle = rayTracingState().m_causticMaterialContextSlotsHeapHandle;
    if(handle.valid()){
        if(__hidden_raytracing_system::IsHeapHandle(handle, Core::GpuDescriptorClass::UniformBuffer))
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic material-context selector has an unexpected descriptor class"));
        return false;
    }

    Core::GpuDescriptorHandle acquired;
    if(!__hidden_raytracing_system::RegisterHeapBuffer(
        heap,
        *rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(),
        Core::GpuDescriptorClass::UniformBuffer,
        false,
        acquired
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register caustic material-context selector in the descriptor heap"));
        return false;
    }
    handle = acquired;
    return true;
}

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
    rayTracingState().m_causticTemporalReuseFrameCount = 0u;

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
    // samples it instead of the read-write surfel pool -- keeping the pool off the lighting dispatch eliminates the
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

    // Half-res surfel producer: the resolve gathers into this (1/HALF_FACTOR each axis); surfel_upsample_cs reconstructs
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
        // A later re-enable must bootstrap from a fresh full/two-phase sequence rather than treating the repeatedly
        // cleared accumulator as converged temporal history.
        rayTracingState().m_causticAccumulatorInitialized = false;
        rayTracingState().m_causticTemporalReuseFrameCount = 0u;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
    }
}

void RendererRayTracingSystem::dispatchCausticResolve(Core::CommandList& commandList, DeferredFrameTargets& targets){
    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticResolve, graphics().getDevice(), commandList);
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // Every resolve access is selected by a global heap slot. Descriptor heaps are invisible to automatic resource-state
    // tracking, so stage every input/output explicitly and retain UAV barriers across the ping-pong hand-offs.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::ShaderResource);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticIrradiance.get(), true);

    // The resolve runs at HALF resolution (1/4 the pixels); only the final UPSAMPLE dispatches at full res.
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    // Geometry downsample pre-pass: fill the half-res geometry cache (world position + receiver validity) ONCE, so the
    // PREPARE + WAVELET passes tap one half-res texel for the edge-stop geometry instead of re-reading the full-res
    // world-position + depth G-buffer at the half pixel's 2x location every a-trous tap.
    {
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticResolveGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        CausticGeometryDownsamplePushConstants geometryPush;
        geometryPush.width = targets.width;
        geometryPush.height = targets.height;
        geometryPush.halfWidth = halfWidth;
        geometryPush.halfHeight = halfHeight;
        geometryPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        geometryPush.depthSlot = targets.bindless.gbufferDepth.slot();
        geometryPush.outputStorageSlot = targets.bindless.causticResolveGeometryStorage.slot();

        Core::ComputeState geometryState;
        geometryState.setPipeline(rayTracingState().m_causticGeometryDownsamplePipeline.get());
        commandList.setComputeState(geometryState);
        heap.bindCompute(commandList, *rayTracingState().m_causticGeometryDownsamplePipeline.get());
        commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
        commandList.dispatch(halfGroupsX, halfGroupsY, 1u);
    }

    // Splat-space temporal EMA normalization: with decay enabled the accumulator holds the EMA accum = photons/(1-decay)
    // at the static steady state, so pre-multiply the exposure by (1-decay) to keep the STATIC caustic brightness
    // byte-unchanged vs the non-temporal path. Disabled (decay <= 0) -> factor 1, the exposure is exactly s_CausticIntensity.
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;

    const auto runPass = [&](const __hidden_caustics::CausticResolvePassResources& input, const __hidden_caustics::CausticResolvePassResources& output, const u32 stepWidth, const CausticResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        NWB_ASSERT(input.texture);
        NWB_ASSERT(output.texture);
        NWB_ASSERT(input.texture != output.texture);
        // The selected wavelet input never aliases this pass's output (the ping-pong invariant), so every heap access
        // can retain an explicit, unambiguous state transition.
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(input.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticResolveGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(output.texture, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        CausticResolvePushConstants resolvePush;
        resolvePush.width = targets.width;
        resolvePush.height = targets.height;
        resolvePush.halfWidth = halfWidth;
        resolvePush.halfHeight = halfHeight;
        resolvePush.causticIntensity = effectiveIntensity;
        resolvePush.stepWidth = stepWidth;
        resolvePush.stage = static_cast<u32>(stage);
        resolvePush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        resolvePush.depthSlot = targets.bindless.gbufferDepth.slot();
        resolvePush.inputColorSlot = input.sampledSlot;
        resolvePush.geometrySlot = targets.bindless.causticResolveGeometry.slot();
        resolvePush.accumulatorSlot = targets.bindless.causticAccumulator.slot();
        resolvePush.outputStorageSlot = output.storageSlot;

        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_causticResolvePipeline.get());
        commandList.setComputeState(computeState);
        heap.bindCompute(commandList, *rayTracingState().m_causticResolvePipeline.get());
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    // PREPARE+DOWNSAMPLE (half-res): sum each half-res pixel's 2x2 accumulator block, un-scale + area-normalize ONCE into
    // the prepared buffer the wavelet reads. The target is seeded by parity so the ping-pong always ENDS in half-B (the
    // upsample input) regardless of PASS_COUNT: an even count starts in half-B, an odd count in half-A.
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources irradiance{
        targets.causticIrradiance.get(),
        targets.bindless.causticIrradiance.slot(),
        targets.bindless.causticIrradianceStorage.slot()
    };
    runPass(
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        1u, CausticResolveStage::PrepareDownsample, halfGroupsX, halfGroupsY
    );

    // Half-res edge-avoiding a-trous wavelet passes at a doubling dilation. Each pass writes the buffer NOT holding its
    // input. srcIsHalfB tracks where the latest result lives, seeded from the prepare target so after PASS_COUNT passes
    // the final result is in half-B.
    bool srcIsHalfB = prepareToHalfB;
    for(u32 pass = 0u; pass < static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT); ++pass){
        runPass(srcIsHalfB ? halfB : halfA, srcIsHalfB ? halfA : halfB, 1u << pass, CausticResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        srcIsHalfB = !srcIsHalfB;
    }

    // UPSAMPLE (full-res): edge-aware bilateral resample of the final half-res caustic (half-B) into the full-res
    // irradiance buffer the deferred lighting adds.
    runPass(halfB, irradiance, 1u, CausticResolveStage::Upsample, fullGroupsX, fullGroupsY);
}

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
    commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    CausticAccumulatorDecayPushConstants decayPush;
    decayPush.width = targets.width;
    decayPush.height = targets.height;
    decayPush.decayFactor = decayFactor;
    decayPush.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();

    Core::ComputeState decayState;
    decayState.setPipeline(rayTracingState().m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *rayTracingState().m_causticAccumulatorDecayPipeline.get());
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );

    // Sync the decay's UAV writes before the producer's atomic-adds hit the same image.
    commandList.commitBarriers();
}

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
        && rayTracingState().m_causticEmissionTargetHeapHandle.valid()
        && rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && rayTracingState().m_rayTraceMaterialContextSlotsBuffer
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && drawState().m_meshViewBuffer
        && drawState().m_meshViewBufferHeapHandle.valid()
        && drawState().m_meshViewBufferHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

bool RendererRayTracingSystem::prepareGpuBvhCausticResources(DeferredFrameTargets& targets){
    // Build the heap-only producer + resolve pipelines, mirroring the SW shadow prepare. Called from
    // the render-prepare path after the SW scene BVH + caustic emission targets are ready. Gated on the prepare-time
    // facts (refractive instances gathered + the SW scene BVH built + the emission-target buffer resident); the
    // caustic-LIGHT gate lives in renderGpuBvhCaustics (the light count is resolved later, in the render pass). This
    // keeps the heap slots ready the same frame the gate first opens. A failure leaves the producer idle (the
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
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !drawState().m_meshViewBuffer
        || !drawState().m_meshViewBufferHeapHandle.valid()
        || !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        drawState().m_meshViewBufferHeapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon heap input has an unexpected descriptor class"));
        return false;
    }
    if(!targets.bindless.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require complete deferred bindless frame resources"));
        return false;
    }

    const bool producerReady = ensureCausticMaterialContextSlotsHeapHandle() && ensureSwCausticPipeline();
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticResolvePipeline()
    ;
    // The splat-space temporal EMA decay pre-pass is only dispatched when temporal is enabled (decay > 0); gate its
    // pipeline on that so the disabled path never builds it.
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || ensureCausticAccumulatorDecayPipeline()
    ;
    return producerReady && resolveReady && temporalReady;
}

bool RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets, const f32 temporalDecay)const{
    return
        rayTracingState().m_causticResolvePipeline
        && rayTracingState().m_causticGeometryDownsamplePipeline
        && (temporalDecay <= 0.f || rayTracingState().m_causticAccumulatorDecayPipeline)
        && targets.causticAccumulator
        && targets.causticIrradiance
        && targets.causticHistory
        && targets.causticResolveHalf
        && targets.causticResolveGeometry
        && targets.bindless.valid()
        && targets.bindless.causticIrradianceStorage.valid()
        && targets.bindless.causticAccumulatorStorage.valid()
        && targets.bindless.causticHistoryStorage.valid()
        && targets.bindless.causticResolveHalfStorage.valid()
        && targets.bindless.causticResolveGeometryStorage.valid()
    ;
}

bool RendererRayTracingSystem::renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Software caustic photon producer + resolve — the no-hardware-ray-tracing fallback. Dispatched in the
    // SW-fallback branch right after the SW shadow pass and BEFORE deferred lighting (which reads the resolved
    // irradiance). The accumulators were already cleared to black by clearCausticTargets this frame. Runs only when
    // hasCausticWork() holds (>=1 caustic light AND >=1 refractive instance), so an empty buffer = additive no-op.

    if(!hasCausticWork())
        return false;
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !rayTracingState().m_swCausticPipeline
        || !rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;
    const u32 temporalPhaseCount = causticTemporalPhaseCount();
    const u32 photonCount = s_CausticSwPhotonCount / temporalPhaseCount;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // Splat-space temporal EMA step (enabled paths only): decay the resident accumulator (or clear it on the first
        // frame / after a resize) before this frame's splat. clearCausticTargets skipped the accumulator clear when
        // temporal is on, so this owns the accumulator's per-frame reset. No-op when temporal is disabled (the
        // accumulator was already cleared to 0 by clearCausticTargets).
        if(temporalDecay > 0.f)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

        // Heap descriptors are invisible to automatic resource-state tracking, so stage every shared traversal input
        // alongside this pass's caustic emission, target, and view resources.
        transitionSwShadowTraversalResources(commandList);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
        commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();

        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        // SW temporal reuse: bootstrap with the original two-phase checkerboard, then use four interleaved 2x2 phases
        // after the EMA warm-up. gridSide stays the FULL emission grid side; photonCount is gridSide^2 / phaseCount, so
        // the physical flux formula scales each photon to retain the same expected full-domain power.
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticSwPhotonGridSide;
        pushConstants.frameIndex = rayTracingState().m_swCausticFrameIndex;
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = rayTracingState().m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = drawState().m_meshViewBufferHeapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = rayTracingState().m_causticMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_swCausticPipeline.get());
        commandList.setComputeState(computeState);
        // SW caustic traversal accesses every resource through the global descriptor heap.
        Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
        NWB_ASSERT(heap.isInitialized());
        heap.bindCompute(commandList, *rayTracingState().m_swCausticPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(DivideUp(photonCount, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        // Advance the interleaved phase and convergence count only after a producer dispatch was recorded.
        rayTracingState().m_swCausticFrameIndex = rayTracingState().m_swCausticFrameIndex + 1u;
        advanceCausticTemporalReuse();
    }

    dispatchCausticResolve(commandList, targets);

    if(!rayTracingState().m_swCausticDispatchLogged){
        rayTracingState().m_swCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched software caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(s_CausticSwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwCausticPipeline(){
    if(rayTracingState().m_swCausticPipeline)
        return true;
    if(rayTracingState().m_swCausticPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require the initialized global descriptor heap"));
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_swCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Every selector and resource is a heap entry selected by the push block. Keep set 0 push-only.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_swCausticBindingLayout = device.createBindingLayout(layoutDesc);
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
    // Pin the global resource and sampler layouts at their fixed heap sets. Set 0 remains push-only.
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_swCausticPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_swCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticResolvePipeline(){
    if(rayTracingState().m_causticResolvePipeline)
        return true;
    if(rayTracingState().m_causticResolvePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic resolve requires the initialized global descriptor heap"));
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_causticResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Every input and output uses a target-generation heap slot. Keep set 0 push-only.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticResolvePushConstants)));

        rayTracingState().m_causticResolveBindingLayout = device.createBindingLayout(layoutDesc);
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
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_causticResolvePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve compute pipeline"));
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticGeometryDownsamplePipeline(){
    if(rayTracingState().m_causticGeometryDownsamplePipeline)
        return true;
    if(rayTracingState().m_causticGeometryDownsamplePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic geometry downsample requires the initialized global descriptor heap"));
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_causticGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // G-buffer reads and geometry-cache output are global heap entries selected by this push block.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticGeometryDownsamplePushConstants)));

        rayTracingState().m_causticGeometryDownsampleBindingLayout = device.createBindingLayout(layoutDesc);
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
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_causticGeometryDownsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

f32 RendererRayTracingSystem::causticTemporalDecay(){
    // Splat-space temporal EMA decay factor: 0.85 = a moderate ~6-7 frame time constant that de-sparkles a spinning
    // refractor while still following its motion. The renderer-state value is clamped to [0,1), so the EMA cannot diverge.
    return rayTracingState().m_causticTemporalDecay;
}

u32 RendererRayTracingSystem::causticTemporalPhaseCount(){
    // A disabled EMA has no history to recombine, so it must retain the full photon grid. Once the resident EMA has
    // received enough accepted producer updates, its existing history safely fills the three untouched 2x2 phases while
    // this frame traces only the fourth.
    if(causticTemporalDecay() <= 0.f)
        return 1u;
    return rayTracingState().m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount
        ? s_CausticTemporalBootstrapPhaseCount
        : s_CausticTemporalConvergedPhaseCount
    ;
}

void RendererRayTracingSystem::advanceCausticTemporalReuse(){
    if(causticTemporalDecay() <= 0.f){
        rayTracingState().m_causticTemporalReuseFrameCount = 0u;
        return;
    }
    if(rayTracingState().m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount)
        rayTracingState().m_causticTemporalReuseFrameCount = rayTracingState().m_causticTemporalReuseFrameCount + 1u;
}

bool RendererRayTracingSystem::ensureCausticAccumulatorDecayPipeline(){
    if(rayTracingState().m_causticAccumulatorDecayPipeline)
        return true;
    if(rayTracingState().m_causticAccumulatorDecayPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic accumulator decay requires the initialized global descriptor heap"));
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_causticAccumulatorDecayBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // The accumulator StorageImage is selected by the push block; set 0 is push-only.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticAccumulatorDecayPushConstants)));

        rayTracingState().m_causticAccumulatorDecayBindingLayout = device.createBindingLayout(layoutDesc);
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
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_causticAccumulatorDecayPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticAccumulatorDecayPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticRtPipeline(){
    if(rayTracingState().m_hwCausticPipeline)
        return true;
    if(rayTracingState().m_hwCausticPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_hwCausticBindingLayout){
        // The fixed set-10 block supplies the TLAS. The photon push block selects every other resource, including the
        // selector payloads and accumulator. The closest-hit fetches per-corner attributes through material-record heap
        // slots, so no per-mesh geometry arrays belong to set 0.
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        // Set 0 carries only the photon push constants.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_hwCausticBindingLayout = device.createBindingLayout(layoutDesc);
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
        !m_renderer.shaderSystem().loadShader(raygenShader, AssetsGraphicsCaustic::s_HwRaygenShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::RayGeneration, "ECSRender_CausticHwRaygen")
        || !m_renderer.shaderSystem().loadShader(missShader, AssetsGraphicsCaustic::s_HwMissShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::Miss, "ECSRender_CausticHwMiss")
        || !m_renderer.shaderSystem().loadShader(closestHitShader, AssetsGraphicsCaustic::s_HwClosestHitShaderName, AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::ClosestHit, "ECSRender_CausticHwClosestHit")
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
    // Pin the global resource (set 8), sampler (set 9), and TLAS (set 10) layouts onto the hardware caustic
    // ray-tracing pipeline. The local caustic RT layout remains positional set 0; the heap layouts carry explicit high
    // set indices and createPipelineLayoutForBindingLayouts gap-fills sets 1-7.
    pipelineDesc.addBindingLayout(heap.getResourceLayout());
    pipelineDesc.addBindingLayout(heap.getSamplerLayout());
    pipelineDesc.addBindingLayout(heap.getAccelStructLayout());

    Core::RayTracingPipelineShaderDesc raygenDesc(arena());
    raygenDesc.setShader(raygenShader).setExportName(__hidden_caustics::s_HwRaygenExportName);
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(arena());
    missDesc.setShader(missShader).setExportName(__hidden_caustics::s_HwMissExportName);
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(arena());
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName(__hidden_caustics::s_HwHitGroupExportName);
    pipelineDesc.addHitGroup(hitGroupDesc);

    rayTracingState().m_hwCausticPipeline = device.createRayTracingPipeline(pipelineDesc);
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
        && rayTracingState().m_causticEmissionTargetHeapHandle.valid()
        && rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && rayTracingState().m_rayTraceMaterialContextSlotsBuffer
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
        && rayTracingState().m_causticMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && drawState().m_meshViewBuffer
        && drawState().m_meshViewBufferHeapHandle.valid()
        && drawState().m_meshViewBufferHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
    ;
}

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
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !drawState().m_meshViewBuffer
        || !drawState().m_meshViewBufferHeapHandle.valid()
        || !rayTracingState().m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        drawState().m_meshViewBufferHeapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon heap input has an unexpected descriptor class"));
        return false;
    }
    if(!targets.bindless.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require complete deferred bindless frame resources"));
        return false;
    }

    const bool producerReady = ensureCausticMaterialContextSlotsHeapHandle() && ensureCausticRtPipeline();
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticResolvePipeline()
    ;
    // The splat-space temporal EMA decay pre-pass is only dispatched when temporal is enabled (decay > 0); gate its
    // pipeline on that so the disabled path never builds it.
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || ensureCausticAccumulatorDecayPipeline()
    ;
    return producerReady && resolveReady && temporalReady;
}

bool RendererRayTracingSystem::renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Hardware ray-traced caustic photon producer + resolve -- the byte-parallel sibling of renderGpuBvhCaustics,
    // dispatched in the HW branch right after the shadow pass + clearCausticTargets and BEFORE deferred lighting. The
    // raygen runs the SHARED iterative bounce loop (recursion 1) over the TLAS and splats into the SAME R32_UINT
    // accumulator the SAME resolve consumes, so the HW + SW paths converge to the same caustic (Monte-Carlo A/B).
    if(!hasHwCausticWork())
        return false;
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    {
        Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
        if(!heap.isInitialized() || !rayTracingState().m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch caustics without the descriptor-buffer TLAS heap handle"));
            return false;
        }
    }
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !rayTracingState().m_hwCausticPipeline
        || !rayTracingState().m_hwCausticShaderTable
        || !rayTracingState().m_causticMaterialContextSlotsHeapHandle.valid()
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;
    const u32 temporalPhaseCount = causticTemporalPhaseCount();
    const u32 photonCount = s_CausticHwPhotonCount / temporalPhaseCount;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // Splat-space temporal EMA step (enabled paths only): decay the resident accumulator (or clear it on the first
        // frame / after a resize) before this frame's splat. Byte-identical to the SW producer's temporal step.
        if(temporalDecay > 0.f)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

        // Move the per-mesh attribute byte buffers to ShaderResource (the heap descriptors the closest-hit reads by
        // attributeSlot point at them, so they still need the transition) + the shadow-owned material context + the
        // emission/view buffers. The G-buffer, selector payloads, and accumulator are also heap-selected, so stage all
        // resources explicitly. HW caustics needs no index buffer because the fixed-function intersector supplies the
        // hit triangle.
        for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot)
            commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
        commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();

        // Push constants byte-identical to the SW producer (same struct + same constants) so the photon grid / flux /
        // emission / jitter match the SW reference exactly. instanceCount is the live TLAS instance count (the SW
        // scene-BVH count is zero on the HW path), used only as the raygen's non-zero geometry guard.
        //
        // HW temporal reuse: byte-identical to the SW producer -- bootstrap with two checkerboard phases, then use four
        // interleaved 2x2 phases after the EMA warm-up. gridSide remains the FULL emission side and photonCount is the
        // per-frame grid budget, so nwbCausticCreatePhoton keeps expected full-domain power invariant.
        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_tlasInstanceCount;
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticHwPhotonGridSide;
        pushConstants.frameIndex = rayTracingState().m_hwCausticFrameIndex;
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = rayTracingState().m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = drawState().m_meshViewBufferHeapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = rayTracingState().m_causticMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::RayTracingState rayTracingPassState;
        rayTracingPassState.setShaderTable(rayTracingState().m_hwCausticShaderTable.get());
        commandList.setRayTracingState(rayTracingPassState);
        // Closest-hit accesses corner attributes through the descriptor heap, so bind its blocks after the
        // RayTracingState and before dispatchRays. Set 10 selects the TLAS generation.
        Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
        heap.bindRayTracing(commandList, *rayTracingState().m_hwCausticPipeline.get(), rayTracingState().m_tlasHeapHandle);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

        // Dispatch one ray per photon over gridSide x (gridSide/phaseCount), so raygen's flattened photonIndex equals
        // the SW dispatch index and exactly photonCount rays launch without wasted early-out threads. The grid side
        // scales per build config (dbg 128 / opt+fin 512); photon flux remains energy-conserving at either budget.
        Core::RayTracingDispatchRaysArguments dispatchArgs;
        dispatchArgs.setDimensions(s_CausticHwPhotonGridSide, s_CausticHwPhotonGridSide / temporalPhaseCount, 1u);
        commandList.dispatchRays(dispatchArgs);
        // Advance the interleaved phase and convergence count only after a producer dispatch was recorded.
        rayTracingState().m_hwCausticFrameIndex = rayTracingState().m_hwCausticFrameIndex + 1u;
        advanceCausticTemporalReuse();
    }

    // Identical resolve to the SW path: the shared heap-selected a-trous wavelet denoise.
    dispatchCausticResolve(commandList, targets);

    if(!rayTracingState().m_hwCausticDispatchLogged){
        rayTracingState().m_hwCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched hardware caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(s_CausticHwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticEmissionTargetBuffer(usize targetCount){
    // The caustic emission-target list is CPU-written each frame and read by the caustic producer, so it is a
    // structured SRV (no UAV) that grows by doubling like the scene-BVH / instance-material buffers. It owns a
    // persistent StorageBuffer heap descriptor: capacity replacement acquires a new slot before retiring the old
    // one, preserving already-recorded photon dispatches until the heap's deferred-free quarantine drains.
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic emission targets require the initialized global descriptor heap"));
        return false;
    }

    const auto acquireHeapHandle = [&](Core::Buffer& buffer, Core::GpuDescriptorHandle& outHandle) -> bool{
        if(!__hidden_raytracing_system::RegisterHeapBuffer(
            heap,
            buffer,
            Core::GpuDescriptorClass::StorageBuffer,
            false,
            outHandle
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register caustic emission targets in the descriptor heap"));
            return false;
        }
        return true;
    };

    if(rayTracingState().m_causticEmissionTargetBuffer && rayTracingState().m_causticEmissionTargetCapacity >= targetCount){
        if(rayTracingState().m_causticEmissionTargetHeapHandle.valid()){
            NWB_ASSERT(rayTracingState().m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer);
            return true;
        }
        return acquireHeapHandle(
            *rayTracingState().m_causticEmissionTargetBuffer.get(),
            rayTracingState().m_causticEmissionTargetHeapHandle
        );
    }

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
        // Graphics uploads this per-frame list during preparation; dedicated AsyncCompute reads it for either photon
        // producer. It is a shared read-only input after the upload, never an ownership-transfer result.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle targetBuffer = graphics().createBuffer(targetBufferDesc);
    if(!targetBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }

    Core::GpuDescriptorHandle targetHeapHandle = Core::GpuDescriptorHandle::invalid();
    if(!acquireHeapHandle(*targetBuffer.get(), targetHeapHandle))
        return false;

    if(rayTracingState().m_causticEmissionTargetHeapHandle.valid())
        heap.free(rayTracingState().m_causticEmissionTargetHeapHandle);
    rayTracingState().m_causticEmissionTargetBuffer = Move(targetBuffer);
    rayTracingState().m_causticEmissionTargetHeapHandle = targetHeapHandle;
    rayTracingState().m_causticEmissionTargetCapacity = capacity;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)")
        , static_cast<u64>(capacity)
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

