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


bool RendererRayTracingSystem::ensureSwCausticPipeline(){
    const bool csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    auto& shader = csg ? m_rayTracingState.m_swCausticCsgShader : m_rayTracingState.m_swCausticShader;
    auto& pipeline = csg ? m_rayTracingState.m_swCausticCsgPipeline : m_rayTracingState.m_swCausticPipeline;
    auto& failed = csg ? m_rayTracingState.m_swCausticCsgPipelineFailed : m_rayTracingState.m_swCausticPipelineFailed;
    if(pipeline)
        return true;
    if(failed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software caustics require the initialized global descriptor heap"));
        failed = true;
        return false;
    }

    if(!m_rayTracingState.m_swCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Set 0 is push-only; resources come from the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        m_rayTracingState.m_swCausticBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_swCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic binding layout"));
            failed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(
        shader,
        AssetsGraphicsCaustic::s_SwPhotonShaderName,
        csg ? "NWB_RT_CSG_ENABLED=1" : "NWB_RT_CSG_ENABLED=0",
        "ECSRender_SwCausticPhotons"
    )){
        failed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(m_rayTracingState.m_swCausticBindingLayout)
    ;
    // Global heap layouts occupy their fixed sets.
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    pipeline = device.createComputePipeline(pipelineDesc);
    if(!pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        failed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticResolvePipeline(){
    CausticResolveState& resolve = m_rayTracingState.m_causticResolve;
    if(
        resolve.m_prepare.m_pipeline && resolve.m_wavelet.m_pipeline
        && resolve.m_waveletStepOne.m_pipeline && resolve.m_waveletStepTwo.m_pipeline
        && resolve.m_waveletDirect.m_pipeline && resolve.m_upsample.m_pipeline
    )
        return true;
    if(resolve.m_failed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic resolve requires the initialized global descriptor heap"));
        resolve.m_failed = true;
        return false;
    }

    if(!resolve.m_bindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Target-generation resources are selected through the push block.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticResolvePushConstants)));

        resolve.m_bindingLayout = device.createBindingLayout(layoutDesc);
        if(!resolve.m_bindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve binding layout"));
            resolve.m_failed = true;
            return false;
        }
    }

    struct StageRequest{
        AStringView m_variant;
        CausticResolveStageState& m_state;
    };
    const StageRequest stages[] = {
        { "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=3", resolve.m_prepare },
        { "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=1", resolve.m_wavelet },
        { "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=5", resolve.m_waveletStepOne },
        { "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=6", resolve.m_waveletStepTwo },
        { "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=4", resolve.m_waveletDirect },
        { "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=2", resolve.m_upsample },
    };
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_DYNAMIC == 3u);
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_DIRECT == 4u);
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_ONE == 5u);
    static_assert(NWB_CAUSTIC_RESOLVE_COMPILED_STAGE_WAVELET_STEP_TWO == 6u);
    static_assert(NWB_CAUSTIC_RESOLVE_STAGE_WAVELET == 1u && NWB_CAUSTIC_RESOLVE_STAGE_UPSAMPLE == 2u);
    for(const StageRequest& stage : stages){
        if(stage.m_state.m_pipeline)
            continue;
        if(!m_shaderSystem.loadShader<ComputeShader>(
            stage.m_state.m_shader,
            AssetsGraphicsCaustic::s_ResolveShaderName,
            stage.m_variant,
            "ECSRender_CausticResolve"
        )){
            resolve.m_failed = true;
            return false;
        }

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(stage.m_state.m_shader)
            .addBindingLayout(resolve.m_bindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        stage.m_state.m_pipeline = device.createComputePipeline(pipelineDesc);
        if(!stage.m_state.m_pipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create specialized caustic resolve pipeline"));
            resolve.m_failed = true;
            return false;
        }
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticGeometryDownsamplePipeline(){
    if(m_rayTracingState.m_causticGeometryDownsamplePipeline)
        return true;
    if(m_rayTracingState.m_causticGeometryDownsamplePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic geometry downsample requires the initialized global descriptor heap"));
        m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_causticGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticGeometryDownsamplePushConstants)));

        m_rayTracingState.m_causticGeometryDownsampleBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_causticGeometryDownsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample binding layout"));
            m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(
        m_rayTracingState.m_causticGeometryDownsampleShader,
        AssetsGraphicsCaustic::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CausticGeometryDownsample"
    )){
        m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_causticGeometryDownsampleShader)
        .addBindingLayout(m_rayTracingState.m_causticGeometryDownsampleBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_causticGeometryDownsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_causticGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        m_rayTracingState.m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

f32 RendererRayTracingSystem::causticTemporalDecay()noexcept{
    return m_rayTracingState.m_causticTemporalDecay;
}

u32 RendererRayTracingSystem::causticTemporalPhaseCount(){
    // Reuse phases only after temporal history warms up.
    if(causticTemporalDecay() <= 0.f)
        return NWB_CAUSTIC_TEMPORAL_DISABLED_PHASE_COUNT;
    return m_rayTracingState.m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount
        ? s_CausticTemporalBootstrapPhaseCount
        : s_CausticTemporalConvergedPhaseCount
    ;
}

void RendererRayTracingSystem::advanceCausticTemporalReuse(){
    if(causticTemporalDecay() <= 0.f){
        m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;
        return;
    }
    if(m_rayTracingState.m_causticTemporalReuseFrameCount < s_CausticTemporalWarmupFrameCount)
        m_rayTracingState.m_causticTemporalReuseFrameCount = m_rayTracingState.m_causticTemporalReuseFrameCount + 1u;
}

bool RendererRayTracingSystem::ensureCausticAccumulatorDecayPipeline(){
    if(m_rayTracingState.m_causticAccumulatorDecayPipeline)
        return true;
    if(m_rayTracingState.m_causticAccumulatorDecayPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: caustic accumulator decay requires the initialized global descriptor heap"));
        m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_causticAccumulatorDecayBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // The accumulator is heap-selected through push constants.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticAccumulatorDecayPushConstants)));

        m_rayTracingState.m_causticAccumulatorDecayBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_causticAccumulatorDecayBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay binding layout"));
            m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(
        m_rayTracingState.m_causticAccumulatorDecayShader,
        AssetsGraphicsCaustic::s_AccumulatorDecayShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CausticAccumulatorDecay"
    )){
        m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_causticAccumulatorDecayShader)
        .addBindingLayout(m_rayTracingState.m_causticAccumulatorDecayBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_causticAccumulatorDecayPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_causticAccumulatorDecayPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        m_rayTracingState.m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticRtPipeline(){
    const bool csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
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

    if(!m_rayTracingState.m_hwCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        m_rayTracingState.m_hwCausticBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_hwCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding layout"));
            failed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_shaderSystem.loadShader<RayGenerationShader>(raygenShader, AssetsGraphicsCaustic::s_HwRaygenShaderName, csg ? "NWB_RT_CSG_ENABLED=1" : "NWB_RT_CSG_ENABLED=0", "ECSRender_CausticHwRaygen")
        || !m_shaderSystem.loadShader<MissShader>(missShader, AssetsGraphicsCaustic::s_HwMissShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CausticHwMiss")
        || !m_shaderSystem.loadShader<ClosestHitShader>(closestHitShader, AssetsGraphicsCaustic::s_HwClosestHitShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CausticHwClosestHit")
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
    raygenDesc.setShader(raygenShader).setExportName(RayTracingCausticsTaskDetail::s_HwRaygenExportName);
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc;
    missDesc.setShader(missShader).setExportName(RayTracingCausticsTaskDetail::s_HwMissExportName);
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc;
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName(RayTracingCausticsTaskDetail::s_HwHitGroupExportName);
    pipelineDesc.addHitGroup(hitGroupDesc);

    Core::RayTracingPipelineHandle pipeline = device.createRayTracingPipeline(pipelineDesc);
    if(!pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic pipeline"));
        failed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = pipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic shader table"));
        failed = true;
        return false;
    }
    if(
        !shaderTable->setRayGenerationShader(RayTracingCausticsTaskDetail::s_HwRaygenExportName)
        || shaderTable->addMissShader(RayTracingCausticsTaskDetail::s_HwMissExportName) != 0u
        || shaderTable->addHitGroup(RayTracingCausticsTaskDetail::s_HwHitGroupExportName) != 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to populate RT caustic shader table"));
        failed = true;
        return false;
    }

    cachedPipeline = Move(pipeline);
    cachedTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT caustic pipeline + shader table"));
    return true;
}

bool RendererRayTracingSystem::hasHwCausticWork()const noexcept{
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return hasHwCausticWork(meshView);
}

bool RendererRayTracingSystem::hasHwCausticWork(const ECSRenderDetail::MeshViewBufferSnapshot& meshView)const noexcept{
    // Hardware photons require a caustic light, refractor, TLAS, and tracked mesh.
    return m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_rayTracingState.m_causticLightCount > 0u
        && m_rayTracingState.m_causticRefractiveInstanceCount > 0u
        && m_rayTracingState.m_tlas
        && m_rayTracingState.m_shadowMeshCount > 0u
        && m_rayTracingState.m_causticEmissionTargetBuffer
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        && m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        && m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
        && meshView.bindingValid()
    ;
}

bool RendererRayTracingSystem::prepareHwCausticResources(DeferredFrameTargets& targets){
    // Prepare hardware resources once geometry and emission targets exist.
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(
        m_rayTracingState.m_causticRefractiveInstanceCount == 0u
        || !m_rayTracingState.m_tlas
        || m_rayTracingState.m_shadowMeshCount == 0u
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
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware caustics require complete deferred bindless frame resources"));
        return false;
    }

    const bool producerReady = ensureRayTraceMaterialContextSlotsHeapHandle() && ensureCausticRtPipeline();
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

bool RendererRayTracingSystem::renderHwCaustics(
    Core::CommandList& commandList,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    DeferredFrameTargets& targets,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    // Hardware photons share the accumulator and resolve with the software reference.
    if(!hasHwCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    {
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        if(!heap.isInitialized() || !m_rayTracingState.m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch caustics without the descriptor-buffer TLAS heap handle"));
            return false;
        }
    }
    const f32 temporalDecay = causticTemporalDecay();
    const bool csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    const auto& pipeline = csg ? m_rayTracingState.m_hwCausticCsgPipeline : m_rayTracingState.m_hwCausticPipeline;
    const auto& shaderTable = csg ? m_rayTracingState.m_hwCausticCsgShaderTable : m_rayTracingState.m_hwCausticShaderTable;
    if(
        !pipeline
        || !shaderTable
        || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;
    const u32 temporalPhaseCount = causticTemporalPhaseCount();
    const CausticPhotonBudget photonBudget = MakeCausticPhotonBudget(m_causticQualitySettings, s_CausticHwPhotonGridSide, temporalPhaseCount);
    const u32 photonCount = photonBudget.photonsPerFrame;

    const auto recordPhotons = [&](){
        // Hardware and software producers use matching photon parameters.
        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = m_rayTracingState.m_tlasInstanceCount;
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
        pushConstants.gridSide = photonBudget.gridSide;
        // Same deterministic phase clock as the software producer above: the graphics frame index is
        // capture-anchored, while the dispatch count shifts with pipeline-warmup skips.
        pushConstants.frameIndex = static_cast<u32>(m_graphics.getFrameIndex());
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = m_rayTracingState.m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = meshView.heapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::RayTracingState rayTracingPassState;
        rayTracingPassState.setShaderTable(shaderTable.get());
        commandList.setRayTracingState(rayTracingPassState);
        // Bind heap blocks after RayTracingState; set 2 selects the TLAS generation.
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        heap.bindRayTracing(commandList, *pipeline.get(), m_rayTracingState.m_tlasHeapHandle);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

        Core::RayTracingDispatchRaysArguments dispatchArgs;
        dispatchArgs.setDimensions(photonBudget.gridSide, photonBudget.gridSide / temporalPhaseCount, 1u);
        commandList.dispatchRays(dispatchArgs);
        advanceCausticTemporalReuse();
    };
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            m_graphics.getDevice(),
            commandList
        );
        recordPhotons();
    }

    if(!m_rayTracingState.m_hwCausticDispatchLogged){
        m_rayTracingState.m_hwCausticDispatchLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched hardware caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(photonBudget.fullGridCount)
            , static_cast<u64>(m_rayTracingState.m_causticLightCount)
            , static_cast<u64>(m_rayTracingState.m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticEmissionTargetBuffer(usize targetCount){
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

    const usize capacity = ::NextGrowingCapacity(
        m_rayTracingState.m_causticEmissionTargetCapacity,
        targetCount,
        s_CausticEmissionTargetInitialCapacity
    );

    Core::BufferDesc targetBufferDesc;
    targetBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbCausticEmissionTargetGpu) * capacity))
        .setStructStride(sizeof(NwbCausticEmissionTargetGpu))
        .setDebugName(Name("caustic_emission_targets"))
        // The upload and asynchronous photon reads share this immutable per-frame input.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle targetBuffer = m_graphics.createBuffer(targetBufferDesc);
    if(!targetBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }

    const auto targetHeapHandle = acquireHeapHandle(*targetBuffer.get());
    if(!targetHeapHandle)
        return false;

    if(m_rayTracingState.m_causticEmissionTargetHeapHandle.valid())
        heap.free(m_rayTracingState.m_causticEmissionTargetHeapHandle);
    m_rayTracingState.m_causticEmissionTargetBuffer = Move(targetBuffer);
    m_rayTracingState.m_causticEmissionTargetHeapHandle = *targetHeapHandle;
    m_rayTracingState.m_causticEmissionTargetCapacity = capacity;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)")
        , static_cast<u64>(capacity)
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

