// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/gi/surfel_gi_task_builder.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_surfel_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(NwbSurfelConstantsGpu) == sizeof(Float4) * NWB_SURFEL_CONSTANTS_FLOAT4_COUNT, "NwbSurfelConstantsGpu must match the shader NwbSurfelConstants layout");
static_assert(s_SurfelUpdateDivisor == NWB_SURFEL_UPDATE_DIVISOR, "s_SurfelUpdateDivisor must match NWB_SURFEL_UPDATE_DIVISOR");
static_assert(s_SurfelCellSize == NWB_SURFEL_CELL_SIZE, "s_SurfelCellSize must match NWB_SURFEL_CELL_SIZE");
static_assert(s_SurfelDefaultRadius == NWB_SURFEL_DEFAULT_RADIUS, "s_SurfelDefaultRadius must match NWB_SURFEL_DEFAULT_RADIUS");
static_assert(s_SurfelMaxAccum == NWB_SURFEL_MAX_ACCUM, "s_SurfelMaxAccum must match NWB_SURFEL_MAX_ACCUM");
static_assert(s_SurfelMaxAge == NWB_SURFEL_MAX_AGE, "s_SurfelMaxAge must match NWB_SURFEL_MAX_AGE");
static_assert(s_SurfelRaysPerSurfel == NWB_SURFEL_RAYS_PER_SURFEL, "s_SurfelRaysPerSurfel must match NWB_SURFEL_RAYS_PER_SURFEL");
static_assert(s_SurfelSpawnTile == NWB_SURFEL_SPAWN_TILE, "s_SurfelSpawnTile must match NWB_SURFEL_SPAWN_TILE");


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

template<b8 DispatchHashBuild, b8 DispatchSpawn, b8 DispatchTraceBuildArgs, b8 DispatchTrace, b8 DispatchResolve>
[[nodiscard]] inline b8 RecordSurfelGiAsyncPass(const SurfelGiAsyncPassPayload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!SurfelAsyncRecordReady(payload, context))
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.raytracingSystem.renderSurfelGiPhases(
        commandList,
        payload.targets,
        payload.deferredLightingResources,
        false,
        DispatchHashBuild,
        DispatchSpawn,
        DispatchTraceBuildArgs,
        DispatchTrace,
        DispatchResolve,
        false
    )){
        if(payload.asyncTiming.has_value()){
            payload.asyncTiming.value().discardTiming();
            payload.asyncTiming.reset();
        }
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SurfelGiAgeFreeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
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

        if(!payload.raytracingSystem.renderSurfelGiPhases(commandList, payload.targets, payload.deferredLightingResources, true, false, false, false, false, false, false)){
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

    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};

template<b8 DispatchHashBuild, b8 DispatchSpawn, b8 DispatchTraceBuildArgs, b8 DispatchTrace, b8 DispatchResolve>
struct SurfelGiAsyncPassGraphTask{
    using Payload = SurfelGiAsyncPassPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSurfelGiAsyncPass<DispatchHashBuild, DispatchSpawn, DispatchTraceBuildArgs, DispatchTrace, DispatchResolve>(payload, commandList, context);
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept requires(DispatchTrace){
        payload.raytracingSystem.confirmRayTraceContextReadSubmission(token);
    }

    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    }
};

using SurfelGiHashBuildGraphTask = SurfelGiAsyncPassGraphTask<true, false, false, false, false>;
using SurfelGiSpawnGraphTask = SurfelGiAsyncPassGraphTask<false, true, false, false, false>;
using SurfelGiTraceBuildArgsGraphTask = SurfelGiAsyncPassGraphTask<false, false, true, false, false>;
using SurfelGiTraceGraphTask = SurfelGiAsyncPassGraphTask<false, false, false, true, false>;
using SurfelGiResolveGraphTask = SurfelGiAsyncPassGraphTask<false, false, false, false, true>;

struct SurfelGiGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // The raytracing system, the frame targets, and the timing ticket are required: the fallback upsample always records against this frame's ticket.
    // The async timing stays optional: only the resolve-owning route brackets the shared async surfel range.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>* asyncTiming = nullptr;
    };


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
        if(!payload.raytracingSystem.renderSurfelGiPhases(commandList, payload.targets, payload.deferredLightingResources, false, false, false, false, false, false, true)){
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

    static void Discarded(Payload& payload)noexcept{
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


public:
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, [[maybe_unused]] Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context)noexcept{
        return payload.raytracingSystem.recordSurfelResourceInitializationLifecycle();
    }

    static void Discarded(Payload& payload)noexcept{
        payload.raytracingSystem.discardSurfelResourceInitialization();
    }

    static void Accepted(Payload& payload, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
        payload.raytracingSystem.finalizeSurfelResourceInitialization();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> RendererRayTracingSystem::declareSurfelResourceInitializationLifecycleTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc){
    return graph.addTask<SurfelGiInitializationLifecycleGraphTask>(desc, SurfelGiInitializationLifecycleGraphTask::Payload{ .raytracingSystem = *this, });
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareAgeFreeTask(
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    return m_graph.addTask<__hidden_rt_surfel_tasks::SurfelGiAgeFreeGraphTask>(
        desc,
        __hidden_rt_surfel_tasks::SurfelGiAgeFreeGraphTask::Payload{
            .raytracingSystem = m_system,
            .graphics = m_system.graphics(),
            .targets = targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
        }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareHashBuildTask(
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    return m_graph.addTask<__hidden_rt_surfel_tasks::SurfelGiHashBuildGraphTask>(
        desc,
        __hidden_rt_surfel_tasks::SurfelGiHashBuildGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareSpawnTask(
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    return m_graph.addTask<__hidden_rt_surfel_tasks::SurfelGiSpawnGraphTask>(
        desc,
        __hidden_rt_surfel_tasks::SurfelGiSpawnGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareTraceBuildArgsTask(
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    return m_graph.addTask<__hidden_rt_surfel_tasks::SurfelGiTraceBuildArgsGraphTask>(
        desc,
        __hidden_rt_surfel_tasks::SurfelGiTraceBuildArgsGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareTraceTask(
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    return m_graph.addTask<__hidden_rt_surfel_tasks::SurfelGiTraceGraphTask>(
        desc,
        __hidden_rt_surfel_tasks::SurfelGiTraceGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareResolveTask(
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    return m_graph.addTask<__hidden_rt_surfel_tasks::SurfelGiResolveGraphTask>(
        desc,
        __hidden_rt_surfel_tasks::SurfelGiResolveGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}

Expected<Core::GpuTaskId> SurfelGiTaskBuilder::declareTerminalTask(
    const Core::GpuTaskDesc& desc,
    const DeferredLightingGraphResources& deferredLightingResources,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming
){
    return m_graph.addTask<__hidden_rt_surfel_tasks::SurfelGiGraphTask>(
        desc,
        __hidden_rt_surfel_tasks::SurfelGiGraphTask::Payload{ .raytracingSystem = m_system, .targets = targets, .deferredLightingResources = deferredLightingResources, .timingTicket = timingTicket, .asyncTiming = asyncTiming, }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

