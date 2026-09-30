// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_command_ir.h"

#include <core/common/log.h>
#include <core/task/gpu/capture/command_ir_raster.h>
#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_command_ir{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReplayCaptured(
    const GpuFrame& frame,
    const Core::GpuTaskRecordContext& context,
    Core::CommandList& commands,
    const Core::GpuCommandIrCapture& capture
){
    if(capture.recordCount() == 0u)
        return !commands.commandRecordingFailed();
    Core::GpuCommandIrOwnedStream stream(frame->m_arena);
    if(!capture.exportOwned(stream))
        return false;
    const Core::GpuCommandIrReplayResult replay = Core::ReplayGpuCommandIrPacket(
        stream, context.declarations, context.compiledPlan, context.packet, commands
    );
    return replay.valid() && !commands.commandRecordingFailed();
}

[[nodiscard]] static bool CaptureRasterState(
    const GpuRasterTask::Payload& payload,
    const Core::GpuTaskRecordContext& context,
    Core::GpuCommandIrCapture& capture,
    Core::GpuDescriptorHeap& heap,
    const Core::GpuCommandIrOwnerAnchor& descriptorOwner,
    const Core::Viewport& viewport,
    const Core::Rect& scissor
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
    return capture.captureBindGraphicsHeap(
        context.task, context.packet, context.queue, payload.pipeline,
        frame->m_resources->m_pipeline, heap, frame->m_arena, descriptorOwner
    );
}

[[nodiscard]] static bool CaptureOutputState(
    const GpuOutputTask::Payload& payload,
    const Core::GpuTaskRecordContext& context,
    Core::GpuCommandIrCapture& capture,
    Core::GpuDescriptorHeap& heap,
    const Core::GpuCommandIrOwnerAnchor& descriptorOwner,
    const Core::Viewport& viewport
){
    Core::GpuCommandIrRasterStateDesc state;
    state.pipeline = payload.graphPipeline;
    state.pipelineOwner = payload.pipeline;
    state.colorAttachment = payload.backBuffer;
    state.framebufferOwner = payload.acquired.framebuffer;
    state.viewport = viewport;
    if(!capture.captureSetGraphicsState(context.task, context.packet, context.queue, state))
        return false;
    return capture.captureBindGraphicsHeap(
        context.task, context.packet, context.queue, payload.graphPipeline,
        payload.pipeline, heap, payload.frame->m_arena, descriptorOwner
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RecordRasterCommandIr(
    const GpuRasterTask::Payload& payload,
    Core::CommandList& commands,
    const Core::GpuTaskRecordContext& context
){
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
    const Core::GpuCommandIrOwnerAnchor descriptorOwner = Core::MakeGpuCommandIrOwnerAnchor(frame->m_arena, GpuFrame(frame));
    const Core::TextureDesc& target = frame->m_target->m_color->getDescription();
    const DisplayMetrics& display = frame->m_snapshot.displayMetrics();
    GpuPaintPushConstants push;
    push.scale = { 2.0f * display.pixelScaleX / static_cast<f32>(target.width), -2.0f * display.pixelScaleY / static_cast<f32>(target.height) };
    push.translate = { -1.0f, 1.0f };
    push.samplerSlot = frame->m_resources->m_samplerDescriptor.slot();
    if(!IsFinite(push.scale.x) || !IsFinite(push.scale.y))
        return false;
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    bool hasDraw = false;
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const f32 minX = Max(0.0f, Floor(draw.clip.x * display.pixelScaleX));
        const f32 minY = Max(0.0f, Floor(draw.clip.y * display.pixelScaleY));
        const f32 maxX = Min(static_cast<f32>(target.width), Ceil((draw.clip.x + draw.clip.width) * display.pixelScaleX));
        const f32 maxY = Min(static_cast<f32>(target.height), Ceil((draw.clip.y + draw.clip.height) * display.pixelScaleY));
        if(maxX <= minX || maxY <= minY || draw.indexCount == 0u)
            continue;
        const Core::Rect scissor(
            static_cast<i32>(minX), static_cast<i32>(maxX), static_cast<i32>(minY), static_cast<i32>(maxY)
        );
        if(!__hidden_ui_gpu_command_ir::CaptureRasterState(payload, context, capture, heap, descriptorOwner, viewport, scissor))
            return false;
        push.material = static_cast<u32>(draw.material);
        push.textureSlot = NWB_UI_INVALID_HEAP_SLOT;
        if(draw.material == PaintMaterial::Skin)
            push.textureSlot = frame->m_skin->m_texture.sampledImageHeapHandle.slot();
        else if(draw.material == PaintMaterial::Glyph){
            if(draw.glyphPageIndex >= frame->m_glyphPages.size())
                return false;
            push.textureSlot = frame->m_glyphPages[draw.glyphPageIndex]->m_sampledImage.slot();
        }
        else if(draw.material == PaintMaterial::SdfGlyph){
            if(draw.sdfPageIndex >= frame->m_sdfPages.size())
                return false;
            const auto& page = frame->m_sdfPages[draw.sdfPageIndex];
            push.textureSlot = page->m_sampledImage.slot();
            push.sdfChannel = draw.sdfChannel;
            push.sdfSpreadPixels = page->m_page->binding().spreadPixels;
            push.sdfDistanceEncoding = page->m_page->binding().distanceEncoding;
        }
        else if(draw.material == PaintMaterial::Image){
            if(draw.textureImageIndex >= frame->m_textureImages.size())
                return false;
            push.textureSlot = frame->m_textureImages[draw.textureImageIndex]->m_texture.sampledImageHeapHandle.slot();
        }
        if(!capture.capturePushConstants(
            context.task, context.packet, context.queue,
            BinaryByteView{ reinterpret_cast<const u8*>(&push), sizeof(push) }
        ))
            return false;
        Core::DrawArguments arguments;
        arguments.setVertexCount(draw.indexCount).setStartIndexLocation(draw.firstIndex);
        if(!capture.captureDraw(context.task, context.packet, context.queue, arguments, true))
            return false;
        hasDraw = true;
    }
    if(hasDraw && !capture.captureEndRenderPass(context.task, context.packet, context.queue))
        return false;
    if(!__hidden_ui_gpu_command_ir::ReplayCaptured(frame, context, commands, capture))
        return false;
    if(hasDraw)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiGpuRenderer: raster command IR replay active"));
    return true;
}

bool RecordOutputCommandIr(
    const GpuOutputTask::Payload& payload,
    Core::CommandList& commands,
    const Core::GpuTaskRecordContext& context
){
    if(context.commandIrCapture)
        return false;
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !frame->m_target || !frame->m_resources || !payload.graphPipeline.valid()
        || context.declarations.graphicsPipelineFor(payload.graphPipeline) != payload.pipeline.get()
    )
        return false;
    Core::GpuCommandIrCapture capture(frame->m_arena);
    if(!capture.beginRecordingAttempt(context.recordingAttemptGeneration))
        return false;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const Core::GpuCommandIrOwnerAnchor descriptorOwner = Core::MakeGpuCommandIrOwnerAnchor(frame->m_arena, GpuFrame(frame));
    const Core::TextureDesc& target = payload.acquired.backBuffer.texture->getDescription();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    if(!__hidden_ui_gpu_command_ir::CaptureOutputState(payload, context, capture, heap, descriptorOwner, viewport))
        return false;
    GpuOutputPushConstants push;
    push.textureSlot = frame->m_target->m_sampledImage.slot();
    push.samplerSlot = frame->m_resources->m_samplerDescriptor.slot();
    push.presentationMode = payload.presentationMode;
    if(!capture.capturePushConstants(
        context.task, context.packet, context.queue,
        BinaryByteView{ reinterpret_cast<const u8*>(&push), sizeof(push) }
    ))
        return false;
    if(
        !capture.captureDraw(context.task, context.packet, context.queue, Core::DrawArguments().setVertexCount(3u), false)
        || !capture.captureEndRenderPass(context.task, context.packet, context.queue)
    )
        return false;
    return __hidden_ui_gpu_command_ir::ReplayCaptured(frame, context, commands, capture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

