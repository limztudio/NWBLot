// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/light_space_plan.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;


TEST(LightSpacePlan, RejectsUnknownCoverage){
    const LightSpaceLightRequest requests[] = {
        { 7u, 3u, Scene::LightType::Directional },
        { 63u, 0u, Scene::LightType::Point },
    };
    SoftwareShadowSettings settings;
    settings.coverage = static_cast<SoftwareShadowCoverage::Enum>(2u);
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u));
}


TEST(LightSpacePlan, RejectsUnknownBlockerPolicy){
    SoftwareShadowSettings settings;
    settings.blockerSearch = static_cast<SoftwareShadowBlockerSearch::Enum>(3u);
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    const LightSpaceLightRequest light{};
    EXPECT_FALSE(BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 20u));
}

TEST(LightSpacePlan, ChargesLargerDepthExtentToPreviouslyAdmittedPointFaces){
    const LightSpaceLightRequest point{ 0u, 0u, Scene::LightType::Point };
    const LightSpaceLightRequest directional{ 1u, 1u, Scene::LightType::Directional };
    SoftwareShadowSettings settings;
    Expected<LightSpacePlan> pointOnly;
    Expected<LightSpacePlan> directionalOnly;
    pointOnly = BuildLightSpacePlan(settings, &point, 1u, Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(pointOnly);
    directionalOnly = BuildLightSpacePlan(settings, &directional, 1u, Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(directionalOnly);
    settings.memoryBudgetBytes = pointOnly->totalByteSize + directionalOnly->totalByteSize;
    const LightSpaceLightRequest requests[] = { point, directional };
    Expected<LightSpacePlan> plan;
    plan = BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->lightCount, 1u);
    EXPECT_EQ(plan->lights[0].lightIndex, 0u);
    EXPECT_EQ(plan->viewCount, 6u);
    EXPECT_EQ(plan->textureResolution, 256u);
    EXPECT_EQ(plan->totalByteSize, pointOnly->totalByteSize);
}

TEST(LightSpacePlan, BudgetBoundaryAdmitsWholeLightsAndLeavesOthersForTracing){
    SoftwareShadowSettings settings;
    const LightSpaceLightRequest light{};
    Expected<LightSpacePlan> expected;
    expected = BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(expected);
    ASSERT_EQ(expected->lightCount, 1u);
    settings.memoryBudgetBytes = expected->totalByteSize;
    Expected<LightSpacePlan> exact;
    exact = BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(exact);
    EXPECT_EQ(exact->lightCount, 1u);
    --settings.memoryBudgetBytes;
    exact = BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(exact);
    EXPECT_EQ(exact->lightCount, 0u);
    EXPECT_EQ(exact->viewCount, 0u);
    EXPECT_EQ(exact->totalByteSize, 0u);
}

TEST(LightSpacePlan, SkipsUnsupportedAndIneligibleCastersWithoutDroppingLaterEligibleLights){
    const LightSpaceLightRequest requests[] = {
        { 0u, 0u, Scene::LightType::Spot },
        { 1u, 1u, Scene::LightType::Directional, false },
        { s_ExpectedDualCount, s_ExpectedDualCount, Scene::LightType::Point },
    };
    Expected<LightSpacePlan> plan;
    SoftwareShadowSettings settings;
    settings.backend = SoftwareShadowBackend::LightSpace;
    plan = BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->lightCount, 1u);
    EXPECT_EQ(plan->lights[0].lightIndex, s_ExpectedDualCount);
    EXPECT_EQ(plan->viewCount, 6u);
}

TEST(LightSpacePlan, RejectsInvalidIdentityWithoutPublishingPartialPlan){
    Expected<LightSpacePlan> plan;
    SoftwareShadowSettings settings;
    LightSpaceLightRequest requests[] = { { 0u, 0u }, { 1u, 1u } };
    EXPECT_FALSE(BuildLightSpacePlan(settings, nullptr, 1u, Limit<u32>::s_Max, 20u));
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, 9u, Limit<u32>::s_Max, 20u));
    requests[1].shadowSlot = 0u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, s_ExpectedDualCount, Limit<u32>::s_Max, 20u));
    requests[1].shadowSlot = 1u;
    requests[1].lightIndex = 0u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, s_ExpectedDualCount, Limit<u32>::s_Max, 20u));
    requests[1].lightIndex = 64u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, s_ExpectedDualCount, Limit<u32>::s_Max, 20u));
    requests[1].lightIndex = 1u;
    requests[1].shadowSlot = 8u;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, s_ExpectedDualCount, Limit<u32>::s_Max, 20u));
    requests[1].shadowSlot = 1u;
    requests[1].type = Scene::LightType::kCount;
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, s_ExpectedDualCount, Limit<u32>::s_Max, 20u));
    plan = BuildLightSpacePlan(settings, nullptr, 0u, Limit<u32>::s_Max, 0u);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->totalByteSize, 0u);
}

TEST(LightSpacePlan, RespectsDeviceDescriptorRangeAndRetainsLaterFittingLights){
    SoftwareShadowSettings settings;
    settings.memoryBudgetBytes = 512u * 1024u * 1024u;
    settings.pointResolution = 512u;
    const LightSpaceLightRequest requests[] = {
        { 0u, 0u, Scene::LightType::Point },
        { 1u, 1u, Scene::LightType::Directional },
    };
    Expected<LightSpacePlan> plan;
    const u64 directionalEventBytes = 512ull * 512ull * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL * NWB_LIGHT_SPACE_EVENT_BYTES;
    plan = BuildLightSpacePlan(settings, requests, LengthOf(requests), directionalEventBytes, 20u);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->lightCount, 1u);
    EXPECT_EQ(plan->lights[0].lightIndex, 1u);
    EXPECT_EQ(plan->eventByteSize, directionalEventBytes);
    EXPECT_LE(plan->countByteSize, directionalEventBytes);
    EXPECT_LE(plan->viewByteSize, directionalEventBytes);
    EXPECT_LE(plan->drawArgumentByteSize, directionalEventBytes);
    plan = BuildLightSpacePlan(settings, requests, LengthOf(requests), directionalEventBytes - 1u, 20u);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->lightCount, 0u);
    EXPECT_FALSE(BuildLightSpacePlan(settings, requests, LengthOf(requests), 0u, 20u));
}

TEST(LightSpacePlan, BoundsExternalSettingsAndShaderAddressSpace){
    SoftwareShadowSettings settings;
    settings.directionalResolution = 0u;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings.directionalResolution = 2049u;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings.directionalResolution = 2048u;
    settings.pointResolution = 2048u;
    settings.memoryBudgetBytes = Limit<u64>::s_Max;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings.memoryBudgetBytes = Limit<u32>::s_Max;
    LightSpaceLightRequest requests[NWB_SCENE_SHADOW_SLOT_COUNT];
    for(u32 index = 0u; index < LengthOf(requests); ++index)
        requests[index] = { index, index, Scene::LightType::Point };
    Expected<LightSpacePlan> plan;
    plan = BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->lightCount, 0u); // One six-face 2048 map exceeds the shader address range.
    settings.pointResolution = 1024u;
    plan = BuildLightSpacePlan(settings, requests, LengthOf(requests), Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->lightCount, s_ExpectedDualCount);
    EXPECT_LE(plan->eventByteSize, Limit<u32>::s_Max);
    EXPECT_LE(plan->totalByteSize, settings.memoryBudgetBytes);
    settings.memoryBudgetBytes = 0u;
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
    settings = SoftwareShadowSettings{};
    settings.backend = static_cast<SoftwareShadowBackend::Enum>(255u);
    EXPECT_FALSE(ValidateSoftwareShadowSettings(settings));
}


TEST(LightSpacePlan, ChargesActualDrawArgumentsAndEnforcesTheirDescriptorRange){
    SoftwareShadowSettings settings;
    settings.directionalResolution = 32u;
    settings.memoryBudgetBytes = 16u * 1024u * 1024u;
    const LightSpaceLightRequest light{};
    constexpr u32 s_DrawCount = 65536u;
    constexpr u64 s_ArgumentBytes = static_cast<u64>(s_DrawCount) * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES;
    Expected<LightSpacePlan> plan;
    plan = BuildLightSpacePlan(settings, &light, 1u, s_ArgumentBytes, s_DrawCount);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->lightCount, 1u);
    EXPECT_EQ(plan->drawArgumentByteSize, s_ArgumentBytes);
    EXPECT_EQ(plan->totalByteSize, plan->eventByteSize + plan->countByteSize + plan->viewByteSize + plan->depthByteSize + s_ArgumentBytes);
    plan = BuildLightSpacePlan(settings, &light, 1u, s_ArgumentBytes - 1u, s_DrawCount);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->lightCount, 0u);
    EXPECT_FALSE(BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, 0u));
    plan = BuildLightSpacePlan(settings, nullptr, 0u, Limit<u32>::s_Max, 0u);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->totalByteSize, 0u);
    plan = BuildLightSpacePlan(settings, &light, 1u, Limit<u32>::s_Max, Limit<u32>::s_Max);
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->lightCount, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

