
#pragma once


#include <impl/ecs_render/kernel/renderer_types.h>

#include <impl/assets/graphics/avboit/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RendererAvboitPushConstants{
    u32 frame[4] = {};
    u32 volume[4] = {};
    Float4 params = Float4(0.f, 0.f, 0.f, 0.f);
};
static_assert(sizeof(RendererAvboitPushConstants) == NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE, "RendererAvboitPushConstants layout must stay stable");
static_assert(offsetof(RendererAvboitPushConstants, frame) == sizeof(u32) * NWB_AVBOIT_PUSH_FRAME_WORD_OFFSET, "RendererAvboit frame push offset drifted");
static_assert(
    offsetof(RendererAvboitPushConstants, volume) == sizeof(u32) * NWB_AVBOIT_PUSH_VOLUME_WORD_OFFSET,
    "RendererAvboit volume push offset drifted"
);
static_assert(
    offsetof(RendererAvboitPushConstants, params) == sizeof(u32) * NWB_AVBOIT_PUSH_PARAMS_WORD_OFFSET,
    "RendererAvboit params push offset drifted"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_RendererAvboitTransparentDrawPushConstantSize = NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::Format::Enum SelectRendererAvboitAccumColorFormat(Core::Device& device);
[[nodiscard]] Core::Format::Enum SelectRendererAvboitAccumExtinctionFormat(Core::Device& device);
[[nodiscard]] Core::Format::Enum SelectRendererAvboitTransmittanceFormat(Core::Device& device);
[[nodiscard]] Core::Format::Enum SelectRendererAvboitLowRasterFormat(Core::Device& device);
[[nodiscard]] Core::RenderState BuildRendererAvboitVoxelRenderState();
[[nodiscard]] Core::RenderState BuildRendererAvboitAccumulateRenderState();
[[nodiscard]] RendererAvboitPushConstants BuildRendererAvboitPushConstants(const AvboitFrameTargets& targets);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

