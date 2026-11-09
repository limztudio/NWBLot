// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/refractive_subsystem.adhd.h>
#include <impl/ecs_render/caustic/caustic_pipeline_set.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/caustic/graph_types.adhd.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <global/algorithm.h>
#include "caustic_emission_target_buffer.h"
#include "caustic_resolve_state.h"
#include "caustic_activity_resources.h"
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/caustic/caustic_emission_target_builder.adhd.h>
#include <impl/ecs_render/caustic/caustic_light_selector.adhd.h>
#include <impl/ecs_render/caustic/caustic_activity_resources.h>
#include <global/hash_utils.h>


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


b8 CausticEmissionTargetBuffer::ensure(Core::GraphicsRuntime& graphics, usize targetCount){
    // Replace the heap slot before retiring the old emission-target buffer.
    auto& device = graphics.getDevice();
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

    if(buffer && this->capacity >= targetCount){
        if(heapHandle.valid()){
            NWB_ASSERT(heapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer);
            return true;
        }
        const auto handle = acquireHeapHandle(*buffer.get());
        if(!handle)
            return false;
        heapHandle = *handle;
        return true;
    }

    const usize capacity = ::NextGrowingCapacity(this->capacity, targetCount, s_CausticEmissionTargetInitialCapacity);

    Core::BufferDesc targetBufferDesc;
    targetBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbCausticEmissionTargetGpu) * capacity))
        .setStructStride(sizeof(NwbCausticEmissionTargetGpu))
        .setDebugName(Name(s_RenderNameCausticEmissionTargets))
        // The upload and asynchronous photon reads share this immutable per-frame input.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto targetBufferResult = graphics.createBuffer(targetBufferDesc);
    if(!targetBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }
    Core::BufferHandle targetBuffer = Move(*targetBufferResult);

    const auto targetHeapHandle = acquireHeapHandle(*targetBuffer.get());
    if(!targetHeapHandle)
        return false;

    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, *targetHeapHandle);
    if(heapHandle.valid())
        heap.free(heapHandle);
    buffer = Move(targetBuffer);
    heapHandle = *targetHeapHandle;
    this->capacity = capacity;
    retireUnpublished.release();
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)"), static_cast<u64>(capacity));
    return true;
}

void CausticEmissionTargetBuffer::releaseDescriptor(Core::GraphicsRuntime& graphics){
    if(!heapHandle.valid())
        return;

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized())
        heap.free(heapHandle);
    heapHandle = Core::GpuDescriptorHandle::Invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CausticResolveActivitySnapshot::valid()const noexcept{
    if(halfWidth == 0u || halfHeight == 0u)
        return false;
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        if(!buffers[index] || !descriptors[index].valid() || descriptors[index].descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
            return false;
    }
    return true;
}

b8 CausticResolveActivitySnapshot::matches(const CausticResolveActivitySnapshot& other)const noexcept{
    if(halfWidth != other.halfWidth || halfHeight != other.halfHeight)
        return false;
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        if(buffers[index] != other.buffers[index] || descriptors[index] != other.descriptors[index])
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CausticResolveActivitySnapshot RendererRayTracingSystem::causticResolveActivitySnapshot(const DeferredFrameTargets& targets)const noexcept{
    const auto& activity = m_rayTracingState.m_causticResolve.m_activity;
    if(!activity.valid() || activity.halfWidth != DivideUp(targets.width, s_CausticResolveDownscaleFactor) || activity.halfHeight != DivideUp(targets.height, s_CausticResolveDownscaleFactor))
        return {};
    return activity;
}

b8 CausticActivityResources::prepareCausticResolveActivity(const u32 halfWidth, const u32 halfHeight){
    const auto& current = m_activity;
    if(current.valid() && current.halfWidth == halfWidth && current.halfHeight == halfHeight)
        return true;
    releaseCausticResolveActivity();
    const Expected<CausticResolveActivityLayout> layout = TryResolveCausticActivityLayout(halfWidth, halfHeight);
    if(!layout)
        return false;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    auto& activity = m_activity;
    for(u32 index = 0u; index < LengthOf(activity.buffers); ++index){
        Core::BufferDesc desc;
        desc.setByteSize(layout->bufferByteSize)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
            .setDebugName(s_CausticResolveActivityBufferNames[index]);
        auto buffersResult = m_graphics.createBuffer(desc);
        if(buffersResult)
            activity.buffers[index] = Move(*buffersResult);
        else
            activity.buffers[index].reset();
        if(!buffersResult){
            releaseCausticResolveActivity();
            return false;
        }
        if(!RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, activity.buffers[index].get()), activity.descriptors[index])){
            releaseCausticResolveActivity();
            return false;
        }
    }
    activity.halfWidth = halfWidth;
    activity.halfHeight = halfHeight;
    return true;
}

void CausticActivityResources::releaseCausticResolveActivity(){
    auto& activity = m_activity;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        for(const Core::GpuDescriptorHandle descriptor : activity.descriptors){
            if(descriptor.valid())
                heap.free(descriptor);
        }
    }
    activity = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingCausticsTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EnableCausticResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
}

void DispatchCausticResolvePass(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    Core::ComputePipeline& pipeline,
    DeferredFrameTargets& targets,
    const CausticResolvePassResources& input,
    const CausticResolvePassResources& output,
    const f32 effectiveIntensity,
    const u32 stepWidth,
    const CausticResolveStage::Enum stage,
    const u32 groupsX,
    const u32 groupsY,
    const CausticResolveActivitySnapshot& activity
){
    NWB_ASSERT(input.texture);
    NWB_ASSERT(output.texture);
    NWB_ASSERT(input.texture != output.texture);
    // Sequence indices match the graph: prepare, five wavelets, then upsample.
    const CausticResolveActivitySlots activitySlots = ResolveCausticActivitySlots(
        stage,
        static_cast<u32>(CountTrailingZeros(stepWidth)),
        NWB_CAUSTIC_RESOLVE_PASS_COUNT,
        activity.valid(),
        activity.descriptors[0u].slot(),
        activity.descriptors[1u].slot()
    );
    const CausticResolvePushConstants resolvePush = ResolveCausticResolvePushConstants(
        targets.width,
        targets.height,
        effectiveIntensity,
        stepWidth,
        stage,
        targets.bindless.gbufferWorldPosition.slot(),
        targets.bindless.gbufferDepth.slot(),
        input.sampledSlot,
        targets.bindless.causticResolveGeometry.slot(),
        targets.bindless.causticAccumulator.slot(),
        output.storageSlot,
        activitySlots.inputSlot,
        activitySlots.outputSlot
    );

    Core::ComputeState computeState;
    computeState.setPipeline(&pipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, pipeline);
    commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
    commandList.dispatch(groupsX, groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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


void RendererRayTracingSystem::dispatchCausticGeometryDownsample(Core::CommandList& commandList, DeferredFrameTargets& targets){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // The graph declares the writable geometry cache and its later sampled handoff.
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);

    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, s_CausticResolveDownscaleFactor, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfWidth = groups.width;
    const u32 halfHeight = groups.height;

    const CausticGeometryDownsamplePushConstants geometryPush = MakeCausticGeometryDownsamplePushConstants(
        targets.width,
        targets.height,
        halfWidth,
        halfHeight,
        targets.bindless.gbufferWorldPosition.slot(),
        targets.bindless.gbufferDepth.slot(),
        targets.bindless.causticResolveGeometryStorage.slot()
    );

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
    if(!targets.causticAccumulator || !targets.causticIrradiance || !meshView.buffer || !meshView.heapHandle.valid() || !m_rayTracingState.m_causticEmissionTargets.heapHandle.valid())
        return true;
    if(meshView.heapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer || m_rayTracingState.m_causticEmissionTargets.heapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer){
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

    CausticPipelineSet pipelines(m_arena, m_graphics, m_shaderSystem, m_rayTracingState, m_lightSpaceShadow.m_csg.snapshot.hasCsg);
    const b8 producerReady = RayTraceMaterialResources(m_graphics, m_rayTracingState, m_rayTracingState).ensureRayTraceMaterialContextSlotsHeapHandle() && (hardware ? pipelines.ensureCausticRtPipeline() : pipelines.ensureSwCausticPipeline());
    // Prepare shared resolve and temporal resources even when the producer is unavailable.
    const b8 resolveReady = pipelines.ensureCausticGeometryDownsamplePipeline() && pipelines.ensureCausticResolvePipeline();
    const b8 temporalReady = pipelines.ensureCausticAccumulatorDecayPipeline();
    return producerReady && resolveReady && temporalReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 ResolveCausticLights(const Span<const f32> causticImportance, const u32 refractiveInstanceCount, Span<SceneLightGpuData> lights)noexcept{
    return CausticLightSelector().assignSlots(causticImportance, refractiveInstanceCount, lights);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::releaseCausticEmissionTargetHeapHandle(){
    m_rayTracingState.m_causticEmissionTargets.releaseDescriptor(m_graphics);
}

b8 RendererRayTracingSystem::createCausticTargets(DeferredFrameTargets& targets){
    // Full-res additive targets with half-res temporal resolve ping-pong.
    targets.causticIrradianceFormat = Core::Format::RGBA16_FLOAT;
    targets.causticAccumulatorFormat = Core::Format::R32_UINT;
    targets.causticHistoryFormat = Core::Format::RGBA16_FLOAT;

    // Fresh accumulators reseed the temporal EMA.
    m_rayTracingState.m_causticAccumulatorInitialized = false;

    const u32 halfWidth = DivideUpWrapping(targets.width, 2u);
    const u32 halfHeight = DivideUpWrapping(targets.height, 2u);

    Core::TextureDesc irradianceDesc;
    irradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setIsUAV(true)
        // Graphics/async production and the graph-owned lagged-history transfer copy share this target concurrently.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setName("engine/caustic/irradiance")
    ;
    auto causticIrradianceResult = m_graphics.createTexture(irradianceDesc);
    if(causticIrradianceResult)
        targets.causticIrradiance = Move(*causticIrradianceResult);
    else
        targets.causticIrradiance.reset();
    if(!causticIrradianceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic irradiance target"));
        return false;
    }

    Core::TextureDesc accumulatorDesc;
    accumulatorDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(s_CausticAccumulatorChannelCount)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.causticAccumulatorFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/accumulator")
    ;
    auto causticAccumulatorResult = m_graphics.createTexture(accumulatorDesc);
    if(causticAccumulatorResult)
        targets.causticAccumulator = Move(*causticAccumulatorResult);
    else
        targets.causticAccumulator.reset();
    if(!causticAccumulatorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator target"));
        return false;
    }

    Core::TextureDesc historyDesc;
    historyDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/atrous_half_a");
    auto causticHistoryResult = m_graphics.createTexture(historyDesc);
    if(causticHistoryResult)
        targets.causticHistory = Move(*causticHistoryResult);
    else
        targets.causticHistory.reset();
    if(!causticHistoryResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-A target"));
        return false;
    }

    Core::TextureDesc halfBDesc;
    halfBDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/atrous_half_b");
    auto causticResolveHalfResult = m_graphics.createTexture(halfBDesc);
    if(causticResolveHalfResult)
        targets.causticResolveHalf = Move(*causticResolveHalfResult);
    else
        targets.causticResolveHalf.reset();
    if(!causticResolveHalfResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-B target"));
        return false;
    }

    // Half-resolution geometry cache for edge-aware resolve.
    Core::TextureDesc geometryDesc;
    geometryDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/resolve_geometry");
    auto causticResolveGeometryResult = m_graphics.createTexture(geometryDesc);
    if(causticResolveGeometryResult)
        targets.causticResolveGeometry = Move(*causticResolveGeometryResult);
    else
        targets.causticResolveGeometry.reset();
    if(!causticResolveGeometryResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve geometry cache target"));
        return false;
    }
    CausticActivityResources activity(m_graphics, m_rayTracingState.m_causticResolve.m_activity);
    if(!activity.prepareCausticResolveActivity(halfWidth, halfHeight))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic activity buffers unavailable; retaining full wavelet filtering"));
    return true;
}

Expected<Optional<Core::GpuUploadBlobId>> RendererRayTracingSystem::retainPreparedCausticEmissionTargetUpload(Core::GpuTaskGraph& graph)const{
    const u32 targetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
    if(targetCount == 0u){
        if(!m_preparedCausticEmissionTargetBytes.empty())
            return MakeUnexpected(Failure{});
        return Optional<Core::GpuUploadBlobId>{};
    }

    usize targetByteCount = 0u;
    if(!CausticEmissionTargetBuilder().validateUpload(
        targetCount,
        m_preparedCausticEmissionTargetBytes.size(),
        static_cast<b8>(m_rayTracingState.m_causticEmissionTargets.buffer),
        m_rayTracingState.m_causticEmissionTargets.capacity,
        m_rayTracingState.m_causticEmissionTargets.heapHandle.valid(),
        m_rayTracingState.m_causticEmissionTargets.heapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer,
        targetByteCount
    ))
        return MakeUnexpected(Failure{});

    const auto blob = graph.copyUploadData(m_preparedCausticEmissionTargetBytes.data(), targetByteCount, alignof(NwbCausticEmissionTargetGpu));
    if(!blob)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blob};
}

b8 RendererRayTracingSystem::prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena){
    // Emission targets are refractive world bounds. Freeze bytes here; graph declaration keeps only this snapshot.
    m_preparedCausticEmissionTargetBytes.clear();
    m_rayTracingState.m_causticRefractiveInstanceCount = 0u;
    m_unsupportedCausticVolumeCount = 0u;
    m_unsupportedCausticVolumeHash = s_Fnv64OffsetBasis;

    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return true;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    Vector<NwbCausticEmissionTargetGpu, Core::Alloc::ScratchArena> targets{ scratchArena };
    targets.reserve(candidateCount);

    SIMDVectorSingle combinedMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVectorSingle combinedMax = VectorReplicate(-s_RayTracingFiniteInfinity);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        const auto materialInfoResult = m_materialSystem.findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
        if(!materialInfo->refractive)
            continue;

        const auto meshResult = RayTracingDetail::ResolveRenderableMeshResources(*meshSystemPtr, m_meshSystem, entity);
        if(!meshResult)
            continue;
        CausticEmissionTargetBuilder().recordUnsupportedVolume(entity.id, renderer.opticalBoundaryMode, m_firstUnsupportedCausticVolumeEntity, m_unsupportedCausticVolumeCount, m_unsupportedCausticVolumeHash);
        if(!meshResult->meshResources.csgLocalBounds.valid())
            continue;

        const NWB::Impl::Scene::TransformComponent* transformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transformPtr ? MatrixAffineTransformation(LoadFloat(transformPtr->scale), VectorZero(), LoadFloat(transformPtr->rotation), LoadFloat(transformPtr->position)) : MatrixIdentity();

        SIMDVectorSingle localMin = LoadFloatInt(meshResult->meshResources.csgLocalBounds.minBounds);
        SIMDVectorSingle localMax = LoadFloatInt(meshResult->meshResources.csgLocalBounds.maxBounds);
        CausticEmissionTargetBuilder().resolveLocalBounds(meshResult->resolvedMesh.runtime, localMin, localMax);
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(!worldBounds)
            continue;
        targets.push_back(CausticEmissionTargetBuilder().makeTarget(worldBounds->minBounds, worldBounds->maxBounds, combinedMin, combinedMax));
    }

    const u32 targetCount = static_cast<u32>(targets.size());
    if(targetCount == 0u){
        m_rayTracingState.m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
        m_rayTracingState.m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
        m_rayTracingState.m_causticRefractiveInstanceCount = 0u;
        return true;
    }

    if(!m_rayTracingState.m_causticEmissionTargets.ensure(m_graphics, targetCount))
        return false;

    CausticEmissionTargetBuilder().captureBytes(
        Span<const NwbCausticEmissionTargetGpu>(targets.data(), targets.size()),
        m_preparedCausticEmissionTargetBytes,
        m_rayTracingState.m_causticRefractiveInstanceCount
    );
    StoreFloat(combinedMin, m_rayTracingState.m_causticTargetBoundsMin);
    StoreFloat(combinedMax, m_rayTracingState.m_causticTargetBoundsMax);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_caustics_gpu_render{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CausticPhotonPushConstants MakeCausticPhotonPushConstants(
    const RendererRayTracingState& state,
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const CausticPhotonBudget& photonBudget,
    const u32 instanceCount,
    const u32 frameIndex,
    const u32 temporalPhaseCount
)noexcept{
    return ResolveCausticPhotonPushConstants(
        targets.width,
        targets.height,
        instanceCount,
        photonBudget,
        state.m_causticRefractiveInstanceCount,
        frameIndex,
        targets.bindless.gbufferDepth.slot(),
        targets.bindless.gbufferWorldPosition.slot(),
        state.m_causticEmissionTargets.heapHandle.slot(),
        meshView.heapHandle.slot(),
        targets.bindless.slotsBufferDescriptor.slot(),
        state.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        targets.bindless.causticAccumulatorStorage.slot(),
        temporalPhaseCount
    );
}

template<typename RecordFn>
static void RecordCausticPhotonPass(
    Core::GraphicsRuntime& graphics,
    const RendererRayTracingState& state,
    const CausticPhotonBudget& photonBudget,
    const u32 temporalPhaseCount,
    const AStringView routeLabel,
    Core::CommandList& commandList,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    b8& dispatchLogged,
    RecordFn&& recordPhotons
){
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics.getDevice(), commandList);

        recordPhotons();
    }

    LogCausticProducerDispatchIfNeeded(
        routeLabel,
        photonBudget,
        temporalPhaseCount,
        state.m_causticLightCount,
        state.m_causticRefractiveInstanceCount,
        dispatchLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderGpuBvhCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* const causticPhotonTiming){
    // Software photon producer runs before deferred lighting.
    if(!hasCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    const auto& pipeline = m_lightSpaceShadow.m_csg.snapshot.hasCsg ? m_rayTracingState.m_swCausticCsgPipeline : m_rayTracingState.m_swCausticPipeline;
    if(!pipeline || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid() || !causticResolveResourcesReady(targets))
        return false;
    const u32 temporalPhaseCount = s_CausticTemporalPhaseCount;
    const CausticPhotonBudget photonBudget = MakeCausticPhotonBudget(m_causticQualitySettings, NWB_CAUSTIC_SW_GRID_SIDE, temporalPhaseCount);

    __hidden_rt_caustics_gpu_render::RecordCausticPhotonPass(
        m_graphics,
        m_rayTracingState,
        photonBudget,
        temporalPhaseCount,
        "software",
        commandList,
        causticPhotonTiming,
        m_rayTracingState.m_swCausticDispatchLogged,
        [&](){
            const CausticPhotonPushConstants pushConstants = __hidden_rt_caustics_gpu_render::MakeCausticPhotonPushConstants(
                m_rayTracingState,
                targets,
                meshView,
                photonBudget,
                m_rayTracingState.m_sceneBvhInstanceCount,
                static_cast<u32>(m_graphics.getFrameIndex()),
                temporalPhaseCount
            );
            Core::ComputeState computeState;
            computeState.setPipeline(pipeline.get());
            commandList.setComputeState(computeState);
            Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
            NWB_ASSERT(heap.isInitialized());
            heap.bindCompute(commandList, *pipeline.get());
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
            commandList.dispatch(DivideUp(photonBudget.photonsPerFrame, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        });
    return true;
}

b8 RendererRayTracingSystem::renderHwCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* const causticPhotonTiming){
    // Hardware photons share the accumulator and resolve with the software reference.
    if(!hasHwCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized() || !m_rayTracingState.m_tlasHeapHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch caustics without the descriptor-buffer TLAS heap handle"));
        return false;
    }
    const b8 csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    const auto& pipeline = csg ? m_rayTracingState.m_hwCausticCsgPipeline : m_rayTracingState.m_hwCausticPipeline;
    const auto& shaderTable = csg ? m_rayTracingState.m_hwCausticCsgShaderTable : m_rayTracingState.m_hwCausticShaderTable;
    if(!pipeline || !shaderTable || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid() || !causticResolveResourcesReady(targets))
        return false;
    const u32 temporalPhaseCount = s_CausticTemporalPhaseCount;
    const CausticPhotonBudget photonBudget = MakeCausticPhotonBudget(m_causticQualitySettings, s_CausticHwPhotonGridSide, temporalPhaseCount);

    __hidden_rt_caustics_gpu_render::RecordCausticPhotonPass(
        m_graphics,
        m_rayTracingState,
        photonBudget,
        temporalPhaseCount,
        "hardware",
        commandList,
        causticPhotonTiming,
        m_rayTracingState.m_hwCausticDispatchLogged,
        [&](){
            const CausticPhotonPushConstants pushConstants = __hidden_rt_caustics_gpu_render::MakeCausticPhotonPushConstants(
                m_rayTracingState,
                targets,
                meshView,
                photonBudget,
                m_rayTracingState.m_tlasInstanceCount,
                static_cast<u32>(m_graphics.getFrameIndex()),
                temporalPhaseCount
            );
            Core::RayTracingState rayTracingPassState;
            rayTracingPassState.setShaderTable(shaderTable.get());
            commandList.setRayTracingState(rayTracingPassState);
            // Bind heap blocks after RayTracingState; set 2 selects the TLAS generation.
            heap.bindRayTracing(commandList, *pipeline.get(), m_rayTracingState.m_tlasHeapHandle);
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

            Core::RayTracingDispatchRaysArguments dispatchArgs;
            dispatchArgs.setDimensions(photonBudget.gridSide, photonBudget.gridSide / temporalPhaseCount, 1u);
            commandList.dispatchRays(dispatchArgs);
        });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::dispatchCausticAccumulatorDecay(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticAccumulator || !targets.bindless.causticAccumulatorStorage.valid() || !m_rayTracingState.m_causticAccumulatorDecayPipeline)
        return false;

    // The task declares the accumulator write and the photon producer dependency.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);

    const CausticAccumulatorDecayPushConstants decayPush = MakeCausticAccumulatorDecayPushConstants(
        targets.width,
        targets.height,
        s_DefaultCausticTemporalDecay,
        targets.bindless.causticAccumulatorStorage.slot()
    );

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
    if(m_rayTracingState.m_causticRefractiveInstanceCount == 0u || m_rayTracingState.m_sceneBvhInstanceCount == 0u || m_rayTracingState.m_swShadowMeshCount == 0u || !m_rayTracingState.m_causticEmissionTargets.buffer)
        return true;
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return prepareCausticResources(meshView, false, targets);
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
    const RayTracingDispatchGroups groups = ResolveRayTracingDispatchGroups(targets.width, targets.height, s_CausticResolveDownscaleFactor, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 effectiveIntensity = EffectiveCausticIntensity(s_DefaultCausticTemporalDecay);
    // Alternate the prepare output and its counterpart while doubling the wavelet sampling distance.
    const b8 inputIsHalfB = ((static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) + passIndex) % 2u) == 0u;
    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfA = RayTracingCausticsTaskDetail::MakeCausticHistoryResolvePassResources(targets);
    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfB = RayTracingCausticsTaskDetail::MakeCausticHalfResolvePassResources(targets);
    const u32 stepWidth = 1u << passIndex;
    const CausticResolveState& resolve = m_rayTracingState.m_causticResolve;
    const CausticResolveStageState& wavelet =
        stepWidth == s_CausticWaveletStepOneWidth ? resolve.m_waveletStepOne
        : stepWidth == s_CausticWaveletStepTwoWidth ? resolve.m_waveletStepTwo
        : stepWidth > NWB_CAUSTIC_RESOLVE_LDS_MAX_STEP ? resolve.m_waveletDirect
        : resolve.m_wavelet;
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
    RayTracingCausticsTaskDetail::EnableCausticResolveBarriers(commandList, targets);
    commandList.setEnableUavBarriersForTexture(targets.causticIrradiance.get(), true);

    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    // Normalize temporal accumulation to retain non-temporal brightness.
    const f32 effectiveIntensity = EffectiveCausticIntensity(s_DefaultCausticTemporalDecay);

    const RayTracingCausticsTaskDetail::CausticResolvePassResources halfB = RayTracingCausticsTaskDetail::MakeCausticHalfResolvePassResources(targets);
    const RayTracingCausticsTaskDetail::CausticResolvePassResources irradiance{
        targets.causticIrradiance.get(),
        targets.bindless.causticIrradiance.slot(),
        targets.bindless.causticIrradianceStorage.slot(),
    };
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


b8 RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets)const noexcept{
    return
        m_rayTracingState.m_causticResolve.m_prepare.m_pipeline
        && m_rayTracingState.m_causticResolve.m_wavelet.m_pipeline
        && m_rayTracingState.m_causticResolve.m_waveletStepOne.m_pipeline
        && m_rayTracingState.m_causticResolve.m_waveletStepTwo.m_pipeline
        && m_rayTracingState.m_causticResolve.m_waveletDirect.m_pipeline
        && m_rayTracingState.m_causticResolve.m_upsample.m_pipeline
        && m_rayTracingState.m_causticGeometryDownsamplePipeline
        && m_rayTracingState.m_causticAccumulatorDecayPipeline
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
