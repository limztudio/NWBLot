// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/gi/hw_binding_slots.h>   // NWB_GI_HW_* -- the U5 HW-RayQuery trace binding slots


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


bool RendererRayTracingSystem::ensureSurfelSpawnPipeline(){
    if(rayTracingState().m_surfelSpawnPipeline)
        return true;
    if(rayTracingState().m_surfelSpawnPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_surfelSpawnBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, 1)); // surfel constants
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, 1)); // pool
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_CELL_HEAD, 1)); // cell head (this frame; claim links)
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_COUNTER, 1)); // counter
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_FREE_LIST, 1)); // free-list (U1 pop)
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SURFEL_BINDING_GBUFFER_WORLD_POSITION, 1)); // G-buffer world position
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SURFEL_BINDING_GBUFFER_NORMAL, 1)); // G-buffer normal
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

    if(!rayTracingState().m_surfelAgeFreeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, 1)); // surfel constants
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, 1)); // pool (write alive = 0)
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_COUNTER, 1)); // counter (FREE_TOP push)
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_FREE_LIST, 1)); // free-list (push)
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

    if(!rayTracingState().m_surfelHashBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, 1)); // surfel constants
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, 1)); // pool
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_CELL_HEAD, 1)); // cell head (write links)
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

    if(!rayTracingState().m_surfelTraceBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // SW scene BVH, instances, light list, and material context. The NWB_GI_SW_BINDING_* ABI is shared with
        // gi_sw_trace.slangi, so the CPU binding layout cannot drift from shader declarations.
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_GI_SW_BINDING_SCENE_SHADING, 1)); // scene shading
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_LIGHT_LIST, 1)); // light list
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_SCENE_NODES, 1)); // scene nodes
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_SCENE_INSTANCES, 1)); // scene instances
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_INSTANCE_MATERIAL, 1)); // instance material
        // Per-mesh geometry is fetched from the global descriptor heap through slots carried by the material record.
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_MATERIAL_TYPED, 1)); // material typed
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_MESH_INSTANCES, 1)); // mesh instances
        // The surfel-specific tail is likewise named in surfel_binding_slots.h. No push constants -- the trace derives
        // the round-robin phase from frameIndex % divisor.
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, 1)); // surfel constants
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, 1)); // surfel pool
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_POOL, 1)); // U4 bounce: prev-frame pool
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_CELL_HEAD, 1)); // U4 bounce: prev-frame cell-head
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
    ;
    // Pin the global descriptor-index heap's resource (set 8) + sampler (set 9) layouts onto the SW surfel-trace
    // pipeline -- the shader-layout-only side of the split: the traversal reads per-mesh geometry through these sets by
    // the host-provided slot index. The classic SW GI layout is added first, so it keeps positional set 0; the two heap
    // layouts carry explicit sets 8/9 and createPipelineLayoutForBindingLayouts gap-fills sets 1-7 with the empty set
    // layout. Guarded on a live heap so builds without one keep the pure set-0 layout. Mirrors the SW-caustic scaffold
    // (rt_caustics.cpp ensureSwCausticPipeline).
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
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
    // ConstantBuffer in every surfel pass, so without the flag the validation layer flags a UNIFORM_BUFFER type/usage
    // + alignment mismatch (VUID-VkWriteDescriptorSet-descriptorType-00330 / -type-11452 / -11461).
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

    // Build the pipelines + persistent binding sets. Target/BVH-dependent binding sets are built by
    // prepareSurfelResources after the per-frame SW scene BVH is resident, so renderSurfelGi only consumes ready
    // resources.
    // The trace pipeline is the ONE backend-specific pass: the HW-shadow branch selects the RayQuery twin, else the SW walk.
    const bool traceReady = rayTracingState().m_surfelUseHwTrace ? ensureSurfelTraceHwPipeline() : ensureSurfelTracePipeline();
    if(!ensureSurfelSpawnPipeline() || !ensureSurfelAgeFreePipeline() || !ensureSurfelHashBuildPipeline() || !traceReady || !ensureSurfelResolvePipeline() || !ensureSurfelUpsamplePipeline() || !ensureSurfelTraceBuildArgsPipeline())
        return false;
    if(!ensureSurfelHashBuildBindingSet() || !ensureSurfelAgeFreeBindingSet())
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The spawn binding set: the surfel buffers + the G-buffer world-position / normal the spawn samples. Rebuilt when the
// G-buffer targets change (on resize); the surfel buffers themselves are persistent.
bool RendererRayTracingSystem::ensureSurfelSpawnBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_surfelSpawnBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelPoolBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCellHeadBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCounterBuffer);
    NWB_ASSERT(rayTracingState().m_surfelFreeListBuffer);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);

    const Core::Texture* worldPosition = targets.worldPosition.get();
    const Core::Texture* normal = targets.normal.get();
    if(
        rayTracingState().m_surfelSpawnBindingSet
        && rayTracingState().m_surfelSpawnBindingSetWorldPosition == worldPosition
        && rayTracingState().m_surfelSpawnBindingSetNormal == normal
    )
        return true;

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, rayTracingState().m_surfelConstants.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, rayTracingState().m_surfelPoolBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_CELL_HEAD, rayTracingState().m_surfelCellHeadBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_COUNTER, rayTracingState().m_surfelCounterBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_FREE_LIST, rayTracingState().m_surfelFreeListBuffer.get()));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SURFEL_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SURFEL_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    rayTracingState().m_surfelSpawnBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelSpawnBindingLayout);
    if(!rayTracingState().m_surfelSpawnBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn binding set"));
        rayTracingState().m_surfelSpawnBindingSetWorldPosition = nullptr;
        rayTracingState().m_surfelSpawnBindingSetNormal = nullptr;
        return false;
    }
    rayTracingState().m_surfelSpawnBindingSetWorldPosition = worldPosition;
    rayTracingState().m_surfelSpawnBindingSetNormal = normal;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The age-free binding set (U1): surfel constants + pool UAV + counter UAV + free-list UAV. All persistent, so it is
// built once (from ensureSurfelResources) and reused, like the hash-build set.
bool RendererRayTracingSystem::ensureSurfelAgeFreeBindingSet(){
    if(rayTracingState().m_surfelAgeFreeBindingSet)
        return true;
    NWB_ASSERT(rayTracingState().m_surfelAgeFreeBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelPoolBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCounterBuffer);
    NWB_ASSERT(rayTracingState().m_surfelFreeListBuffer);

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, rayTracingState().m_surfelConstants.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, rayTracingState().m_surfelPoolBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_COUNTER, rayTracingState().m_surfelCounterBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_FREE_LIST, rayTracingState().m_surfelFreeListBuffer.get()));

    rayTracingState().m_surfelAgeFreeBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelAgeFreeBindingLayout);
    if(!rayTracingState().m_surfelAgeFreeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free binding set"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The hash-build binding set: the surfel buffers only (constants + pool UAV + cell-head UAV). All persistent, so this
// is built once (from ensureSurfelResources) and reused.
bool RendererRayTracingSystem::ensureSurfelHashBuildBindingSet(){
    if(rayTracingState().m_surfelHashBuildBindingSet)
        return true;
    NWB_ASSERT(rayTracingState().m_surfelHashBuildBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelPoolBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCellHeadBuffer);

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, rayTracingState().m_surfelConstants.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, rayTracingState().m_surfelPoolBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_CELL_HEAD, rayTracingState().m_surfelCellHeadBuffer.get()));

    rayTracingState().m_surfelHashBuildBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelHashBuildBindingLayout);
    if(!rayTracingState().m_surfelHashBuildBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build binding set"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The trace binding set: the SW scene-BVH ABI + the surfel constants CB + the surfel pool UAV. Rebuilt when the
// scene-BVH / instance buffers or the distinct-mesh count change, mirroring the SW shadow set's rebuild guard.
bool RendererRayTracingSystem::ensureSurfelTraceBindingSet(){
    NWB_ASSERT(rayTracingState().m_surfelTraceBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelPoolBuffer);
    NWB_ASSERT(rayTracingState().m_surfelPoolSnapshotBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCellHeadSnapshotBuffer);
    NWB_ASSERT(rayTracingState().m_sceneBvhNodeBuffer);
    NWB_ASSERT(rayTracingState().m_sceneInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);

    Core::Buffer* sceneNodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    const u32 meshCount = rayTracingState().m_swShadowMeshCount;
    if(
        rayTracingState().m_surfelTraceBindingSet
        && rayTracingState().m_surfelTraceBindingSetSceneNodes == sceneNodeBuffer
        && rayTracingState().m_surfelTraceBindingSetInstances == instanceBuffer
        && rayTracingState().m_surfelTraceBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_surfelTraceBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_surfelTraceBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_GI_SW_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get())); // scene shading
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get())); // light list
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_SCENE_NODES, sceneNodeBuffer)); // scene nodes
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_SCENE_INSTANCES, instanceBuffer)); // scene instances
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_INSTANCE_MATERIAL, rayTracingState().m_shadowInstanceMaterialBuffer.get())); // instance material
    // Per-mesh geometry is read from the global descriptor heap through material-record slots; the backing buffers
    // remain transitioned for those reads.
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_MATERIAL_TYPED, materialTypedBuffer)); // material typed
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_SW_BINDING_MESH_INSTANCES, meshInstanceBuffer)); // mesh instances
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, rayTracingState().m_surfelConstants.get())); // surfel constants
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, rayTracingState().m_surfelPoolBuffer.get())); // surfel pool
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_POOL, rayTracingState().m_surfelPoolSnapshotBuffer.get())); // U4 bounce: prev-frame pool
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_CELL_HEAD, rayTracingState().m_surfelCellHeadSnapshotBuffer.get())); // U4 bounce: prev-frame cell-head

    rayTracingState().m_surfelTraceBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelTraceBindingLayout);
    if(!rayTracingState().m_surfelTraceBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace binding set"));
        rayTracingState().m_surfelTraceBindingSetSceneNodes = nullptr;
        rayTracingState().m_surfelTraceBindingSetInstances = nullptr;
        rayTracingState().m_surfelTraceBindingSetMaterialTyped = nullptr;
        rayTracingState().m_surfelTraceBindingSetMeshInstances = nullptr;
        rayTracingState().m_surfelTraceBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_surfelTraceBindingSetSceneNodes = sceneNodeBuffer;
    rayTracingState().m_surfelTraceBindingSetInstances = instanceBuffer;
    rayTracingState().m_surfelTraceBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_surfelTraceBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_surfelTraceBindingSetMeshCount = meshCount;
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

    if(!rayTracingState().m_surfelTraceHwBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // 0/1 scene shading + light list (shared with the SW trace); 2 = scene TLAS; 3 = InstanceID-indexed material
        // record; 7/8 = the typed material + mutable-instance context the generated material-surface evaluator reads.
        // Closest-hit reads positions, indices, and attributes through descriptor-heap slots; the driver walks the TLAS.
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_GI_HW_BINDING_SCENE_SHADING, 1)); // scene shading
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_LIGHT_LIST, 1)); // light list
        layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_GI_HW_BINDING_TLAS, 1)); // scene TLAS
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_INSTANCE_MATERIAL, 1)); // instance material
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_MATERIAL_TYPED, 1)); // material typed
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_MESH_INSTANCES, 1)); // mesh instances
        // Surfel tail (constants 12 / pool 13 / snapshot 20/21) -- shared verbatim with the SW trace.
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, 1)); // surfel constants
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, 1)); // surfel pool
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_POOL, 1)); // U4 bounce: prev-frame pool
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_CELL_HEAD, 1)); // U4 bounce: prev-frame cell-head
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
        Core::ShaderArchive::s_DefaultVariant,
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
    // Pin the global descriptor-index heap's resource (set 8) + sampler (set 9) layouts onto the hardware
    // surfel-trace pipeline -- the shader-layout-only side of the split: the inline-RayQuery closest-hit reads each
    // mesh's position / index / attribute buffers through these sets by the host-provided slot index. The classic HW GI
    // layout is added first (positional set 0); the two heap layouts carry explicit sets 8/9 and
    // createPipelineLayoutForBindingLayouts gap-fills sets 1-7. Guarded on a live heap so builds without one keep the
    // pure set-0 layout. Mirrors the SW GI scaffold -- this is a COMPUTE pipeline (inline RayQuery), so the heap binds
    // via bindCompute like the SW paths, not the HW-caustic bindRayTracing.
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();
    if(heap.isInitialized()){
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
    }
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


// The HW trace binding set: the scene TLAS + the HW-resident InstanceID-material record + per-mesh position/index arrays
// (all built by the HW-shadow branch, indexed by material.meshSlot) + the shared surfel tail. Rebuilt when the TLAS /
// instance-material buffer / distinct-mesh count changes, mirroring the HW shadow set's tracked-pointer guard.
bool RendererRayTracingSystem::ensureSurfelTraceHwBindingSet(){
    NWB_ASSERT(rayTracingState().m_surfelTraceHwBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelPoolBuffer);
    NWB_ASSERT(rayTracingState().m_surfelPoolSnapshotBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCellHeadSnapshotBuffer);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);

    Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    Core::Buffer* instanceMaterial = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTyped = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstances = rayTracingState().m_shadowInstanceBuffer.get();
    const u32 meshCount = rayTracingState().m_shadowMeshCount;
    if(
        rayTracingState().m_surfelTraceHwBindingSet
        && rayTracingState().m_surfelTraceHwBindingSetTlas == tlas
        && rayTracingState().m_surfelTraceHwBindingSetInstanceMaterial == instanceMaterial
        && rayTracingState().m_surfelTraceHwBindingSetMaterialTyped == materialTyped
        && rayTracingState().m_surfelTraceHwBindingSetMeshInstances == meshInstances
        && rayTracingState().m_surfelTraceHwBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_GI_HW_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get())); // scene shading
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get())); // light list
    desc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_GI_HW_BINDING_TLAS, tlas)); // scene TLAS
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_INSTANCE_MATERIAL, instanceMaterial)); // instance material
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_MATERIAL_TYPED, materialTyped)); // material typed
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_GI_HW_BINDING_MESH_INSTANCES, meshInstances)); // mesh instances
    // HW GI reads per-mesh positions, indices, and attributes through material-record descriptor-heap slots; backing
    // buffers remain transitioned for those reads.
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SURFEL_BINDING_CONSTANTS, rayTracingState().m_surfelConstants.get())); // surfel constants
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_BINDING_POOL, rayTracingState().m_surfelPoolBuffer.get())); // surfel pool
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_POOL, rayTracingState().m_surfelPoolSnapshotBuffer.get())); // U4 bounce: prev-frame pool
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SURFEL_BINDING_SNAPSHOT_CELL_HEAD, rayTracingState().m_surfelCellHeadSnapshotBuffer.get())); // U4 bounce: prev-frame cell-head

    rayTracingState().m_surfelTraceHwBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelTraceHwBindingLayout);
    if(!rayTracingState().m_surfelTraceHwBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace binding set"));
        rayTracingState().m_surfelTraceHwBindingSetTlas = nullptr;
        rayTracingState().m_surfelTraceHwBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_surfelTraceHwBindingSetMaterialTyped = nullptr;
        rayTracingState().m_surfelTraceHwBindingSetMeshInstances = nullptr;
        rayTracingState().m_surfelTraceHwBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_surfelTraceHwBindingSetTlas = tlas;
    rayTracingState().m_surfelTraceHwBindingSetInstanceMaterial = instanceMaterial;
    rayTracingState().m_surfelTraceHwBindingSetMaterialTyped = materialTyped;
    rayTracingState().m_surfelTraceHwBindingSetMeshInstances = meshInstances;
    rayTracingState().m_surfelTraceHwBindingSetMeshCount = meshCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelResolvePipeline(){
    if(rayTracingState().m_surfelResolvePipeline)
        return true;
    if(rayTracingState().m_surfelResolvePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_surfelResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SURFEL_RESOLVE_BINDING_CONSTANTS, 1)); // surfel constants
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SURFEL_RESOLVE_BINDING_POOL, 1)); // pool (SRV)
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SURFEL_RESOLVE_BINDING_CELL_HEAD, 1)); // cell head (SRV)
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SURFEL_RESOLVE_BINDING_GBUFFER_WORLD_POSITION, 1)); // G-buffer world position
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SURFEL_RESOLVE_BINDING_GBUFFER_NORMAL, 1)); // G-buffer normal
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SURFEL_RESOLVE_BINDING_OUTPUT, 1)); // surfelIrradiance (UAV)
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


// The resolve set: the surfel constants/pool(SRV)/cell-head(SRV) + the G-buffer world-position/normal + the HALF-res
// surfelIrradianceHalf UAV (U6). Rebuilt when any bound target changes (on resize), mirroring the spawn set's guard.
bool RendererRayTracingSystem::ensureSurfelResolveBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_surfelResolveBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelPoolBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCellHeadBuffer);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.surfelIrradianceHalf);

    const Core::Texture* worldPosition = targets.worldPosition.get();
    const Core::Texture* normal = targets.normal.get();
    const Core::Texture* output = targets.surfelIrradianceHalf.get();
    if(
        rayTracingState().m_surfelResolveBindingSet
        && rayTracingState().m_surfelResolveBindingSetWorldPosition == worldPosition
        && rayTracingState().m_surfelResolveBindingSetNormal == normal
        && rayTracingState().m_surfelResolveBindingSetOutput == output
    )
        return true;

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SURFEL_RESOLVE_BINDING_CONSTANTS, rayTracingState().m_surfelConstants.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SURFEL_RESOLVE_BINDING_POOL, rayTracingState().m_surfelPoolBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SURFEL_RESOLVE_BINDING_CELL_HEAD, rayTracingState().m_surfelCellHeadBuffer.get()));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SURFEL_RESOLVE_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SURFEL_RESOLVE_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SURFEL_RESOLVE_BINDING_OUTPUT,
        targets.surfelIrradianceHalf.get(),
        targets.surfelIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    rayTracingState().m_surfelResolveBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelResolveBindingLayout);
    if(!rayTracingState().m_surfelResolveBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve binding set"));
        rayTracingState().m_surfelResolveBindingSetWorldPosition = nullptr;
        rayTracingState().m_surfelResolveBindingSetNormal = nullptr;
        rayTracingState().m_surfelResolveBindingSetOutput = nullptr;
        return false;
    }
    rayTracingState().m_surfelResolveBindingSetWorldPosition = worldPosition;
    rayTracingState().m_surfelResolveBindingSetNormal = normal;
    rayTracingState().m_surfelResolveBindingSetOutput = output;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelUpsamplePipeline(){
    if(rayTracingState().m_surfelUpsamplePipeline)
        return true;
    if(rayTracingState().m_surfelUpsamplePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_surfelUpsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Phase 3 (Backend C): surfel upsample is the first surfel-GI pass migrated to VK_EXT_descriptor_buffer, after
        // the caustic resolve / geometry downsample / accumulator decay passes. Its shape is segment-coherent pure-
        // resource (3 texture SRVs + 1 texture UAV, no samplers) -- and uniquely carries NO push constants (the joint-
        // bilinear filter is driven by the G-buffer alone), the minimal no-push case. The opt-in declares intent only;
        // where the extension is absent the backend downgrades this layout to non-descriptor-buffer-compatible and the
        // classic descriptor-set path (Backend A) serves the pass unchanged, so no device capability gate is needed
        // here.
        layoutDesc.setUseDescriptorBuffer(true);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SURFEL_UPSAMPLE_BINDING_HALF_IRRADIANCE, 1)); // half-res irradiance
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SURFEL_UPSAMPLE_BINDING_GBUFFER_NORMAL, 1)); // full-res G-buffer normal
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SURFEL_UPSAMPLE_BINDING_GBUFFER_WORLD_POSITION, 1)); // full-res G-buffer world position
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SURFEL_UPSAMPLE_BINDING_OUTPUT, 1)); // full-res surfelIrradiance (UAV)
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


// The upsample set: the HALF-res surfel irradiance (SRV) + the full-res G-buffer normal/world-position (SRVs) + the
// full-res surfelIrradiance (UAV). Rebuilt when any bound target changes (on resize), mirroring the resolve set's guard.
bool RendererRayTracingSystem::ensureSurfelUpsampleBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_surfelUpsampleBindingLayout);
    NWB_ASSERT(targets.surfelIrradianceHalf);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.surfelIrradiance);

    const Core::Texture* halfIrradiance = targets.surfelIrradianceHalf.get();
    const Core::Texture* normal = targets.normal.get();
    const Core::Texture* worldPosition = targets.worldPosition.get();
    const Core::Texture* output = targets.surfelIrradiance.get();
    if(
        rayTracingState().m_surfelUpsampleBindingSet
        && rayTracingState().m_surfelUpsampleBindingSetHalfIrradiance == halfIrradiance
        && rayTracingState().m_surfelUpsampleBindingSetNormal == normal
        && rayTracingState().m_surfelUpsampleBindingSetWorldPosition == worldPosition
        && rayTracingState().m_surfelUpsampleBindingSetOutput == output
    )
        return true;

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SURFEL_UPSAMPLE_BINDING_HALF_IRRADIANCE,
        targets.surfelIrradianceHalf.get(),
        targets.surfelIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SURFEL_UPSAMPLE_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SURFEL_UPSAMPLE_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SURFEL_UPSAMPLE_BINDING_OUTPUT,
        targets.surfelIrradiance.get(),
        targets.surfelIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    rayTracingState().m_surfelUpsampleBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelUpsampleBindingLayout);
    if(!rayTracingState().m_surfelUpsampleBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample binding set"));
        rayTracingState().m_surfelUpsampleBindingSetHalfIrradiance = nullptr;
        rayTracingState().m_surfelUpsampleBindingSetNormal = nullptr;
        rayTracingState().m_surfelUpsampleBindingSetWorldPosition = nullptr;
        rayTracingState().m_surfelUpsampleBindingSetOutput = nullptr;
        return false;
    }
    rayTracingState().m_surfelUpsampleBindingSetHalfIrradiance = halfIrradiance;
    rayTracingState().m_surfelUpsampleBindingSetNormal = normal;
    rayTracingState().m_surfelUpsampleBindingSetWorldPosition = worldPosition;
    rayTracingState().m_surfelUpsampleBindingSetOutput = output;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelTraceBuildArgsPipeline(){
    if(rayTracingState().m_surfelTraceBuildArgsPipeline)
        return true;
    if(rayTracingState().m_surfelTraceBuildArgsPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_surfelTraceBuildArgsBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SURFEL_TRACE_BUILDARGS_BINDING_CONSTANTS, 1)); // surfel constants (divisor .w)
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_TRACE_BUILDARGS_BINDING_COUNTER, 1)); // counter (read BUMP_TOP)
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SURFEL_TRACE_BUILDARGS_BINDING_ARGS, 1)); // indirect args (write)
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


// The build-args set binds only PERSISTENT buffers (surfel constants + counter + the indirect-args buffer), so it is
// built ONCE and reused (no per-target rebuild, mirroring the hash-build/age-free sets).
bool RendererRayTracingSystem::ensureSurfelTraceBuildArgsBindingSet(){
    if(rayTracingState().m_surfelTraceBuildArgsBindingSet)
        return true;

    NWB_ASSERT(rayTracingState().m_surfelTraceBuildArgsBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelCounterBuffer);
    NWB_ASSERT(rayTracingState().m_surfelTraceIndirectArgsBuffer);

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SURFEL_TRACE_BUILDARGS_BINDING_CONSTANTS, rayTracingState().m_surfelConstants.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_TRACE_BUILDARGS_BINDING_COUNTER, rayTracingState().m_surfelCounterBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SURFEL_TRACE_BUILDARGS_BINDING_ARGS, rayTracingState().m_surfelTraceIndirectArgsBuffer.get()));

    rayTracingState().m_surfelTraceBuildArgsBindingSet = graphics().getDevice()->createBindingSet(desc, rayTracingState().m_surfelTraceBuildArgsBindingLayout);
    if(!rayTracingState().m_surfelTraceBuildArgsBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args binding set"));
        return false;
    }
    return true;
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

    // The trace binding set is the ONE backend-specific set: HW (TLAS + HW-resident per-mesh) or SW (scene BVH).
    const bool traceSetReady = rayTracingState().m_surfelUseHwTrace ? ensureSurfelTraceHwBindingSet() : ensureSurfelTraceBindingSet();
    if(
        !ensureSurfelSpawnBindingSet(targets)
        || !ensureSurfelHashBuildBindingSet()
        || !traceSetReady
        || !ensureSurfelResolveBindingSet(targets)
        || !ensureSurfelUpsampleBindingSet(targets)
        || !ensureSurfelTraceBuildArgsBindingSet()
    )
        return false;

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
    commandList.setBufferState(cb, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderSurfelGi(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    // The trace pass runs the SELECTED backend (U5): the HW RayQuery twin on the HW-shadow branch, else the SW BVH walk.
    // Everything else (snapshot copy, age-free/clear/hash-build/spawn/resolve) is backend-agnostic.
    const bool useHwTrace = rayTracingState().m_surfelUseHwTrace;
    Core::ComputePipeline* const tracePipeline = useHwTrace ? rayTracingState().m_surfelTraceHwPipeline.get() : rayTracingState().m_surfelTracePipeline.get();
    Core::BindingSet* const traceBindingSet = useHwTrace ? rayTracingState().m_surfelTraceHwBindingSet.get() : rayTracingState().m_surfelTraceBindingSet.get();

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
        !rayTracingState().m_surfelSpawnBindingSet
        || !rayTracingState().m_surfelAgeFreeBindingSet
        || !rayTracingState().m_surfelHashBuildBindingSet
        || !traceBindingSet
        || !rayTracingState().m_surfelResolveBindingSet
        || !rayTracingState().m_surfelUpsampleBindingSet
        || !rayTracingState().m_surfelTraceBuildArgsBindingSet
    )
        return true;

    // The age-free / hash-build passes dispatch over the full pool (they touch every slot); the trace dispatches per
    // LIVE surfel via (3b)'s indirect args and derives its round-robin phase from the CB divisor.
    const u32 poolCapacity = rayTracingState().m_surfelPoolCapacity;

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

    // (0) Age-free (U1 recycling): one thread per pool slot; free surfels unseen for maxAge frames (alive = 0) and PUSH
    // their ids onto the free-list for the spawn to reuse. Runs FIRST -- it reads lastSeenFrame written by the PREVIOUS
    // frame's spawn keep-alive, and frees the slots BEFORE the hash-build re-links live surfels + the spawn pops. The
    // push (here) and the spawn's pop are barrier-separated (clear + hash-build between), so the free-list stack has no
    // concurrent push/pop -> no ABA. The linear workgroup width is shared with the shader.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, graphics().getDevice(), commandList);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelAgeFreeBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelAgeFreePipeline.get());
        state.addBindingSet(rayTracingState().m_surfelAgeFreeBindingSet.get());
        commandList.setComputeState(state);
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
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelHashBuildBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelHashBuildPipeline.get());
        state.addBindingSet(rayTracingState().m_surfelHashBuildBindingSet.get());
        commandList.setComputeState(state);
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    // (3) Spawn: one thread per screen tile. Reads the freshly-built cell head; where a cell is still empty, atomically
    // claims it and bump-allocates one surfel (one surfel per hash bucket). [numthreads(8,8,1)] over (screen / spawnTile).
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelSpawn, graphics().getDevice(), commandList);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelSpawnBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelSpawnPipeline.get());
        state.addBindingSet(rayTracingState().m_surfelSpawnBindingSet.get());
        commandList.setComputeState(state);
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
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelTraceBuildArgsBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelTraceBuildArgsPipeline.get());
        state.addBindingSet(rayTracingState().m_surfelTraceBuildArgsBindingSet.get());
        commandList.setComputeState(state);
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
    // + the same shadow-owned material context. setResourceStatesForBindingSet covers the TLAS + the InstanceID-material
    // record + the shared surfel tail.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, graphics().getDevice(), commandList);
        if(useHwTrace){
            for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
                commandList.setBufferState(rayTracingState().m_shadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            }
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
            commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        commandList.setResourceStatesForBindingSet(traceBindingSet);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(tracePipeline);
        state.addBindingSet(traceBindingSet);
        // The args buffer carries the workgroup count; setComputeState auto-transitions it UnorderedAccess->IndirectArgument.
        state.setIndirectParams(rayTracingState().m_surfelTraceIndirectArgsBuffer.get());
        commandList.setComputeState(state);
        // Both surfel shaders access per-mesh geometry through the descriptor heap, so bind its tables against the
        // selected pipeline before dispatch. bindCompute touches only sets 8/9; non-bindless builds skip it.
        {
            Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
            if(heap.isInitialized())
                heap.bindCompute(commandList, *tracePipeline);
        }
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
        commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_surfelCellHeadBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelResolveBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelResolvePipeline.get());
        state.addBindingSet(rayTracingState().m_surfelResolveBindingSet.get());
        commandList.setComputeState(state);
        const u32 groupSize = static_cast<u32>(NWB_SURFEL_RESOLVE_GROUP_SIZE);
        commandList.dispatch(DivideUp(halfWidth, groupSize), DivideUp(halfHeight, groupSize), 1u);
    }

    // (5b) Upsample (FULL-res, U6): reconstruct the full-res surfelIrradiance from the half-res resolve with a surface-
    // gated joint-bilinear filter (no bleed across silhouettes/creases; irradiance is HDR so no clamp; coverage preserved
    // so the lighting contract is unchanged). Sync the half-res UAV write -> the upsample's SRV read first.
    commandList.setTextureState(targets.surfelIrradianceHalf.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelUpsample, graphics().getDevice(), commandList);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelUpsampleBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelUpsamplePipeline.get());
        state.addBindingSet(rayTracingState().m_surfelUpsampleBindingSet.get());
        commandList.setComputeState(state);
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

