// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_shadow_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowPipeline(){
    if(m_rayTracingState.m_shadowPipeline)
        return true;
    if(m_rayTracingState.m_shadowPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_rayTracingState.m_shadowPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_rayTracingState.m_shadowPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        appendShadowTraceBindingLayout(layoutDesc);

        m_rayTracingState.m_shadowBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_shadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
            m_rayTracingState.m_shadowPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowShader,
        AssetsGraphicsShadow::s_RayQueryShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_ShadowRayQuery"
    )){
        m_rayTracingState.m_shadowPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowShader)
        .addBindingLayout(m_rayTracingState.m_shadowBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    m_rayTracingState.m_shadowPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        m_rayTracingState.m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureShadowSoftPipeline(){
    if(m_rayTracingState.m_shadowSoftPipeline)
        return true;
    if(m_rayTracingState.m_shadowSoftPipelineFailed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayQuery) || !m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        m_rayTracingState.m_shadowSoftPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft RayQuery shadows require the descriptor-buffer TLAS heap layout"));
        m_rayTracingState.m_shadowSoftPipelineFailed = true;
        return false;
    }

    // Soft and hard traces share their push-only layout.
    if(!m_rayTracingState.m_shadowBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow binding layout missing for the soft RayQuery pipeline"));
        m_rayTracingState.m_shadowSoftPipelineFailed = true;
        return false;
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowSoftShader,
        AssetsGraphicsShadow::s_RayQuerySoftShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_ShadowRayQuerySoft"
    )){
        m_rayTracingState.m_shadowSoftPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowSoftShader)
        .addBindingLayout(m_rayTracingState.m_shadowBindingLayout)
    ;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    m_rayTracingState.m_shadowSoftPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowSoftPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery soft shadow compute pipeline"));
        m_rayTracingState.m_shadowSoftPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery soft shadow compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPipeline(){
    if(m_rayTracingState.m_swShadowPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software shadows require the initialized global descriptor heap"));
        m_rayTracingState.m_swShadowPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_swShadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // All pass resources are selected through the fixed push ABI.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SwShadowHeapPushConstants)));

        m_rayTracingState.m_swShadowBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_swShadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            m_rayTracingState.m_swShadowPipelineFailed = true;
            return false;
        }


        // Compaction uses a persistent counter and UAV-writable indirect-args buffer.
        Core::BufferDesc edgeCounterDesc;
        edgeCounterDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_COUNTER_SIZE))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("sw_shadow_edge_counter"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_swShadowEdgeCounterBuffer = m_graphics.createBuffer(edgeCounterDesc);
        if(!m_rayTracingState.m_swShadowEdgeCounterBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-counter buffer"));
            m_rayTracingState.m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc indirectArgsDesc;
        indirectArgsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("sw_shadow_indirect_args"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_swShadowIndirectArgsBuffer = m_graphics.createBuffer(indirectArgsDesc);
        if(!m_rayTracingState.m_swShadowIndirectArgsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow indirect-args buffer"));
            m_rayTracingState.m_swShadowPipelineFailed = true;
            return false;
        }
    }

    const bool heapResourcesReady =
        RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_swShadowEdgeCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_swShadowEdgeCounterHeapHandle)
        && RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_swShadowIndirectArgsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_swShadowIndirectArgsHeapHandle)
    ;
    if(!heapResourcesReady){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register software-shadow work buffers in the descriptor heap"));
        m_rayTracingState.m_swShadowPipelineFailed = true;
        return false;
    }

    const bool passesReady =
        ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowOpaquePrepassShader, m_rayTracingState.m_swShadowOpaquePrepassPipeline, AssetsGraphicsShadow::s_SwOpaquePrepassShaderName, MakeNotNull("ECSRender_SwShadowOpaquePrepass"))
        && ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowSoftOpaqueShader, m_rayTracingState.m_swShadowSoftOpaquePipeline, AssetsGraphicsShadow::s_SwSoftOpaqueShaderName, MakeNotNull("ECSRender_SwShadowSoftOpaque"))
        && ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowTransparentCoarseShader, m_rayTracingState.m_swShadowTransparentCoarsePipeline, AssetsGraphicsShadow::s_SwTransparentCoarseShaderName, MakeNotNull("ECSRender_SwShadowTransparentCoarse"))
        && ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowTransparentResolveShader, m_rayTracingState.m_swShadowTransparentResolvePipeline, AssetsGraphicsShadow::s_SwTransparentResolveShaderName, MakeNotNull("ECSRender_SwShadowTransparentResolve"))
        && ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowTransparentClassifyShader, m_rayTracingState.m_swShadowTransparentClassifyPipeline, AssetsGraphicsShadow::s_SwTransparentClassifyShaderName, MakeNotNull("ECSRender_SwShadowTransparentClassify"))
        && ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowTransparentBuildArgsShader, m_rayTracingState.m_swShadowTransparentBuildArgsPipeline, AssetsGraphicsShadow::s_SwTransparentBuildArgsShaderName, MakeNotNull("ECSRender_SwShadowTransparentBuildArgs"))
        && ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowTransparentIndirectShader, m_rayTracingState.m_swShadowTransparentIndirectPipeline, AssetsGraphicsShadow::s_SwTransparentIndirectShaderName, MakeNotNull("ECSRender_SwShadowTransparentIndirect"))
        && ensureSwShadowPassPipeline(m_rayTracingState.m_swShadowTransparentUniformShader, m_rayTracingState.m_swShadowTransparentUniformPipeline, AssetsGraphicsShadow::s_SwTransparentUniformShaderName, MakeNotNull("ECSRender_SwShadowTransparentUniform"))
        && ensureSoftwareTransparentSamplingPipeline()
    ;
    if(!passesReady){
        m_rayTracingState.m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const NotNull<const char*> debugLabel){
    if(pipeline)
        return true;

    if(!m_shaderSystem.loadShader(
        shader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        debugLabel.get()
    ))
        return false;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(m_rayTracingState.m_swShadowBindingLayout)
    ;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    pipeline = m_graphics.getDevice().createComputePipeline(pipelineDesc);
    if(!pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // CPU-uploaded material context is shared by the exclusive HW/SW backends.
    if(m_rayTracingState.m_shadowInstanceMaterialBuffer && m_rayTracingState.m_shadowInstanceMaterialCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *m_rayTracingState.m_shadowInstanceMaterialBuffer.get(),
            m_rayTracingState.m_shadowInstanceMaterialHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(
        m_rayTracingState.m_shadowInstanceMaterialCapacity,
        instanceCount,
        s_ShadowInstanceMaterialInitialCapacity
    );

    Core::BufferDesc materialBufferDesc;
    materialBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbRtInstanceMaterialGpu) * capacity))
        .setStructStride(sizeof(NwbRtInstanceMaterialGpu))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("shadow_instance_material"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialBuffer = m_graphics.createBuffer(materialBufferDesc);
    if(!materialBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*materialBuffer.get(), m_rayTracingState.m_shadowInstanceMaterialHeapHandle))
        return false;
    m_rayTracingState.m_shadowInstanceMaterialBuffer = Move(materialBuffer);
    m_rayTracingState.m_shadowInstanceMaterialCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow tracing needs an instance record for every gathered occluder.
    if(instanceCount == 0u)
        return !m_rayTracingState.m_shadowInstanceBuffer || ensureRayTraceMaterialContextHeapHandle(
            *m_rayTracingState.m_shadowInstanceBuffer.get(),
            m_rayTracingState.m_shadowInstanceHeapHandle
        );
    if(m_rayTracingState.m_shadowInstanceBuffer && m_rayTracingState.m_shadowInstanceCapacity >= instanceCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *m_rayTracingState.m_shadowInstanceBuffer.get(),
            m_rayTracingState.m_shadowInstanceHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("shadow_instance_context"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), m_rayTracingState.m_shadowInstanceHeapHandle))
        return false;
    m_rayTracingState.m_shadowInstanceBuffer = Move(instanceBuffer);
    m_rayTracingState.m_shadowInstanceCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Keep one word so the heap binding remains valid with no transparent occluders.
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
    if(m_rayTracingState.m_shadowMaterialTypedBuffer && m_rayTracingState.m_shadowMaterialTypedCapacity >= requiredByteCount)
        return ensureRayTraceMaterialContextHeapHandle(
            *m_rayTracingState.m_shadowMaterialTypedBuffer.get(),
            m_rayTracingState.m_shadowMaterialTypedHeapHandle
        );

    const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("shadow_material_typed"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialTypedBuffer = m_graphics.createBuffer(materialTypedBufferDesc);
    if(!materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    if(!replaceRayTraceMaterialContextHeapHandle(*materialTypedBuffer.get(), m_rayTracingState.m_shadowMaterialTypedHeapHandle))
        return false;
    m_rayTracingState.m_shadowMaterialTypedBuffer = Move(materialTypedBuffer);
    m_rayTracingState.m_shadowMaterialTypedCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

