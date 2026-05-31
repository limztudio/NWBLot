// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"

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
static_assert(offsetof(RendererAvboitPushConstants, frame) == 0u, "RendererAvboit frame push offset drifted");
static_assert(
    offsetof(RendererAvboitPushConstants, volume)
        == offsetof(RendererAvboitPushConstants, frame) + sizeof(((RendererAvboitPushConstants*)nullptr)->frame),
    "RendererAvboit volume push offset drifted"
);
static_assert(
    offsetof(RendererAvboitPushConstants, params)
        == offsetof(RendererAvboitPushConstants, volume) + sizeof(((RendererAvboitPushConstants*)nullptr)->volume),
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
[[nodiscard]] bool MaterialPipelinePassUsesRendererAvboit(MaterialPipelinePass::Enum pass);
[[nodiscard]] RendererAvboitPushConstants BuildRendererAvboitPushConstants(const RendererSystem::AvboitFrameTargets& targets);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

