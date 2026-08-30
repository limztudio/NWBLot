// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

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


namespace RayTracingSurfelGiTaskDetail{


struct SurfelGiAgeFreeGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        DeferredLightingGraphResources deferredLightingResources;
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
            || !payload.deferredLightingResources.valid()
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
            payload.deferredLightingResources,
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
        DeferredLightingGraphResources deferredLightingResources;
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
            || !payload.deferredLightingResources.valid()
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
            payload.deferredLightingResources,
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
        DeferredLightingGraphResources deferredLightingResources;
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
            || !payload.deferredLightingResources.valid()
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
            payload.deferredLightingResources,
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
        DeferredLightingGraphResources deferredLightingResources;
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
            || !payload.deferredLightingResources.valid()
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
            payload.deferredLightingResources,
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
        DeferredLightingGraphResources deferredLightingResources;
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
            || !payload.deferredLightingResources.valid()
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
            payload.deferredLightingResources,
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
        DeferredLightingGraphResources deferredLightingResources;
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
            || !payload.deferredLightingResources.valid()
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
            payload.deferredLightingResources,
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
        DeferredLightingGraphResources deferredLightingResources;
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
        if(!payload.raytracingSystem || !payload.graphics || !payload.targets || !payload.deferredLightingResources.valid() || !payload.timingTicket)
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
                payload.deferredLightingResources,
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
            payload.deferredLightingResources,
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


// The typed clear primitives own the four persistent-buffer writes. Keep this tiny final task so the renderer's
// CPU mirror still becomes pending only after every clear recorded, and becomes initialized only after their shared
// packet accepts.
struct RendererRayTracingSystem::SurfelGiInitializationLifecycleGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        return payload.raytracingSystem
            && payload.raytracingSystem->recordSurfelResourceInitializationLifecycle();
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
            // The persistent counter is written by GI on Compute and may be copied by the late diagnostic
            // readback on Transfer before the next Compute frame imports its accepted tail state.
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

void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelConstantsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCounterHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelFreeListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolSnapshotHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle);
        return;
    }

    m_rayTracingState.m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::hasSurfelWork()const noexcept{
    return m_rayTracingState.m_surfelEnabled;
}

bool RendererRayTracingSystem::shouldCaptureSurfelCountReadback()const noexcept{
    return hasSurfelWork()
        && m_rayTracingState.m_surfelCounterBuffer
        && m_rayTracingState.m_surfelCounterReadback
        && !m_rayTracingState.m_surfelCountReadbackSubmissionToken.valid()
        && (m_rayTracingState.m_surfelFrameIndex % s_SurfelCountLogInterval) == 0u
    ;
}

void RendererRayTracingSystem::markSurfelCountReadbackScheduled()noexcept{
    m_rayTracingState.m_surfelCountReadbackFrame = m_rayTracingState.m_surfelFrameIndex;
}

bool RendererRayTracingSystem::needsSurfelResourceInitialization()const noexcept{
    return hasSurfelWork() && m_rayTracingState.m_surfelResourcesNeedClear;
}

bool RendererRayTracingSystem::prepareSurfelResources(DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    if(!ensureSurfelResources())
        return false;

    // Register the remaining heap-selected trace context.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        || !RayTracingDetail::EnsureHeapBuffer(
            heap,
            *m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            false,
            m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle
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
    if(!m_rayTracingState.m_surfelConstants){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI has no preflighted constant buffer"));
        return false;
    }

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(m_rayTracingState, targets);
    outBlob = graph.copyUploadData(
        &params,
        sizeof(params),
        alignof(NwbSurfelConstantsGpu)
    );
    return outBlob.valid();
}

void RendererRayTracingSystem::finalizeSurfelResourceInitialization(){
    if(!m_rayTracingState.m_surfelResourcesClearPending)
        return;

    m_rayTracingState.m_surfelResourcesClearPending = false;
    m_rayTracingState.m_surfelResourcesNeedClear = false;
}

bool RendererRayTracingSystem::recordSurfelResourceInitializationLifecycle()noexcept{
    if(!m_rayTracingState.m_surfelResourcesNeedClear)
        return false;

    m_rayTracingState.m_surfelResourcesClearPending = true;
    return true;
}

void RendererRayTracingSystem::discardSurfelResourceInitialization(){
    // Keep the clear pending until a producer succeeds.
    m_rayTracingState.m_surfelResourcesClearPending = false;
}

Core::GpuTaskId RendererRayTracingSystem::declareSurfelGiAgeFreeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiAgeFreeGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiHashBuildGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiSpawnGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiTraceBuildArgsGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiTraceGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiResolveGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
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
    return graph.addTask<RayTracingSurfelGiTaskDetail::SurfelGiGraphTask>(
        desc,
        RayTracingSurfelGiTaskDetail::SurfelGiGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
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


Core::GpuTaskId RendererRayTracingSystem::declareSurfelResourceInitializationLifecycleTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc
){
    return graph.addTask<SurfelGiInitializationLifecycleGraphTask>(
        desc,
        SurfelGiInitializationLifecycleGraphTask::Payload{
            .raytracingSystem = this,
        }
    );
}


bool RendererRayTracingSystem::renderSurfelGi(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
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
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
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
    const DeferredLightingGraphResources& deferredLightingResources,
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
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Only the trace pass is backend-specific.
    const bool useHwTrace = m_rayTracingState.m_surfelUseHwTrace;
    Core::ComputePipeline* const tracePipeline = useHwTrace ? m_rayTracingState.m_surfelTraceHwPipeline.get() : m_rayTracingState.m_surfelTracePipeline.get();

    if(
        !m_rayTracingState.m_surfelSpawnPipeline
        || !m_rayTracingState.m_surfelAgeFreePipeline
        || !m_rayTracingState.m_surfelHashBuildPipeline
        || !tracePipeline
        || !m_rayTracingState.m_surfelResolvePipeline
        || !m_rayTracingState.m_surfelUpsamplePipeline
        || !m_rayTracingState.m_surfelTraceBuildArgsPipeline
    )
        return true;

    if(
        !targets.bindless.valid()
        || !deferredLightingResources.valid()
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelConstantsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelFreeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.gbufferWorldPosition, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.gbufferNormal, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceHalf, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceStorage, Core::GpuDescriptorClass::StorageImage)
        || (useHwTrace && (!m_rayTracingState.m_tlas || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_tlasHeapHandle, Core::GpuDescriptorClass::AccelStruct)))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI heap registration is incomplete"));
        return false;
    }

    const u32 poolCapacity = m_rayTracingState.m_surfelPoolCapacity;

    SurfelHeapPushConstants surfelPush;
    surfelPush.constantsHeapSlot = m_rayTracingState.m_surfelConstantsHeapHandle.slot();
    surfelPush.poolHeapSlot = m_rayTracingState.m_surfelPoolHeapHandle.slot();
    surfelPush.cellHeadHeapSlot = m_rayTracingState.m_surfelCellHeadHeapHandle.slot();
    surfelPush.counterHeapSlot = m_rayTracingState.m_surfelCounterHeapHandle.slot();
    surfelPush.freeListHeapSlot = m_rayTracingState.m_surfelFreeListHeapHandle.slot();
    surfelPush.snapshotPoolHeapSlot = m_rayTracingState.m_surfelPoolSnapshotHeapHandle.slot();
    surfelPush.snapshotCellHeadHeapSlot = m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle.slot();
    surfelPush.traceIndirectArgsHeapSlot = m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle.slot();
    surfelPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
    surfelPush.materialContextSlotsHeapSlot = m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle.slot();

    // Order every in-place field update, including prior-frame spawn writes.
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelPoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCellHeadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCounterBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelFreeListBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), true);

    // Age-free recycles unseen surfels before the graph-owned cell-head reset and hash rebuild.
    if(dispatchAgeFree){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelAgeFreePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelAgeFreePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchHashBuild && !dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Rebuild occupancy before spawning into empty cells.
    if(!graphOwnsCellHeadClear){
        Core::Buffer* cellHead = m_rayTracingState.m_surfelCellHeadBuffer.get();
        commandList.setBufferState(cellHead, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(cellHead, NWB_SURFEL_CELL_INVALID);
    }

    if(dispatchHashBuild){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelHashBuild, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        if(!graphOwnsCellHeadClear)
            commandList.setBufferState(m_rayTracingState.m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelHashBuildPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelHashBuildPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Spawn claims only empty hash cells.
    if(dispatchSpawn){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelSpawn, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        if(!graphOwnsHashBuild)
            commandList.setBufferState(m_rayTracingState.m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        commandList.commitBarriers();

        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();

        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelSpawnPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelSpawnPipeline.get());
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
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
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
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned && useHwTrace){
            for(u32 slot = 0u; slot < m_rayTracingState.m_shadowMeshCount; ++slot){
                commandList.setBufferState(m_rayTracingState.m_shadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(m_rayTracingState.m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(m_rayTracingState.m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            }
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        }
        else if(!graphEntryStatesOwned){
            transitionSwShadowTraversalResources(commandList);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned && useHwTrace)
            commandList.setAccelStructState(m_rayTracingState.m_tlas.get(), Core::ResourceStates::AccelStructRead);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphOwnsTraceBuildArgs)
            commandList.setBufferState(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(tracePipeline);
        state.setIndirectParams(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get());
        commandList.setComputeState(state);
        // Hardware trace additionally selects the TLAS generation at set 10.
        if(m_rayTracingState.m_surfelUseHwTrace && !m_rayTracingState.m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch surfel HW GI without the descriptor-heap TLAS handle"));
            return false;
        }
        const Core::GpuDescriptorHandle tlasHeapHandle = m_rayTracingState.m_surfelUseHwTrace
            ? m_rayTracingState.m_tlasHeapHandle
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
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelResolve, m_graphics.getDevice(), commandList);
            if(!graphEntryStatesOwned)
                commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            if(!graphOwnsTrace){
                commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::ShaderResource);
                commandList.setBufferState(m_rayTracingState.m_surfelCellHeadBuffer.get(), Core::ResourceStates::ShaderResource);
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
            state.setPipeline(m_rayTracingState.m_surfelResolvePipeline.get());
            commandList.setComputeState(state);
            heap.bindCompute(commandList, *m_rayTracingState.m_surfelResolvePipeline.get());
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
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelUpsample, m_graphics.getDevice(), commandList);
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
        state.setPipeline(m_rayTracingState.m_surfelUpsamplePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelUpsamplePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        const u32 groupSize = static_cast<u32>(NWB_SURFEL_UPSAMPLE_GROUP_SIZE);
        commandList.dispatch(DivideUp(targets.width, groupSize), DivideUp(targets.height, groupSize), 1u);
    }

    // The prepared graph declares the actual downstream consumer: live Lighting samples the output, while the
    // lagged route copies it. Keep the compatibility return layout for direct callers, but let graph lowering own
    // the precise UAV-to-SRV or UAV-to-CopySource handoff.
    if(!graphOwnsResolve){
        commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    // The graph-owned late copy publishes its token only after Transfer/Compute/Graphics accepts. This pass only
    // consumes completed diagnostics; resource-state transitions and native copy recording live in that graph task.
    {
        const u32 frameIndex = m_rayTracingState.m_surfelFrameIndex;
        Core::Buffer* readback = m_rayTracingState.m_surfelCounterReadback.get();
        const Core::QueueSubmissionToken submissionToken = m_rayTracingState.m_surfelCountReadbackSubmissionToken;
        const bool submissionComplete =
            submissionToken.valid()
            && submissionToken.hasPhysicalQueueIdentity()
            && m_graphics.getDevice().queueGetCompletedInstance(
                Core::GpuPhysicalQueueId{
                    submissionToken.physicalQueueIndex,
                    submissionToken.deviceGeneration,
                }
            ) >= submissionToken.value
        ;
        if(
            submissionToken.valid()
            && (frameIndex - m_rayTracingState.m_surfelCountReadbackFrame) >= s_SurfelCountLogDelay
            && submissionComplete
        ){
            const u32* counts = static_cast<const u32*>(m_graphics.getDevice().mapBuffer(readback, Core::CpuAccessMode::Read));
            if(counts){
                const u32 bumpTop = counts[NWB_SURFEL_COUNTER_BUMP_TOP];
                const u32 freeTop = counts[NWB_SURFEL_COUNTER_FREE_TOP];
                m_graphics.getDevice().unmapBuffer(readback);
                NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: surfel live count = {} (bump {} - free {}) of {} pool capacity")
                    , static_cast<u64>(bumpTop - freeTop)
                    , static_cast<u64>(bumpTop)
                    , static_cast<u64>(freeTop)
                    , static_cast<u64>(m_rayTracingState.m_surfelPoolCapacity)
                );
            }
            m_rayTracingState.m_surfelCountReadbackSubmissionToken = {};
        }
    }

    // Subsequent frames use steady-state round-robin updates.
    m_rayTracingState.m_surfelSeeded = true;
    m_rayTracingState.m_surfelFrameIndex = m_rayTracingState.m_surfelFrameIndex + 1u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

