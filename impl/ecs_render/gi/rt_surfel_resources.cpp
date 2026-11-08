// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/gi/quality_settings.adhd.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/gi/surfel_gi_pipeline_set.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::createSurfelTargets(DeferredFrameTargets& targets){
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

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(
        m_rayTracingState.m_surfelSeeded,
        m_rayTracingState.m_surfelHashCellCount,
        m_rayTracingState.m_surfelPoolCapacity,
        m_rayTracingState.m_surfelFrameIndex,
        targets.width,
        targets.height,
        targets.surfelResolveFactor
    );
    const auto blob = graph.copyUploadData(&params, sizeof(params), alignof(NwbSurfelConstantsGpu));
    if(!blob)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blob};
}

void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelConstantsHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelGuidePoolHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCounterHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelFreeListHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolSnapshotHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle);
}

RayTracingSurfelPersistentResourceSnapshot RendererRayTracingSystem::snapshotSurfelPersistentResources()const noexcept{
    return RayTracingSurfelPersistentResourceSnapshot{
        .constantsBuffer = m_rayTracingState.m_surfelConstants,
        .poolBuffer = m_rayTracingState.m_surfelPoolBuffer,
        .guidePoolBuffer = m_rayTracingState.m_surfelGuidePoolBuffer,
        .cellHeadBuffer = m_rayTracingState.m_surfelCellHeadBuffer,
        .counterBuffer = m_rayTracingState.m_surfelCounterBuffer,
        .traceIndirectArgsBuffer = m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
        .freeListBuffer = m_rayTracingState.m_surfelFreeListBuffer,
        .poolSnapshotBuffer = m_rayTracingState.m_surfelPoolSnapshotBuffer,
        .cellHeadSnapshotBuffer = m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
        .counterReadbackBuffer = m_rayTracingState.m_surfelCounterReadback,
        .countReadbackSubmissionToken = m_rayTracingState.m_surfelCountReadbackSubmissionToken,
    };
}

b8 RendererRayTracingSystem::surfelCountReadbackSubmissionMatches(const Core::QueueSubmissionToken& submissionToken)const noexcept{
    const Core::QueueSubmissionToken& accepted = m_rayTracingState.m_surfelCountReadbackSubmissionToken;
    return accepted.valid() && submissionToken.valid() && accepted.queue == submissionToken.queue && accepted.value == submissionToken.value && accepted.matchesPhysicalQueue(submissionToken.physicalQueueIndex, submissionToken.deviceGeneration);
}

void RendererRayTracingSystem::confirmSurfelCountReadbackSubmission(const Core::QueueSubmissionToken& submissionToken)noexcept{
    NWB_ASSERT(submissionToken.valid());
    m_rayTracingState.m_surfelCountReadbackSubmissionToken = submissionToken;
}

b8 RendererRayTracingSystem::recordSurfelResourceInitializationLifecycle()noexcept{
    return BeginSurfelResourceInitializationLifecycle(m_rayTracingState.m_surfelResourcesNeedClear, m_rayTracingState.m_surfelResourcesClearPending);
}

void RendererRayTracingSystem::finalizeSurfelResourceInitialization()noexcept{
    FinalizeSurfelResourceInitializationLifecycle(m_rayTracingState.m_surfelResourcesClearPending, m_rayTracingState.m_surfelResourcesNeedClear);
}

b8 RendererRayTracingSystem::ensureSurfelResources(){
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

    const auto ensureBuffer = [&](
        const Core::BufferDesc& desc,
        const tchar* const errorMessage,
        const b8 markNeedsClear,
        const b8 resetSeeded,
        Core::BufferHandle& inOutBuffer
    ){
        if(inOutBuffer)
            return true;
        auto bufferResult = m_graphics.createBuffer(desc);
        if(bufferResult)
            inOutBuffer = Move(*bufferResult);
        else{
            inOutBuffer.reset();
            NWB_LOGGER_ERROR(NWB_TEXT("{}"), errorMessage);
            return false;
        }
        if(resetSeeded)
            m_rayTracingState.m_surfelSeeded = false;
        if(markNeedsClear)
            m_rayTracingState.m_surfelResourcesNeedClear = true;
        return true;
    };
    const auto makeStructuredDesc = [](
        const u64 byteSize,
        const u32 stride,
        const b8 canHaveUavs,
        const b8 indirectArgs,
        const Core::ResourceQueueSharing::Mask queueSharing,
        const Name debugName
    ){
        Core::BufferDesc desc;
        desc
            .setByteSize(byteSize)
            .setStructStride(stride)
            .setCanHaveUAVs(canHaveUavs)
            .setIsDrawIndirectArgs(indirectArgs)
            .setQueueSharing(queueSharing)
            .setDebugName(debugName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        return desc;
    };

    Core::BufferDesc readbackDesc;
    readbackDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE)
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setDebugName(Name("surfel_counter_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferDesc constantsDesc;
    constantsDesc
        .setByteSize(sizeof(NwbSurfelConstantsGpu))
        .setIsConstantBuffer(true)
        // The upload and asynchronous consumers share this selector.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("surfel_constants"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    if(
        // Fresh pool storage needs one clear before tracing.
        !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity, NWB_SURFEL_RECORD_SIZE, true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name("surfel_pool")),
            NWB_TEXT("RendererSystem: failed to create surfel pool buffer"),
            true,
            true,
            m_rayTracingState.m_surfelPoolBuffer
        )
        // Four float4 guide lanes per surfel; this live-only buffer is never needed by bounce gathers.
        || !ensureBuffer(
            makeStructuredDesc(
                static_cast<u64>(NWB_SURFEL_GUIDE_FLOAT4_STRIDE) * NWB_SURFEL_GUIDE_FLOAT4S_PER_SURFEL * poolCapacity,
                NWB_SURFEL_GUIDE_FLOAT4_STRIDE, true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name("surfel_guide_pool")
            ),
            NWB_TEXT("RendererSystem: failed to create surfel guide pool buffer"),
            true,
            false,
            m_rayTracingState.m_surfelGuidePoolBuffer
        )
        // Hash heads use 0xFFFFFFFF as the empty sentinel.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(sizeof(u32)) * cellCount, sizeof(u32), true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name("surfel_cell_head")),
            NWB_TEXT("RendererSystem: failed to create surfel cell-head buffer"),
            true,
            false,
            m_rayTracingState.m_surfelCellHeadBuffer
        )
        // The persistent counter writes on Compute; the diagnostic tail may copy it on Transfer before the next import.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE, sizeof(u32), true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name("surfel_counter")),
            NWB_TEXT("RendererSystem: failed to create surfel counter buffer"),
            true,
            false,
            m_rayTracingState.m_surfelCounterBuffer
        )
        // Build-args rewrites the indirect dispatch buffer each frame.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_TRACE_INDIRECT_ARGS_WORD_COUNT, sizeof(u32), true, true, Core::ResourceQueueSharing::GraphicsAndAsyncCompute, Name("surfel_trace_indirect_args")),
            NWB_TEXT("RendererSystem: failed to create surfel trace indirect-args buffer"),
            false,
            false,
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer
        )
        // Age-free pushes ids and spawn pops them from this persistent free list.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(sizeof(u32)) * poolCapacity, sizeof(u32), true, false, Core::ResourceQueueSharing::GraphicsAndAsyncCompute, Name("surfel_free_list")),
            NWB_TEXT("RendererSystem: failed to create surfel free-list buffer"),
            true,
            false,
            m_rayTracingState.m_surfelFreeListBuffer
        )
        // Bounce gathers read a stable previous-frame pool snapshot.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity, NWB_SURFEL_RECORD_SIZE, false, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name("surfel_pool_snapshot")),
            NWB_TEXT("RendererSystem: failed to create surfel pool snapshot buffer"),
            false,
            false,
            m_rayTracingState.m_surfelPoolSnapshotBuffer
        )
        // Snapshot hash heads alongside the pool for a consistent bounce gather.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(sizeof(u32)) * cellCount, sizeof(u32), false, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name("surfel_cell_head_snapshot")),
            NWB_TEXT("RendererSystem: failed to create surfel cell-head snapshot buffer"),
            false,
            false,
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer
        )
        || !ensureBuffer(
            readbackDesc,
            NWB_TEXT("RendererSystem: failed to create surfel counter readback buffer"),
            false,
            false,
            m_rayTracingState.m_surfelCounterReadback
        )
        // Every surfel pass binds this per-frame parameter buffer as a ConstantBuffer.
        || !ensureBuffer(
            constantsDesc,
            NWB_TEXT("RendererSystem: failed to create surfel constant buffer"),
            false,
            false,
            m_rayTracingState.m_surfelConstants
        )
    )
        return false;

    // Persistent descriptors own backing resources until deferred retirement.
    if(
        !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::UniformBuffer, false, heap, *m_rayTracingState.m_surfelConstants.get(), m_rayTracingState.m_surfelConstantsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelPoolBuffer.get(), m_rayTracingState.m_surfelPoolHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelGuidePoolBuffer.get(), m_rayTracingState.m_surfelGuidePoolHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelCellHeadBuffer.get(), m_rayTracingState.m_surfelCellHeadHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelCounterBuffer.get(), m_rayTracingState.m_surfelCounterHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelFreeListBuffer.get(), m_rayTracingState.m_surfelFreeListHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, *m_rayTracingState.m_surfelPoolSnapshotBuffer.get(), m_rayTracingState.m_surfelPoolSnapshotHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, *m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(), m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register persistent surfel resources in the descriptor heap"));
        return false;
    }

    // Only tracing differs between hardware and software surfel paths.
    SurfelGiPipelineSet pipelines(m_arena, m_graphics, m_shaderSystem, m_rayTracingState);
    const b8 traceReady = m_rayTracingState.m_surfelUseHwTrace ? pipelines.ensureSurfelTraceHwPipeline() : pipelines.ensureSurfelTracePipeline();
    if(!pipelines.ensureSurfelSpawnPipeline() || !pipelines.ensureSurfelAgeFreePipeline() || !pipelines.ensureSurfelHashBuildPipeline() || !traceReady || !pipelines.ensureSurfelResolvePipeline() || !pipelines.ensureSurfelUpsamplePipeline() || !pipelines.ensureSurfelTraceBuildArgsPipeline())
        return false;
    return true;
}

b8 RendererRayTracingSystem::shouldCaptureSurfelCountReadback()const noexcept{
    return hasSurfelWork() && m_rayTracingState.m_surfelCounterBuffer && m_rayTracingState.m_surfelCounterReadback && !m_rayTracingState.m_surfelCountReadbackSubmissionToken.valid() && IsSurfelCountReadbackDue(m_rayTracingState.m_surfelFrameIndex);
}

b8 RendererRayTracingSystem::prepareSurfelResources(DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    m_rayTracingState.m_surfelUseCsgTrace = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    if(!ensureSurfelResources())
        return false;

    // GI consumes the same selector descriptor as the other current ray effects.
    if(!targets.bindless.valid() || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer) || !RayTraceMaterialResources(m_graphics, m_rayTracingState, m_rayTracingState).ensureRayTraceMaterialContextSlotsHeapHandle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI trace heap context is incomplete"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

