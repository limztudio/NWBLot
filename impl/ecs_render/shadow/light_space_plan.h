// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shadow/light_space_settings.adhd.h>

#include <impl/ecs_scene/components.adhd.h>
#include <impl/assets/graphics/shadow/light_space_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>

#include <global/containers.h>
#include <global/simdmath.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(alignof(LightSpaceViewGpu) >= 16u);
static_assert(sizeof(LightSpaceViewGpu) == NWB_LIGHT_SPACE_VIEW_BYTES);
static_assert(offsetof(LightSpaceViewGpu, originOrDirection) == s_LightSpaceViewGpuOriginOrDirectionOffset);
static_assert(offsetof(LightSpaceViewGpu, depthRange) == s_LightSpaceViewGpuDepthRangeOffset);
static_assert(offsetof(LightSpaceViewGpu, map) == s_LightSpaceViewGpuMapOffset);
static_assert(offsetof(LightSpaceViewGpu, light) == s_LightSpaceViewGpuLightOffset);

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
