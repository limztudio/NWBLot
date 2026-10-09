// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"
#include "renderer_command_ir.h"

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>
#include <global/math/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRasterTask::Record(
    const Payload& payload,
    Core::CommandList& commands,
    const Core::GpuTaskRecordContext& context
){
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
        if(
            context.declarations.textureForResource(payload.textureImages[index])
            != frame->m_textureImages[index]->m_texture.texture.get()
        )
            return false;
    }
    if(frame->m_recordingMode == GpuCommandRecordingMode::CommandIrReplay)
        return RecordRasterCommandIr(payload, commands, context);
    const Core::TextureDesc& target = frame->m_target->m_color->getDescription();
    const DisplayMetrics& display = frame->m_snapshot.displayMetrics();
    GpuPaintPushConstants push;
    const SIMDVector pixelScale = VectorSet(display.pixelScaleX, display.pixelScaleY, display.pixelScaleX, display.pixelScaleY);
    const SIMDVector projection = VectorMultiply(VectorSet(2.0f, -2.0f, 2.0f, -2.0f), pixelScale);
    const SIMDVector targetSize = VectorSet(static_cast<f32>(target.width), static_cast<f32>(target.height),
        static_cast<f32>(target.width), static_cast<f32>(target.height));
    const SIMDVector projectionScale = VectorDivide(projection, targetSize);
    push.scale = { VectorGetX(projectionScale), VectorGetY(projectionScale) };
    push.translate = { -1.0f, 1.0f };
    push.samplerSlot = frame->m_resources->m_samplerDescriptor.slot();
    if(!IsFinite(push.scale.x) || !IsFinite(push.scale.y))
        return false;
    Core::GpuDescriptorHeap& heap = frame->m_resources->m_graphics.getDevice().getDescriptorHeap();
    const Core::Viewport viewport(0.0f, static_cast<f32>(target.width), 0.0f, static_cast<f32>(target.height), 0.0f, 1.0f);
    Core::Rect boundScissor;
    bool heapBound = false;
    for(const DrawCommand& draw : frame->m_snapshot.commands()){
        const SIMDVector clip = VectorSet(draw.clip.x, draw.clip.y, draw.clip.x, draw.clip.y);
        const SIMDVector extent = VectorSet(0.0f, 0.0f, draw.clip.width, draw.clip.height);
        const SIMDVector ends = VectorAdd(clip, extent);
        const SIMDVector coordinates = VectorPermute<0, 1, 6, 7>(clip, ends);
        const SIMDVector physicalClip = VectorMultiply(coordinates, pixelScale);
        const SIMDVector first = VectorFloor(VectorSwizzle<0, 1, 0, 1>(physicalClip));
        const SIMDVector last = VectorCeiling(VectorSwizzle<2, 3, 2, 3>(physicalClip));
        const SIMDVector minimum = VectorSelect(first, VectorZero(), VectorGreater(VectorZero(), first));
        const SIMDVector maximum = VectorSelect(last, targetSize, VectorLess(targetSize, last));
        const f32 minX = VectorGetX(minimum);
        const f32 minY = VectorGetY(minimum);
        const f32 maxX = VectorGetX(maximum);
        const f32 maxY = VectorGetY(maximum);
        if(maxX <= minX || maxY <= minY)
            continue;
        const Core::Rect scissor(
            static_cast<i32>(minX), static_cast<i32>(maxX), static_cast<i32>(minY), static_cast<i32>(maxY)
        );
        if(!heapBound || scissor != boundScissor){
            Core::ViewportState view;
            view.addViewport(viewport).addScissorRect(scissor);
            Core::GraphicsState state;
            state
                .setPipeline(frame->m_resources->m_pipeline.get())
                .setFramebuffer(frame->m_target->m_framebuffer.get())
                .setViewport(view)
                .addVertexBuffer(
                    Core::VertexBufferBinding().setBuffer(frame->m_vertices.get()).setSlot(NWB_UI_VERTEX_BUFFER_INDEX)
                )
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

void GpuRasterTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
    payload.frame->m_raster = token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuOutputTask::Record(
    const Payload& payload,
    Core::CommandList& commands,
    const Core::GpuTaskRecordContext& context
){
    const GpuFrame& frame = payload.frame;
    if(
        !frame || !payload.pipeline || !payload.acquired.valid()
        || context.declarations.textureForResource(payload.backBuffer) != payload.acquired.backBuffer.texture.get()
        || context.declarations.textureForResource(payload.color) != frame->m_target->m_color.get()
    )
        return false;
    if(frame->m_recordingMode == GpuCommandRecordingMode::CommandIrReplay)
        return RecordOutputCommandIr(payload, commands, context);
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

void GpuOutputTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
    payload.frame->m_finalConsumer = token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

