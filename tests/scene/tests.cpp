// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/module.h>
#include <core/common/module.h>
#include <impl/ecs_scene/module.h>

#include <tests/ecs_test_world.h>
#include <tests/test_context.h>

#include <global/compile.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_SCENE_TEST_CHECK NWB_TEST_CHECK


using TestWorld = NWB::Tests::EcsTestWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestSceneAndMainCamera(TestContext& context){
    TestWorld testWorld;

    auto activeCameraEntity = testWorld.world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    NWB_SCENE_TEST_CHECK(context, activeCamera.camera == NWB::Core::ECS::ENTITY_ID_INVALID);

    auto cameraEntity = testWorld.world.createEntity();
    auto& transform = cameraEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    auto& camera = cameraEntity.addComponent<NWB::Impl::Scene::CameraComponent>();
    activeCamera.camera = cameraEntity.id();

    NWB_SCENE_TEST_CHECK(context, activeCameraEntity.hasComponent<NWB::Impl::Scene::ActiveCameraComponent>());
    NWB_SCENE_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Impl::Scene::TransformComponent>());
    NWB_SCENE_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Impl::Scene::CameraComponent>());
    NWB_SCENE_TEST_CHECK(context, activeCamera.camera == cameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, activeCamera.camera.valid());
    NWB_SCENE_TEST_CHECK(context, testWorld.world.entityCount() == 2);

    NWB_SCENE_TEST_CHECK(
        context,
        (reinterpret_cast<usize>(&transform) % alignof(NWB::Impl::Scene::TransformComponent)) == 0
    );
    NWB_SCENE_TEST_CHECK(context, (reinterpret_cast<usize>(&camera) % alignof(NWB::Impl::Scene::CameraComponent)) == 0);
    NWB_SCENE_TEST_CHECK(context, (reinterpret_cast<usize>(&camera.projection) % alignof(Float4)) == 0);
    NWB_SCENE_TEST_CHECK(context, transform.position.x == 0.0f);
    NWB_SCENE_TEST_CHECK(context, transform.position.y == 0.0f);
    NWB_SCENE_TEST_CHECK(context, transform.position.z == 0.0f);
    NWB_SCENE_TEST_CHECK(context, transform.rotation.x == 0.0f);
    NWB_SCENE_TEST_CHECK(context, transform.rotation.y == 0.0f);
    NWB_SCENE_TEST_CHECK(context, transform.rotation.z == 0.0f);
    NWB_SCENE_TEST_CHECK(context, transform.rotation.w == 1.0f);
    NWB_SCENE_TEST_CHECK(context, transform.scale.x == 1.0f);
    NWB_SCENE_TEST_CHECK(context, transform.scale.y == 1.0f);
    NWB_SCENE_TEST_CHECK(context, transform.scale.z == 1.0f);
    NWB_SCENE_TEST_CHECK(context, camera.verticalFovRadians() > 0.0f);
    NWB_SCENE_TEST_CHECK(context, camera.nearPlane() > 0.0f);
    NWB_SCENE_TEST_CHECK(context, camera.farPlane() > camera.nearPlane());
    NWB_SCENE_TEST_CHECK(context, camera.aspectRatio() == 0.0f);

    const NWB::Core::ECS::EntityID cameraEntityId = cameraEntity.id();
    usize cameraViewCount = 0;
    testWorld.world.view<
        NWB::Impl::Scene::TransformComponent,
        NWB::Impl::Scene::CameraComponent
    >().each(
        [&context, &cameraViewCount, cameraEntityId](
            NWB::Core::ECS::EntityID entityId,
            NWB::Impl::Scene::TransformComponent& viewTransform,
            NWB::Impl::Scene::CameraComponent& viewCamera
        ){
            ++cameraViewCount;
            NWB_SCENE_TEST_CHECK(context, entityId == cameraEntityId);
            NWB_SCENE_TEST_CHECK(context, viewTransform.rotation.w == 1.0f);
            NWB_SCENE_TEST_CHECK(context, viewCamera.aspectRatio() == 0.0f);
        }
    );
    NWB_SCENE_TEST_CHECK(context, cameraViewCount == 1);
}

static void TestSceneCameraResolution(TestContext& context){
    TestWorld testWorld;

    NWB::Impl::Scene::SceneCameraView emptyCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, !emptyCameraView.valid());

    auto firstCameraEntity = testWorld.world.createEntity();
    firstCameraEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    firstCameraEntity.addComponent<NWB::Impl::Scene::CameraComponent>();

    auto secondCameraEntity = testWorld.world.createEntity();
    secondCameraEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    secondCameraEntity.addComponent<NWB::Impl::Scene::CameraComponent>();

    auto* firstTransform = testWorld.world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(firstCameraEntity.id());
    auto* firstCamera = testWorld.world.tryGetComponent<NWB::Impl::Scene::CameraComponent>(firstCameraEntity.id());
    auto* secondTransform = testWorld.world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(secondCameraEntity.id());
    auto* secondCamera = testWorld.world.tryGetComponent<NWB::Impl::Scene::CameraComponent>(secondCameraEntity.id());

    NWB::Impl::Scene::SceneCameraView fallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.camera == firstCamera);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.projectionData.projectionParams.x > 0.0f);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.projectionData.tanHalfVerticalFov > 0.0f);

    auto activeCameraEntity = testWorld.world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    activeCamera.camera = secondCameraEntity.id();

    NWB::Impl::Scene::SceneCameraView requestedCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.entity == secondCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.transform == secondTransform);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.camera == secondCamera);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.projectionData.projectionParams.y > 0.0f);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.projectionData.aspectRatio == 1.0f);

    NWB::Impl::Scene::SceneCameraView invalidProjectionView = requestedCameraView;
    invalidProjectionView.projectionData = NWB::Impl::Scene::CameraProjectionData{};
    NWB_SCENE_TEST_CHECK(context, !invalidProjectionView.valid());

    secondCamera->setNearPlane(0.0f);
    NWB::Impl::Scene::SceneCameraView invalidRequestedFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.camera == firstCamera);
    secondCamera->setNearPlane(NWB::Impl::Scene::CameraComponent{}.nearPlane());

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    NWB::Impl::Scene::SceneCameraView invalidTransformFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.camera == firstCamera);
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    secondCamera->setAspectRatio(s_MaxF32);
    NWB::Impl::Scene::SceneCameraView invalidProjectionFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.camera == firstCamera);
    *secondCamera = NWB::Impl::Scene::CameraComponent{};

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB::Impl::Scene::SceneCameraView invalidFallbackAspectCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world, s_MaxF32);
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.camera == firstCamera);
    *secondCamera = NWB::Impl::Scene::CameraComponent{};

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Impl::Scene::SceneCameraView nonUnitTransformFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.camera == firstCamera);
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    f32 nonFiniteScale = s_MaxF32;
    nonFiniteScale *= 2.0f;
    secondTransform->scale = Float4(nonFiniteScale, 1.0f, 1.0f);
    NWB::Impl::Scene::SceneCameraView nonFiniteScaleFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.camera == firstCamera);
    secondTransform->scale = Float4(1.0f, 1.0f, 1.0f);

    auto staleMainCameraEntity = testWorld.world.createEntity();
    activeCamera.camera = staleMainCameraEntity.id();

    NWB::Impl::Scene::SceneCameraView staleFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.camera == firstCamera);

    firstTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Impl::Scene::SceneCameraView invalidFallbackSkippedCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, invalidFallbackSkippedCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidFallbackSkippedCameraView.entity == secondCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidFallbackSkippedCameraView.transform == secondTransform);
    NWB_SCENE_TEST_CHECK(context, invalidFallbackSkippedCameraView.camera == secondCamera);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_SCENE_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Scene, SceneAndMainCamera){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestSceneAndMainCamera(nwbTestContext);
}

TEST(Scene, SceneCameraResolution){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestSceneCameraResolution(nwbTestContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

