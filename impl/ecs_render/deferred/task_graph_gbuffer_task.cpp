// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/task_graph_gbuffer_task.h>

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool GbufferGraphTask::record(
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

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

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
    const bool regularDrawResourcesReady =
        deferredResourcesReady
        && materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.regular, payload.frameBindings)
    ;
    MaterialPassDrawItems regularMeshDrawItems{ scratchArena };
    const MaterialPassDrawItems* regularDrawItemsForGbuffer = &opaqueDrawItems.regular;
    if(payload.regularSharedComputeEmulationDrawsGraphOwned){
        regularMeshDrawItems.meshDrawItems.assign(
            opaqueDrawItems.regular.meshDrawItems.begin(),
            opaqueDrawItems.regular.meshDrawItems.end()
        );
        regularDrawItemsForGbuffer = &regularMeshDrawItems;
    }
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
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csgReceiverSurface.empty()
            || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface, payload.frameBindings))
    ;
    if(deferredResourcesReady){
        // Every opaque CSG frame byte is now captured in immutable graph uploads. Native recording consumes those
        // declared resources without rewriting the clip-context or interval-sample uniform payloads.
        const bool csgSampleStateReady = csgResourcesReady;
        if(csgSampleStateReady && csgFrameData.hasWork())
            csgSystem.dispatchCsgIntervalPeels(
                commandList,
                deferredTargets,
                csgFrameData,
                payload.csgResources,
                payload.frameBindings,
                payload.csgIntervalPeelTargetStatesGraphOwned,
                payload.csgClipBufferStatesGraphOwned,
                payload.materialFrameStatesGraphOwned
            );
        const MaterialPassDrawContext opaqueDrawContext{
            commandList,
            deferredTargets,
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            nullptr,
            deferredViewportState,
            false,
            false,
            false,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.regularComputeEmulationOutputStatesGraphOwned,
            nullptr,
            &payload.frameBindings
        };
        if(
            regularDrawResourcesReady
            && (
                payload.regularSharedComputeEmulationDrawsGraphOwned
                || !regularDrawItemsForGbuffer->empty()
            )
        ){
            if(payload.regularSharedComputeEmulationDrawsGraphOwned){
                if(
                    !payload.regularSharedComputeEmulationTiming
                    || payload.regularSharedComputeEmulationTiming->has_value()
                )
                    return false;
                payload.regularSharedComputeEmulationTiming->emplace(
                    graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueRegular,
                    graphics.getDevice(),
                    commandList
                );
                // The timestamps span ordered graph callbacks; the marker must still close in this producer
                // callback before its command list can be finalized.
                payload.regularSharedComputeEmulationTiming->value().finishMarker();
                if(!regularDrawItemsForGbuffer->empty()){
                    materialSystem.renderMaterialPassDrawItems(
                        opaqueDrawContext,
                        *regularDrawItemsForGbuffer
                    );
                }
            }
            else{
                Core::GpuTimingMeasure timing(
                    graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueRegular,
                    graphics.getDevice(),
                    commandList
                );
                materialSystem.renderMaterialPassDrawItems(
                    opaqueDrawContext,
                    *regularDrawItemsForGbuffer
                );
            }
        }

        Core::ViewportState csgIntervalViewportState;
        csgIntervalViewportState
            .addViewport(deferredTargets.framebuffer->getFramebufferInfo().getViewport())
            .addScissorRect(csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height))
        ;
        const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
            commandList,
            deferredTargets,
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            nullptr,
            csgIntervalViewportState,
            payload.csgReceiverSurfaceImageStatesGraphOwned,
            false,
            payload.csgClipBufferStatesGraphOwned,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.csgReceiverComputeEmulationOutputStatesGraphOwned,
            &payload.csgResources,
            &payload.frameBindings
        };
        if(csgSampleStateReady && csgReceiverSurfaceDrawResourcesReady && !opaqueDrawItems.csgReceiverSurface.empty()){
            Core::GpuTimingMeasure timing(
                graphics.gpuTiming(),
                RendererGpuTimingScope::s_OpaqueCsgReceiverSurface,
                graphics.getDevice(),
                commandList
            );
            materialSystem.renderMaterialPassDrawItems(
                csgReceiverSurfaceDrawContext,
                opaqueDrawItems.csgReceiverSurface
            );
        }
    }
    commandList.endRenderPass();
    return true;
}


void GbufferGraphTask::discarded(Payload& payload){
    if(
        !payload.regularSharedComputeEmulationTiming
        || !payload.regularSharedComputeEmulationTiming->has_value()
    )
        return;
    payload.regularSharedComputeEmulationTiming->value().discardTiming();
    payload.regularSharedComputeEmulationTiming->reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

