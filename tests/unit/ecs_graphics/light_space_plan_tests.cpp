// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/light_space_plan.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(LightSpacePlan, PacksDirectionalAndEveryPointFaceWithoutPixelOrLayerOverlap){
    const LightSpaceLightRequest requests[] = {
        { 7u, 3u, Scene::LightType::Directional },
        { 63u, 0u, Scene::LightType::Point },
    };
    LightSpacePlan plan;
    ASSERT_TRUE(BuildLightSpacePlan(SoftwareShadowSettings{}, requests, LengthOf(requests), Limit<u32>::s_Max, 20u, plan));
    ASSERT_EQ(plan.lightCount, 2u);
    ASSERT_EQ(plan.viewCount, 7u);
    EXPECT_EQ(plan.textureResolution, 512u);
    EXPECT_EQ(plan.totalPixels, 655360u);
    EXPECT_EQ(plan.eventByteSize, static_cast<u64>(plan.totalPixels) * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL * NWB_LIGHT_SPACE_EVENT_BYTES);
    EXPECT_EQ(plan.countByteSize, 2621440u);
    EXPECT_EQ(plan.depthByteSize, 7340032u);
    EXPECT_EQ(plan.viewByteSize, 896u);
    EXPECT_EQ(plan.drawArgumentByteSize, 7u * 20u * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
    EXPECT_EQ(plan.totalByteSize, plan.eventByteSize + plan.countByteSize + plan.depthByteSize + plan.viewByteSize + plan.drawArgumentByteSize);
    EXPECT_EQ(plan.lights[1].firstView, 1u);
    EXPECT_EQ(plan.lights[1].pixelOffset, 262144u);
    u32 end = 0u;
    for(u32 index = 0u; index < plan.viewCount; ++index){
        const auto& view = plan.views[index];
        EXPECT_EQ(view.map[2], end);
        EXPECT_EQ(view.map[3], index);
        EXPECT_EQ(view.map[0], index == 0u ? 512u : 256u);
        EXPECT_EQ(view.map[0], view.map[1]);
        EXPECT_EQ(view.light[0], index == 0u ? 7u : 63u);
        EXPECT_EQ(view.light[1], index == 0u ? 3u : 0u);
        EXPECT_EQ(view.light[2], index == 0u ? 0u : index - 1u);
        EXPECT_EQ(view.light[3], NWB_LIGHT_SPACE_FLAG_ELIGIBLE | (index == 0u ? 0u : NWB_LIGHT_SPACE_FLAG_POINT));
        EXPECT_EQ(view.depthRange.w, 0.0f); // Only the GPU can validate its current-pose fit.
        end += view.map[0] * view.map[1];
    }
    EXPECT_EQ(end, plan.totalPixels);
}

TEST(LightSpacePlan, ChargesLargerDepthExtentToPreviouslyAdmittedPointFaces){
    const LightSpaceLightRequest point{ 0u, 0u, Scene::LightType::Point };
    const LightSpaceLightRequest directional{ 1u, 1u, Scene::LightType::Directional };
    SoftwareShadowSettings settings;
    LightSpacePlan pointOnly;
    LightSpacePlan directionalOnly;
    ASSERT_TRUE(BuildLightSpacePlan(settings, &point, 1u, Limit<u32>::s_Max, 20u, pointOnly));
    ASSERT_TRUE(BuildLightSpacePlan(settings, &directional, 1u, Limit<u32>::s_Max, 20u, directionalOnly));
    settings.memoryBudgetBytes = pointOnly.totalByteSize + directionalOnly.totalByteSize;
    const LightSpaceLightRequest requests[] = { point, directional };
    LightSpacePlan plan;
    ASSERT_TRUE(BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u, plan));
    ASSERT_EQ(plan.lightCount, 1u);
    EXPECT_EQ(plan.lights[0].lightIndex, 0u);
    EXPECT_EQ(plan.viewCount, 6u);
    EXPECT_EQ(plan.textureResolution, 256u);
    EXPECT_EQ(plan.totalByteSize, pointOnly.totalByteSize);
}

TEST(LightSpacePlan, BudgetBoundaryAdmitsWholeLightsAndLeavesOthersForTracing){
    SoftwareShadowSettings settings;
    const LightSpaceLightRequest light{};
    LightSpacePlan expected;
    ASSERT_TRUE(BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 20u, expected));
    ASSERT_EQ(expected.lightCount, 1u);
    settings.memoryBudgetBytes = expected.totalByteSize;
    LightSpacePlan exact;
    ASSERT_TRUE(BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 20u, exact));
    EXPECT_EQ(exact.lightCount, 1u);
    --settings.memoryBudgetBytes;
    ASSERT_TRUE(BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 20u, exact));
    EXPECT_EQ(exact.lightCount, 0u);
    EXPECT_EQ(exact.viewCount, 0u);
    EXPECT_EQ(exact.totalByteSize, 0u);
}

TEST(LightSpacePlan, SkipsUnsupportedAndIneligibleCastersWithoutDroppingLaterEligibleLights){
    const LightSpaceLightRequest requests[] = {
        { 0u, 0u, Scene::LightType::Spot },
        { 1u, 1u, Scene::LightType::Directional, false },
        { 2u, 2u, Scene::LightType::Point },
    };
    LightSpacePlan plan;
    SoftwareShadowSettings settings;
    settings.backend = SoftwareShadowBackend::LightSpace;
    ASSERT_TRUE(BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u, plan));
    ASSERT_EQ(plan.lightCount, 1u);
    EXPECT_EQ(plan.lights[0].lightIndex, 2u);
    EXPECT_EQ(plan.viewCount, 6u);
    settings.backend = SoftwareShadowBackend::SoftwareTrace;
    ASSERT_TRUE(BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u, plan));
    EXPECT_EQ(plan.lightCount, 0u);
    EXPECT_EQ(plan.totalByteSize, 0u);
}

TEST(LightSpacePlan, RejectsInvalidIdentityWithoutPublishingPartialPlan){
    LightSpacePlan plan;
    plan.totalByteSize = 123u;
    SoftwareShadowSettings settings;
    LightSpaceLightRequest requests[] = { { 0u, 0u }, { 1u, 1u } };
    EXPECT_FALSE(BuildLightSpacePlan(settings, nullptr, 1u, Limit<u32>::s_Max, 20u, plan));
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, 9u, Limit<u32>::s_Max, 20u, plan));
    requests[1].shadowSlot = 0u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, 2u, Limit<u32>::s_Max, 20u, plan));
    requests[1].shadowSlot = 1u;
    requests[1].lightIndex = 0u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, 2u, Limit<u32>::s_Max, 20u, plan));
    requests[1].lightIndex = 64u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, 2u, Limit<u32>::s_Max, 20u, plan));
    requests[1].lightIndex = 1u;
    requests[1].shadowSlot = 8u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, 2u, Limit<u32>::s_Max, 20u, plan));
    requests[1].shadowSlot = 1u;
    requests[1].type = Scene::LightType::kCount;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, 2u, Limit<u32>::s_Max, 20u, plan));
    EXPECT_EQ(plan.totalByteSize, 123u);
    EXPECT_TRUE(BuildLightSpacePlan(settings, nullptr, 0u, Limit<u32>::s_Max, 0u, plan));
    EXPECT_EQ(plan.totalByteSize, 0u);
}

TEST(LightSpacePlan, RespectsDeviceDescriptorRangeAndRetainsLaterFittingLights){
    SoftwareShadowSettings settings;
    settings.memoryBudgetBytes = 512u * 1024u * 1024u;
    settings.pointResolution = 512u;
    const LightSpaceLightRequest requests[] = {
        { 0u, 0u, Scene::LightType::Point },
        { 1u, 1u, Scene::LightType::Directional },
    };
    LightSpacePlan plan;
    const u64 directionalEventBytes = 512ull * 512ull * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL * NWB_LIGHT_SPACE_EVENT_BYTES;
    ASSERT_TRUE(BuildLightSpacePlan(settings, requests, LengthOf(requests), directionalEventBytes, 20u, plan));
    ASSERT_EQ(plan.lightCount, 1u);
    EXPECT_EQ(plan.lights[0].lightIndex, 1u);
    EXPECT_EQ(plan.eventByteSize, directionalEventBytes);
    EXPECT_LE(plan.countByteSize, directionalEventBytes);
    EXPECT_LE(plan.viewByteSize, directionalEventBytes);
    EXPECT_LE(plan.drawArgumentByteSize, directionalEventBytes);
    ASSERT_TRUE(BuildLightSpacePlan(settings, requests, LengthOf(requests), directionalEventBytes - 1u, 20u, plan));
    EXPECT_EQ(plan.lightCount, 0u);
    plan.totalByteSize = 123u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, LengthOf(requests), 0u, 20u, plan));
    EXPECT_EQ(plan.totalByteSize, 123u);
}

TEST(LightSpacePlan, BoundsExternalSettingsAndShaderAddressSpace){
    SoftwareShadowSettings settings;
    EXPECT_TRUE(ValidateSoftwareShadowSettings(settings));
    settings.directionalResolution = 0u;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings.directionalResolution = 2049u;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings.directionalResolution = 2048u;
    settings.pointResolution = 2048u;
    settings.memoryBudgetBytes = Limit<u64>::s_Max;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings.memoryBudgetBytes = Limit<u32>::s_Max;
    EXPECT_TRUE(ValidateSoftwareShadowSettings(settings));
    LightSpaceLightRequest requests[NWB_SCENE_SHADOW_SLOT_COUNT];
    for(u32 index = 0u; index < LengthOf(requests); ++index)
        requests[index] = { index, index, Scene::LightType::Point };
    LightSpacePlan plan;
    ASSERT_TRUE(BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u, plan));
    EXPECT_EQ(plan.lightCount, 0u); // One six-face 2048 map exceeds the shader address range.
    settings.pointResolution = 1024u;
    ASSERT_TRUE(BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u, plan));
    ASSERT_EQ(plan.lightCount, 2u);
    EXPECT_LE(plan.eventByteSize, Limit<u32>::s_Max);
    EXPECT_LE(plan.totalByteSize, settings.memoryBudgetBytes);
    settings.memoryBudgetBytes = 0u;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings = SoftwareShadowSettings{};
    settings.backend = static_cast<SoftwareShadowBackend::Enum>(255u);
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
}


TEST(LightSpacePlan, ChargesActualCasterArgumentsAndEnforcesTheirDescriptorRange){
    SoftwareShadowSettings settings;
    settings.directionalResolution = 32u;
    settings.memoryBudgetBytes = 16u * 1024u * 1024u;
    const LightSpaceLightRequest light{};
    constexpr u32 casterCount = 65536u;
    constexpr u64 argumentBytes = static_cast<u64>(casterCount) * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES;
    LightSpacePlan plan;
    ASSERT_TRUE(BuildLightSpacePlan(settings, &light, 1u, argumentBytes, casterCount, plan));
    ASSERT_EQ(plan.lightCount, 1u);
    EXPECT_EQ(plan.drawArgumentByteSize, argumentBytes);
    EXPECT_EQ(plan.totalByteSize, plan.eventByteSize + plan.countByteSize + plan.viewByteSize + plan.depthByteSize + argumentBytes);
    ASSERT_TRUE(BuildLightSpacePlan(settings, &light, 1u, argumentBytes - 1u, casterCount, plan));
    EXPECT_EQ(plan.lightCount, 0u);
    plan.totalByteSize = 123u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 0u, plan));
    EXPECT_EQ(plan.totalByteSize, 123u);
    ASSERT_TRUE(BuildLightSpacePlan(settings, nullptr, 0u, Limit<u32>::s_Max, 0u, plan));
    EXPECT_EQ(plan.totalByteSize, 0u);
    ASSERT_TRUE(BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, Limit<u32>::s_Max, plan));
    EXPECT_EQ(plan.lightCount, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

