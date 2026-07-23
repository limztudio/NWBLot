// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/module.h>
#include <core/common/module.h>
#include <impl/ecs_scene/module.h>

#include <tests/ecs_test_world.h>

#include <global/compile.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Scene, LightComponents){
    TestWorld testWorld;

    auto directionalEntity = testWorld.world.createEntity();
    auto& directionalTransform = directionalEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    auto& directionalLight = directionalEntity.addComponent<NWB::Impl::Scene::LightComponent>();

    EXPECT_TRUE(directionalEntity.hasComponent<NWB::Impl::Scene::TransformComponent>());
    EXPECT_TRUE(directionalEntity.hasComponent<NWB::Impl::Scene::LightComponent>());
    EXPECT_EQ(directionalLight.type, NWB::Impl::Scene::LightType::Directional);
    EXPECT_EQ(directionalLight.color().x, 1.0f);
    EXPECT_EQ(directionalLight.color().y, 1.0f);
    EXPECT_EQ(directionalLight.color().z, 1.0f);
    EXPECT_GT(directionalLight.intensity(), 0.0f);
    EXPECT_GT(directionalLight.range, 0.0f);
    EXPECT_EQ(directionalTransform.rotation.w, 1.0f);

    auto pointEntity = testWorld.world.createEntity();
    auto& pointTransform = pointEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    auto& pointLight = pointEntity.addComponent<NWB::Impl::Scene::LightComponent>();
    pointTransform.position = Float4(1.0f, 2.0f, 3.0f);
    pointLight.type = NWB::Impl::Scene::LightType::Point;
    pointLight.setColor(Float4(1.0f, 0.75f, 0.5f));
    pointLight.setIntensity(4.0f);
    pointLight.range = 12.0f;

    EXPECT_TRUE(pointEntity.hasComponent<NWB::Impl::Scene::TransformComponent>());
    EXPECT_TRUE(pointEntity.hasComponent<NWB::Impl::Scene::LightComponent>());
    EXPECT_EQ(pointLight.type, NWB::Impl::Scene::LightType::Point);
    EXPECT_EQ(pointLight.color().x, 1.0f);
    EXPECT_EQ(pointLight.color().y, 0.75f);
    EXPECT_EQ(pointLight.color().z, 0.5f);
    EXPECT_EQ(pointLight.intensity(), 4.0f);
    EXPECT_EQ(pointLight.range, 12.0f);

    EXPECT_EQ((reinterpret_cast<usize>(&directionalLight) % alignof(NWB::Impl::Scene::LightComponent)), 0u);
    EXPECT_EQ((reinterpret_cast<usize>(&pointLight) % alignof(NWB::Impl::Scene::LightComponent)), 0u);

    const NWB::Core::ECS::EntityID pointEntityId = pointEntity.id();
    usize lightViewCount = 0;
    usize directionalLightCount = 0;
    usize pointLightCount = 0;
    testWorld.world.view<
        NWB::Impl::Scene::TransformComponent,
        NWB::Impl::Scene::LightComponent
    >().each(
        [&lightViewCount, &directionalLightCount, &pointLightCount, pointEntityId](
            NWB::Core::ECS::EntityID entityId,
            NWB::Impl::Scene::TransformComponent& viewTransform,
            NWB::Impl::Scene::LightComponent& viewLight
        ){
            ++lightViewCount;
            if(viewLight.type == NWB::Impl::Scene::LightType::Directional){
                ++directionalLightCount;
                EXPECT_GT(viewLight.intensity(), 0.0f);
            }
            else if(viewLight.type == NWB::Impl::Scene::LightType::Point){
                ++pointLightCount;
                EXPECT_EQ(entityId, pointEntityId);
                EXPECT_EQ(viewTransform.position.x, 1.0f);
                EXPECT_GT(viewLight.range, 0.0f);
            }
            else{
                ADD_FAILURE();
            }
        }
    );
    EXPECT_EQ(lightViewCount, 2u);
    EXPECT_EQ(directionalLightCount, 1u);
    EXPECT_EQ(pointLightCount, 1u);
}


TEST(Scene, SceneAndMainCamera){
    TestWorld testWorld;

    auto activeCameraEntity = testWorld.world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    EXPECT_EQ(activeCamera.camera, NWB::Core::ECS::ENTITY_ID_INVALID);

    auto cameraEntity = testWorld.world.createEntity();
    auto& transform = cameraEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    auto& camera = cameraEntity.addComponent<NWB::Impl::Scene::CameraComponent>();
    activeCamera.camera = cameraEntity.id();

    EXPECT_TRUE(activeCameraEntity.hasComponent<NWB::Impl::Scene::ActiveCameraComponent>());
    EXPECT_TRUE(cameraEntity.hasComponent<NWB::Impl::Scene::TransformComponent>());
    EXPECT_TRUE(cameraEntity.hasComponent<NWB::Impl::Scene::CameraComponent>());
    EXPECT_EQ(activeCamera.camera, cameraEntity.id());
    EXPECT_TRUE(activeCamera.camera.valid());
    EXPECT_EQ(testWorld.world.entityCount(), 2u);

    EXPECT_EQ((reinterpret_cast<usize>(&transform) % alignof(NWB::Impl::Scene::TransformComponent)), 0u);
    EXPECT_EQ((reinterpret_cast<usize>(&camera) % alignof(NWB::Impl::Scene::CameraComponent)), 0u);
    EXPECT_EQ((reinterpret_cast<usize>(&camera.projection) % alignof(Float4)), 0u);
    EXPECT_EQ(transform.position.x, 0.0f);
    EXPECT_EQ(transform.position.y, 0.0f);
    EXPECT_EQ(transform.position.z, 0.0f);
    EXPECT_EQ(transform.rotation.x, 0.0f);
    EXPECT_EQ(transform.rotation.y, 0.0f);
    EXPECT_EQ(transform.rotation.z, 0.0f);
    EXPECT_EQ(transform.rotation.w, 1.0f);
    EXPECT_EQ(transform.scale.x, 1.0f);
    EXPECT_EQ(transform.scale.y, 1.0f);
    EXPECT_EQ(transform.scale.z, 1.0f);
    EXPECT_GT(camera.verticalFovRadians(), 0.0f);
    EXPECT_GT(camera.nearPlane(), 0.0f);
    EXPECT_GT(camera.farPlane(), camera.nearPlane());
    EXPECT_EQ(camera.aspectRatio(), 0.0f);

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
            EXPECT_EQ(entityId, cameraEntityId);
            EXPECT_EQ(viewTransform.rotation.w, 1.0f);
            EXPECT_EQ(viewCamera.aspectRatio(), 0.0f);
        }
    );
    EXPECT_EQ(cameraViewCount, 1u);
}

TEST(Scene, SceneCameraResolution){
    TestWorld testWorld;

    NWB::Impl::Scene::SceneCameraView emptyCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_FALSE(emptyCameraView.valid());

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
    EXPECT_TRUE(fallbackCameraView.valid());
    EXPECT_EQ(fallbackCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(fallbackCameraView.transform, firstTransform);
    EXPECT_EQ(fallbackCameraView.camera, firstCamera);
    EXPECT_GT(fallbackCameraView.projectionData.projectionParams.x, 0.0f);
    EXPECT_GT(fallbackCameraView.projectionData.tanHalfVerticalFov, 0.0f);

    auto activeCameraEntity = testWorld.world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    activeCamera.camera = secondCameraEntity.id();

    NWB::Impl::Scene::SceneCameraView requestedCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(requestedCameraView.valid());
    EXPECT_EQ(requestedCameraView.entity, secondCameraEntity.id());
    EXPECT_EQ(requestedCameraView.transform, secondTransform);
    EXPECT_EQ(requestedCameraView.camera, secondCamera);
    EXPECT_GT(requestedCameraView.projectionData.projectionParams.y, 0.0f);
    EXPECT_EQ(requestedCameraView.projectionData.aspectRatio, 1.0f);

    NWB::Impl::Scene::SceneCameraView invalidProjectionView = requestedCameraView;
    invalidProjectionView.projectionData = NWB::Impl::Scene::CameraProjectionData{};
    EXPECT_FALSE(invalidProjectionView.valid());

    secondCamera->setNearPlane(0.0f);
    NWB::Impl::Scene::SceneCameraView invalidRequestedFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(invalidRequestedFallbackCameraView.valid());
    EXPECT_EQ(invalidRequestedFallbackCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(invalidRequestedFallbackCameraView.transform, firstTransform);
    EXPECT_EQ(invalidRequestedFallbackCameraView.camera, firstCamera);
    secondCamera->setNearPlane(NWB::Impl::Scene::CameraComponent{}.nearPlane());

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    NWB::Impl::Scene::SceneCameraView invalidTransformFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(invalidTransformFallbackCameraView.valid());
    EXPECT_EQ(invalidTransformFallbackCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(invalidTransformFallbackCameraView.transform, firstTransform);
    EXPECT_EQ(invalidTransformFallbackCameraView.camera, firstCamera);
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    secondCamera->setAspectRatio(s_MaxF32);
    NWB::Impl::Scene::SceneCameraView invalidProjectionFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(invalidProjectionFallbackCameraView.valid());
    EXPECT_EQ(invalidProjectionFallbackCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(invalidProjectionFallbackCameraView.transform, firstTransform);
    EXPECT_EQ(invalidProjectionFallbackCameraView.camera, firstCamera);
    *secondCamera = NWB::Impl::Scene::CameraComponent{};

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB::Impl::Scene::SceneCameraView invalidFallbackAspectCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world, s_MaxF32);
    EXPECT_TRUE(invalidFallbackAspectCameraView.valid());
    EXPECT_EQ(invalidFallbackAspectCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(invalidFallbackAspectCameraView.transform, firstTransform);
    EXPECT_EQ(invalidFallbackAspectCameraView.camera, firstCamera);
    *secondCamera = NWB::Impl::Scene::CameraComponent{};

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Impl::Scene::SceneCameraView nonUnitTransformFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(nonUnitTransformFallbackCameraView.valid());
    EXPECT_EQ(nonUnitTransformFallbackCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(nonUnitTransformFallbackCameraView.transform, firstTransform);
    EXPECT_EQ(nonUnitTransformFallbackCameraView.camera, firstCamera);
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    f32 nonFiniteScale = s_MaxF32;
    nonFiniteScale *= 2.0f;
    secondTransform->scale = Float4(nonFiniteScale, 1.0f, 1.0f);
    NWB::Impl::Scene::SceneCameraView nonFiniteScaleFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(nonFiniteScaleFallbackCameraView.valid());
    EXPECT_EQ(nonFiniteScaleFallbackCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(nonFiniteScaleFallbackCameraView.transform, firstTransform);
    EXPECT_EQ(nonFiniteScaleFallbackCameraView.camera, firstCamera);
    secondTransform->scale = Float4(1.0f, 1.0f, 1.0f);

    auto staleMainCameraEntity = testWorld.world.createEntity();
    activeCamera.camera = staleMainCameraEntity.id();

    NWB::Impl::Scene::SceneCameraView staleFallbackCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(staleFallbackCameraView.valid());
    EXPECT_EQ(staleFallbackCameraView.entity, firstCameraEntity.id());
    EXPECT_EQ(staleFallbackCameraView.transform, firstTransform);
    EXPECT_EQ(staleFallbackCameraView.camera, firstCamera);

    firstTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Impl::Scene::SceneCameraView invalidFallbackSkippedCameraView = NWB::Impl::Scene::ResolveSceneCameraView(testWorld.world);
    EXPECT_TRUE(invalidFallbackSkippedCameraView.valid());
    EXPECT_EQ(invalidFallbackSkippedCameraView.entity, secondCameraEntity.id());
    EXPECT_EQ(invalidFallbackSkippedCameraView.transform, secondTransform);
    EXPECT_EQ(invalidFallbackSkippedCameraView.camera, secondCamera);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

