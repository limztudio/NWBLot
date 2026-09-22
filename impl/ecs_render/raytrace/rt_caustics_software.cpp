// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::dispatchCausticResolveWaveletPass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const u32 passIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates){
    NWB_ASSERT(passIndex < static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT));
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
    // Alternate the prepare output and its counterpart while doubling the wavelet sampling distance.
    const bool inputIsHalfB = ((static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) + passIndex) % 2u) == 0u;
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
    const u32 stepWidth = 1u << passIndex;
    const CausticResolveStageState& wavelet = stepWidth > NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP
        ? m_rayTracingState.m_causticResolve.m_waveletDirect
        : m_rayTracingState.m_causticResolve.m_wavelet
    ;
    NWB_ASSERT(wavelet.m_pipeline);
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *wavelet.m_pipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        inputIsHalfB ? halfB : halfA,
        inputIsHalfB ? halfA : halfB,
        effectiveIntensity,
        stepWidth,
        CausticResolveStage::Wavelet,
        halfGroupsX,
        halfGroupsY,
        causticResolveActivitySnapshot(targets)
    );
}


void RendererRayTracingSystem::dispatchCausticWaveletResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
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
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;

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
    // Edge-aware upsample into deferred-lighting irradiance.
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolve.m_upsample.m_pipeline.get(),
        targets,
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        halfB,
        irradiance,
        effectiveIntensity,
        1u,
        CausticResolveStage::Upsample,
        fullGroupsX,
        fullGroupsY
    );
}

void RendererRayTracingSystem::prepareCausticAccumulatorForSplat(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor){
    // Bootstrap temporal accumulation once; later frames decay before atomic splats.
    if(!m_rayTracingState.m_causticAccumulatorInitialized){
        m_rayTracingState.m_causticAccumulatorInitialized = true;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(*targets.causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
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
    decayState.setPipeline(m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );

    // Order decay writes before photon atomic adds.
    commandList.commitBarriers();
}

bool RendererRayTracingSystem::dispatchCausticAccumulatorDecay(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const f32 decayFactor,
    const bool graphEntryStatesOwned
){
    if(
        !targets.causticAccumulator
        || !targets.bindless.causticAccumulatorStorage.valid()
        || !m_rayTracingState.m_causticAccumulatorDecayPipeline
    )
        return false;

    // The normal deferred graph arrives with the accumulator already lowered to UAV by this task's declared use. Compatibility callers retain the native transition; the following graph-owned photon task receives the compiler-planned UAV barrier, whereas direct callers keep their existing packet-local fence.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    if(!graphEntryStatesOwned){
        commandList.setTextureState(
            targets.causticAccumulator.get(),
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::UnorderedAccess
        );
        commandList.commitBarriers();
    }

    CausticAccumulatorDecayPushConstants decayPush;
    decayPush.width = targets.width;
    decayPush.height = targets.height;
    decayPush.decayFactor = decayFactor;
    decayPush.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();

    Core::ComputeState decayState;
    decayState.setPipeline(m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setComputeState(decayState);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());
    heap.bindCompute(commandList, *m_rayTracingState.m_causticAccumulatorDecayPipeline.get());
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );
    return true;
}

bool RendererRayTracingSystem::hasCausticWork()const noexcept{
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return hasCausticWork(meshView);
}

bool RendererRayTracingSystem::hasCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept{
    // Software photons require a caustic light, refractor, and software scene BVH.
    const bool hardwareShadowSupported =
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    ;
    return
        !hardwareShadowSupported
        && m_rayTracingState.m_causticLightCount > 0u
        && m_rayTracingState.m_causticRefractiveInstanceCount > 0u
        && m_rayTracingState.m_sceneBvhInstanceCount > 0u
        && m_rayTracingState.m_swShadowMeshCount > 0u
        && m_rayTracingState.m_causticEmissionTargetBuffer
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && meshView.bindingValid()
    ;
}

bool RendererRayTracingSystem::prepareGpuBvhCausticResources(DeferredFrameTargets& targets){
    // Prepare heap-only software pipelines once geometry and emission targets exist.
    if(m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && m_graphics.queryFeatureSupport(Core::Feature::RayQuery))
        return true;
    if(
        m_rayTracingState.m_causticRefractiveInstanceCount == 0u
        || m_rayTracingState.m_sceneBvhInstanceCount == 0u
        || m_rayTracingState.m_swShadowMeshCount == 0u
        || !m_rayTracingState.m_causticEmissionTargetBuffer
    )
        return true;
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    if(
        !targets.causticAccumulator
        || !targets.causticIrradiance
        || !meshView.buffer
        || !meshView.heapHandle.valid()
        || !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
    )
        return true;
    if(
        meshView.heapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic photon heap input has an unexpected descriptor class"));
        return false;
    }
    if(!targets.bindless.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require complete deferred bindless frame resources"));
        return false;
    }

    const bool producerReady = ensureRayTraceMaterialContextSlotsHeapHandle() && ensureSwCausticPipeline();
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticResolvePipeline()
    ;
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || ensureCausticAccumulatorDecayPipeline()
    ;
    return producerReady && resolveReady && temporalReady;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

