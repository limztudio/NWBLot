// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpaqueRegularComputeEmulationGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : plan(arena)
{}


bool OpaqueRegularComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.meshSystem
        || !payload.materialSystem
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
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    const bool frameSetupReady =
        *payload.meshViewSetupReady
        && *payload.sceneShadingSetupReady
    ;
    if(!frameSetupReady)
        return true;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    payload.plan.materialize(drawItems);
    // The graph imported the exact persistent output handles retained by the frozen plan. Reject a live mesh
    // resource replacement instead of dispatching into a newly selected descriptor target outside that set.
    if(!payload.plan.matches(meshSystem, drawItems.computeDrawItems))
        return false;
    if(
        !payload.materialDrawBuffersUploaded
        || !payload.frameBindings.frameReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
        || !materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings)
    )
        return true;

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(
        payload.targets->framebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        nullptr,
        MaterialPipelinePass::Opaque,
        nullptr,
        deferredViewportState,
        false,
        false,
        false,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
        nullptr,
        &payload.frameBindings
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


void OpaqueRegularSharedComputeEmulationGraphTask::discardTiming(
    Optional<Core::GpuTimingMeasure>* const timing
){
    if(!timing || !timing->has_value())
        return;
    timing->value().discardTiming();
    timing->reset();
}


bool OpaqueRegularSharedComputeEmulationGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.meshSystem
        || !payload.materialSystem
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueRegularTiming
        || !payload.plan.captured
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    // G-buffer starts the one preserved Opaque Regular range only after its exact frozen material resources
    // are ready. A later defensive miss must be a no-op instead of rasterizing stale generated vertices.
    if(!payload.opaqueRegularTiming->has_value()){
        if(payload.phase == Phase::Raster)
            commandList.endRenderPass();
        return true;
    }

    RendererMeshSystem& meshSystem = *payload.meshSystem;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    const bool frameSetupReady =
        *payload.meshViewSetupReady
        && *payload.sceneShadingSetupReady
    ;
    if(!frameSetupReady || !payload.plan.matches(meshSystem, payload.drawIndex))
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    payload.plan.materialize(payload.drawIndex, drawItems);
    if(
        !payload.materialDrawBuffersUploaded
        || !payload.frameBindings.frameReady(
            payload.instanceCount,
            payload.materialTypedByteCount
        )
        || !materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings)
    ){
        discardTiming(payload.opaqueRegularTiming);
        if(payload.phase == Phase::Raster)
            commandList.endRenderPass();
        return true;
    }

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(
        payload.targets->framebuffer->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        *payload.targets,
        payload.phase == Phase::Raster ? payload.targets->framebuffer.get() : nullptr,
        MaterialPipelinePass::Opaque,
        nullptr,
        deferredViewportState,
        false,
        false,
        false,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true,
        nullptr,
        &payload.frameBindings
    };
    if(payload.phase == Phase::Generate)
        materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    else{
        materialSystem.renderComputeMaterialPassDrawItemsRasterOnly(
            drawContext,
            drawItems.computeDrawItems
        );
        if(payload.finishTiming){
            payload.opaqueRegularTiming->value().finishTiming(commandList);
            payload.opaqueRegularTiming->reset();
        }
        commandList.endRenderPass();
    }
    return true;
}


void OpaqueRegularSharedComputeEmulationGraphTask::discarded(Payload& payload){
    discardTiming(payload.opaqueRegularTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

