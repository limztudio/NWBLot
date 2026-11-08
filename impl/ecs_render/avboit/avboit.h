// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/avboit/avboit_frame_targets.h>
#include <impl/ecs_render/avboit/types.adhd.h>
#include <impl/assets/graphics/avboit/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(RendererAvboitPushConstants) == NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE, "RendererAvboitPushConstants layout must stay stable");
static_assert(offsetof(RendererAvboitPushConstants, frame) == sizeof(u32) * NWB_AVBOIT_PUSH_FRAME_WORD_OFFSET, "RendererAvboit frame push offset drifted");
static_assert(offsetof(RendererAvboitPushConstants, volume) == sizeof(u32) * NWB_AVBOIT_PUSH_VOLUME_WORD_OFFSET, "RendererAvboit volume push offset drifted");
static_assert(offsetof(RendererAvboitPushConstants, params) == sizeof(u32) * NWB_AVBOIT_PUSH_PARAMS_WORD_OFFSET, "RendererAvboit params push offset drifted");
static_assert(offsetof(RendererAvboitPushConstants, heapSlots) == sizeof(u32) * NWB_AVBOIT_PUSH_HEAP_SLOTS_WORD_OFFSET, "RendererAvboit heap-slot push offset drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



[[nodiscard]] NWB_INLINE RendererAvboitPushConstants BuildRendererAvboitPushConstants(const AvboitFrameTargets& targets, const b8 hdr10OutputActive)noexcept{
    NWB_ASSERT(targets.deferredSlotsBufferDescriptor.valid());
    NWB_ASSERT(targets.deferredSlotsBufferDescriptor.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer);
    return ResolveRendererAvboitPushConstants(
        targets.fullWidth,
        targets.fullHeight,
        targets.lowWidth,
        targets.lowHeight,
        targets.virtualSliceCount,
        targets.physicalSliceCount,
        hdr10OutputActive,
        targets.refractionFramebuffer != nullptr,
        targets.deferredSlotsBufferDescriptor.slot()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

