// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shadow/light_space_settings.adhd.h>
#include <impl/assets/graphics/shadow/light_space_constants.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(alignof(LightSpaceViewGpu) >= 16u);
static_assert(sizeof(LightSpaceViewGpu) == NWB_LIGHT_SPACE_VIEW_BYTES);
static_assert(offsetof(LightSpaceViewGpu, originOrDirection) == NWB_LIGHT_SPACE_VIEW_ORIGIN_OFFSET);
static_assert(offsetof(LightSpaceViewGpu, depthRange) == NWB_LIGHT_SPACE_VIEW_DEPTH_RANGE_OFFSET);
static_assert(offsetof(LightSpaceViewGpu, map) == NWB_LIGHT_SPACE_VIEW_MAP_OFFSET);
static_assert(offsetof(LightSpaceViewGpu, light) == NWB_LIGHT_SPACE_VIEW_LIGHT_OFFSET);

// Invalid input is rejected. Valid but ineligible or over-budget lights retain software tracing.
[[nodiscard]] inline Expected<LightSpacePlan> BuildLightSpacePlan(const SoftwareShadowSettings& settings, const Span<const LightSpaceLightRequest> requests, const u64 maxStorageBufferRange, const u32 drawCount, const b8 csgIntervalDepth = false)noexcept{
    LightSpacePlan plan;
    if(!TryBuildLightSpacePlan(settings, requests, maxStorageBufferRange, drawCount, csgIntervalDepth, plan))
        return MakeUnexpected(Failure{});
    return plan;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

