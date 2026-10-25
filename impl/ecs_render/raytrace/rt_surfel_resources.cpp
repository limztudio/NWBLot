// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/gi/quality_settings.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>

#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::createSurfelTargets(DeferredFrameTargets& targets){
    const u32 factor = targets.surfelResolveFactor;
    const SurfelGiResolveSize resolveSize = MakeSurfelGiResolveSize(targets.width, targets.height, factor);
    if(resolveSize.width == 0u || resolveSize.height == 0u)
        return false;

    // Deferred lighting samples resolved surfel GI, never the writable pool.
    targets.surfelIrradianceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc surfelIrradianceDesc;
    surfelIrradianceDesc.setWidth(targets.width).setHeight(targets.height).setFormat(targets.surfelIrradianceFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setName("engine/gi/surfel_irradiance");
    auto surfelIrradianceResult = m_graphics.createTexture(surfelIrradianceDesc);
    if(surfelIrradianceResult)
        targets.surfelIrradiance = Move(*surfelIrradianceResult);
    else
        targets.surfelIrradiance.reset();
    if(!surfelIrradianceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel irradiance target"));
        return false;
    }

    // Transient reduced-resolution surfel resolve input.
    Core::TextureDesc surfelIrradianceHalfDesc;
    surfelIrradianceHalfDesc.setWidth(resolveSize.width).setHeight(resolveSize.height).setFormat(targets.surfelIrradianceFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/gi/surfel_irradiance_half");
    auto surfelIrradianceHalfResult = m_graphics.createTexture(surfelIrradianceHalfDesc);
    if(surfelIrradianceHalfResult)
        targets.surfelIrradianceHalf = Move(*surfelIrradianceHalfResult);
    else
        targets.surfelIrradianceHalf.reset();
    if(!surfelIrradianceHalfResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel reduced-resolution irradiance target"));
        return false;
    }

    m_rayTracingState.m_surfelResolveDispatchLogged = false;
    return true;
}

Expected<Optional<Core::GpuUploadBlobId>> RendererRayTracingSystem::retainPreparedSurfelFrameConstantsUpload(Core::GpuTaskGraph& graph, const DeferredFrameTargets& targets)const{
    if(!hasSurfelWork())
        return Optional<Core::GpuUploadBlobId>{};
    if(!m_rayTracingState.m_surfelConstants){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI has no preflighted constant buffer"));
        return MakeUnexpected(Failure{});
    }

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(m_rayTracingState, targets);
    const auto blob = graph.copyUploadData(&params, sizeof(params), alignof(NwbSurfelConstantsGpu));
    if(!blob)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blob};
}

void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelConstantsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelGuidePoolHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCounterHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelFreeListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolSnapshotHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle);
        return;
    }

    m_rayTracingState.m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelGuidePoolHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::Invalid();
}

bool RendererRayTracingSystem::hasSurfelWork()const noexcept{
    return m_rayTracingState.m_surfelEnabled;
}

bool RendererRayTracingSystem::needsSurfelResourceInitialization()const noexcept{
    return hasSurfelWork() && m_rayTracingState.m_surfelResourcesNeedClear;
}

bool RendererRayTracingSystem::recordSurfelResourceInitializationLifecycle()noexcept{
    if(!m_rayTracingState.m_surfelResourcesNeedClear)
        return false;

    m_rayTracingState.m_surfelResourcesClearPending = true;
    return true;
}

void RendererRayTracingSystem::finalizeSurfelResourceInitialization()noexcept{
    if(!m_rayTracingState.m_surfelResourcesClearPending)
        return;

    m_rayTracingState.m_surfelResourcesClearPending = false;
    m_rayTracingState.m_surfelResourcesNeedClear = false;
}

void RendererRayTracingSystem::discardSurfelResourceInitialization()noexcept{
    // Keep the clear pending until a producer succeeds.
    m_rayTracingState.m_surfelResourcesClearPending = false;
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

bool RendererRayTracingSystem::shouldCaptureSurfelCountReadback()const noexcept{
    return hasSurfelWork() && m_rayTracingState.m_surfelCounterBuffer && m_rayTracingState.m_surfelCounterReadback && !m_rayTracingState.m_surfelCountReadbackSubmissionToken.valid() && (m_rayTracingState.m_surfelFrameIndex % s_SurfelCountLogInterval) == 0u;
}

void RendererRayTracingSystem::markSurfelCountReadbackScheduled()noexcept{
    m_rayTracingState.m_surfelCountReadbackFrame = m_rayTracingState.m_surfelFrameIndex;
}

bool RendererRayTracingSystem::prepareSurfelResources(DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    m_rayTracingState.m_surfelUseCsgTrace = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    if(!ensureSurfelResources())
        return false;

    // GI consumes the same selector descriptor as the other current ray effects.
    if(!targets.bindless.valid() || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer) || !ensureRayTraceMaterialContextSlotsHeapHandle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI trace heap context is incomplete"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

