// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

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
        !payload.renderer
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
    RendererSystem& renderer = *payload.renderer;
    const bool drawBuffersReady = renderer.m_materialSystem.materialPassDrawBuffersReady(
        payload.transparentCsgSnapshot.instanceCount,
        payload.transparentCsgSnapshot.materialTypedByteCount
    );
    const bool csgResourcesReady = renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData);
    const bool receiverSurfaceDrawResourcesReady = renderer.m_materialSystem.materialPassDrawResourcesReady(
        receiverSurfaceDrawItems
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
        renderer.m_csgSystem.dispatchCsgReceiverSpanBuild(
            commandList,
            *payload.targets,
            csgFrameData,
            payload.receiverSpanOutputImageStatesGraphOwned,
            payload.receiverSpanInputImageStatesGraphOwned
        );
    }
    else{
        // Pre and Span use the same frozen readiness snapshot. A defensive mismatch must not leave a timestamp
        // reservation open or let Combine consume an unbuilt span image.
        payload.transparentCsgIntervalsTiming->value().discardTiming();
        payload.transparentCsgIntervalsTiming->reset();
    }
    commandList.endRenderPass();
    return true;
}


void AvboitCsgReceiverSpanGraphTask::discarded(Payload& payload){
    if(payload.transparentCsgIntervalsTiming && payload.transparentCsgIntervalsTiming->has_value()){
        payload.transparentCsgIntervalsTiming->value().discardTiming();
        payload.transparentCsgIntervalsTiming->reset();
    }
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
        !payload.renderer
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
    RendererSystem& renderer = *payload.renderer;
    const bool drawBuffersReady = renderer.m_materialSystem.materialPassDrawBuffersReady(
        payload.transparentCsgSnapshot.instanceCount,
        payload.transparentCsgSnapshot.materialTypedByteCount
    );
    const bool csgResourcesReady = renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData);
    const bool receiverSurfaceDrawResourcesReady = renderer.m_materialSystem.materialPassDrawResourcesReady(
        receiverSurfaceDrawItems
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
        renderer.m_csgSystem.dispatchCsgIntervalCombine(
            commandList,
            *payload.targets,
            csgFrameData,
            payload.removedIntervalOutputImageStatesGraphOwned,
            payload.intervalCombineInputImageStatesGraphOwned
        );
        payload.transparentCsgIntervalsTiming->value().finishTiming(commandList);
        payload.transparentCsgIntervalsTiming->reset();
    }
    else{
        // Pre and Combine use the same frozen readiness snapshot. A defensive mismatch must not leave a
        // timestamp reservation open or publish a stale removed-interval image.
        payload.transparentCsgIntervalsTiming->value().discardTiming();
        payload.transparentCsgIntervalsTiming->reset();
    }
    commandList.endRenderPass();
    return true;
}


void AvboitCsgIntervalCombineGraphTask::discarded(Payload& payload){
    if(payload.transparentCsgIntervalsTiming && payload.transparentCsgIntervalsTiming->has_value()){
        payload.transparentCsgIntervalsTiming->value().discardTiming();
        payload.transparentCsgIntervalsTiming->reset();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

