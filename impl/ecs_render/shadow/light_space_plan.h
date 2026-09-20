// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "light_space_settings.h"

#include <impl/ecs_scene/components.h>
#include <impl/assets/graphics/shadow/light_space_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>

#include <global/containers.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LightSpaceViewGpu{
    Float4U worldToClipRows[4] = {};
    Float4U originOrDirection = {};
    Float4U depthRange = {};
    u32 map[4] = {};
    u32 light[4] = {};
};
static_assert(sizeof(LightSpaceViewGpu) == NWB_LIGHT_SPACE_VIEW_BYTES);
static_assert(offsetof(LightSpaceViewGpu, originOrDirection) == 64u);
static_assert(offsetof(LightSpaceViewGpu, depthRange) == 80u);
static_assert(offsetof(LightSpaceViewGpu, map) == 96u);
static_assert(offsetof(LightSpaceViewGpu, light) == 112u);

struct LightSpaceLightRequest{
    u32 lightIndex = 0u;
    u32 shadowSlot = 0u;
    Scene::LightType::Enum type = Scene::LightType::Directional;
    bool eligible = true;
};

struct LightSpaceLightPlan{
    u32 lightIndex = 0u;
    u32 shadowSlot = 0u;
    u32 firstView = 0u;
    u32 viewCount = 0u;
    u32 resolution = 0u;
    u32 pixelOffset = 0u;
};

struct LightSpacePlan{
    Array<LightSpaceViewGpu, NWB_SCENE_SHADOW_SLOT_COUNT * 6u> views = {};
    Array<LightSpaceLightPlan, NWB_SCENE_SHADOW_SLOT_COUNT> lights = {};
    u32 lightCount = 0u;
    u32 viewCount = 0u;
    u32 textureResolution = 0u;
    u32 totalPixels = 0u;
    u64 eventByteSize = 0u;
    u64 countByteSize = 0u;
    u64 depthByteSize = 0u;
    u64 viewByteSize = 0u;
    u64 drawArgumentByteSize = 0u;
    u64 totalByteSize = 0u;
};

// Invalid input leaves the output unchanged. Valid but ineligible or over-budget lights retain software tracing.
[[nodiscard]] bool BuildLightSpacePlan(
    const SoftwareShadowSettings& settings, const LightSpaceLightRequest* requests, usize requestCount,
    u64 maxStorageBufferRange, u32 casterCount, LightSpacePlan& outPlan
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

