// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_scene/module.h>

#include <core/ecs/entity.h>

#include <tests/common/ecs_test_world.h>

#include <global/timer.h>

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

TEST(SceneCameraSelection, FirstSelectorAndLiveProjectionRemainAuthoritative){
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
    EXPECT_FLOAT_EQ(resolved.projection.aspectRatio, 1.25f);
    auto& camera = testWorld.world.entity(second).getComponent<CameraComponent>();
    camera.setNearPlane(0.5f);
    camera.setVerticalFovRadians(0.8f);
    resolved = ResolveSceneCameraView(testWorld.world, 2.0f);
    ASSERT_TRUE(resolved.valid());
    EXPECT_EQ(resolved.entity, second);
    EXPECT_EQ(resolved.camera, &camera);
    EXPECT_FLOAT_EQ(resolved.projection.nearPlane, 0.5f);
    EXPECT_FLOAT_EQ(resolved.projection.aspectRatio, 2.0f);
    EXPECT_NEAR(resolved.projection.tanHalfVerticalFov, 0.42279322f, 0.00001f);

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


void MeasureCameraSelection(const usize cameraCount, const bool useActiveCamera){
    TestWorld testWorld;
    EntityID firstCamera;
    EntityID lastCamera;
    for(usize index = 0u; index < cameraCount; ++index){
        lastCamera = CreateSceneCameraEntity(testWorld.world, Float4(static_cast<f32>(index), 0.0f, 0.0f));
        if(index == 0u)
            firstCamera = lastCamera;
    }
    if(useActiveCamera){
        auto selector = testWorld.world.createEntity();
        selector.addComponent<ActiveCameraComponent>().camera = lastCamera;
    }
    const EntityID expectedCamera = useActiveCamera ? lastCamera : firstCamera;
    const usize iterations = cameraCount == 1u ? 16384u : 1024u;
    constexpr usize s_SampleCount = 7u;
    Array<u64, s_SampleCount> samples{};
    const ArenaMemoryStats beforeHeap = HeapBackingMemoryStats();
    for(usize sampleIndex = 0u; sampleIndex <= s_SampleCount; ++sampleIndex){
        u64 checksum = 0u;
        bool valid = true;
        const Timer begin = TimerNow();
        for(usize iteration = 0u; iteration < iterations; ++iteration){
            const SceneCameraView resolved = ResolveSceneCameraView(testWorld.world, 1.5f);
            checksum += resolved.entity.index();
            valid &= resolved.entity == expectedCamera && resolved.projection.aspectRatio == 1.5f;
        }
        const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(valid);
        ASSERT_EQ(checksum, static_cast<u64>(expectedCamera.index()) * iterations);
        if(sampleIndex != 0u)
            samples[sampleIndex - 1u] = elapsed;
    }
    const ArenaMemoryStats afterHeap = HeapBackingMemoryStats();
    EXPECT_EQ(afterHeap.allocationCount, beforeHeap.allocationCount);
    Sort(samples.begin(), samples.end());
    char elapsedText[32u]{};
    testing::Test::RecordProperty("median_ns", FormatDecimal(samples[s_SampleCount / 2u], elapsedText).data());
    testing::Test::RecordProperty("camera_count", static_cast<int>(cameraCount));
    testing::Test::RecordProperty("iterations_per_sample", static_cast<int>(iterations));
    testing::Test::RecordProperty("measured_samples", static_cast<int>(s_SampleCount));
}

TEST(SceneCameraSelectionBenchmark, DISABLED_SingleActiveCamera){
    MeasureCameraSelection(1u, true);
}

TEST(SceneCameraSelectionBenchmark, DISABLED_64CamerasActiveLast){
    MeasureCameraSelection(64u, true);
}

TEST(SceneCameraSelectionBenchmark, DISABLED_1024CamerasActiveLast){
    MeasureCameraSelection(1024u, true);
}

TEST(SceneCameraSelectionBenchmark, DISABLED_1024CamerasNoActive){
    MeasureCameraSelection(1024u, false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

