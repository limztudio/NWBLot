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
    const auto counted = LightSpaceShadowDrawCount(casters, LengthOf(casters));
    ASSERT_TRUE(counted);
    const u32 drawCount = *counted;
    ASSERT_EQ(drawCount, 7u);
    casters[1].meshletCount = NWB_LIGHT_SPACE_MESHLETS_PER_DRAW;
    const auto recounted = LightSpaceShadowDrawCount(casters, LengthOf(casters));
    ASSERT_TRUE(recounted);
    EXPECT_EQ(*recounted, drawCount);

    const LightSpaceLightRequest requests[] = { { 0u, 0u, Scene::LightType::Directional }, { 1u, 1u, Scene::LightType::Point } };
    Expected<LightSpacePlan> plan;
    plan = BuildLightSpacePlan(SoftwareShadowSettings{}, requests, LengthOf(requests), Limit<u32>::s_Max, drawCount);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->lightCount, 2u);
    ASSERT_EQ(plan->viewCount, 7u);
    EXPECT_EQ(plan->drawArgumentByteSize, 7u * 7u * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
    LightSpaceShadowStorageCapacity capacity{ plan->countByteSize, plan->eventByteSize, plan->viewByteSize, plan->drawArgumentByteSize, plan->textureResolution, plan->viewCount };
    EXPECT_TRUE(LightSpaceShadowStorageFits(*plan, capacity));
    --capacity.drawArgumentBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(*plan, capacity));
}

TEST(LightSpaceShadowStorage, RejectsInvalidAndOverflowingDrawSpansWithoutWrapping){
    const auto emptyNull = LightSpaceShadowDrawCount(nullptr, 0u);
    ASSERT_TRUE(emptyNull);
    EXPECT_EQ(*emptyNull, 0u);
    EXPECT_FALSE(LightSpaceShadowDrawCount(nullptr, 1u));
    LightSpaceShadowCaster casters[32];
    const auto emptySpan = LightSpaceShadowDrawCount(casters, 0u);
    ASSERT_TRUE(emptySpan);
    EXPECT_EQ(*emptySpan, 0u);
    for(auto& caster : casters)
        caster.meshletCount = Limit<u32>::s_Max;
    const auto singleMaximum = LightSpaceShadowDrawCount(casters, 1u);
    ASSERT_TRUE(singleMaximum);
    EXPECT_EQ(*singleMaximum, 134217728u);
    const auto lastRepresentable = LightSpaceShadowDrawCount(casters, 31u);
    ASSERT_TRUE(lastRepresentable);
    EXPECT_EQ(*lastRepresentable, 4160749568u);
    EXPECT_FALSE(LightSpaceShadowDrawCount(casters, LengthOf(casters)));
}

TEST(LightSpaceShadowStorage, AcceptsExactGenerationAndRejectsEachInsufficientResource){
    const LightSpaceLightRequest requests[] = { { 0u, 0u, Scene::LightType::Directional }, { 1u, 1u, Scene::LightType::Point } };
    Expected<LightSpacePlan> plan;
    plan = BuildLightSpacePlan(SoftwareShadowSettings{}, requests, LengthOf(requests), Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(plan);
    const LightSpaceShadowStorageCapacity exact{ plan->countByteSize, plan->eventByteSize, plan->viewByteSize, plan->drawArgumentByteSize, plan->textureResolution, plan->viewCount };
    ASSERT_TRUE(LightSpaceShadowStorageFits(*plan, exact));
    auto capacity = exact;
    --capacity.countBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(*plan, capacity));
    capacity = exact;
    --capacity.eventBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(*plan, capacity));
    capacity = exact;
    --capacity.viewBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(*plan, capacity));
    capacity = exact;
    --capacity.drawArgumentBytes;
    EXPECT_FALSE(LightSpaceShadowStorageFits(*plan, capacity));
    capacity = exact;
    --capacity.textureResolution;
    EXPECT_FALSE(LightSpaceShadowStorageFits(*plan, capacity));
    capacity = exact;
    --capacity.arrayLayers;
    EXPECT_FALSE(LightSpaceShadowStorageFits(*plan, capacity));
}

TEST(LightSpaceShadowStorage, CanSelectSmallerCurrentPlanWithoutAllocatingAndDeclinesGrowth){
    const LightSpaceLightRequest directional{ 0u, 0u, Scene::LightType::Directional };
    const LightSpaceLightRequest point{ 1u, 1u, Scene::LightType::Point };
    const LightSpaceLightRequest both[] = { directional, point };
    Expected<LightSpacePlan> larger;
    Expected<LightSpacePlan> smaller;
    larger = BuildLightSpacePlan(SoftwareShadowSettings{}, both, LengthOf(both), Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(larger);
    smaller = BuildLightSpacePlan(SoftwareShadowSettings{}, &point, 1u, Limit<u32>::s_Max, 20u);
    ASSERT_TRUE(smaller);
    const LightSpaceShadowStorageCapacity capacity{ larger->countByteSize, larger->eventByteSize, larger->viewByteSize, larger->drawArgumentByteSize, larger->textureResolution, larger->viewCount };
    EXPECT_TRUE(LightSpaceShadowStorageFits(*smaller, capacity));
    const LightSpaceShadowStorageCapacity small{ smaller->countByteSize, smaller->eventByteSize, smaller->viewByteSize, smaller->drawArgumentByteSize, smaller->textureResolution, smaller->viewCount };
    EXPECT_FALSE(LightSpaceShadowStorageFits(*larger, small));
    EXPECT_FALSE(LightSpaceShadowStorageFits(LightSpacePlan{}, capacity));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

