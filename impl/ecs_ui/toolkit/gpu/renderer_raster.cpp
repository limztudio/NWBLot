// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Optional<Core::Rect> ResolveGpuScissorRect(const GpuRasterGeometry& geometry, const DrawCommand& draw)noexcept{
    const auto bounds = TryComputeGpuScissorBounds(geometry, draw);
    if(!bounds)
        return {};
    return Core::Rect(bounds->x, bounds->y, bounds->z, bounds->w);
}

b8 ResolvePaintPushConstants(const GpuFrameData& frame, const DrawCommand& draw, GpuPaintPushConstants& push)noexcept{
    push.material = static_cast<u32>(draw.material);
    push.textureSlot = NWB_UI_INVALID_HEAP_SLOT;
    if(draw.material == PaintMaterial::Skin)
        push.textureSlot = frame.m_skin->m_texture.sampledImageHeapHandle.slot();
    else if(draw.material == PaintMaterial::Glyph){
        if(draw.glyphPageIndex >= frame.m_glyphPages.size())
            return false;
        push.textureSlot = frame.m_glyphPages[draw.glyphPageIndex]->m_sampledImage.slot();
    }
    else if(draw.material == PaintMaterial::SdfGlyph){
        if(draw.sdfPageIndex >= frame.m_sdfPages.size())
            return false;
        const auto& page = frame.m_sdfPages[draw.sdfPageIndex];
        push.textureSlot = page->m_sampledImage.slot();
        push.sdfChannel = draw.sdfChannel;
        push.sdfSpreadPixels = page->m_page->binding().spreadPixels;
        push.sdfDistanceEncoding = page->m_page->binding().distanceEncoding;
    }
    else if(draw.material == PaintMaterial::Image){
        if(draw.textureImageIndex >= frame.m_textureImages.size())
            return false;
        push.textureSlot = frame.m_textureImages[draw.textureImageIndex]->m_texture.sampledImageHeapHandle.slot();
    }
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

