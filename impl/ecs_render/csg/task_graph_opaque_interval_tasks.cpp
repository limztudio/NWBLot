// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <core/graphics/gpu_timing.h>


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
        !payload.renderer
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueDrawSnapshot.captured
    )
        return false;

    RendererSystem& renderer = *payload.renderer;
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
        && renderer.m_materialSystem.materialPassDrawBuffersReady(
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
                && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
            )
        )
    ;
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csgReceiverSurface.empty()
            || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
    ;
    if(csgResourcesReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
        renderer.m_csgSystem.dispatchCsgReceiverSpanBuild(
            commandList,
            deferredTargets,
            csgFrameData,
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
        !payload.renderer
        || !payload.targets
        || !payload.timingTicket
        || !*payload.timingTicket
        || !payload.meshViewSetupReady
        || !payload.sceneShadingSetupReady
        || !payload.opaqueDrawSnapshot.captured
    )
        return false;

    RendererSystem& renderer = *payload.renderer;
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
        && renderer.m_materialSystem.materialPassDrawBuffersReady(
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
                && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
            )
        )
    ;
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csgReceiverSurface.empty()
            || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
    ;
    if(csgResourcesReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
        renderer.m_csgSystem.dispatchCsgIntervalCombine(
            commandList,
            deferredTargets,
            csgFrameData,
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
        !payload.renderer
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

    RendererSystem& renderer = *payload.renderer;
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
        && renderer.m_materialSystem.materialPassDrawBuffersReady(
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
                && renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData)
            )
        )
    ;
    const bool csgDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csg.empty() || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
    ;
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (opaqueDrawItems.csgReceiverSurface.empty()
            || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
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
            deferredTargets.framebuffer.get(),
            MaterialPipelinePass::Opaque,
            nullptr,
            deferredViewportState,
            false,
            payload.intervalSampleImageStatesGraphOwned,
            payload.csgClipBufferStatesGraphOwned,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.csgComputeEmulationOutputStatesGraphOwned
        };
        if(!opaqueDrawItems.csg.empty()){
            if(payload.csgComputeEmulationOutputStatesGraphOwned){
                renderer.m_materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
                payload.opaqueCsgComputeEmulationTiming->value().finishTiming(commandList);
                payload.opaqueCsgComputeEmulationTiming->reset();
            }
            else{
                Core::GpuTimingMeasure timing(
                    renderer.m_graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueCsg,
                    renderer.m_graphics.getDevice(),
                    commandList
                );
                renderer.m_materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
            }
        }
        if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
            renderer.m_csgSystem.renderCsgIntervalCaps(
                commandList,
                deferredTargets,
                csgFrameData,
                payload.intervalSampleImageStatesGraphOwned,
                payload.csgClipBufferStatesGraphOwned,
                payload.materialFrameStatesGraphOwned
            );
        }
    }
    commandList.endRenderPass();
    return true;
}


void CsgIntervalSampleGraphTask::discarded(Payload& payload){
    if(
        !payload.opaqueCsgComputeEmulationTiming
        || !payload.opaqueCsgComputeEmulationTiming->has_value()
    )
        return;
    payload.opaqueCsgComputeEmulationTiming->value().discardTiming();
    payload.opaqueCsgComputeEmulationTiming->reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

