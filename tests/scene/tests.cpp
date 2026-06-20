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


using TestWorld = NWB::Tests::EcsTestWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestSceneAndMainCamera(){
    TestWorld testWorld;

    auto activeCameraEntity = testWorld.world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    EXPECT_TRUE((activeCamera.camera == NWB::Core::ECS::ENTITY_ID_INVALID));

    auto cameraEntity = testWorld.world.createEntity();
    auto& transform = cameraEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    auto& camera = cameraEntity.addComponent<NWB::Impl::Scene::CameraComponent>();
    activeCamera.camera = cameraEntity.id();

    EXPECT_TRUE((activeCameraEntity.hasComponent<NWB::Impl::Scene::ActiveCameraComponent>()));
    EXPECT_TRUE((cameraEntity.hasComponent<NWB::Impl::Scene::TransformComponent>()));
    EXPECT_TRUE((cameraEntity.hasComponent<NWB::Impl::Scene::CameraComponent>()));
    EXPECT_TRUE((activeCamera.camera == cameraEntity.id()));
    EXPECT_TRUE((activeCamera.camera.valid()));
    EXPECT_TRUE((testWorld.world.entityCount() == 2));

    EXPECT_TRUE(((reinterpret_cast<usize>(&transform) % alignof(NWB::Impl::Scene::TransformComponent)) == 0));
    EXPECT_TRUE(((reinterpret_cast<usize>(&camera) % alignof(NWB::Impl::Scene::CameraComponent)) == 0));
    EXPECT_TRUE(((reinterpret_cast<usize>(&camera.projection) % alignof(Float4)) == 0));
    EXPECT_TRUE((transform.position.x == 0.0f));
    EXPECT_TRUE((transform.position.y == 0.0f));
    EXPECT_TRUE((transform.position.z == 0.0f));
    EXPECT_TRUE((transform.rotation.x == 0.0f));
    EXPECT_TRUE((transform.rotation.y == 0.0f));
    EXPECT_TRUE((transform.rotation.z == 0.0f));
    EXPECT_TRUE((transform.rotation.w == 1.0f));
    EXPECT_TRUE((transform.scale.x == 1.0f));
    EXPECT_TRUE((transform.scale.y == 1.0f));
    EXPECT_TRUE((transform.scale.z == 1.0f));
    EXPECT_TRUE((camera.verticalFovRadians() > 0.0f));
    EXPECT_TRUE((camera.nearPlane() > 0.0f));
    EXPECT_TRUE((camera.farPlane() > camera.nearPlane()));
    EXPECT_TRUE((camera.aspectRatio() == 0.0f));

    const NWB::Core::ECS::EntityID cameraEntityId = cameraEntity.id();
    usize cameraViewCount = 0;
    testWorld.world.view<
        NWB::Impl::Scene::TransformComponent,
        NWB::Impl::Scene::CameraComponent
    >().each(
        [&cameraViewCount, cameraEntityId](
            NWB::Core::ECS::EntityID entityId,
            NWB::Impl::Scene::TransformComponent& viewTransform,
            NWB::Impl::Scene::CameraComponent& viewCamera
        ){
            ++cameraViewCount;
            EXPECT_TRUE((entityId == cameraEntityId));
            EXPECT_TRUE((viewTransform.rotation.w == 1.0f));
            EXPECT_TRUE((viewCamera.aspectRatio() == 0.0f));
        }
    );
    EXPECT_TRUE((cameraViewCount == 1));
}

static void TestSceneCameraResolution(){
    TestWorld testWorld;

    NWB::Impl::Scene::SceneCameraView emptyCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((!emptyCameraView.valid()));

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
    EXPECT_TRUE((fallbackCameraView.valid()));
    EXPECT_TRUE((fallbackCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((fallbackCameraView.transform == firstTransform));
    EXPECT_TRUE((fallbackCameraView.camera == firstCamera));
    EXPECT_TRUE((fallbackCameraView.projectionData.projectionParams.x > 0.0f));
    EXPECT_TRUE((fallbackCameraView.projectionData.tanHalfVerticalFov > 0.0f));

    auto activeCameraEntity = testWorld.world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    activeCamera.camera = secondCameraEntity.id();

    NWB::Impl::Scene::SceneCameraView requestedCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((requestedCameraView.valid()));
    EXPECT_TRUE((requestedCameraView.entity == secondCameraEntity.id()));
    EXPECT_TRUE((requestedCameraView.transform == secondTransform));
    EXPECT_TRUE((requestedCameraView.camera == secondCamera));
    EXPECT_TRUE((requestedCameraView.projectionData.projectionParams.y > 0.0f));
    EXPECT_TRUE((requestedCameraView.projectionData.aspectRatio == 1.0f));

    NWB::Impl::Scene::SceneCameraView invalidProjectionView = requestedCameraView;
    invalidProjectionView.projectionData = NWB::Impl::Scene::CameraProjectionData{};
    EXPECT_TRUE((!invalidProjectionView.valid()));

    secondCamera->setNearPlane(0.0f);
    NWB::Impl::Scene::SceneCameraView invalidRequestedFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((invalidRequestedFallbackCameraView.valid()));
    EXPECT_TRUE((invalidRequestedFallbackCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((invalidRequestedFallbackCameraView.transform == firstTransform));
    EXPECT_TRUE((invalidRequestedFallbackCameraView.camera == firstCamera));
    secondCamera->setNearPlane(NWB::Impl::Scene::CameraComponent{}.nearPlane());

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    NWB::Impl::Scene::SceneCameraView invalidTransformFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((invalidTransformFallbackCameraView.valid()));
    EXPECT_TRUE((invalidTransformFallbackCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((invalidTransformFallbackCameraView.transform == firstTransform));
    EXPECT_TRUE((invalidTransformFallbackCameraView.camera == firstCamera));
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    secondCamera->setAspectRatio(s_MaxF32);
    NWB::Impl::Scene::SceneCameraView invalidProjectionFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((invalidProjectionFallbackCameraView.valid()));
    EXPECT_TRUE((invalidProjectionFallbackCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((invalidProjectionFallbackCameraView.transform == firstTransform));
    EXPECT_TRUE((invalidProjectionFallbackCameraView.camera == firstCamera));
    *secondCamera = NWB::Impl::Scene::CameraComponent{};

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB::Impl::Scene::SceneCameraView invalidFallbackAspectCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world, s_MaxF32);
    EXPECT_TRUE((invalidFallbackAspectCameraView.valid()));
    EXPECT_TRUE((invalidFallbackAspectCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((invalidFallbackAspectCameraView.transform == firstTransform));
    EXPECT_TRUE((invalidFallbackAspectCameraView.camera == firstCamera));
    *secondCamera = NWB::Impl::Scene::CameraComponent{};

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Impl::Scene::SceneCameraView nonUnitTransformFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((nonUnitTransformFallbackCameraView.valid()));
    EXPECT_TRUE((nonUnitTransformFallbackCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((nonUnitTransformFallbackCameraView.transform == firstTransform));
    EXPECT_TRUE((nonUnitTransformFallbackCameraView.camera == firstCamera));
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    f32 nonFiniteScale = s_MaxF32;
    nonFiniteScale *= 2.0f;
    secondTransform->scale = Float4(nonFiniteScale, 1.0f, 1.0f);
    NWB::Impl::Scene::SceneCameraView nonFiniteScaleFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((nonFiniteScaleFallbackCameraView.valid()));
    EXPECT_TRUE((nonFiniteScaleFallbackCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((nonFiniteScaleFallbackCameraView.transform == firstTransform));
    EXPECT_TRUE((nonFiniteScaleFallbackCameraView.camera == firstCamera));
    secondTransform->scale = Float4(1.0f, 1.0f, 1.0f);

    auto staleMainCameraEntity = testWorld.world.createEntity();
    activeCamera.camera = staleMainCameraEntity.id();

    NWB::Impl::Scene::SceneCameraView staleFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((staleFallbackCameraView.valid()));
    EXPECT_TRUE((staleFallbackCameraView.entity == firstCameraEntity.id()));
    EXPECT_TRUE((staleFallbackCameraView.transform == firstTransform));
    EXPECT_TRUE((staleFallbackCameraView.camera == firstCamera));

    firstTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Impl::Scene::SceneCameraView invalidFallbackSkippedCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE((invalidFallbackSkippedCameraView.valid()));
    EXPECT_TRUE((invalidFallbackSkippedCameraView.entity == secondCameraEntity.id()));
    EXPECT_TRUE((invalidFallbackSkippedCameraView.transform == secondTransform));
    EXPECT_TRUE((invalidFallbackSkippedCameraView.camera == secondCamera));
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Scene, SceneAndMainCamera){
    __hidden_tests::TestSceneAndMainCamera();
}

TEST(Scene, SceneCameraResolution){
    __hidden_tests::TestSceneCameraResolution();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

