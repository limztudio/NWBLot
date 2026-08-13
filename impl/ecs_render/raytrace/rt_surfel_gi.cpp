// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <core/graphics/task_graph/compiled_graph.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU/shader ABI mirror for surfel constants.
struct NwbSurfelConstantsGpu{
    Float4 cameraPositionCellSize;  // xyz = camera world position, w = hash cell size
    Float4 hashPoolFrameDivisor;    // x = hash cell count, y = pool capacity, z = frame index, w = update divisor
    Float4 coverageRadiusBiasHyst;  // x = reserved (coverage sum dropped for one-surfel-per-cell), y = default radius, z = normal bias, w = accumulation cap
    Float4 ageRaysTileScreen;       // x = max age, y = maximum rays/surfel, z = spawn tile (px), w = screen width
    Float4 screenHeightPad;         // x = screen height, yzw = pad
};
static_assert(sizeof(NwbSurfelConstantsGpu) == sizeof(Float4) * NWB_SURFEL_CONSTANTS_FLOAT4_COUNT, "NwbSurfelConstantsGpu must match the shader NwbSurfelConstants layout");

// Offset trace and gather points to avoid self-intersection.
inline constexpr f32 s_SurfelNormalBias = 0.05f;

template<typename StateT>
[[nodiscard]] NwbSurfelConstantsGpu BuildSurfelFrameConstants(
    const StateT& state,
    const DeferredFrameTargets& targets
){
    // First frame traces every surfel; later frames use round-robin updates.
    const u32 updateDivisor = state.m_surfelSeeded ? Max<u32>(NWB_SURFEL_UPDATE_DIVISOR, 1u) : 1u;
    const f32 cellSize = NWB_SURFEL_CELL_SIZE;

    NwbSurfelConstantsGpu params;
    params.cameraPositionCellSize = Float4(0.0f, 0.0f, 0.0f, cellSize);
    params.hashPoolFrameDivisor = Float4(
        static_cast<f32>(state.m_surfelHashCellCount),
        static_cast<f32>(state.m_surfelPoolCapacity),
        static_cast<f32>(state.m_surfelFrameIndex),
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
    return params;
}

// Delayed counter readback avoids stalling on the asynchronous copy.
inline constexpr u32 s_SurfelCountLogInterval = 120u;
inline constexpr u32 s_SurfelCountLogDelay = 3u;


namespace __hidden_surfel_gi_task{


struct SurfelGiAgeFreeGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.graphics
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(!queue || payload.asyncTiming->has_value())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(queue->queueClass == Core::CommandQueue::Compute){
            payload.asyncTiming->emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncSurfelGi,
                payload.graphics->getDevice(),
                commandList
            );
        }

        if(!payload.raytracingSystem->renderSurfelGiAgeFree(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        )){
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            return false;
        }
        // The timestamp endpoint follows in the remaining-GI callback, but this callback's nested marker must
        // close before the packet recorder advances to the graph-owned cell-head clear task.
        if(payload.asyncTiming->has_value())
            payload.asyncTiming->value().finishMarker();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
    }
};


struct SurfelGiHashBuildGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming->has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.raytracingSystem->renderSurfelGiHashBuild(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        )){
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            return false;
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
    }
};


struct SurfelGiSpawnGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming->has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.raytracingSystem->renderSurfelGiSpawn(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        )){
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            return false;
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
    }
};


struct SurfelGiTraceBuildArgsGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming->has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.raytracingSystem->renderSurfelGiTraceBuildArgs(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        )){
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            return false;
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
    }
};


struct SurfelGiTraceGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming->has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.raytracingSystem->renderSurfelGiTrace(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        )){
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            return false;
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
    }
};


struct SurfelGiResolveGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.raytracingSystem
            || !payload.targets
            || !payload.timingTicket
            || !payload.asyncTiming
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming->has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(!payload.raytracingSystem->renderSurfelGiResolve(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        )){
            if(payload.asyncTiming->has_value()){
                payload.asyncTiming->value().discardTiming();
                payload.asyncTiming->reset();
            }
            return false;
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
    }
};


struct SurfelGiGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
        bool graphEntryStatesOwned = false;
        bool graphOwnsCellHeadClear = false;
        bool graphOwnsHashBuild = false;
        bool graphOwnsSpawn = false;
        bool graphOwnsTraceBuildArgs = false;
        bool graphOwnsTrace = false;
        bool graphOwnsResolve = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(!payload.raytracingSystem || !payload.graphics || !payload.targets || !payload.timingTicket)
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.graph.queueInfo(context.queue);
        if(!queue)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.graphOwnsCellHeadClear){
            if(
                queue->queueClass == Core::CommandQueue::Compute
                && (!payload.asyncTiming || !payload.asyncTiming->has_value())
            )
                return false;
            if(!payload.raytracingSystem->renderSurfelGiAfterAgeFree(
                commandList,
                *payload.targets,
                payload.graphEntryStatesOwned,
                true,
                payload.graphOwnsHashBuild,
                payload.graphOwnsSpawn,
                payload.graphOwnsTraceBuildArgs,
                payload.graphOwnsTrace,
                payload.graphOwnsResolve
            )){
                if(payload.asyncTiming && payload.asyncTiming->has_value()){
                    payload.asyncTiming->value().discardTiming();
                    payload.asyncTiming->reset();
                }
                return false;
            }
            if(payload.asyncTiming && payload.asyncTiming->has_value()){
                payload.asyncTiming->value().finishTiming(commandList);
                payload.asyncTiming->reset();
            }
            return true;
        }

        Optional<Core::GpuTimingMeasure> asyncTiming;
        if(queue->queueClass == Core::CommandQueue::Compute){
            asyncTiming.emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncSurfelGi,
                payload.graphics->getDevice(),
                commandList
            );
        }

        if(!payload.raytracingSystem->renderSurfelGi(
            commandList,
            *payload.targets,
            payload.graphEntryStatesOwned
        ))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI render pass failed"));

        if(asyncTiming){
            asyncTiming->finishTiming(commandList);
            asyncTiming.reset();
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.asyncTiming && payload.asyncTiming->has_value()){
            payload.asyncTiming->value().discardTiming();
            payload.asyncTiming->reset();
        }
    }

};


};


struct RendererRayTracingSystem::SurfelGiInitializationGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        bool graphEntryStatesOwned = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return payload.raytracingSystem
            && payload.raytracingSystem->initializeSurfelResources(
                commandList,
                payload.graphEntryStatesOwned
            );
    }

    static void discarded(Payload& payload){
        if(payload.raytracingSystem)
            payload.raytracingSystem->discardSurfelResourceInitialization();
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.raytracingSystem)
            payload.raytracingSystem->finalizeSurfelResourceInitialization();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSurfelSpawnPipeline(){
    if(rayTracingState().m_surfelSpawnPipeline)
        return true;
    if(rayTracingState().m_surfelSpawnPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel spawn requires the initialized global descriptor heap"));
        rayTracingState().m_surfelSpawnPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelSpawnBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelSpawnBindingLayout = device.createBindingLayout(layoutDesc);
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
    rayTracingState().m_surfelSpawnPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelSpawnPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn compute pipeline"));
        rayTracingState().m_surfelSpawnPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel spawn compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelAgeFreePipeline(){
    if(rayTracingState().m_surfelAgeFreePipeline)
        return true;
    if(rayTracingState().m_surfelAgeFreePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel age-free requires the initialized global descriptor heap"));
        rayTracingState().m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelAgeFreeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelAgeFreeBindingLayout = device.createBindingLayout(layoutDesc);
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
    rayTracingState().m_surfelAgeFreePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelAgeFreePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free compute pipeline"));
        rayTracingState().m_surfelAgeFreePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel age-free compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelHashBuildPipeline(){
    if(rayTracingState().m_surfelHashBuildPipeline)
        return true;
    if(rayTracingState().m_surfelHashBuildPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel hash-build requires the initialized global descriptor heap"));
        rayTracingState().m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelHashBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelHashBuildBindingLayout = device.createBindingLayout(layoutDesc);
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
    rayTracingState().m_surfelHashBuildPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelHashBuildPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build compute pipeline"));
        rayTracingState().m_surfelHashBuildPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel hash-build compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelTracePipeline(){
    if(rayTracingState().m_surfelTracePipeline)
        return true;
    if(rayTracingState().m_surfelTracePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace requires the initialized global descriptor heap"));
        rayTracingState().m_surfelTracePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelTraceBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelTraceBindingLayout = device.createBindingLayout(layoutDesc);
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
    rayTracingState().m_surfelTracePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelTracePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace compute pipeline"));
        rayTracingState().m_surfelTracePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel trace compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelResources(){
    // Persistent field storage survives window resizes.
    if(!hasSurfelWork())
        return true;

    const u32 poolCapacity = rayTracingState().m_surfelPoolCapacity;
    const u32 cellCount = rayTracingState().m_surfelHashCellCount;
    if(poolCapacity == 0u || cellCount == 0u)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI requires the initialized global descriptor heap"));
        return false;
    }

    // Fresh pool storage needs one clear before tracing.
    if(!rayTracingState().m_surfelPoolBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity)
            .setStructStride(NWB_SURFEL_RECORD_SIZE)
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
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

    // Hash heads use 0xFFFFFFFF as the empty sentinel.
    if(!rayTracingState().m_surfelCellHeadBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * cellCount)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
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

    // Build-args rewrites the indirect dispatch buffer each frame.
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

    // Age-free pushes ids and spawn pops them from this persistent free list.
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

    // Bounce gathers read a stable previous-frame pool snapshot.
    if(!rayTracingState().m_surfelPoolSnapshotBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(NWB_SURFEL_RECORD_SIZE) * poolCapacity)
            .setStructStride(NWB_SURFEL_RECORD_SIZE)
            .setCanHaveUAVs(false)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_pool_snapshot"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelPoolSnapshotBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelPoolSnapshotBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel pool snapshot buffer"));
            return false;
        }
    }

    // Snapshot hash heads alongside the pool for a consistent bounce gather.
    if(!rayTracingState().m_surfelCellHeadSnapshotBuffer){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * cellCount)
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(false)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_cell_head_snapshot"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelCellHeadSnapshotBuffer = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelCellHeadSnapshotBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel cell-head snapshot buffer"));
            return false;
        }
    }

    if(!rayTracingState().m_surfelCounterReadback){
        Core::BufferDesc desc;
        desc
            .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE)
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .setDebugName(Name("surfel_counter_readback"))
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        rayTracingState().m_surfelCounterReadback = graphics().createBuffer(desc);
        if(!rayTracingState().m_surfelCounterReadback){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel counter readback buffer"));
            return false;
        }
    }

    // Every surfel pass binds this per-frame parameter buffer as a ConstantBuffer.
    if(!rayTracingState().m_surfelConstants){
        Core::BufferDesc cbDesc;
        cbDesc
            .setByteSize(sizeof(NwbSurfelConstantsGpu))
            .setIsConstantBuffer(true)
            // Graphics upload and async consume share this selector.
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("surfel_constants"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_surfelConstants = graphics().createBuffer(cbDesc);
        if(!rayTracingState().m_surfelConstants){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel constant buffer"));
            return false;
        }
    }

    // Persistent descriptors own backing resources until deferred retirement.
    if(
        !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelConstants.get(), Core::GpuDescriptorClass::UniformBuffer, false, rayTracingState().m_surfelConstantsHeapHandle)
        || !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelPoolBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelPoolHeapHandle)
        || !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelCellHeadBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelCellHeadHeapHandle)
        || !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelCounterHeapHandle)
        || !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelTraceIndirectArgsHeapHandle)
        || !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelFreeListBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true, rayTracingState().m_surfelFreeListHeapHandle)
        || !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelPoolSnapshotBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, false, rayTracingState().m_surfelPoolSnapshotHeapHandle)
        || !__hidden_raytracing_system::EnsureHeapBuffer(heap, *rayTracingState().m_surfelCellHeadSnapshotBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, false, rayTracingState().m_surfelCellHeadSnapshotHeapHandle)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register persistent surfel resources in the descriptor heap"));
        return false;
    }

    // Only tracing differs between hardware and software surfel paths.
    const bool traceReady = rayTracingState().m_surfelUseHwTrace ? ensureSurfelTraceHwPipeline() : ensureSurfelTracePipeline();
    if(!ensureSurfelSpawnPipeline() || !ensureSurfelAgeFreePipeline() || !ensureSurfelHashBuildPipeline() || !traceReady || !ensureSurfelResolvePipeline() || !ensureSurfelUpsamplePipeline() || !ensureSurfelTraceBuildArgsPipeline())
        return false;
    return true;
}

// Hardware trace twin; other surfel passes are shared.
bool RendererRayTracingSystem::ensureSurfelTraceHwPipeline(){
    if(rayTracingState().m_surfelTraceHwPipeline)
        return true;
    if(rayTracingState().m_surfelTraceHwPipelineFailed)
        return false;

    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct) || !graphics().queryFeatureSupport(Core::Feature::RayQuery)){
        rayTracingState().m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel HW trace requires the descriptor-buffer TLAS heap layout"));
        rayTracingState().m_surfelTraceHwPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelTraceHwBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelTraceHwBindingLayout = device.createBindingLayout(layoutDesc);
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
    // Preserve resource, sampler, and TLAS heap sets for hardware trace.
    pipelineDesc
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    rayTracingState().m_surfelTraceHwPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelTraceHwPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace compute pipeline"));
        rayTracingState().m_surfelTraceHwPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel HW trace compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelResolvePipeline(){
    if(rayTracingState().m_surfelResolvePipeline)
        return true;
    if(rayTracingState().m_surfelResolvePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel resolve requires the initialized global descriptor heap"));
        rayTracingState().m_surfelResolvePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelResolveBindingLayout = device.createBindingLayout(layoutDesc);
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
    rayTracingState().m_surfelResolvePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve compute pipeline"));
        rayTracingState().m_surfelResolvePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel resolve compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelUpsamplePipeline(){
    if(rayTracingState().m_surfelUpsamplePipeline)
        return true;
    if(rayTracingState().m_surfelUpsamplePipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel upsample requires the initialized global descriptor heap"));
        rayTracingState().m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelUpsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelUpsampleBindingLayout = device.createBindingLayout(layoutDesc);
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
    rayTracingState().m_surfelUpsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelUpsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample compute pipeline"));
        rayTracingState().m_surfelUpsamplePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel upsample compute pipeline"));
    return true;
}

bool RendererRayTracingSystem::ensureSurfelTraceBuildArgsPipeline(){
    if(rayTracingState().m_surfelTraceBuildArgsPipeline)
        return true;
    if(rayTracingState().m_surfelTraceBuildArgsPipelineFailed)
        return false;

    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace build-args requires the initialized global descriptor heap"));
        rayTracingState().m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    if(!rayTracingState().m_surfelTraceBuildArgsBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        rayTracingState().m_surfelTraceBuildArgsBindingLayout = device.createBindingLayout(layoutDesc);
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
    rayTracingState().m_surfelTraceBuildArgsPipeline = device.createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_surfelTraceBuildArgsPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args compute pipeline"));
        rayTracingState().m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel trace build-args compute pipeline"));
    return true;
}

void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    auto& device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelConstantsHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelPoolHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelCellHeadHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelCounterHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelTraceIndirectArgsHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelFreeListHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelPoolSnapshotHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelCellHeadSnapshotHeapHandle);
        __hidden_raytracing_system::RetireHeapHandle(heap, rayTracingState().m_surfelMaterialContextSlotsHeapHandle);
        return;
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

bool RendererRayTracingSystem::hasSurfelWork()const noexcept{
    return rayTracingState().m_surfelEnabled;
}

bool RendererRayTracingSystem::shouldCaptureSurfelCountReadback()const noexcept{
    return hasSurfelWork()
        && rayTracingState().m_surfelCounterBuffer
        && rayTracingState().m_surfelCounterReadback
        && !rayTracingState().m_surfelCountReadbackSubmissionToken.valid()
        && (rayTracingState().m_surfelFrameIndex % s_SurfelCountLogInterval) == 0u
    ;
}

void RendererRayTracingSystem::markSurfelCountReadbackScheduled()noexcept{
    rayTracingState().m_surfelCountReadbackFrame = rayTracingState().m_surfelFrameIndex;
}

bool RendererRayTracingSystem::needsSurfelResourceInitialization()const noexcept{
    return hasSurfelWork() && rayTracingState().m_surfelResourcesNeedClear;
}

bool RendererRayTracingSystem::initializeSurfelResources(
    Core::CommandList& commandList,
    const bool graphEntryStatesOwned
){
    if(!rayTracingState().m_surfelResourcesNeedClear)
        return true;

    Core::Buffer* pool = rayTracingState().m_surfelPoolBuffer.get();
    Core::Buffer* cellHead = rayTracingState().m_surfelCellHeadBuffer.get();
    Core::Buffer* counter = rayTracingState().m_surfelCounterBuffer.get();
    Core::Buffer* freeList = rayTracingState().m_surfelFreeListBuffer.get();
    if(!pool || !cellHead || !counter || !freeList)
        return false;

    // The normal graph establishes CopyDest in its packet prologue. Direct compatibility callers retain their
    // local setup; both routes leave the clear body and following snapshot's final CopyDest state unchanged.
    if(!graphEntryStatesOwned){
        commandList.setBufferState(pool, Core::ResourceStates::CopyDest);
        commandList.setBufferState(cellHead, Core::ResourceStates::CopyDest);
        commandList.setBufferState(counter, Core::ResourceStates::CopyDest);
        commandList.setBufferState(freeList, Core::ResourceStates::CopyDest);
    }
    commandList.commitBarriers();
    commandList.clearBufferUInt(pool, 0u);
    commandList.clearBufferUInt(cellHead, NWB_SURFEL_CELL_INVALID);
    commandList.clearBufferUInt(counter, 0u);
    commandList.clearBufferUInt(freeList, 0u);   // contents cosmetic; counter FREE_TOP=0 is what marks it empty
    rayTracingState().m_surfelResourcesClearPending = true;
    return true;
}

bool RendererRayTracingSystem::prepareSurfelResources(DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    if(!ensureSurfelResources())
        return false;

    // Register the remaining heap-selected trace context.
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(
        !targets.bindless.valid()
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !rayTracingState().m_rayTraceMaterialContextSlotsBuffer
        || !__hidden_raytracing_system::EnsureHeapBuffer(
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

    return true;
}

bool RendererRayTracingSystem::retainPreparedSurfelFrameConstantsUpload(
    Core::GpuTaskGraph& graph,
    const DeferredFrameTargets& targets,
    Core::GpuUploadBlobId& outBlob
)const{
    outBlob = {};
    if(!hasSurfelWork())
        return true;
    if(!rayTracingState().m_surfelConstants){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI has no preflighted constant buffer"));
        return false;
    }

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(rayTracingState(), targets);
    outBlob = graph.copyUploadData(
        &params,
        sizeof(params),
        alignof(NwbSurfelConstantsGpu)
    );
    return outBlob.valid();
}

bool RendererRayTracingSystem::recordPreparedSurfelFrameConstants(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets
){
    if(!hasSurfelWork() || !rayTracingState().m_surfelConstants)
        return true;

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(rayTracingState(), targets);

    Core::Buffer* cb = rayTracingState().m_surfelConstants.get();
    commandList.setBufferState(cb, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(cb, &params, sizeof(params));
    commandList.setBufferState(cb, Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

void RendererRayTracingSystem::finalizeSurfelResourceInitialization(){
    if(!rayTracingState().m_surfelResourcesClearPending)
        return;

    rayTracingState().m_surfelResourcesClearPending = false;
    rayTracingState().m_surfelResourcesNeedClear = false;
}

void RendererRayTracingSystem::discardSurfelResourceInitialization(){
    // Keep the clear pending until a producer succeeds.
    rayTracingState().m_surfelResourcesClearPending = false;
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiAgeFreeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_surfel_gi_task::SurfelGiAgeFreeGraphTask>(
        desc,
        __hidden_surfel_gi_task::SurfelGiAgeFreeGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &graphics(),
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = &asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiHashBuildTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_surfel_gi_task::SurfelGiHashBuildGraphTask>(
        desc,
        __hidden_surfel_gi_task::SurfelGiHashBuildGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiSpawnTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_surfel_gi_task::SurfelGiSpawnGraphTask>(
        desc,
        __hidden_surfel_gi_task::SurfelGiSpawnGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiTraceBuildArgsTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_surfel_gi_task::SurfelGiTraceBuildArgsGraphTask>(
        desc,
        __hidden_surfel_gi_task::SurfelGiTraceBuildArgsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiTraceTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_surfel_gi_task::SurfelGiTraceGraphTask>(
        desc,
        __hidden_surfel_gi_task::SurfelGiTraceGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiResolveTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<__hidden_surfel_gi_task::SurfelGiResolveGraphTask>(
        desc,
        __hidden_surfel_gi_task::SurfelGiResolveGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsCellHeadClear,
    const bool graphOwnsHashBuild,
    const bool graphOwnsSpawn,
    const bool graphOwnsTraceBuildArgs,
    const bool graphOwnsTrace,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const asyncTiming
){
    return graph.addTask<__hidden_surfel_gi_task::SurfelGiGraphTask>(
        desc,
        __hidden_surfel_gi_task::SurfelGiGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &graphics(),
            .targets = &targets,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsCellHeadClear = graphOwnsCellHeadClear,
            .graphOwnsHashBuild = graphOwnsHashBuild,
            .graphOwnsSpawn = graphOwnsSpawn,
            .graphOwnsTraceBuildArgs = graphOwnsTraceBuildArgs,
            .graphOwnsTrace = graphOwnsTrace,
            .graphOwnsResolve = graphOwnsResolve,
        }
    );
}


Core::GpuTaskId RendererRayTracingSystem::declareSurfelResourceInitializationTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const bool graphEntryStatesOwned
){
    return graph.addTask<SurfelGiInitializationGraphTask>(
        desc,
        SurfelGiInitializationGraphTask::Payload{
            .raytracingSystem = this,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}


bool RendererRayTracingSystem::renderSurfelGi(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
        false,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiAgeFree(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        true,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiAfterAgeFree(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsCellHeadClear,
    const bool graphOwnsHashBuild,
    const bool graphOwnsSpawn,
    const bool graphOwnsTraceBuildArgs,
    const bool graphOwnsTrace,
    const bool graphOwnsResolve
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        false,
        !graphOwnsHashBuild,
        !graphOwnsSpawn,
        !graphOwnsTraceBuildArgs,
        !graphOwnsTrace,
        !graphOwnsResolve,
        true,
        graphOwnsCellHeadClear,
        graphOwnsHashBuild,
        graphOwnsTraceBuildArgs,
        graphOwnsTrace,
        graphOwnsResolve
    );
}


bool RendererRayTracingSystem::renderSurfelGiHashBuild(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        false,
        true,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiSpawn(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        false,
        false,
        true,
        false,
        false,
        false,
        false,
        true,
        true,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiTraceBuildArgs(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        false,
        false,
        false,
        true,
        false,
        false,
        false,
        true,
        true,
        true,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiTrace(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        false,
        false,
        false,
        false,
        true,
        false,
        false,
        true,
        true,
        true,
        true,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        graphEntryStatesOwned,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        true,
        true,
        true,
        true,
        true
    );
}


bool RendererRayTracingSystem::renderSurfelGiPhases(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool dispatchAgeFree,
    const bool dispatchHashBuild,
    const bool dispatchSpawn,
    const bool dispatchTraceBuildArgs,
    const bool dispatchTrace,
    const bool dispatchResolve,
    const bool dispatchRemaining,
    const bool graphOwnsCellHeadClear,
    const bool graphOwnsHashBuild,
    const bool graphOwnsTraceBuildArgs,
    const bool graphOwnsTrace,
    const bool graphOwnsResolve
){
    if(!hasSurfelWork())
        return true;

    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Only the trace pass is backend-specific.
    const bool useHwTrace = rayTracingState().m_surfelUseHwTrace;
    Core::ComputePipeline* const tracePipeline = useHwTrace ? rayTracingState().m_surfelTraceHwPipeline.get() : rayTracingState().m_surfelTracePipeline.get();

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
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelConstantsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelPoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelCellHeadHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelTraceIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelFreeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelPoolSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelCellHeadSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_surfelMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.gbufferWorldPosition, Core::GpuDescriptorClass::SampledImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.gbufferNormal, Core::GpuDescriptorClass::SampledImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.surfelIrradianceHalf, Core::GpuDescriptorClass::SampledImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.surfelIrradianceHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !__hidden_raytracing_system::IsHeapHandle(targets.bindless.surfelIrradianceStorage, Core::GpuDescriptorClass::StorageImage)
        || (useHwTrace && (!rayTracingState().m_tlas || !__hidden_raytracing_system::IsHeapHandle(rayTracingState().m_tlasHeapHandle, Core::GpuDescriptorClass::AccelStruct)))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI heap registration is incomplete"));
        return false;
    }

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

    // Order every in-place field update, including prior-frame spawn writes.
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelPoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelCellHeadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelCounterBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelFreeListBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), true);

    // Age-free recycles unseen surfels before the graph-owned cell-head reset and hash rebuild.
    if(dispatchAgeFree){
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, graphics().getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelAgeFreePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelAgeFreePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchHashBuild && !dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Rebuild occupancy before spawning into empty cells.
    if(!graphOwnsCellHeadClear){
        Core::Buffer* cellHead = rayTracingState().m_surfelCellHeadBuffer.get();
        commandList.setBufferState(cellHead, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(cellHead, NWB_SURFEL_CELL_INVALID);
    }

    if(dispatchHashBuild){
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelHashBuild, graphics().getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        if(!graphOwnsCellHeadClear)
            commandList.setBufferState(rayTracingState().m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(rayTracingState().m_surfelHashBuildPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *rayTracingState().m_surfelHashBuildPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Spawn claims only empty hash cells.
    if(dispatchSpawn){
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelSpawn, graphics().getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        if(!graphOwnsHashBuild)
            commandList.setBufferState(rayTracingState().m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(rayTracingState().m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
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

    if(!dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Build an indirect dispatch sized for live surfels.
    if(dispatchTraceBuildArgs){
        if(!graphEntryStatesOwned){
            commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(rayTracingState().m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
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

    if(!dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Direct callers stage heap-selected trace inputs locally; prepared graph callers inherit the compiler-lowered
    // trace-argument state after the graph-owned build-arguments task.
    if(dispatchTrace){
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, graphics().getDevice(), commandList);
        if(!graphEntryStatesOwned && useHwTrace){
            for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
                commandList.setBufferState(rayTracingState().m_shadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            }
            commandList.setBufferState(rayTracingState().m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        }
        else if(!graphEntryStatesOwned){
            transitionSwShadowTraversalResources(commandList);
            commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned && useHwTrace)
            commandList.setAccelStructState(rayTracingState().m_tlas.get(), Core::ResourceStates::AccelStructRead);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_surfelPoolSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_surfelCellHeadSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredState().m_sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredState().m_lightBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphOwnsTraceBuildArgs)
            commandList.setBufferState(rayTracingState().m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(tracePipeline);
        state.setIndirectParams(rayTracingState().m_surfelTraceIndirectArgsBuffer.get());
        commandList.setComputeState(state);
        // Hardware trace additionally selects the TLAS generation at set 10.
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

    if(!dispatchResolve && !dispatchRemaining)
        return true;

    // Resolve at half resolution so deferred lighting never touches the writable pool.
    if(dispatchResolve){
        const u32 halfWidth = DivideUp(targets.width, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR));
        const u32 halfHeight = DivideUp(targets.height, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR));
        {
            Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelResolve, graphics().getDevice(), commandList);
            if(!graphEntryStatesOwned)
                commandList.setBufferState(rayTracingState().m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            if(!graphOwnsTrace){
                commandList.setBufferState(rayTracingState().m_surfelPoolBuffer.get(), Core::ResourceStates::ShaderResource);
                commandList.setBufferState(rayTracingState().m_surfelCellHeadBuffer.get(), Core::ResourceStates::ShaderResource);
            }
            if(!graphEntryStatesOwned){
                commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                commandList.setTextureState(targets.surfelIrradianceHalf.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
            }
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
    }

    if(!dispatchRemaining)
        return true;

    // Surface-aware upsample preserves coverage across edges.
    if(!graphOwnsResolve)
        commandList.setTextureState(targets.surfelIrradianceHalf.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SurfelUpsample, graphics().getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned)
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

    commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // The graph-owned late copy publishes its token only after Transfer/Compute/Graphics accepts. This pass only
    // consumes completed diagnostics; resource-state transitions and native copy recording live in that graph task.
    {
        const u32 frameIndex = rayTracingState().m_surfelFrameIndex;
        Core::Buffer* readback = rayTracingState().m_surfelCounterReadback.get();
        const Core::QueueSubmissionToken submissionToken = rayTracingState().m_surfelCountReadbackSubmissionToken;
        const bool submissionComplete =
            submissionToken.valid()
            && submissionToken.hasPhysicalQueueIdentity()
            && graphics().getDevice().queueGetCompletedInstance(
                Core::GpuPhysicalQueueId{
                    submissionToken.physicalQueueIndex,
                    submissionToken.deviceGeneration,
                }
            ) >= submissionToken.value
        ;
        if(
            submissionToken.valid()
            && (frameIndex - rayTracingState().m_surfelCountReadbackFrame) >= s_SurfelCountLogDelay
            && submissionComplete
        ){
            const u32* counts = static_cast<const u32*>(graphics().getDevice().mapBuffer(readback, Core::CpuAccessMode::Read));
            if(counts){
                const u32 bumpTop = counts[NWB_SURFEL_COUNTER_BUMP_TOP];
                const u32 freeTop = counts[NWB_SURFEL_COUNTER_FREE_TOP];
                graphics().getDevice().unmapBuffer(readback);
                NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: surfel live count = {} (bump {} - free {}) of {} pool capacity")
                    , static_cast<u64>(bumpTop - freeTop)
                    , static_cast<u64>(bumpTop)
                    , static_cast<u64>(freeTop)
                    , static_cast<u64>(rayTracingState().m_surfelPoolCapacity)
                );
            }
            rayTracingState().m_surfelCountReadbackSubmissionToken = {};
        }
    }

    // Subsequent frames use steady-state round-robin updates.
    rayTracingState().m_surfelSeeded = true;
    rayTracingState().m_surfelFrameIndex = rayTracingState().m_surfelFrameIndex + 1u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

