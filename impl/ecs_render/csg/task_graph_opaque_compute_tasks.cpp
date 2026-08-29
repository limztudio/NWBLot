// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/module.h>


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
        !payload.meshSystem
        || !payload.materialSystem
        || !payload.csgSystem
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.plan.captured
    )
        return false;

    RendererMeshSystem& meshSystem = *payload.meshSystem;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
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
    // The output set imported by the graph is immutable.  Do not re-resolve a changed mesh resource into an
    // arbitrary descriptor slot after declaration; reject and let the existing graph retry path rebuild it.
    if(!payload.plan.matches(meshSystem))
        return false;

    const bool deferredResourcesReady =
        payload.materialDrawBuffersUploaded
        && materialSystem.materialPassDrawBuffersReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    ;
    const bool csgResourcesReady =
        deferredResourcesReady
        && payload.csgFrameBuffersUploaded
        && csgFrameData.hasWork()
        && csgSystem.csgFrameBuffersReady(csgFrameData)
    ;
    if(
        !csgResourcesReady
        || !materialSystem.materialPassDrawResourcesReady(drawItems)
    )
        return true;

    Core::ViewportState csgViewportState;
    csgViewportState
        .addViewport(payload.targets->framebuffer->getFramebufferInfo().getViewport())
        .addScissorRect(csgFrameData.workRegion.resolveRect(payload.targets->width, payload.targets->height))
    ;
    const MaterialPassDrawContext drawContext{
        commandList,
        payload.targets->framebuffer.get(),
        MaterialPipelinePass::CsgReceiverSurface,
        nullptr,
        csgViewportState,
        // Receiver-event images are raster-only outputs.  The subsequent G-buffer task owns their UAV state;
        // this compute producer uses the CSG clip bindings but must not claim or transition those images.
        true,
        false,
        true,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


OpaqueCsgIntervalSampleComputeEmulationGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : plan(arena)
{}


void OpaqueCsgIntervalSampleComputeEmulationGraphTask::discardTiming(
    Optional<Core::GpuTimingMeasure>* const timing
){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}


bool OpaqueCsgIntervalSampleComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.meshSystem
        || !payload.materialSystem
        || !payload.csgSystem
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueCsgTiming
        || !payload.plan.captured
    )
        return false;

    Core::Graphics& graphics = *payload.graphics;
    RendererMeshSystem& meshSystem = *payload.meshSystem;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
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
    // The graph imported these exact output handles and descriptor slots. A live replacement must reject the
    // packet, not write through a newly selected CSG material descriptor after declaration.
    if(!payload.plan.matches(meshSystem))
        return false;

    const bool deferredResourcesReady =
        payload.materialDrawBuffersUploaded
        && materialSystem.materialPassDrawBuffersReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
    ;
    const bool csgResourcesReady =
        deferredResourcesReady
        && payload.csgFrameBuffersUploaded
        && csgFrameData.hasWork()
        && csgSystem.csgFrameBuffersReady(csgFrameData)
    ;
    if(
        !csgResourcesReady
        || !materialSystem.materialPassDrawResourcesReady(drawItems)
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
    // The scope crosses the following raster callback, so close its marker in this producer before command-list
    // finalization. The terminal sample callback owns finishTiming/discard.
    payload.opaqueCsgTiming->value().finishMarker();
    const MaterialPassDrawContext drawContext{
        commandList,
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
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


void OpaqueCsgIntervalSampleComputeEmulationGraphTask::discarded(Payload& payload){
    discardTiming(payload.opaqueCsgTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

