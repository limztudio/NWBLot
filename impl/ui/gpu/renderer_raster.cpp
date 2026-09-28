// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRasterTask::record(
    const Payload& payload,
    Core::CommandList& commands,
    const Core::GpuTaskRecordContext& context){
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !frame->m_prepared || !frame->m_target || !frame->m_resources
        || context.declarations.textureForResource(payload.color) != frame->m_target->m_color.get()
        || context.declarations.textureForResource(payload.skin) != frame->m_skin->m_texture.texture.get()
    )
        return false;
    const Core::TextureDesc& target = frame->m_target->m_color->getDescription();
    const DisplayMetrics& display = frame->m_snapshot.displayMetrics();
    GpuPaintPushConstants push;
    push.scale = { 2.0f * display.pixelScaleX / static_cast<f32>(target.width), -2.0f * display.pixelScaleY / static_cast<f32>(target.height) };
    push.translate = { -1.0f, 1.0f };
    push.samplerSlot = frame->m_resources->m_samplerDescriptor.slot();
    if(!IsFinite(push.scale.x) || !IsFinite(push.scale.y))
        return false;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const f32 minX = Max(0.0f, Floor(draw.clip.x * display.pixelScaleX));
        const f32 minY = Max(0.0f, Floor(draw.clip.y * display.pixelScaleY));
        const f32 maxX = Min(static_cast<f32>(target.width), Ceil((draw.clip.x + draw.clip.width) * display.pixelScaleX));
        const f32 maxY = Min(static_cast<f32>(target.height), Ceil((draw.clip.y + draw.clip.height) * display.pixelScaleY));
        if(maxX <= minX || maxY <= minY)
            continue;
        Core::ViewportState view;
        view.addViewport(viewport).addScissorRect(Core::Rect(
            static_cast<i32>(minX), static_cast<i32>(maxX), static_cast<i32>(minY), static_cast<i32>(maxY)
        ));
        Core::GraphicsState state;
        state
            .setPipeline(frame->m_resources->m_pipeline.get())
            .setFramebuffer(frame->m_target->m_framebuffer.get())
            .setViewport(view)
            .addVertexBuffer(Core::VertexBufferBinding().setBuffer(frame->m_vertices.get()).setSlot(NWB_UI_VERTEX_BUFFER_INDEX))
            .setIndexBuffer(Core::IndexBufferBinding().setBuffer(frame->m_indices.get()).setFormat(Core::Format::R32_UINT))
        ;
        commands.setGraphicsState(state);
        heap.bindGraphics(commands, *frame->m_resources->m_pipeline);
        push.material = static_cast<u32>(draw.material);
        push.textureSlot = draw.material == PaintMaterial::Skin ? frame->m_skin->m_texture.sampledImageHeapHandle.slot() : NWB_UI_INVALID_HEAP_SLOT;
        commands.setPushConstants(&push, sizeof(push));
        Core::DrawArguments arguments;
        arguments.setVertexCount(draw.indexCount).setStartIndexLocation(draw.firstIndex);
        commands.drawIndexed(arguments);
    }
    commands.endRenderPass();
    return true;
}

void GpuRasterTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    payload.frame->m_raster = token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuOutputTask::record(
    const Payload& payload,
    Core::CommandList& commands,
    const Core::GpuTaskRecordContext& context){
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !payload.pipeline || !payload.acquired.valid()
        || context.declarations.textureForResource(payload.backBuffer) != payload.acquired.backBuffer.texture.get()
        || context.declarations.textureForResource(payload.color) != frame->m_target->m_color.get()
    )
        return false;
    const Core::TextureDesc& target = payload.acquired.backBuffer.texture->getDescription();
    Core::ViewportState viewport;
    viewport.addViewport(Core::Viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f));
    Core::GraphicsState state;
    state.setPipeline(payload.pipeline.get()).setFramebuffer(payload.acquired.framebuffer.get()).setViewport(viewport);
    commands.setGraphicsState(state);
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    heap.bindGraphics(commands, *payload.pipeline);
    GpuOutputPushConstants push;
    push.textureSlot = frame->m_target->m_sampledImage.slot();
    push.samplerSlot = frame->m_resources->m_samplerDescriptor.slot();
    push.presentationMode = payload.presentationMode;
    commands.setPushConstants(&push, sizeof(push));
    commands.draw(Core::DrawArguments().setVertexCount(3u));
    commands.endRenderPass();
    return true;
}

void GpuOutputTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    payload.frame->m_finalConsumer = token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

