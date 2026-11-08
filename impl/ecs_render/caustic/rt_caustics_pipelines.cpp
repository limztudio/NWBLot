// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/refractive_subsystem.adhd.h>
#include <impl/ecs_render/caustic/caustic_pipeline_set.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/caustic/graph_types.adhd.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::prepareHwCausticResources(DeferredFrameTargets& targets){
    // Prepare hardware resources once geometry and emission targets exist.
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(m_rayTracingState.m_causticRefractiveInstanceCount == 0u || !m_rayTracingState.m_tlas || m_rayTracingState.m_shadowMeshCount == 0u || !m_rayTracingState.m_causticEmissionTargets.buffer)
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

b8 CausticPipelineSet::ensureSwCausticPipeline(){
    const b8 csg = m_hasCsg;
    auto& shader = csg ? m_caustic.m_swCausticCsgShader : m_caustic.m_swCausticShader;
    auto& pipeline = csg ? m_caustic.m_swCausticCsgPipeline : m_caustic.m_swCausticPipeline;
    auto& failed = csg ? m_caustic.m_swCausticCsgPipelineFailed : m_caustic.m_swCausticPipelineFailed;
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
        m_caustic.m_swCausticBindingLayout,
        shader,
        pipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        failed = true;
        return false;
    }
    return true;
}

b8 CausticPipelineSet::ensureCausticResolvePipeline(){
    CausticResolveState& resolve = m_caustic.m_causticResolve;
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

b8 CausticPipelineSet::ensureCausticGeometryDownsamplePipeline(){
    if(m_caustic.m_causticGeometryDownsamplePipeline)
        return true;
    if(m_caustic.m_causticGeometryDownsamplePipelineFailed)
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
        m_caustic.m_causticGeometryDownsampleBindingLayout,
        m_caustic.m_causticGeometryDownsampleShader,
        m_caustic.m_causticGeometryDownsamplePipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        m_caustic.m_causticGeometryDownsamplePipelineFailed = true;
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
        && m_rayTracingState.m_causticEmissionTargets.buffer
        && m_rayTracingState.m_causticEmissionTargets.heapHandle.valid()
        && m_rayTracingState.m_causticEmissionTargets.heapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && meshView.bindingValid()
    ;
}

b8 CausticPipelineSet::ensureCausticAccumulatorDecayPipeline(){
    if(m_caustic.m_causticAccumulatorDecayPipeline)
        return true;
    if(m_caustic.m_causticAccumulatorDecayPipelineFailed)
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
        m_caustic.m_causticAccumulatorDecayBindingLayout,
        m_caustic.m_causticAccumulatorDecayShader,
        m_caustic.m_causticAccumulatorDecayPipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        m_caustic.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}

b8 CausticPipelineSet::ensureCausticRtPipeline(){
    const b8 csg = m_hasCsg;
    auto& cachedPipeline = csg ? m_caustic.m_hwCausticCsgPipeline : m_caustic.m_hwCausticPipeline;
    auto& cachedTable = csg ? m_caustic.m_hwCausticCsgShaderTable : m_caustic.m_hwCausticShaderTable;
    auto& failed = csg ? m_caustic.m_hwCausticCsgPipelineFailed : m_caustic.m_hwCausticPipelineFailed;
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

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CausticPhotonPushConstants), Core::ShaderType::AllRayTracing, m_caustic.m_hwCausticBindingLayout)){
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
    pipelineDesc.addBindingLayout(m_caustic.m_hwCausticBindingLayout);
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

