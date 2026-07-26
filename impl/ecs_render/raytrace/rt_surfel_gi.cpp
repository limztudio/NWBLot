// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU mirror of the shader NwbSurfelConstants (5 x Float4 = 80 bytes, matches surfel_constants.slangi lane-for-lane).
struct NwbSurfelConstantsGpu{
    Float4 cameraPositionCellSize;  // xyz = camera world position (unused until U6 distance-scaled spawn), w = hash cell size
    Float4 hashPoolFrameDivisor;    // x = hash cell count, y = pool capacity, z = frame index, w = update divisor
    Float4 coverageRadiusBiasHyst;  // x = reserved (coverage sum dropped for one-surfel-per-cell), y = default radius, z = normal bias, w = accumulation cap
    Float4 ageRaysTileScreen;       // x = max age, y = rays/surfel, z = spawn tile (px), w = screen width
    Float4 screenHeightPad;         // x = screen height, yzw = pad
};
static_assert(sizeof(NwbSurfelConstantsGpu) == sizeof(f32) * 4u * 5u, "NwbSurfelConstantsGpu must match the shader NwbSurfelConstants layout");

// The surfel normal bias: push the trace ray origin + the gather sample point off the surface along the normal so a
// ray/query does not self-hit the surface it belongs to. Small world-space offset; U6 scales it by camera distance.
inline constexpr f32 s_SurfelNormalBias = 0.05f;

// Live-count diagnostic (U1): snapshot the surfel counter every s_SurfelCountLogInterval frames and map + log it
// s_SurfelCountLogDelay frames later (the copy is async, so the delay lets the GPU finish before the CPU maps).
inline constexpr u32 s_SurfelCountLogInterval = 120u;
inline constexpr u32 s_SurfelCountLogDelay = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_surfel_gi{
    [[nodiscard]] bool IsHeapHandle(const Core::GpuDescriptorHandle handle, const Core::GpuDescriptorClass::Enum descriptorClass){
        return handle.valid() && handle.descriptorClass() == descriptorClass;
    }

    [[nodiscard]] bool RegisterHeapBuffer(
        Core::GpuDescriptorHeap& heap,
        Core::Buffer& buffer,
        const Core::GpuDescriptorClass::Enum descriptorClass,
        const bool writable,
        Core::GpuDescriptorHandle& outHandle
    ){
        outHandle = Core::GpuDescriptorHandle::invalid();
        const Core::GpuDescriptorHandle handle = heap.allocate(descriptorClass);
        if(!handle.valid())
            return false;

        const Core::DescriptorWriteItem item = descriptorClass == Core::GpuDescriptorClass::UniformBuffer
            ? Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer)
            : (writable
                ? Core::DescriptorWriteItem::StructuredBuffer_UAV(0u, &buffer)
                : Core::DescriptorWriteItem::StructuredBuffer_SRV(0u, &buffer)
            )
        ;
        if(!heap.write(handle, item)){
            heap.free(handle);
            return false;
        }
        outHandle = handle;
        return true;
    }

    [[nodiscard]] bool EnsureHeapBuffer(
        Core::GpuDescriptorHeap& heap,
        Core::Buffer& buffer,
        const Core::GpuDescriptorClass::Enum descriptorClass,
        const bool writable,
        Core::GpuDescriptorHandle& inOutHandle
    ){
        if(inOutHandle.valid())
            return IsHeapHandle(inOutHandle, descriptorClass);

        Core::GpuDescriptorHandle acquired;
        if(!RegisterHeapBuffer(heap, buffer, descriptorClass, writable, acquired))
            return false;
        inOutHandle = acquired;
        return true;
    }

    void RetireHeapHandle(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle){
        if(handle.valid())
            heap.free(handle);
        handle = Core::GpuDescriptorHandle::invalid();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelSpawnPipeline(){
    if(rayTracingState().m_surfelSpawnPipeline)
        return true;
    if(rayTracingState().m_surfelSpawnPipelineFailed)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel spawn requires the initialized global descriptor heap"));
        rayTracingState().m_surfelSpawnPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelSpawnBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // All surfel resources and frame inputs are selected through the common heap-slot push block.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelSpawnBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelSpawnBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn binding layout"));
            rayTracingState().m_surfelSpawnPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelSpawnShader,
        AssetsGraphicsGi::s_SurfelSpawnShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelSpawn"
    )){
        rayTracingState().m_surfelSpawnPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelSpawnShader)
        .addBindingLayout(rayTracingState().m_surfelSpawnBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_surfelSpawnPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelSpawnPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn compute pipeline"));
        rayTracingState().m_surfelSpawnPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel spawn compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelAgeFreePipeline(){
    if(rayTracingState().m_surfelAgeFreePipeline)
        return true;
    if(rayTracingState().m_surfelAgeFreePipelineFailed)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel age-free requires the initialized global descriptor heap"));
        rayTracingState().m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelAgeFreeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // The maintenance field is entirely heap-selected by the common push block.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelAgeFreeBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelAgeFreeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free binding layout"));
            rayTracingState().m_surfelAgeFreePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelAgeFreeShader,
        AssetsGraphicsGi::s_SurfelAgeFreeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelAgeFree"
    )){
        rayTracingState().m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelAgeFreeShader)
        .addBindingLayout(rayTracingState().m_surfelAgeFreeBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_surfelAgeFreePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelAgeFreePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free compute pipeline"));
        rayTracingState().m_surfelAgeFreePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel age-free compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelHashBuildPipeline(){
    if(rayTracingState().m_surfelHashBuildPipeline)
        return true;
    if(rayTracingState().m_surfelHashBuildPipelineFailed)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel hash-build requires the initialized global descriptor heap"));
        rayTracingState().m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelHashBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Pool and hash-head views are selected through the global descriptor heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelHashBuildBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelHashBuildBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build binding layout"));
            rayTracingState().m_surfelHashBuildPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelHashBuildShader,
        AssetsGraphicsGi::s_SurfelHashBuildShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelHashBuild"
    )){
        rayTracingState().m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelHashBuildShader)
        .addBindingLayout(rayTracingState().m_surfelHashBuildBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_surfelHashBuildPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelHashBuildPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build compute pipeline"));
        rayTracingState().m_surfelHashBuildPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel hash-build compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelTracePipeline(){
    if(rayTracingState().m_surfelTracePipeline)
        return true;
    if(rayTracingState().m_surfelTracePipelineFailed)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace requires the initialized global descriptor heap"));
        rayTracingState().m_surfelTracePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelTraceBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Deferred scene slots, material-context slots, live/snapshot surfels, and geometry all come from the heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelTraceBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelTraceBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace binding layout"));
            rayTracingState().m_surfelTracePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelTraceShader,
        AssetsGraphicsGi::s_SurfelTraceShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelTrace"
    )){
        rayTracingState().m_surfelTracePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelTraceShader)
        .addBindingLayout(rayTracingState().m_surfelTraceBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_surfelTracePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelTracePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace compute pipeline"));
        rayTracingState().m_surfelTracePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel trace compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelResources(){
    // Create the persistent pool / cell-head / counter / params buffers lazily. They live on RendererRayTracingState
    // (NOT DeferredFrameTargets) so a window resize does not reset surfel convergence.
    if(!hasSurfelWork())
        return true;

    const u32 poolCapacity = rayTracingState().m_surfelPoolCapacity;
    const u32 cellCount = rayTracingState().m_surfelHashCellCount;
    if(poolCapacity == 0u || cellCount == 0u)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI requires the initialized global descriptor heap"));
        return false;
    }

    // Surfel pool (poolCapacity * 64B). UAV-writable (the spawn/hash-build/trace passes write it); the gather binds it
    // as an SRV. On (re)creation, request the one-shot clear (this function has no command list) + reset the seed.
    if(!rayTracingState().m_surfelPoolBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity)
            .setStructStride(NWB_SURFEL_RECORD_SIZE)
            .setCanHaveUAVs(true)
            .setDebugName(Name("surfel_pool"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelPoolBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelPoolBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel pool buffer"));
            return false;
        }
        rayTracingState().m_surfelSeeded = false;
        rayTracingState().m_surfelResourcesNeedClear = true;
    }

    // Cell-head buffer (cellCount uints -- one linked-list head per hash cell). Cleared to 0xFFFFFFFF (empty).
    if(!rayTracingState().m_surfelCellHeadBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * cellCount)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("surfel_cell_head"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelCellHeadBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelCellHeadBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel cell-head buffer"));
            return false;
        }
        rayTracingState().m_surfelResourcesNeedClear = true;
    }

    // Allocation counter (bump top + free top). Cleared to 0.
    if(!rayTracingState().m_surfelCounterBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("surfel_counter"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelCounterBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelCounterBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel counter buffer"));
            return false;
        }
        rayTracingState().m_surfelResourcesNeedClear = true;
    }

    // Trace indirect-args buffer (U6): DispatchIndirectArguments{ceil(BUMP_TOP/divisor),1,1}, rewritten by the build-args
    // pass each frame (no clear needed -- fully overwritten before the trace reads it). isDrawIndirectArgs marks it for
    // the IndirectArgument state; canHaveUAVs lets the build-args pass write it. Automatic state tracking barriers the
    // build-args UAV write -> the trace's indirect consume.
    if(!rayTracingState().m_surfelTraceIndirectArgsBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_TRACE_INDIRECT_ARGS_WORD_COUNT)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setDebugName(Name("surfel_trace_indirect_args"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelTraceIndirectArgsBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelTraceIndirectArgsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace indirect-args buffer"));
            return false;
        }
    }

    // Free-list (U1 recycling): poolCapacity uints, a persistent LIFO stack of recycled surfel ids (depth = counter
    // FREE_TOP). Age-free pushes; spawn pops. Same barrier/state-tracking as the pool so the intra-frame push->pop
    // (pass 0 -> pass 3) UAV barrier is emitted. Contents cleared to 0 once (FREE_TOP=0 is what marks it empty).
    if(!rayTracingState().m_surfelFreeListBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * poolCapacity)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("surfel_free_list"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelFreeListBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelFreeListBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel free-list buffer"));
            return false;
        }
        rayTracingState().m_surfelResourcesNeedClear = true;
    }

    // Snapshot pool (U4 infinite bounce): a copy of the previous frame's converged pool the trace's bounce gather reads
    // as an SRV (never the live pool it is writing), so surfel->surfel feedback reads a stable frame-start field. SRV-only
    // (canHaveUAVs false -- only copyBuffer writes it), same size/stride as the live pool. No clear: fully overwritten by
    // the copyBuffer at the top of renderSurfelGi before any read (frame 0's snapshot is a copy of the freshly-cleared
    // pool, so the bounce is 0 until the first real frame lands -- the documented "single frame shows first bounce only").
    if(!rayTracingState().m_surfelPoolSnapshotBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity)
            .setStructStride(NWB_SURFEL_RECORD_SIZE)
            .setCanHaveUAVs(false)
            .setDebugName(Name("surfel_pool_snapshot"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelPoolSnapshotBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelPoolSnapshotBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel pool snapshot buffer"));
            return false;
        }
    }

    // Snapshot cell-head (U4): the matching prev-frame cell-head, so the bounce gather's 3x3x3 walk is mutually
    // consistent with its snapshot pool (both captured at the same frame-start). SRV-only; overwritten by copyBuffer.
    if(!rayTracingState().m_surfelCellHeadSnapshotBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * cellCount)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(false)
            .setDebugName(Name("surfel_cell_head_snapshot"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelCellHeadSnapshotBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelCellHeadSnapshotBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel cell-head snapshot buffer"));
            return false;
        }
    }

    // CPU-readable copy of the counter for the periodic live-count diagnostic (U1). Snapshotted on a log-interval frame,
    // mapped a few frames later (mirrors the SW-shadow edge-stats readback).
    if(!rayTracingState().m_surfelCounterReadback){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE)
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setDebugName(Name("surfel_counter_readback"))
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        rayTracingState().m_surfelCounterReadback = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelCounterReadback){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel counter readback buffer"));
            return false;
        }
    }

    // Params CB (5 x Float4). Uploaded each rendered frame in prepareSurfelResources. setIsConstantBuffer marks it a
    // uniform buffer (adds VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT + 16-byte-aligns the suballocation); it is bound as a
    // ConstantBuffer in every surfel pass, so without the flag validation reports a uniform-buffer type/usage and
    // alignment mismatch.
    if(!rayTracingState().m_surfelConstants){
        Core::BufferDesc cbDesc;
        cbDesc
            .setByteSize(sizeof(NwbSurfelConstantsGpu))
            .setIsConstantBuffer(true)
            .setDebugName(Name("surfel_constants"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelConstants = graphics().createBuffer(cbDesc);
        if(!rayTracingState().m_surfelConstants){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel constant buffer"));
            return false;
        }
    }

    // Register each persistent surfel buffer exactly once in the global heap. The typed shader aliases choose read or
    // writable views at use time; the descriptor itself owns the backing-buffer lifetime until its retirement delay.
    if(
        !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelConstants.get(), Core::GpuDescriptorClass::UniformBuffer, false, rayTracingState().m_surfelConstantsHeapHandle)
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelPoolBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelPoolHeapHandle)
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelCellHeadBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelCellHeadHeapHandle)
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelCounterHeapHandle)
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelTraceIndirectArgsHeapHandle)
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelFreeListBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelFreeListHeapHandle)
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelPoolSnapshotBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, false, rayTracingState().m_surfelPoolSnapshotHeapHandle)
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(heap, *rayTracingState().m_surfelCellHeadSnapshotBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, false, rayTracingState().m_surfelCellHeadSnapshotHeapHandle)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register persistent surfel resources in the descriptor heap"));
        return false;
    }

    // The trace pipeline is the one backend-specific pass: the HW-shadow branch selects the RayQuery twin, else the SW walk.
    const bool traceReady = rayTracingState().m_surfelUseHwTrace ? ensureSurfelTraceHwPipeline() : ensureSurfelTracePipeline();
    if(!ensureSurfelSpawnPipeline() || !ensureSurfelAgeFreePipeline() || !ensureSurfelHashBuildPipeline() || !traceReady || !ensureSurfelResolvePipeline() || !ensureSurfelUpsamplePipeline() || !ensureSurfelTraceBuildArgsPipeline())
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// U5 HW-RayQuery trace twin. Same 5 surfel passes; only the TRACE swaps its pipeline: inline RayQuery over the scene
// TLAS (surfel_trace_hw_cs -> gi_hw_trace.slangi) instead of the SW BVH walk. Gated on accel-struct + ray-query support,
// so it only builds on the HW-shadow branch (which is where surfels are enabled on real RT hardware).
bool RendererRayTracingSystem::ensureSurfelTraceHwPipeline(){
    if(rayTracingState().m_surfelTraceHwPipeline)
        return true;
    if(rayTracingState().m_surfelTraceHwPipelineFailed)
        return false;

    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct) || !graphics().queryFeatureSupport(Core::Feature::RayQuery)){
        rayTracingState().m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel HW trace requires the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelTraceHwBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // The fixed heap blocks provide the TLAS, trace contexts, surfels, and geometry; the local layout is push-only.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelTraceHwBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelTraceHwBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace binding layout"));
            rayTracingState().m_surfelTraceHwPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelTraceHwShader,
        AssetsGraphicsGi::s_SurfelTraceHwShaderName,
        AStringView("NWB_BINDLESS_TLAS=1"),
        Core::ShaderType::Compute,
        "ECSRender_SurfelTraceHw"
    )){
        rayTracingState().m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelTraceHwShader)
        .addBindingLayout(rayTracingState().m_surfelTraceHwBindingLayout)
    ;
    // Pin the global resource (set 8), sampler (set 9), and TLAS (set 10) layouts onto the hardware surfel-trace
    // pipeline. The local HW GI layout remains positional set 0; the heap layouts carry explicit high set indices and
    // createPipelineLayoutForBindingLayouts gap-fills sets 1-7. This is a COMPUTE pipeline, so the heap binds through
    // bindCompute.
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    rayTracingState().m_surfelTraceHwPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelTraceHwPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace compute pipeline"));
        rayTracingState().m_surfelTraceHwPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel HW trace compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelResolvePipeline(){
    if(rayTracingState().m_surfelResolvePipeline)
        return true;
    if(rayTracingState().m_surfelResolvePipelineFailed)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel resolve requires the initialized global descriptor heap"));
        rayTracingState().m_surfelResolvePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // G-buffer, surfel field, and half-resolution storage output are all selected from heap slots.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelResolveBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve binding layout"));
            rayTracingState().m_surfelResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelResolveShader,
        AssetsGraphicsGi::s_SurfelResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelResolve"
    )){
        rayTracingState().m_surfelResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelResolveShader)
        .addBindingLayout(rayTracingState().m_surfelResolveBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_surfelResolvePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve compute pipeline"));
        rayTracingState().m_surfelResolvePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel resolve compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelUpsamplePipeline(){
    if(rayTracingState().m_surfelUpsamplePipeline)
        return true;
    if(rayTracingState().m_surfelUpsamplePipelineFailed)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel upsample requires the initialized global descriptor heap"));
        rayTracingState().m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelUpsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Half irradiance, G-buffer, and full-resolution storage output are heap-selected.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelUpsampleBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelUpsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample binding layout"));
            rayTracingState().m_surfelUpsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelUpsampleShader,
        AssetsGraphicsGi::s_SurfelUpsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelUpsample"
    )){
        rayTracingState().m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelUpsampleShader)
        .addBindingLayout(rayTracingState().m_surfelUpsampleBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_surfelUpsamplePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelUpsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample compute pipeline"));
        rayTracingState().m_surfelUpsamplePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel upsample compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelTraceBuildArgsPipeline(){
    if(rayTracingState().m_surfelTraceBuildArgsPipeline)
        return true;
    if(rayTracingState().m_surfelTraceBuildArgsPipelineFailed)
        return false;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace build-args requires the initialized global descriptor heap"));
        rayTracingState().m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelTraceBuildArgsBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Constants, counter, and indirect argument output are selected by heap slots.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelTraceBuildArgsBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_surfelTraceBuildArgsBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args binding layout"));
            rayTracingState().m_surfelTraceBuildArgsPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_surfelTraceBuildArgsShader,
        AssetsGraphicsGi::s_SurfelTraceBuildArgsShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SurfelTraceBuildArgs"
    )){
        rayTracingState().m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_surfelTraceBuildArgsShader)
        .addBindingLayout(rayTracingState().m_surfelTraceBuildArgsBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    rayTracingState().m_surfelTraceBuildArgsPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelTraceBuildArgsPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args compute pipeline"));
        rayTracingState().m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel trace build-args compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    if(auto* device = graphics().getDevice()){
        Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
        if(heap.isInitialized()){
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelConstantsHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelPoolHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelCellHeadHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelCounterHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelTraceIndirectArgsHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelFreeListHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelPoolSnapshotHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelCellHeadSnapshotHeapHandle);
            __hidden_rt_surfel_gi::RetireHeapHandle(heap, rayTracingState().m_surfelMaterialContextSlotsHeapHandle);
            return;
        }
    }

    rayTracingState().m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    rayTracingState().m_surfelMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::hasSurfelWork()const noexcept{
    // Surfel GI is disabled until m_surfelEnabled is set (in prepareShadowVisibilityResources, once the SW scene BVH is
    // resident). A zero-init pool + 0xFFFFFFFF cell heads make the gather a no-op (-> hemiAmbient) until then.
    return rayTracingState().m_surfelEnabled;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareSurfelResources(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    // Lazily create the persistent buffers + pipelines. They live on RendererRayTracingState so a window resize does
    // not reset surfel convergence.
    if(!ensureSurfelResources())
        return false;

    // The shared deferred target-generation payload already owns a UniformBuffer heap descriptor. Register the one
    // remaining trace selector (material-context slots) in surfel state; this avoids changing shadow/caustic bindings
    // while leaving both surfel trace backends entirely heap-addressed.
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    if(
        !targets.bindless.valid()
        || !__hidden_rt_surfel_gi::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !rayTracingState().m_rayTraceMaterialContextSlotsBuffer
        || !__hidden_rt_surfel_gi::EnsureHeapBuffer(
            heap,
            *rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            false,
            rayTracingState().m_surfelMaterialContextSlotsHeapHandle
        )
    )
    {
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI trace heap context is incomplete"));
        return false;
    }

    // One-shot clear of the freshly-created buffers (ensureSurfelResources has no command list). Pool -> zero (alive ==
    // 0 everywhere), cell-head -> 0xFFFFFFFF (empty lists), counter -> 0 (bump top at slot 0). Cleared exactly once per
    // (re)creation; the pool then accumulates surfels across frames (recycling lands in U1).
    if(rayTracingState().m_surfelResourcesNeedClear){
        Core::Buffer* pool = rayTracingState().m_surfelPoolBuffer.get();
        Core::Buffer* cellHead = rayTracingState().m_surfelCellHeadBuffer.get();
        Core::Buffer* counter = rayTracingState().m_surfelCounterBuffer.get();
        Core::Buffer* freeList = rayTracingState().m_surfelFreeListBuffer.get();
        commandList.setBufferState(pool, Core::ResourceStates::CopyDest);
        commandList.setBufferState(cellHead, Core::ResourceStates::CopyDest);
        commandList.setBufferState(counter, Core::ResourceStates::CopyDest);
        commandList.setBufferState(freeList, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(pool, 0u);
        commandList.clearBufferUInt(cellHead, NWB_SURFEL_CELL_INVALID);
        commandList.clearBufferUInt(counter, 0u);
        commandList.clearBufferUInt(freeList, 0u);   // contents cosmetic; counter FREE_TOP=0 is what marks it empty
        commandList.setBufferState(pool, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(cellHead, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(counter, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(freeList, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        rayTracingState().m_surfelResourcesNeedClear = false;
    }

    // Upload the params CB. The cell size sets the surfel spacing (one surfel per hash cell); the gather radius is a bit
    // larger (NWB_SURFEL_DEFAULT_RADIUS) so the 3x3x3 neighbour blend overlaps smoothly.
    // The update divisor is 1 on the first (not-yet-seeded) frame so EVERY surfel traces to bootstrap in ONE frame --
    // the unfocused smoke app renders only that frame -- then reverts to the round-robin divisor. The trace's temporal
    // accumulation is a bounded running mean capped at NWB_SURFEL_MAX_ACCUM (carried in coverageRadiusBiasHyst.w); the
    // per-surfel sampleCount drives the seed (n==0 -> first sample), so no CPU seeded/hysteresis branch is needed. The
    // camera position rides xyz for U6's distance scaling.
    const u32 updateDivisor = rayTracingState().m_surfelSeeded ? Max<u32>(NWB_SURFEL_UPDATE_DIVISOR, 1u) : 1u;
    const f32 cellSize = NWB_SURFEL_CELL_SIZE;

    NwbSurfelConstantsGpu params;
    params.cameraPositionCellSize = Float4(0.0f, 0.0f, 0.0f, cellSize);
    params.hashPoolFrameDivisor = Float4(
        static_cast<f32>(rayTracingState().m_surfelHashCellCount),
        static_cast<f32>(rayTracingState().m_surfelPoolCapacity),
        static_cast<f32>(rayTracingState().m_surfelFrameIndex),
        static_cast<f32>(updateDivisor)
    );
    params.coverageRadiusBiasHyst = Float4(0.0f, NWB_SURFEL_DEFAULT_RADIUS, s_SurfelNormalBias, static_cast<f32>(NWB_SURFEL_MAX_ACCUM));
    params.ageRaysTileScreen = Float4(
        static_cast<f32>(NWB_SURFEL_MAX_AGE),
        static_cast<f32>(NWB_SURFEL_RAYS_PER_SURFEL),
        static_cast<f32>(NWB_SURFEL_SPAWN_TILE),
        static_cast<f32>(targets.width)
    );
    params.screenHeightPad = Float4(static_cast<f32>(targets.height), 0.0f, 0.0f, 0.0f);

    Core::Buffer* cb = rayTracingState().m_surfelConstants.get();
    commandList.setBufferState(cb, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(cb, &params, sizeof(params));
    commandList.setBufferState(cb, Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderSurfelGi(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // The trace pass runs the SELECTED backend (U5): the HW RayQuery twin on the HW-shadow branch, else the SW BVH walk.
    // Everything else (snapshot copy, age-free/clear/hash-build/spawn/resolve) is backend-agnostic.
    const bool useHwTrace = rayTracingState().m_surfelUseHwTrace;
    Core::ComputePipeline* const tracePipeline = useHwTrace ? rayTracingState().m_surfelTraceHwPipeline.get() : rayTracingState().m_surfelTracePipeline.get();

    // Guard: every pass needs its pipeline (a prior ensure failure leaves the block inert this frame).
    if(
        !rayTracingState().m_surfelSpawnPipeline
        || !rayTracingState().m_surfelAgeFreePipeline
        || !rayTracingState().m_surfelHashBuildPipeline
        || !tracePipeline
        || !rayTracingState().m_surfelResolvePipeline
        || !rayTracingState().m_surfelUpsamplePipeline
        || !rayTracingState().m_surfelTraceBuildArgsPipeline
    )
        return true;

    if(
        !targets.bindless.valid()
        || !deferredState().m_sceneShadingBuffer
        || !deferredState().m_lightBuffer
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelConstantsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelPoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelCellHeadHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelTraceIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelFreeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelPoolSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelCellHeadSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_surfelMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_rt_surfel_gi::IsHeapHandle(targets.bindless.gbufferWorldPosition, Core::GpuDescriptorClass::SampledImage)
        || !__hidden_rt_surfel_gi::IsHeapHandle(targets.bindless.gbufferNormal, Core::GpuDescriptorClass::SampledImage)
        || !__hidden_rt_surfel_gi::IsHeapHandle(targets.bindless.surfelIrradianceHalf, Core::GpuDescriptorClass::SampledImage)
        || !__hidden_rt_surfel_gi::IsHeapHandle(targets.bindless.surfelIrradianceHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_rt_surfel_gi::IsHeapHandle(targets.bindless.surfelIrradianceStorage, Core::GpuDescriptorClass::StorageImage)
        || (useHwTrace && (!rayTracingState().m_tlas || !__hidden_rt_surfel_gi::IsHeapHandle(rayTracingState().m_tlasHeapHandle, Core::GpuDescriptorClass::AccelStruct)))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI heap registration is incomplete"));
        return false;
    }

    // The age-free / hash-build passes dispatch over the full pool (they touch every slot); the trace dispatches per
    // LIVE surfel via (3b)'s indirect args and derives its round-robin phase from the CB divisor.
    const u32 poolCapacity = rayTracingState().m_surfelPoolCapacity;

    SurfelHeapPushConstants surfelPush;
    surfelPush.constantsHeapSlot = rayTracingState().m_surfelConstantsHeapHandle.slot();
    surfelPush.poolHeapSlot = rayTracingState().m_surfelPoolHeapHandle.slot();
    surfelPush.cellHeadHeapSlot = rayTracingState().m_surfelCellHeadHeapHandle.slot();
    surfelPush.counterHeapSlot = rayTracingState().m_surfelCounterHeapHandle.slot();
    surfelPush.freeListHeapSlot = rayTracingState().m_surfelFreeListHeapHandle.slot();
    surfelPush.snapshotPoolHeapSlot = rayTracingState().m_surfelPoolSnapshotHeapHandle.slot();
    surfelPush.snapshotCellHeadHeapSlot = rayTracingState().m_surfelCellHeadSnapshotHeapHandle.slot();
    surfelPush.traceIndirectArgsHeapSlot = rayTracingState().m_surfelTraceIndirectArgsHeapHandle.slot();
    surfelPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
    surfelPush.materialContextSlotsHeapSlot = rayTracingState().m_surfelMaterialContextSlotsHeapHandle.slot();

    // (U4 infinite bounce) Snapshot the previous frame's converged pool + cell-head into the SRV-only snapshot buffers
    // BEFORE any pass mutates them this frame, so the trace's per-ray bounce gather reads a stable frame-start field
    // (== the PREVIOUS frame's converged result -- only the trace writes SH, and it runs after this copy). Copying BOTH
    // keeps the snapshot walk mutually consistent (a slot recycled this frame must not be reachable from a stale head).
    // On the (re)creation frame the source pool is post-clear (UnorderedAccess) rather than the prev-frame resolve's
    // ShaderResource; either way the CopySource transition barrier below covers it. ~2.5 MB/frame -- negligible.
    {
        const u32 cellCount = rayTracingState().m_surfelHashCellCount;
        Core::Buffer* pool = rayTracingState().m_surfelPoolBuffer.get();
        Core::Buffer* cellHead = rayTracingState().m_surfelCellHeadBuffer.get();
        Core::Buffer* poolSnapshot = rayTracingState().m_surfelPoolSnapshotBuffer.get();
        Core::Buffer* cellHeadSnapshot = rayTracingState().m_surfelCellHeadSnapshotBuffer.get();
        commandList.setBufferState(pool, Core::ResourceStates::CopySource);
        commandList.setBufferState(cellHead, Core::ResourceStates::CopySource);
        commandList.setBufferState(poolSnapshot, Core::ResourceStates::CopyDest);
        commandList.setBufferState(cellHeadSnapshot, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.copyBuffer(poolSnapshot, 0u, pool, 0u, static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity);
        commandList.copyBuffer(cellHeadSnapshot, 0u, cellHead, 0u, static_cast<u64>(sizeof(u32)) * cellCount);
        commandList.setBufferState(poolSnapshot, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(cellHeadSnapshot, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    // The passes UAV-write the surfel buffers then UAV/SRV-read them next; enable automatic UAV barriers so the
    // commitBarriers between passes serialises the writes. This enable block MUST sit ABOVE pass (0) so the age-free
    // pass's first counter[FREE_TOP]/free-list writes are barriered against the previous frame's spawn writes.
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelPoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelCellHeadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelCounterBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelFreeListBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), true);

    // (0) Age-free (U1 recycling): one thread per pool slot; free surfels unseen for maxAge frames (alive = 0) and PUSH
    // their ids onto the free-list for the spawn to reuse. Runs FIRST -- it reads lastSeenFrame written by the PREVIOUS
    // frame's spawn keep-alive, and frees the slots BEFORE the hash-build re-links live surfels + the spawn pops. The
    // push (here) and the spawn's pop are barrier-separated (clear + hash-build between), so the free-list stack has no
    // concurrent push/pop -> no ABA. The linear workgroup width is shared with the shader.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, graphics().getDevice(), commandList);
        commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelAgeFreePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelAgeFreePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    // (1) Clear the cell-head to empty, then rebuild the hash from the live pool BEFORE the spawn, so the spawn sees this
    // frame's exact occupancy (a non-empty cell head == a surfel already covers the cell) and fills only empty cells.
    {
        Core::Buffer* cellHead = rayTracingState().m_surfelCellHeadBuffer.get();
        commandList.setBufferState(cellHead, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(cellHead, NWB_SURFEL_CELL_INVALID);
    }

    // (2) Hash-build: one thread per pool slot; link each live surfel into its cell's list.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelHashBuild, graphics().getDevice(), commandList);
        commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelHashBuildPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelHashBuildPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    // (3) Spawn: one thread per screen tile. Reads the freshly-built cell head; where a cell is still empty, atomically
    // claims it and bump-allocates one surfel (one surfel per hash bucket). [numthreads(8,8,1)] over (screen / spawnTile).
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelSpawn, graphics().getDevice(), commandList);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();

        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelSpawnPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelSpawnPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        const u32 tilesX = DivideUp(targets.width, NWB_SURFEL_SPAWN_TILE);
        const u32 tilesY = DivideUp(targets.height, NWB_SURFEL_SPAWN_TILE);
        commandList.dispatch(DivideUp(tilesX, static_cast<u32>(NWB_SURFEL_GROUP_SIZE)), DivideUp(tilesY, static_cast<u32>(NWB_SURFEL_GROUP_SIZE)), 1u);
    }

    // (3b) Build the trace's indirect args (U6): 1 thread reads the POST-spawn BUMP_TOP + the divisor (surfel CB) and
    // writes {ceil(BUMP_TOP/divisor),1,1}, so the trace dispatches one workgroup per LIVE surfel instead of the fixed
    // ceil(poolCapacity/divisor) (a ~pool/live over-dispatch of dead-slot workgroups). The spawn's counter UAV write is
    // synced to the build-args read by the counter's per-frame UAV barriers; the args UAV write is synced to the trace's
    // IndirectArgument consume by setComputeState (auto) below.
    {
        commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(rayTracingState().m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelTraceBuildArgsPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelTraceBuildArgsPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(
            NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_X,
            NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_Y,
            NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_Z
        );
    }

    // (4) Trace: one workgroup per LIVE surfel (64 threads = 64 hemisphere rays), via dispatchIndirect off (3b)'s args.
    // Stage the trace's geometry inputs to
    // ShaderResource, then dispatch the SELECTED backend (U5). HW = the driver walks the TLAS; we still stage the
    // HW-resident per-mesh position/index/attribute buffers plus the shadow-owned material context (the shader uses all
    // of them to reconstruct the authored surface at the hit). SW = the per-mesh BVH nodes/positions/indices/attributes
    // + its scene BVH/instance tables and the same material context. Every one of these accesses is heap-addressed, so
    // their backing resources are transitioned explicitly here.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, graphics().getDevice(), commandList);
        if(useHwTrace){
            for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
                commandList.setBufferState(rayTracingState().m_shadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            }
            commandList.setBufferState(rayTracingState().m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        else{
            for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
                commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_swShadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            }
            commandList.setBufferState(rayTracingState().m_sceneBvhNodeBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_sceneInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(useHwTrace)
            commandList.setAccelStructState(rayTracingState().m_tlas.get(), Core::ResourceStates::AccelStructRead);
        commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(rayTracingState().m_surfelPoolSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_surfelCellHeadSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(tracePipeline);
        // The explicit IndirectArgument transition above makes the heap-written args buffer visible to the dispatch.
        state.setIndirectParams(rayTracingState().m_surfelTraceIndirectArgsBuffer.get());
        commandList.setComputeState(state);
        // Both surfel shaders access per-mesh geometry through the descriptor heap, so bind its blocks against the
        // selected pipeline before dispatch. The HW path also selects its TLAS generation at set 10.
        if(rayTracingState().m_surfelUseHwTrace && !rayTracingState().m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch surfel HW GI without the descriptor-heap TLAS handle"));
            return false;
        }
        const Core::GpuDescriptorHandle tlasHeapHandle = rayTracingState().m_surfelUseHwTrace
            ? rayTracingState().m_tlasHeapHandle
            : Core::GpuDescriptorHandle::invalid();
        heap.bindCompute(commandList, *tracePipeline, tlasHeapHandle);
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatchIndirect(0u);
    }

    // (5) Resolve (HALF-res, U6): one thread per half-res pixel, gather the surfel field (pool + cell-head as COMPUTE
    // SRVs) + the G-buffer into surfelIrradianceHalf. This is what keeps the pool off the pixel shader (compute-only),
    // eliminating the frames-in-flight pool race, and running the 125-cell gather at 1/FACTOR^2 the pixels is the U6 win.
    // The pool/cell-head UAV writes (trace/hash-build) are synced to SRV here; the half-res UAV write is synced to SRV
    // for the upsample after.
    const u32 halfWidth = DivideUp(targets.width, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR));
    const u32 halfHeight = DivideUp(targets.height, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR));
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelResolve, graphics().getDevice(), commandList);
        commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_surfelCellHeadBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.surfelIrradianceHalf.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();
        surfelPush.outputStorageHeapSlot = targets.bindless.surfelIrradianceHalfStorage.slot();

        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelResolvePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelResolvePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        const u32 groupSize = static_cast<u32>(NWB_SURFEL_RESOLVE_GROUP_SIZE);
        commandList.dispatch(DivideUp(halfWidth, groupSize), DivideUp(halfHeight, groupSize), 1u);
    }

    // (5b) Upsample (FULL-res, U6): reconstruct the full-res surfelIrradiance from the half-res resolve with a surface-
    // gated joint-bilinear filter (no bleed across silhouettes/creases; irradiance is HDR so no clamp; coverage preserved
    // so the lighting contract is unchanged). Sync the half-res UAV write -> the upsample's SRV read first.
    commandList.setTextureState(targets.surfelIrradianceHalf.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelUpsample, graphics().getDevice(), commandList);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        surfelPush.halfIrradianceSlot = targets.bindless.surfelIrradianceHalf.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();
        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.outputStorageHeapSlot = targets.bindless.surfelIrradianceStorage.slot();

        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelUpsamplePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelUpsamplePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        const u32 groupSize = static_cast<u32>(NWB_SURFEL_UPSAMPLE_GROUP_SIZE);
        commandList.dispatch(DivideUp(targets.width, groupSize), DivideUp(targets.height, groupSize), 1u);
    }

    // (6) Sync the surfelIrradiance UAV write -> the deferred-lighting pixel-shader SRV read.
    commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Live-count diagnostic (U1): map a prior snapshot (async copy done by now) and log it, else snapshot this frame.
    // live = BUMP_TOP - FREE_TOP is exact (BUMP_TOP is CAS-capped at poolCapacity). On a static fully-visible scene
    // FREE_TOP stays 0 + BUMP_TOP is stable; under camera motion FREE_TOP rises as off-screen surfels age out and falls
    // as revealed cells reuse the freed ids -> the live count stays bounded (the point of recycling).
    {
        const u32 frameIndex = rayTracingState().m_surfelFrameIndex;
        Core::Buffer* counter = rayTracingState().m_surfelCounterBuffer.get();
        Core::Buffer* readback = rayTracingState().m_surfelCounterReadback.get();
        if(
            rayTracingState().m_surfelCountReadbackPending
            && (frameIndex - rayTracingState().m_surfelCountReadbackFrame) >= s_SurfelCountLogDelay
        ){
            const u32* counts = static_cast<const u32*>(graphics().getDevice()->mapBuffer(readback, Core::CpuAccessMode::Read));
            if(counts){
                const u32 bumpTop = counts[NWB_SURFEL_COUNTER_BUMP_TOP];
                const u32 freeTop = counts[NWB_SURFEL_COUNTER_FREE_TOP];
                graphics().getDevice()->unmapBuffer(readback);
                NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: surfel live count = {} (bump {} - free {}) of {} pool capacity")
                    , static_cast<u64>(bumpTop - freeTop)
                    , static_cast<u64>(bumpTop)
                    , static_cast<u64>(freeTop)
                    , static_cast<u64>(rayTracingState().m_surfelPoolCapacity)
                );
            }
            rayTracingState().m_surfelCountReadbackPending = false;
        }
        else if(!rayTracingState().m_surfelCountReadbackPending && (frameIndex % s_SurfelCountLogInterval) == 0u){
            commandList.setBufferState(counter, Core::ResourceStates::CopySource);
            commandList.commitBarriers();
            commandList.copyBuffer(readback, 0u, counter, 0u, static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE);
            rayTracingState().m_surfelCountReadbackPending = true;
            rayTracingState().m_surfelCountReadbackFrame = frameIndex;
        }
    }

    // Advance the frame counter (seeds the ray rotation + round-robin) and mark seeded so the next frame uses the
    // steady-state divisor + EMA hysteresis.
    rayTracingState().m_surfelSeeded = true;
    rayTracingState().m_surfelFrameIndex = rayTracingState().m_surfelFrameIndex + 1u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

