// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelSpawnPipeline(){
    if(m_rayTracingState.m_surfelSpawnPipeline)
        return true;
    if(m_rayTracingState.m_surfelSpawnPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel spawn requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelSpawnPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelSpawnBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelSpawnBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelSpawnBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn binding layout"));
            m_rayTracingState.m_surfelSpawnPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelSpawnShader,
        AssetsGraphicsGi::s_SurfelSpawnShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelSpawn"
    )){
        m_rayTracingState.m_surfelSpawnPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelSpawnShader)
        .addBindingLayout(m_rayTracingState.m_surfelSpawnBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_surfelSpawnPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelSpawnPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn compute pipeline"));
        m_rayTracingState.m_surfelSpawnPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel spawn compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelAgeFreePipeline(){
    if(m_rayTracingState.m_surfelAgeFreePipeline)
        return true;
    if(m_rayTracingState.m_surfelAgeFreePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel age-free requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelAgeFreeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelAgeFreeBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelAgeFreeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free binding layout"));
            m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelAgeFreeShader,
        AssetsGraphicsGi::s_SurfelAgeFreeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelAgeFree"
    )){
        m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelAgeFreeShader)
        .addBindingLayout(m_rayTracingState.m_surfelAgeFreeBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_surfelAgeFreePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelAgeFreePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free compute pipeline"));
        m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel age-free compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelHashBuildPipeline(){
    if(m_rayTracingState.m_surfelHashBuildPipeline)
        return true;
    if(m_rayTracingState.m_surfelHashBuildPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel hash-build requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelHashBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelHashBuildBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelHashBuildBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build binding layout"));
            m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelHashBuildShader,
        AssetsGraphicsGi::s_SurfelHashBuildShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelHashBuild"
    )){
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelHashBuildShader)
        .addBindingLayout(m_rayTracingState.m_surfelHashBuildBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_surfelHashBuildPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelHashBuildPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build compute pipeline"));
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel hash-build compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelTracePipeline(){
    if(m_rayTracingState.m_surfelTracePipeline)
        return true;
    if(m_rayTracingState.m_surfelTracePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelTracePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelTraceBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelTraceBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace binding layout"));
            m_rayTracingState.m_surfelTracePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelTraceShader,
        AssetsGraphicsGi::s_SurfelTraceShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelTrace"
    )){
        m_rayTracingState.m_surfelTracePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelTraceShader)
        .addBindingLayout(m_rayTracingState.m_surfelTraceBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_surfelTracePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelTracePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace compute pipeline"));
        m_rayTracingState.m_surfelTracePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel trace compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelResources(){
    // Persistent field storage survives window resizes.
    if(!hasSurfelWork())
        return true;

    const u32 poolCapacity = m_rayTracingState.m_surfelPoolCapacity;
    const u32 cellCount = m_rayTracingState.m_surfelHashCellCount;
    if(poolCapacity == 0u || cellCount == 0u)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI requires the initialized global descriptor heap"));
        return false;
    }

    // Fresh pool storage needs one clear before tracing.
    if(!m_rayTracingState.m_surfelPoolBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity)
            .setStructStride(NWB_SURFEL_RECORD_SIZE)
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_pool"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelPoolBuffer = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelPoolBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel pool buffer"));
            return false;
        }
        m_rayTracingState.m_surfelSeeded = false;
        m_rayTracingState.m_surfelResourcesNeedClear = true;
    }

    // Hash heads use 0xFFFFFFFF as the empty sentinel.
    if(!m_rayTracingState.m_surfelCellHeadBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * cellCount)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_cell_head"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelCellHeadBuffer = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelCellHeadBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel cell-head buffer"));
            return false;
        }
        m_rayTracingState.m_surfelResourcesNeedClear = true;
    }

    if(!m_rayTracingState.m_surfelCounterBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            // The persistent counter is written by GI on Compute and may be copied by the late diagnostic readback on Transfer before the next Compute frame imports its accepted tail state.
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_counter"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelCounterBuffer = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelCounterBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel counter buffer"));
            return false;
        }
        m_rayTracingState.m_surfelResourcesNeedClear = true;
    }

    // Build-args rewrites the indirect dispatch buffer each frame.
    if(!m_rayTracingState.m_surfelTraceIndirectArgsBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_TRACE_INDIRECT_ARGS_WORD_COUNT)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("surfel_trace_indirect_args"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelTraceIndirectArgsBuffer = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelTraceIndirectArgsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace indirect-args buffer"));
            return false;
        }
    }

    // Age-free pushes ids and spawn pops them from this persistent free list.
    if(!m_rayTracingState.m_surfelFreeListBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * poolCapacity)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("surfel_free_list"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelFreeListBuffer = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelFreeListBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel free-list buffer"));
            return false;
        }
        m_rayTracingState.m_surfelResourcesNeedClear = true;
    }

    // Bounce gathers read a stable previous-frame pool snapshot.
    if(!m_rayTracingState.m_surfelPoolSnapshotBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity)
            .setStructStride(NWB_SURFEL_RECORD_SIZE)
            .setCanHaveUAVs(false)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_pool_snapshot"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelPoolSnapshotBuffer = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelPoolSnapshotBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel pool snapshot buffer"));
            return false;
        }
    }

    // Snapshot hash heads alongside the pool for a consistent bounce gather.
    if(!m_rayTracingState.m_surfelCellHeadSnapshotBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * cellCount)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(false)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_cell_head_snapshot"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelCellHeadSnapshotBuffer = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelCellHeadSnapshotBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel cell-head snapshot buffer"));
            return false;
        }
    }

    if(!m_rayTracingState.m_surfelCounterReadback){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE)
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_counter_readback"))
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        m_rayTracingState.m_surfelCounterReadback = m_graphics.createBuffer(desc);
        if(!m_rayTracingState.m_surfelCounterReadback){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel counter readback buffer"));
            return false;
        }
    }

    // Every surfel pass binds this per-frame parameter buffer as a ConstantBuffer.
    if(!m_rayTracingState.m_surfelConstants){
        Core::BufferDesc cbDesc;
        cbDesc
            .setByteSize(sizeof(NwbSurfelConstantsGpu))
            .setIsConstantBuffer(true)
            // Graphics upload and async consume share this selector.
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("surfel_constants"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        m_rayTracingState.m_surfelConstants = m_graphics.createBuffer(cbDesc);
        if(!m_rayTracingState.m_surfelConstants){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel constant buffer"));
            return false;
        }
    }

    // Persistent descriptors own backing resources until deferred retirement.
    if(
        !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelConstants.get(), Core::GpuDescriptorClass::UniformBuffer, false, m_rayTracingState.m_surfelConstantsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelPoolBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_surfelPoolHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelCellHeadBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_surfelCellHeadHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_surfelCounterHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelFreeListBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_surfelFreeListHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelPoolSnapshotBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, false, m_rayTracingState.m_surfelPoolSnapshotHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, false, m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register persistent surfel resources in the descriptor heap"));
        return false;
    }

    // Only tracing differs between hardware and software surfel paths.
    const bool traceReady = m_rayTracingState.m_surfelUseHwTrace ? ensureSurfelTraceHwPipeline() : ensureSurfelTracePipeline();
    if(!ensureSurfelSpawnPipeline() || !ensureSurfelAgeFreePipeline() || !ensureSurfelHashBuildPipeline() || !traceReady || !ensureSurfelResolvePipeline() || !ensureSurfelUpsamplePipeline() || !ensureSurfelTraceBuildArgsPipeline())
        return false;
    return true;
}

// Hardware trace twin; other surfel passes are shared.
bool RendererRayTracingSystem::ensureSurfelTraceHwPipeline(){
    if(m_rayTracingState.m_surfelTraceHwPipeline)
        return true;
    if(m_rayTracingState.m_surfelTraceHwPipelineFailed)
        return false;

    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) || !m_graphics.queryFeatureSupport(Core::Feature::RayQuery)){
        m_rayTracingState.m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel HW trace requires the descriptor-buffer TLAS heap layout"));
        m_rayTracingState.m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceHwBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelTraceHwBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelTraceHwBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace binding layout"));
            m_rayTracingState.m_surfelTraceHwPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelTraceHwShader,
        AssetsGraphicsGi::s_SurfelTraceHwShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_SurfelTraceHw"
    )){
        m_rayTracingState.m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelTraceHwShader)
        .addBindingLayout(m_rayTracingState.m_surfelTraceHwBindingLayout)
    ;
    // Preserve resource, sampler, and TLAS heap sets for hardware trace.
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    m_rayTracingState.m_surfelTraceHwPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelTraceHwPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace compute pipeline"));
        m_rayTracingState.m_surfelTraceHwPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel HW trace compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelResolvePipeline(){
    if(m_rayTracingState.m_surfelResolvePipeline)
        return true;
    if(m_rayTracingState.m_surfelResolvePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel resolve requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelResolvePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelResolveBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve binding layout"));
            m_rayTracingState.m_surfelResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelResolveShader,
        AssetsGraphicsGi::s_SurfelResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelResolve"
    )){
        m_rayTracingState.m_surfelResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelResolveShader)
        .addBindingLayout(m_rayTracingState.m_surfelResolveBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_surfelResolvePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve compute pipeline"));
        m_rayTracingState.m_surfelResolvePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel resolve compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelUpsamplePipeline(){
    if(m_rayTracingState.m_surfelUpsamplePipeline)
        return true;
    if(m_rayTracingState.m_surfelUpsamplePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel upsample requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelUpsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelUpsampleBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelUpsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample binding layout"));
            m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelUpsampleShader,
        AssetsGraphicsGi::s_SurfelUpsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelUpsample"
    )){
        m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelUpsampleShader)
        .addBindingLayout(m_rayTracingState.m_surfelUpsampleBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_surfelUpsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelUpsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample compute pipeline"));
        m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel upsample compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelTraceBuildArgsPipeline(){
    if(m_rayTracingState.m_surfelTraceBuildArgsPipeline)
        return true;
    if(m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace build-args requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceBuildArgsBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        m_rayTracingState.m_surfelTraceBuildArgsBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_surfelTraceBuildArgsBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args binding layout"));
            m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_surfelTraceBuildArgsShader,
        AssetsGraphicsGi::s_SurfelTraceBuildArgsShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelTraceBuildArgs"
    )){
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_surfelTraceBuildArgsShader)
        .addBindingLayout(m_rayTracingState.m_surfelTraceBuildArgsBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_surfelTraceBuildArgsPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_surfelTraceBuildArgsPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args compute pipeline"));
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel trace build-args compute pipeline"));
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

