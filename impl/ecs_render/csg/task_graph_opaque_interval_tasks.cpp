// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_opaque_interval_tasks.h"

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


// The span-build and combine passes inspect the same opaque snapshot and gate on the same draw-buffer, CSG-resource, and receiver-surface readiness.
struct OpaqueIntervalReadiness{
    CsgFrameGpuDataView csgFrameData;
    b8 csgResourcesReady = false;
    b8 csgReceiverSurfaceDrawResourcesReady = false;
};

[[nodiscard]] inline OpaqueIntervalReadiness ResolveOpaqueIntervalReadiness(const CsgOpaqueIntervalRecordInputs& payload, RendererMaterialSystem& materialSystem)noexcept{
    OpaqueIntervalReadiness readiness{};
    const b8 frameSetupReady = ECSRenderDetail::FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    const MaterialPassDrawItemPartitionsView opaqueDrawItems = frameSetupReady ? payload.opaqueDrawSnapshot.drawItemsView() : MaterialPassDrawItemPartitionsView{};
    if(frameSetupReady)
        readiness.csgFrameData = payload.opaqueDrawSnapshot.csgFrameDataView();

    const b8 hasDeferredDrawItems = !opaqueDrawItems.empty();
    const b8 deferredResourcesReady = hasDeferredDrawItems && payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.opaqueDrawSnapshot.instanceCount, payload.opaqueDrawSnapshot.materialTypedByteCount);
    readiness.csgResourcesReady = deferredResourcesReady && (!readiness.csgFrameData.hasWork() || (payload.csgFrameBuffersUploaded && payload.csgResources.frameReady(readiness.csgFrameData)));
    readiness.csgReceiverSurfaceDrawResourcesReady = readiness.csgResourcesReady && (opaqueDrawItems.csgReceiverSurface.empty() || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface, payload.frameBindings));
    return readiness;
}

template<typename Dispatch>
[[nodiscard]] inline b8 RecordOpaqueInterval(const CsgOpaqueIntervalRecordInputs& payload, Core::CommandList& commandList, Dispatch dispatch){
    if(!payload.opaqueDrawSnapshot.captured)
        return false;

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const OpaqueIntervalReadiness readiness = ResolveOpaqueIntervalReadiness(payload, materialSystem);
    if(readiness.csgResourcesReady && readiness.csgFrameData.hasWork() && readiness.csgReceiverSurfaceDrawResourcesReady){
        dispatch(csgSystem, commandList, deferredTargets, readiness.csgFrameData, payload.csgResources);
    }
    commandList.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgReceiverSpanBuildGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    return __hidden_opaque_interval_tasks::RecordOpaqueInterval(
        payload,
        commandList,
        [](RendererCsgSystem& csgSystem, Core::CommandList& cmd, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const CsgGraphResourceSnapshot& csgResources){
            csgSystem.dispatchCsgReceiverSpanBuild(cmd, targets, csgFrameData, csgResources);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgIntervalCombineGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    return __hidden_opaque_interval_tasks::RecordOpaqueInterval(
        payload,
        commandList,
        [](RendererCsgSystem& csgSystem, Core::CommandList& cmd, DeferredFrameTargets& targets, const CsgFrameGpuDataView& csgFrameData, const CsgGraphResourceSnapshot& csgResources){
            csgSystem.dispatchCsgIntervalCombine(cmd, targets, csgFrameData, csgResources);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgIntervalSampleGraphTask::Payload::Payload(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphicsIn,
    RendererMaterialSystem& materialSystemIn,
    RendererCsgSystem& csgSystemIn,
    DeferredFrameTargets& targetsIn,
    Core::GpuTimingSubmissionTicket*& timingTicketIn,
    const b8& meshViewSetupReadyIn,
    const b8& sceneShadingSetupReadyIn
)noexcept
    : graphics(graphicsIn)
    , materialSystem(materialSystemIn)
    , csgSystem(csgSystemIn)
    , targets(targetsIn)
    , timingTicket(timingTicketIn)
    , meshViewSetupReady(meshViewSetupReadyIn)
    , sceneShadingSetupReady(sceneShadingSetupReadyIn)
    , opaqueDrawSnapshot(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgIntervalSampleGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    static_cast<void>(context);
    if(!payload.opaqueDrawSnapshot.captured || (payload.csgComputeEmulationOutputStatesGraphOwned && !payload.opaqueCsgComputeEmulationTiming))
        return false;

    Core::GraphicsRuntime& graphics = payload.graphics;
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    RendererCsgSystem& csgSystem = payload.csgSystem;
    DeferredFrameTargets& deferredTargets = payload.targets;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);

    const b8 frameSetupReady = ECSRenderDetail::FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    const MaterialPassDrawItemPartitionsView opaqueDrawItems = frameSetupReady ? payload.opaqueDrawSnapshot.drawItemsView() : MaterialPassDrawItemPartitionsView{};
    const CsgFrameGpuDataView csgFrameData = frameSetupReady ? payload.opaqueDrawSnapshot.csgFrameDataView() : CsgFrameGpuDataView{};

    const b8 hasDeferredDrawItems = !opaqueDrawItems.empty();
    const b8 deferredResourcesReady = hasDeferredDrawItems && payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.opaqueDrawSnapshot.instanceCount, payload.opaqueDrawSnapshot.materialTypedByteCount);
    const b8 csgResourcesReady = deferredResourcesReady && (!csgFrameData.hasWork() || (payload.csgFrameBuffersUploaded && payload.csgResources.frameReady(csgFrameData)));
    const b8 csgDrawResourcesReady = csgResourcesReady && (opaqueDrawItems.csg.empty() || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg, payload.frameBindings));
    const b8 csgReceiverSurfaceDrawResourcesReady = csgResourcesReady && (opaqueDrawItems.csgReceiverSurface.empty() || materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface, payload.frameBindings));
    // Retire the reservation on later disagreement; never leave it stale.
    if(payload.csgComputeEmulationOutputStatesGraphOwned && payload.opaqueCsgComputeEmulationTiming->has_value() && !csgDrawResourcesReady){
        payload.opaqueCsgComputeEmulationTiming->value().discardTiming();
        payload.opaqueCsgComputeEmulationTiming->reset();
        commandList.endRenderPass();
        return true;
    }
    const b8 csgComputeEmulationReady = !payload.csgComputeEmulationOutputStatesGraphOwned || payload.opaqueCsgComputeEmulationTiming->has_value();
    if(csgDrawResourcesReady && csgComputeEmulationReady){
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
                Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, graphics.getDevice(), commandList);

                materialSystem.renderMaterialPassDrawItems(csgDrawContext, opaqueDrawItems.csg);
            }
        }
        if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady){
            csgSystem.renderCsgIntervalCaps(commandList, deferredTargets, csgFrameData, payload.csgResources);
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

