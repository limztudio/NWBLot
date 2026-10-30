// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/gi/quality_settings.adhd.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(NwbSurfelConstantsGpu) == sizeof(Float4) * NWB_SURFEL_CONSTANTS_FLOAT4_COUNT, "NwbSurfelConstantsGpu must match the shader NwbSurfelConstants layout");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StateT>
[[nodiscard]] NwbSurfelConstantsGpu BuildSurfelFrameConstants(const StateT& state, const DeferredFrameTargets& targets)noexcept(
    noexcept(static_cast<b8>(state.m_surfelSeeded))
    && noexcept(static_cast<f32>(state.m_surfelHashCellCount))
    && noexcept(static_cast<f32>(state.m_surfelPoolCapacity))
    && noexcept(static_cast<f32>(state.m_surfelFrameIndex))
){
    // First frame traces every surfel; later frames use round-robin updates.
    const u32 updateDivisor = state.m_surfelSeeded ? Max<u32>(NWB_SURFEL_UPDATE_DIVISOR, 1u) : 1u;
    const f32 cellSize = NWB_SURFEL_CELL_SIZE;

    NwbSurfelConstantsGpu params;
    params.hashPoolFrameDivisor = Float4(static_cast<f32>(state.m_surfelHashCellCount), static_cast<f32>(state.m_surfelPoolCapacity), static_cast<f32>(state.m_surfelFrameIndex), static_cast<f32>(updateDivisor));
    params.cellSizeRadiusBiasAccum = Float4(cellSize, NWB_SURFEL_DEFAULT_RADIUS, s_SurfelNormalBias, static_cast<f32>(NWB_SURFEL_MAX_ACCUM));
    params.ageRaysTileScreen = Float4(static_cast<f32>(NWB_SURFEL_MAX_AGE), static_cast<f32>(NWB_SURFEL_RAYS_PER_SURFEL), static_cast<f32>(NWB_SURFEL_SPAWN_TILE), static_cast<f32>(targets.width));
    params.screenHeightPad = Float4(static_cast<f32>(targets.height), static_cast<f32>(targets.surfelResolveFactor), 0.0f, 0.0f);
    return params;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingSurfelGiTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SurfelGiAsyncPassPayload{
    RendererRayTracingSystem& raytracingSystem;
    DeferredFrameTargets& targets;
    DeferredLightingGraphResources deferredLightingResources;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& asyncTiming;
};

// Hash-build, spawn, trace-args, trace, and resolve share one async-guard preamble; only the render call differs.
template<typename PayloadT>
[[nodiscard]] inline b8 SurfelAsyncRecordReady(const PayloadT& payload, const Core::GpuTaskRecordContext& context)noexcept(
    noexcept(static_cast<b8>(!payload.deferredLightingResources.valid()))
    && noexcept(static_cast<b8>(!DeclVal<const Core::GpuPhysicalQueueInfo*>() || (DeclVal<const Core::GpuPhysicalQueueInfo*>()->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value())))
){
    if(!payload.deferredLightingResources.valid())
        return false;

    const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
    if(!queue || (queue->queueClass == Core::CommandQueue::Compute && !payload.asyncTiming.has_value()))
        return false;
    return true;
}

using SurfelGiAsyncPassRecorder = b8 (RendererRayTracingSystem::*)(Core::CommandList&, DeferredFrameTargets&, const DeferredLightingGraphResources&);

template<SurfelGiAsyncPassRecorder Recorder>
[[nodiscard]] inline b8 RecordSurfelGiAsyncPass(const SurfelGiAsyncPassPayload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_assert(Recorder != nullptr);
    if(!SurfelAsyncRecordReady(payload, context))
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!(payload.raytracingSystem.*Recorder)(commandList, payload.targets, payload.deferredLightingResources)){
        if(payload.asyncTiming.has_value()){
            payload.asyncTiming.value().discardTiming();
            payload.asyncTiming.reset();
        }
        return false;
    }
    return true;
}

inline void DiscardSurfelGiAsyncPass(SurfelGiAsyncPassPayload& payload){
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SurfelGiAgeFreeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings are required: age/free always brackets the async surfel range for this frame.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.deferredLightingResources.valid())
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(!queue || payload.asyncTiming.has_value())
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(queue->queueClass == Core::CommandQueue::Compute)
            payload.asyncTiming.emplace(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_AsyncSurfelGi, payload.graphics.getDevice(), commandList);

        if(!payload.raytracingSystem.renderSurfelGiAgeFree(commandList, payload.targets, payload.deferredLightingResources)){
            if(payload.asyncTiming.has_value()){
                payload.asyncTiming.value().discardTiming();
                payload.asyncTiming.reset();
            }
            return false;
        }
        // The endpoint follows in the remaining-GI callback, but this nested marker must close before the recorder advances.
        if(payload.asyncTiming.has_value() && !Core::FinishSplitGpuTimingMarker(&payload.asyncTiming))
            return false;
        return true;
    }

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};

struct SurfelGiHashBuildGraphTask{
    using Payload = SurfelGiAsyncPassPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSurfelGiAsyncPass<&RendererRayTracingSystem::renderSurfelGiHashBuild>(payload, commandList, context);
    }

    static void Discarded(Payload& payload){
        DiscardSurfelGiAsyncPass(payload);
    }
};

struct SurfelGiSpawnGraphTask{
    using Payload = SurfelGiAsyncPassPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSurfelGiAsyncPass<&RendererRayTracingSystem::renderSurfelGiSpawn>(payload, commandList, context);
    }

    static void Discarded(Payload& payload){
        DiscardSurfelGiAsyncPass(payload);
    }
};

struct SurfelGiTraceBuildArgsGraphTask{
    using Payload = SurfelGiAsyncPassPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSurfelGiAsyncPass<&RendererRayTracingSystem::renderSurfelGiTraceBuildArgs>(payload, commandList, context);
    }

    static void Discarded(Payload& payload){
        DiscardSurfelGiAsyncPass(payload);
    }
};

struct SurfelGiTraceGraphTask{
    using Payload = SurfelGiAsyncPassPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSurfelGiAsyncPass<&RendererRayTracingSystem::renderSurfelGiTrace>(payload, commandList, context);
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        payload.raytracingSystem.confirmRayTraceContextReadSubmission(token);
    }

    static void Discarded(Payload& payload){
        DiscardSurfelGiAsyncPass(payload);
    }
};

struct SurfelGiResolveGraphTask{
    using Payload = SurfelGiAsyncPassPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSurfelGiAsyncPass<&RendererRayTracingSystem::renderSurfelGiResolve>(payload, commandList, context);
    }

    static void Discarded(Payload& payload){
        DiscardSurfelGiAsyncPass(payload);
    }
};

struct SurfelGiGraphTask{
    // The raytracing system, the frame targets, and the timing ticket are required: the fallback upsample always records against this frame's ticket.
    // The async timing stays optional: only the resolve-owning route brackets the shared async surfel range.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
    };

    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.deferredLightingResources.valid())
            return false;

        const Core::GpuPhysicalQueueInfo* const queue = context.compiledPlan.queueInfo(context.queue);
        if(!queue)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
        if(payload.raytracingSystem.hasSurfelWork() && queue->queueClass == Core::CommandQueue::Compute && (!payload.asyncTiming || !payload.asyncTiming->has_value()))
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
        Core::DiscardGpuTimingMeasure(payload.asyncTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The typed clear primitives own the four persistent-buffer writes.
// The CPU mirror becomes pending after every clear records and initialized after their shared packet accepts.
// The raytracing system is required: the lifecycle always records against this frame's system.
struct RendererRayTracingSystem::SurfelGiInitializationLifecycleGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context)noexcept{
        static_cast<void>(commandList);
        static_cast<void>(context);
        return payload.raytracingSystem.recordSurfelResourceInitializationLifecycle();
    }

    static void Discarded(Payload& payload)noexcept{
        payload.raytracingSystem.discardSurfelResourceInitialization();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        static_cast<void>(token);
        payload.raytracingSystem.finalizeSurfelResourceInitialization();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

