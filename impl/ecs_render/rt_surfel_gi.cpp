// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_private.h"


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
        // SW scene BVH + instance + light list + per-mesh descriptor arrays (slots 0-10; identical to the SW
        // shadow/caustic trace -- the trace body is gi_sw_trace.slangi, reused verbatim).
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(0, 1)); // scene shading
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1)); // light list
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1)); // scene nodes
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1)); // scene instances
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(4, 1)); // instance material
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, NWB_SW_SHADOW_MAX_MESHES)); // mesh nodes
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(6, NWB_SW_SHADOW_MAX_MESHES)); // mesh positions
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(7, NWB_SW_SHADOW_MAX_MESHES)); // mesh indices
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(8, NWB_SW_SHADOW_MAX_MESHES)); // mesh attributes
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(9, 1)); // material typed
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(10, 1)); // mesh instances
        // Surfel bindings (11 = constants CB, 12 = pool UAV, 19/20 = prev-frame snapshot pool/cell-head SRVs the U4
        // bounce gather reads). No push constants -- the trace derives the round-robin phase from frameIndex % divisor.
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
    // (NOT DeferredFrameTargets) so a window resize does not reset surfel convergence. See .helper/surfel_gi_plan.md.
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

    // Build the pipelines + the hash-build binding set (which depends only on the persistent buffers, so it is built
    // once here). The spawn + trace sets depend on per-frame targets/BVH, so they build in renderSurfelGi. Non-fatal:
    // a sub-failure leaves the pipeline handle null and the render dispatch guards each on its own handle.
    if(!ensureSurfelSpawnPipeline() || !ensureSurfelAgeFreePipeline() || !ensureSurfelHashBuildPipeline() || !ensureSurfelTracePipeline() || !ensureSurfelResolvePipeline())
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


// The trace binding set: the SW scene BVH (slots 0-10; the same geometry the SW shadow trace uses) + the surfel
// constants CB + the surfel pool UAV. Rebuilt when the scene-BVH / instance buffers or the distinct-mesh count change,
// mirroring the SW shadow set's rebuild guard.
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
    desc.addItem(Core::BindingSetItem::ConstantBuffer(0, deferredState().m_sceneShadingBuffer.get())); // scene shading
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, deferredState().m_lightBuffer.get())); // light list
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, sceneNodeBuffer)); // scene nodes
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, instanceBuffer)); // scene instances
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(4, rayTracingState().m_shadowInstanceMaterialBuffer.get())); // instance material
    // Per-mesh descriptor arrays: bind every slot; pad the unused tail with the last real mesh (the trace only indexes
    // meshIndex < meshCount), mirroring the SW shadow set.
    for(u32 slot = 0u; slot < NWB_SW_SHADOW_MAX_MESHES; ++slot){
        const u32 source = (slot < meshCount) ? slot : (meshCount > 0u ? (meshCount - 1u) : 0u);
        desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, rayTracingState().m_swShadowMeshNodeBuffers[source]).setArrayElement(slot));
        desc.addItem(Core::BindingSetItem::RawBuffer_SRV(6, rayTracingState().m_swShadowMeshPositionBuffers[source]).setArrayElement(slot));
        desc.addItem(Core::BindingSetItem::RawBuffer_SRV(7, rayTracingState().m_swShadowMeshIndexBuffers[source]).setArrayElement(slot));
        desc.addItem(Core::BindingSetItem::RawBuffer_SRV(8, rayTracingState().m_swShadowMeshAttributeBuffers[source]).setArrayElement(slot));
    }
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(9, materialTypedBuffer)); // material typed
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(10, meshInstanceBuffer)); // mesh instances
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


// The resolve set: the surfel constants/pool(SRV)/cell-head(SRV) + the G-buffer world-position/normal + the
// surfelIrradiance UAV. Rebuilt when any bound target changes (on resize), mirroring the spawn set's guard.
bool RendererRayTracingSystem::ensureSurfelResolveBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_surfelResolveBindingLayout);
    NWB_ASSERT(rayTracingState().m_surfelConstants);
    NWB_ASSERT(rayTracingState().m_surfelPoolBuffer);
    NWB_ASSERT(rayTracingState().m_surfelCellHeadBuffer);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.surfelIrradiance);

    const Core::Texture* worldPosition = targets.worldPosition.get();
    const Core::Texture* normal = targets.normal.get();
    const Core::Texture* output = targets.surfelIrradiance.get();
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
        targets.surfelIrradiance.get(),
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

    // Guard: every pass needs its pipeline (a prior ensure failure leaves the block inert this frame).
    if(
        !rayTracingState().m_surfelSpawnPipeline
        || !rayTracingState().m_surfelAgeFreePipeline
        || !rayTracingState().m_surfelHashBuildPipeline
        || !rayTracingState().m_surfelTracePipeline
        || !rayTracingState().m_surfelResolvePipeline
    )
        return true;

    // Build the per-frame binding sets: the spawn + resolve sets need the G-buffer + surfelIrradiance (recreated on
    // resize); the trace set needs the SW scene BVH (built this frame in prepareShadowVisibilityResources). Non-fatal:
    // skip the whole block on failure.
    if(
        !ensureSurfelSpawnBindingSet(targets)
        || !ensureSurfelHashBuildBindingSet()
        || !ensureSurfelTraceBindingSet()
        || !ensureSurfelResolveBindingSet(targets)
        || !rayTracingState().m_surfelSpawnBindingSet
        || !rayTracingState().m_surfelAgeFreeBindingSet
        || !rayTracingState().m_surfelHashBuildBindingSet
        || !rayTracingState().m_surfelTraceBindingSet
        || !rayTracingState().m_surfelResolveBindingSet
    )
        return true;

    const u32 poolCapacity = rayTracingState().m_surfelPoolCapacity;
    const u32 updateDivisor = rayTracingState().m_surfelSeeded ? Max<u32>(NWB_SURFEL_UPDATE_DIVISOR, 1u) : 1u;
    const u32 activeSurfelCount = DivideUp(poolCapacity, updateDivisor);

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
    // concurrent push/pop -> no ABA. [numthreads(64,1,1)].
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, graphics().getDevice(), commandList);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelAgeFreeBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelAgeFreePipeline.get());
        state.addBindingSet(rayTracingState().m_surfelAgeFreeBindingSet.get());
        commandList.setComputeState(state);
        commandList.dispatch(DivideUp(poolCapacity, 64u), 1u, 1u);
    }

    // (1) Clear the cell-head to empty, then rebuild the hash from the live pool BEFORE the spawn, so the spawn sees this
    // frame's exact occupancy (a non-empty cell head == a surfel already covers the cell) and fills only empty cells.
    {
        Core::Buffer* cellHead = rayTracingState().m_surfelCellHeadBuffer.get();
        commandList.setBufferState(cellHead, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(cellHead, NWB_SURFEL_CELL_INVALID);
    }

    // (2) Hash-build: one thread per pool slot; link each live surfel into its cell's list. [numthreads(64,1,1)].
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelHashBuild, graphics().getDevice(), commandList);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelHashBuildBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelHashBuildPipeline.get());
        state.addBindingSet(rayTracingState().m_surfelHashBuildBindingSet.get());
        commandList.setComputeState(state);
        commandList.dispatch(DivideUp(poolCapacity, 64u), 1u, 1u);
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

    // (4) Trace: one workgroup per active surfel (64 threads = 64 hemisphere rays through the SW scene BVH). Stage the
    // per-mesh geometry + shadow-owned material context to ShaderResource for the trace, then dispatch.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, graphics().getDevice(), commandList);
        for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
            commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        }
        commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_surfelTraceBindingSet.get());
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelTracePipeline.get());
        state.addBindingSet(rayTracingState().m_surfelTraceBindingSet.get());
        commandList.setComputeState(state);
        commandList.dispatch(activeSurfelCount, 1u, 1u);
    }

    // (5) Resolve: one thread per screen pixel, gather the surfel field (pool + cell-head as COMPUTE SRVs) + the
    // G-buffer, write the screen-space surfelIrradiance the deferred lighting samples. This is what keeps the pool off
    // the pixel shader (compute-only), eliminating the frames-in-flight pool race. The pool/cell-head UAV writes (trace/
    // hash-build) are synced to SRV here; the surfelIrradiance UAV write is synced to SRV for the lighting after.
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

