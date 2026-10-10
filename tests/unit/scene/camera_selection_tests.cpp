// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_scene/module.h>

#include <core/ecs/entity.h>

#include <tests/common/ecs_test_world.h>


#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_camera_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl::Scene;
using NWB::Core::ECS::EntityID;
using TestWorld = NWB::Tests::EcsTestWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SceneCameraSelection, ActiveGenerationAndMissingComponentsRecover){
    TestWorld testWorld;
    const EntityID fallback = CreateSceneCameraEntity(testWorld.world, Float4(1.0f, 0.0f, 0.0f));
    const EntityID original = CreateSceneCameraEntity(testWorld.world, Float4(2.0f, 0.0f, 0.0f));
    auto selector = testWorld.world.createEntity();
    selector.addComponent<ActiveCameraComponent>().camera = original;
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, original);

    testWorld.world.destroyEntity(original);
    const EntityID replacement = CreateSceneCameraEntity(testWorld.world, Float4(3.0f, 0.0f, 0.0f));
    ASSERT_EQ(original.index(), replacement.index());
    ASSERT_NE(original.generation(), replacement.generation());
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, fallback);

    selector.getComponent<ActiveCameraComponent>().camera = replacement;
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, replacement);
    testWorld.world.entity(replacement).removeComponent<TransformComponent>();
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, fallback);
    testWorld.world.entity(replacement).addComponent<TransformComponent>();
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, replacement);
    testWorld.world.entity(replacement).removeComponent<CameraComponent>();
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, fallback);
    testWorld.world.entity(replacement).addComponent<CameraComponent>();
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, replacement);
}

TEST(SceneCameraSelection, FirstSelectorRemainsAuthoritativeAcrossRemovalAndRebinding){
    TestWorld testWorld;
    const EntityID first = CreateSceneCameraEntity(testWorld.world, Float4(1.0f, 0.0f, 0.0f));
    const EntityID second = CreateSceneCameraEntity(testWorld.world, Float4(2.0f, 0.0f, 0.0f));
    auto firstSelector = testWorld.world.createEntity();
    firstSelector.addComponent<ActiveCameraComponent>().camera = second;
    auto secondSelector = testWorld.world.createEntity();
    secondSelector.addComponent<ActiveCameraComponent>().camera = first;

    SceneCameraView resolved = ResolveSceneCameraView(testWorld.world, 1.25f);
    ASSERT_TRUE(resolved.valid());
    EXPECT_EQ(resolved.entity, second);
    firstSelector.removeComponent<ActiveCameraComponent>();
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, first);
    secondSelector.getComponent<ActiveCameraComponent>().camera = second;
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, second);
}

TEST(SceneCameraSelection, FallbackPreservesSparseViewOrderAfterRemoval){
    TestWorld testWorld;
    const EntityID first = CreateSceneCameraEntity(testWorld.world, Float4(1.0f, 0.0f, 0.0f));
    const EntityID second = CreateSceneCameraEntity(testWorld.world, Float4(2.0f, 0.0f, 0.0f));
    const EntityID third = CreateSceneCameraEntity(testWorld.world, Float4(3.0f, 0.0f, 0.0f));
    testWorld.world.entity(first).removeComponent<CameraComponent>();
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, third);

    auto selector = testWorld.world.createEntity();
    selector.addComponent<ActiveCameraComponent>().camera = first;
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, third);
    auto& thirdCamera = testWorld.world.entity(third).getComponent<CameraComponent>();
    thirdCamera.setNearPlane(0.0f);
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, second);
    testWorld.world.entity(second).getComponent<TransformComponent>().rotation = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(ResolveSceneCameraView(testWorld.world).valid());
    thirdCamera.setNearPlane(0.25f);
    EXPECT_EQ(ResolveSceneCameraView(testWorld.world).entity, third);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

