// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/light_space_shadow.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_shadow_storage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(LightSpaceShadowStorage, ReservesWholeCasterAndRoundedMeshletGroupDraws){
    LightSpaceShadowCaster casters[4];
    casters[1].meshletCount = 1u;
    casters[2].meshletCount = NWB_LIGHT_SPACE_MESHLETS_PER_DRAW + 1u;
    casters[3].meshletCount = 2u * NWB_LIGHT_SPACE_MESHLETS_PER_DRAW + 1u;
    const u32 drawCount = LightSpaceShadowDrawCount(casters, LengthOf(casters));
    ASSERT_EQ(drawCount, 7u);
    casters[1].meshletCount = NWB_LIGHT_SPACE_MESHLETS_PER_DRAW;
    EXPECT_EQ(LightSpaceShadowDrawCount(casters, LengthOf(casters)), drawCount);

    const LightSpaceLightRequest requests[] = { { 0u, 0u, Scene::LightType::Directional }, { 1u, 1u, Scene::LightType::Point } };
    LightSpacePlan plan;
    ASSERT_TRUE(BuildLightSpacePlan(SoftwareShadowSettings{}, requests, LengthOf(requests), Limit<u32>::s_Max, drawCount, plan));
    ASSERT_EQ(plan.lightCount, 2u);
    ASSERT_EQ(plan.viewCount, 7u);
    EXPECT_EQ(plan.drawArgumentByteSize, 7u * 7u * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
    LightSpaceShadowStorageCapacity capacity{ plan.countByteSize, plan.eventByteSize, plan.viewByteSize,
        plan.drawArgumentByteSize, plan.textureResolution, plan.viewCount };
    EXPECT_TRUE(LightSpaceShadowStorageFits(plan, capacity));
    --capacity.drawArgumentBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(plan, capacity));
}

TEST(LightSpaceShadowStorage, RejectsInvalidAndOverflowingDrawSpansWithoutWrapping){
    EXPECT_EQ(LightSpaceShadowDrawCount(nullptr, 0u), 0u);
    EXPECT_EQ(LightSpaceShadowDrawCount(nullptr, 1u), 0u);
    LightSpaceShadowCaster casters[32];
    EXPECT_EQ(LightSpaceShadowDrawCount(casters, 0u), 0u);
    for(auto& caster : casters)
        caster.meshletCount = Limit<u32>::s_Max;
    EXPECT_EQ(LightSpaceShadowDrawCount(casters, 1u), 134217728u);
    EXPECT_EQ(LightSpaceShadowDrawCount(casters, 31u), 4160749568u);
    EXPECT_EQ(LightSpaceShadowDrawCount(casters, LengthOf(casters)), 0u);
}

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

