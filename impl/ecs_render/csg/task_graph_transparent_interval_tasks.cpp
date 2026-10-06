// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_interval_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both transparent interval passes materialize the same snapshot and gate on the same
// framebuffer, draw-buffer, CSG-resource, and material-resource readiness.
struct TransparentIntervalReadiness{
    MaterialPassDrawItems receiverSurfaceDrawItems;
    CsgFrameGpuData csgFrameData;
    bool ready = false;

    explicit TransparentIntervalReadiness(Core::Alloc::ScratchArena& arena)
        : receiverSurfaceDrawItems(arena)
        , csgFrameData(arena)
    {}
};

template<typename PayloadT>
[[nodiscard]] inline TransparentIntervalReadiness ResolveTransparentIntervalReadiness(
    const PayloadT& payload,
    RendererMaterialSystem& materialSystem,
    Core::Alloc::ScratchArena& scratchArena
){
    TransparentIntervalReadiness readiness{ scratchArena };
    payload.transparentCsgSnapshot.materialize(readiness.receiverSurfaceDrawItems, readiness.csgFrameData);
    const bool drawBuffersReady = payload.frameBindings.frameReady(
        payload.transparentCsgSnapshot.instanceCount,
        payload.transparentCsgSnapshot.materialTypedByteCount
    );
    const bool csgResourcesReady = payload.csgResources.frameReady(readiness.csgFrameData);
    const bool receiverSurfaceDrawResourcesReady = materialSystem.materialPassDrawResourcesReady(
        readiness.receiverSurfaceDrawItems,
        payload.frameBindings
    );
    readiness.ready =
        payload.csgFrameBuffersUploaded
        && payload.targets.framebuffer
        && !readiness.receiverSurfaceDrawItems.empty()
        && readiness.csgFrameData.hasWork()
        && drawBuffersReady
        && csgResourcesReady
        && receiverSurfaceDrawResourcesReady
    ;
    return readiness;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitCsgReceiverSpanGraphTask::Payload::Payload(
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystemIn,
    RendererCsgSystem& csgSystemIn,
    DeferredFrameTargets& targetsIn,
    Core::GpuTimingSubmissionTicket& timingTicketIn,
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTimingIn
)
    : materialSystem(materialSystemIn)
    , csgSystem(csgSystemIn)
    , targets(targetsIn)
    , timingTicket(timingTicketIn)
    , transparentCsgIntervalsTiming(transparentCsgIntervalsTimingIn)
    , transparentCsgSnapshot(arena)
{}


bool AvboitCsgReceiverSpanGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(!payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming.has_value()){
        commandList.endRenderPass();
        return true;
    }
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    const __hidden_transparent_interval_tasks::TransparentIntervalReadiness readiness =
        __hidden_transparent_interval_tasks::ResolveTransparentIntervalReadiness(payload, materialSystem, scratchArena);
    if(readiness.ready){
        csgSystem.dispatchCsgReceiverSpanBuild(
            commandList,
            payload.targets,
            readiness.csgFrameData,
            payload.csgResources
        );
    }
    else{
        // Drop the reservation on mismatch; never feed Combine a stale image.
        DiscardGpuTimingMeasure(&payload.transparentCsgIntervalsTiming);
    }
    commandList.endRenderPass();
    return true;
}


AvboitCsgIntervalCombineGraphTask::Payload::Payload(
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystemIn,
    RendererCsgSystem& csgSystemIn,
    DeferredFrameTargets& targetsIn,
    Core::GpuTimingSubmissionTicket& timingTicketIn,
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTimingIn
)
    : materialSystem(materialSystemIn)
    , csgSystem(csgSystemIn)
    , targets(targetsIn)
    , timingTicket(timingTicketIn)
    , transparentCsgIntervalsTiming(transparentCsgIntervalsTimingIn)
    , transparentCsgSnapshot(arena)
{}


bool AvboitCsgIntervalCombineGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(!payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming.has_value()){
        commandList.endRenderPass();
        return true;
    }
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    const __hidden_transparent_interval_tasks::TransparentIntervalReadiness readiness =
        __hidden_transparent_interval_tasks::ResolveTransparentIntervalReadiness(payload, materialSystem, scratchArena);
    if(readiness.ready){
        csgSystem.dispatchCsgIntervalCombine(
            commandList,
            payload.targets,
            readiness.csgFrameData,
            payload.csgResources
        );
        payload.transparentCsgIntervalsTiming.value().finishTiming(commandList);
        payload.transparentCsgIntervalsTiming.reset();
    }
    else{
        // Drop the reservation on mismatch; never publish a stale image.
        DiscardGpuTimingMeasure(&payload.transparentCsgIntervalsTiming);
    }
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

