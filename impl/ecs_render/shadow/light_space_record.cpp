// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_shadow.h"

#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/vulkan/backend.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RecordLightSpaceViews(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot){
    if(!snapshot.ready || !snapshot.viewPipeline || !snapshot.drawArguments || snapshot.plan.viewCount == 0u)
        return false;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(snapshot.viewPipeline.get()));
    heap.bindCompute(commandList, *snapshot.viewPipeline);
    commandList.setPushConstants(&snapshot.push, sizeof(snapshot.push));
    commandList.dispatch(DivideUp(snapshot.plan.viewCount, static_cast<u32>(NWB_LIGHT_SPACE_VIEW_GROUP_SIZE)), 1u, 1u);
    return true;
}

bool RecordLightSpaceCull(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot){
    if(!snapshot.ready || !snapshot.cullPipeline || !snapshot.drawArguments || !snapshot.casters || snapshot.plan.viewCount == 0u)
        return false;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(snapshot.cullPipeline.get()));
    heap.bindCompute(commandList, *snapshot.cullPipeline);
    LightSpaceShadowPush push = snapshot.push;
    push.casterDrawOffset = 0u;
    for(usize index = 0u; index < snapshot.casterCount; ++index){
        const auto& caster = snapshot.casters[index];
        const u32 drawCount = LightSpaceShadowDrawCount(&caster, 1u);
        push.instanceIndex = caster.instanceIndex;
        push.meshletDescSlot = caster.meshletDescSlot;
        push.meshletBoundsSlot = caster.meshletBoundsSlot;
        push.meshletCount = caster.meshletCount;
        commandList.setPushConstants(&push, sizeof(push));
        const u32 groups = DivideUp(drawCount, static_cast<u32>(NWB_LIGHT_SPACE_CULL_GROUP_SIZE));
        commandList.dispatch(Min(groups, static_cast<u32>(NWB_LIGHT_SPACE_CULL_MAX_GROUPS_X)), snapshot.plan.viewCount,
            DivideUp(groups, static_cast<u32>(NWB_LIGHT_SPACE_CULL_MAX_GROUPS_X)));
        push.casterDrawOffset += drawCount;
    }
    return true;
}

bool RecordLightSpaceCapture(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    const LightSpaceShadowSnapshot& snapshot,
    const u32 viewIndex,
    const bool transparent){
    static_assert(sizeof(Core::DrawIndexedIndirectArguments) == NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
    if(!snapshot.ready || !snapshot.drawArguments || viewIndex >= snapshot.plan.viewCount || (snapshot.casterCount != 0u && !snapshot.casters))
        return false;
    const auto& framebuffer = transparent ? snapshot.transparentFramebuffers[viewIndex] : snapshot.opaqueFramebuffers[viewIndex];
    const auto& pipeline = transparent ? snapshot.transparentCapture : snapshot.opaqueCapture;
    if(!framebuffer || !pipeline)
        return false;
    for(usize index = 0u; index < snapshot.casterCount; ++index){
        const auto& caster = snapshot.casters[index];
        if((caster.transparent || caster.csg) != transparent || caster.indexCount == 0u
            || ((snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_HW_COMPOSE) != 0u && !caster.csg))
            continue;
        if(!caster.triangleIndexBuffer || !caster.triangleIndexBuffer->getCreationDescription().isIndexBuffer)
            return false;
    }
    Core::RenderPassParameters parameters;
    parameters.depthClearValue = 1.f;
    parameters.depthAttachmentActions.loadAction = transparent ? Core::RenderPassLoadAction::Load : Core::RenderPassLoadAction::Clear;
    commandList.beginRenderPass(*framebuffer, parameters);
    const u32 resolution = snapshot.plan.views[viewIndex].map[0];
    Core::ViewportState viewport;
    viewport.addViewport(Core::Viewport(static_cast<f32>(resolution), static_cast<f32>(resolution)));
    viewport.addScissorRect(Core::Rect(0, static_cast<i32>(resolution), 0, static_cast<i32>(resolution)));
    Core::GraphicsState graphics;
    graphics.setPipeline(pipeline.get()).setFramebuffer(framebuffer.get()).setViewport(viewport).setIndirectParams(snapshot.drawArguments.get());

    LightSpaceShadowPush push = snapshot.push;
    push.viewIndex = viewIndex;
    u32 drawOffset = viewIndex * snapshot.push.captureDrawCount;
    for(usize index = 0u; index < snapshot.casterCount; ++index){
        const LightSpaceShadowCaster& caster = snapshot.casters[index];
        const u32 drawCount = LightSpaceShadowDrawCount(&caster, 1u);
        const u32 argumentOffset = drawOffset * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES;
        drawOffset += drawCount;
        if((caster.transparent || caster.csg) != transparent || caster.indexCount == 0u
            || ((snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_HW_COMPOSE) != 0u && !caster.csg))
            continue;
        graphics.setIndexBuffer(Core::IndexBufferBinding{}.setBuffer(caster.triangleIndexBuffer.get()).setFormat(Core::Format::R32_UINT));
        commandList.setGraphicsState(graphics);
        heap.bindGraphics(commandList, *pipeline);
        push.instanceIndex = caster.instanceIndex;
        commandList.setPushConstants(&push, sizeof(push));
        const u32 captureCount = (snapshot.plan.views[viewIndex].light[3] & NWB_LIGHT_SPACE_FLAG_POINT) != 0u ? drawCount : 1u;
        constexpr u32 s_MaxIndirectDraws = 65535u;
        for(u32 firstDraw = 0u; firstDraw < captureCount;){
            const u32 batchCount = Min(captureCount - firstDraw, s_MaxIndirectDraws);
            commandList.drawIndexedIndirect(argumentOffset + firstDraw * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES, batchCount);
            firstDraw += batchCount;
        }
    }
    commandList.endRenderPass();
    return true;
}

bool RecordLightSpaceShade(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot){
    const auto& pipeline = (snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u
        ? snapshot.csgShadePipeline : snapshot.shadePipeline;
    if(!snapshot.ready || !pipeline || snapshot.plan.totalPixels == 0u)
        return false;
    LightSpaceShadowPush push = snapshot.push;
    push.width = snapshot.plan.totalPixels;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(pipeline.get()));
    heap.bindCompute(commandList, *pipeline);
    commandList.setPushConstants(&push, sizeof(push));
    const u32 groups = DivideUp(push.width, static_cast<u32>(NWB_LIGHT_SPACE_SHADE_GROUP_SIZE));
    const u32 groupsX = Min(groups, static_cast<u32>(NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X));
    const u32 groupsY = DivideUp(groups, static_cast<u32>(NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X));
    commandList.dispatch(groupsX, groupsY, 1u);
    return true;
}

bool RecordLightSpaceResolve(
    Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, Core::GpuTimingRecorder& timing,
    const LightSpaceShadowSnapshot& snapshot,
    Core::Texture& outputTexture, const u32 frameIndex, const u32 sampleCount, const u32 outputSlot, const bool transparent){
    const bool csg = (snapshot.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u;
    const auto& pipeline = csg ? (transparent ? snapshot.csgTransparentResolve : snapshot.csgOpaqueResolve)
        : (transparent ? snapshot.transparentResolve : snapshot.opaqueResolve);
    const auto& fallback = csg ? (transparent ? snapshot.csgTransparentFallback : snapshot.csgOpaqueFallback)
        : (transparent ? snapshot.transparentFallback : snapshot.opaqueFallback);
    if(!snapshot.ready || !pipeline || !fallback || sampleCount == 0u)
        return false;
    LightSpaceShadowPush push = snapshot.push;
    push.frameIndex = frameIndex;
    push.sampleCount = sampleCount;
    push.outputSlot = outputSlot;
    commandList.setComputeState(Core::ComputeState{}.setPipeline(pipeline.get()));
    heap.bindCompute(commandList, *pipeline);
    commandList.setPushConstants(&push, sizeof(push));
    const u32 halfWidth = DivideUp(push.width, push.receiverFactor);
    const u32 halfHeight = DivideUp(push.height, push.receiverFactor);
    const u32 groupsX = DivideUp(halfWidth, static_cast<u32>(NWB_LIGHT_SPACE_GROUP_SIZE));
    const u32 groupsY = DivideUp(halfHeight, static_cast<u32>(NWB_LIGHT_SPACE_GROUP_SIZE));
    {
        const auto& scope = transparent ? RendererGpuTimingScope::s_LightSpaceShadowMapTransparent
            : RendererGpuTimingScope::s_LightSpaceShadowMapOpaque;
        Core::GpuTimingMeasure measure(timing, scope, commandList.getDevice(), commandList);

        commandList.dispatch(groupsX, groupsY, 1u);
    }
    if((push.csgFlags & NWB_CSG_SHADOW_FLAG_HW_COMPOSE) != 0u)
        return true;
    // Alpha is initialized by the map dispatch before the fallback reads it through the same UAV binding.
    commandList.setTextureState(&outputTexture, Core::TextureSubresourceSet{}, Core::ResourceStates::UnorderedAccess, true);
    commandList.commitBarriers();
    commandList.setComputeState(Core::ComputeState{}.setPipeline(fallback.get()));
    heap.bindCompute(commandList, *fallback);
    commandList.setPushConstants(&push, sizeof(push));
    {
        const auto& scope = transparent ? RendererGpuTimingScope::s_LightSpaceShadowFallbackTransparent
            : RendererGpuTimingScope::s_LightSpaceShadowFallbackOpaque;
        Core::GpuTimingMeasure measure(timing, scope, commandList.getDevice(), commandList);

        commandList.dispatch(groupsX, groupsY, 1u);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

