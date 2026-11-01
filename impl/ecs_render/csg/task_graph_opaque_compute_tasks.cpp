// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_opaque_compute_tasks.h"

#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueCsgReceiverComputeEmulationGraphPlan::reset()noexcept{
    meshDrawItems.clear();
    indexedDrawItems.clear();
    drawItems.clear();
    regularDrawItems.clear();
    outputBuffers.clear();
    outputLayouts.clear();
    regularOutputBuffers.clear();
    regularOutputLayouts.clear();
    receiverRanges.clear();
    cutters.clear();
    workRegion = {};
    captured = false;
}

b8 OpaqueCsgReceiverComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& receiverSurfaceDrawItems, const MaterialPassDrawItemsView& sourceRegularDrawItems, const CsgFrameGpuDataView& csgFrameData, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(receiverSurfaceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
        return false;

    meshDrawItems.reserve(receiverSurfaceDrawItems.meshDrawItems.size());
    meshDrawItems.assign(receiverSurfaceDrawItems.meshDrawItems.begin(), receiverSurfaceDrawItems.meshDrawItems.end());
    indexedDrawItems.reserve(receiverSurfaceDrawItems.indexedDrawItems.size());
    indexedDrawItems.assign(receiverSurfaceDrawItems.indexedDrawItems.begin(), receiverSurfaceDrawItems.indexedDrawItems.end());
    regularDrawItems.reserve(sourceRegularDrawItems.computeDrawItems.size());
    regularDrawItems.assign(sourceRegularDrawItems.computeDrawItems.begin(), sourceRegularDrawItems.computeDrawItems.end());
    drawItems.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
    outputBuffers.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
    outputLayouts.reserve(receiverSurfaceDrawItems.computeDrawItems.size());
    regularOutputBuffers.reserve(regularDrawItems.size());
    regularOutputLayouts.reserve(regularDrawItems.size());
    MaterialPassEmulationOutputIndex<NotNull<Core::Buffer*>> outputs(scratchArena);
    for(const MaterialPassDrawItem& regularDrawItem : regularDrawItems){
        const MaterialPassMeshResourceSnapshot& regularMesh = regularDrawItem.meshResources;
        if(!regularMesh.emulationVertexBuffer || !regularMesh.emulationVertexHeapHandle.valid()){
            reset();
            return false;
        }
        regularOutputBuffers.push_back(regularMesh.emulationVertexBuffer);
        regularOutputLayouts.push_back({ regularMesh.emulationIndexByteOffset, regularDrawItem.pipelineResources.indexedGeometryOutput });
        outputs.include(MakeNotNull(regularMesh.emulationVertexBuffer.get()));
    }
    for(const MaterialPassDrawItem& drawItem : receiverSurfaceDrawItems.computeDrawItems){
        if(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None){
            reset();
            return false;
        }
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        if(!mesh.emulationVertexBuffer || !mesh.emulationVertexHeapHandle.valid()){
            reset();
            return false;
        }
        // Earlier ownership and receiver outputs both exclude this receiver.
        if(!outputs.insert(MakeNotNull(mesh.emulationVertexBuffer.get()))){
            reset();
            return false;
        }
        drawItems.push_back(drawItem);
        outputBuffers.push_back(mesh.emulationVertexBuffer);
        outputLayouts.push_back({ mesh.emulationIndexByteOffset, drawItem.pipelineResources.indexedGeometryOutput });
    }
    receiverRanges.reserve(csgFrameData.receiverRanges.size());
    receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
    cutters.reserve(csgFrameData.cutters.size());
    cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
    workRegion = csgFrameData.workRegion;
    captured = drawItems.size() == outputBuffers.size() && !drawItems.empty();
    return captured;
}

b8 OpaqueCsgReceiverComputeEmulationGraphPlan::matches(Core::Alloc::ScratchArena& scratchArena)const{
    if(!captured || outputBuffers.size() != drawItems.size() || outputLayouts.size() != drawItems.size() || regularOutputBuffers.size() != regularDrawItems.size() || regularOutputLayouts.size() != regularDrawItems.size())
        return false;
    MaterialPassEmulationOutputIndex<NotNull<Core::Buffer*>> receiverOutputs(scratchArena);
    for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
        const MaterialPassMeshResourceSnapshot& mesh = drawItems[drawIndex].meshResources;
        if(
            !mesh.emulationVertexBuffer
            || !mesh.emulationVertexHeapHandle.valid()
            || mesh.emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
            || !outputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, drawItems[drawIndex].pipelineResources.indexedGeometryOutput)
        )
            return false;
        receiverOutputs.include(MakeNotNull(mesh.emulationVertexBuffer.get()));
    }
    for(usize drawIndex = 0u; drawIndex < regularDrawItems.size(); ++drawIndex){
        const MaterialPassMeshResourceSnapshot& mesh = regularDrawItems[drawIndex].meshResources;
        if(
            !mesh.emulationVertexBuffer
            || !mesh.emulationVertexHeapHandle.valid()
            || mesh.emulationVertexBuffer.get() != regularOutputBuffers[drawIndex].get()
            || !regularOutputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, regularDrawItems[drawIndex].pipelineResources.indexedGeometryOutput)
        )
            return false;
        if(receiverOutputs.contains(MakeNotNull(mesh.emulationVertexBuffer.get())))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueCsgIntervalSampleComputeEmulationGraphPlan::reset()noexcept{
    OpaqueRegularComputeEmulationGraphPlan::reset();
    receiverRanges.clear();
    cutters.clear();
    workRegion = {};
}

b8 OpaqueCsgIntervalSampleComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, const CsgFrameGpuDataView& csgFrameData, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(sourceDrawItems.computeDrawItems.empty() || !csgFrameData.hasWork())
        return false;

    meshDrawItems.reserve(sourceDrawItems.meshDrawItems.size());
    meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
    indexedDrawItems.reserve(sourceDrawItems.indexedDrawItems.size());
    indexedDrawItems.assign(sourceDrawItems.indexedDrawItems.begin(), sourceDrawItems.indexedDrawItems.end());
    if(!CaptureAliasFreeGeneratedGeometryDraws(sourceDrawItems, [](const MaterialPipelineCsgMode::Enum mode)noexcept{ return mode != MaterialPipelineCsgMode::None; }, *this, scratchArena)){
        reset();
        return false;
    }
    receiverRanges.reserve(csgFrameData.receiverRanges.size());
    receiverRanges.assign(csgFrameData.receiverRanges.begin(), csgFrameData.receiverRanges.end());
    cutters.reserve(csgFrameData.cutters.size());
    cutters.assign(csgFrameData.cutters.begin(), csgFrameData.cutters.end());
    workRegion = csgFrameData.workRegion;
    captured = drawItems.size() == outputBuffers.size() && outputBuffers.size() == outputHeapSlots.size() && !drawItems.empty();
    return captured;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 OpaqueCsgReceiverComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.plan.captured)
        return false;

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    if(!frameSetupReady)
        return true;

    Core::Alloc::ScratchArena scratchArena(s_RenderArena);
    // The imported output set is immutable; reject a corrupted plan.
    if(!payload.plan.matches(scratchArena))
        return false;

    const MaterialPassDrawItemsView drawItems = payload.plan.drawItemsView();
    const CsgFrameGpuDataView csgFrameData = payload.plan.csgFrameDataView();
    const b8 deferredResourcesReady = payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount);
    const b8 csgResourcesReady = deferredResourcesReady && payload.csgFrameBuffersUploaded && csgFrameData.hasWork() && payload.csgResources.frameReady(csgFrameData);
    if(!csgResourcesReady || !materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings))
        return true;

    Core::ViewportState csgViewportState;
    csgViewportState.addViewport(payload.targets.framebuffer->getFramebufferInfo().getViewport()).addScissorRect(ToCoreRect(csgFrameData.workRegion.resolveRect(payload.targets.width, payload.targets.height)));
    const MaterialPassDrawContext drawContext{
        commandList,
        payload.targets.framebuffer.get(),
        nullptr,
        csgViewportState,
        &payload.csgResources,
        payload.frameBindings,
        MaterialPipelinePass::CsgReceiverSurface,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 OpaqueCsgIntervalSampleComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.plan.captured)
        return false;

    Core::GraphicsRuntime& graphics = payload.graphics;
    RendererMaterialSystem& materialSystem = payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    if(!frameSetupReady)
        return true;

    // Reject a corrupted retained plan.
    if(!payload.plan.matches())
        return false;

    const MaterialPassDrawItemsView drawItems = payload.plan.drawItemsView();
    const CsgFrameGpuDataView csgFrameData = payload.plan.csgFrameDataView();
    const b8 deferredResourcesReady = payload.materialDrawBuffersUploaded && payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount);
    const b8 csgResourcesReady = deferredResourcesReady && payload.csgFrameBuffersUploaded && csgFrameData.hasWork() && payload.csgResources.frameReady(csgFrameData);
    if(!csgResourcesReady || !materialSystem.materialPassDrawResourcesReady(drawItems, payload.frameBindings))
        return true;
    if(payload.opaqueCsgTiming.has_value())
        return false;

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(payload.targets.framebuffer->getFramebufferInfo().getViewport());
    payload.opaqueCsgTiming.emplace(graphics.gpuTiming(), RendererGpuTimingScope::s_OpaqueCsg, graphics.getDevice(), commandList);
    // Close the marker here; the sample callback owns finishTiming/discard.
    if(!Core::FinishSplitGpuTimingMarker(&payload.opaqueCsgTiming))
        return false;
    const MaterialPassDrawContext drawContext{
        commandList,
        nullptr,
        nullptr,
        deferredViewportState,
        &payload.csgResources,
        payload.frameBindings,
        MaterialPipelinePass::Opaque,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

