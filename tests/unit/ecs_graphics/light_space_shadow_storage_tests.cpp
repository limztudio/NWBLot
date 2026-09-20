// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/light_space_shadow.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_shadow_storage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(LightSpaceShadowStorage, AcceptsExactGenerationAndRejectsEachInsufficientResource){
    const LightSpaceLightRequest requests[] = { { 0u, 0u, Scene::LightType::Directional }, { 1u, 1u, Scene::LightType::Point } };
    LightSpacePlan plan;
    ASSERT_TRUE(BuildLightSpacePlan(SoftwareShadowSettings{}, requests, LengthOf(requests), Limit<u32>::s_Max, 20u, plan));
    const LightSpaceShadowStorageCapacity exact{ plan.countByteSize, plan.eventByteSize, plan.viewByteSize, plan.drawArgumentByteSize, plan.textureResolution, plan.viewCount };
    ASSERT_TRUE(LightSpaceShadowStorageFits(plan, exact));
    auto capacity = exact;
    --capacity.countBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(plan, capacity));
    capacity = exact;
    --capacity.eventBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(plan, capacity));
    capacity = exact;
    --capacity.viewBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(plan, capacity));
    capacity = exact;
    --capacity.drawArgumentBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(plan, capacity));
    capacity = exact;
    --capacity.textureResolution;
    EXPECT_FALSE(LightSpaceShadowStorageFits(plan, capacity));
    capacity = exact;
    --capacity.arrayLayers;
    EXPECT_FALSE(LightSpaceShadowStorageFits(plan, capacity));
}

TEST(LightSpaceShadowStorage, CanSelectSmallerCurrentPlanWithoutAllocatingAndDeclinesGrowth){
    const LightSpaceLightRequest directional{ 0u, 0u, Scene::LightType::Directional };
    const LightSpaceLightRequest point{ 1u, 1u, Scene::LightType::Point };
    const LightSpaceLightRequest both[] = { directional, point };
    LightSpacePlan larger;
    LightSpacePlan smaller;
    ASSERT_TRUE(BuildLightSpacePlan(SoftwareShadowSettings{}, both, LengthOf(both), Limit<u32>::s_Max, 20u, larger));
    ASSERT_TRUE(BuildLightSpacePlan(SoftwareShadowSettings{}, &point, 1u, Limit<u32>::s_Max, 20u, smaller));
    const LightSpaceShadowStorageCapacity capacity{ larger.countByteSize, larger.eventByteSize, larger.viewByteSize, larger.drawArgumentByteSize,
        larger.textureResolution, larger.viewCount };
    EXPECT_TRUE(LightSpaceShadowStorageFits(smaller, capacity));
    const LightSpaceShadowStorageCapacity small{ smaller.countByteSize, smaller.eventByteSize, smaller.viewByteSize, smaller.drawArgumentByteSize,
        smaller.textureResolution, smaller.viewCount };
    EXPECT_FALSE(LightSpaceShadowStorageFits(larger, small));
    EXPECT_FALSE(LightSpaceShadowStorageFits(LightSpacePlan{}, capacity));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

