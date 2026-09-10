// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpaqueCsgReceiverComputeEmulationGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : plan(arena)
{}


bool OpaqueCsgReceiverComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.materialSystem
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.plan.captured
    )
        return false;

    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    const bool frameSetupReady =
        *payload.meshViewSetupReady
        && *payload.sceneShadingSetupReady
    ;
    if(!frameSetupReady)
        return true;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    payload.plan.materialize(drawItems, csgFrameData);
    // The imported output set is immutable; reject a corrupted plan.
    if(!payload.plan.matches(scratchArena))
        return false;

    const bool deferredResourcesReady =
        payload.materialDrawBuffersUploaded
        && payload.frameBindings.frameReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    ;
    const bool csgResourcesReady =
        deferredResourcesReady
        && payload.csgFrameBuffersUploaded
        && csgFrameData.hasWork()
        && payload.csgResources.frameReady(csgFrameData)
    ;
    if(
        !csgResourcesReady
        || !materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings)
    )
        return true;

    Core::ViewportState csgViewportState;
    csgViewportState
        .addViewport(payload.targets->framebuffer->getFramebufferInfo().getViewport())
        .addScissorRect(csgFrameData.workRegion.resolveRect(payload.targets->width, payload.targets->height))
    ;
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        payload.targets->framebuffer.get(),
        MaterialPipelinePass::CsgReceiverSurface,
        nullptr,
        csgViewportState,
        // Receiver-event images are raster-owned; do not claim them here.
        true,
        false,
        true,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
        &payload.csgResources,
        payload.frameBindings
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


OpaqueCsgIntervalSampleComputeEmulationGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : plan(arena)
{}


bool OpaqueCsgIntervalSampleComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.materialSystem
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueCsgTiming
        || !payload.plan.captured
    )
        return false;

    Core::GraphicsRuntime& graphics = *payload.graphics;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    const bool frameSetupReady =
        *payload.meshViewSetupReady
        && *payload.sceneShadingSetupReady
    ;
    if(!frameSetupReady)
        return true;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    payload.plan.materialize(drawItems, csgFrameData);
    // Reject a corrupted retained plan.
    if(!payload.plan.matches())
        return false;

    const bool deferredResourcesReady =
        payload.materialDrawBuffersUploaded
        && payload.frameBindings.frameReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    ;
    const bool csgResourcesReady =
        deferredResourcesReady
        && payload.csgFrameBuffersUploaded
        && csgFrameData.hasWork()
        && payload.csgResources.frameReady(csgFrameData)
    ;
    if(
        !csgResourcesReady
        || !materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings)
    )
        return true;
    if(payload.opaqueCsgTiming->has_value())
        return false;

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(
        payload.targets->framebuffer->getFramebufferInfo().getViewport()
    );
    payload.opaqueCsgTiming->emplace(
        graphics.gpuTiming(),
        RendererGpuTimingScope::s_OpaqueCsg,
        graphics.getDevice(),
        commandList
    );
    // Close the marker here; the sample callback owns finishTiming/discard.
    if(!Core::FinishSplitGpuTimingMarker(payload.opaqueCsgTiming))
        return false;
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        nullptr,
        MaterialPipelinePass::Opaque,
        nullptr,
        deferredViewportState,
        false,
        payload.intervalSampleImageStatesGraphOwned,
        payload.csgClipBufferStatesGraphOwned,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
        &payload.csgResources,
        payload.frameBindings
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

