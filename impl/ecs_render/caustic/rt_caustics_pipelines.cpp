// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::prepareHwCausticResources(DeferredFrameTargets& targets){
    // Prepare hardware resources once geometry and emission targets exist.
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(m_rayTracingState.m_causticRefractiveInstanceCount == 0u || !m_rayTracingState.m_tlas || m_rayTracingState.m_shadowMeshCount == 0u || !m_rayTracingState.m_causticEmissionTargetBuffer)
        return true;
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return prepareCausticResources(meshView, true, targets);
}

b8 RendererRayTracingSystem::hasHwCausticWork()const noexcept{
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return hasHwCausticWork(meshView);
}

b8 RendererRayTracingSystem::hasHwCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept{
    // Hardware photons require a caustic light, refractor, TLAS, and tracked mesh.
    return
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && (m_lightSpaceShadow.m_csg.snapshot.hasCsg ? m_rayTracingState.m_hwCausticCsgPipeline : m_rayTracingState.m_hwCausticPipeline)
        && (m_lightSpaceShadow.m_csg.snapshot.hasCsg ? m_rayTracingState.m_hwCausticCsgShaderTable : m_rayTracingState.m_hwCausticShaderTable)
        && m_rayTracingState.m_tlas
        && m_rayTracingState.m_shadowMeshCount > 0u
        && hasCommonCausticWork(meshView)
    ;
}

b8 RendererRayTracingSystem::ensureSwCausticPipeline(){
    const b8 csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    auto& shader = csg ? m_rayTracingState.m_swCausticCsgShader : m_rayTracingState.m_swCausticShader;
    auto& pipeline = csg ? m_rayTracingState.m_swCausticCsgPipeline : m_rayTracingState.m_swCausticPipeline;
    auto& failed = csg ? m_rayTracingState.m_swCausticCsgPipelineFailed : m_rayTracingState.m_swCausticPipelineFailed;
    if(pipeline)
        return true;
    if(failed)
        return false;

    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &RefractiveSubsystem::causticPhotonSwCs,
        csg ? s_RayTracingCsgEnabledVariant : s_RayTracingCsgDisabledVariant,
        "ECSRender_SwCausticPhotons",
        sizeof(CausticPhotonPushConstants),
        false,
        m_rayTracingState.m_swCausticBindingLayout,
        shader,
        pipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        failed = true;
        return false;
    }
    return true;
}

b8 RendererRayTracingSystem::ensureCausticResolvePipeline(){
    CausticResolveState& resolve = m_rayTracingState.m_causticResolve;
    if(resolve.m_prepare.m_pipeline && resolve.m_wavelet.m_pipeline && resolve.m_waveletStepOne.m_pipeline && resolve.m_waveletStepTwo.m_pipeline && resolve.m_waveletDirect.m_pipeline && resolve.m_upsample.m_pipeline)
        return true;
    if(resolve.m_failed)
        return false;

    auto& device = m_graphics.getDevice();
    if(!device.getDescriptorHeap().isInitialized() || !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CausticResolvePushConstants), Core::ShaderType::Compute, resolve.m_bindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve binding layout"));
        resolve.m_failed = true;
        return false;
    }

    struct StageRequest{
        AStringView m_variant;
        CausticResolveStageState& m_state;
    };
    const Array<StageRequest, 6u> stages = {{
        { s_CausticResolvePrepareVariant, resolve.m_prepare },
        { s_CausticResolveWaveletVariant, resolve.m_wavelet },
        { s_CausticResolveWaveletStepOneVariant, resolve.m_waveletStepOne },
        { s_CausticResolveWaveletStepTwoVariant, resolve.m_waveletStepTwo },
        { s_CausticResolveWaveletDirectVariant, resolve.m_waveletDirect },
        { s_CausticResolveUpsampleVariant, resolve.m_upsample },
    }};
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_DYNAMIC == 3u);
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_DIRECT == 4u);
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_ONE == 5u);
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_TWO == 6u);
    static_assert(NWB_CAUSTIC_RESOLVE_STAGE_WAVELET == 1u && NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE == 2u);
    for(const StageRequest& stage : stages){
        if(stage.m_state.m_pipeline)
            continue;
        if(
            !m_shaderSystem.loadShader<ComputeShader>(&RefractiveSubsystem::causticResolveCs, stage.m_variant, "ECSRender_CausticResolve", stage.m_state.m_shader)
            || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, stage.m_state.m_shader, resolve.m_bindingLayout, false, stage.m_state.m_pipeline)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create specialized caustic resolve pipeline"));
            resolve.m_failed = true;
            return false;
        }
    }
    return true;
}

b8 RendererRayTracingSystem::ensureCausticGeometryDownsamplePipeline(){
    if(m_rayTracingState.m_causticGeometryDownsamplePipeline)
        return true;
    if(m_rayTracingState.m_causticGeometryDownsamplePipelineFailed)
        return false;

    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &RefractiveSubsystem::causticGeometryDownsampleCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CausticGeometryDownsample",
        sizeof(CausticGeometryDownsamplePushConstants),
        false,
        m_rayTracingState.m_causticGeometryDownsampleBindingLayout,
        m_rayTracingState.m_causticGeometryDownsampleShader,
        m_rayTracingState.m_causticGeometryDownsamplePipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

b8 RendererRayTracingSystem::hasCommonCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept{
    return
        m_shadowVisibilityTraceResourcesPreflighted
        && m_shadowVisibilityPreparedTargets
        && causticResolveResourcesReady(*m_shadowVisibilityPreparedTargets)
        && m_rayTracingState.m_causticLightCount > 0u
        && m_unsupportedCausticVolumeCount == 0u
        && m_rayTracingState.m_causticRefractiveInstanceCount > 0u
        && m_rayTracingState.m_causticEmissionTargetBuffer
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && meshView.bindingValid()
    ;
}

b8 RendererRayTracingSystem::ensureCausticAccumulatorDecayPipeline(){
    if(m_rayTracingState.m_causticAccumulatorDecayPipeline)
        return true;
    if(m_rayTracingState.m_causticAccumulatorDecayPipelineFailed)
        return false;

    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &RefractiveSubsystem::causticAccumulatorDecayCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CausticAccumulatorDecay",
        sizeof(CausticAccumulatorDecayPushConstants),
        false,
        m_rayTracingState.m_causticAccumulatorDecayBindingLayout,
        m_rayTracingState.m_causticAccumulatorDecayShader,
        m_rayTracingState.m_causticAccumulatorDecayPipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}

b8 RendererRayTracingSystem::ensureCausticRtPipeline(){
    const b8 csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    auto& cachedPipeline = csg ? m_rayTracingState.m_hwCausticCsgPipeline : m_rayTracingState.m_hwCausticPipeline;
    auto& cachedTable = csg ? m_rayTracingState.m_hwCausticCsgShaderTable : m_rayTracingState.m_hwCausticShaderTable;
    auto& failed = csg ? m_rayTracingState.m_hwCausticCsgPipelineFailed : m_rayTracingState.m_hwCausticPipelineFailed;
    if(cachedPipeline && cachedTable)
        return true;
    if(cachedPipeline || cachedTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RT caustic pipeline and shader table cache is inconsistent"));
        cachedPipeline.reset();
        cachedTable.reset();
        failed = true;
        return false;
    }
    if(failed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        failed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require the descriptor-buffer TLAS heap layout"));
        failed = true;
        return false;
    }

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CausticPhotonPushConstants), Core::ShaderType::AllRayTracing, m_rayTracingState.m_hwCausticBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding layout"));
        failed = true;
        return false;
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_shaderSystem.loadShader<RayGenerationShader>(&RefractiveSubsystem::causticPhotonHwRaygen, csg ? s_RayTracingCsgEnabledVariant : s_RayTracingCsgDisabledVariant, "ECSRender_CausticHwRaygen", raygenShader)
        || !m_shaderSystem.loadShader<MissShader>(&RefractiveSubsystem::causticPhotonHwMiss, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CausticHwMiss", missShader)
        || !m_shaderSystem.loadShader<ClosestHitShader>(&RefractiveSubsystem::causticPhotonHwChit, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CausticHwClosestHit", closestHitShader)
    ){
        failed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(m_arena);
    // The iterative bounce loop needs no shader recursion.
    pipelineDesc.setMaxPayloadSize(NWB_CAUSTIC_HW_MAX_PAYLOAD_BYTES);
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(m_rayTracingState.m_hwCausticBindingLayout);
    // Preserve global resource, sampler, and TLAS heap sets.
    pipelineDesc.addBindingLayout(heap.getResourceLayout());
    pipelineDesc.addBindingLayout(heap.getSamplerLayout());
    pipelineDesc.addBindingLayout(heap.getAccelStructLayout());

    Core::RayTracingPipelineShaderDesc raygenDesc;
    raygenDesc.setShader(raygenShader).setExportName(s_CausticHwRaygenExportName);
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc;
    missDesc.setShader(missShader).setExportName(s_CausticHwMissExportName);
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc;
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName(s_CausticHwHitGroupExportName);
    pipelineDesc.addHitGroup(hitGroupDesc);

    auto pipelineResult = device.createRayTracingPipeline(pipelineDesc);
    if(!pipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic pipeline"));
        failed = true;
        return false;
    }
    Core::RayTracingPipelineHandle pipeline = Move(*pipelineResult);

    auto shaderTableResult = pipeline->createShaderTable();
    if(!shaderTableResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic shader table"));
        failed = true;
        return false;
    }
    Core::RayTracingShaderTableHandle shaderTable = Move(*shaderTableResult);
    b8 shaderTablePopulated = shaderTable->setRayGenerationShader(s_CausticHwRaygenExportName);
    if(shaderTablePopulated){
        const auto missIndex = shaderTable->addMissShader(s_CausticHwMissExportName);
        shaderTablePopulated = missIndex && *missIndex == 0u;
    }
    if(shaderTablePopulated){
        const auto hitIndex = shaderTable->addHitGroup(s_CausticHwHitGroupExportName);
        shaderTablePopulated = hitIndex && *hitIndex == 0u;
    }
    if(!shaderTablePopulated){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to populate RT caustic shader table"));
        failed = true;
        return false;
    }

    cachedPipeline = Move(pipeline);
    cachedTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT caustic pipeline + shader table"));
    return true;
}

b8 RendererRayTracingSystem::ensureCausticEmissionTargetBuffer(usize targetCount){
    // Replace the heap slot before retiring the old emission-target buffer.
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic emission targets require the initialized global descriptor heap"));
        return false;
    }

    const auto acquireHeapHandle = [&](Core::Buffer& buffer) -> Expected<Core::GpuDescriptorHandle>{
        const auto handle = RayTracingDetail::RegisterHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false);
        if(!handle){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register caustic emission targets in the descriptor heap"));
            return MakeUnexpected(Failure{});
        }
        return *handle;
    };

    if(m_rayTracingState.m_causticEmissionTargetBuffer && m_rayTracingState.m_causticEmissionTargetCapacity >= targetCount){
        if(m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()){
            NWB_ASSERT(m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer);
            return true;
        }
        const auto handle = acquireHeapHandle(*m_rayTracingState.m_causticEmissionTargetBuffer.get());
        if(!handle)
            return false;
        m_rayTracingState.m_causticEmissionTargetHeapHandle = *handle;
        return true;
    }

    const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_causticEmissionTargetCapacity, targetCount, s_CausticEmissionTargetInitialCapacity);

    Core::BufferDesc targetBufferDesc;
    targetBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbCausticEmissionTargetGpu) * capacity))
        .setStructStride(sizeof(NwbCausticEmissionTargetGpu))
        .setDebugName(Name("caustic_emission_targets"))
        // The upload and asynchronous photon reads share this immutable per-frame input.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto targetBufferResult = m_graphics.createBuffer(targetBufferDesc);
    if(!targetBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }
    Core::BufferHandle targetBuffer = Move(*targetBufferResult);

    const auto targetHeapHandle = acquireHeapHandle(*targetBuffer.get());
    if(!targetHeapHandle)
        return false;

    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, *targetHeapHandle);
    if(m_rayTracingState.m_causticEmissionTargetHeapHandle.valid())
        heap.free(m_rayTracingState.m_causticEmissionTargetHeapHandle);
    m_rayTracingState.m_causticEmissionTargetBuffer = Move(targetBuffer);
    m_rayTracingState.m_causticEmissionTargetHeapHandle = *targetHeapHandle;
    m_rayTracingState.m_causticEmissionTargetCapacity = capacity;
    retireUnpublished.release();
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)"), static_cast<u64>(capacity));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

