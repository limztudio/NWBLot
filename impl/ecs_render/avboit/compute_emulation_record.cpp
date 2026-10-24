// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compute_emulation_record.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitComputeEmulation(const AvboitComputeEmulationRecordInputs& inputs, Core::CommandList& commandList, const AvboitComputeEmulationRecordTrait& trait){
    if(!trait.framebuffer || (!inputs.plan.captured && !inputs.csgPlan.captured) || inputs.plan.captured == inputs.csgPlan.captured)
        return false;

    Core::GraphicsRuntime& graphics = inputs.graphics;
    RendererMaterialSystem& materialSystem = inputs.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(inputs.timingTicket);
    const bool csgComputeEmulation = inputs.csgPlan.captured;
    if(!(csgComputeEmulation ? inputs.csgPlan.matches() : inputs.plan.matches()) || !inputs.materialDrawBuffersUploaded || !inputs.frameBindings.frameReady(inputs.instanceCount, inputs.materialTypedByteCount))
        return false;

    const Span<const MaterialPassDrawItem> computeDrawItems = csgComputeEmulation ? Span<const MaterialPassDrawItem>(inputs.csgPlan.drawItems) : Span<const MaterialPassDrawItem>(inputs.plan.drawItems);
    // Reject late losses so the packet is discarded and the next frame re-preflights.
    const bool drawResourcesReady = csgComputeEmulation
        ? materialSystem.materialPassDrawResourcesReady(inputs.csgPlan.drawItemsView(), inputs.frameBindings)
        : materialSystem.computeMaterialPassDrawResourcesReady(computeDrawItems, inputs.frameBindings)
    ;
    if(!drawResourcesReady)
        return false;
    if(csgComputeEmulation){
        const CsgFrameGpuDataView csgFrameData = inputs.csgPlan.csgFrameDataView();
        if(!inputs.csgFrameBuffersUploaded || !csgFrameData.hasWork() || !inputs.csgResources.frameReady(csgFrameData))
            return false;
    }
    if(inputs.timing.has_value())
        return false;

    commandList.endRenderPass();
    inputs.timing.emplace(graphics.gpuTiming(), trait.timingScope, graphics.getDevice(), commandList);
    // Close the marker now; the raster consumer owns finishTiming/discard.
    if(!Core::FinishSplitGpuTimingMarker(&inputs.timing))
        return false;
    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect((inputs.targets.avboit.*(trait.framebuffer))->getFramebufferInfo().getViewport());
    const MaterialPassDrawContext drawContext{
        commandList,
        nullptr,
        &inputs.targets.avboit,
        viewportState,
        csgComputeEmulation ? &inputs.csgResources : nullptr,
        inputs.frameBindings,
        trait.pipelinePass,
        inputs.materialFrameStatesGraphOwned,
        inputs.materialGeometryStatesGraphOwned,
        true,
        inputs.conservativeGeometryScissor
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, computeDrawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitSharedComputeEmulation(const AvboitSharedComputeEmulationRecordInputs& inputs, Core::CommandList& commandList, const AvboitSharedComputeEmulationRecordTrait& trait){
    const bool generatePhase = inputs.phase == AvboitSharedComputeEmulationPhase::Generate;
    if(!trait.framebuffer || !(inputs.targets.avboit.*(trait.framebuffer)) || !inputs.plan.captured || inputs.drawIndex >= inputs.plan.drawCount)
        return false;

    Core::GraphicsRuntime& graphics = inputs.graphics;
    RendererMaterialSystem& materialSystem = inputs.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(inputs.timingTicket);
    if(!inputs.plan.matches(inputs.drawIndex) || !inputs.materialDrawBuffersUploaded || !inputs.frameBindings.frameReady(inputs.instanceCount, inputs.materialTypedByteCount))
        return false;

    const Span<const MaterialPassDrawItem> drawItems(&inputs.plan.drawItems[inputs.drawIndex], 1u);
    if(!materialSystem.computeMaterialPassDrawResourcesReady(drawItems, inputs.frameBindings))
        return false;

    if(generatePhase){
        // End the prior raster phase before the marker and compute bind.
        commandList.endRenderPass();
        if(inputs.beginTiming){
            if(inputs.timing.has_value())
                return false;
            inputs.timing.emplace(graphics.gpuTiming(), trait.timingScope, graphics.getDevice(), commandList);
            // Close the marker before advancing to the raster consumer.
            if(!Core::FinishSplitGpuTimingMarker(&inputs.timing))
                return false;
        }
        else if(!inputs.timing.has_value())
            return false;
    }
    else if(!inputs.timing.has_value())
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect((inputs.targets.avboit.*(trait.framebuffer))->getFramebufferInfo().getViewport());
    Core::Framebuffer* const rasterFramebuffer = generatePhase ? nullptr : (inputs.targets.avboit.*(trait.framebuffer)).get();
    const MaterialPassDrawContext drawContext{ commandList, rasterFramebuffer, &inputs.targets.avboit, viewportState, nullptr, inputs.frameBindings, trait.pipelinePass, inputs.materialFrameStatesGraphOwned, inputs.materialGeometryStatesGraphOwned, true };
    if(generatePhase){
        materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems);
    }
    else{
        materialSystem.renderComputeMaterialPassDrawItemsRasterOnly(drawContext, drawItems);
        if(inputs.finishTiming){
            inputs.timing.value().finishTiming(commandList);
            inputs.timing.reset();
        }
        // Never bind compute while dynamic rendering is active.
        commandList.endRenderPass();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

