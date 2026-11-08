// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/light_space_shadow.h>
#include <impl/ecs_render/shadow/task_graph_light_space_shadow.h>
#include <impl/ecs_scene/components.adhd.h>

#include <global/limit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_shadow_storage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(LightSpaceShadowStorage, GraphRequiresEachResourceAndValidTaskGeneration){
    LightSpaceShadowGraph graph;
    EXPECT_FALSE(graph.valid());
    const NWB::Core::GpuGraphResourceId resource{ .generation = 1u, .index = 0u };
    graph.counts = resource;
    graph.events = resource;
    graph.views = resource;
    graph.depth = resource;
    graph.ready = { .generation = 1u, .index = 0u };
    ASSERT_TRUE(graph.valid());
    const Array<NWB::Core::GpuGraphResourceId LightSpaceShadowGraph::*, 4> required = { &LightSpaceShadowGraph::counts, &LightSpaceShadowGraph::events, &LightSpaceShadowGraph::views, &LightSpaceShadowGraph::depth };
    for(const auto field : required){
        auto missing = graph;
        missing.*field = {};
        EXPECT_FALSE(missing.valid());
    }
    graph.ready.generation = 0u;
    EXPECT_FALSE(graph.valid());
}

TEST(LightSpaceShadowStorage, PipelineSelectionRespectsCsgRangeBoundaries){
    for(u32 index = 0u; index < 12u; ++index){
        EXPECT_EQ(ShouldSkipLightSpacePipeline(index, false, false), index >= 6u && index <= 10u);
        EXPECT_EQ(ShouldSkipLightSpacePipeline(index, true, true), index >= 9u && index <= 10u);
        EXPECT_FALSE(ShouldSkipLightSpacePipeline(index, true, false));
    }
}

TEST(LightSpaceShadowStorage, ReservesWholeCasterAndRoundedMeshletGroupDraws){
    Array<LightSpaceShadowCaster, 4> casters{};
    casters[1].meshletCount = 1u;
    casters[2].meshletCount = NWB_LIGHT_SPACE_MESHLETS_PER_DRAW + 1u;
    casters[3].meshletCount = 2u * NWB_LIGHT_SPACE_MESHLETS_PER_DRAW + 1u;
    const auto counted = LightSpaceShadowDrawCount(casters);
    ASSERT_TRUE(counted);
    const u32 drawCount = *counted;
    ASSERT_EQ(drawCount, 7u);
    casters[1].meshletCount = NWB_LIGHT_SPACE_MESHLETS_PER_DRAW;
    const auto recounted = LightSpaceShadowDrawCount(casters);
    ASSERT_TRUE(recounted);
    EXPECT_EQ(*recounted, drawCount);

    const Array<LightSpaceLightRequest, 2> requests = {{ { 0u, 0u, Scene::LightType::Directional }, { 1u, 1u, Scene::LightType::Point } }};
    const Expected<LightSpacePlan> plan = TryBuildLightSpacePlan(SoftwareShadowSettings{}, requests, Limit<u32>::s_Max, drawCount, false);
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->lightCount, 2u);
    ASSERT_EQ(plan->viewCount, 7u);
    EXPECT_EQ(plan->drawArgumentByteSize, 7u * 7u * NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES);
}

TEST(LightSpaceShadowStorage, RejectsInvalidAndOverflowingDrawSpansWithoutWrapping){
    const auto emptyNull = LightSpaceShadowDrawCount(Span<const LightSpaceShadowCaster>{});
    ASSERT_TRUE(emptyNull);
    EXPECT_EQ(*emptyNull, 0u);
    Array<LightSpaceShadowCaster, 32> casters{};
    const auto emptySpan = LightSpaceShadowDrawCount(Span<const LightSpaceShadowCaster>(casters.data(), 0u));
    ASSERT_TRUE(emptySpan);
    EXPECT_EQ(*emptySpan, 0u);
    for(auto& caster : casters)
        caster.meshletCount = Limit<u32>::s_Max;
    const auto singleMaximum = LightSpaceShadowDrawCount(Span<const LightSpaceShadowCaster>(casters.data(), 1u));
    ASSERT_TRUE(singleMaximum);
    EXPECT_EQ(*singleMaximum, 134217728u);
    const auto lastRepresentable = LightSpaceShadowDrawCount(Span<const LightSpaceShadowCaster>(casters.data(), 31u));
    ASSERT_TRUE(lastRepresentable);
    EXPECT_EQ(*lastRepresentable, 4160749568u);
    EXPECT_FALSE(LightSpaceShadowDrawCount(casters));
}

TEST(LightSpaceShadowStorage, AcceptsExactGenerationAndRejectsEachInsufficientResource){
    const Array<LightSpaceLightRequest, 2> requests = {{ { 0u, 0u, Scene::LightType::Directional }, { 1u, 1u, Scene::LightType::Point } }};
    const Expected<LightSpacePlan> plan = TryBuildLightSpacePlan(SoftwareShadowSettings{}, requests, Limit<u32>::s_Max, 20u, false);
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
    const Array<LightSpaceLightRequest, 2> both = { directional, point };
    const Expected<LightSpacePlan> larger = TryBuildLightSpacePlan(SoftwareShadowSettings{}, both, Limit<u32>::s_Max, 20u, false);
    const Expected<LightSpacePlan> smaller = TryBuildLightSpacePlan(SoftwareShadowSettings{}, Span<const LightSpaceLightRequest>(&point, 1u), Limit<u32>::s_Max, 20u, false);
    ASSERT_TRUE(larger);
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

