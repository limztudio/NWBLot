// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::dispatchCausticAccumulatorDecay(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticAccumulator || !targets.bindless.causticAccumulatorStorage.valid() || !m_rayTracingState.m_causticAccumulatorDecayPipeline)
        return false;

    // The task declares the accumulator write and the photon producer dependency.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);

    CausticAccumulatorDecayPushConstants decayPush;
    decayPush.width = targets.width;
    decayPush.height = targets.height;
    decayPush.decayFactor = ECSRenderDetail::s_DefaultCausticTemporalDecay;
    decayPush.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();

    Core::ComputeState decayState;
    decayState.setPipeline(m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)), 1u);
    return true;
}

b8 RendererRayTracingSystem::prepareGpuBvhCausticResources(DeferredFrameTargets& targets){
    // Prepare heap-only software pipelines once geometry and emission targets exist.
    if(m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && m_graphics.queryFeatureSupport(Core::Feature::RayQuery))
        return true;
    if(m_rayTracingState.m_causticRefractiveInstanceCount == 0u || m_rayTracingState.m_sceneBvhInstanceCount == 0u || m_rayTracingState.m_swShadowMeshCount == 0u || !m_rayTracingState.m_causticEmissionTargetBuffer)
        return true;
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return prepareCausticResources(meshView, targets, false);
}

b8 RendererRayTracingSystem::hasCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept{
    // Software photons require a caustic light, refractor, and software scene BVH.
    const b8 hardwareShadowSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && m_graphics.queryFeatureSupport(Core::Feature::RayQuery);
    return
        !hardwareShadowSupported
        && (m_lightSpaceShadow.m_csg.snapshot.hasCsg ? m_rayTracingState.m_swCausticCsgPipeline : m_rayTracingState.m_swCausticPipeline)
        && m_rayTracingState.m_sceneBvhInstanceCount > 0u
        && m_rayTracingState.m_swShadowMeshCount > 0u
        && hasCommonCausticWork(meshView)
    ;
}

void RendererRayTracingSystem::dispatchCausticResolveWaveletPass(Core::CommandList& commandList, DeferredFrameTargets& targets, const u32 passIndex){
    NWB_ASSERT(passIndex < static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT));
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    RayTracingCausticsTaskDetail::EnableCausticResolveBarriers(commandList, targets);
    const RayTracingDetail::DispatchGroups groups = RayTracingDetail::ResolveDispatchGroups(targets.width, targets.height, s_CausticResolveDownscaleFactor, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = ECSRenderDetail::s_DefaultCausticTemporalDecay;
    const f32 effectiveIntensity = VectorGetX(EffectiveCausticIntensity(VectorReplicate(temporalDecay)));
    // Alternate the prepare output and its counterpart while doubling the wavelet sampling distance.
    const b8 inputIsHalfB = ((static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) + passIndex) % 2u) == 0u;
    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfA{ targets.causticHistory.get(), targets.bindless.causticHistory.slot(), targets.bindless.causticHistoryStorage.slot() };
    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfB{ targets.causticResolveHalf.get(), targets.bindless.causticResolveHalf.slot(), targets.bindless.causticResolveHalfStorage.slot() };
    const u32 stepWidth = 1u << passIndex;
    const CausticResolveState& resolve = m_rayTracingState.m_causticResolve;
    constexpr u32 s_WaveletStepOneWidth = 1u;
    constexpr u32 s_WaveletStepTwoWidth = 2u;
    const CausticResolveStageState& wavelet = stepWidth == s_WaveletStepOneWidth ? resolve.m_waveletStepOne : stepWidth == s_WaveletStepTwoWidth ? resolve.m_waveletStepTwo : stepWidth > NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP ? resolve.m_waveletDirect : resolve.m_wavelet;
    NWB_ASSERT(wavelet.m_pipeline);
    RayTracingCausticsTaskDetail::DispatchCausticResolvePass(
        commandList,
        heap,
        *wavelet.m_pipeline.get(),
        targets,
        inputIsHalfB ? halfB : halfA,
        inputIsHalfB ? halfA : halfB,
        effectiveIntensity,
        stepWidth,
        CausticResolveStage::Wavelet,
        groups.x,
        groups.y,
        causticResolveActivitySnapshot(targets)
    );
}

void RendererRayTracingSystem::dispatchCausticResolveUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // The graph-owned five wavelet passes have completed. Only the fixed half-B upsample remains in this callback.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticIrradiance.get(), true);

    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    // Normalize temporal accumulation to retain non-temporal brightness.
    const f32 temporalDecay = ECSRenderDetail::s_DefaultCausticTemporalDecay;
    const f32 effectiveIntensity = VectorGetX(EffectiveCausticIntensity(VectorReplicate(temporalDecay)));

    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfB{ targets.causticResolveHalf.get(), targets.bindless.causticResolveHalf.slot(), targets.bindless.causticResolveHalfStorage.slot() };
    const RayTracingCausticsTaskDetail::CausticResolvePassResources irradiance{ targets.causticIrradiance.get(), targets.bindless.causticIrradiance.slot(), targets.bindless.causticIrradianceStorage.slot() };
    // Edge-aware upsample into deferred-lighting irradiance.
    RayTracingCausticsTaskDetail::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolve.m_upsample.m_pipeline.get(),
        targets,
        halfB,
        irradiance,
        effectiveIntensity,
        1u,
        CausticResolveStage::Upsample,
        fullGroupsX,
        fullGroupsY,
        causticResolveActivitySnapshot(targets)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

