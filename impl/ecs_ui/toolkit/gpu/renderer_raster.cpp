// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <core/common/log.h>
#include <core/task/gpu/capture/command_ir_raster.h>
#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_command_ir{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ReplayCaptured(const GpuFrame& frame, const Core::GpuTaskRecordContext& context, const Core::GpuCommandIrCapture& capture, Core::CommandList& commands){
    if(capture.recordCount() == 0u)
        return !commands.commandRecordingFailed();
    const auto stream = capture.exportOwned(frame->m_arena);
    if(!stream)
        return false;
    const Core::GpuCommandIrReplayResult replay = Core::ReplayGpuCommandIrPacket(*stream, context.declarations, context.compiledPlan, context.packet, commands);
    return replay.valid() && !commands.commandRecordingFailed();
}

[[nodiscard]] static b8 CaptureRasterState(
    const GpuRasterTask::Payload& payload,
    const Core::GpuTaskRecordContext& context,
    const Core::GpuCommandIrOwnerAnchor& descriptorOwner,
    const Core::Viewport& viewport,
    const Core::Rect& scissor,
    Core::GpuDescriptorHeap& heap,
    Core::GpuCommandIrCapture& capture
){
    const GpuFrame& frame = payload.frame;
    Core::GpuCommandIrRasterStateDesc state;
    state.pipeline = payload.pipeline;
    state.pipelineOwner = frame->m_resources->m_pipeline;
    state.colorAttachment = payload.color;
    state.framebufferOwner = frame->m_target->m_framebuffer;
    state.viewport = viewport;
    state.scissor = scissor;
    state.hasScissor = true;
    state.vertexBuffers.push_back({ payload.vertices, frame->m_vertices, NWB_UI_VERTEX_BUFFER_INDEX, 0u });
    state.indexResource = payload.indices;
    state.indexBuffer = frame->m_indices;
    state.indexFormat = Core::Format::R32_UINT;
    if(!capture.captureSetGraphicsState(context.task, context.packet, context.queue, state))
        return false;
    return capture.captureBindGraphicsHeap(heap, frame->m_arena, context.task, context.packet, context.queue, payload.pipeline, frame->m_resources->m_pipeline, descriptorOwner);
}

[[nodiscard]] static b8 CaptureOutputState(
    const GpuOutputTask::Payload& payload,
    const Core::GpuTaskRecordContext& context,
    const Core::GpuCommandIrOwnerAnchor& descriptorOwner,
    const Core::Viewport& viewport,
    Core::GpuDescriptorHeap& heap,
    Core::GpuCommandIrCapture& capture
){
    Core::GpuCommandIrRasterStateDesc state;
    state.pipeline = payload.graphPipeline;
    state.pipelineOwner = payload.pipeline;
    state.colorAttachment = payload.backBuffer;
    state.framebufferOwner = payload.acquired.framebuffer;
    state.viewport = viewport;
    if(!capture.captureSetGraphicsState(context.task, context.packet, context.queue, state))
        return false;
    return capture.captureBindGraphicsHeap(heap, payload.frame->m_arena, context.task, context.packet, context.queue, payload.graphPipeline, payload.pipeline, descriptorOwner);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RecordRasterCommandIr(const GpuRasterTask::Payload& payload, const Core::GpuTaskRecordContext& context, Core::CommandList& commands){
    // A task-local stream must not silently disappear from a caller's separate packet capture.
    if(context.commandIrCapture)
        return false;
    const GpuFrame& frame = payload.frame;
    if(!frame || !frame->m_target || !frame->m_resources || !payload.pipeline.valid())
        return false;
    if(context.declarations.graphicsPipelineFor(payload.pipeline) != frame->m_resources->m_pipeline.get())
        return false;
    if(frame->m_snapshot.vertices().empty())
        return frame->m_snapshot.commands().empty() && !commands.commandRecordingFailed();
    if(
        !frame->m_vertices || !frame->m_indices || !payload.vertices.valid() || !payload.indices.valid()
        || context.declarations.bufferForResource(payload.vertices) != frame->m_vertices.get()
        || context.declarations.bufferForResource(payload.indices) != frame->m_indices.get()
    )
        return false;
    Core::GpuCommandIrCapture capture(frame->m_arena);
    if(!capture.beginRecordingAttempt(context.recordingAttemptGeneration))
        return false;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const auto descriptorOwner = Core::MakeGpuCommandIrOwnerAnchor(frame->m_arena, GpuFrame(frame));
    if(!descriptorOwner)
        return false;
    const Core::TextureDesc& target = frame->m_target->m_color->getDescription();
    const GpuRasterGeometry geometry(frame->m_snapshot.displayMetrics(), target.width, target.height);
    const auto initialPush = TryInitializeGpuPaintPushConstants(geometry, frame->m_resources->m_samplerDescriptor.slot());
    if(!initialPush)
        return false;
    GpuPaintPushConstants push = *initialPush;
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    Core::Rect boundScissor;
    b8 hasRasterState = false;
    b8 hasDraw = false;
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const auto resolvedScissor = ResolveGpuScissorRect(geometry, draw);
        if(!resolvedScissor || draw.indexCount == 0u)
            continue;
        const Core::Rect& scissor = *resolvedScissor;
        if(!hasRasterState || scissor != boundScissor){
            if(!__hidden_ui_gpu_command_ir::CaptureRasterState(payload, context, *descriptorOwner, viewport, scissor, heap, capture))
                return false;
            boundScissor = scissor;
            hasRasterState = true;
        }
        if(!ResolvePaintPushConstants(*frame, draw, push))
            return false;
        if(!capture.capturePushConstants(context.task, context.packet, context.queue, BinaryByteView{ reinterpret_cast<const u8*>(&push), sizeof(push) }))
            return false;
        Core::DrawArguments arguments;
        arguments.setVertexCount(draw.indexCount).setStartIndexLocation(draw.firstIndex);
        if(!capture.captureDraw(context.task, context.packet, context.queue, arguments, true))
            return false;
        hasDraw = true;
    }
    if(hasDraw && !capture.captureEndRenderPass(context.task, context.packet, context.queue))
        return false;
    if(!__hidden_ui_gpu_command_ir::ReplayCaptured(frame, context, capture, commands))
        return false;
    if(hasDraw)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiGpuRenderer: raster command IR replay active"));
    return true;
}

b8 RecordOutputCommandIr(const GpuOutputTask::Payload& payload, const Core::GpuTaskRecordContext& context, Core::CommandList& commands){
    if(context.commandIrCapture)
        return false;
    const GpuFrame& frame = payload.frame;
    if(!frame || !frame->m_target || !frame->m_resources || !payload.graphPipeline.valid() || context.declarations.graphicsPipelineFor(payload.graphPipeline) != payload.pipeline.get())
        return false;
    Core::GpuCommandIrCapture capture(frame->m_arena);
    if(!capture.beginRecordingAttempt(context.recordingAttemptGeneration))
        return false;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const auto descriptorOwner = Core::MakeGpuCommandIrOwnerAnchor(frame->m_arena, GpuFrame(frame));
    if(!descriptorOwner)
        return false;
    const Core::TextureDesc& target = payload.acquired.backBuffer.texture->getDescription();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    if(!__hidden_ui_gpu_command_ir::CaptureOutputState(payload, context, *descriptorOwner, viewport, heap, capture))
        return false;
    const GpuOutputPushConstants push = MakeGpuOutputPushConstants(frame->m_target->m_sampledImage.slot(), frame->m_resources->m_samplerDescriptor.slot(), payload.presentationMode);
    if(!capture.capturePushConstants(context.task, context.packet, context.queue, BinaryByteView{ reinterpret_cast<const u8*>(&push), sizeof(push) }))
        return false;
    if(!capture.captureDraw(context.task, context.packet, context.queue, Core::DrawArguments().setVertexCount(s_GpuFullscreenTriangleVertexCount), false) || !capture.captureEndRenderPass(context.task, context.packet, context.queue))
        return false;
    return __hidden_ui_gpu_command_ir::ReplayCaptured(frame, context, capture, commands);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Optional<Core::Rect> ResolveGpuScissorRect(const GpuRasterGeometry& geometry, const DrawCommand& draw)noexcept{
    const auto bounds = TryComputeGpuScissorBounds(geometry, draw);
    if(!bounds)
        return {};
    return Core::Rect(bounds->x, bounds->y, bounds->z, bounds->w);
}

b8 ResolvePaintPushConstants(const GpuFrameData& frame, const DrawCommand& draw, GpuPaintPushConstants& push)noexcept{
    u32 textureSlot = NWB_UI_INVALID_HEAP_SLOT;
    u32 sdfSpreadPixels = 0u;
    u32 sdfDistanceEncoding = 0u;
    if(draw.material == PaintMaterial::Skin)
        textureSlot = frame.m_skin->m_texture.sampledImageHeapHandle.slot();
    else if(draw.material == PaintMaterial::Glyph){
        if(draw.glyphPageIndex >= frame.m_glyphPages.size())
            return false;
        textureSlot = frame.m_glyphPages[draw.glyphPageIndex]->m_sampledImage.slot();
    }
    else if(draw.material == PaintMaterial::SdfGlyph){
        if(draw.sdfPageIndex >= frame.m_sdfPages.size())
            return false;
        const auto& page = frame.m_sdfPages[draw.sdfPageIndex];
        textureSlot = page->m_sampledImage.slot();
        sdfSpreadPixels = page->m_page->binding().spreadPixels;
        sdfDistanceEncoding = page->m_page->binding().distanceEncoding;
    }
    else if(draw.material == PaintMaterial::Image){
        if(draw.textureImageIndex >= frame.m_textureImages.size())
            return false;
        textureSlot = frame.m_textureImages[draw.textureImageIndex]->m_texture.sampledImageHeapHandle.slot();
    }
    AssignGpuPaintMaterialPushConstants(draw.material, textureSlot, draw.sdfChannel, sdfSpreadPixels, sdfDistanceEncoding, push);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRasterTask::Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !frame->m_prepared || !frame->m_target || !frame->m_resources
        || context.declarations.textureForResource(payload.color) != frame->m_target->m_color.get()
        || context.declarations.textureForResource(payload.skin) != frame->m_skin->m_texture.texture.get()
    )
        return false;
    if(payload.glyphPages.size() != frame->m_glyphPages.size())
        return false;
    for(usize index = 0u; index < payload.glyphPages.size(); ++index){
        if(context.declarations.textureForResource(payload.glyphPages[index]) != frame->m_glyphPages[index]->m_texture.get())
            return false;
    }
    if(payload.sdfPages.size() != frame->m_sdfPages.size())
        return false;
    for(usize index = 0u; index < payload.sdfPages.size(); ++index){
        if(context.declarations.textureForResource(payload.sdfPages[index]) != frame->m_sdfPages[index]->m_texture.get())
            return false;
    }
    if(payload.textureImages.size() != frame->m_textureImages.size())
        return false;
    for(usize index = 0u; index < payload.textureImages.size(); ++index){
        if(context.declarations.textureForResource(payload.textureImages[index]) != frame->m_textureImages[index]->m_texture.texture.get())
            return false;
    }
    if(frame->m_recordingMode == GpuCommandRecordingMode::CommandIrReplay)
        return RecordRasterCommandIr(payload, context, commands);
    const Core::TextureDesc& target = frame->m_target->m_color->getDescription();
    const GpuRasterGeometry geometry(frame->m_snapshot.displayMetrics(), target.width, target.height);
    const auto initialPush = TryInitializeGpuPaintPushConstants(geometry, frame->m_resources->m_samplerDescriptor.slot());
    if(!initialPush)
        return false;
    GpuPaintPushConstants push = *initialPush;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    Core::Rect boundScissor;
    b8 heapBound = false;
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const auto resolvedScissor = ResolveGpuScissorRect(geometry, draw);
        if(!resolvedScissor)
            continue;
        const Core::Rect& scissor = *resolvedScissor;
        if(!heapBound || scissor != boundScissor){
            Core::ViewportState view;
            view.addViewport(viewport).addScissorRect(scissor);
            Core::GraphicsState state;
            state
                .setPipeline(frame->m_resources->m_pipeline.get())
                .setFramebuffer(frame->m_target->m_framebuffer.get())
                .setViewport(view)
                .addVertexBuffer(Core::VertexBufferBinding().setBuffer(frame->m_vertices.get()).setSlot(NWB_UI_VERTEX_BUFFER_INDEX))
                .setIndexBuffer(Core::IndexBufferBinding().setBuffer(frame->m_indices.get()).setFormat(Core::Format::R32_UINT))
            ;
            commands.setGraphicsState(state);
            if(!heapBound){
                heap.bindGraphics(commands, *frame->m_resources->m_pipeline);
                heapBound = true;
            }
            boundScissor = scissor;
        }
        if(!ResolvePaintPushConstants(*frame, draw, push))
            return false;
        commands.setPushConstants(&push, sizeof(push));
        Core::DrawArguments arguments;
        arguments.setVertexCount(draw.indexCount).setStartIndexLocation(draw.firstIndex);
        commands.drawIndexed(arguments);
    }
    commands.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuOutputTask::Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context){
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !payload.pipeline || !payload.acquired.valid()
        || context.declarations.textureForResource(payload.backBuffer) != payload.acquired.backBuffer.texture.get()
        || context.declarations.textureForResource(payload.color) != frame->m_target->m_color.get()
    )
        return false;
    if(frame->m_recordingMode == GpuCommandRecordingMode::CommandIrReplay)
        return RecordOutputCommandIr(payload, context, commands);
    const Core::TextureDesc& target = payload.acquired.backBuffer.texture->getDescription();
    Core::ViewportState viewport;
    viewport.addViewport(Core::Viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f));
    Core::GraphicsState state;
    state.setPipeline(payload.pipeline.get()).setFramebuffer(payload.acquired.framebuffer.get()).setViewport(viewport);
    commands.setGraphicsState(state);
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    heap.bindGraphics(commands, *payload.pipeline);
    const GpuOutputPushConstants push = MakeGpuOutputPushConstants(frame->m_target->m_sampledImage.slot(), frame->m_resources->m_samplerDescriptor.slot(), payload.presentationMode);
    commands.setPushConstants(&push, sizeof(push));
    commands.draw(Core::DrawArguments().setVertexCount(s_GpuFullscreenTriangleVertexCount));
    commands.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

