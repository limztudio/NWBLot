// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"
#include "renderer_command_ir.h"

#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuPaintPushConstants> GpuRasterGeometry::initializePushConstants(const u32 samplerSlot)const noexcept{
    const SIMDVectorSingle projection = VectorMultiply(VectorSet(s_GpuClipSpaceExtent, -s_GpuClipSpaceExtent, s_GpuClipSpaceExtent, -s_GpuClipSpaceExtent), pixelScale);
    const SIMDVectorSingle projectionScale = VectorDivide(projection, targetSize);
    GpuPaintPushConstants push;
    push.scale = { VectorGetX(projectionScale), VectorGetY(projectionScale) };
    push.translate = { -1.0f, 1.0f };
    push.samplerSlot = samplerSlot;
    if(!IsFinite(push.scale.x) || !IsFinite(push.scale.y))
        return MakeUnexpected(Failure{});
    return push;
}

Optional<Core::Rect> GpuRasterGeometry::scissor(const DrawCommand& draw)const noexcept{
    const SIMDVectorSingle clip = VectorSet(draw.clip.x, draw.clip.y, draw.clip.x, draw.clip.y);
    const SIMDVectorSingle extent = VectorSet(0.0f, 0.0f, draw.clip.width, draw.clip.height);
    const SIMDVectorSingle ends = VectorAdd(clip, extent);
    const SIMDVectorSingle coordinates = VectorPermute<0, 1, 6, 7>(clip, ends);
    const SIMDVectorSingle physicalClip = VectorMultiply(coordinates, pixelScale);
    const SIMDVectorSingle first = VectorFloor(VectorSwizzle<0, 1, 0, 1>(physicalClip));
    const SIMDVectorSingle last = VectorCeiling(VectorSwizzle<2, 3, 2, 3>(physicalClip));
    const SIMDVectorSingle minimum = VectorSelect(first, VectorZero(), VectorGreater(VectorZero(), first));
    const SIMDVectorSingle maximum = VectorSelect(last, targetSize, VectorLess(targetSize, last));
    const f32 minX = VectorGetX(minimum);
    const f32 minY = VectorGetY(minimum);
    const f32 maxX = VectorGetX(maximum);
    const f32 maxY = VectorGetY(maximum);
    if(maxX <= minX || maxY <= minY)
        return {};
    return Core::Rect(static_cast<i32>(minX), static_cast<i32>(maxX), static_cast<i32>(minY), static_cast<i32>(maxY));
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
    const GpuRasterGeometry geometry(frame->m_snapshot.displayMetrics(), target);
    const auto initialPush = geometry.initializePushConstants(frame->m_resources->m_samplerDescriptor.slot());
    if(!initialPush)
        return false;
    GpuPaintPushConstants push = *initialPush;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    Core::Rect boundScissor;
    b8 heapBound = false;
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const auto resolvedScissor = geometry.scissor(draw);
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
    GpuOutputPushConstants push;
    push.textureSlot = frame->m_target->m_sampledImage.slot();
    push.samplerSlot = frame->m_resources->m_samplerDescriptor.slot();
    push.presentationMode = payload.presentationMode;
    commands.setPushConstants(&push, sizeof(push));
    commands.draw(Core::DrawArguments().setVertexCount(s_GpuFullscreenTriangleVertexCount));
    commands.endRenderPass();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

