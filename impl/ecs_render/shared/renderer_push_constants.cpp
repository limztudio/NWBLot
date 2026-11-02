// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shared/renderer_push_constants_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderDrivenPushConstants BuildShaderDrivenPushConstants(
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const MeshFrameHeapSlots& frameHeapSlots,
    const u32 dispatchFlags
)noexcept{
    const b8 hasViewport = !viewportState.viewports.empty();
    const Core::Viewport viewport = hasViewport ? viewportState.viewports[0] : Core::Viewport{};
    const Core::Rect scissorRect = hasViewport ? (!viewportState.scissorRects.empty() ? viewportState.scissorRects[0] : Core::Rect::FromViewport(viewport)) : Core::Rect{};
    return ResolveShaderDrivenPushConstants(
        meshletCount,
        instanceIndex,
        materialConstantByteOffset,
        hasViewport,
        Float4(viewport.minX, viewport.minY, viewport.maxX, viewport.maxY),
        Float4(static_cast<f32>(scissorRect.minX), static_cast<f32>(scissorRect.minY), static_cast<f32>(scissorRect.maxX), static_cast<f32>(scissorRect.maxY)),
        frameHeapSlots,
        dispatchFlags
    );
}

void SetShaderDrivenPushConstants(
    Core::CommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const MeshFrameHeapSlots& frameHeapSlots,
    const u32 dispatchFlags
){
    const ShaderDrivenPushConstants pushConstants = BuildShaderDrivenPushConstants(
        meshletCount,
        instanceIndex,
        materialConstantByteOffset,
        viewportState,
        frameHeapSlots,
        dispatchFlags
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}

void SetTransparentDrawPushConstants(
    Core::CommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const AvboitFrameTargets& targets,
    const MeshFrameHeapSlots& frameHeapSlots,
    const u32 dispatchFlags,
    const b8 hdr10OutputActive,
    const u32 csgContextHeapSlot,
    const b8 refractionCapture
){
    NWB_ASSERT(targets.deferredSlotsBufferDescriptor.valid());
    NWB_ASSERT(targets.deferredSlotsBufferDescriptor.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer);
    const TransparentDrawPushConstants pushConstants = ResolveTransparentDrawPushConstants(
        BuildShaderDrivenPushConstants(
            meshletCount,
            instanceIndex,
            materialConstantByteOffset,
            viewportState,
            frameHeapSlots,
            dispatchFlags
        ),
        ResolveRendererAvboitPushConstants(
            targets.fullWidth,
            targets.fullHeight,
            targets.lowWidth,
            targets.lowHeight,
            targets.virtualSliceCount,
            targets.physicalSliceCount,
            hdr10OutputActive,
            targets.refractionFramebuffer != nullptr,
            targets.deferredSlotsBufferDescriptor.slot()
        ),
        csgContextHeapSlot,
        refractionCapture
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

