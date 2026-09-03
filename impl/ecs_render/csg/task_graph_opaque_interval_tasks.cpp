// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgReceiverSpanBuildGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : opaqueDrawSnapshot(arena)
{}


bool CsgReceiverSpanBuildGraphTask::record(
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
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueDrawSnapshot.captured
    )
        return false;

    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
    DeferredFrameTargets& deferredTargets = *payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

    MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    const bool frameSetupReady =
        *payload.meshViewSetupReady
        && payload.sceneShadingSetupReady
        && *payload.sceneShadingSetupReady
    ;
    if(frameSetupReady)
        payload.opaqueDrawSnapshot.materialize(opaqueDrawItems, csgFrameData);

    const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
    const bool deferredResourcesReady =
        hasDeferredDrawItems
        && payload.materialDrawBuffersUploaded
        && payload.frameBindings.frameReady(
            payload.opaqueDrawSnapshot.instanceCount,
            payload.opaqueDrawSnapshot.materialTypedByteCount
        )
    ;
    const bool csgResourcesReady =
        deferredResourcesReady
        && (
            !csgFrameData.hasWork()
            || (
                payload.csgFrameBuffersUploaded
                && payload.csgResources.frameReady(csgFrameData)
            )
        )
    ;
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csgReceiverSurface.empty()
            || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface, payload.frameBindings))
    ;
    if(csgResourcesReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
        csgSystem.dispatchCsgReceiverSpanBuild(
            commandList,
            deferredTargets,
            csgFrameData,
            payload.csgResources,
            payload.receiverSpanOutputImageStatesGraphOwned,
            payload.receiverSpanInputImageStatesGraphOwned
        );
    }
    commandList.endRenderPass();
    return true;
}


CsgIntervalCombineGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : opaqueDrawSnapshot(arena)
{}


bool CsgIntervalCombineGraphTask::record(
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
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueDrawSnapshot.captured
    )
        return false;

    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
    DeferredFrameTargets& deferredTargets = *payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

    MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    const bool frameSetupReady =
        *payload.meshViewSetupReady
        && payload.sceneShadingSetupReady
        && *payload.sceneShadingSetupReady
    ;
    if(frameSetupReady)
        payload.opaqueDrawSnapshot.materialize(opaqueDrawItems, csgFrameData);

    const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
    const bool deferredResourcesReady =
        hasDeferredDrawItems
        && payload.materialDrawBuffersUploaded
        && payload.frameBindings.frameReady(
            payload.opaqueDrawSnapshot.instanceCount,
            payload.opaqueDrawSnapshot.materialTypedByteCount
        )
    ;
    const bool csgResourcesReady =
        deferredResourcesReady
        && (
            !csgFrameData.hasWork()
            || (
                payload.csgFrameBuffersUploaded
                && payload.csgResources.frameReady(csgFrameData)
            )
        )
    ;
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csgReceiverSurface.empty()
            || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface, payload.frameBindings))
    ;
    if(csgResourcesReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
        csgSystem.dispatchCsgIntervalCombine(
            commandList,
            deferredTargets,
            csgFrameData,
            payload.csgResources,
            payload.removedIntervalOutputImageStatesGraphOwned,
            payload.intervalCombineInputImageStatesGraphOwned
        );
    }
    commandList.endRenderPass();
    return true;
}


CsgIntervalSampleGraphTask::Payload::Payload(Core::Alloc::GlobalArena& arena)
    : opaqueDrawSnapshot(arena)
{}


bool CsgIntervalSampleGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.materialSystem
        || !payload.csgSystem
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueDrawSnapshot.captured
        || (payload.csgComputeEmulationOutputStatesGraphOwned
            && !payload.opaqueCsgComputeEmulationTiming)
    )
        return false;

    Core::Graphics& graphics = *payload.graphics;
    RendererMaterialSystem& materialSystem = *payload.materialSystem;
    RendererCsgSystem& csgSystem = *payload.csgSystem;
    DeferredFrameTargets& deferredTargets = *payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

    MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    const bool frameSetupReady =
        *payload.meshViewSetupReady
        && payload.sceneShadingSetupReady
        && *payload.sceneShadingSetupReady
    ;
    if(frameSetupReady)
        payload.opaqueDrawSnapshot.materialize(opaqueDrawItems, csgFrameData);

    const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
    const bool deferredResourcesReady =
        hasDeferredDrawItems
        && payload.materialDrawBuffersUploaded
        && payload.frameBindings.frameReady(
            payload.opaqueDrawSnapshot.instanceCount,
            payload.opaqueDrawSnapshot.materialTypedByteCount
        )
    ;
    const bool csgResourcesReady =
        deferredResourcesReady
        && (
            !csgFrameData.hasWork()
            || (
                payload.csgFrameBuffersUploaded
                && payload.csgResources.frameReady(csgFrameData)
                && payload.frameBindings.bindingValid()
            )
        )
    ;
    const bool csgDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csg.empty()
            || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg, payload.frameBindings))
    ;
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csgReceiverSurface.empty()
            || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface, payload.frameBindings))
    ;
    // The producer validates the same frozen full CSG stream before opening this cross-callback measure. Keep
    // the fallback defensive: if a later readiness check disagrees, retire the reservation instead of leaving
    // a stale generated-vertex raster or an unsubmitted timing scope alive.
    if(
        payload.csgComputeEmulationOutputStatesGraphOwned
        && payload.opaqueCsgComputeEmulationTiming->has_value()
        && (!csgResourcesReady || !csgDrawResourcesReady)
    ){
        payload.opaqueCsgComputeEmulationTiming->value().discardTiming();
        payload.opaqueCsgComputeEmulationTiming->reset();
        commandList.endRenderPass();
        return true;
    }
    const bool csgComputeEmulationReady =
        !payload.csgComputeEmulationOutputStatesGraphOwned
        || payload.opaqueCsgComputeEmulationTiming->has_value()
    ;
    if(csgResourcesReady && csgDrawResourcesReady && csgComputeEmulationReady){
        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());
        const MaterialPassDrawContext csgDrawContext{
            commandList,
            deferredTargets,
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            nullptr,
            deferredViewportState,
            false,
            payload.intervalSampleImageStatesGraphOwned,
            payload.csgClipBufferStatesGraphOwned,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.csgComputeEmulationOutputStatesGraphOwned,
            &payload.csgResources,
            payload.frameBindings
        };
        if(!opaqueDrawItems.csg.empty()){
            if(payload.csgComputeEmulationOutputStatesGraphOwned){
                materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
                payload.opaqueCsgComputeEmulationTiming->value().finishTiming(commandList);
                payload.opaqueCsgComputeEmulationTiming->reset();
            }
            else{
                Core::GpuTimingMeasure timing(
                    graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueCsg,
                    graphics.getDevice(),
                    commandList
                );
                materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
            }
        }
        if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
            csgSystem.renderCsgIntervalCaps(
                commandList,
                deferredTargets,
                csgFrameData,
                payload.csgResources,
                payload.frameBindings,
                payload.intervalSampleImageStatesGraphOwned,
                payload.csgClipBufferStatesGraphOwned,
                payload.materialFrameStatesGraphOwned
            );
        }
    }
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

