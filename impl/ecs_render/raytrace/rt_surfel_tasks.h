// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/task/gpu/compiled_graph.h>


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingSurfelGiTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SurfelGiAgeFreeGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
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

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
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
        // The timestamp endpoint follows in the remaining-GI callback, but this callback's nested marker must close before the packet recorder advances to the graph-owned cell-head clear task.
        if(payload.asyncTiming->has_value() && !Core::FinishSplitGpuTimingMarker(payload.asyncTiming))
            return false;
        return true;
    }

    static void discarded(Payload& payload){
        DiscardGpuTimingMeasure(payload.asyncTiming);
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

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
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
        DiscardGpuTimingMeasure(payload.asyncTiming);
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

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
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
        DiscardGpuTimingMeasure(payload.asyncTiming);
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

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
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
        DiscardGpuTimingMeasure(payload.asyncTiming);
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

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
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
        DiscardGpuTimingMeasure(payload.asyncTiming);
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

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
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
        DiscardGpuTimingMeasure(payload.asyncTiming);
    }
};


struct SurfelGiGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
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

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
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
        DiscardGpuTimingMeasure(payload.asyncTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The typed clear primitives own the four persistent-buffer writes. Keep this tiny final task
// the renderer's CPU mirror still becomes pending only after every clear recorded, and becomes initialized only after their shared packet accepts.
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

