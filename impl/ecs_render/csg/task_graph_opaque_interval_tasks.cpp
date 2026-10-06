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
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_opaque_interval_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The span-build and combine passes materialize the same opaque snapshot and gate on
// the same draw-buffer, CSG-resource, and receiver-surface readiness.
struct OpaqueIntervalReadiness{
    MaterialPassDrawItemPartitions opaqueDrawItems;
    CsgFrameGpuData csgFrameData;
    bool csgResourcesReady = false;
    bool csgReceiverSurfaceDrawResourcesReady = false;

    explicit OpaqueIntervalReadiness(Core::Alloc::ScratchArena& arena)
        : opaqueDrawItems(arena)
        , csgFrameData(arena)
    {}
};

[[nodiscard]] inline OpaqueIntervalReadiness ResolveOpaqueIntervalReadiness(
    const CsgOpaqueIntervalRecordInputs& payload,
    RendererMaterialSystem& materialSystem,
    Core::Alloc::ScratchArena& scratchArena
){
    OpaqueIntervalReadiness readiness{ scratchArena };
    const bool frameSetupReady = ECSRenderDetail::FrameSetupReady(
        payload.meshViewSetupReady,
        payload.sceneShadingSetupReady
    );
    if(frameSetupReady)
        payload.opaqueDrawSnapshot.materialize(readiness.opaqueDrawItems, readiness.csgFrameData);

    const bool hasDeferredDrawItems = !readiness.opaqueDrawItems.empty();
    const bool deferredResourcesReady =
        hasDeferredDrawItems
        && payload.materialDrawBuffersUploaded
        && payload.frameBindings.frameReady(
            payload.opaqueDrawSnapshot.instanceCount,
            payload.opaqueDrawSnapshot.materialTypedByteCount
        )
    ;
    readiness.csgResourcesReady =
        deferredResourcesReady
        && (
            !readiness.csgFrameData.hasWork()
            || (
                payload.csgFrameBuffersUploaded
                && payload.csgResources.frameReady(readiness.csgFrameData)
            )
        )
    ;
    readiness.csgReceiverSurfaceDrawResourcesReady =
        readiness.csgResourcesReady
        && (readiness.opaqueDrawItems.csgReceiverSurface.empty()
            || materialSystem.materialPassDrawResourcesReady(readiness.opaqueDrawItems.csgReceiverSurface, payload.frameBindings))
    ;
    return readiness;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CsgReceiverSpanBuildGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(!payload.opaqueDrawSnapshot.captured)
        return false;

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    __hidden_opaque_interval_tasks::OpaqueIntervalReadiness readiness =
        __hidden_opaque_interval_tasks::ResolveOpaqueIntervalReadiness(payload, materialSystem, scratchArena);
    if(readiness.csgResourcesReady && readiness.csgFrameData.hasWork() && readiness.csgReceiverSurfaceDrawResourcesReady){
        csgSystem.dispatchCsgReceiverSpanBuild(
            commandList,
            deferredTargets,
            readiness.csgFrameData,
            payload.csgResources
        );
    }
    commandList.endRenderPass();
    return true;
}


bool CsgIntervalCombineGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(!payload.opaqueDrawSnapshot.captured)
        return false;

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    __hidden_opaque_interval_tasks::OpaqueIntervalReadiness readiness =
        __hidden_opaque_interval_tasks::ResolveOpaqueIntervalReadiness(payload, materialSystem, scratchArena);
    if(readiness.csgResourcesReady && readiness.csgFrameData.hasWork() && readiness.csgReceiverSurfaceDrawResourcesReady){
        csgSystem.dispatchCsgIntervalCombine(
            commandList,
            deferredTargets,
            readiness.csgFrameData,
            payload.csgResources
        );
    }
    commandList.endRenderPass();
    return true;
}


CsgIntervalSampleGraphTask::Payload::Payload(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphicsIn,
    RendererMaterialSystem& materialSystemIn,
    RendererCsgSystem& csgSystemIn,
    DeferredFrameTargets& targetsIn,
    Core::GpuTimingSubmissionTicket*& timingTicketIn,
    const bool& meshViewSetupReadyIn,
    const bool& sceneShadingSetupReadyIn
)
    : graphics(graphicsIn)
    , materialSystem(materialSystemIn)
    , csgSystem(csgSystemIn)
    , targets(targetsIn)
    , timingTicket(timingTicketIn)
    , meshViewSetupReady(meshViewSetupReadyIn)
    , sceneShadingSetupReady(sceneShadingSetupReadyIn)
    , opaqueDrawSnapshot(arena)
{}


bool CsgIntervalSampleGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.opaqueDrawSnapshot.captured
        || (payload.csgComputeEmulationOutputStatesGraphOwned
            && !payload.opaqueCsgComputeEmulationTiming)
    )
        return false;

    Core::GraphicsRuntime& graphics = payload.graphics;
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

    MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    const bool frameSetupReady = ECSRenderDetail::FrameSetupReady(
        payload.meshViewSetupReady,
        payload.sceneShadingSetupReady
    );
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
    // Retire the reservation on later disagreement; never leave it stale.
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
            nullptr,
            deferredViewportState,
            &payload.csgResources,
            payload.frameBindings,
            MaterialPipelinePass::Opaque,
            payload.materialFrameStatesGraphOwned,
            payload.materialGeometryStatesGraphOwned,
            payload.csgComputeEmulationOutputStatesGraphOwned
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
                payload.csgResources
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

