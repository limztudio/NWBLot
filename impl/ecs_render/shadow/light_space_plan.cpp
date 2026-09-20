// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_plan.h"

#include <global/algorithm.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildLightSpacePlan(
    const SoftwareShadowSettings& settings, const LightSpaceLightRequest* const requests, const usize requestCount,
    const u64 maxStorageBufferRange, const u32 casterCount, LightSpacePlan& outPlan){
    if(maxStorageBufferRange == 0u || !ValidateSoftwareShadowSettings(settings) || requestCount > NWB_SCENE_SHADOW_SLOT_COUNT || (requestCount != 0u && !requests))
        return false;
    u32 seenSlots = 0u;
    u64 seenLights = 0u;
    for(usize index = 0u; index < requestCount; ++index){
        const auto& request = requests[index];
        if(request.lightIndex >= NWB_SCENE_MAX_LIGHTS || request.shadowSlot >= NWB_SCENE_SHADOW_SLOT_COUNT || request.type >= Scene::LightType::kCount)
            return false;
        const u32 slotBit = 1u << request.shadowSlot;
        const u64 lightBit = 1ull << request.lightIndex;
        if((seenSlots & slotBit) != 0u || (seenLights & lightBit) != 0u)
            return false;
        seenSlots |= slotBit;
        seenLights |= lightBit;
    }
    LightSpacePlan plan;
    if(settings.backend == SoftwareShadowBackend::SoftwareTrace){
        outPlan = plan;
        return true;
    }
    for(usize index = 0u; index < requestCount; ++index){
        const auto& request = requests[index];
        if(!request.eligible || request.type == Scene::LightType::Spot)
            continue;
        if(casterCount == 0u)
            return false;
        const bool point = request.type == Scene::LightType::Point;
        const u32 resolution = point ? settings.pointResolution : settings.directionalResolution;
        const u32 viewCount = point ? 6u : 1u;
        const u64 pixelsPerView = static_cast<u64>(resolution) * resolution;
        const u64 pixels = plan.totalPixels + pixelsPerView * viewCount;
        const u32 newViewCount = plan.viewCount + viewCount;
        const u32 textureResolution = Max(plan.textureResolution, resolution);
        const u64 eventBytes = pixels * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL * NWB_LIGHT_SPACE_EVENT_BYTES;
        const u64 countBytes = pixels * sizeof(u32);
        // Every D32 array layer uses the largest selected extent, even when its viewport is smaller.
        const u64 depthBytes = static_cast<u64>(textureResolution) * textureResolution * newViewCount * sizeof(f32);
        const u64 viewBytes = static_cast<u64>(newViewCount) * sizeof(LightSpaceViewGpu);
        const u64 drawArgumentBytes = static_cast<u64>(newViewCount) * casterCount * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES;
        const u64 totalBytes = eventBytes + countBytes + depthBytes + viewBytes + drawArgumentBytes;
        if(
            totalBytes > settings.memoryBudgetBytes || eventBytes > Limit<u32>::s_Max || pixels > Limit<u32>::s_Max
            || eventBytes > maxStorageBufferRange || countBytes > maxStorageBufferRange || viewBytes > maxStorageBufferRange
            || drawArgumentBytes > maxStorageBufferRange || drawArgumentBytes > Limit<u32>::s_Max
        )
            continue;
        plan.lights[plan.lightCount] = { request.lightIndex, request.shadowSlot, plan.viewCount, viewCount, resolution, plan.totalPixels };
        ++plan.lightCount;
        for(u32 face = 0u; face < viewCount; ++face){
            const u32 viewIndex = plan.viewCount + face;
            auto& view = plan.views[viewIndex];
            view.map[0] = resolution;
            view.map[1] = resolution;
            view.map[2] = plan.totalPixels + static_cast<u32>(pixelsPerView) * face;
            view.map[3] = viewIndex;
            view.light[0] = request.lightIndex;
            view.light[1] = request.shadowSlot;
            view.light[2] = face;
            view.light[3] = NWB_LIGHT_SPACE_FLAG_ELIGIBLE | (point ? NWB_LIGHT_SPACE_FLAG_POINT : 0u);
        }
        plan.viewCount = newViewCount;
        plan.textureResolution = textureResolution;
        plan.totalPixels = static_cast<u32>(pixels);
        plan.eventByteSize = eventBytes;
        plan.countByteSize = countBytes;
        plan.depthByteSize = depthBytes;
        plan.viewByteSize = viewBytes;
        plan.drawArgumentByteSize = drawArgumentBytes;
        plan.totalByteSize = totalBytes;
    }
    outPlan = plan;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

