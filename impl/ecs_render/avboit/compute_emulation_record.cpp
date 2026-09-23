// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/compute_emulation_record.h>

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitComputeEmulation(
    const AvboitComputeEmulationRecordInputs& inputs,
    Core::CommandList& commandList,
    const AvboitComputeEmulationRecordTrait& trait
){
    if(
        !inputs.graphics
        || !inputs.materialSystem
        || !inputs.targets
        || !inputs.timingTicket
        || !inputs.timing
        || !inputs.frameBindings
        || !inputs.csgResources
        || !inputs.plan
        || !inputs.csgPlan
        || !trait.timingScope
        || !trait.framebuffer
        || (!inputs.plan->captured && !inputs.csgPlan->captured)
        || inputs.plan->captured == inputs.csgPlan->captured
    )
        return false;

    Core::GraphicsRuntime& graphics = *inputs.graphics;
    RendererMaterialSystem& materialSystem = *inputs.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*inputs.timingTicket);
    const bool csgComputeEmulation = inputs.csgPlan->captured;
    if(
        !(csgComputeEmulation
            ? inputs.csgPlan->matches()
            : inputs.plan->matches())
        || !inputs.materialDrawBuffersUploaded
        || !inputs.frameBindings->frameReady(
            inputs.instanceCount,
            inputs.materialTypedByteCount
        )
    )
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    if(csgComputeEmulation)
        inputs.csgPlan->materialize(drawItems, csgFrameData);
    else
        inputs.plan->materialize(drawItems);
    // Reject late losses so the packet is discarded and the next frame re-preflights.
    if(
        !materialSystem.materialPassDrawResourcesReady(drawItems, *inputs.frameBindings)
        || (csgComputeEmulation && (
            !inputs.csgFrameBuffersUploaded
            || !inputs.csgIntervalSampleImageStatesGraphOwned
            || !inputs.csgClipBufferStatesGraphOwned
            || !csgFrameData.hasWork()
            || !inputs.csgResources->frameReady(csgFrameData)
        ))
    )
        return false;
    if(inputs.timing->has_value())
        return false;

    commandList.endRenderPass();
    inputs.timing->emplace(
        graphics.gpuTiming(),
        *trait.timingScope,
        graphics.getDevice(),
        commandList
    );
    // Close the marker now; the raster consumer owns finishTiming/discard.
    if(!Core::FinishSplitGpuTimingMarker(inputs.timing))
        return false;
    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        (inputs.targets->avboit.*(trait.framebuffer))->getFramebufferInfo().getViewport()
    );
    const MaterialPassDrawContext drawContext{
        commandList,
        *inputs.targets,
        nullptr,
        &inputs.targets->avboit,
        viewportState,
        csgComputeEmulation ? inputs.csgResources : nullptr,
        *inputs.frameBindings,
        trait.pipelinePass,
        false,
        csgComputeEmulation && inputs.csgIntervalSampleImageStatesGraphOwned,
        csgComputeEmulation && inputs.csgClipBufferStatesGraphOwned,
        inputs.materialFrameStatesGraphOwned,
        inputs.materialGeometryStatesGraphOwned,
        true,
        inputs.conservativeGeometryScissor
    };
    materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitSharedComputeEmulation(
    const AvboitSharedComputeEmulationRecordInputs& inputs,
    Core::CommandList& commandList,
    const AvboitSharedComputeEmulationRecordTrait& trait
){
    const bool generatePhase = inputs.phase == AvboitSharedComputeEmulationPhase::Generate;
    if(
        !inputs.graphics
        || !inputs.materialSystem
        || !inputs.targets
        || !inputs.timingTicket
        || !inputs.timing
        || !inputs.frameBindings
        || !inputs.plan
        || !trait.timingScope
        || !trait.framebuffer
        || !(inputs.targets->avboit.*(trait.framebuffer))
        || !inputs.plan->captured
        || inputs.drawIndex >= inputs.plan->drawCount
    )
        return false;

    Core::GraphicsRuntime& graphics = *inputs.graphics;
    RendererMaterialSystem& materialSystem = *inputs.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*inputs.timingTicket);
    if(
        !inputs.plan->matches(inputs.drawIndex)
        || !inputs.materialDrawBuffersUploaded
        || !inputs.frameBindings->frameReady(
            inputs.instanceCount,
            inputs.materialTypedByteCount
        )
    )
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItems drawItems{ scratchArena };
    inputs.plan->materialize(inputs.drawIndex, drawItems);
    if(!materialSystem.materialPassDrawResourcesReady(drawItems, *inputs.frameBindings))
        return false;

    if(generatePhase){
        // End the prior raster phase before the marker and compute bind.
        commandList.endRenderPass();
        if(inputs.beginTiming){
            if(inputs.timing->has_value())
                return false;
            inputs.timing->emplace(
                graphics.gpuTiming(),
                *trait.timingScope,
                graphics.getDevice(),
                commandList
            );
            // Close the marker before advancing to the raster consumer.
            if(!Core::FinishSplitGpuTimingMarker(inputs.timing))
                return false;
        }
        else if(!inputs.timing->has_value())
            return false;
    }
    else if(!inputs.timing->has_value())
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(
        (inputs.targets->avboit.*(trait.framebuffer))->getFramebufferInfo().getViewport()
    );
    Core::Framebuffer* const rasterFramebuffer = generatePhase
        ? nullptr
        : (inputs.targets->avboit.*(trait.framebuffer)).get()
    ;
    const MaterialPassDrawContext drawContext{
        commandList,
        *inputs.targets,
        rasterFramebuffer,
        &inputs.targets->avboit,
        viewportState,
        nullptr,
        *inputs.frameBindings,
        trait.pipelinePass,
        false,
        false,
        false,
        inputs.materialFrameStatesGraphOwned,
        inputs.materialGeometryStatesGraphOwned,
        true
    };
    if(generatePhase){
        materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems.computeDrawItems);
    }
    else{
        materialSystem.renderComputeMaterialPassDrawItemsRasterOnly(
            drawContext,
            drawItems.computeDrawItems
        );
        if(inputs.finishTiming){
            inputs.timing->value().finishTiming(commandList);
            inputs.timing->reset();
        }
        // Never bind compute while dynamic rendering is active.
        commandList.endRenderPass();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

