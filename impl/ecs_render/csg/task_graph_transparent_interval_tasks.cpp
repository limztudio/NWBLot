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


AvboitCsgReceiverSpanGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : transparentCsgSnapshot(arena)
{}


bool AvboitCsgReceiverSpanGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.materialSystem
        || !payload.csgSystem
        || !payload.targets
        || !payload.timingTicket
        || !payload.transparentCsgIntervalsTiming
        || !payload.transparentCsgSnapshot.captured
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming->has_value()){
        commandList.endRenderPass();
        return true;
    }
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems receiverSurfaceDrawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    payload.transparentCsgSnapshot.materialize(receiverSurfaceDrawItems, csgFrameData);
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
    const bool drawBuffersReady = payload.frameBindings.frameReady(
        payload.transparentCsgSnapshot.instanceCount,
        payload.transparentCsgSnapshot.materialTypedByteCount
    );
    const bool csgResourcesReady = payload.csgResources.frameReady(csgFrameData);
    const bool receiverSurfaceDrawResourcesReady = materialSystem.materialPassDrawResourcesReady(
        receiverSurfaceDrawItems,
        payload.frameBindings
    );
    const bool spanReady =
        payload.csgFrameBuffersUploaded
        && payload.targets->framebuffer
        && !receiverSurfaceDrawItems.empty()
        && csgFrameData.hasWork()
        && drawBuffersReady
        && csgResourcesReady
        && receiverSurfaceDrawResourcesReady
    ;
    if(spanReady){
        csgSystem.dispatchCsgReceiverSpanBuild(
            commandList,
            *payload.targets,
            csgFrameData,
            payload.csgResources,
            payload.receiverSpanOutputImageStatesGraphOwned,
            payload.receiverSpanInputImageStatesGraphOwned
        );
    }
    else{
        // Drop the reservation on mismatch; never feed Combine a stale image.
        DiscardGpuTimingMeasure(payload.transparentCsgIntervalsTiming);
    }
    commandList.endRenderPass();
    return true;
}


AvboitCsgIntervalCombineGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : transparentCsgSnapshot(arena)
{}


bool AvboitCsgIntervalCombineGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.materialSystem
        || !payload.csgSystem
        || !payload.targets
        || !payload.timingTicket
        || !payload.transparentCsgIntervalsTiming
        || !payload.transparentCsgSnapshot.captured
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(!payload.transparentCsgIntervalsTiming->has_value()){
        commandList.endRenderPass();
        return true;
    }
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems receiverSurfaceDrawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    payload.transparentCsgSnapshot.materialize(receiverSurfaceDrawItems, csgFrameData);
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
    const bool drawBuffersReady = payload.frameBindings.frameReady(
        payload.transparentCsgSnapshot.instanceCount,
        payload.transparentCsgSnapshot.materialTypedByteCount
    );
    const bool csgResourcesReady = payload.csgResources.frameReady(csgFrameData);
    const bool receiverSurfaceDrawResourcesReady = materialSystem.materialPassDrawResourcesReady(
        receiverSurfaceDrawItems,
        payload.frameBindings
    );
    const bool combineReady =
        payload.csgFrameBuffersUploaded
        && payload.targets->framebuffer
        && !receiverSurfaceDrawItems.empty()
        && csgFrameData.hasWork()
        && drawBuffersReady
        && csgResourcesReady
        && receiverSurfaceDrawResourcesReady
    ;
    if(combineReady){
        csgSystem.dispatchCsgIntervalCombine(
            commandList,
            *payload.targets,
            csgFrameData,
            payload.csgResources,
            payload.removedIntervalOutputImageStatesGraphOwned,
            payload.intervalCombineInputImageStatesGraphOwned
        );
        payload.transparentCsgIntervalsTiming->value().finishTiming(commandList);
        payload.transparentCsgIntervalsTiming->reset();
    }
    else{
        // Drop the reservation on mismatch; never publish a stale image.
        DiscardGpuTimingMeasure(payload.transparentCsgIntervalsTiming);
    }
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

