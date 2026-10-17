// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>

#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        auto surfelPoolBufferResult = m_graphics.createBuffer(desc);
        if(surfelPoolBufferResult)
            m_rayTracingState.m_surfelPoolBuffer = Move(*surfelPoolBufferResult);
        else
            m_rayTracingState.m_surfelPoolBuffer.reset();
        if(!surfelPoolBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel pool buffer"));
            return false;
        }
        m_rayTracingState.m_surfelSeeded = false;
        m_rayTracingState.m_surfelResourcesNeedClear = true;
    }

    // Four float4 guide lanes per surfel; this live-only buffer is never needed by bounce gathers.
    if(!m_rayTracingState.m_surfelGuidePoolBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(NWB_SURFEL_GUIDE_FLOAT4_STRIDE) * NWB_SURFEL_GUIDE_FLOAT4S_PER_SURFEL * poolCapacity)
            .setStructStride(NWB_SURFEL_GUIDE_FLOAT4_STRIDE)
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_guide_pool"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto surfelGuidePoolBufferResult = m_graphics.createBuffer(desc);
        if(surfelGuidePoolBufferResult)
            m_rayTracingState.m_surfelGuidePoolBuffer = Move(*surfelGuidePoolBufferResult);
        else
            m_rayTracingState.m_surfelGuidePoolBuffer.reset();
        if(!surfelGuidePoolBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel guide pool buffer"));
            return false;
        }
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
        auto surfelCellHeadBufferResult = m_graphics.createBuffer(desc);
        if(surfelCellHeadBufferResult)
            m_rayTracingState.m_surfelCellHeadBuffer = Move(*surfelCellHeadBufferResult);
        else
            m_rayTracingState.m_surfelCellHeadBuffer.reset();
        if(!surfelCellHeadBufferResult){
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
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setDebugName(Name("surfel_counter")).enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto surfelCounterBufferResult = m_graphics.createBuffer(desc);
        if(surfelCounterBufferResult)
            m_rayTracingState.m_surfelCounterBuffer = Move(*surfelCounterBufferResult);
        else
            m_rayTracingState.m_surfelCounterBuffer.reset();
        if(!surfelCounterBufferResult){
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
        auto surfelTraceIndirectArgsBufferResult = m_graphics.createBuffer(desc);
        if(surfelTraceIndirectArgsBufferResult)
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer = Move(*surfelTraceIndirectArgsBufferResult);
        else
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer.reset();
        if(!surfelTraceIndirectArgsBufferResult){
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
        auto surfelFreeListBufferResult = m_graphics.createBuffer(desc);
        if(surfelFreeListBufferResult)
            m_rayTracingState.m_surfelFreeListBuffer = Move(*surfelFreeListBufferResult);
        else
            m_rayTracingState.m_surfelFreeListBuffer.reset();
        if(!surfelFreeListBufferResult){
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
        auto surfelPoolSnapshotBufferResult = m_graphics.createBuffer(desc);
        if(surfelPoolSnapshotBufferResult)
            m_rayTracingState.m_surfelPoolSnapshotBuffer = Move(*surfelPoolSnapshotBufferResult);
        else
            m_rayTracingState.m_surfelPoolSnapshotBuffer.reset();
        if(!surfelPoolSnapshotBufferResult){
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
        auto surfelCellHeadSnapshotBufferResult = m_graphics.createBuffer(desc);
        if(surfelCellHeadSnapshotBufferResult)
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer = Move(*surfelCellHeadSnapshotBufferResult);
        else
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer.reset();
        if(!surfelCellHeadSnapshotBufferResult){
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
        auto surfelCounterReadbackResult = m_graphics.createBuffer(desc);
        if(surfelCounterReadbackResult)
            m_rayTracingState.m_surfelCounterReadback = Move(*surfelCounterReadbackResult);
        else
            m_rayTracingState.m_surfelCounterReadback.reset();
        if(!surfelCounterReadbackResult){
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
            // The upload and asynchronous consumers share this selector.
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(Name("surfel_constants")).enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto surfelConstantsResult = m_graphics.createBuffer(cbDesc);
        if(surfelConstantsResult)
            m_rayTracingState.m_surfelConstants = Move(*surfelConstantsResult);
        else
            m_rayTracingState.m_surfelConstants.reset();
        if(!surfelConstantsResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel constant buffer"));
            return false;
        }
    }

    // Persistent descriptors own backing resources until deferred retirement.
    if(
        !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelConstants.get(), Core::GpuDescriptorClass::UniformBuffer, false, m_rayTracingState.m_surfelConstantsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelPoolBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_surfelPoolHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(heap, *m_rayTracingState.m_surfelGuidePoolBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, m_rayTracingState.m_surfelGuidePoolHeapHandle)
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
        auto surfelSpawnBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelSpawnBindingLayoutResult)
            m_rayTracingState.m_surfelSpawnBindingLayout = Move(*surfelSpawnBindingLayoutResult);
        else
            m_rayTracingState.m_surfelSpawnBindingLayout.reset();
        if(!surfelSpawnBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn binding layout"));
            m_rayTracingState.m_surfelSpawnPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelSpawnShader, AssetsGraphicsGi::s_SurfelSpawnShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelSpawn")){
        m_rayTracingState.m_surfelSpawnPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelSpawnShader).addBindingLayout(m_rayTracingState.m_surfelSpawnBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelSpawnPipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelSpawnPipelineResult)
        m_rayTracingState.m_surfelSpawnPipeline = Move(*surfelSpawnPipelineResult);
    else
        m_rayTracingState.m_surfelSpawnPipeline.reset();
    if(!surfelSpawnPipelineResult){
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
        auto surfelAgeFreeBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelAgeFreeBindingLayoutResult)
            m_rayTracingState.m_surfelAgeFreeBindingLayout = Move(*surfelAgeFreeBindingLayoutResult);
        else
            m_rayTracingState.m_surfelAgeFreeBindingLayout.reset();
        if(!surfelAgeFreeBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free binding layout"));
            m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelAgeFreeShader, AssetsGraphicsGi::s_SurfelAgeFreeShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelAgeFree")){
        m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelAgeFreeShader).addBindingLayout(m_rayTracingState.m_surfelAgeFreeBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelAgeFreePipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelAgeFreePipelineResult)
        m_rayTracingState.m_surfelAgeFreePipeline = Move(*surfelAgeFreePipelineResult);
    else
        m_rayTracingState.m_surfelAgeFreePipeline.reset();
    if(!surfelAgeFreePipelineResult){
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
        auto surfelHashBuildBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelHashBuildBindingLayoutResult)
            m_rayTracingState.m_surfelHashBuildBindingLayout = Move(*surfelHashBuildBindingLayoutResult);
        else
            m_rayTracingState.m_surfelHashBuildBindingLayout.reset();
        if(!surfelHashBuildBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build binding layout"));
            m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelHashBuildShader, AssetsGraphicsGi::s_SurfelHashBuildShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelHashBuild")){
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelHashBuildShader).addBindingLayout(m_rayTracingState.m_surfelHashBuildBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelHashBuildPipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelHashBuildPipelineResult)
        m_rayTracingState.m_surfelHashBuildPipeline = Move(*surfelHashBuildPipelineResult);
    else
        m_rayTracingState.m_surfelHashBuildPipeline.reset();
    if(!surfelHashBuildPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build compute pipeline"));
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel hash-build compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelTracePipeline(){
    const bool csg = m_rayTracingState.m_surfelUseCsgTrace;
    auto& pipeline = csg ? m_rayTracingState.m_surfelTraceCsgPipeline : m_rayTracingState.m_surfelTracePipeline;
    auto& shader = csg ? m_rayTracingState.m_surfelTraceCsgShader : m_rayTracingState.m_surfelTraceShader;
    auto& failed = csg ? m_rayTracingState.m_surfelTraceCsgPipelineFailed : m_rayTracingState.m_surfelTracePipelineFailed;

    if(pipeline)
        return true;
    if(failed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace requires the initialized global descriptor heap"));
        failed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelTraceBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelTraceBindingLayoutResult)
            m_rayTracingState.m_surfelTraceBindingLayout = Move(*surfelTraceBindingLayoutResult);
        else
            m_rayTracingState.m_surfelTraceBindingLayout.reset();
        if(!surfelTraceBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace binding layout"));
            failed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(shader, AssetsGraphicsGi::s_SurfelTraceShaderName, csg ? AStringView("NWB_GI_CSG_ENABLED=1") : AStringView("NWB_GI_CSG_ENABLED=0"), "ECSRender_SurfelTrace")){
        failed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader).addBindingLayout(m_rayTracingState.m_surfelTraceBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto pipelineResult = device.createComputePipeline(pipelineDesc);
    if(pipelineResult)
        pipeline = Move(*pipelineResult);
    else
        pipeline.reset();
    if(!pipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace compute pipeline"));
        failed = true;
        return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: created surfel trace compute pipeline"));
    return true;
}

// Hardware trace twin; other surfel passes are shared.
bool RendererRayTracingSystem::ensureSurfelTraceHwPipeline(){
    const bool csg = m_rayTracingState.m_surfelUseCsgTrace;
    auto& pipeline = csg ? m_rayTracingState.m_surfelTraceCsgHwPipeline : m_rayTracingState.m_surfelTraceHwPipeline;
    auto& shader = csg ? m_rayTracingState.m_surfelTraceCsgHwShader : m_rayTracingState.m_surfelTraceHwShader;
    auto& failed = csg ? m_rayTracingState.m_surfelTraceCsgHwPipelineFailed : m_rayTracingState.m_surfelTraceHwPipelineFailed;

    if(pipeline)
        return true;
    if(failed)
        return false;

    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) || !m_graphics.queryFeatureSupport(Core::Feature::RayQuery)){
        failed = true;
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel HW trace requires the descriptor-buffer TLAS heap layout"));
        failed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceHwBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelTraceHwBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelTraceHwBindingLayoutResult)
            m_rayTracingState.m_surfelTraceHwBindingLayout = Move(*surfelTraceHwBindingLayoutResult);
        else
            m_rayTracingState.m_surfelTraceHwBindingLayout.reset();
        if(!surfelTraceHwBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace binding layout"));
            failed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(shader, AssetsGraphicsGi::s_SurfelTraceHwShaderName, csg ? AStringView("NWB_GI_CSG_ENABLED=1") : AStringView("NWB_GI_CSG_ENABLED=0"), "ECSRender_SurfelTraceHw")){
        failed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader).addBindingLayout(m_rayTracingState.m_surfelTraceHwBindingLayout);
    // Preserve resource, sampler, and TLAS heap sets for hardware trace.
    pipelineDesc.addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout()).addBindingLayout(heap.getAccelStructLayout());
    auto pipelineResult = device.createComputePipeline(pipelineDesc);
    if(pipelineResult)
        pipeline = Move(*pipelineResult);
    else
        pipeline.reset();
    if(!pipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace compute pipeline"));
        failed = true;
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
        auto surfelResolveBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelResolveBindingLayoutResult)
            m_rayTracingState.m_surfelResolveBindingLayout = Move(*surfelResolveBindingLayoutResult);
        else
            m_rayTracingState.m_surfelResolveBindingLayout.reset();
        if(!surfelResolveBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve binding layout"));
            m_rayTracingState.m_surfelResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelResolveShader, AssetsGraphicsGi::s_SurfelResolveShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelResolve")){
        m_rayTracingState.m_surfelResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelResolveShader).addBindingLayout(m_rayTracingState.m_surfelResolveBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelResolvePipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelResolvePipelineResult)
        m_rayTracingState.m_surfelResolvePipeline = Move(*surfelResolvePipelineResult);
    else
        m_rayTracingState.m_surfelResolvePipeline.reset();
    if(!surfelResolvePipelineResult){
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
        auto surfelUpsampleBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelUpsampleBindingLayoutResult)
            m_rayTracingState.m_surfelUpsampleBindingLayout = Move(*surfelUpsampleBindingLayoutResult);
        else
            m_rayTracingState.m_surfelUpsampleBindingLayout.reset();
        if(!surfelUpsampleBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample binding layout"));
            m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelUpsampleShader, AssetsGraphicsGi::s_SurfelUpsampleShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelUpsample")){
        m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelUpsampleShader).addBindingLayout(m_rayTracingState.m_surfelUpsampleBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelUpsamplePipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelUpsamplePipelineResult)
        m_rayTracingState.m_surfelUpsamplePipeline = Move(*surfelUpsamplePipelineResult);
    else
        m_rayTracingState.m_surfelUpsamplePipeline.reset();
    if(!surfelUpsamplePipelineResult){
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
        auto surfelTraceBuildArgsBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelTraceBuildArgsBindingLayoutResult)
            m_rayTracingState.m_surfelTraceBuildArgsBindingLayout = Move(*surfelTraceBuildArgsBindingLayoutResult);
        else
            m_rayTracingState.m_surfelTraceBuildArgsBindingLayout.reset();
        if(!surfelTraceBuildArgsBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args binding layout"));
            m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelTraceBuildArgsShader, AssetsGraphicsGi::s_SurfelTraceBuildArgsShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelTraceBuildArgs")){
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelTraceBuildArgsShader).addBindingLayout(m_rayTracingState.m_surfelTraceBuildArgsBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelTraceBuildArgsPipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelTraceBuildArgsPipelineResult)
        m_rayTracingState.m_surfelTraceBuildArgsPipeline = Move(*surfelTraceBuildArgsPipelineResult);
    else
        m_rayTracingState.m_surfelTraceBuildArgsPipeline.reset();
    if(!surfelTraceBuildArgsPipelineResult){
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

