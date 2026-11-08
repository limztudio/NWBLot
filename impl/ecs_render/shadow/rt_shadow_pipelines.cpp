// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/shadow_subsystem.adhd.h>
#include <impl/ecs_render/shadow/shadow_pipeline_set.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shadow/types.adhd.h>
#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPipelineSet::ensureShadowPipeline(){
    if(m_shadow.m_shadowPipeline)
        return true;
    if(m_shadow.m_shadowPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    // Trace layouts are push-only; resources come from the global heap.
    static_assert(sizeof(ShadowRqSoftPushConstants) >= sizeof(ShadowRqPushConstants), "shadow-trace push-constant range must cover both the hard and soft trace push structs");
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(ShadowRqSoftPushConstants), Core::ShaderType::Compute, m_shadow.m_shadowBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowRayqueryCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_ShadowRayQuery", m_shadow.m_shadowShader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_shadow.m_shadowShader, m_shadow.m_shadowBindingLayout, true, m_shadow.m_shadowPipeline)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        m_shadow.m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
    return true;
}

b8 ShadowPipelineSet::ensureShadowSoftPipeline(){
    if(m_softShadow.m_shadowSoftPipeline)
        return true;
    if(m_softShadow.m_shadowSoftPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    // Soft and hard traces share their push-only layout.
    if(!m_shadow.m_shadowBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow binding layout missing for the soft RayQuery pipeline"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowRayquerySoftCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_ShadowRayQuerySoft", m_softShadow.m_shadowSoftShader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_softShadow.m_shadowSoftShader, m_shadow.m_shadowBindingLayout, true, m_softShadow.m_shadowSoftPipeline)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery soft shadow compute pipeline"));
        m_softShadow.m_shadowSoftPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery soft shadow compute pipeline"));
    return true;
}

b8 ShadowPipelineSet::ensureSwShadowPipeline(){
    if(m_shadow.m_swShadowPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software shadows require the initialized global descriptor heap"));
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }

    if(!m_shadow.m_swShadowBindingLayout){
        if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(SwShadowHeapPushConstants), Core::ShaderType::Compute, m_shadow.m_swShadowBindingLayout)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }

        // Compaction uses a persistent counter and UAV-writable indirect-args buffer.
        Core::BufferDesc edgeCounterDesc;
        edgeCounterDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_COUNTER_SIZE))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name(s_SoftwareShadowEdgeCounter))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto swShadowEdgeCounterBufferResult = m_graphics.createBuffer(edgeCounterDesc);
        if(swShadowEdgeCounterBufferResult)
            m_shadow.m_swShadowEdgeCounterBuffer = Move(*swShadowEdgeCounterBufferResult);
        else
            m_shadow.m_swShadowEdgeCounterBuffer.reset();
        if(!swShadowEdgeCounterBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-counter buffer"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc indirectArgsDesc;
        indirectArgsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name(s_SoftwareShadowIndirectArgs))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto swShadowIndirectArgsBufferResult = m_graphics.createBuffer(indirectArgsDesc);
        if(swShadowIndirectArgsBufferResult)
            m_shadow.m_swShadowIndirectArgsBuffer = Move(*swShadowIndirectArgsBufferResult);
        else
            m_shadow.m_swShadowIndirectArgsBuffer.reset();
        if(!swShadowIndirectArgsBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow indirect-args buffer"));
            m_shadow.m_swShadowPipelineFailed = true;
            return false;
        }
    }

    const b8 heapResourcesReady =
        RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_shadow.m_swShadowEdgeCounterBuffer.get(), m_shadow.m_swShadowEdgeCounterHeapHandle)
        && RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_shadow.m_swShadowIndirectArgsBuffer.get(), m_shadow.m_swShadowIndirectArgsHeapHandle)
    ;
    if(!heapResourcesReady){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-shadow work buffers in the descriptor heap"));
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }

    const b8 passesReady =
        ensureSwShadowPassPipeline(m_shadow.m_swShadowOpaquePrepassShader, m_shadow.m_swShadowOpaquePrepassPipeline, &ShadowSubsystem::swShadowOpaquePrepassCs, "ECSRender_SwShadowOpaquePrepass")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowSoftOpaqueShader, m_shadow.m_swShadowSoftOpaquePipeline, &ShadowSubsystem::swShadowSoftOpaqueCs, "ECSRender_SwShadowSoftOpaque")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentCoarseShader, m_shadow.m_swShadowTransparentCoarsePipeline, &ShadowSubsystem::swShadowTransparentCoarseCs, "ECSRender_SwShadowTransparentCoarse")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentResolveShader, m_shadow.m_swShadowTransparentResolvePipeline, &ShadowSubsystem::swShadowTransparentResolveCs, "ECSRender_SwShadowTransparentResolve")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentClassifyShader, m_shadow.m_swShadowTransparentClassifyPipeline, &ShadowSubsystem::swShadowTransparentClassifyCs, "ECSRender_SwShadowTransparentClassify")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentBuildArgsShader, m_shadow.m_swShadowTransparentBuildArgsPipeline, &ShadowSubsystem::swShadowTransparentBuildargsCs, "ECSRender_SwShadowTransparentBuildArgs")
        && ensureSwShadowPassPipeline(m_shadow.m_swShadowTransparentIndirectShader, m_shadow.m_swShadowTransparentIndirectPipeline, &ShadowSubsystem::swShadowTransparentIndirectCs, "ECSRender_SwShadowTransparentIndirect")
        && ensureSoftwareTransparentSamplingPipeline()
    ;
    if(!passesReady){
        m_shadow.m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

b8 ShadowPipelineSet::ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::* shaderMember, const NameText<char> debugLabel){
    if(pipeline)
        return true;

    if(!m_shaderSystem.loadShader<ComputeShader>(shaderMember, Core::ShaderArchive::s_DefaultVariant, Name(debugLabel), shader))
        return false;
    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(m_graphics.getDevice(), shader, m_shadow.m_swShadowBindingLayout, false, pipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        return false;
    }
    return true;
}

b8 RayTraceMaterialResources::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // CPU-uploaded material context is shared by the exclusive HW/SW backends.
    if(m_shadow.m_shadowInstanceMaterialBuffer && m_shadow.m_shadowInstanceMaterialCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceMaterialBuffer.get(), m_shadow.m_shadowInstanceMaterialHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowInstanceMaterialCapacity, instanceCount, s_ShadowInstanceMaterialInitialCapacity);

    Core::BufferDesc materialBufferDesc;
    materialBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbRtInstanceMaterialGpu) * capacity))
        .setStructStride(sizeof(NwbRtInstanceMaterialGpu))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceInstanceMaterial))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto materialBufferResult = m_graphics.createBuffer(materialBufferDesc);
    if(!materialBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    Core::BufferHandle materialBuffer = Move(*materialBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*materialBuffer.get(), m_shadow.m_shadowInstanceMaterialHeapHandle))
        return false;
    m_shadow.m_shadowInstanceMaterialBuffer = Move(materialBuffer);
    m_shadow.m_shadowInstanceMaterialCapacity = capacity;
    return true;
}

b8 RayTraceMaterialResources::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow tracing needs an instance record for every gathered occluder.
    if(instanceCount == 0u)
        return !m_shadow.m_shadowInstanceBuffer || ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle);
    if(m_shadow.m_shadowInstanceBuffer && m_shadow.m_shadowInstanceCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowInstanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceInstanceContext))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto instanceBufferResult = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    Core::BufferHandle instanceBuffer = Move(*instanceBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), m_shadow.m_shadowInstanceHeapHandle))
        return false;
    m_shadow.m_shadowInstanceBuffer = Move(instanceBuffer);
    m_shadow.m_shadowInstanceCapacity = capacity;
    return true;
}

b8 RayTraceMaterialResources::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Keep one word so the heap binding remains valid with no transparent occluders.
    const usize requiredByteCount = ResolveMaterialTypedBufferByteCount(byteCount, sizeof(u32));
    if(m_shadow.m_shadowMaterialTypedBuffer && m_shadow.m_shadowMaterialTypedCapacity >= requiredByteCount)
        return ensureRayTraceMaterialContextHeapHandle(*m_shadow.m_shadowMaterialTypedBuffer.get(), m_shadow.m_shadowMaterialTypedHeapHandle);

    const usize capacity = ::NextGrowingCapacity(m_shadow.m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_ShadowResourceMaterialTyped))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto materialTypedBufferResult = m_graphics.createBuffer(materialTypedBufferDesc);
    if(!materialTypedBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    Core::BufferHandle materialTypedBuffer = Move(*materialTypedBufferResult);
    if(!replaceRayTraceMaterialContextHeapHandle(*materialTypedBuffer.get(), m_shadow.m_shadowMaterialTypedHeapHandle))
        return false;
    m_shadow.m_shadowMaterialTypedBuffer = Move(materialTypedBuffer);
    m_shadow.m_shadowMaterialTypedCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

