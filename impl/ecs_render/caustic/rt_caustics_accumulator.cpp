// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::setCausticQualitySettings(const CausticQualitySettings& settings)noexcept{
    if(!ValidateCausticQualitySettings(settings))
        return false;
    if(m_causticQualitySettings.photonGridDivisor == settings.photonGridDivisor)
        return true;
    m_causticQualitySettings = settings;
    // The next selected producer clears its accumulator through the existing accepted bootstrap transaction.
    m_rayTracingState.m_causticAccumulatorInitialized = false;
    m_rayTracingState.m_swCausticDispatchLogged = false;
    m_rayTracingState.m_hwCausticDispatchLogged = false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::confirmCausticAccumulatorBootstrapClear()noexcept{
    m_rayTracingState.m_causticAccumulatorInitialized = true;
}

void RendererRayTracingSystem::dispatchCausticGeometryDownsample(Core::CommandList& commandList, DeferredFrameTargets& targets){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // The graph declares the writable geometry cache and its later sampled handoff.
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);

    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, s_CausticResolveDownscaleFactor, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfWidth = groups.width;
    const u32 halfHeight = groups.height;

    CausticGeometryDownsamplePushConstants geometryPush;
    geometryPush.width = targets.width;
    geometryPush.height = targets.height;
    geometryPush.halfWidth = halfWidth;
    geometryPush.halfHeight = halfHeight;
    geometryPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
    geometryPush.depthSlot = targets.bindless.gbufferDepth.slot();
    geometryPush.outputStorageSlot = targets.bindless.causticResolveGeometryStorage.slot();

    Core::ComputeState geometryState;
    geometryState.setPipeline(m_rayTracingState.m_causticGeometryDownsamplePipeline.get());
    commandList.setComputeState(geometryState);
    heap.bindCompute(commandList, *m_rayTracingState.m_causticGeometryDownsamplePipeline.get());
    commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
    commandList.dispatch(groups.x, groups.y, 1u);
}

void RendererRayTracingSystem::dispatchCausticResolvePrepare(Core::CommandList& commandList, DeferredFrameTargets& targets){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // Prepare reads accumulated photons and resolve geometry, then writes the parity-selected half-resolution target.
    RayTracingCausticsTaskDetail::EnableCausticResolveBarriers(commandList, targets);
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, s_CausticResolveDownscaleFactor, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 effectiveIntensity = EffectiveCausticIntensity(s_DefaultCausticTemporalDecay);
    const b8 prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfA = RayTracingCausticsTaskDetail::MakeCausticHistoryResolvePassResources(targets);
    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfB = RayTracingCausticsTaskDetail::MakeCausticHalfResolvePassResources(targets);
    RayTracingCausticsTaskDetail::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolve.m_prepare.m_pipeline.get(),
        targets,
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        effectiveIntensity,
        1u,
        CausticResolveStage::PrepareDownsample,
        groups.x,
        groups.y,
        causticResolveActivitySnapshot(targets)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::prepareCausticResources(const ECSRenderDetail::MeshViewBufferSnapshot& meshView, const b8 hardware, DeferredFrameTargets& targets){
    if(!targets.causticAccumulator || !targets.causticIrradiance || !meshView.buffer || !meshView.heapHandle.valid() || !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid())
        return true;
    if(meshView.heapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer || m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon heap input has an unexpected descriptor class"));
        return false;
    }
    if(!targets.bindless.valid()){
        if(hardware)
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require complete deferred bindless frame resources"));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require complete deferred bindless frame resources"));
        return false;
    }

    const b8 producerReady = ensureRayTraceMaterialContextSlotsHeapHandle() && (hardware ? ensureCausticRtPipeline() : ensureSwCausticPipeline());
    // Prepare shared resolve and temporal resources even when the producer is unavailable.
    const b8 resolveReady = ensureCausticGeometryDownsamplePipeline() && ensureCausticResolvePipeline();
    const b8 temporalReady = ensureCausticAccumulatorDecayPipeline();
    return producerReady && resolveReady && temporalReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

