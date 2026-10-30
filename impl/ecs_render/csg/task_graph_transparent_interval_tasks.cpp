// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_transparent_interval_tasks.h"

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_interval_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both transparent interval passes share one snapshot and gate on the same framebuffer, draw-buffer, CSG, and material readiness.
struct TransparentIntervalReadiness{
    CsgFrameGpuDataView csgFrameData;
    b8 ready = false;
};

[[nodiscard]] inline TransparentIntervalReadiness ResolveTransparentIntervalReadiness(const TransparentCsgIntervalRecordInputs& payload, RendererMaterialSystem& materialSystem)noexcept{
    TransparentIntervalReadiness readiness{};
    const MaterialPassDrawItemsView receiverSurfaceDrawItems = payload.transparentCsgSnapshot.receiverSurfaceDrawItemsView();
    readiness.csgFrameData = payload.transparentCsgSnapshot.csgFrameDataView();
    const b8 drawBuffersReady = payload.frameBindings.frameReady(payload.transparentCsgSnapshot.instanceCount, payload.transparentCsgSnapshot.materialTypedByteCount);
    const b8 csgResourcesReady = payload.csgResources.frameReady(readiness.csgFrameData);
    const b8 receiverSurfaceDrawResourcesReady = materialSystem.materialPassDrawResourcesReady(receiverSurfaceDrawItems, payload.frameBindings);
    readiness.ready = payload.csgFrameBuffersUploaded && payload.targets.framebuffer && !receiverSurfaceDrawItems.empty() && readiness.csgFrameData.hasWork() && drawBuffersReady && csgResourcesReady && receiverSurfaceDrawResourcesReady;
    return readiness;
}


template<typename Dispatch>
[[nodiscard]] b8 RecordTransparentInterval(const TransparentCsgIntervalRecordInputs& payload, Core::CommandList& commandList, Dispatch dispatch){
    const TransparentIntervalReadiness readiness = ResolveTransparentIntervalReadiness(payload, payload.materialSystem);
    if(readiness.ready)
        dispatch(readiness);
    else
        Core::DiscardGpuTimingMeasure(&payload.transparentCsgIntervalsTiming);
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TransparentCsgIntervalRecordInputs::TransparentCsgIntervalRecordInputs(
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystemIn,
    RendererCsgSystem& csgSystemIn,
    DeferredFrameTargets& targetsIn,
    Core::GpuTimingSubmissionTicket& timingTicketIn,
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTimingIn
)noexcept
    : materialSystem(materialSystemIn)
    , csgSystem(csgSystemIn)
    , targets(targetsIn)
    , timingTicket(timingTicketIn)
    , transparentCsgIntervalsTiming(transparentCsgIntervalsTimingIn)
    , transparentCsgSnapshot(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitCsgReceiverSpanGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    if(!payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming.has_value()){
        commandList.endRenderPass();
        return true;
    }
    return __hidden_transparent_interval_tasks::RecordTransparentInterval(payload, commandList, [&](const auto& readiness){
        payload.csgSystem.dispatchCsgReceiverSpanBuild(commandList, payload.targets, readiness.csgFrameData, payload.csgResources);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitCsgIntervalCombineGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    if(!payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming.has_value()){
        commandList.endRenderPass();
        return true;
    }
    return __hidden_transparent_interval_tasks::RecordTransparentInterval(payload, commandList, [&](const auto& readiness){
        payload.csgSystem.dispatchCsgIntervalCombine(commandList, payload.targets, readiness.csgFrameData, payload.csgResources);
        payload.transparentCsgIntervalsTiming.value().finishTiming(commandList);
        payload.transparentCsgIntervalsTiming.reset();
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

