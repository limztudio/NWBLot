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
    Float4 coverageRadiusBiasHyst;  // x = reserved, y = default radius, z = normal bias, w = accumulation cap
    Float4 ageRaysTileScreen;       // x = max age, y = maximum rays/surfel, z = spawn tile (px), w = screen width
    Float4 screenHeightPad;         // x = screen height, y = resolve factor, zw = pad
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
    params.screenHeightPad = Float4(static_cast<f32>(targets.height), static_cast<f32>(targets.surfelResolveFactor), 0.0f, 0.0f);
    return params;
}

// Delayed counter readback avoids stalling on the asynchronous copy.
inline constexpr u32 s_SurfelCountLogInterval = 120u;
inline constexpr u32 s_SurfelCountLogDelay = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingSurfelGiTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SurfelGiAgeFreeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings are required: age/free always brackets the async surfel range for this frame.
    // References (not nullable pointers) carry those bindings so a missing binding fails at
    // declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.deferredLightingResources.valid()
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(!queue || payload.asyncTiming.has_value())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(queue->queueClass == Core::CommandQueue::Compute){
            payload.asyncTiming.emplace(
                payload.graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncSurfelGi,
                payload.graphics.getDevice(),
                commandList
            );
        }

        if(!payload.raytracingSystem.renderSurfelGiAgeFree(
            commandList,
            payload.targets,
            payload.deferredLightingResources
        )){
            if(payload.asyncTiming.has_value()){
                payload.asyncTiming.value().discardTiming();
                payload.asyncTiming.reset();
            }
            return false;
        }
        // The timestamp endpoint follows in the remaining-GI callback, but this callback's nested marker must close before the packet recorder advances to the graph-owned cell-head clear task.
        if(payload.asyncTiming.has_value() && !Core::FinishSplitGpuTimingMarker(&payload.asyncTiming))
            return false;
        return true;
    }

    static void Discarded(Payload& payload){
        DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};


struct SurfelGiHashBuildGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings are required: hash-build always records against this frame's ticket and brackets
    // the shared async surfel range. References (not nullable pointers) carry those bindings so a
    // missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.deferredLightingResources.valid()
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(!payload.raytracingSystem.renderSurfelGiHashBuild(
            commandList,
            payload.targets,
            payload.deferredLightingResources
        )){
            if(payload.asyncTiming.has_value()){
                payload.asyncTiming.value().discardTiming();
                payload.asyncTiming.reset();
            }
            return false;
        }
        return true;
    }

    static void Discarded(Payload& payload){
        DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};


struct SurfelGiSpawnGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings are required: spawn always records against this frame's ticket and brackets the
    // shared async surfel range. References (not nullable pointers) carry those bindings so a missing
    // binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.deferredLightingResources.valid()
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(!payload.raytracingSystem.renderSurfelGiSpawn(
            commandList,
            payload.targets,
            payload.deferredLightingResources
        )){
            if(payload.asyncTiming.has_value()){
                payload.asyncTiming.value().discardTiming();
                payload.asyncTiming.reset();
            }
            return false;
        }
        return true;
    }

    static void Discarded(Payload& payload){
        DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};


struct SurfelGiTraceBuildArgsGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings are required: trace-args always records against this frame's ticket and brackets
    // the shared async surfel range. References (not nullable pointers) carry those bindings so a
    // missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.deferredLightingResources.valid()
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(!payload.raytracingSystem.renderSurfelGiTraceBuildArgs(
            commandList,
            payload.targets,
            payload.deferredLightingResources
        )){
            if(payload.asyncTiming.has_value()){
                payload.asyncTiming.value().discardTiming();
                payload.asyncTiming.reset();
            }
            return false;
        }
        return true;
    }

    static void Discarded(Payload& payload){
        DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};


struct SurfelGiTraceGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings are required: trace always records against this frame's ticket and brackets the
    // shared async surfel range. References (not nullable pointers) carry those bindings so a missing
    // binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.deferredLightingResources.valid()
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(!payload.raytracingSystem.renderSurfelGiTrace(
            commandList,
            payload.targets,
            payload.deferredLightingResources
        )){
            if(payload.asyncTiming.has_value()){
                payload.asyncTiming.value().discardTiming();
                payload.asyncTiming.reset();
            }
            return false;
        }
        return true;
    }

    static void Discarded(Payload& payload){
        DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};


struct SurfelGiResolveGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings are required: resolve always records against this frame's ticket and brackets the
    // shared async surfel range. References (not nullable pointers) carry those bindings so a missing
    // binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(
            !payload.deferredLightingResources.valid()
        )
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(
            !queue
            || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value())
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(!payload.raytracingSystem.renderSurfelGiResolve(
            commandList,
            payload.targets,
            payload.deferredLightingResources
        )){
            if(payload.asyncTiming.has_value()){
                payload.asyncTiming.value().discardTiming();
                payload.asyncTiming.reset();
            }
            return false;
        }
        return true;
    }

    static void Discarded(Payload& payload){
        DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};


struct SurfelGiGraphTask{
    // The raytracing system, the frame targets, and the timing ticket are required: the fallback
    // upsample always records against this frame's ticket. The async timing stays optional: only the
    // resolve-owning route brackets the shared async surfel range. References (not nullable pointers)
    // carry the required bindings so a missing binding fails at declaration time instead of silently
    // returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
    };

    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(!payload.deferredLightingResources.valid())
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(!queue)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(
            payload.raytracingSystem.hasSurfelWork()
            && queue->queueClass == Core::CommandQueue::Compute
            && (!payload.asyncTiming || !payload.asyncTiming->has_value())
        )
            return false;
        if(!payload.raytracingSystem.renderSurfelGiUpsample(commandList, payload.targets, payload.deferredLightingResources)){
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

    static void Discarded(Payload& payload){
        DiscardGpuTimingMeasure(payload.asyncTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The typed clear primitives own the four persistent-buffer writes. Keep this tiny final task
// the renderer's CPU mirror still becomes pending only after every clear recorded, and becomes initialized only after their shared packet accepts.
// The raytracing system is required: the lifecycle always records against this frame's system.
// A reference (not a nullable pointer) carries that binding so a missing binding fails at declaration
// time instead of silently returning `false` inside Record.
struct RendererRayTracingSystem::SurfelGiInitializationLifecycleGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        return payload.raytracingSystem.recordSurfelResourceInitializationLifecycle();
    }

    static void Discarded(Payload& payload){
        payload.raytracingSystem.discardSurfelResourceInitialization();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        payload.raytracingSystem.finalizeSurfelResourceInitialization();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

